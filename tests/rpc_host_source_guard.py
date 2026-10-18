#!/usr/bin/env python3
"""R3d-3 source boundaries and explicit negative mutations. GPL-2.0-only."""
from pathlib import Path
import hashlib,re,subprocess,sys
root=Path(sys.argv[1]);cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def require(ok,text):
    if not ok:raise RuntimeError(text)
def block(text,token):
    start=text.index(token);brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
def validate(files):
    prep=files['rpc_preparation.c'];front=files['arcdroid_libretro.c'];host=files['rpc_preparation_host.c']
    frozen=block(prep,'    if(p->state==ARC_RPC_PREP_RUNNING){')
    require(hashlib.sha256(frozen.encode()).hexdigest()=='dfa5174d2f8de0a67c85133991067b05e65c436284accf0a74ceb146cddd4fc5','frozen merged RUNNING body changed')
    require(not re.search(r'\barc_io_(?:remove|rename|create_file|truncate_file)\s*\(',prep),'preparation bypasses a token-bound store')
    reset=block(front,'RETRO_API void retro_reset(void)');route=block(reset,'   if(arc_rpc_preparation_route)')
    require('arc_riscpc_host_reset(' in route and 'arc_engine_reset(' not in route,'preparation reset bypasses host')
    require(route.rstrip().endswith('return;\n   }'),'preparation reset falls through')
    for name in ('arc_disk_set_eject','arc_disk_set_index','arc_disk_replace','arc_disk_add'):
        body=block(front,'static bool '+name+'(').split('{',1)[1].lstrip()
        require(body.startswith('if (arc_disk_rpc_pending()) return false;'),name+': side effect before admission')
    stopped=block(front,'static bool arc_rpc_stopped_callback(')
    require('arc_rpc_restage_media(false,' in stopped and not re.search(r'\barc_engine_\w+\s*\(',stopped),'stopped callback enters engine')
    restage=block(front,'static bool arc_rpc_restage_media(')
    require('arc_engine_rpc_eject(1)' not in restage and 'arc_engine_rpc_mount(1' not in restage,'drive 1 restaged by host')
    require('arc_riscpc_preparation_replace_media(' in restage,'stale drive-0 view')
    require('arc_riscpc_preparation_helper_write_policy(' in restage,'stale explicit-helper write policy')
    reset_module=block(prep,'bool arc_riscpc_preparation_reset(')
    require(reset_module.index('release(p);')<reset_module.index('restage_stopped')<reset_module.index('restart(p);'),'non-RUNNING reset reordered')
    require('if(!staged)' in reset_module and 'failed(p,' in reset_module.split('if(!staged)',1)[1].split('restart(p);')[0],'stopped failure left runnable')
    host_reset=block(host,'bool arc_riscpc_host_reset(')
    require(host_reset.index('arc_riscpc_preparation_complete_publication(')<host_reset.index('arc_riscpc_preparation_reset('),'reset lost in-flight completion')
    load_failure=block(host,'void arc_riscpc_host_load_failure(')
    require('static ArcRpcFrontendHost h;' in load_failure and load_failure.rstrip().endswith('memset(&h,0,sizeof(h));\n}'),'load failure puts histogram on stack or retains borrowed callbacks')
    message=block(host,'static void display(')
    require(message.count('RETRO_MESSAGE_TARGET_OSD')==1 and 'RETRO_MESSAGE_TARGET_ALL' not in host,'host message target')
    require('severity==ARC_RPC_NOTICE_CLEAR?3u' in message,'host clear priority')
    require('arc_test_plan_cmos_phase' not in host and 'strstr(' not in host,'notice severity inferred from text or test phase')
    progress=block(host,'static void progress(')
    require('!h->advisory_frames' in progress,'progress overwrites advisory')
    require('notice(p,"",false)' not in block(prep,'static void failed('),'pre-terminal clear')
    require('ARC_AUDIO_RATE/arc_video_rate' in block(front,'static void arc_rpc_pending_present('),'fixed pending audio count')
    release=block(prep,'static void release(')
    require('arc_lock_release(&p->content_owner)' not in release,'content owner released on Restart')
    require('p->hd_close_ok=arc_riscpc_hd_close' in release,'HD close result lost')
    unit=block(prep,'static void unit(')
    require(unit.index('acquire_owner(p,false)')<unit.index('arc_riscpc_hd_open_begin('),'late content lock')
    require('&p->defaults_owner,p->error' in unit,'defaults mutation uses content owner')
    require('arc_lock_release(&p->defaults_owner)' in unit,'defaults owner lifetime widened')
    fault=block(prep,'static void fault_failed(')
    require(fault.index('snapshot_cmos(')<fault.index('failed(p,'),'fault snapshot dropped or after stop')
    require(prep.count('fault_failed(p,')==2,'one maintenance stop omits snapshot/drain')
    drain=block(prep,'void arc_riscpc_preparation_failed_frame(')
    require('arc_riscpc_cmos_store_put(' not in drain and 'arc_riscpc_cmos_store_put_step(' in drain,'synchronous drain')
    require('arc_riscpc_preparation_failed_frame' not in block(prep,'void arc_riscpc_preparation_maintenance('),'drain driven by maintenance')
    free=block(prep,'bool arc_riscpc_preparation_free(')
    require(free.index('snapshot_cmos(')<free.index('release(p);'),'free snapshots after stop')
    require('arc_riscpc_cmos_store_mark_unsaved(' in free,'publication-only marker omitted')
    require('release(p);release(p);' not in prep,'double release')
    frontend_release=block(front,'static void arc_release_session(')
    require(frontend_release.index('arc_riscpc_host_free(')<frontend_release.index('arc_engine_stop();'),'host freed after engine stop')
    safe=block(front,'static void arc_rpc_safe_error(')
    literals=re.findall(r'"([^"\n]*)"',safe[safe.index('allowed[]='):safe.index('    if(!out')])
    require(len(literals)==15 and all('%' not in text for text in literals),'literal allow-list changed')
    producers=front.replace(safe,'')+'\n'+'\n'.join(text for name,text in files.items() if name.startswith('core/'))
    for text in [*literals,'Staged JFD digest mismatch.']:
        require('"'+text+'"' in producers,'allow-listed cause is no longer a source literal: '+text)
    for token in ('static bool arc_rpc_stage_safe(', 'static bool arc_rpc_restage_callback(', 'static bool arc_rpc_stopped_callback('):
        require('arc_rpc_safe_error(' in block(front,token),'media callback passes private error buffer')
    named=block(files['rpc_boot.c'],'static bool named_failure(')
    require('a file in the boot archives' in named and 'entry->name' not in named,'boot member leaks to error buffer')
    require('RETRO_LOG_DEBUG' not in host and 'RETRO_LOG_DEBUG' not in prep,'private DEBUG catalogue echo')
    for name in ('rpc_hd_store.c','rpc_cmos_store.c','rpc_launch_record_store.c'):
        owner=block(files[name],'static bool owned(')
        compact=re.sub(r'\s+','',owner)
        require('&&arc_lock_held(s->owner)&&arc_lock_verify(s->owner)==ARC_LOCK_HELD;' in compact,'store accepts absent/lost ownership: '+name)
files={p.name:p.read_text() for p in (root/'libretro').glob('*.c')}
files.update({'core/'+p.name:p.read_text() for p in (root/'core').glob('*.c')})
validate(files)
args=[cc,'-E','-P','-I'+str(root/'libretro'),'-I'+str(root/'libretro/include'),'-I'+str(root/'engine'),'-I'+str(root/'core'),str(root/'libretro/rpc_preparation_host.c')]
shipping=subprocess.check_output(args,text=True)
require('arc_riscpc_host_low_level' not in shipping and 'ArcRpcFrontendExtension' not in shipping,'shipping includes test entry/table')
mutants=[
 ('setter-before-admission','arcdroid_libretro.c','if (arc_disk_rpc_pending()) return false;','arc_disc_owned=false; if (arc_disk_rpc_pending()) return false;'),
 ('old-running-reset','arcdroid_libretro.c','(void)arc_riscpc_host_reset(arc_rpc_host);','arc_engine_reset(NULL,0);'),
 ('drive1-restage','arcdroid_libretro.c','static bool arc_rpc_restage_media(bool running,char *error,size_t size)\n{','static bool arc_rpc_restage_media(bool running,char *error,size_t size)\n{ arc_engine_rpc_eject(1);'),
 ('stale-view','arcdroid_libretro.c','arc_riscpc_preparation_replace_media(','removed_replace_media('),
 ('stopped-helper-policy','arcdroid_libretro.c','arc_riscpc_preparation_helper_write_policy(','removed_helper_policy('),
 ('engine-inside-stopped','arcdroid_libretro.c','arc_rpc_restage_media(false,','arc_engine_media_insert(NULL,NULL,0);arc_rpc_restage_media(false,'),
 ('reordered-stopped-restage','rpc_preparation.c','    release(p);\n    memset(&p->media','    /* release omitted before restage */\n    memset(&p->media'),
 ('restage-failure-runnable','rpc_preparation.c','if(!staged){','if(staged){'),
 ('missing-inflight-completion','rpc_preparation_host.c','arc_riscpc_preparation_complete_publication(','removed_complete('),
 ('phase-inferred-severity','rpc_preparation_host.c','if(event.severity==ARC_RPC_NOTICE_PROGRESS)','if(arc_test_plan_cmos_phase()==1)'),
 ('clear-priority-two','rpc_preparation_host.c','severity==ARC_RPC_NOTICE_CLEAR?3u','severity==ARC_RPC_NOTICE_CLEAR?2u'),
 ('preterminal-clear','rpc_preparation.c','    notice(p,p->error,true);','    notice(p,"",false);notice(p,p->error,true);'),
 ('advisory-overwritten','rpc_preparation_host.c','&& !h->advisory_frames','&& true'),
 ('target-all','rpc_preparation_host.c','RETRO_MESSAGE_TARGET_OSD','RETRO_MESSAGE_TARGET_ALL'),
 ('member-fallback','rpc_boot.c','a file in the boot archives','unredacted archive member'),
 ('debug-private-member','rpc_preparation_host.c','void arc_riscpc_host_clear(','/* RETRO_LOG_DEBUG catalogue echo */\nvoid arc_riscpc_host_clear('),
 ('fixed-audio-800','arcdroid_libretro.c','ARC_AUDIO_RATE/arc_video_rate','800.0'),
 ('load-stack-histogram','rpc_preparation_host.c','static ArcRpcFrontendHost h;','ArcRpcFrontendHost h;'),
 ('owner-released-on-reset','rpc_preparation.c','    if(p->released)return;','    arc_lock_release(&p->content_owner);if(p->released)return;'),
 ('late-content-owner','rpc_preparation.c','acquire_owner(p,false)','removed_acquire(p,false)'),
 ('hd-close-discarded','rpc_preparation.c','p->hd_close_ok=arc_riscpc_hd_close','(void)arc_riscpc_hd_close'),
 ('snapshot-after-stop','rpc_preparation.c','snapshot_cmos(p->drain_payload);','failed(p,cause);snapshot_cmos(p->drain_payload);'),
 ('drain-synchronous','rpc_preparation.c','ok=arc_riscpc_cmos_store_put_step(&p->record,&done,error,sizeof(error));','ok=arc_riscpc_cmos_store_put(&p->record,p->drain_payload,NULL,error,sizeof(error));'),
 ('drain-through-maintenance','rpc_preparation.c','    ++p->stats.identity_checks;','    arc_riscpc_preparation_failed_frame(p);++p->stats.identity_checks;'),
 ('marker-skipped','rpc_preparation.c','arc_riscpc_cmos_store_mark_unsaved(','removed_mark_unsaved('),
 ('defaults-after-release','rpc_preparation.c','&p->defaults_owner,p->error','&p->content_owner,p->error'),
 ('free-after-stop','arcdroid_libretro.c','if(arc_rpc_host){char error','arc_engine_stop();if(arc_rpc_host){char error'),
 ('double-release','rpc_preparation.c','    release(p);\n    if(!p->hd_close_ok)','    release(p);release(p);\n    if(!p->hd_close_ok)'),
 ('snapshot-dropped','rpc_preparation.c','snapshot_cmos(p->drain_payload);','(void)0;'),
 ('observation-snapshot-dropped','rpc_preparation.c','fault_failed(p,"RiscPC boot observation invalidation','failed(p,"RiscPC boot observation invalidation'),
 ('private-media-buffer','arcdroid_libretro.c','arc_rpc_safe_error(error,n,local,true)','snprintf(error,n,"%s",local)'),
]
for name in ('rpc_hd_store.c','rpc_cmos_store.c','rpc_launch_record_store.c'):
    mutants.append(('unheld-'+name,name,'arc_lock_held(s->owner) &&','true &&'))
    mutants.append(('unheld-compact-'+name,name,'arc_lock_held(s->owner)&&','true&&'))
killed=0
for label,name,old,new in mutants:
    if old not in files[name]:
        require(label.startswith('unheld-'),'mutation anchor moved: '+label);continue
    variant=files.copy();variant[name]=variant[name].replace(old,new,1)
    try:validate(variant)
    except (RuntimeError,ValueError):killed+=1
    else:raise RuntimeError('source-boundary mutant survived: '+label)
print(f'R3d-3 source boundaries: frozen reset, host/store ownership, OSD, privacy, drain and cleanup; {killed} explicit negative mutations rejected')

#!/usr/bin/env python3
"""R3c-2 policy mutants: successful rebuilds must fail observable tests. GPL-2.0-only."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from rpc_ninja_commands import rebuild_commands
sys.dont_write_bytecode = True
root=Path(__file__).resolve().parents[1]
build=Path(sys.argv[1]).resolve();target=Path(sys.argv[2]).resolve()
frontend=len(sys.argv)>3 and not sys.argv[3].startswith("--policy-shard=")
core=Path(sys.argv[3]).resolve() if frontend else None
cache=(build/'CMakeCache.txt').read_text()
ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
mutants=[
 ('control-close-v','engine/rpc/rpc_observers.c','e->matched && !(e->nzcv&0x10000000u) && state.error_return_closes','e->matched && true && state.error_return_closes'),
 ('control-immediate','engine/rpc/rpc_observers.c','(e->registers[1]&0x60u)!=0','(e->registers[1]&0x40u)!=0'),
 ('control-close-loss','engine/rpc/rpc_observers.c','(e->registers[1]&0x60u)!=0','(e->registers[1]&0x20u)!=0'),
 ('control-poll','engine/rpc/rpc_observers.c','!state.error_unbounded && !state.error_watch','!state.error_watch'),
 ('control-return','engine/rpc/rpc_observers.c','state.error.modal_unknown=state.error_unbounded ||','state.error.modal_unknown=false ||'),
 ('control-close-return','engine/rpc/rpc_observers.c','(e->registers[1]&0x40u)!=0 || state.error.recognised_open','false || state.error.recognised_open'),
 ('control-ordinary-return','engine/rpc/rpc_observers.c','(e->registers[1]&0x40u)!=0 || state.error.recognised_open','(e->registers[1]&0x40u)!=0 || false'),
 ('control-close-match','engine/rpc/rpc_observers.c','e->kind==ARC_RPC_OBS_CONTINUATION && e->matched && !(e->nzcv&0x10000000u) && state.error_return_closes','e->kind==ARC_RPC_OBS_CONTINUATION && true && !(e->nzcv&0x10000000u) && state.error_return_closes'),
 ('control-unknown-return','engine/rpc/rpc_observers.c','(e->registers[1]&0x40u)!=0 || state.error.recognised_open','true'),
 ('control-reset','engine/rpc/rpc_observers.c','arc_rpc_observers_bind(catalogue,cpu);\n#if defined(ARCDROID_TEST_FAULTS)','bool stale_control=state.error_unbounded;arc_rpc_observers_bind(catalogue,cpu);state.error_unbounded=stale_control;state.error.modal_unknown|=stale_control;\n#if defined(ARCDROID_TEST_FAULTS)'),
 ('control-epoch','engine/rpc/rpc_observers.c','memset(&state,0,sizeof(state));','bool stale_control=state.error_unbounded;memset(&state,0,sizeof(state));state.error_unbounded=stale_control;'),
 ('renew-cancel','engine/rpc/rpc_observers.c','arc_rpc_observer_cancel(ARC_RPC_OBS_ERROR);error_loss();return;','error_loss();return;'),
 ('user26','engine/rpc/rpc_observers.c','return mode==0u || mode==16u;','return mode==16u;'),
 ('a-filesystem','engine/rpc/rpc_observers.c','e->registers[1]!=8?"filesystem":NULL','false?"filesystem":NULL'),
 ('a-rom','engine/rpc/rpc_observers.c','e->backing.kind!=ARC_RPC_BACKING_ROM?"non-rom":','false?"non-rom":'),
 ('a-upcall4','engine/rpc/rpc_observers.c','(swi==0x33u && e->registers[0]==4u)','false'),
 ('b-busy-latch','engine/rpc/rpc_observers.c','state.retry_pending=true;state.retry_work=*w;','state.retry_pending=false;state.retry_work=*w;'),
 ('b-busy-retry','engine/rpc/rpc_observers.c','if(state.retry_pending){','if(false){'),
 ('found-return-armed','engine/rpc/rpc_observers.c','if(!state.disc.armed){state.disc_watch=0;return;}','(void)0;'),
 ('found-return-match','engine/rpc/rpc_observers.c','else if(e->matched && !(e->nzcv','else if(!(e->nzcv'),
 ('found-queued-identity','engine/rpc/rpc_observers.c','arc_rpc_observer_drop_queued(ARC_RPC_OBS_DISC,ARC_DISC_SIGNAL_B,state.disc.object,strlen(state.disc.object)+1);','arc_rpc_observer_cancel(ARC_RPC_OBS_DISC);'),
 ('found-queued-same','engine/rpc/rpc_observers.c','arc_rpc_observer_drop_queued(ARC_RPC_OBS_DISC,ARC_DISC_SIGNAL_B,state.disc.object,strlen(state.disc.object)+1);','(void)0;'),
 ('lifecycle-a-fields','engine/rpc/rpc_observers.c','state.disc.a_notified=false;state.disc.a_name[0]=0;','(void)0;'),
 ('lifecycle-a-cell','engine/rpc/rpc_observers.c','(arc_rpc_observers_capability(ARC_RPC_CELL_DISC_A) || arc_rpc_observers_capability(ARC_RPC_CELL_DISC_B))','arc_rpc_observers_capability(ARC_RPC_CELL_DISC_B)'),
 ('lifecycle-later-arm','engine/rpc/rpc_observers.c','if(observation.armed){','if(false){'),
 ('lifecycle-queued-order','engine/rpc/rpc_observers.c','queue_disc(e,DISC_CLEAR);state.disc.armed=false;','arc_rpc_observers_disc_clear();state.disc.armed=false;'),
 ('overflow-history','engine/rpc/rpc_observers.c','if(!arc_rpc_observer_queue(&work)){state.disc.armed=false;state.disc_watch=0;}','if(!arc_rpc_observer_queue(&work)){arc_disc_request_clear(&state.disc);state.disc_watch=0;}'),
 ('category-form','engine/arc_error_box.c','(!(r1 & 0x100u) && (r1 & ~0x1fu))','(r1 & ~0x11fu)'),
 ('production-answer-gates','engine/rpc/rpc_observers.c',('out->consumed==ARC_ERROR_NONE &&\n        arc_rpc_observers_capability(ARC_RPC_CELL_ANSWER)','return arc_rpc_observers_capability(ARC_RPC_CELL_ANSWER) && state.error_watch'),('out->consumed==ARC_ERROR_NONE','return state.error_watch')),
 ('reset-open-error','engine/rpc/rpc_observers.c','arc_rpc_observers_bind(catalogue,cpu);\n#if defined(ARCDROID_TEST_FAULTS)','ArcErrorBox stale=state.error;arc_rpc_observers_bind(catalogue,cpu);state.error=stale;state.error.profile=&state.error_profile;\n#if defined(ARCDROID_TEST_FAULTS)'),
 ('reset-open-disc','engine/rpc/rpc_observers.c',('unsigned catalogue=state.catalogue,cpu=actual_cpu;','arc_rpc_observers_disc_configure(enabled,inserted,resolve,observe,opaque);'),('ArcDiscRequest stale_disc=state.disc;uint64_t stale_media=state.disc_media,stale_helper=state.helper_media;unsigned catalogue=state.catalogue,cpu=actual_cpu;','arc_rpc_observers_disc_configure(enabled,inserted,resolve,observe,opaque);state.disc=stale_disc;state.disc_media=stale_media;state.helper_media=stale_helper;')),
 ('swi-cache','engine/rpc/rpc_observer.c','if(!wanted && !swi_interest.unrestricted && !cached_swi','if(false && !swi_interest.unrestricted && !cached_swi'),
 ('swi-list-owner','engine/rpc/rpc_observer.c','if(!f->swi_count)return true;','return true;'),
 ('swi-list-bound','engine/rpc/rpc_observer.c','filter->swi_count>32','false'),
 ('swi-list-null','engine/rpc/rpc_observer.c','(filter->swi_count && !filter->swis)','false'),
 ('swi-metadata','engine/rpc/rpc_observer.c','if(!swi_wanted(opcode)){if(observers_active)++observer.order;return false;}\n    observer.origin.instruction','observer.origin.pc=pc;if(!swi_wanted(opcode)){if(observers_active)++observer.order;return false;}\n    observer.origin.instruction'),
 ('swi-capture','engine/rpc/rpc_observer.c','static bool swi_wanted(uint32_t opcode)\n{','static bool swi_wanted(uint32_t opcode)\n{ return true;'),
 ('production-graphical-evidence','engine/rpc/rpc_observer.c','ga-*, gb-*, gna-*, gnb-*, ','ga-*, gb-*, '),
 ('production-table','engine/rpc/rpc_observer.c','ARC_RPC_OBS_ERROR,ARC_RPC_CAP_OBSERVE,true,error','ARC_RPC_OBS_ERROR,ARC_RPC_CAP_OBSERVE,false,error'),
 ('production-reason','engine/rpc/rpc_observer.c','awaiting R3c-3 launch hook','awaiting a different hook'),
 ('production-duplicate-row','engine/rpc/rpc_observer.c','PAIR(4,3,ERROR_EVIDENCE,DISC_A_EVIDENCE)\n};','PAIR(4,3,ERROR_EVIDENCE,DISC_A_EVIDENCE),PAIR(1,0,ERROR_EVIDENCE,DISC_A_EVIDENCE)\n};'),
 ('production-extra-row','engine/rpc/rpc_observer.c','PAIR(4,3,ERROR_EVIDENCE,DISC_A_EVIDENCE)\n};','PAIR(4,3,ERROR_EVIDENCE,DISC_A_EVIDENCE),PAIR(100,0,ERROR_EVIDENCE,DISC_A_EVIDENCE)\n};'),
 ('production-error-number','engine/rpc/rpc_observers.c','!arc_rpc_inspect_word(e->registers[0],false,&number) ||\n                !arc_error_number_answerable(&state.error_profile,number)','false'),
 ('watch-renew','engine/rpc/rpc_observer.c','observer.watches[i].deadline=observer.instruction+attempts;return true;','return true;'),
 ('renew-site','engine/arc_error_box.c','address == box->site && sp == box->sp && mode == box->poll_mode','true && sp == box->sp && mode == box->poll_mode'),
 ('renew-stack','engine/arc_error_box.c','address == box->site && sp == box->sp && mode == box->poll_mode','address == box->site && true && mode == box->poll_mode'),
 ('renew-mode','engine/arc_error_box.c','address == box->site && sp == box->sp && mode == box->poll_mode','address == box->site && sp == box->sp && true'),
 ('renew-rom','engine/arc_error_box.c','box->recognised_open && in_rom &&','box->recognised_open && true &&'),
 ('modal-recovery','engine/rpc/rpc_observers.c','state.error.modal_unknown=false;return;','return;'),
 ('modal-recovery-user','engine/rpc/rpc_observers.c','swi==0x400e1u) && user(e->mode) &&','swi==0x400e1u) && true &&'),
 ('modal-recovery-pending','engine/rpc/rpc_observers.c','!state.error_watch && !state.error.recognised_open','true'),
 ('modal-recovery-idle','engine/rpc/rpc_observers.c','(swi==0x400c7u || swi==0x400e1u) && user(e->mode)','swi==0x400c7u && user(e->mode)'),
 ('disc-loss-identity','engine/rpc/rpc_observers.c','if(e->kind==ARC_RPC_OBS_WATCH_LOST){\n        if(e->watch!=state.disc_watch)return;','if(e->kind==ARC_RPC_OBS_WATCH_LOST){'),
 ('disc-replacement-identity','engine/rpc/rpc_observers.c','state.disc_watch=0; /* Replacement loss belongs to the superseded request. */','(void)0;'),
 ('production-number-class','engine/rpc/rpc_observers.c','.numbers=ARC_ERROR_NUMBER_ORDINARY};','.numbers=ARC_ERROR_NUMBER_ZERO};'),
 ('number-zero','engine/arc_error_box.c','(number == 0 || p->numbers == ARC_ERROR_NUMBER_ORDINARY)','true'),
 ('number-ordinary','engine/arc_error_box.c','(number == 0 || p->numbers == ARC_ERROR_NUMBER_ORDINARY)','number == 0'),
 ('number-serious','engine/arc_error_box.c','!(number & UINT32_C(0x80000000))','true'),
 ('number-reset','engine/rpc/rpc_observers.c','arc_rpc_observers_test_error_numbers(profile.numbers)','arc_rpc_observers_test_error_numbers(ARC_ERROR_NUMBER_ZERO)'),
 ('number-retirement','engine/rpc/rpc_observers.c','numbers!=state.error_profile.numbers && state.error_watch','false'),
 ('cell-gate','engine/rpc/rpc_observers.c','(state.cells&bit)==bit','true'),
 ('poll-r1','engine/arc_error_box.c','r1 != p->poll_r1 ||','false ||'),
 ('poll-r2','engine/arc_error_box.c','r2 != p->poll_r2','false'),
 ('poll-rom','engine/arc_error_box.c','!box->recognised_open || !in_rom ||','!box->recognised_open || false ||'),
 ('poll-full-mode','engine/arc_error_box.c','if (mode != box->poll_mode) return false;','if (false) return false;'),
 ('poll-irq','engine/arc_error_box.c','(mode>31 || !(p->poll_modes&(UINT32_C(1)<<mode)))','false'),
 ('site','engine/arc_error_box.c','address != box->site ||','false ||'),
 ('stack','engine/arc_error_box.c','sp != box->sp)','false)'),
 ('learn-consumes','engine/arc_error_box.c','box->answer_available = box->ok || box->cancel;\n      return false;','box->answer_available = box->ok || box->cancel;\n      return true;'),
 ('answer-capability','engine/rpc/rpc_observers.c',('(mask&ARC_RPC_CELL_ANSWER)?forms:0','(state.cells&bit)==bit'),('forms','((state.cells&bit)==bit || bit==ARC_RPC_CELL_ANSWER)')),
 ('form-gate','engine/arc_error_box.c','r1 <= 31u && (box->profile->answer_forms & (UINT32_C(1) << r1))','r1 <= 31u'),
 ('open-modal','engine/rpc/rpc_observers.c','!state.error.recognised_open && !state.error.modal_unknown','true'),
 ('unknown-modal','engine/rpc/rpc_observers.c','&& !state.error.modal_unknown','&& true'),
 ('upcall-x','engine/arc_disc_request.c','(profile && profile->require_x && !(event->swi&0x20000u))','false'),
 ('upcall-device','engine/arc_disc_request.c','!(device_form & (profile?profile->device_forms:(ARC_DISC_DEVICE_0|ARC_DISC_DEVICE_UNSPECIFIED)))','false'),
 ('upcall-default','engine/rpc/rpc_observers.c','ARC_DISC_DEVICE_UNSPECIFIED,true,true','ARC_DISC_DEVICE_0,true,true'),
 ('upcall-other-device','engine/arc_disc_request.c','event->r3==UINT32_MAX?ARC_DISC_DEVICE_UNSPECIFIED:0u','event->r3==UINT32_MAX?ARC_DISC_DEVICE_UNSPECIFIED:ARC_DISC_DEVICE_0'),
 ('device-override','engine/rpc/rpc_observers.c','state.disc_profile.device_forms=forms;','state.disc_profile.device_forms=ARC_DISC_DEVICE_UNSPECIFIED;'),
 ('device-override-bound','engine/rpc/rpc_observers.c','!state.overridden || (forms&~3u)','!state.overridden'),
 ('device-reset','engine/rpc/rpc_observers.c','arc_rpc_observers_test_disc_devices(devices)','arc_rpc_observers_test_disc_devices(ARC_DISC_DEVICE_UNSPECIFIED)'),
 ('device-retirement','engine/rpc/rpc_observers.c','if(forms!=state.disc_profile.device_forms)arc_rpc_observers_disc_clear();','if(false)arc_rpc_observers_disc_clear();'),
 ('a-supersedes-b','engine/rpc/rpc_observers.c','arc_rpc_observer_cancel(ARC_RPC_OBS_DISC); /* A supersedes copied B misses synchronously. */','(void)0;'),
 ('helper-generation','engine/rpc/rpc_observers.c','w->helper_generation==arc_rpc_fdc_media_generation(1)','state.helper_media==arc_rpc_fdc_media_generation(1)'),
 ('bare-name','engine/arc_disc_request.c','profile && profile->strict_bare &&','false &&'),
 ('copy-object','engine/rpc/rpc_observers.c','strcpy(observation.object,copy.object);','strcpy(observation.object,state.disc.object);'),
 ('copy-continuation','engine/rpc/rpc_observers.c','observation.continuation=copy.continuation;','observation.continuation=state.disc.continuation;'),
 ('helper-exclusion','engine/rpc/rpc_observers.c','(helper[0] && equal(name,helper))','false'),
 ('hd-exclusion','engine/rpc/rpc_observers.c','(hd[0] && equal(name,hd))','false'),
 ('unknown-identity','engine/rpc/rpc_observers.c','!arc_rpc_observers_identities(helper,hd)','(arc_rpc_observers_identities(helper,hd),false)'),
 ('nested-pending','engine/arc_error_box.c','box->pending = box->consumed = ARC_ERROR_NONE;','box->consumed = ARC_ERROR_NONE;'),
 ('watch-expiry','engine/rpc/rpc_observer.c','observer.instruction>observer.watches[i].deadline','false'),
 ('watch-loss','engine/rpc/rpc_observers.c','if(e->kind==ARC_RPC_OBS_WATCH_LOST || e->kind==ARC_RPC_OBS_CONTINUATION)','if(e->kind==ARC_RPC_OBS_CONTINUATION)'),
 ('privileged-nested-report','engine/rpc/rpc_observers.c','if(state.error_watch){arc_rpc_observer_cancel(ARC_RPC_OBS_ERROR);error_loss();}','if(false){arc_rpc_observer_cancel(ARC_RPC_OBS_ERROR);error_loss();}'),
 ('launch-user','engine/rpc/rpc_observers.c','(void)opaque;if(e->kind!=ARC_RPC_OBS_SWI || !user(e->mode))return;\n    char command','(void)opaque;if(e->kind!=ARC_RPC_OBS_SWI)return;\n    char command'),
 ('launch-exit-rom','engine/rpc/rpc_observers.c','{600000000u,true,true,true}','{600000000u,false,true,true}'),
 ('launch-close-rom','engine/rpc/rpc_observers.c','{600000000u,true,true,true}','{600000000u,true,false,true}'),
 ('launch-poll-rom','engine/rpc/rpc_observers.c','{600000000u,true,true,true}','{600000000u,true,true,false}'),
 ('launch-horizon','engine/rpc/rpc_observers.c','{600000000u,true,true,true}','{600000001u,true,true,true}'),
 ('boot-user','engine/rpc/rpc_observers.c',('0x8000u,true,true','(void)opaque;if(e->kind!=ARC_RPC_OBS_SWI || !user(e->mode))return;\n    arc_boot_watch_observe'),('0x8000u,false,true','(void)opaque;if(e->kind!=ARC_RPC_OBS_SWI)return;\n    arc_boot_watch_observe')),
 ('boot-buffer','engine/rpc/rpc_observers.c','0x8000u,true,true','0x8001u,true,true'),
 ('boot-horizon','engine/rpc/rpc_observers.c','{40000000u,2000000u,0x8000u,true,true}','{40000001u,2000000u,0x8000u,true,true}'),
 ('boot-rom','engine/rpc/rpc_observers.c','0x8000u,true,true','0x8000u,true,false'),
]
if not frontend and len(sys.argv)>3:
    shard=int(sys.argv[3].split('=',1)[1]);assert shard in (0,1)
    mutants=mutants[shard::2]
if frontend:
    mutants=[
      ('b-log-path-word','engine/rpc/rpc_observers.c','"path-other"','"path-refused"'),
      ('b-log-outcome-word','engine/rpc/rpc_observers.c','"outcome-not-miss"','"outcome-refused"'),
      ('control-hint','libretro/arcdroid_libretro.c','error_box.recognised_open || (error_box.flags&0x60u) ||','error_box.recognised_open || false ||'),
      ('b-log-identity','engine/rpc/rpc_observers.c','*out=disc_observation?*disc_observation:state.disc;','*out=state.disc;'),
      ('b-replace-cell','engine/rpc/rpc_observers.c','signal==ARC_DISC_SIGNAL_A?ARC_RPC_CELL_REPLACE_A:ARC_RPC_CELL_REPLACE_B','signal==ARC_DISC_SIGNAL_A?ARC_RPC_CELL_REPLACE_A:ARC_RPC_CELL_REPLACE_A'),
      ('b-commit-signal','libretro/arcdroid_libretro.c','arc_disc_watch_generation, ARC_DISC_SIGNAL_B);','arc_disc_watch_generation, ARC_DISC_SIGNAL_A);'),
      ('b-replace-capability','libretro/arcdroid_libretro.c','arc_engine_is_rpc() && !arc_engine_rpc_observers_capability(ARC_RPC_CELL_REPLACE_B)','false'),
      ('b-replace-exclusion','engine/rpc/rpc_observers.c','state.disc.b_triggered && !excluded(state.inserted)','state.disc.b_triggered'),
      ('b-work-exclusion','engine/rpc/rpc_observers.c','copy.signal>=DISC_FOUND || !excluded(copy.signal==ARC_DISC_SIGNAL_A?copy.object:state.inserted)','copy.signal!=ARC_DISC_SIGNAL_A || !excluded(copy.object)'),
      ('hint-after-answer','libretro/arcdroid_libretro.c','bool hint_answerable=error_box.answer_available || answered;','bool hint_answerable=error_box.answer_available;'),
      ('cancel-only-pad','libretro/arcdroid_libretro.c','arc_engine_error_request(ARC_ERROR_OK);','arc_engine_error_request(error_box.ok?ARC_ERROR_OK:ARC_ERROR_CANCEL);'),
      ('cancel-only-hint','libretro/arcdroid_libretro.c','box->ok && box->cancel ?','box->cancel ?'),
      ('reset-binding-cpu','engine/rpc/rpc_session.c','arc_rpc_observers_rebase(arc_rpc_session_cpu());','arc_rpc_observers_rebase(0);'),
      ('session-frame','engine/rpc/rpc_session.c','arc_rpc_observers_frame(arc_rpc_scheduler.now_ns/1000u);','(void)0;'),
      ('session-frame-unit','engine/rpc/rpc_session.c','arc_rpc_observers_frame(arc_rpc_scheduler.now_ns/1000u);','arc_rpc_observers_frame(arc_rpc_scheduler.now_ns);'),
      ('reset-swap-owner','libretro/arcdroid_libretro.c','arc_disc_owned=false;arc_disc_previous_index=0;arc_disc_swapped_us=0;\n      arc_disc_notice_read=arc_disc_notice_write=0;\n#if defined(ARCDROID_TEST_PLANS)','(void)0;\n#if defined(ARCDROID_TEST_PLANS)'),
      ('hd-record-agreement','engine/arcdroid_engine.c','memcmp(boot+0x1c0+22,map+26,10) || memcmp(map+26,backup+26,10)','false'),
      ('hd-checksum','engine/arcdroid_engine.c','sum!=boot[511] ||','false ||'),
      ('hd-geometry','engine/arcdroid_engine.c','boot[0x1c0]!=9 || boot[0x1c1]!=63 || boot[0x1c2]!=16 ||','false ||'),
      ('helper-writable','engine/arcdroid_engine.c','!rpc_helper.kind || rpc_helper.writable','!rpc_helper.kind'),
      ('identity-collision','engine/arcdroid_engine.c','if(same)return false;','if(false)return false;'),
      ('identity-case','engine/rpc/rpc_observers.c',("if(x>='A'&&x<='Z')x+='a'-'A';","if(y>='A'&&y<='Z')y+='a'-'A';"),('(void)0;','(void)0;')),
      ('helper-long-path','engine/arcdroid_engine.c','drive==1 && media && media->path && strlen(media->path)>=sizeof(rpc_helper_path)','false'),
      ('hash-skip','engine/rpc/rpc_session.c','arc_sha1_update(&arc_rpc_firmware_hash,copied,take);','(void)0;'),
      ('hash-halve','engine/rpc/rpc_session.c','arc_sha1_update(&arc_rpc_firmware_hash,copied,take);','arc_sha1_update(&arc_rpc_firmware_hash,copied,take/2);'),
      ('reset-hint','libretro/arcdroid_libretro.c','if(arc_engine_is_rpc() && arc_engine_active()) {\n      arc_error_hint_generation=arc_error_seen_generation=0;arc_error_hint_state=0;','if(arc_engine_is_rpc() && arc_engine_active()) {'),
      ('observation-only-pad','libretro/arcdroid_libretro.c','rpc_error ? error_box.answer_available :','rpc_error ? error_box.recognised_open :'),
      ('pad-release','libretro/arcdroid_libretro.c','(error_ready || arc_error_pad_gate)) continue;','(error_ready || false)) continue;'),
      ('pad-simultaneous','libretro/arcdroid_libretro.c','!arc_error_pad_gate && error_a != error_b','!arc_error_pad_gate && true'),
      ('pad-answer','libretro/arcdroid_libretro.c','(void)arc_engine_error_request(ARC_ERROR_OK);','(void)arc_engine_error_request(ARC_ERROR_CANCEL);'),
      ('binding-catalogue','engine/arcdroid_engine.c','catalogue==actual?actual:0','catalogue'),
      ('binding-cpu','engine/arcdroid_engine.c','arc_rpc_observers_bind(catalogue==actual?actual:0,arc_rpc_session_cpu());','arc_rpc_observers_bind(catalogue==actual?actual:0,0);'),
      ('commit-recheck-generation','libretro/arcdroid_libretro.c','if(arc_engine_is_rpc() && (generation!=arc_disc_generation ||','if(arc_engine_is_rpc() && (false ||'),
      ('commit-recheck-idle','libretro/arcdroid_libretro.c','!arc_engine_disc_controller_idle() || !arc_engine_rpc_observers_replace(signal)))','false || !arc_engine_rpc_observers_replace(signal)))'),
      ('commit-recheck-replacement','libretro/arcdroid_libretro.c','!arc_engine_disc_controller_idle() || !arc_engine_rpc_observers_replace(signal)))','!arc_engine_disc_controller_idle() || false))'),
      ('identity-write-invalidation','engine/arcdroid_engine.c','end>rpc_map_lba+5)))rpc_hd_known=false;','end>rpc_map_lba+5)))rpc_hd_known=true;'),
      ('identity-unchanged-name','engine/arcdroid_engine.c','!memcmp(record+(r?26:0x1c0+22),rpc_hd_record_name,10)','true'),
      ('identity-write-success','engine/arcdroid_engine.c','rpc_hd_known=ok && known && unchanged;','rpc_hd_known=known && unchanged;'),
      ('identity-frame-reads','engine/arcdroid_engine.c','memcpy(helper,rpc_helper_name,11);memcpy(hd,rpc_hd_name,11);','rpc_helper_derive();rpc_hd_derive();memcpy(helper,rpc_helper_name,11);memcpy(hd,rpc_hd_name,11);'),
      ('hint-transition','libretro/arcdroid_libretro.c','rpc_error && arc_error_hint_state != hint_state','false'),
      ('hint-retired','libretro/arcdroid_libretro.c','error_hint_open || error_hint_unknown','error_hint_open'),
      ('disc-disabled-repeat','libretro/arcdroid_libretro.c','slot==2u && time-history[slot].time','true && time-history[slot].time'),
      ('disc-busy-repeat','libretro/arcdroid_libretro.c','slot==2u && time-history[slot].time','false && time-history[slot].time'),
      ('manual-hint','libretro/arcdroid_libretro.c','rpc_error ? error_box.recognised_open : error_ready','rpc_error ? false : error_ready'),
    ]
    if len(sys.argv)>4:
        shard=int(sys.argv[4]);assert shard in (0,1,2,3)
        mutants=mutants[shard::4]
    subprocess.run([sys.executable,str(root/'tests/rpc_observers_frontend_test.py'),str(target),str(core),str(build/"tests/fixtures/generated")],check=True,cwd=build,timeout=60,capture_output=True)
    for focus in ('h-error','h-b','h-identity','h-session'):
        subprocess.run([sys.executable,str(root/'tests/rpc_observers_frontend_test.py'),str(target),str(core),str(build/"tests/fixtures/generated"),focus],check=True,cwd=build,timeout=60,capture_output=True)
else:
    subprocess.run([str(target)],check=True,cwd=build,timeout=30,capture_output=True)
for name,relative,old,new in mutants:
    source=root/relative;text=source.read_text()
    edits=list(zip(old,new)) if isinstance(old,tuple) else [(old,new)]
    for before,after in edits:
        if text.count(before)!=1:raise RuntimeError((name,'anchor',text.count(before)))
        text=text.replace(before,after)
    target_name=('arcdroid_rpcplan_libretro_rpc' if relative.startswith('engine/rpc/') else 'arcdroid_rpcplan_libretro') if frontend else 'rpc_observers_test'
    compile,link=rebuild_commands(commands,source,target_name,build,os.name=='nt')
    with tempfile.TemporaryDirectory(prefix='arc-rpc-observers-mutant-') as folder:
        folder=Path(folder);copy=folder/source.name;obj=folder/'mutant.o';binary=folder/(core.name if frontend else target.name)
        copy.write_text(text)
        original_obj=compile[compile.index('-o')+1];compile[compile.index('-o')+1]=str(obj);compile[compile.index('-c')+1]=str(copy)
        compile+=['-I',str(source.parent)];link=[str(obj) if x==original_obj else x for x in link];link[link.index('-o')+1]=str(binary)
        for command in (compile,link):
            result=subprocess.run(command,cwd=build,capture_output=True,text=True,timeout=60)
            if result.returncode:raise RuntimeError((name,'build failure',result.stderr))
        focus='mutant'
        if name.startswith('b-') or name=='reset-swap-owner' or name=='identity-case': focus='h-b'
        if name in ('control-hint','hint-after-answer','cancel-only-pad','cancel-only-hint','reset-binding-cpu'): focus='h-error'
        if name.startswith('session-frame'): focus='h-session'
        if name in ('hd-record-agreement','hd-checksum','hd-geometry','helper-writable','identity-collision','helper-long-path'): focus='h-identity'
        argv=[sys.executable,str(root/'tests/rpc_observers_frontend_test.py'),str(target),str(binary),str(build/"tests/fixtures/generated"),focus] if frontend else [str(binary)]
        result=subprocess.run(argv,cwd=build,capture_output=True,text=True,timeout=60)
        if not result.returncode and not frontend:
            result=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_observer_qualification_test.py'),str(binary),str(root/'docs/riscpc-observer-qualification.md')],cwd=build,capture_output=True,text=True,timeout=30)
        if not result.returncode:raise RuntimeError((name,'SURVIVED'))
        print(name+': killed',flush=True)
print(f'{len(mutants)} RPC observer mutants killed; no build failures counted')

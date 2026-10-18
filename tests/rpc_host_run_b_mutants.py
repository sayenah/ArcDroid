"""Behavioural killers for each S68 QA survivor; never invoke a source guard.
Each variant compiles to a private object and links unchanged production objects.
GPL-2.0-only.
"""
from pathlib import Path
import json,os,subprocess,sys,tempfile
from rpc_ninja_commands import rebuild_commands,same_path
build,root,exe,frontend,core,fixtures=map(lambda p:Path(p).resolve(),sys.argv[1:])
cache=(build/'CMakeCache.txt').read_text();ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
rows=[
 ('maintenance-identity','libretro/rpc_preparation.c','ArcLockResult identity=arc_lock_verify(&p->content_owner);','ArcLockResult identity=ARC_LOCK_HELD;','host-b'),
 ('exception-1','libretro/rpc_preparation_host.c','(event.stopping && !h->stopping)','false','host-b'),
 ('fallback-resend','libretro/rpc_preparation_host.c','display(h,h->terminal,ARC_RPC_NOTICE_TERMINAL);h->terminal_frames=60;','display(h,h->terminal,ARC_RPC_NOTICE_TERMINAL);h->terminal_frames=0;','host-b'),
 ('defaults-phase-exit','libretro/rpc_preparation.c','else if(done){save_record(p,p->defaults.payload);arc_lock_release(&p->defaults_owner);','else if(done){save_record(p,p->defaults.payload);/* retained defaults owner */','host-b'),
 ('stepped-CMOS','libretro/rpc_preparation.c','persisted=arc_riscpc_cmos_store_put_begin(&p->record,payload,NULL,e,sizeof(e));','persisted=arc_riscpc_cmos_store_put(&p->record,payload,NULL,e,sizeof(e));','host-b'),
 ('launch-publication-identity','libretro/rpc_launch_record_store.c',' && arc_lock_verify(s->owner)==ARC_LOCK_HELD','', 'host-b'),
 ('unredacted-staging','libretro/arcdroid_libretro.c','    (void)format;\n    if(arc_log)arc_log(level,"status: RiscPC disc staging: the selected content; the selected disc; the content folder.\\n");','    char text[8192];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);\n    if(arc_log)arc_log(level,"%s",text);','freeze'),
 ('raw-attach-error','libretro/arcdroid_libretro.c','arc_rpc_safe_error(error,n,local,true)','snprintf(error,n,"%s",local)','stage-error'),
]
probe='''
RETRO_API bool arc_test_host_stage_error(const char *path,char *error,size_t n)
{ArcRpcPreparationRequest request={0};return arc_rpc_stage_safe(NULL,path,"",&request,error,n);}
'''
def execute(binary,mode):
    if mode=='host-b':return [sys.executable,'-B',str(root/'tests/rpc_orchestration_test.py'),str(binary),str(fixtures),mode]
    return [sys.executable,'-B',str(root/'tests/rpc_host_frontend_test.py'),str(frontend),str(binary),str(fixtures),mode]
def build_variant(temp,relative,text,mode):
    target='rpc_orchestration_test' if mode=='host-b' else 'arcdroid_rpcplan_libretro'
    source=root/relative;cc,link=rebuild_commands(commands,source,target,build,os.name=='nt')
    changed=temp/source.name;changed.write_text(('#include <stdarg.h>\n'+text+probe) if relative.endswith('arcdroid_libretro.c') else text)
    obj=temp/'variant.o';previous=cc[cc.index('-o')+1];cc[cc.index('-o')+1]=str(obj);cc[cc.index('-c')+1]=str(changed)
    if '-MF' in cc:cc[cc.index('-MF')+1]=str(temp/'variant.d')
    output=temp/('variant.exe' if os.name=='nt' else 'variant') if mode=='host-b' else temp/core.name
    link=[str(obj) if x.lower().endswith(('.o','.obj')) and same_path(x,previous,build,os.name=='nt') else x for x in link]
    link[link.index('-o')+1]=str(output)
    link=['-Wl,--out-implib,'+str(temp/'variant.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
    for cmd in (cc,link):
        r=subprocess.run(cmd,cwd=build,capture_output=True,text=True,timeout=120)
        if r.returncode:raise RuntimeError('variant build failed (not a kill): '+r.stdout+r.stderr)
    return output
with tempfile.TemporaryDirectory(prefix='arc-host-run-b-mutants-') as temporary:
    temp=Path(temporary)
    # Prove both the ordinary test and the temporary callback probe accept good code.
    for mode in ('host-b','freeze'):
        result=subprocess.run(execute(exe if mode=='host-b' else core,mode),capture_output=True,text=True,timeout=180)
        if result.returncode:raise RuntimeError('baseline fails: '+result.stdout+result.stderr)
    text=(root/'libretro/arcdroid_libretro.c').read_text()
    baseline=build_variant(temp,'libretro/arcdroid_libretro.c',text,'stage-error')
    result=subprocess.run(execute(baseline,'stage-error'),capture_output=True,text=True,timeout=180)
    if result.returncode:raise RuntimeError('stage callback baseline fails: '+result.stdout+result.stderr)
    for label,relative,old,new,mode in rows:
        original=(root/relative).read_text()
        if original.count(old)!=1:raise RuntimeError(f'{label}: expected one mutation anchor, got {original.count(old)}')
        binary=build_variant(temp,relative,original.replace(old,new,1),mode)
        result=subprocess.run(execute(binary,mode),capture_output=True,text=True,timeout=180)
        if result.returncode==0:raise RuntimeError('behavioural mutant survived: '+label)
        if 'Assertion failed:' not in result.stderr and 'Assertion `' not in result.stderr and 'AssertionError' not in result.stderr:
            raise RuntimeError('mutant failed without an assertion (not a kill): '+label+'\n'+result.stdout+result.stderr)
        reason=next((line for line in result.stderr.splitlines() if 'Assertion' in line),'assertion')
        print(f'{label}: killed by {mode}: {reason}',flush=True)

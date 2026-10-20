"""Ordinary entry-point matrix with temporary read-only probes, never shipping.
GPL-2.0-only.
"""
from pathlib import Path
import importlib.util,json,os,shutil,subprocess,sys,tempfile
from rpc_ninja_commands import rebuild_commands,same_path
build,root,exe,fixtures,legacy=map(lambda p:Path(p).resolve(),sys.argv[1:6])
cache=(build/'CMakeCache.txt').read_text()
ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[r['command'] for r in json.loads(subprocess.check_output([ninja,'-C',str(build),'-t','compdb','-x'],text=True))]
with tempfile.TemporaryDirectory(prefix='rpc-route-matrix-') as temporary:
    temp=Path(temporary);objects={};link=None
    for relative in ('libretro/rpc_preparation.c','libretro/arcdroid_libretro.c'):
        original=root/relative;source=temp/original.name;text=original.read_text(encoding='utf-8')
        if relative.endswith('/rpc_preparation.c'):
            # End a frame at each STATE boundary, while retaining the normal
            # bounded stepping within that state. No need to rasterize one
            # black frame per individual I/O unit (D-209).
            declaration='ArcRpcPreparationState arc_riscpc_preparation_frame(ArcRpcPreparation *p)'
            assert text.count(declaration)==1
            text=text.replace(declaration,'static ArcRpcPreparationState arc_route_probe_frame_step(ArcRpcPreparation *p)')
            text+='''
RETRO_API void arc_route_preparation_position(const ArcRpcPreparation *p,unsigned *state)
{*state=p?p->state:ARC_RPC_PREP_CANCELLED;}
static const ArcRpcPreparation *route_clock_owner;
static unsigned route_clock_state;
uint64_t arc_route_unit_clock(void *opaque)
{
 (void)opaque;static uint64_t value;value+=1000;
 if(route_clock_owner && route_clock_owner->state!=route_clock_state){
     route_clock_state=route_clock_owner->state;value+=UINT64_C(66666665);
 }
 return value;
}
ArcRpcPreparationState arc_riscpc_preparation_frame(ArcRpcPreparation *p)
{
 route_clock_owner=p;route_clock_state=p?(unsigned)p->state:0;
 ArcRpcPreparationState state=arc_route_probe_frame_step(p);route_clock_owner=NULL;return state;
}
'''
        else:
            text=text.replace('static ArcRpcFrontendOps arc_riscpc_host_ops(void);','extern uint64_t arc_route_unit_clock(void *);\nstatic ArcRpcFrontendOps arc_riscpc_host_ops(void);')
            text=text.replace('arc_riscpc_preparation_clock','arc_route_unit_clock')
            clock_anchor='.kind=arc_kind,.module={.attach=arc_rpc_attach'
            assert text.count(clock_anchor)==1
            text=text.replace(clock_anchor,'.kind=arc_kind,.module={.clock=arc_route_unit_clock,.attach=arc_rpc_attach')
            text+='''
extern void arc_route_preparation_position(const ArcRpcPreparation *,unsigned *);
RETRO_API void arc_route_probe_position(unsigned *state)
{arc_route_preparation_position(arc_riscpc_host_preparation(arc_rpc_host),state);}
RETRO_API void arc_route_probe_fingerprint(uint8_t output[32])
{
 ArcSha256 hash;arc_sha256_init(&hash);
#define OBSERVE(value) arc_sha256_update(&hash,&value,sizeof(value))
 OBSERVE(arc_inputs);OBSERVE(arc_keyboard_raw);OBSERVE(arc_pad_keys);
 OBSERVE(arc_media_count);OBSERVE(arc_image_index);OBSERVE(arc_ejected);
 OBSERVE(arc_disc_owned);OBSERVE(arc_disc_generation);OBSERVE(arc_disc_paths);OBSERVE(arc_disc_names);
 OBSERVE(arc_disc_names_valid);OBSERVE(arc_disc_notices);OBSERVE(arc_disc_notice_read);OBSERVE(arc_disc_notice_write);
#undef OBSERVE
 arc_sha256_final(&hash,output);
}
'''
        source.write_text(text,encoding='utf-8')
        cc,original_link=rebuild_commands(commands,original,'arcdroid_rpcroute_fault_libretro',build,os.name=='nt')
        obj=temp/(original.stem+'.o');old=cc[cc.index('-o')+1];objects[old]=str(obj)
        cc[cc.index('-o')+1]=str(obj);cc[cc.index('-c')+1]=str(source)
        cc=[arg for arg in cc if not arg.startswith('-DARC_BUILD_SUBSTITUTIONS=')]
        cc+=['-DARC_BUILD_SUBSTITUTIONS="authored-catalogue,test-faults,test-plans,read-only-position,unit-clock"']
        if '-MF' in cc:cc[cc.index('-MF')+1]=str(temp/(original.stem+'.d'))
        r=subprocess.run(cc,cwd=build,capture_output=True,text=True,timeout=90);assert r.returncode==0,r.stderr
        link=original_link
    library=temp/('route-matrix.dll' if os.name=='nt' else 'route-matrix.dylib' if sys.platform=='darwin' else 'route-matrix.so')
    link=[next((new for old,new in objects.items() if x.lower().endswith(('.o','.obj')) and same_path(x,old,build,os.name=='nt')),x) for x in link]
    link[link.index('-o')+1]=str(library)
    link=['-Wl,--out-implib,'+str(temp/'route-matrix.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
    r=subprocess.run(link,cwd=build,capture_output=True,text=True,timeout=90);assert r.returncode==0,r.stderr
    # Every probe substitution is explicit; the same distribution classifier
    # must reject this executable surface even though it is temporary.
    manifest=temp/'manifest.json';manifest.write_text(json.dumps({'targets':[{'file':library.name,'target':'rpc-route-matrix','gate_open':1,'authored':1,'test_faults':1,'test_plans':1,'substitutions':'authored-catalogue,test-faults,test-plans,read-only-position,unit-clock'}]}))
    stage=temp/'stage';stage.mkdir();shutil.copy2(library,stage/library.name)
    for name in ('COPYING','THIRD_PARTY_NOTICES.md'):shutil.copy2(root/name,stage/name)
    (stage/'SOURCE.json').write_text(json.dumps({'revision':'0'*40,'url':'https://github.com/sayenah/ArcDroid/tree/'+'0'*40,'archive':'https://github.com/sayenah/ArcDroid/archive/'+'0'*40+'.zip'}))
    spec=importlib.util.spec_from_file_location('route_classifier',root/'tools/check-rpc-artifacts.py');classifier=importlib.util.module_from_spec(spec);spec.loader.exec_module(classifier)
    try:classifier.classify(stage,manifest)
    except ValueError as error:assert 'ordinary-route-capable' in str(error)
    else:raise AssertionError('test probe core was distributable')
    secondary=temp/('secondary'+library.suffix);shutil.copy2(library,secondary)
    identity=subprocess.run([exe,library,fixtures/'system',temp,fixtures/'route.jfd','metadata-fault',secondary],capture_output=True,text=True,timeout=30)
    assert identity.returncode==0,identity.stdout+identity.stderr
    if sys.argv[6:]==['identity']:
        print('ordinary matrix: every probe/clock/catalogue/fault substitution identified and distribution refused');sys.exit(0)
    baseline=None
    for state in (map(int,sys.argv[6:]) if sys.argv[6:] else range(12)):
        for action in ('reset','unload','deinit'):
            work=temp/f'{state}-{action}';system=work/'system';shutil.copytree(fixtures/'system',system);save=work/'save';save.mkdir()
            shutil.copy2(legacy/'arcdroid-test.rom',system/'arcdroid/firmware/control.rom')
            shutil.copy2(legacy/'arcdroid-test.adf',system/'arc-control.adf')
            run=subprocess.run([exe,library,system,save,fixtures/'route.jfd',f'matrix:{state}:{action}',secondary],capture_output=True,text=True,timeout=120)
            assert run.returncode==0,(state,action,run.stdout,run.stderr)
            trace=run.stdout.split('arculator-trace=')[1].splitlines()[0]
            if baseline is None:baseline=trace
            assert trace==baseline,(state,action,trace,baseline)
        print(f'ordinary RPC state={state}: setters, black/silent pending frames, reset/unload/deinit and fresh Arculator trace passed',flush=True)

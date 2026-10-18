"""Real retro_* matrix with read-only probes and the injectable clock.
Probes live only in temporary test objects; production interfaces are unchanged.
GPL-2.0-only.
"""
from pathlib import Path
import json,os,shutil,struct,subprocess,sys,tempfile
from rpc_ninja_commands import rebuild_commands,same_path
from rpc_boot_packages_test import inputs
from rpc_cmos_factory_rom import generate,factory
build,root,exe,fixtures=map(lambda p:Path(p).resolve(),sys.argv[1:5])
cache=(build/'CMakeCache.txt').read_text();ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[r['command'] for r in json.loads(subprocess.check_output([ninja,'-C',str(build),'-t','compdb','-x'],text=True))]
with tempfile.TemporaryDirectory(prefix='arc-host-matrix-') as temporary:
    temp=Path(temporary);objects={};link=None
    for relative in ('libretro/rpc_preparation.c','tests/arc_test_plan.c','libretro/arcdroid_libretro.c'):
        original=root/relative;source=temp/original.name;text=original.read_text()
        if relative.endswith('/rpc_preparation.c'):
            text+='''
/* Read-only position probe; no state or I/O mutation. */
RETRO_API void arc_riscpc_test_host_position(const ArcRpcPreparation *p,unsigned *state,unsigned *substep)
{*state=p?p->state:ARC_RPC_PREP_CANCELLED;*substep=p?p->substep:0;}
uint64_t arc_riscpc_test_unit_clock(void *opaque)
{(void)opaque;static uint64_t value;return value+=UINT64_C(66666665);}
'''
        elif relative.endswith('/arc_test_plan.c'):
            anchor='void arc_test_plan_set_host_ops(const ArcRpcFrontendOps *ops){host_ops=*ops;}'
            assert text.count(anchor)==1
            text=text.replace(anchor,'''extern uint64_t arc_riscpc_test_unit_clock(void *);
void arc_test_plan_set_host_ops(const ArcRpcFrontendOps *ops){host_ops=*ops;host_ops.module.clock=arc_riscpc_test_unit_clock;}''')
            text+='''
extern void arc_riscpc_test_host_position(const ArcRpcPreparation *,unsigned *,unsigned *);
RETRO_API void arc_test_host_position(unsigned *state,unsigned *substep)
{arc_riscpc_test_host_position(preparation,state,substep);}
RETRO_API void arc_test_host_attempt_fingerprint(uint8_t output[32])
{ArcRpcLaunchAttempt attempt={0};arc_riscpc_preparation_attempt(preparation,&attempt);
 ArcSha256 hash;arc_sha256_init(&hash);arc_sha256_update(&hash,&attempt,sizeof(attempt));arc_sha256_final(&hash,output);}
'''
        else:
            text+='''
RETRO_API void arc_test_host_pending_fingerprint(uint8_t output[32])
{
 ArcSha256 hash;arc_sha256_init(&hash);
#define OBSERVE(value) arc_sha256_update(&hash,&value,sizeof(value))
 OBSERVE(arc_inputs);OBSERVE(arc_keyboard_raw);OBSERVE(arc_pad_keys);
 OBSERVE(arc_media_count);OBSERVE(arc_image_index);OBSERVE(arc_ejected);
 OBSERVE(arc_disc_owned);OBSERVE(arc_disc_previous_index);OBSERVE(arc_disc_swapped_us);
 OBSERVE(arc_disc_generation);OBSERVE(arc_disc_paths);OBSERVE(arc_disc_names);
 OBSERVE(arc_disc_names_valid);OBSERVE(arc_disc_notices);OBSERVE(arc_disc_notice_read);OBSERVE(arc_disc_notice_write);
 OBSERVE(arc_rpc_refusal_pending);OBSERVE(arc_rpc_refusal_noticed);OBSERVE(arc_rpc_storage_noticed);
 ArcDiscRequest disc={0};arc_engine_disc_snapshot(&disc);OBSERVE(disc);
 ArcErrorBox error={0};arc_engine_error_snapshot(&error);OBSERVE(error);
 ArcLaunchOutcome launch={0};arc_engine_launch_outcome_snapshot(&launch);OBSERVE(launch);
#undef OBSERVE
 arc_sha256_final(&hash,output);
}
'''
        source.write_text(text)
        cc,original_link=rebuild_commands(commands,original,'arcdroid_rpcplan_libretro',build,os.name=='nt')
        obj=temp/(original.stem+'.o');old=cc[cc.index('-o')+1];objects[old]=str(obj)
        cc[cc.index('-o')+1]=str(obj);cc[cc.index('-c')+1]=str(source)
        if '-MF' in cc:cc[cc.index('-MF')+1]=str(temp/(original.stem+'.d'))
        r=subprocess.run(cc,cwd=build,capture_output=True,text=True,timeout=90);assert r.returncode==0,r.stderr
        link=original_link
    library=temp/('host.dll' if os.name=='nt' else 'host.dylib' if sys.platform=='darwin' else 'host.so')
    link=[next((new for old,new in objects.items() if x.lower().endswith(('.o','.obj')) and same_path(x,old,build,os.name=='nt')),x) for x in link]
    link[link.index('-o')+1]=str(library)
    link=['-Wl,--out-implib,'+str(temp/'host.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
    r=subprocess.run(link,cwd=build,capture_output=True,text=True,timeout=90);assert r.returncode==0,r.stderr
    system=temp/'system';roms=generate(system);fw=system/'arcdroid/firmware';fw.mkdir(parents=True)
    shutil.copyfile(fixtures/'arcdroid-test.rom',fw/'authored.data');shutil.copyfile(fixtures/'arcdroid-test.adf',system/'ordinary.adf')
    shutil.copyfile(fixtures/'jfd-writes/rw-d.jfd',system/'pending.jfd')
    shutil.copyfile(fixtures/'arcdroid-test.adf',system/'helper.adf')
    packages=inputs(temp/'packages');shutil.copytree(packages/'arcdroid/riscpc',system/'arcdroid/riscpc')
    words=factory('full').words;idle=max(i for i,w in enumerate(words) if w==0xeafffffe);words[idle]=0xee0f0f58
    rom=system/'idle.rom';rom.write_bytes(struct.pack('<%dI'%len(words),*words)+struct.pack('<I',0xeafffffe)*(524288-len(words)))
    cases=[(state,0 if state in (5,8,9) else 4294967295,'boot') for state in range(11)]+[(5,1,'boot')]+[(8,sub,'blank') for sub in (1,2,3)]+[(9,sub,'boot') for sub in (1,2,3)]+[(11,4294967295,'failed')]
    for state,substep,profile in cases:
        save=temp/f'save-{state}-{substep}';save.mkdir();plan=temp/f'{state}-{substep}.arcplan'
        text=f'version=2\nmodel=sa110\nfirmware={rom}\ncmos=persist\ncmos_layout=ro370\ncontent_id={"e"*40}\nrtc_epoch=0\nrate_mips=1\nhard_disc=on\n'
        text+=f'media={system/"pending.jfd"}\ndrive1={system/"helper.adf"}\n'
        if profile!='blank':text+='hd_populate=on\n'
        if profile=='failed':
            missing=temp/'missing';missing.mkdir();text=text.replace(str(rom),str(rom));sysroot=missing
        else:sysroot=system
        plan.write_text(text)
        # Failure still needs ordinary follow-up firmware; remove only packages.
        if profile=='failed':
            shutil.copytree(system/'arcdroid/firmware',sysroot/'arcdroid/firmware');shutil.copyfile(system/'ordinary.adf',sysroot/'ordinary.adf')
        for action in ('reset','unload','deinit'):
            action_save=save/action;action_save.mkdir()
            r=subprocess.run([str(exe),str(library),str(plan),str(sysroot),str(action_save),f'matrix:{state}:{substep}:{action}'],capture_output=True,text=True,timeout=240)
            assert r.returncode==0,(state,substep,action,r.stdout,r.stderr)
        print(f'retro_* state={state} substep={substep if substep!=4294967295 else "none"}: setter/input isolation, black/audio, reset/unload/deinit, then clean Arculator passed',flush=True)

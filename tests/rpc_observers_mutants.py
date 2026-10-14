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
frontend=len(sys.argv)>3
core=Path(sys.argv[3]).resolve() if frontend else None
cache=(build/'CMakeCache.txt').read_text()
ninja=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands=[x['command'] for x in json.loads(subprocess.check_output([ninja,'-t','compdb','-x'],cwd=build,text=True))]
mutants=[
 ('production-table','engine/rpc/rpc_observer.c','ARC_RPC_OBS_ERROR,ARC_RPC_CAP_OBSERVE,true,error','ARC_RPC_OBS_ERROR,ARC_RPC_CAP_OBSERVE,false,error'),
 ('production-reason','engine/rpc/rpc_observer.c','awaiting R3c-3 launch hook','awaiting a different hook'),
 ('production-duplicate-row','engine/rpc/rpc_observer.c','PAIR(4,3,ERROR_EVIDENCE,DISC_A_EVIDENCE)\n};','PAIR(4,3,ERROR_EVIDENCE,DISC_A_EVIDENCE),PAIR(1,0,ERROR_EVIDENCE,DISC_A_EVIDENCE)\n};'),
 ('production-extra-row','engine/rpc/rpc_observer.c','PAIR(4,3,ERROR_EVIDENCE,DISC_A_EVIDENCE)\n};','PAIR(4,3,ERROR_EVIDENCE,DISC_A_EVIDENCE),PAIR(100,0,ERROR_EVIDENCE,DISC_A_EVIDENCE)\n};'),
 ('production-error-number','engine/rpc/rpc_observers.c','!arc_rpc_inspect_word(e->registers[0],false,&number) || number!=0','false'),
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
if frontend:
    mutants=[
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
    subprocess.run([sys.executable,str(root/'tests/rpc_observers_frontend_test.py'),str(target),str(core),str(build/"tests/fixtures/generated")],check=True,cwd=build,timeout=60,capture_output=True)
else:
    subprocess.run([str(target)],check=True,cwd=build,timeout=30,capture_output=True)
for name,relative,old,new in mutants:
    source=root/relative;text=source.read_text()
    edits=list(zip(old,new)) if isinstance(old,tuple) else [(old,new)]
    for before,after in edits:
        if text.count(before)!=1:raise RuntimeError((name,'anchor',text.count(before)))
        text=text.replace(before,after)
    compile,link=rebuild_commands(commands,source,'arcdroid_rpcplan_libretro' if frontend else 'rpc_observers_test',build,os.name=='nt')
    with tempfile.TemporaryDirectory(prefix='arc-rpc-observers-mutant-') as folder:
        folder=Path(folder);copy=folder/source.name;obj=folder/'mutant.o';binary=folder/(core.name if frontend else target.name)
        copy.write_text(text)
        original_obj=compile[compile.index('-o')+1];compile[compile.index('-o')+1]=str(obj);compile[compile.index('-c')+1]=str(copy)
        compile+=['-I',str(source.parent)];link=[str(obj) if x==original_obj else x for x in link];link[link.index('-o')+1]=str(binary)
        for command in (compile,link):
            result=subprocess.run(command,cwd=build,capture_output=True,text=True,timeout=60)
            if result.returncode:raise RuntimeError((name,'build failure',result.stderr))
        argv=[sys.executable,str(root/'tests/rpc_observers_frontend_test.py'),str(target),str(binary),str(build/"tests/fixtures/generated")] if frontend else [str(binary)]
        result=subprocess.run(argv,cwd=build,capture_output=True,text=True,timeout=60)
        if not result.returncode and not frontend:
            result=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_observer_qualification_test.py'),str(binary),str(root/'docs/riscpc-observer-qualification.md')],cwd=build,capture_output=True,text=True,timeout=30)
        if not result.returncode:raise RuntimeError((name,'SURVIVED'))
        print(name+': killed',flush=True)
print(f'{len(mutants)} RPC observer mutants killed; no build failures counted')

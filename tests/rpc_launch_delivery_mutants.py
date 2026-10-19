"""Mutate the real delivery, publication and reset modules. GPL-2.0-only."""
from pathlib import Path
import json, os, subprocess, sys, tempfile
from rpc_ninja_commands import rebuild_commands, split_command, same_path

build, root, fixtures = map(lambda x: Path(x).resolve(), sys.argv[1:4])
ninja = next(s.split('=', 1)[1] for s in (build/'CMakeCache.txt').read_text().splitlines()
             if s.startswith('CMAKE_MAKE_PROGRAM:FILEPATH='))
commands = [r['command'] for r in json.loads(subprocess.check_output(
    [ninja, '-C', str(build), '-t', 'compdb', '-x'], text=True))]
rows = [split_command(s.strip(), os.name == 'nt') for c in commands
        for s in c.split('&&') if ' -o ' in s]

groups = [
 ('tests/arc_test_plan.c', 'arcdroid_rpcplan_libretro', 'plan', [
  ('jfd-decoded-view', 'arc_launch_discover(NULL,0,NULL,&jfd,&discovery)', 'arc_launch_discover(raw,size,&info,&jfd,&discovery)'),
 ]),
 ('core/rpc_launch_attempt.c', 'rpc_launch_test', ['attempt', str(fixtures)], [
  ('expiry-retains-delivery', 'a->outcome=ARC_RPC_OUTCOME_UNKNOWN;a->capabilities=0;', 'a->delivery=ARC_RPC_DELIVERY_UNKNOWN;a->outcome=ARC_RPC_OUTCOME_UNKNOWN;a->capabilities=0;'),
  ('expiry-disables-cells', 'a->outcome=ARC_RPC_OUTCOME_UNKNOWN;a->capabilities=0;', 'a->outcome=ARC_RPC_OUTCOME_UNKNOWN;'),
  ('prompt-loss-transition', 'case ARC_RPC_EVENT_WAIT_UNOBSERVED:', 'case 100: '),
  ('expiry-phase', 'if(a->delivery!=ARC_RPC_DELIVERY_PREPARED)return ARC_RPC_TRANSITION_REFUSED;', 'if(false)return ARC_RPC_TRANSITION_REFUSED;'),
 ]),
 ('libretro/rpc_launch_record_store.c', 'rpc_launch_record_store_test', [], [
  ('remove-before-recovery', '++s->namespace_operations;if(!arc_io_remove(s->path))return fail(s);s->phase=', '++s->namespace_operations;if(false)return fail(s);s->phase='),
  ('temporary-removal', 'if(!arc_io_remove(s->temporary))', 'if(false)'),
  ('record-size', 'n==ARC_RPC_BOOT_RECORD_BYTES &&', 'n>=0 &&'),
  ('publication-revalidation', 'if(!valid)', 'if(false)'),
  ('write-result', 'arc_io_write_file(s->file,s->bytes,sizeof(s->bytes))==sizeof(s->bytes)', '(arc_io_write_file(s->file,s->bytes,sizeof(s->bytes)),true)'),
  ('flush-result', 'bool flushed=arc_io_flush_file(s->file);', 'bool flushed=(arc_io_flush_file(s->file),true);'),
  ('rename-result', '!arc_io_rename(s->temporary,s->path)', '(arc_io_rename(s->temporary,s->path),false)'),
  ('discard-candidate', 'if(s)s->have_old=false;', 'if(s)s->have_old=true;'),
 ]),
 ('core/rpc_launch_controller.c', 'rpc_launch_controller_test', [], [
  ('session-binding', '!t->session ||', ''),
  ('epoch-binding', '!t->epoch ||', ''),
  ('serial-binding', '!t->serial || t->serial-1>UINT64_MAX-t->epoch ||', ''),
  ('wire-epoch-overflow', 't->serial-1>UINT64_MAX-t->epoch ||', ''),
  ('plan-binding', 'memcmp(digest,t->plan,32)', 'false'),
 ]),
 ('core/rpc_launch_controller.c', 'rpc_launch_controller_test', 'guest', [
  ('guest-consumption', 'notify(&a,ARC_RPC_STATUS_CONSUME);', ''),
  ('guest-latch', 'word(&a,0xef020024);', 'word(&a,0xef020023);'),
  ('guest-close', 'word(&a,0xef0600dd);', 'word(&a,0xef020006);'),
  ('guest-latch-error', 'word(&a,0xef020024);branch(&a,L_REJECT,6,false);', 'word(&a,0xef020024);'),
  ('guest-registration-error', 'word(&a,0xef0600c0);branch(&a,L_REJECT,6,false);', 'word(&a,0xef0600c0);'),
  ('guest-dispatch-error', 'word(&a,0xef0600de);branch(&a,L_REJECT,6,false);', 'word(&a,0xef0600de);'),
  ('guest-ramfs-read-error', 'word(&a,0xef02005c);branch(&a,L_REJECT,6,false);', 'word(&a,0xef02005c);'),
  ('guest-ramfs-create-error', 'word(&a,0xef02002a);branch(&a,L_REJECT,6,false);', 'word(&a,0xef02002a);'),
  ('guest-close-error', 'word(&a,0xef0600dd);branch(&a,L_FATAL,6,false);', 'word(&a,0xef0600dd);'),
  ('guest-plan-compare', 'branch(&a,L_REJECT,1,false);', 'word(&a,0xe1a00000);'),
 ]),
 ('core/launch_discovery.c', 'rpc_launch_product_test', [], [
  ('product-parent', 'arc_le24(tail)!=parent', 'false'),
  ('product-name', 'return arc_name_ascii_equal(&actual,&entry->name);', 'return true;'),
  ('product-run-allocation', 'arc_rpc_run_valid(fs,&child[item])', 'true'),
 ]),
 ('engine/rpcemu/src/arm.c', 'rpc_orchestration_test', None, [
  ('retirement-instruction-yield','if(arc_rpc_observer_boundary_pending)count=(unsigned)linecyc+1;', '(void)0;'),
 ]),
 ('engine/rpc/rpc_session.c', 'arcdroid_rpcplan_libretro', 'frame', [
  ('frame-skips-boundary', 'arc_rpc_advance(16666667u, true)', 'arc_rpc_advance_internal(16666667u, true)'),
  ('session-batch-break', 'if (completed || arc_rpc_observer_boundary_pending) break;', 'if (completed) break;'),
 ]),
 ('core/rpc_launch_helper.c', 'rpc_launch_controller_test', [], [
  ('carrier-oldid', 'out[507]=0x45;', ''),
 ]),
 ('engine/rpc/rpc_observers.c', 'rpc_orchestration_test', None, [
  ('abort-owner-notification', 'if(old.aborted)old.aborted(old.opaque,reason);', ''),
 ]),
 ('libretro/rpc_preparation.c', 'rpc_orchestration_test', None, [
  ('skip-retirement','if(!p->retirement_pending)return true;', 'return true;'),
  ('retirement-before-consume','p->state=ARC_RPC_PREP_RUNNING;p->execution_barrier=false;', 'p->state=ARC_RPC_PREP_RUNNING;p->execution_barrier=false;launch_consumed(p);(void)launch_boundary(p);'),
  ('reset-keeps-controller','p->generated_helper && arc_riscpc_helper_build(p->generated_helper,ARC_RPC_HELPER_SIZE)', 'p->generated_helper'),
  ('reset-noop-ready-skipped','arc_riscpc_attempt_noop_ready(&p->attempt,true)', 'true'),
  ('inject-retirement-build','!arc_riscpc_helper_build(p->generated_helper,ARC_RPC_HELPER_SIZE) ||', 'true ||'),
  ('inject-retirement-mount','!arc_engine_rpc_mount(1,&p->helper,p->error,sizeof(p->error))', 'true'),
  ('rollback-snapshot-order','p->execution_barrier=true;p->launch_suppressed=true;p->launch_captured=false;', 'p->execution_barrier=true;p->launch_suppressed=true;p->launch_captured=false;arc_engine_stop();'),
  ('reset-dead-engine', 'if(!arc_engine_active() || !arc_engine_is_rpc()){', 'if(false){'),
  ('rollback-owner','if(reason!=ARC_RPC_INTERRUPT_SWAP_ROLLBACK)return;', 'return;'),
  ('reset-suppression', 'p->execution_barrier=true;p->launch_suppressed=true;\n    arc_riscpc_preparation_interrupt', 'p->execution_barrier=true;\n    arc_riscpc_preparation_interrupt'),
  ('reset-interruption', 'arc_riscpc_preparation_interrupt(p,ARC_RPC_INTERRUPT_RESET);', ''),
  ('reset-drive0-result', 'p->host.restage_drive0 && p->host.restage_drive0(p->host.opaque,p->error,sizeof(p->error))', '(p->host.restage_drive0(p->host.opaque,p->error,sizeof(p->error)),true)'),
  ('reset-rebind-result', 'p->host.reset_rebind && p->host.reset_rebind(p->host.opaque,p->error,sizeof(p->error))', '(p->host.reset_rebind(p->host.opaque,p->error,sizeof(p->error)),true)'),
  ('reset-barrier', 'else {p->retirement_pending=false;p->execution_barrier=false;}', 'else {p->retirement_pending=false;p->execution_barrier=true;}'),
 ]),
]

with tempfile.TemporaryDirectory(prefix='arc-launch-mutants-') as temp:
 temp = Path(temp)
 for relative, target, args, mutants in groups:
  source = root/relative
  test_source = root/'tests/arc_test_plan.c' if args=='frame' else source if args=='plan' else root/'tests'/('rpc_launch_test.c' if target == 'rpc_launch_test' else target+'.c')
  _, link = rebuild_commands(commands, test_source, target, build, os.name == 'nt')
  compile = next(r.copy() for r in rows if '-c' in r and
                 same_path(r[r.index('-c')+1], source, build, os.name == 'nt') and
                 (relative.startswith('core/') or any(same_path(x,r[r.index('-o')+1],build,os.name=='nt') for x in link if x.endswith(('.o','.obj')))))
  original = source.read_text(); old_obj = compile[compile.index('-o')+1]
  obj = temp/'mutant.o'; c = temp/source.name; exe = temp/('mutant.dll' if args in ('plan','frame') and os.name=='nt' else 'mutant.exe' if os.name == 'nt' else 'mutant')
  compile[compile.index('-c')+1] = str(c); compile[compile.index('-o')+1] = str(obj)
  compile += ['-I', str(source.parent)]
  if '-MF' in compile: compile[compile.index('-MF')+1] = str(temp/'mutant.d')
  direct = any(same_path(x, old_obj, build, os.name == 'nt') for x in link if x.endswith(('.o','.obj')))
  if direct: link = [str(obj) if same_path(x, old_obj, build, os.name == 'nt') else x for x in link]
  else: link.insert(link.index('-o'), str(obj))
  link[link.index('-o')+1] = str(exe)
  link = ['-Wl,--out-implib,'+str(temp/'mutant.dll.a') if x.startswith('-Wl,--out-implib,') else x for x in link]
  for label, old, new in [('baseline', None, None), *mutants]:
   text = original
   if old:
    if text.count(old) != 1: raise RuntimeError('anchor changed: '+label)
    text = text.replace(old, new)
   c.write_text(text)
   subprocess.run(compile, cwd=build, check=True, capture_output=True)
   subprocess.run(link, cwd=build, check=True, capture_output=True)
   extension='.dll' if os.name=='nt' else '.dylib' if sys.platform=='darwin' else '.so'
   command = [sys.executable,'-B',str(root/'tests/rpc_boot_plan_test.py'),str(build/('rpc_cmos_plan_test.exe' if os.name=='nt' else 'rpc_cmos_plan_test')),str(exe),str(build/('rpc_runner.exe' if os.name=='nt' else 'rpc_runner')),str(build/('arcdroid_libretro'+extension)),str(root/'tests/rpc_ide_rom.py'),str(fixtures)] if args=='plan' else [sys.executable,'-B',str(root/'tests/rpc_launch_controller_guest.py'),str(build/'rpc_runner'),str(build/('arcdroid_rpcplan_libretro'+extension)),str(exe)] if args=='guest' else [str(exe), *args] if args is not None else [sys.executable, '-B', str(root/'tests/rpc_orchestration_test.py'), str(exe), str(fixtures), 'launch']
   if args=='frame':
    command = [sys.executable, '-B', str(root/'tests/rpc_launch_frame_test.py'),
     str(build/('rpc_runner.exe' if os.name=='nt' else 'rpc_runner')), str(exe),
     str(build/('rpc_launch_controller_test.exe' if os.name=='nt' else 'rpc_launch_controller_test')),
     str(build/('rpc_launch_helper_test.exe' if os.name=='nt' else 'rpc_launch_helper_test'))]
   injected=label.startswith('inject-retirement-')
   if injected:command[-1]='retirement-failure'
   result = subprocess.run(command, cwd=build, capture_output=True, text=True, timeout=180)
   if (label == 'baseline' or injected) and result.returncode: raise RuntimeError('baseline '+relative+': '+result.stderr[-1500:])
   if label != 'baseline' and not injected and not result.returncode: raise RuntimeError('survived: '+label)
   print(relative, label, 'injected failure passed' if injected else 'passed' if label == 'baseline' else 'killed', flush=True)

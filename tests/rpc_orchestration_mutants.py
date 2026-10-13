"""Real production module mutations; fresh authored inputs per mutant. GPL-2.0-only."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
from rpc_ninja_commands import split_command
build,root,fixtures=sys.argv[1:];build=Path(build).resolve();root=Path(root).resolve()
source=root/'libretro/rpc_preparation.c'
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','rpc_orchestration_test'],text=True).splitlines()
compile=next(split_command(c,os.name=='nt') for c in commands if str(source) in c and 'rpc_orchestration_test.dir' in c and ' -c ' in c)
link=split_command(commands[-1],os.name=='nt');link=link[link.index('&&')+1:link.index('&&',2)]
mutants=[
 ('validation-before-candidate','arc_riscpc_boot_validation_complete(p->boot)','true'),
 ('requested-before-population','if(p->needs_population){p->state=ARC_RPC_PREP_POPULATE;p->substep=0;}','if(p->needs_population){p->state=ARC_RPC_PREP_START;p->substep=0;}'),
 ('generation-isolated-factory','if(!start(p,true))','if(!start(p,false))'),
 ('reset-recovery','release(p);restart(p);return true;','release(p);return true;'),
 ('final-attachment','(!p->host.attach||p->host.attach(p->host.opaque,p->error,sizeof(p->error)))','true'),
 ('authoritative-record','memcpy(p->seed+16,p->record.payload,240);after_cmos(p);break;','memset(p->seed+16,0,240);after_cmos(p);break;'),
 ('static-reservation','uint64_t charged=faulty?(used>reserved?used:reserved):used;','uint64_t charged=used>reserved?used:reserved;'),
 ('offline-ordinary-budget','uint64_t budget=generation?ARC_RPC_GENERATION_BUDGET_NS:ARC_RPC_PREP_OFFLINE_BUDGET_NS;',
  'uint64_t budget=generation?ARC_RPC_GENERATION_BUDGET_NS:ns||attachment?ARC_RPC_PREP_OFFLINE_BUDGET_NS:UINT64_C(3333333);'),
 ('offline-namespace-budget','uint64_t budget=generation?ARC_RPC_GENERATION_BUDGET_NS:ARC_RPC_PREP_OFFLINE_BUDGET_NS;',
  'uint64_t budget=generation?ARC_RPC_GENERATION_BUDGET_NS:ns||attachment?UINT64_C(8333333):ARC_RPC_PREP_OFFLINE_BUDGET_NS;'),
 ('generation-offline-budget','uint64_t budget=generation?ARC_RPC_GENERATION_BUDGET_NS:ARC_RPC_PREP_OFFLINE_BUDGET_NS;',
  'uint64_t budget=ARC_RPC_PREP_OFFLINE_BUDGET_NS;'),
 ('ordinary-ceiling','ns||attachment?ARC_RPC_PREP_NAMESPACE_UNIT_NS:ARC_RPC_PREP_UNIT_NS;',
  'ns||attachment?ARC_RPC_PREP_NAMESPACE_UNIT_NS:UINT64_C(100000);'),
 ('namespace-ceiling','ns||attachment?ARC_RPC_PREP_NAMESPACE_UNIT_NS:ARC_RPC_PREP_UNIT_NS;',
  'ns||attachment?ARC_RPC_PREP_OFFLINE_BUDGET_NS:ARC_RPC_PREP_UNIT_NS;'),
 ('live-clock','    if(p->disc){arc_riscpc_hd_tick(p->disc);',
  '    p->host.clock(p->host.opaque);\n    if(p->disc){arc_riscpc_hd_tick(p->disc);'),
 ('live-batching','    if(p->disc){arc_riscpc_hd_tick(p->disc);',
  '    if(p->disc){arc_riscpc_hd_tick(p->disc);arc_riscpc_hd_tick(p->disc);'),
 ('namespace-declaration','ns=next_namespace(p)','ns=false'),
 ('notice-terminal-precedence','arc_engine_stop();p->state=ARC_RPC_PREP_FAILED;\n    notice(p,"",false);notice(p,p->error,true);','arc_engine_stop();p->state=ARC_RPC_PREP_FAILED;\n    notice(p,"Preparing the RiscPC hard disc",false);'),
 ('refused-cmos-precedence','if(p->record.refused){notice(p,"",false);notice(p,p->error,true);','if(p->record.refused){notice(p,p->error,true);'),
 ('newer-refusal','o==ARC_RPC_HD_UNSUPPORTED||',''),
 ('damaged-refusal','o==ARC_RPC_HD_CORRUPT||',''),
 ('unavailable-refusal','||o==ARC_RPC_HD_UNAVAILABLE',''),
 ('legacy-refusal','p->request.populate && o==ARC_RPC_HD_LEGACY','false'),
 ('reset-clear','    notice(p,"",false);\n    if(p->state==ARC_RPC_PREP_RUNNING)','    if(p->state==ARC_RPC_PREP_RUNNING)'),
 ('unload-clear','notice(p,"",false);release(p);free(p->firmware);free(p);return ok;','release(p);free(p->firmware);free(p);return ok;'),
 ('saved-rtc-prefix','memset(p->seed,0,16);memcpy(p->seed+16,p->record.payload,240);after_cmos(p);break;}','memcpy(p->seed+16,p->record.payload,240);after_cmos(p);break;}'),
 ('requested-epoch','factory?ARC_RPC_CMOS_GENERATION_EPOCH:p->request.epoch','ARC_RPC_CMOS_GENERATION_EPOCH'),
 ('quantized-clock','if(!used)faulty=true;','faulty=true;'),
 ('faulty-clock-reservation','uint64_t charged=faulty?(used>reserved?used:reserved):used;','uint64_t charged=used;'),
 ('step-cap','p->stats.steps<ARC_RPC_PREP_STEPS','true'),
 ('spike-ends-batch','((attempts||advance) && actual>cost) || actual>ceiling','actual>ceiling'),
 ('generation-estimate','advance?ARC_RPC_GENERATION_UNIT_NS:','advance?UINT64_C(8333333):'),
 ('attachment-declaration','attachment=p->state==ARC_RPC_PREP_START && p->substep==2','attachment=false'),
 ('attachment-census','p->stats.attachment_operations=io.operations','p->stats.attachment_operations=0'),
 ('refresh-unit-count','reserved+=ceiling;refresh(p,false);','reserved+=ceiling;refresh(p,true);'),
 ('notice-refresh','p->notice_frames=60;','p->notice_frames=600;'),
]
original=source.read_text()
with tempfile.TemporaryDirectory(prefix='arcdroid-orchestration-mutants-') as temp:
    temp=Path(temp);c=temp/'rpc_preparation.c';obj=temp/'rpc_preparation.o';exe=temp/('mutant.exe' if os.name=='nt' else 'mutant')
    command=compile.copy();old_obj=command[command.index('-o')+1];command[command.index('-o')+1]=str(obj)
    command[command.index('-c')+1]=str(c)
    if '-MF' in command:command[command.index('-MF')+1]=str(temp/'mutant.d')
    link=[str(obj) if item==old_obj else item for item in link];link[link.index('-o')+1]=str(exe)
    for label,old,new in [('baseline',None,None),*mutants]:
        text=original
        if old:
            if text.count(old)!=1:raise RuntimeError('anchor changed: '+label)
            text=text.replace(old,new)
        c.write_text(text)
        subprocess.run(command,cwd=build,check=True,capture_output=True,timeout=60)
        subprocess.run(link,cwd=build,check=True,capture_output=True,timeout=60)
        result=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_orchestration_test.py'),str(exe),fixtures,'mutant'],capture_output=True,text=True,timeout=60)
        if label=='baseline':
            if result.returncode:raise RuntimeError('baseline: '+result.stderr[-2000:])
        elif not result.returncode:raise RuntimeError('survived: '+label)
        else:print('orchestration mutant killed: '+label,flush=True)

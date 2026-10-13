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
 ('final-attachment','(p->host.attach&&!p->host.attach(p->host.opaque,p->error,sizeof(p->error)))','false'),
 ('authoritative-record','memcpy(p->seed+16,p->record.payload,240);after_cmos(p);break;','memset(p->seed+16,0,240);after_cmos(p);break;'),
 ('clock-reservation','uint64_t charged=used>reserved?used:reserved;','uint64_t charged=used;'),
 ('namespace-declaration','bool ns=next_namespace(p);','bool ns=false;'),
 ('notice-terminal-precedence','notice(p,"",false);notice(p,p->error,true);','notice(p,"Preparing the RiscPC hard disc",false);'),
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

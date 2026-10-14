#!/usr/bin/env python3
"""Mutation proof for descriptor admission, no-op and actual vector body. GPL-2.0-only."""
from pathlib import Path
import subprocess,sys,tempfile
compiler,runner,core,root=sys.argv[1:5];root=Path(root).resolve()
source=(root/'core/rpc_launch_helper.c').read_text()
mutants={'magic':('memcmp(p,"ARCRHLP1",8)','false'),
    **{f'field-{at}':(f'get(p+{at})!=1','false') for at in (8,20,24,32,36,40)},
    'header':('get(p+12)!=64','false'),'length':('get(p+16)!=n','false'),
    **{f'reserved-{at}':(f'get(p+{at})','false') for at in (52,56,60)},
    'entry-min':('e<64','false'),'entry-max':('e>n-4','false'),'entry-alignment':('(e&3)','false'),
    'plan-start':('at<e+4','false'),'plan-max':('at>n','false'),
    'plan-alignment':('(at&3)','false'),'plan-length':('bytes>n-at','false'),
    'action':('0x03500000','0xe1a00000'),'plan-size':('0xe3530008','0xe3530004')}
gate=(root/'assets/riscpc-boot/ArcGate').read_text()
vector_mutants={'filesystem':('CMP R1,#8','CMP R1,#7'),
    'first-miss':('CMP R0,#1','CMP R0,#5'),'unknown-media':('160 CMP R0,#2','160 CMP R0,#5'),
    'cancel-result':('MVNEQ R0,#0','MOVEQ R0,#0'),
    'cancel-intercept':('210 LDMEQFD R13!,{PC}','210 MOVS PC,R14'),
    'cleanup-upcall':('CMP R0,#4','CMP R0,#5'),
    'replacement':('CMP R0,#256','CMP R0,#255')}
with tempfile.TemporaryDirectory(prefix='arc-launch-mutants-') as tmp:
    tmp=Path(tmp);file=tmp/'helper.c';exe=tmp/'helper';gfile=tmp/'ArcGate'
    for name,old,new in [('baseline',None,None),*[(n,*m) for n,m in mutants.items()]]:
        if old is not None:assert source.count(old)==1,(name,'anchor')
        file.write_text(source if old is None else source.replace(old,new))
        subprocess.run([compiler,'-std=c11','-O2','-UNDEBUG','-I'+str(root/'core'),str(root/'tests/rpc_launch_helper_test.c'),str(file),'-o',str(exe)],check=True,capture_output=True,timeout=30)
        r=subprocess.run([str(exe)],capture_output=True,timeout=15)
        if name=='baseline':assert r.returncode==0;continue
        if r.returncode==0:
            r=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_launch_helper_guest_test.py'),runner,core,str(exe)],capture_output=True,timeout=30)
        assert r.returncode!=0,(name,'survived')
        print('RPC launch mutant killed: '+name,flush=True)
    for name,(old,new) in vector_mutants.items():
        assert gate.count(old)==1,(name,'anchor');gfile.write_text(gate.replace(old,new))
        r=subprocess.run([sys.executable,'-B',str(root/'tests/rpc_gate_vector_test.py'),runner,core,str(gfile)],capture_output=True,timeout=30)
        assert r.returncode!=0,(name,'survived')
        assert b'Traceback' in r.stderr and b'AssertionError' in r.stderr,(name,'invalid mutant')
        print('RPC gate vector mutant killed: '+name,flush=True)

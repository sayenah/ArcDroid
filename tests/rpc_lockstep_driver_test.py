#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""CLI input identity and structured refusals before process orchestration."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import rpc_lockstep_run as driver

with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-driver-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
    root=Path(tmp);plan=root/'input.arcplan';plan.write_text('version=1\nmodel=sa110\n', encoding='utf-8')
    common=['driver','--baseline-runner','runner-b','--baseline-core','core-b',
        '--candidate-runner','runner-c','--candidate-core','core-c','--system',str(root),'--plan',str(plan)]
    def invoke(arguments):
        output=io.StringIO()
        with patch.object(sys,'argv',arguments),redirect_stdout(output):code=driver.main()
        return code,json.loads(output.getvalue())
    with patch.object(driver,'run_pair',lambda args:dict(ok=True,model=args.model)):
        code,result=invoke(common)
        assert code==0 and result['model']==2,('default header ignored the plan model',result)
        code,result=invoke(common+['--model','1'])
        assert code==0 and result['model']==1,('explicit model override lost',result)
    contexts=[]
    def mismatch(args,detail=None):
        contexts.append(detail)
        return dict(ok=False,key=[4,9,5,0,0,0],field='fast')
    with patch.object(driver,'run_pair',mismatch),patch.object(driver.compare,'reproduced',lambda *args:True):
        code,result=invoke(common)
        assert code==1 and contexts==[None,[4,9,5,0,0,0]],'detail rerun lost generation/epoch context'
    saves=[]
    class Process:
        returncode=0
        def __init__(self,command,**kwargs):
            save=Path(command[command.index('--save')+1]);assert save.is_dir();saves.append(save)
        def poll(self):return 0
        def wait(self,**kwargs):return 0
    with patch.object(driver.subprocess,'Popen',Process),patch.object(driver,'Pipe',lambda *args:io.BytesIO()),patch.object(driver.compare,'compare',lambda *args:dict(ok=True)):
        code,result=invoke(common)
        assert code==0 and len(saves)==2 and saves[0]!=saves[1] and saves[0].parent==saves[1].parent,'peers shared a mutable save directory'
    with patch.object(driver,'run_pair',lambda args:dict(ok=True,model=args.model)):
        plan.unlink()
        code,result=invoke(common+['--hash-only'])
        assert code==2 and len(result['refused'])==64 and str(root) not in json.dumps(result),'missing input escaped structured hash-only refusal'
print('driver: plan-derived model, explicit override, isolated save roots and missing-input hash-only refusal passed')

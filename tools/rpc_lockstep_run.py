#!/usr/bin/env python3
"""Run two explicit runner/core pairs over bounded v1 streams. GPL-2.0-only."""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode=True
import rpc_lockstep_compare as compare


class Pipe:
    """An owned FIFO reader with a deadline, including failed-open children."""
    def __init__(self,path,process,deadline):
        self.fd=os.open(path,os.O_RDONLY|os.O_NONBLOCK)
        self.process,self.deadline,self.started=process,deadline,False
    def read(self,n):
        while time.monotonic()<self.deadline:
            ready,_,_=select.select([self.fd],[],[],0.05)
            if ready:
                data=os.read(self.fd,n)
                if data:self.started=True;return data
                if self.started or self.process.poll() is not None:return b''
                time.sleep(0.01)
            elif self.process.poll() is not None:
                data=os.read(self.fd,n)
                if data:self.started=True;return data
                return b''
        raise TimeoutError('lockstep runner deadline')
    def close(self):os.close(self.fd)


def stop(process):
    # This Popen handle is the sole ownership authority; never use name scans.
    if process.poll() is None:
        process.terminate()
        try:process.wait(timeout=2)
        except subprocess.TimeoutExpired:process.kill();process.wait(timeout=2)


def run_pair(args,detail=None):
    schema,wire_hash=compare.load_schema(args.schema)
    scratch='/private/tmp' if Path('/private/tmp').is_dir() else None
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-run-',dir=scratch) as tmp,ExitStack() as stack:
        root=Path(tmp);processes=[];handles=[]
        deadline=time.monotonic()+args.timeout
        try:
            for side in ('baseline','candidate'):
                save=root/side;save.mkdir()
                stream=root/(side+'.stream')
                if os.name!='nt':os.mkfifo(stream,0o600)
                stdout=stack.enter_context((root/(side+'.stdout')).open('w+b'))
                stderr=stack.enter_context((root/(side+'.stderr')).open('w+b'))
                runner=getattr(args,side+'_runner');core=getattr(args,side+'_core')
                plan=args.candidate_plan if side=='candidate' and args.candidate_plan else args.plan
                command=[str(runner),'--core',str(core),'--plan',str(plan),
                    '--system',str(args.system),'--save',str(save),'--frames',str(args.frames),
                    '--lockstep-stream',str(stream),'--lockstep-role',side,
                    '--lockstep-mode',args.mode,'--lockstep-model',str(args.model),
                    '--lockstep-density',args.density,'--lockstep-p1',args.p1,
                    '--lockstep-seed',str(args.seed),'--lockstep-plan-hash',args.plan_hash,
                    '--lockstep-schedule-hash',args.schedule_hash,
                    '--lockstep-configuration-hash',args.configuration_hash,
                    '--lockstep-max-bytes',str(args.max_bytes)]
                if detail is not None:
                    generation,epoch,k=detail[:3]
                    command+=['--lockstep-detail',f'{k}:{k}','--lockstep-stop-after',str(k),
                        '--lockstep-key-context',f'{generation}:{epoch}']
                command+=args.runner_arg
                process=subprocess.Popen(command,stdout=stdout,stderr=stderr)
                processes.append(process)
                if os.name!='nt':
                    handle=Pipe(stream,process,deadline);stack.callback(handle.close);handles.append(handle)
            if os.name=='nt':
                for process in processes:process.wait(timeout=max(0.01,deadline-time.monotonic()))
                for side in ('baseline','candidate'):
                    path=root/(side+'.stream')
                    if path.stat().st_size>args.max_bytes:raise ValueError('bounded stream file exceeded')
                    handles.append(stack.enter_context(path.open('rb')))
            result=compare.compare(*handles,schema,wire_hash,args.mode,True)
            # A failed comparison may leave writers waiting on full pipes.
            if not result['ok']:
                for process in processes:stop(process)
            else:
                for process in processes:process.wait(timeout=max(0.01,deadline-time.monotonic()))
                if any(p.returncode not in (0,1) for p in processes):raise ValueError('runner refused/failed')
                outputs=[]
                for side in ('baseline','candidate'):
                    outputs.append(compare.stdout_projection((root/(side+'.stdout')).read_text(encoding='utf-8'),args.mode,schema))
                if outputs[0]!=outputs[1]:
                    result=dict(ok=False,field='runner.stdout',baseline=compare.digest(outputs[0]),candidate=compare.digest(outputs[1]))
            result['runner_returncodes']=[p.returncode for p in processes]
            return result
        finally:
            for process in processes:stop(process)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for side in ('baseline','candidate'):
        p.add_argument('--'+side+'-runner',type=Path,required=True)
        p.add_argument('--'+side+'-core',type=Path,required=True)
    p.add_argument('--plan',type=Path,required=True);p.add_argument('--candidate-plan',type=Path)
    p.add_argument('--system',type=Path,required=True)
    p.add_argument('--schema',type=Path,default=compare.DEFAULT_SCHEMA)
    p.add_argument('--mode',choices=compare.MODES,default='strict')
    p.add_argument('--density',choices=('every-instruction','full-audit'),default='every-instruction')
    p.add_argument('--p1',choices=('on','off'),default='on')
    p.add_argument('--model',type=int,choices=range(4),help='header model; defaults to the plan model')
    p.add_argument('--seed',type=int,default=1);p.add_argument('--frames',type=int,default=1)
    p.add_argument('--plan-hash');p.add_argument('--schedule-hash',default='00'*32)
    p.add_argument('--configuration-hash',default='00'*32)
    p.add_argument('--timeout',type=float,default=180)
    p.add_argument('--max-bytes',type=int,default=512*1024*1024)
    p.add_argument('--runner-arg',action='append',default=[])
    p.add_argument('--hash-only',action='store_true')
    a=p.parse_args()
    try:
        plan_bytes=a.plan.read_bytes()
        if not a.plan_hash:a.plan_hash=hashlib.sha256(plan_bytes).hexdigest()
        if a.model is None:
            models=[value.strip() for line in plan_bytes.decode('utf-8').splitlines()
                for key,separator,value in [line.partition('=')] if separator and key.strip()=='model']
            if len(models)!=1 or models[0] not in ('arm610','arm710','sa110','arm7500'):
                raise ValueError('plan has no unique supported model; provide --model explicitly')
            a.model=('arm610','arm710','sa110','arm7500').index(models[0])
        result=run_pair(a)
        if not result['ok'] and result.get('key'):
            rerun=run_pair(a,result['key'])
            if compare.reproduced(result,rerun):
                rerun['original_field']=result['field'];result=rerun
            else:result=dict(ok=False,field='non-deterministic',original=result,rerun_result=rerun)
        if a.hash_only:result={k:v if k in ('ok','max_buffered_records') else compare.digest(v) for k,v in result.items()}
        print(json.dumps(result,sort_keys=True));return 0 if result['ok'] else 1
    except (ValueError,OSError,TimeoutError,subprocess.TimeoutExpired) as error:
        print(json.dumps(dict(ok=False,refused=compare.digest(str(error)) if a.hash_only else str(error))));return 2


if __name__=='__main__':sys.exit(main())

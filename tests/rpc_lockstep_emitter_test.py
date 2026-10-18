#!/usr/bin/env python3
"""Real emitter boundary, accounting and stream checks. GPL-2.0-only."""
import argparse
import collections
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import rpc_lockstep_compare as c
from rpc_timing_rom import Guest


def require(ok,message):
    if not ok:raise RuntimeError(message)


def program():
    g=Guest();g.literal(0,0x70);g.emit(0xee010f10)
    g.literal(0,0xd3);g.emit(0xe121f000)
    g.store(0x10040000,0x2a);g.literal(0,0x10040000)
    g.emit(0xe5902000,0xee112f10,0xef000000)
    require(g.steps==17,('independent attempt count',g.steps))
    data=struct.pack('<'+'I'*len(g.words),*g.words)
    return data+bytes(2097152-len(data))


def records(path,schema,sh,mode='strict'):
    with path.open('rb') as f:
        r=c.Reader(f,schema,sh,mode)
        while (record:=r.next()) is not None:yield record


def main():
    p=argparse.ArgumentParser();p.add_argument('loader');p.add_argument('core');p.add_argument('fixtures',type=Path)
    p.add_argument('--section',choices=('all','points','projection','fatal','audit','observers','lifecycle','sink','encoding','media','allocations','reach','null-batch'),default='all')
    a=p.parse_args();schema,sh=c.load_schema();start=time.monotonic()
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2b-emitter-',dir='/private/tmp' if Path('/private/tmp').is_dir() else None) as tmp:
        root=Path(tmp);rom=root/'points.rom';rom.write_bytes(program());counter=0
        # Event chronology and timestamps remain independently checked across
        # quotas in addition to complete checkpoint comparison below.
        event_guest=Guest();event_guest.literal(0,0x70);event_guest.emit(0xee010f10)
        event_guest.store(0x10040000,0x2a)
        event_guest.literal(0,0x03000000);event_guest.emit(0xe5901000)
        event_guest.literal(0,0x03310000);event_guest.emit(0xe5801000)
        event_rom=root/'event-times.rom'
        payload=struct.pack('<'+'I'*len(event_guest.words),*event_guest.words)
        event_rom.write_bytes(payload+bytes(2097152-len(payload)))
        def run(model=0,flags=0,quota=200,shift=0,inject=0,inject_k=0,rom_path=rom,operation='end:17',duration=100000,stream=True,detail=None,frames=0):
            nonlocal counter
            path=root/(str(counter)+'.stream');counter+=1
            args=[a.loader,a.core,str(rom_path),str(model),str(path) if stream else 'off',
                str(duration),str(flags),str(inject),str(inject_k),str(shift),str(quota),operation]
            if detail:args+=list(map(str,detail))
            elif frames:args+=['-','-']
            if frames:args+=['0',str(frames)]
            r=subprocess.run(args,capture_output=True,text=True,timeout=120, encoding='utf-8')
            require(r.returncode==0,(args,r.returncode,r.stderr))
            return path,r.stdout
        def compare(left,right,mode='strict'):
            with left.open('rb') as l,right.open('rb') as r:result=c.compare(l,r,schema,sh,mode)
            require(result['ok'],result)
        def without_p1(path):
            return [(r.kind,r.key,r.value) for r in records(path,schema,sh)
                if not(r.kind in (1,2) and r.key[3]==1)]
        table={}
        for model in (range(4) if a.section in ('all','points') else ()):
            left,stdout=run(model,1);right,other=run(model)
            require(stdout==other,'two-process stdout');compare(left,right)
            counts=collections.Counter();edges=collections.Counter();attempts=0
            for r in records(right,schema,sh):
                if r.kind in (1,2):
                    if r.key[3]==0:attempts+=1
                    if r.key[3]==1:counts[r.key[4]]+=1;edges[(r.key[4],r.edge)]+=1
            expected={1:17,2:12,3:2,4:1,5:1,6:1,7:1,8:1}
            require(attempts==17 and dict(counts)==expected,('P1 independently stated points',model,attempts,dict(counts)))
            require(edges[(2,1)]==edges[(2,2)]==6 and edges[(3,1)]==edges[(3,2)]==1,'paired accessor edges')
            table[model]=dict(counts)
            off,off_stdout=run(model,8)
            require(stdout==off_stdout,'P1-off guest effect')
            require(without_p1(right)==without_p1(off),'P1-off changed non-P1 evidence')
            shifted,shift_stdout=run(model,0,shift=7)
            require(stdout==shift_stdout,'shift changed counted allocation semantics');compare(left,shifted)
            _,plain=run(model,stream=False)
            require(stdout==plain,'sink attachment changed guest behaviour')
            event_vectors=[]
            for quota in (1,7,200):
                event_path,_=run(model,flags=1<<8,quota=quota,rom_path=event_rom,operation=f'end:{event_guest.steps}')
                event_vectors.append([r for r in records(event_path,schema,sh,'cross-quota') if r.kind==3])
            require({r.value[9] for r in event_vectors[0]}=={0,1} and
                all(r.value[8]==0 for r in event_vectors[0]),'cross-quota CPU RAM/MMIO event coverage')
            require(event_vectors[0]==event_vectors[1]==event_vectors[2],
                ('cross-quota physical event/time disagreement',model,event_vectors))
        # Pure P0/P1/P2 projection at the required quota schedules.
        if a.section in ('all','encoding'):
            compact,_=run(flags=1);detail,_=run(flags=16)
            left=records(compact,schema,sh);right=records(detail,schema,sh)
            for x,y in zip(left,right,strict=True):
                require(x.key==y.key and x.edge==y.edge,'detail/compact identity')
                if y.kind==1:
                    require(x.kind==2 and x.value==c.compact_groups(y,schema,'strict'),'cached C compact hash differs from independent detail recurrence')
                else:require((x.kind,x.value)==(y.kind,y.value),'detail/compact non-checkpoint evidence')
        if a.section in ('all','projection'):
            for model in range(4):
                left,_=run(model,flags=1|(1<<8))
                for quota in (1,7,200):
                    right,_=run(model,flags=1<<8,quota=quota);compare(left,right,'cross-quota')
                # Inspect actual detail values at every point, using the
                # stream's attempt identity as an independent expected count.
                detail,_=run(model,flags=16|(1<<8),quota=7)
                for record in records(detail,schema,sh,'cross-quota'):
                    if record.kind!=1:continue
                    expected=record.key[2]+int(record.key[3] in (2,3))
                    require(record.value['arm.inscount']==expected,
                        ('R-M2b-O3 actual completion projection',model,record.key))
                # Strict C detail must retain the real batch-return counter
                # and requested quota, rather than applying mode (b)'s rules.
                for quota in (1,200):
                    strict,_=run(model,quota=quota,detail=(1,1))
                    point=next(r for r in records(strict,schema,sh)
                        if r.kind==1 and r.key[2:4]==(1,0))
                    require(point.value['rpc_session.arc_rpc_test_quota']==quota and
                        point.value['arm.inscount']==int(quota==1),'strict raw quota/inscount lost')
                reset,_=run(model,flags=1<<8,quota=7,operation='reset-run',duration=5000,detail=(0,0))
                starts=[r for r in records(reset,schema,sh,'cross-quota')
                    if r.kind==1 and r.key[2:4]==(0,0)]
                require(len(starts)==2 and all(r.value['arm.inscount']==0 for r in starts),
                    'R-M2b-O3 completion projection did not reset with the CPU')
        if a.section in ('all','observers'):
            for model in range(4):
                left,plain=run(model,flags=1|(2<<8))
                for operation in ('unmatched','trace','active'):
                    right,out=run(model,flags=2<<8,operation=operation)
                    require(out==plain,('non-completing observer guest effect',model,operation));compare(left,right,'guest-output')
                off,_=run(model,flags=1|(3<<8));trace,_=run(model,flags=3<<8,operation='trace')
                compare(off,trace,'trace-toggle')
                consumed,completed_stdout=run(model,flags=2<<8,operation='consume')
                require(completed_stdout!=plain,'SWI completion intended divergence missing')
                rr=list(records(consumed,schema,sh,'guest-output'))
                require(sum(r.kind in (1,2) and r.key[3:5]==(1,4) for r in rr)==1,'observer-completed SWI missing P1')
                with left.open('rb') as l,consumed.open('rb') as r:divergence=c.compare(l,r,schema,sh,'guest-output')
                require(not divergence['ok'],'completion was treated as an equality configuration')
        # The first due issue is reached exactly at the advance deadline.
        # The driver therefore uses its existing arc_rpc_arm_exec_batch(1,NULL)
        # path (G-1), whose work counter was booked before admission.
        if a.section in ('all','null-batch'):
            for model,first in enumerate((50,29,63,29)):
                left,plain=run(model,flags=17,duration=first,operation='none')
                right,out=run(model,flags=16,duration=first,operation='none')
                require(plain==out and f'now={first} executed=1 ' in out,'NULL-batch first-issue boundary')
                compare(left,right)
                points=[r for r in records(right,schema,sh) if r.kind==1 and r.key[3] in (0,2)]
                require([r.key[3] for r in points]==[0,2],'NULL-batch P0/P2 missing')
                require([r.value['driver.instruction_work'] for r in points]==[0,1],
                    'NULL-batch booked work was counted twice')
                require(all(not r.value['fast.integer_batch'] for r in points),'NULL-batch rational path lost')
        # Independent shadow audit over all memory, with full P3 page evidence.
        if a.section in ('all','audit'):
            _,plain=run(stream=False);audit,audit_stdout=run(flags=4)
            require(audit_stdout==plain,'audit guest effect')
            audit_pages=collections.defaultdict(set)
            for r in records(audit,schema,sh):
                if r.kind==4 and r.value[0]==0 and r.value[2]:audit_pages[r.value[1]].add(r.value[2:4])
            complete={(allocation,page) for allocation,count in ((2,8192),(3,8192),(4,512)) for page in range(count)}
            require(audit_pages and all(pages==complete for pages in audit_pages.values()),'full-audit P3 must contain every RAM/VRAM page')
        # Actual bad-mode fatal and the three separately labelled injected paths.
        for model in (range(4) if a.section in ('all','fatal') else ()):
            fatal,_=run(model,rom_path=a.fixtures/f'fatal-mode-{model}.rom',operation='none')
            fatal_records=list(records(fatal,schema,sh));terminal=[r for r in fatal_records if r.kind==5]
            require(len(terminal)==1 and terminal[0].key==(1,2,11,5,1,0),'bad-mode ruled fatal key')
            require(any(r.kind==3 and r.value[9]==0 and r.value[10]==0x10040000 and r.value[13]==0xface for r in fatal_records),'committed fatal RAM log')
            expected=bytearray(4096);struct.pack_into('<I',expected,0,0xface)
            require(any(r.kind==4 and r.value[:1]==(4,) and r.value[2:4]==(2,64) and r.value[4]==hashlib.sha256(expected).hexdigest() for r in fatal_records),'committed fatal RAM page')
            off,_=run(model,flags=8,rom_path=a.fixtures/f'fatal-mode-{model}.rom',operation='none')
            require(without_p1(fatal)==without_p1(off),'fatal evidence changed with P1 disabled')
        for injected in ((1,2,3) if a.section in ('all','fatal') else ()):
            path,_=run(inject=injected,inject_k=12,quota=1,operation='none')
            rr=list(records(path,schema,sh));require(sum(r.kind==5 for r in rr)==1,('injected fatal missing',injected))
            fatal_key=next(r.key for r in rr if r.kind==5)
            require(fatal_key==((1,2,12,5,3,1) if injected==3 else (1,2,12,5,1,0)),('injected ruled fatal key',injected,fatal_key))
            off,_=run(inject=injected,inject_k=12,quota=1,operation='none',flags=8)
            require(without_p1(path)==without_p1(off),('injected fatal P1 density',injected))

            require(any(r.kind==3 and r.value[9]==0 and r.value[10]==0x10040000 and r.value[13]==0x2a for r in rr),('injected fatal committed log',injected))
            expected=bytearray(4096);struct.pack_into('<I',expected,0,0x2a)
            require(any(r.kind==4 and r.value[0]==4 and r.value[2:4]==(2,64) and r.value[4]==hashlib.sha256(expected).hexdigest() for r in rr),('injected fatal committed page',injected))
        for operation in (('reset','failed-start') if a.section in ('all','fatal') else ()):
            path,_=run(operation=operation,duration=0)
            rr=list(records(path,schema,sh));require(any(r.kind==4 and r.value[0]==3 for r in rr),('post-transition missing',operation))
        if a.section in ('all','lifecycle'):
            for operation in ('none','reset','failed-start','rate','stop','idle-frames'):
                path,_=run(operation=operation,duration=0,flags=16)
                rr=list(records(path,schema,sh))
                post=[r for r in rr if r.kind==4 and r.value[0]==3 and r.value[2]==0]
                expected=2 if operation in ('reset','rate','stop') else 1
                require(len(post)==expected,('outer lifecycle latch',operation,len(post)))
                require([r.key[1] for r in post]==list(range(2,2+expected)),('epoch bump count',operation))
                require(all(r.key[2]==0 for r in post),('epoch k reset',operation))
                if operation=='failed-start':require(all(r.key[0]==0 for r in post),'failed first start generation')
                if operation=='idle-frames':
                    boundaries=[r for r in rr if r.kind==4 and r.value[0]==1 and r.value[2]==0]
                    require(len(boundaries)==2 and boundaries[1].value[1]==boundaries[0].value[1]+1,'idle frame occurrence')
        if a.section in ('all','sink'):
            for operation,expected in (('fail-sink','lockstep sink error'),('reentrant','lockstep sink reentrancy')):
                path=root/(operation+'.stream')
                args=[a.loader,a.core,str(rom),'0',str(path),'0','0','0','0','0','200',operation]
                r=subprocess.run(args,capture_output=True,text=True,timeout=20, encoding='utf-8')
                require(r.returncode==1 and expected in r.stderr,('sink failure status',operation,r.returncode,r.stderr))
        if a.section in ('all','allocations'):
            for stage in range(1,5):
                path,out=run(operation=f'alloc:{stage}',duration=0)
                _,off=run(operation=f'alloc:{stage}',duration=0,stream=False)
                require(out==off and f'allocations={stage} live=0' in out,('allocation failure stage moved',stage,out,off))
                rr=list(records(path,schema,sh));require(any(r.kind==5 for r in rr),'allocation fatal missing')
                if stage<5:require(not any(r.kind==4 and r.value[2]!=0 for r in rr),'unprepared memory was read')
            g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
            for reg,value in ((0x1d8,0x10000000),(0x1dc,0x10000000),(0x1d4,0x10001000),(0x1e0,0x70)):
                g.store(0x03200000+reg,value)
            for value in (0x83000000,0x84000010,0x93000002,0x9400000a,0x80000038,0x9000000e,0xd0000000,0xe0000080):
                g.store(0x03400000,value)
            video=root/'allocation-video.rom';data=struct.pack('<'+'I'*len(g.words),*g.words);video.write_bytes(data+bytes(2097152-len(data)))
            path,out=run(operation='alloc:5',rom_path=video,duration=100000)
            _,off=run(operation='alloc:5',rom_path=video,duration=100000,stream=False)
            require(out==off and 'allocations=5' in out and 'fault=1' in out,('presentation allocation stage moved',out,off))
            list(records(path,schema,sh))
            left,out=run(operation='end:300',rom_path=video,flags=1)
            shifted,other=run(operation='end:300',rom_path=video,shift=7)
            require(out==other and 'allocations=5' in out,('presentation allocation shift',out,other));compare(left,shifted)
            path,_=run(operation='reattach',duration=0,shift=7,flags=4)
            list(records(path,schema,sh))
        if a.section in ('all','media'):
            for kind in ('raw','jfd'):
                left,out=run(operation=kind,flags=17)
                shifted,other=run(operation=kind,shift=7,flags=16)
                require(out==other,'media allocation shift guest effect');compare(left,shifted)
                changed,_=run(operation=kind+'-changed',flags=16)
                with left.open('rb') as l,changed.open('rb') as r:diff=c.compare(l,r,schema,sh)
                require(not diff['ok'] and diff['field'].startswith('fdc.media_contents['),('media contents missing',kind,diff))
                for path in (left,shifted,changed):path.unlink()
        if a.section in ('all','reach'):
            from rpc_timing_rom import boundary_guest
            from rpc_timing_oracle import cases as timing_cases
            evidence={}
            for model in range(4):
                spec=next(case for case in timing_cases() if case['model']==model and case['kind']=='buffer-retire' and case['k']==32)
                words,first,last=boundary_guest(spec)
                # Eight-word STMs fill the ARM6/7 word capacity and the SA's
                # eight packet slots. The original calibration uses STRs and
                # does not guarantee a full buffer at an observable boundary.
                words=[0x188901fe if word==0x14892200 else word for word in words]
                capacity_rom=root/f'buffer-capacity-{model}.rom';data=struct.pack('<'+'I'*len(words),*words);capacity_rom.write_bytes(data+bytes(2097152-len(data)))
                capacity_path,_=run(model,rom_path=capacity_rom,operation=f'end:{last+1}',duration=1000000,detail=(first-1,last+1))
                capacity=[r for r in records(capacity_path,schema,sh) if r.kind==1 and
                    (r.value['memory.bcount']==8 if model==2 else r.value['memory.bwords']==8)]
                require(capacity,('A-8 full write buffer not reached',model,first,last))
                capacity_key=capacity[0].key;capacity_path.unlink()
                path,_=run(model,rom_path=a.fixtures/f'g-8-{model}.rom',operation='end:50000',frames=2)
                # Mutate actual C-produced event frames, preserving their
                # shared sequence keys. RAM and MMIO have one chronology.
                raw=path.read_bytes();frames=[];offset=8
                while offset<len(raw):
                    kind,size=struct.unpack_from('<BI',raw,offset);end=offset+5+size
                    frames.append((kind,offset+5,end));offset=end
                ram=next((i for i,(kind,begin,end) in enumerate(frames) if kind==3 and struct.unpack_from('<Q',raw,begin+9*8)[0]==0),None)
                mmio=next((i for i,(kind,begin,end) in enumerate(frames) if kind==3 and struct.unpack_from('<Q',raw,begin+9*8)[0]==1),None)
                require(ram is not None and mmio is not None,('real cross-order events missing',model))
                swapped=bytearray(raw);_,left_begin,left_end=frames[ram];_,right_begin,right_end=frames[mmio]
                swapped[left_begin+7*8:left_end],swapped[right_begin+7*8:right_end]=raw[right_begin+7*8:right_end],raw[left_begin+7*8:left_end]
                shifted=bytearray(raw);struct.pack_into('<Q',shifted,left_begin+7*8,struct.unpack_from('<Q',raw,left_begin+7*8)[0]+1)
                baseline_raw=bytearray(raw)
                # Only the declared header role differs in this control.
                # Frame 0 begins after magic/envelope; flags follow 144 bytes.
                struct.pack_into('<I',baseline_raw,8+5+144,struct.unpack_from('<I',raw,8+5+144)[0]|1)
                require(c.compare(io.BytesIO(baseline_raw),io.BytesIO(raw),schema,sh)['ok'],'real event mutation no-op control')
                for label,data in (('RAM-MMIO-cross-order',swapped),('physical-time-shift',shifted)):
                    result=c.compare(io.BytesIO(baseline_raw),io.BytesIO(data),schema,sh)
                    require(not result['ok'] and result.get('field')=='event',('real event mutant escaped',model,label,result))
                events=[r for r in records(path,schema,sh) if r.kind==3 and r.value[9:10]==(1,) and r.value[10] in (0x03200080,0x0320003c) and r.value[12]==1]
                require(events,('timing-register event absent',model))
                k=events[0].key[2]
                detail,_=run(model,rom_path=a.fixtures/f'g-8-{model}.rom',operation='end:50000',frames=2,detail=(max(0,k-1),k+1))
                snapshots=[r for r in records(detail,schema,sh) if r.kind==1]
                pending=[r for r in snapshots if r.key[2]==k and r.key[3:5]==(1,3) and r.edge==2 and r.value['memory.count']>0]
                require(pending,('A-8 timing write without pending commands',model,k))
                evidence[str(model)]={'buffer_at_capacity':capacity_key,'timing_write_pending':pending[0].key,'commands':pending[0].value['memory.count']}
                path.unlink();detail.unlink()
                path,_=run(model,rom_path=a.fixtures/f'g-9-{model}.rom',operation='end:50000',frames=2)
                stores=[r for r in records(path,schema,sh) if r.kind==3 and r.value[9:11]==(0,0x10011004) and r.value[12]==1 and r.value[13]!=0xe3a03001]
                require(stores,('PC-next SMC event absent',model));store=stores[0];k=store.key[2]
                detail,_=run(model,rom_path=a.fixtures/f'g-9-{model}.rom',operation='end:50000',frames=2,detail=(k,k+1))
                snapshots=[r for r in records(detail,schema,sh) if r.kind==1]
                retained=[r for r in snapshots if r.key[2]==k and r.key[3]==2 and r.value['fast.opcode']==0xe50f1004]
                fetched=[r for r in snapshots if r.key[2:5]==(k+1,1,1) and r.value['fast.opcode']==store.value[13]]
                require(retained and fetched,('A-8 SMC fetch-state boundary',model,k))
                evidence[str(model)]['SMC_retained_fetch']=retained[0].key;evidence[str(model)]['SMC_next_fetch']=fetched[0].key
                path.unlink();detail.unlink()
            print('A-8 field witnesses '+json.dumps(evidence,sort_keys=True))
        print('P1 counts by model (logical entry/exit paired): '+json.dumps(table,sort_keys=True))
        print(f'emitter probes={counter} duration={time.monotonic()-start:.3f}s; budget fatals=injected; clock overflow=unexercised')


if __name__=='__main__':main()

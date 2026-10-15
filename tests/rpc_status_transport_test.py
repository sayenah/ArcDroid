#!/usr/bin/env python3
"""Exercise the passive ARCS decoder with authored RAM and an authored kernel.
Valid 64-bit bindings, every structural refusal, no ROM/IO copies, and exact
trace-off guest parity. Real ByteV coexistence is a private controller leg.
GPL-2.0-only.
"""
from pathlib import Path
import json,struct,subprocess,sys,tempfile
from rpc_video_rom import Guest
runner,core=sys.argv[1:3]
base=[0x53435241,1,64,0,0,0,0,0,1,1,123,456,0,0,0,0]
cases=[('diagnostic',base,None,None,True)]
attempt=base.copy();attempt[3:10]=[1,0,2,0,3,15,7]
cases.append(('attempt',attempt,None,None,True))
for at,value in ((0,0),(1,2),(2,60),(3,2),(4,1),(6,1),(8,0),(8,8),(9,0),*( (i,1) for i in range(12,16))):
    words=base.copy();words[at]=value;cases.append((f'field-{at}-{value}',words,None,None,False))
for at in (5,7):
    words=attempt.copy();words[at]=0;cases.append((f'zero-binding-{at}',words,None,None,False))
for reg,value in ((0,1),(1,0),(2,1),(4,0),(5,60),(6,1)):
    cases.append((f'register-{reg}',base,None,(reg,value),False))
for name,address in (('unaligned',0x10020001),('wrap',0xfffffff0),('rom',0),('io',0x03000000),('unmapped',0x80000000)):
    cases.append((name,base,address,None,False))
with tempfile.TemporaryDirectory(prefix='arc-status-transport-') as temp:
    root=Path(temp);(root/'seed').write_bytes(bytes(256))
    g=Guest();g.emit(0xea00003e,*([0xeafffffe]*63))
    g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
    g.literal(13,0x10030000);g.emit(0xe321f010);g.literal(13,0x10031000)
    for i,(name,words,address,override,valid) in enumerate(cases):
        for n,w in enumerate(words):g.write(0x10020000+4*n,w)
        args=[0,1,0,address if address is not None else 0x10020000,0x53435241,64,words[3]]
        if override:args[override[0]]=override[1]
        for reg,value in enumerate(args):g.literal(reg,value)
        g.emit(0xef020006);g.literal(4,0x10022000+4*i);g.emit(0xe5841000)
    g.emit(0xeafffffe);handler=len(g.w)*4;g.emit(0xe3a01006,0xe3a0202a,0xe1b0f00e)
    g.w[2]=0xea000000|((handler-16)//4)
    rom=bytearray(struct.pack('<I',0xeafffffe)*524288);rom[:len(g.w)*4]=struct.pack('<'+'I'*len(g.w),*g.w)
    (root/'authored.rom').write_bytes(rom)
    for model in ('arm610','sa110'):
        original=f'version=1\nmodel={model}\nfirmware={root/"authored.rom"}\ncmos=seed\nseed={root/"seed"}\nrtc_epoch=0\n'
        plan=root/'test.arcplan';results=[]
        for trace in (False,True,'authored'):
            plan.write_text(original+('observation_trace=status.jsonl\nobservation_swis=0x20006\n' if trace else '')+('observation_status=authored\n' if trace=='authored' else ''))
            r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(root),'--save',str(root),
                '--frames','1','--ram-offset',str(0x22000),'--ram-words',str(len(cases))],capture_output=True,text=True,timeout=15)
            assert r.returncode==0,(model,r.stderr)
            projection=[line for line in r.stdout.splitlines() if line.startswith(('frame=','regs=','video=','pcm=','ram='))]
            results.append(projection)
            if trace is True:all_rows=[json.loads(line) for line in (root/'status.jsonl').read_text().splitlines()]
            assert 'ram='+','.join(['00000006']*len(cases)) in r.stdout,(model,'query changed',r.stdout)
        assert results[0]==results[1]==results[2],(model,'passive trace changed guest')
        narrow_rows=[json.loads(line) for line in (root/'status.jsonl').read_text().splitlines()]
        expected_narrow=[row for row,case in zip(all_rows,cases) if case[0] not in ('register-0','register-1','register-2','register-4','register-5')]
        assert [{k:v for k,v in row.items() if k not in ('order','epoch')} for row in narrow_rows]==[{k:v for k,v in row.items() if k not in ('order','epoch')} for row in expected_narrow],(model,'authored selection changed status or retained untagged calls')
        rows=all_rows
        assert len(rows)==len(cases),(model,len(rows))
        for row,(name,words,address,override,valid) in zip(rows,cases):
            assert bool(row['status_valid'])==valid,(model,name,row)
            assert not row['path_hex'] and not any(row['registers'][3:13]),(name,'pointer or payload leaked')
            if valid:
                assert row['status']==dict(role=words[3],event=words[8],sequence=words[9],
                    token=words[4]|words[5]<<32,epoch=words[6]|words[7]<<32,detail0=words[10],detail1=words[11]),name
            else:assert not any(row['status'].values()),name
    print(f'ARCS transport: {len(cases)} cases on ARM610/SA-110, scalar-only validation and passive query parity')

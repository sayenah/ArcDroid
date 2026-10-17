#!/usr/bin/env python3
"""Execute the gate's actual inline vector body with authored firmware. GPL-2.0-only."""
from pathlib import Path
import struct,subprocess,sys,tempfile
from rpc_video_rom import Guest
runner,core,gate=sys.argv[1:4]
# Small test-side encoder for exactly the assembly dialect used by this gate.
# It consumes the source, so predicate/branch/restoration mutations affect code.
ops=[];labels={};inside=False
for line in Path(gate).read_text().splitlines():
    text=line.split(' ',1)[1]
    if text.startswith('[OPT'):inside=True;continue
    if text==']':break
    if not inside:continue
    if text.startswith('.'):
        labels[text[1:]]=len(ops);continue
    ops.append(text)
words=[]
for at,text in enumerate(ops):
    if text.startswith('CMP R'):
        reg,value=text[5:].split(',#');reg=int(reg);value=int(value)
        encoded=0x3500000|reg<<16
        for rotate in range(16):
            v=((value<<(2*rotate))|(value>>(32-2*rotate if rotate else 32)))&0xffffffff
            if v<256:encoded|=rotate<<8|v;break
        else:raise RuntimeError(text)
        words.append(0xe0000000|encoded)
    elif text.startswith(('BEQ ','BNE ')):
        op,label=text.split();words.append((0x0a000000 if op=='BEQ' else 0x1a000000)|((labels[label]-at-2)&0xffffff))
    else:
        words.append({'STMFD R13!,{R0}':0xe92d0001,'LDR R0,[R12]':0xe59c0000,
            'LDMFD R13!,{R0}':0xe8bd0001,'MOVEQ R0,#0':0x03a00000,'MVNEQ R0,#0':0x03e00000,
            'LDMEQFD R13!,{PC}':0x08bd8000,'MOVS PC,R14':0xe1b0f00e}[text])
with tempfile.TemporaryDirectory(prefix='arc-gate-vector-') as tmp:
    root=Path(tmp);(root/'seed').write_bytes(bytes(256))
    for model in ('arm610','sa110'):
        cases=((1,8,0xffffffff,True),(2,8,0xffffffff,True),
                (1,7,1,False),(2,7,2,False),(3,8,3,False),(4,8,4,True),
                (256,8,0,True),(257,8,257,False))
        for active,reason,fs,expected,intercept in ([(1,*c) for c in cases]+[(0,r,f,r,False) for r,f,_,_ in cases]):
            g=Guest();g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000,0xe161f000)
            for i,w in enumerate(words):g.write(0x10021000+i*4,w)
            # Residual handler/workspace outlive reused application storage.
            g.write(0x10023000,active);g.literal(12,0x10023000)
            if not active:
                for i in range(16):g.write(0x10024000+4*i,0xdeadbeef)
            g.write(0x10020200,0);stack_literal=len(g.w)-1
            g.literal(13,0x10020200);g.literal(0,reason);g.literal(1,fs);g.literal(3,0xffffffff)
            g.literal(4,0x10021000);g.literal(14,0);link_literal=len(g.w)-1;g.emit(0xe1a0f004)
            g.w[stack_literal]=g.w[link_literal]=len(g.w)*4
            g.literal(4,0x10020000);g.emit(0xe5840000,0xe584d004,0xe5841008,0xe584300c,0xeafffffe)
            rom=bytearray(struct.pack('<I',0xeafffffe)*524288);rom[:len(g.w)*4]=struct.pack('<'+'I'*len(g.w),*g.w)
            (root/'gate.rom').write_bytes(rom);plan=root/'test.arcplan'
            plan.write_text(f'version=1\nmodel={model}\nfirmware={root/"gate.rom"}\ncmos=seed\nseed={root/"seed"}\nrtc_epoch=0\n')
            r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--system',str(root),'--save',str(root),
                '--frames','1','--ram-offset','131072','--ram-words','4'],capture_output=True,text=True,timeout=15)
            expected_ram=f'ram={expected:08x},{0x10020200+4*intercept:08x},{fs:08x},ffffffff'
            assert r.returncode==0 and expected_ram in r.stdout,(model,reason,fs,r.stderr,r.stdout)
    print('Gate vector: first media miss cancelled, other filesystem passed, cleanup/replacement reasons, stack and register preservation pass')

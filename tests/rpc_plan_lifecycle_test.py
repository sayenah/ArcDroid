#!/usr/bin/env python3
"""Plan-route device, reset, rejection and contained-failure classes. GPL-2.0-only."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

runner,core,directory,mode=sys.argv[1:5]
directory=Path(directory).resolve()
def run(name,frames=2,extra=(),success=True,pacing='flat'):
    r=subprocess.run([runner,'--core',core,'--plan',str(directory/(name+'.arcplan')),
        '--system',str(directory),'--frames',str(frames),'--pacing',pacing,*extra],capture_output=True,text=True)
    assert (r.returncode==0)==success,r.stderr+r.stdout
    return r.stdout
def ram(text):return [int(w,16) for w in re.findall(r'^ram=(.*)$',text,re.M)[-1].split(',')]
if mode=='devices':
    canonical=[]
    for m in range(4):
        extra=['--ram-words','16']
        a=run(f'devices-{m}',extra=extra);b=run(f'devices-{m}',extra=extra,pacing='burst');assert a==b
        w=ram(a)
        assert w[0]>0 and w[1]&0x60==0x60 and w[2:9]==[0x40,5,0,0,0,0,0],w
        assert w[9:14]==[0,0xffff,0xffff,0xffffffff,0xa5],w
        assert w[14]==0xfa and w[15]==(0xfe if m==3 else 0xffffffff),w
        device_words=w
        a=run(f'fiq-{m}',extra=['--ram-offset','131072','--ram-words','2'])
        b=run(f'fiq-{m}',extra=['--ram-offset','131072','--ram-words','2'],pacing='burst');assert a==b
        assert ram(a)==[1,0xa5],a
        canonical.append(f'model={m} timer-irq={device_words[1]&0x60:02x} fiq-handler={ram(a)[0]} '
            +'fdc='+','.join(f'{x:02x}' for x in device_words[2:9])
            +' absent='+','.join(f'{x:08x}' for x in device_words[9:13])
            +f' keyboard={device_words[14]:02x} mouse={device_words[15]:08x}')
        for size in [2,4,8]:
            a=run(f'aperture-{m}-{size}',extra=['--ram-offset','131072','--ram-words','4'])
            assert ram(a)==[0x10+i%(size//2) for i in range(4)],a
    assert '\n'.join(canonical)+'\n'==Path(__file__).with_name('fixtures').joinpath('rpc-plan-devices.trace').read_text()
    print('plans: all models timer IRQ, guest FIQ handler, empty FDC, absent IDE/podule/EASI and pacing passed')
elif mode=='lifecycle':
    expected=bytes(i^0xa5 for i in range(256)).hex()
    for m in range(4):
        a=run(f'input-{m}',frames=4,extra=['--reset-at','2','--cmos'])
        assert re.findall(r'^cmos=(.*)$',a,re.M)==[expected]*4,'reset changed volatile seed'
        extra=['--reset-at','2','--ram-offset','131072','--ram-words','140']
        a=run(f'input-{m}',frames=9,extra=extra)
        b=run(f'input-{m}',frames=9,extra=extra,pacing='burst');assert a==b
        w=ram(a)
        assert w[0]==3 and w[64:67]==[0x1c,0xf0,0x1c],'reset lost restarted script keys'
        if m==3:assert w[1]==10 and w[128:138]==[0xfa,0x09,0,0,0x09,12,7,0x08,0,0],'reset lost script packets'
        else:assert w[2:4]==[12,7] and w[5]==0x70,'reset lost script mouse motion'
        for stage in range(1,4 if m==3 else 5):
            run(f'audio-{m}',extra=['--fail-alloc',str(stage)],success=False)
        # The fifth stage is the lazy presentation allocation in a real field.
        if m!=3:run(f'video-{m}-palette',extra=['--fail-alloc','5'],success=False)
        else:run(f'video-{m}-palette',extra=['--fail-alloc','4'],success=False)
        text=run(f'audio-{m}',frames=3,extra=['--fault-at','1'],success=False)
        assert 'fault=1' in text and 'discarded=0 queued=0' in text
    print('plans: declared seeds/reset, every allocation stage, protected-entry failures, post-fault audio passed')
elif mode=='factory':
    with tempfile.TemporaryDirectory(prefix='arc-authored-capture-') as temp:
        for m in [2,3]:
            seed=Path(temp)/f'{m}.seed'
            a=run(f'factory-{m}',frames=1900,extra=['--capture-seed',str(seed)])
            assert seed.read_bytes()==bytes(i^0x5a for i in range(256))
            assert 'capture writes=256 mask='+('ff'*32) in a
            # Re-admit the exact exported bytes as a seed, then frontend reset
            # must keep them. Plan parsing/capture never persists internally.
            plan=Path(temp)/'reload.arcplan'
            plan.write_text(f'version=1\nmodel={"sa110" if m==2 else "arm7500"}\nfirmware={directory / f"input-{m}.rom"}\ncmos=seed\nseed={seed}\nrtc_epoch=0\n')
            r=subprocess.run([runner,'--core',core,'--plan',str(plan),'--frames','3','--reset-at','1','--cmos'],capture_output=True,text=True)
            assert r.returncode==0,r.stderr
            assert re.findall(r'^cmos=(.*)$',r.stdout,re.M)==[seed.read_bytes().hex()]*3
            run(f'empty-factory-{m}',frames=1900,success=False)
    print('plans: explicit runner seed export, exact bytes/mask, no-write terminal containment passed')
else:raise AssertionError(mode)

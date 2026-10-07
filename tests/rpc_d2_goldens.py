#!/usr/bin/env python3
"""Independent timeline model for D2 goldens; never runs or reads the core.
Virtual source edges hold the next DMA word; 48 kHz edges sample that hold.
MMIO after tied source/output edges. All clocks are exact Fractions.
"""
from fractions import Fraction
from pathlib import Path
from rpc_d2_rom import audio, RATES, PCM

def ceil(value):return -(-value.numerator//value.denominator)
def expected(model,compat=False,end=10000000):
    g=audio(RATES[model],compat)
    actions=[(ceil(Fraction(n*1000,RATES[model])),kind,value) for n,kind,value in g.actions]
    enabled=False;frequency=control=0;source=None;source_origin=0;source_index=0
    ready=[False,False];bank=0;cache=[];held=0;frames=blocks=0;pcm=[]
    output_index=1;action_index=0
    def period():return Fraction(1000000000*(frequency+2),250000 if control&1 else 176400)
    while True:
        src=ceil(source) if enabled and source is not None else end+1
        out=ceil(Fraction(output_index*1000000000,48000))
        mmio=actions[action_index][0] if action_index<len(actions) else end+1
        now=min(src,out,mmio)
        if now>end:break
        if src==now:
            if not cache and ready[bank]:
                cache=list(PCM);ready[bank]=False;bank^=1;blocks+=1
            if cache:
                held=cache.pop(0);frames+=1
                if not(control&2):held=0
            else:held=0
            source_index+=1;source=source_origin+source_index*period()
        if out==now:
            pcm.append(held);output_index+=1
        if mmio==now:
            _,kind,value=actions[action_index];action_index+=1
            if kind[0]=='control':
                if value&128:ready=[False,False];bank=0;cache=[];held=0
                if enabled!=bool(value&32):
                    enabled=bool(value&32);source_origin=now;source_index=1;source=now+period()
                    if not enabled:held=0
            elif kind[0]=='pointer':
                if kind[1]&1:ready[kind[1]//2]=True
            else:
                newfreq,newctrl=kind[1:]
                if (newfreq,newctrl)!=(frequency,control):
                    frequency,control=newfreq,newctrl
                    source_origin=now;source_index=1;source=now+period()
                if not control&2:held=0
    return pcm,frames,blocks

def fnv(words):
    h=14695981039346656037
    for w in words:
        for shift in range(0,32,8):h=((h^((w>>shift)&255))*1099511628211)&((1<<64)-1)
    return h

def trace(model):
    lines=[]
    for compat in [False,True]:
        pcm,frames,blocks=expected(model,compat)
        lines.append(f'{"compat" if compat else "pcm"} model={model} generated=480 accepted=480 discarded=0 queued=0 dma={frames}/{blocks} hash={fnv(pcm):016x}')
    lines.append('dma-state=06,06,02,07,07,03,06,02,00,06,02,00,07,03,06,07 quads=8 pairs=32 wrap=4k')
    lines.append('pressure generated=2400 accepted=1066 discarded=1334 queued=0 recovery=800')
    pcm,_,_=expected(model,end=50000001)
    lines.append(f'frontend generated=2400 accepted=2400 hash={fnv(pcm):016x}')
    lines.append('overflow generated=9600 discarded=1408 queued=8192 recovered=8192')
    lines.append('fault silence=960 frozen=1 purge=1 reset=1 stop=1')
    lines.append('keyboard make=1c repeat=1c,1c break=f0,1c shared=held reset=released')
    lines.append('typematic param=00 make=1c repeat=1c,1c,1c,1c,1c,1c,1c,1c break=f0,1c')
    lines.append('pad space=29,f0,29 mouse='+('ps2=fa,09,00,00,09,0c,07,08,00,00' if model==3 else 'quadrature=12,7 buttons=30,70'))
    lines.append('buttons all=09,0b,0f retire=0e,0c,08 motion=-130,130 split=63,63,4')
    lines.append('frontend-input remap=29,f0,29,1c shared=held keyboard=f0,1c mouse='+('fa,08,0c,07,09,00,00,08,00,00' if model==3 else '12,7,30,70'))
    return '\n'.join(lines)+'\n'

def main():
    root=Path(__file__).parent
    for m in range(4):(root/f'fixtures/rpc-d2-{m}.trace').write_text(trace(m))
    for m in [2,3]:
        (root/f'fixtures/rpc-factory-{m}.trace').write_text(f'factory model={m} time=30000000000 delete=0 writes=256 mask='+('ff'*32)+'\nbytes='+bytes(i^0x5a for i in range(256)).hex()+'\nempty time=30000000000 fault=1 writes=0\n')
if __name__=='__main__':main()

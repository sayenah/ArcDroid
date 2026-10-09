#!/usr/bin/env python3
"""Authored D-139 calibration guests. GPL-2.0-only; no firmware inputs.
The separate arithmetic oracle consumes declared case parameters only.
"""
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from rpc_timing_oracle import cases, duration

class Guest:
    def __init__(self):
        self.words = [0xeafffffe]*64; self.words[0] = 0xea00003e; self.steps = 1
    def emit(self,*w): self.words.extend(w); self.steps += len(w)
    def literal(self,r,v):
        self.words.extend([0xe59f0000 | r<<12,0xea000000,v]); self.steps += 2
    def store(self,address,value):
        self.literal(0,address); self.literal(1,value); self.emit(0xe5801000)

def boundary_guest(c):
    """Execute conditional transfers with Z=1 to warm code without stores.
    Setup is unmeasured; measured execution repeats with Z=0 and empty FIFO.
    """
    g=Guest();code=0x10001000;kind=c['kind'];k=c.get('k',1)
    g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
    if c['model']==2:g.emit(0xee0f0f51)
    if c['model']==3:g.store(0x0320003c,7)
    for section,physical,attr in ((0,0,0),(0x100,0x10000000,8),(0x201,0x10100000,4)):
        g.store(0x10004000+section*4,physical|0xc02|attr)
    if kind.startswith('buffer-'):body=[0x14892200]*k+[0xe1a0f00b]
    elif kind=='word-slots':body=[0x18a901fe,0x15892000,0xe1a0f00b]
    else:body=[0xee0f0f52]+([0x15992000] if kind.endswith('miss') else [])+[0xe1a0f00b]
    for i,w in enumerate(body):g.store(code+4*i,w)
    g.literal(0,0x10004000);g.emit(0xee020f10);g.literal(0,3);g.emit(0xee030f10)
    g.literal(9,0x20100000);g.literal(0,0x107d);g.emit(0xee010f10,0xe1500000)
    ret=len(g.words)+2;g.literal(11,0);g.literal(12,code);g.emit(0xe1a0f00c)
    g.words[ret]=len(g.words)*4;g.steps+=len(body)
    if kind.startswith('clock-'):g.emit(0xee0f0f51)
    g.literal(9,0x20100000);g.emit(0xe3500001)
    if kind.startswith('clock-'):g.literal(11,code+4*(len(body)-1))
    g.emit(0xe1a0f00c);first=g.steps+1
    count=k if kind.startswith('buffer-') else 2 if kind=='word-slots' else k+len(body)-1
    return g.words,first,first+count-1

def icache_m0_guest(c):
    g=Guest();g.literal(0,0x1070);g.emit(0xee010f10,0xee0f0f51)
    g.literal(12,0x8000);g.emit(0xe1a0f00c);first=g.steps+1
    while len(g.words)<0x8000//4:g.words.append(0xeafffffe)
    g.words.append(0xeafffffe)
    return g.words,first,first+c['k']-1

def geometry_guest(c):
    """Keep code in sets 1/2; all distinct data lines occupy set zero.
    Warm with V=1/Z=1, so VC transfers and the NE loop branch all fail.
    Measured SUBS never overflows: VC probes still execute after Z becomes 1.
    """
    g=Guest();code=0x10001020 if c['model']==2 else 0x10001010;data=0x20100000
    g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
    if c['model']==2:g.emit(0xee0f0f51)
    if c['model']==3:g.store(0x0320003c,7)
    for section,physical,attr in ((0,0,0),(0x100,0x10000000,8),(0x201,0x10100000,8)):
        g.store(0x10004000+section*4,physical|0xc02|attr)
    # LDRVC r1,[r10],r5; SUBSVC r7,r7,#1; BNE loop;
    # Probes use r5=stride or r6=3*stride; zero uses [r9].
    body=[0x769a1005,0x72577001,0x1afffffc]
    body += [0x75991000 if line==0 else 0x77991005 if line==1 else 0x77991006
             for line in c['probes']]
    body += [0xe1a0f00b]
    for i,w in enumerate(body):g.store(code+4*i,w)
    g.literal(0,0x10004000);g.emit(0xee020f10);g.literal(0,3);g.emit(0xee030f10)
    g.literal(9,data);g.literal(10,data);g.literal(5,c['stride']);g.literal(7,c['lines'])
    if 3 in c['probes']:g.literal(6,3*c['stride'])
    g.literal(0,0x1075 if c['model']==2 else 0x75);g.emit(0xee010f10)
    g.literal(0,0x80000000);g.emit(0xe0900000) # ADDS two negatives: zero, Z=1/V=1
    ret=len(g.words)+2;g.literal(11,0);g.literal(12,code);g.emit(0xe1a0f00c)
    g.words[ret]=len(g.words)*4;g.steps+=len(body)
    g.emit(0xe3500001) # warm ADDS left r0=0; CMP #1 sets Z=0/V=0
    g.emit(0xe1a0f00c);first=g.steps+1
    return g.words,first,first+3*c['lines']+len(c['probes'])-1

def instruction_geometry_guest(c):
    """Cold SA RAM branch chain: same I set, then first line again. D off."""
    g=Guest();code=0x10008020
    g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000,0xee0f0f51)
    for section,physical,attr in ((0,0,0),(0x100,0x10000000,8)):
        g.store(0x10004000+section*4,physical|0xc02|attr)
    for i in range(c['lines']):
        address=code+i*c['stride'];target=code+((i+1)%c['lines'])*c['stride']
        g.store(address,0xea000000|(((target-address-8)//4)&0xffffff))
    g.literal(0,0x10004000);g.emit(0xee020f10);g.literal(0,3);g.emit(0xee030f10)
    g.literal(12,code);g.literal(0,0x1071);g.emit(0xee010f10,0xe1a0f00c)
    first=g.steps+1
    return g.words,first,first+c['lines']

def rom_castout_guest(c):
    """Warm I tags by execution, then architecturally invalidate only D tags."""
    g=Guest(); code=0x10001000
    g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000,0xee0f0f51)
    g.store(0x03200080,c['romcr'])
    for section,physical,attr in ((0,0,0),(0x32,0x03200000,0),
                                  (0x100,0x10000000,8),(0x200,0,8),(0x201,0x10100000,8)):
        g.store(0x10004000+section*4,physical|0xc02|attr)
    body=[0xe5993000,0xe5892000]+[0xe49a1200]*32+[0xe1a0f00b]
    for i,w in enumerate(body):g.store(code+4*i,w)
    g.literal(0,0x10004000);g.emit(0xee020f10);g.literal(0,3);g.emit(0xee030f10)
    g.literal(9,0x20008000);g.literal(10,0x20100000);g.literal(2,0x12345678)
    g.literal(0,0x1075|(8 if c.get('wb') else 0));g.emit(0xee010f10)
    return_literal=len(g.words)+2;g.literal(11,0);g.literal(12,code);g.emit(0xe1a0f00c)
    g.words[return_literal]=len(g.words)*4
    g.steps+=len(body) # all lines actually executed, outside the interval
    g.emit(0xee070f16) # invalidate D only; I tags remain
    g.literal(10,0x20100000);g.emit(0xe1a0f00c)
    first=g.steps+1
    return g.words,first,first+33

def rom_detour_guest(c):
    g=Guest();code=0x00101000;physical=0x10001000
    g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
    if c['model']==2:g.emit(0xee0f0f51)
    if c['model']==3:g.store(0x0320003c,7)
    g.store(0x03200080,c['romcr'])
    for section,base,attr in ((0,0,0),(1,0x10000000,8),(0x100,0x10000000,8)):
        g.store(0x10004000+section*4,base|0xc02|attr)
    g.store(physical,0xe1a0f00b) # cached trampoline: MOV pc,r11
    g.literal(0,0x10004000);g.emit(0xee020f10);g.literal(0,3);g.emit(0xee030f10)
    g.literal(0,0x1075);g.emit(0xee010f10)
    ret=len(g.words)+2;g.literal(11,0);g.literal(12,code);g.emit(0xe1a0f00c)
    g.words[ret]=len(g.words)*4;g.steps+=1
    g.literal(11,0x8004);g.literal(12,0x8000);g.emit(0xe1a0f00c)
    first=g.steps+1
    while len(g.words)<0x8000//4:g.words.append(0xeafffffe)
    g.words.extend([0xea000000|((code-0x8000-8)//4),0xe1a00000,0xeafffffe])
    return g.words,first,first+2

def guest(c):
    if c['kind']=='geometry-i':return instruction_geometry_guest(c)
    if c['kind']=='geometry':return geometry_guest(c)
    if c['kind']=='hot-dma':
        from rpc_hot_dma_rom import calibration
        return calibration(c)[:3]
    if c['kind'] in ('buffer-retire','buffer-drain','word-slots','clock-disable-hot','clock-disable-miss'):return boundary_guest(c)
    if c['kind']=='icache-m0':return icache_m0_guest(c)
    if c['kind']=='romdetour':return rom_detour_guest(c)
    if c['kind'] in ('romnext','romfallthrough'):
        w,first,_=guest(dict(c,kind='rom',k=2,clear_z=c['kind']=='romfallthrough'))
        w[0x8000//4]=0xeaffffff if c['kind']=='romnext' else 0x0affffff
        return w,first,first+1
    if c['kind']=='romcastout':return rom_castout_guest(c)
    g=Guest(); model=c['model'];kind=c['kind']; n=c.get('n',1); k=c.get('k',1)
    code=0x10001000; data=0x20100040+c.get('offset',0)
    if kind in ('abortload','abortstore'):data=0x201ffff8
    g.literal(0,0x70);g.emit(0xee010f10);g.literal(0,0xd3);g.emit(0xe121f000)
    if model==2 and not c.get('slow'):g.emit(0xee0f0f51)
    if model==3:g.store(0x0320003c,7)
    if c.get('width'):g.store(0x032000d0,1)
    if 'romcr' in c:g.store(0x03200080,c['romcr'])
    # Identity ROM bootstrap is C=0. Enabling caches cannot populate it.
    for section,physical,attr in ((0,0,8 if kind=='romhot' else 0),(0x32,0x03200000,0),
                                  (0x100,0x10000000,8),
                                  (0x201,0x10100000,(8 if c.get('cache') else 0)|(4 if c.get('bufferable',True) else 0))):
        g.store(0x10004000+section*4,physical|0xc02|attr)
    if c.get('space')=='vram':g.store(0x10004000+0x201*4,0x02000000|0xc02|
        (8 if c.get('cache') else 0)|(4 if c.get('bufferable',True) else 0))
    mask=(1<<n)-1
    if kind in ('alu','disable'):body=[0xe24ff008]*4 # SUB pc,pc,#8: one hot ALU instruction
    elif kind in ('branch','romhot'):body=[0xeafffffe]*4
    elif kind=='load':body=[(0xe5da1000 if c.get('byte') else 0xe59a1000),0xeafffffe,0xe1a00000,0xeafffffb]
    elif kind=='store':body=[(0xe5ca1000 if c.get('byte') else 0xe58a1000),0xeafffffe,0xe1a00000,0xeafffffb]
    elif kind in ('ldm','stm','abortload','abortstore'):
        pu={'ia':0x800000,'ib':0x1800000,'da':0,'db':0x1000000}[c.get('mode','ia')]
        body=[0xe80a0000|pu|(0x100000 if kind in ('ldm','abortload') else 0)|mask,0xeafffffe,0xe1a00000,0xeafffffb]
    elif kind=='swap':body=[0xe10a1092|(0x400000 if c.get('byte') else 0),0xeafffffe,0xe1a00000,0xeafffffb]
    elif kind in ('stream','streamwrite'):body=[0xe48a1004|(0x100000 if kind=='stream' else 0),0xe2577001,0x1afffffc,0xeafffffb]
    elif kind in ('random','randomwrite'):
        shift=5 if model==2 else 4
        if not c.get('cache'):shift=2
        body=[0xe78a1002|(0x100000 if kind=='random' else 0)|shift<<7,0xe0822005,0xe0022006,0xeafffffb]
        # 257 is not an ARM immediate: two byte contributions would break the
        # prescribed four-instruction cadence. Use a register holding 257.
        body[1]=0xe0822005
    elif kind=='clear':body=[0xe3330000,0x18aa01fe,0x1afffffc,0xeafffffb] # TEQ r3,#0; STMNE r10!,r1-r8
    elif kind=='io':body=[0xe59a1000,0xeafffffd,0xe1a00000,0xeafffffb]
    elif kind in ('rom','rombranch'):body=[0xe1a00000]*k+[0xeafffffe] if kind=='rom' else [0xeafffffe]*4
    elif kind=='abortfetch':body=[0xeafffffe]*4
    else:raise ValueError(kind)
    if kind=='romhot':
        code=0x8000
    elif kind in ('rom','rombranch'):code=0x8000
    else:
        for i,w in enumerate(body):g.store(code+4*i,w)
    # LDM PC returns to a cache-hot, benign branch, preserving real transfer16.
    if kind=='ldm' and n==16:
        mode=c.get('mode','ia');start=data+(4 if mode=='ib' else -4*n+4 if mode=='da' else -4*n if mode=='db' else 0)
        g.store((start & 0xfffff)|0x10100000 if c.get('space')!='vram' else (start & 0xfffff)|0x02000000,0)
        g.store(((start+60)&0xfffff)|(0x02000000 if c.get('space')=='vram' else 0x10100000),code+4)
    g.literal(0,0x10004000);g.emit(0xee020f10);g.literal(0,3);g.emit(0xee030f10)
    g.literal(10,0x03200094 if kind=='io' else data)
    if kind in ('stream','streamwrite','clear','random','randomwrite'):g.literal(10,0x20100000)
    g.literal(7,k);g.literal(6,1023);g.literal(5,257);g.literal(2,0);g.literal(3,1)
    g.literal(11,0x20400000 if kind=='abortfetch' else code if kind in ('rom','rombranch') else code+12)
    ctrl=0x75|(0x1000 if model==2 else 0)|(8 if c.get('wb') else 0)
    g.literal(0,ctrl);g.emit(0xee010f10)
    if c.get('hot_data'):g.emit(0xe59a2000)
    if kind=='disable':
        # Warm the eventual line with a data load, then stop allocations.
        g.literal(9,code);g.emit(0xe5992000);g.literal(0,ctrl&~4);g.emit(0xee010f10)
    if c.get('clear_z'):g.emit(0xe3500001) # CMP known control value != 1: Z=0
    g.emit(0xe1a0f00b)
    first=g.steps+1+(0 if kind in ('rom','rombranch','abortfetch') else 1) # warming branch excluded
    if kind in ('rom','rombranch','romhot'):
        while len(g.words)<code//4:g.words.append(0xeafffffe)
        g.words.extend(body)
    count=k*(3 if kind in ('stream','streamwrite','clear') else 4 if kind in ('random','randomwrite') else 2 if kind in ('alu','disable','io') else 1)
    # ALU fixture includes its hot branch, so oracle's k counts instructions.
    if kind in ('alu','disable'):count=k
    return g.words,first,first+count-1

def main(path):
    out=['/* Authored ARM guests and independent rational expectations. GPL-2.0-only. */',
         '#include <stdint.h>',
         'typedef struct { const char *name; unsigned model,rate; const uint32_t *words; unsigned count; uint64_t first,last,numerator,denominator; int drained; } RpcTimingCase;']
    rows=[]
    for i,c in enumerate(cases()):
        w,first,last=guest(c);t=duration(c)
        out.append(f'static const uint32_t rpc_timing_words_{i}[]={{'+','.join(f'0x{x:08x}u' for x in w)+'};')
        rows.append('{"%s",%d,%d,rpc_timing_words_%d,%d,%d,%d,%d,%d,%d}' %
                    (c['name'],c['model'],c['rate'],i,len(w),first,last,t.numerator,t.denominator,int(c.get('drained',c.get('wb',False)))))
    out.append('static const RpcTimingCase rpc_timing_cases[]={'+',\n'.join(rows)+'};')
    Path(path).write_text('\n'.join(out)+'\n')
if __name__=='__main__':main(sys.argv[1])

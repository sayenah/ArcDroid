#!/usr/bin/env python3
"""Independent per-instruction and own-fetch repricing equations. GPL-2.0-only."""
from fractions import Fraction as F
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from rpc_timing_rom import Guest, guest
from rpc_timing_oracle import duration, base, rom, RATES


def check(harness,core,folder,model,words,first,last,expected,name):
    path=folder/'interval.rom';data=struct.pack('<'+'I'*len(words),*words)
    path.write_bytes(data+bytes(2097152-len(data)))
    for observer in range(4):
        result=subprocess.run([harness,core,str(path),str(model),str(RATES[model]),str(first),str(last),'0',
                               str(expected.numerator),str(expected.denominator),str(observer)],capture_output=True,text=True,timeout=30)
        if result.returncode:raise RuntimeError((name,observer,'independent interval disagreement',result.returncode,result.stdout,result.stderr))
        print(name,'observer='+str(observer),result.stdout.strip(),flush=True)


def main():
    harness,core=sys.argv[1:3]
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2a-interval-') as temp:
        folder=Path(temp)
        for model in range(4):
            c=dict(kind='rom',model=model,rate=RATES[model],k=8)
            words,first,last=guest(c)
            for ordinal in range(8):
                check(harness,core,folder,model,words,first+ordinal,first+ordinal,
                      duration(dict(c,k=1)),f'G-1-issue-{model}-{ordinal}')
            burst=dict(c,romcr=0x19)
            words,first,last=guest(burst)
            check(harness,core,folder,model,words,first,last,duration(burst),f'AC-M2-burst-{model}')
            # STR's functional IOMD write precedes service of its own queued
            # ROM fetch. ROMCR=0x21: 6*31.25 ns; IOMD write: 4*31.25 ns.
            # These constants/recurrences are rpc_timing_oracle.rom/base.
            for clock in ((False,True) if model==3 else (False,)):
                g=Guest();g.literal(0,0x70);g.emit(0xee010f10)
                g.literal(0,0xd3);g.emit(0xe121f000)
                if model==2:g.emit(0xee0f0f51)
                if clock:
                    g.store(0x03200080,0x20)
                    address,value,cr=0x0320003c,7,0x20
                else:
                    if model==3:g.store(0x0320003c,7)
                    address,value,cr=0x03200080,0x21,0x21
                g.literal(0,address);g.literal(1,value)
                first=g.steps+1;g.emit(0xe5801000,0xeafffffe)
                expected=base(c)+rom(dict(c,romcr=cr),1)+F(125)
                check(harness,core,folder,model,g.words,first,first,expected,
                      f'G-8-own-fetch-{model}-'+('CLKCTL' if clock else 'ROMCR'))


if __name__=='__main__':main()

#!/usr/bin/env python3
"""Driver limits, field completion, capture clipping and cheap SWI. GPL-2.0-only."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
from rpc_video_rom import program
from rpc_d2_rom import inputs
from rpc_timing_oracle import RATES
from rpc_lockstep_rom import build


def main():
    harness,core=sys.argv[1:3]
    with tempfile.TemporaryDirectory(prefix='arcdroid-m2a-boundaries-') as temp:
        path=Path(temp)/'authored.rom'
        for model in range(4):
            cases=[('limit',[0xea00003e]+[0xeafffffe]*64),
                   ('field',program(model,32,8,3,0,'plain')),
                   ('cheap-swi',build(model,0,('modes','carry26'))[0])]
            if model in (2,3):cases.append(('capture',inputs(model,factory=True).words))
            for mode,words in cases:
                data=struct.pack('<'+'I'*len(words),*words);path.write_bytes(data+bytes(2097152-len(data)))
                for observer in range(4):
                    r=subprocess.run([harness,core,str(path),str(model),str(RATES[model]),mode,str(observer)],capture_output=True,text=True,timeout=90)
                    if r.returncode:raise RuntimeError((model,mode,observer,r.returncode,r.stdout,r.stderr))
                    print(model,'observer='+str(observer),r.stdout.strip(),flush=True)


if __name__=='__main__':main()

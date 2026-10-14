#!/usr/bin/env python3
"""D-139 100 MIPS cache-hot ceiling workload, authored bytes. GPL-2.0-only.
Run 601 frames: frame zero covers setup; the next 600 cover ten guest seconds.
The only repeated instruction is a warmed RAM branch. No video DMA or I/O is
in the loop. The independent expectation is exactly 100,000,000 instructions
per guest second after setup, under both frontend pacings.
"""
from pathlib import Path
import struct,sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from rpc_timing_rom import guest

def generate(directory, hold_report=False):
    directory.mkdir(parents=True,exist_ok=True)
    words,_,_=guest(dict(model=2,rate=100,kind='branch',k=1000000))
    if hold_report:
        # Same warmed RAM branch; a real user report holds its return watch
        # while the authored supervisor handler jumps into that branch.
        assert words[-1]==0xe1a0f00b
        words[-1:]=[0xe3a0d201,0xe28dd902,0xe3a01003,0xe321f0d0,0xef0400df,0xeafffffe]
        words[2]=0xe1a0f00b
    rom=bytearray(struct.pack('<I',0xeafffffe)*(2097152//4))
    rom[:len(words)*4]=struct.pack('<'+'I'*len(words),*words)
    (directory/'hot-authored.rom').write_bytes(rom)
    (directory/'hot.seed').write_bytes(bytes(256))
    (directory/'hot.arcplan').write_text('version=1\nmodel=sa110\nrate_mips=100\nfirmware=hot-authored.rom\ncmos=seed\nseed=hot.seed\nrtc_epoch=0\n')

if __name__=='__main__':
    if len(sys.argv) not in (2,3) or (len(sys.argv)==3 and sys.argv[2]!='--hold-report'):raise SystemExit('rpc_hot_workload.py DIR [--hold-report]')
    generate(Path(sys.argv[1]),len(sys.argv)==3)

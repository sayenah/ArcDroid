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

def generate(directory):
    directory.mkdir(parents=True,exist_ok=True)
    words,_,_=guest(dict(model=2,rate=100,kind='branch',k=1000000))
    rom=bytearray(struct.pack('<I',0xeafffffe)*(2097152//4))
    rom[:len(words)*4]=struct.pack('<'+'I'*len(words),*words)
    (directory/'hot-authored.rom').write_bytes(rom)
    (directory/'hot.seed').write_bytes(bytes(256))
    (directory/'hot.arcplan').write_text('version=1\nmodel=sa110\nrate_mips=100\nfirmware=hot-authored.rom\ncmos=seed\nseed=hot.seed\nrtc_epoch=0\n')

if __name__=='__main__':generate(Path(sys.argv[1]))

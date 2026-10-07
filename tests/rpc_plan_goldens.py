#!/usr/bin/env python3
"""Independent declared integrated matrix observations; never read a core. GPL-2.0-only."""
from pathlib import Path
from rpc_plan_matrix_rom import matrix
from rpc_video_goldens import expected as video_expected
from rpc_d2_goldens import expected as audio_expected, fnv

def matrix_golden(model):
    _,rows=matrix(model)
    return '\n'.join(f'{i:03d}: '+','.join(f'{v:08x}' for v in row) for i,row in enumerate(rows))+'\n'

if __name__=='__main__':
    for m in range(4):
        Path(__file__).with_name('fixtures').joinpath(f'rpc-plan-matrix-{m}.trace').write_text(matrix_golden(m))
        out=[]
        for line in video_expected(m).splitlines():
            fields=line.split();out.append(f'raster {fields[0]} {fields[1]} {fields[3][5:]}')
        pcm,_,_=audio_expected(m,end=50000001)
        out.append(f'pcm {fnv(pcm):016x} frames=2400 accepted=2400 discarded=0')
        palette_line=next(line for line in video_expected(m).splitlines() if line.startswith('palette '))
        pcm,_,_=audio_expected(m,end=10000000)
        out.append(f'mixed first-raster={palette_line.split()[3][5:]} pcm480={fnv(pcm):016x}')
        out.append('input keyboard=1c,f0,1c mouse='+('fa,09,00,00,09,0c,07,08,00,00' if m==3 else '12,7,70'))
        Path(__file__).with_name('fixtures').joinpath(f'rpc-plan-av-{m}.trace').write_text('\n'.join(out)+'\n')

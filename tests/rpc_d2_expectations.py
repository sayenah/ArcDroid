#!/usr/bin/env python3
"""Generate build-only analytic PCM arrays, compare traces to committed goldens."""
from pathlib import Path
import subprocess,sys
sys.dont_write_bytecode=True
from rpc_d2_goldens import expected,fnv,dma_observations
if sys.argv[1]=='--header':
    out=['/* Analytic PCM expectations, no emulator output. GPL-2.0-only. */']
    for m in range(4):
        states,blocks,frames,completion=dma_observations(m)
        out.append(f'static const uint64_t rpc_expected_dma_end_{m} = UINT64_C({completion});')
        out.append(f'static const uint8_t rpc_expected_dma_states_{m}[] = {{'+','.join(map(str,states))+'};')
        out.append(f'static const unsigned rpc_expected_dma_counts_{m}[] = {{{blocks},{frames}}};')
        for c in [False,True]:
            pcm,frames,blocks=expected(m,c)
            name=f'rpc_expected_{"compat" if c else "audio"}_{m}'
            out += [f'static const uint32_t {name}[] = {{'+','.join(f'0x{w:08x}u' for w in pcm)+'};',f'static const unsigned {name}_dma[] = {{{frames},{blocks}}};']
        pcm,_,_=expected(m,end=50000001)
        out.append(f'static const uint64_t rpc_expected_frontend_{m} = UINT64_C(0x{fnv(pcm):016x});')
    Path(sys.argv[2]).write_text('\n'.join(out)+'\n')
else:
    r=subprocess.run(sys.argv[1:5],capture_output=True,text=True)
    if r.returncode:sys.stderr.write(r.stdout+r.stderr);raise SystemExit(r.returncode)
    wanted=Path(sys.argv[5]).read_text()
    if r.stdout!=wanted:
        import difflib
        sys.stderr.write(''.join(difflib.unified_diff(wanted.splitlines(True),r.stdout.splitlines(True),fromfile='analytic golden',tofile='core trace')));raise SystemExit(1)
    print('D2 trace matches independent committed golden')

#!/usr/bin/env python3
"""Compare same-library switching with a fresh-process Arculator trace. GPL-2.0-only."""
import subprocess
import sys
exe,core,rom=sys.argv[1:]
def run(mode):
    result=subprocess.check_output([exe,core,mode,rom],text=True)
    return [line for line in result.splitlines() if line.startswith('arculator-trace=')]
a=run('--fresh');b=run('--switch')
assert len(a)==1 and a==b,(a,b)
print('rpc-session-switch: four RPC models preserve the fresh-process Arculator trace')

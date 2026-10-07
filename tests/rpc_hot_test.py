#!/usr/bin/env python3
"""Verify ceiling progress, independent of host speed. GPL-2.0-only."""
import re,subprocess,sys
runner,core,directory=sys.argv[1:4]
r=subprocess.run([runner,'--core',core,'--plan',directory+'/hot.arcplan','--system',directory,
                  '--frames','601'],capture_output=True,text=True)
assert r.returncode==0,r.stderr+r.stdout
samples=[tuple(map(int,x)) for x in re.findall(r'^frame=\d+ ns=(\d+) cpu=(\d+)',r.stdout,re.M)]
assert len(samples)==601
ns=samples[-1][0]-samples[0][0];instructions=samples[-1][1]-samples[0][1]
assert ns>=10000000000 and abs(10*instructions-ns)<=9,(ns,instructions)
assert 'fault=1' not in r.stdout
print('hot code: >=10 guest seconds at the 100 MIPS ceiling, exact carried instruction count')

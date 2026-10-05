#!/usr/bin/env python3
"""Public runner integration: footer, exact captures, on/off output equivalence."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
from evidence import completion, core_rows, EvidenceError, read_rows, include_csv_cost

p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);p.add_argument('--core',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(exist_ok=False,parents=True)
shots=a.output/'times.txt';shots.write_text('0.03\n0.06\n0.09\n')
base=[str(a.runner.resolve()),str(a.core.resolve()),'.12','unthrottled','rpcprobe_authored=enabled','rpcprobe_model=SA110','rpcprobe_rate=20']
ledgers=[]
for mode in ('enabled','disabled'):
    out=a.output/mode;out.mkdir()
    cmd=base+['rpcprobe_output='+str(out.resolve()),'rpcprobe_measurement='+mode,'--ledger='+str((out/'ledger.csv').resolve()),'--timing='+str((out/'timing.csv').resolve()),'--png='+str((out/'capture').resolve()),'--shot-times='+str(shots.resolve())]
    with (out/'runner.log').open('w') as f:
        result=subprocess.run(cmd,stdout=f,stderr=f)
    assert result.returncode==0,'runner failed'
    rows=read_rows(out/'timing.csv',('frame','emu_ns','period_ns','exec_ns'))
    footer=completion(out/'timing.csv',rows,expected_ns=120000000)
    assert footer['measurement']==(mode=='enabled')
    if mode=='enabled':
        measured=include_csv_cost(out/'csv-cost.csv',core_rows(out/'frames.csv'))
        for core,whole in zip(measured,rows):
            assert core['frame']==whole['frame'] and core['emu_ns']==whole['emu_ns']
            assert core['exec_ns']<=whole['exec_ns']+1000,'execution cost exceeds full call'
    else:assert not (out/'frames.csv').exists(),'off must omit full instrumentation'
    with (out/'ledger.csv').open(newline='') as f:ledgers.append(list(csv.DictReader(f)))
    assert len(list(out.glob('capture.time-*.png')))==3
assert ledgers[0]==ledgers[1],'on/off guest output differs'
# Intentionally mismatched requested declaration: runner completes its own
# limit, but a clean CSV cannot satisfy the core's longer declared run.
out=a.output/'shortened';out.mkdir()
with (out/'runner.log').open('w') as f:
    result=subprocess.run(base+['rpcprobe_output='+str(out.resolve()),'rpcprobe_expected_seconds=.2'],stdout=f,stderr=f)
assert result.returncode==0
try:completion(out/'frames.csv',core_rows(out/'frames.csv'))
except EvidenceError:pass
else:raise AssertionError('incomplete core footer accepted')
for text in ('nan\n','0.06\n0.03\n','0.13\n'):
    bad=a.output/'invalid.txt';bad.write_text(text)
    result=subprocess.run(base+['--png='+str(a.output/'invalid'),'--shot-times='+str(bad.resolve())],capture_output=True)
    assert result.returncode==2,'invalid absolute capture times accepted'
for duration in ('nan','inf',''):
    cmd=base[:];cmd[2]=duration
    assert subprocess.run(cmd,capture_output=True).returncode==2,'invalid guest duration accepted'
print('PASS runner integration: full/off identical AV, footer limits, timed captures and malformed time refusal')
# Every-frame region hashes must cover the same exact frames and times as
# the independent AV ledger. Out-of-viewport regions are explicitly missing.
regions=a.output/'regions.txt';regions.write_text('hud,0,0,16,2\noutside,1900,1900,16,2\n')
out=a.output/'regions';out.mkdir()
cmd=base+['rpcprobe_output='+str(out.resolve()),'--ledger='+str((out/'ledger.csv').resolve()),'--regions='+str(regions.resolve()),'--frame-hashes='+str((out/'frame-hashes.csv').resolve()),'--region-pixels='+str((out/'region-pixels.csv').resolve()),'--pixel-regions=hud,outside']
with (out/'runner.log').open('w') as f:assert subprocess.run(cmd,stdout=f,stderr=f).returncode==0
with (out/'frame-hashes.csv').open() as f:observations=list(csv.DictReader(f))
with (out/'ledger.csv').open() as f:ledger=list(csv.DictReader(f))
assert len(observations)==len(ledger) and len(ledger)>1
for observed,independent in zip(observations,ledger):
    assert observed['frame']==independent['frame'] and observed['emu_ns']==independent['emu_ns']
    assert observed['full_hash']==independent['video_hash']
    assert observed['outside']=='MISSING' and len(observed['hud'])==16
for text in ('duplicate,0,0,1,1\nduplicate,0,0,1,1\n','bad,2048,0,1,1\n','bad,0,0,0,1\n','bad,0,0,1,1,junk\n'):
    regions.write_text(text)
    assert subprocess.run(base+['--regions='+str(regions.resolve()),'--frame-hashes='+str(out/'invalid.csv')],capture_output=True).returncode==2
print('PASS every-frame HUD/full hashes, exact timestamps, explicit missing bounds and malformed region refusal')

from region_pixels import validate, decode
assert validate(out, ['hud','outside'])['status']=='PASS'
with (out/'region-pixels.csv').open() as f:tiles=list(csv.DictReader(f))
assert any(decode(tile) is None for tile in tiles)
for selection in ('hud,hud','unknown',''):
    regions.write_text('hud,0,0,16,2\n')
    assert subprocess.run(base+['--regions='+str(regions.resolve()),'--frame-hashes='+str(out/'invalid.csv'),'--region-pixels='+str(out/'invalid-tiles.csv'),'--pixel-regions='+selection],capture_output=True).returncode==2
for rle in ('1:00000000;', '999999:00000000;', 'x:00000000;', '32:00000000'):
    try:decode(dict(hash='0000000000000000', width='16', height='2', rle=rle))
    except EvidenceError:pass
    else:raise AssertionError('invalid tile accepted')
original=(out/'region-pixels.csv').read_text()
(out/'region-pixels.csv').write_text(original.splitlines()[0]+'\n')
try:validate(out,['hud','outside'])
except EvidenceError:pass
else:raise AssertionError('missing tile coverage accepted')
(out/'region-pixels.csv').write_text(original)
print('PASS bounded HUD pixel round trip, missing geometry, malformed RLE and incomplete coverage refusal')

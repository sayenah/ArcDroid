#!/usr/bin/env python3
"""Retained-data oracle in new executable processes after both streams. GPL-2.0-only."""
from pathlib import Path
import argparse,subprocess,tempfile,time
parser=argparse.ArgumentParser()
parser.add_argument('binary',type=Path)
parser.add_argument('--profile',choices=('full','sanitizer'),default='full')
args=parser.parse_args();binary=args.binary.resolve()
volumes=(16,12) if args.profile=='sanitizer' else (512,64)
mode='bounds-sanitizer' if args.profile=='sanitizer' else 'bounds'
with tempfile.TemporaryDirectory(prefix='arc-rpc-hd-bounds-') as tmp:
    paths=[Path(tmp)/name for name in ('sequential.snapshot','scatter.snapshot')]
    started=time.monotonic()
    with tempfile.TemporaryFile(mode='w+t') as output:
        try:
            subprocess.run([str(binary),mode,*[str(p) for p in paths]],stdout=output,check=True,timeout=3000)
        finally:
            output.seek(0);report=output.read();print(report,end='',flush=True)
    print(f'bounds profile={args.profile} stream_wall_s={time.monotonic()-started:.3f} stream_limit_s=3000',flush=True)
    # Duration is reduced only after preserving several complete cycles and
    # the full-volume streams' cap-relevant journal high-water marks.
    for name,mib,high in zip(('sequential','scatter'),volumes,(3630976,6392736)):
        row=next(line for line in report.splitlines() if line.startswith('stream '+name+' '))
        stats=dict(item.split('=',1) for item in row.split()[2:])
        assert int(stats['bytes'])>=mib*1048576,row
        # One publication creates the initial blank root; require at least
        # three further complete stream cycles, followed by idle(s).
        assert int(stats['checkpoints'])>=4,row
        if name=='sequential':assert int(stats['wraps'])>=1,row
        assert int(stats['journal_high'])>=high,row
    for path in paths:
        started=time.monotonic()
        subprocess.run([str(binary),'reopen',str(path)],check=True,timeout=120)
        print(f'bounds recovery={path.stem} wall_s={time.monotonic()-started:.3f} limit_s=120',flush=True)

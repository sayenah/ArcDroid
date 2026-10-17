#!/usr/bin/env python3
"""Confined fresh frontend directory; native Windows and host. GPL-2.0-only."""
import re
import subprocess
import sys
sys.dont_write_bytecode=True
import tempfile
import shutil
import hashlib
import zlib
from pathlib import Path
from rpc_boot_packages_test import inputs
with tempfile.TemporaryDirectory(prefix='arcdroid-rpc-observer-frontend-') as directory:
    system=inputs(Path(directory))
    shutil.copytree(system/"arcdroid",Path(directory)/"arcdroid")
    result=subprocess.run([sys.argv[1],sys.argv[2],directory,*sys.argv[3:]],timeout=240,capture_output=True,text=True)
    sys.stdout.write(result.stdout);sys.stderr.write(result.stderr)
    result.check_returncode()
    if len(sys.argv)>4 and sys.argv[4]=='off':
        if 'observers off: qualified' not in result.stdout:raise RuntimeError('missing off scenario')
        sys.exit(0)
    termination_lines=[line.split('\t',1)[1] for line in result.stdout.splitlines() if line.startswith('termination-log\t')]
    profile=sys.argv[4] if len(sys.argv)>4 else 'full'
    if profile in ('full','h-b','sanitizer','sanitizer-extended'): assert termination_lines, 'multi-disc log scenario did not run'
    if termination_lines:
        assert not re.search(r'(?i)refused','\n'.join(termination_lines)), 'termination log had a notice'
        assert any('gate=path-other' in line for line in termination_lines), 'unrelated-path log missing'
        assert any('outcome-not-miss' in line for line in termination_lines), 'found-object log missing'
    seen=set()
    for line in result.stdout.splitlines():
        if not line.startswith('copied-image\t'): continue
        _,model,size,crc,sha1=line.split('\t');model=int(model)
        assert model not in seen;seen.add(model)
        image=(Path(directory)/f'digest-{model}.rom').read_bytes()
        assert int(size)==len(image) and crc==f'{zlib.crc32(image):08x}' and sha1==hashlib.sha1(image).hexdigest(),line
    focused=len(sys.argv)>4 and (sys.argv[4].startswith(('h-','sanitizer-disc-')) or sys.argv[4]=='sanitizer-extended')
    assert seen==(set() if focused else set(range(4))),seen
    if seen: print('copied-image digest: four CPUs match independent Python size/CRC32/SHA-1; source replacement and retirement checked')

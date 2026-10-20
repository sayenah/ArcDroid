#!/usr/bin/env python3
"""Runtime option and metadata identity; only seven help suffixes vary. GPL-2.0-only."""
from pathlib import Path
import subprocess,sys,tempfile
exe,closed,opened,*baseline=sys.argv[1:]
suffix=' Archimedes sessions only. RiscPC starts manually with its hard disc attached.'
with tempfile.TemporaryDirectory(prefix='rpc-route-metadata-') as temporary:
    def read(core):
        run=subprocess.run([exe,core,temporary,temporary,'unused','metadata','unused'],check=True,text=True,capture_output=True)
        assert 'option|' in run.stdout and 'system|' in run.stdout
        return run.stdout
    original=read(closed);candidate=read(opened)
    changed={line.split('|')[1] for line in candidate.splitlines() if suffix in line}
    assert changed=={'arcdroid_joystick_if','arcdroid_launch','arcdroid_launch_chooser','arcdroid_launch_ramdisc','arcdroid_jfd_boot','arcdroid_adffs_package','arcdroid_hard_disc'},changed
    assert candidate.replace(suffix,'')==original
    for core in baseline:assert read(core)==original
print('rpc-route-metadata: all runtime option keys/values/defaults/order and system info identical; exactly seven help suffixes permitted')

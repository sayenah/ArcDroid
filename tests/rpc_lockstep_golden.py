#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Opt-in golden runner: compare complete stdout against the no-emitter core."""
import os
from pathlib import Path
import subprocess


def run(command,**kwargs):
    density=os.environ.get('ARCDROID_RPC_LOCKSTEP_TEST_DENSITY')
    if not density:return subprocess.run(command,**kwargs)
    original=list(command);candidate=list(command)
    index=original.index('--core')+1
    original[index]=os.environ['ARCDROID_RPC_LOCKSTEP_PLAN_CORE']
    plan=Path(candidate[candidate.index('--plan')+1]).read_text()
    model=next(line.split('=',1)[1] for line in plan.splitlines() if line.startswith('model='))
    model=('arm610','arm710','sa110','arm7500').index(model)
    candidate+=['--lockstep-stream',os.devnull,'--lockstep-role','baseline',
        '--lockstep-density',density,'--lockstep-model',str(model)]
    expected=subprocess.run(original,**kwargs)
    actual=subprocess.run(candidate,**kwargs)
    if expected.returncode!=actual.returncode or expected.stdout!=actual.stdout:
        raise RuntimeError(('lockstep golden guest effect',command,expected.returncode,actual.returncode,
            expected.stdout[-1000:],actual.stdout[-1000:],actual.stderr[-1000:]))
    return actual

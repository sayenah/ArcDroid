#!/usr/bin/env python3
"""Characterize fresh load and current in-session reset. GPL-2.0-only.

Programmed VIDC registers persist across machine recreation, so early reset
frames differ from fresh load. Fable carries this pre-existing behaviour as a
separate Arculator follow-up; R1 pins it without changing production behaviour.
"""
import difflib
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='arc-reset-poweron-') as directory:
    result = subprocess.run([*sys.argv[1:], directory, '--reset-poweron'],
                            capture_output=True, text=True)
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(result.returncode)

actual = [line for line in result.stdout.splitlines()
          if line.startswith('arculator-reset ')]
golden = Path(__file__).parent / 'fixtures' / 'arculator-reset-poweron.trace'
expected = [line for line in golden.read_text().splitlines()
            if line and not line.startswith('#')]
assert len(actual) == len(expected) == 48, '24 frames each for fresh load and reset'
if actual != expected:
    sys.stderr.write('\n'.join(difflib.unified_diff(
        expected, actual, fromfile=str(golden), tofile='actual')) + '\n')
    raise SystemExit(1)
print('Arculator fresh/reset characterization: 48 pixel/PCM/count/geometry records match; '
      'CPU credit, MEMC DMA and VIDC raster reset assertions pass')

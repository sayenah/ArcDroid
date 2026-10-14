#!/usr/bin/env python3
"""Confined fresh frontend directory; native Windows and host. GPL-2.0-only."""
import subprocess
import sys
import tempfile
import shutil
from pathlib import Path
from rpc_boot_packages_test import inputs
with tempfile.TemporaryDirectory(prefix='arcdroid-rpc-observer-frontend-') as directory:
    system=inputs(Path(directory))
    shutil.copytree(system/"arcdroid",Path(directory)/"arcdroid")
    subprocess.run([sys.argv[1],sys.argv[2],directory,*sys.argv[3:]],check=True,timeout=240)

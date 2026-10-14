#!/usr/bin/env python3
"""Confined fresh frontend directory; native Windows and host. GPL-2.0-only."""
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='arcdroid-rpc-observer-frontend-') as directory:
    subprocess.run([sys.argv[1],sys.argv[2],directory,*sys.argv[3:]],check=True,timeout=240)

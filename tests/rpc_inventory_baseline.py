#!/usr/bin/env python3
"""R3d-2 authored ordinary-load projection against the pinned base.

Usage: script LOAD_TEST BASE_CORE CANDIDATE_CORE AUTHORED_ROM EVIDENCE_DIRECTORY
Build BASE_CORE from be44b0a. No private inputs; no normal build dependency on git.
GPL-2.0-only.
"""
from pathlib import Path
import difflib
import subprocess
import sys

sys.dont_write_bytecode = True
load, base, candidate, rom, output = sys.argv[1:]
output = Path(output).resolve()
output.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parent
projections = []
stores = []
for label, core in (("base", base), ("candidate", candidate)):
    data = output / label
    projection = output / (label + ".txt")
    subprocess.run([sys.executable, "-B", str(root / "rpc_selection_load_test.py"),
                    load, core, rom, str(data), str(projection), label], check=True)
    text = projection.read_text().replace(str(data), "<root>")
    # Exact approved deltas only. All other diagnostic fields and every other
    # output byte remain part of the comparison.
    if label == "candidate":
        text = text.replace("reason=legacy model=none os=none needed=none inventory=skipped",
                            "reason=legacy model=none os=none needed=none inventory=complete")
        text = text.replace("reason=incomplete-scan model=none os=none needed=3.50,3.60,3.70,3.71 inventory=incomplete",
                            "reason=missing-firmware model=none os=none needed=3.50,3.60,3.70,3.71 inventory=incomplete")
    projections.append(text)
    # All persistent and staged bytes, keyed by their path below the save root.
    stores.append({str(p.relative_to(data)): p.read_bytes()
                   for p in (data / "arcdroid").rglob("*") if p.is_file()})
if projections[0] != projections[1]:
    diff = "".join(difflib.unified_diff(projections[0].splitlines(True),
                                       projections[1].splitlines(True), "base", "candidate"))
    (output / "difference.txt").write_text(diff)
    raise RuntimeError("ordinary-load projection differs: " + diff[:6000])
if not stores[0] or stores[0] != stores[1]:
    raise RuntimeError("persistent/staged file set or bytes differ (or census empty)")
print(f"Pinned be44b0a projection: logs, notices, results, media masks and legacy I/O equal; {len(stores[0])} persistent/staged files byte-identical; only enumerated diagnostic deltas")

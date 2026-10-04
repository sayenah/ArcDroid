#!/usr/bin/env python3
"""Authored content/workload integration checks. No firmware or private media."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--runner", type=Path, required=True)
p.add_argument("--core", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
args = p.parse_args()
root = args.output.resolve()
root.mkdir()  # Evidence must be a fresh session.
work = root / "system/arcdroid/rpcprobe/authored"
work.mkdir(parents=True)
(work / "script.txt").write_text("0 checkpoint 0 0 origin\n50000 end 0 0 release\n")
cmos = bytes(256)
(work / "cmos.bin").write_bytes(cmos)
(work / "cmos.sha1").write_text(hashlib.sha1(cmos).hexdigest() + "\n")
# Independent JFD contract: two absent tracks; no sectors or payload.
disc = bytearray(312)
disc[:4] = b"JFDI"
struct.pack_into("<I", disc, 8, len(disc))
struct.pack_into("<III", disc, 24, 304, 312, 312)
struct.pack_into("<II", disc, 304, 0xffffffff, 0xffffffff)
(root / "empty.jfd").write_bytes(disc)
(root / "pair.m3u").write_text("# authored relative paths\nempty.jfd\nempty.jfd\n")
(root / "bad.m3u").write_text("absent.jfd\n")
base = [str(args.runner.resolve()), str(args.core.resolve()), "0.1", "unthrottled",
        "--system=" + str(root / "system"), "rpcprobe_workload=authored",
        "rpcprobe_authored=enabled", "rpcprobe_firmware=ROM371",
        "rpcprobe_model=SA110", "rpcprobe_rate=100", "rpcprobe_ide=disabled"]
results = []
traces = []
for content, expected in [("empty.jfd", 0), ("pair.m3u", 0), ("bad.m3u", 1)]:
    command = base + ["--content=" + str(root / content)]
    completed = subprocess.run(command, capture_output=True, text=True)
    (root / (content + ".log")).write_text(completed.stdout + completed.stderr)
    results.append({"content": content, "argv": command, "exit": completed.returncode})
    assert completed.returncode == expected, results[-1]
    if expected == 0:
        session = work / ("run-%06d" % (len(traces)))
        rows = list(csv.DictReader((session / "frames.csv").open()))
        assert rows and all(r["status"] == "OK" for r in rows)
        traces.append([(r["emu_ns"], r["video_hash"], r["audio_hash"], r["audio_frames"]) for r in rows])
        media = list(csv.reader((session / "media-custody.csv").open()))
        assert sum(r[0] == "media" for r in media) == (1 if content == "empty.jfd" else 2)
assert traces[0] == traces[1], "Authored AV differs between direct and playlist content"
(root / "result.json").write_text(json.dumps({"result": "PASS", "runs": results}, indent=2))
print("frontend integration PASS: direct JFD, two-disc relative M3U, identical authored AV, missing media refused")

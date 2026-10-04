#!/usr/bin/env python3
"""Firmware-owned factory CMOS capture. GPL-2.0-only; no private fixtures."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

ACCEPTED = {
    "c31318e712ee7de92e46f9df6c791b614d4c9304",
    "3cf75aa4d4dc8fe57110a124fbd1560d46c549b1",
    "71cbc9f12debaa7aa2aeb91d38682e6c5c4a34a5",
    "c5fe0645e48894fb4b245abeefdc9a65d659af59",
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runner", type=Path, required=True)
    p.add_argument("--core", type=Path, required=True)
    p.add_argument("--firmware", type=Path, required=True)
    p.add_argument("--model", choices=("ARM610", "ARM710", "SA110", "A7000"), required=True)
    p.add_argument("--out", type=Path, required=True, help="new private output directory")
    a = p.parse_args()
    with a.firmware.open("rb") as f:
        firmware = f.read(8 * 1024 * 1024 + 1)
    digest = hashlib.sha1(firmware).hexdigest()
    if digest not in ACCEPTED:
        p.error("firmware fingerprint refused")
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    script = out / "script.txt"
    script.write_text("0 key_down 369 0 factory_delete\n"
                      "12000000 key_up 369 0 release_delete\n"
                      "29500000 end 0 0 release\n", encoding="ascii")
    rate = "35" if a.model in ("ARM710", "A7000") else "20"
    cmd = [str(a.runner.resolve()), str(a.core.resolve()), "30", "unthrottled",
           "rpcprobe_model=" + a.model, "rpcprobe_rate=" + rate,
           "rpcprobe_firmware=" + str(a.firmware.resolve()),
           "rpcprobe_cmos_factory=enabled", "rpcprobe_ide=enabled",
           "rpcprobe_output=" + str(out), "rpcprobe_script=" + str(script),
           "--png=" + str(out / "desktop.png")]
    custody = {"firmware_sha1": digest, "model": a.model, "rate": int(rate),
               "core_sha256": hashlib.sha256(a.core.read_bytes()).hexdigest(),
               "runner_sha256": hashlib.sha256(a.runner.read_bytes()).hexdigest(),
               "script_sha256": hashlib.sha256(script.read_bytes()).hexdigest(),
               "origin_ns": 0, "held_key_set2": "E0 71", "duration_ns": 30_000_000_000,
               "qualification": "CAPTURE ONLY; inspect desktop.png before cold use"}
    with (out / "runner.log").open("wb") as log:
        result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    custody["runner_exit"] = result.returncode
    (out / "capture-custody.json").write_text(json.dumps(custody, indent=2) + "\n")
    if result.returncode:
        raise SystemExit("factory runner failed; baseline unqualified")
    with (out / "frames.csv").open(newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows or any(r.get("status") != "OK" for r in rows):
        raise SystemExit("factory CSV missing/failed; baseline unqualified")
    baseline = (out / "cmos-final.bin").read_bytes()
    writes = sum(",cmos_write," in line for line in (out / "boot-devices.csv").read_text().splitlines())
    if len(baseline) != 256 or writes < 200:
        raise SystemExit("firmware reset writes not demonstrated; baseline unqualified")
    custody.update(cmos_sha1=hashlib.sha1(baseline).hexdigest(), firmware_cmos_writes=writes,
                   actual_emu_ns=int(rows[-1]["emu_ns"]))
    (out / "capture-custody.json").write_text(json.dumps(custody, indent=2) + "\n")
    print(json.dumps(custody, indent=2))


if __name__ == "__main__":
    main()

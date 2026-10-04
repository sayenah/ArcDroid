#!/usr/bin/env python3
"""Independent captured PCM/container/count/hash checks, not an audible verdict."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import wave

p = argparse.ArgumentParser()
p.add_argument("session", type=Path)
p.add_argument("--segment", action="append", default=[], help="active virtual seconds START:END")
p.add_argument("--output", type=Path, required=True)
args = p.parse_args()
segments = [tuple(map(float, s.split(":"))) for s in args.segment]
assert all(0 <= a < b for a, b in segments)
core = list(csv.DictReader((args.session / "frames.csv").open()))
front = list(csv.DictReader((args.session / "audio.wav.csv").open()))
assert core and len(core) == len(front), "Missing callback rows"
rolling = 1469598103934665603
sha = hashlib.sha256()
pairs = nonzero = clipped = peak = 0
active = [{"start_seconds": a, "end_seconds": b, "callbacks": 0,
           "silent_callbacks": 0, "nonzero_pairs": 0} for a, b in segments]
with wave.open(str(args.session / "audio.wav"), "rb") as wav:
    assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) == (2, 2, 48000, "NONE")
    expected = wav.getnframes()
    for c, f in zip(core, front):
        assert c["status"] == "OK" and c["frame"] == f["frame"]
        count = int(c["audio_frames"])
        assert count == int(f["audio_frames"])
        raw = wav.readframes(count)
        assert len(raw) == count * 4, "Short PCM capture"
        sha.update(raw)
        for byte in raw:
            rolling = ((rolling ^ byte) * 1099511628211) & 0xffffffffffffffff
        assert "%016x" % rolling == c["audio_hash"], "PCM differs from core hash"
        nz = cp = pk = 0
        for left, right in struct.iter_unpack("<hh", raw):
            nz += bool(left or right)
            cp += left in (-32768, 32767) or right in (-32768, 32767)
            pk = max(pk, abs(left), abs(right))
        assert (nz, cp, pk) == (int(f["nonzero_pairs"]), int(f["clipped_pairs"]), int(f["peak"]))
        pairs += count
        nonzero += nz
        clipped += cp
        peak = max(peak, pk)
        t = int(c["emu_ns"]) / 1e9
        for s in active:
            if s["start_seconds"] <= t < s["end_seconds"]:
                s["callbacks"] += 1
                s["silent_callbacks"] += not nz
                s["nonzero_pairs"] += nz
    assert pairs == expected and not wav.readframes(1), "PCM duration mismatch"
result = {"container_count_and_pcm_hash": "PASS", "frames": len(core),
          "stereo_pairs": pairs, "nonzero_pairs": nonzero, "clipped_pairs": clipped,
          "peak": peak, "pcm_sha256": sha.hexdigest(), "active_segments": active,
          "core_underrun_max": max(int(c["underruns"]) for c in core),
          "core_overrun_max": max(int(c["overruns"]) for c in core),
          "audible_fidelity": "MISSING", "physical_output_verification": "MISSING"}
args.output.write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({k: result[k] for k in ("container_count_and_pcm_hash", "frames", "stereo_pairs", "nonzero_pairs", "clipped_pairs", "peak")}))

#!/usr/bin/env python3
"""Collect/recompute full instrumentation on/off pairs; diagnostic, not AC-8.

The runner measures the entire retro_run call, including all instrumentation
and raw writes. Its independent AV ledger is hashed outside that call. Both
legs use identical immutable inputs and fresh scratch/overlays. GPL-2.0-only.
"""
import argparse
import csv
import json
from pathlib import Path
import statistics
import platform
import shutil
import subprocess
import time

from evidence import completion, digest, EvidenceError, quantile, read_rows


def recompute(directory):
    directory = Path(directory)
    manifest = json.loads((directory / "overhead.json").read_text())
    if manifest.get("kind") != "diagnostic-overhead" or len(manifest["pairs"]) < 3:
        raise EvidenceError("three diagnostic on/off pairs required")
    locations = directory / 'input-locations.json'
    locations = json.loads(locations.read_text()) if locations.exists() else {}
    for name, expected in manifest["immutable_sha256"].items():
        actual = Path(locations.get(name, name))
        if not actual.is_absolute():
            actual = directory / actual
        if digest(actual) != expected:
            raise EvidenceError("immutable overhead input changed", "FAIL")
    pairs = []
    for pair in manifest["pairs"]:
        legs = {}
        for mode in ("on", "off"):
            leg = pair[mode]
            if leg.get("exit") != 0:
                raise EvidenceError("overhead runner failed/not completed", "FAIL")
            p = directory / leg["directory"]
            rows = read_rows(p / "timing.csv", ("frame", "emu_ns", "period_ns", "exec_ns"))
            completion(p / "timing.csv", rows, expected_ns=manifest["requested_ns"])
            with (p / "ledger.csv").open(newline="") as f:
                ledger = list(csv.DictReader(f))
            if len(ledger) != len(rows):
                raise EvidenceError("incomplete independent AV ledger")
            legs[mode] = rows, ledger
        on, off = legs["on"], legs["off"]
        if on[1] != off[1] or any((x["frame"], x["emu_ns"], x["period_ns"]) != (y["frame"], y["emu_ns"], y["period_ns"]) for x, y in zip(on[0], off[0])):
            raise EvidenceError("on/off guest output differs", "FAIL")
        if len(on[0]) != len(off[0]):
            raise EvidenceError("on/off frame count differs", "FAIL")
        differences = [(x["exec_ns"] - y["exec_ns"]) / x["period_ns"] for x, y in zip(on[0], off[0]) if manifest.get('window_start_ns', 0) < x['emu_ns'] <= manifest.get('window_end_ns', manifest['requested_ns'])]
        if not differences:
            raise EvidenceError('empty overhead phase window')
        pairs.append(dict(mean_delta_T=statistics.mean(differences),
                          p95_delta_T=quantile(differences, .95),
                          median_delta_T=statistics.median(differences), frames=len(differences)))
    means = [p["mean_delta_T"] for p in pairs]
    mean = statistics.mean(means)
    # Small-sample 95% interval: t(df=2)=4.303 for the required three pairs;
    # keep this conservative constant for additional pairs as well.
    radius = 4.303 * statistics.stdev(means) / len(means) ** .5
    lower, upper = mean - radius, mean + radius
    # This declared 5% T overhead bound is a diagnostic engineering budget,
    # additional to (and never a relaxation of) the fixed AC-8 thresholds.
    bound = manifest["declared_bound_T"]
    status = "FAIL" if lower > bound else "PASS" if lower >= 0 and upper <= bound else "MISSING"
    return dict(status=status, kind="diagnostic-overhead", pairs=pairs,
                mean_delta_T=mean, mean_95_interval_T=[lower, upper], declared_bound_T=bound,
                interpretation="Complete instrumentation including core CSV/events; common runner callbacks. MISSING means host variance does not resolve the bound.",
                private_gameplay_platform_transfer="MISSING until repeated on the final workload/binary/platform")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runner", type=Path)
    p.add_argument("--core", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--seconds", type=float, default=5)
    p.add_argument("--options", type=Path, help="JSON list of core key=value options; paths remain private")
    p.add_argument("--recompute", action="store_true")
    p.add_argument("--start-seconds", type=float, default=0)
    p.add_argument("--end-seconds", type=float)
    a = p.parse_args()
    try:
        if not a.recompute:
            if not a.runner or not a.core or not (0 < a.seconds <= 3600):
                p.error("runner/core and bounded duration required")
            a.output.mkdir(parents=True, exist_ok=False)
            binaries = a.output / 'binary'
            binaries.mkdir()
            # Build products can be rebuilt while the evidence is retained.
            # Run frozen copies and keep their paths relative to the raw suite.
            for name in ('runner', 'core'):
                source = getattr(a, name).resolve()
                frozen = binaries / source.name
                shutil.copy2(source, frozen)
                setattr(a, name, frozen)
            opts = json.loads(a.options.read_text()) if a.options else ["rpcprobe_authored=enabled", "rpcprobe_model=SA110", "rpcprobe_rate=20"]
            if not isinstance(opts, list) or any(not isinstance(x, str) or "=" not in x for x in opts):
                raise EvidenceError("options must be a JSON list of key=value strings")
            reserved = ("rpcprobe_output", "rpcprobe_measurement", "rpcprobe_diagnostics", "rpcprobe_register_trace", "rpcprobe_activity", "rpcprobe_expected_seconds")
            if any(x.split("=", 1)[0] in reserved for x in opts):
                raise EvidenceError("overhead options override controlled instrumentation settings")
            inputs = [a.runner.resolve(), a.core.resolve()]
            for x in opts:
                value = x.split("=", 1)[1]
                if Path(value).is_file():
                    inputs.append(Path(value).resolve())
            end = a.seconds if a.end_seconds is None else a.end_seconds
            if not 0 <= a.start_seconds < end <= a.seconds:
                raise EvidenceError('invalid overhead window')
            manifest = dict(kind="diagnostic-overhead", requested_ns=int(a.seconds * 1e9),
                            window_start_ns=int(a.start_seconds * 1e9), window_end_ns=int(end * 1e9),
                            core_sha256=digest(a.core), host=dict(system=platform.system(), machine=platform.machine()),
                            declared_bound_T=.05, options=opts, pairs=[],
                            immutable_sha256={(str(x.relative_to(a.output.resolve())) if x.is_relative_to(a.output.resolve()) else str(x)): digest(x) for x in inputs})
            m = a.output / "overhead.json"
            # Declaration and immutable hashes are on disk BEFORE any run.
            m.write_text(json.dumps(manifest, indent=2) + "\n")
            for i in range(3):
                pair = {}
                manifest["pairs"].append(pair)
                for mode in (("on", "off") if i % 2 == 0 else ("off", "on")):
                    out = a.output / f"pair-{i}-{mode}"
                    out.mkdir()
                    command = [str(a.runner.resolve()), str(a.core.resolve()), str(a.seconds), "unthrottled", *opts,
                               "rpcprobe_measurement=" + ("enabled" if mode == "on" else "disabled"),
                               "rpcprobe_output=" + str(out.resolve()), "rpcprobe_diagnostics=disabled",
                               "rpcprobe_register_trace=disabled", "rpcprobe_activity=disabled",
                               "--timing=" + str((out / "timing.csv").resolve()), "--ledger=" + str((out / "ledger.csv").resolve())]
                    leg = dict(directory=out.name, argv=command, start=time.time())
                    pair[mode] = leg
                    m.write_text(json.dumps(manifest, indent=2) + "\n")
                    with (out / "runner.log").open("w") as log:
                        process = subprocess.Popen(command, stdout=log, stderr=log)
                        leg["pid"] = process.pid
                        m.write_text(json.dumps(manifest, indent=2) + "\n")
                        leg["exit"] = process.wait()
                    leg["finish"] = time.time()
                    m.write_text(json.dumps(manifest, indent=2) + "\n")
                    if leg["exit"]:
                        raise EvidenceError("overhead leg failed", "FAIL")
        result = recompute(a.output)
        (a.output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        return 0 if result["status"] == "PASS" else 1 if result["status"] == "FAIL" else 2
    except (EvidenceError, OSError, ValueError, KeyError) as e:
        print(getattr(e, "status", "MISSING"), str(e))
        return 1 if getattr(e, "status", "MISSING") == "FAIL" else 2


if __name__ == "__main__":
    raise SystemExit(main())

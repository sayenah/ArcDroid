#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check the local-tests ON/OFF registration delta against the nightly shards.

Only CTest listings are read; no emulator or golden matrix is executed.
"""
import argparse
from collections import Counter
import copy
import json
from pathlib import Path
import re
import subprocess
import sys


LOCAL = "rpc-lockstep-local"


def verify_workflow(workflow, count):
    # This job deliberately uses a literal, JSON-compatible matrix. Refuse a
    # different spelling so the guard cannot silently miss a scheduling change.
    job = re.search(r"^  host-lockstep-local:\n(.*?)(?=^  [\w-]+:|\Z)", workflow, re.M | re.S)
    if job is None:
        return ["missing host-lockstep-local job"]
    matrix = re.search(r"^        shard: (\[[^\n]+\])$", job[1], re.M)
    if matrix is None or json.loads(matrix[1]) != list(range(count)):
        return ["workflow shard IDs differ from manifest"]
    if "-L '^rpc-lockstep-local-${{ matrix.shard }}$'" not in job[1] or "--no-tests=error" not in job[1]:
        return ["workflow shard selection changed"]
    return []


def listing(build):
    return json.loads(subprocess.check_output(
        ["ctest", "--test-dir", str(build), "--show-only=json-v1"],
        text=True, encoding="utf-8"))["tests"]


def verify(on, off, shards):
    """Return errors, independently of how CMake assigns the labels."""
    errors = []
    on_names = {t["name"] for t in on}
    off_names = {t["name"] for t in off}
    local = on_names - off_names
    if off_names - on_names:
        errors.append("ON configuration lost tests: " + ", ".join(sorted(off_names - on_names)))
    if not local:
        errors.append("empty local-tests registration delta")
    if not shards or any(not shard for shard in shards):
        errors.append("empty shard")
    placed = Counter(name for shard in shards for name in shard)
    for name, count in sorted(placed.items()):
        if count != 1:
            errors.append(f"test placed {count} times: {name}")
    if local - placed.keys():
        errors.append("unplaced local tests: " + ", ".join(sorted(local - placed.keys())))
    if placed.keys() - local:
        errors.append("placements outside local-tests delta: " + ", ".join(sorted(placed.keys() - local)))
    for tests, enabled in ((on, True), (off, False)):
        for test in tests:
            name = test["name"]
            labels = next((p["value"] for p in test.get("properties", []) if p["name"] == "LABELS"), [])
            actual = {label for label in labels if label == LOCAL or label.startswith(LOCAL + "-")}
            expected = set()
            if enabled and name in local:
                expected.add(LOCAL)
                expected.update(f"{LOCAL}-{i}" for i, shard in enumerate(shards) if name in shard)
            if actual != expected:
                errors.append(f"incorrect shard labels: {name}: {sorted(actual)} != {sorted(expected)}")
    return errors


def self_test():
    def test(name, labels=()):
        return {"name": name, "properties": [{"name": "LABELS", "value": list(labels)}]}
    off = [test("ordinary")]
    on = off + [test("local-a", (LOCAL, LOCAL + "-0")), test("local-b", (LOCAL, LOCAL + "-1"))]
    shards = [["local-a"], ["local-b"]]
    if verify(on, off, shards):
        raise RuntimeError("valid partition refused")
    mutants = {
        "unplaced test": (on + [test("new-unplaced-local-test", (LOCAL,))], off, shards),
        "duplicate placement": (on, off, [["local-a"], ["local-a", "local-b"]]),
        "unknown placement": (on, off, [["local-a"], ["local-b", "unknown"]]),
        "ordinary test in shard": (on, off, [["local-a", "ordinary"], ["local-b"]]),
        "empty shard": (on, off, shards + [[]]),
        "empty delta": (off, off, shards),
        "lost ordinary test": (on[1:], off, shards),
    }
    wrong = copy.deepcopy(on)
    wrong[-1]["properties"][0]["value"].append(LOCAL + "-0")
    mutants["multiple shard labels"] = (wrong, off, shards)
    mutants["local test leaked with option OFF"] = (on, off + [on[-1]], shards)
    for name, args in mutants.items():
        if not verify(*args):
            raise RuntimeError("surviving mutant: " + name)
        print("killed:", name)
    workflow = "  host-lockstep-local:\n        shard: [0, 1]\n    run: ctest --no-tests=error -L '^rpc-lockstep-local-${{ matrix.shard }}$'\n"
    if verify_workflow(workflow, 2):
        raise RuntimeError("valid workflow refused")
    for matrix in ("[0]", "[0, 0]", "[1, 2]"):
        if not verify_workflow(workflow.replace("[0, 1]", matrix), 2):
            raise RuntimeError("surviving workflow matrix mutant")
    print("killed: omitted, repeated and shifted workflow shard IDs")
    print("lockstep shard guard: self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--on", type=Path, help="lockstep build with local tests ON")
    parser.add_argument("--off", type=Path, help="otherwise identical build with local tests OFF")
    parser.add_argument("--manifest", type=Path, default=Path(__file__).resolve().parents[1] /
                        "cmake/rpc-lockstep-local-shards.json")
    parser.add_argument("--shard-count", type=int, help="number of jobs in the CI matrix")
    parser.add_argument("--workflow", type=Path, default=Path(__file__).resolve().parents[1] /
                        ".github/workflows/ci.yml")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.on is None or args.off is None:
        parser.error("--on and --off are required")
    for build, expected in ((args.on, "ON"), (args.off, "OFF")):
        cache = (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
        if ("ARCDROID_RPC_LOCKSTEP:BOOL=ON" not in cache or
                f"ARCDROID_RPC_LOCKSTEP_LOCAL_TESTS:BOOL={expected}" not in cache):
            raise ValueError(f"expected lockstep ON and local tests {expected}")
    shards = json.loads(args.manifest.read_text(encoding="utf-8"))["shards"]
    if (not isinstance(shards, list) or not shards or
            any(not isinstance(s, list) or any(not isinstance(n, str) or not n for n in s) for s in shards)):
        raise ValueError("invalid shard manifest")
    if args.shard_count is not None and args.shard_count != len(shards):
        raise ValueError("workflow matrix size differs from manifest")
    errors = verify(listing(args.on), listing(args.off), shards)
    errors += verify_workflow(args.workflow.read_text(encoding="utf-8"), len(shards))
    if errors:
        for error in errors:
            print("lockstep shard guard:", error, file=sys.stderr)
        return 1
    print(f"lockstep shard guard: {sum(map(len, shards))} local tests, exactly once in {len(shards)} shards")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print("lockstep shard guard:", error, file=sys.stderr)
        raise SystemExit(1)

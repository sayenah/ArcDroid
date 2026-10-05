"""Shared raw-run integrity checks. GPL-2.0-only. No inferred completion."""
import csv
import hashlib
import json
import math
from pathlib import Path
import re


class EvidenceError(ValueError):
    def __init__(self, reason, status="MISSING"):
        super().__init__(reason)
        self.status = status


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read_rows(path, numeric, required=()):
    try:
        with Path(path).open(newline="") as f:
            rows = list(csv.DictReader(f))
        if not rows:
            raise EvidenceError("empty CSV")
        for r in rows:
            if None in r or any(r.get(k) in (None, "") for k in (*numeric, *required)):
                raise EvidenceError("missing/truncated CSV fields")
            for k in numeric:
                r[k] = int(r[k])
                if r[k] < 0:
                    raise EvidenceError("negative " + k)
            if r.get("status", "OK") != "OK":
                raise EvidenceError("explicit core failure", "FAIL")
        if rows[0]["frame"] != 0:
            raise EvidenceError("missing initial frame")
        for x, y in zip(rows, rows[1:]):
            if y["frame"] != x["frame"] + 1 or y["emu_ns"] <= x["emu_ns"]:
                raise EvidenceError("missing/reset/nonmonotonic frames")
            if "wall_ns" in y and y["wall_ns"] < x["wall_ns"]:
                raise EvidenceError("nonmonotonic wall clock")
        if "period_ns" in numeric and any(r["period_ns"] <= 0 for r in rows):
            raise EvidenceError("zero frame period")
        return rows
    except (OSError, KeyError, TypeError, ValueError) as e:
        if isinstance(e, EvidenceError):
            raise
        raise EvidenceError(str(e)) from e


CORE_NUMERIC = ("frame", "emu_ns", "period_ns", "exec_ns", "instructions",
                "audio_frames", "underruns", "overruns", "wall_ns", "instrument_ns")


def core_rows(path):
    rows = read_rows(path, CORE_NUMERIC, ("video_hash", "audio_hash", "status"))
    for r in rows:
        if any(re.fullmatch(r"[0-9a-f]{16}", r[k]) is None for k in ("video_hash", "audio_hash")):
            raise EvidenceError("invalid AV hash")
    return rows


def include_csv_cost(path, rows):
    """Use primary CSV append costs; historical raw exec_ns excludes them."""
    try:
        with Path(path).open(newline="") as f:
            costs = list(csv.DictReader(f))
        if len(costs) != len(rows):
            raise EvidenceError("missing CSV write cost rows")
        for row, cost in zip(rows, costs):
            ns = int(cost["csv_append_ns"])
            if int(cost["frame"]) != row["frame"] or ns < 0:
                raise EvidenceError("invalid CSV write cost")
            row["exec_ns"] += ns
        return rows
    except (OSError, ValueError, KeyError, TypeError) as e:
        if isinstance(e, EvidenceError):
            raise
        raise EvidenceError(str(e)) from e


def completion(csv_path, rows, footer=None, expected_ns=None):
    """Validate core JSON or exactly one runner footer, including row count.

    Historical runner artifacts can use runner.log. RetroArch needs the core's
    final JSON. A clean-looking last row by itself never establishes completion.
    """
    parent = Path(csv_path).parent
    path = Path(footer) if footer else parent / "run-footer.json"
    try:
        if path.exists():
            f = json.loads(path.read_text())
        else:
            path = Path(footer) if footer else parent / "runner.log"
            matches = re.findall(r"runner status=(\w+) frames=(\d+) requested_ns=(\d+) advanced_ns=(\d+) wall_ns=(\d+)", path.read_text())
            if len(matches) != 1:
                raise EvidenceError("requires exactly one final runner footer")
            status, frames, requested, advanced, wall = matches[0]
            f = dict(status=status, frames=int(frames), requested_ns=int(requested), advanced_ns=int(advanced), wall_ns=int(wall))
        if f["status"] == "FAIL" or f.get("reset"):
            raise EvidenceError("failed/reset completion footer", "FAIL")
        if f["status"] != "COMPLETE":
            raise EvidenceError("incomplete completion footer")
        for k in ("frames", "requested_ns", "advanced_ns"):
            if type(f[k]) is not int or f[k] <= 0:
                raise EvidenceError("invalid completion " + k)
        if f["frames"] != len(rows) or f["advanced_ns"] != rows[-1]["emu_ns"]:
            raise EvidenceError("footer/CSV disagreement", "FAIL")
        if f["advanced_ns"] < f["requested_ns"]:
            raise EvidenceError("shortened run", "FAIL")
        if expected_ns is not None and f["requested_ns"] != expected_ns:
            raise EvidenceError("duration differs from predeclared request", "FAIL")
        # A single final frame may exceed the requested duration. More suggests
        # a wrong declaration, delayed collection, or unrelated footer.
        period = rows[-1].get("period_ns", 100000000)
        if f["advanced_ns"] - f["requested_ns"] > period:
            raise EvidenceError("unexpected run duration overshoot", "FAIL")
        return dict(f, raw_file=str(path), sha256=digest(path))
    except (OSError, KeyError, TypeError, ValueError) as e:
        if isinstance(e, EvidenceError):
            raise
        raise EvidenceError(str(e)) from e


def quantile(values, p):
    if not values or not all(math.isfinite(x) for x in values):
        raise EvidenceError("empty/nonfinite measurement")
    return sorted(values)[max(0, math.ceil(len(values) * p) - 1)]

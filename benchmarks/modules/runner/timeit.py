#!/usr/bin/env python3
"""Time an external command N times and report median/min/max/spread.

Usage:
  timeit.py --label NAME [--reps N] [--warmup W] [--out FILE] -- CMD [ARGS...]

Prints one JSON object per run to stdout (or appends to --out):
  {"label":..., "reps":N, "samples":[...], "median":..., "min":..., "max":...,
   "mean":..., "stdev":..., "spread_pct": (max-min)/median*100, "rc":[..]}

`rc` records each run's exit status so a failing command is never silently
counted as a timing. A non-zero command makes the script exit non-zero.
"""
from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--out", type=str)
    ap.add_argument("--timeout", type=float, default=1800.0)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    args = ap.parse_args()

    cmd = args.cmd
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        print("timeit: no command given", file=sys.stderr)
        return 2

    samples: list[float] = []
    rcs: list[int] = []
    for i in range(args.warmup + args.reps):
        t0 = time.perf_counter()
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=args.timeout)
        dt = time.perf_counter() - t0
        if i < args.warmup:
            continue
        samples.append(dt)
        rcs.append(r.returncode)

    med = statistics.median(samples)
    rec = {
        "label": args.label,
        "reps": args.reps,
        "samples": [round(s, 6) for s in samples],
        "median": round(med, 6),
        "min": round(min(samples), 6),
        "max": round(max(samples), 6),
        "mean": round(statistics.fmean(samples), 6),
        "stdev": round(statistics.pstdev(samples), 6) if len(samples) > 1 else 0.0,
        "spread_pct": round((max(samples) - min(samples)) / med * 100.0, 2) if med else 0.0,
        "rc": rcs,
    }
    line = json.dumps(rec)
    if args.out:
        with open(args.out, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    else:
        print(line)
    return 0 if all(rc == 0 for rc in rcs) else 1


if __name__ == "__main__":
    raise SystemExit(main())

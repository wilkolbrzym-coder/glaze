#!/usr/bin/env python3
"""Time an external command N times and report median/min/max/spread.

Usage:
  timeit.py --label NAME [--reps N] [--warmup W] [--out FILE] [--pin CPU] -- CMD [ARGS...]

For every run it records BOTH:
  * wall time  (perf_counter around the subprocess), and
  * child CPU time (user+sys from resource.getrusage(RUSAGE_CHILDREN) deltas).

CPU time is much less sensitive to concurrent machine load, so on a busy box it
is the trustworthy number; wall time is reported alongside for reproducibility.

Prints/appends one JSON object per invocation:
  {"label":..., "reps":N, "wall_samples":[...], "cpu_samples":[...],
   "wall_median":..., "wall_min":..., "wall_max":..., "wall_spread_pct":...,
   "cpu_median":..., "cpu_min":..., "cpu_max":..., "cpu_spread_pct":..., "rc":[..]}

A non-zero command exit makes the script exit non-zero; a failing command is
never silently counted as a timing.
"""
from __future__ import annotations

import argparse
import json
import resource
import statistics
import subprocess
import sys
import time


def cpu_children() -> float:
    r = resource.getrusage(resource.RUSAGE_CHILDREN)
    return r.ru_utime + r.ru_stime


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--out", type=str)
    ap.add_argument("--pin", type=int, default=-1, help="optionally pin to one CPU")
    ap.add_argument("--timeout", type=float, default=1800.0)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    args = ap.parse_args()

    cmd = args.cmd
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        print("timeit: no command given", file=sys.stderr)
        return 2
    if args.pin >= 0:
        cmd = ["taskset", "-c", str(args.pin)] + cmd

    wall: list[float] = []
    cpu: list[float] = []
    rcs: list[int] = []
    for i in range(args.warmup + args.reps):
        c0 = cpu_children()
        t0 = time.perf_counter()
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=args.timeout)
        dt = time.perf_counter() - t0
        dcpu = cpu_children() - c0
        if i < args.warmup:
            continue
        wall.append(dt)
        cpu.append(dcpu)
        rcs.append(r.returncode)

    def stats(xs: list[float]) -> dict:
        med = statistics.median(xs)
        return {
            "median": round(med, 6),
            "min": round(min(xs), 6),
            "max": round(max(xs), 6),
            "mean": round(statistics.fmean(xs), 6),
            "stdev": round(statistics.pstdev(xs), 6) if len(xs) > 1 else 0.0,
            "spread_pct": round((max(xs) - min(xs)) / med * 100.0, 2) if med else 0.0,
        }

    ws, cs = stats(wall), stats(cpu)
    rec = {
        "label": args.label,
        "reps": args.reps,
        "wall_samples": [round(s, 6) for s in wall],
        "cpu_samples": [round(s, 6) for s in cpu],
        "wall_median": ws["median"], "wall_min": ws["min"], "wall_max": ws["max"],
        "wall_mean": ws["mean"], "wall_stdev": ws["stdev"], "wall_spread_pct": ws["spread_pct"],
        "cpu_median": cs["median"], "cpu_min": cs["min"], "cpu_max": cs["max"],
        "cpu_mean": cs["mean"], "cpu_stdev": cs["stdev"], "cpu_spread_pct": cs["spread_pct"],
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

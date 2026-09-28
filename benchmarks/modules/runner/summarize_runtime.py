#!/usr/bin/env python3
"""Aggregate raw `RT ...` lines from one or more runs into per-op medians.

Usage:
  summarize_runtime.py --path LABEL --compiler LABEL --files a.txt [b.txt ...] [--tsv metrics.tsv]

Groups lines by (format, op) and reports median/min/max of ns_per_op and
mb_per_s across runs, plus the serialized byte count. It refuses to aggregate
runs whose `bytes=` differ for the same (format, op), and reports the number of
runs found, so a single-run result is visible rather than implied.
"""
from __future__ import annotations

import argparse
import statistics
import sys


def parse_line(line: str):
    parts = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            parts[k] = v
    if "format" not in parts or "op" not in parts:
        return None
    return parts


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--path", required=True)
    ap.add_argument("--compiler", required=True)
    ap.add_argument("--files", nargs="+", required=True)
    ap.add_argument("--tsv", type=str)
    args = ap.parse_args()

    groups: dict[tuple[str, str], list[dict]] = {}
    for fn in args.files:
        try:
            fh = open(fn, encoding="utf-8", errors="replace")
        except FileNotFoundError:
            continue
        for line in fh:
            if not line.startswith("RT "):
                continue
            p = parse_line(line)
            if p:
                groups.setdefault((p["format"], p["op"]), []).append(p)

    if not groups:
        print(f"summarize_runtime: no RT lines in {args.files}", file=sys.stderr)
        return 1

    has_cpu = all("cpu_ns_per_op" in r for runs in groups.values() for r in runs)

    def spread(xs, med):
        return (max(xs) - min(xs)) / med * 100.0 if med else 0.0

    tsv_rows = []
    print(f"-- runtime medians ({args.path}, {args.compiler})")
    if has_cpu:
        print("   CPU ns/op and CPU MB/s are load-robust (process CPU time);")
        print("   wall ns/op / MB/s are reported for reference and are load-dominated.")
    print(f"{'format':7s} {'op':5s} {'runs':>4s} {'bytes':>7s} | {'cpu ns/op':>10s} {'cpu MB/s':>9s} "
          f"{'cpu_spr%':>8s} | {'wall ns/op':>10s} {'wall MB/s':>9s} {'wall_spr%':>9s}")
    for (fmt, op), runs in sorted(groups.items()):
        nss = [float(r["ns_per_op"]) for r in runs]
        mbs = [float(r["mb_per_s"]) for r in runs]
        bys = {int(r["bytes"]) for r in runs}
        if len(bys) != 1:
            print(f"   ERROR: {fmt}/{op}: byte counts differ across runs: {sorted(bys)}", file=sys.stderr)
            return 2
        b = bys.pop()
        med_ns = statistics.median(nss)
        med_mb = statistics.median(mbs)
        cns = [float(r.get("cpu_ns_per_op", "nan")) for r in runs]
        cmb = [float(r.get("cpu_mb_per_s", "nan")) for r in runs]
        med_cns = statistics.median(cns) if has_cpu else float("nan")
        med_cmb = statistics.median(cmb) if has_cpu else float("nan")
        spr_cns = spread(cns, med_cns)
        print(f"{fmt:7s} {op:5s} {len(runs):4d} {b:7d} | {med_cns:10.1f} {med_cmb:9.1f} {spr_cns:8.1f} | "
              f"{med_ns:10.1f} {med_mb:9.1f} {spread(nss, med_ns):9.1f}")
        if has_cpu:
            tsv_rows.append(("runtime_cpu_ns_per_op", args.path, args.compiler, f"{med_cns:.1f}", "ns/op",
                             f"{spr_cns:.1f}", f"{fmt} {op}, {len(runs)} runs"))
            tsv_rows.append(("runtime_cpu_mb_per_s", args.path, args.compiler, f"{med_cmb:.1f}", "MB/s",
                             f"{spread(cmb, med_cmb):.1f}", f"{fmt} {op}, {len(runs)} runs"))
        tsv_rows.append(("runtime_wall_ns_per_op", args.path, args.compiler, f"{med_ns:.1f}", "ns/op",
                         f"{spread(nss, med_ns):.1f}", f"{fmt} {op}, {len(runs)} runs"))

    if args.tsv:
        with open(args.tsv, "a", encoding="utf-8") as f:
            for row in tsv_rows:
                f.write("\t".join(row) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

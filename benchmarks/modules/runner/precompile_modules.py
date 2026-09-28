#!/usr/bin/env python3
"""Dependency-closure module precompiler for the Glaze modules benchmark.

Given one or more module roots (e.g. ``glaze.json``), this tool walks the
``import`` graph of ``modules/**/*.ixx``, computes the transitive closure
(std and non-glaze imports excluded), topologically orders it, and precompiles
each unit with clang++ into one BMI (``.pcm``) plus one object file.

It deliberately builds *only the closure of the requested roots* so the
benchmark does not depend on unrelated, heavyweight modules (net/openssl,
api, ...) that are irrelevant to a JSON/BEVE/CBOR/TOML workload.

Everything is cached by source hash, so a repeated run is cheap and lets the
caller measure the cost of compiling a *consumer* TU with BMIs already present
separately from the one-off cost of building the BMIs.

Outputs (all under --work):
  pcm/<module>.pcm      one BMI per module unit
  obj/<module>.o        one object per module unit (needed to link importers)
  manifest.json         {roots, units:[{name,status,pcm,obj,seconds}], ok}

Nothing here writes to the tracked tree.
"""

from __future__ import annotations

import argparse
import concurrent.futures as cf
import hashlib
import json
import re
import subprocess
import sys
import time
from pathlib import Path

MODULE_DECL = re.compile(r"^\s*(?:export\s+)?module\s+([A-Za-z_][\w.]*(?::[A-Za-z_][\w.]*)?)\s*;")
IMPORT_DECL = re.compile(r"^\s*(?:export\s+)?import\s+([^;]+?)\s*;")


def parse_unit(p: Path) -> dict:
    name = None
    imports: list[str] = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if name is None:
            m = MODULE_DECL.match(line)
            if m:
                name = m.group(1)
                continue
        m = IMPORT_DECL.match(line)
        if m:
            t = m.group(1).strip()
            if t in ("std",) or re.fullmatch(r"[A-Za-z_][\w.]*", t) or t.startswith(":"):
                imports.append(t)
    return {"path": p, "name": name, "imports": imports}


def sanitize(name: str) -> str:
    return name.replace(".", "_").replace(":", "__")


class Driver:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.root = args.root.resolve()
        self.modules = (args.modules or (self.root / "modules")).resolve()
        self.include = (args.include or (self.root / "include")).resolve()
        self.work = args.work.resolve()
        self.pcm_dir = self.work / "pcm"
        self.obj_dir = self.work / "obj"
        self.log_dir = self.work / "logs"
        for d in (self.pcm_dir, self.obj_dir, self.log_dir):
            d.mkdir(parents=True, exist_ok=True)
        self.std_pcm = args.std_pcm.resolve()
        self.cxx = args.cxx
        self.cflag = f"-fmodule-file=std={self.std_pcm}"
        self.results: dict[str, dict] = {}

    def base_flags(self) -> list[str]:
        return [
            "-std=c++23",
            "-stdlib=libc++",
            self.cflag,
            f"-fprebuilt-module-path={self.pcm_dir}",
            "-I", str(self.modules),
            "-I", str(self.include),
            "-Wno-everything",
        ]

    def run(self, cmd: list[str], timeout: int = 900):
        t0 = time.time()
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.returncode, (r.stdout + r.stderr).strip(), time.time() - t0

    @staticmethod
    def first_error(log: str) -> str:
        for line in log.splitlines():
            if " error:" in line or "fatal error" in line:
                return line.strip()[:300]
        return log.splitlines()[-1].strip()[:300] if log else ""

    def discover(self) -> None:
        units = [parse_unit(p) for p in sorted(self.modules.rglob("*.ixx"))]
        missing = [str(u["path"]) for u in units if not u["name"]]
        if missing:
            raise SystemExit(f"setup error: units without module declaration: {missing}")
        self.by_name = {u["name"]: u for u in units}

        def resolve(t: str, owner: str) -> str | None:
            if t == "std":
                return None
            if t.startswith(":"):
                return f"{owner}:{t[1:]}"
            return t if t.startswith("glaze") else None

        for u in units:
            owner = u["name"].partition(":")[0]
            u["deps"] = sorted({d for d in (resolve(t, owner) for t in u["imports"]) if d and d != u["name"]})
            for d in u["deps"]:
                if d not in self.by_name:
                    raise SystemExit(f"setup error: {u['name']} imports unknown module {d}")

    def closure(self, roots: list[str]) -> list[dict]:
        seen: set[str] = set()
        order: list[str] = []

        def visit(n: str) -> None:
            if n in seen:
                return
            seen.add(n)
            for d in self.by_name[n]["deps"]:
                visit(d)
            order.append(n)

        for r in roots:
            if r not in self.by_name:
                raise SystemExit(f"unknown module root: {r}")
            visit(r)
        return [self.by_name[n] for n in order]

    def pcm_path(self, name: str) -> Path:
        return self.pcm_dir / (name.replace(":", "-") + ".pcm")

    def obj_path(self, name: str) -> Path:
        return self.obj_dir / (sanitize(name) + ".o")

    def build_one(self, u: dict) -> None:
        name = u["name"]
        src: Path = u["path"]
        pcm = self.pcm_path(name)
        obj = self.obj_path(name)
        key = hashlib.sha1(src.read_bytes()).hexdigest() + "|" + self.cxx
        rec: dict = {"name": name, "file": str(src), "pcm": str(pcm), "obj": str(obj)}

        stamp = pcm.with_suffix(".stamp")
        if pcm.exists() and obj.exists() and stamp.exists() and stamp.read_text().strip() == key:
            rec.update(status="cached", seconds=0.0)
            rec["pcm_bytes"] = pcm.stat().st_size
            rec["obj_bytes"] = obj.stat().st_size
            self.results[name] = rec
            return

        cmd = [self.cxx] + self.base_flags() + [
            "-x", "c++-module", "--precompile", str(src), "-o", str(pcm),
        ]
        rc, log, dt = self.run(cmd)
        if rc != 0 or not pcm.exists():
            if pcm.exists():
                pcm.unlink()
            (self.log_dir / f"precompile_{sanitize(name)}.log").write_text(log)
            rec.update(status="precompile_failed", seconds=round(dt, 2),
                       first_error=self.first_error(log))
            self.results[name] = rec
            return

        cmd = [self.cxx] + self.base_flags() + [
            "-x", "c++-module", "-c", str(src), "-o", str(obj),
            f"-fmodule-output={self.obj_dir / (sanitize(name) + '.pcm')}",
        ]
        rc, log, dt2 = self.run(cmd)
        if rc != 0 or not obj.exists():
            if obj.exists():
                obj.unlink()
            (self.log_dir / f"object_{sanitize(name)}.log").write_text(log)
            rec.update(status="object_failed", seconds=round(dt + dt2, 2),
                       first_error=self.first_error(log))
            self.results[name] = rec
            return

        stamp.write_text(key)
        rec.update(status="ok", seconds=round(dt + dt2, 2),
                   pcm_bytes=pcm.stat().st_size, obj_bytes=obj.stat().st_size)
        self.results[name] = rec

    def build(self, units: list[dict]) -> None:
        pending = list(units)
        jobs = max(1, self.args.jobs)
        with cf.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures: dict = {}
            while pending or futures:
                ready = [u for u in pending
                         if all(d in self.results and self.results[d]["status"] in ("ok", "cached")
                                for d in u["deps"])]
                if not ready and not futures:
                    # a dependency failed: mark everything left as blocked
                    for u in pending:
                        self.results[u["name"]] = {
                            "name": u["name"], "file": str(u["path"]), "status": "blocked",
                            "pcm": str(self.pcm_path(u["name"])), "obj": str(self.obj_path(u["name"])),
                            "first_error": "a dependency failed to build",
                        }
                    pending = []
                    break
                for u in ready:
                    futures[pool.submit(self.build_one, u)] = u
                    pending.remove(u)
                if futures:
                    f = next(cf.as_completed(list(futures)))
                    futures.pop(f)
                    f.result()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    ap.add_argument("--modules", type=Path)
    ap.add_argument("--include", type=Path)
    ap.add_argument("--std-pcm", type=Path,
                    default=Path("/home/bosyj/Projects/Modules - Glaze/toolchain/std.pcm"))
    ap.add_argument("--work", type=Path, required=True)
    ap.add_argument("--cxx", default="clang++-22")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--roots", required=True,
                    help="comma separated module names, e.g. glaze.json,glaze.beve")
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()

    if not args.std_pcm.exists():
        print(f"setup error: std.pcm not found at {args.std_pcm}", file=sys.stderr)
        return 2
    drv = Driver(args)
    drv.discover()
    roots = [r.strip() for r in args.roots.split(",") if r.strip()]
    units = drv.closure(roots)
    print(f"== precompile closure of {roots}: {len(units)} units ==")
    ok = fail = 0
    drv.build(units)
    for u in units:
        rec = drv.results.get(u["name"])
        if rec and rec["status"] in ("ok", "cached"):
            ok += 1
        else:
            fail += 1
            if rec:
                print(f"  FAIL {rec['name']}: {rec.get('first_error', '')}")
    manifest = {
        "roots": roots,
        "units": [drv.results[u["name"]] for u in units if u["name"] in drv.results],
        "ok": fail == 0 and len(drv.results) == len(units),
        "ok_count": ok,
        "fail_count": fail,
        "ok_pcms": [drv.results[u["name"]]["pcm"] for u in units
                    if drv.results.get(u["name"], {}).get("status") in ("ok", "cached")],
        "ok_objs": [drv.results[u["name"]]["obj"] for u in units
                    if drv.results.get(u["name"], {}).get("status") in ("ok", "cached")],
    }
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"== closure result: {ok}/{len(units)} ok ==")
    return 0 if manifest["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

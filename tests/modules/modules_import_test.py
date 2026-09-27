#!/usr/bin/env python3
"""Import-level test driver for the Glaze C++20 modules.

For every module unit under modules/**/*.ixx this driver:

  1. precompiles the unit in topological order (clang++-22, libc++),
     producing one BMI per module in <work>/pcm;
  2. generates a tiny translation unit that only `import`s the module and
     compiles *and links* it against the BMIs, proving the unit can stand
     alone in an importing TU.

Nothing here reads or writes include/, modules/ or any other tracked file;
all artefacts go under --work (default /tmp/glz-modules-tests).

Exit status:
  0  every module precompiled and its import TU compiled+linked;
  1  at least one module FAILED (or a behavioural prerequisite is missing);
  2  setup error (toolchain / paths / zero modules discovered).

The intent is that a module which cannot be imported is never silently
skipped: it is either PASS or FAIL, and a partition is reported as covered
by its primary module.
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
        m = MODULE_DECL.match(line)
        if m and name is None:
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
        self.gen_dir = self.work / "import_tu"
        self.obj_dir = self.work / "obj"
        self.bin_dir = self.work / "bin"
        self.log_dir = self.work / "logs"
        for d in (self.pcm_dir, self.gen_dir, self.obj_dir, self.bin_dir, self.log_dir):
            d.mkdir(parents=True, exist_ok=True)
        self.std_pcm = args.std_pcm.resolve()
        self.cxx = args.cxx
        self.results: dict[str, dict] = {}

    # ---------------------------------------------------------------- helpers
    def base_cmd(self) -> list[str]:
        return [
            self.cxx, "-std=c++23", "-stdlib=libc++",
            f"-fmodule-file=std={self.std_pcm}",
            f"-fprebuilt-module-path={self.pcm_dir}",
            "-I", str(self.modules), "-I", str(self.include),
            "-Wno-everything",
        ]

    def run(self, cmd: list[str], timeout: int = 600) -> tuple[int, str, float]:
        t0 = time.time()
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
            out = (r.stdout + r.stderr).strip()
            return r.returncode, out, time.time() - t0
        except subprocess.TimeoutExpired:
            return 124, f"timeout after {timeout}s", time.time() - t0

    @staticmethod
    def first_error(log: str) -> str:
        for l in log.splitlines():
            if " error:" in l or "fatal error" in l:
                return l.strip()[:300]
        return (log.splitlines()[-1].strip()[:300] if log else "")

    # ------------------------------------------------------------- discovery
    def discover(self) -> list[dict]:
        units = [parse_unit(p) for p in sorted(self.modules.rglob("*.ixx"))]
        unnamed = [str(u["path"]) for u in units if not u["name"]]
        if unnamed:
            raise SystemExit(f"setup error: module units without a module declaration: {unnamed}")
        self.units = units
        self.by_name = {u["name"]: u for u in units}

        def resolve(t: str, owner: str) -> str | None:
            if t == "std":
                return "std"
            if t.startswith(":"):
                return f"{owner}:{t[1:]}"
            return t

        for u in units:
            owner = (u["name"] or "").partition(":")[0]
            u["deps"] = sorted(
                {d for d in (resolve(t, owner) for t in u["imports"]) if d and d != u["name"]}
            )

        # topological levels (std external)
        pending = dict(self.by_name)
        built = {"std"}
        levels: list[list[dict]] = []
        while pending:
            ready = [u for u in pending.values()
                     if all(d in built or not d.startswith("glaze") for d in u["deps"])]
            if not ready:
                levels.append(list(pending.values()))
                break
            levels.append(ready)
            for u in ready:
                built.add(u["name"])
                pending.pop(u["name"], None)
        self.levels = levels
        return units

    # ---------------------------------------------------------- precompiling
    def pcm_path(self, name: str) -> Path:
        return self.pcm_dir / f"{name.replace(':', '-')}.pcm"

    def precompile_one(self, u: dict) -> dict:
        name = u["name"]
        src: Path = u["path"]
        out = self.pcm_path(name)
        src_hash = hashlib.sha1(src.read_bytes()).hexdigest()
        stamp = out.with_suffix(".stamp")
        key = src_hash + "|" + self.cxx
        if out.exists() and stamp.exists() and stamp.read_text().strip() == key:
            return {"name": name, "file": str(src), "status": "cached", "log": ""}
        cmd = [self.cxx, "-std=c++23", "-stdlib=libc++", "-x", "c++-module",
               "--precompile", str(src), "-o", str(out),
               f"-fmodule-file=std={self.std_pcm}",
               f"-fprebuilt-module-path={self.pcm_dir}",
               "-I", str(self.modules), "-I", str(self.include),
               "-Wno-everything"]
        rc, log, dt = self.run(cmd)
        ok = rc == 0 and out.exists()
        if ok:
            stamp.write_text(key)
            return {"name": name, "file": str(src), "status": "ok", "seconds": round(dt, 1), "log": ""}
        if out.exists():
            out.unlink()
        (self.log_dir / f"precompile_{sanitize(name)}.log").write_text(log)
        return {"name": name, "file": str(src), "status": "FAIL", "seconds": round(dt, 1),
                "first_error": self.first_error(log), "log": log}

    def precompile_all(self) -> None:
        jobs = self.args.jobs
        with cf.ThreadPoolExecutor(max_workers=jobs) as pool:
            for level in self.levels:
                fut = {pool.submit(self.precompile_one, u): u for u in level}
                for f in cf.as_completed(fut):
                    res = f.result()
                    self.results[res["name"]] = {"precompile": res}

    # --------------------------------------------- object code for linking
    def compile_object_one(self, u: dict) -> str:
        rec = self.results[u["name"]]
        if rec["precompile"]["status"] not in ("ok", "cached"):
            return "skip"
        name = u["name"]
        obj = self.obj_dir / f"{sanitize(name)}.o"
        src: Path = u["path"]
        src_hash = hashlib.sha1(src.read_bytes()).hexdigest()
        key = src_hash + "|obj|" + self.cxx
        stamp = obj.with_suffix(".stamp")
        if obj.exists() and stamp.exists() and stamp.read_text().strip() == key:
            rec["object"] = {"status": "cached", "path": str(obj)}
            return "cached"
        cmd = [self.cxx, "-std=c++23", "-stdlib=libc++", "-x", "c++-module", "-c",
               str(src), "-o", str(obj),
               f"-fmodule-output={self.obj_dir / (sanitize(name) + '.pcm')}",
               f"-fmodule-file=std={self.std_pcm}",
               f"-fprebuilt-module-path={self.pcm_dir}",
               "-I", str(self.modules), "-I", str(self.include),
               "-Wno-everything"]
        rc, log, _ = self.run(cmd)
        if rc == 0 and obj.exists():
            stamp.write_text(key)
            rec["object"] = {"status": "ok", "path": str(obj)}
            return "ok"
        if obj.exists():
            obj.unlink()
        (self.log_dir / f"object_{sanitize(name)}.log").write_text(log)
        rec["object"] = {"status": "FAIL", "first_error": self.first_error(log)}
        return "FAIL"

    def compile_objects(self) -> None:
        ordered = [u for level in self.levels for u in level]
        with cf.ThreadPoolExecutor(max_workers=self.args.jobs) as pool:
            list(pool.map(self.compile_object_one, ordered))

    # ------------------------------------------------------- import TUs pass
    def import_tu_source(self, name: str) -> str:
        return (f"// generated by modules_import_test.py -- import-level test\n"
                f"// module: {name}\n"
                f"import {name};\n"
                f"int main() {{ return 0; }}\n")

    def test_import_one(self, u: dict) -> dict:
        name = u["name"]
        rec = self.results[name]
        partial = {k: v for k, v in u.items() if k != "imports"}
        if ":" in name:
            primary = name.partition(":")[0]
            rec["import"] = {"status": "partition", "primary": primary,
                             "reason": "module partition; covered by its primary module"}
            rec["covered_by"] = primary
            return rec["import"]
        if rec["precompile"]["status"] == "FAIL":
            rec["import"] = {"status": "FAIL", "reason": "module did not precompile",
                             "first_error": rec["precompile"].get("first_error", "")}
            return rec["import"]
        src = self.gen_dir / f"import_{sanitize(name)}.cpp"
        src.write_text(self.import_tu_source(name))
        exe = self.bin_dir / f"import_{sanitize(name)}"
        # Importing a module makes the TU reference that module's initializer,
        # so the module object files are needed to link even an empty main.
        objects = sorted(str(o) for o in self.obj_dir.glob("*.o"))
        cmd = self.base_cmd() + [str(src)] + objects + ["-o", str(exe)]
        rc, log, dt = self.run(cmd)
        if rc == 0 and exe.exists():
            rec["import"] = {"status": "ok", "seconds": round(dt, 1)}
        else:
            (self.log_dir / f"import_{sanitize(name)}.log").write_text(log)
            rec["import"] = {"status": "FAIL", "first_error": self.first_error(log), "log": log}
        return rec["import"]

    def test_import_all(self) -> None:
        # topological order so reports read naturally
        ordered = [u for level in self.levels for u in level]
        jobs = self.args.jobs
        with cf.ThreadPoolExecutor(max_workers=jobs) as pool:
            list(pool.map(self.test_import_one, ordered))

    # ------------------------------------------------------------- reporting
    def summary(self) -> dict:
        pre_ok = pre_fail = 0
        imp_ok = imp_fail = part = 0
        obj_ok = 0
        failures = []
        for name, rec in sorted(self.results.items()):
            p = rec["precompile"]
            if p["status"] in ("ok", "cached"):
                pre_ok += 1
            else:
                pre_fail += 1
            o = rec.get("object", {})
            if o.get("status") in ("ok", "cached"):
                obj_ok += 1
            i = rec.get("import", {})
            st = i.get("status")
            if st == "ok":
                imp_ok += 1
            elif st == "partition" and p["status"] in ("ok", "cached"):
                part += 1
            else:
                # includes partitions whose own precompile failed: a partition
                # is only "covered by its primary" if it actually built.
                imp_fail += 1
                failures.append({
                    "module": name,
                    "file": p["file"],
                    "precompile": p["status"],
                    "precompile_error": p.get("first_error", ""),
                    "import_error": i.get("first_error", ""),
                    "reason": i.get("reason", "") or "module unit did not build",
                })
        total = len(self.results)
        return {
            "units_total": total,
            "precompile_ok": pre_ok,
            "precompile_failed": pre_fail,
            "object_ok": obj_ok,
            "import_ok": imp_ok,
            "import_partition": part,
            "import_failed": imp_fail,
            "failures": failures,
            "pass": total > 0 and pre_fail == 0 and imp_fail == 0,
        }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    ap.add_argument("--modules", type=Path)
    ap.add_argument("--include", type=Path)
    ap.add_argument("--std-pcm", type=Path,
                    default=Path("/home/bosyj/Projects/Modules - Glaze/toolchain/std.pcm"))
    ap.add_argument("--work", type=Path, default=Path("/tmp/glz-modules-tests"))
    ap.add_argument("--cxx", default="clang++-22")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--no-cache", action="store_true")
    args = ap.parse_args()

    if not args.std_pcm.exists():
        print(f"setup error: std.pcm not found at {args.std_pcm}", file=sys.stderr)
        return 2
    drv = Driver(args)
    if args.no_cache:
        for d in (drv.pcm_dir, drv.gen_dir, drv.bin_dir, drv.log_dir):
            for f in d.glob("*"):
                f.unlink()
    if not drv.modules.is_dir():
        print(f"setup error: modules dir {drv.modules} missing", file=sys.stderr)
        return 2

    units = drv.discover()
    print("== GLAZE MODULE IMPORT TEST ==")
    print(f"  modules dir : {drv.modules}")
    print(f"  include dir : {drv.include}")
    print(f"  compiler    : {drv.cxx}")
    print(f"  std module  : {drv.std_pcm}")
    print(f"  units       : {len(units)}")
    print(f"  levels      : {[len(l) for l in drv.levels]}")
    print()
    print("-- phase 1: precompile module units --")
    t0 = time.time()
    drv.precompile_all()
    si = drv.summary()
    print(f"   precompiled: {si['precompile_ok']}/{si['units_total']} "
          f"(failed {si['precompile_failed']}) in {time.time()-t0:.1f}s")
    print()
    print("-- phase 1b: object code for linkable modules --")
    t0 = time.time()
    drv.compile_objects()
    si = drv.summary()
    print(f"   objects     : {si['object_ok']} compiled in {time.time()-t0:.1f}s")
    print()
    print("-- phase 2: import TUs (compile + link) --")
    t0 = time.time()
    drv.test_import_all()
    si = drv.summary()
    print(f"   importable : {si['import_ok']} ok, {si['import_partition']} partition, "
          f"{si['import_failed']} FAILED in {time.time()-t0:.1f}s")
    print()

    print("== PER-MODULE RESULTS ==")
    for name, rec in sorted(drv.results.items()):
        p = rec["precompile"]
        i = rec.get("import", {})
        if p["status"] in ("ok", "cached") and i.get("status") == "ok":
            mark = "PASS"
        elif p["status"] in ("ok", "cached") and i.get("status") == "partition":
            mark = "PASS(partition)"
        else:
            mark = "FAIL"
        detail = ""
        if mark == "FAIL":
            detail = " | " + (p.get("first_error") or i.get("first_error") or i.get("reason", ""))[:200]
        print(f"  [{mark:>14}] {name}{detail}")

    if not si["pass"]:
        print()
        print("== FAILURES ==")
        for f in si["failures"]:
            print(f"  {f['module']}")
            if f["precompile"] != "ok" and f["precompile_error"]:
                print(f"     precompile: {f['precompile_error']}")
            if f["import_error"]:
                print(f"     import    : {f['import_error']}")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(si, indent=2) + "\n", encoding="utf-8")

    print()
    print(f"  RESULT: {'PASS' if si['pass'] else 'FAIL'} "
          f"({si['import_ok']} importable, {si['import_partition']} partition, "
          f"{si['import_failed']} failed of {si['units_total']})")
    return 0 if si["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

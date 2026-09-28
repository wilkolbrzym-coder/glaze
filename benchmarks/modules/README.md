# Glaze modules benchmark

Quantifies what the C++20 module conversion changes relative to the existing
header-only path: per-translation-unit compile time, binary/BMI size,
preprocessed-line fan-in, and runtime throughput for JSON / BEVE / CBOR / TOML.
Correctness first: both paths must serialize the same payloads to **identical
bytes**, which the driver checks with `cmp`.

Nothing in this directory is part of the library build. It writes only under
`--work` (default `/home/bosyj/glz-bench1-work`) and `--results`.

## Run

```sh
benchmarks/modules/run_benchmarks.sh            # 5 reps, -O2, clang + gcc + module
benchmarks/modules/run_benchmarks.sh --reps 9   # more reps for a tighter spread
benchmarks/modules/run_benchmarks.sh --skip-module          # header half only
benchmarks/modules/run_benchmarks.sh --skip-header          # module half only
benchmarks/modules/run_benchmarks.sh --skip-gcc             # clang only
# point at a different module tree (e.g. a fix branch), without editing this repo:
benchmarks/modules/run_benchmarks.sh --modules /path/to/modules --include /path/to/include
```

Everything is scripted; no command needs to be run by hand. Output:

* `$RESULTS/metrics.tsv` — one row per metric (metric, path, compiler, value,
  unit, spread %, note).
* `$WORK/timing.jsonl` — raw per-rep timings for every compile-time measurement.
* `$WORK/rt_header_*.txt`, `$WORK/rt_module.txt` — raw runtime lines.
* `$WORK/dump_header/*.bin`, `$WORK/dump_module/*.bin` — serialized bytes used
  by the cross-path comparison.
* `$WORK/closure_*.json` — which module units built, their BMI/object sizes.

## Layout

| file | purpose |
|---|---|
| `run_benchmarks.sh` | driver: builds and times both paths, prints the table |
| `runner/path_formats.hpp` | the single switch: `#include` vs `import` for json/beve/cbor/toml |
| `runner/path_umbrella.hpp` | switch for the umbrella surface (`glaze.hpp` / `glaze`) |
| `runner/runtime_main.cpp` | shared workload; compiled unchanged for both paths |
| `runner/compile_probe.cpp` | small umbrella TU for the headline compile-time number |
| `runner/precompile_modules.py` | builds only the dependency closure of given roots into BMIs + objects |
| `runner/timeit.py` | runs a command N times, reports median/min/max/spread |

## What each number means

* **compile_umbrella** — wall time to compile `compile_probe.cpp` (`-c`) on the
  umbrella surface. Header: `#include "glaze/glaze.hpp"`. Module:
  `import glaze;`, with the whole BMI closure already built. Median of N reps.
* **compile_runtime_tu** — same, for `runtime_main.cpp` (json+beve+cbor+toml),
  `-c`. This is the honest per-TU comparison: header re-parses the library every
  time; modules load a BMI.
* **build_runtime_exe** — full compile+link wall time. For the module path the
  closure object files are linked in, so this is *not* a pure per-TU cost.
* **bmi_cold_build_formats** — one-off wall time to precompile the closure of
  json+beve+cbor+toml (BMIs + objects) with 4 jobs. Modules move this cost from
  every TU to a one-time library build; it is not included in per-TU numbers.
* **object_size_runtime_tu** — size of the `-c` object file produced from the
  same source on each path.
* **exe_size_runtime** — size of the linked executable.
* **bmi_total_size / bmi_count / module_objects_total_size** — size and count of
  the module interface and object files for the closure.
* **preprocessed_lines** — `clang++ -E runtime_main.cpp | wc -l`. Header: the
  whole library + libc++ preprocessed text. Module: the consumer TU is almost
  empty (only the few `#include`s in the shared header).
* **preprocessed_lines_umbrella** — same for the umbrella probe.
* **distinct_glaze_headers** — number of `.hpp` files pulled in via `-MM`.
* **RT ...** runtime lines: `ns_per_op` and `mb_per_s` per format/op. The
  driver runs the executable three times and keeps the raw log; the report uses
  the median of those runs at the analysis stage.
* **byte_identical** — 1 iff `cmp` says all four dumped serializations match
  between paths.

## Honesty notes

* Every compile-time metric is reported twice: **wall** time (median/spread)
  and **child CPU** time (`ru_utime + ru_stime` of the compiler processes,
  median/spread). On a machine with other processes running, wall time is
  load-dominated — the measured spread makes this obvious — while CPU time is
  reproducible to a few percent. Both are medians over `--reps` runs, never a
  single run; the raw samples are in `timing.jsonl`.
* This machine runs other applications concurrently (load average well above
  the 4 available cores during measurement), so **prefer the `_cpu` rows for
  compile-time comparisons** and treat wall times as upper bounds.
* The **module half is only reported if the module units actually compile**.
  If the dependency closure fails, the driver prints the failing units and
  marks the module metrics unavailable rather than inventing numbers.
* **BMI state**: the module per-TU numbers are measured with BMIs already
  built ("warm"). The cold BMI build cost is reported separately as
  `bmi_cold_build_formats`. Header numbers have no analogous warm state — every
  TU always preprocesses the full header tree.
* The compiler-version, exact flags and opt level are printed in the driver
  banner and recorded in `metrics.tsv`. clang uses `-stdlib=libc++`; gcc uses
  its default libstdc++.
* Wall times are affected by machine load (other processes running
  concurrently). Spread is reported so this is visible; comparisons are only
  made between paths measured under the same conditions on the same machine.
* `-E | wc -l` measures preprocessed *text lines*, not compiler work. For the
  module path it deliberately shows that the consumer TU no longer contains the
  library text.

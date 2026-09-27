#!/usr/bin/env bash
# Reproducible benchmark driver: header-only path vs C++20 module path.
#
#   benchmarks/modules/run_benchmarks.sh [--reps N] [--opt FLAG]
#                                        [--work DIR] [--results DIR]
#                                        [--modules DIR] [--include DIR]
#                                        [--skip-module] [--skip-gcc]
#
# What it does
#   1. header path  (clang++-22 -stdlib=libc++ and g++): compile-time reps,
#      object/executable size, preprocessed-line count, dependency count,
#      runtime throughput, serialized bytes dumped for a cross-path cmp.
#   2. module path  (clang++-22): precompiles the dependency closure of
#      glaze.{json,beve,cbor,toml,} into BMIs, then measures the same things.
#      If the module units do not compile, it records exactly which failed and
#      skips the module half -- it never fabricates a module number.
#   3. compares the dumped serialized bytes of both paths with cmp.
#
# Every compile-time number is a median over --reps runs (default 5) plus the
# spread; see runner/timeit.py. All artefacts live under --work (default
# /home/bosyj/glz-bench1-work), never in the tracked tree. A copy of the final
# tables lands in $RESULTS.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
RUNNER="$HERE/runner"

REPS=5
OPT="-O2"
WORK="${WORK:-/home/bosyj/glz-bench1-work}"
RESULTS="${RESULTS:-$HERE/results}"
INCLUDE="$ROOT/include"
MODULES="$ROOT/modules"
SKIP_MODULE=0
SKIP_GCC=0
SKIP_HEADER=0

while [ $# -gt 0 ]; do
  case "$1" in
    --reps) REPS="$2"; shift 2;;
    --opt) OPT="$2"; shift 2;;
    --work) WORK="$2"; shift 2;;
    --results) RESULTS="$2"; shift 2;;
    --modules) MODULES="$2"; shift 2;;
    --include) INCLUDE="$2"; shift 2;;
    --skip-module) SKIP_MODULE=1; shift;;
    --skip-gcc) SKIP_GCC=1; shift;;
    --skip-header) SKIP_HEADER=1; shift;;
    -h|--help) sed -n '2,30p' "$0"; exit 0;;
    *) echo "unknown argument: $1" >&2; exit 2;;
  esac
done

CXX_CLANG="${CXX_CLANG:-clang++-22}"
CXX_GCC="${CXX_GCC:-g++-15}"
STD_PCM="${STD_PCM:-/home/bosyj/Projects/Modules - Glaze/toolchain/std.pcm}"

mkdir -p "$WORK" "$RESULTS" "$WORK/pcm_final" "$WORK/bin"
TIMING_JSONL="$WORK/timing.jsonl"
METRICS_TSV="$RESULTS/metrics.tsv"
rm -f "$TIMING_JSONL" "$METRICS_TSV"

: > "$METRICS_TSV"
echo -e "metric\tpath\tcompiler\tvalue\tunit\tspread_pct\tnote" >> "$METRICS_TSV"

record() { # metric path compiler value unit spread note
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" "$6" "$7" >> "$METRICS_TSV"
}

# timeit LABEL REPS CMD... -> echoes the JSON line; also records to jsonl
timeit() {
  local label="$1"; shift
  local reps="$1"; shift
  python3 "$RUNNER/timeit.py" --label "$label" --reps "$reps" --warmup 1 --out "$TIMING_JSONL" -- "$@"
  return $?
}

# field JSON_FILE(ignored) KEY LABEL : parse from last jsonl line by label
json_field() { # key label
  python3 - "$TIMING_JSONL" "$2" "$1" <<'PY'
import json,sys
path,label,key=sys.argv[1],sys.argv[2],sys.argv[3]
last=None
for line in open(path):
    d=json.loads(line)
    if d["label"]==label: last=d
print(last[key] if last else "NA")
PY
}

measured() { # label reps cmd... ; records median/spread, returns rc via global
  local label="$1"; shift
  local reps="$1"; shift
  timeit "$label" "$reps" "$@"
  local rc=$?
  LAST_MED="$(json_field median "$label")"
  LAST_SPREAD="$(json_field spread_pct "$label")"
  return $rc
}

echo "########################################################################"
echo "# Glaze modules benchmark"
echo "#   repo      : $ROOT"
echo "#   compiler  : $($CXX_CLANG --version | head -1)"
echo "#   gcc       : $($CXX_GCC --version | head -1)"
echo "#   opt       : $OPT"
echo "#   reps      : $REPS"
echo "#   include   : $INCLUDE"
echo "#   modules   : $MODULES"
echo "#   work      : $WORK"
echo "#   results   : $RESULTS"
echo "########################################################################"
echo

########################################################################
# 1. HEADER PATH
########################################################################
run_header_path() {
  local cxx="$1" tag="$2"; shift 2
  local stdflag=("$@")
  echo "== header path: $tag =="
  local IDIR=(-I "$INCLUDE")

  # --- compile-time: umbrella probe ---
  measured "hdr_${tag}_compile_probe" "$REPS" "$cxx" -std=c++23 "${stdflag[@]}" "$OPT" "${IDIR[@]}" \
      -c "$RUNNER/compile_probe.cpp" -o "$WORK/bin/probe_${tag}.o"
  record "compile_umbrella" "header" "$tag" "$LAST_MED" "s" "$LAST_SPREAD" "median of $REPS, -c, $OPT"

  # --- compile-time: runtime TU ---
  measured "hdr_${tag}_runtime_compile" "$REPS" "$cxx" -std=c++23 "${stdflag[@]}" "$OPT" "${IDIR[@]}" \
      -c "$RUNNER/runtime_main.cpp" -o "$WORK/bin/runtime_${tag}.o"
  record "compile_runtime_tu" "header" "$tag" "$LAST_MED" "s" "$LAST_SPREAD" "median of $REPS, -c, $OPT"

  # --- full executable build (compile+link) ---
  measured "hdr_${tag}_runtime_exe" "$REPS" "$cxx" -std=c++23 "${stdflag[@]}" "$OPT" "${IDIR[@]}" \
      "$RUNNER/runtime_main.cpp" -o "$WORK/bin/runtime_${tag}"
  record "build_runtime_exe" "header" "$tag" "$LAST_MED" "s" "$LAST_SPREAD" "median of $REPS, compile+link, $OPT"

  # --- object / executable size ---
  local objsize exesize
  objsize=$(stat -c %s "$WORK/bin/runtime_${tag}.o")
  exesize=$(stat -c %s "$WORK/bin/runtime_${tag}")
  record "object_size_runtime_tu" "header" "$tag" "$objsize" "bytes" "" "-c output, $OPT"
  record "exe_size_runtime" "header" "$tag" "$exesize" "bytes" "" "linked, $OPT"

  # --- preprocessed lines / dependency fan-in ---
  local ppl clang_inc
  ppl=$("$cxx" -std=c++23 "${stdflag[@]}" "${IDIR[@]}" -E "$RUNNER/runtime_main.cpp" 2>/dev/null | wc -l)
  record "preprocessed_lines" "header" "$tag" "$ppl" "lines" "" "runtime_main.cpp -E | wc -l"
  clang_inc=$("$cxx" -std=c++23 "${stdflag[@]}" "${IDIR[@]}" -MM "$RUNNER/runtime_main.cpp" 2>/dev/null \
              | tr ' ' '\n' | grep -c '\.hpp$')
  record "distinct_glaze_headers" "header" "$tag" "$clang_inc" "files" "" "runtime_main.cpp -MM"
  local ppl_probe
  ppl_probe=$("$cxx" -std=c++23 "${stdflag[@]}" "${IDIR[@]}" -E "$RUNNER/compile_probe.cpp" 2>/dev/null | wc -l)
  record "preprocessed_lines_umbrella" "header" "$tag" "$ppl_probe" "lines" "" "compile_probe.cpp -E | wc -l"

  # --- runtime: run each rep, take best-of; dump bytes once ---
  echo "  running runtime workload ($tag) ..."
  local run_out="$WORK/rt_header_${tag}.txt"
  : > "$run_out"
  local r
  for r in 1 2 3; do
    "$WORK/bin/runtime_${tag}" >> "$run_out"
  done
  mkdir -p "$WORK/dump_header"
  "$WORK/bin/runtime_${tag}" --dump "$WORK/dump_header" > "$WORK/dump_header/summary.txt"
  record "runtime_raw_log" "header" "$tag" "$(wc -l < "$run_out")" "lines" "" "$run_out"
  echo
}

if [ "$SKIP_HEADER" -eq 0 ]; then
  run_header_path "$CXX_CLANG" "clang" -stdlib=libc++
  if [ "$SKIP_GCC" -eq 0 ]; then
    run_header_path "$CXX_GCC" "gcc"
  fi
else
  # module-only run still needs the header dumps for the byte comparison
  if [ -x "$WORK/bin/runtime_clang" ]; then
    mkdir -p "$WORK/dump_header"
    "$WORK/bin/runtime_clang" --dump "$WORK/dump_header" > "$WORK/dump_header/summary.txt"
  fi
fi

########################################################################
# 2. MODULE PATH
########################################################################
FORMATS_OK=0
UMBRELLA_OK=0
MODULE_NOTE=""
PCM_DIR="$WORK/pcm_final"
OBJ_DIR="$PCM_DIR/obj"
PCMS="$PCM_DIR/pcm"
if [ "$SKIP_MODULE" -eq 0 ]; then
  echo "== module path: precompiling dependency closures =="
  if [ ! -f "$STD_PCM" ]; then
    MODULE_NOTE="std.pcm missing at $STD_PCM"
  else
    fmt_log="$WORK/closure_formats.log"
    t0=$(date +%s)
    python3 "$RUNNER/precompile_modules.py" --root "$ROOT" --modules "$MODULES" --include "$INCLUDE" \
        --work "$PCM_DIR" --jobs 4 --roots "glaze.json,glaze.beve,glaze.cbor,glaze.toml" \
        --json "$WORK/closure_formats.json" > "$fmt_log" 2>&1
    fmt_rc=$?
    t1=$(date +%s)
    record "bmi_cold_build_formats" "module" "clang" "$((t1 - t0))" "s" "" "closure of json+beve+cbor+toml, 4 jobs, one-off"
    if [ "$fmt_rc" -eq 0 ]; then FORMATS_OK=1; echo "  formats closure: OK ($((t1-t0))s)"; else
      echo "  formats closure: FAILED"; grep -E '^\s+FAIL' "$fmt_log" | head -20 | sed 's/^/    /'
      MODULE_NOTE="$MODULE_NOTE formats closure failed;"
    fi

    umb_log="$WORK/closure_umbrella.log"
    t0=$(date +%s)
    python3 "$RUNNER/precompile_modules.py" --root "$ROOT" --modules "$MODULES" --include "$INCLUDE" \
        --work "$PCM_DIR" --jobs 4 --roots "glaze" \
        --json "$WORK/closure_umbrella.json" > "$umb_log" 2>&1
    umb_rc=$?
    t1=$(date +%s)
    if [ "$umb_rc" -eq 0 ]; then UMBRELLA_OK=1; echo "  umbrella closure: OK ($((t1-t0))s)"; else
      echo "  umbrella closure: FAILED"; grep -E '^\s+FAIL' "$umb_log" | head -10 | sed 's/^/    /'
      MODULE_NOTE="$MODULE_NOTE umbrella closure failed;"
    fi
    echo
  fi
fi

if [ "$FORMATS_OK" -eq 1 ]; then
  MF=(-std=c++23 -stdlib=libc++ "-fmodule-file=std=$STD_PCM" "-fprebuilt-module-path=$PCMS"
      -I "$MODULES" -I "$INCLUDE" -DGLZ_BENCH_MODULES)

  # BMI sizes for the formats closure actually needed by the consumer
  python3 - "$WORK/closure_formats.json" > "$WORK/fmt_stats.txt" <<'PY'
import json,sys,os
d=json.load(open(sys.argv[1]))
pcms=d["ok_pcms"]; objs=d["ok_objs"]
print(sum(u.get("pcm_bytes",0) for u in d["units"]))
print(len(pcms))
print(sum(u.get("obj_bytes",0) for u in d["units"]))
print("\n".join(objs))
PY
  bmi_total=$(sed -n 1p "$WORK/fmt_stats.txt")
  bmi_count=$(sed -n 2p "$WORK/fmt_stats.txt")
  obj_total=$(sed -n 3p "$WORK/fmt_stats.txt")
  mapfile -t MOD_OBJS < <(sed -n '4,$p' "$WORK/fmt_stats.txt")
  record "bmi_total_size" "module" "clang" "$bmi_total" "bytes" "" "$bmi_count BMIs (formats closure)"
  record "bmi_count" "module" "clang" "$bmi_count" "files" "" "formats closure units"
  record "module_objects_total_size" "module" "clang" "$obj_total" "bytes" "" "formats closure objects"

  # --- compile-time: runtime TU ---
  measured "mod_clang_runtime_compile" "$REPS" "$CXX_CLANG" "${MF[@]}" "$OPT" \
      -c "$RUNNER/runtime_main.cpp" -o "$WORK/bin/runtime_mod.o"
  record "compile_runtime_tu" "module" "clang" "$LAST_MED" "s" "$LAST_SPREAD" "median of $REPS, -c, $OPT, BMIs prebuilt"

  # --- full executable build (compile+link with module objects) ---
  measured "mod_clang_runtime_exe" "$REPS" "$CXX_CLANG" "${MF[@]}" "$OPT" \
      "$RUNNER/runtime_main.cpp" "${MOD_OBJS[@]}" -o "$WORK/bin/runtime_mod"
  record "build_runtime_exe" "module" "clang" "$LAST_MED" "s" "$LAST_SPREAD" "median of $REPS, compile+link, BMIs prebuilt"

  objsize=$(stat -c %s "$WORK/bin/runtime_mod.o")
  exesize=$(stat -c %s "$WORK/bin/runtime_mod")
  record "object_size_runtime_tu" "module" "clang" "$objsize" "bytes" "" "-c output, $OPT"
  record "exe_size_runtime" "module" "clang" "$exesize" "bytes" "" "linked, $OPT"

  ppl=$("$CXX_CLANG" "${MF[@]}" -E "$RUNNER/runtime_main.cpp" 2>/dev/null | wc -l)
  record "preprocessed_lines" "module" "clang" "$ppl" "lines" "" "runtime_main.cpp -E | wc -l"
  record "distinct_glaze_headers" "module" "clang" "0" "files" "" "no textual includes in consumer"

  echo "  running runtime workload (module) ..."
  run_out="$WORK/rt_module.txt"
  : > "$run_out"
  for r in 1 2 3; do
    "$WORK/bin/runtime_mod" >> "$run_out"
  done
  mkdir -p "$WORK/dump_module"
  "$WORK/bin/runtime_mod" --dump "$WORK/dump_module" > "$WORK/dump_module/summary.txt"
  record "runtime_raw_log" "module" "clang" "$(wc -l < "$run_out")" "lines" "" "$run_out"

  # --- correctness: identical bytes ---
  echo "== cross-path byte comparison =="
  cmp_rc=0
  for f in json beve cbor toml; do
    if cmp -s "$WORK/dump_header/$f.bin" "$WORK/dump_module/$f.bin"; then
      echo "  [IDENTICAL] $f"
    else
      echo "  [DIFFER]    $f"
      cmp_rc=1
    fi
  done
  record "byte_identical" "both" "clang" "$([ $cmp_rc -eq 0 ] && echo 1 || echo 0)" "bool" "" "cmp of dumped bytes"
else
  record "byte_identical" "both" "clang" "NA" "bool" "" "module path unavailable: $MODULE_NOTE"
fi

if [ "$UMBRELLA_OK" -eq 1 ]; then
  MF=(-std=c++23 -stdlib=libc++ "-fmodule-file=std=$STD_PCM" "-fprebuilt-module-path=$PCMS"
      -I "$MODULES" -I "$INCLUDE" -DGLZ_BENCH_MODULES)
  measured "mod_clang_compile_probe" "$REPS" "$CXX_CLANG" "${MF[@]}" "$OPT" \
      -c "$RUNNER/compile_probe.cpp" -o "$WORK/bin/probe_mod.o"
  record "compile_umbrella" "module" "clang" "$LAST_MED" "s" "$LAST_SPREAD" "median of $REPS, -c, $OPT, BMIs prebuilt"
  ppl_probe=$("$CXX_CLANG" "${MF[@]}" -E "$RUNNER/compile_probe.cpp" 2>/dev/null | wc -l)
  record "preprocessed_lines_umbrella" "module" "clang" "$ppl_probe" "lines" "" "compile_probe.cpp -E | wc -l"
fi

########################################################################
# 3. REPORT
########################################################################
echo
echo "== metrics =="
column -t -s $'\t' "$METRICS_TSV" 2>/dev/null || cat "$METRICS_TSV"
echo
echo "raw runtime lines:"
for f in "$WORK"/rt_header_*.txt "$WORK"/rt_module.txt; do
  [ -f "$f" ] || continue
  echo "-- $(basename "$f")"
  cat "$f"
done
echo
echo "MODULE FORMATS PATH : $([ "$FORMATS_OK" -eq 1 ] && echo measured || echo 'NOT measured')"
echo "MODULE UMBRELLA PATH: $([ "$UMBRELLA_OK" -eq 1 ] && echo measured || echo 'NOT measured')"
[ -n "$MODULE_NOTE" ] && echo "  note:$MODULE_NOTE"
echo "results written to $RESULTS/metrics.tsv and $WORK"

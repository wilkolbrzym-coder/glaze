#!/usr/bin/env bash
# Strict runner for tests/modules.
#
#   tests/modules/run_modules_tests.sh [--jobs N] [--no-cache] [--skip-import]
#                                      [--work DIR]
#
# Phase 1 (import level): modules_import_test.py precompiles every
#   modules/**/*.ixx and compiles+links an import-only TU for each module.
# Phase 2 (behavioural): every tests/modules/behaviour/*.cpp is compiled
#   against the precompiled modules it imports and executed. A test whose
#   imported module failed to precompile is reported ERROR, never PASS.
#
# Exit status is non-zero if any import test failed, any behavioural test
# failed to build or run, or zero tests executed.
#
# This script never writes outside --work (default /tmp/glz-modules-tests) and
# never touches the tracked source tree.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ROOT_OVERRIDE=""
WORK=/tmp/glz-modules-tests
JOBS=8
NO_CACHE=0
SKIP_IMPORT=0
HEADER_CROSSCHECK=0

while [ $# -gt 0 ]; do
  case "$1" in
    --jobs) JOBS="$2"; shift 2;;
    --work) WORK="$2"; shift 2;;
    --root) ROOT_OVERRIDE="$2"; shift 2;;
    --no-cache) NO_CACHE=1; shift;;
    --skip-import) SKIP_IMPORT=1; shift;;
    --header-crosscheck) HEADER_CROSSCHECK=1; shift;;
    -h|--help) sed -n '2,20p' "$0"; exit 0;;
    *) echo "unknown argument: $1" >&2; exit 2;;
  esac
done

if [ -n "$ROOT_OVERRIDE" ]; then
  ROOT="$(cd "$ROOT_OVERRIDE" && pwd)" || { echo "bad --root: $ROOT_OVERRIDE" >&2; exit 2; }
fi

CXX=${CXX:-clang++-22}
STD_PCM=${STD_PCM:-/home/bosyj/Projects/Modules - Glaze/toolchain/std.pcm}
PCM_DIR="$WORK/pcm"
BEH_DIR="$WORK/behaviour"
LOGS="$WORK/logs"
mkdir -p "$BEH_DIR" "$LOGS"

IMPORT_RC=0
BEH_BUILD_FAIL=0
BEH_RUN_FAIL=0
BEH_ERROR=0
BEH_PASS=0
TESTS_EXECUTED=0
CHECKS_EXECUTED=0

echo "########################################################################"
echo "# Glaze C++20 module test suite"
echo "#   repo    : $ROOT"
echo "#   work    : $WORK"
echo "#   compiler: $CXX"
echo "########################################################################"
echo

# ------------------------------------------------------------------ phase 1
if [ "$SKIP_IMPORT" -eq 1 ]; then
  echo "== phase 1: import-level tests SKIPPED (--skip-import) =="
else
  echo "== phase 1: import-level tests (every modules/**/*.ixx) =="
  python3 "$HERE/modules_import_test.py" --root "$ROOT" --std-pcm "$STD_PCM" \
      --work "$WORK" --cxx "$CXX" --jobs "$JOBS" --json "$WORK/import_report.json" \
      ${NO_CACHE:+--no-cache} 2>&1 | tee "$LOGS/import.log"
  IMPORT_RC=${PIPESTATUS[0]}
  echo "phase 1 exit: $IMPORT_RC"
fi
echo

# ------------------------------------------------------------------ phase 2
echo "== phase 2: behavioural tests (tests/modules/behaviour) =="
shopt -s nullglob
BEH_FILES=("$HERE"/behaviour/*.cpp)
shopt -u nullglob

if [ "${#BEH_FILES[@]}" -eq 0 ]; then
  echo "  ERROR: no behavioural test sources found" >&2
  BEH_ERROR=$((BEH_ERROR+1))
fi

for src in "${BEH_FILES[@]}"; do
  name="$(basename "$src" .cpp)"
  echo "-- $name"
  # required imports
  missing=""
  while IFS= read -r mod; do
    [ -z "$mod" ] && continue
    pcm="$PCM_DIR/$(printf '%s' "$mod" | tr ':' '-').pcm"
    if [ ! -f "$pcm" ]; then
      missing="$missing $mod"
    fi
  done < <(grep -oE '^import [A-Za-z_][A-Za-z0-9_.:]*;' "$src" | sed -E 's/^import (.*);/\1/')

  if [ -n "$missing" ]; then
    echo "   [ERROR] cannot build: required module(s) failed to precompile:$missing"
    echo "           see $LOGS/import.log (module precompile failures)"
    BEH_ERROR=$((BEH_ERROR+1))
    continue
  fi

  exe="$BEH_DIR/$name"
  clog="$LOGS/behaviour_${name}.build.log"
  shopt -s nullglob
  OBJ_FILES=("$WORK"/obj/*.o)
  shopt -u nullglob
  if ! "$CXX" -std=c++23 -stdlib=libc++ \
        "-fmodule-file=std=$STD_PCM" \
        "-fprebuilt-module-path=$PCM_DIR" \
        -I "$ROOT/include" -I "$ROOT/modules" -I "$HERE/harness" \
        "$src" "${OBJ_FILES[@]}" -o "$exe" > "$clog" 2>&1; then
    echo "   [ERROR] build failed; log: $clog"
    sed -n '1,8p' "$clog" | sed 's/^/           /'
    BEH_BUILD_FAIL=$((BEH_BUILD_FAIL+1))
    continue
  fi

  out="$LOGS/behaviour_${name}.run.log"
  "$exe" > "$out" 2>&1
  rc=$?
  # harness prints "[ RUN  ] name" per test
  n_tests=$(grep -c '^\[ RUN  \]' "$out" || true)
  TESTS_EXECUTED=$((TESTS_EXECUTED + n_tests))
  while IFS= read -r line; do
    c=$(printf '%s' "$line" | sed -nE 's/.*: [0-9]+ tests, ([0-9]+) checks, [0-9]+ failures.*/\1/p')
    [ -n "$c" ] && CHECKS_EXECUTED=$((CHECKS_EXECUTED + c))
  done < "$out"
  if [ "$rc" -eq 0 ]; then
    echo "   [PASS] $(grep -c '^\[PASS \]' "$out") tests passed ($(grep -oE '[0-9]+ checks' "$out" | tail -1))"
    BEH_PASS=$((BEH_PASS+1))
  else
    echo "   [FAIL] exit=$rc; log: $out"
    grep -E '^\[FAIL \]|CHECK FAILED|ERROR:' "$out" | head -12 | sed 's/^/           /'
    BEH_RUN_FAIL=$((BEH_RUN_FAIL+1))
  fi
done
echo

# ------------------------------------------------------- phase 3 crosscheck
XC_PASS=0
XC_FAIL=0
XC_SKIP=0
if [ "$HEADER_CROSSCHECK" -eq 1 ]; then
  echo "== phase 3: header cross-check (same assertions, header path) =="
  XC_DIR="$WORK/headercheck"
  rm -rf "$XC_DIR"; mkdir -p "$XC_DIR"
  mapfile -t XC_FILES < <(python3 "$HERE/header_crosscheck.py" --src "$HERE/behaviour" --out "$XC_DIR")
  for xsrc in "${XC_FILES[@]}"; do
    [ -z "$xsrc" ] && continue
    xname="$(basename "$xsrc" .cpp)"
    xexe="$XC_DIR/$xname"
    xlog="$LOGS/crosscheck_${xname}.build.log"
    if ! "$CXX" -std=c++23 -stdlib=libc++ -I "$ROOT/include" -I "$HERE/harness" \
          "$xsrc" -o "$xexe" > "$xlog" 2>&1; then
      echo "   [XC-FAIL] $xname: build error; log: $xlog"
      sed -n '1,6p' "$xlog" | sed 's/^/           /'
      XC_FAIL=$((XC_FAIL+1))
      continue
    fi
    xout="$LOGS/crosscheck_${xname}.run.log"
    "$xexe" > "$xout" 2>&1
    xrc=$?
    xnt=$(grep -c '^\[ RUN  \]' "$xout" || true)
    if [ "$xrc" -eq 0 ]; then
      echo "   [XC-PASS] $xname ($xnt tests)"
      XC_PASS=$((XC_PASS+1))
      TESTS_EXECUTED=$((TESTS_EXECUTED + xnt))
      while IFS= read -r line; do
        c=$(printf '%s' "$line" | sed -nE 's/.*: [0-9]+ tests, ([0-9]+) checks, [0-9]+ failures.*/\1/p')
        [ -n "$c" ] && CHECKS_EXECUTED=$((CHECKS_EXECUTED + c))
      done < "$xout"
    else
      echo "   [XC-FAIL] $xname: exit=$xrc; log: $xout"
      grep -E '^\[FAIL \]|CHECK FAILED' "$xout" | head -8 | sed 's/^/           /'
      XC_FAIL=$((XC_FAIL+1))
    fi
  done
  echo "   cross-check: $XC_PASS passed, $XC_FAIL failed"
  echo
fi

# ------------------------------------------------------------------ summary
echo "########################################################################"
echo "# SUMMARY"
echo "#   import-level : exit $IMPORT_RC"
echo "#   behavioural  : $BEH_PASS passed, $BEH_RUN_FAIL failed, $BEH_BUILD_FAIL build-error, $BEH_ERROR prereq/error"
if [ "$HEADER_CROSSCHECK" -eq 1 ]; then
  echo "#   header-check : $XC_PASS passed, $XC_FAIL failed"
fi
echo "#   tests run    : $TESTS_EXECUTED ($CHECKS_EXECUTED assertions)"
if [ -f "$WORK/import_report.json" ]; then
  python3 - "$WORK/import_report.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print(f"#   modules      : {d['import_ok']} importable, {d['import_partition']} partition, "
      f"{d['import_failed']} failed (precompile {d['precompile_ok']}/{d['units_total']})")
PY
fi
echo "########################################################################"

FAILED=0
[ "$IMPORT_RC" -ne 0 ] && FAILED=1
[ "$BEH_RUN_FAIL" -ne 0 ] && FAILED=1
[ "$BEH_BUILD_FAIL" -ne 0 ] && FAILED=1
[ "$BEH_ERROR" -ne 0 ] && FAILED=1
[ "$XC_FAIL" -ne 0 ] && FAILED=1
if [ "$TESTS_EXECUTED" -eq 0 ] && [ "$SKIP_IMPORT" -eq 1 ]; then
  echo "ERROR: zero behavioural tests executed" >&2
  FAILED=1
fi
[ "$SKIP_IMPORT" -eq 0 ] && [ "$IMPORT_RC" -eq 2 ] && FAILED=1
if [ "$FAILED" -eq 0 ] && [ "$SKIP_IMPORT" -eq 0 ] && [ "$IMPORT_RC" -eq 0 ]; then
  echo "RESULT: PASS"
  exit 0
fi
echo "RESULT: FAIL"
exit 1

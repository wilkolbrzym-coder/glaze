#!/usr/bin/env bash
#
# One driver for the SIMD benchmarks. Architecture-aware:
#
#   x86_64  builds and runs the header path four ways (scalar / SSE2 / SSSE3 / AVX2),
#           builds and runs the module path when the module units precompile, diffs the
#           emitted assembly of a representative kernel (header vs module), and compiles
#           the real NEON headers for aarch64 without running them.
#
#   aarch64 builds and runs the header path with NEON (native), and prints the same table.
#
#   other   prints an explicit SKIPPED line and does nothing else. It never silently
#           passes: an unsupported host is a visible skip, not an empty success.
#
# Every number is a median over BENCH_REPS runs (default 11); min..max is the spread.
# Set BENCH_REPS / BENCH_INNER to trade time for stability. Set BENCH_CSV to emit CSV.
#
# Usage: benchmarks/simd/run_simd_benchmarks.sh [build_dir]
#
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"

CC="${CXX:-clang++-22}"
STD_FLAGS=(-std=c++23 -O3)
STD_PCM="${GLZ_STD_PCM:-/home/bosyj/Projects/Modules - Glaze/toolchain/std.pcm}"
ARCH="$(uname -m)"
BUILD="${1:-$(mktemp -d -p "${TMPDIR:-$HOME}" glz-simd-bench.XXXXXX)}"
mkdir -p "$BUILD"
CSV="$BUILD/results.csv"
: > "$CSV"

BENCH_REPS="${BENCH_REPS:-15}"
BENCH_INNER="${BENCH_INNER:-5}"
export BENCH_REPS BENCH_INNER BENCH_CSV=1

PIN=""
if command -v taskset >/dev/null 2>&1; then
   PIN="taskset -c 0"
fi

say() { printf '%s\n' "$*"; }
hr()  { printf '%s\n' "------------------------------------------------------------------------"; }

# ---------------------------------------------------------------------------
# header path
# ---------------------------------------------------------------------------
build_header() { # name extra_flags label
   local name="$1" flags="$2" label="$3"
   local out="$BUILD/simd_bench_$name"
   # shellcheck disable=SC2086
   if "$CC" "${STD_FLAGS[@]}" $flags -DGLZ_BENCH_FLAGS="\"$label\"" \
        "$SCRIPT_DIR/simd_bench.cpp" -I"$ROOT/include" -o "$out" 2>"$BUILD/$name.build.log"; then
      say "built  header/$name  ($flags)"
      printf '' # built
   else
      say "FAILED to build header/$name ($flags); see $BUILD/$name.build.log"
      return 1
   fi
}

run_header() { # name
   local name="$1"
   local out="$BUILD/simd_bench_$name"
   [ -x "$out" ] || return 0
   # shellcheck disable=SC2086
   $PIN "$out" >> "$CSV"
}

# ---------------------------------------------------------------------------
# module path (x86_64 only here; needs the prebuilt std.pcm)
# ---------------------------------------------------------------------------
PCM="$BUILD/pcm"
precompile() { # unit module_name extra_flags...
   local unit="$1" mod="$2"; shift 2
   "$CC" -std=c++23 -mavx2 -x c++-module --precompile "$ROOT/$unit" -I"$ROOT/include" \
      -fmodule-file="std=$STD_PCM" "$@" -o "$PCM/${mod##*.}.pcm" >"$BUILD/pcm.log" 2>&1
}

build_modules() {
   mkdir -p "$PCM"
   [ -f "$STD_PCM" ] || { say "module path SKIPPED: std.pcm not found at $STD_PCM"; return 1; }
   precompile modules/glaze/core/basic_types.ixx glaze.core.basic_types || { say "module SKIP: basic_types failed"; return 1; }
   precompile modules/glaze/util/bit.ixx glaze.util.bit \
      -fmodule-file="glaze.core.basic_types=$PCM/basic_types.pcm" || { say "module SKIP: bit failed"; return 1; }
   precompile modules/glaze/simd/simd.ixx glaze.simd.simd \
      -fmodule-file="glaze.core.basic_types=$PCM/basic_types.pcm" -fmodule-file="glaze.util.bit=$PCM/bit.pcm" \
      || { say "module SKIP: simd failed"; return 1; }
   precompile modules/glaze/simd/utf8_validation.ixx glaze.simd.utf8_validation \
      -fmodule-file="glaze.core.basic_types=$PCM/basic_types.pcm" -fmodule-file="glaze.util.bit=$PCM/bit.pcm" \
      || { say "module SKIP: utf8_validation failed"; return 1; }
   return 0
}

build_module_bench() {
   "$CC" "${STD_FLAGS[@]}" -mavx2 -stdlib=libc++ \
      -DGLZ_BENCH_FLAGS='"module-avx2"' -DGLZ_BENCH_UTF8_BACKEND='"AVX2(module)"' \
      "$SCRIPT_DIR/simd_bench_module.cpp" \
      -fmodule-file="std=$STD_PCM" \
      -fmodule-file="glaze.core.basic_types=$PCM/basic_types.pcm" \
      -fmodule-file="glaze.simd.utf8_validation=$PCM/utf8_validation.pcm" \
      -o "$BUILD/simd_bench_module_avx2" 2>"$BUILD/module.build.log"
}

# ---------------------------------------------------------------------------
# codegen comparison
# ---------------------------------------------------------------------------
codegen_diff() {
   local CG="$SCRIPT_DIR/codegen"
   "$CC" "${STD_FLAGS[@]}" -mavx2 -stdlib=libc++ -S "$CG/utf8_kernel_header.cpp" -I"$ROOT/include" \
      -o "$BUILD/utf8_header.s" 2>"$BUILD/cg_header.log" || return 1
   "$CC" "${STD_FLAGS[@]}" -mavx2 -stdlib=libc++ -S "$CG/utf8_kernel_module.cpp" \
      -fmodule-file="std=$STD_PCM" \
      -fmodule-file="glaze.core.basic_types=$PCM/basic_types.pcm" \
      -fmodule-file="glaze.simd.utf8_validation=$PCM/utf8_validation.pcm" \
      -o "$BUILD/utf8_module.s" 2>"$BUILD/cg_module.log" || return 1
   local ndiff
   ndiff=$(diff "$BUILD/utf8_header.s" "$BUILD/utf8_module.s" | grep -c '^[<>]')
   say "codegen: $ndiff differing lines between header and module assembly"
   say "         (of which non-.file: $(diff "$BUILD/utf8_header.s" "$BUILD/utf8_module.s" | grep '^[<>]' | grep -vc '\.file'))"
   diff "$BUILD/utf8_header.s" "$BUILD/utf8_module.s" | grep '^[<>]' | grep -v '\.file' | sed 's/^/         /' | head -20
   return 0
}

# ---------------------------------------------------------------------------
# NEON compile check for aarch64 (no execution)
# ---------------------------------------------------------------------------
neon_cross_check() {
   local shim="$SCRIPT_DIR/neon_sysroot_shim"
   local resdir
   resdir="$("$CC" -print-resource-dir)/include"
   if "$CC" "${STD_FLAGS[@]}" --target=aarch64-linux-gnu -march=armv8-a+simd -c \
        "$SCRIPT_DIR/neon_compile_check.cpp" -I"$ROOT/include" \
        -nostdinc -nostdinc++ -isystem "$resdir" -isystem "$shim" \
        -o "$BUILD/neon_check.o" 2>"$BUILD/neon_cross.log"; then
      local file_out neon_count
      file_out="$(file -b "$BUILD/neon_check.o" 2>/dev/null || echo unknown)"
      neon_count="$(llvm-objdump-22 -d "$BUILD/neon_check.o" 2>/dev/null | grep -cE '\bv[0-9]+\.' || true)"
      say "NEON cross-compile: OK -> $file_out"
      say "                    vector-register instructions in object: $neon_count"
      say "                    (real Glaze NEON headers + clang arm_neon.h; std shimmed, no aarch64 libc on host)"
      say "                    NOT executed: no aarch64 host/emulator here. arm64 CI runs the native path."
   else
      say "NEON cross-compile: FAILED; see $BUILD/neon_cross.log"
      head -20 "$BUILD/neon_cross.log" | sed 's/^/   /'
   fi
}

# ===========================================================================
say "Glaze SIMD benchmark driver"
say "  host arch   : $ARCH"
say "  compiler    : $("$CC" --version | head -1)"
say "  cpu         : $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //')"
say "  flags       : ${STD_FLAGS[*]}"
say "  reps/inner  : $BENCH_REPS / $BENCH_INNER   ($( [[ -n $PIN ]] && echo "pinned: $PIN" || echo "not pinned" ))"
say "  build dir   : $BUILD"
hr

case "$ARCH" in
x86_64|amd64)
   build_header scalar "-DGLZ_DISABLE_SIMD"        "scalar"
   build_header sse2   ""                          "default(SSE2)"
   build_header ssse3  "-mssse3"                   "ssse3"
   build_header avx2   "-mavx2"                    "avx2"
   if grep -qm1 avx512bw /proc/cpuinfo 2>/dev/null; then
      build_header avx512 "-mavx512bw -mavx512vl"  "avx512bw"
   else
      say "avx512bw not in /proc/cpuinfo -> skipping AVX-512 build"
   fi

   hr
   say "running header path (scalar / SSE2 / SSSE3 / AVX2):"
   for n in scalar sse2 ssse3 avx2 avx512; do
      [ -x "$BUILD/simd_bench_$n" ] && say "  running $n ..."
      run_header "$n"
   done

   hr
   say "module path:"
   if build_modules && build_module_bench; then
      $PIN "$BUILD/simd_bench_module_avx2" >> "$CSV"
      say "built and ran module/avx2"
   else
      say "module path SKIPPED (a required unit did not precompile; see $BUILD/pcm.log)"
   fi

   hr
   say "codegen (module vs header), kernel=utf8_simd::validate, -O3 -mavx2:"
   if [ -f "$PCM/utf8_validation.pcm" ]; then
      codegen_diff || say "codegen comparison SKIPPED (module pcm unavailable)"
   else
      say "codegen comparison SKIPPED (module pcm unavailable)"
   fi

   hr
   say "NEON:"
   neon_cross_check
   ;;
aarch64|arm64)
   build_header scalar  "-DGLZ_DISABLE_SIMD" "scalar"
   build_header neon    ""                   "NEON(native)"
   build_header neon64  "-march=armv8.2-a+simd" "NEON64(native)"
   hr
   say "running header path (scalar / NEON), native:"
   for n in scalar neon neon64; do
      [ -x "$BUILD/simd_bench_$n" ] && say "  running $n ..."
      run_header "$n"
   done
   ;;
*)
   hr
   say "SKIPPED: architecture '$ARCH' is neither x86_64 nor aarch64."
   say "         This is an explicit skip, not a pass. No numbers were measured."
   ;;
esac

hr
say "results: median MB/s by kernel and build (full min/max spread in $CSV):"
if grep -qv '^build,' "$CSV" 2>/dev/null; then
   awk -F, '
      $1 == "build" { next }
      NF >= 6 && $1 != "" {
         if (!(($2) in seen))   { seen[$2]=1; builds[++nb]=$2 }
         if (!(($3) in kseen))  { kseen[$3]=1; keys[++nk]=$3 }
         val[$3 SUBSEP $2] = $5
      }
      END {
         printf "  %-26s", "kernel";
         for (i = 1; i <= nb; i++) printf " %-16s", builds[i];
         printf "\n";
         for (j = 1; j <= nk; j++) {
            printf "  %-26s", keys[j];
            for (i = 1; i <= nb; i++) {
               v = val[keys[j] SUBSEP builds[i]];
               printf " %-16s", (v == "" ? "-" : sprintf("%.1f", v));
            }
            printf "\n";
         }
      }' "$CSV"
else
   say "  (no results)"
fi
say "done"

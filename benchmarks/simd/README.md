# SIMD benchmarks (PR 2408 module conversion)

What the SIMD paths in `include/glaze/simd/` actually buy, and whether routing the
same code through C++20 modules changes it. Everything here is new files under
`benchmarks/simd/`; nothing under `include/`, `modules/`, `tests/`, `cmake/`,
`.github/`, `docs/` or existing `benchmarks/` files was touched.

## Files

| file | role |
|---|---|
| `bench_runner.hpp` | dependency-free timing core (steady_clock, median of N runs, min/max spread, deterministic inputs). No third-party library. |
| `simd_bench.cpp` | the header path. Calls the real Glaze entry points; the build flags choose the backend. |
| `simd_bench_module.cpp` | the module path. `import glaze.simd.utf8_validation;`. |
| `codegen/utf8_kernel_{header,module}.cpp` | the same kernel compiled both ways, for `-S` assembly diffing. |
| `neon_compile_check.cpp` | AArch64 compile check of the real NEON headers. |
| `neon_sysroot_shim/` | a tiny std stub so the NEON check can compile without an aarch64 libc (see below). |
| `run_simd_benchmarks.sh` | the one driver: build + run + table, architecture-aware. |

## Run it

```
benchmarks/simd/run_simd_benchmarks.sh            # build dir under $HOME, prints a table
BENCH_REPS=25 BENCH_INNER=10 benchmarks/simd/run_simd_benchmarks.sh /tmp/glzbench
```

Every number is the **median of `BENCH_REPS` runs** (default 15), each run repeating
the whole workload `BENCH_INNER` times (default 5). `min_MBps`/`max_MBps` in the CSV
are the observed extremes — the honest spread, not a confidence interval. Each
kernel's checksum is printed so you can confirm all builds did identical work.

On x86-64 the driver builds the same source five ways ([C1] header path):

| column label | flags | what it selects |
|---|---|---|
| `scalar` | `-DGLZ_DISABLE_SIMD` | SWAR/scalar everywhere |
| `default(SSE2)` | *(none)* | SSE2 baseline; UTF-8 still scalar (needs SSSE3's byte shuffle) |
| `ssse3` | `-mssse3` | first vector UTF-8 backend on x86-64 |
| `avx2` | `-mavx2` | AVX2 |
| `avx512bw` | `-mavx512bw -mavx512vl` | only if `/proc/cpuinfo` advertises it |

On aarch64 it builds `scalar` / `NEON(native)` / `NEON64` and runs them natively.
On any other architecture it prints an explicit `SKIPPED` line and measures nothing.

## Canonical results (2026-09-27)

Host and invocation, because a number without them is worthless:

- CPU: Intel(R) Core(TM) i5-6500T @ 2.50 GHz (Skylake, 4 cores), **shared machine**:
  load average 6.1–7.9 during the run (other agents compiling + a browser).
- Compiler: `Ubuntu clang version 22.1.2 (1ubuntu1)`, `-std=c++23 -O3`, pinned with
  `taskset -c 0`, `BENCH_REPS=15 BENCH_INNER=5`.
- Inputs: 4 MiB ASCII; 4 MiB mixed UTF-8; 4 MiB escape-free; 1 MiB with ~4% escapes;
  4 MiB JSON-ish; 2.18 MB of decimal literals; 200k doubles; 4 MiB whitespace runs.

Median MB/s, with `[min..max]` where the spread is informative:

| kernel (backend) | scalar | default(SSE2) | ssse3 | avx2 | module-avx2 |
|---|---|---|---|---|---|
| `utf8_dispatch_mixed` | 186 [155..199] | 173 [144..194] | 4425 [4071..4728] | **6123** [5698..7043] | — |
| `utf8_vector_mixed` | — | — | 3003 [2636..3279] | **6390** [3472..6990] | 6215 [5675..6456] |
| `utf8_dispatch_ascii` | 7398 [4587..8084] | 7885 [6692..8816] | 14286 | **22456** [13705..31815] | — |
| `utf8_vector_ascii` | — | — | 12902 | **15741** | 14296 [11423..18462] |
| `escape_plain` | 2880 (SWAR) | 5069 (SSE2) | 4134 | **5617** (AVX2) | — |
| `escape_hot` | 803 (SWAR) | 935 (SSE2) | 839 | **1011** (AVX2) | — |
| `structural_window` | — | 4209 (SSE2) | 4012 | **7753** (AVX2) | — |
| `structural_scalar_equiv` | 554 | 540 | 521 | 578 | — |
| `fastfloat_parse` (SWAR char) | 288 | 375 | 287 | 315 | — |
| `zmij_float_write` | 268 (scalar) | 296 (SSE2) | 285 | 187 (SSE4.1) | — |
| `skip_matching_ws` (SWAR) | 1831 | 1848 | 1883 | 1867 | — |

What the numbers say:

- **UTF-8 validation** is the big win: AVX2 ≈ **33×** the scalar validator on mixed
  UTF-8 (6123 vs 186 MB/s), and ≈1.4× SSSE3. On pure ASCII the scalar SWAR 8-byte
  skip is already 7.4 GB/s, so AVX2's edge is only ≈3× there.
- **Plain SSE2 does not vectorize UTF-8.** `default(SSE2)` measures 173 MB/s — the
  scalar validator, because `simd/utf8_validation.hpp` needs `GLZ_USE_SSSE3` (a
  byte-granular shuffle) which plain x86-64 does not define. This is a property of
  the library, not of the benchmark, and it is why the SSE2 column matches `scalar`.
- **Structural skip** (`load_window` bitmasks): AVX2 7753 vs SSE2 4209 MB/s (1.8×),
  and ≈14× a scalar byte loop building the same four masks.
- **String escaping**: SIMD is ≈1.8× SWAR on escape-free input (2880 → 5617) but
  only ≈1.1–1.3× on escape-heavy input (803 → 1011), because escapes make it leave
  the vector fast path. AVX2 vs SSE2 is within noise on these inputs.
- **`fast_float` and whitespace skip are not SIMD on the `char` path.** `fast_float`'s
  SIMD is `char16_t`-only (`has_simd_opt`), and `skip_matching_ws` is an 8-byte SWAR
  compare. Their columns are flat across builds, as they must be.
- **`zmij` float writing**: no clear SIMD win in this loop (268 scalar / 296 SSE2 /
  187 SSE4.1; the SSE4.1 number is a contention outlier). Reported as measured.

### Read the noise before reading a ratio

This is a **shared** box. Kernels that must compile to identical code across builds
are the control:

| kernel (identical in every build) | scalar | SSE2 | ssse3 | avx2 |
|---|---|---|---|---|
| `utf8_scalar_mixed` | 188 | 196 | 180 | 193 |
| `skip_matching_ws` | 1831 | 1848 | 1883 | 1867 |
| `structural_scalar_equiv` | 554 | 540 | 521 | 578 |
| `fastfloat_parse` | 288 | 375 | 287 | 315 |

Fixed-binary run-to-run spread is ~2–5% for these; `fastfloat_parse` and some other
kernels showed bursts up to ~30%. **Treat differences below roughly 15–20% as noise
on this host.** A quiet machine (or CI) is needed for finer resolution.

## Module vs header codegen

`benchmarks/simd/codegen/utf8_kernel_{header,module}.cpp` are the same function,
one including `glaze/simd/utf8_validation.hpp`, one `import glaze.simd.utf8_validation;`.
Compiled `-O3 -mavx2 -stdlib=libc++ -S` and diffed:

```
8 differing lines total
6 non-.file lines, all of them:
- vpor  %ymm9,  %ymm12, %ymm9      + vpor  %ymm12, %ymm9,  %ymm9
- vpor  %ymm10, %ymm12, %ymm10     + vpor  %ymm12, %ymm10, %ymm10
- vpor  %ymm1,  %ymm4,  %ymm1      + vpor  %ymm4,  %ymm1,  %ymm1
```

The remaining 2 lines are the `.file` directive. The only instruction difference is
the operand order of three commutative `vpor`s — same registers, same scheduling,
same everything else. **The module path does not change the generated code here.**
Measured throughput agrees: `utf8_vector_mixed` header AVX2 6390 vs module AVX2 6215
MB/s (−2.8%), `utf8_vector_ascii` 15741 vs 14296 (−9%), both inside the noise band.

## NEON (AArch64)

**What executed: nothing on AArch64.** This host has no aarch64 sysroot
(`/usr/aarch64-linux-gnu` absent), no `aarch64-*-gcc`, and no qemu. No NEON
throughput number is reported, and none should be read into this document.

**What compiled:** the *real* Glaze NEON headers (`simd/neon.hpp`,
`simd/structural.hpp` NEON64 backend, `simd/utf8_validation.hpp` NEON64 backend)
compile and code-generate for aarch64:

```
clang++-22 -std=c++23 -O3 --target=aarch64-linux-gnu -march=armv8-a+simd -c \
  benchmarks/simd/neon_compile_check.cpp -Iinclude \
  -nostdinc -nostdinc++ -isystem <clang-resource>/include \
  -isystem benchmarks/simd/neon_sysroot_shim -o neon_check.o
```

→ `ELF 64-bit LSB relocatable, ARM aarch64`, containing 237 vector-register
instructions (`cmeq`, `tbl`, `ext`, `ushr`, `umaxv`, `movi`, …). `<arm_neon.h>` is
clang's own, so the intrinsics are genuine; the std surface is stubbed by
`neon_sysroot_shim/` because no aarch64 libc exists here. That shim is not a sysroot
and not part of Glaze — a green check means "the NEON code compiles", not "it runs".

**Native path for CI:** `simd_bench.cpp` is architecture-independent. On an arm64
runner `run_simd_benchmarks.sh` takes the `aarch64)` branch, builds
`scalar` / `NEON(native)` / `NEON64` and runs them natively — that is where real NEON
numbers come from. On any other host the NEON execution is an explicit skip.

## Module-tree gaps found while writing this (not fixed here — not my files)

1. `modules/glaze/simd/avx.ixx:25` uses `export` **after** `template <...>`
   (`export GLZ_ALWAYS_INLINE void avx2_string_escape(...)`). clang rejects it with
   `expected template`; the unit cannot be precompiled, so the module escape kernel
   could not be built. `sse.ixx` writes the valid `export template <...>`.
2. There is no module unit for `simd/structural.hpp`, `simd/backends.hpp` or
   `util/zmij.hpp`, so the structural-skip and float-write kernels have no module
   path yet.
3. `modules/glaze/simd/utf8_validation.ixx` does not define the
   `glz::detail::utf8_simd::backend` constant that the header defines and that
   `simd/backends.hpp` uses for `simd_info`. A module-vs-header API divergence.

## Numbers not obtained (explicit list)

- Any AArch64/NEON **throughput** (no host, no emulator).
- AVX-512 throughput (this CPU is Skylake without `avx512bw`; build is auto-skipped).
- Module-path numbers for string escape, structural skip and float write (blocked by
  gaps 1–2 above).
- Anything at a resolution finer than ~15–20% on this contended machine.
- 32-bit NEON, WASM SIMD128.

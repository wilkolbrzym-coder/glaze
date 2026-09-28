# neon_sysroot_shim

This is **not** a sysroot and **not** part of Glaze. It exists only so that
`run_simd_benchmarks.sh` can compile the real Glaze NEON headers for
`--target=aarch64-linux-gnu` on a host that has no aarch64 libc (this machine has
no `/usr/aarch64-linux-gnu`, no `aarch64-*-gcc`, no qemu).

The Glaze headers themselves are the real ones in `include/`; only the C/C++
standard-library surface they touch is stubbed here (`<cstdint>`, `<cstddef>`,
`<cstring>`, `<string_view>`, `<bit>`). `<arm_neon.h>` is clang's own, so the NEON
intrinsics that actually get type-checked and code-generated are genuine.

A green compile here means "the NEON intrinsic code in the Glaze headers compiles
for AArch64". It does **not** mean the full benchmark runs on AArch64 — that only
happens natively on an arm64 runner.

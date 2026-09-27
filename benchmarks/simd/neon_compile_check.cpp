// AArch64 compile check for the real Glaze NEON code paths.
//
// Compiled by run_simd_benchmarks.sh with:
//   clang++-22 --target=aarch64-linux-gnu -march=armv8-a+simd -c
//   -nostdinc -nostdinc++ -isystem <clang resource include>
//   -isystem benchmarks/simd/neon_sysroot_shim
//
// The Glaze headers and <arm_neon.h> are real; only the small std surface they
// touch is stubbed (no aarch64 libc exists on this host). Emitting an object file
// with -c exercises the NEON intrinsics through codegen, not just parsing. This
// does not execute anything on AArch64; see README.md.

#include <cstddef>
#include <cstdint>

#include "glaze/simd/neon.hpp"
#include "glaze/simd/structural.hpp"
#include "glaze/simd/utf8_validation.hpp"

#if !defined(GLZ_USE_NEON)
#error "NEON not selected for this target"
#endif
#if !defined(GLZ_USE_NEON64)
#error "NEON64 not selected for this target"
#endif

namespace
{
   void append_escape(const char*& c, char*& data)
   {
      *data++ = *c;
      ++c;
   }
}

extern "C" std::size_t neon_structural(const char* p)
{
   const auto w = glz::detail::structural::load_window(p, '[', ']');
   return std::size_t(w.quote) + std::size_t(w.backslash) + std::size_t(w.open) + std::size_t(w.close) +
          std::size_t(w.non_ascii);
}

extern "C" int neon_utf8(const std::uint8_t* p, std::size_t n)
{
   return glz::detail::utf8_simd::validate(p, p + n) ? 1 : 0;
}

extern "C" std::size_t neon_escape(const char* s, std::size_t n, char* out)
{
   const char* c = s;
   const char* e = s + n;
   char* data = out;
   auto we = [&]() { append_escape(c, data); };
   glz::detail::neon_string_escape(c, e, data, n, we);
   return std::size_t(data - out);
}

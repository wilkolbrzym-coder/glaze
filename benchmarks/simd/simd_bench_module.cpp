// SIMD benchmark — MODULE path.
//
// Imports module units instead of including headers. Coverage is deliberately narrow
// because that is all the module tree currently supports (see README.md):
//
//   * UTF-8 validation  -> glaze.simd.utf8_validation  (works, this is the kernel measured)
//   * string escaping   -> glaze.simd.avx is BROKEN at this revision: avx.ixx places
//                          `export` after `template <...>`, which clang rejects with
//                          "expected template". It cannot be precompiled, so the module
//                          escape kernel is absent here.
//   * structural skip   -> no module unit exists (no simd/structural.ixx)
//   * float write       -> no module unit exists (no util/zmij.ixx export of glz::to_chars)
//
// The point of this binary is the codegen question: does routing the same kernel through
// a module change the generated code? The UTF-8 kernel answers it (the driver diffs the
// emitted assembly against the header build). No number here is a substitute for the
// header-path table.

import std;
import glaze.simd.utf8_validation;

#define BENCH_IMPORT_STD
#include "bench_runner.hpp"

#ifndef GLZ_BENCH_FLAGS
#define GLZ_BENCH_FLAGS "unspecified"
#endif

// The header defines glz::detail::utf8_simd::backend; the module unit does not (a
// module-vs-header divergence, see README). Take the label from the build instead.
#ifndef GLZ_BENCH_UTF8_BACKEND
#define GLZ_BENCH_UTF8_BACKEND "unknown"
#endif

namespace
{
   std::string g_utf8 = bench::make_mixed_utf8(4u * 1024 * 1024);
   std::string g_ascii = bench::make_ascii(4u * 1024 * 1024);

   std::uint64_t k_utf8_vector()
   {
      const auto* p = reinterpret_cast<const std::uint8_t*>(g_utf8.data());
      return glz::detail::utf8_simd::validate(p, p + g_utf8.size()) ? 1u : 0u;
   }

   std::uint64_t k_utf8_vector_ascii()
   {
      const auto* p = reinterpret_cast<const std::uint8_t*>(g_ascii.data());
      return glz::detail::utf8_simd::validate(p, p + g_ascii.size()) ? 1u : 0u;
   }
}

int main()
{
   const std::string utf8_be{GLZ_BENCH_UTF8_BACKEND};
   std::vector<bench::kernel> kernels{
      {"utf8_vector_mixed", utf8_be, g_utf8.size(), &k_utf8_vector},
      {"utf8_vector_ascii", utf8_be, g_ascii.size(), &k_utf8_vector_ascii},
   };
   return bench::bench_entry("module", GLZ_BENCH_FLAGS, std::move(kernels));
}

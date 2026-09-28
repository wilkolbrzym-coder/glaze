// SIMD benchmark — HEADER path.
//
// Includes the real Glaze headers and calls the real accelerated entry points, so the
// backend for each kernel is chosen by the same GLZ_USE_* macros the library uses.
// Build the same source with:
//   default x86-64        -> SSE2 baseline (utf8 stays scalar: no SSSE3 byte shuffle)
//   -mssse3               -> SSSE3 (first vector UTF-8 backend on x86-64)
//   -mavx2                -> AVX2
//   -DGLZ_DISABLE_SIMD    -> scalar / SWAR everywhere
// The flags actually in force are printed with the numbers.
//
// No intrinsics are written here: every kernel drives a library function or the
// library's own escape cascade.

#include <bit>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

#include "glaze/simd/avx.hpp"
#include "glaze/simd/backends.hpp"
#include "glaze/simd/neon.hpp"
#include "glaze/simd/simd.hpp"
#include "glaze/simd/sse.hpp"
#include "glaze/simd/structural.hpp"
#include "glaze/simd/utf8_validation.hpp"
#include "glaze/util/fast_float.hpp"
#include "glaze/util/parse.hpp"
#include "glaze/util/zmij.hpp"

#include "bench_runner.hpp"

#ifndef GLZ_BENCH_FLAGS
#define GLZ_BENCH_FLAGS "unspecified"
#endif

namespace
{
   // ---- inputs (generated once, before any timing) --------------------------
   std::string g_ascii = bench::make_ascii(4u * 1024 * 1024);
   std::string g_utf8 = bench::make_mixed_utf8(4u * 1024 * 1024);
   std::string g_esc_plain = bench::make_escapes(4u * 1024 * 1024, 0);
   std::string g_esc_hot = bench::make_escapes(1u * 1024 * 1024, 4);
   std::string g_json = bench::make_json_like(4u * 1024 * 1024);
   std::string g_numbers = bench::make_numbers(200000);
   std::vector<double> g_doubles = bench::make_doubles(200000);
   std::string g_ws = bench::make_ws_runs(4u * 1024 * 1024, 8);
   std::vector<char> g_out(16u * 1024 * 1024);

   // The escape writer, matching the library's default (2 bytes for quote/backslash,
   // 6 for a control character, 1 otherwise). Not the point of the measurement — the
   // scan is — but it must be a real writer so the scan cannot be optimized away.
   GLZ_ALWAYS_INLINE void append_escape(const char*& c, char*& data)
   {
      const unsigned char b = static_cast<unsigned char>(*c);
      if (b == '"') {
         std::memcpy(data, "\\\"", 2);
         data += 2;
      }
      else if (b == '\\') {
         std::memcpy(data, "\\\\", 2);
         data += 2;
      }
      else if (b < 0x20) {
         std::memcpy(data, "\\u0000", 6);
         data += 6;
      }
      else {
         *data++ = *c;
      }
      ++c;
   }

   // The exact cascade from glaze/json/write.hpp (readable at the source), minus the
   // surrounding buffer bookkeeping. GLZ_USE_* selects the helpers; the SWAR tail is
   // always compiled and is the whole story under GLZ_DISABLE_SIMD.
   std::size_t escape_into(const char* src, std::size_t n, char* out)
   {
      const char* c = src;
      const char* const e = src + n;
      char* data = out;

      auto write_escape = [&]() { append_escape(c, data); };

#if defined(GLZ_USE_AVX2)
      glz::detail::avx2_string_escape(c, e, data, n, write_escape);
#endif
#if defined(GLZ_USE_SSE2)
      glz::detail::sse2_string_escape(c, e, data, n, write_escape);
#elif defined(GLZ_USE_NEON)
      glz::detail::neon_string_escape(c, e, data, n, write_escape);
#endif

      // SWAR: 8 bytes at a time (copied verbatim from json/write.hpp).
      if (n > 7) {
         for (const auto end_m7 = e - 7; c < end_m7;) {
            std::memcpy(data, c, 8);
            uint64_t swar;
            std::memcpy(&swar, c, 8);
            if constexpr (std::endian::native == std::endian::big) swar = std::byteswap(swar);

            constexpr uint64_t lo7_mask = glz::repeat_byte8(0b01111111);
            const uint64_t lo7 = swar & lo7_mask;
            const uint64_t quote = (lo7 ^ glz::repeat_byte8('"')) + lo7_mask;
            const uint64_t backslash = (lo7 ^ glz::repeat_byte8('\\')) + lo7_mask;
            const uint64_t less_32 = (swar & glz::repeat_byte8(0b01100000)) + lo7_mask;
            uint64_t next = ~((quote & backslash & less_32) | swar);
            next &= glz::repeat_byte8(0b10000000);
            if (next == 0) {
               data += 8;
               c += 8;
               continue;
            }
            const auto length = (glz::countr_zero(next) >> 3);
            c += length;
            data += length;
            write_escape();
         }
      }

      // Scalar tail.
      for (; c < e; ++c) {
         const unsigned char b = static_cast<unsigned char>(*c);
         if (b == '"' || b == '\\' || b < 0x20) append_escape(c, data);
         else *data++ = *c;
      }
      return std::size_t(data - out);
   }

   // ---- kernels -------------------------------------------------------------

   std::uint64_t k_utf8_dispatch()
   {
      return glz::validate_utf8(g_utf8.data(), g_utf8.size()) ? 1u : 0u;
   }

   std::uint64_t k_utf8_dispatch_ascii()
   {
      return glz::validate_utf8(g_ascii.data(), g_ascii.size()) ? 1u : 0u;
   }

   std::uint64_t k_utf8_scalar()
   {
      const auto* p = reinterpret_cast<const std::uint8_t*>(g_utf8.data());
      return glz::validate_utf8_scalar(p, p + g_utf8.size()) ? 1u : 0u;
   }

#if defined(GLZ_UTF8_SIMD)
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
#endif

   std::uint64_t k_escape_plain()
   {
      return escape_into(g_esc_plain.data(), g_esc_plain.size(), g_out.data());
   }
   std::uint64_t k_escape_hot()
   {
      return escape_into(g_esc_hot.data(), g_esc_hot.size(), g_out.data());
   }

#if defined(GLZ_STRUCTURAL_SIMD)
   std::uint64_t k_structural()
   {
      const char* p = g_json.data();
      const char* const end = p + g_json.size();
      std::uint64_t acc = 0;
      while (std::size_t(end - p) >= 64) {
         const auto w = glz::detail::structural::load_window(p, '[', ']');
         acc += std::size_t(std::popcount(w.quote)) + std::size_t(std::popcount(w.open)) +
                std::size_t(std::popcount(w.close)) + w.non_ascii;
         p += 64;
      }
      return acc;
   }
#endif

   // Scalar equivalent of the same four masks, byte at a time. Present in every build
   // so the SIMD mask construction has a same-work baseline to compare against.
   std::uint64_t k_structural_scalar()
   {
      const char* p = g_json.data();
      const char* const end = p + g_json.size();
      std::uint64_t acc = 0;
      while (std::size_t(end - p) >= 64) {
         // Keep this genuinely scalar: without it -O3 auto-vectorizes the byte loop
         // differently per -m flag and the "scalar" baseline stops being scalar.
         #pragma clang loop vectorize(disable) unroll(disable)
         for (int i = 0; i < 64; ++i) {
            const unsigned char b = static_cast<unsigned char>(p[i]);
            acc += (b == '"') + (b == '[') + (b == ']') + ((b & 0x80) != 0);
         }
         p += 64;
      }
      return acc;
   }

   std::uint64_t k_fastfloat()
   {
      const char* p = g_numbers.data();
      const char* const e = p + g_numbers.size();
      double sum = 0.0;
      while (p < e) {
         double v = 0.0;
         const auto r = glz::fast_float::from_chars(p, e, v);
         if (r.ec != std::errc{}) break;
         sum += v;
         p = r.ptr;
         while (p < e && (*p == ' ' || *p == '\n')) ++p;
      }
      return std::bit_cast<std::uint64_t>(sum);
   }

   std::uint64_t k_zmij()
   {
      char buf[64];
      std::uint64_t acc = 0;
      for (const double d : g_doubles) {
         acc += std::size_t(glz::to_chars(buf, d) - buf);
      }
      return acc;
   }

   // Linear: walk to each run of spaces and let the library SWAR compare skip it.
   // (An earlier version called memchr looking for a newline and rescanned the tail
   // every iteration, which was quadratic and dominated the number.)
   std::uint64_t k_skip_matching_ws()
   {
      const char ws[8] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
      const char* p = g_ws.data();
      const char* const e = p + g_ws.size();
      std::uint64_t acc = 0;
      while (p < e) {
         while (p < e && *p != ' ') ++p;
         if (p == e) break;
         auto q = p;
         glz::skip_matching_ws(ws, q, 8);
         acc += std::size_t(q - p);
         p = q;
      }
      return acc;
   }
}

int main()
{
   std::vector<bench::kernel> kernels;
   auto add = [&](std::string name, std::string backend, std::size_t bytes, std::uint64_t (*fn)()) {
      kernels.push_back({std::move(name), std::move(backend), bytes, fn});
   };

#if defined(GLZ_UTF8_SIMD)
   const std::string utf8_be{glz::detail::utf8_simd::backend};
#else
   const std::string utf8_be{"scalar"};
#endif

   add("utf8_dispatch_mixed", utf8_be, g_utf8.size(), &k_utf8_dispatch);
   add("utf8_dispatch_ascii", utf8_be, g_ascii.size(), &k_utf8_dispatch_ascii);
   add("utf8_scalar_mixed", "scalar", g_utf8.size(), &k_utf8_scalar);
#if defined(GLZ_UTF8_SIMD)
   add("utf8_vector_mixed", utf8_be, g_utf8.size(), &k_utf8_vector);
   add("utf8_vector_ascii", utf8_be, g_ascii.size(), &k_utf8_vector_ascii);
#endif
   add("escape_plain", std::string(glz::detail::string_escape_simd()), g_esc_plain.size(), &k_escape_plain);
   add("escape_hot", std::string(glz::detail::string_escape_simd()), g_esc_hot.size(), &k_escape_hot);
#if defined(GLZ_STRUCTURAL_SIMD)
   add("structural_window", std::string(glz::detail::structural::backend), g_json.size(), &k_structural);
#endif
   add("structural_scalar_equiv", "scalar", g_json.size(), &k_structural_scalar);
   add("fastfloat_parse", "SWAR(char)", g_numbers.size(), &k_fastfloat);
   add("zmij_float_write", std::string(glz::detail::float_write_simd()), g_doubles.size() * 8, &k_zmij);
   add("skip_matching_ws", "SWAR", g_ws.size(), &k_skip_matching_ws);

   return bench::bench_entry("header", GLZ_BENCH_FLAGS, std::move(kernels));
}

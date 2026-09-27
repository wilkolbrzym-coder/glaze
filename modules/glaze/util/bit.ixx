// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/util/bit.hpp"
// glz:header std=<bit>
// glz:header std=<cstdint>
// glz:header include="glaze/util/inline.hpp"
// glz:header project_imports=ignore
export module glaze.util.bit;

import std;
import glaze.core.basic_types;

#include "glaze/util/inline.hpp"


export namespace glz
{
   // std::countr_zero uses another branch check whether the input is zero,
   // we use this function when we know that x > 0
   GLZ_ALWAYS_INLINE auto countr_zero(const glz::uint32_t x) noexcept
   {
#ifdef _MSC_VER
      return std::countr_zero(x);
#else
#if __has_builtin(__builtin_ctzl)
      return __builtin_ctzl(x);
#else
      return std::countr_zero(x);
#endif
#endif
   }

   GLZ_ALWAYS_INLINE auto countr_zero(const glz::uint64_t x) noexcept
   {
#ifdef _MSC_VER
      return std::countr_zero(x);
#else
#if __has_builtin(__builtin_ctzll)
      return __builtin_ctzll(x);
#else
      return std::countr_zero(x);
#endif
#endif
   }

#if defined(__SIZEOF_INT128__)
   GLZ_ALWAYS_INLINE auto countr_zero(__uint128_t x) noexcept
   {
      glz::uint64_t low = glz::uint64_t(x);
      if (low != 0) {
         return countr_zero(low);
      }
      else {
         glz::uint64_t high = glz::uint64_t(x >> 64);
         return countr_zero(high) + 64;
      }
   }
#endif

   // std::countl_zero uses another branch check whether the input is zero,
   // we use this function when we know that x > 0
   GLZ_ALWAYS_INLINE constexpr auto countl_zero(const glz::uint32_t x) noexcept
   {
#ifdef _MSC_VER
      return std::countl_zero(x);
#else
#if __has_builtin(__builtin_clz)
      return __builtin_clz(x);
#else
      return std::countl_zero(x);
#endif
#endif
   }

   constexpr int int_log2(glz::uint32_t x) noexcept { return 31 - glz::countl_zero(x | 1); }

   consteval glz::uint32_t repeat_byte4(const auto repeat) { return glz::uint32_t(0x01010101u) * glz::uint8_t(repeat); }

   consteval glz::uint64_t repeat_byte8(const glz::uint8_t repeat) { return 0x0101010101010101ull * repeat; }

   // Byte-granular tests over a word, the SWAR technique from Mycroft's bit twiddling notes: a
   // byte that matches becomes zero after the xor, and a zero byte is the only one that borrows
   // into its own high bit. The result marks the low bit group of every match, so the first match
   // is the lowest set bit however many bytes matched.
   GLZ_ALWAYS_INLINE constexpr glz::uint64_t has_zero(const glz::uint64_t chunk) noexcept
   {
      return (((chunk - 0x0101010101010101ull) & ~chunk) & 0x8080808080808080ull);
   }

   GLZ_ALWAYS_INLINE constexpr glz::uint64_t has_quote(const glz::uint64_t chunk) noexcept
   {
      return has_zero(chunk ^ repeat_byte8('"'));
   }

   GLZ_ALWAYS_INLINE constexpr glz::uint64_t has_escape(const glz::uint64_t chunk) noexcept
   {
      return has_zero(chunk ^ repeat_byte8('\\'));
   }

   GLZ_ALWAYS_INLINE constexpr glz::uint64_t has_space(const glz::uint64_t chunk) noexcept
   {
      return has_zero(chunk ^ repeat_byte8(' '));
   }

   template <char Char>
   GLZ_ALWAYS_INLINE constexpr glz::uint64_t has_char(const glz::uint64_t chunk) noexcept
   {
      return has_zero(chunk ^ repeat_byte8(Char));
   }
}

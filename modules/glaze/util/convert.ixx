// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/util/convert.hpp"
// glz:header std=<bit>
// glz:header std=<cstdint>
// glz:header project_imports=ignore
export module glaze.util.convert;

import std;
import glaze.core.basic_types;


namespace glz
{
   // Creates a uint16_t from two chars that matches memcpy behavior on any endianness
   export consteval glz::uint16_t to_uint16_t(const char chars[2])
   {
      if constexpr (std::endian::native == std::endian::little) {
         return glz::uint16_t(chars[0]) | (glz::uint16_t(chars[1]) << 8);
      }
      else {
         return (glz::uint16_t(chars[0]) << 8) | glz::uint16_t(chars[1]);
      }
   }
}

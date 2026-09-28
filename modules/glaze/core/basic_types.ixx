// Glaze Library
// For the license information refer to glaze.ixx
// glz:header skip
// glz:header std=<cstddef>
// glz:header std=<cstdint>
export module glaze.core.basic_types;

import std;

// Basic C type aliases. Glaze code uses glz::size_t, glz::uint64_t, ... so that
// module-local `using std::...;` declarations are never needed and never leak
// into generated headers or a consumer's global namespace.
export namespace glz
{
   using size_t = std::size_t;
   using ptrdiff_t = std::ptrdiff_t;
   using int8_t = std::int8_t;
   using int16_t = std::int16_t;
   using int32_t = std::int32_t;
   using int64_t = std::int64_t;
   using uint8_t = std::uint8_t;
   using uint16_t = std::uint16_t;
   using uint32_t = std::uint32_t;
   using uint64_t = std::uint64_t;
}

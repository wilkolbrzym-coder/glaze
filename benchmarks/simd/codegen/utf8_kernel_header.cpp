// Representative SIMD kernel, HEADER translation unit, for the codegen comparison.
// Emitted assembly is diffed against utf8_kernel_module.cpp.
#include <cstddef>
#include <cstdint>

#include "glaze/simd/utf8_validation.hpp"

extern "C" std::size_t utf8_kernel(const std::uint8_t* p, std::size_t n)
{
   return glz::detail::utf8_simd::validate(p, p + n) ? 1u : 0u;
}

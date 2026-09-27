// Representative SIMD kernel, MODULE translation unit, for the codegen comparison.
// Same body as utf8_kernel_header.cpp; the only difference is that the code comes
// from an imported module rather than an included header.
import std;
import glaze.simd.utf8_validation;

extern "C" std::size_t utf8_kernel(const std::uint8_t* p, std::size_t n)
{
   return glz::detail::utf8_simd::validate(p, p + n) ? 1u : 0u;
}

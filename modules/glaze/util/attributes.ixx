// Glaze Library
// For the license information refer to glaze.hpp
// glz:header path="glaze/util/attributes.hpp"
module;

#ifndef __has_cpp_attribute
#define GLZ_LIFETIMEBOUND
#elif __has_cpp_attribute(msvc::lifetimebound)
#define GLZ_LIFETIMEBOUND [[msvc::lifetimebound]]
#elif __has_cpp_attribute(clang::lifetimebound)
#define GLZ_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define GLZ_LIFETIMEBOUND
#endif
// glz:emit std
export module glaze.util.attributes;

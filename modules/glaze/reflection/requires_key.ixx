// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/reflection/requires_key.hpp"
// glz:header std=<string_view>
// glz:header project_imports=ignore
// glz:header trailing_newline=no
export module glaze.reflection.requires_key;

import std;

export import glaze.forward;

namespace glz
{
   // glz:module-only
   // The primary template lives in glaze/forward.hpp (re-exported below), so
   // the unit must not declare it again.  The declaration still has to appear
   // in the standalone header, where nothing is imported: it is written here
   // wrapped in `#if 0` so the module's own translation unit skips it, while
   // the module-only markers hide the preprocessor scaffolding from the header.
#if 0
   // glz:end-module-only
   template <class T>
   struct meta;
   // glz:module-only
#endif
   // glz:end-module-only

   /// Concept for when requires_key is available in glz::meta
   export template <class T>
   concept meta_has_requires_key = requires(T t, const std::string_view s, const bool is_nullable) {
      { glz::meta<std::remove_cvref_t<T>>::requires_key(s, is_nullable) } -> std::same_as<bool>;
   };
}

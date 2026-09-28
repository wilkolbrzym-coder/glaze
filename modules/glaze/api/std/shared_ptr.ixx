// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/api/std/shared_ptr.hpp"
// glz:header std=<memory>
// glz:header include="glaze/core/meta.hpp"
// glz:header project_imports=ignore
export module glaze.api.std.shared_ptr;

import std;

import glaze.core.meta;
import glaze.util.string_literal;

export namespace glz
{
   template <class T>
   struct meta<std::shared_ptr<T>>
   {
      static constexpr std::string_view name = join_v<chars<"std::shared_ptr<">, name_v<T>, chars<">">>;
   };
}

// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/api/std/list.hpp"
// glz:header std=<list>
// glz:header include="glaze/core/meta.hpp"
// glz:header project_imports=ignore
export module glaze.api.std.list;

import std;

import glaze.core.meta;
import glaze.util.string_literal;

export namespace glz
{
   template <class T>
   struct meta<std::list<T>>
   {
      static constexpr std::string_view name = join_v<chars<"std::list<">, name_v<T>, chars<">">>;
   };
}

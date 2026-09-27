// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/api/tuplet.hpp"
// glz:header include="glaze/core/meta.hpp"
// glz:header include="glaze/tuplet/tuple.hpp"
// glz:header project_imports=ignore
export module glaze.api.tuplet;

import std;

import glaze.core.meta;
import glaze.tuplet;
import glaze.util.string_literal;
import glaze.core.basic_types;


export namespace glz
{
   template <class... T>
   struct meta<glz::tuple<T...>>
   {
      static constexpr std::string_view name = []<glz::size_t... I>(std::index_sequence<I...>) {
         return join_v<chars<"glz::tuple<">,
                       ((I != sizeof...(T) - 1) ? join_v<name_v<T>, chars<",">> : join_v<name_v<T>>)..., chars<">">>;
      }(std::make_index_sequence<sizeof...(T)>{});
   };
}

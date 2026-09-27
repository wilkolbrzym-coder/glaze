// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/api/std/span.hpp"
// glz:header std=<span>
// glz:header include="glaze/api/hash.hpp"
// glz:header include="glaze/core/meta.hpp"
// glz:header project_imports=ignore
export module glaze.api.std.span;

import std;

import glaze.api.hash;

import glaze.core.meta;
import glaze.util.string_literal;
import glaze.core.basic_types;


export namespace glz
{
   template <class, template <class, glz::size_t> class>
   inline constexpr bool is_span_v = false;
   template <template <class, glz::size_t> class T, class Element, glz::size_t Extent>
   inline constexpr bool is_span_v<T<Element, Extent>, T> = true;

   template <class T>
   concept span = is_span_v<T, std::span>;

   template <span T>
   struct meta<T>
   {
      using V = typename T::element_type;
      static constexpr std::string_view extent = to_sv<T::extent>();
      static constexpr std::string_view name = join_v<chars<"std::span<">, name_v<V>, chars<",">, extent, chars<">">>;
   };
}

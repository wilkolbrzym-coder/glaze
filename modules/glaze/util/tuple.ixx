// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/util/tuple.hpp"
// glz:header std=<tuple>
// glz:header include="glaze/reflection/get_name.hpp"
// glz:header include="glaze/core/tuple.hpp"
// glz:header include="glaze/util/for_each.hpp"
// glz:header include="glaze/util/string_literal.hpp"
// glz:header project_imports=ignore
export module glaze.util.tuple;

import std;

import glaze.util.for_each;
import glaze.util.string_literal;
import glaze.util.type_traits;

import glaze.core.tuple;

export namespace glz
{
   template <class T>
   concept is_std_tuple = is_specialization_v<T, std::tuple>;
}

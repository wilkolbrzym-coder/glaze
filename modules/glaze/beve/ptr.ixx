// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/beve/ptr.hpp"
// glz:header include="glaze/beve/read.hpp"
// glz:header include="glaze/beve/write.hpp"
// glz:header include="glaze/core/ptr.hpp"
// glz:header project_imports=ignore
export module glaze.beve.ptr;

import std;

import glaze.beve.read;
import glaze.beve.write;

import glaze.core.opts;
import glaze.core.ptr;

import glaze.util.string_literal;

namespace glz
{
   template <class T, class B>
   bool read_as_binary(T&& root_value, const sv json_ptr, B&& buffer)
   {
      return read_as<opts{.format = BEVE}>(std::forward<T>(root_value), json_ptr, buffer);
   }

   template <class T, class B>
   bool write_as_binary(T&& root_value, const sv json_ptr, B&& buffer)
   {
      return write_as<opts{.format = BEVE}>(std::forward<T>(root_value), json_ptr, buffer);
   }
}

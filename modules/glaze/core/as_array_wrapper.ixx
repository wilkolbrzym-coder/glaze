// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/core/as_array_wrapper.hpp"
// glz:header include="glaze/core/opts.hpp"
// glz:header include="glaze/core/read.hpp"
// glz:header include="glaze/core/write.hpp"
// glz:header include="glaze/reflection/to_tuple.hpp"
// glz:header project_imports=ignore
export module glaze.core.as_array_wrapper;

import std;

import glaze.core.opts;
import glaze.core.read;
import glaze.core.write;
import glaze.core.context;
import glaze.core.common;
import glaze.util.type_traits;

import glaze.reflection.to_tuple;
import glaze.core.basic_types;


export namespace glz
{
   template <glz::uint32_t Format, class T>
      requires(is_specialization_v<T, as_array_wrapper>)
   struct from<Format, T>
   {
      template <auto Opts>
      static void op(auto&& wrapper, is_context auto&& ctx, auto&& it, auto end)
      {
         auto tie = to_tie(wrapper.value);
         parse<Format>::template op<Opts>(tie, ctx, it, end);
      }
   };

   template <glz::uint32_t Format, class T>
      requires(is_specialization_v<T, as_array_wrapper>)
   struct to<Format, T>
   {
      template <auto Opts, class... Args>
      static void op(auto&& wrapper, is_context auto&& ctx, Args&&... args)
      {
         auto tie = to_tie(wrapper.value);
         serialize<Format>::template op<Opts>(tie, ctx, std::forward<Args>(args)...);
      }
   };
}

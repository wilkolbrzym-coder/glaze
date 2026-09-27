// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/bson/wrappers.hpp"
// glz:header include="glaze/bson/read.hpp"
// glz:header include="glaze/bson/skip.hpp"
// glz:header include="glaze/core/custom.hpp"
// glz:header project_imports=ignore
export module glaze.bson.wrappers;

import std;

import glaze.bson.read;
import glaze.bson.skip;
import glaze.core.context;
import glaze.core.custom;
import glaze.forward;
import glaze.core.basic_types;
import glaze.util.type_traits;


namespace glz
{
   // BSON readers receive the element tag that the caller already consumed, so glz::custom needs its
   // own entry point here; the generic one in core/custom.hpp cannot pass the tag along. The write
   // side resolves custom getters in bson_detail::write_member_element, because the element type
   // byte depends on what the getter yields.
   export template <class T>
      requires(is_specialization_v<T, custom_t>)
   struct from<BSON, T>
   {
      template <auto Opts, class Value, is_context Ctx, class It, class End>
      static void op(Value&& value, glz::uint8_t tag, Ctx&& ctx, It& it, const End& end)
      {
         detail::dispatch_custom_read<T>(
            value, ctx,
            [&](auto& input) {
               from<BSON, std::decay_t<decltype(input)>>::template op<Opts>(input, tag, ctx, it, end);
            },
            [&] { skip_value<BSON>::template op<Opts>(tag, ctx, it, end); });
      }
   };
}

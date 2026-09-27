// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/msgpack/wrappers.hpp"
// glz:header include="glaze/core/custom.hpp"
// glz:header include="glaze/msgpack/read.hpp"
// glz:header include="glaze/msgpack/skip.hpp"
// glz:header project_imports=ignore
export module glaze.msgpack.wrappers;

import std;

import glaze.core.context;
import glaze.core.custom;
import glaze.forward;
import glaze.msgpack.read;
import glaze.msgpack.skip;
import glaze.core.basic_types;
import glaze.util.type_traits;


namespace glz
{
   // MessagePack readers receive the format tag that the caller already consumed, so glz::custom
   // needs its own entry point here; the generic one in core/custom.hpp cannot pass the tag along.
   // The write side needs nothing extra - to<Format, custom_t> is already format independent.
   export template <class T>
      requires(is_specialization_v<T, custom_t>)
   struct from<MSGPACK, T>
   {
      template <auto Opts, class Value, is_context Ctx, class It, class End>
      static void op(Value&& value, glz::uint8_t tag, Ctx&& ctx, It& it, const End& end)
      {
         detail::dispatch_custom_read<T>(
            value, ctx,
            [&](auto& input) {
               from<MSGPACK, std::decay_t<decltype(input)>>::template op<Opts>(input, tag, ctx, it, end);
            },
            [&] { skip_value<MSGPACK>::template op<Opts>(tag, ctx, it, end); });
      }
   };
}

// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/jsonb/skip.hpp"
// glz:header include="glaze/core/context.hpp"
// glz:header include="glaze/core/opts.hpp"
// glz:header include="glaze/jsonb/header.hpp"
// glz:header project_imports=ignore
export module glaze.jsonb.skip;

import std;

import glaze.jsonb.header;

import glaze.core.context;
import glaze.core.opts;
import glaze.core.basic_types;

#include "glaze/util/inline.hpp"


namespace glz
{
   template <>
   struct skip_value<JSONB>
   {
      template <auto Opts>
      GLZ_ALWAYS_INLINE static void op(is_context auto& ctx, auto& it, auto end) noexcept
      {
         glz::uint8_t type_code{};
         glz::uint64_t payload_size{};
         if (!jsonb::read_header(ctx, it, end, type_code, payload_size)) {
            return;
         }
         if (static_cast<glz::uint64_t>(end - it) < payload_size) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }
         it += payload_size;
      }
   };
}

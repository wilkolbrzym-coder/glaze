// glz:header path="glaze/eetf/wrappers.hpp"
// glz:header include="opts.hpp" group=g_rel0
// glz:header include="read.hpp" group=g_rel0
// glz:header include="types.hpp" group=g_rel0
// glz:header include="write.hpp" group=g_rel0
// glz:header project_imports=ignore
module;
// glz:module-only
#include "glaze/concepts/container_concepts.hpp"
#include "glaze/eetf/opts.hpp"
#include "glaze/eetf/read.hpp"
#include "glaze/eetf/types.hpp"
#include "glaze/eetf/write.hpp"
// glz:end-module-only

#include <glaze/core/custom.hpp>
#include <glaze/core/wrappers.hpp>

// glz:emit g_rel0
export module glaze.eetf.wrappers;

import std;
import glaze.core.basic_types;

export namespace glz
{
   // write.hpp closes with an unconstrained `to<EETF, T>` that has no op, so that an unsupported
   // value type fails to compile. Fixing Format makes that sink more specialized than the format
   // independent to<Format, custom_t>, so glz::custom needs an EETF entry point to get past it.
   // The read side has no such sink and uses the generic one.
   template <class T>
      requires(is_specialization_v<T, custom_t>)
   struct to<EETF, T>
   {
      template <auto Opts>
      static void op(auto&& value, is_context auto&& ctx, auto&&... args)
      {
         detail::dispatch_custom_write<EETF, Opts, T>(value, ctx, args...);
      }
   };

   template <class T>
   struct atom_as_string_t
   {
      static constexpr bool glaze_wrapper = true;
      using value_type = T;
      T& val;
   };

   template <class T>
   struct from<EETF, atom_as_string_t<T>>
   {
      template <auto Opts>
      static void op(auto&& value, auto&&... args)
      {
         eetf::atom a{};
         parse<EETF>::op<Opts>(a, args...);
         value.val = a;
      }
   };

   template <class T>
   struct to<EETF, atom_as_string_t<T>>
   {
      template <auto Opts>
      static void op(auto&& value, is_context auto&& ctx, auto&& b, auto&& ix)
      {
         eetf::atom s(value.val);
         using S = core_t<decltype(s)>;
         to<EETF, S>::template op<Opts>(s, ctx, b, ix);
      }
   };

   template <auto MemPtr>
   inline constexpr decltype(auto) aas_impl() noexcept
   {
      return [](auto&& val) { return atom_as_string_t<std::remove_reference_t<decltype(val.*MemPtr)>>{val.*MemPtr}; };
   }

   // Read and write atoms as string
   template <auto MemPtr>
   constexpr auto atom_as_string = aas_impl<MemPtr>();
}

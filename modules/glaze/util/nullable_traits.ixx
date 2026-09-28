// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/util/nullable_traits.hpp"
// glz:header std=<memory>
// glz:header std=<optional>
// glz:header include="glaze/core/common.hpp"
// glz:header include="glaze/core/opts.hpp"
// glz:header project_imports=ignore
export module glaze.util.nullable_traits;

import std;

import glaze.concepts.container_concepts;
import glaze.core.common;
import glaze.core.context;
import glaze.core.meta;
import glaze.core.opts;
import glaze.util.type_traits;

export namespace glz
{
   // Emplace a default-constructed value into a nullable type so the inner value can be parsed into.
   // Handles std::optional, std::unique_ptr, std::shared_ptr, raw pointers (when Opts/ctx permit
   // allocation), and any other nullable_like type exposing emplace() or a glz::meta constructor.
   // Returns true on success; on failure, sets ctx.error = invalid_nullable_read and returns false.
   template <auto Opts, nullable_like T>
   inline bool nullable_emplace(T& nullable, auto& ctx)
   {
      if constexpr (optional_like<T>) {
         nullable.emplace();
         return true;
      }
      else if constexpr (is_specialization_v<T, std::unique_ptr>) {
         nullable = std::make_unique<typename T::element_type>();
         return true;
      }
      else if constexpr (is_specialization_v<T, std::shared_ptr>) {
         nullable = std::make_shared<typename T::element_type>();
         return true;
      }
      else if constexpr (requires { nullable.emplace(); }) {
         nullable.emplace();
         return true;
      }
      else if constexpr (constructible<T>) {
         nullable = meta_construct_v<T>();
         return true;
      }
      else if constexpr (std::is_pointer_v<T> && can_allocate_raw_pointer<Opts, std::decay_t<decltype(ctx)>>) {
         return try_allocate_raw_pointer<Opts>(nullable, ctx);
      }
      else {
         ctx.error = error_code::invalid_nullable_read;
         return false;
      }
   }
}

// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/toml/opts.hpp"
// glz:header std=<glaze/core/opts.hpp>
// glz:header project_imports=ignore
// glz:header license=none
export module glaze.toml.opts;

import glaze.core.opts;

import std;
import glaze.core.basic_types;


export namespace glz::toml
{

   enum struct opts_internal : glz::uint32_t {
      none = 0,
      internal_struct = 1 << 0, // Currently in an inner struct
   };

   struct toml_opts
   {
      glz::uint32_t format = TOML;
      bool error_on_unknown_keys{true};

      glz::uint32_t internal{}; // default to 0
   };

   consteval bool check_is_internal(auto&& o) { return o.internal & glz::uint32_t(opts_internal::internal_struct); }

   template <auto Opts>
   constexpr auto is_internal_on()
   {
      auto ret = Opts;
      ret.internal |= glz::uint32_t(opts_internal::internal_struct);
      return ret;
   }

} // namespace glz::toml

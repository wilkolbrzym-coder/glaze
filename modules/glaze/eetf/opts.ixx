// glz:header path="glaze/eetf/opts.hpp"
// glz:header project_imports=ignore
module;

#include "glaze/core/opts.hpp"
// glz:emit std
export module glaze.eetf.opts;

import std;
import glaze.core.basic_types;

export namespace glz::eetf
{

   // layout erlang term
   inline constexpr glz::uint8_t map_layout = 0;
   inline constexpr glz::uint8_t proplist_layout = 1;

   struct eetf_opts
   {
      glz::uint32_t format = EETF;
      glz::uint32_t internal{};
      glz::uint8_t layout = map_layout;
      bool error_on_unknown_keys = true;
      bool shrink_to_fit = false;
      bool prettify = false;
      bool quoted_num = false; // treat numbers as quoted or array-like types as having quoted numbers
      bool string_as_number = false; // treats all types like std::string as numbers: read/write these quoted numbers
      bool unquoted = false; // write out string like values without quotes
      bool raw_string = false; // do not decode/encode escaped characters for strings (improves read/write performance)
   };

} // namespace glz::eetf

// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/format/format_to.hpp"
// glz:header include="glaze/core/write.hpp"
// glz:header include="glaze/util/itoa.hpp"
// glz:header include="glaze/util/zmij.hpp"
// glz:header project_imports=ignore
export module glaze.format.format_to;

import glaze.core.write;
import glaze.util.itoa;
import glaze.concepts.container_concepts;
import glaze.util.zmij;

import std;
import glaze.core.basic_types;


namespace glz
{
   export template <num_t T>
   void format_to(std::string& buffer, T&& value)
   {
      auto ix = buffer.size();
      buffer.resize((std::max)(buffer.size() * 2, ix + 64));

      const auto start = buffer.data() + ix;
      const auto end = glz::to_chars(start, std::forward<T>(value));
      ix += glz::size_t(end - start);
      buffer.resize(ix);
   }
}

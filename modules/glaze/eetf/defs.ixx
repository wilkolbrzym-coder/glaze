// glz:header path="glaze/eetf/defs.hpp"
// glz:header include="types.hpp" group=g_rel0
// glz:header project_imports=ignore
module;
// glz:module-only
#include "glaze/concepts/container_concepts.hpp"
#include "glaze/eetf/types.hpp"
// glz:end-module-only

#include <glaze/core/common.hpp>

// glz:emit g_rel0
export module glaze.eetf.defs;

import std;
import glaze.core.basic_types;

export namespace glz
{
   inline constexpr int eetf_magic_version = 131;

   template <class T>
   concept atom_t = string_t<T> && std::same_as<typename T::tag, eetf::tag_atom>;
} // namespace glz

// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/chrono.hpp"
// glz:header include="glaze/core/chrono.hpp"
// glz:header include="glaze/json/chrono_format.hpp"
// glz:header include="glaze/json/read.hpp"
// glz:header include="glaze/json/write.hpp"
// glz:header project_imports=ignore
export module glaze.chrono;

// Convenience header for std::chrono support
// Includes all necessary headers for chrono serialization

export import glaze.core.chrono;
export import glaze.json.chrono_format;
export import glaze.json.read;
export import glaze.json.write;

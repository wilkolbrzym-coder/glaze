// Glaze Library
// For the license information refer to glaze.ixx
// glz:header path="glaze/csv.hpp"
// glz:header include="glaze/core/as_array_wrapper.hpp" group=a
// glz:header include="glaze/core/custom.hpp" group=b
// glz:header include="glaze/core/wrapper_traits.hpp" group=b
// glz:header include="glaze/csv/read.hpp" group=b
// glz:header include="glaze/csv/skip.hpp" group=b
// glz:header include="glaze/csv/write.hpp" group=b
// glz:header include="glaze/thread/atomic.hpp" group=b
// glz:header project_imports=ignore
module;

// glz:emit a
// CSV cannot serialize a glz::custom field - its columnar layout requires every struct field to be
// a container of row values, not a single value. This is included anyway so that attempting it
// fails inside the CSV writer, which names the real constraint, rather than on an undefined
// from/to specialization that reads like a missing include.
// glz:emit b
export module glaze.csv;

export import glaze.core.as_array_wrapper;
export import glaze.core.wrapper_traits;

export import glaze.csv.read;
export import glaze.csv.skip;
export import glaze.csv.write;

export import glaze.thread.atomic;

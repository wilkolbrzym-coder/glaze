// Glaze Library
// For the license information refer to glaze.hpp

// C++ helper for implementing REPE plugins
// Provides common functionality to reduce code duplication
// glz:header path="glaze/rpc/repe/plugin_helper.hpp"
// glz:header std=<span>
// glz:header std=<string>
// glz:header include="glaze/rpc/registry.hpp"
// glz:header include="glaze/rpc/repe/buffer.hpp"
// glz:header include="glaze/rpc/repe/header.hpp"
// glz:header include="glaze/rpc/repe/plugin.h"
// glz:header project_imports=ignore
module;

// glz:emit std

// glz:emit project

export module glaze.rpc.repe.plugin_helper;

import std;

import glaze.core.basic_types;
import glaze.rpc.registry;
import glaze.rpc.repe.buffer;
import glaze.rpc.repe.header;
import glaze.rpc.repe.plugin;


export namespace glz::repe
{
   // Thread-local response buffer for plugin implementations
   // Note: This buffer grows as needed but does not shrink during the thread's lifetime
   inline thread_local std::string plugin_response_buffer;

   // Create an error response with proper REPE format (zero-copy to thread-local buffer)
   inline void plugin_error_response(error_code ec, std::string_view error_msg, glz::uint64_t id = 0)
   {
      encode_error_buffer(ec, plugin_response_buffer, error_msg, id);
   }

   // Zero-copy plugin call implementation
   // Dispatches a REPE request to a registry and returns the response
   // Note: Plugin initialization should be done via repe_plugin_init before any calls
   template <typename Registry>
   repe_buffer plugin_call(Registry& registry, const char* request, glz::uint64_t request_size)
   {
      // Use zero-copy span-based call - parses request in-place, writes directly to response buffer
      // Exception handling is done internally by the registry
      registry.call(std::span<const char>{request, static_cast<glz::size_t>(request_size)}, plugin_response_buffer);
      return {plugin_response_buffer.data(), plugin_response_buffer.size()};
   }

} // namespace glz::repe

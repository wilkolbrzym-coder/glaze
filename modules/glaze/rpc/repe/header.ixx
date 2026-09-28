// Glaze Library
// For the license information refer to glaze.hpp
// glz:header path="glaze/rpc/repe/header.hpp"
// glz:header std=<cstdint>
// glz:header std=<limits>
// glz:header std=<string>
// glz:header std=<string_view>
// glz:header include="glaze/core/context.hpp"
// glz:header project_imports=ignore
module;

// glz:emit std

// glz:emit project

export module glaze.rpc.repe.header;

import std;

import glaze.core.basic_types;
import glaze.core.context;


export namespace glz::repe
{
   // REPE protocol magic bytes (0x1507 = 5383)
   inline constexpr glz::uint16_t repe_magic = 0x1507;

   // REPE Reserved Query Formats (0-4095 are reserved)
   enum class query_format : glz::uint16_t { RAW_BINARY = 0, JSON_POINTER = 1 };

   // REPE Reserved Body Formats (0-4095 are reserved)
   enum class body_format : glz::uint16_t { RAW_BINARY = 0, BEVE = 1, JSON = 2, UTF8 = 3 };

   struct header
   {
      glz::uint64_t length{}; // Total length of [header, query, body]
      //
      glz::uint16_t spec{repe_magic}; // Magic two bytes to denote the REPE specification
      glz::uint8_t version = 1; // REPE version
      glz::uint8_t notify{}; // 1 (true) for no response from server
      glz::uint32_t reserved{}; // Must be zero, receivers must ignore this field
      //
      glz::uint64_t id{}; // Identifier
      //
      glz::uint64_t query_length{}; // The total length of the query
      //
      glz::uint64_t body_length{}; // The total length of the body
      //
      repe::query_format query_format{};
      repe::body_format body_format{};
      error_code ec{};
   };

   static_assert(sizeof(header) == 48);

   // Computes the expected total message length (header + query + body) from an
   // attacker-controllable header without overflowing. query_length and body_length
   // are 64-bit fields supplied on the wire, so a naive sum can wrap and defeat the
   // subsequent buffer-size validation, producing out-of-bounds query/body views.
   // Returns false if the lengths would overflow, in which case the header is malformed.
   [[nodiscard]] inline bool checked_message_length(const header& hdr, glz::uint64_t& expected) noexcept
   {
      constexpr glz::uint64_t header_size = sizeof(header);
      if (hdr.query_length > (std::numeric_limits<glz::uint64_t>::max)() - header_size) {
         return false;
      }
      const glz::uint64_t header_and_query = header_size + hdr.query_length;
      if (hdr.body_length > (std::numeric_limits<glz::uint64_t>::max)() - header_and_query) {
         return false;
      }
      expected = header_and_query + hdr.body_length;
      return true;
   }

   // query and body are heap allocated and we want to be able to reuse memory
   struct message final
   {
      repe::header header{};
      std::string query{};
      std::string body{};

      operator bool() const { return bool(header.ec); }

      error_code error() const { return header.ec; }
   };

   // User interface that will be encoded into a REPE header
   struct user_header final
   {
      std::string_view query = ""; // The JSON pointer path to call or member to access/assign
      glz::uint64_t id{}; // Identifier
      error_code ec{};
      bool notify{};
   };

   inline repe::header encode(const user_header& h) noexcept
   {
      repe::header ret{
         .notify = h.notify, //
         .id = h.id, //
         .query_length = h.query.size(), //
         .ec = h.ec //
      };
      return ret;
   }
}


// Behavioural tests for the glaze.bson module.
#include "harness.hpp"

import glaze.bson;

#include <cstdint>
#include <string>
#include <vector>

struct doc {
   std::string title{};
   std::int32_t n{};
   std::vector<double> xs{};
};

GLZ_TEST(bson_roundtrip_doc)
{
   doc d{"bson", 7, {1.5, 2.5}};
   const auto enc = glz::write_bson(d);
   GLZ_CHECK(enc.has_value());
   // BSON documents start with a 4-byte little-endian length.
   GLZ_CHECK(enc->size() >= 4);

   const auto dec = glz::read_bson<doc>(enc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(dec->title, std::string{"bson"});
   GLZ_CHECK_EQ(dec->n, std::int32_t{7});
   GLZ_CHECK_EQ(dec->xs.size(), std::size_t{2});
}

GLZ_TEST(bson_length_prefix_matches_payload)
{
   doc d{"len", 1, {}};
   const auto enc = glz::write_bson(d);
   GLZ_CHECK(enc.has_value());
   if (enc.has_value() && enc->size() >= 4) {
      const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>((*enc)[0]));
      const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>((*enc)[1]));
      const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>((*enc)[2]));
      const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>((*enc)[3]));
      const std::uint32_t declared = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
      GLZ_CHECK_EQ(declared, static_cast<std::uint32_t>(enc->size()));
   }
   else {
      GLZ_CHECK(false);
   }
}

GLZ_TEST(bson_truncated_fails)
{
   doc d{"trunc", 9, {1.0, 2.0}};
   auto enc = glz::write_bson(d);
   GLZ_CHECK(enc.has_value());
   if (enc.has_value() && enc->size() > 6) {
      const std::string_view truncated{enc->data(), enc->size() - 4};
      doc out{};
      const auto err = glz::read_bson(out, truncated);
      GLZ_CHECK(static_cast<bool>(err));
   }
   else {
      GLZ_CHECK(false);
   }
}

GLZ_TEST_MAIN("modules/bson_test")

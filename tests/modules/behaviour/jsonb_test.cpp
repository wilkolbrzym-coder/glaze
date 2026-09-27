// Behavioural tests for the glaze.jsonb module (binary JSON).
#include "harness.hpp"

import glaze.jsonb;

#include <cstdint>
#include <string>
#include <vector>

struct record {
   std::string id{};
   std::vector<std::uint32_t> values{};
};

GLZ_TEST(jsonb_roundtrip_record)
{
   record r{"node", {10, 20, 30}};
   const auto enc = glz::write_jsonb(r);
   GLZ_CHECK(enc.has_value());
   GLZ_CHECK(!enc->empty());

   const auto dec = glz::read_jsonb<record>(enc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(dec->id, std::string{"node"});
   GLZ_CHECK_EQ(dec->values.size(), std::size_t{3});
   GLZ_CHECK_EQ(dec->values[1], std::uint32_t{20});
}

GLZ_TEST(jsonb_scalar_roundtrip)
{
   const auto enc = glz::write_jsonb(std::uint64_t{123456789});
   GLZ_CHECK(enc.has_value());
   const auto dec = glz::read_jsonb<std::uint64_t>(enc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(*dec, std::uint64_t{123456789});
}

GLZ_TEST(jsonb_truncated_fails)
{
   record r{"x", {1, 2}};
   auto enc = glz::write_jsonb(r);
   GLZ_CHECK(enc.has_value());
   if (enc.has_value() && enc->size() > 2) {
      const std::string_view truncated{enc->data(), enc->size() / 2};
      record out{};
      const auto err = glz::read_jsonb(out, truncated);
      GLZ_CHECK(static_cast<bool>(err));
   }
   else {
      GLZ_CHECK(false); // cannot truncate an empty document
   }
}

GLZ_TEST_MAIN("modules/jsonb_test")

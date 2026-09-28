// Behavioural tests for the glaze.beve module (binary efficient value encoding).
#include "harness.hpp"

import glaze.beve;

#include <cstdint>
#include <string>
#include <vector>

struct sample {
   std::string label{};
   std::vector<double> data{};
   bool flag{};
};

GLZ_TEST(beve_roundtrip_sample)
{
   sample s{"unit", {1.0, 2.0, 3.0}, true};
   const auto enc = glz::write_beve(s);
   GLZ_CHECK(enc.has_value());
   GLZ_CHECK(!enc->empty());

   const auto dec = glz::read_beve<sample>(enc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(dec->label, std::string{"unit"});
   GLZ_CHECK_EQ(dec->data.size(), std::size_t{3});
   GLZ_CHECK_EQ(dec->flag, true);
}

GLZ_TEST(beve_roundtrip_scalars)
{
   const auto enc = glz::write_beve(std::int32_t{-7});
   GLZ_CHECK(enc.has_value());
   const auto dec = glz::read_beve<std::int32_t>(enc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(*dec, std::int32_t{-7});
}

GLZ_TEST(beve_truncated_fails)
{
   sample s{"abcd", {1.0, 2.0, 3.0, 4.0}, true};
   auto enc = glz::write_beve(s);
   GLZ_CHECK(enc.has_value());
   if (enc.has_value() && enc->size() > 4) {
      const std::string_view truncated{enc->data(), enc->size() - 3};
      sample out{};
      const auto err = glz::read_beve(out, truncated);
      GLZ_CHECK(static_cast<bool>(err));
   }
   else {
      GLZ_CHECK(false);
   }
}

GLZ_TEST_MAIN("modules/beve_test")

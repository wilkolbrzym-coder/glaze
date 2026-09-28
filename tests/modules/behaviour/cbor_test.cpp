// Behavioural tests for the glaze.cbor module (RFC 8949).
#include "harness.hpp"

import glaze.cbor;

#include <cstdint>
#include <string>
#include <vector>

struct point {
   std::int32_t x{};
   std::int32_t y{};
};

GLZ_TEST(cbor_small_int_exact)
{
   // RFC 8949 canonical encoding of the unsigned integer 5 is the single
   // byte 0x05.
   const auto enc = glz::write_cbor(std::uint8_t{5});
   GLZ_CHECK(enc.has_value());
   GLZ_CHECK_EQ(enc->size(), std::size_t{1});
   GLZ_CHECK_EQ(static_cast<unsigned char>((*enc)[0]), 0x05u);
}

GLZ_TEST(cbor_array_roundtrip)
{
   std::vector<int> v{1, 2, 3};
   const auto enc = glz::write_cbor(v);
   GLZ_CHECK(enc.has_value());
   const auto dec = glz::read_cbor<std::vector<int>>(enc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(*dec, v);
}

GLZ_TEST(cbor_struct_roundtrip)
{
   point p{3, -4};
   const auto enc = glz::write_cbor(p);
   GLZ_CHECK(enc.has_value());
   const auto dec = glz::read_cbor<point>(enc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(dec->x, std::int32_t{3});
   GLZ_CHECK_EQ(dec->y, std::int32_t{-4});
}

GLZ_TEST(cbor_truncated_fails)
{
   std::vector<int> v{1, 2, 3, 4, 5};
   auto enc = glz::write_cbor(v);
   GLZ_CHECK(enc.has_value());
   if (enc.has_value() && enc->size() > 2) {
      const std::string_view truncated{enc->data(), enc->size() - 1};
      std::vector<int> out{};
      const auto err = glz::read_cbor(out, truncated);
      GLZ_CHECK(static_cast<bool>(err));
   }
   else {
      GLZ_CHECK(false);
   }
}

GLZ_TEST_MAIN("modules/cbor_test")

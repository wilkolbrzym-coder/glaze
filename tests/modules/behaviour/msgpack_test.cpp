// Behavioural tests for the glaze.msgpack module (MessagePack).
#include "harness.hpp"

import glaze.msgpack;

#include <cstdint>
#include <string>
#include <vector>

struct rec {
   std::string s{};
   std::vector<int> v{};

   friend bool operator==(const rec&, const rec&) = default;
};

GLZ_TEST(msgpack_positive_fixint_exact)
{
   // MessagePack encodes 0..127 as a single positive fixint byte.
   const auto enc = glz::write_msgpack(5u);
   GLZ_CHECK(enc.has_value());
   GLZ_CHECK_EQ(enc->size(), std::size_t{1});
   GLZ_CHECK_EQ(static_cast<unsigned char>((*enc)[0]), 0x05u);
}

GLZ_TEST(msgpack_roundtrip_rec)
{
   rec r{"m", {4, 5, 6}};
   const auto enc = glz::write_msgpack(r);
   GLZ_CHECK(enc.has_value());
   const auto dec = glz::read_msgpack<rec>(enc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(dec->s, std::string{"m"});
   GLZ_CHECK_EQ(*dec, r);
}

GLZ_TEST(msgpack_negative_fixint_exact)
{
   // -1 is 0xff in MessagePack.
   const auto enc = glz::write_msgpack(-1);
   GLZ_CHECK(enc.has_value());
   GLZ_CHECK_EQ(enc->size(), std::size_t{1});
   GLZ_CHECK_EQ(static_cast<unsigned char>((*enc)[0]), 0xffu);
}

GLZ_TEST(msgpack_truncated_fails)
{
   rec r{"trunc", {1, 2, 3, 4}};
   auto enc = glz::write_msgpack(r);
   GLZ_CHECK(enc.has_value());
   if (enc.has_value() && enc->size() > 2) {
      const std::string_view truncated{enc->data(), enc->size() - 1};
      rec out{};
      const auto err = glz::read_msgpack(out, truncated);
      GLZ_CHECK(static_cast<bool>(err));
   }
   else {
      GLZ_CHECK(false);
   }
}

GLZ_TEST_MAIN("modules/msgpack_test")

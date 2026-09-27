// Cross-module consistency: one value, every binary format, equal result.
#include "harness.hpp"

import glaze.json;
import glaze.jsonb;
import glaze.beve;
import glaze.cbor;
import glaze.bson;
import glaze.msgpack;

#include <cstdint>
#include <string>
#include <vector>

struct payload {
   std::string name{};
   std::int64_t count{};
   std::vector<double> values{};
   bool active{};

   friend bool operator==(const payload&, const payload&) = default;
};

const payload sample{"cross", 99, {1.25, 2.5, 3.75}, true};

template <class Fmt>
bool roundtrip_equal(const payload& in, const char* what)
{
   const auto enc = Fmt::write(in);
   if (!enc.has_value()) {
      std::printf("        %s: write returned an error\n", what);
      return false;
   }
   const auto dec = Fmt::read(enc.value());
   if (!dec.has_value()) {
      std::printf("        %s: read returned an error\n", what);
      return false;
   }
   return *dec == in;
}

struct json_fmt {
   static auto write(const payload& p) { return glz::write_json(p); }
   static auto read(const std::string& b) { return glz::read_json<payload>(b); }
};
struct jsonb_fmt {
   static auto write(const payload& p) { return glz::write_jsonb(p); }
   static auto read(const std::string& b) { return glz::read_jsonb<payload>(b); }
};
struct beve_fmt {
   static auto write(const payload& p) { return glz::write_beve(p); }
   static auto read(const std::string& b) { return glz::read_beve<payload>(b); }
};
struct cbor_fmt {
   static auto write(const payload& p) { return glz::write_cbor(p); }
   static auto read(const std::string& b) { return glz::read_cbor<payload>(b); }
};
struct bson_fmt {
   static auto write(const payload& p) { return glz::write_bson(p); }
   static auto read(const std::string& b) { return glz::read_bson<payload>(b); }
};
struct msgpack_fmt {
   static auto write(const payload& p) { return glz::write_msgpack(p); }
   static auto read(const std::string& b) { return glz::read_msgpack<payload>(b); }
};

GLZ_TEST(cross_json_roundtrip)
{
   GLZ_CHECK(roundtrip_equal<json_fmt>(sample, "json"));
}

GLZ_TEST(cross_jsonb_roundtrip)
{
   GLZ_CHECK(roundtrip_equal<jsonb_fmt>(sample, "jsonb"));
}

GLZ_TEST(cross_beve_roundtrip)
{
   GLZ_CHECK(roundtrip_equal<beve_fmt>(sample, "beve"));
}

GLZ_TEST(cross_cbor_roundtrip)
{
   GLZ_CHECK(roundtrip_equal<cbor_fmt>(sample, "cbor"));
}

GLZ_TEST(cross_bson_roundtrip)
{
   GLZ_CHECK(roundtrip_equal<bson_fmt>(sample, "bson"));
}

GLZ_TEST(cross_msgpack_roundtrip)
{
   GLZ_CHECK(roundtrip_equal<msgpack_fmt>(sample, "msgpack"));
}

GLZ_TEST(cross_all_binary_agree_on_values)
{
   // Decode every binary encoding and compare the fields against JSON's result.
   const auto jenc = glz::write_json(sample);
   GLZ_CHECK(jenc.has_value());
   const auto jdec = glz::read_json<payload>(jenc.value());
   GLZ_CHECK(jdec.has_value());

   if (jdec.has_value()) {
      const auto comparisons = {
         roundtrip_equal<jsonb_fmt>(*jdec, "jsonb"),
         roundtrip_equal<beve_fmt>(*jdec, "beve"),
         roundtrip_equal<cbor_fmt>(*jdec, "cbor"),
         roundtrip_equal<bson_fmt>(*jdec, "bson"),
         roundtrip_equal<msgpack_fmt>(*jdec, "msgpack"),
      };
      for (const bool ok : comparisons) {
         GLZ_CHECK(ok);
      }
   }
}

GLZ_TEST_MAIN("modules/cross_format_test")

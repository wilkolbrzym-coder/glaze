// Behavioural tests for the glaze.toml module.
#include "harness.hpp"

import glaze.toml;

#include <cstdint>
#include <string>

struct config {
   std::string name{};
   int port{};
};

GLZ_TEST(toml_write_contains_key_values)
{
   config c{"service", 8080};
   const auto enc = glz::write_toml(c);
   GLZ_CHECK(enc.has_value());
   if (enc.has_value()) {
      GLZ_CHECK(enc->find("name = \"service\"") != std::string::npos);
      GLZ_CHECK(enc->find("port = 8080") != std::string::npos);
   }
}

GLZ_TEST(toml_roundtrip_struct)
{
   config c{"svc", 1234};
   const auto enc = glz::write_toml(c);
   GLZ_CHECK(enc.has_value());

   config out{};
   const auto dec = glz::read_toml(out, enc.value());
   GLZ_CHECK(!static_cast<bool>(dec));
   GLZ_CHECK_EQ(out.name, std::string{"svc"});
   GLZ_CHECK_EQ(out.port, 1234);
}

GLZ_TEST(toml_bad_input_fails)
{
   config out{};
   const auto dec = glz::read_toml(out, std::string_view{"this is not = = toml"});
   GLZ_CHECK(static_cast<bool>(dec));
}

GLZ_TEST_MAIN("modules/toml_test")

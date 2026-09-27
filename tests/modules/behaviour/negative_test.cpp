// Negative / error-path tests. A test that merely "does not crash" is not
// enough: each call below must report failure, and a call that unexpectedly
// succeeds fails the test.
#include "harness.hpp"

import glaze.json;

#include <cstdint>
#include <string>
#include <vector>

struct small {
   std::uint8_t byte{};
};

struct strict {
   int a{};
   int b{};
};

GLZ_TEST(negative_parse_error_detected)
{
   std::vector<int> v{};
   const auto ec = glz::read_json(v, std::string_view{"[1,2,"});
   GLZ_CHECK(static_cast<bool>(ec));
}

GLZ_TEST(negative_unexpected_end)
{
   std::string s{};
   const auto ec = glz::read_json(s, std::string_view{R"("abc)"});
   GLZ_CHECK(static_cast<bool>(ec));
   GLZ_CHECK(ec.ec == glz::error_code::unexpected_end || ec.ec == glz::error_code::parse_error ||
             ec.ec == glz::error_code::syntax_error);
}

GLZ_TEST(negative_wrong_type_for_array)
{
   std::vector<int> v{};
   const auto ec = glz::read_json(v, std::string_view{"123"});
   GLZ_CHECK(static_cast<bool>(ec));
}

GLZ_TEST(negative_out_of_range_uint8)
{
   small s{};
   // 256 does not fit in std::uint8_t.
   const auto ec = glz::read_json(s, std::string_view{R"({"byte":256})"});
   GLZ_CHECK(static_cast<bool>(ec));
}

GLZ_TEST(negative_missing_key_with_option)
{
   strict s{};
   constexpr auto opts = glz::opts{.error_on_missing_keys = true};
   const auto ec = glz::read<opts>(s, std::string_view{R"({"a":1})"});
   GLZ_CHECK(static_cast<bool>(ec));
}

GLZ_TEST(negative_unknown_key_with_option)
{
   strict s{};
   constexpr auto opts = glz::opts{.error_on_unknown_keys = true};
   const auto ec = glz::read<opts>(s, std::string_view{R"({"a":1,"b":2,"c":3})"});
   GLZ_CHECK(static_cast<bool>(ec));
}

GLZ_TEST(negative_success_path_still_works)
{
   // Guard against a harness that passes everything: this one *must* succeed.
   strict s{};
   const auto ec = glz::read_json(s, std::string_view{R"({"a":1,"b":2})"});
   GLZ_CHECK(!static_cast<bool>(ec));
   GLZ_CHECK_EQ(s.a, 1);
   GLZ_CHECK_EQ(s.b, 2);
}

GLZ_TEST_MAIN("modules/negative_test")

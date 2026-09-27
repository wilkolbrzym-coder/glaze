// Behavioural round-trip tests for the glaze.json module.
//
// Compiled with: import glaze.json;  (no glaze headers)
#include "harness.hpp"

import glaze.json;

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct person {
   std::string name{};
   int age{};
};

struct nested {
   person who{};
   std::vector<int> scores{};
   std::optional<std::string> nickname{};
};

struct keyed_choice {
   std::variant<int, std::string> value{};
};

GLZ_TEST(json_object_exact_output)
{
   person p{"Alice", 30};
   const auto out = glz::write_json(p);
   GLZ_CHECK(out.has_value());
   GLZ_CHECK_EQ(out.value(), std::string{R"({"name":"Alice","age":30})"});
}

GLZ_TEST(json_roundtrip_struct)
{
   nested n{{"Bob", 41}, {1, 2, 3}, std::nullopt};
   const auto enc = glz::write_json(n);
   GLZ_CHECK(enc.has_value());
   // Default opts.skip_null_members is true, so the empty optional is omitted.
   GLZ_CHECK_EQ(enc.value(), std::string{R"({"who":{"name":"Bob","age":41},"scores":[1,2,3]})"});

   const auto dec = glz::read_json<nested>(enc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(dec->who.name, std::string{"Bob"});
   GLZ_CHECK_EQ(dec->who.age, 41);
   GLZ_CHECK_EQ(dec->scores.size(), std::size_t{3});
   GLZ_CHECK(!dec->nickname.has_value());
}

GLZ_TEST(json_vector_and_map)
{
   std::vector<int> v{5, 6, 7};
   const auto enc = glz::write_json(v);
   GLZ_CHECK(enc.has_value());
   GLZ_CHECK_EQ(enc.value(), std::string{"[5,6,7]"});

   std::map<std::string, int> m{{"a", 1}, {"b", 2}};
   const auto menc = glz::write_json(m);
   GLZ_CHECK(menc.has_value());
   GLZ_CHECK_EQ(menc.value(), std::string{R"({"a":1,"b":2})"});

   const auto dec = glz::read_json<std::map<std::string, int>>(menc.value());
   GLZ_CHECK(dec.has_value());
   GLZ_CHECK_EQ(dec->at("b"), 2);
}

GLZ_TEST(json_boolean_and_null)
{
   bool t = true;
   auto e = glz::write_json(t);
   GLZ_CHECK(e.has_value());
   GLZ_CHECK_EQ(e.value(), std::string{"true"});

   std::optional<int> n = std::nullopt;
   auto ne = glz::write_json(n);
   GLZ_CHECK(ne.has_value());
   GLZ_CHECK_EQ(ne.value(), std::string{"null"});
}

GLZ_TEST(json_parse_malformed_is_parse_error)
{
   person p{};
   const auto err = glz::read_json(p, std::string_view{R"({"name":"x",})"});
   GLZ_CHECK(static_cast<bool>(err)); // error_ctx truthy on error
   GLZ_CHECK(err.ec != glz::error_code::none);
}

GLZ_TEST(json_parse_truncated_fails)
{
   std::vector<int> v{};
   const auto err = glz::read_json(v, std::string_view{"[1,2"});
   GLZ_CHECK(static_cast<bool>(err));
   GLZ_CHECK(static_cast<bool>(err.ec != glz::error_code::none));
}

GLZ_TEST(json_parse_wrong_type_fails)
{
   std::vector<int> v{};
   const auto err = glz::read_json(v, std::string_view{"{}"});
   GLZ_CHECK(static_cast<bool>(err));
   GLZ_CHECK(static_cast<bool>(err.ec != glz::error_code::none));
}

GLZ_TEST(json_variant_string_roundtrip)
{
   keyed_choice k{std::string{"hi"}};
   auto e = glz::write_json(k);
   GLZ_CHECK(e.has_value());
   auto d = glz::read_json<keyed_choice>(e.value());
   GLZ_CHECK(d.has_value());
   GLZ_CHECK(std::holds_alternative<std::string>(d->value));
   GLZ_CHECK_EQ(std::get<std::string>(d->value), std::string{"hi"});
}

GLZ_TEST_MAIN("modules/json_test")

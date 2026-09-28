// Behavioural + compile-time tests for directly callable core/util modules.
//
// These modules precompile cleanly on this branch, so this file is expected to
// build and run; it proves the module API is usable and that the harness exits
// non-zero on failure.
#include "harness.hpp"

import glaze.core.context;
import glaze.core.opts;
import glaze.containers.flat_map;
import glaze.util.compare;
import glaze.util.expected;
import glaze.util.type_traits;

#include <cstdint>
#include <string>
#include <vector>

// --- compile-time assertions (requirement: violations must be compile errors) -
static_assert(glz::is_specialization_v<std::vector<int>, std::vector>);
static_assert(!glz::is_specialization_v<int, std::vector>);
static_assert(!glz::false_v<int>);
static_assert(static_cast<std::uint32_t>(glz::error_code::none) == 0u);
static_assert(glz::opts{}.format == 10u); // glz::JSON == 10 (glaze/forward.hpp)
static_assert(glz::opts{}.prettify == false);
static_assert(glz::opts{}.error_on_unknown_keys == true);

GLZ_TEST(expected_moves_value_and_error)
{
   glz::expected<int, int> ok{42};
   GLZ_CHECK(ok.has_value());
   GLZ_CHECK_EQ(ok.value(), 42);

   glz::expected<int, int> bad{glz::unexpected{7}};
   GLZ_CHECK(!bad.has_value());
   GLZ_CHECK_EQ(bad.error(), 7);
}

GLZ_TEST(error_ctx_semantics)
{
   const glz::error_ctx none{};
   GLZ_CHECK(!static_cast<bool>(none));
   GLZ_CHECK(none.ec == glz::error_code::none);

   const glz::error_ctx bad{3, glz::error_code::parse_error};
   GLZ_CHECK(static_cast<bool>(bad));
   GLZ_CHECK(bad.ec == glz::error_code::parse_error);
   GLZ_CHECK_EQ(bad.count, std::size_t{3});
}

// NOTE: include/glaze/core/context.hpp exports glz::parse_failed(...), but the
// module unit modules/glaze/core/context.ixx does not declare or export it.
// A module-vs-header API divergence is therefore recorded rather than asserted
// here (referencing the missing symbol would be a compile error, not a test).

GLZ_TEST(compare_exact_widths)
{
   GLZ_CHECK(glz::compare<3>("abc", "abc"));
   GLZ_CHECK(!glz::compare<3>("abc", "abd"));
   GLZ_CHECK(glz::compare<4>("abcd", "abcd"));
   GLZ_CHECK(!glz::compare<4>("abcd", "abce"));
   GLZ_CHECK(glz::compare<8>("abcdefgh", "abcdefgh"));
   GLZ_CHECK(!glz::compare<8>("abcdefgh", "abcdefgi"));
}

GLZ_TEST(flat_map_insert_lookup)
{
   glz::flat_map<std::string, int> m{};
   m["a"] = 1;
   m["b"] = 2;
   GLZ_CHECK_EQ(m.size(), std::size_t{2});
   GLZ_CHECK_EQ(m.at("b"), 2);
   GLZ_CHECK_EQ(m.at("a"), 1);
}

GLZ_TEST_MAIN("modules/core_test")

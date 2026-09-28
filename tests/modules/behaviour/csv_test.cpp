// Behavioural tests for the glaze.csv module.
#include "harness.hpp"

import glaze.csv;
import glaze.core.opts; // for glz::colwise

#include <string>
#include <vector>

struct row {
   std::string name{};
   int age{};
};

GLZ_TEST(csv_write_header_and_rows)
{
   std::vector<row> rows{{"alice", 30}, {"bob", 41}};
   const auto enc = glz::write_csv(rows);
   GLZ_CHECK(enc.has_value());
   if (enc.has_value()) {
      GLZ_CHECK(enc->find("name") != std::string::npos);
      GLZ_CHECK(enc->find("age") != std::string::npos);
      GLZ_CHECK(enc->find("alice") != std::string::npos);
      GLZ_CHECK(enc->find("bob") != std::string::npos);
   }
}

GLZ_TEST(csv_roundtrip_rows)
{
   std::vector<row> rows{{"carol", 22}, {"dave", 55}};
   const auto enc = glz::write_csv(rows);
   GLZ_CHECK(enc.has_value());

   // Reading a vector of structs is the colwise layout; rowwise reads into a
   // struct of parallel columns instead.
   std::vector<row> out{};
   const auto ec = glz::read_csv<glz::colwise>(out, enc.value());
   GLZ_CHECK(!static_cast<bool>(ec));
   GLZ_CHECK_EQ(out.size(), std::size_t{2});
   if (out.size() == 2) {
      GLZ_CHECK_EQ(out[0].name, std::string{"carol"});
      GLZ_CHECK_EQ(out[1].age, 55);
   }
}

GLZ_TEST(csv_truncated_row_is_error)
{
   std::vector<row> out{};
   // Header promises two columns, row supplies one.
   const auto ec = glz::read_csv<glz::colwise>(out, std::string_view{"name,age\nalice\n"});
   GLZ_CHECK(static_cast<bool>(ec));
}

GLZ_TEST_MAIN("modules/csv_test")

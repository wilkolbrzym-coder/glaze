// YAML behavioural test.
//
// IMPORTANT: on this branch there is NO `glaze.yaml` module unit under
// modules/ -- modules/glaze/yaml.hpp and modules/glaze/yaml/*.hpp exist only
// as plain headers. YAML therefore cannot be exercised through `import`; this
// translation unit exercises it through the header path instead and is clearly
// labelled as such in the runner output. It is still a real, exact round-trip
// test of the YAML subsystem that the module conversion must not break.
#include "harness.hpp"

#include "glaze/yaml.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct yaml_cfg {
   std::string name{};
   int port{};
   std::vector<int> ids{};
};

GLZ_TEST(yaml_roundtrip_struct)
{
   yaml_cfg c{"svc", 4321, {1, 2, 3}};
   const auto enc = glz::write_yaml(c);
   GLZ_CHECK(enc.has_value());

   yaml_cfg out{};
   const auto ec = glz::read_yaml(out, enc.value());
   GLZ_CHECK(!static_cast<bool>(ec));
   GLZ_CHECK_EQ(out.name, std::string{"svc"});
   GLZ_CHECK_EQ(out.port, 4321);
   GLZ_CHECK_EQ(out.ids.size(), std::size_t{3});
}

GLZ_TEST(yaml_scalar_roundtrip)
{
   const auto enc = glz::write_yaml(std::int32_t{17});
   GLZ_CHECK(enc.has_value());
   std::int32_t v{};
   const auto ec = glz::read_yaml(v, enc.value());
   GLZ_CHECK(!static_cast<bool>(ec));
   GLZ_CHECK_EQ(v, std::int32_t{17});
}

GLZ_TEST(yaml_bad_indentation_fails)
{
   yaml_cfg out{};
   const auto ec = glz::read_yaml(out, std::string_view{"name: x\n   port: not-a-number\n"});
   GLZ_CHECK(static_cast<bool>(ec));
}

GLZ_TEST_MAIN("modules/yaml_header_test")

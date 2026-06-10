// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/json.hpp"
#include "glaze/cbor.hpp"
#include "glaze/beve.hpp"
#include "glaze/yaml.hpp"
#include "glaze/toml.hpp"
#include "glaze/msgpack.hpp"
#include "glaze/bson.hpp"

#include <thread>
#include "ut/ut.hpp"

using namespace ut;

struct valid_struct {
   int x{42};
   std::string y{"hello"};
};

struct unsupported_struct {
private:
   [[maybe_unused]] int x;
};


suite diagnostics_tests = [] {
   "valid_struct_supported"_test = [] {
      // Test writing support concepts
      static_assert(glz::write_supported<valid_struct, glz::JSON>);
      static_assert(glz::write_supported<valid_struct, glz::CBOR>);
      static_assert(glz::write_supported<valid_struct, glz::BEVE>);
      static_assert(glz::write_supported<valid_struct, glz::YAML>);
      static_assert(glz::write_supported<valid_struct, glz::TOML>);
      static_assert(glz::write_supported<valid_struct, glz::MSGPACK>);
      static_assert(glz::write_supported<valid_struct, glz::BSON>);

      // Test reading support concepts
      static_assert(glz::read_supported<valid_struct, glz::JSON>);
      static_assert(glz::read_supported<valid_struct, glz::CBOR>);
      static_assert(glz::read_supported<valid_struct, glz::BEVE>);
      static_assert(glz::read_supported<valid_struct, glz::YAML>);
      static_assert(glz::read_supported<valid_struct, glz::TOML>);
      static_assert(glz::read_supported<valid_struct, glz::MSGPACK>);
      static_assert(glz::read_supported<valid_struct, glz::BSON>);
   };

   "invalid_struct_unsupported"_test = [] {
      // Test writing support concepts should be false for unsupported types
      static_assert(!glz::write_supported<unsupported_struct, glz::JSON>);
      static_assert(!glz::write_supported<unsupported_struct, glz::CBOR>);
      static_assert(!glz::write_supported<unsupported_struct, glz::BEVE>);
      static_assert(!glz::write_supported<unsupported_struct, glz::YAML>);
      static_assert(!glz::write_supported<unsupported_struct, glz::TOML>);
      static_assert(!glz::write_supported<unsupported_struct, glz::MSGPACK>);
      static_assert(!glz::write_supported<unsupported_struct, glz::BSON>);

      // Test reading support concepts should be false for unsupported types
      static_assert(!glz::read_supported<unsupported_struct, glz::JSON>);
      static_assert(!glz::read_supported<unsupported_struct, glz::CBOR>);
      static_assert(!glz::read_supported<unsupported_struct, glz::BEVE>);
      static_assert(!glz::read_supported<unsupported_struct, glz::YAML>);
      static_assert(!glz::read_supported<unsupported_struct, glz::TOML>);
      static_assert(!glz::read_supported<unsupported_struct, glz::MSGPACK>);
      static_assert(!glz::read_supported<unsupported_struct, glz::BSON>);
   };

   "roundtrips"_test = [] {
      valid_struct obj{100, "world"};
      
      // JSON
      {
         std::string buf;
         expect(!glz::write_json(obj, buf));
         valid_struct res;
         expect(!glz::read_json(res, buf));
         expect(res.x == 100);
         expect(res.y == "world");
      }

      // CBOR
      {
         std::vector<std::byte> buf;
         expect(!glz::write_cbor(obj, buf));
         valid_struct res;
         expect(!glz::read_cbor(res, buf));
         expect(res.x == 100);
         expect(res.y == "world");
      }

      // BEVE
      {
         std::vector<std::byte> buf;
         expect(!glz::write_beve(obj, buf));
         valid_struct res;
         expect(!glz::read_beve(res, buf));
         expect(res.x == 100);
         expect(res.y == "world");
      }

      // YAML
      {
         std::string buf;
         expect(!glz::write_yaml(obj, buf));
         valid_struct res;
         expect(!glz::read_yaml(res, buf));
         expect(res.x == 100);
         expect(res.y == "world");
      }

      // TOML
      {
         std::string buf;
         expect(!glz::write_toml(obj, buf));
         valid_struct res;
         expect(!glz::read_toml(res, buf));
         expect(res.x == 100);
         expect(res.y == "world");
      }

      // MSGPACK
      {
         std::vector<std::byte> buf;
         expect(!glz::write_msgpack(obj, buf));
         valid_struct res;
         expect(!glz::read_msgpack(res, buf));
         expect(res.x == 100);
         expect(res.y == "world");
      }

      // BSON
      {
         std::vector<std::byte> buf;
         expect(!glz::write_bson(obj, buf));
         valid_struct res;
         expect(!glz::read_bson(res, buf));
         expect(res.x == 100);
         expect(res.y == "world");
      }
   };
};

int main() { return 0; }

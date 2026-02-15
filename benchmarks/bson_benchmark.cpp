#include <string>
#include <vector>
#include "bencher/bencher.hpp"
#include "bencher/diagnostics.hpp"
#include "glaze/glaze.hpp"

struct BenchUser
{
   int64_t id{};
   std::string name{};
   std::string email{};
   int64_t age{};
   bool active{};
   int64_t score{};
};

inline std::vector<BenchUser> generate_users(size_t count)
{
   std::vector<BenchUser> users;
   users.reserve(count);
   for (size_t i = 0; i < count; ++i) {
      users.push_back({
         static_cast<int64_t>(i),
         "User " + std::to_string(i),
         "user" + std::to_string(i) + "@test.com",
         static_cast<int64_t>(20 + (i % 50)),
         (i % 2 == 0),
         static_cast<int64_t>(i * 10)
      });
   }
   return users;
}

int main()
{
   const auto users = generate_users(1000);
   std::string json_buf, bson_buf, beve_buf;

   (void)glz::write_json(users, json_buf);
   (void)glz::write_bson(users, bson_buf);
   (void)glz::write_beve(users, beve_buf);

   {
      bencher::stage stage;
      stage.name = "Writing (1000 users)";
      stage.run("JSON", [&] {
         json_buf.clear();
         (void)glz::write_json(users, json_buf);
         return json_buf.size();
      });
      stage.run("BSON", [&] {
         bson_buf.clear();
         (void)glz::write_bson(users, bson_buf);
         return bson_buf.size();
      });
      stage.run("BEVE", [&] {
         beve_buf.clear();
         (void)glz::write_beve(users, beve_buf);
         return beve_buf.size();
      });
      bencher::print_results(stage);
   }

   {
      bencher::stage stage;
      stage.name = "Reading (1000 users)";
      std::vector<BenchUser> users2;
      stage.run("JSON", [&] {
         users2.clear();
         (void)glz::read_json(users2, json_buf);
         return json_buf.size();
      });
      stage.run("BSON", [&] {
         users2.clear();
         (void)glz::read_bson(users2, bson_buf);
         return bson_buf.size();
      });
      stage.run("BEVE", [&] {
         users2.clear();
         (void)glz::read_beve(users2, beve_buf);
         return beve_buf.size();
      });
      bencher::print_results(stage);
   }

   return 0;
}

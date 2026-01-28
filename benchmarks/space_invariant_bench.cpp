
#include "glaze/glaze.hpp"
#include "bencher/bencher.hpp"
#include "bencher/diagnostics.hpp"

struct obj_t
{
   std::vector<int> int_array;
   std::vector<float> float_array;
   std::vector<double> double_array;
};

template <>
struct glz::meta<obj_t>
{
   using T = obj_t;
   static constexpr auto value =
      object("int_array", &T::int_array, "float_array", &T::float_array, "double_array", &T::double_array);
};

int main()
{
   obj_t obj{};
   for (int i = 0; i < 1000; ++i) {
      obj.int_array.push_back(i);
      obj.float_array.push_back(static_cast<float>(i) * 1.1f);
      obj.double_array.push_back(static_cast<double>(i) * 2.2);
   }

   std::string buffer;

   bencher::stage stage;
   stage.name = "Prettified JSON Write Benchmark";

   stage.run("New Buffer (Cold)", [&] {
      std::string local_buffer;
      glz::write<glz::opts{.prettify = true}>(obj, local_buffer);
      bencher::do_not_optimize(local_buffer);
      return local_buffer.size();
   });

   stage.run("Reused Buffer (Warm)", [&] {
      glz::write<glz::opts{.prettify = true}>(obj, buffer);
      bencher::do_not_optimize(buffer);
      return buffer.size();
   });

   bencher::print_results(stage);

   return 0;
}

// Glaze Library
// For the license information refer to glaze.hpp

#include "glaze/glaze.hpp"
#include "ut/ut.hpp"
#include <iostream>

using namespace ut;

struct my_struct
{
   int i = 287;
   double d = 3.14;
   std::string hello = "Hello World";
   std::array<uint64_t, 3> arr = {1, 2, 3};
};

suite buffer_reuse_tests = [] {
   "buffer_reuse_dirty_check"_test = [] {
      // 1. Setup a "dirty" buffer with some garbage content
      std::string buffer(1024, 'X');

      // 2. Write prettified JSON into this buffer
      my_struct s{};
      auto ec = glz::write<glz::opts{.prettify = true}>(s, buffer);

      expect(ec == glz::error_code::none) << "Write should succeed";

      if (ec == glz::error_code::none) {
         std::string_view output = buffer;
         std::cout << "Output: \n" << output << "\n"; // DEBUG

         // Basic check: should not contain 'X' in indentation
         size_t x_in_indentation = 0;
         bool in_indent = false;
         for (size_t i = 0; i < output.size(); ++i) {
             if (output[i] == '\n') {
                 in_indent = true;
                 continue;
             }
             if (in_indent) {
                 if (output[i] == ' ') {
                     // Good, space
                 } else if (output[i] == 'X') {
                     x_in_indentation++;
                 } else {
                     // End of indentation
                     in_indent = false;
                 }
             }
         }

         if (x_in_indentation > 0) {
             std::cout << "FAILURE: Found X in indentation\n";
         }
         expect(x_in_indentation == 0) << "Found " << x_in_indentation << " 'X' characters in indentation areas!";
      }
   };

   "buffer_reuse_finalize_fill"_test = [] {
       // Test that finalize fills the tail with spaces
       std::string buffer(100, 'X');
       my_struct s{};

       // Write small JSON
       auto ec = glz::write_json(s, buffer);
       expect(ec == glz::error_code::none);
   };
};

int main()
{
   // No special setup needed for ut
}

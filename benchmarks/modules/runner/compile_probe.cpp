// Headline compile-time probe: the umbrella surface.
//   header path:  #include "glaze/glaze.hpp"
//   module path:  import glaze;
// The body forces instantiation of the JSON read/write templates so the two
// compilers do comparable work rather than parsing an empty TU.
#include "path_umbrella.hpp"

#include <string>

struct probe_point
{
   double x{};
   double y{};
   std::string label;
};

int main()
{
   probe_point p{1.0, 2.0, "probe"};
   std::string buffer;
   glz::write_json(p, buffer);
   probe_point q{};
   glz::read_json(q, buffer);
   return (q.x == 1.0 && q.y == 2.0 && q.label == "probe") ? 0 : 1;
}

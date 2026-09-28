// Minimal, strict test harness for the Glaze C++20 module behavioural tests.
//
// Design goals (per the mod-tests brief):
//   * every test has a printed name and a PASS/FAIL verdict;
//   * a test that runs zero assertions is a FAILURE, not a pass;
//   * the process exits non-zero if anything failed or if no test ran.
//
// This header is compiled into each behavioural translation unit *after* the
// `import` declarations; it deliberately includes nothing from Glaze and does
// not use <print> so it also works on the header-only path.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace glztest {

struct test_case {
   const char* name;
   void (*fn)();
};

inline std::vector<test_case>& cases()
{
   static std::vector<test_case> c;
   return c;
}

inline long& total_checks() { static long n = 0; return n; }
inline long& total_failures() { static long n = 0; return n; }

struct registrar {
   registrar(const char* name, void (*fn)()) { cases().push_back({name, fn}); }
};

// --- value formatting for assertion diagnostics -----------------------------
inline std::string describe(const std::string& s) { return '"' + s + '"'; }
inline std::string describe(std::string_view s) { return '"' + std::string(s) + '"'; }
inline std::string describe(const char* s) { return std::string("\"") + (s ? s : "<null>") + '"'; }
inline std::string describe(bool b) { return b ? "true" : "false"; }

template <class T>
std::string describe(const T& v)
{
   if constexpr (std::is_arithmetic_v<T>) {
      return std::to_string(v);
   }
   else {
      (void)v;
      return "<value>";
   }
}

inline void check(bool ok, const char* expr, const char* file, int line)
{
   ++total_checks();
   if (!ok) {
      ++total_failures();
      std::printf("        CHECK FAILED: %s\n            at %s:%d\n", expr, file, line);
   }
}

template <class A, class B>
void check_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line)
{
   ++total_checks();
   if (!(a == b)) {
      ++total_failures();
      std::printf("        CHECK FAILED: %s == %s\n", ea, eb);
      std::printf("            %s = %s\n", ea, describe(a).c_str());
      std::printf("            %s = %s\n", eb, describe(b).c_str());
      std::printf("            at %s:%d\n", file, line);
   }
}

inline int run_all(const char* suite)
{
   std::printf("== %s ==\n", suite);
   std::size_t ran = 0;
   for (auto& c : cases()) {
      const long before_fail = total_failures();
      const long before_checks = total_checks();
      std::printf("[ RUN  ] %s\n", c.name);
      c.fn();
      const long checks = total_checks() - before_checks;
      const long failed = total_failures() - before_fail;
      ++ran;
      if (checks == 0) {
         ++total_failures();
         std::printf("[FAIL ] %s (no assertions executed)\n", c.name);
      }
      else if (failed != 0) {
         std::printf("[FAIL ] %s (%ld/%ld checks failed)\n", c.name, failed, checks);
      }
      else {
         std::printf("[PASS ] %s (%ld checks)\n", c.name, checks);
      }
   }
   std::printf("-- %s: %zu tests, %ld checks, %ld failures\n", suite, ran, total_checks(),
               total_failures());
   if (ran == 0) {
      std::printf("-- ERROR: no tests were registered\n");
      return 2;
   }
   return total_failures() == 0 ? 0 : 1;
}

} // namespace glztest

#define GLZ_TEST(name)                                                            \
   static void glz_test_fn_##name();                                              \
   static ::glztest::registrar glz_test_reg_##name(#name, &glz_test_fn_##name);   \
   static void glz_test_fn_##name()

#define GLZ_CHECK(expr) ::glztest::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define GLZ_CHECK_EQ(a, b) ::glztest::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define GLZ_TEST_MAIN(suite) int main() { return ::glztest::run_all(suite); }

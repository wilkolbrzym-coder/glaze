// Shared, dependency-free measurement core for the SIMD benchmarks.
//
// Included by both simd_bench.cpp (header path) and simd_bench_module.cpp (module path),
// so the two produce numbers with exactly the same methodology and can be compared.
//
// No third-party timing library: only <chrono>. Every number is the median of `reps`
// timed runs, each run repeating the whole workload `inner` times. min/max are the
// observed extremes of those runs and are the honest spread, not a confidence interval.

#pragma once

// The module translation unit does `import std;` before including this file — mixing an
// `import std;` with textual standard headers from a different standard library
// implementation is a hard error. BENCH_IMPORT_STD says the std names are already there.
#ifndef BENCH_IMPORT_STD
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#endif

namespace bench
{
   using clock_type = std::chrono::steady_clock;

   struct kernel
   {
      std::string name;
      std::string backend;
      std::size_t bytes; // bytes of input processed by one call
      std::uint64_t (*fn)();
   };

   struct result
   {
      double median_mbps;
      double min_mbps;
      double max_mbps;
      std::uint64_t checksum;
      int reps;
      int inner;
   };

   inline int env_int(const char* name, int fallback)
   {
      if (const char* v = std::getenv(name)) {
         const int n = std::atoi(v);
         if (n > 0) return n;
      }
      return fallback;
   }

   // Runs `fn` `inner` times per timed rep, `reps` reps, and returns the median throughput.
   inline result measure(const kernel& k, int reps, int inner)
   {
      std::vector<double> samples;
      samples.reserve(std::size_t(reps));
      std::uint64_t checksum = 0;

      // Warm up once (also gives us the checksum). Sum, not xor: a sum is sensitive to
      // every call's value, so a kernel that silently does less work in one build shows
      // up as a differing checksum rather than cancelling out.
      for (int i = 0; i < 2; ++i) checksum += k.fn();

      for (int r = 0; r < reps; ++r) {
         const auto t0 = clock_type::now();
         for (int i = 0; i < inner; ++i) checksum += k.fn();
         const auto t1 = clock_type::now();
         const double seconds = std::chrono::duration<double>(t1 - t0).count();
         const double mb = double(k.bytes) * double(inner) / 1.0e6;
         samples.push_back(mb / seconds);
      }

      std::sort(samples.begin(), samples.end());
      const double median = samples[samples.size() / 2];
      return {median, samples.front(), samples.back(), checksum, reps, inner};
   }

   inline void print_result(std::string_view build, std::string_view flags, const kernel& k, const result& r,
                            bool as_csv)
   {
      if (as_csv) {
         std::printf("%s,%s,%s,%s,%.1f,%.1f,%.1f,%zu,%llu,%d,%d\n", std::string(build).c_str(),
                     std::string(flags).c_str(), k.name.c_str(), k.backend.c_str(), r.median_mbps,
                     r.min_mbps, r.max_mbps, k.bytes, static_cast<unsigned long long>(r.checksum), r.reps,
                     r.inner);
      }
      else {
         std::printf("%-26s %-10s %10.1f MB/s  [%10.1f .. %10.1f]  chk=%016llx\n", k.name.c_str(),
                     k.backend.c_str(), r.median_mbps, r.min_mbps, r.max_mbps,
                     static_cast<unsigned long long>(r.checksum));
      }
      std::fflush(nullptr);
   }

   inline int bench_entry(std::string_view build, std::string_view flags, std::vector<kernel> kernels)
   {
      const int reps = env_int("BENCH_REPS", 11);
      const int inner = env_int("BENCH_INNER", 3);
      const bool as_csv = std::getenv("BENCH_CSV") != nullptr;

      if (as_csv) {
         std::printf("build,flags,kernel,backend,median_MBps,min_MBps,max_MBps,bytes,checksum,reps,inner\n");
      }
      else {
         std::printf("=== build=%s flags=%s reps=%d inner=%d ===\n", std::string(build).c_str(),
                     std::string(flags).c_str(), reps, inner);
      }

      for (const auto& k : kernels) {
         if (k.fn == nullptr) {
            if (!as_csv) std::printf("%-26s %-10s SKIPPED (backend not compiled)\n", k.name.c_str(),
                                     k.backend.c_str());
            continue;
         }
         const result r = measure(k, reps, inner);
         print_result(build, flags, k, r, as_csv);
      }
      return 0;
   }

   // ---------------------------------------------------------------------------
   // Deterministic input generation. Fixed seed, so the header and module builds
   // run on byte-identical inputs and their checksums are comparable.
   // ---------------------------------------------------------------------------

   struct rng
   {
      std::uint64_t s;
      explicit rng(std::uint64_t seed) : s(seed) {}
      std::uint32_t next()
      {
         s = s * 6364136223846793005ull + 1442695040888963407ull;
         return std::uint32_t(s >> 33);
      }
      std::uint32_t below(std::uint32_t n) { return next() % n; }
   };

   inline std::string make_ascii(std::size_t n)
   {
      rng r(1);
      std::string s;
      s.reserve(n);
      constexpr char abc[] = "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,";
      while (s.size() < n) s.push_back(abc[r.below(sizeof(abc) - 1)]);
      return s;
   }

   inline std::string make_mixed_utf8(std::size_t n)
   {
      rng r(2);
      std::string s;
      s.reserve(n + 8);
      while (s.size() < n) {
         switch (r.below(6)) {
         case 0:
            s.append("\xC3\xA9"); // e-acute, 2 bytes
            break;
         case 1:
            s.append("\xE2\x82\xAC"); // euro sign, 3 bytes
            break;
         case 2:
            s.append("\xF0\x9F\x98\x80"); // emoji, 4 bytes
            break;
         default:
            s.push_back(char('a' + r.below(26)));
            break;
         }
      }
      return s;
   }

   inline std::string make_escapes(std::size_t n, unsigned escape_percent)
   {
      rng r(3);
      std::string s;
      s.reserve(n);
      constexpr char abc[] = "abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
      while (s.size() < n) {
         if (r.below(100) < escape_percent) {
            const std::uint32_t pick = r.below(3);
            if (pick == 0) s.push_back('"');
            else if (pick == 1) s.push_back('\\');
            else s.push_back(char(0x01 + r.below(0x1F)));
         }
         else {
            s.push_back(abc[r.below(sizeof(abc) - 1)]);
         }
      }
      return s;
   }

   inline std::string make_json_like(std::size_t n)
   {
      rng r(4);
      std::string s;
      s.reserve(n);
      while (s.size() < n) {
         const std::uint32_t pick = r.below(12);
         if (pick < 5) s.push_back(char('a' + r.below(26)));
         else if (pick == 5) s.push_back('"');
         else if (pick == 6) s.push_back('{');
         else if (pick == 7) s.push_back('}');
         else if (pick == 8) s.push_back('[');
         else if (pick == 9) s.push_back(']');
         else if (pick == 10) s.push_back(',');
         else s.push_back(' ');
      }
      return s;
   }

   inline std::string make_numbers(std::size_t count)
   {
      rng r(5);
      std::string s;
      s.reserve(count * 8);
      char buf[32];
      for (std::size_t i = 0; i < count; ++i) {
         const int ip = int(r.below(1000000));
         const int fp = int(r.below(1000));
         const int len = std::snprintf(buf, sizeof(buf), "%d.%03d ", ip, fp);
         s.append(buf, std::size_t(len));
      }
      return s;
   }

   inline std::vector<double> make_doubles(std::size_t count)
   {
      rng r(6);
      std::vector<double> v;
      v.reserve(count);
      for (std::size_t i = 0; i < count; ++i) {
         const double frac = double(r.below(1000000)) / 1000.0;
         v.push_back(frac * (r.next() & 1 ? 1.0 : -1.0));
      }
      return v;
   }

   inline std::string make_ws_runs(std::size_t n, std::size_t run)
   {
      rng r(7);
      std::string s;
      s.reserve(n);
      while (s.size() < n) {
         for (std::size_t i = 0; i < 56 && s.size() < n; ++i) s.push_back(char('a' + r.below(26)));
         for (std::size_t i = 0; i < run && s.size() < n; ++i) s.push_back(' ');
      }
      return s;
   }
}

// Runtime benchmark for the Glaze modules conversion.
//
// The exact same source is compiled against two paths (see path_formats.hpp):
//   header path:  #include "glaze/{json,beve,cbor,toml}.hpp"
//   module path:  import glaze.{json,beve,cbor,toml};
//
// For every workload it prints an FNV-1a hash of the produced bytes so the two
// paths can be proven byte-identical, and (in --dump mode) writes the raw
// serialized output so the driver can `cmp` them.
//
// Output lines are machine-readable:
//   RT format=<f> op=<read|write> iters=<n> total_ns=<ns> ns_per_op=<x> mb_per_s=<y> bytes=<b> checksum=<hex>
#include "path_formats.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

// Wall clock AND process CPU clock. On a machine with other work running, wall
// throughput is load-dominated; CPU time per op is the reproducible number.
static double now_cpu_ns()
{
   return static_cast<double>(std::clock()) * (1.0e9 / static_cast<double>(CLOCKS_PER_SEC));
}

using namespace std::chrono;

struct inner
{
   std::string tag;
   std::vector<int32_t> data;
   double weight{};
};

struct record
{
   int64_t id{};
   std::string name;
   bool active{};
   double score{};
   std::vector<inner> items;
   std::vector<std::string> labels;
   std::vector<int32_t> counters;
};

struct toml_doc
{
   int32_t version{};
   double ratio{};
   std::string title;
   std::vector<int32_t> samples;
   bool enabled{};
};

static record make_record(std::size_t n)
{
   record r;
   r.id = 987654321;
   r.name = "glaze modules benchmark payload";
   r.active = true;
   r.score = 3.141592653589793;
   r.items.reserve(n);
   for (std::size_t i = 0; i < n; ++i) {
      inner it;
      it.tag = "item_" + std::to_string(i);
      it.weight = static_cast<double>(i) * 0.5;
      it.data.resize(8);
      for (std::size_t k = 0; k < it.data.size(); ++k) {
         it.data[k] = static_cast<int32_t>((i * 31 + k * 7) % 100003) - 50000;
      }
      r.items.push_back(std::move(it));
   }
   for (int i = 0; i < 16; ++i) {
      r.labels.push_back("label_" + std::to_string(i));
      r.counters.push_back(i * i - 3);
   }
   return r;
}

static toml_doc make_toml(std::size_t n)
{
   toml_doc d;
   d.version = 7;
   d.ratio = 2.718281828459045;
   d.title = "glaze toml";
   d.enabled = true;
   d.samples.reserve(n);
   for (std::size_t i = 0; i < n; ++i) {
      d.samples.push_back(static_cast<int32_t>(i * 3) - 17);
   }
   return d;
}

static uint64_t fnv1a(const void* p, std::size_t n, uint64_t h = 1469598103934665603ull)
{
   const auto* b = static_cast<const unsigned char*>(p);
   for (std::size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ull;
   }
   return h;
}

static uint64_t record_sum(const record& r)
{
   uint64_t h = 0;
   h = fnv1a(r.name.data(), r.name.size(), h);
   h ^= static_cast<uint64_t>(r.id);
   h ^= static_cast<uint64_t>(r.score * 1e6) & 0xffffff;
   h += static_cast<uint64_t>(r.active);
   for (const auto& it : r.items) {
      h = fnv1a(it.tag.data(), it.tag.size(), h);
      for (int32_t v : it.data) h = h * 131 + static_cast<uint64_t>(static_cast<int64_t>(v) + 50000);
      h = h * 131 + static_cast<uint64_t>(it.weight);
   }
   for (const auto& s : r.labels) h = fnv1a(s.data(), s.size(), h);
   for (int32_t v : r.counters) h = h * 131 + static_cast<uint64_t>(static_cast<int64_t>(v) + 50000);
   return h;
}

static uint64_t toml_sum(const toml_doc& d)
{
   uint64_t h = fnv1a(d.title.data(), d.title.size());
   h ^= static_cast<uint64_t>(d.version);
   h = h * 131 + static_cast<uint64_t>(d.ratio * 1e6);
   h += static_cast<uint64_t>(d.enabled);
   for (int32_t v : d.samples) h = h * 131 + static_cast<uint64_t>(static_cast<int64_t>(v) + 50000);
   return h;
}

template <class F>
struct timed
{
   size_t iters;
   double total_ns;
   double cpu_ns;
   uint64_t checksum;
};

// Runs `op` repeatedly until at least `target_ns` of WALL time have elapsed
// (>= 3 iters), recording both wall and process-CPU time for the loop.
template <class F>
static timed<F> time_op(F&& op, double target_ns = 2.0e8)
{
   uint64_t checksum = op(); // warm-up, also validates it works
   auto t0 = steady_clock::now();
   double c0 = now_cpu_ns();
   size_t iters = 0;
   checksum = 0;
   do {
      checksum += op();
      ++iters;
      if (steady_clock::now() - t0 > std::chrono::seconds(30)) break;
   } while (duration_cast<nanoseconds>(steady_clock::now() - t0).count() < target_ns || iters < 3);
   double total_ns = static_cast<double>(duration_cast<nanoseconds>(steady_clock::now() - t0).count());
   double cpu_ns = now_cpu_ns() - c0;
   return {iters, total_ns, cpu_ns, checksum};
}

static void emit(const char* format, const char* op, size_t iters, double total_ns, double cpu_ns,
                 size_t bytes, uint64_t checksum)
{
   double ns_per_op = total_ns / static_cast<double>(iters);
   double mb_per_s = (static_cast<double>(bytes) * static_cast<double>(iters)) / (total_ns / 1.0e9) / 1.0e6;
   double cpu_ns_per_op = cpu_ns / static_cast<double>(iters);
   double cpu_mb_per_s = (static_cast<double>(bytes) * static_cast<double>(iters)) / (cpu_ns / 1.0e9) / 1.0e6;
   std::printf("RT format=%-4s op=%-5s iters=%zu total_ns=%.0f ns_per_op=%.1f mb_per_s=%.1f "
               "cpu_total_ns=%.0f cpu_ns_per_op=%.1f cpu_mb_per_s=%.1f bytes=%zu "
               "checksum=0x%016" PRIx64 "\n",
               format, op, iters, total_ns, ns_per_op, mb_per_s, cpu_ns, cpu_ns_per_op, cpu_mb_per_s,
               bytes, checksum);
}

int main(int argc, char** argv)
{
   const char* dump_dir = nullptr;
   if (argc >= 3 && std::strcmp(argv[1], "--dump") == 0) dump_dir = argv[2];
   const bool dump = dump_dir != nullptr;

   if (dump) {
      // Emit deterministic bytes for the cross-path cmp check, then exit.
      record r = make_record(1000);
      toml_doc d = make_toml(2000);
      std::string buf;
      glz::write_json(r, buf);
      std::printf("DUMP json bytes=%zu checksum=0x%016" PRIx64 "\n", buf.size(), fnv1a(buf.data(), buf.size()));
      std::FILE* f = std::fopen((std::string(dump_dir) + "/json.bin").c_str(), "wb");
      std::fwrite(buf.data(), 1, buf.size(), f);
      std::fclose(f);
      buf.clear();
      glz::write_beve(r, buf);
      std::printf("DUMP beve bytes=%zu checksum=0x%016" PRIx64 "\n", buf.size(), fnv1a(buf.data(), buf.size()));
      f = std::fopen((std::string(dump_dir) + "/beve.bin").c_str(), "wb");
      std::fwrite(buf.data(), 1, buf.size(), f);
      std::fclose(f);
      buf.clear();
      glz::write_cbor(r, buf);
      std::printf("DUMP cbor bytes=%zu checksum=0x%016" PRIx64 "\n", buf.size(), fnv1a(buf.data(), buf.size()));
      f = std::fopen((std::string(dump_dir) + "/cbor.bin").c_str(), "wb");
      std::fwrite(buf.data(), 1, buf.size(), f);
      std::fclose(f);
      buf.clear();
      glz::write_toml(d, buf);
      std::printf("DUMP toml bytes=%zu checksum=0x%016" PRIx64 "\n", buf.size(), fnv1a(buf.data(), buf.size()));
      f = std::fopen((std::string(dump_dir) + "/toml.bin").c_str(), "wb");
      std::fwrite(buf.data(), 1, buf.size(), f);
      std::fclose(f);
      return 0;
   }

   record r = make_record(1000);
   toml_doc d = make_toml(2000);

   // ---- JSON ------------------------------------------------------------
   std::string json;
   glz::write_json(r, json);
   {
      auto t = time_op([&] {
         std::string b;
         glz::write_json(r, b);
         return fnv1a(b.data(), b.size());
      });
      emit("json", "write", t.iters, t.total_ns, t.cpu_ns, json.size(), t.checksum);
   }
   {
      auto t = time_op([&] {
         record out;
         glz::read_json(out, json);
         return record_sum(out);
      });
      emit("json", "read", t.iters, t.total_ns, t.cpu_ns, json.size(), t.checksum);
   }

   // ---- BEVE ------------------------------------------------------------
   std::string beve;
   glz::write_beve(r, beve);
   {
      auto t = time_op([&] {
         std::string b;
         glz::write_beve(r, b);
         return fnv1a(b.data(), b.size());
      });
      emit("beve", "write", t.iters, t.total_ns, t.cpu_ns, beve.size(), t.checksum);
   }
   {
      auto t = time_op([&] {
         record out;
         glz::read_beve(out, beve);
         return record_sum(out);
      });
      emit("beve", "read", t.iters, t.total_ns, t.cpu_ns, beve.size(), t.checksum);
   }

   // ---- CBOR ------------------------------------------------------------
   std::string cbor;
   glz::write_cbor(r, cbor);
   {
      auto t = time_op([&] {
         std::string b;
         glz::write_cbor(r, b);
         return fnv1a(b.data(), b.size());
      });
      emit("cbor", "write", t.iters, t.total_ns, t.cpu_ns, cbor.size(), t.checksum);
   }
   {
      auto t = time_op([&] {
         record out;
         glz::read_cbor(out, cbor);
         return record_sum(out);
      });
      emit("cbor", "read", t.iters, t.total_ns, t.cpu_ns, cbor.size(), t.checksum);
   }

   // ---- TOML ------------------------------------------------------------
   std::string toml;
   glz::write_toml(d, toml);
   {
      auto t = time_op([&] {
         std::string b;
         glz::write_toml(d, b);
         return fnv1a(b.data(), b.size());
      });
      emit("toml", "write", t.iters, t.total_ns, t.cpu_ns, toml.size(), t.checksum);
   }
   {
      auto t = time_op([&] {
         toml_doc out;
         glz::read_toml(out, toml);
         return toml_sum(out);
      });
      emit("toml", "read", t.iters, t.total_ns, t.cpu_ns, toml.size(), t.checksum);
   }

   return 0;
}

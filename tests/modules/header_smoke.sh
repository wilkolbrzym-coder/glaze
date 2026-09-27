#!/usr/bin/env bash
# Header-only smoke test for compilers that cannot `import std;`.
#
#   tests/modules/header_smoke.sh <c++-compiler>
#
# GCC's module support does not provide the standard `std` module (libstdc++ ships
# no libstdc++.modules.json) and Apple's libc++ predates std.cppm, so those legs of
# the modules workflow cannot run the module build. They run this instead: every
# dependency-free public umbrella header is compiled as the sole #include of a
# translation unit, then a small JSON round-trip is built and executed so template
# code is instantiated and runs on the host CPU (NEON on arm64, AVX on x86_64).
#
# This is the header-only path. It never touches modules/ or imports anything; it
# only proves the generated include/ tree still compiles and runs under GCC and
# AppleClang. The exhaustive per-header standalone check with optional
# dependencies (Erlang/OpenSSL/Eigen) lives in
# cmake/ci/standalone-headers and is driven by .github/workflows/standalone-headers.yml.
#
# Exit status is non-zero if any header fails to compile, or the round-trip fails
# to build or run.

set -euo pipefail

CXX="${1:?usage: header_smoke.sh <c++-compiler>}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
INCLUDE="${2:-$ROOT/include}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Dependency-free public umbrella headers (no Erlang/OpenSSL/Eigen/Asio needed).
HEADERS=(
  glaze/glaze.hpp
  glaze/forward.hpp
  glaze/glaze_exceptions.hpp
  glaze/version.hpp
  glaze/json.hpp
  glaze/beve.hpp
  glaze/cbor.hpp
  glaze/bson.hpp
  glaze/msgpack.hpp
  glaze/toml.hpp
  glaze/csv.hpp
  glaze/jsonb.hpp
  glaze/chrono.hpp
  glaze/yaml.hpp
)

echo "== header smoke: $CXX, include root $INCLUDE =="

# AppleClang before clang 17 does not accept -std=c++23; c++2b is the same draft
# and is what Xcode 15's clang knows. Use the first spelling the compiler accepts
# rather than assuming one.
STD=c++23
for cand in c++23 c++2b; do
  printf 'int main() { return 0; }\n' > "$WORK/probe.cpp"
  if "$CXX" -std="$cand" -fsyntax-only "$WORK/probe.cpp" >/dev/null 2>&1; then STD=$cand; break; fi
done
echo "  standard: -std=$STD"

for header in "${HEADERS[@]}"; do
  printf '#include <%s>\nint main() { return 0; }\n' "$header" > "$WORK/one.cpp"
  "$CXX" -std="$STD" -I "$INCLUDE" -c "$WORK/one.cpp" -o "$WORK/one.o"
  echo "  ok  $header"
done

cat > "$WORK/roundtrip.cpp" <<'CPP'
#include <glaze/glaze.hpp>
#include <string>
struct record {
  int id{};
  std::string name{};
};
int main() {
  record in{7, "glaze"};
  const std::string json = glz::write_json(in).value_or("ERR");
  record out{};
  if (glz::read_json(out, json)) return 1;
  return (out.id == 7 && out.name == "glaze") ? 0 : 2;
}
CPP
"$CXX" -std="$STD" -I "$INCLUDE" "$WORK/roundtrip.cpp" -o "$WORK/roundtrip"
"$WORK/roundtrip"

echo "== header smoke PASSED =="

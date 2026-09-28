#pragma once
// Single switch point between the header-only and C++20 module paths.
// Exactly the same translation-unit source is compiled twice; only this
// header differs, so any measured difference is attributable to the path.
//
//   header path : #include "glaze/<format>.hpp"
//   module path : import glaze.<format>;   (requires prebuilt BMIs)

#ifdef GLZ_BENCH_MODULES
import std;
import glaze.json;
import glaze.beve;
import glaze.cbor;
import glaze.toml;
#else
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cstdio>

#include "glaze/json.hpp"
#include "glaze/beve.hpp"
#include "glaze/cbor.hpp"
#include "glaze/toml.hpp"
#endif

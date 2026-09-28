#pragma once
// Umbrella-surface switch: the direct analogue of including glz's umbrella
// header. Used only for the headline compile-time probe.

#ifdef GLZ_BENCH_MODULES
import std;
import glaze;
#else
#include <cstdint>
#include <string>

#include "glaze/glaze.hpp"
#endif

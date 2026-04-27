#pragma once

// CUDA-side variant of the relativity math leaf.
//
// Every function in `RelativityMath.h` is already declared `RR_HD inline`
// using cross-target intrinsics from `<cmath>`, so this header is
// currently a thin re-export. It exists so kernel translation units
// can `#include "relativity/RelativityMath.cuh"` to signal intent and
// so future device-specific overrides (rsqrtf, __fdividef,
// __saturatef, fast-math intrinsics) can be added here without
// touching the host surface.

#include "relativity/RelativityMath.h"

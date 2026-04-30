#pragma once

// CUDA-side re-export of the host+device RNG header. CUDA TUs
// (`*.cu`) include this `.cuh` file rather than `RNG.h` directly
// so future device-only specialisations (intrinsic-based
// lane-stepping, warp-sized batched generators, ...) can land
// here without churning every kernel call site.
//
// Stage 11A: every entry point is RR_HD inline in `RNG.h`; this
// file is a single re-export.
#include "pathtracer/RNG.h"

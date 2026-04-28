#pragma once

// CUDA-side variant of the path tracer's RNG.
//
// `RNG.h` is already `RR_HD inline` and uses only host- and
// device-callable arithmetic, so this header is a thin re-export
// for kernel translation units. Future device-specific overrides
// (warp-coherent stream advancement, shared-memory state,
// hardware-RNG hooks) can land here without touching the host
// surface.

#include "pathtracer/RNG.h"

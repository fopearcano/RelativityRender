#pragma once

// CUDA-side variant of the path tracer's sampling foundation.
//
// `Sampling.h` is already `RR_HD inline` and uses only host- and
// device-callable arithmetic, so this header is a thin re-export
// for kernel translation units. Future device-specific overrides
// (warp-coherent low-discrepancy sequences, fast-math intrinsics,
// device-side stratification helpers) can land here without
// touching the host surface.

#include "pathtracer/Sampling.h"

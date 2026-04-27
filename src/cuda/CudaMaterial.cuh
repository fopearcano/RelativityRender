#pragma once

// CUDA-side variant of the material foundation.
//
// Currently a thin re-export of `MaterialTypes.h` so kernels can
// `#include "cuda/CudaMaterial.cuh"` to signal intent. When the
// BSDF interface lands (`sample` / `eval` / `pdf`) it will live
// here as `RR_HD inline` helpers callable from kernel TUs. Future
// device-specific overrides (packed-field POD, fast-math intrinsics)
// land here without touching the host surface.
//
// `MaterialParams` is already host- and device-readable via
// `RR_HD`-friendly POD layout; a single shared struct is enough for
// the M11 foundation.

#include "material/MaterialTypes.h"

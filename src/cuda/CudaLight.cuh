#pragma once

// CUDA-side variant of the lighting foundation.
//
// Currently a thin re-export of `Light.h` so kernels can
// `#include "cuda/CudaLight.cuh"` to signal intent. When the path
// tracer (M14) lands, `RR_HD inline` sampling / eval / pdf helpers
// for each `LightType` (point / directional / area / environment)
// will live here as device-callable counterparts. Future
// device-specific intrinsics (rsqrtf, __fdividef) for hot
// importance-sampling paths can be added here without changing the
// host surface.

#include "lighting/Light.h"

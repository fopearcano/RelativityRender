#pragma once

// Kernel-side helpers and host-callable launch wrappers shared across
// `.cu` translation units in the CUDA backend. This header pulls in
// `<cuda_runtime.h>`, so it is only safe to include from `.cu` files.

#include <cuda_runtime.h>

namespace rr::cuda {

// Host-callable launcher for the gradient diagnostic kernel.
// Defined in CudaTestKernel.cu. Writes width*height Rgba32F pixels
// into `device_pixels` (channel-interleaved, row-major, top-left
// origin):
//   R = u = x / (width  - 1)
//   G = v = y / (height - 1)
//   B = 0
//   A = 1
// All per-pixel work happens on the device; the host only allocates,
// launches, and downloads.
void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream = 0);

}

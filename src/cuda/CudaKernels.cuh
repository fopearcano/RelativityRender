#pragma once

// Kernel-side helpers and host-callable launch wrappers shared across
// `.cu` translation units in the CUDA backend. This header is only safe
// to include from `.cu` files because it pulls in `cuda_runtime.h`.

#include <cuda_runtime.h>

namespace rr::cuda {

// Host-callable launch wrapper for the gradient test kernel.
// Defined in CudaTestKernel.cu. Writes width*height Rgba32F pixels into
// `device_pixels` (channel-interleaved, R = u, G = v, B = 0, A = 1).
void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream = 0);

}

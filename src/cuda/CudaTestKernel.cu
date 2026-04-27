#include "cuda/CudaKernels.cuh"

namespace rr::cuda {

namespace {

// One thread = one pixel. Output is the Rgba32F layout used by
// rr::image::Image (interleaved, top-left origin, row stride
// width * 4 floats). The gradient is computed entirely on the
// device - no CPU pixel loop touches this data.
__global__ void k_gradient_rgba32f(float* pixels, int width, int height) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const float u = (width  > 1) ? float(x) / float(width  - 1) : 0.0f;
    const float v = (height > 1) ? float(y) / float(height - 1) : 0.0f;

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = u;
    pixels[idx + 1] = v;
    pixels[idx + 2] = 0.0f;
    pixels[idx + 3] = 1.0f;
}

}

void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_gradient_rgba32f<<<grid, block, 0, stream>>>(device_pixels, width, height);
}

}

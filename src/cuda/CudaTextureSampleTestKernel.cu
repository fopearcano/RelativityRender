// Stage 13B.2 nearest-neighbor texture sampling validation kernel.
//
// For each output pixel:
//   1. Compute uv = (x / (W-1), y / (H-1)). Origin is top-left,
//      matching `Image`'s row-major / top-left convention.
//   2. Sample the device-resident texture via
//      `sampleTextureNearest(view, uv)` (clamp-to-edge nearest).
//   3. Write the RGB result into the Rgba32F framebuffer with
//      alpha = 1.
//
// All per-pixel work runs on the device. The host only:
//   - synthesises a small `ImageTexture` (CudaRenderer.cu builds a
//     2x2 four-color test pattern; this file is agnostic to which
//     texture is uploaded),
//   - uploads it via `GpuTexture` (also CudaRenderer.cu's job),
//   - launches this kernel with the resulting `DeviceTextureView`,
//   - downloads the framebuffer and saves PPM (main.cpp's job).
//
// No materials, no path-tracer integration, no UV-on-mesh path.
// Stage 13B.2 verifies the device sampler in isolation: a working
// validation image is end-to-end proof that texture upload +
// device-side sampling + UV addressing are consistent.

#include "cuda/CudaKernels.cuh"
#include "cuda/CudaTexture.cuh"

#include "math/Vec2.h"
#include "math/Vec3.h"

namespace rr::cuda {

namespace {

__global__ void k_texture_sample_test(float*            pixels,
                                      int               width,
                                      int               height,
                                      DeviceTextureView view) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    // Map output pixel to UV in [0, 1] x [0, 1]. Use (W-1) /
    // (H-1) so the corner pixels land exactly at uv = 0 and
    // uv = 1; this matches `Image`'s discrete-pixel addressing
    // and keeps the 2x2 reference texture's four quadrants
    // perfectly aligned with the four corners of the output.
    // Guard against width / height == 1 to avoid div-by-zero
    // (kernel runs but the launcher refuses such dims earlier).
    const float u = (width  > 1)
        ? static_cast<float>(x) / static_cast<float>(width  - 1)
        : 0.0f;
    const float v = (height > 1)
        ? static_cast<float>(y) / static_cast<float>(height - 1)
        : 0.0f;

    const rr::math::Vec3 rgb = sampleTextureNearest(view, rr::math::Vec2{u, v});

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = rgb.x;
    pixels[idx + 1] = rgb.y;
    pixels[idx + 2] = rgb.z;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

void launch_texture_sample_test(float*            device_pixels,
                                int               width,
                                int               height,
                                DeviceTextureView view,
                                cudaStream_t      stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_texture_sample_test<<<grid, block, 0, stream>>>(device_pixels,
                                                      width, height,
                                                      view);
}

}  // namespace rr::cuda

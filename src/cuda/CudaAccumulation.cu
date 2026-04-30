// Stage 11B progressive accumulation kernels.
//
// Three element-wise kernels drive the host
// `rr::renderer::AccumulationBuffer`:
//
//   k_accum_add      (frame i)      acc[i]     += sample[i]
//   k_accum_resolve  (read-only)    display[i]  = acc[i] * inv_n
//   k_random_rgba    (test source)  per pixel: (next_float, next_float,
//                                               next_float, 1.0)
//
// Plus the clear path, which is a `cudaMemset` (no kernel needed -
// faster than launching a per-element store). Each launcher
// contains the dim/grid setup so callers (the host
// AccumulationBuffer + CudaRenderer::render_accumulation_test)
// stay free of CUDA-launch syntax.

#include "cuda/CudaAccumulation.cuh"
#include "pathtracer/RNG.cuh"

#include <cuda_runtime.h>

#include <cstdint>

namespace rr::cuda {

namespace {

__global__ void k_accum_add(float* acc, const float* sample,
                            std::size_t float_count) {
    const std::size_t i =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= float_count) return;
    acc[i] += sample[i];
}

__global__ void k_accum_resolve(const float* acc, float* display,
                                std::size_t float_count,
                                float inv_samples) {
    const std::size_t i =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= float_count) return;
    display[i] = acc[i] * inv_samples;
}

__global__ void k_random_rgba_sample(float* pixels,
                                     int width, int height,
                                     unsigned int global_seed,
                                     unsigned int sample_index) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    rr::pathtracer::Rng rng = rr::pathtracer::make_pixel_rng(
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
        /*frame_index=*/static_cast<std::uint32_t>(sample_index),
        static_cast<std::uint64_t>(global_seed));

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = rr::pathtracer::next_float(rng);
    pixels[idx + 1] = rr::pathtracer::next_float(rng);
    pixels[idx + 2] = rr::pathtracer::next_float(rng);
    pixels[idx + 3] = 1.0f;
}

// Common 1D launch helper. Returns false on a CUDA launch error
// (drains `cudaGetLastError` so the next real call sees a clean
// state). The block size of 256 is the standard "good for
// element-wise kernels" choice; adaptive tuning lands when the
// real path tracer needs it.
template <typename Launch>
bool run_1d_elementwise(std::size_t float_count, Launch&& launch) {
    if (float_count == 0) return true;
    constexpr unsigned int block = 256u;
    const std::size_t blocks_sz = (float_count + block - 1u) / block;
    if (blocks_sz > 0xFFFFFFFFull) return false;     // grid limit guard
    const dim3 grid(static_cast<unsigned int>(blocks_sz));
    const dim3 blk(block);
    launch(grid, blk);
    if (cudaGetLastError() != cudaSuccess) {
        (void)cudaGetLastError();
        return false;
    }
    return true;
}

}  // namespace

bool launch_accum_clear(float* device_acc, std::size_t float_count) {
    if (device_acc == nullptr || float_count == 0) {
        return device_acc != nullptr || float_count == 0;
    }
    const cudaError_t e =
        cudaMemset(device_acc, 0, float_count * sizeof(float));
    if (e != cudaSuccess) {
        (void)cudaGetLastError();
        return false;
    }
    return true;
}

bool launch_accum_add(float* device_acc, const float* device_sample,
                      std::size_t float_count) {
    if (device_acc == nullptr || device_sample == nullptr) return false;
    return run_1d_elementwise(float_count,
        [device_acc, device_sample, float_count](dim3 grid, dim3 blk) {
            k_accum_add<<<grid, blk>>>(device_acc, device_sample,
                                       float_count);
        });
}

bool launch_accum_resolve(const float* device_acc, float* device_display,
                          std::size_t float_count, float inv_samples) {
    if (device_acc == nullptr || device_display == nullptr) return false;
    return run_1d_elementwise(float_count,
        [device_acc, device_display, float_count, inv_samples](
            dim3 grid, dim3 blk) {
            k_accum_resolve<<<grid, blk>>>(device_acc, device_display,
                                           float_count, inv_samples);
        });
}

bool launch_random_rgba_sample(float* device_pixels,
                               int width, int height,
                               unsigned int seed,
                               unsigned int sample_index) {
    if (device_pixels == nullptr || width <= 0 || height <= 0) return false;
    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);
    k_random_rgba_sample<<<grid, block>>>(device_pixels, width, height,
                                          seed, sample_index);
    if (cudaGetLastError() != cudaSuccess) {
        (void)cudaGetLastError();
        return false;
    }
    return true;
}

}  // namespace rr::cuda

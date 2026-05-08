// Stage 11B progressive accumulation kernels.
//
// Three element-wise primitives drive the host
// `rr::renderer::AccumulationBuffer`:
//
//   k_accum_add      (frame i)      acc[i]     += sample[i]
//   k_accum_resolve  (read-only)    display[i]  = acc[i] * inv_n
//   k_random_rgba    (test source)  per pixel: (next_float, next_float,
//                                               next_float, 1.0)
//
// Plus the clear path, which is a `cudaMemset` (no kernel needed -
// faster than launching a per-element store).
//
// Stage 18A.4 progressive-optimisation slice. The accumulation
// hotpath was identified as the path tracer's biggest per-frame
// memory-bandwidth consumer (PathTracer::render runs spp add /
// resolve passes over a full Rgba32F framebuffer per render). Two
// orthogonal optimisations land here:
//
// 1. **float4 vectorisation** of `k_accum_add` and `k_accum_resolve`.
//    Rgba32F means the buffer is 16-byte aligned and exactly four
//    floats per pixel, so we can treat it as a `float4*` array and
//    do one 16-byte load + one 16-byte store per pixel instead of
//    four scalar load/store pairs. The GPU's load-store unit
//    prefers 16-byte transactions; this collapses the per-element
//    instruction count by 4x and lets a single warp service four
//    pixels per memory cycle. The launchers fall back to the
//    scalar kernel when `float_count % 4 != 0` so the API contract
//    stays open to non-Rgba32F callers (none today; safety net).
//
// 2. **First-sample fast path** via `launch_accum_first_sample`,
//    which is a `cudaMemcpy(... D2D)` rather than a kernel launch.
//    The host-side `AccumulationBuffer::accumulate_sample` routes
//    `samples_ == 0` through this path, skipping the read-of-zeros
//    the add kernel would otherwise perform on the post-cudaMemset
//    buffer. cudaMemcpy d2d uses the memory controller's bulk-copy
//    fast path; no SM occupancy at all.
//
// Both optimisations are pixel-bit-identical to the scalar
// baseline (single-precision adds and copies are deterministic).

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

// Stage 18A.4: vectorised add. One thread services one float4 (one
// Rgba32F pixel). Same arithmetic as the scalar kernel; the
// compiler emits 1x ld.global.v4 + 1x st.global.v4 instead of
// 4x ld.global.f32 + 4x st.global.f32.
__global__ void k_accum_add_float4(float4* acc, const float4* sample,
                                   std::size_t float4_count) {
    const std::size_t i =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= float4_count) return;
    float4       a = acc[i];
    const float4 s = sample[i];
    a.x += s.x;
    a.y += s.y;
    a.z += s.z;
    a.w += s.w;
    acc[i] = a;
}

__global__ void k_accum_resolve(const float* acc, float* display,
                                std::size_t float_count,
                                float inv_samples) {
    const std::size_t i =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= float_count) return;
    display[i] = acc[i] * inv_samples;
}

// Stage 18A.4: vectorised resolve. Same shape as the scalar kernel,
// one float4 per thread. The multiply-by-scalar maps to four FMUL
// instructions on a single load/store pair.
__global__ void k_accum_resolve_float4(const float4* acc, float4* display,
                                       std::size_t float4_count,
                                       float inv_samples) {
    const std::size_t i =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= float4_count) return;
    const float4 a = acc[i];
    float4       d;
    d.x = a.x * inv_samples;
    d.y = a.y * inv_samples;
    d.z = a.z * inv_samples;
    d.w = a.w * inv_samples;
    display[i] = d;
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
bool run_1d_elementwise(std::size_t element_count, Launch&& launch) {
    if (element_count == 0) return true;
    constexpr unsigned int block = 256u;
    const std::size_t blocks_sz = (element_count + block - 1u) / block;
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

// Stage 18A.4: when `float_count` is a multiple of 4 (the Rgba32F
// invariant every documented caller honours) the scalar pointers
// can be reinterpreted as `float4*`. cudaMalloc's returned pointer
// is at least 256-byte aligned per the CUDA Programming Guide, so
// 16-byte float4 alignment is always satisfied.
constexpr bool is_float4_aligned(std::size_t float_count) {
    return (float_count & 0x3u) == 0u;
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

bool launch_accum_first_sample(float* device_acc, const float* device_sample,
                               std::size_t float_count) {
    if (device_acc == nullptr || device_sample == nullptr) return false;
    if (float_count == 0) return true;
    // Stage 18A.4: `cudaMemcpy(... D2D)` on the same device uses
    // the memory controller's bulk-copy fast path; no kernel
    // launch, no SM occupancy. Equivalent to the scalar add path
    // bit-for-bit when the accumulator is in its zeroed state
    // (acc + sample == sample), but skips the wasted read-of-zeros
    // and the add-kernel launch entirely.
    const cudaError_t e =
        cudaMemcpy(device_acc, device_sample,
                   float_count * sizeof(float),
                   cudaMemcpyDeviceToDevice);
    if (e != cudaSuccess) {
        (void)cudaGetLastError();
        return false;
    }
    return true;
}

bool launch_accum_add(float* device_acc, const float* device_sample,
                      std::size_t float_count) {
    if (device_acc == nullptr || device_sample == nullptr) return false;

    // Stage 18A.4: float4 fast path on the documented Rgba32F
    // layout. 1/4 the threads, 1/4 the memory transactions, and
    // 16-byte coalesced loads/stores (the SM's preferred
    // granularity).
    if (is_float4_aligned(float_count)) {
        const std::size_t float4_count = float_count >> 2u;
        return run_1d_elementwise(float4_count,
            [device_acc, device_sample, float4_count](dim3 grid, dim3 blk) {
                k_accum_add_float4<<<grid, blk>>>(
                    reinterpret_cast<float4*>(device_acc),
                    reinterpret_cast<const float4*>(device_sample),
                    float4_count);
            });
    }

    // Scalar fallback for non-Rgba32F-aligned counts. None today;
    // safety net for future callers with a different layout.
    return run_1d_elementwise(float_count,
        [device_acc, device_sample, float_count](dim3 grid, dim3 blk) {
            k_accum_add<<<grid, blk>>>(device_acc, device_sample,
                                       float_count);
        });
}

bool launch_accum_resolve(const float* device_acc, float* device_display,
                          std::size_t float_count, float inv_samples) {
    if (device_acc == nullptr || device_display == nullptr) return false;

    // Stage 18A.4: same float4 fast path as the add kernel.
    if (is_float4_aligned(float_count)) {
        const std::size_t float4_count = float_count >> 2u;
        return run_1d_elementwise(float4_count,
            [device_acc, device_display, float4_count, inv_samples](
                dim3 grid, dim3 blk) {
                k_accum_resolve_float4<<<grid, blk>>>(
                    reinterpret_cast<const float4*>(device_acc),
                    reinterpret_cast<float4*>(device_display),
                    float4_count, inv_samples);
            });
    }

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

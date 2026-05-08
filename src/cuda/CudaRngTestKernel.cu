// Stage 11A RNG / sampling validation kernel.
//
// Splits the framebuffer into four quadrants and uses each one to
// exercise one of the four primitives the Stage 11A prompt
// requires:
//
//   top-left  (TL):  next_float    -> grayscale white noise
//   top-right (TR):  next_vec2     -> r=u.x, g=u.y, b=0
//   bottom-left  (BL): sample_uniform_hemisphere ->
//                                 (r=dx*0.5+0.5, g=dy*0.5+0.5, b=dz)
//   bottom-right (BR): sample_cosine_hemisphere  -> same encoding
//
// All per-pixel work happens on the device; the host only sets up
// the launch and downloads the framebuffer (master rule: GPU
// renders all pixels, no CPU per-pixel loop).
//
// The two hemisphere quadrants emit the sampled direction's
// tangent-frame coordinates as RGB. Both quadrants should look
// "noisy but coloured"; the BR quadrant should be visibly bluer
// on average (cos-weighted samples cluster toward +Z, so dz is
// larger more often). This is the visual confirmation that the
// sampling primitives produce different distributions.

#include "cuda/CudaKernels.cuh"
#include "pathtracer/RNG.cuh"
#include "pathtracer/Sampling.cuh"

#include "math/Vec2.h"
#include "math/Vec3.h"

#include <cstdint>

namespace rr::cuda {

namespace {

__device__ inline void write_rgba(float* pixels, int idx,
                                  float r, float g, float b) {
    pixels[idx + 0] = r;
    pixels[idx + 1] = g;
    pixels[idx + 2] = b;
    pixels[idx + 3] = 1.0f;
}

__global__ void k_rng_test_visualize(float* pixels,
                                     int width, int height,
                                     unsigned int global_seed) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    // Per-pixel RNG. The frame index is 0 today; the path tracer
    // (master module 16) advances it across re-renders.
    rr::pathtracer::Rng rng = rr::pathtracer::make_pixel_rng(
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
        /*frame_index=*/0u,
        static_cast<std::uint64_t>(global_seed));

    const int half_w = width  / 2;
    const int half_h = height / 2;
    const bool right = (x >= half_w);
    const bool lower = (y >= half_h);

    const int idx = (y * width + x) * 4;

    if (!right && !lower) {
        // TL: scalar white noise.
        const float v = rr::pathtracer::next_float(rng);
        write_rgba(pixels, idx, v, v, v);
    } else if (right && !lower) {
        // TR: 2D uniform sample - red = u.x, green = u.y.
        const rr::math::Vec2 u = rr::pathtracer::next_vec2(rng);
        write_rgba(pixels, idx, u.x, u.y, 0.0f);
    } else if (!right && lower) {
        // BL: uniform-hemisphere sample, encoded as colour.
        const rr::math::Vec2 u   = rr::pathtracer::next_vec2(rng);
        const rr::math::Vec3 dir = rr::pathtracer::sample_uniform_hemisphere(u);
        write_rgba(pixels, idx,
                   dir.x * 0.5f + 0.5f,
                   dir.y * 0.5f + 0.5f,
                   dir.z);
    } else {
        // BR: cosine-weighted hemisphere sample. Same colour
        // encoding so the visual difference is purely the
        // distribution: this quadrant should bias toward more
        // blue on average because dz is larger more often.
        const rr::math::Vec2 u   = rr::pathtracer::next_vec2(rng);
        const rr::math::Vec3 dir = rr::pathtracer::sample_cosine_hemisphere(u);
        write_rgba(pixels, idx,
                   dir.x * 0.5f + 0.5f,
                   dir.y * 0.5f + 0.5f,
                   dir.z);
    }
}

}  // namespace

void launch_rng_test_visualize(float* device_pixels, int width, int height,
                               unsigned int global_seed,
                               cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_rng_test_visualize<<<grid, block, 0, stream>>>(device_pixels,
                                                     width, height,
                                                     global_seed);
}

}  // namespace rr::cuda

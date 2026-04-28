#pragma once

// Device-side AOV writer pack. Mirrors the host-side
// `rr::renderer::AOV` enumeration: each named device pointer is a
// per-pixel buffer the renderer kernel writes into during a launch.
// The host owns the device allocations (in `CudaRenderer::render_aovs`)
// and after the launch downloads each non-null buffer back into a
// host `rr::renderer::AOV`.
//
// Layout matches the framebuffer we already use elsewhere: every
// buffer is `width * height * 4` floats, channel-interleaved
// (Rgba32F). Scalar AOVs (`depth`, `doppler`, `searchlight`) pack
// their value in the R channel and write `0` for G / B and `1` for
// A so the saved PPM has consistent alpha.
//
// Any pointer left null in `CudaAOVPack` instructs the kernel to
// skip writing that AOV. v1 always writes all six, but the slot
// design lets future render configs request a subset cheaply.

#include <cuda_runtime.h>

#include "math/Vec3.h"

namespace rr::cuda {

struct CudaAOVPack {
    float* beauty             = nullptr;  // Rgba32F shaded result
    float* normal             = nullptr;  // Rgba32F encoded `0.5*N + 0.5`
    float* depth              = nullptr;  // scalar in R, 0/0/1 elsewhere
    float* albedo             = nullptr;  // Rgba32F base colour
    float* doppler_factor     = nullptr;  // scalar in R
    float* searchlight_factor = nullptr;  // scalar in R
};

// Write an Rgba32F-style triple at `(x, y)` with alpha 1.0. The four
// floats are written contiguously, matching `rr::image::Image`'s
// channel-interleaved layout.
RR_HD inline void aov_write_rgba(float* buffer, int x, int y, int width,
                                 rr::math::Vec3 v) {
    if (buffer == nullptr) return;
    const int idx = (y * width + x) * 4;
    buffer[idx + 0] = v.x;
    buffer[idx + 1] = v.y;
    buffer[idx + 2] = v.z;
    buffer[idx + 3] = 1.0f;
}

// Pack a scalar AOV value at `(x, y)`. R holds the value; G / B are
// zero; alpha is 1.0. The host save path (`AOV::save_ppm`) reads R
// and renormalises the brightest pixel to 1.0 before emitting an
// 8-bit grayscale PPM, so writing the raw value here is intentional.
RR_HD inline void aov_write_scalar(float* buffer, int x, int y, int width,
                                   float value) {
    if (buffer == nullptr) return;
    const int idx = (y * width + x) * 4;
    buffer[idx + 0] = value;
    buffer[idx + 1] = 0.0f;
    buffer[idx + 2] = 0.0f;
    buffer[idx + 3] = 1.0f;
}

}

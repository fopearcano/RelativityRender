#pragma once

#include "gpu/GpuBuffer.h"
#include "image/Image.h"

#include <cstddef>
#include <string>

namespace rr::renderer {

// Progressive accumulation buffer. Stage 11B foundation for the
// GPU path tracer: a device-side Rgba32F sum buffer plus a
// per-buffer sample counter, with primitives for clear, add a
// new sample frame, and resolve to a host-side display image.
//
// Layout: width*height pixels, 4 floats per pixel, channel-
// interleaved row-major top-left origin (matching every other
// `rr::image::Image::PixelFormat::Rgba32F` buffer in the
// project, including the framebuffers `CudaRenderer::*` writes).
// The accumulation invariant is that, after `samples_count()`
// calls to `accumulate_sample`, the buffer holds the per-channel
// SUM of all sample values; the resolve step divides by
// `samples_count()` to produce a normalised display frame.
//
// This is path-tracer-ready data layout, not path-tracer
// integration: Stage 11B does not wire it into any render
// action. A validation kernel + the `--render-accumulation-test`
// CLI action exercise the buffer end-to-end (clear -> N sample
// adds -> resolve -> save PPM), but the existing render
// handlers are untouched.
class AccumulationBuffer {
public:
    AccumulationBuffer() = default;

    // Re-allocate the device buffer to hold `width * height`
    // pixels of Rgba32F. Resets the sample count to 0 and
    // re-zeroes the buffer. Returns false on allocation or
    // memset failure (no GPU backend, OOM, etc.) and leaves the
    // buffer in a default-constructed state on that path so the
    // caller's next `valid()` check is honest.
    [[nodiscard]] bool resize(int width, int height);

    // Zero every accumulated channel + reset the sample counter
    // to 0. Idempotent. Forwards to the CUDA accumulation
    // launcher under RR_HAS_CUDA; returns false (without
    // touching state) when no GPU backend is compiled in.
    [[nodiscard]] bool reset();

    // Add one sample frame onto the running sum. `device_sample`
    // points at a device-side Rgba32F buffer with the same
    // dimensions as this accumulation buffer. The host never
    // touches per-pixel data; the addition runs on the device
    // (matches the master "GPU accumulates samples" rule). On
    // success, `samples_count()` advances by 1.
    [[nodiscard]] bool accumulate_sample(const float* device_sample);

    // Resolve: divide the running sum by `samples_count()` on
    // the device, download the normalised buffer into a fresh
    // host `Image`, and return it. Calls with
    // `samples_count() == 0` return an empty Image (every
    // channel zero) rather than divide-by-zero. The
    // accumulation buffer itself is untouched, so the caller
    // can keep adding samples after resolving for a preview.
    [[nodiscard]] rr::image::Image resolve_to_image();

    [[nodiscard]] int  width()         const noexcept { return width_; }
    [[nodiscard]] int  height()        const noexcept { return height_; }
    [[nodiscard]] int  samples_count() const noexcept { return samples_; }
    [[nodiscard]] bool valid()         const noexcept {
        return width_ > 0 && height_ > 0
            && device_.size() ==
                 static_cast<std::size_t>(width_) * height_ * 4u;
    }

    // Raw device pointer to the accumulator. Exposed for
    // diagnostics (e.g. tests that inspect the buffer between
    // operations); kernels normally drive this via the
    // launchers in `cuda/CudaAccumulation.cuh`.
    [[nodiscard]] const float* device_ptr() const noexcept {
        return device_.device_ptr();
    }
    [[nodiscard]] float*       device_ptr()       noexcept {
        return device_.device_ptr();
    }

private:
    int width_   = 0;
    int height_  = 0;
    int samples_ = 0;
    rr::gpu::GpuBuffer<float> device_;  // 4 floats / pixel
};

}  // namespace rr::renderer

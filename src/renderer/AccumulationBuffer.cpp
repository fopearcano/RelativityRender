#include "renderer/AccumulationBuffer.h"

#include "image/Image.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaAccumulation.cuh"
#endif

#include <cstddef>
#include <utility>

namespace rr::renderer {

namespace {

// Number of floats per pixel for the Rgba32F layout the rest of
// the project uses. Centralised here so the magic 4 doesn't get
// scattered across the resize / reset / accumulate / resolve
// math.
constexpr std::size_t kFloatsPerPixel = 4u;

}  // namespace

bool AccumulationBuffer::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        // Return the buffer to a clean default-constructed state
        // so `valid()` is honest about not being usable.
        device_.reset();
        width_   = 0;
        height_  = 0;
        samples_ = 0;
        return false;
    }

    const std::size_t float_count =
        static_cast<std::size_t>(width) * height * kFloatsPerPixel;

    if (!device_.allocate(float_count)) {
        // GpuBuffer::allocate handles the "no backend / OOM"
        // path by leaving itself empty; mirror that here so the
        // caller's later valid() check returns false honestly.
        width_   = 0;
        height_  = 0;
        samples_ = 0;
        return false;
    }
    width_   = width;
    height_  = height;
    samples_ = 0;

    // Resize zeroes the buffer so the caller's first
    // `accumulate_sample` adds onto a clean sum. `reset()`
    // shares this code path.
    return reset();
}

bool AccumulationBuffer::reset() {
    if (!valid()) return false;
    samples_ = 0;
#ifdef RR_HAS_CUDA
    return rr::cuda::launch_accum_clear(device_.device_ptr(),
                                        device_.size());
#else
    // No GPU backend: a host-only build can't honour the master
    // "GPU accumulates samples" rule. Return false so callers
    // know the operation didn't happen rather than silently
    // mutating host state to fake it.
    return false;
#endif
}

bool AccumulationBuffer::accumulate_sample(const float* device_sample) {
    if (!valid() || device_sample == nullptr) return false;
#ifdef RR_HAS_CUDA
    // Stage 18A.4: when the accumulator is fresh (samples_ == 0)
    // the freshly-cleared buffer holds zeros, and `acc + sample`
    // would just produce `sample`. Routing the first sample
    // through `cudaMemcpy(D2D)` skips the wasted read-of-zeros
    // and the add-kernel launch entirely; the memory controller's
    // bulk-copy path is much cheaper than an element-wise kernel.
    // Subsequent samples (samples_ > 0) keep using the
    // float4-vectorised add kernel.
    const bool ok = (samples_ == 0)
        ? rr::cuda::launch_accum_first_sample(device_.device_ptr(),
                                              device_sample,
                                              device_.size())
        : rr::cuda::launch_accum_add(device_.device_ptr(),
                                     device_sample,
                                     device_.size());
    if (!ok) return false;
    ++samples_;
    return true;
#else
    (void)device_sample;
    return false;
#endif
}

rr::image::Image AccumulationBuffer::resolve_to_image() {
    if (!valid()) {
        return rr::image::Image{};  // empty
    }

    rr::image::Image img(width_, height_, rr::image::PixelFormat::Rgba32F);

    // Zero-sample early-out: spec'd to return an empty (zeroed)
    // image rather than divide-by-zero. The Image constructor
    // already zero-initialises Rgba32F storage; nothing further
    // is needed here.
    if (samples_ == 0) {
        return img;
    }

#ifdef RR_HAS_CUDA
    // Allocate a temporary device-side display buffer for the
    // resolve. The accumulator is left untouched so the caller
    // can keep adding samples after resolving for a preview.
    rr::gpu::GpuBuffer<float> display;
    if (!display.allocate(device_.size())) {
        return rr::image::Image{};
    }
    const float inv_samples = 1.0f / static_cast<float>(samples_);
    if (!rr::cuda::launch_accum_resolve(device_.device_ptr(),
                                        display.device_ptr(),
                                        device_.size(),
                                        inv_samples)) {
        return rr::image::Image{};
    }
    if (!display.download(img.data(), img.size_in_floats())) {
        return rr::image::Image{};
    }
    return img;
#else
    // Host-only build: same honest no-op posture as the other
    // accumulation primitives. The empty-default Image returned
    // is the same shape callers see when no backend is present.
    return rr::image::Image{};
#endif
}

}  // namespace rr::renderer

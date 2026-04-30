#pragma once

#include "gpu/GpuBuffer.h"
#include "renderer/AOV.h"

#include <cstddef>
#include <vector>

namespace rr::renderer {

// Per-pass GPU AOV buffer (Stage 14A.2; master order #19).
//
// Owns a single device-side `GpuBuffer<float>` sized to
// `width * height * aov_component_count(type)` floats, plus the
// host-side `AOV` identity (id + type + name) the buffer is
// associated with. The number of float channels per pixel is
// driven by the AOV type: 3 for vector passes
// (Beauty / Normal / Albedo) and 1 for scalar passes
// (Depth / DopplerFactor / SearchlightFactor); the constructor
// reads the value through `AOV::component_count()` so the host
// always allocates the right size.
//
// Stage 14A.2 scope: allocation + management + download. There
// is NO kernel hook, NO renderer integration, NO automatic save
// path. The buffer's `device_ptr()` is exposed so the eventual
// renderer-integration sub-stage can hand it to a kernel as a
// write target; today the buffer stays untouched after
// `resize(...)`. CPU only allocates the buffer, downloads it,
// and (in a future sub-stage) saves it.
//
// Move-only, like `AccumulationBuffer` and `GpuTexture`. When
// `RR_HAS_CUDA` is not defined, allocation reports failure
// honestly via `valid() == false`; the rest of the API stays
// safe to call (downloads return false; reset is a no-op).
class GpuAOVBuffer {
public:
    GpuAOVBuffer() = default;
    ~GpuAOVBuffer() = default;

    GpuAOVBuffer(const GpuAOVBuffer&)            = delete;
    GpuAOVBuffer& operator=(const GpuAOVBuffer&) = delete;
    GpuAOVBuffer(GpuAOVBuffer&&) noexcept            = default;
    GpuAOVBuffer& operator=(GpuAOVBuffer&&) noexcept = default;

    // Construct a buffer that will hold the given AOV. No device
    // allocation happens here; the caller invokes `resize(w, h)`
    // to allocate.
    explicit GpuAOVBuffer(AOV aov);

    // (Re-)allocate device storage to `width * height *
    // component_count(type)` floats. Drops any prior allocation
    // first; on failure, leaves the buffer empty (no partial
    // state). `width == 0` or `height == 0` is a successful
    // clear, mirroring `GpuMesh::upload_vertices(host, 0)`'s
    // precedent.
    [[nodiscard]] bool resize(int width, int height);

    // Free the device allocation and zero the dimensions. Safe to
    // call repeatedly and on a moved-from / never-allocated
    // buffer. The destructor calls this automatically via
    // `GpuBuffer<float>`'s RAII.
    void reset() noexcept;

    // Download the device buffer into a host-side vector. The
    // destination is resized to the current buffer's size in
    // floats. Returns false if the buffer is empty (nothing to
    // download), the size is inconsistent, or the device-to-host
    // copy failed.
    [[nodiscard]] bool download(std::vector<float>& host_dst) const;

    // Accessors.
    [[nodiscard]] AOVType            type()             const noexcept { return aov_.type(); }
    [[nodiscard]] const AOV&         aov()              const noexcept { return aov_; }
    [[nodiscard]] int                width()            const noexcept { return width_;  }
    [[nodiscard]] int                height()           const noexcept { return height_; }
    [[nodiscard]] int                component_count()  const noexcept { return aov_.component_count(); }
    [[nodiscard]] std::size_t        size_in_floats()   const noexcept { return device_.size(); }
    [[nodiscard]] bool               empty()            const noexcept { return device_.empty(); }
    [[nodiscard]] bool               has_data()         const noexcept { return !device_.empty(); }

    // True iff dimensions are positive and the device buffer is
    // sized to match `width * height * component_count`. Mirrors
    // `AccumulationBuffer::valid()`'s contract.
    [[nodiscard]] bool valid() const noexcept;

    // Raw device pointer to the buffer. Exposed for the eventual
    // renderer-integration sub-stage's kernel to write into;
    // kernels normally drive this through a launcher that takes
    // `float*` + dims, like every other GPU output buffer in the
    // project. Returns nullptr when no allocation has happened.
    [[nodiscard]] const float* device_ptr() const noexcept {
        return device_.device_ptr();
    }
    [[nodiscard]] float*       device_ptr()       noexcept {
        return device_.device_ptr();
    }

private:
    AOV                       aov_;
    int                       width_  = 0;
    int                       height_ = 0;
    rr::gpu::GpuBuffer<float> device_;
};

// Build a vector of one `GpuAOVBuffer` per declared `AOVType`,
// using `AOV::make_*()` factories with their default lowercase
// names. Order: Beauty, Normal, Depth, Albedo, DopplerFactor,
// SearchlightFactor. None of the buffers are allocated yet -
// the caller calls `resize(width, height)` on each entry to
// commit device memory. Useful for validation handlers and
// future renderer-integration sub-stages that want every pass
// available; callers that only need a subset can build their
// own vector by hand.
[[nodiscard]] std::vector<GpuAOVBuffer> make_default_aov_set();

}  // namespace rr::renderer

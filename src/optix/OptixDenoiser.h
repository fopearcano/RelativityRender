#pragma once

#include <cstddef>
#include <string>

#include "gpu/GpuBuffer.h"  // Stage 21B.7: GpuBuffer<std::byte> for state + scratch

// Stage 21B.1 / 21B.2 - OptixDenoiser wrapper class skeleton.
//
// Class is declared unconditionally so consumers can include
// this header in any TU without depending on OptiX gating
// macros. The class' public surface is identical in both
// build modes; only the per-method behaviour changes.
//
// Compile gating contract (Stage 21B.2):
// - `RR_ENABLE_OPTIX=OFF` -> rr_optix is not built per the
//   Stage 12B.3 contract; consumers gate their own
//   `OptixDenoiser` usage with `#ifdef
//   RELATIVITYRENDER_ENABLE_OPTIX`. If the .cpp were forced
//   to compile in this mode, every method returns the
//   documented "OptiX disabled at build time" error.
// - `RR_ENABLE_OPTIX=ON` -> the class is "prepared for OptiX
//   usage": every method currently returns the documented
//   "not implemented in Stage 21B.1" error and will be
//   filled in by subsequent Stage 21B sub-stages
//   (initialize -> 21B.x, set_inputs -> 21B.y, invoke ->
//   21B.z, ... per the Stage 21A plan).
//
// In both modes there are zero `<optix.h>` includes and zero
// SDK calls; the actual OptiX wiring lands later.

namespace rr::optix {

class OptixBackend;

class OptixDenoiser {
public:
    struct Inputs {
        const float* beauty_device     = nullptr;
        int          beauty_components = 3;
        const float* albedo_device     = nullptr;
        const float* normal_device     = nullptr;
        int          width             = 0;
        int          height            = 0;
    };

    struct Output {
        float* device = nullptr;
        int    width  = 0;
        int    height = 0;
    };

    OptixDenoiser() noexcept = default;
    ~OptixDenoiser();

    OptixDenoiser(const OptixDenoiser&)            = delete;
    OptixDenoiser& operator=(const OptixDenoiser&) = delete;
    OptixDenoiser(OptixDenoiser&&) noexcept;
    OptixDenoiser& operator=(OptixDenoiser&&) noexcept;

    [[nodiscard]] bool initialize(OptixBackend& backend) noexcept;
    [[nodiscard]] bool set_inputs(const Inputs& inputs) noexcept;
    [[nodiscard]] bool invoke(const Output& output)     noexcept;
    void               shutdown() noexcept;

    [[nodiscard]] bool               is_initialized() const noexcept;
    [[nodiscard]] bool               inputs_set()     const noexcept;
    [[nodiscard]] int                input_width()    const noexcept;
    [[nodiscard]] int                input_height()   const noexcept;
    [[nodiscard]] void*              denoiser_handle() const noexcept;
    [[nodiscard]] const std::string& last_error()      const noexcept;

private:
    void*       denoiser_                = nullptr;
    bool        initialized_             = false;
    void*       input_images_            = nullptr;
    bool        inputs_set_              = false;
    int         input_width_             = 0;
    int         input_height_            = 0;
    int         input_beauty_components_ = 0;

    // Stage 21B.6: memory-resource sizes returned by
    // `optixDenoiserComputeMemoryResources` for the most
    // recent successful `set_inputs(...)` call. Stored
    // here so subsequent sub-stages can size the device-
    // side state + scratch buffers without re-querying.
    // No allocation happens in Stage 21B.6 itself.
    std::size_t state_size_              = 0;
    std::size_t scratch_size_            = 0;

    // Stage 21B.7: device-side state + scratch buffers
    // sized by `set_inputs(...)` per the memory-resource
    // query. Both buffers are freed automatically by
    // `GpuBuffer`'s destructor / `shutdown()`'s explicit
    // `.reset()`. The audit-host fallback never allocates
    // these (its `set_inputs` stub returns `false` before
    // reaching the allocation block).
    rr::gpu::GpuBuffer<std::byte> state_buffer_;
    rr::gpu::GpuBuffer<std::byte> scratch_buffer_;

    std::string last_error_;
};

}  // namespace rr::optix

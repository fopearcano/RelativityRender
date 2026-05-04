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
    // Stage 21C.1 denoiser input contract.
    //
    // Three required device-resident buffers per
    // `docs/DENOISER_PLAN.md` "Required inputs" section:
    // Beauty (noisy linear-RGB radiance), Albedo (linear
    // RGB, base colour at hit before lighting), and
    // Normal (per-pixel shading normal). All three live
    // on the GPU and are populated by the renderer's
    // AOV-aware launch (`CudaRenderer::render_scene_with_aovs`
    // for the CUDA path; `OptixRenderer::render_aovs` for
    // the OptiX path) before this struct is filled in by
    // the host.
    //
    // The denoiser does NOT take ownership of the device
    // buffers; the caller (renderer host orchestration)
    // keeps them alive across the eventual
    // `optixDenoiserInvoke`.
    struct Inputs {
        // Beauty (noisy linear-RGB radiance). Layout
        // depends on `beauty_components`:
        // - 3 floats / pixel (FLOAT3, default; matches
        //   the Stage 14A `AOVType::Beauty` buffer
        //   layout the CUDA / OptiX AOV pipelines
        //   produce).
        // - 4 floats / pixel (FLOAT4; reserved for the
        //   path-tracer's RGBA32F resolve buffer).
        // The OptiX denoiser maps each to
        // `OPTIX_PIXEL_FORMAT_FLOAT3` /
        // `OPTIX_PIXEL_FORMAT_FLOAT4` respectively.
        const float* beauty_device     = nullptr;
        int          beauty_components = 3;

        // Albedo (linear RGB, base colour BEFORE
        // lighting). 3 floats / pixel (FLOAT3). Source:
        // the Stage 14A `AOVType::Albedo` buffer
        // produced by either AOV pipeline.
        const float* albedo_device     = nullptr;

        // Normal (per-pixel shading normal). 3 floats /
        // pixel (FLOAT3). The Stage 14A AOV pipelines
        // emit `0.5 * n + 0.5` (encoded into [0, 1]) for
        // hits and `(0, 0, 0)` for misses; the OptiX
        // denoiser handles this layout through its HDR
        // model. Same convention used by the CUDA
        // `--render-aovs` -> denoiser handoff.
        const float* normal_device     = nullptr;

        // Framebuffer dimensions. Same value across all
        // three input buffers; the OptiX denoiser
        // requires uniform dims across Beauty + guide
        // layers.
        int          width             = 0;
        int          height            = 0;
    };

    // Stage 21C.1 denoiser output contract.
    //
    // Single device-side buffer the denoiser writes into.
    // Caller-owned: the denoiser does not allocate or
    // free this buffer. Sized to `width * height *
    // beauty_components` floats (matching the bound
    // Beauty input's layout). On `optixDenoiserInvoke`
    // success, the device buffer carries the denoised
    // linear-RGB radiance the consumer can download +
    // save to `output/denoised.ppm` per the Stage 21A.6
    // output contract.
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

    // Stage 21D.1: high-level denoise entry point. Wraps
    // the initialize -> set_inputs -> invoke pipeline
    // behind a single public call so consumers do not
    // need to manage the three-step sequence themselves.
    //
    // Stage 21D.1 ships only the shell: the function
    // checks `isAvailable()`, runs the Stage 21C.5
    // `validateDenoiserInputs` precondition check, and
    // returns the documented "shell only, invoke not yet
    // wired" status when both succeed. The actual
    // `optixDenoiserInvoke` wiring lands in subsequent
    // Stage 21D sub-stages.
    //
    // Returns `true` once the full pipeline (validate ->
    // prepare -> invoke -> synchronise) succeeds; until
    // then the call always returns `false` with
    // `last_error()` populated to describe which stage of
    // the pipeline was reached.
    //
    // Pre-conditions (will be enforced via
    // `validateDenoiserInputs` in Stage 21D.1):
    // - `isAvailable() == true` (denoiser was initialised
    //   successfully).
    // - `inputs.beauty_device`,
    //   `inputs.albedo_device`, and
    //   `inputs.normal_device` non-null.
    // - `output.device` non-null.
    // - `inputs.width / ::height > 0`.
    // - `output.width == inputs.width` and
    //   `output.height == inputs.height`.
    // - `inputs.beauty_components` in `{3, 4}`.
    [[nodiscard]] bool denoise(const Inputs& inputs,
                               const Output& output) noexcept;

    [[nodiscard]] bool               is_initialized() const noexcept;
    [[nodiscard]] bool               inputs_set()     const noexcept;
    [[nodiscard]] int                input_width()    const noexcept;
    [[nodiscard]] int                input_height()   const noexcept;
    [[nodiscard]] void*              denoiser_handle() const noexcept;
    [[nodiscard]] const std::string& last_error()      const noexcept;

    // Stage 21B.9: high-level availability query. Returns
    // `true` iff the build was configured with
    // `-DRR_ENABLE_OPTIX=ON` AND `initialize(backend)`
    // succeeded. Defined inline so callers can consume the
    // header in either build mode without gating their own
    // call site - in the OFF build the method is a
    // constant-`false` no-op; in the ON build it forwards
    // to the runtime `initialized_` flag.
    //
    // Pure read; "no execution" per the Stage 21B.9 rule.
    [[nodiscard]] bool isAvailable() const noexcept {
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
        return initialized_;
#else
        return false;
#endif
    }

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

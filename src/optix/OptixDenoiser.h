#pragma once

#include <string>

// Stage 19B.1 OptiX denoiser context owner. Per
// `docs/DENOISER_PLAN.md` §3.1 / §9.1, the denoiser reuses the
// `OptixDeviceContext` the project already creates and owns
// through `OptixBackend` (Stage 17A.1) - no second runtime
// context, no second CUDA primary context.
//
// Stage 19B.1 scope: context lifecycle ONLY. NO image
// processing, NO buffer allocation, NO `optixDenoiserSetup`
// / `optixDenoiserInvoke`. This slice ships exactly two real
// OptiX calls (`optixDenoiserCreate` on init,
// `optixDenoiserDestroy` on shutdown) plus the host-side RAII
// wrapper. Subsequent 19B sub-stages add memory-resource
// queries, working-buffer allocation, and the actual denoise
// invocation.
//
// Two-layer macro gating mirrors `OptixBackend` /
// `OptixPipeline` / `OptixGas`:
// - `RELATIVITYRENDER_ENABLE_OPTIX` undefined -> `rr_optix`
//   is not built at all (per Stage 12B.3); this header is
//   not consumed by any TU.
// - `RELATIVITYRENDER_OPTIX_SDK_FOUND` undefined ->
//   `initialize()` returns `false` with the documented
//   "SDK not found" message; the audit-host build still
//   compiles cleanly. Same fallback shape as every other
//   rr_optix subsystem.
//
// Public surface deliberately avoids `<optix.h>`. The opaque
// denoiser handle is exposed as `void*` (real type:
// `OptixDenoiser`); consumers that need the typed handle
// reinterpret_cast on their own side after including
// `<optix.h>`.

namespace rr::optix {

class OptixBackend;

// Move-only RAII owner for an `OptixDenoiser` handle plus its
// initialisation state. Created via `initialize(backend)`;
// destroyed automatically on scope exit (or via explicit
// `shutdown()`).
//
// Stage 19B.1's `OptixDenoiserOptions` are pinned to:
// - `guideAlbedo  = 1` (Albedo guide layer required per
//   DENOISER_PLAN §8.1.2),
// - `guideNormal  = 1` (Normal guide layer required per
//   DENOISER_PLAN §8.1.3),
// - `denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY`
//   (the project's Beauty alpha is always 1; nothing to
//   denoise on that channel).
//
// The model kind is `OPTIX_DENOISER_MODEL_KIND_HDR` because
// the path tracer produces unbounded radiance values
// (DENOISER_PLAN §8.1.1); LDR would clip values >1 to white.
// Temporal models require motion vectors (DENOISER_PLAN
// §8.2.2) which the renderer does not produce yet; AOV mode
// is a future slice's call.
//
// Stage 19B.2 extends the class with the input-binding
// surface: an `Inputs` POD + `set_inputs(...)` that converts
// raw device pointers from the renderer's AOV pipeline into
// the OptiX-required `OptixImage2D` descriptors. No invoke,
// no working-buffer allocation, no file output - the descriptors
// are stored in private state for the next 19B sub-stage's
// `optixDenoiserInvoke`. Per DENOISER_PLAN §8.4 the descriptors
// are non-owning views: the renderer keeps the underlying
// `GpuAOVBuffer` / display buffer alive across the denoiser
// call.
class OptixDenoiser {
public:
    // Stage 19B.2 input binding. The three required device
    // pointers per DENOISER_PLAN §8.1 (Beauty / Albedo /
    // Normal) plus the framebuffer dimensions. All three
    // pointers must be device-resident; the caller owns the
    // underlying memory (typically a path-tracer resolve
    // buffer + two `GpuAOVBuffer`s). Beauty is FLOAT4
    // (Rgba32F, 4 floats / pixel - the path tracer's
    // resolve output per DENOISER_PLAN §8.3.1 route B);
    // Albedo and Normal are FLOAT3 (RGB / XYZ, 3 floats /
    // pixel - the Stage 14A AOV layout).
    //
    // The optional Depth + Motion inputs declared in
    // DENOISER_PLAN §8.2 are intentionally absent here:
    // Depth is not consumed by the OptiX denoiser today;
    // Motion requires a future `AOVType::Motion` AOV the
    // renderer does not produce yet. Adding them is a
    // future slice's responsibility.
    struct Inputs {
        // Beauty (noisy radiance), Rgba32F device buffer.
        // Layout: `width * height * 4` floats, channel-
        // interleaved, top-left origin. Mapped to
        // `OPTIX_PIXEL_FORMAT_FLOAT4` so the alpha channel
        // (always 1 in this project) passes through under
        // `OPTIX_DENOISER_ALPHA_MODE_COPY`.
        const float* beauty_device = nullptr;

        // Albedo (linear-space RGB, base colour at hit before
        // lighting), 3 floats / pixel device buffer. Mapped
        // to `OPTIX_PIXEL_FORMAT_FLOAT3`. Source: the Stage
        // 14A `GpuAOVBuffer` for `AOVType::Albedo`, populated
        // by `CudaRenderer::render_scene_with_aovs` via its
        // `AOVTargets::albedo` field.
        const float* albedo_device = nullptr;

        // Normal (world-space XYZ, components in [-1, 1]),
        // 3 floats / pixel device buffer. Mapped to
        // `OPTIX_PIXEL_FORMAT_FLOAT3`. Source: the Stage 14A
        // `GpuAOVBuffer` for `AOVType::Normal`, populated by
        // `render_scene_with_aovs::AOVTargets::normal`.
        const float* normal_device = nullptr;

        // Framebuffer dimensions. Same value across all three
        // input buffers; the OptiX denoiser requires uniform
        // dims across Beauty + guide layers.
        int          width  = 0;
        int          height = 0;
    };

    OptixDenoiser() noexcept = default;
    ~OptixDenoiser();

    OptixDenoiser(const OptixDenoiser&)            = delete;
    OptixDenoiser& operator=(const OptixDenoiser&) = delete;
    OptixDenoiser(OptixDenoiser&&) noexcept;
    OptixDenoiser& operator=(OptixDenoiser&&) noexcept;

    // Create the underlying `OptixDenoiser` against the given
    // backend. The backend must be initialised
    // (`backend.isInitialized() == true`); otherwise the call
    // returns `false` and `last_error()` reports the cause.
    // Idempotent: calling on an already-initialised denoiser
    // is a no-op success.
    //
    // On the audit-host fallback (SDK not found) this always
    // returns `false` with the documented "requires OptiX
    // SDK" error and leaves the denoiser uninitialised.
    [[nodiscard]] bool initialize(OptixBackend& backend) noexcept;

    // Stage 19B.2: bind the renderer's AOV buffers to the
    // denoiser's input slots. Builds three internal
    // `OptixImage2D` descriptors (Beauty -> FLOAT4, Albedo
    // -> FLOAT3, Normal -> FLOAT3) from the raw device
    // pointers + dimensions. The descriptors are stored in
    // private state for the next sub-stage's
    // `optixDenoiserInvoke`; this slice does NOT launch any
    // CUDA / OptiX work.
    //
    // Validation:
    // - All three device pointers must be non-null.
    // - `width > 0` and `height > 0`.
    // - The denoiser does NOT need to be initialised first
    //   - the input-binding step is decoupled from
    //   `initialize()` so a caller can stage the inputs
    //   before / after creating the underlying handle.
    //
    // The denoiser does NOT take ownership of the device
    // buffers. The caller (renderer host orchestration)
    // must keep them alive through the eventual
    // `optixDenoiserInvoke`. Re-calling `set_inputs(...)`
    // overwrites the previous descriptors.
    //
    // On the audit-host fallback this always returns `false`
    // with the documented "requires OptiX SDK" error.
    [[nodiscard]] bool set_inputs(const Inputs& inputs) noexcept;

    // Destroy the underlying `OptixDenoiser` (if any) and
    // reset internal state, including any `set_inputs`
    // descriptors. Idempotent. The destructor calls this
    // automatically.
    void shutdown() noexcept;

    [[nodiscard]] bool               is_initialized() const noexcept;

    // True iff the most recent `set_inputs(...)` call
    // succeeded and the descriptors have not been cleared
    // (e.g. by a subsequent `shutdown()`).
    [[nodiscard]] bool               inputs_set()     const noexcept;

    // Width / height of the framebuffer described by the
    // last successful `set_inputs(...)` call. Zero when
    // `inputs_set()` is false.
    [[nodiscard]] int                input_width()    const noexcept;
    [[nodiscard]] int                input_height()   const noexcept;

    // The OptiX denoiser handle (real type: `OptixDenoiser`,
    // a typedef for `OptixDenoiser_t*` in the SDK). Returned
    // as `void*` to keep this header SDK-free. Consumers
    // reinterpret in their own .cpp after including
    // `<optix.h>`.
    [[nodiscard]] void*              denoiser_handle() const noexcept;

    [[nodiscard]] const std::string& last_error()      const noexcept;

private:
    // Opaque storage. The .cpp casts back to `::OptixDenoiser`
    // (the SDK typedef) when the SDK is available; on the
    // audit-host fallback this stays null and `is_initialized`
    // returns false.
    void*       denoiser_    = nullptr;  // OptixDenoiser handle
    bool        initialized_ = false;

    // Stage 19B.2 input descriptors. Real type:
    // `::OptixImage2D[3]` (slot 0 = Beauty FLOAT4, slot 1 =
    // Albedo FLOAT3, slot 2 = Normal FLOAT3). Stored as
    // `void*` to keep the header SDK-free; the .cpp casts
    // back to the typed array. Owned by this object - the
    // .cpp `new[]`s the array on `set_inputs` success and
    // `delete[]`s it on `shutdown`. The DEVICE pointers
    // referenced inside the descriptors are NOT owned here;
    // the caller's `GpuAOVBuffer` / resolve buffer is the
    // owner.
    void*       input_images_ = nullptr;
    bool        inputs_set_   = false;
    int         input_width_  = 0;
    int         input_height_ = 0;

    std::string last_error_;
};

}  // namespace rr::optix

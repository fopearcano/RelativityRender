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
class OptixDenoiser {
public:
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

    // Destroy the underlying `OptixDenoiser` (if any) and
    // reset internal state. Idempotent. The destructor calls
    // this automatically.
    void shutdown() noexcept;

    [[nodiscard]] bool               is_initialized() const noexcept;

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
    std::string last_error_;
};

}  // namespace rr::optix

#pragma once

#include "image/Image.h"

#include <string>

// Per OPTIX_BACKEND_PLAN.md §19, host-facing OptiX render
// orchestrator - the analogue of `cuda/CudaRenderer.{h,cu}`.
//
// Stage 12B.2 shipped the file skeleton + a static `render()`
// placeholder. Stage 17A.3 adds the first real entry point:
// `render_test()` runs the minimum-viable pipeline (raygen +
// miss; no closest-hit, no materials, no path tracing) and
// returns a host-side `Image` containing the framebuffer the
// raygen wrote. The CUDA path is unaffected; OptiX remains an
// opt-in alternative gated on `-DRELATIVITYRENDER_ENABLE_OPTIX=
// ON` + a located OptiX SDK.

namespace rr::optix {

class OptixRenderer {
public:
    // Mirror of the eventual `CudaRenderer::Result` shape, with
    // the `image` field added now that the OptiX pipeline
    // actually produces pixels (Stage 17A.3).
    struct Result {
        bool             ok = false;
        std::string      message;
        rr::image::Image image;  // populated on success
        // Stage 18A.1 GPU timing. Elapsed time in milliseconds
        // measured via a `cudaEvent_t` pair around `optixLaunch`.
        // 0 means timing was not captured (audit-host fallback,
        // allocation failure, or early exit). Same format helper
        // as the CUDA path: `rr::gpu::format_gpu_timing_line`.
        float            gpu_time_ms = 0.0f;
    };

    // Stage 12B.2 placeholder; kept for backwards-compat with
    // any caller that already references it. Always returns
    // failure with a message describing the current state.
    [[nodiscard]] static Result render() noexcept;

    // Stage 17A.3 minimum-viable test render. Initialises the
    // OptiX backend, builds a pipeline (raygen + miss), allocates
    // a `width * height` Rgba32F framebuffer on the device,
    // launches the raygen which writes a flat colour, downloads
    // the framebuffer, and returns it as an `rr::image::Image`.
    //
    // On a no-CUDA / no-OptiX-SDK build this returns
    // `ok = false` with a clear remediation message. The
    // function never dispatches to the CUDA renderer; the
    // OptiX path is genuinely independent.
    [[nodiscard]] static Result render_test(int width, int height) noexcept;

    // Stage 17A.4 triangle render. Initialises the backend,
    // builds the same pipeline (now with a closest-hit program
    // group), uploads a single front-facing equilateral
    // triangle (matching the CUDA `--render-triangle` fixture
    // byte-for-byte), builds a single triangle GAS, populates
    // launch params with the camera + scene handle, launches
    // the raygen which fires one primary ray per pixel, and
    // returns the framebuffer.
    //
    // Closest-hit shading: `0.5 * normal + 0.5` (normal-as-
    // colour), matching the CUDA path. Miss shading: vertical
    // sky gradient, also matching the CUDA path. No path
    // tracing, no materials, no relativity.
    //
    // Same audit-host fallback semantics as render_test.
    [[nodiscard]] static Result render_triangle(int width, int height) noexcept;

    // Stage 17A.5 relativistic triangle render. Same pipeline
    // and same single-triangle GAS as `render_triangle`, but
    // the launch parameters carry a non-zero `Observer` (beta
    // = 0.5 along -Z, the camera's default forward direction)
    // plus the default `RelativityParams` (all effects enabled
    // at strength 1.0). The raygen Lorentz-aberrates the
    // primary ray; the closest-hit / miss programs apply the
    // Doppler colour shift + the bolometric searchlight scale
    // to their respective base shades. The math leaf is the
    // same `rr::relativity::*` header the CUDA path uses, so
    // both backends agree pixel-for-pixel for matched inputs.
    //
    // Default beta choice mirrors `--render-aovs` (Stage
    // 14A.3): a moderate -Z velocity that produces a clearly
    // visible blueshift + forward aberration + beaming
    // brightening, but stays well clear of the high-beta
    // numerical regime.
    //
    // Same audit-host fallback semantics as render_test.
    [[nodiscard]] static Result render_relativistic(int width, int height) noexcept;
};

}  // namespace rr::optix

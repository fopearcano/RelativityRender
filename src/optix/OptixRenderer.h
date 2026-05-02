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
};

}  // namespace rr::optix

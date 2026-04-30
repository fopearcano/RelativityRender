#pragma once

#include <string>

// Stage 12B.2 placeholder for the OptiX render orchestrator.
//
// Per OPTIX_BACKEND_PLAN.md §19, this is the analogue of
// `cuda/CudaRenderer.{h,cu}` for the OptiX backend - the eventual
// host-facing high-level rendering API (`OptixRenderer::Result`
// matching `CudaRenderer::Result`'s shape, plus the public render
// entry points `rr_renderer` host code dispatches into).
//
// Stage 12B.2's role is the *file skeleton only*: this header
// declares the class and a single static `render()` placeholder
// that always reports failure. The class exists; it does not
// render. Subsequent 12B sub-stages add the OptiX pipeline
// lifecycle, program-group construction, SBT + AS building, and
// the per-launch `optixLaunch` driving that turns this from a
// placeholder into a real backend.
//
// What this header is NOT yet:
// - Does not include `<optix.h>` or any other OptiX SDK header.
// - Does not include `image/Image.h` (placeholder Result carries
//   only `ok` + `message`; future implementations grow it to
//   match `CudaRenderer::Result`'s `image` field once the OptiX
//   path actually produces pixels).
// - Does not link any rendering work to the CUDA renderer; the
//   CUDA path tracer (Stage 11C) remains the primary path until
//   the OptiX backend lands real implementation.

namespace rr::optix {

class OptixRenderer {
public:
    // Mirror of the eventual `CudaRenderer::Result` shape (per
    // §17.5 / §19 / §24's "no duplication of high-level scene
    // structures" commitment), minus the `image` field. The
    // placeholder always returns `ok = false` with a message
    // explaining the current state; future sub-stages grow the
    // struct to carry an `rr::image::Image` when OptiX actually
    // renders.
    struct Result {
        bool        ok = false;
        std::string message;
    };

    // Stage 12B.2 placeholder. Always returns
    // `Result{ ok = false, message = "..." }`. The message
    // distinguishes the two states the user can be in:
    // - `RELATIVITYRENDER_ENABLE_OPTIX` was defined at compile
    //   time but the OptiX backend has no real implementation
    //   yet (12B.2 file skeleton).
    // - The macro was not defined (operator did not pass
    //   `-DRELATIVITYRENDER_ENABLE_OPTIX=ON`) and the OptiX path
    //   is intentionally absent.
    [[nodiscard]] static Result render() noexcept;
};

}  // namespace rr::optix

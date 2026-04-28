#pragma once

#include "image/Image.h"

#include <string>

// Host-side OptiX renderer entry point. Mirrors the shape of
// `rr::cuda::CudaRenderer` so callers (`main.cpp`, the future
// renderer server) can switch backends behind the same `Result`
// surface.
//
// At this milestone the implementation is a scaffold-only stub:
// `render_placeholder` always returns `ok = false` with a
// descriptive message. The real OptiX pipeline (AS / SBT / raygen /
// miss / closest-hit / launch) lands in subsequent M15 slices per
// `docs/OPTIX_BACKEND_PLAN.md`.

namespace rr::optix {

class OptixRenderer {
public:
    struct Result {
        bool             ok = false;
        rr::image::Image image;     // populated only when ok == true
        std::string      message;   // human-readable description
    };

    // Placeholder render entry point. At this milestone the body
    // only reports backend state - it does not produce an image.
    // The signature matches the CUDA renderer's pattern so the
    // future M15.4 implementation can drop in without touching
    // callers.
    [[nodiscard]] static Result render_placeholder(int width, int height);
};

}

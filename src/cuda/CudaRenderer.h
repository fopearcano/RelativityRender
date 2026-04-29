#pragma once

#include "image/Image.h"

#include <string>

// Host-side, CUDA-Runtime-free header. The implementation lives in
// `CudaRenderer.cu` and is only compiled when `-DRR_ENABLE_CUDA=ON`;
// callers gate use of this class on the `RR_HAS_CUDA` macro that the
// `rr_gpu` library propagates publicly when CUDA is enabled.
//
// Stage 6 surface: a single GPU diagnostic - a UV-gradient render.
// Real renderers (scene render, path trace, AOV pack, relativistic
// perception) come back in later stages on top of this scaffold.

namespace rr::cuda {

class CudaRenderer {
public:
    struct Result {
        bool             ok = false;
        rr::image::Image image;    // populated only when ok == true
        std::string      message;  // populated only when ok == false
    };

    // Render a UV-gradient framebuffer of the given size on the GPU
    // and download the pixels into a host Image (Rgba32F). Every
    // per-pixel write happens on the device; the host only allocates,
    // launches, and downloads.
    [[nodiscard]] static Result render_gradient(int width, int height);
};

}

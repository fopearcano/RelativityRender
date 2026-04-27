#pragma once

#include "image/Image.h"

#include <string>

// Host-side, CUDA-Runtime-free header. The implementation lives in
// `CudaRenderer.cu` and is only compiled when `-DRR_ENABLE_CUDA=ON`;
// callers gate use of this class on the `RR_HAS_CUDA` macro that the
// `rr_gpu` library propagates publicly when CUDA is enabled.

namespace rr::cuda {

class CudaRenderer {
public:
    struct Result {
        bool             ok = false;
        rr::image::Image image;    // populated only when ok == true
        std::string      message;  // populated only when ok == false
    };

    // Render a UV-gradient framebuffer of the given size on the GPU
    // and download the pixels into a host Image (Rgba32F). The CPU
    // does not generate the pixels - the gradient is computed by the
    // kernel, which is the whole point of this milestone. The caller
    // is responsible for saving the returned image.
    static Result render_gradient(int width, int height);
};

}

#pragma once

#include "camera/Camera.h"
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
    // and download the pixels into a host Image (Rgba32F). Kept as a
    // diagnostic since M6.
    static Result render_gradient(int width, int height);

    // Render a camera-ray-direction visualisation: for each pixel,
    // the GPU generates the primary pinhole ray from `camera` and
    // encodes the direction as RGB. All ray generation happens on the
    // GPU; the host only uploads the camera POD, launches, and
    // downloads.
    static Result render_camera_rays(const rr::camera::Camera& camera,
                                     int width, int height);
};

}


#pragma once

#include "image/Image.h"

#include <string>

// Host-side, CUDA-Runtime-free header. The implementation lives in
// `CudaRenderer.cu` and is only compiled when `-DRR_ENABLE_CUDA=ON`;
// callers gate use of this class on the `RR_HAS_CUDA` macro that the
// `rr_gpu` library propagates publicly when CUDA is enabled.
//
// Stage 7 surface: two GPU diagnostics - a UV-gradient render and a
// camera-ray-direction visualisation. Real renderers (scene render,
// path trace, AOV pack, relativistic perception) come back in later
// stages on top of this scaffold.

namespace rr::camera { class Camera; }

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

    // Render a camera-ray-direction visualisation: for each pixel,
    // the GPU generates the primary pinhole ray from `camera` (via
    // the same RR_HD `generate_camera_ray` host tests use) and
    // encodes the direction as RGB. All ray generation happens on
    // the GPU; the host only snapshots the camera into a `GpuCamera`
    // POD, launches, and downloads.
    [[nodiscard]] static Result render_camera_rays(const rr::camera::Camera& camera,
                                                   int width, int height);
};

}

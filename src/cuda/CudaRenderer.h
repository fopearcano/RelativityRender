#pragma once

#include "camera/Camera.h"
#include "geometry/Sphere.h"
#include "image/Image.h"
#include "relativity/RelativityParams.h"
#include "renderer/AOV.h"

#include <array>
#include <string>

namespace rr::gpu { class GpuScene; }

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

    // Render the first real scene: one sphere intersected against
    // primary rays. On hit the GPU shades with `0.5*n + 0.5`; on miss
    // it draws a simple vertical sky gradient. The CPU only constructs
    // the camera + sphere structs and launches; the kernel does
    // ray-gen, intersection, shading, and framebuffer writes.
    static Result render_sphere(const rr::camera::Camera& camera,
                                const rr::geometry::Sphere& sphere,
                                int width, int height);

    // M9: same scene as `render_sphere`, but the kernel runs the full
    // relativistic perception pipeline (aberration -> intersection ->
    // Doppler colour -> beaming). The CPU configures the camera,
    // observer, params, and sphere, then launches; every per-pixel
    // step happens on the GPU.
    static Result render_relativistic_sphere(
        const rr::camera::Camera&             camera,
        const rr::relativity::Observer&       observer,
        const rr::relativity::RelativityParams& params,
        const rr::geometry::Sphere&           sphere,
        int width, int height);

    // M10: render an uploaded `GpuScene`. The kernel reads camera /
    // observer / relativity / sphere array directly from the device
    // and runs a closest-hit loop per pixel. The CPU's only job is
    // to populate the `GpuScene` (via `upload_from(...)` or the
    // individual upload methods) and call this function.
    static Result render_scene(const rr::gpu::GpuScene& scene,
                               int width, int height);

    // M14: minimal CUDA path tracer. Per pixel: traces `spp`
    // independent paths with cosine-weighted Lambertian bounces up
    // to `max_depth`, accumulates emission + environment-fallback
    // radiance, applies the existing relativistic pipeline (Doppler
    // + searchlight) to the integrated value, and writes the
    // average to the framebuffer. The CPU only configures + launches
    // + saves; every per-ray step runs on the device.
    //
    // `seed_offset` is forwarded to the per-pixel RNG seed so a
    // caller running multiple launches for true progressive
    // accumulation can advance the RNG between invocations and
    // blend the resulting framebuffers themselves.
    static Result render_pathtrace(const rr::gpu::GpuScene& scene,
                                   int width, int height,
                                   int spp, int max_depth,
                                   unsigned int seed_offset = 0u);

    // M17: render the same scene as `render_scene` but capture every
    // intermediate quantity into a per-AOV buffer in a single GPU
    // launch. Returns a `kAOVCount`-sized array of populated host
    // `AOV`s (indexed by `static_cast<int>(AOVKind)`); when `ok` is
    // false the array is empty and `message` describes the failure.
    struct AOVResult {
        bool                                                            ok = false;
        std::array<rr::renderer::AOV, rr::renderer::kAOVCount>          aovs{};
        std::string                                                     message;
    };
    static AOVResult render_aovs(const rr::gpu::GpuScene& scene,
                                 int width, int height);
};

}



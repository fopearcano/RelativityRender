#pragma once

#include "image/Image.h"

#include <string>

// Host-side, CUDA-Runtime-free header. The implementation lives in
// `CudaRenderer.cu` and is only compiled when `-DRR_ENABLE_CUDA=ON`;
// callers gate use of this class on the `RR_HAS_CUDA` macro that the
// `rr_gpu` library propagates publicly when CUDA is enabled.
//
// Stage 6B surface: five GPU diagnostics - a UV-gradient render, a
// camera-ray-direction visualisation, a single-sphere intersection
// diagnostic, a relativistic single-sphere render, and a multi-sphere
// scene render that runs the full relativistic pipeline over an
// uploaded `GpuScene`. The full renderer (path trace, AOV pack)
// comes back in later stages on top of this scaffold.

namespace rr::camera   { class  Camera; }
namespace rr::geometry { struct Sphere; }
namespace rr::gpu      { class  GpuScene; }

namespace rr::relativity {
struct Observer;
struct RelativityParams;
}

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

    // Render a single-sphere intersection diagnostic: per pixel the
    // GPU generates the primary ray, intersects against `sphere`,
    // shades hits with `0.5*n + 0.5` and misses with a simple sky
    // gradient. All ray-gen + intersection + shading happens on the
    // device; the host only uploads the camera + sphere PODs as
    // launch arguments, launches, and downloads.
    [[nodiscard]] static Result render_sphere(const rr::camera::Camera&   camera,
                                              const rr::geometry::Sphere& sphere,
                                              int width, int height);

    // Render a single sphere with the full relativistic perception
    // pipeline: per pixel the GPU runs aberration -> intersection ->
    // base shade -> Doppler colour -> searchlight beaming. The host
    // only uploads the camera / observer / params / sphere PODs as
    // launch arguments. `params.max_beta` is the caller's
    // responsibility to set; the kernel does not re-clamp.
    [[nodiscard]] static Result render_relativistic_sphere(
        const rr::camera::Camera&             camera,
        const rr::relativity::Observer&       observer,
        const rr::relativity::RelativityParams& params,
        const rr::geometry::Sphere&           sphere,
        int width, int height);

    // Render an uploaded multi-sphere scene with the full
    // relativistic perception pipeline. The kernel runs a closest-hit
    // loop over the spheres in `scene` and writes the framebuffer.
    // Camera / observer / params travel inside `scene` as host PODs;
    // the sphere array is read from the device pointer + count
    // `scene` exposes through `device_spheres()` / `sphere_count()`.
    // The host never touches per-pixel state.
    //
    // Pre-conditions:
    //   - `scene.has_camera()` is true (otherwise the kernel reads
    //     a default-constructed GpuCamera, which projects from the
    //     origin with zero FOV - the result is uninformative but
    //     not crashy).
    //   - `scene.sphere_count() == 0` is allowed; the kernel falls
    //     through to the sky-gradient miss path for every pixel.
    [[nodiscard]] static Result render_scene(const rr::gpu::GpuScene& scene,
                                             int width, int height);

    // Render the Stage 11A RNG / sampling validation image. The
    // GPU writes a four-quadrant visualisation that exercises
    // each `pathtracer::*` primitive (white noise, 2D uniform,
    // uniform hemisphere, cosine hemisphere). `seed` mixes
    // through `make_pixel_rng` per pixel, so re-running with a
    // different seed produces a fresh noise field. Stage 11A is
    // sampling-foundation only; this is not a path-tracer
    // integration point.
    [[nodiscard]] static Result render_rng_test(int          width,
                                                int          height,
                                                unsigned int seed = 0u);

    // Render the Stage 13B.2 nearest-neighbor texture-sampling
    // validation image. Builds a small synthetic 2x2 RGBA8 four-
    // colour test pattern (red / green / blue / yellow) on the
    // host, uploads it via `rr::gpu::GpuTexture`, and launches
    // the kernel that for every output pixel maps to UV ->
    // samples the texture -> writes the resulting RGB to the
    // framebuffer. With clamp-to-edge nearest sampling on a 2x2
    // source the output is exactly four solid colour quadrants;
    // any other result indicates a UV-mapping or format-decode
    // bug. The host's only per-pixel responsibility is the final
    // download / save; the device does the sampling work.
    [[nodiscard]] static Result render_texture_sample_test(int width,
                                                           int height);
};

}

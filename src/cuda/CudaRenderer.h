#pragma once

#include "image/Image.h"
#include "manifold/CoordinateChart.h"  // SCHW.5: AOVTargets manifold payload
#include "manifold/ManifoldMode.h"     // SCHW.5: AOVTargets manifold payload

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
        // Stage 18A.1 GPU timing. Elapsed kernel time in
        // milliseconds, measured via a `cudaEvent_t` pair around
        // the kernel-launch region inside `run_kernel_render`.
        // 0 means timing was not captured (allocation failure or
        // early exit before the stop event recorded). Callers
        // typically format this via
        // `rr::gpu::format_gpu_timing_line` and emit a line via
        // `Logger::info`.
        float            gpu_time_ms = 0.0f;
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

    // Stage 14A.3 (master order #19) AOV render path. Renders the
    // given scene with the same `k_render_scene` kernel every
    // other Stage 6+ action uses, but populates the kernel's
    // `DeviceAOVView` so the kernel additionally writes per-pixel
    // values for the requested AOVs (Beauty / Normal / Depth /
    // Albedo / DopplerFactor / SearchlightFactor).
    //
    // Each pointer in `targets` is a raw device pointer into the
    // caller's per-pass storage (typically a
    // `rr::renderer::GpuAOVBuffer::device_ptr()`); pass `nullptr`
    // to skip a pass. The renderer does not allocate or own the
    // AOV buffers - the caller resizes them to (width, height)
    // before invoking. Layout per pass matches
    // `aov_component_count`: 3 floats / pixel for Beauty /
    // Normal / Albedo, 1 float / pixel for Depth /
    // DopplerFactor / SearchlightFactor.
    //
    // The 4-channel Rgba32F framebuffer is still allocated +
    // written + downloaded by `run_kernel_render` so the
    // returned `Result.image` matches the Beauty AOV's RGB;
    // callers that only want AOV outputs can ignore it.
    //
    // Note that `CudaRenderer.h` lives in rr_gpu and
    // `GpuAOVBuffer` lives in rr_renderer (which depends on
    // rr_gpu); using raw `float*` pointers here keeps the
    // dependency direction one-way, matching how
    // `AccumulationBuffer` (rr_renderer) is fed into
    // `launch_accumulate` (rr_gpu) by raw `float*` pointer.
    struct AOVTargets {
        float* beauty               = nullptr;
        float* normal               = nullptr;
        float* depth                = nullptr;
        float* albedo               = nullptr;
        float* doppler_factor       = nullptr;
        float* searchlight_factor   = nullptr;
        // MANI-I.8 — manifold debug coordinate-visualisation
        // AOV. Default `nullptr` means "not requested"; the
        // kernel skips the write arm and the existing six
        // AOVs are byte-identical to the pre-MANI-I.8
        // baseline. Set to a device buffer pointer when
        // `--render-aovs --manifold-debug` is in effect.
        float* manifold_coordinates = nullptr;

        // SCHW.5 — per-launch manifold payload. Defaults
        // are the pre-pivot disabled / Euclidean /
        // strength-0 no-op anchor. The CUDA kernel's
        // `ManifoldCoordinates` AOV write arm gates on
        // `is_active(manifold_mode) && chart ==
        // SchwarzschildLike && strength > 0`; on the
        // default the arm short-circuits and writes the
        // raw `best.position` (MANI-I.8 baseline). When
        // the operator engages the SchwarzschildLike
        // chart, the arm invokes the shared SCHW.1 math
        // leaf with `coordinate_chart.params`-derived
        // arguments + the `manifold_mode.strength`
        // runtime dial.
        rr::manifold::ManifoldMode    manifold_mode    = {};
        rr::manifold::CoordinateChart coordinate_chart = {};
    };

    [[nodiscard]] static Result render_scene_with_aovs(
        const rr::gpu::GpuScene& scene,
        int                      width,
        int                      height,
        const AOVTargets&        targets);
};

}

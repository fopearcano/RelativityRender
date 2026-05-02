#pragma once

#include "camera/CameraRay.h"
#include "relativity/RelativityParams.h"

#include <cstdint>

// Stage 17A.3 / 17A.4 / 17A.5 OptiX launch-parameters POD per
// `docs/OPTIX_BACKEND_PLAN.md` §23. The struct is shared by host
// and device code: the host (`OptixPipeline`) populates an
// instance, copies it to a device-resident buffer, and passes the
// device pointer through `optixLaunch`'s `pipelineParams`
// argument; the device (`OptixPrograms.cu`) reads it from the
// `optixLaunchParams` `__constant__` symbol.
//
// Stage 17A.3 added `framebuffer` + `width`/`height` +
// `flat_color_*` for the no-trace flat-colour smoke render
// (`render_test`). Stage 17A.4 grew the POD with `camera` and
// `scene_handle` so the raygen can fire actual rays. Stage
// 17A.5 grows it further with the relativistic observer state:
// - `observer`: the `rr::relativity::Observer` POD carrying the
//   observer's 3-velocity (beta) in the scene's rest frame.
//   Identical to what the CUDA path's `--render-relativistic`
//   handler uploads via `GpuScene::upload_relativity`.
// - `params`: the `rr::relativity::RelativityParams` knobs
//   (per-effect enable bits + strength multipliers + beta cap).
//   The OptiX raygen / closest-hit / miss programs gate
//   aberration / Doppler / searchlight on these flags exactly
//   the way `k_sphere_relativistic` and `k_render_scene` do in
//   the CUDA path.
//
// At |beta| = 0 every relativistic helper is identity, so a
// caller that does not need the relativity pipeline (e.g.
// `render_triangle`) leaves the new fields default-constructed
// and gets the Stage 17A.4 behaviour byte-for-byte.
//
// Stage 17A.5 still NO RNG state, NO AOV pointers, NO bounce
// loop; subsequent 17A+ sub-stages grow the POD as needed.
//
// Header is host-friendly: pulls in `camera/CameraRay.h` (which
// is RR_HD-safe) and `relativity/RelativityParams.h` (a host /
// device POD aggregate). Device-side `.cu` files include
// `<optix.h>` themselves before pulling this header.

namespace rr::optix {

struct OptixLaunchParams {
    // ---- output framebuffer ----
    float* framebuffer = nullptr;  // Rgba32F, channel-interleaved
    std::int32_t width  = 0;
    std::int32_t height = 0;

    // ---- Stage 17A.3 flat-colour write (used when scene_handle == 0)
    float flat_color_r = 1.0f;
    float flat_color_g = 0.0f;
    float flat_color_b = 1.0f;

    // ---- Stage 17A.4 traced raygen ----

    // Camera POD shared with the CUDA path's
    // `rr::camera::generate_camera_ray`. The raygen calls the
    // same RR_HD inline helper so both backends produce the same
    // primary rays for the same `(x, y, width, height)`.
    rr::camera::GpuCamera camera{};

    // OptixTraversableHandle of the root acceleration structure.
    // 0 = "no trace; raygen writes flat colour" (Stage 17A.3
    // backwards compatibility). Non-zero = "trace this GAS"
    // (Stage 17A.4).
    std::uint64_t scene_handle = 0;

    // ---- Stage 17A.5 relativistic observer state ----
    //
    // Identical PODs to the ones the CUDA path's `CudaSceneView`
    // carries. At default-constructed values (|beta| = 0,
    // every effect enabled) the relativity helpers degenerate
    // to identity, so the Stage 17A.4 triangle pipeline keeps
    // its existing pixel output - the relativity transforms
    // only diverge when the host populates a non-zero observer
    // velocity.
    rr::relativity::Observer         observer{};
    rr::relativity::RelativityParams params{};
};

}  // namespace rr::optix

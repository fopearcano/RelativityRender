#pragma once

#include "camera/CameraRay.h"

#include <cstdint>

// Stage 17A.3 / 17A.4 OptiX launch-parameters POD per
// `docs/OPTIX_BACKEND_PLAN.md` §23. The struct is shared by host
// and device code: the host (`OptixPipeline`) populates an
// instance, copies it to a device-resident buffer, and passes the
// device pointer through `optixLaunch`'s `pipelineParams`
// argument; the device (`OptixPrograms.cu`) reads it from the
// `optixLaunchParams` `__constant__` symbol.
//
// Stage 17A.3 added `framebuffer` + `width`/`height` +
// `flat_color_*` for the no-trace flat-colour smoke render
// (`render_test`). Stage 17A.4 grows the POD with the bits the
// raygen needs to actually fire rays:
// - `camera`: the `rr::camera::GpuCamera` POD already used by
//   the CUDA path's `generate_camera_ray`. Letting the OptiX
//   raygen include the same RR_HD-friendly helper keeps both
//   backends visually consistent.
// - `scene_handle`: the `OptixTraversableHandle` (exposed as
//   `uint64_t` to keep this header SDK-free) of the GAS the
//   raygen traces against. When `scene_handle == 0` the raygen
//   skips `optixTrace` and falls back to the Stage 17A.3
//   flat-colour write - keeping `render_test` working without
//   an extra raygen entry point.
//
// Stage 17A.4 still NO RNG state, NO AOV pointers, NO bounce
// loop; subsequent 17A+ sub-stages grow the POD as needed.
//
// Header is host-friendly: pulls in `camera/CameraRay.h` (which
// is RR_HD-safe) and `<cstdint>`. Device-side `.cu` files
// include `<optix.h>` themselves before pulling this header.

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
};

}  // namespace rr::optix

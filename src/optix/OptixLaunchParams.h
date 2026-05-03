#pragma once

#include "camera/CameraRay.h"
#include "lighting/Light.h"           // Stage 20K: Light POD union
#include "relativity/RelativityParams.h"

#include <cstdint>

// Stage 17A.3 / 17A.4 / 17A.5 / 20B OptiX launch-parameters POD
// per `docs/OPTIX_BACKEND_PLAN.md` §23. The struct is shared by
// host and device code: the host (`OptixPipeline`) populates an
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
// Stage 20B adds two placeholder fields for the eventual
// progressive-accumulation integration:
// - `accum_buffer`: device-side Rgba32F (4 floats / pixel)
//   accumulator buffer pointer. Layout matches
//   `rr::renderer::AccumulationBuffer::device_ptr()` so the
//   OptiX path can share an `AccumulationBuffer` instance with
//   the CUDA path eventually. Default `nullptr` means "no
//   accumulation; raygen writes the framebuffer directly" —
//   the Stage 17A.3-17A.5 raygen / closest-hit / miss programs
//   ignore this field and continue to write `framebuffer`
//   directly, byte-for-byte.
// - `sample_index`: per-launch sample counter (matches the
//   CUDA path tracer's `unsigned int sample_index` argument
//   in `CudaPathTracer.cu`). Default `0` means "first sample"
//   so RNG seeding stays deterministic for the no-progressive
//   path. Existing programs do not consume this field; it is
//   reserved for the path-tracer-through-OptiX wiring slice.
//
// At |beta| = 0 every relativistic helper is identity, so a
// caller that does not need the relativity pipeline (e.g.
// `render_triangle`) leaves the new fields default-constructed
// and gets the Stage 17A.4 behaviour byte-for-byte.
//
// Stage 20B still NO RNG state on the POD itself, NO AOV
// pointers, NO bounce loop, NO SBT changes; subsequent
// sub-stages grow the POD as the integration lands.
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

    // ---- Stage 20B progressive accumulation (placeholders) ----
    //
    // Layout identical to `rr::renderer::AccumulationBuffer`:
    // `width * height` pixels, 4 floats / pixel (Rgba32F),
    // channel-interleaved row-major top-left origin. When
    // `accum_buffer == nullptr` the OptiX raygen ignores both
    // fields and writes `framebuffer` directly (Stage 17A.3-
    // 17A.5 behaviour, byte-for-byte). When non-null, the
    // (yet-to-be-written) progressive raygen will accumulate
    // each sample's contribution into `accum_buffer` and use
    // `sample_index` for RNG seeding. The fields are wired
    // through the launch-params POD now so the integration
    // slice (Stage 20C+ / OptiX path-tracer wiring) does not
    // need to grow the POD again.
    float*        accum_buffer = nullptr;  // Rgba32F, 4 floats / pixel
    std::uint32_t sample_index = 0;        // first sample = 0

    // ---- Stage 20I path-tracer launch state ----
    //
    // Used by the `__raygen__pathtrace` /
    // `__miss__pathtrace` / `__closesthit__pathtrace`
    // entry-point family. Existing entries
    // (`__raygen__pinhole` etc.) ignore these fields.
    //
    // - `spp` = samples per pixel for the launch. The path-
    //   tracer raygen iterates this loop GPU-side, seeding
    //   `rr::pathtracer::Rng` from
    //   `(x, y, sample_index, seed)` for each sample. Default
    //   1 = single-sample (no AA jitter beyond the deterministic
    //   pixel-centre).
    // - `max_bounces` = bounce-loop limit per sample (1 means
    //   primary only; the path tracer breaks out of the loop
    //   on miss or after the limit is hit). Default 1 keeps
    //   the launch-params POD backwards-safe.
    // - `seed` = artist-supplied RNG seed (combined with
    //   `(x, y, sample_index)` via `pathtracer::make_pixel_rng`).
    //   Default 0 = deterministic.
    std::int32_t  spp          = 1;
    std::int32_t  max_bounces  = 1;
    std::uint32_t seed         = 0;

    // ---- Stage 20K direct-lighting state ----
    //
    // Used by the radiance closest-hit when the SBT hit-record
    // carries `shading_mode == 2` (Stage 20K direct lighting).
    // The host populates `lights` with a device-resident
    // `rr::lighting::Light` array uploaded via cudaMalloc +
    // cudaMemcpy; `light_count` is the number of entries.
    // Other shading modes (0 = normal-as-color, 1 = material
    // flat from Stage 20G) ignore these fields.
    //
    // At default (`lights == nullptr`, `light_count == 0`) the
    // direct-lighting branch finds zero lights, falls into
    // its "no lights uploaded" path (Stage 8B facing-ratio
    // fallback), and produces a sensible image without
    // requiring the host to populate the field — same shape
    // as the CUDA `--render-direct-lighting`'s no-lights
    // safety net.
    const rr::lighting::Light* lights      = nullptr;
    std::int32_t               light_count = 0;
};

}  // namespace rr::optix

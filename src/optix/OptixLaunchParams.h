#pragma once

#include "camera/CameraRay.h"
#include "cuda/CudaTexture.cuh"        // Stage 20M: DeviceTextureView
#include "geometry/Triangle.h"         // Stage 20M: per-vertex UV indexing
#include "lighting/Light.h"            // Stage 20K: Light POD union
#include "manifold/ManifoldMode.h"     // MANI-I.5: per-launch manifold mode
#include "math/Vec2.h"                 // Stage 20M: per-vertex UVs
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

    // PT-P.24: per-channel firefly clamp on the per-sample
    // radiance, mirroring the CUDA path-tracer kernel's
    // `firefly_clamp` parameter. 0.0f disables the clamp
    // (default; matches `PathTraceConfig::firefly_clamp`'s
    // PT-P.21 default exactly); > 0 produces a per-channel
    // `fminf(radiance.x|y|z, firefly_clamp)` in
    // `__raygen__pathtrace` BEFORE the per-sample
    // `rgb_sum +=` accumulation. Both backends apply the same
    // clamp expression at the same point in their integrators
    // so their outputs remain convergent at non-zero clamp.
    // See `OptixRenderer::render_pathtrace*`'s
    // `firefly_clamp` parameter for the host-side wiring.
    float         firefly_clamp = 0.0f;

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

    // ---- Stage 20L direct-lighting visibility ----
    //
    // When `true` and `shading_mode == 2`, the closest-hit
    // traces an occlusion ray per light before accumulating
    // that light's contribution. Default `false` preserves
    // the Stage 20K behaviour (every light contributes
    // unconditionally; the CUDA Stage 9B "shadows are
    // deferred" precedent).
    //
    // Shadow rays use the single existing ray type but pass
    // `OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT |
    // OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT` + `missSbtIndex
    // = 1`, so the closest-hit is bypassed entirely; the
    // dedicated `__miss__shadow` program (bound to miss SBT
    // record 1) sets a single visibility-flag payload
    // register when the ray escapes.
    bool          enable_shadows = false;

    // ---- NEE.4 path-trace direct lighting (Next Event Estimation) ----
    //
    // OptiX-side mirror of NEE.2's CUDA `PathTraceConfig::enable_nee`.
    // Consumed exclusively by `__raygen__pathtrace`: when `true`
    // AND `light_count > 0`, the raygen invokes
    // `rr::pathtracer::sample_direct_light_uniform` once per
    // bounce vertex (using one in-guard `next_float(rng)` draw
    // for the uniform light selection), traces an any-hit
    // shadow ray that reuses the Stage 20L `__miss__shadow` SBT
    // record (`missSbtIndex = 1` + `OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT
    // | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT`; the shadow miss
    // sets payload register 0 = 1 on visibility, 0 on occlusion),
    // and adds a Lambert-BRDF + cosine + throughput-modulated
    // direct-light contribution to the running per-sample
    // radiance.
    //
    // Default `false` keeps the OptiX raygen byte-for-byte
    // identical with the pre-NEE.4 build: the in-guard
    // `next_float(rng)` draw is never executed, the cosine-
    // hemisphere `next_vec2(rng)` immediately below pulls
    // from a bit-identical RNG state, no shadow ray is traced,
    // and the per-pixel write is bit-exact with the pre-NEE.4
    // arithmetic. Same IEEE-754 zero-add-exactness argument
    // PT-P.21 / PT-P.24 used for the firefly-clamp default-off
    // path.
    //
    // Other OptiX entries (`__raygen__pinhole`, `__raygen__test`,
    // `__raygen__aov_sample`, `__raygen__direct_lighting` -
    // i.e. the radiance pipeline + the Stage 20K `shading_mode
    // == 2` direct-lighting closest-hit) IGNORE this field;
    // their dispatchers leave it default-`false` and their
    // shading paths do not consume it. The NEE branch lives
    // ONLY inside `__raygen__pathtrace`, mirroring the CUDA
    // path's confinement to `k_pathtrace_sample`.
    //
    // Light-type scope: `LightType::Point` and
    // `LightType::Directional` contribute through the helper
    // (matching CUDA NEE.2). `LightType::Area` and
    // `LightType::Environment` are PLACEHOLDER per
    // `Light.h:20-31`; the helper returns `pdf_inv = 0` for
    // those types and the kernel naturally treats them as
    // zero-contribution samples. MIS is reserved for the
    // future area-light slice (the v1 "no double-count
    // window" argument from `docs/PATH_TRACER_NEE_TASK.md`
    // §1 holds because Point + Directional lights have no
    // mesh).
    //
    // See `OptixRenderer::render_pathtrace*`'s `enable_nee`
    // parameter for the host-side wiring, and
    // `PathTraceConfig::enable_nee` in
    // `src/pathtracer/PathTracer.h` for the CUDA-side
    // counterpart this field mirrors.
    bool          enable_nee = false;

    // ---- Stage 20M textured-material state ----
    //
    // Used by the radiance closest-hit when the SBT hit-record
    // carries `shading_mode == 1` AND the picked material has
    // `useBaseColorTexture == true`. The closest-hit interpolates
    // UVs via `optixGetTriangleBarycentrics()`, looks up
    // `textures[baseColorTextureId]`, and calls
    // `rr::cuda::sampleTextureNearest(...)` instead of using
    // `params.baseColor` directly.
    //
    // Defaults (all-null + zero) preserve Stage 20G behaviour
    // byte-for-byte: the closest-hit's
    // `useBaseColorTexture` check evaluates to "no" in that
    // state and falls back to `params.baseColor`.
    //
    // - `mesh_uvs`: device-resident array of per-vertex `Vec2`
    //   UVs (one per Vertex). Indexed by triangle vertex
    //   indices.
    // - `mesh_indices`: device-resident array of `Triangle`
    //   (3 x uint32_t). Same data as the GAS index buffer;
    //   uploaded separately so the closest-hit can find a
    //   triangle's vertex indices.
    // - `textures`: device-resident array of
    //   `rr::cuda::DeviceTextureView`. One entry per scene
    //   texture; the host builds the array by allocating
    //   per-texture pixel buffers + recording (pixels, w, h,
    //   format) for each.
    // - `texture_count`: number of entries in `textures`.
    const rr::math::Vec2*                mesh_uvs      = nullptr;
    const rr::geometry::Triangle*        mesh_indices  = nullptr;
    const rr::cuda::DeviceTextureView*   textures      = nullptr;
    std::int32_t                         texture_count = 0;

    // ---- Stage 20N AOV outputs ----
    //
    // Six per-pixel device buffers, one per `rr::renderer::AOVType`.
    // Layout: `width * height * component_count` floats, channel-
    // interleaved row-major top-left origin (matches the host-side
    // `rr::renderer::GpuAOVBuffer` contract).
    // - aov_beauty             : 3 floats / pixel (RGB; lit shade)
    // - aov_normal             : 3 floats / pixel (encoded
    //                            `0.5 * n + 0.5` for hits;
    //                            (0, 0, 0) on miss)
    // - aov_depth              : 1 float / pixel (`1 / (1 + t)` for
    //                            hits; 0 on miss)
    // - aov_albedo             : 3 floats / pixel (material
    //                            baseColor; env color on miss)
    // - aov_doppler_factor     : 1 float / pixel (D from primary
    //                            ray direction; same for hit + miss)
    // - aov_searchlight_factor : 1 float / pixel (D^4)
    //
    // Defaults (all-null) preserve existing-entry behaviour
    // byte-for-byte: the closest-hit / miss / raygen short-circuit
    // their AOV writes when the corresponding pointer is null.
    // Only `--render-optix-aovs` populates these.
    float* aov_beauty             = nullptr;
    float* aov_normal             = nullptr;
    float* aov_depth              = nullptr;
    float* aov_albedo             = nullptr;
    float* aov_doppler_factor     = nullptr;
    float* aov_searchlight_factor = nullptr;

    // ---- MANI-I.5 manifold rendering mode ----
    //
    // Per-launch Manifold Core mode the renderer reads to
    // decide *whether* the Manifold Core should engage with
    // the chart-aware ray seam. Default-constructed
    // `ManifoldMode{}` ("disabled, Euclidean, strength 0,
    // debug off") preserves every existing entry-point's
    // pixel output bit-for-bit because:
    //   - `is_active(manifold_mode)` returns `false` (the
    //     `enabled` bit is `false`), so any future kernel
    //     guard short-circuits before any chart math runs;
    //   - even with `enabled = true`, the active chart is
    //     `Euclidean` by default, and `is_active(...)` only
    //     returns `true` for non-Euclidean families;
    //   - no MANI-I.5 code path actually reads this field
    //     in any kernel (the MANI-I.5 scope is plumbing
    //     only — the field exists on the launch-params POD
    //     so MANI-I.6 / MANI-I.7+ can flip a single guard
    //     to enable per-chart logic without growing the
    //     POD again).
    //
    // Populated by the OptiX dispatcher
    // (`OptixRenderer::render_pathtrace_progressive`'s
    // trailing `manifold_mode` parameter — same shape as
    // the existing `firefly_clamp` / `enable_nee`
    // arguments). Default value preserves the byte-identity
    // invariant the integration plan §2 declares.
    rr::manifold::ManifoldMode manifold_mode{};
};

}  // namespace rr::optix

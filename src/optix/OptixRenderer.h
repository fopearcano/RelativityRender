#pragma once

#include "image/Image.h"
#include "gpu/GpuBuffer.h"  // OptiX Gap A Step 1: GpuBuffer<float> for retained AOV device buffers
#include "manifold/ManifoldMode.h"  // MANI-I.5: trailing render_pathtrace_progressive arg

#include <string>
#include <vector>

// Per OPTIX_BACKEND_PLAN.md §19, host-facing OptiX render
// orchestrator - the analogue of `cuda/CudaRenderer.{h,cu}`.
//
// Stage 12B.2 shipped the file skeleton + a static `render()`
// placeholder. Stage 17A.3 adds the first real entry point:
// `render_test()` runs the minimum-viable pipeline (raygen +
// miss; no closest-hit, no materials, no path tracing) and
// returns a host-side `Image` containing the framebuffer the
// raygen wrote. The CUDA path is unaffected; OptiX remains an
// opt-in alternative gated on `-DRR_ENABLE_OPTIX=
// ON` + a located OptiX SDK.

// Forward decls so `render_mesh_scene` / `render_textured_material`
// / `render_aovs` can take Scene / ImageTexture / Light references
// without `OptixRenderer.h` pulling those headers in transitively.
namespace rr::scene    { struct Scene; }
namespace rr::texture  { class  ImageTexture; }
namespace rr::lighting { struct Light; }

namespace rr::optix {

class OptixRenderer {
public:
    // Mirror of the eventual `CudaRenderer::Result` shape, with
    // the `image` field added now that the OptiX pipeline
    // actually produces pixels (Stage 17A.3).
    struct Result {
        bool             ok = false;
        std::string      message;
        rr::image::Image image;  // populated on success
        // Stage 18A.1 GPU timing. Elapsed time in milliseconds
        // measured via a `cudaEvent_t` pair around `optixLaunch`.
        // 0 means timing was not captured (audit-host fallback,
        // allocation failure, or early exit). Same format helper
        // as the CUDA path: `rr::gpu::format_gpu_timing_line`.
        float            gpu_time_ms = 0.0f;
    };

    // Stage 12B.2 placeholder; kept for backwards-compat with
    // any caller that already references it. Always returns
    // failure with a message describing the current state.
    [[nodiscard]] static Result render() noexcept;

    // Stage 17A.3 minimum-viable test render. Initialises the
    // OptiX backend, builds a pipeline (raygen + miss), allocates
    // a `width * height` Rgba32F framebuffer on the device,
    // launches the raygen which writes a flat colour, downloads
    // the framebuffer, and returns it as an `rr::image::Image`.
    //
    // On a no-CUDA / no-OptiX-SDK build this returns
    // `ok = false` with a clear remediation message. The
    // function never dispatches to the CUDA renderer; the
    // OptiX path is genuinely independent.
    [[nodiscard]] static Result render_test(int width, int height) noexcept;

    // Stage 17A.4 triangle render. Initialises the backend,
    // builds the same pipeline (now with a closest-hit program
    // group), uploads a single front-facing equilateral
    // triangle (matching the CUDA `--render-triangle` fixture
    // byte-for-byte), builds a single triangle GAS, populates
    // launch params with the camera + scene handle, launches
    // the raygen which fires one primary ray per pixel, and
    // returns the framebuffer.
    //
    // Closest-hit shading: `0.5 * normal + 0.5` (normal-as-
    // colour), matching the CUDA path. Miss shading: vertical
    // sky gradient, also matching the CUDA path. No path
    // tracing, no materials, no relativity.
    //
    // Same audit-host fallback semantics as render_test.
    [[nodiscard]] static Result render_triangle(int width, int height) noexcept;

    // Stage 17A.5 relativistic triangle render. Same pipeline
    // and same single-triangle GAS as `render_triangle`, but
    // the launch parameters carry a non-zero `Observer` (beta
    // = 0.5 along -Z, the camera's default forward direction)
    // plus the default `RelativityParams` (all effects enabled
    // at strength 1.0). The raygen Lorentz-aberrates the
    // primary ray; the closest-hit / miss programs apply the
    // Doppler colour shift + the bolometric searchlight scale
    // to their respective base shades. The math leaf is the
    // same `rr::relativity::*` header the CUDA path uses, so
    // both backends agree pixel-for-pixel for matched inputs.
    //
    // Default beta choice mirrors `--render-aovs` (Stage
    // 14A.3): a moderate -Z velocity that produces a clearly
    // visible blueshift + forward aberration + beaming
    // brightening, but stays well clear of the high-beta
    // numerical regime.
    //
    // Stage 20H: `beta_magnitude` lets the caller pick the
    // |beta| along -Z; the observer velocity becomes
    // (0, 0, -beta_magnitude). Default 0.5 preserves the
    // Stage 17A.5 shape so callers that pass no explicit
    // beta get the documented `output/optix_relativity.ppm`
    // pixels byte-for-byte. Magnitude is clamped at <=
    // 0.999999 by `rr::relativity::clampBeta` inside the
    // implementation.
    //
    // Same audit-host fallback semantics as render_test.
    [[nodiscard]] static Result render_relativistic(
        int width, int height,
        float beta_magnitude = 0.5f) noexcept;

    // Stage 20C raygen-only render. Exercises the OptiX raygen
    // + miss + minimal-SBT + pipeline-creation surface end-to-
    // end, independent of any closest-hit behaviour.
    //
    // Implementation: builds a tiny degenerate triangle GAS
    // placed BEHIND the camera (z = +5; default camera looks
    // at -Z). The raygen launches per pixel, calls
    // `optixTrace` with the GAS handle, every primary ray
    // misses the geometry, and `__miss__radiance` runs per
    // pixel - producing the project's vertical sky-gradient
    // environment colour for every pixel.
    //
    // Observer / relativity params default-constructed
    // (|beta| = 0); the miss program's Doppler / searchlight
    // helpers degenerate to identity. Output is a flat
    // gradient sky.
    //
    // The pipeline still includes `__closesthit__radiance` (it
    // has been in the SBT since Stage 17A.4) but the geometry
    // is arranged so closest-hit never fires. This entry
    // therefore proves out the raygen / miss / SBT / pipeline
    // surface in isolation.
    //
    // Same audit-host fallback semantics as render_test.
    [[nodiscard]] static Result render_raygen(int width, int height) noexcept;

    // Stage 20F mesh-scene render. Builds an OptiX GAS from
    // the first non-empty mesh in `scene.meshes` (extracting
    // positions to a tightly-packed `float3` buffer that
    // `build_mesh_gas` consumes), uses the scene's `camera`
    // for primary-ray generation, and runs the existing
    // raygen + miss + closest-hit pipeline (Stages 17A.3 -
    // 17A.5). Closest-hit emits normal-as-color shading;
    // miss emits the gradient sky. No materials beyond
    // the closest-hit's normal-as-color base (per Stage 20F
    // rules); no path tracing.
    //
    // The caller is expected to have populated `scene` via
    // `rr::io::SceneLoader::load(...)`. Multi-mesh scenes
    // are supported in the parser but only the first
    // visible non-empty mesh is uploaded for this slice
    // (the existing `GpuScene::upload_mesh` slot holds one
    // mesh; OptiX-side multi-mesh / IAS lands later).
    //
    // On failure (no mesh in scene, allocation failure, GAS
    // build error, launch error) the result is `ok = false`
    // with a human-readable `message`; image is empty.
    //
    // Same audit-host fallback semantics as render_test
    // (returns `ok = false` with a "requires OptiX SDK"
    // message when the SDK was not located at build time).
    [[nodiscard]] static Result render_mesh_scene(
        const rr::scene::Scene& scene,
        int width, int height) noexcept;

    // Stage 20G material-scene render. Same single-mesh
    // selection + GAS-build path as `render_mesh_scene`,
    // but additionally:
    // - Looks up the picked mesh's material via
    //   `picked->material_id` in `scene.materials`. If
    //   `material_id < 0` or out of range, falls back to a
    //   default-constructed `MaterialParams` (baseColor =
    //   light grey, emission = 0).
    // - Calls `OptixPipeline::set_hit_material(mat, 1)`
    //   after `pipeline.create()`, so the closest-hit emits
    //   `baseColor + emissionColor * emissionStrength`
    //   instead of normal-as-color.
    //
    // The Stage 17A.5 Doppler / searchlight stack still
    // composes on top of the material output (identity at
    // |beta| = 0, default observer); no path tracing; no
    // textures (Stage 20G rules).
    //
    // Same audit-host fallback semantics as render_test.
    [[nodiscard]] static Result render_material_scene(
        const rr::scene::Scene& scene,
        int width, int height) noexcept;

    // Stage 20I minimum-viable OptiX path tracer. Builds a
    // path-tracer pipeline (raygen / miss / closest-hit
    // entries that iterate samples + bounces in raygen),
    // builds an OptiX GAS from the first non-empty mesh in
    // `scene.meshes`, populates the hit-group SBT record with
    // the picked mesh's material (closest-hit reads
    // `params.baseColor` as the diffuse albedo), and runs the
    // path-tracer launch. The raygen owns:
    //   - per-pixel RNG (seeded from
    //     `rr::pathtracer::make_pixel_rng(x, y, sample, seed)`)
    //   - sample loop (`spp` iterations)
    //   - bounce loop (up to `max_bounces` per sample)
    //   - throughput / radiance accumulation
    // Miss returns the Stage 17A.4 sky gradient as
    // environment radiance; closest-hit fills payload with
    // hit position / normal / albedo. No NEE / MIS / shadows
    // / textures yet (Stage 20I scope).
    //
    // `seed` controls deterministic RNG (default 0). The
    // Doppler / searchlight stack composes on top of the
    // accumulated radiance using the primary aberrated ray
    // direction (matches CUDA path-tracer behaviour
    // conceptually; per-bounce relativistic effects deferred).
    //
    // Same audit-host fallback semantics as render_test.
    //
    // NEE.4 grew the trailing argument list with `enable_nee`,
    // mirroring the CUDA `launch_pathtrace_sample(...,
    // enable_nee)` parameter and threading the flag into
    // `OptixLaunchParams::enable_nee` for `__raygen__pathtrace`.
    // Default `false` preserves the pre-NEE.4 behaviour byte-
    // for-byte (the raygen's `enable_nee` guard is never
    // entered, no shadow ray is traced, and the per-pixel
    // arithmetic is bit-identical with the pre-NEE.4 build).
    [[nodiscard]] static Result render_pathtrace(
        const rr::scene::Scene& scene,
        int width, int height,
        int spp, int max_bounces,
        unsigned int seed = 0u,
        float firefly_clamp = 0.0f,    // PT-P.24
        bool  enable_nee   = false) noexcept;  // NEE.4

    // Stage 20J progressive checkpoint snapshot. One per
    // requested element of `checkpoint_samples` argument to
    // `render_pathtrace_progressive`.
    struct PathtraceCheckpoint {
        int              sample_count = 0;     // 1, 16, ...
        rr::image::Image image;                // resolved Rgba32F
    };

    // Stage 20J progressive result: each requested checkpoint
    // produces one image. `total_gpu_time_ms` is the sum of
    // the per-launch elapsed times across every accumulated
    // sample (not just the checkpointed ones).
    struct PathtraceProgressiveResult {
        bool                              ok = false;
        std::string                       message;
        std::vector<PathtraceCheckpoint>  checkpoints;
        float                             total_gpu_time_ms = 0.0f;
    };

    // Stage 20J progressive OptiX path tracer. Connects the
    // `__raygen__pathtrace` family from Stage 20I to the
    // existing `rr::cuda::launch_accum_*` accumulation
    // primitives in rr_gpu. Each entry of
    // `checkpoint_samples` (in ascending order; values are
    // sample counts, not zero-based indices) produces one
    // resolved `PathtraceCheckpoint` image. The largest
    // checkpoint determines the total samples accumulated.
    //
    // Per-launch flow:
    //   - Allocate single-sample framebuffer + accumulator +
    //     display buffers (cudaMalloc).
    //   - launch_accum_clear(accumulator).
    //   - For sample_index in [0, max_checkpoint):
    //     - Set OptixLaunchParams.spp = 1, sample_index =
    //       sample_index.
    //     - optixLaunch (raygen writes single-sample radiance
    //       to framebuffer).
    //     - First sample: launch_accum_first_sample(accum,
    //       framebuffer); subsequent: launch_accum_add(accum,
    //       framebuffer).
    //     - If sample_index + 1 is a checkpoint, run
    //       launch_accum_resolve(accum, display, 1.0f /
    //       (sample_index+1)) + cudaMemcpy(D2H) into a fresh
    //       Image; append to result.checkpoints.
    //
    // Mirrors `rr::renderer::AccumulationBuffer` semantics
    // (clear, accumulate, resolve) without taking a hard
    // dependency on rr_renderer; the launchers reside in
    // rr_gpu (via `cuda/CudaAccumulation.cuh`) which rr_optix
    // already PRIVATE-links per Stage 18A.1.
    //
    // Same audit-host fallback semantics as render_test.
    //
    // NEE.4 grew the trailing argument list with `enable_nee`
    // (same shape as `render_pathtrace`), threaded into the
    // shared `OptixLaunchParams::enable_nee` field so every
    // per-sample launch in the progressive loop sees the same
    // flag. Default `false` preserves the pre-NEE.4 behaviour
    // byte-for-byte across every checkpoint.
    [[nodiscard]] static PathtraceProgressiveResult
    render_pathtrace_progressive(
        const rr::scene::Scene& scene,
        int width, int height,
        int max_bounces,
        unsigned int seed,
        const std::vector<int>& checkpoint_samples,
        float firefly_clamp = 0.0f,    // PT-P.24
        bool  enable_nee   = false,    // NEE.4
        // MANI-I.5: per-launch Manifold Core mode. Default
        // `disabled_manifold_mode()` keeps every existing
        // caller's output bit-for-bit (the renderer's kernels
        // do not consume this field this slice; the field
        // rides in `OptixLaunchParams::manifold_mode` so
        // MANI-I.6 / MANI-I.7+ can flip a guard without
        // re-growing the launch-params POD).
        rr::manifold::ManifoldMode manifold_mode = {}) noexcept;

    // Stage 20K basic direct-lighting render. Same first-non-
    // empty-mesh selection + GAS-build path as render_mesh_scene.
    // Uploads `scene.lights` to a device-resident buffer, threads
    // the pointer + count into `OptixLaunchParams::lights /
    // light_count`, sets the SBT hit-record's
    // `shading_mode = 2` so the closest-hit evaluates direct
    // lighting (point + directional + emission + environment
    // ambient), and runs a single launch (no spp loop, no
    // bounce loop). Output: `output/optix_direct_lighting.ppm`.
    //
    // Stage 20L: optional `enable_shadows` argument toggles
    // visibility testing per directional + point light. Default
    // `false` preserves Stage 20K (no shadow rays). When
    // `true`, the closest-hit traces a shadow ray per light
    // before accumulating its contribution; rays use
    // OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT |
    // OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT + missSbtIndex = 1
    // so they terminate on first hit + dispatch the dedicated
    // `__miss__shadow` program (which sets a single-register
    // visibility flag).
    //
    // Mirrors the CUDA `--render-direct-lighting` shape
    // conceptually (Stage 9B). The Stage 17A.5 / 20H Doppler +
    // searchlight stack composes on top of the lit shade via
    // the existing payload-D channel.
    //
    // Same audit-host fallback semantics as render_test.
    [[nodiscard]] static Result render_direct_lighting(
        const rr::scene::Scene& scene,
        int width, int height,
        bool enable_shadows = false) noexcept;

    // Stage 20M textured-material render. Same first-non-
    // empty-mesh selection + GAS-build path as
    // render_material_scene. Additionally:
    // - Uploads per-vertex UVs + triangle indices for the
    //   picked mesh to device buffers; threads the pointers
    //   into `OptixLaunchParams::mesh_uvs` /
    //   `mesh_indices` so the closest-hit can interpolate
    //   UVs at hit time via `optixGetTriangleBarycentrics()`.
    // - Uploads `scene.textures` to device-resident
    //   per-texture pixel buffers + builds a
    //   `rr::cuda::DeviceTextureView` array on device;
    //   threads the array pointer + count into
    //   `OptixLaunchParams::textures` /
    //   `texture_count`.
    // - Sets the SBT hit-record's `shading_mode = 1` so the
    //   closest-hit material-flat branch fires; that branch
    //   reads `useBaseColorTexture` /
    //   `baseColorTextureId` on the material and samples
    //   `textures[id]` via `rr::cuda::sampleTextureNearest`
    //   (Stage 13B.2 RR_HD inline) instead of using flat
    //   `baseColor`.
    //
    // Output: `output/optix_textured_material.ppm`.
    // Mirrors the CUDA `--render-textured-material`'s
    // Stage 13B.3 shape conceptually. No advanced filtering
    // (Stage 20M rule); nearest-neighbour only.
    //
    // Textures are passed as a separate argument because
    // `rr::scene::Scene` does not currently carry a textures
    // field; the caller is responsible for keeping the
    // ImageTexture vector alive across the call (the
    // function reads the raw byte data from each entry's
    // `pixels()` and copies it to device buffers up front).
    //
    // Same audit-host fallback semantics as render_test.
    [[nodiscard]] static Result render_textured_material(
        const rr::scene::Scene& scene,
        const std::vector<rr::texture::ImageTexture>& textures,
        int width, int height) noexcept;

    // Stage 20N AOV result. Six per-pixel passes packed into
    // host Images. The renderer owns the allocation; the
    // caller saves whichever passes it cares about via
    // `Image::save_ppm` (typically the same six files the
    // CUDA `--render-aovs` writes).
    //
    // Image formats (mirroring `rr::renderer::aov_component_count`):
    //   beauty / normal / albedo : Rgb32F (3 floats / pixel)
    //   depth / doppler_factor / searchlight_factor : Rgb32F
    //     replicated to RGB so the saved PPM is directly
    //     viewable. (CUDA path uses the host-side
    //     `save_aov_to_ppm` helper to do the same replication
    //     when downloading from `GpuAOVBuffer`.)
    struct AovResult {
        bool             ok = false;
        std::string      message;
        rr::image::Image beauty;
        rr::image::Image normal;
        rr::image::Image depth;
        rr::image::Image albedo;
        rr::image::Image doppler_factor;
        rr::image::Image searchlight_factor;
        float            gpu_time_ms = 0.0f;
    };

    // Stage 20N: render six AOV passes through the OptiX
    // direct-lighting closest-hit. Same first-non-empty-mesh
    // selection + GAS-build path as render_direct_lighting.
    // The closest-hit / miss / raygen programs write per-pixel
    // values to six device buffers (one per AOV); this entry
    // allocates the buffers, threads them through
    // `OptixLaunchParams`, runs the launch, downloads each
    // buffer into a host Image, and returns all six in one
    // `AovResult`.
    //
    // `lights` is uploaded the same way as
    // `render_direct_lighting`; pass an empty vector for
    // unlit Beauty.
    //
    // Same audit-host fallback semantics as render_test.
    [[nodiscard]] static AovResult render_aovs(
        const rr::scene::Scene& scene,
        const std::vector<rr::lighting::Light>& lights,
        int width, int height) noexcept;

    // OptiX Gap A Step 1: durable AOV buffer ownership for
    // the OptiX path. See `docs/OPTIX_GAP_A_POLISH_PLAN.md`
    // for the full motivation; the short version: the
    // existing `render_aovs(...)` allocates AOV device
    // buffers internally, downloads to host Images, and
    // frees the device buffers before returning. The
    // OptiX denoiser (Stage 21D) needs Beauty / Albedo /
    // Normal **device pointers** alive across an
    // `optixDenoiserInvoke` call; this entry returns
    // those three buffers via `rr::gpu::GpuBuffer<float>`
    // RAII handles so the caller controls lifetime.
    //
    // Behaviour:
    // - Same first-non-empty-mesh selection + GAS-build +
    //   direct-lighting closest-hit as `render_aovs`.
    // - Allocates Beauty / Albedo / Normal device buffers
    //   ONLY (depth / doppler / searchlight are skipped;
    //   the OptiX programs null-check before writing AOVs
    //   so the launch produces only the three the
    //   denoiser consumes).
    // - Does NOT download to host Images. Caller can
    //   download from `result.beauty_device.device_ptr()`
    //   etc. if a host-side copy is needed.
    // - Returns ownership of the three `GpuBuffer<float>`
    //   instances inside `AovRetainedBuffers`. The
    //   buffers stay alive until the result struct goes
    //   out of scope, at which point `GpuBuffer`'s
    //   destructor calls `cudaFree`.
    //
    // Caller protocol for the denoiser:
    //   auto r = OptixRenderer::render_aovs_retain(...);
    //   if (!r.ok) { ...handle... }
    //   OptixDenoiser::Inputs inputs;
    //   inputs.beauty_device = r.beauty_device.device_ptr();
    //   inputs.albedo_device = r.albedo_device.device_ptr();
    //   inputs.normal_device = r.normal_device.device_ptr();
    //   inputs.width  = r.width;
    //   inputs.height = r.height;
    //   inputs.beauty_components = 3;
    //   denoiser.denoise(inputs, output);  // r still owns the buffers
    //   // r goes out of scope here -> buffers freed.
    //
    // Step 1 (this slice) ships the type + declaration +
    // audit-host / OFF / SDK_FOUND stubs ONLY. The actual
    // SDK-found body (the launch + buffer retention)
    // lands in Step 2 per `OPTIX_GAP_A_POLISH_PLAN.md`.
    // Until Step 2, this method always returns
    // `ok = false` with the documented "not implemented
    // in OptiX Gap A Step 1" message.
    struct AovRetainedBuffers {
        bool                       ok = false;
        std::string                message;
        rr::gpu::GpuBuffer<float>  beauty_device;
        rr::gpu::GpuBuffer<float>  albedo_device;
        rr::gpu::GpuBuffer<float>  normal_device;
        int                        width  = 0;
        int                        height = 0;
        float                      gpu_time_ms = 0.0f;
    };

    [[nodiscard]] static AovRetainedBuffers render_aovs_retain(
        const rr::scene::Scene& scene,
        const std::vector<rr::lighting::Light>& lights,
        int width, int height) noexcept;
};

}  // namespace rr::optix

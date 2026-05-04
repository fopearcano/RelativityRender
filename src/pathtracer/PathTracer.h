#pragma once

#include "image/Image.h"
#include "math/Vec3.h"

#include <string>

// Forward declarations: PathTracer's public surface only references
// `GpuScene` and `Image` by reference / value, so we don't need to
// pull the heavy GpuScene / CUDA headers into every consumer.
namespace rr::gpu { class GpuScene; }

namespace rr::pathtracer {

// PT-P.6 soft cap on `PathTraceConfig::max_bounces`. Callers
// asking for a longer path get a single `Logger::warning` line
// from `PathTracer::render` and their bounce count clamped down
// to this value. The cap is a SUGGESTION (32 deeper bounces
// than the default 4 already produce a substantially deeper
// integration); it is not a hard ABI limit. The value lives in
// the header so dispatchers / future UI surfaces can reuse it
// when validating user input. Mirrors the OptiX backend's own
// `OPTIX_PIPELINE_MAX_TRACE_DEPTH` constraint surface.
inline constexpr int kMaxBouncesCap = 32;

// PT-P.9 soft cap on `PathTraceConfig::samples_per_pixel`.
// Callers asking for an absurdly large sample budget (e.g. a
// fat-finger error of 10000 instead of 1000) get a single
// `Logger::warning` line from `PathTracer::render` and their spp
// count clamped down to this value. The cap is a SUGGESTION
// (4096 samples produce a substantially deeper integration than
// the default 16; the cap exists primarily to catch typos at
// scene-authoring time, not to encode a hard ABI limit). Lives
// alongside `kMaxBouncesCap` so the two host-side
// `PathTraceConfig` validation prelude limits are searchable as
// a pair.
inline constexpr int kSamplesPerPixelCap = 4096;

// Configuration for one path-trace render. All fields have sensible
// defaults so callers can default-construct and only override what
// they need; the path tracer is "minimal" by design (master rule
// "Keep materials simple" + "No MIS yet").
struct PathTraceConfig {
    // Bounce budget. `max_bounces == 1` traces only the primary
    // ray (emission and environment hits are still accounted for;
    // diffuse bounces are not generated). `max_bounces == 0` is
    // accepted but produces a black image (no rays traced).
    int max_bounces = 4;

    // Sample-per-pixel count. The path-tracer host loop launches
    // the per-sample kernel this many times and accumulates the
    // results through `rr::renderer::AccumulationBuffer`. spp >= 1.
    int samples_per_pixel = 16;

    // Seed mixed into the per-pixel `pathtracer::Rng` via
    // `make_pixel_rng(x, y, sample_index, seed)`. Re-running with
    // the same seed produces a deterministic image.
    unsigned int seed = 0u;

    // Environment fallback. When a ray misses every scene primitive
    // the path tracer treats this as the radiance arriving from
    // infinity. `environment_color * environment_intensity` is the
    // emitted spectral colour; both are linear-space RGB. Defaults
    // produce a moderate cool sky tint so a scene with no emissive
    // surfaces still produces a visible image.
    //
    // PT-P.12: setting `environment_intensity == 0.0f` produces a
    // fully black background for missed rays — the kernel still
    // adds `throughput * env` to the radiance on every miss, but
    // `env` evaluates to `(0, 0, 0)` so the contribution is zero.
    // Use this when authoring scenes whose only light sources are
    // emissive surfaces / explicit lights and the operator wants
    // no background ambient term. The kernel has no `env_intensity
    // > 0` short-circuit; the multiply-and-add is unconditional,
    // and the zero-valued add is the documented contract rather
    // than a special case.
    rr::math::Vec3 environment_color     = {0.55f, 0.70f, 1.00f};
    float          environment_intensity = 0.30f;
};

// Result of a path-trace render. Mirrors the shape used by every
// other GPU diagnostic (`CudaRenderer::Result`, etc.) so call sites
// dispatch through one common pattern.
struct PathTraceResult {
    bool             ok = false;
    rr::image::Image image;     // populated only when ok == true
    std::string      message;   // populated only when ok == false
    // Stage 18A.1 GPU timing. Cumulative kernel time in
    // milliseconds across the entire spp loop (per-sample
    // path-trace kernel + per-sample accumulate kernel). 0 means
    // timing was not captured. Format via
    // `rr::gpu::format_gpu_timing_line`.
    float            gpu_time_ms = 0.0f;
};

// Minimal diffuse GPU path tracer. Stage 11C foundation; the
// algorithm is the standard "hit-shade-bounce" loop:
//
//   1. Generate a primary ray with sub-pixel jitter (RNG-driven).
//   2. Closest-hit against the scene's spheres + (single) mesh
//      slot. The triangle / sphere primitives compete for the
//      same nearest-hit slot, identical to `k_render_scene`'s
//      shape.
//   3. Add `material.emissionColor * emissionStrength` modulated
//      by the running throughput.
//   4. If we still have bounce budget, sample a cosine-weighted
//      hemisphere direction (`pathtracer::sample_cosine_hemisphere`),
//      align it with the surface normal, multiply throughput by
//      the diffuse albedo (the cosine and pi factors cancel by
//      construction for cos-weighted sampling on a Lambert BRDF),
//      and continue.
//   5. On miss, add `environment_color * environment_intensity *
//      throughput`.
//
// The kernel runs one sample per launch; the host orchestration
// (`PathTracer::render`) loops `samples_per_pixel` times and
// accumulates the per-sample radiance into a Stage 11B
// `AccumulationBuffer`, then resolves to a host Image. Lights
// (point / directional / area) are uploaded but not directly
// sampled in this slice - explicit no-MIS-yet per the prompt;
// only emissive surfaces contribute illumination.
class PathTracer {
public:
    // Run the path tracer against `scene` at the given resolution
    // with `cfg`. The scene must already have its uploads done
    // (camera, sphere array, materials, mesh slot if any). On a
    // host-only build (no `RR_HAS_CUDA`) the call returns
    // `ok = false` with a message - the master rule "All ray
    // paths on GPU" rules out a CPU fallback.
    [[nodiscard]] PathTraceResult render(const rr::gpu::GpuScene& scene,
                                         int width, int height,
                                         const PathTraceConfig& cfg) const;
};

}  // namespace rr::pathtracer

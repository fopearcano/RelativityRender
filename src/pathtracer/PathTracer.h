#pragma once

#include "image/Image.h"
#include "manifold/ManifoldMode.h"
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

    // PT-P.21 placeholder: per-channel firefly clamp on the
    // per-sample radiance. Default 0.0f disables the clamp (the
    // integrator stays unbiased; the field is currently NOT read
    // by any kernel). When > 0, a future slice will wire the
    // value through both backends' path-trace raygens so each
    // per-sample `radiance.x|y|z` is `fminf(radiance.x|y|z,
    // firefly_clamp)` before being added to the accumulator.
    //
    // Default-off rationale: clamping introduces a small
    // downward bias in scenes with high-variance light paths
    // (e.g. small bright emitters surfaced by NEE / area lights);
    // making it opt-in keeps the unbiased integrator the
    // canonical baseline.
    //
    // Wiring is deferred per
    // `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_TASK.md` (PT-P.20):
    // the CUDA path-trace kernel + the OptiX
    // `__raygen__pathtrace` need a paired update so the two
    // backends' outputs remain convergent at non-zero clamp;
    // landing one without the other would silently diverge them.
    // PT-P.21 ships ONLY this field + its doc-comment so the
    // future kernel-wiring slice has a stable forward-compatible
    // anchor to attach itself to.
    float firefly_clamp = 0.0f;

    // NEE.2 skeleton: enable explicit direct-light sampling
    // (Next Event Estimation) at every bounce vertex. Default
    // `false` keeps the path tracer emission + environment-
    // only — the kernel-side guard
    // `if (enable_nee && light_count > 0)` is not entered, no
    // shadow ray is traced, no extra RNG draw is performed,
    // and the per-pixel arithmetic is byte-identical with
    // the pre-NEE build. The byte-identity invariant is
    // verified statically (the guard is the only consumer of
    // the field; both halves are zero at default) and
    // dynamically (the existing fixture goldens are
    // unchanged by this slice).
    //
    // When `true`, the CUDA path-trace kernel
    // (`k_pathtrace_sample` in `CudaPathTracer.cu`) picks
    // one light uniformly per bounce vertex via
    // `pathtracer::sample_direct_light_uniform`
    // (`pathtracer/DirectLight.cuh`), traces an any-hit
    // shadow ray, and adds the visibility-modulated, BRDF-
    // modulated, throughput-modulated contribution to the
    // running radiance. Supported light types are
    // `LightType::Point` and `LightType::Directional`;
    // `LightType::Area` and `LightType::Environment` are
    // PLACEHOLDER per `Light.h:20-31` and contribute zero
    // through the NEE branch.
    //
    // OptiX backend status (post-NEE.4): the OptiX path-
    // trace raygen now mirrors the CUDA NEE branch via
    // `OptixLaunchParams::enable_nee` and the trailing
    // `bool enable_nee` argument on
    // `OptixRenderer::render_pathtrace*`. The two backends
    // are convergence-equivalent at every value of
    // `enable_nee` (both off: byte-identical to the pre-
    // NEE build; both on: same Lambert-BRDF + cosine +
    // throughput-modulated direct-light contribution from
    // the same `sample_direct_light_uniform` helper, the
    // same Stage 20L `__miss__shadow` SBT record reused for
    // visibility, and the same Point + Directional light-
    // type scope). `PathTraceConfig::enable_nee` flows
    // through `launch_pathtrace_sample` for the CUDA path;
    // the OptiX dispatcher takes `enable_nee` as a separate
    // argument because it does not consume `PathTraceConfig`
    // directly. A caller that drives both backends from a
    // single `PathTraceConfig` should pass `cfg.enable_nee`
    // to both APIs; the CLI flag that does this is reserved
    // for NEE.5 (`--enable-nee`).
    //
    // Default `enable_nee == false` continues to keep both
    // backends byte-identical with the pre-NEE build — the
    // §5.5 atomicity-equivalent invariant from
    // `docs/PATH_TRACER_NEE_AUDIT.md` §3.2 is upheld.
    //
    // No MIS yet. v1 sums the existing emission term and
    // the new NEE term naively. The "no double-count
    // window" argument from
    // `docs/PATH_TRACER_NEE_TASK.md` §1 holds because
    // Point + Directional lights have no mesh — the
    // emission term and the NEE term sample disjoint
    // contributions for the v1 light-type scope. MIS is
    // reserved for the future area-light slice, where
    // double-counting becomes real.
    //
    // See `docs/PATH_TRACER_NEE_TASK.md` for the full
    // design contract; the current slice (NEE.2 — CUDA NEE
    // skeleton) ships only the CUDA-side helper shells and
    // the guarded call site so the wiring compiles end-to-
    // end. No caller passes `true` today.
    bool enable_nee = false;

    // MANI-I.3 — per-render Manifold Core mode the renderer
    // will eventually consult to decide *how* the Manifold
    // Core engages (see
    // `docs/MANIFOLD_INTEGRATION_PLAN.md` §5). Plumbed from
    // `rr::core::Config::manifold` (shipped at MANI-I.1) into
    // every `PathTraceConfig` construction site in
    // `src/main.cpp`. Default
    // `disabled_manifold_mode()` (`enabled = false`,
    // `chart = Euclidean`, `strength = 0`, `debug = off`)
    // preserves the pre-pivot renderer output bit-for-bit;
    // MANI-I.3 ships only the field + host-side echo log line,
    // no kernel consumption — the CUDA path-trace kernel and
    // the OptiX `__raygen__pathtrace` program continue to
    // ignore this field. MANI-I.4 is the first GPU touch.
    rr::manifold::ManifoldMode manifold;
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

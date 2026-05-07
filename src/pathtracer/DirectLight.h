#pragma once

// NEE.2 skeleton — direct-light sample data structure.
//
// One Monte Carlo sample drawn from the scene's `Light` array
// at a path-trace bounce vertex. The kernel multiplies the
// (unattenuated) light radiance by the BRDF, the running
// throughput, the cosine to the surface normal, and a binary
// shadow-ray visibility, and adds the product to the per-
// sample radiance accumulator.
//
// The struct is plain data so the helper that produces it
// (`pathtracer/DirectLight.cuh::sample_direct_light_uniform`)
// can be RR_HD inline and host tests can exercise the same
// code the GPU kernel runs.
//
// NEE.2 is the CUDA-side skeleton: the helper that returns
// this POD is implemented and invoked by the path-trace
// kernel ONLY when `PathTraceConfig::enable_nee == true`.
// The PathTraceConfig default (`enable_nee == false`) keeps
// the kernel-side branch un-entered, no helper is invoked,
// no shadow ray is traced, and the per-pixel arithmetic is
// byte-identical with the pre-NEE build. See
// `docs/PATH_TRACER_NEE_TASK.md` for the full design.

#include "math/Vec3.h"

namespace rr::pathtracer {

// One uniform-by-count direct-light sample.
//
// Field meanings:
//
//   `wi`              World-space unit direction FROM the hit
//                     vertex TO the light. For Point lights:
//                     `normalize(light.position - hit_position)`.
//                     For Directional lights: `-normalize(
//                     light.direction)`.
//
//   `distance`        Shadow-ray `t_max` for the visibility
//                     query. For Point lights this is the
//                     Euclidean distance to the light position
//                     (the kernel subtracts a small epsilon to
//                     avoid self-intersection at the light end).
//                     For Directional lights this is a large
//                     finite sentinel (`kDirectionalShadowTMax`)
//                     so the shadow ray runs out to "infinity".
//
//   `li_unattenuated` Incoming spectral radiance arriving at
//                     the receiver from the sampled light,
//                     pre-BRDF, pre-cosine, pre-throughput,
//                     pre-visibility. For Point lights this
//                     already includes the `1/r²` falloff
//                     (`color * intensity / r²`). For
//                     Directional lights this is just `color *
//                     intensity` (no falloff; directional
//                     sources are at infinity).
//
//   `pdf_inv`         Inverse PDF of the light *selection*
//                     step. With uniform-by-count selection
//                     this is `light_count`. The kernel
//                     multiplies the contribution by `pdf_inv`
//                     so the estimator is unbiased across light
//                     counts. A return value of `0.0f` is the
//                     "this sample contributes zero" signal:
//                     `lights == nullptr`, `count <= 0`, the
//                     picked light is a PLACEHOLDER type
//                     (Area / Environment), or the light is
//                     behind the receiver (`cos_theta <= 0`).
//
// All four fields default to "no contribution" so a default-
// constructed sample naturally adds zero to the radiance
// accumulator.
struct DirectLightSample {
    rr::math::Vec3 wi              = {0.0f, 0.0f, 0.0f};
    float          distance        = 0.0f;
    rr::math::Vec3 li_unattenuated = {0.0f, 0.0f, 0.0f};
    float          pdf_inv         = 0.0f;
};

}  // namespace rr::pathtracer

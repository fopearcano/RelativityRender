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
//   `pdf_solid_angle` (MIS.3) Per-steradian directional PDF of
//                     choosing the returned `wi`. Read by the
//                     future MIS-aware integrator
//                     (`pathtracer::power_heuristic`, MIS.4) to
//                     weight the NEE-side estimator against the
//                     BSDF-side estimator. For Point and
//                     Directional lights at v1 this is a Dirac
//                     delta — the field carries the sentinel
//                     `0.0f` and the MIS helper short-circuits
//                     on `is_delta == true` without reading
//                     this value. For future area lights this
//                     carries the area-to-solid-angle Jacobian
//                     `(1/light_count) · (1/area) · r² /
//                     cos(theta_light)` and `is_delta == false`.
//                     Default `0.0f` matches the bit-zero
//                     "no contribution" convention the four
//                     pre-existing fields share.
//
//   `is_delta`        (MIS.3) `true` iff the light is a Dirac
//                     delta in direction (Point or Directional);
//                     `false` for finite-PDF lights (future
//                     area / IBL). The MIS helper checks this
//                     flag FIRST: on `true` the NEE-side
//                     estimator gets MIS weight = 1.0 (Veach
//                     1995 §10.3 delta-light convention), and
//                     the BSDF-bounce-as-light contribution to
//                     this light is zero (zero measure). On
//                     `false` the helper computes the power
//                     heuristic from `pdf_solid_angle`.
//                     Default `false` flows through every
//                     "no contribution" branch unchanged so a
//                     default-constructed `DirectLightSample`
//                     remains the bit-zero "no contribution"
//                     sentinel.
//
// All six fields default to "no contribution" so a default-
// constructed sample naturally adds zero to the radiance
// accumulator. **The default-constructed bit pattern is also
// preserved across the MIS.3 field addition** (both new fields
// have bit-zero defaults: `pdf_solid_angle = 0.0f` and
// `is_delta = false`), which keeps the NEE.5 byte-identity
// anchor at `tests/pathtracer_nee_tests.cpp::test_zero_
// contribution_is_bit_default` passing without modification.
//
// MIS.3 ships ONLY the data-model extension. No caller reads
// `pdf_solid_angle` or `is_delta` in this slice; the existing
// `pdf_inv`-based NEE arithmetic in both backends'
// integrators is unchanged. The MIS-aware integrator slices
// (MIS.5 CUDA, MIS.6 OptiX) consume both new fields. See
// `docs/PATH_TRACER_MIS_LIGHT_PDF_TASK.md` for the canonical
// per-field contract + `docs/PATH_TRACER_MIS_PLAN.md` for the
// arc-level design.
struct DirectLightSample {
    rr::math::Vec3 wi              = {0.0f, 0.0f, 0.0f};
    float          distance        = 0.0f;
    rr::math::Vec3 li_unattenuated = {0.0f, 0.0f, 0.0f};
    float          pdf_inv         = 0.0f;
    // MIS.3 additions: directional PDF + delta-light flag for
    // future MIS-aware integrator consumption (MIS.5 CUDA,
    // MIS.6 OptiX). Inert in this slice — no caller reads
    // these fields. Bit-zero defaults preserve the NEE.5
    // byte-identity anchor.
    float          pdf_solid_angle = 0.0f;
    bool           is_delta        = false;
};

}  // namespace rr::pathtracer

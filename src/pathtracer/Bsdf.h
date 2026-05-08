#pragma once

// MIS.2 — BSDF sample data structure.
//
// One Monte Carlo sample drawn from the BSDF sampler at a
// path-trace bounce vertex. Captures the chosen outgoing
// direction + the BRDF value evaluated at that direction +
// the sampler's PDF + a validity flag the integrator gates
// on.
//
// MIS.2 ships ONLY the data type. No sampler / PDF / eval
// helper is shipped in this slice (those land in a future
// slice that adds `Bsdf.cuh` alongside this header). No
// caller invokes the type yet; both path-trace integrators
// (`k_pathtrace_sample` in `src/cuda/CudaPathTracer.cu` and
// `__raygen__pathtrace` in `src/optix/OptixPrograms.cu`)
// continue to use their existing inline cosine-bounce
// arithmetic. **No rendering behaviour change.**
//
// The contract this slice ships is forward-compatible with
// the planned MIS arc (per
// `docs/PATH_TRACER_MIS_PLAN.md`). The MIS arc closes the
// double-count window between Next Event Estimation
// (NEE.{1..6}) and BSDF sampling that opens when area
// lights land. At v1 (Point + Directional only) the window
// has zero measure under BSDF sampling — delta lights are
// never reached by random bounce — so MIS at v1 is a no-op
// and byte-identity with the post-NEE-arc baseline is
// preserved by construction. The data model + helpers ship
// now so the future area-light arc can flip the area-light
// flag without architectural rework.
//
// MIS.5 + MIS.6 (the future integrator slices on the CUDA
// and OptiX backends respectively) will consume
// `BsdfSample` like this (paraphrased; final shape lives
// in those slices):
//
//     const BsdfSample s = sample_bsdf(material, wi, normal, u);
//     if (!s.valid) {
//         // Defensive: skip the bounce if the sampler
//         // produced a degenerate sample. Integrator does
//         // NOT consume `s.wo` / `s.pdf` / `s.value` when
//         // valid == false (they may carry sentinel
//         // values).
//         break;
//     }
//
//     // Throughput update for the bounce. Cancels
//     // analytically for cosine-weighted Lambert:
//     //     (baseColor/pi · cos_theta_o) / (cos_theta_o/pi)
//     //   = baseColor
//     // — which is exactly the existing inline
//     // `throughput *= m.baseColor` arithmetic both
//     // integrators use today. The MIS arc preserves this
//     // simplification's bit-result at v1.
//     const float cos_theta_o = max(0, dot(normal, s.wo));
//     throughput *= (s.value * cos_theta_o) / s.pdf;
//
//     // MIS weight on the BSDF-bounce-as-light estimator.
//     // Computed from `s.pdf` and the corresponding NEE
//     // light-PDF at the same direction. At v1 (delta
//     // lights only), the MIS helper short-circuits and
//     // the weight is exactly 1.0 for diffuse / 0.0 for
//     // delta-light overlap (which has zero measure
//     // anyway).
//     const float w_bsdf = power_heuristic(s.pdf, p_light_at_wo);
//     // ... continues into the integrator's MIS-aware
//     // accumulation.
//
// See `docs/PATH_TRACER_MIS_BSDF_PDF_TASK.md` §2 for the
// canonical per-field semantics + the §5 PASS-criterion
// list the helper-host tests will anchor.

#include "math/Vec3.h"

namespace rr::pathtracer {

// One Monte Carlo sample of a BSDF bounce direction.
//
// Default-constructed instance is the "no-contribution"
// sentinel: `valid == false` short-circuits the
// integrator, matching `DirectLightSample{}`'s
// `pdf_inv == 0.0f` short-circuit on the NEE side. Both
// shapes are bit-zero by default so a future regression
// that introduces non-zero default state would fail the
// host-only memcmp anchor that matches the NEE.5 pattern
// at `tests/pathtracer_nee_tests.cpp::
// test_zero_contribution_is_bit_default`.
//
// Field semantics:
//
//   `wo`     World-space unit direction the BSDF sampler
//            chose for the bounce. Lies in the upper
//            hemisphere with respect to the surface
//            normal at the hit point (`dot(normal, wo) >
//            0` whenever `valid == true`). For Lambert,
//            produced by aligning a tangent-space
//            `sample_cosine_hemisphere` sample to the
//            world-space normal via the same tangent-
//            frame algorithm the existing CUDA + OptiX
//            integrators use inline. Default
//            `(0, 0, 0)` is the non-direction sentinel
//            consumed only when `valid == true`.
//
//   `value`  RGB BRDF value evaluated at the (incoming,
//            outgoing) direction pair, in radiance-per-
//            steradian units. For Lambert: `baseColor /
//            pi` — independent of `wi` and `wo` modulo
//            the `cos_theta_o > 0` gate the BSDF
//            implicitly applies. The integrator
//            multiplies `value` by `cos_theta_o` and
//            divides by `pdf` for the per-bounce
//            throughput update; the cancellation
//            simplifies to `baseColor` for Lambert
//            (see the doc-comment block above for the
//            byte-identity argument).
//
//   `pdf`    BSDF probability density (per steradian)
//            of the sampler choosing direction `wo`.
//            Used by the MIS helper (a future slice's
//            `pathtracer::power_heuristic`) to compute
//            the MIS weight on the BSDF-sampler
//            estimator's contribution. For Lambert:
//            `max(0, cos_theta_o) / pi`, matching the
//            existing `pathtracer::pdf_cosine_hemisphere`
//            helper at `pathtracer/Sampling.h`. A
//            `pdf == 0.0f` sample is degenerate (e.g.
//            below-horizon `wo` from a numerically
//            edge-case alignment); the integrator
//            relies on `valid` rather than `pdf > 0` to
//            gate consumption, but the zero default
//            keeps the shape consistent with
//            `DirectLightSample::pdf_inv`.
//
//   `valid`  `true` iff the sampler produced a usable
//            sample with non-zero contribution. The
//            integrator checks this flag BEFORE
//            consuming any other field; when `valid ==
//            false` the integrator skips the bounce and
//            terminates the path early. Possible
//            `valid == false` cases (any future BSDF):
//            (a) sampled direction below the surface
//            horizon (`cos_theta_o <= 0`; never
//            produced by cosine-hemisphere sampling
//            against a healthy normal, but defence-in-
//            depth guards numerical edges); (b) BSDF
//            value identically zero (e.g. a future
//            opaque material's transmission lobe at
//            zero transmission); (c) sampler PDF is
//            zero (degenerate). Default `false` makes
//            the default-constructed sample a safe
//            no-op in any caller that forgets to
//            populate the struct.
//
// MIS.2 (this slice) ships only the POD. The sampler
// (`sample_bsdf`), the PDF evaluator
// (`bsdf_pdf`), and the BRDF eval (`bsdf_eval`) are
// reserved for a follow-up slice that adds
// `pathtracer/Bsdf.cuh`. The integrator wiring
// (`k_pathtrace_sample`, `__raygen__pathtrace`) lands
// at MIS.5 (CUDA) and MIS.6 (OptiX). Until those slices
// land, no caller invokes this type and the path
// tracer's per-pixel arithmetic is byte-identical with
// the post-NEE-arc baseline at commit `827f5de`.
struct BsdfSample {
    rr::math::Vec3 wo    = rr::math::Vec3{0.0f, 0.0f, 0.0f};
    rr::math::Vec3 value = rr::math::Vec3{0.0f, 0.0f, 0.0f};
    float          pdf   = 0.0f;
    bool           valid = false;
};

}  // namespace rr::pathtracer

#pragma once

// MIS.2 — BSDF sampling + PDF evaluation helpers (Lambert).
//
// Re-exports the `BsdfSample` data type from `Bsdf.h` plus
// three RR_HD inline helpers consumed by the future MIS.5
// (CUDA) and MIS.6 (OptiX) integrator slices:
//
//   sample_bsdf(material, wi, normal, u)
//       Produce one Monte Carlo sample of a bounce
//       direction with its PDF + BRDF value + valid flag.
//       For Lambert: cosine-weighted hemisphere sample
//       aligned to the world-space normal.
//
//   bsdf_pdf(material, wo, normal)
//       Evaluate the BSDF PDF (per steradian) at an
//       arbitrary outgoing direction `wo`. For Lambert:
//       max(0, cos_theta_o) / pi. Used by the MIS helper
//       (a future slice) to compute the BSDF-side weight
//       on an NEE-chosen direction.
//
//   bsdf_eval(material, wi, wo, normal)
//       Evaluate the BRDF value at (wi, wo). For Lambert:
//       baseColor / pi (rotation-invariant); the integrator
//       multiplies by `cos_theta_o` separately. Used by
//       the NEE branch (Lambert evaluated at the toward-
//       light direction) and by the MIS-aware integrator's
//       BSDF-bounce-as-light contribution.
//
// MIS.2 ships these helpers but does NOT wire them into
// the path-trace integrators. Both `k_pathtrace_sample`
// (CUDA) and `__raygen__pathtrace` (OptiX) continue to use
// their existing inline cosine-bounce arithmetic; MIS.5 +
// MIS.6 replace those inline copies with calls to these
// helpers. **No rendering behaviour change in this slice.**
//
// Module split mirrors `pathtracer/DirectLight.{h,cuh}`:
// `Bsdf.h` carries the POD (`BsdfSample`); `Bsdf.cuh`
// provides the helpers. Both are RR_HD inline so the same
// code path the GPU kernels run is also exercised by the
// host-only tests in `tests/pathtracer_bsdf_tests.cpp`.
//
// See `docs/PATH_TRACER_MIS_PLAN.md` §3.1 + §3.5 + §5.1
// and `docs/PATH_TRACER_MIS_BSDF_PDF_TASK.md` §2 for the
// canonical contract this module implements.

#include "material/MaterialTypes.h"
#include "math/MathUtils.h"          // RR_HD + kInvPi
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "pathtracer/Bsdf.h"         // BsdfSample POD
#include "pathtracer/Sampling.h"     // sample_cosine_hemisphere + pdf_cosine_hemisphere

namespace rr::pathtracer {

namespace detail {

// Build a tangent frame from a world-space normal and
// transform a tangent-space direction `local` into world
// space. Mirrors the existing inline `align_to_normal`
// implementations in
// `src/cuda/CudaPathTracer.cu` (lambda) and
// `src/optix/OptixPrograms.cu::pt_align_to_normal`
// (line 790-799). Centralising the algorithm in this
// header lets future MIS.5 / MIS.6 slices replace the
// inline copies with a single helper call without
// changing the produced direction.
//
// Caller guarantees `dot(n, n) > 0` (non-degenerate
// normal). The frame's parity is unspecified beyond the
// requirement that `(t, b, n)` form an orthonormal basis
// with `n` as the up axis; this is sufficient for
// cosine-weighted sampling because the resulting
// world-space direction's `dot(wo, n)` equals
// `local.z` exactly.
RR_HD inline rr::math::Vec3 align_to_normal(
        rr::math::Vec3 local, rr::math::Vec3 n) {
    using rr::math::Vec3;
    using rr::math::cross;
    using rr::math::normalize;
    const Vec3 helper = (fabsf(n.z) < 0.999f)
                      ? Vec3{0.0f, 0.0f, 1.0f}
                      : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 t = normalize(cross(helper, n));
    const Vec3 b = cross(n, t);
    return t * local.x + b * local.y + n * local.z;
}

}  // namespace detail

// Sample one Lambert bounce direction.
//
// `wi` is the toward-camera direction (currently unused;
// Lambert is rotation-invariant and the cosine-weighted
// sampler does not consult the incoming direction). Future
// non-Lambert BSDFs (GGX metal, dielectric, glass) will
// consume `wi`; the parameter ships now so the future
// helpers can drop in without changing the integrator's
// call shape.
//
// `normal` must be a unit vector pointing AWAY from the
// surface (toward the upper hemisphere). A degenerate
// normal (`dot(n, n) <= 0`) returns a default-constructed
// (`valid == false`) sample; the integrator must check
// the flag before consuming any other field.
//
// `u` is a uniform-[0, 1)² random sample (typically from
// `rr::pathtracer::next_vec2(rng)`).
RR_HD inline BsdfSample sample_bsdf(
        const rr::material::MaterialParams& m,
        rr::math::Vec3 /*wi*/,
        rr::math::Vec3 normal,
        rr::math::Vec2 u) {
    BsdfSample s;  // default-constructed: valid = false

    // Defence-in-depth: degenerate normal short-circuits.
    const float n_sq = rr::math::dot(normal, normal);
    if (n_sq <= 0.0f) {
        return s;
    }

    // Cosine-weighted hemisphere sample in tangent space.
    // The resulting `local.z` IS the cos_theta_o angle to
    // the local normal (== world normal after alignment)
    // by construction.
    const rr::math::Vec3 local = sample_cosine_hemisphere(u);
    const float cos_theta_o = local.z;

    // Below-horizon defence (numerical edge at the equator
    // where cosine-weighted sampling produces ~0
    // cos_theta_o). The cosine-hemisphere sampler is
    // designed to never produce z < 0 on a healthy normal,
    // but the strict `<=` gate covers the z == 0 case
    // (would produce a divide-by-zero in the integrator's
    // throughput update).
    if (cos_theta_o <= 0.0f) {
        return s;
    }

    s.wo    = detail::align_to_normal(local, normal);
    s.pdf   = pdf_cosine_hemisphere(cos_theta_o);  // cos_theta_o / pi
    s.value = m.baseColor * rr::math::kInvPi;      // Lambert BRDF
    s.valid = true;
    return s;
}

// Evaluate the BSDF PDF at an arbitrary outgoing
// direction `wo`. Returns 0 for any `wo` below the
// surface horizon; otherwise returns the cosine-
// hemisphere PDF.
//
// This helper must be CONSISTENT with `sample_bsdf` in
// the sense that for any `(material, normal, u)` and
// the resulting `wo = sample_bsdf(...).wo`, the equality
// `BsdfSample::pdf == bsdf_pdf(material, wo, normal)`
// holds bit-exactly. The helper-host test
// `test_lambert_pdf_matches_sampler` anchors this
// invariant via `std::memcmp` on the float bit pattern.
RR_HD inline float bsdf_pdf(
        const rr::material::MaterialParams& /*m*/,
        rr::math::Vec3 wo,
        rr::math::Vec3 normal) {
    const float cos_theta_o = rr::math::dot(normal, wo);
    if (cos_theta_o <= 0.0f) {
        return 0.0f;
    }
    return pdf_cosine_hemisphere(cos_theta_o);
}

// Evaluate the BRDF value at the (incoming, outgoing)
// direction pair. Lambert is rotation-invariant: the
// returned value is `baseColor / pi` regardless of the
// directions or the normal. The integrator multiplies
// the result by `cos_theta_o` separately for the
// throughput update; the eval itself does not gate on
// geometry. This matches the convention the existing
// inline `m.baseColor * kInvPi` arithmetic in both
// integrators uses.
RR_HD inline rr::math::Vec3 bsdf_eval(
        const rr::material::MaterialParams& m,
        rr::math::Vec3 /*wi*/,
        rr::math::Vec3 /*wo*/,
        rr::math::Vec3 /*normal*/) {
    return m.baseColor * rr::math::kInvPi;
}

}  // namespace rr::pathtracer

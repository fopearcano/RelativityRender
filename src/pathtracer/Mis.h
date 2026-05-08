#pragma once

// MIS.4 — Multiple Importance Sampling helper.
//
// Single RR_HD inline helper computing the Veach 1995
// §9.2.4 power heuristic with β = 2 for two
// estimators with one sample each. Pure scalar math;
// no POD; no CUDA-specific intrinsics. Mirrors the
// `pathtracer/Sampling.h` shape (host/device-shared
// pure-math helpers in a single `.h` file).
//
// MIS.4 ships ONLY the helper — no caller invokes it
// in this slice. The future MIS.5 (CUDA integrator)
// and MIS.6 (OptiX integrator) slices wire it into
// the path-trace bounce loop's NEE branch +
// BSDF-bounce-as-light contribution. **No rendering
// behaviour change in this slice.**
//
// At v1 (Point + Directional lights only) the future
// MIS-aware integrator will short-circuit
// `is_delta == true` BEFORE calling
// `power_heuristic` (the NEE-side weight is 1.0 for
// delta lights per Veach §10.3 by convention; the
// BSDF-side contribution to delta lights is zero with
// probability 1). The Dirac short-circuit logic
// lives at the caller; this helper is pure math.
//
// See `docs/PATH_TRACER_MIS_PLAN.md` §3.3 for the
// arc-level rationale + `docs/PATH_TRACER_MIS_POWER_
// HEURISTIC_TASK.md` §2 for the per-edge-case
// contract.

#include "math/MathUtils.h"  // RR_HD

namespace rr::pathtracer {

// Veach 1995 §9.2.4 power heuristic with β = 2.
// Returns the MIS weight on the FIRST estimator's
// contribution given two estimators with one sample
// each:
//
//                       p_a²
//     w_a(p_a, p_b) = ─────────
//                     p_a² + p_b²
//
// `p_a` and `p_b` are the two estimators' PDFs at
// the same sampled direction, in the SAME UNITS
// (per steradian for direct lighting; the MIS arc
// uses sr⁻¹ end-to-end). Caller is responsible for
// ensuring both inputs are non-negative finite
// floats; NaN / inf inputs are out-of-contract and
// produce undefined results.
//
// The B-estimator's MIS weight is symmetric:
// `w_b = power_heuristic(p_b, p_a) = p_b² / (p_a²
// + p_b²)`. Caller computes both weights by
// invoking the helper twice with swapped
// arguments. Sum-to-one invariant: `w_a + w_b ≈
// 1.0f` to within ~1 ULP for any non-zero pair.
//
// Edge-case behaviour (per the §2.1 stable-zero
// contract):
//
//   `(0, 0)`         ⇒ `0.0f` (denominator-zero
//                      guard; the natural `0/0`
//                      would be NaN).
//   `(0, p_b > 0)`   ⇒ `0.0f` (numerator zero;
//                      the A-estimator can't
//                      sample this direction).
//   `(p_a > 0, 0)`   ⇒ `1.0f` (denominator
//                      collapses to `p_a²`;
//                      ratio is exactly 1).
//   `(x, x), x > 0`  ⇒ `0.5f` (equal estimators
//                      get equal weight).
//
// Properties (anchored by host-only tests in
// `tests/pathtracer_mis_tests.cpp`):
//
//   - Range: `[0.0f, 1.0f]` for any non-negative
//     inputs.
//   - Symmetry: `w_a + w_b ≈ 1.0f`.
//   - Monotonicity: `w_a` is non-decreasing in
//     `p_a` (for fixed `p_b`).
//   - One-dominates limit: as `p_a / p_b → ∞`,
//     `w_a → 1.0f`.
//   - Pure / deterministic: identical inputs ⇒
//     bit-equal outputs (no hidden state).
//
// Caller-responsibility note: at v1 (Point +
// Directional lights), the MIS-aware integrator
// MUST check the `DirectLightSample::is_delta`
// flag (MIS.3) BEFORE calling this helper. For
// `is_delta == true`, the NEE-side weight is 1.0
// (Veach §10.3 delta-light convention), and the
// helper is NOT invoked — the v1 light-side
// `pdf_solid_angle` field carries a sentinel
// `0.0f` value that would yield `w_NEE = 0` if
// passed to the helper, silently corrupting the
// integrator's MIS-aware estimator. Documented
// here so a future caller does not regress the
// short-circuit pattern.
RR_HD inline float power_heuristic(float p_a, float p_b) {
    const float pa2   = p_a * p_a;
    const float pb2   = p_b * p_b;
    const float denom = pa2 + pb2;
    return denom > 0.0f ? pa2 / denom : 0.0f;
}

}  // namespace rr::pathtracer

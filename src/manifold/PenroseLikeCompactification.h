#pragma once

// Penrose-like artistic coordinate-compactification math
// helpers. See `docs/PENROSE_LIKE_COMPACTIFICATION_PLAN.md`
// for the canonical design; this header is the PENROSE.2
// leaf in the PENROSE.* sub-slice ladder (PENROSE.1 was
// the planning slice).
//
// The helpers are **artistic, not physical**: they
// approximate the visual signature of conformal
// compactification (mapping asymptotic infinity onto a
// finite bounded chart radius) without performing a real
// conformal compactification of a Lorentzian metric. The
// architecture-doc §8 non-goals "physically exact Kerr
// ray tracing" and "full GR solver" stay in force; the
// *Like naming convention from MANIFOLD.1 flags the
// artistic-not-physical status at every call site. There
// is no conformal factor; the spacetime metric is not
// rescaled; light-like geodesics are NOT preserved as 45°
// lines in the chart.
//
// Closed-form math, RR_HD inline throughout, callable
// from both host and device code. The plan's §6 safety
// invariants are enforced by construction:
//
//   - **Bounded transforms.** `r_chart = R_max *
//     tanh(strength * (r / scale)^falloff)` is bounded
//     in `[0, R_max]` by the natural saturation of
//     `tanh`. For `r → ∞`, `r_chart → R_max` exactly in
//     IEEE-754 single precision (tanh saturates at
//     `1.0f` for `|x| > ~16`).
//
//   - **No NaN / Inf.** `scale > 0` and `R_max > 0`
//     enforced by `penrose_like_validate_params(...)`;
//     `falloff` clamped to `[0.5, 4.0]`; `strength`
//     rejected if non-finite. The forward map never
//     evaluates `pow` of a negative base or `1 / 0`.
//
//   - **Euclidean fallback.** `strength = 0` or
//     `R_max = 0` returns the input unchanged
//     (defensive short-circuit; the natural math at
//     `strength = 0` would shrink everything to the
//     origin via `r_chart = R_max * tanh(0) = 0`,
//     which is NOT identity — the explicit
//     short-circuit returns `p_world` instead).
//
//   - **Reversible (analytical).** The `_chart_to_world`
//     helper inverts via `atanh` (closed-form, NOT
//     Newton-Raphson). Inverse residual `≤ 1e-6` for
//     `r_chart < R_max * (1 - kBoundaryEpsilon)`.
//     Strictly at `r_chart = R_max`, `atanh` is `+∞`;
//     the helper clamps `r_chart` to `R_max * (1 -
//     kBoundaryEpsilon)` before inversion to guarantee
//     a finite output.
//
//   - **Bit-identity on the Euclidean off-path.** The
//     PENROSE.* integration slices gate these helpers
//     behind `is_active(manifold_mode) && chart ==
//     PenroseLike` so the existing pre-pivot code path
//     stays unchanged when the chart family is
//     `Euclidean` or anything other than
//     `PenroseLike`.
//
//   - **Defence-in-depth.** Every helper calls
//     `penrose_like_validate_params(...)` before
//     evaluating the formula; on invalid input the
//     helper returns the documented fallback (input
//     pass-through, treated as Euclidean).
//
// PENROSE.2 scope: this header only. No
// `ManifoldTransform.h` change yet (PENROSE.3); no
// kernel code change (PENROSE.4 / PENROSE.5); no AOV
// encoding change (PENROSE.6); no audit doc (PENROSE.7).
// The header compiles cleanly on its own under
// `g++ -std=c++20 -Isrc -Wall -Wextra -Werror`.

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"

#include <cmath>

namespace rr::manifold {

// Per-slice parameter bag for the Penrose-like chart.
// Mirrors the plan §3 parameter mapping (the fields
// hosted on `CoordinateChart` and
// `CoordinateChartParameters` are reinterpreted per
// chart family; this struct gives the PenroseLike math
// its own named fields without an ABI bump on the
// chart POD).
//
//   - `r_max`     := bounded chart-radius (the
//                    asymptotic compactification
//                    boundary). Pixels with `r → ∞`
//                    saturate at this radius.
//                    Range `[0, ∞)`; default `0` is
//                    the Euclidean fallback regardless
//                    of `strength`.
//                    Sourced from
//                    `CoordinateChartParameters::mass`
//                    when the host builds these params
//                    from a `CoordinateChart`.
//
//   - `strength`  := interpolation factor between
//                    identity (`0`) and full
//                    compactification (`1`). Nominal
//                    range `[0, 1]`; out-of-range
//                    values pass through per
//                    `ManifoldMode::strength`'s
//                    contract (this helper does NOT
//                    clamp the dial). `0` is the
//                    Euclidean fallback.
//
//   - `scale`     := compactification "knee" radius.
//                    Smaller values produce more
//                    aggressive compactification
//                    (sharper boundary); larger values
//                    produce gentler far-field
//                    compression. Range `(0, ∞)`;
//                    default `1.0`.
//                    Sourced from
//                    `CoordinateChartParameters::
//                    compactification_scale` —
//                    the CANONICAL named use of that
//                    field per MANIFOLD.1.
//
//   - `falloff`   := exponent applied to `r / scale`
//                    before the `tanh`. Controls the
//                    transition curvature: `falloff
//                    > 1` produces a sharper knee;
//                    `falloff < 1` produces a softer
//                    knee. Range `[0.5, 4.0]`;
//                    default `1.0`.
//                    Sourced from
//                    `CoordinateChartParameters::spin`
//                    (per the plan §3 reinterpretation
//                    table; parallel to
//                    SchwarzschildLike's reuse of the
//                    same slot for its own falloff).
struct PenroseLikeCompactificationParams {
    float r_max    = 0.0f;
    float strength = 0.0f;
    float scale    = 1.0f;
    float falloff  = 1.0f;
};

// Validate parameter ranges per the plan §6.5 host-side
// validator. Returns `true` when every parameter is
// finite AND inside its documented range:
//
//   - `r_max >= 0` and finite;
//   - `strength` finite (out-of-nominal-range values
//     pass through per the `ManifoldMode::strength`
//     contract; only NaN / Inf are rejected here);
//   - `scale > 0` and finite;
//   - `falloff` finite AND in `[0.5, 4.0]`.
//
// On `false`, callers fall back to the Euclidean
// default (input pass-through) per the plan §6.3.
RR_HD inline bool penrose_like_validate_params(
        const PenroseLikeCompactificationParams& p) {
    if (!std::isfinite(p.r_max) || p.r_max < 0.0f) return false;
    if (!std::isfinite(p.strength))                return false;
    if (!std::isfinite(p.scale) || p.scale <= 0.0f) return false;
    if (!std::isfinite(p.falloff) ||
        p.falloff < 0.5f || p.falloff > 4.0f)      return false;
    return true;
}

// Boundary clamp epsilon for the analytical inverse.
// Used to prevent `atanh(r_chart / r_max)` from
// evaluating at exactly `1.0`, which would produce
// `+∞`. With `kBoundaryEpsilon = 1e-6f`, the inverse
// caps at `atanh(1 - 1e-6) ≈ 7.6`, well within float
// range; the maximum reconstructable `r` is
// `scale * (7.6 / strength)^(1 / falloff)`, which
// for typical parameters is much larger than any
// reasonable scene scale.
namespace detail {
constexpr float kBoundaryEpsilon = 1.0e-6f;
}

// Forward map: world-space position → chart-space
// position. Closed-form radial compactification
// centred on `origin`:
//
//     delta    = p_world - origin
//     r        = |delta|
//     r_norm   = r / scale
//     t        = strength * r_norm^falloff
//     r_chart  = r_max * tanh(t)
//     chart_pos = origin + (r_chart / r) * delta
//
// Direction-preserving (unit vector
// `normalize(delta)` is invariant under the
// transform; only the radial scalar changes).
// Sign-preserving (positive `r` maps to positive
// `r_chart`). Monotonic (`r_chart` is a strictly-
// increasing function of `r`). Bounded above by
// `r_max` (`tanh` saturates at `+1.0f` in IEEE-754
// single precision for `t > ~16`).
//
// Returns `p_world` unchanged when:
//   - `validate_params(...)` returns `false`
//     (defensive fallback);
//   - `strength == 0` (artist disabled);
//   - `r_max == 0` (no compactification boundary
//     configured);
//   - `|delta| == 0` (`p_world == origin`, the
//     fixed point of the radial map).
//
// The output is finite by construction (no NaN /
// Inf possible for any finite, in-range input).
RR_HD inline rr::math::Vec3 penrose_like_world_to_chart(
        rr::math::Vec3 p_world,
        rr::math::Vec3 origin,
        const PenroseLikeCompactificationParams& p) {
    using rr::math::length;
    using rr::math::Vec3;

    if (!penrose_like_validate_params(p)) return p_world;
    if (p.strength == 0.0f)                return p_world;
    if (p.r_max == 0.0f)                   return p_world;

    const Vec3  delta = p_world - origin;
    const float r     = length(delta);
    if (r <= 1.0e-20f) return p_world;  // at the origin

    const float r_norm  = r / p.scale;
    const float ratio   = std::pow(r_norm, p.falloff);
    const float t       = p.strength * ratio;
    const float r_chart = p.r_max * std::tanh(t);
    const float scale_factor = r_chart / r;
    return origin + delta * scale_factor;
}

// Inverse map: chart-space position → world-space
// position via analytical `atanh` (closed-form, no
// Newton-Raphson iteration; the forward map's
// monotonicity along the radial scalar makes the
// inverse one-to-one).
//
//     delta_chart = chart_pos - origin
//     r_chart     = |delta_chart|
//     r_clamped   = min(r_chart, r_max * (1 - epsilon))
//     arg         = r_clamped / r_max
//     t           = atanh(arg)
//     ratio       = t / strength
//     r_norm      = ratio^(1 / falloff)
//     r           = scale * r_norm
//     world_pos   = origin + (r / r_chart) * delta_chart
//
// Returns `chart_pos` unchanged when:
//   - `validate_params(...)` returns `false`;
//   - `strength == 0`;
//   - `r_max == 0`;
//   - `|delta_chart| == 0` (`chart_pos == origin`).
//
// Safety:
//   - The `r_chart >= r_max` boundary is clamped to
//     `r_max * (1 - kBoundaryEpsilon)` before
//     `atanh` to keep the inverse finite. The
//     residual at the clamp is documented in the
//     plan §6.4 (`≤ 1e-6` for typical parameter
//     ranges).
//   - `pow(ratio, 1/falloff)` is well-defined for
//     `ratio >= 0` and `falloff > 0` (both
//     validator-enforced).
RR_HD inline rr::math::Vec3 penrose_like_chart_to_world(
        rr::math::Vec3 chart_pos,
        rr::math::Vec3 origin,
        const PenroseLikeCompactificationParams& p) {
    using rr::math::length;
    using rr::math::Vec3;

    if (!penrose_like_validate_params(p)) return chart_pos;
    if (p.strength == 0.0f)                return chart_pos;
    if (p.r_max == 0.0f)                   return chart_pos;

    const Vec3  delta = chart_pos - origin;
    const float r_chart = length(delta);
    if (r_chart <= 1.0e-20f) return chart_pos;  // at the origin

    // Boundary clamp: r_chart must be strictly less
    // than r_max to keep atanh finite.
    const float upper = p.r_max * (1.0f - detail::kBoundaryEpsilon);
    const float r_eval = r_chart > upper ? upper : r_chart;
    const float arg    = r_eval / p.r_max;

    const float t      = std::atanh(arg);
    const float ratio  = t / p.strength;
    const float r_norm = std::pow(ratio, 1.0f / p.falloff);
    const float r      = p.scale * r_norm;

    const float scale_factor = r / r_chart;
    return origin + delta * scale_factor;
}

}  // namespace rr::manifold

#pragma once

// Schwarzschild-like artistic coordinate-warp math helpers.
// See `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` for the
// canonical design; this header is the SCHW.1 leaf in
// the SCHW.* sub-slice ladder.
//
// The helpers are **artistic, not physical**: they
// approximate the visual signature of Schwarzschild-
// like coordinate compression near a configured mass
// without integrating any geodesic ODE or evaluating
// Christoffel symbols. The architecture-doc §8
// non-goals "physically exact Kerr ray tracing" and
// "full GR solver" stay in force; the *Like naming
// convention from MANIFOLD.1 flags the artistic-not-
// physical status at every call site.
//
// Closed-form math, RR_HD inline throughout, callable
// from both host and device code. The plan's §5
// safety invariants are enforced by construction:
//
//   - No NaN / Inf: `clamp_radius > 0` prevents the
//     `1 / r^falloff` singularity at the mass origin;
//     `pow(r, falloff)` for `r > 0` is finite.
//   - Euclidean fallback: `warp_strength = 0` or
//     `r_s = 0` returns the input unchanged
//     (verifiable analytically; no FP drift).
//   - Bounded transforms: the displacement scalar
//     `f = warp_strength * r_s / r^falloff` is
//     bounded above by `warp_strength * r_s /
//     clamp_radius^falloff` (proven analytically).
//   - Reversible (approximate): the
//     `_chart_to_world` helper runs bounded Newton-
//     Raphson on the radial equation with a hard cap
//     on iteration count.
//   - Bit-identity on the Euclidean off-path: the
//     SCHW.* integration slices gate these helpers
//     behind `is_active(manifold_mode)` so the
//     existing pre-pivot code path stays unchanged
//     when the chart family is `Euclidean`.
//   - Defence-in-depth: every helper calls
//     `schwarzschild_like_validate_params(...)`
//     before evaluating the formula; on invalid
//     input the helper returns the documented
//     fallback (input pass-through, treated as
//     Euclidean).
//
// SCHW.1 scope: this header only. No
// `ManifoldTransform.h` change yet (SCHW.2); no kernel
// code change (SCHW.3 / SCHW.4); no AOV encoding
// change (SCHW.5); no audit doc (SCHW.6). The header
// compiles cleanly on its own under
// `g++ -std=c++20 -Isrc -Wall -Wextra -Werror`.

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"

#include <cmath>

namespace rr::manifold {

// Per-slice parameter bag for the Schwarzschild-like
// chart. Mirrors the plan §3 parameter mapping (the
// fields hosted on `CoordinateChart` and
// `CoordinateChartParameters` are reinterpreted per
// chart family; this struct gives the SchwarzschildLike
// math its own named fields without an ABI bump on the
// chart POD).
//
//   - `r_s`            := schwarzschildRadiusLike;
//                         the "event horizon proxy"
//                         radius. Range `[0, ∞)`;
//                         default `0` is the
//                         Euclidean fallback
//                         regardless of `warp_strength`.
//   - `warp_strength`  := artist scalar dial;
//                         nominal `[0, 1]`. Out-of-
//                         range values pass through
//                         per `ManifoldMode::strength`'s
//                         contract; this helper does
//                         NOT clamp the dial.
//   - `falloff`        := exponent in
//                         `1 / r^falloff`. Range
//                         `[0.5, 4.0]`; default `1.0`
//                         is Newtonian-like
//                         (1/r far-field).
//   - `clamp_radius`   := minimum `r` the formula
//                         uses; positive lower bound
//                         on the `1/r` denominator.
//                         Range `(0, ∞)`; default
//                         `1.0`.
struct SchwarzschildLikeWarpParams {
    float r_s           = 0.0f;
    float warp_strength = 0.0f;
    float falloff       = 1.0f;
    float clamp_radius  = 1.0f;
};

// Validate parameter ranges per the plan §5.6 host-side
// validator. Returns `true` when every parameter is
// finite AND inside its documented range:
//
//   - `r_s >= 0` and finite;
//   - `warp_strength` finite (out-of-nominal-range
//     values pass through per the
//     `ManifoldMode::strength` contract; only NaN /
//     Inf are rejected here);
//   - `falloff` finite AND in `[0.5, 4.0]`;
//   - `clamp_radius > 0` and finite.
//
// On `false`, callers fall back to the Euclidean
// default (input pass-through) per the plan §5.2.
RR_HD inline bool schwarzschild_like_validate_params(
        const SchwarzschildLikeWarpParams& p) {
    if (!std::isfinite(p.r_s) || p.r_s < 0.0f) return false;
    if (!std::isfinite(p.warp_strength))       return false;
    if (!std::isfinite(p.falloff) ||
        p.falloff < 0.5f || p.falloff > 4.0f)  return false;
    if (!std::isfinite(p.clamp_radius) ||
        p.clamp_radius <= 0.0f)                return false;
    return true;
}

// Forward map: world-space position → chart-space
// position. Closed-form radial displacement centred
// on `mass_origin`:
//
//     delta = p_world - mass_origin
//     r     = max(|delta|, clamp_radius)
//     f     = warp_strength * r_s / r^falloff
//     chart_pos = p_world + f * delta
//
// Returns `p_world` unchanged when:
//   - `validate_params(...)` returns `false`
//     (defensive fallback);
//   - `warp_strength == 0` (artist disabled);
//   - `r_s == 0` (no Schwarzschild radius
//     configured).
//
// The output is finite by construction:
//   - `r >= clamp_radius > 0` so `r^falloff > 0`
//     and `1 / r^falloff` is finite;
//   - `f` is bounded above by
//     `warp_strength * r_s / clamp_radius^falloff`.
RR_HD inline rr::math::Vec3 schwarzschild_like_world_to_chart(
        rr::math::Vec3 p_world,
        rr::math::Vec3 mass_origin,
        const SchwarzschildLikeWarpParams& p) {
    using rr::math::length;
    using rr::math::Vec3;

    if (!schwarzschild_like_validate_params(p)) return p_world;
    if (p.warp_strength == 0.0f)                return p_world;
    if (p.r_s == 0.0f)                          return p_world;

    const Vec3  delta     = p_world - mass_origin;
    const float r_raw     = length(delta);
    const float r         = r_raw < p.clamp_radius ? p.clamp_radius : r_raw;
    const float inv_r_pow = std::pow(r, -p.falloff);
    const float f         = p.warp_strength * p.r_s * inv_r_pow;
    return p_world + delta * f;
}

// Approximate inverse map: chart-space position →
// world-space position via bounded Newton-Raphson on
// the radial equation. The forward map is purely
// radial along `p_world - mass_origin`, so the
// inverse reduces to a 1-D root-find on the radial
// scalar:
//
//   F(r) = (1 + warp_strength * r_s / r^falloff) * r
//          - |chart_pos - mass_origin| = 0
//
//   F'(r) = 1 + warp_strength * r_s * (1 - falloff)
//             / r^falloff
//         = 1 + f_val * (1 - falloff)
//
// Iteration count is hard-capped at 8; convergence
// tolerance `1.0e-5f`. The plan §5.4 documents the
// residual as `≤ 1e-4` for typical parameter ranges;
// in practice the iteration converges in 3-5 steps
// for `r_s = 1`, `warp_strength ∈ [0, 1]`, `falloff
// ∈ [0.5, 4.0]`.
//
// Safety:
//   - The clamped-region case `r < clamp_radius`
//     uses `F'(r) = 1` (the warp factor is constant
//     in the clamp shell; F reduces to the linear
//     map).
//   - If `F'` drops below `1.0e-9f` (pathological
//     parameter combinations), the iteration breaks
//     early and returns the current best `r`.
//   - Negative `r` after a NR step is clamped to
//     `clamp_radius` to keep the iteration in the
//     valid domain.
RR_HD inline rr::math::Vec3 schwarzschild_like_chart_to_world(
        rr::math::Vec3 chart_pos,
        rr::math::Vec3 mass_origin,
        const SchwarzschildLikeWarpParams& p) {
    using rr::math::length;
    using rr::math::Vec3;

    if (!schwarzschild_like_validate_params(p)) return chart_pos;
    if (p.warp_strength == 0.0f)                return chart_pos;
    if (p.r_s == 0.0f)                          return chart_pos;

    const Vec3  g     = chart_pos - mass_origin;
    const float g_len = length(g);
    if (g_len <= 1.0e-12f) return chart_pos;  // at the mass origin

    // Initial guess: r_world ≈ g_len (the Euclidean limit).
    float r = g_len;

    constexpr int   kMaxIterations = 8;
    constexpr float kTolerance     = 1.0e-5f;
    for (int iter = 0; iter < kMaxIterations; ++iter) {
        const bool  in_clamp_shell = r < p.clamp_radius;
        const float r_eval         = in_clamp_shell ? p.clamp_radius : r;
        const float inv_r_pow      = std::pow(r_eval, -p.falloff);
        const float f_val          = p.warp_strength * p.r_s * inv_r_pow;
        const float F              = (1.0f + f_val) * r - g_len;
        if (std::fabs(F) < kTolerance) break;

        const float F_prime = in_clamp_shell
                              ? 1.0f
                              : (1.0f + f_val * (1.0f - p.falloff));
        if (std::fabs(F_prime) < 1.0e-9f) break;

        r = r - F / F_prime;
        if (r < 0.0f) r = p.clamp_radius;
    }

    // Reconstruct the world position from the radial scalar
    // `r` and the chart-space direction (the warp is
    // radial, so `p_world` is along the same line as
    // `chart_pos` measured from `mass_origin`).
    return mass_origin + g * (r / g_len);
}

// Optional: warp a primary-ray direction toward the
// configured mass origin. Produces the visual lensing
// signature described in the plan §4.2 / §6.2.
//
// The bending strength falls off as `1 / r` from the
// ray's origin to the mass (a simpler model than the
// coordinate-warp's `1 / r^falloff`; the primary-ray
// warp is intentionally a separate, simpler formula
// that does not invoke `falloff`).
//
//   to_mass    = mass_origin - ray_origin
//   distance   = max(|to_mass|, clamp_radius)
//   bend       = clamp(warp_strength * r_s / distance,
//                       -0.5, +0.5)
//   bend_dir   = normalize(to_mass)
//   out_dir    = normalize(ray_dir + bend * bend_dir)
//
// The bending factor is **hard-capped at `0.5`** per
// the plan §6.2 so the primary ray cannot flip
// direction or terminate inside the mass. The cap
// makes the helper safe to call from any caller
// without further bounds-checking.
//
// Returns `ray_dir` unchanged when:
//   - `validate_params(...)` returns `false`;
//   - `warp_strength == 0`;
//   - `r_s == 0`.
RR_HD inline rr::math::Vec3 schwarzschild_like_warp_ray_direction(
        rr::math::Vec3 ray_origin,
        rr::math::Vec3 ray_dir,
        rr::math::Vec3 mass_origin,
        const SchwarzschildLikeWarpParams& p) {
    using rr::math::length;
    using rr::math::normalize;
    using rr::math::Vec3;

    if (!schwarzschild_like_validate_params(p)) return ray_dir;
    if (p.warp_strength == 0.0f)                return ray_dir;
    if (p.r_s == 0.0f)                          return ray_dir;

    const Vec3  to_mass         = mass_origin - ray_origin;
    const float distance        = length(to_mass);
    const float distance_eval   = distance < p.clamp_radius
                                    ? p.clamp_radius
                                    : distance;

    float bend = p.warp_strength * p.r_s / distance_eval;

    // Hard cap at ±0.5 to prevent direction flips.
    constexpr float kBendCap = 0.5f;
    if (bend >  kBendCap) bend =  kBendCap;
    if (bend < -kBendCap) bend = -kBendCap;

    const Vec3 bend_dir = (distance > 1.0e-12f)
                             ? to_mass * (1.0f / distance)
                             : Vec3{0.0f, 0.0f, 0.0f};
    return normalize(ray_dir + bend_dir * bend);
}

}  // namespace rr::manifold

#pragma once

// Relativity math leaf. Host- and device-callable (RR_HD inline).
//
// Conventions
// -----------
// - Natural units throughout: c = 1, so a 3-velocity component is a
//   dimensionless beta value in (-1, +1).
// - Direction inputs are unit-length unless explicitly noted; outputs
//   that should be unit-length are renormalised before return.
// - All arithmetic is single-precision. Double precision (for
//   high-beta extremes) is a future task and lives behind the same
//   API.
//
// Each function carries a header tag - PHYSICAL or ARTISTIC
// APPROXIMATION - explaining whether it is a textbook special-relativity
// formula or a placeholder that exists until a more rigorous
// implementation arrives (typically waiting on a spectral pipeline).

#include "math/MathUtils.h"
#include "math/Vec3.h"

#include <cmath>

namespace rr::relativity {

// PHYSICAL.
// Clamp |beta| just below the lightspeed singularity so `gamma` stays
// finite. Negative inputs are folded to magnitude. `max_beta` is
// itself capped at 0.999999 to keep gamma well-conditioned even when
// the caller passes through artist-supplied values.
RR_HD inline float clampBeta(float beta_magnitude, float max_beta) {
    if (beta_magnitude < 0.0f) beta_magnitude = -beta_magnitude;
    if (max_beta       < 0.0f) max_beta       = -max_beta;
    if (max_beta > 0.999999f) max_beta = 0.999999f;
    return beta_magnitude > max_beta ? max_beta : beta_magnitude;
}

// PHYSICAL.
// Lorentz factor:  gamma = 1 / sqrt(1 - beta^2).
// Caller is responsible for clamping `beta_magnitude` first; this
// guards `1 - beta^2 <= 0` only as a numerical safety net.
RR_HD inline float gamma(float beta_magnitude) {
    const float b2    = beta_magnitude * beta_magnitude;
    const float denom = 1.0f - b2;
    if (denom <= 0.0f) {
        // Should never happen if clampBeta was called.
        return 1.0f / std::sqrt(1.0e-12f);
    }
    return 1.0f / std::sqrt(denom);
}

// PHYSICAL.
// Length contraction along the direction of motion:
//   L_observed = L_rest * lorentzContraction(beta) = L_rest / gamma.
// Returns 1/gamma, i.e. sqrt(1 - beta^2). Equals 1 at rest and
// approaches 0 as |beta| -> 1.
RR_HD inline float lorentzContraction(float beta_magnitude) {
    const float b2       = beta_magnitude * beta_magnitude;
    const float radicand = 1.0f - b2;
    if (radicand <= 0.0f) return 0.0f;
    return std::sqrt(radicand);
}

// PHYSICAL.
// Relativistic Doppler factor for an observer with 3-velocity
// `beta_vec` (in c-units) receiving a photon arriving along unit
// `direction` (the direction of travel of the photon in the scene
// frame).
//
//   D = 1 / [ gamma * (1 - beta . dir) ]
//
//   D > 1 : blueshift (source approaching).
//   D < 1 : redshift  (source receding).
// Identity at |beta| = 0.
RR_HD inline float dopplerFactor(rr::math::Vec3 beta_vec,
                                 rr::math::Vec3 direction) {
    const float beta_mag = rr::math::length(beta_vec);
    const float g        = gamma(beta_mag);
    const float bdotd    = rr::math::dot(beta_vec, direction);
    const float denom    = g * (1.0f - bdotd);
    if (denom <= 1.0e-12f) return 1.0f;  // numerical safety net
    return 1.0f / denom;
}

// PHYSICAL (relativistic beaming, bolometric form).
// Specific intensity per frequency follows the Lorentz invariant
// I_nu / nu^3, so a photon arriving with Doppler factor D has its
// per-frequency intensity scaled by D^3. Integrated over frequency
// (bolometric) the scaling is D^4. We expose D^4 as the canonical
// factor; the renderer is free to use D^3 for monochromatic light.
//
// `searchlight_strength` from RelativityParams is intended to
// modulate the result outside this leaf; this function deliberately
// stays pure (just D^4) so the physics is one place.
RR_HD inline float searchlightFactor(float doppler_factor) {
    const float D2 = doppler_factor * doppler_factor;
    return D2 * D2;  // D^4
}

// PHYSICAL (Lorentz aberration of a unit light direction).
// Vector form, equivalent to the textbook angle relation
// cos(theta') = (cos(theta) - beta) / (1 - beta cos(theta)) for the
// special case where `direction` lies in the plane of `beta_vec`:
//
//   d' = ( d/gamma + beta * [ gamma (beta . d) / (gamma + 1) - 1 ] )
//        / (1 - beta . d)
//
// Returns the aberrated direction renormalised to unit length. At
// |beta| = 0 the function is the identity. The output is the
// direction of the same photon as observed in the boosted frame.
RR_HD inline rr::math::Vec3 aberrateDirection(rr::math::Vec3 beta_vec,
                                              rr::math::Vec3 direction) {
    using rr::math::Vec3;
    using rr::math::dot;
    using rr::math::normalize;

    const float beta_mag = rr::math::length(beta_vec);
    if (beta_mag <= 1.0e-12f) return direction;

    const float g     = gamma(beta_mag);
    const float bdotd = dot(beta_vec, direction);
    const float coef  = (g * bdotd) / (g + 1.0f) - 1.0f;
    const Vec3  num   = direction * (1.0f / g) + beta_vec * coef;
    const float denom = 1.0f - bdotd;
    if (denom * denom <= 1.0e-24f) return direction;

    return normalize(num * (1.0f / denom));
}

// ---- Stage 18A.3: precomputed launch invariants -----------------
//
// `aberrateDirection(beta_vec, dir)` and `dopplerFactor(beta_vec,
// dir)` both internally compute `length(beta_vec)` and
// `gamma(beta_mag)`. With the relativity stack on (the default)
// every pixel pays four `sqrt`s for two scalar values that depend
// only on the per-launch observer velocity, not on the per-pixel
// ray direction.
//
// `PrecomputedRelativity` snapshots the invariants once. Kernels
// that call both helpers per-pixel build it once at thread entry
// (or once per launch via constant memory in a future slice) and
// pass the snapshot to the precomputed-input overloads below,
// halving the per-pixel `sqrt` count and trimming dependent-chain
// length through the relativity stack.
//
// At |beta| = 0 the gamma value is 1 and the precomputed-input
// helpers degenerate to identity, matching the existing
// two-argument variants byte-for-byte.
struct PrecomputedRelativity {
    rr::math::Vec3 beta_vec = {0.0f, 0.0f, 0.0f};
    float          beta_mag = 0.0f;  // length(beta_vec)
    float          gamma    = 1.0f;  // 1 / sqrt(1 - beta^2)
};

// PHYSICAL.
// Build the per-launch precomputed invariants. Pure host/device
// helper; runs once per kernel launch (or per thread, if the
// launch parameters are read from constant memory).
RR_HD inline PrecomputedRelativity precompute_relativity(
        rr::math::Vec3 beta_vec) {
    PrecomputedRelativity p;
    p.beta_vec = beta_vec;
    p.beta_mag = rr::math::length(beta_vec);
    p.gamma    = gamma(p.beta_mag);
    return p;
}

// PHYSICAL.
// Doppler factor with the per-launch invariants supplied. Equivalent
// to `dopplerFactor(p.beta_vec, direction)` but skips the redundant
// `length` + `gamma` reductions (saves two `sqrt`s per call).
RR_HD inline float dopplerFactor(const PrecomputedRelativity& p,
                                 rr::math::Vec3 direction) {
    const float bdotd = rr::math::dot(p.beta_vec, direction);
    const float denom = p.gamma * (1.0f - bdotd);
    if (denom <= 1.0e-12f) return 1.0f;
    return 1.0f / denom;
}

// PHYSICAL.
// Aberration with the per-launch invariants supplied. Equivalent
// to `aberrateDirection(p.beta_vec, direction)` but skips the
// redundant `length` + `gamma` reductions (saves two `sqrt`s per
// call). Returns the input direction unchanged at |beta| = 0.
RR_HD inline rr::math::Vec3 aberrateDirection(
        const PrecomputedRelativity& p,
        rr::math::Vec3               direction) {
    using rr::math::Vec3;
    using rr::math::dot;
    using rr::math::normalize;

    if (p.beta_mag <= 1.0e-12f) return direction;

    const float bdotd = dot(p.beta_vec, direction);
    const float coef  = (p.gamma * bdotd) / (p.gamma + 1.0f) - 1.0f;
    const Vec3  num   = direction * (1.0f / p.gamma) + p.beta_vec * coef;
    const float denom = 1.0f - bdotd;
    if (denom * denom <= 1.0e-24f) return direction;

    return normalize(num * (1.0f / denom));
}

// ARTISTIC APPROXIMATION.
// A physically correct Doppler colour shift requires a spectral
// representation: each spectral band is shifted by D in frequency
// and the result is re-projected onto the renderer's colour
// primaries. The texture / shading pipeline is not spectral yet -
// that lands with M16 / M17 - so this routine is a placeholder.
//
// It maps `doppler_factor` to a signed mix factor via
// `tanh(0.5 * log(D)) * strength`, which is monotonic in D, exactly
// 0 at D = 1, bounded in [-1, +1], and identity at strength = 0.
// Positive values mix the input RGB toward a cool tint
// (blueshift); negative values mix toward a warm tint (redshift).
//
// Replace with a proper spectral remap once the spectral pipeline
// lands.
RR_HD inline rr::math::Vec3 applyDopplerColor(rr::math::Vec3 rgb,
                                              float doppler_factor,
                                              float strength) {
    using rr::math::Vec3;
    using rr::math::clamp;
    using rr::math::lerp;

    if (strength <= 0.0f)        return rgb;
    if (doppler_factor <= 0.0f)  doppler_factor = 1.0e-6f;

    const float t_raw = std::tanh(0.5f * std::log(doppler_factor));
    const float t     = clamp(t_raw * strength, -1.0f, 1.0f);

    const Vec3 cool_tint{0.6f, 0.8f, 1.0f};  // blueshift target
    const Vec3 warm_tint{1.0f, 0.6f, 0.5f};  // redshift target

    if (t > 0.0f) return lerp(rgb, rgb * cool_tint,  t);
    if (t < 0.0f) return lerp(rgb, rgb * warm_tint, -t);
    return rgb;
}

}

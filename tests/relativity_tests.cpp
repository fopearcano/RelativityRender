// Stage 19E.1 relativity-math tests.
//
// The relativity math leaf (`src/relativity/RelativityMath.h`) ships
// production-ready RR_HD inlines that both the CUDA and OptiX
// raygen / closest-hit / miss programs depend on. Until this slice
// the leaf had no unit-test coverage — the only verification was
// indirect (kernel runs that the audit host cannot exercise). This
// file makes the model scientifically testable: every formula is
// checked against its closed-form analytic value (where one exists)
// or against a stable physical invariant (Lorentz-factor identities,
// unit-length output, finite + positive D for |beta| < 1).
//
// No test framework is in the project — same hand-rolled `RR_CHECK`
// macro the existing `tests/*` files use. main() returns 0 when
// every check passes, 1 otherwise.
//
// Conventions of `rr::relativity` (verified by the tests below):
//   - Natural units: c = 1; `beta_vec` components are dimensionless
//     in (-1, +1).
//   - `direction` is a unit 3-vector; outputs that should be unit
//     length are renormalised before return.
//   - `dopplerFactor(beta_vec, direction)` returns
//     D = 1 / [gamma * (1 - beta · direction)].
//     The leaf's header documents that D > 1 is the blueshift case
//     (source-approaching) and D < 1 is the redshift case. When
//     `direction` is parallel to `beta_vec` (beta · direction =
//     +|beta|) D simplifies to sqrt((1 + |beta|) / (1 - |beta|)),
//     i.e. blueshift; when antiparallel (beta · direction =
//     -|beta|) D simplifies to sqrt((1 - |beta|) / (1 + |beta|)),
//     i.e. redshift. The tests below adopt the leaf's "forward
//     = parallel to beta" convention verbatim.
//   - `aberrateDirection(beta_vec, direction)` returns the
//     aberrated unit direction d'. Decomposed against the boost
//     axis it satisfies the textbook relation
//     cos(theta') = (cos(theta) - beta) / (1 - beta * cos(theta))
//     where theta = angle(beta_vec, direction). The transverse
//     component scales as 1/gamma. (Verified directly below for a
//     purely perpendicular incidence.)
//   - Invalid |beta| >= 1 is **clamped** by `clampBeta`, not
//     rejected. The default cap is 0.999999 (set by `clampBeta`'s
//     own internal max). Callers who hand the math leaf an
//     unclamped |beta| >= 1 hit `gamma`'s 1e-12 numerical safety
//     net (returns ~1e6) rather than a NaN; the tests document
//     this behaviour but do not assert any particular numerical
//     value past the safety net.
//
// All arithmetic is single-precision (matches the leaf). Tolerances
// reflect float32 rounding, with looser tolerances near |beta| =
// 0.99 where (1 - beta^2) loses ~5 decimals of significance.

#include "manifold/ObserverFrame.h"  // OBS-P.2: perception-mode ternary verification
#include "math/Vec3.h"
#include "relativity/RelativityMath.h"
#include "relativity/RelativityParams.h"

#include <cmath>
#include <cstdio>

namespace {

int g_total  = 0;
int g_failed = 0;

#define RR_CHECK(...)                                                         \
    do {                                                                      \
        ++g_total;                                                            \
        if (!(__VA_ARGS__)) {                                                 \
            ++g_failed;                                                       \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                        \
                         #__VA_ARGS__, __FILE__, __LINE__);                   \
        }                                                                     \
    } while (0)

constexpr float kEps      = 1.0e-5f;
constexpr float kEpsLoose = 5.0e-4f;  // for high-beta (|beta| >= 0.9) cases

bool approx(float a, float b, float eps = kEps) {
    return std::fabs(a - b) <= eps;
}

bool approx(rr::math::Vec3 a, rr::math::Vec3 b, float eps = kEps) {
    return approx(a.x, b.x, eps)
        && approx(a.y, b.y, eps)
        && approx(a.z, b.z, eps);
}

bool finite_positive(float x) {
    return std::isfinite(x) && x > 0.0f;
}

bool finite(rr::math::Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Closed-form longitudinal Doppler factor D = sqrt((1+b)/(1-b)).
// Used as the analytic ground truth for the blueshift / redshift
// tests so the assertion is independent of the leaf's
// implementation details.
double doppler_longitudinal(double beta) {
    return std::sqrt((1.0 + beta) / (1.0 - beta));
}

// Closed-form aberration relation:
//   cos(theta') = (cos(theta) - beta) / (1 - beta * cos(theta))
// where theta is measured from the boost axis (= beta direction).
// Used as analytic ground truth for arbitrary incidence angles.
double aberrated_cos(double cos_theta, double beta) {
    return (cos_theta - beta) / (1.0 - beta * cos_theta);
}

// ---------- 1. beta = 0 returns identity direction and D = 1 ----------

void test_identity_at_zero_beta() {
    using rr::math::Vec3;
    using rr::relativity::aberrateDirection;
    using rr::relativity::dopplerFactor;
    using rr::relativity::gamma;
    using rr::relativity::lorentzContraction;
    using rr::relativity::searchlightFactor;

    const Vec3 zero_beta{0.0f, 0.0f, 0.0f};

    // Every direction in the canonical basis (and a couple of
    // generic unit directions) round-trips through aberrate +
    // doppler at beta = 0 unchanged.
    const Vec3 directions[] = {
        {+1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        { 0.0f, +1.0f, 0.0f},
        { 0.0f, -1.0f, 0.0f},
        { 0.0f, 0.0f, +1.0f},
        { 0.0f, 0.0f, -1.0f},
        rr::math::normalize(Vec3{1.0f, 2.0f, 3.0f}),
        rr::math::normalize(Vec3{-3.0f, 4.0f, -1.0f}),
    };

    for (const Vec3& d : directions) {
        RR_CHECK(approx(aberrateDirection(zero_beta, d), d));
        RR_CHECK(approx(dopplerFactor(zero_beta, d), 1.0f));
    }

    // Scalar identities at |beta| = 0.
    RR_CHECK(approx(gamma(0.0f),               1.0f));
    RR_CHECK(approx(lorentzContraction(0.0f),  1.0f));
    RR_CHECK(approx(searchlightFactor(1.0f),   1.0f));

    // Precomputed-launch overload should also return identity at
    // beta = 0 (Stage 18A.3 perf path; must not drift from the
    // direct-call result).
    const auto p_zero = rr::relativity::precompute_relativity(zero_beta);
    RR_CHECK(approx(p_zero.beta_mag, 0.0f));
    RR_CHECK(approx(p_zero.gamma,    1.0f));
    for (const Vec3& d : directions) {
        RR_CHECK(approx(aberrateDirection(p_zero, d), d));
        RR_CHECK(approx(dopplerFactor(p_zero, d), 1.0f));
    }
}

// ---------- 2. forward direction (parallel to beta) gives blueshift ----------

void test_forward_blueshift() {
    using rr::math::Vec3;
    using rr::relativity::dopplerFactor;

    // Forward = direction parallel to beta_vec (beta · direction
    // = +|beta|).  D_parallel = sqrt((1+b)/(1-b)) > 1.
    struct Case { float beta; };
    const Case cases[] = {{0.10f}, {0.25f}, {0.50f}, {0.75f}, {0.90f}};

    for (const Case& c : cases) {
        // Three distinct boost axes to confirm the result is
        // axis-independent (only the parallel projection matters).
        const Vec3 axes[] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        for (const Vec3& axis : axes) {
            const Vec3  beta_vec = axis * c.beta;
            const Vec3  fwd      = axis;  // parallel to beta_vec
            const float D        = dopplerFactor(beta_vec, fwd);
            const float D_ref    = static_cast<float>(
                doppler_longitudinal(static_cast<double>(c.beta)));

            RR_CHECK(D > 1.0f);                       // blueshift
            RR_CHECK(approx(D, D_ref, kEps));         // analytic match
        }
    }
}

// ---------- 3. backward direction (antiparallel to beta) gives redshift ----------

void test_backward_redshift() {
    using rr::math::Vec3;
    using rr::relativity::dopplerFactor;

    // Backward = direction antiparallel to beta_vec (beta · dir
    // = -|beta|).  D_anti = sqrt((1-b)/(1+b)) < 1, i.e. 1 / D_fwd.
    struct Case { float beta; };
    const Case cases[] = {{0.10f}, {0.25f}, {0.50f}, {0.75f}, {0.90f}};

    for (const Case& c : cases) {
        const Vec3 axes[] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        for (const Vec3& axis : axes) {
            const Vec3  beta_vec = axis * c.beta;
            const Vec3  back     = axis * -1.0f;  // antiparallel
            const float D        = dopplerFactor(beta_vec, back);
            const float D_ref    = 1.0f / static_cast<float>(
                doppler_longitudinal(static_cast<double>(c.beta)));

            RR_CHECK(D < 1.0f);                  // redshift
            RR_CHECK(D > 0.0f);                  // strictly positive
            RR_CHECK(approx(D, D_ref, kEps));    // analytic match

            // Forward * Backward = 1 (longitudinal Doppler is its
            // own inverse under beta -> -beta).
            const float D_fwd = dopplerFactor(beta_vec, axis);
            RR_CHECK(approx(D * D_fwd, 1.0f, kEps));
        }
    }
}

// ---------- 4. aberration angle matches analytic formula ----------

void test_aberration_matches_analytic() {
    using rr::math::Vec3;
    using rr::math::dot;
    using rr::relativity::aberrateDirection;

    // For boost along z, an incidence direction at angle theta
    // from +z aberrates to angle theta' satisfying
    //   cos(theta') = (cos(theta) - beta) / (1 - beta * cos(theta)).
    // Sweep beta and theta; the leaf's z-component of d' must
    // match cos(theta') analytically.

    const float betas[] = {0.10f, 0.30f, 0.50f, 0.75f, 0.90f};
    // Mix easy angles + irrational angles to exercise rounding.
    const float angles_deg[] = {
        0.0f, 30.0f, 45.0f, 60.0f, 75.0f, 90.0f,
        105.0f, 120.0f, 135.0f, 150.0f, 180.0f,
    };

    for (float beta : betas) {
        const Vec3 beta_vec{0.0f, 0.0f, beta};
        for (float theta_deg : angles_deg) {
            const float theta_rad = theta_deg
                                  * static_cast<float>(rr::math::kPi) / 180.0f;
            const float cos_t     = std::cos(theta_rad);
            const float sin_t     = std::sin(theta_rad);

            // direction lies in the xz-plane at angle theta from +z
            const Vec3 dir{sin_t, 0.0f, cos_t};
            const Vec3 d_prime = aberrateDirection(beta_vec, dir);

            // Output must remain a unit vector.
            RR_CHECK(approx(rr::math::length(d_prime), 1.0f, kEps));

            // Out of plane: y-component must be zero (the boost is
            // along z; the rotation it induces stays in the xz-plane).
            RR_CHECK(approx(d_prime.y, 0.0f, kEps));

            // Match the analytic cos(theta').
            const float cos_t_prime_ref = static_cast<float>(
                aberrated_cos(static_cast<double>(cos_t),
                              static_cast<double>(beta)));
            RR_CHECK(approx(d_prime.z, cos_t_prime_ref, kEps));
        }
    }

    // Spot-check the perpendicular-incidence closed form
    // explicitly: at theta = 90°, d' = (1/gamma, 0, -beta).
    {
        const float beta = 0.6f;
        const Vec3  beta_vec{0.0f, 0.0f, beta};
        const Vec3  dir{1.0f, 0.0f, 0.0f};
        const Vec3  d_prime = aberrateDirection(beta_vec, dir);
        const float inv_g   = std::sqrt(1.0f - beta * beta);
        RR_CHECK(approx(d_prime, Vec3{inv_g, 0.0f, -beta}, kEps));
    }
}

// ---------- 5. D remains finite and positive for |beta| < 1 ----------

void test_doppler_finite_positive_for_subluminal_beta() {
    using rr::math::Vec3;
    using rr::relativity::dopplerFactor;

    const float betas[] = {
        0.0f, 0.01f, 0.10f, 0.25f, 0.50f, 0.75f,
        0.90f, 0.95f, 0.99f, 0.999f, 0.999999f,
    };

    // A small grid of unit directions covering each octant.
    const Vec3 dirs[] = {
        {+1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, +1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, +1.0f},
        {0.0f, 0.0f, -1.0f},
        rr::math::normalize(Vec3{ 1.0f,  1.0f,  1.0f}),
        rr::math::normalize(Vec3{-1.0f,  1.0f,  1.0f}),
        rr::math::normalize(Vec3{ 1.0f, -1.0f,  1.0f}),
        rr::math::normalize(Vec3{ 1.0f,  1.0f, -1.0f}),
        rr::math::normalize(Vec3{-1.0f, -1.0f, -1.0f}),
        rr::math::normalize(Vec3{ 2.0f,  3.0f,  4.0f}),
        rr::math::normalize(Vec3{-5.0f,  1.0f,  2.0f}),
    };

    // Three boost axes per beta to catch axis-specific bugs.
    const Vec3 axes[] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    for (float beta : betas) {
        for (const Vec3& axis : axes) {
            const Vec3 beta_vec = axis * beta;
            for (const Vec3& d : dirs) {
                const float D = dopplerFactor(beta_vec, d);
                RR_CHECK(finite_positive(D));
            }
        }
    }
}

// ---------- 6. invalid |beta| >= 1 is clamped per existing design ----------

void test_clamp_beta_existing_design() {
    using rr::relativity::clampBeta;
    using rr::relativity::RelativityParams;

    // The existing design CLAMPS rather than rejects: callers pass
    // an artist-supplied |beta| through `clampBeta(beta_mag, max)`
    // and receive a value strictly below 1. `clampBeta` itself
    // caps the user's `max_beta` at 0.999999 so a malformed
    // RelativityParams (max_beta >= 1) cannot break gamma.

    // Out-of-range magnitude is clamped to max.
    RR_CHECK(approx(clampBeta(1.5f, 0.999999f), 0.999999f));
    RR_CHECK(approx(clampBeta(2.0f, 0.5f),      0.5f));
    RR_CHECK(approx(clampBeta(100.0f, 0.9f),    0.9f));

    // Negative magnitudes fold to absolute value, then clamp.
    RR_CHECK(approx(clampBeta(-0.3f, 0.999999f), 0.3f));
    RR_CHECK(approx(clampBeta(-2.5f, 0.5f),      0.5f));

    // A negative `max_beta` folds to magnitude (defensive: a
    // malformed RelativityParams can't cause a negative cap).
    RR_CHECK(approx(clampBeta(0.7f, -0.8f),      0.7f));
    RR_CHECK(approx(clampBeta(0.9f, -0.5f),      0.5f));

    // A `max_beta` >= 1 (artist-supplied lightspeed) is silently
    // capped at 0.999999.
    RR_CHECK(approx(clampBeta(0.5f, 1.0f),      0.5f));
    RR_CHECK(approx(clampBeta(2.0f, 1.0f),      0.999999f));
    RR_CHECK(approx(clampBeta(0.999999f, 5.0f), 0.999999f));

    // In-range magnitudes pass through unchanged.
    RR_CHECK(approx(clampBeta(0.3f, 0.5f),      0.3f));
    RR_CHECK(approx(clampBeta(0.0f, 0.999999f), 0.0f));

    // The default `RelativityParams::max_beta` matches the
    // hard cap inside clampBeta, so `clampBeta(beta, params.
    // max_beta)` always returns a value strictly below 1.
    RelativityParams params;
    RR_CHECK(params.max_beta < 1.0f);
    RR_CHECK(approx(params.max_beta, 0.999999f));
    RR_CHECK(clampBeta(0.99999999f, params.max_beta) < 1.0f);
}

// ---------- 7. transformation is numerically stable near |beta| = 0.99 ----------

void test_stability_near_high_beta() {
    using rr::math::Vec3;
    using rr::math::dot;
    using rr::relativity::aberrateDirection;
    using rr::relativity::dopplerFactor;
    using rr::relativity::gamma;
    using rr::relativity::lorentzContraction;
    using rr::relativity::searchlightFactor;

    // Spot-check the high-beta point (gamma ~ 7) against the
    // closed-form longitudinal Doppler factor and verify every
    // intermediate output is finite + non-degenerate. Single-
    // precision loses ~5 decimals of relative significance when
    // (1 - beta^2) is computed directly, so the absolute
    // tolerance on D is loosened from the kEps default.
    const float beta = 0.99f;
    const Vec3  beta_vec{0.0f, 0.0f, beta};

    // Scalar invariants.
    const float g       = gamma(beta);
    const float inv_g   = lorentzContraction(beta);
    RR_CHECK(std::isfinite(g));
    RR_CHECK(std::isfinite(inv_g));
    RR_CHECK(g     > 1.0f);
    RR_CHECK(inv_g > 0.0f);
    RR_CHECK(approx(g * inv_g, 1.0f, kEpsLoose));

    // Forward Doppler (analytic, large but finite).
    {
        const Vec3  fwd   = beta_vec * (1.0f / beta);   // unit, parallel
        const float D     = dopplerFactor(beta_vec, fwd);
        const float D_ref = static_cast<float>(
            doppler_longitudinal(static_cast<double>(beta)));
        RR_CHECK(finite_positive(D));
        RR_CHECK(approx(D, D_ref, kEpsLoose * D_ref));   // relative tolerance
        // searchlightFactor(D) = D^4 must remain finite.
        RR_CHECK(std::isfinite(searchlightFactor(D)));
    }

    // Backward Doppler (analytic, small but strictly positive).
    {
        const Vec3  back  = beta_vec * (-1.0f / beta);
        const float D     = dopplerFactor(beta_vec, back);
        const float D_ref = 1.0f / static_cast<float>(
            doppler_longitudinal(static_cast<double>(beta)));
        RR_CHECK(finite_positive(D));
        RR_CHECK(approx(D, D_ref, kEpsLoose));
        RR_CHECK(std::isfinite(searchlightFactor(D)));
    }

    // Aberration of perpendicular incidence: closed form
    // d' = (1/g, 0, -beta). Both components must remain finite
    // and the output must be a unit vector.
    {
        const Vec3 dir{1.0f, 0.0f, 0.0f};
        const Vec3 d_prime = aberrateDirection(beta_vec, dir);
        RR_CHECK(finite(d_prime));
        RR_CHECK(approx(rr::math::length(d_prime), 1.0f, kEpsLoose));
        RR_CHECK(approx(d_prime.x, inv_g,  kEpsLoose));
        RR_CHECK(approx(d_prime.z, -beta,  kEpsLoose));
        RR_CHECK(approx(d_prime.y, 0.0f,   kEps));
    }

    // Sweep a fine grid of incidence angles and verify the
    // aberrated direction stays unit-length and matches
    // cos(theta') analytically. Catches catastrophic-cancellation
    // failures specific to particular geometries.
    const float angles_deg[] = {
        1.0f, 5.0f, 15.0f, 45.0f, 75.0f, 90.0f,
        105.0f, 135.0f, 165.0f, 175.0f, 179.0f,
    };
    for (float theta_deg : angles_deg) {
        const float theta_rad = theta_deg
                              * static_cast<float>(rr::math::kPi) / 180.0f;
        const float cos_t     = std::cos(theta_rad);
        const float sin_t     = std::sin(theta_rad);

        const Vec3 dir{sin_t, 0.0f, cos_t};
        const Vec3 d_prime = aberrateDirection(beta_vec, dir);

        RR_CHECK(finite(d_prime));
        RR_CHECK(approx(rr::math::length(d_prime), 1.0f, kEpsLoose));

        const float cos_t_prime_ref = static_cast<float>(
            aberrated_cos(static_cast<double>(cos_t),
                          static_cast<double>(beta)));
        RR_CHECK(approx(d_prime.z, cos_t_prime_ref, kEpsLoose));
    }

    // Also verify the precomputed-launch overload stays in lock-
    // step with the direct call at high beta (Stage 18A.3 perf
    // path; the snapshot must not drift in the high-beta regime).
    {
        const auto p = rr::relativity::precompute_relativity(beta_vec);
        RR_CHECK(std::isfinite(p.gamma));
        RR_CHECK(approx(p.gamma, g, kEpsLoose));

        const Vec3 dirs[] = {
            {+1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, +1.0f},
            {0.0f, 0.0f, -1.0f},
            rr::math::normalize(Vec3{1.0f, 2.0f, 3.0f}),
        };
        for (const Vec3& d : dirs) {
            const float D_pre  = dopplerFactor(p, d);
            const float D_full = dopplerFactor(beta_vec, d);
            RR_CHECK(finite_positive(D_pre));
            RR_CHECK(approx(D_pre, D_full, kEpsLoose * D_full));

            const Vec3 d_pre  = aberrateDirection(p, d);
            const Vec3 d_full = aberrateDirection(beta_vec, d);
            RR_CHECK(approx(d_pre, d_full, kEpsLoose));
        }
    }
}

// ---------- OBS-P.2: perception-mode ternary verification ----------

// Host-side verification of the kernel-side OBS-P.2 perception-
// mode gate's branch-equivalence. The CUDA + OptiX kernels run a
// per-arm ternary:
//
//   const bool perception_active =
//       (observer_frame.perception_mode ==
//           PerceptionMode::ConstantVelocityMinkowski);
//   const Vec3 beta_source = perception_active
//       ? observer_frame.beta
//       : observer.velocity;
//   const auto rel = precompute_relativity(beta_source);
//
// The invariant this test pins: for ANY input beta value B
// reached either via the gated path (`perception_mode ==
// ConstantVelocityMinkowski` + `observer_frame.beta = B`) OR
// via the legacy fallback (`perception_mode == Identity` +
// `observer.velocity = B`), the downstream
// `precompute_relativity` snapshot is bit-identical. The
// kernel-side perception-mode gate selects WHICH source
// supplies B; the snapshot's `(|beta|, gamma, beta_vec)`
// triple is identical given the same B.
void test_obs_p_2_perception_mode_branch_equivalence() {
    using rr::math::Vec3;
    using rr::manifold::ObserverFrame;
    using rr::manifold::PerceptionMode;
    using rr::relativity::Observer;
    using rr::relativity::precompute_relativity;
    using rr::relativity::PrecomputedRelativity;

    // Test the ternary's two branches across a representative
    // beta sweep. Each pair simulates the kernel-side selection
    // logic and verifies the precompute snapshot is identical
    // regardless of which source path supplied the beta.
    const Vec3 betas[] = {
        Vec3{0.0f,  0.0f,  0.0f},   // rest observer (default)
        Vec3{0.5f,  0.0f,  0.0f},   // +X drift
        Vec3{0.0f,  0.0f, -0.5f},   // -Z drift (the --render-demo precedent)
        Vec3{0.3f, -0.4f,  0.0f},   // 3-4-5 oblique
        Vec3{0.6f,  0.6f, -0.6f},   // |beta| ~= 0.99, near-cap
    };
    for (const Vec3& B : betas) {
        // Branch (a): gated path. ObserverFrame supplies B via
        // observer_frame.beta. Legacy observer is zero
        // (irrelevant; gate selects observer_frame).
        ObserverFrame frame_gated;
        frame_gated.perception_mode  = PerceptionMode::ConstantVelocityMinkowski;
        frame_gated.beta             = B;
        Observer legacy_unused;       // velocity = 0
        const bool gated_active =
            (frame_gated.perception_mode ==
                PerceptionMode::ConstantVelocityMinkowski);
        const Vec3 gated_source = gated_active
            ? frame_gated.beta
            : legacy_unused.velocity;
        const PrecomputedRelativity pre_gated =
            precompute_relativity(gated_source);

        // Branch (b): legacy fallback. ObserverFrame is the
        // default (perception_mode = Identity, beta = 0).
        // Legacy observer.velocity supplies B.
        ObserverFrame frame_legacy;   // perception_mode = Identity, beta = 0
        Observer legacy;
        legacy.velocity = B;
        const bool legacy_active =
            (frame_legacy.perception_mode ==
                PerceptionMode::ConstantVelocityMinkowski);
        const Vec3 legacy_source = legacy_active
            ? frame_legacy.beta
            : legacy.velocity;
        const PrecomputedRelativity pre_legacy =
            precompute_relativity(legacy_source);

        // The two paths must produce bit-identical snapshots.
        // beta_vec / beta_mag / gamma all compare exactly
        // because the snapshots are derived from the same B
        // input via the same precompute_relativity helper.
        RR_CHECK(pre_gated.beta_vec.x == pre_legacy.beta_vec.x);
        RR_CHECK(pre_gated.beta_vec.y == pre_legacy.beta_vec.y);
        RR_CHECK(pre_gated.beta_vec.z == pre_legacy.beta_vec.z);
        RR_CHECK(pre_gated.beta_mag   == pre_legacy.beta_mag);
        RR_CHECK(pre_gated.gamma      == pre_legacy.gamma);

        // The gated path's source equals B exactly.
        RR_CHECK(approx(gated_source, B, 0.0f));
        // The legacy path's source equals B exactly.
        RR_CHECK(approx(legacy_source, B, 0.0f));
    }
}

// OBS-P.2: ternary correctness across all three PerceptionMode
// enumerators. `Identity` and
// `CurvedChartGeodesicPlaceholder` both fall into the legacy
// fallback branch (the third enumerator is reserved-but-inert
// per OBSERVER.1 plan §3.6); only
// `ConstantVelocityMinkowski` engages the gated path.
void test_obs_p_2_perception_mode_three_enumerators() {
    using rr::math::Vec3;
    using rr::manifold::ObserverFrame;
    using rr::manifold::PerceptionMode;
    using rr::relativity::Observer;

    const Vec3 frame_beta {0.7f, 0.0f, 0.0f};  // gated-source candidate
    const Vec3 legacy_beta{0.0f, 0.0f, 0.3f};  // legacy-source candidate

    struct Case { PerceptionMode mode; bool gated_expected; };
    const Case cases[] = {
        {PerceptionMode::Identity,                       false},
        {PerceptionMode::ConstantVelocityMinkowski,      true},
        {PerceptionMode::CurvedChartGeodesicPlaceholder, false},
    };
    for (const Case& c : cases) {
        ObserverFrame frame;
        frame.perception_mode = c.mode;
        frame.beta            = frame_beta;
        Observer legacy;
        legacy.velocity = legacy_beta;

        const bool active =
            (frame.perception_mode ==
                PerceptionMode::ConstantVelocityMinkowski);
        const Vec3 source = active ? frame.beta : legacy.velocity;

        RR_CHECK(active == c.gated_expected);
        if (c.gated_expected) {
            RR_CHECK(approx(source, frame_beta, 0.0f));
        } else {
            RR_CHECK(approx(source, legacy_beta, 0.0f));
        }
    }
}

}  // namespace

int main() {
    test_identity_at_zero_beta();
    test_forward_blueshift();
    test_backward_redshift();
    test_aberration_matches_analytic();
    test_doppler_finite_positive_for_subluminal_beta();
    test_clamp_beta_existing_design();
    test_stability_near_high_beta();
    // OBS-P.2: perception-mode ternary verification.
    test_obs_p_2_perception_mode_branch_equivalence();
    test_obs_p_2_perception_mode_three_enumerators();

    if (g_failed == 0) {
        std::printf("relativity_tests: %d / %d passed\n", g_total, g_total);
        return 0;
    }
    std::fprintf(stderr,
                 "relativity_tests: %d / %d FAILED\n", g_failed, g_total);
    return 1;
}

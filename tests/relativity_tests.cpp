// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "relativity/RelativityMath.h"
#include "relativity/RelativityParams.h"

#include <cmath>
#include <cstdio>

namespace {

int g_total  = 0;
int g_failed = 0;

float abs_f(float a) { return a < 0.0f ? -a : a; }

bool nearly_equal(float a, float b, float eps = 1.0e-4f) {
    const float scale  = 1.0f > abs_f(a) ? 1.0f : abs_f(a);
    const float scale2 = scale > abs_f(b) ? scale : abs_f(b);
    return abs_f(a - b) <= eps * scale2;
}

bool nearly_equal(rr::math::Vec3 a, rr::math::Vec3 b, float eps = 1.0e-4f) {
    return nearly_equal(a.x, b.x, eps)
        && nearly_equal(a.y, b.y, eps)
        && nearly_equal(a.z, b.z, eps);
}

#define RR_CHECK(...)                                                         \
    do {                                                                      \
        ++g_total;                                                            \
        if (!(__VA_ARGS__)) {                                                 \
            ++g_failed;                                                       \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                        \
                         #__VA_ARGS__, __FILE__, __LINE__);                   \
        }                                                                     \
    } while (0)

using rr::math::Vec3;
using rr::relativity::aberrateDirection;
using rr::relativity::applyDopplerColor;
using rr::relativity::clampBeta;
using rr::relativity::dopplerFactor;
using rr::relativity::gamma;
using rr::relativity::lorentzContraction;
using rr::relativity::searchlightFactor;
using rr::relativity::Observer;
using rr::relativity::RelativityParams;

void test_clamp_beta() {
    RR_CHECK(nearly_equal(clampBeta(0.0f,  0.999999f), 0.0f));
    RR_CHECK(nearly_equal(clampBeta(0.5f,  0.999999f), 0.5f));
    RR_CHECK(nearly_equal(clampBeta(2.0f,  0.999999f), 0.999999f));
    // Negative input is folded to magnitude.
    RR_CHECK(nearly_equal(clampBeta(-0.7f, 0.999999f), 0.7f));
    // max_beta is itself capped at 0.999999.
    RR_CHECK(nearly_equal(clampBeta(0.5f,  2.0f),      0.5f));
    RR_CHECK(clampBeta(2.0f, 2.0f) <= 0.999999f);
}

void test_gamma() {
    RR_CHECK(nearly_equal(gamma(0.0f), 1.0f));
    // gamma(0.5) = 1 / sqrt(0.75) approx 1.1547
    RR_CHECK(nearly_equal(gamma(0.5f), 1.0f / std::sqrt(0.75f)));
    // gamma(0.6) -> 1.25 (3-4-5 triangle).
    RR_CHECK(nearly_equal(gamma(0.6f), 1.25f));
    // Near c, gamma is large but finite (no NaN).
    const float g_high = gamma(clampBeta(2.0f, 0.999999f));
    RR_CHECK(g_high > 100.0f);
    RR_CHECK(std::isfinite(g_high));
}

void test_lorentz_contraction() {
    RR_CHECK(nearly_equal(lorentzContraction(0.0f), 1.0f));
    RR_CHECK(nearly_equal(lorentzContraction(0.6f), 0.8f));            // 1/1.25
    RR_CHECK(nearly_equal(lorentzContraction(0.8f), 0.6f));            // 1/(5/3)
    // Identity vs 1/gamma at sample point.
    RR_CHECK(nearly_equal(lorentzContraction(0.5f), 1.0f / gamma(0.5f)));
}

void test_doppler_factor_at_rest() {
    // Zero velocity -> identity for any direction.
    RR_CHECK(nearly_equal(dopplerFactor({0, 0, 0}, {1, 0, 0}), 1.0f));
    RR_CHECK(nearly_equal(dopplerFactor({0, 0, 0}, {0, 1, 0}), 1.0f));
    RR_CHECK(nearly_equal(dopplerFactor({0, 0, 0}, {0, 0, 1}), 1.0f));
}

void test_doppler_factor_blue_and_redshift() {
    // beta . dir > 0 -> photon and observer co-move along dir;
    // (1 - beta.dir) shrinks -> D > 1 (blueshift).
    const float D_blue = dopplerFactor({0.5f, 0, 0}, {1, 0, 0});
    RR_CHECK(D_blue > 1.0f);

    // beta . dir < 0 -> opposite directions -> D < 1 (redshift).
    const float D_red = dopplerFactor({0.5f, 0, 0}, {-1, 0, 0});
    RR_CHECK(D_red < 1.0f);

    // Symmetry: for purely longitudinal opposite directions
    //   D(+dir) * D(-dir)
    //     = [1 / (g (1 - b))] * [1 / (g (1 + b))]
    //     = 1 / [g^2 (1 - b^2)]
    //     = 1                     (since g^2 (1 - b^2) = 1)
    RR_CHECK(nearly_equal(D_blue * D_red, 1.0f));
}

void test_doppler_factor_transverse() {
    // Transverse Doppler effect: pure |beta| with direction perpendicular
    // to it gives D = 1 / gamma (always a redshift).
    const float D = dopplerFactor({0.5f, 0, 0}, {0, 1, 0});
    RR_CHECK(nearly_equal(D, 1.0f / gamma(0.5f)));
}

void test_searchlight_factor() {
    RR_CHECK(nearly_equal(searchlightFactor(1.0f), 1.0f));
    RR_CHECK(nearly_equal(searchlightFactor(2.0f), 16.0f));
    RR_CHECK(nearly_equal(searchlightFactor(0.5f), 0.0625f));
}

void test_aberrate_direction_zero_beta_identity() {
    const Vec3 d{0, 0, -1};
    RR_CHECK(nearly_equal(aberrateDirection({0, 0, 0}, d), d));
    RR_CHECK(nearly_equal(aberrateDirection({0, 0, 0}, Vec3{1, 0, 0}),
                                            Vec3{1, 0, 0}));
}

void test_aberrate_direction_unit_length() {
    // Output should be unit length (or very close) for any input.
    const auto d = aberrateDirection({0.7f, 0, 0}, Vec3{0, 1, 0});
    RR_CHECK(nearly_equal(rr::math::length(d), 1.0f));
}

void test_aberrate_direction_perpendicular_tilts_photon_backward() {
    // Observer moving in +X. A photon traveling in +Y in the scene
    // frame, viewed in the observer's frame, has its direction of
    // travel tilted toward -X. This is the photon-direction
    // counterpart of forward beaming: the apparent SOURCE shifts
    // forward (toward +X) but the PHOTON'S direction-of-travel
    // shifts backward (toward -X). Both are descriptions of the
    // same Lorentz aberration.
    const auto d = aberrateDirection({0.7f, 0, 0}, Vec3{0, 1, 0});
    RR_CHECK(d.x < 0.0f);
    RR_CHECK(d.y > 0.0f);                 // still mostly upward
    RR_CHECK(abs_f(d.x) > 0.3f);          // shift is substantial at beta = 0.7
    // Closed-form check: at zero parallel component the formula
    // collapses to (-beta, |perp| / gamma, 0).
    const float g = gamma(0.7f);
    RR_CHECK(nearly_equal(d.x, -0.7f));
    RR_CHECK(nearly_equal(d.y, 1.0f / g));
}

void test_aberrate_direction_along_motion_invariant() {
    // A photon already arriving along +X with the observer also moving
    // along +X is invariant under aberration: parallel velocity and
    // direction means the perpendicular component is zero, so no
    // angular shift occurs.
    const auto d = aberrateDirection({0.7f, 0, 0}, Vec3{1, 0, 0});
    RR_CHECK(nearly_equal(d, Vec3{1, 0, 0}, 1.0e-3f));
}

void test_apply_doppler_color_identity_paths() {
    const Vec3 rgb{0.5f, 0.6f, 0.7f};
    // strength = 0 -> identity.
    RR_CHECK(nearly_equal(applyDopplerColor(rgb, 1.7f, 0.0f), rgb));
    // D = 1 -> identity even at full strength (log(1) = 0).
    RR_CHECK(nearly_equal(applyDopplerColor(rgb, 1.0f, 1.0f), rgb));
}

void test_apply_doppler_color_redshift_warms() {
    // Strong redshift, full strength: red channel rises relative to
    // green and blue compared to the unshifted input.
    const Vec3  rgb{0.5f, 0.5f, 0.5f};
    const float D_red = 0.25f;  // significant redshift
    const Vec3  out   = applyDopplerColor(rgb, D_red, 1.0f);

    RR_CHECK(out.x > 0.0f);
    // Red is preserved (warm tint multiplier = 1 in the R channel) while
    // green and blue are dampened, so R should be at least as large as
    // both other channels.
    RR_CHECK(out.x >= out.y - 1.0e-5f);
    RR_CHECK(out.x >= out.z - 1.0e-5f);
    RR_CHECK(out.y < rgb.y);
    RR_CHECK(out.z < rgb.z);
}

void test_apply_doppler_color_blueshift_cools() {
    const Vec3  rgb{0.5f, 0.5f, 0.5f};
    const float D_blue = 4.0f;  // significant blueshift
    const Vec3  out    = applyDopplerColor(rgb, D_blue, 1.0f);

    // Cool tint preserves blue; red and green get dampened.
    RR_CHECK(out.z >= out.x - 1.0e-5f);
    RR_CHECK(out.z >= out.y - 1.0e-5f);
    RR_CHECK(out.x < rgb.x);
    RR_CHECK(out.y < rgb.y);
}

void test_relativity_params_defaults() {
    RelativityParams p;
    RR_CHECK(p.enable_aberration);
    RR_CHECK(p.enable_doppler);
    RR_CHECK(p.enable_searchlight);
    RR_CHECK(nearly_equal(p.doppler_color_strength, 1.0f));
    RR_CHECK(nearly_equal(p.searchlight_strength,   1.0f));
    RR_CHECK(p.max_beta > 0.99f && p.max_beta < 1.0f);

    // Observer defaults to rest.
    Observer obs;
    RR_CHECK(nearly_equal(obs.velocity, Vec3{0, 0, 0}));
}

}

int main() {
    test_clamp_beta();
    test_gamma();
    test_lorentz_contraction();
    test_doppler_factor_at_rest();
    test_doppler_factor_blue_and_redshift();
    test_doppler_factor_transverse();
    test_searchlight_factor();
    test_aberrate_direction_zero_beta_identity();
    test_aberrate_direction_unit_length();
    test_aberrate_direction_perpendicular_tilts_photon_backward();
    test_aberrate_direction_along_motion_invariant();
    test_apply_doppler_color_identity_paths();
    test_apply_doppler_color_redshift_warms();
    test_apply_doppler_color_blueshift_cools();
    test_relativity_params_defaults();

    std::printf("relativity_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

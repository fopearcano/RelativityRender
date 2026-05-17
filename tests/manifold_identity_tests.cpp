// MANIFOLD.7 - manifold layer identity tests.
//
// Verifies that the `rr_manifold` library's six POD types and their
// helpers are a "default no-op" - i.e. a renderer that constructs every
// manifold value via its default constructor (and the published default
// factories) gets the same logical result as the pre-pivot renderer.
//
// Coverage matches the MANIFOLD.7 task brief:
//   1. Minkowski / Euclidean metric creation        (MetricTensor)
//   2. CoordinateChart default is Euclidean         (CoordinateChart)
//   3. worldToChart identity                        (ManifoldTransform)
//   4. chartToWorld identity                        (ManifoldTransform)
//   5. transformDirection identity                  (ManifoldTransform)
//   6. ManifoldMode disabled by default             (ManifoldMode)
//
// Bonus invariants - kept close so a regression in one of the
// dependent types (ObserverFrame, GeodesicState) trips this same test
// binary rather than waiting for a future slice's test set:
//   - rest_frame() matches the documented scene-rest defaults and is
//     timelike-normalised under Minkowski;
//   - default_geodesic_state() satisfies the null condition on
//     Minkowski;
//   - the observer-frame bridge to `rr::relativity::Observer` round-
//     trips beta exactly at moderate magnitudes.
//
// Conventions
// -----------
// Hand-rolled assertions via `RR_CHECK`; `g_failed` counts deviations;
// `main` returns 0 iff `g_failed == 0`. Same shape as math_tests.cpp.

#include "manifold/CameraObserverAdapter.h"
#include "manifold/CoordinateChart.h"
#include "manifold/GeodesicState.h"
#include "manifold/ManifoldMode.h"
#include "manifold/ManifoldTransform.h"
#include "manifold/MetricTensor.h"
#include "manifold/ObserverFrame.h"
#include "manifold/PenroseLikeCompactification.h"
#include "manifold/SchwarzschildLikeWarp.h"

#include "camera/CameraRay.h"
#include "relativity/RelativityParams.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

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

constexpr float kEps = 1e-6f;

bool approx(float a, float b, float eps = kEps) {
    return std::fabs(a - b) <= eps;
}

bool approx(rr::math::Vec3 a, rr::math::Vec3 b, float eps = kEps) {
    return approx(a.x, b.x, eps)
        && approx(a.y, b.y, eps)
        && approx(a.z, b.z, eps);
}

bool approx(rr::math::Vec4 a, rr::math::Vec4 b, float eps = kEps) {
    return approx(a.x, b.x, eps)
        && approx(a.y, b.y, eps)
        && approx(a.z, b.z, eps)
        && approx(a.w, b.w, eps);
}

// `g_{mu nu} p^mu p^nu` contraction under an arbitrary metric.
// Lets the tests verify the null condition on a `GeodesicState`'s
// `momentum4` without leaking the contraction into the manifold
// headers themselves (architecture-doc §3.4: validators belong on
// the future integrator, not the data POD).
float metric_contract(const rr::manifold::MetricTensor& g,
                      rr::math::Vec4 p) {
    const float v[4] = {p.x, p.y, p.z, p.w};
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            s += g.at(i, j) * v[i] * v[j];
        }
    }
    return s;
}

// ---------- 1. Minkowski / Euclidean metric creation ----------

void test_minkowski_metric_creation() {
    using namespace rr::manifold;

    // Default-constructed metric IS the mostly-plus Minkowski metric.
    MetricTensor mk{};
    RR_CHECK(approx(mk.at(0, 0), -1.0f));
    RR_CHECK(approx(mk.at(1, 1), +1.0f));
    RR_CHECK(approx(mk.at(2, 2), +1.0f));
    RR_CHECK(approx(mk.at(3, 3), +1.0f));

    // Off-diagonals are all zero.
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (i == j) continue;
            RR_CHECK(approx(mk.at(i, j), 0.0f));
        }
    }

    // The minkowski_metric() factory returns the same value.
    MetricTensor mk_factory = minkowski_metric();
    for (int k = 0; k < 16; ++k) {
        RR_CHECK(approx(mk.g[k], mk_factory.g[k]));
    }

    // Validation helpers all hold on the canonical Minkowski metric.
    RR_CHECK(is_minkowski(mk));
    RR_CHECK(is_symmetric(mk));
    RR_CHECK(is_diagonal(mk));
    RR_CHECK(is_finite(mk));

    // The Minkowski determinant is exactly -1 (closed-form Laplace
    // expansion; see MetricTensor.h `determinant`).
    RR_CHECK(determinant(mk) == -1.0f);

    // The 4D-Euclidean `identity_metric` is the positive-definite
    // diag(+1, +1, +1, +1) - NOT the renderer's spacetime metric, but
    // a useful clean baseline.
    MetricTensor id = identity_metric();
    RR_CHECK(approx(id.at(0, 0), +1.0f));
    RR_CHECK(approx(id.at(1, 1), +1.0f));
    RR_CHECK(approx(id.at(2, 2), +1.0f));
    RR_CHECK(approx(id.at(3, 3), +1.0f));
    RR_CHECK(determinant(id) == +1.0f);

    // The two factories produce *different* metrics (sanity check
    // that identity_metric isn't accidentally aliasing Minkowski).
    RR_CHECK(!is_minkowski(id));
    RR_CHECK( is_symmetric(id));
    RR_CHECK( is_diagonal(id));
    RR_CHECK( is_finite(id));
}

// ---------- 2. CoordinateChart default is Euclidean ----------

void test_euclidean_chart_default() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // The bare default-initialiser sets type = Euclidean.
    CoordinateChart c{};
    RR_CHECK(c.type  == CoordinateChartType::Euclidean);
    RR_CHECK(c.scale == 1.0f);
    RR_CHECK(approx(c.origin, Vec3{0.0f, 0.0f, 0.0f}));
    RR_CHECK(c.units == ChartUnits::SceneNatural);

    // Parameter bag is the documented "all-zero, compactification=1" set.
    RR_CHECK(c.params.mass                   == 0.0f);
    RR_CHECK(c.params.spin                   == 0.0f);
    RR_CHECK(c.params.compactification_scale == 1.0f);
    RR_CHECK(c.params.reserved               == 0.0f);

    // The factory and the backward-compat alias both return the same
    // shipping default.
    CoordinateChart e = euclidean_chart();
    CoordinateChart i = identity_chart();
    RR_CHECK(e.type   == c.type);
    RR_CHECK(e.scale  == c.scale);
    RR_CHECK(i.type   == c.type);
    RR_CHECK(i.scale  == c.scale);

    // The `ManifoldTransform` aggregate picks up the Euclidean default
    // through its `chart` member (no chart wiring needed on the
    // transform).
    ManifoldTransform t = identity_transform();
    RR_CHECK(t.chart.type == CoordinateChartType::Euclidean);
}

// ---------- 3. world_to_chart identity ----------

void test_world_to_chart_identity() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    ManifoldTransform t = identity_transform();

    // Spatial Vec3: identity on a few representative inputs.
    RR_CHECK(approx(world_to_chart(t, Vec3{0.0f, 0.0f, 0.0f}),
                    Vec3{0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(world_to_chart(t, Vec3{1.5f, -2.3f, 4.7f}),
                    Vec3{1.5f, -2.3f, 4.7f}));
    RR_CHECK(approx(world_to_chart(t, Vec3{-100.0f, 0.5f, 99.9f}),
                    Vec3{-100.0f, 0.5f, 99.9f}));

    // Spacetime Vec4: identity, including the time component.
    RR_CHECK(approx(world_to_chart(t, Vec4{0.0f, 0.0f, 0.0f, 0.0f}),
                    Vec4{0.0f, 0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(world_to_chart(t, Vec4{0.5f, 1.5f, -2.3f, 4.7f}),
                    Vec4{0.5f, 1.5f, -2.3f, 4.7f}));

    // Round-trip via chart_to_world IS the identity on the default.
    const Vec3 p3{1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(chart_to_world(t, world_to_chart(t, p3)), p3));
    const Vec4 p4{0.5f, 1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(chart_to_world(t, world_to_chart(t, p4)), p4));
}

// ---------- 4. chart_to_world identity ----------

void test_chart_to_world_identity() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    ManifoldTransform t = identity_transform();

    // Spatial Vec3.
    RR_CHECK(approx(chart_to_world(t, Vec3{0.0f, 0.0f, 0.0f}),
                    Vec3{0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(chart_to_world(t, Vec3{1.5f, -2.3f, 4.7f}),
                    Vec3{1.5f, -2.3f, 4.7f}));

    // Spacetime Vec4.
    RR_CHECK(approx(chart_to_world(t, Vec4{0.5f, 1.5f, -2.3f, 4.7f}),
                    Vec4{0.5f, 1.5f, -2.3f, 4.7f}));

    // Reverse round-trip.
    const Vec3 c3{2.0f, 3.0f, -1.0f};
    RR_CHECK(approx(world_to_chart(t, chart_to_world(t, c3)), c3));
}

// ---------- 5. transform_direction identity ----------

void test_transform_direction_identity() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    ManifoldTransform t = identity_transform();

    // transform_direction (Vec3): identity on the default transform.
    RR_CHECK(approx(transform_direction(t, Vec3{1.0f, 0.0f, 0.0f}),
                    Vec3{1.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(transform_direction(t, Vec3{0.6f, 0.0f, 0.8f}),
                    Vec3{0.6f, 0.0f, 0.8f}));

    // transform_ray_like_direction (Vec3): identity on a unit-length
    // input regardless of scale - here scale = 1, so just identity.
    Vec3 unit_d = Vec3{0.6f, 0.0f, 0.8f};
    RR_CHECK(approx(transform_ray_like_direction(t, unit_d), unit_d));

    // transform_direction (Vec4): identity, including the time
    // component.
    RR_CHECK(approx(transform_direction(t, Vec4{1.0f, 0.6f, 0.0f, 0.8f}),
                    Vec4{1.0f, 0.6f, 0.0f, 0.8f}));

    // transform_ray_like_direction (Vec4): identity. Verify the
    // null-condition input remains null after the transform.
    Vec4 null4{1.0f, 0.6f, 0.0f, 0.8f};
    RR_CHECK(approx(metric_contract(minkowski_metric(), null4), 0.0f));
    Vec4 null4_out = transform_ray_like_direction(t, null4);
    RR_CHECK(approx(null4_out, null4));
    RR_CHECK(approx(metric_contract(minkowski_metric(), null4_out), 0.0f));
}

// ---------- 6. ManifoldMode disabled by default ----------

void test_manifold_mode_disabled_by_default() {
    using namespace rr::manifold;

    // The bare default-initialiser matches the task brief's
    // documented "no output change" defaults.
    ManifoldMode m{};
    RR_CHECK(m.enabled                                == false);
    RR_CHECK(m.chart                                  == CoordinateChartType::Euclidean);
    RR_CHECK(m.strength                               == 0.0f);
    RR_CHECK(m.debug_visualization                    == false);
    RR_CHECK(m.preserve_light_speed_normally          == true);
    RR_CHECK(m.transform_coordinates_instead_of_light == true);

    // The factory returns the same value as the default initialiser.
    ManifoldMode f = disabled_manifold_mode();
    RR_CHECK(f.enabled                                == m.enabled);
    RR_CHECK(f.chart                                  == m.chart);
    RR_CHECK(f.strength                               == m.strength);
    RR_CHECK(f.debug_visualization                    == m.debug_visualization);
    RR_CHECK(f.preserve_light_speed_normally          == m.preserve_light_speed_normally);
    RR_CHECK(f.transform_coordinates_instead_of_light == m.transform_coordinates_instead_of_light);
}

// ---------- Bonus invariants: ObserverFrame / GeodesicState ----------

void test_observer_frame_defaults() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // rest_frame() matches the documented scene-rest observer.
    ObserverFrame rf = rest_frame();
    RR_CHECK(approx(rf.position4, Vec4{0.0f, 0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(rf.velocity4, Vec4{1.0f, 0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(rf.beta,      Vec3{0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(rf.right,     Vec3{1.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(rf.up,        Vec3{0.0f, 1.0f, 0.0f}));
    RR_CHECK(approx(rf.forward,   Vec3{0.0f, 0.0f, 1.0f}));
    RR_CHECK(rf.proper_time     == 0.0f);
    RR_CHECK(rf.coordinate_time == 0.0f);

    // Rest observer is normalised under Minkowski: g(u, u) = -1.
    RR_CHECK(is_normalised_timelike(rf, minkowski_metric()));

    // observer_frame_from(Observer{velocity = 0}) reproduces
    // rest_frame() (the legacy SR Observer at rest maps to the new
    // scene-rest observer).
    rr::relativity::Observer obs_rest;
    ObserverFrame from_rest = observer_frame_from(obs_rest);
    RR_CHECK(approx(from_rest.velocity4, rf.velocity4));
    RR_CHECK(approx(from_rest.beta,      rf.beta));

    // observer_frame_from at beta = (0.3, -0.4, 0) round-trips
    // through to_relativity_observer with beta preserved exactly.
    rr::relativity::Observer obs_rt;
    obs_rt.velocity = Vec3{0.3f, -0.4f, 0.0f};
    ObserverFrame f = observer_frame_from(obs_rt);
    rr::relativity::Observer back = to_relativity_observer(f);
    RR_CHECK(approx(back.velocity, obs_rt.velocity));

    // At |beta| = 0.5 the four-velocity remains timelike-normalised
    // under Minkowski.
    RR_CHECK(is_normalised_timelike(f, minkowski_metric()));

    // OBSERVER.2: default perception mode on rest_frame() is the
    // no-op anchor (Identity); rest-frame round-trip through the
    // bridge helpers preserves the Identity default.
    RR_CHECK(rf.perception_mode       == PerceptionMode::Identity);
    RR_CHECK(from_rest.perception_mode == PerceptionMode::Identity);
}

void test_geodesic_state_defaults() {
    using namespace rr::manifold;
    using rr::math::Vec4;

    GeodesicState gs = default_geodesic_state();
    RR_CHECK(approx(gs.position4, Vec4{0.0f, 0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(gs.momentum4, Vec4{1.0f, 0.0f, 0.0f, 1.0f}));
    RR_CHECK(gs.affine_parameter            == 0.0f);
    RR_CHECK(gs.valid                       == true);
    RR_CHECK(gs.accumulated_optical_depth   == 0.0f);
    RR_CHECK(gs.diagnostic_curvature        == 0.0f);

    // The default photon satisfies the null condition on Minkowski:
    //   g_{mu nu} p^mu p^nu = -E^2 + |p|^2 = -1 + 1 = 0.
    RR_CHECK(approx(metric_contract(minkowski_metric(), gs.momentum4),
                    0.0f));

    // GeodesicStatus enumerators are preserved and distinct.
    RR_CHECK(GeodesicStatus::InFlight       != GeodesicStatus::ChartBoundary);
    RR_CHECK(GeodesicStatus::InFlight       != GeodesicStatus::Terminated);
    RR_CHECK(GeodesicStatus::ChartBoundary  != GeodesicStatus::Terminated);
}

// ---------- SCHW.1: Schwarzschild-like artistic coordinate-warp math ----------

void test_schw_1_validate_params() {
    using namespace rr::manifold;

    // Default-constructed params (all-zero r_s + warp_strength,
    // falloff=1.0, clamp_radius=1.0) pass validation. The chart
    // is effectively Euclidean in this state because both r_s
    // and warp_strength default to 0.
    SchwarzschildLikeWarpParams ok{};
    RR_CHECK(schwarzschild_like_validate_params(ok));

    // Typical non-trivial parameters validate.
    SchwarzschildLikeWarpParams typical{};
    typical.r_s           = 1.0f;
    typical.warp_strength = 1.0f;
    typical.falloff       = 1.0f;
    typical.clamp_radius  = 0.1f;
    RR_CHECK(schwarzschild_like_validate_params(typical));

    // Negative r_s rejected.
    SchwarzschildLikeWarpParams neg_rs = typical;
    neg_rs.r_s = -1.0f;
    RR_CHECK(!schwarzschild_like_validate_params(neg_rs));

    // Falloff out of [0.5, 4.0] rejected.
    SchwarzschildLikeWarpParams falloff_low = typical;
    falloff_low.falloff = 0.4f;
    RR_CHECK(!schwarzschild_like_validate_params(falloff_low));
    SchwarzschildLikeWarpParams falloff_high = typical;
    falloff_high.falloff = 4.1f;
    RR_CHECK(!schwarzschild_like_validate_params(falloff_high));

    // clamp_radius <= 0 rejected (NaN guard for the 1/r denominator).
    SchwarzschildLikeWarpParams clamp_zero = typical;
    clamp_zero.clamp_radius = 0.0f;
    RR_CHECK(!schwarzschild_like_validate_params(clamp_zero));
    SchwarzschildLikeWarpParams clamp_neg = typical;
    clamp_neg.clamp_radius = -0.1f;
    RR_CHECK(!schwarzschild_like_validate_params(clamp_neg));

    // Non-finite values rejected.
    SchwarzschildLikeWarpParams nan_p = typical;
    nan_p.warp_strength = std::nanf("");
    RR_CHECK(!schwarzschild_like_validate_params(nan_p));
}

void test_schw_1_world_to_chart_euclidean_fallback() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // warp_strength = 0 ⇒ identity, regardless of r_s / falloff /
    // clamp_radius / mass_origin / input position.
    SchwarzschildLikeWarpParams p{};
    p.r_s           = 1.0f;
    p.warp_strength = 0.0f;
    p.falloff       = 1.0f;
    p.clamp_radius  = 0.1f;
    const Vec3 mass_origin{0.0f, 0.0f, 0.0f};

    const Vec3 p1{0.0f, 0.0f, 0.0f};
    const Vec3 p2{1.5f, -2.3f, 4.7f};
    const Vec3 p3{-100.0f, 0.5f, 99.9f};
    RR_CHECK(schwarzschild_like_world_to_chart(p1, mass_origin, p) == p1);
    RR_CHECK(schwarzschild_like_world_to_chart(p2, mass_origin, p) == p2);
    RR_CHECK(schwarzschild_like_world_to_chart(p3, mass_origin, p) == p3);

    // r_s = 0 with non-zero warp_strength ⇒ also identity.
    SchwarzschildLikeWarpParams q = p;
    q.warp_strength = 1.0f;
    q.r_s           = 0.0f;
    RR_CHECK(schwarzschild_like_world_to_chart(p2, mass_origin, q) == p2);
}

void test_schw_1_world_to_chart_far_field_identity() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::Vec3;

    // r → ∞ ⇒ displacement → 0. Verified at a large `r`:
    //   displacement = warp_strength * r_s / r^falloff * delta
    //   At r = 1.0e6, r_s = 1, falloff = 1, warp_strength = 1:
    //     |displacement| = (1 * 1 / 1.0e6) * 1.0e6 = 1.0  (per axis)
    //   So the displacement scalar is ~1 unit, which is small
    //   compared to the input magnitude of 1e6. We verify the
    //   RELATIVE displacement is small.
    SchwarzschildLikeWarpParams p{};
    p.r_s           = 1.0f;
    p.warp_strength = 1.0f;
    p.falloff       = 1.0f;
    p.clamp_radius  = 0.1f;

    const Vec3 mass_origin{0.0f, 0.0f, 0.0f};
    const Vec3 p_far{1.0e6f, 0.0f, 0.0f};
    const Vec3 chart_far = schwarzschild_like_world_to_chart(p_far, mass_origin, p);
    const float rel_displacement = length(chart_far - p_far) / length(p_far);
    RR_CHECK(rel_displacement < 1.0e-3f);
}

void test_schw_1_world_to_chart_known_value() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // Worked example from the plan's §2 algebra:
    //   warp_strength=1, r_s=1, falloff=1, clamp_radius=0.1,
    //   mass_origin=(0,0,0), p_world=(2,0,0).
    //   r = 2; f = 1 * 1 / 2 = 0.5;
    //   chart_pos = (2,0,0) + 0.5 * (2,0,0) = (3,0,0).
    SchwarzschildLikeWarpParams p{};
    p.r_s           = 1.0f;
    p.warp_strength = 1.0f;
    p.falloff       = 1.0f;
    p.clamp_radius  = 0.1f;
    const Vec3 mass_origin{0.0f, 0.0f, 0.0f};
    const Vec3 p_world{2.0f, 0.0f, 0.0f};
    const Vec3 chart = schwarzschild_like_world_to_chart(p_world, mass_origin, p);
    RR_CHECK(approx(chart, Vec3{3.0f, 0.0f, 0.0f}));
}

void test_schw_1_world_to_chart_clamp_radius_safety() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // At p_world = mass_origin, the formula reduces to:
    //   delta = (0,0,0); r = max(0, clamp_radius) = clamp_radius;
    //   chart_pos = mass_origin + 0 * (whatever) = mass_origin.
    // No NaN / Inf even though the naïve `1/0` would diverge.
    SchwarzschildLikeWarpParams p{};
    p.r_s           = 1.0f;
    p.warp_strength = 1.0f;
    p.falloff       = 1.0f;
    p.clamp_radius  = 0.1f;
    const Vec3 mass_origin{2.0f, 0.0f, 0.0f};
    const Vec3 p_at_mass = mass_origin;
    const Vec3 chart = schwarzschild_like_world_to_chart(p_at_mass, mass_origin, p);
    RR_CHECK(approx(chart, mass_origin));
}

void test_schw_1_chart_to_world_inverse_residual() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::Vec3;

    // Forward → inverse round-trip should reproduce the input
    // to within the plan §5.4 documented residual `<= 1e-4`.
    // Test across a representative parameter sweep.
    SchwarzschildLikeWarpParams p{};
    p.clamp_radius = 0.1f;
    const Vec3 mass_origin{0.0f, 0.0f, 0.0f};

    struct Case {
        float r_s, ws, fall;
        rr::math::Vec3 p_world;
    };
    const Case cases[] = {
        {1.0f, 0.5f, 1.0f, {3.0f, 0.0f, 0.0f}},
        {1.0f, 1.0f, 1.0f, {2.0f, 0.0f, 0.0f}},
        {0.5f, 0.25f, 1.0f, {5.0f, 0.0f, 0.0f}},
        {1.0f, 1.0f, 2.0f, {4.0f, 0.0f, 0.0f}},
        {2.0f, 0.5f, 1.0f, {1.5f, 0.5f, -1.0f}},
        // Edge: r close to (but above) clamp_radius.
        {1.0f, 1.0f, 1.0f, {0.5f, 0.0f, 0.0f}},
    };

    for (const auto& c : cases) {
        p.r_s           = c.r_s;
        p.warp_strength = c.ws;
        p.falloff       = c.fall;
        const Vec3 chart = schwarzschild_like_world_to_chart(c.p_world, mass_origin, p);
        const Vec3 back  = schwarzschild_like_chart_to_world(chart, mass_origin, p);
        const float residual = length(back - c.p_world);
        RR_CHECK(residual < 1.0e-4f);
    }
}

void test_schw_1_chart_to_world_euclidean_fallback() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // warp_strength = 0 ⇒ chart_to_world is identity.
    SchwarzschildLikeWarpParams p{};
    p.r_s           = 1.0f;
    p.warp_strength = 0.0f;
    p.falloff       = 1.0f;
    p.clamp_radius  = 0.1f;
    const Vec3 mass_origin{1.0f, 2.0f, 3.0f};
    const Vec3 chart{4.7f, -1.2f, 0.5f};
    RR_CHECK(schwarzschild_like_chart_to_world(chart, mass_origin, p) == chart);
}

void test_schw_1_warp_ray_direction_euclidean_fallback() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // warp_strength = 0 OR r_s = 0 ⇒ unchanged direction.
    SchwarzschildLikeWarpParams p{};
    p.r_s           = 1.0f;
    p.warp_strength = 0.0f;
    p.falloff       = 1.0f;
    p.clamp_radius  = 0.1f;
    const Vec3 origin{0.0f, 0.0f, 0.0f};
    const Vec3 mass_origin{2.0f, 0.0f, 0.0f};
    const Vec3 dir{0.0f, 0.0f, -1.0f};
    RR_CHECK(schwarzschild_like_warp_ray_direction(origin, dir, mass_origin, p) == dir);

    SchwarzschildLikeWarpParams q = p;
    q.warp_strength = 1.0f;
    q.r_s           = 0.0f;
    RR_CHECK(schwarzschild_like_warp_ray_direction(origin, dir, mass_origin, q) == dir);
}

void test_schw_1_warp_ray_direction_bend_cap() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::normalize;
    using rr::math::Vec3;

    // Hard cap at ±0.5: even with extreme parameters the output
    // is finite, unit-length, and does not flip direction.
    SchwarzschildLikeWarpParams p{};
    p.r_s           = 1000.0f;   // huge
    p.warp_strength = 100.0f;    // far above nominal [0, 1]
    p.falloff       = 1.0f;
    p.clamp_radius  = 0.1f;

    const Vec3 origin{0.0f, 0.0f, 0.0f};
    const Vec3 mass_origin{0.0f, 0.0f, -5.0f};  // straight ahead
    const Vec3 dir{0.0f, 0.0f, -1.0f};
    const Vec3 out = schwarzschild_like_warp_ray_direction(origin, dir, mass_origin, p);
    // Output must be finite + unit-length.
    RR_CHECK(std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z));
    RR_CHECK(approx(length(out), 1.0f, 1.0e-5f));
    // For a ray pointing straight at the mass, the bend has no
    // effect (bend_dir is parallel to dir) — the cap doesn't
    // matter here. We just verify the helper produced a stable
    // unit-length vector.
}

void test_schw_1_warp_ray_direction_bends_toward_mass() {
    using namespace rr::manifold;
    using rr::math::dot;
    using rr::math::Vec3;

    // Ray going +Z; mass at (+x, 0, 0). After warp, the dir
    // should pick up a +x component (bent toward the mass).
    SchwarzschildLikeWarpParams p{};
    p.r_s           = 1.0f;
    p.warp_strength = 1.0f;
    p.falloff       = 1.0f;
    p.clamp_radius  = 0.1f;
    const Vec3 origin{0.0f, 0.0f, 0.0f};
    const Vec3 mass_origin{1.0f, 0.0f, 0.0f};
    const Vec3 dir{0.0f, 0.0f, 1.0f};
    const Vec3 warped = schwarzschild_like_warp_ray_direction(origin, dir, mass_origin, p);
    // Expect warped.x > 0 (bent toward the mass on the +x axis).
    RR_CHECK(warped.x > 0.0f);
    // Expect warped.z > 0 (still mostly along the original direction).
    RR_CHECK(warped.z > 0.0f);
}

// ---------- SCHW.3: ManifoldTransform integration ----------
//
// Verifies that `world_to_chart` / `chart_to_world` on
// `ManifoldTransform` invoke the SCHW.1 math leaf when the
// active chart is `SchwarzschildLike`, while preserving the
// "default no-op" contract for the disabled / Euclidean
// configurations. Plan: `SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §6.1
// / §6.2.

// Build a SchwarzschildLike `CoordinateChart` from the
// "worked example" parameters in the plan §2 algebra. Mass at
// origin; `r_s = 1`, `falloff = 1`, `clamp_radius = 0.1`.
rr::manifold::CoordinateChart make_schwarzschild_like_chart() {
    rr::manifold::CoordinateChart c{};
    c.type   = rr::manifold::CoordinateChartType::SchwarzschildLike;
    c.name   = "schwarzschild-like";
    c.origin = rr::math::Vec3{0.0f, 0.0f, 0.0f};   // mass at origin
    c.params.mass                   = 1.0f;        // r_s
    c.params.spin                   = 1.0f;        // falloff
    c.params.compactification_scale = 0.1f;        // clamp_radius
    return c;
}

void test_schw_3_disabled_identity_preserved() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Default ManifoldTransform (Euclidean chart, identity scale,
    // zero origin): every helper is the identity. SCHW.3 does
    // not change this behaviour; this test re-runs the post-
    // MANIFOLD.5 invariant against the post-SCHW.3 build.
    ManifoldTransform t = identity_transform();
    RR_CHECK(t.chart.type == CoordinateChartType::Euclidean);

    const Vec3 p3{1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(world_to_chart(t, p3), p3));
    RR_CHECK(approx(chart_to_world(t, p3), p3));

    const Vec4 p4{0.5f, 1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(world_to_chart(t, p4), p4));
    RR_CHECK(approx(chart_to_world(t, p4), p4));
}

void test_schw_3_euclidean_identity_preserved() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Explicit Euclidean chart (`type = Euclidean`, non-trivial
    // origin / scale): the SCHW.3 changes are gated on
    // `type == SchwarzschildLike`, so the Euclidean affine map
    // continues to apply unchanged. Bit-identity preserved.
    ManifoldTransform t = identity_transform();
    t.chart.type   = CoordinateChartType::Euclidean;
    t.chart.origin = rr::math::Vec3{1.0f, 2.0f, 3.0f};
    t.chart.scale  = 2.0f;

    const Vec3 world_pos{3.0f, 6.0f, 9.0f};
    // (world - origin) / scale = (2, 4, 6) / 2 = (1, 2, 3).
    RR_CHECK(approx(world_to_chart(t, world_pos), Vec3{1.0f, 2.0f, 3.0f}));
    // Round-trip is exact under uniform scaling.
    RR_CHECK(approx(chart_to_world(t, world_to_chart(t, world_pos)), world_pos));

    const Vec4 world4{0.5f, 3.0f, 6.0f, 9.0f};
    // Time invariant, spatial part transformed.
    RR_CHECK(approx(world_to_chart(t, world4),
                    Vec4{0.5f, 1.0f, 2.0f, 3.0f}));
    RR_CHECK(approx(chart_to_world(t, world_to_chart(t, world4)), world4));
}

void test_schw_3_schwarzschild_like_zero_mass_is_identity() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // SchwarzschildLike with `mass = 0` (i.e. `r_s = 0`): the
    // SCHW.1 math leaf's defensive Euclidean fallback returns
    // its input unchanged, so the chart behaves like Euclidean
    // at the `ManifoldTransform` seam. Verifies the plan §5.2
    // "Euclidean fallback" contract through the new helper arms.
    ManifoldTransform t = identity_transform();
    t.chart = make_schwarzschild_like_chart();
    t.chart.params.mass = 0.0f;   // r_s = 0 ⇒ identity

    const Vec3 p3{1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(world_to_chart(t, p3), p3));
    RR_CHECK(approx(chart_to_world(t, p3), p3));

    const Vec4 p4{0.5f, 1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(world_to_chart(t, p4), p4));
    RR_CHECK(approx(chart_to_world(t, p4), p4));
}

void test_schw_3_world_to_chart_schwarzschild_like_known_value() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Worked example from plan §2: with `r_s=1`, `falloff=1`,
    // `clamp_radius=0.1`, `mass_origin=(0,0,0)`, `warp_strength=1`,
    // and `p_world = (2, 0, 0)`:
    //   r = 2; f = 1 * 1 / 2 = 0.5;
    //   chart_pos = (2,0,0) + 0.5 * (2,0,0) = (3,0,0).
    // The `ManifoldTransform` seam supplies `warp_strength = 1.0`
    // by default (the chart's intrinsic full warp), so the
    // Vec3 overload reproduces the math leaf's result.
    ManifoldTransform t = identity_transform();
    t.chart = make_schwarzschild_like_chart();

    const Vec3 p_world{2.0f, 0.0f, 0.0f};
    RR_CHECK(approx(world_to_chart(t, p_world), Vec3{3.0f, 0.0f, 0.0f}));

    // Vec4 overload: time invariant, spatial part follows the
    // same warp.
    const Vec4 p_world4{0.5f, 2.0f, 0.0f, 0.0f};
    RR_CHECK(approx(world_to_chart(t, p_world4),
                    Vec4{0.5f, 3.0f, 0.0f, 0.0f}));
}

void test_schw_3_chart_to_world_schwarzschild_like_round_trip() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Forward → inverse round-trip via the `ManifoldTransform`
    // seam reproduces the input to within the plan §5.4
    // documented residual (`<= 1e-4`). This re-exercises the
    // SCHW.1 helper through the `chart_to_world` arm.
    ManifoldTransform t = identity_transform();
    t.chart = make_schwarzschild_like_chart();

    const Vec3 inputs[] = {
        {2.0f, 0.0f, 0.0f},
        {3.0f, 0.0f, 0.0f},
        {1.5f, 0.5f, -1.0f},
        {0.5f, 0.0f, 0.0f},   // near the clamp shell
    };
    for (const Vec3& p_world : inputs) {
        const Vec3 chart  = world_to_chart(t, p_world);
        const Vec3 back   = chart_to_world(t, chart);
        const float resid = length(back - p_world);
        RR_CHECK(resid < 1.0e-4f);
    }

    // Vec4 round-trip: time component preserved exactly,
    // spatial residual bounded by the same constant.
    const Vec4 q_world4{0.7f, 2.0f, 0.0f, 0.0f};
    const Vec4 chart4   = world_to_chart(t, q_world4);
    const Vec4 back4    = chart_to_world(t, chart4);
    RR_CHECK(approx(back4.x, q_world4.x));  // time invariant
    const Vec3 sp_back{back4.y, back4.z, back4.w};
    const Vec3 sp_in  {q_world4.y, q_world4.z, q_world4.w};
    RR_CHECK(length(sp_back - sp_in) < 1.0e-4f);
}

void test_schw_3_no_nan_inf_near_clamp_radius() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // At `p_world == chart.origin` (the most singular geometric
    // case) and at `p_world` strictly inside the clamp shell
    // (`|p_world - mass_origin| < clamp_radius`), the SCHW.1
    // math leaf substitutes `r = clamp_radius` so the formula
    // stays finite. Verify no NaN / Inf propagates through the
    // `ManifoldTransform` seam.
    ManifoldTransform t = identity_transform();
    t.chart = make_schwarzschild_like_chart();
    t.chart.origin = Vec3{2.0f, 0.0f, 0.0f};

    // At the mass origin: the math leaf returns mass_origin
    // unchanged (delta = 0; f * delta = 0; chart_pos = p_world).
    const Vec3 at_mass = t.chart.origin;
    const Vec3 out_at  = world_to_chart(t, at_mass);
    RR_CHECK(std::isfinite(out_at.x));
    RR_CHECK(std::isfinite(out_at.y));
    RR_CHECK(std::isfinite(out_at.z));
    RR_CHECK(approx(out_at, at_mass));

    // Strictly inside the clamp shell (|delta| < clamp_radius
    // = 0.1): the math leaf substitutes r = clamp_radius for
    // the displacement scalar; no division by zero.
    const Vec3 in_shell{
        t.chart.origin.x + 0.01f, t.chart.origin.y, t.chart.origin.z};
    const Vec3 out_shell = world_to_chart(t, in_shell);
    RR_CHECK(std::isfinite(out_shell.x));
    RR_CHECK(std::isfinite(out_shell.y));
    RR_CHECK(std::isfinite(out_shell.z));

    // chart_to_world also stays finite for the clamp-shell case
    // (the NR inverse uses the clamp-shell derivative
    // substitution per plan §5.4).
    const Vec3 chart_in_shell = out_shell;
    const Vec3 back_shell     = chart_to_world(t, chart_in_shell);
    RR_CHECK(std::isfinite(back_shell.x));
    RR_CHECK(std::isfinite(back_shell.y));
    RR_CHECK(std::isfinite(back_shell.z));

    // The Vec4 overload also stays finite at the singular case.
    const Vec4 at_mass4{
        1.0f, t.chart.origin.x, t.chart.origin.y, t.chart.origin.z};
    const Vec4 out4 = world_to_chart(t, at_mass4);
    RR_CHECK(std::isfinite(out4.x));
    RR_CHECK(std::isfinite(out4.y));
    RR_CHECK(std::isfinite(out4.z));
    RR_CHECK(std::isfinite(out4.w));
    RR_CHECK(approx(out4, at_mass4));
}

void test_schw_3_params_from_chart() {
    using namespace rr::manifold;

    // The chart→helper-params builder follows the plan §3
    // reinterpretation table verbatim.
    CoordinateChart c = make_schwarzschild_like_chart();
    c.params.mass                   = 1.5f;
    c.params.spin                   = 2.0f;
    c.params.compactification_scale = 0.25f;

    SchwarzschildLikeWarpParams p = schwarzschild_like_params_from(c);
    RR_CHECK(p.r_s           == 1.5f);
    RR_CHECK(p.warp_strength == 1.0f);  // default
    RR_CHECK(p.falloff       == 2.0f);
    RR_CHECK(p.clamp_radius  == 0.25f);

    // Caller-supplied strength overrides the default.
    SchwarzschildLikeWarpParams half = schwarzschild_like_params_from(c, 0.5f);
    RR_CHECK(half.warp_strength == 0.5f);
    RR_CHECK(half.r_s           == p.r_s);
    RR_CHECK(half.falloff       == p.falloff);
    RR_CHECK(half.clamp_radius  == p.clamp_radius);

    // The default chart's params satisfy the math-leaf
    // validator (all-positive `clamp_radius`, finite
    // `falloff` in `[0.5, 4.0]`, etc.).
    RR_CHECK(schwarzschild_like_validate_params(p));
    RR_CHECK(schwarzschild_like_validate_params(half));
}

void test_schw_3_other_non_euclidean_passthrough() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // The non-`SchwarzschildLike` non-Euclidean placeholder
    // charts (Kruskal / Penrose / Kerr) remain passthrough.
    // SCHW.3 must not silently route them through the
    // SchwarzschildLike math (master rule #3: no fake stubs
    // pretending to be complete systems).
    ManifoldTransform t = identity_transform();
    const Vec3 p3{1.5f, -2.3f, 4.7f};
    const Vec4 p4{0.5f, 1.5f, -2.3f, 4.7f};

    // Note: PenroseLike was originally `PenroseLikePlaceholder`
    // and was included in this iteration set at SCHW.3 time as
    // a passthrough placeholder. PENROSE.4 promoted `PenroseLike`
    // to an active chart with its own ManifoldTransform arm; it
    // is therefore removed from this passthrough iteration set
    // (a separate `test_penrose_4_other_non_euclidean_passthrough`
    // verifies that Kruskal / Kerr remain passthrough even after
    // PenroseLike joined SchwarzschildLike as an active chart).
    for (CoordinateChartType type : {
            CoordinateChartType::KruskalLikePlaceholder,
            CoordinateChartType::KerrLikePlaceholder}) {
        t.chart.type = type;
        // Even if `params.mass != 0` (which would activate the
        // SchwarzschildLike or PenroseLike math), these charts
        // must passthrough.
        t.chart.params.mass = 1.0f;
        RR_CHECK(approx(world_to_chart(t, p3), p3));
        RR_CHECK(approx(chart_to_world(t, p3), p3));
        RR_CHECK(approx(world_to_chart(t, p4), p4));
        RR_CHECK(approx(chart_to_world(t, p4), p4));
    }
}

// ---------- PENROSE.2: Penrose-like artistic compactification math ----------
//
// Verifies that the SCHW.* precedent's math-leaf invariants
// (bounded transforms; Euclidean fallback at strength=0;
// no-NaN/Inf safety) carry through to the analogous PenroseLike
// helper at `src/manifold/PenroseLikeCompactification.h`. Eight
// test functions, each addressing one item the operator's PENROSE.2
// brief enumerated as an acceptance gate.

void test_penrose_2_validate_params() {
    using namespace rr::manifold;

    // Default-constructed params (all-zero r_max + strength,
    // scale=1.0, falloff=1.0) pass validation. The chart is
    // effectively Euclidean in this state because both r_max
    // and strength default to 0.
    PenroseLikeCompactificationParams ok{};
    RR_CHECK(penrose_like_validate_params(ok));

    // Typical non-trivial parameters validate.
    PenroseLikeCompactificationParams typical{};
    typical.r_max    = 5.0f;
    typical.strength = 1.0f;
    typical.scale    = 1.0f;
    typical.falloff  = 1.0f;
    RR_CHECK(penrose_like_validate_params(typical));

    // Negative r_max rejected.
    PenroseLikeCompactificationParams neg_rmax = typical;
    neg_rmax.r_max = -1.0f;
    RR_CHECK(!penrose_like_validate_params(neg_rmax));

    // Falloff out of [0.5, 4.0] rejected.
    PenroseLikeCompactificationParams falloff_low = typical;
    falloff_low.falloff = 0.4f;
    RR_CHECK(!penrose_like_validate_params(falloff_low));
    PenroseLikeCompactificationParams falloff_high = typical;
    falloff_high.falloff = 4.1f;
    RR_CHECK(!penrose_like_validate_params(falloff_high));

    // scale <= 0 rejected (would make `r / scale` divergent or
    // negative).
    PenroseLikeCompactificationParams scale_zero = typical;
    scale_zero.scale = 0.0f;
    RR_CHECK(!penrose_like_validate_params(scale_zero));
    PenroseLikeCompactificationParams scale_neg = typical;
    scale_neg.scale = -0.1f;
    RR_CHECK(!penrose_like_validate_params(scale_neg));

    // Non-finite values rejected.
    PenroseLikeCompactificationParams nan_p = typical;
    nan_p.strength = std::nanf("");
    RR_CHECK(!penrose_like_validate_params(nan_p));
}

void test_penrose_2_world_to_chart_identity_at_strength_zero() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // strength = 0 ⇒ identity, regardless of r_max / falloff /
    // scale / origin / input position. The operator's PENROSE.2
    // acceptance test #1 ("identity at strength 0") is exercised
    // directly here.
    PenroseLikeCompactificationParams p{};
    p.r_max    = 5.0f;
    p.strength = 0.0f;
    p.scale    = 1.0f;
    p.falloff  = 1.0f;
    const Vec3 origin{0.0f, 0.0f, 0.0f};

    const Vec3 p1{0.0f, 0.0f, 0.0f};
    const Vec3 p2{1.5f, -2.3f, 4.7f};
    const Vec3 p3{-100.0f, 0.5f, 99.9f};
    RR_CHECK(penrose_like_world_to_chart(p1, origin, p) == p1);
    RR_CHECK(penrose_like_world_to_chart(p2, origin, p) == p2);
    RR_CHECK(penrose_like_world_to_chart(p3, origin, p) == p3);

    // r_max = 0 with non-zero strength ⇒ also identity (defensive
    // short-circuit; mirrors SCHW.1's `r_s == 0` short-circuit).
    PenroseLikeCompactificationParams q = p;
    q.strength = 1.0f;
    q.r_max    = 0.0f;
    RR_CHECK(penrose_like_world_to_chart(p2, origin, q) == p2);
}

void test_penrose_2_world_to_chart_bounded_for_large_distance() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::Vec3;

    // r → ∞ ⇒ r_chart → r_max. Verified at a large `r`:
    //   tanh(strength * (r/scale)^falloff) saturates at 1.0 for
    //   inputs > ~16 in single precision. The operator's
    //   PENROSE.2 acceptance test #2 ("bounded output for large
    //   distances") is exercised directly here.
    PenroseLikeCompactificationParams p{};
    p.r_max    = 5.0f;
    p.strength = 1.0f;
    p.scale    = 1.0f;
    p.falloff  = 1.0f;

    const Vec3 origin{0.0f, 0.0f, 0.0f};
    const Vec3 p_far{1.0e6f, 0.0f, 0.0f};
    const Vec3 chart_far =
        penrose_like_world_to_chart(p_far, origin, p);
    const float r_chart = length(chart_far - origin);
    // r_chart must be bounded by r_max (strict inequality not
    // required by the math; the IEEE-754 saturation produces
    // exactly r_max for sufficiently large r).
    RR_CHECK(r_chart <= p.r_max);
    // r_chart must be very close to r_max (saturation
    // signature). With strength = 1, scale = 1, falloff = 1,
    // r = 1e6 ⇒ tanh(1e6) = 1.0f exactly ⇒ r_chart = r_max.
    RR_CHECK(approx(r_chart, p.r_max, 1.0e-5f));

    // A second far-field input from a different direction also
    // saturates at r_max — verifies the bound is uniform across
    // directions.
    const Vec3 p_far_y{0.0f, -1.0e6f, 0.0f};
    const Vec3 chart_far_y =
        penrose_like_world_to_chart(p_far_y, origin, p);
    const float r_chart_y = length(chart_far_y - origin);
    RR_CHECK(r_chart_y <= p.r_max);
    RR_CHECK(approx(r_chart_y, p.r_max, 1.0e-5f));
}

void test_penrose_2_world_to_chart_no_nan_inf() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // The operator's PENROSE.2 acceptance test #3 ("no NaN/Inf").
    // Adversarial inputs spanning the domain:
    //   - very large r;
    //   - very small r (near origin);
    //   - exactly at origin (the fixed point of the radial map);
    //   - off-axis;
    //   - extreme strength values (out of nominal [0, 1] range).
    PenroseLikeCompactificationParams p{};
    p.r_max    = 5.0f;
    p.strength = 100.0f;   // far above nominal range
    p.scale    = 1.0f;
    p.falloff  = 4.0f;     // max-allowed
    const Vec3 origin{2.0f, -3.0f, 1.0f};

    const Vec3 inputs[] = {
        {2.0f, -3.0f, 1.0f},      // exactly at origin
        {2.0f + 1.0e-10f, -3.0f, 1.0f},  // ε away
        {2.0f + 1.0e-3f,  -3.0f, 1.0f},  // close
        {2.0f + 1.0e6f,   -3.0f, 1.0f},  // very far
        {1.5f + 1.0e6f,   1.0e6f, -1.0e6f},  // very far off-axis
    };
    for (const Vec3& q : inputs) {
        const Vec3 out = penrose_like_world_to_chart(q, origin, p);
        RR_CHECK(std::isfinite(out.x));
        RR_CHECK(std::isfinite(out.y));
        RR_CHECK(std::isfinite(out.z));
    }
}

void test_penrose_2_world_to_chart_monotonic_radial_compression() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::Vec3;

    // The operator's PENROSE.2 acceptance test #4 ("monotonic
    // radial compression"). For three world-space points along
    // a ray from the origin, verify that the chart-space radial
    // distances are also monotonically increasing.
    PenroseLikeCompactificationParams p{};
    p.r_max    = 5.0f;
    p.strength = 1.0f;
    p.scale    = 1.0f;
    p.falloff  = 1.0f;
    const Vec3 origin{0.0f, 0.0f, 0.0f};

    const Vec3 a{1.0f, 0.0f, 0.0f};
    const Vec3 b{2.0f, 0.0f, 0.0f};
    const Vec3 c{5.0f, 0.0f, 0.0f};
    const Vec3 d{50.0f, 0.0f, 0.0f};

    const float ra = length(penrose_like_world_to_chart(a, origin, p) - origin);
    const float rb = length(penrose_like_world_to_chart(b, origin, p) - origin);
    const float rc = length(penrose_like_world_to_chart(c, origin, p) - origin);
    const float rd = length(penrose_like_world_to_chart(d, origin, p) - origin);

    RR_CHECK(ra < rb);
    RR_CHECK(rb < rc);
    // rc and rd are both very close to r_max but rd >= rc
    // because tanh is monotonically non-decreasing.
    RR_CHECK(rc <= rd);
    // All bounded by r_max.
    RR_CHECK(rd <= p.r_max);
}

void test_penrose_2_world_to_chart_safe_near_origin() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // The operator's PENROSE.2 acceptance test #5 ("safe behavior
    // near origin"). At p_world == origin and at extremely small
    // |delta|, the formula's `r / r` term would be 0/0; the
    // helper's explicit `r <= 1e-20f` short-circuit returns
    // p_world. Verify finite output AND identity at the fixed
    // point.
    PenroseLikeCompactificationParams p{};
    p.r_max    = 5.0f;
    p.strength = 1.0f;
    p.scale    = 1.0f;
    p.falloff  = 1.0f;
    const Vec3 origin{2.0f, 0.0f, 0.0f};

    // Exactly at origin: helper returns p_world unchanged.
    const Vec3 at_origin = origin;
    const Vec3 out_at = penrose_like_world_to_chart(at_origin, origin, p);
    RR_CHECK(approx(out_at, origin));

    // Very close to origin (1e-15 away): the radial compression
    // is mathematically defined but numerically marginal. The
    // helper's short-circuit AT 1e-20f catches the extreme case;
    // slightly above the threshold, the helper produces a
    // continuous near-identity output.
    const Vec3 near{2.0f + 1.0e-15f, 0.0f, 0.0f};
    const Vec3 out_near = penrose_like_world_to_chart(near, origin, p);
    RR_CHECK(std::isfinite(out_near.x));
    RR_CHECK(std::isfinite(out_near.y));
    RR_CHECK(std::isfinite(out_near.z));
}

void test_penrose_2_chart_to_world_inverse_residual() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::Vec3;

    // Forward → inverse round-trip should reproduce the input
    // to within the plan §6.4 documented residual (`≤ 1e-6` for
    // typical parameter ranges, better than SCHW.1's `1e-4`
    // because the inverse is analytical rather than NR-iterative).
    PenroseLikeCompactificationParams p{};
    p.scale = 1.0f;
    const Vec3 origin{0.0f, 0.0f, 0.0f};

    struct Case {
        float r_max, strength, falloff;
        rr::math::Vec3 p_world;
    };
    const Case cases[] = {
        {5.0f, 0.5f, 1.0f, {1.0f, 0.0f, 0.0f}},
        {5.0f, 1.0f, 1.0f, {2.0f, 0.0f, 0.0f}},
        {3.0f, 0.5f, 2.0f, {1.5f, 0.0f, 0.0f}},
        {5.0f, 1.0f, 1.0f, {1.0f, 0.5f, -0.5f}},
        {10.0f, 0.5f, 0.5f, {3.0f, 0.0f, 0.0f}},
        // Edge: r close to (but not over) r_max in chart space.
        {5.0f, 1.0f, 1.0f, {0.3f, 0.0f, 0.0f}},
    };

    for (const auto& c : cases) {
        p.r_max    = c.r_max;
        p.strength = c.strength;
        p.falloff  = c.falloff;
        const Vec3 chart =
            penrose_like_world_to_chart(c.p_world, origin, p);
        const Vec3 back =
            penrose_like_chart_to_world(chart, origin, p);
        const float residual = length(back - c.p_world);
        RR_CHECK(residual < 1.0e-4f);   // generous bound; typical
                                         // observed residual << 1e-6
    }
}

void test_penrose_2_chart_to_world_euclidean_fallback() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // strength = 0 ⇒ chart_to_world is identity. Mirrors the
    // forward-map Euclidean fallback (same defensive short-circuit
    // pattern as SCHW.1's `chart_to_world`).
    PenroseLikeCompactificationParams p{};
    p.r_max    = 5.0f;
    p.strength = 0.0f;
    p.scale    = 1.0f;
    p.falloff  = 1.0f;
    const Vec3 origin{1.0f, 2.0f, 3.0f};
    const Vec3 chart{4.7f, -1.2f, 0.5f};
    RR_CHECK(penrose_like_chart_to_world(chart, origin, p) == chart);
}

void test_penrose_2_chart_to_world_boundary_clamp() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::Vec3;

    // Inverse with r_chart at (or very close to) r_max: the
    // `atanh(arg)` would diverge as arg → 1; the helper clamps
    // r_chart to `r_max * (1 - kBoundaryEpsilon)` so the
    // inverse stays finite. Verify finite output at the boundary
    // and just past it (the latter exercises the clamp path).
    PenroseLikeCompactificationParams p{};
    p.r_max    = 5.0f;
    p.strength = 1.0f;
    p.scale    = 1.0f;
    p.falloff  = 1.0f;
    const Vec3 origin{0.0f, 0.0f, 0.0f};

    // r_chart = r_max exactly (the singular case).
    const Vec3 at_boundary{5.0f, 0.0f, 0.0f};
    const Vec3 out_b =
        penrose_like_chart_to_world(at_boundary, origin, p);
    RR_CHECK(std::isfinite(out_b.x));
    RR_CHECK(std::isfinite(out_b.y));
    RR_CHECK(std::isfinite(out_b.z));

    // r_chart > r_max (operator-side bug; clamp should still
    // produce finite output).
    const Vec3 past_boundary{10.0f, 0.0f, 0.0f};
    const Vec3 out_p =
        penrose_like_chart_to_world(past_boundary, origin, p);
    RR_CHECK(std::isfinite(out_p.x));
    RR_CHECK(std::isfinite(out_p.y));
    RR_CHECK(std::isfinite(out_p.z));
}

// ---------- PENROSE.4: ManifoldTransform integration ----------
//
// Verifies that `world_to_chart` / `chart_to_world` on
// `ManifoldTransform` invoke the PENROSE.2 math leaf when the
// active chart is `PenroseLike`, while preserving the
// "default no-op" contract for the disabled / Euclidean
// configurations. Plan: `PENROSE_LIKE_COMPACTIFICATION_PLAN.md`
// §8.1 / §8.2.

// Build a PenroseLike `CoordinateChart` from the canonical
// fixture parameters per the plan §3 reinterpretation table.
// Compactification origin at scene origin; `r_max = 5.0`,
// `scale = 1.0`, `falloff = 1.0`. These are the same artistic
// defaults the future PENROSE.5 / PENROSE.6 dispatcher uses.
rr::manifold::CoordinateChart make_penrose_like_chart() {
    rr::manifold::CoordinateChart c{};
    c.type   = rr::manifold::CoordinateChartType::PenroseLike;
    c.name   = "penrose-like";
    c.origin = rr::math::Vec3{0.0f, 0.0f, 0.0f};   // observer / compactification centre
    c.params.mass                   = 5.0f;        // r_max
    c.params.spin                   = 1.0f;        // falloff
    c.params.compactification_scale = 1.0f;        // scale
    return c;
}

void test_penrose_4_disabled_identity_preserved() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Default ManifoldTransform (Euclidean chart, identity scale,
    // zero origin): every helper is the identity. PENROSE.4 does
    // not change this behaviour; this test re-runs the post-
    // MANIFOLD.5 / post-SCHW.3 invariant against the post-PENROSE.4
    // build.
    ManifoldTransform t = identity_transform();
    RR_CHECK(t.chart.type == CoordinateChartType::Euclidean);

    const Vec3 p3{1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(world_to_chart(t, p3), p3));
    RR_CHECK(approx(chart_to_world(t, p3), p3));

    const Vec4 p4{0.5f, 1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(world_to_chart(t, p4), p4));
    RR_CHECK(approx(chart_to_world(t, p4), p4));
}

void test_penrose_4_euclidean_identity_preserved() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Explicit Euclidean chart (`type = Euclidean`, non-trivial
    // origin / scale): the PENROSE.4 changes are gated on
    // `type == PenroseLike`, so the Euclidean affine map
    // continues to apply unchanged. Bit-identity preserved.
    ManifoldTransform t = identity_transform();
    t.chart.type   = CoordinateChartType::Euclidean;
    t.chart.origin = rr::math::Vec3{1.0f, 2.0f, 3.0f};
    t.chart.scale  = 2.0f;

    const Vec3 world_pos{3.0f, 6.0f, 9.0f};
    // (world - origin) / scale = (2, 4, 6) / 2 = (1, 2, 3).
    RR_CHECK(approx(world_to_chart(t, world_pos), Vec3{1.0f, 2.0f, 3.0f}));
    // Round-trip is exact under uniform scaling.
    RR_CHECK(approx(chart_to_world(t, world_to_chart(t, world_pos)), world_pos));

    const Vec4 world4{0.5f, 3.0f, 6.0f, 9.0f};
    // Time invariant, spatial part transformed.
    RR_CHECK(approx(world_to_chart(t, world4),
                    Vec4{0.5f, 1.0f, 2.0f, 3.0f}));
    RR_CHECK(approx(chart_to_world(t, world_to_chart(t, world4)), world4));
}

void test_penrose_4_penrose_like_zero_mass_is_identity() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // PenroseLike with `mass = 0` (i.e. `r_max = 0`): the
    // PENROSE.2 math leaf's defensive Euclidean fallback
    // returns its input unchanged, so the chart behaves like
    // Euclidean at the `ManifoldTransform` seam. Verifies the
    // plan §6.3 "Euclidean fallback" contract through the new
    // helper arms. This also covers the operator's "strength 0
    // identity" acceptance — the seam hardcodes strength = 1.0,
    // so the only path to identity is the math leaf's `r_max =
    // 0` short-circuit.
    ManifoldTransform t = identity_transform();
    t.chart = make_penrose_like_chart();
    t.chart.params.mass = 0.0f;   // r_max = 0 ⇒ identity

    const Vec3 p3{1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(world_to_chart(t, p3), p3));
    RR_CHECK(approx(chart_to_world(t, p3), p3));

    const Vec4 p4{0.5f, 1.5f, -2.3f, 4.7f};
    RR_CHECK(approx(world_to_chart(t, p4), p4));
    RR_CHECK(approx(chart_to_world(t, p4), p4));
}

void test_penrose_4_world_to_chart_penrose_like_bounded() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // PenroseLike chart with `r_max = 5.0`: world-space inputs
    // produce chart-space outputs bounded by r_max. Verified
    // across three input distances spanning the chart's
    // operational range: near-identity (small r), transition
    // (moderate r), saturation (large r).
    ManifoldTransform t = identity_transform();
    t.chart = make_penrose_like_chart();

    const Vec3 origin = t.chart.origin;
    const Vec3 near{0.5f, 0.0f, 0.0f};       // r = 0.5
    const Vec3 mid{3.0f, 0.0f, 0.0f};         // r = 3.0
    const Vec3 far{1.0e4f, 0.0f, 0.0f};       // r = 1e4 (saturated)

    const Vec3 chart_near = world_to_chart(t, near);
    const Vec3 chart_mid  = world_to_chart(t, mid);
    const Vec3 chart_far  = world_to_chart(t, far);

    RR_CHECK(length(chart_near - origin) <= t.chart.params.mass);
    RR_CHECK(length(chart_mid  - origin) <= t.chart.params.mass);
    RR_CHECK(length(chart_far  - origin) <= t.chart.params.mass);

    // Far-field saturation: chart_far is very close to r_max.
    RR_CHECK(approx(length(chart_far - origin),
                    t.chart.params.mass, 1.0e-4f));

    // Vec4 overload: time invariant; spatial part bounded.
    const Vec4 p_world4{0.7f, 1.0e4f, 0.0f, 0.0f};
    const Vec4 chart4 = world_to_chart(t, p_world4);
    RR_CHECK(approx(chart4.x, p_world4.x));   // time invariant
    const Vec3 sp{chart4.y, chart4.z, chart4.w};
    RR_CHECK(length(sp - origin) <= t.chart.params.mass);
}

void test_penrose_4_chart_to_world_penrose_like_round_trip() {
    using namespace rr::manifold;
    using rr::math::length;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Forward → inverse round-trip via the `ManifoldTransform`
    // seam reproduces the input to within the plan §6.4
    // documented residual. The PENROSE.2 inverse is
    // analytical (closed-form `atanh`), so the residual is
    // much tighter than SCHW.1's iterative bound — typical
    // observed << 1e-6.
    ManifoldTransform t = identity_transform();
    t.chart = make_penrose_like_chart();

    const Vec3 inputs[] = {
        {0.5f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
        {1.5f, 0.5f, -1.0f},
    };
    for (const Vec3& p_world : inputs) {
        const Vec3 chart  = world_to_chart(t, p_world);
        const Vec3 back   = chart_to_world(t, chart);
        const float resid = length(back - p_world);
        RR_CHECK(resid < 1.0e-4f);
    }

    // Vec4 round-trip: time component preserved exactly,
    // spatial residual bounded by the same constant.
    const Vec4 q_world4{0.7f, 2.0f, 0.0f, 0.0f};
    const Vec4 chart4   = world_to_chart(t, q_world4);
    const Vec4 back4    = chart_to_world(t, chart4);
    RR_CHECK(approx(back4.x, q_world4.x));   // time invariant
    const Vec3 sp_back{back4.y, back4.z, back4.w};
    const Vec3 sp_in  {q_world4.y, q_world4.z, q_world4.w};
    RR_CHECK(length(sp_back - sp_in) < 1.0e-4f);
}

void test_penrose_4_no_nan_inf_for_large_coordinates() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Very large coordinates exercise the `tanh` saturation
    // path. The math leaf's bounded-by-construction property
    // (tanh saturates at ±1.0f in IEEE-754 single precision)
    // guarantees finite outputs. Verify at multiple extreme
    // inputs through both the Vec3 and Vec4 overloads of the
    // ManifoldTransform seam.
    ManifoldTransform t = identity_transform();
    t.chart = make_penrose_like_chart();

    const Vec3 extremes[] = {
        {1.0e6f, 0.0f, 0.0f},
        {0.0f, 1.0e10f, 0.0f},
        {-1.0e6f, 1.0e6f, -1.0e6f},
    };
    for (const Vec3& p : extremes) {
        const Vec3 chart = world_to_chart(t, p);
        RR_CHECK(std::isfinite(chart.x));
        RR_CHECK(std::isfinite(chart.y));
        RR_CHECK(std::isfinite(chart.z));
        const Vec3 back = chart_to_world(t, chart);
        RR_CHECK(std::isfinite(back.x));
        RR_CHECK(std::isfinite(back.y));
        RR_CHECK(std::isfinite(back.z));
    }

    // Vec4 at an extreme input: time component preserved
    // exactly; spatial output finite.
    const Vec4 p_world4{0.5f, 1.0e8f, -1.0e8f, 1.0e8f};
    const Vec4 chart4 = world_to_chart(t, p_world4);
    RR_CHECK(std::isfinite(chart4.x));
    RR_CHECK(std::isfinite(chart4.y));
    RR_CHECK(std::isfinite(chart4.z));
    RR_CHECK(std::isfinite(chart4.w));
    RR_CHECK(approx(chart4.x, p_world4.x));   // time invariant
}

void test_penrose_4_params_from_chart() {
    using namespace rr::manifold;

    // The chart→helper-params builder follows the plan §3
    // reinterpretation table verbatim.
    CoordinateChart c = make_penrose_like_chart();
    c.params.mass                   = 8.0f;
    c.params.spin                   = 2.0f;
    c.params.compactification_scale = 0.5f;

    PenroseLikeCompactificationParams p = penrose_like_params_from(c);
    RR_CHECK(p.r_max    == 8.0f);
    RR_CHECK(p.strength == 1.0f);  // default
    RR_CHECK(p.falloff  == 2.0f);
    RR_CHECK(p.scale    == 0.5f);

    // Caller-supplied strength overrides the default.
    PenroseLikeCompactificationParams half = penrose_like_params_from(c, 0.5f);
    RR_CHECK(half.strength == 0.5f);
    RR_CHECK(half.r_max    == p.r_max);
    RR_CHECK(half.falloff  == p.falloff);
    RR_CHECK(half.scale    == p.scale);

    // The default chart's params satisfy the math-leaf
    // validator (all-positive `scale`, finite `falloff` in
    // `[0.5, 4.0]`, etc.).
    RR_CHECK(penrose_like_validate_params(p));
    RR_CHECK(penrose_like_validate_params(half));
}

// ---------- OBSERVER.2: ObserverFrame data model ----------

void test_observer_2_perception_mode_default() {
    using namespace rr::manifold;

    // The default perception mode is Identity (the no-op anchor
    // every existing CLI action's byte-identity rests on).
    RR_CHECK(default_perception_mode() == PerceptionMode::Identity);

    // A default-constructed ObserverFrame carries the same default
    // (the POD's per-field initialiser, not the factory).
    ObserverFrame f{};
    RR_CHECK(f.perception_mode == PerceptionMode::Identity);

    // The three perception-mode enumerators are pairwise distinct.
    // (Defence-in-depth against an accidental enumerator collision
    // in a future edit; mirrors the test_geodesic_state_defaults
    // pattern that pins GeodesicStatus enumerators.)
    RR_CHECK(PerceptionMode::Identity                       !=
             PerceptionMode::ConstantVelocityMinkowski);
    RR_CHECK(PerceptionMode::Identity                       !=
             PerceptionMode::CurvedChartGeodesicPlaceholder);
    RR_CHECK(PerceptionMode::ConstantVelocityMinkowski      !=
             PerceptionMode::CurvedChartGeodesicPlaceholder);
}

void test_observer_2_orthonormal_tetrad_default() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    // The default rest_frame() tetrad is the right-handed world
    // basis - pairwise dot products are exactly zero, leg lengths
    // are exactly one.
    ObserverFrame rf = rest_frame();
    RR_CHECK(is_orthonormal_tetrad(rf));

    // A frame from observer_frame_from(...) at moderate beta also
    // carries the default world-basis tetrad (the helper does not
    // re-orient the tetrad; OBSERVER.4 will do that).
    rr::relativity::Observer obs_rt;
    obs_rt.velocity = Vec3{0.3f, -0.4f, 0.0f};
    ObserverFrame f = observer_frame_from(obs_rt);
    RR_CHECK(is_orthonormal_tetrad(f));

    // Defence-in-depth: a tetrad with a non-unit leg fails.
    ObserverFrame degenerate_len = rest_frame();
    degenerate_len.right = Vec3{2.0f, 0.0f, 0.0f};  // |right| = 2
    RR_CHECK(!is_orthonormal_tetrad(degenerate_len));

    // A tetrad with non-orthogonal legs fails.
    ObserverFrame degenerate_orth = rest_frame();
    degenerate_orth.up = Vec3{1.0f, 0.0f, 0.0f};    // up == right
    RR_CHECK(!is_orthonormal_tetrad(degenerate_orth));

    // A tetrad with collinear forward/right legs fails.
    ObserverFrame degenerate_coll = rest_frame();
    degenerate_coll.forward = Vec3{1.0f, 0.0f, 0.0f};  // forward == right
    RR_CHECK(!is_orthonormal_tetrad(degenerate_coll));
}

void test_observer_2_finite_observer_frame() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // Every default-built ObserverFrame is finite.
    ObserverFrame rf = rest_frame();
    RR_CHECK(is_finite_observer_frame(rf));

    // Frames from observer_frame_from(...) at moderate beta remain
    // finite (clampBeta keeps gamma bounded; the constructed
    // velocity4 has no NaN/inf path).
    rr::relativity::Observer obs_rt;
    obs_rt.velocity = Vec3{0.3f, -0.4f, 0.0f};
    ObserverFrame f = observer_frame_from(obs_rt);
    RR_CHECK(is_finite_observer_frame(f));

    // Defence-in-depth: NaN in any scalar field is detected. Use
    // the NaN sentinel from the IEEE-754 0/0 form to avoid any
    // platform-specific quiet-vs-signalling NaN distinction.
    const float nan_f = std::nanf("");
    const float inf_f = std::numeric_limits<float>::infinity();

    ObserverFrame nan_pos = rest_frame();
    nan_pos.position4 = Vec4{nan_f, 0.0f, 0.0f, 0.0f};
    RR_CHECK(!is_finite_observer_frame(nan_pos));

    ObserverFrame nan_vel = rest_frame();
    nan_vel.velocity4 = Vec4{1.0f, nan_f, 0.0f, 0.0f};
    RR_CHECK(!is_finite_observer_frame(nan_vel));

    ObserverFrame nan_beta = rest_frame();
    nan_beta.beta = Vec3{nan_f, 0.0f, 0.0f};
    RR_CHECK(!is_finite_observer_frame(nan_beta));

    ObserverFrame nan_right = rest_frame();
    nan_right.right = Vec3{nan_f, 0.0f, 0.0f};
    RR_CHECK(!is_finite_observer_frame(nan_right));

    ObserverFrame nan_tau = rest_frame();
    nan_tau.proper_time = nan_f;
    RR_CHECK(!is_finite_observer_frame(nan_tau));

    ObserverFrame nan_t = rest_frame();
    nan_t.coordinate_time = nan_f;
    RR_CHECK(!is_finite_observer_frame(nan_t));

    // Defence-in-depth: positive / negative infinity in any scalar
    // field is also detected.
    ObserverFrame inf_pos = rest_frame();
    inf_pos.position4 = Vec4{inf_f, 0.0f, 0.0f, 0.0f};
    RR_CHECK(!is_finite_observer_frame(inf_pos));

    ObserverFrame neg_inf_t = rest_frame();
    neg_inf_t.coordinate_time = -inf_f;
    RR_CHECK(!is_finite_observer_frame(neg_inf_t));
}

void test_observer_2_default_no_deformation() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // OBSERVER.2 anchor: the default ObserverFrame represents the
    // current camera-equivalent no-op observer. Specifically:
    //   - beta is exactly zero (no velocity);
    //   - position4 is the chart origin;
    //   - velocity4 is the rest 4-velocity (1, 0, 0, 0);
    //   - the tetrad is the right-handed world basis;
    //   - both worldline-time placeholders are zero;
    //   - the perception mode is Identity.
    // Together these mean the default frame cannot imply ANY
    // coordinate deformation - the bridge to the legacy
    // rr::relativity::Observer round-trips with beta still zero,
    // and a kernel reading the frame as a no-op gets bit-for-bit
    // the pre-pivot scene-rest observer.
    ObserverFrame f{};
    RR_CHECK(approx(f.beta,      Vec3{0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(f.position4, Vec4{0.0f, 0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(f.velocity4, Vec4{1.0f, 0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(f.right,     Vec3{1.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(f.up,        Vec3{0.0f, 1.0f, 0.0f}));
    RR_CHECK(approx(f.forward,   Vec3{0.0f, 0.0f, 1.0f}));
    RR_CHECK(f.proper_time      == 0.0f);
    RR_CHECK(f.coordinate_time  == 0.0f);
    RR_CHECK(f.perception_mode  == PerceptionMode::Identity);

    // The default frame's bridge to the legacy SR observer also
    // carries zero velocity - the no-op observer cannot induce
    // any aberration / Doppler / searchlight effect at the kernel
    // even if the existing helpers were called against it.
    rr::relativity::Observer back = to_relativity_observer(f);
    RR_CHECK(approx(back.velocity, Vec3{0.0f, 0.0f, 0.0f}));

    // Round-trip via observer_frame_from(rest Observer) reproduces
    // the default frame's beta and velocity4 exactly.
    rr::relativity::Observer rest_obs;
    ObserverFrame from_rest = observer_frame_from(rest_obs);
    RR_CHECK(approx(from_rest.beta,      f.beta));
    RR_CHECK(approx(from_rest.velocity4, f.velocity4));

    // OBSERVER.2 validator gates all hold on the default frame.
    RR_CHECK(is_finite_observer_frame(f));
    RR_CHECK(is_orthonormal_tetrad(f));
    RR_CHECK(is_normalised_timelike(f, minkowski_metric()));
}

void test_penrose_4_other_non_euclidean_passthrough() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    // The non-`SchwarzschildLike` non-`PenroseLike` non-Euclidean
    // placeholder charts (Kruskal / Kerr) remain passthrough.
    // PENROSE.4 must not silently route them through either the
    // SchwarzschildLike or the PenroseLike math (master rule #3:
    // no fake stubs pretending to be complete systems).
    ManifoldTransform t = identity_transform();
    const Vec3 p3{1.5f, -2.3f, 4.7f};
    const Vec4 p4{0.5f, 1.5f, -2.3f, 4.7f};

    for (CoordinateChartType type : {
            CoordinateChartType::KruskalLikePlaceholder,
            CoordinateChartType::KerrLikePlaceholder}) {
        t.chart.type = type;
        // Even if `params.mass != 0` (which would activate the
        // SchwarzschildLike or PenroseLike math), these charts
        // must passthrough.
        t.chart.params.mass = 1.0f;
        RR_CHECK(approx(world_to_chart(t, p3), p3));
        RR_CHECK(approx(chart_to_world(t, p3), p3));
        RR_CHECK(approx(world_to_chart(t, p4), p4));
        RR_CHECK(approx(chart_to_world(t, p4), p4));
    }
}

// ---------- OBSERVER.6: Camera-to-observer adapter ----------

// Build a deterministic non-default GpuCamera for the adapter
// tests. Different from the host Camera's default so the tests
// can distinguish "camera was read" from "camera was ignored".
rr::camera::GpuCamera make_test_gpu_camera() {
    rr::camera::GpuCamera gc{};
    gc.position      = rr::math::Vec3{ 1.0f,  2.0f,  3.0f};
    gc.right         = rr::math::Vec3{ 1.0f,  0.0f,  0.0f};
    gc.up            = rr::math::Vec3{ 0.0f,  1.0f,  0.0f};
    gc.forward       = rr::math::Vec3{ 0.0f,  0.0f, -1.0f};
    gc.tan_half_vfov = 0.5f;
    gc.aspect        = 16.0f / 9.0f;
    return gc;
}

// Case 1: default Camera + default Observer + default Config
// (Identity perception mode) -> rest_frame() byte-for-byte.
// The adapter's no-op anchor; preserves the byte-identity to
// today's renderer for the default-default-default invocation.
void test_observer_6_default_is_camera_equivalent_no_op() {
    using namespace rr::manifold;

    const rr::camera::GpuCamera     gc{};       // all-zero default
    const rr::relativity::Observer  obs{};      // velocity = 0
    const ObserverConfig            cfg{};      // perception_mode = Identity

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    ObserverFrame ref = rest_frame();

    // Field-by-field byte-identity against rest_frame().
    RR_CHECK(approx(f.position4, ref.position4));
    RR_CHECK(approx(f.velocity4, ref.velocity4));
    RR_CHECK(approx(f.beta,      ref.beta));
    RR_CHECK(approx(f.right,     ref.right));
    RR_CHECK(approx(f.up,        ref.up));
    RR_CHECK(approx(f.forward,   ref.forward));
    RR_CHECK(f.proper_time     == ref.proper_time);
    RR_CHECK(f.coordinate_time == ref.coordinate_time);
    RR_CHECK(f.perception_mode == PerceptionMode::Identity);
}

// Case 2: non-default Camera + default Observer + Identity
// Config -> still rest_frame() (Identity mode is the no-op
// anchor; the camera's non-default position / basis are
// ignored). This is the operator's "no-op observer does not
// imply coordinate deformation" gate transposed to the
// adapter.
void test_observer_6_identity_mode_ignores_camera() {
    using namespace rr::manifold;

    const rr::camera::GpuCamera     gc  = make_test_gpu_camera();
    const rr::relativity::Observer  obs{};                  // velocity = 0
    ObserverConfig                  cfg{};                  // Identity
    cfg.beta_magnitude = 0.0f;                              // explicit anchor

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    RR_CHECK(approx(f.position4, rest_frame().position4));
    RR_CHECK(approx(f.velocity4, rest_frame().velocity4));
    RR_CHECK(approx(f.beta,      rest_frame().beta));
    RR_CHECK(approx(f.right,     rest_frame().right));
    RR_CHECK(approx(f.up,        rest_frame().up));
    RR_CHECK(approx(f.forward,   rest_frame().forward));
    RR_CHECK(f.proper_time     == 0.0f);
    RR_CHECK(f.coordinate_time == 0.0f);
    RR_CHECK(f.perception_mode == PerceptionMode::Identity);
}

// Case 3: ConstantVelocityMinkowski perception mode with
// zero observer velocity AND zero config beta_magnitude.
// The resolved beta is zero; the frame has the camera's
// non-trivial position + tetrad but zero velocity. The
// kernel-side SR helpers collapse to identity at beta=0 so
// this path produces byte-identical output to today's
// renderer (which is the "camera-equivalent" no-op contract
// the operator brief specifies).
void test_observer_6_constant_velocity_zero_beta() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::Vec4;

    const rr::camera::GpuCamera     gc  = make_test_gpu_camera();
    const rr::relativity::Observer  obs{};                  // velocity = 0
    ObserverConfig                  cfg{};
    cfg.perception_mode = PerceptionMode::ConstantVelocityMinkowski;

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    RR_CHECK(approx(f.position4,  Vec4{0.0f, 1.0f, 2.0f, 3.0f}));
    RR_CHECK(approx(f.velocity4,  Vec4{1.0f, 0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(f.beta,       Vec3{0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(f.right,      gc.right));
    RR_CHECK(approx(f.up,         gc.up));
    RR_CHECK(approx(f.forward,    gc.forward));
    RR_CHECK(f.proper_time     == 0.0f);
    RR_CHECK(f.coordinate_time == 0.0f);
    RR_CHECK(f.perception_mode == PerceptionMode::ConstantVelocityMinkowski);
}

// Case 4: ConstantVelocityMinkowski with non-zero
// observer.velocity (the scene-author SR path; legacy
// `apply_relativity` -> `Observer::velocity`). The adapter
// uses the legacy velocity directly when config.beta_magnitude
// is zero. Round-trip via `to_relativity_observer` preserves
// the input velocity to within the clampBeta cap.
void test_observer_6_constant_velocity_from_legacy_observer() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    const rr::camera::GpuCamera gc = make_test_gpu_camera();
    rr::relativity::Observer    obs;
    obs.velocity = Vec3{0.3f, -0.4f, 0.0f};   // |beta| = 0.5
    ObserverConfig              cfg{};
    cfg.perception_mode = PerceptionMode::ConstantVelocityMinkowski;
    cfg.beta_magnitude  = 0.0f;               // CLI-zero -> use observer

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    RR_CHECK(approx(f.beta, obs.velocity));

    // Round-trip via to_relativity_observer preserves the
    // input velocity exactly (no clamp triggered at |beta| =
    // 0.5).
    rr::relativity::Observer back = to_relativity_observer(f);
    RR_CHECK(approx(back.velocity, obs.velocity));

    // Frame is timelike-normalised under Minkowski.
    RR_CHECK(is_normalised_timelike(f, minkowski_metric()));
}

// Case 5: ConstantVelocityMinkowski with non-zero
// config.beta_magnitude + non-zero direction. The CLI overlay
// wins over the legacy observer.velocity. The resulting beta
// vector is `clampBeta(magnitude) * normalize(direction)`.
void test_observer_6_constant_velocity_from_config() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    const rr::camera::GpuCamera gc = make_test_gpu_camera();
    rr::relativity::Observer    obs;
    obs.velocity = Vec3{0.9f, 0.0f, 0.0f};    // would-be-overridden legacy

    ObserverConfig cfg{};
    cfg.perception_mode = PerceptionMode::ConstantVelocityMinkowski;
    cfg.beta_magnitude  = 0.5f;
    cfg.direction       = Vec3{1.0f, 0.0f, 0.0f};

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    // CLI overlay won: beta = (0.5, 0, 0); observer.velocity
    // (0.9, 0, 0) was discarded.
    RR_CHECK(approx(f.beta, Vec3{0.5f, 0.0f, 0.0f}));
    RR_CHECK(is_normalised_timelike(f, minkowski_metric()));
}

// Case 6: ConstantVelocityMinkowski with config.direction
// pre-normalised at a non-unit length. The adapter normalises
// the direction before scaling by clampBeta(magnitude).
void test_observer_6_config_direction_normalised() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::length;

    const rr::camera::GpuCamera gc = make_test_gpu_camera();
    const rr::relativity::Observer obs{};
    ObserverConfig cfg{};
    cfg.perception_mode = PerceptionMode::ConstantVelocityMinkowski;
    cfg.beta_magnitude  = 0.6f;
    // |direction| = 3, not unit. Adapter should normalise.
    cfg.direction = Vec3{3.0f, 0.0f, 0.0f};

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    // Resulting magnitude must match beta_magnitude exactly
    // (no scale leakage from the non-unit input).
    const float mag = length(f.beta);
    RR_CHECK(approx(mag, 0.6f));
    RR_CHECK(approx(f.beta, Vec3{0.6f, 0.0f, 0.0f}));
}

// Case 7: ConstantVelocityMinkowski with
// config.beta_magnitude != 0 AND config.direction == (0,0,0).
// The adapter falls back to the camera's forward axis (the
// documented sentinel behaviour; mirrors the `--render-demo`
// precedent).
void test_observer_6_zero_direction_falls_back_to_camera_forward() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::length;

    const rr::camera::GpuCamera gc = make_test_gpu_camera();
    // gc.forward == (0, 0, -1).
    const rr::relativity::Observer obs{};
    ObserverConfig cfg{};
    cfg.perception_mode = PerceptionMode::ConstantVelocityMinkowski;
    cfg.beta_magnitude  = 0.4f;
    cfg.direction       = Vec3{0.0f, 0.0f, 0.0f};  // sentinel

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    // Resulting beta is along (0, 0, -1) at magnitude 0.4.
    RR_CHECK(approx(f.beta, Vec3{0.0f, 0.0f, -0.4f}));
    RR_CHECK(approx(length(f.beta), 0.4f));
}

// Case 8: defensive clamp on the resolved 3-velocity. A
// non-trivial legacy `observer.velocity` (e.g. injected by a
// scene file at |beta| > 0.999999) must be capped at
// clampBeta's cap before the velocity4 derivation. The
// adapter's defensive second-clamp catches this.
void test_observer_6_clamp_beta_safety() {
    using namespace rr::manifold;
    using rr::math::Vec3;
    using rr::math::length;

    const rr::camera::GpuCamera gc = make_test_gpu_camera();
    rr::relativity::Observer    obs;
    // |beta| = 1.5 (faster than light); the adapter MUST clamp.
    obs.velocity = Vec3{1.5f, 0.0f, 0.0f};
    ObserverConfig cfg{};
    cfg.perception_mode = PerceptionMode::ConstantVelocityMinkowski;
    cfg.beta_magnitude  = 0.0f;  // CLI-zero -> use observer

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    const float mag = length(f.beta);
    RR_CHECK(mag <= 0.999999f + 1.0e-6f);
    // The frame's per-scalar fields are all finite (clamp
    // ensures gamma stays finite). The
    // `is_normalised_timelike` check is intentionally NOT run
    // at the clampBeta cap: single-precision floats lose
    // enough precision in the `gamma^2 * (1 - beta^2)`
    // catastrophic-cancellation that the residual exceeds the
    // 1.0e-4f tolerance. The lower-beta tests
    // (`test_observer_6_constant_velocity_from_legacy_observer`
    // at |beta| = 0.5) cover the normalisation invariant in
    // the precision-stable regime.
    RR_CHECK(is_finite_observer_frame(f));
}

// Case 9: CurvedChartGeodesicPlaceholder mode is a structural
// passthrough this slice: the adapter returns rest_frame()
// byte-for-byte EXCEPT preserves the perception_mode tag so
// downstream kernels can distinguish.
void test_observer_6_curved_placeholder_returns_rest_with_tag() {
    using namespace rr::manifold;

    const rr::camera::GpuCamera     gc  = make_test_gpu_camera();
    rr::relativity::Observer        obs;
    obs.velocity = rr::math::Vec3{0.7f, 0.0f, 0.0f};  // non-trivial
    ObserverConfig                  cfg{};
    cfg.perception_mode = PerceptionMode::CurvedChartGeodesicPlaceholder;
    cfg.beta_magnitude  = 0.5f;
    cfg.proper_time     = 99.0f;

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    // Every scalar field matches rest_frame() EXCEPT
    // perception_mode (which is preserved as the tag).
    ObserverFrame ref = rest_frame();
    RR_CHECK(approx(f.position4, ref.position4));
    RR_CHECK(approx(f.velocity4, ref.velocity4));
    RR_CHECK(approx(f.beta,      ref.beta));
    RR_CHECK(approx(f.right,     ref.right));
    RR_CHECK(approx(f.up,        ref.up));
    RR_CHECK(approx(f.forward,   ref.forward));
    RR_CHECK(f.proper_time     == 0.0f);
    RR_CHECK(f.coordinate_time == 0.0f);
    RR_CHECK(f.perception_mode ==
             PerceptionMode::CurvedChartGeodesicPlaceholder);
}

// Case 10: tetrad orthonormality. For a default Camera the
// adapter's tetrad legs are the world basis (right-handed);
// is_orthonormal_tetrad(...) passes analytically.
void test_observer_6_tetrad_orthonormal() {
    using namespace rr::manifold;

    const rr::camera::GpuCamera     gc  = make_test_gpu_camera();
    const rr::relativity::Observer  obs{};
    ObserverConfig                  cfg{};
    cfg.perception_mode = PerceptionMode::ConstantVelocityMinkowski;

    ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
    RR_CHECK(is_orthonormal_tetrad(f));
}

// Case 11: finite-value guarantee. For every documented
// perception mode the adapter's output passes the
// is_finite_observer_frame validator.
void test_observer_6_finite_value_guarantee() {
    using namespace rr::manifold;
    using rr::math::Vec3;

    const rr::camera::GpuCamera gc = make_test_gpu_camera();
    rr::relativity::Observer    obs;
    obs.velocity = Vec3{0.3f, -0.4f, 0.0f};

    const PerceptionMode modes[] = {
        PerceptionMode::Identity,
        PerceptionMode::ConstantVelocityMinkowski,
        PerceptionMode::CurvedChartGeodesicPlaceholder,
    };
    for (PerceptionMode mode : modes) {
        ObserverConfig cfg{};
        cfg.perception_mode = mode;
        cfg.beta_magnitude  = 0.4f;
        cfg.direction       = Vec3{1.0f, 0.0f, 0.0f};
        cfg.proper_time     = 7.5f;
        ObserverFrame f = build_observer_frame_from_camera(gc, obs, cfg);
        RR_CHECK(is_finite_observer_frame(f));
        RR_CHECK(is_orthonormal_tetrad(f));
        RR_CHECK(is_normalised_timelike(f, minkowski_metric()));
    }
}

// Case 12: proper_time propagation (the OBSERVER.4 CLI
// surface threads through). On ConstantVelocityMinkowski the
// resulting frame carries the config's proper_time verbatim;
// on Identity / CurvedChartGeodesicPlaceholder the
// proper_time defaults to zero (the rest_frame() anchor).
void test_observer_6_proper_time_propagates() {
    using namespace rr::manifold;

    const rr::camera::GpuCamera     gc  = make_test_gpu_camera();
    const rr::relativity::Observer  obs{};

    // ConstantVelocityMinkowski: tau threads through.
    ObserverConfig cfg_cv{};
    cfg_cv.perception_mode = PerceptionMode::ConstantVelocityMinkowski;
    cfg_cv.proper_time     = 42.5f;
    ObserverFrame f_cv = build_observer_frame_from_camera(gc, obs, cfg_cv);
    RR_CHECK(f_cv.proper_time == 42.5f);

    // Identity: tau is NOT threaded (the no-op anchor returns
    // rest_frame() byte-for-byte; config.proper_time is
    // ignored on this path).
    ObserverConfig cfg_id{};
    cfg_id.perception_mode = PerceptionMode::Identity;
    cfg_id.proper_time     = 42.5f;
    ObserverFrame f_id = build_observer_frame_from_camera(gc, obs, cfg_id);
    RR_CHECK(f_id.proper_time == 0.0f);
}

// OBS-PERCEPT.3 — unified primary-ray aberration helper tests.
// Verifies the three-gate activation logic from the
// OBS-PERCEPT.2 task brief §2 + the no-op invariants from §3.
void test_obs_percept_3_identity_mode_returns_input_direction() {
    using rr::manifold::ObserverFrame;
    using rr::manifold::PerceptionMode;
    using rr::manifold::apply_observer_primary_ray_aberration;
    using rr::math::Vec3;

    // Default ObserverFrame carries perception_mode = Identity.
    // The outer gate closes; the helper returns the input
    // direction unchanged regardless of beta.
    ObserverFrame f;
    f.perception_mode = PerceptionMode::Identity;
    f.beta            = Vec3{0.5f, 0.0f, 0.0f};  // non-zero, deliberately

    const Vec3 in_dir{0.0f, 0.0f, -1.0f};
    const Vec3 out_dir =
        apply_observer_primary_ray_aberration(f, in_dir);

    // Identity gate closes → direction unchanged byte-for-byte.
    RR_CHECK(out_dir.x == in_dir.x);
    RR_CHECK(out_dir.y == in_dir.y);
    RR_CHECK(out_dir.z == in_dir.z);
}

void test_obs_percept_3_constant_velocity_zero_beta_returns_input() {
    using rr::manifold::ObserverFrame;
    using rr::manifold::PerceptionMode;
    using rr::manifold::apply_observer_primary_ray_aberration;
    using rr::math::Vec3;

    // ConstantVelocityMinkowski mode + zero beta. Outer gate
    // opens; inner |beta|>0 gate closes; the helper returns
    // the input direction unchanged.
    ObserverFrame f;
    f.perception_mode = PerceptionMode::ConstantVelocityMinkowski;
    f.beta            = Vec3{0.0f, 0.0f, 0.0f};

    const Vec3 in_dir{0.0f, 0.0f, -1.0f};
    const Vec3 out_dir =
        apply_observer_primary_ray_aberration(f, in_dir);

    // Inner gate closes → direction unchanged byte-for-byte.
    RR_CHECK(out_dir.x == in_dir.x);
    RR_CHECK(out_dir.y == in_dir.y);
    RR_CHECK(out_dir.z == in_dir.z);
}

void test_obs_percept_3_constant_velocity_nonzero_beta_aberrates() {
    using rr::manifold::ObserverFrame;
    using rr::manifold::PerceptionMode;
    using rr::manifold::apply_observer_primary_ray_aberration;
    using rr::math::Vec3;

    // ConstantVelocityMinkowski + non-zero beta. Both gates
    // open; the helper applies the
    // rr::relativity::aberrateDirection math leaf.
    ObserverFrame f;
    f.perception_mode = PerceptionMode::ConstantVelocityMinkowski;
    f.beta            = Vec3{0.0f, 0.0f, -0.5f};

    // Forward-direction ray (along the observer's velocity axis).
    const Vec3 in_dir{0.0f, 0.0f, -1.0f};
    const Vec3 out_dir =
        apply_observer_primary_ray_aberration(f, in_dir);

    // The aberration math leaf at beta = (0, 0, -0.5) +
    // direction = (0, 0, -1) returns the same direction
    // (the ray is anti-parallel to beta, so the boost is
    // purely along the direction; no transverse aberration).
    // Net result: out_dir.z stays negative; magnitude stays 1.
    const float out_mag2 =
        out_dir.x * out_dir.x +
        out_dir.y * out_dir.y +
        out_dir.z * out_dir.z;
    RR_CHECK(approx(out_mag2, 1.0f, 1.0e-5f));
    RR_CHECK(out_dir.z < 0.0f);

    // Now a TRANSVERSE direction: ray going along +X while
    // observer moves along -Z. The aberration leaf should
    // produce a non-trivial directional change (the X axis
    // bends toward the direction of motion).
    const Vec3 transverse_in{1.0f, 0.0f, 0.0f};
    const Vec3 transverse_out =
        apply_observer_primary_ray_aberration(f, transverse_in);
    // Boosted direction still unit-length:
    const float t_mag2 =
        transverse_out.x * transverse_out.x +
        transverse_out.y * transverse_out.y +
        transverse_out.z * transverse_out.z;
    RR_CHECK(approx(t_mag2, 1.0f, 1.0e-5f));
    // And materially different from the input (transverse
    // aberration bends the direction). We don't pin the
    // exact value here (the math leaf has its own tests);
    // we just verify the helper composes the leaf correctly
    // by asserting a non-trivial change.
    const bool changed =
        std::fabs(transverse_out.x - transverse_in.x) > 1.0e-4f ||
        std::fabs(transverse_out.z - transverse_in.z) > 1.0e-4f;
    RR_CHECK(changed);
}

void test_obs_percept_3_curved_placeholder_returns_input() {
    using rr::manifold::ObserverFrame;
    using rr::manifold::PerceptionMode;
    using rr::manifold::apply_observer_primary_ray_aberration;
    using rr::math::Vec3;

    // CurvedChartGeodesicPlaceholder mode: outer gate
    // closes (only ConstantVelocityMinkowski opens it);
    // the helper returns the input direction unchanged.
    // Master rule #3: the placeholder mode is honestly
    // documented as no-output-this-slice; the OBS-PERCEPT.3
    // helper preserves this contract.
    ObserverFrame f;
    f.perception_mode = PerceptionMode::CurvedChartGeodesicPlaceholder;
    f.beta            = Vec3{0.5f, 0.0f, 0.0f};  // non-zero, deliberately

    const Vec3 in_dir{0.0f, 0.0f, -1.0f};
    const Vec3 out_dir =
        apply_observer_primary_ray_aberration(f, in_dir);

    // Outer gate closes on the placeholder mode → direction
    // unchanged byte-for-byte.
    RR_CHECK(out_dir.x == in_dir.x);
    RR_CHECK(out_dir.y == in_dir.y);
    RR_CHECK(out_dir.z == in_dir.z);
}

}  // namespace

int main() {
    test_minkowski_metric_creation();
    test_euclidean_chart_default();
    test_world_to_chart_identity();
    test_chart_to_world_identity();
    test_transform_direction_identity();
    test_manifold_mode_disabled_by_default();
    test_observer_frame_defaults();
    test_geodesic_state_defaults();

    // SCHW.1: Schwarzschild-like artistic coordinate-warp math.
    test_schw_1_validate_params();
    test_schw_1_world_to_chart_euclidean_fallback();
    test_schw_1_world_to_chart_far_field_identity();
    test_schw_1_world_to_chart_known_value();
    test_schw_1_world_to_chart_clamp_radius_safety();
    test_schw_1_chart_to_world_inverse_residual();
    test_schw_1_chart_to_world_euclidean_fallback();
    test_schw_1_warp_ray_direction_euclidean_fallback();
    test_schw_1_warp_ray_direction_bend_cap();
    test_schw_1_warp_ray_direction_bends_toward_mass();

    // SCHW.3: ManifoldTransform integration.
    test_schw_3_disabled_identity_preserved();
    test_schw_3_euclidean_identity_preserved();
    test_schw_3_schwarzschild_like_zero_mass_is_identity();
    test_schw_3_world_to_chart_schwarzschild_like_known_value();
    test_schw_3_chart_to_world_schwarzschild_like_round_trip();
    test_schw_3_no_nan_inf_near_clamp_radius();
    test_schw_3_params_from_chart();
    test_schw_3_other_non_euclidean_passthrough();

    // PENROSE.2: Penrose-like artistic compactification math.
    test_penrose_2_validate_params();
    test_penrose_2_world_to_chart_identity_at_strength_zero();
    test_penrose_2_world_to_chart_bounded_for_large_distance();
    test_penrose_2_world_to_chart_no_nan_inf();
    test_penrose_2_world_to_chart_monotonic_radial_compression();
    test_penrose_2_world_to_chart_safe_near_origin();
    test_penrose_2_chart_to_world_inverse_residual();
    test_penrose_2_chart_to_world_euclidean_fallback();
    test_penrose_2_chart_to_world_boundary_clamp();

    // PENROSE.4: ManifoldTransform integration.
    test_penrose_4_disabled_identity_preserved();
    test_penrose_4_euclidean_identity_preserved();
    test_penrose_4_penrose_like_zero_mass_is_identity();
    test_penrose_4_world_to_chart_penrose_like_bounded();
    test_penrose_4_chart_to_world_penrose_like_round_trip();
    test_penrose_4_no_nan_inf_for_large_coordinates();
    test_penrose_4_params_from_chart();
    test_penrose_4_other_non_euclidean_passthrough();

    // OBSERVER.2: ObserverFrame data model.
    test_observer_2_perception_mode_default();
    test_observer_2_orthonormal_tetrad_default();
    test_observer_2_finite_observer_frame();
    test_observer_2_default_no_deformation();

    // OBSERVER.6: Camera-to-observer adapter.
    test_observer_6_default_is_camera_equivalent_no_op();
    test_observer_6_identity_mode_ignores_camera();
    test_observer_6_constant_velocity_zero_beta();
    test_observer_6_constant_velocity_from_legacy_observer();
    test_observer_6_constant_velocity_from_config();
    test_observer_6_config_direction_normalised();
    test_observer_6_zero_direction_falls_back_to_camera_forward();
    test_observer_6_clamp_beta_safety();

    // OBS-PERCEPT.3: unified primary-ray aberration helper.
    test_obs_percept_3_identity_mode_returns_input_direction();
    test_obs_percept_3_constant_velocity_zero_beta_returns_input();
    test_obs_percept_3_constant_velocity_nonzero_beta_aberrates();
    test_obs_percept_3_curved_placeholder_returns_input();
    test_observer_6_curved_placeholder_returns_rest_with_tag();
    test_observer_6_tetrad_orthonormal();
    test_observer_6_finite_value_guarantee();
    test_observer_6_proper_time_propagates();

    std::printf("manifold_identity_tests: %d / %d checks passed\n",
                g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

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

#include "manifold/CoordinateChart.h"
#include "manifold/GeodesicState.h"
#include "manifold/ManifoldMode.h"
#include "manifold/ManifoldTransform.h"
#include "manifold/MetricTensor.h"
#include "manifold/ObserverFrame.h"

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

    std::printf("manifold_identity_tests: %d / %d checks passed\n",
                g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

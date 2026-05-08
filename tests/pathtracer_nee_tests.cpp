// NEE.4 host-side helper tests for
// `rr::pathtracer::sample_direct_light_uniform` (defined in
// `pathtracer/DirectLight.cuh`).
//
// The helper is `RR_HD inline`, so the same code path the
// CUDA / OptiX device kernels execute is also exercised here
// by the host C++ compiler. These tests anchor the behaviour
// the kernels rely on so a future regression on the helper's
// branching is caught at host-build time without requiring a
// CUDA-equipped runtime host.
//
// Coverage (per `docs/PATH_TRACER_NEE_AUDIT.md` §3.3 and
// `docs/PATH_TRACER_NEE_TASK.md` §5.7):
//   - count == 0 and lights == nullptr return zero-contribution
//     samples (`pdf_inv == 0`).
//   - Point light in front of receiver: `pdf_inv == count`,
//     `wi` points from hit toward light, `distance == |light -
//     hit|`, `li_unattenuated` carries 1/r² falloff.
//   - Point light behind receiver (cos_theta <= 0): zero
//     contribution.
//   - Point light coincident with receiver (r² == 0): zero
//     contribution (avoids divide-by-zero).
//   - Directional light pointing toward receiver:
//     `distance == kDirectionalShadowTMax`, no falloff,
//     `wi == -normalize(direction)`.
//   - Directional light pointing away from receiver: zero
//     contribution.
//   - Directional light with degenerate direction (zero
//     vector): zero contribution.
//   - PLACEHOLDER `LightType::Area` and `LightType::Environment`:
//     zero contribution (deferred per `Light.h:20-31`).
//   - Uniform-by-count selection: with `count == 4` and
//     `u_select` walking the (1/8, 3/8, 5/8, 7/8) bin centres,
//     each bin maps to a distinct light index.
//
// Hand-rolled assertions; same RR_CHECK pattern as
// pathtracer_tests.cpp / math_tests.cpp.

#include "pathtracer/DirectLight.cuh"
#include "pathtracer/DirectLight.h"
#include "lighting/Light.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"

#include <cmath>
#include <cstdio>
#include <cstring>      // NEE.5 (test expansion): std::memcmp for byte-identity

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

constexpr float kEps = 1e-5f;

bool approx(float a, float b, float eps = kEps) {
    return std::fabs(a - b) <= eps;
}

using rr::lighting::Light;
using rr::lighting::LightType;
using rr::math::Vec3;
using rr::pathtracer::DirectLightSample;
using rr::pathtracer::sample_direct_light_uniform;
using rr::pathtracer::kDirectionalShadowTMax;

Light make_point(Vec3 pos, Vec3 color, float intensity) {
    Light L;
    L.type      = LightType::Point;
    L.position  = pos;
    L.color     = color;
    L.intensity = intensity;
    return L;
}

Light make_directional(Vec3 dir, Vec3 color, float intensity) {
    Light L;
    L.type      = LightType::Directional;
    L.direction = dir;
    L.color     = color;
    L.intensity = intensity;
    return L;
}

Light make_area() {
    Light L;
    L.type = LightType::Area;
    return L;
}

Light make_environment(Vec3 color, float intensity) {
    Light L;
    L.type      = LightType::Environment;
    L.color     = color;
    L.intensity = intensity;
    return L;
}

// ---------- count / pointer guards ----------

void test_zero_count_returns_zero_contribution() {
    Light dummy = make_point(Vec3{1.0f, 0.0f, 0.0f},
                             Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    const DirectLightSample s = sample_direct_light_uniform(
        &dummy, /*count=*/0,
        /*hit_position=*/Vec3{0.0f, 0.0f, 0.0f},
        /*normal=*/Vec3{0.0f, 1.0f, 0.0f},
        /*u_select=*/0.5f);
    RR_CHECK(approx(s.pdf_inv, 0.0f));
    RR_CHECK(approx(s.distance, 0.0f));
}

void test_null_lights_returns_zero_contribution() {
    const DirectLightSample s = sample_direct_light_uniform(
        /*lights=*/nullptr, /*count=*/3,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.5f);
    RR_CHECK(approx(s.pdf_inv, 0.0f));
}

// ---------- Point light cases ----------

void test_point_light_in_front() {
    // Receiver at origin, normal +Y; light at (0, 5, 0).
    // wi should be +Y; distance = 5; li = color * intensity / 25.
    const Light L = make_point(Vec3{0.0f, 5.0f, 0.0f},
                               Vec3{1.0f, 1.0f, 1.0f},
                               /*intensity=*/2.0f);
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);
    RR_CHECK(approx(s.pdf_inv, 1.0f));
    RR_CHECK(approx(s.distance, 5.0f));
    RR_CHECK(approx(s.wi.x, 0.0f) && approx(s.wi.y, 1.0f)
          && approx(s.wi.z, 0.0f));
    // 1/r² = 1/25 = 0.04; intensity = 2; expected = 2 * 0.04 = 0.08.
    RR_CHECK(approx(s.li_unattenuated.x, 0.08f));
    RR_CHECK(approx(s.li_unattenuated.y, 0.08f));
    RR_CHECK(approx(s.li_unattenuated.z, 0.08f));
}

void test_point_light_behind_receiver() {
    // Receiver at origin, normal +Y; light at (0, -5, 0).
    // The cos to the sample direction is -1; helper returns zero.
    const Light L = make_point(Vec3{0.0f, -5.0f, 0.0f},
                               Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);
    RR_CHECK(approx(s.pdf_inv, 0.0f));
}

void test_point_light_coincident() {
    // Receiver and light at the same position: r² == 0; helper
    // returns zero (no divide-by-zero).
    const Light L = make_point(Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);
    RR_CHECK(approx(s.pdf_inv, 0.0f));
}

// ---------- Directional light cases ----------

void test_directional_light_toward_surface() {
    // Light direction = (0, -1, 0) means photons travel down;
    // the to-light direction is (0, +1, 0). Receiver normal is
    // +Y so cos_theta = +1; sample is valid.
    const Light L = make_directional(Vec3{0.0f, -1.0f, 0.0f},
                                     Vec3{1.0f, 0.95f, 0.85f},
                                     /*intensity=*/0.9f);
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);
    RR_CHECK(approx(s.pdf_inv, 1.0f));
    RR_CHECK(approx(s.distance, kDirectionalShadowTMax));
    RR_CHECK(approx(s.wi.x, 0.0f) && approx(s.wi.y, 1.0f)
          && approx(s.wi.z, 0.0f));
    // No 1/r² falloff: li_unattenuated = color * intensity.
    RR_CHECK(approx(s.li_unattenuated.x, 0.9f));
    RR_CHECK(approx(s.li_unattenuated.y, 0.9f * 0.95f));
    RR_CHECK(approx(s.li_unattenuated.z, 0.9f * 0.85f));
}

void test_directional_light_pointing_up_misses_surface() {
    // Light direction = (0, +1, 0): photons travel up. The
    // to-light is (0, -1, 0); cos_theta against the +Y normal
    // is -1 (light is behind the receiver); helper returns zero.
    const Light L = make_directional(Vec3{0.0f, 1.0f, 0.0f},
                                     Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);
    RR_CHECK(approx(s.pdf_inv, 0.0f));
}

void test_directional_light_zero_direction() {
    // Degenerate Directional light: zero direction vector.
    // The helper guards against divide-by-zero and returns
    // a zero-contribution sample.
    const Light L = make_directional(Vec3{0.0f, 0.0f, 0.0f},
                                     Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);
    RR_CHECK(approx(s.pdf_inv, 0.0f));
}

// ---------- placeholder light types ----------

void test_area_light_placeholder_returns_zero() {
    const Light L = make_area();
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);
    RR_CHECK(approx(s.pdf_inv, 0.0f));
}

void test_environment_light_placeholder_returns_zero() {
    const Light L = make_environment(Vec3{0.4f, 0.6f, 0.9f}, 1.0f);
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);
    RR_CHECK(approx(s.pdf_inv, 0.0f));
}

// ---------- uniform-by-count selection ----------

void test_uniform_selection_walks_bin_centres() {
    // Four point lights, each at a distinct +Y position so the
    // returned `distance` fingerprints which one was picked.
    Light Ls[4] = {
        make_point(Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, 1.0f),
        make_point(Vec3{0.0f, 2.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 1.0f),
        make_point(Vec3{0.0f, 3.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, 1.0f),
        make_point(Vec3{0.0f, 4.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}, 1.0f),
    };

    const float bin_centres[4] = {0.125f, 0.375f, 0.625f, 0.875f};
    const float expected_distances[4] = {1.0f, 2.0f, 3.0f, 4.0f};

    for (int i = 0; i < 4; ++i) {
        const DirectLightSample s = sample_direct_light_uniform(
            Ls, /*count=*/4,
            Vec3{0.0f, 0.0f, 0.0f},
            Vec3{0.0f, 1.0f, 0.0f},
            bin_centres[i]);
        // pdf_inv == count == 4 across every valid bin.
        RR_CHECK(approx(s.pdf_inv, 4.0f));
        RR_CHECK(approx(s.distance, expected_distances[i]));
    }

    // Defence-in-depth: u_select == 1.0f maps to li == count and
    // is clamped back to count - 1 (the last light). Verifies
    // the in-helper clamp at `DirectLight.cuh:124-129`.
    const DirectLightSample s_top = sample_direct_light_uniform(
        Ls, /*count=*/4,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        /*u_select=*/1.0f);
    RR_CHECK(approx(s_top.pdf_inv, 4.0f));
    RR_CHECK(approx(s_top.distance, 4.0f));
}

// ---------- NEE.5 (test expansion) byte-identity anchors ----------
//
// Per `docs/PATH_TRACER_ENABLE_NEE_CLI_TASK.md` §4.2 Option A
// (host-only deterministic-arithmetic anchor) — the audit host
// cannot run the kernel, so a runtime PPM `cmp` (Option B) is
// recorded as runtime-deferred in the BUILD_PLAN. These two
// host-only cases are the runnable byte-identity proxy for the
// default-OFF path: the helper is the kernel's only stateful
// per-bounce consumer of the NEE flag, so anchoring its
// determinism + the bit-equal-default null-guard output anchors
// the kernel-level byte-identity argument by extension.

// Determinism anchor: calling the helper twice with the same
// inputs produces bit-equal samples. Anchors that the helper
// is a pure function of its arguments (no hidden global / TLS
// state); a future regression that introduces non-determinism
// (e.g. caching, threading) is caught at host-build time.
void test_helper_determinism() {
    const Light L = make_point(Vec3{1.0f, 2.0f, 3.0f},
                               Vec3{0.4f, 0.6f, 0.9f},
                               /*intensity=*/1.5f);
    const DirectLightSample s1 = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        /*u_select=*/0.5f);
    const DirectLightSample s2 = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        /*u_select=*/0.5f);
    RR_CHECK(std::memcmp(&s1, &s2, sizeof(DirectLightSample)) == 0);
}

// Bit-equal-default anchor: the zero-contribution sample
// produced by the helper's `lights == nullptr || count <= 0`
// guard is bit-equal with a default-constructed
// `DirectLightSample`. This is the formal byte-identity proxy
// for the default-OFF code path: when `enable_nee == false` (or
// `enable_nee == true` on a no-lights scene), the kernel guard
// short-circuits and the per-pixel `radiance` accumulator
// receives nothing. Bit-equality with the default constructor
// ensures the helper does not emit subtle FP noise (e.g.
// negative-zero `-0.0f` vs `+0.0f`) that could in principle
// disagree with the static IEEE-754 byte-identity argument
// from `PATH_TRACER_NEE_AUDIT.md` §1.2. memcmp distinguishes
// `+0.0f` from `-0.0f`; if a future regression introduces
// negative-zero into the zero path, this case fails.
void test_zero_contribution_is_bit_default() {
    const DirectLightSample default_constructed;  // {0,0,0; 0; 0,0,0; 0}

    // Null-pointer guard.
    const DirectLightSample s_null = sample_direct_light_uniform(
        /*lights=*/nullptr, /*count=*/3,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        /*u_select=*/0.5f);
    RR_CHECK(std::memcmp(&s_null, &default_constructed,
                         sizeof(DirectLightSample)) == 0);

    // Zero-count guard.
    const Light dummy = make_point(Vec3{1.0f, 0.0f, 0.0f},
                                   Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    const DirectLightSample s_zero = sample_direct_light_uniform(
        &dummy, /*count=*/0,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        /*u_select=*/0.5f);
    RR_CHECK(std::memcmp(&s_zero, &default_constructed,
                         sizeof(DirectLightSample)) == 0);
}

// ---------- MIS.3 light PDF data model anchors ----------
//
// Per `docs/PATH_TRACER_MIS_LIGHT_PDF_TASK.md` §5.5 (three
// mandatory cases). The new fields are inert at v1 — no
// caller reads them — so these cases anchor the
// data-model contract: Point + Directional set
// `is_delta == true` + sentinel `pdf_solid_angle == 0.0f`;
// every "no contribution" branch leaves both fields at
// their bit-zero defaults. Future MIS-aware integrator
// slices (MIS.5 CUDA, MIS.6 OptiX) consume these fields;
// a regression that flips `is_delta` to false for v1
// delta lights would silently break the future MIS
// helper's short-circuit, producing biased renders. The
// host-only test catches it at host-build time.

void test_point_light_sets_is_delta_and_zero_pdf() {
    // Reuse the same fixture as
    // test_point_light_in_front: receiver at origin,
    // normal +Y, light at (0, 5, 0), intensity 2.
    const Light L = make_point(Vec3{0.0f, 5.0f, 0.0f},
                               Vec3{1.0f, 1.0f, 1.0f}, 2.0f);
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);

    // MIS.3 contract: Point light is a Dirac delta.
    RR_CHECK(s.is_delta == true);
    RR_CHECK(s.pdf_solid_angle == 0.0f);

    // The four pre-existing fields are unchanged from the
    // NEE.5 byte-identity baseline (cross-check against
    // test_point_light_in_front's expected values).
    RR_CHECK(approx(s.pdf_inv, 1.0f));
    RR_CHECK(approx(s.distance, 5.0f));
    RR_CHECK(approx(s.wi.y, 1.0f));
    // li = intensity * (1/r²) = 2 * 1/25 = 0.08.
    RR_CHECK(approx(s.li_unattenuated.x, 0.08f));
}

void test_directional_light_sets_is_delta_and_zero_pdf() {
    // Reuse the test_directional_light_toward_surface
    // fixture: light direction (0, -1, 0); receiver
    // normal +Y; expected wi = (0, +1, 0).
    const Light L = make_directional(Vec3{0.0f, -1.0f, 0.0f},
                                     Vec3{1.0f, 0.95f, 0.85f},
                                     0.9f);
    const DirectLightSample s = sample_direct_light_uniform(
        &L, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
        0.0f);

    // MIS.3 contract: Directional light is a Dirac delta.
    RR_CHECK(s.is_delta == true);
    RR_CHECK(s.pdf_solid_angle == 0.0f);

    // Pre-existing fields unchanged from NEE.5 baseline.
    RR_CHECK(approx(s.pdf_inv, 1.0f));
    RR_CHECK(approx(s.distance, kDirectionalShadowTMax));
    RR_CHECK(approx(s.wi.y, 1.0f));
}

void test_zero_contribution_sample_has_default_is_delta() {
    // Every "no contribution" branch must leave both new
    // fields at bit-zero defaults so the existing NEE.5
    // bit-default memcmp anchor continues to pass. This
    // case directly verifies eight zero-contribution
    // branches against the field-by-field expectation.

    // Helper to assert both new fields are at defaults.
    auto check_default = [](const DirectLightSample& s,
                            const char* tag) {
        if (!(s.is_delta == false)) {
            std::fprintf(stderr,
                         "FAIL: %s leaked is_delta == true\n", tag);
            ++g_failed; ++g_total; return;
        }
        if (!(s.pdf_solid_angle == 0.0f)) {
            std::fprintf(stderr,
                         "FAIL: %s leaked pdf_solid_angle != 0\n",
                         tag);
            ++g_failed; ++g_total; return;
        }
        ++g_total;  // recorded as a passing assertion.
    };

    // 1. count == 0.
    Light dummy = make_point(Vec3{1.0f, 0.0f, 0.0f},
                             Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    check_default(sample_direct_light_uniform(
        &dummy, /*count=*/0,
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.5f),
        "count==0");

    // 2. lights == nullptr.
    check_default(sample_direct_light_uniform(
        nullptr, /*count=*/3,
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.5f),
        "nullptr lights");

    // 3. Point light coincident with receiver (r² == 0).
    const Light coincident = make_point(Vec3{0.0f, 0.0f, 0.0f},
                                        Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    check_default(sample_direct_light_uniform(
        &coincident, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f),
        "coincident point");

    // 4. Point light behind receiver.
    const Light behind = make_point(Vec3{0.0f, -5.0f, 0.0f},
                                    Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    check_default(sample_direct_light_uniform(
        &behind, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f),
        "point behind");

    // 5. Directional pointing away (toward-light below
    //    horizon).
    const Light dir_away = make_directional(Vec3{0.0f, 1.0f, 0.0f},
                                            Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    check_default(sample_direct_light_uniform(
        &dir_away, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f),
        "directional away");

    // 6. Directional with zero-length direction.
    const Light dir_zero = make_directional(Vec3{0.0f, 0.0f, 0.0f},
                                            Vec3{1.0f, 1.0f, 1.0f}, 1.0f);
    check_default(sample_direct_light_uniform(
        &dir_zero, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f),
        "directional zero");

    // 7. Area-light PLACEHOLDER.
    const Light area = make_area();
    check_default(sample_direct_light_uniform(
        &area, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f),
        "area placeholder");

    // 8. Environment-light PLACEHOLDER.
    const Light env = make_environment(Vec3{0.4f, 0.6f, 0.9f}, 1.0f);
    check_default(sample_direct_light_uniform(
        &env, /*count=*/1,
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f),
        "environment placeholder");
}

}  // namespace

int main() {
    test_zero_count_returns_zero_contribution();
    test_null_lights_returns_zero_contribution();
    test_point_light_in_front();
    test_point_light_behind_receiver();
    test_point_light_coincident();
    test_directional_light_toward_surface();
    test_directional_light_pointing_up_misses_surface();
    test_directional_light_zero_direction();
    test_area_light_placeholder_returns_zero();
    test_environment_light_placeholder_returns_zero();
    test_uniform_selection_walks_bin_centres();
    test_helper_determinism();                    // NEE.5
    test_zero_contribution_is_bit_default();      // NEE.5
    test_point_light_sets_is_delta_and_zero_pdf();           // MIS.3
    test_directional_light_sets_is_delta_and_zero_pdf();     // MIS.3
    test_zero_contribution_sample_has_default_is_delta();    // MIS.3

    std::fprintf(stderr, "pathtracer_nee_tests: %d/%d passed\n",
                 g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

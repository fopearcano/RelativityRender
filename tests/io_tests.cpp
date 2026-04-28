// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.
//
// Loads `scenes/test_minimal.rrscene` and prints what was parsed,
// then asserts a handful of expected values. The fixture path is
// passed in via the `RR_TEST_FIXTURES_DIR` compile definition so
// the test runs from any working directory `ctest` chooses.

#include "io/SceneLoader.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#ifndef RR_TEST_FIXTURES_DIR
    #error "RR_TEST_FIXTURES_DIR must be defined by the build system"
#endif

namespace {

int g_total  = 0;
int g_failed = 0;

float abs_f(float a) { return a < 0.0f ? -a : a; }

bool nearly_equal(float a, float b, float eps = 1.0e-5f) {
    const float scale  = 1.0f > abs_f(a) ? 1.0f : abs_f(a);
    const float scale2 = scale > abs_f(b) ? scale : abs_f(b);
    return abs_f(a - b) <= eps * scale2;
}

bool nearly_equal(rr::math::Vec3 a, rr::math::Vec3 b, float eps = 1.0e-5f) {
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

void print_scene(const rr::scene::Scene& s) {
    std::printf("--- loaded scene ---\n");
    std::printf("  render_settings:\n");
    std::printf("    width  = %d\n", s.render_settings.width);
    std::printf("    height = %d\n", s.render_settings.height);

    std::printf("  camera:\n");
    std::printf("    position = (%.3f, %.3f, %.3f)\n",
                s.camera.position().x, s.camera.position().y, s.camera.position().z);
    std::printf("    forward  = (%.3f, %.3f, %.3f)\n",
                s.camera.forward().x, s.camera.forward().y, s.camera.forward().z);
    std::printf("    up       = (%.3f, %.3f, %.3f)\n",
                s.camera.up().x, s.camera.up().y, s.camera.up().z);
    std::printf("    fov      = %.2f deg\n",   s.camera.vertical_fov_degrees());
    std::printf("    aspect   = %.4f\n",       s.camera.aspect());

    std::printf("  relativity:\n");
    std::printf("    velocity = (%.3f, %.3f, %.3f)  |beta| = %.4f\n",
                s.observer.velocity.x, s.observer.velocity.y, s.observer.velocity.z,
                rr::math::length(s.observer.velocity));
    std::printf("    enable_aberration       = %s\n", s.relativity.enable_aberration ? "true" : "false");
    std::printf("    enable_doppler          = %s\n", s.relativity.enable_doppler    ? "true" : "false");
    std::printf("    enable_searchlight      = %s\n", s.relativity.enable_searchlight? "true" : "false");
    std::printf("    doppler_color_strength  = %.3f\n", s.relativity.doppler_color_strength);
    std::printf("    searchlight_strength    = %.3f\n", s.relativity.searchlight_strength);
    std::printf("---------------------\n");
}

void test_load_test_minimal_scene() {
    const std::filesystem::path fixture =
        std::filesystem::path(RR_TEST_FIXTURES_DIR) / "test_minimal.rrscene";

    auto result = rr::io::load_rrscene(fixture);
    if (!result.ok) {
        std::fprintf(stderr, "load_rrscene failed: %s\n", result.message.c_str());
    }
    RR_CHECK(result.ok);
    if (!result.ok) return;

    print_scene(result.scene);

    const auto& s = result.scene;
    RR_CHECK(s.render_settings.width  == 640);
    RR_CHECK(s.render_settings.height == 480);

    RR_CHECK(s.camera.position()                 == rr::math::Vec3(0.0f, 0.0f, 0.0f));
    RR_CHECK(nearly_equal(s.camera.forward(),       rr::math::Vec3(0.0f, 0.0f, -1.0f)));
    RR_CHECK(nearly_equal(s.camera.up(),            rr::math::Vec3(0.0f, 1.0f,  0.0f)));
    RR_CHECK(nearly_equal(s.camera.vertical_fov_degrees(), 50.0f));
    RR_CHECK(nearly_equal(s.camera.aspect(),        640.0f / 480.0f));

    // Relativity: velocity = beta * normalize(direction).
    RR_CHECK(nearly_equal(s.observer.velocity,
                          rr::math::Vec3(0.0f, 0.0f, -0.3f)));
    RR_CHECK(s.relativity.enable_aberration);
    RR_CHECK(s.relativity.enable_doppler);
    RR_CHECK(s.relativity.enable_searchlight);
    RR_CHECK(nearly_equal(s.relativity.doppler_color_strength, 1.0f));
    RR_CHECK(nearly_equal(s.relativity.searchlight_strength,   1.0f));
}

void test_missing_file_fails_predictably() {
    auto result = rr::io::load_rrscene(
        std::filesystem::path(RR_TEST_FIXTURES_DIR) / "does_not_exist.rrscene");
    RR_CHECK(!result.ok);
    RR_CHECK(!result.message.empty());
}

}

int main() {
    test_load_test_minimal_scene();
    test_missing_file_fails_predictably();

    std::printf("io_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

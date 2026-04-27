// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.

#include "lighting/Light.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"

#include <cstdio>
#include <cstdint>

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

using rr::lighting::Light;
using rr::lighting::LightType;
using rr::lighting::make_area_light;
using rr::lighting::make_directional_light;
using rr::lighting::make_environment_light;
using rr::lighting::make_point_light;
using rr::math::Vec3;

void test_light_type_enum_values() {
    // Stable ordinals are part of the upload contract.
    RR_CHECK(static_cast<int>(LightType::Point)       == 0);
    RR_CHECK(static_cast<int>(LightType::Directional) == 1);
    RR_CHECK(static_cast<int>(LightType::Area)        == 2);
    RR_CHECK(static_cast<int>(LightType::Environment) == 3);
}

void test_default_light_is_neutral_point() {
    Light l;
    RR_CHECK(l.type == LightType::Point);
    RR_CHECK(l.color     == Vec3(1, 1, 1));
    RR_CHECK(nearly_equal(l.intensity, 1.0f));
    RR_CHECK(l.position  == Vec3(0, 0, 0));
    RR_CHECK(l.direction == Vec3(0, -1, 0));
    RR_CHECK(nearly_equal(l.area_width,  0.0f));
    RR_CHECK(nearly_equal(l.area_height, 0.0f));
}

void test_make_point_light() {
    const auto l = make_point_light(Vec3{1, 2, 3}, Vec3{0.8f, 0.4f, 0.2f}, 5.0f);
    RR_CHECK(l.type == LightType::Point);
    RR_CHECK(l.position  == Vec3(1, 2, 3));
    RR_CHECK(l.color     == Vec3(0.8f, 0.4f, 0.2f));
    RR_CHECK(nearly_equal(l.intensity, 5.0f));
    // direction / area fields are unused for Point but should keep
    // their POD defaults so the upload remains well-defined.
    RR_CHECK(l.direction == Vec3(0, -1, 0));
    RR_CHECK(nearly_equal(l.area_width,  0.0f));
    RR_CHECK(nearly_equal(l.area_height, 0.0f));
}

void test_make_directional_light_normalizes_input() {
    // Non-unit input -> normalised in the factory so the kernel can
    // skip a per-thread normalize.
    const auto l = make_directional_light(Vec3{0, -2, 0}, Vec3{1, 1, 1}, 3.0f);
    RR_CHECK(l.type == LightType::Directional);
    RR_CHECK(nearly_equal(l.direction, Vec3{0, -1, 0}));
    RR_CHECK(nearly_equal(rr::math::length(l.direction), 1.0f));
    RR_CHECK(nearly_equal(l.intensity, 3.0f));
}

void test_make_directional_light_falls_back_for_zero_input() {
    const auto l = make_directional_light(Vec3{0, 0, 0}, Vec3{1, 1, 1}, 1.0f);
    // Degenerate input collapses to "down".
    RR_CHECK(l.type == LightType::Directional);
    RR_CHECK(nearly_equal(l.direction, Vec3{0, -1, 0}));
}

void test_make_area_light_placeholder() {
    const auto l = make_area_light(/*position=*/Vec3{0, 5, 0},
                                   /*normal=*/Vec3{0, -3, 0},
                                   /*width=*/2.0f,
                                   /*height=*/1.5f,
                                   /*color=*/Vec3{1, 1, 0.8f},
                                   /*intensity=*/10.0f);
    RR_CHECK(l.type == LightType::Area);
    RR_CHECK(l.position == Vec3(0, 5, 0));
    RR_CHECK(nearly_equal(l.direction, Vec3{0, -1, 0})); // normal normalized
    RR_CHECK(nearly_equal(l.area_width,  2.0f));
    RR_CHECK(nearly_equal(l.area_height, 1.5f));
    RR_CHECK(nearly_equal(l.intensity,   10.0f));
}

void test_area_light_clamps_negative_dimensions() {
    const auto l = make_area_light(Vec3{0, 0, 0}, Vec3{0, 1, 0},
                                   /*width=*/-1.0f,
                                   /*height=*/-2.0f,
                                   Vec3{1, 1, 1}, 1.0f);
    RR_CHECK(nearly_equal(l.area_width,  0.0f));
    RR_CHECK(nearly_equal(l.area_height, 0.0f));
}

void test_make_environment_light() {
    const auto l = make_environment_light(Vec3{0.4f, 0.6f, 0.9f}, 0.5f);
    RR_CHECK(l.type == LightType::Environment);
    RR_CHECK(l.color == Vec3(0.4f, 0.6f, 0.9f));
    RR_CHECK(nearly_equal(l.intensity, 0.5f));
}

}

int main() {
    test_light_type_enum_values();
    test_default_light_is_neutral_point();
    test_make_point_light();
    test_make_directional_light_normalizes_input();
    test_make_directional_light_falls_back_for_zero_input();
    test_make_area_light_placeholder();
    test_area_light_clamps_negative_dimensions();
    test_make_environment_light();

    std::printf("lighting_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

// Hand-rolled assertion runner. A real test framework (Catch2 / doctest)
// is on the M2 deferred list and will replace this file's plumbing once
// it lands; the assertions themselves stay.

#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Vec4.h"

#include <cmath>
#include <cstdio>

namespace {

int g_total  = 0;
int g_failed = 0;

float abs_f(float a) { return a < 0.0f ? -a : a; }

bool nearly_equal(float a, float b, float eps = 1.0e-5f) {
    const float scale = 1.0f > abs_f(a) ? 1.0f : abs_f(a);
    const float scale2 = scale > abs_f(b) ? scale : abs_f(b);
    return abs_f(a - b) <= eps * scale2;
}

bool nearly_equal(rr::math::Vec3 a, rr::math::Vec3 b, float eps = 1.0e-5f) {
    return nearly_equal(a.x, b.x, eps)
        && nearly_equal(a.y, b.y, eps)
        && nearly_equal(a.z, b.z, eps);
}

// Variadic so braced-init-lists like `Vec3{0, 0, 0}` inside the check
// expression don't get split by the preprocessor.
#define RR_CHECK(...)                                                         \
    do {                                                                      \
        ++g_total;                                                            \
        if (!(__VA_ARGS__)) {                                                 \
            ++g_failed;                                                       \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                        \
                         #__VA_ARGS__, __FILE__, __LINE__);                   \
        }                                                                     \
    } while (0)

void test_mathutils_scalar() {
    using rr::math::clamp;
    using rr::math::lerp;
    using rr::math::saturate;

    RR_CHECK(clamp(5,  0, 10) == 5);
    RR_CHECK(clamp(-1, 0, 10) == 0);
    RR_CHECK(clamp(11, 0, 10) == 10);
    RR_CHECK(nearly_equal(lerp(0.0f, 10.0f, 0.25f), 2.5f));
    RR_CHECK(nearly_equal(saturate(-3.0f), 0.0f));
    RR_CHECK(nearly_equal(saturate( 3.0f), 1.0f));
    RR_CHECK(nearly_equal(saturate( 0.5f), 0.5f));
}

void test_vec3_add_sub_scalar() {
    using rr::math::Vec3;
    const Vec3 a{1, 2, 3};
    const Vec3 b{4, 5, 6};

    RR_CHECK(a + b == Vec3(5, 7, 9));
    RR_CHECK(b - a == Vec3(3, 3, 3));
    RR_CHECK(-a    == Vec3(-1, -2, -3));
    RR_CHECK(a * 2.0f == Vec3(2, 4, 6));
    RR_CHECK(2.0f * a == Vec3(2, 4, 6));
    RR_CHECK(b / 2.0f == Vec3(2, 2.5f, 3));

    Vec3 c = a;
    c += b;
    RR_CHECK(c == Vec3(5, 7, 9));
    c -= b;
    RR_CHECK(c == a);
    c *= 3.0f;
    RR_CHECK(c == Vec3(3, 6, 9));
}

void test_vec3_dot_cross() {
    using rr::math::Vec3;
    using rr::math::cross;
    using rr::math::dot;

    RR_CHECK(dot(Vec3{1, 2, 3}, Vec3{4, -5, 6}) == 1 * 4 + 2 * (-5) + 3 * 6); // 12

    // Right-handed basis: x cross y = z, y cross z = x, z cross x = y.
    RR_CHECK(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == Vec3(0, 0, 1));
    RR_CHECK(cross(Vec3{0, 1, 0}, Vec3{0, 0, 1}) == Vec3(1, 0, 0));
    RR_CHECK(cross(Vec3{0, 0, 1}, Vec3{1, 0, 0}) == Vec3(0, 1, 0));
    // Anti-commutativity.
    RR_CHECK(cross(Vec3{1, 2, 3}, Vec3{4, 5, 6}) == -cross(Vec3{4, 5, 6}, Vec3{1, 2, 3}));
}

void test_vec3_length_normalize() {
    using rr::math::Vec3;
    using rr::math::length;
    using rr::math::length_squared;
    using rr::math::normalize;

    RR_CHECK(nearly_equal(length(Vec3{3, 4, 0}), 5.0f));
    RR_CHECK(nearly_equal(length_squared(Vec3{1, 2, 2}), 9.0f));

    const Vec3 n = normalize(Vec3{0, 0, 7});
    RR_CHECK(nearly_equal(n, Vec3{0, 0, 1}));
    RR_CHECK(nearly_equal(length(normalize(Vec3{3, -4, 12})), 1.0f));

    // Degenerate input returns zero, not NaN.
    RR_CHECK(normalize(Vec3{0, 0, 0}) == Vec3{0, 0, 0});
}

void test_vec3_clamp_lerp() {
    using rr::math::Vec3;
    using rr::math::clamp;
    using rr::math::lerp;

    RR_CHECK(clamp(Vec3{-1, 0.5f, 2}, 0.0f, 1.0f) == Vec3(0, 0.5f, 1));
    RR_CHECK(clamp(Vec3{-5, 5, 5}, Vec3{0, 0, 0}, Vec3{1, 10, 3}) == Vec3(0, 5, 3));

    RR_CHECK(nearly_equal(lerp(Vec3{0, 0, 0}, Vec3{2, 4, 8}, 0.0f), Vec3{0, 0, 0}));
    RR_CHECK(nearly_equal(lerp(Vec3{0, 0, 0}, Vec3{2, 4, 8}, 1.0f), Vec3{2, 4, 8}));
    RR_CHECK(nearly_equal(lerp(Vec3{0, 0, 0}, Vec3{2, 4, 8}, 0.5f), Vec3{1, 2, 4}));
}

void test_mat4_identity() {
    using rr::math::Mat4;
    using rr::math::Vec3;
    using rr::math::transform_point;
    using rr::math::transform_vector;

    const Mat4 I = Mat4::identity();
    RR_CHECK(transform_point(I,  Vec3{1, 2, 3}) == Vec3(1, 2, 3));
    RR_CHECK(transform_vector(I, Vec3{1, 2, 3}) == Vec3(1, 2, 3));
    RR_CHECK((I * I).m[0][0] == 1.0f);
    RR_CHECK((I * I).m[1][1] == 1.0f);
}

void test_mat4_translation() {
    using rr::math::Mat4;
    using rr::math::Vec3;
    using rr::math::transform_point;
    using rr::math::transform_vector;

    const Mat4 T = Mat4::translation(Vec3{10, 20, 30});

    // Points pick up the translation.
    RR_CHECK(transform_point(T, Vec3{1, 2, 3}) == Vec3(11, 22, 33));
    // Vectors do not.
    RR_CHECK(transform_vector(T, Vec3{1, 2, 3}) == Vec3(1, 2, 3));
}

void test_mat4_scale() {
    using rr::math::Mat4;
    using rr::math::Vec3;
    using rr::math::transform_point;
    using rr::math::transform_vector;

    const Mat4 S = Mat4::scale(Vec3{2, 3, 4});
    RR_CHECK(transform_point(S,  Vec3{1, 1, 1}) == Vec3(2, 3, 4));
    RR_CHECK(transform_vector(S, Vec3{1, 1, 1}) == Vec3(2, 3, 4));
}

void test_mat4_multiply() {
    using rr::math::Mat4;
    using rr::math::Vec3;
    using rr::math::transform_point;

    const Mat4 T  = Mat4::translation(Vec3{1, 2, 3});
    const Mat4 S  = Mat4::scale(Vec3{2, 2, 2});
    const Mat4 TS = T * S;   // applied to p: scale first, then translate.

    RR_CHECK(transform_point(TS, Vec3{1, 1, 1}) == Vec3(3, 4, 5));
    RR_CHECK(transform_point(TS, Vec3{0, 0, 0}) == Vec3(1, 2, 3));

    // Non-commutativity: ST != TS.
    const Mat4 ST = S * T;
    RR_CHECK(transform_point(ST, Vec3{1, 1, 1}) == Vec3(4, 6, 8));
}

}

int main() {
    test_mathutils_scalar();
    test_vec3_add_sub_scalar();
    test_vec3_dot_cross();
    test_vec3_length_normalize();
    test_vec3_clamp_lerp();
    test_mat4_identity();
    test_mat4_translation();
    test_mat4_scale();
    test_mat4_multiply();

    std::printf("math_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

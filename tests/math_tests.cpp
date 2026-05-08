// Stage 2 math-library tests.
//
// No test framework is in the project yet, so this file is a simple
// stand-alone executable wired to ctest. Hand-rolled assertions with a
// `RR_CHECK` macro; main() returns 0 when every check passed and 1
// otherwise.
//
// Coverage matches the Stage-2 prompt:
//   Vec3 must support: construction, +, -, scalar */, dot, cross,
//                      length, normalize, clamp, lerp
//   Mat4 must support: identity, translation, scale, multiplication,
//                      transform_point, transform_vector
// Vec2 and Vec4 get smoke tests so their basic surface is covered.

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

bool approx(rr::math::Vec3 a, rr::math::Vec3 b, float eps = kEps) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) && approx(a.z, b.z, eps);
}

// ---------- MathUtils ----------

void test_math_utils() {
    using namespace rr::math;

    RR_CHECK(min(2, 5)        == 2);
    RR_CHECK(max(2, 5)        == 5);
    RR_CHECK(clamp(3,  0, 10) == 3);
    RR_CHECK(clamp(-1, 0, 10) == 0);
    RR_CHECK(clamp(11, 0, 10) == 10);

    RR_CHECK(approx(saturate(-0.5f), 0.0f));
    RR_CHECK(approx(saturate( 0.5f), 0.5f));
    RR_CHECK(approx(saturate( 1.5f), 1.0f));

    RR_CHECK(approx(radians(180.0f), kPi));
    RR_CHECK(approx(degrees(kPi),    180.0f));

    RR_CHECK(approx(lerp(0.0f, 10.0f, 0.0f),  0.0f));
    RR_CHECK(approx(lerp(0.0f, 10.0f, 0.5f),  5.0f));
    RR_CHECK(approx(lerp(0.0f, 10.0f, 1.0f), 10.0f));
}

// ---------- Vec2 ----------

void test_vec2() {
    using rr::math::Vec2;

    Vec2 a{1.0f, 2.0f};
    Vec2 b{3.0f, 4.0f};
    RR_CHECK((a + b) == Vec2{4.0f, 6.0f});
    RR_CHECK((a - b) == Vec2{-2.0f, -2.0f});
    RR_CHECK((a * 2.0f) == Vec2{2.0f, 4.0f});
    RR_CHECK(2.0f * a   == Vec2{2.0f, 4.0f});
    RR_CHECK(approx(dot(a, b), 11.0f));
}

// ---------- Vec3 ----------

void test_vec3_construction() {
    using rr::math::Vec3;

    Vec3 def;
    RR_CHECK(def.x == 0.0f && def.y == 0.0f && def.z == 0.0f);

    Vec3 v3{1.0f, 2.0f, 3.0f};
    RR_CHECK(v3.x == 1.0f && v3.y == 2.0f && v3.z == 3.0f);

    Vec3 splat{4.0f};
    RR_CHECK(splat.x == 4.0f && splat.y == 4.0f && splat.z == 4.0f);
}

void test_vec3_arithmetic() {
    using rr::math::Vec3;

    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{4.0f, 5.0f, 6.0f};

    RR_CHECK(approx(a + b, Vec3{5.0f, 7.0f, 9.0f}));
    RR_CHECK(approx(a - b, Vec3{-3.0f, -3.0f, -3.0f}));
    RR_CHECK(approx(-a,    Vec3{-1.0f, -2.0f, -3.0f}));
    RR_CHECK(approx(a * 2.0f, Vec3{2.0f, 4.0f, 6.0f}));
    RR_CHECK(approx(2.0f * a, Vec3{2.0f, 4.0f, 6.0f}));
    RR_CHECK(approx(b / 2.0f, Vec3{2.0f, 2.5f, 3.0f}));

    Vec3 c{1.0f, 2.0f, 3.0f};
    c += b;
    RR_CHECK(approx(c, Vec3{5.0f, 7.0f, 9.0f}));
    c -= b;
    RR_CHECK(approx(c, Vec3{1.0f, 2.0f, 3.0f}));
    c *= 2.0f;
    RR_CHECK(approx(c, Vec3{2.0f, 4.0f, 6.0f}));
    c /= 2.0f;
    RR_CHECK(approx(c, Vec3{1.0f, 2.0f, 3.0f}));
}

void test_vec3_dot_cross_length_normalize() {
    using rr::math::Vec3;

    RR_CHECK(approx(dot(Vec3{1.0f, 2.0f, 3.0f}, Vec3{4.0f, -5.0f, 6.0f}), 12.0f));

    // cross(x, y) == z; cross(y, z) == x; cross(z, x) == y.
    RR_CHECK(approx(cross(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}),
                    Vec3{0.0f, 0.0f, 1.0f}));
    RR_CHECK(approx(cross(Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}),
                    Vec3{1.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(cross(Vec3{0.0f, 0.0f, 1.0f}, Vec3{1.0f, 0.0f, 0.0f}),
                    Vec3{0.0f, 1.0f, 0.0f}));

    RR_CHECK(approx(length(Vec3{3.0f, 4.0f, 0.0f}), 5.0f));
    RR_CHECK(approx(length_squared(Vec3{2.0f, 3.0f, 4.0f}), 29.0f));

    const Vec3 n = normalize(Vec3{0.0f, 5.0f, 0.0f});
    RR_CHECK(approx(n, Vec3{0.0f, 1.0f, 0.0f}));
    RR_CHECK(approx(length(n), 1.0f));

    // Degenerate input: documented to return zero, not NaN.
    RR_CHECK(approx(normalize(Vec3{0.0f, 0.0f, 0.0f}), Vec3{0.0f, 0.0f, 0.0f}));
}

void test_vec3_clamp_lerp() {
    using rr::math::Vec3;
    using rr::math::clamp;
    using rr::math::lerp;

    // Scalar bounds overload.
    RR_CHECK(approx(clamp(Vec3{-1.0f, 0.5f, 2.0f}, 0.0f, 1.0f),
                    Vec3{0.0f, 0.5f, 1.0f}));
    // Vec3 bounds overload.
    RR_CHECK(approx(clamp(Vec3{5.0f, 5.0f, 5.0f},
                          Vec3{0.0f, 0.0f, 0.0f},
                          Vec3{4.0f, 5.0f, 6.0f}),
                    Vec3{4.0f, 5.0f, 5.0f}));

    RR_CHECK(approx(lerp(Vec3{0.0f, 0.0f, 0.0f}, Vec3{10.0f, 20.0f, 30.0f}, 0.0f),
                    Vec3{0.0f, 0.0f, 0.0f}));
    RR_CHECK(approx(lerp(Vec3{0.0f, 0.0f, 0.0f}, Vec3{10.0f, 20.0f, 30.0f}, 0.5f),
                    Vec3{5.0f, 10.0f, 15.0f}));
    RR_CHECK(approx(lerp(Vec3{0.0f, 0.0f, 0.0f}, Vec3{10.0f, 20.0f, 30.0f}, 1.0f),
                    Vec3{10.0f, 20.0f, 30.0f}));
}

// ---------- Vec4 ----------

void test_vec4() {
    using rr::math::Vec3;
    using rr::math::Vec4;

    const Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};

    RR_CHECK(a + b == Vec4{6.0f, 8.0f, 10.0f, 12.0f});
    RR_CHECK(a * 2.0f == Vec4{2.0f, 4.0f, 6.0f, 8.0f});
    RR_CHECK(approx(dot(a, b), 1.0f * 5.0f + 2.0f * 6.0f + 3.0f * 7.0f + 4.0f * 8.0f));

    const Vec4 from_v3{Vec3{1.0f, 2.0f, 3.0f}, 4.0f};
    RR_CHECK(from_v3 == a);
    RR_CHECK(approx(from_v3.xyz(), Vec3{1.0f, 2.0f, 3.0f}));
}

// ---------- Mat4 ----------

void test_mat4_identity() {
    using rr::math::Mat4;
    using rr::math::Vec3;

    const Mat4 I = Mat4::identity();
    const Vec3 p{2.0f, -3.0f, 5.0f};
    RR_CHECK(approx(transform_point(I,  p), p));
    RR_CHECK(approx(transform_vector(I, p), p));

    const Mat4 II = I * I;
    RR_CHECK(approx(transform_point(II, p), p));
}

void test_mat4_translation() {
    using rr::math::Mat4;
    using rr::math::Vec3;

    const Mat4 T = Mat4::translation(Vec3{10.0f, 20.0f, 30.0f});
    RR_CHECK(approx(transform_point(T,  Vec3{1.0f, 2.0f, 3.0f}),
                    Vec3{11.0f, 22.0f, 33.0f}));

    // Direction must be unaffected by translation.
    RR_CHECK(approx(transform_vector(T, Vec3{1.0f, 0.0f, 0.0f}),
                    Vec3{1.0f, 0.0f, 0.0f}));
}

void test_mat4_scale() {
    using rr::math::Mat4;
    using rr::math::Vec3;

    const Mat4 S = Mat4::scale(Vec3{2.0f, 3.0f, 4.0f});
    RR_CHECK(approx(transform_point(S,  Vec3{1.0f, 1.0f, 1.0f}),
                    Vec3{2.0f, 3.0f, 4.0f}));
    RR_CHECK(approx(transform_vector(S, Vec3{1.0f, 1.0f, 1.0f}),
                    Vec3{2.0f, 3.0f, 4.0f}));
}

void test_mat4_composition() {
    using rr::math::Mat4;
    using rr::math::Vec3;

    // M = T * S; applied to a point: scale first, then translate.
    // p' = M * p = T * (S * p)
    const Mat4 T = Mat4::translation(Vec3{10.0f, 20.0f, 30.0f});
    const Mat4 S = Mat4::scale(Vec3{2.0f, 3.0f, 4.0f});
    const Mat4 M = T * S;

    const Vec3 p{1.0f, 1.0f, 1.0f};
    const Vec3 expected_point  = Vec3{12.0f, 23.0f, 34.0f};
    const Vec3 expected_vector = Vec3{ 2.0f,  3.0f,  4.0f};   // T contributes 0 to vectors
    RR_CHECK(approx(transform_point(M,  p), expected_point));
    RR_CHECK(approx(transform_vector(M, p), expected_vector));

    // Order matters: S * T should differ from T * S for a non-origin point.
    const Mat4 ST = S * T;
    const Vec3 from_TS = transform_point(M,  p);
    const Vec3 from_ST = transform_point(ST, p);
    RR_CHECK(!approx(from_TS, from_ST));
}

}  // namespace

int main() {
    test_math_utils();
    test_vec2();
    test_vec3_construction();
    test_vec3_arithmetic();
    test_vec3_dot_cross_length_normalize();
    test_vec3_clamp_lerp();
    test_vec4();
    test_mat4_identity();
    test_mat4_translation();
    test_mat4_scale();
    test_mat4_composition();

    std::printf("math_tests: %d / %d passed\n",
                g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

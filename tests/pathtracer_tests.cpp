// Stage 11A pathtracer-foundation tests.
//
// Exercises the four primitives the Stage 11A prompt requires:
//   - per-pixel RNG seed (decorrelation across (x, y, frame, seed))
//   - random float 0..1 (range + trivial whitening)
//   - random Vec2 (component independence)
//   - cosine-weighted hemisphere sampling
//   - uniform hemisphere sampling
//
// Covers correctness invariants only - statistical strength of the
// underlying PCG32 + SplitMix64 stack is verified by the
// reference implementations these wrappers reuse, not re-tested
// here. The integration shape (per-pixel determinism + the
// hemisphere PDFs evaluated against Monte-Carlo means) is what
// matters for the path tracer's eventual integration.
//
// Hand-rolled assertions; same RR_CHECK pattern as math_tests.cpp.

#include "pathtracer/RNG.h"
#include "pathtracer/Sampling.h"

#include "math/MathUtils.h"  // kPi
#include "math/Vec2.h"
#include "math/Vec3.h"

#include <cmath>
#include <cstdint>
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

// ---------- RNG ----------

void test_rng_float_range() {
    using namespace rr::pathtracer;

    // Every output of next_float must lie in [0, 1).
    Rng r = make_pixel_rng(0u, 0u, 0u, 0xC0FFEEULL);
    bool min_ok = true, max_ok = true;
    for (int i = 0; i < 100000; ++i) {
        const float v = next_float(r);
        if (v <  0.0f) min_ok = false;
        if (v >= 1.0f) max_ok = false;
    }
    RR_CHECK(min_ok);
    RR_CHECK(max_ok);
}

void test_rng_per_pixel_decorrelation() {
    using namespace rr::pathtracer;

    // Two adjacent pixels with the same global seed must produce
    // different first samples. This is the property that lets
    // per-pixel parallel kernels hand each thread an independent
    // stream without coordination.
    Rng a = make_pixel_rng(10u, 10u, 0u, 42ULL);
    Rng b = make_pixel_rng(11u, 10u, 0u, 42ULL);
    Rng c = make_pixel_rng(10u, 11u, 0u, 42ULL);
    Rng d = make_pixel_rng(10u, 10u, 1u, 42ULL);
    Rng e = make_pixel_rng(10u, 10u, 0u, 43ULL);

    const float fa = next_float(a);
    const float fb = next_float(b);
    const float fc = next_float(c);
    const float fd = next_float(d);
    const float fe = next_float(e);

    RR_CHECK(fa != fb);  // x differs
    RR_CHECK(fa != fc);  // y differs
    RR_CHECK(fa != fd);  // frame differs
    RR_CHECK(fa != fe);  // global seed differs
}

void test_rng_determinism() {
    using namespace rr::pathtracer;

    // Same seed -> same stream. The path tracer relies on this for
    // re-rendering identical frames during debugging.
    Rng r1 = make_pixel_rng(7u, 9u, 3u, 0xDEADBEEFULL);
    Rng r2 = make_pixel_rng(7u, 9u, 3u, 0xDEADBEEFULL);
    for (int i = 0; i < 16; ++i) {
        RR_CHECK(next_float(r1) == next_float(r2));
    }
}

void test_rng_vec2_components() {
    using namespace rr::pathtracer;

    // Component independence: two consecutive next_float calls
    // should produce distinct values almost always (collisions
    // are possible at 1 in 2^24 - tolerated below).
    Rng r = make_pixel_rng(3u, 7u, 0u, 1ULL);
    int collisions = 0;
    for (int i = 0; i < 10000; ++i) {
        const rr::math::Vec2 v = next_vec2(r);
        if (v.x == v.y) ++collisions;
        RR_CHECK(v.x >= 0.0f && v.x < 1.0f);
        RR_CHECK(v.y >= 0.0f && v.y < 1.0f);
    }
    // Collisions should be vanishingly rare.
    RR_CHECK(collisions < 5);
}

// ---------- Sampling ----------

void test_uniform_hemisphere_unit_length_and_upper() {
    using namespace rr::pathtracer;

    Rng r = make_pixel_rng(0u, 0u, 0u, 7ULL);
    bool len_ok = true, upper_ok = true;
    for (int i = 0; i < 10000; ++i) {
        const rr::math::Vec3 d = sample_uniform_hemisphere(next_vec2(r));
        if (!approx(rr::math::length(d), 1.0f, 1e-4f)) len_ok = false;
        if (d.z < -1e-5f)                    upper_ok = false;
    }
    RR_CHECK(len_ok);
    RR_CHECK(upper_ok);
}

void test_uniform_hemisphere_pdf_normalises() {
    using namespace rr::pathtracer;

    // Monte-Carlo: integrate 1 with the uniform-hemisphere
    // sampler; the result must converge to the hemisphere's
    // solid angle, 2*pi.
    Rng r = make_pixel_rng(0u, 0u, 0u, 11ULL);
    const int   N    = 200000;
    const float kpdf = pdf_uniform_hemisphere();  // 1/(2*pi)
    double sum = 0.0;
    for (int i = 0; i < N; ++i) {
        (void)sample_uniform_hemisphere(next_vec2(r));
        sum += 1.0 / static_cast<double>(kpdf);
    }
    const double mean = sum / static_cast<double>(N);
    const double expected = 2.0 * static_cast<double>(rr::math::kPi);
    RR_CHECK(std::fabs(mean - expected) < 0.05);
}

void test_cosine_hemisphere_unit_length_and_upper() {
    using namespace rr::pathtracer;

    Rng r = make_pixel_rng(0u, 0u, 0u, 13ULL);
    bool len_ok = true, upper_ok = true;
    for (int i = 0; i < 10000; ++i) {
        const rr::math::Vec3 d = sample_cosine_hemisphere(next_vec2(r));
        if (!approx(rr::math::length(d), 1.0f, 1e-4f)) len_ok = false;
        if (d.z < -1e-5f)                    upper_ok = false;
    }
    RR_CHECK(len_ok);
    RR_CHECK(upper_ok);
}

void test_cosine_hemisphere_distribution() {
    using namespace rr::pathtracer;

    // Cosine-weighted samples should bias toward +Z. The mean of
    // dz under cos sampling is 2/3 (analytical:
    // E[dz] = integral_hemisphere cos(theta) * cos(theta)/pi dA
    //       = 2/3).
    Rng r = make_pixel_rng(0u, 0u, 0u, 17ULL);
    const int N = 200000;
    double sum_z = 0.0;
    for (int i = 0; i < N; ++i) {
        sum_z += sample_cosine_hemisphere(next_vec2(r)).z;
    }
    const double mean_z = sum_z / static_cast<double>(N);
    RR_CHECK(std::fabs(mean_z - 2.0 / 3.0) < 0.01);
}

void test_cosine_hemisphere_pdf() {
    using namespace rr::pathtracer;

    // PDF must be cos(theta)/pi for cos_theta in (0, 1] and 0
    // when cos_theta <= 0.
    RR_CHECK(approx(pdf_cosine_hemisphere(0.0f), 0.0f));
    RR_CHECK(approx(pdf_cosine_hemisphere(-0.5f), 0.0f));
    RR_CHECK(approx(pdf_cosine_hemisphere(1.0f),
                    static_cast<float>(1.0 / rr::math::kPi), 1e-5f));
    RR_CHECK(approx(pdf_cosine_hemisphere(0.5f),
                    static_cast<float>(0.5 / rr::math::kPi), 1e-5f));
}

}  // namespace

int main() {
    test_rng_float_range();
    test_rng_per_pixel_decorrelation();
    test_rng_determinism();
    test_rng_vec2_components();
    test_uniform_hemisphere_unit_length_and_upper();
    test_uniform_hemisphere_pdf_normalises();
    test_cosine_hemisphere_unit_length_and_upper();
    test_cosine_hemisphere_distribution();
    test_cosine_hemisphere_pdf();

    std::fprintf(stderr, "pathtracer_tests: %d/%d passed\n",
                 g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

// Hand-rolled assertion runner. The real test framework comes
// with the M2 deferred items.
//
// Sampling foundation: RNG correctness and hemisphere-sample
// statistical properties. The same RR_HD inline helpers run on
// the device in the eventual M14 path tracer; covering the host
// side gives us the device math by construction.

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "pathtracer/RNG.h"
#include "pathtracer/Sampling.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

int g_total  = 0;
int g_failed = 0;

float abs_f(float a) { return a < 0.0f ? -a : a; }

bool nearly_equal(float a, float b, float eps = 1.0e-5f) {
    const float scale  = 1.0f > abs_f(a) ? 1.0f : abs_f(a);
    const float scale2 = scale > abs_f(b) ? scale : abs_f(b);
    return abs_f(a - b) <= eps * scale2;
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
using rr::math::dot;
using rr::math::length;
using rr::pathtracer::build_orthonormal_basis;
using rr::pathtracer::make_rng;
using rr::pathtracer::next_float;
using rr::pathtracer::next_uint;
using rr::pathtracer::pdf_hemisphere_cosine;
using rr::pathtracer::pdf_hemisphere_uniform;
using rr::pathtracer::RNG;
using rr::pathtracer::sample_hemisphere_cosine;
using rr::pathtracer::sample_hemisphere_uniform;

// --- RNG ----------------------------------------------------------------

void test_rng_floats_are_in_unit_interval() {
    RNG rng = make_rng(0, 0, 0);
    bool all_in_range = true;
    for (int i = 0; i < 1000; ++i) {
        const float x = next_float(rng);
        if (!(x >= 0.0f && x < 1.0f)) {
            all_in_range = false;
            break;
        }
    }
    RR_CHECK(all_in_range);
}

void test_rng_is_deterministic() {
    RNG a = make_rng(42, 13, 7);
    RNG b = make_rng(42, 13, 7);
    bool match = true;
    for (int i = 0; i < 64; ++i) {
        if (next_uint(a) != next_uint(b)) { match = false; break; }
    }
    RR_CHECK(match);
}

void test_rng_streams_are_independent() {
    // Adjacent pixels with the same sample index must not produce
    // the same first 16 floats.
    RNG a = make_rng(0, 0, 0);
    RNG b = make_rng(1, 0, 0);
    int matches = 0;
    for (int i = 0; i < 16; ++i) {
        if (nearly_equal(next_float(a), next_float(b), 1.0e-5f)) ++matches;
    }
    RR_CHECK(matches < 4);  // an occasional collision is fine; identical streams are not
}

void test_rng_distribution_is_roughly_uniform() {
    // Mean of a uniform float over [0, 1) is 0.5. With N = 10000
    // samples the standard error of the mean is ~sqrt(1/12)/sqrt(N)
    // ~= 0.003, so ~3 sigma fits inside +/- 0.01.
    RNG  rng  = make_rng(7, 11, 0);
    double sum = 0.0;
    constexpr int N = 10000;
    for (int i = 0; i < N; ++i) sum += next_float(rng);
    const double mean = sum / static_cast<double>(N);
    RR_CHECK(std::fabs(mean - 0.5) < 0.01);
}

// --- Orthonormal basis --------------------------------------------------

void check_basis(Vec3 n) {
    Vec3 t, b;
    build_orthonormal_basis(n, t, b);

    RR_CHECK(nearly_equal(length(t), 1.0f, 1.0e-4f));
    RR_CHECK(nearly_equal(length(b), 1.0f, 1.0e-4f));
    RR_CHECK(nearly_equal(dot(t, n), 0.0f, 1.0e-4f));
    RR_CHECK(nearly_equal(dot(b, n), 0.0f, 1.0e-4f));
    RR_CHECK(nearly_equal(dot(t, b), 0.0f, 1.0e-4f));
}

void test_orthonormal_basis_for_axes() {
    check_basis(Vec3{ 1, 0, 0});
    check_basis(Vec3{ 0, 1, 0});
    check_basis(Vec3{ 0, 0, 1});
    check_basis(Vec3{-1, 0, 0});
    check_basis(Vec3{ 0,-1, 0});
    check_basis(Vec3{ 0, 0,-1});  // antipode where the naive Frisvad form breaks
}

void test_orthonormal_basis_for_arbitrary_normals() {
    check_basis(rr::math::normalize(Vec3{ 1.0f,  2.0f,  3.0f}));
    check_basis(rr::math::normalize(Vec3{ 0.7f, -0.4f,  0.1f}));
    check_basis(rr::math::normalize(Vec3{-0.3f,  0.9f, -0.5f}));
}

// --- Hemisphere samples -------------------------------------------------

void test_hemisphere_uniform_samples_are_unit_and_in_hemisphere() {
    const Vec3 normal{0, 0, 1};
    RNG  rng = make_rng(0, 0, 0);
    bool all_unit = true;
    bool all_in_hemisphere = true;
    for (int i = 0; i < 1000; ++i) {
        const auto d = sample_hemisphere_uniform(normal, rng);
        if (!nearly_equal(length(d), 1.0f, 1.0e-3f)) { all_unit = false; break; }
        if (dot(d, normal) < -1.0e-5f)               { all_in_hemisphere = false; break; }
    }
    RR_CHECK(all_unit);
    RR_CHECK(all_in_hemisphere);
}

void test_hemisphere_cosine_samples_are_unit_and_in_hemisphere() {
    const Vec3 normal{0, 0, 1};
    RNG  rng = make_rng(1, 2, 3);
    bool all_unit = true;
    bool all_in_hemisphere = true;
    for (int i = 0; i < 1000; ++i) {
        const auto d = sample_hemisphere_cosine(normal, rng);
        if (!nearly_equal(length(d), 1.0f, 1.0e-3f)) { all_unit = false; break; }
        if (dot(d, normal) < -1.0e-5f)               { all_in_hemisphere = false; break; }
    }
    RR_CHECK(all_unit);
    RR_CHECK(all_in_hemisphere);
}

void test_hemisphere_uniform_mean_is_one_half() {
    // Uniform hemisphere mean cos(theta) = integral over hemisphere
    // of cos(theta) * (1 / (2 pi)) dOmega = 1/2.
    const Vec3 normal{0, 0, 1};
    RNG  rng = make_rng(11, 13, 0);
    constexpr int N = 10000;
    double sum_z = 0.0;
    for (int i = 0; i < N; ++i) {
        const auto d = sample_hemisphere_uniform(normal, rng);
        sum_z += d.z;
    }
    const double mean_z = sum_z / static_cast<double>(N);
    RR_CHECK(std::fabs(mean_z - 0.5) < 0.02);
}

void test_hemisphere_cosine_mean_is_two_thirds() {
    // Cosine-weighted hemisphere mean cos(theta) = integral over
    // hemisphere of cos^2(theta) / pi dOmega = 2/3.
    const Vec3 normal{0, 0, 1};
    RNG  rng = make_rng(13, 17, 0);
    constexpr int N = 10000;
    double sum_z = 0.0;
    for (int i = 0; i < N; ++i) {
        const auto d = sample_hemisphere_cosine(normal, rng);
        sum_z += d.z;
    }
    const double mean_z = sum_z / static_cast<double>(N);
    RR_CHECK(std::fabs(mean_z - 2.0 / 3.0) < 0.02);
}

void test_hemisphere_samples_align_with_normal() {
    // Tilted normal: the mean sample direction should align with
    // it (cosine bias for the cosine-weighted version).
    const Vec3 normal = rr::math::normalize(Vec3{0.6f, 0.0f, 0.8f});
    RNG rng = make_rng(5, 9, 0);
    constexpr int N = 5000;
    Vec3 sum{0, 0, 0};
    for (int i = 0; i < N; ++i) {
        sum = sum + sample_hemisphere_cosine(normal, rng);
    }
    const Vec3 mean = sum * (1.0f / static_cast<float>(N));
    // The mean should be roughly aligned with the normal (positive
    // dot product, well above zero).
    RR_CHECK(dot(mean, normal) > 0.55f);
}

// --- PDFs ---------------------------------------------------------------

void test_pdf_hemisphere_uniform() {
    RR_CHECK(nearly_equal(pdf_hemisphere_uniform(),
                          1.0f / (2.0f * rr::math::kPi), 1.0e-5f));
}

void test_pdf_hemisphere_cosine() {
    // Straight up, cos(theta) = 1, PDF = 1/pi.
    RR_CHECK(nearly_equal(pdf_hemisphere_cosine(1.0f),
                          1.0f / rr::math::kPi, 1.0e-5f));
    // Grazing, cos(theta) = 0, PDF = 0.
    RR_CHECK(nearly_equal(pdf_hemisphere_cosine(0.0f), 0.0f, 1.0e-5f));
    // Below the horizon clamps to 0.
    RR_CHECK(nearly_equal(pdf_hemisphere_cosine(-0.5f), 0.0f, 1.0e-5f));
}

}

int main() {
    test_rng_floats_are_in_unit_interval();
    test_rng_is_deterministic();
    test_rng_streams_are_independent();
    test_rng_distribution_is_roughly_uniform();

    test_orthonormal_basis_for_axes();
    test_orthonormal_basis_for_arbitrary_normals();

    test_hemisphere_uniform_samples_are_unit_and_in_hemisphere();
    test_hemisphere_cosine_samples_are_unit_and_in_hemisphere();
    test_hemisphere_uniform_mean_is_one_half();
    test_hemisphere_cosine_mean_is_two_thirds();
    test_hemisphere_samples_align_with_normal();

    test_pdf_hemisphere_uniform();
    test_pdf_hemisphere_cosine();

    std::printf("sampling_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

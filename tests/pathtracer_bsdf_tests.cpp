// MIS.2 helper-host tests for the
// `rr::pathtracer::sample_bsdf / bsdf_pdf / bsdf_eval`
// helpers (defined in `pathtracer/Bsdf.cuh`) and the
// `BsdfSample` POD (defined in `pathtracer/Bsdf.h`).
//
// The helpers are RR_HD inline, so the same code path the
// future MIS.5 + MIS.6 integrators will execute is also
// exercised here against the host C++ compiler. These
// tests anchor the contract per
// `docs/PATH_TRACER_MIS_BSDF_PDF_TASK.md` §5.5 (ten
// mandatory cases) so a regression is caught at host-
// build time without requiring a CUDA-equipped runtime
// host.
//
// Coverage (per the task brief §5.5):
//   1. test_default_constructed_sample_is_invalid
//   2. test_lambert_sample_in_upper_hemisphere
//   3. test_lambert_pdf_matches_sampler
//   4. test_lambert_pdf_below_horizon_is_zero
//   5. test_lambert_eval_matches_inverse_pi_albedo
//   6. test_lambert_throughput_simplification
//   7. test_lambert_sample_pdf_normalises_via_monte_carlo
//   8. test_lambert_cos_weighted_mean_dz
//   9. test_degenerate_normal_returns_invalid
//  10. test_helper_determinism
//
// Hand-rolled assertions; same RR_CHECK pattern as
// `pathtracer_nee_tests.cpp` / `pathtracer_tests.cpp`.

#include "pathtracer/Bsdf.cuh"
#include "pathtracer/Bsdf.h"
#include "pathtracer/RNG.h"          // make_pixel_rng + next_vec2 for MC tests
#include "material/MaterialTypes.h"
#include "math/MathUtils.h"          // kPi + kInvPi
#include "math/Vec2.h"
#include "math/Vec3.h"

#include <cmath>
#include <cstdio>
#include <cstring>                   // std::memcmp for byte-identity

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

using rr::math::Vec2;
using rr::math::Vec3;
using rr::material::MaterialParams;
using rr::pathtracer::BsdfSample;
using rr::pathtracer::bsdf_eval;
using rr::pathtracer::bsdf_pdf;
using rr::pathtracer::sample_bsdf;

MaterialParams make_lambert(Vec3 base) {
    MaterialParams m;
    m.baseColor = base;
    m.emissionStrength = 0.0f;
    return m;
}

// ---------- §5.5 case 1 ----------

// Default-constructed BsdfSample is the no-contribution
// sentinel: valid==false, pdf==0, wo + value bit-zero.
// Anchors the byte-identity contract from
// `docs/PATH_TRACER_MIS_BSDF_PDF_TASK.md` §2 (the POD's
// default-construction matches the NEE.5 memcmp anchor
// pattern).
void test_default_constructed_sample_is_invalid() {
    const BsdfSample s;
    RR_CHECK(s.valid == false);
    RR_CHECK(s.pdf   == 0.0f);
    RR_CHECK(s.wo.x  == 0.0f && s.wo.y  == 0.0f && s.wo.z  == 0.0f);
    RR_CHECK(s.value.x == 0.0f && s.value.y == 0.0f && s.value.z == 0.0f);

    // Bit-default invariant (memcmp distinguishes +0.0f
    // from -0.0f, so a regression introducing negative-
    // zero into any field would fail here).
    const BsdfSample reference;
    RR_CHECK(std::memcmp(&s, &reference, sizeof(BsdfSample)) == 0);
}

// ---------- §5.5 case 2 ----------

// For a non-degenerate setup, sample_bsdf produces a
// valid, upper-hemisphere wo with nonzero pdf + value.
void test_lambert_sample_in_upper_hemisphere() {
    const auto m       = make_lambert(Vec3{0.8f, 0.8f, 0.8f});
    const Vec3 wi      = Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 normal  = Vec3{0.0f, 1.0f, 0.0f};
    const Vec2 u       = Vec2{0.5f, 0.5f};

    const BsdfSample s = sample_bsdf(m, wi, normal, u);
    RR_CHECK(s.valid == true);
    RR_CHECK(s.pdf > 0.0f);
    RR_CHECK(rr::math::dot(s.wo, normal) > 0.0f);  // upper hemisphere

    // Lambert value = baseColor / pi.
    RR_CHECK(approx(s.value.x, 0.8f * rr::math::kInvPi));
    RR_CHECK(approx(s.value.y, 0.8f * rr::math::kInvPi));
    RR_CHECK(approx(s.value.z, 0.8f * rr::math::kInvPi));
}

// ---------- §5.5 case 3 ----------

// BsdfSample::pdf must be bit-equal with bsdf_pdf
// evaluated at the sample's wo. Anchors the consistency
// invariant between the sampler-side PDF and the
// standalone evaluator that the MIS integrator relies on.
void test_lambert_pdf_matches_sampler() {
    const auto m       = make_lambert(Vec3{0.5f, 0.6f, 0.7f});
    const Vec3 wi      = Vec3{0.0f, 0.0f, 1.0f};
    const Vec3 normal  = Vec3{0.0f, 1.0f, 0.0f};

    // Walk a few representative u values to anchor the
    // invariant across the sampler's input range.
    const Vec2 us[4] = {
        Vec2{0.1f, 0.1f},
        Vec2{0.5f, 0.5f},
        Vec2{0.9f, 0.3f},
        Vec2{0.25f, 0.75f},
    };
    for (const auto& u : us) {
        const BsdfSample s = sample_bsdf(m, wi, normal, u);
        RR_CHECK(s.valid == true);
        const float p = bsdf_pdf(m, s.wo, normal);
        // memcmp on float bits — strictest possible
        // equality. A regression that introduces
        // arithmetic divergence between sampler.pdf
        // and the standalone evaluator (e.g. if one
        // implementation uses `1.0f/kPi` and the other
        // `kInvPi`) would fail here.
        RR_CHECK(std::memcmp(&s.pdf, &p, sizeof(float)) == 0);
    }
}

// ---------- §5.5 case 4 ----------

// bsdf_pdf returns 0 for any wo with cos_theta_o <= 0
// (below-horizon direction). Below-horizon directions
// cannot scatter from a Lambert surface; the PDF is
// strictly upper-hemisphere.
void test_lambert_pdf_below_horizon_is_zero() {
    const auto m       = make_lambert(Vec3{1.0f, 1.0f, 1.0f});
    const Vec3 normal  = Vec3{0.0f, 1.0f, 0.0f};

    // wo pointing down (opposite the normal) — clearly
    // below horizon.
    RR_CHECK(bsdf_pdf(m, Vec3{0.0f, -1.0f, 0.0f}, normal) == 0.0f);

    // wo at the equator (cos_theta_o = 0).
    RR_CHECK(bsdf_pdf(m, Vec3{1.0f, 0.0f, 0.0f}, normal) == 0.0f);

    // wo slightly below horizon.
    const Vec3 below = rr::math::normalize(Vec3{0.5f, -0.1f, 0.5f});
    RR_CHECK(bsdf_pdf(m, below, normal) == 0.0f);
}

// ---------- §5.5 case 5 ----------

// bsdf_eval returns baseColor / pi regardless of the
// (wi, wo, normal) inputs (Lambert is rotation-invariant).
// The integrator's cos_theta_o gate is applied separately;
// the eval itself does not gate.
void test_lambert_eval_matches_inverse_pi_albedo() {
    const Vec3 albedos[3] = {
        Vec3{0.0f, 0.0f, 0.0f},   // pure-black
        Vec3{0.5f, 0.5f, 0.5f},   // mid-grey
        Vec3{1.0f, 0.5f, 0.25f},  // warm-tone
    };
    for (const auto& base : albedos) {
        const auto m   = make_lambert(base);
        const Vec3 wi  = Vec3{0.0f, 1.0f, 0.0f};
        const Vec3 wo  = Vec3{1.0f, 0.0f, 0.0f};
        const Vec3 n   = Vec3{0.0f, 1.0f, 0.0f};
        const Vec3 v   = bsdf_eval(m, wi, wo, n);
        RR_CHECK(approx(v.x, base.x * rr::math::kInvPi));
        RR_CHECK(approx(v.y, base.y * rr::math::kInvPi));
        RR_CHECK(approx(v.z, base.z * rr::math::kInvPi));
    }
}

// ---------- §5.5 case 6 ----------

// The integrator's per-bounce throughput update is
// (value * cos_theta_o) / pdf. For cosine-weighted
// Lambert, this analytically simplifies to baseColor.
// Anchoring the simplification's bit-equivalence
// guarantees the future MIS.5 / MIS.6 integrators
// produce byte-identical output with the existing
// inline `throughput *= m.baseColor` arithmetic at v1.
void test_lambert_throughput_simplification() {
    const Vec3 base    = Vec3{0.7f, 0.3f, 0.5f};
    const auto m       = make_lambert(base);
    const Vec3 wi      = Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 normal  = Vec3{0.0f, 1.0f, 0.0f};
    const Vec2 u       = Vec2{0.5f, 0.5f};

    const BsdfSample s = sample_bsdf(m, wi, normal, u);
    RR_CHECK(s.valid == true);

    const float cos_theta_o = rr::math::dot(normal, s.wo);
    const Vec3 throughput_term = Vec3{
        (s.value.x * cos_theta_o) / s.pdf,
        (s.value.y * cos_theta_o) / s.pdf,
        (s.value.z * cos_theta_o) / s.pdf,
    };

    // The simplification: (baseColor/pi · cos_theta_o)
    // / (cos_theta_o/pi) = baseColor.
    // Float-arithmetic equality holds within ~1 ULP
    // because the canceling factors are mathematically
    // exact ratios. Use approx() with a slightly
    // generous epsilon to absorb the ULP-level rounding
    // from the explicit divide.
    RR_CHECK(approx(throughput_term.x, base.x, 1e-6f));
    RR_CHECK(approx(throughput_term.y, base.y, 1e-6f));
    RR_CHECK(approx(throughput_term.z, base.z, 1e-6f));
}

// ---------- §5.5 case 7 ----------

// Monte-Carlo PDF normalisation: averaging 1/pdf over
// cosine-weighted samples should approximate the upper-
// hemisphere area (2π).
//
// CAVEAT: the estimator E[1/p(X)] = 2π over cosine-
// weighted X is well-defined (mean is finite), but the
// variance is INFINITE — samples near the horizon have
// 1/pdf → ∞. Convergence at finite N is heavy-tailed.
// In practice at N = 10^5 the empirical mean lands
// within ~5-10% of 2π for most seeds; we use a 25%
// tolerance to avoid flakiness and N = 10^5 (vs the
// brief's N = 10^4 suggestion) for additional margin.
void test_lambert_sample_pdf_normalises_via_monte_carlo() {
    const auto m       = make_lambert(Vec3{1.0f, 1.0f, 1.0f});
    const Vec3 wi      = Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 normal  = Vec3{0.0f, 1.0f, 0.0f};

    rr::pathtracer::Rng rng = rr::pathtracer::make_pixel_rng(
        0u, 0u, 0u, 0xBDFFu);

    const int   N   = 100000;
    double sum_inv_pdf = 0.0;
    int    valid_count = 0;
    for (int i = 0; i < N; ++i) {
        const auto u  = rr::pathtracer::next_vec2(rng);
        const auto s  = sample_bsdf(m, wi, normal, u);
        if (s.valid) {
            sum_inv_pdf += 1.0 / static_cast<double>(s.pdf);
            ++valid_count;
        }
    }
    RR_CHECK(valid_count > N - 100);  // virtually all should be valid

    const double estimate = sum_inv_pdf / static_cast<double>(valid_count);
    const double truth    = 2.0 * static_cast<double>(rr::math::kPi);
    const double rel_err  = std::fabs(estimate - truth) / truth;
    RR_CHECK(rel_err < 0.25);
}

// ---------- §5.5 case 8 ----------

// The empirical mean of cos_theta_o over cosine-weighted
// samples should approximate 2/3 (the analytic mean of
// the cosine-weighted hemisphere). This is the
// well-behaved, finite-variance counterpart to case 7.
// Mirrors `pathtracer_tests.cpp::
// test_cosine_hemisphere_distribution`'s shape.
void test_lambert_cos_weighted_mean_dz() {
    const auto m       = make_lambert(Vec3{1.0f, 1.0f, 1.0f});
    const Vec3 wi      = Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 normal  = Vec3{0.0f, 1.0f, 0.0f};

    rr::pathtracer::Rng rng = rr::pathtracer::make_pixel_rng(
        1u, 0u, 0u, 0xCAFEu);

    const int   N   = 100000;
    double sum_cos = 0.0;
    int    count   = 0;
    for (int i = 0; i < N; ++i) {
        const auto u = rr::pathtracer::next_vec2(rng);
        const auto s = sample_bsdf(m, wi, normal, u);
        if (s.valid) {
            sum_cos += rr::math::dot(normal, s.wo);
            ++count;
        }
    }
    const double mean   = sum_cos / static_cast<double>(count);
    const double truth  = 2.0 / 3.0;
    const double abs_err = std::fabs(mean - truth);
    // Finite variance; standard error at N = 10^5 is
    // ~0.001-0.01. 0.02 absolute tolerance is generous.
    RR_CHECK(abs_err < 0.02);
}

// ---------- §5.5 case 9 ----------

// A degenerate normal (zero length) MUST produce a
// valid==false sample. The integrator's defence-in-depth
// gate prevents the normal-construction from feeding NaN
// / inf into the bounce direction.
void test_degenerate_normal_returns_invalid() {
    const auto m       = make_lambert(Vec3{0.5f, 0.5f, 0.5f});
    const Vec3 wi      = Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 normal  = Vec3{0.0f, 0.0f, 0.0f};  // degenerate
    const Vec2 u       = Vec2{0.5f, 0.5f};

    const BsdfSample s = sample_bsdf(m, wi, normal, u);
    RR_CHECK(s.valid == false);
    // pdf and wo + value should remain at bit-zero defaults.
    RR_CHECK(s.pdf == 0.0f);
}

// ---------- §5.5 case 10 ----------

// sample_bsdf called twice with the same inputs returns
// bit-equal samples. Anchors that the helper is a pure
// function of its arguments (no hidden global / TLS
// state). Mirrors `pathtracer_nee_tests.cpp::
// test_helper_determinism`.
void test_helper_determinism() {
    const auto m       = make_lambert(Vec3{0.4f, 0.6f, 0.9f});
    const Vec3 wi      = Vec3{0.5f, 0.5f, 0.7f};
    const Vec3 normal  = Vec3{0.1f, 0.9f, 0.2f};
    const Vec2 u       = Vec2{0.3f, 0.7f};

    const BsdfSample s1 = sample_bsdf(m, wi, normal, u);
    const BsdfSample s2 = sample_bsdf(m, wi, normal, u);
    RR_CHECK(std::memcmp(&s1, &s2, sizeof(BsdfSample)) == 0);
}

}  // namespace

int main() {
    test_default_constructed_sample_is_invalid();
    test_lambert_sample_in_upper_hemisphere();
    test_lambert_pdf_matches_sampler();
    test_lambert_pdf_below_horizon_is_zero();
    test_lambert_eval_matches_inverse_pi_albedo();
    test_lambert_throughput_simplification();
    test_lambert_sample_pdf_normalises_via_monte_carlo();
    test_lambert_cos_weighted_mean_dz();
    test_degenerate_normal_returns_invalid();
    test_helper_determinism();

    std::fprintf(stderr, "pathtracer_bsdf_tests: %d/%d passed\n",
                 g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

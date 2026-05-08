// MIS.4 helper-host tests for the
// `rr::pathtracer::power_heuristic(float, float)`
// helper (defined in `pathtracer/Mis.h`).
//
// The helper is RR_HD inline + pure scalar math, so
// the same code path the future MIS.5 (CUDA) and
// MIS.6 (OptiX) integrators will execute is
// exercised here against the host C++ compiler.
// These tests anchor the contract per
// `docs/PATH_TRACER_MIS_POWER_HEURISTIC_TASK.md`
// §5.5 (eight mandatory cases) so a regression is
// caught at host-build time without requiring a
// CUDA-equipped runtime host.
//
// Coverage (per the task brief §5.5):
//   1. test_power_heuristic_both_zero_returns_zero
//   2. test_power_heuristic_p_a_zero
//   3. test_power_heuristic_p_b_zero
//   4. test_power_heuristic_equal_pdfs
//   5. test_power_heuristic_squares_pdfs (β=2 anti-regression)
//   6. test_power_heuristic_one_dominates
//   7. test_power_heuristic_sum_to_one
//   8. test_power_heuristic_purity
//
// Hand-rolled assertions; same RR_CHECK pattern as
// `pathtracer_bsdf_tests.cpp` /
// `pathtracer_nee_tests.cpp` / `pathtracer_tests.cpp`.

#include "pathtracer/Mis.h"

#include <cmath>
#include <cstdio>
#include <cstring>          // std::memcmp for purity anchor

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

using rr::pathtracer::power_heuristic;

// ---------- §5.5 case 1 ----------

// Both PDFs zero: the natural `0/0` would be NaN;
// the helper's `denom > 0.0f` guard MUST return an
// explicit `0.0f`. Strict equality (the literal
// `0.0f` from the guard branch).
void test_power_heuristic_both_zero_returns_zero() {
    RR_CHECK(power_heuristic(0.0f, 0.0f) == 0.0f);
}

// ---------- §5.5 case 2 ----------

// p_a == 0: numerator is zero; ratio is exactly
// `0.0f` for any non-zero finite `p_b`. The A
// estimator can't sample this direction.
void test_power_heuristic_p_a_zero() {
    const float xs[] = {0.1f, 1.0f, 100.0f, 1.0e6f};
    for (const float x : xs) {
        RR_CHECK(power_heuristic(0.0f, x) == 0.0f);
    }
}

// ---------- §5.5 case 3 ----------

// p_b == 0: denominator collapses to `p_a²`; ratio
// is exactly `1.0f` for any non-zero finite `p_a`.
// All weight goes to A.
void test_power_heuristic_p_b_zero() {
    const float xs[] = {0.1f, 1.0f, 100.0f, 1.0e6f};
    for (const float x : xs) {
        RR_CHECK(power_heuristic(x, 0.0f) == 1.0f);
    }
}

// ---------- §5.5 case 4 ----------

// Equal PDFs: both estimators are equally
// informative; the weight is exactly `0.5f`.
//
// IEEE-754 argument: with `pa = pb = x`,
// `pa2 = pb2 = x²` (both rounded to nearest float
// from the same product). `denom = 2 · x²` is
// exactly representable (multiplying by 2 just
// adjusts the exponent). The ratio `x² / (2 · x²)`
// is `0.5` exactly per IEEE-754 division
// correctness. Strict equality holds.
void test_power_heuristic_equal_pdfs() {
    const float xs[] = {0.1f, 1.0f, 100.0f, 1.0e6f};
    for (const float x : xs) {
        RR_CHECK(power_heuristic(x, x) == 0.5f);
    }
}

// ---------- §5.5 case 5 ----------

// Squares-PDFs (β=2 anti-regression): the helper
// MUST use the power-2 heuristic, not the balance
// heuristic (β=1).
//
// For (p_a, p_b) = (2, 1):
//   β=2 (correct):  pa²/(pa² + pb²) = 4/5 = 0.8
//   β=1 (wrong):    pa /(pa  + pb)  = 2/3 ≈ 0.667
//
// A regression that accidentally writes the
// balance heuristic would silently produce
// suboptimal MIS weights without breaking any
// other test (the values are still in [0, 1] +
// sum to 1). This case explicitly catches the
// β-confusion bug.
void test_power_heuristic_squares_pdfs() {
    const float w = power_heuristic(2.0f, 1.0f);
    // β=2 expected value.
    RR_CHECK(approx(w, 4.0f / 5.0f));
    // Explicit β=1 anti-regression: must NOT equal
    // 2/3 (with a generous epsilon to ensure the
    // two heuristics are clearly distinguished).
    RR_CHECK(!approx(w, 2.0f / 3.0f, 1e-3f));
}

// ---------- §5.5 case 6 ----------

// One-dominates limit: as `p_a / p_b → ∞`, the
// weight on the A-estimator approaches 1. Mirror
// case for the B-estimator.
//
// Test `(p_a, p_b) = (1e6, 1e-3)`:
//   pa² = 1e12, pb² = 1e-6, denom ≈ 1e12.
//   w_a ≈ 1e12 / 1e12 = 1.0f exactly (the 1e-6
//   addition is below FP precision relative to
//   1e12). Symmetric for the swapped pair.
void test_power_heuristic_one_dominates() {
    RR_CHECK(power_heuristic(1.0e6f, 1.0e-3f) > 0.999f);
    RR_CHECK(power_heuristic(1.0e-3f, 1.0e6f) < 0.001f);
}

// ---------- §5.5 case 7 ----------

// Sum-to-one invariant: for any non-zero (p_a,
// p_b), `w(p_a, p_b) + w(p_b, p_a) ≈ 1.0f`. The
// future MIS integrator relies on this for
// unbiased estimator combination — the two
// estimators' weighted contributions partition the
// unit total exactly.
//
// IEEE-754 caveat: each ratio is correctly rounded
// individually; their sum can differ from `1.0f`
// by ~1 ULP due to compounded rounding. Use
// `approx` with `kEps` (1e-5) — generous enough
// to absorb the FP noise but tight enough to
// catch genuine regressions that perturb the sum
// by more than rounding error.
void test_power_heuristic_sum_to_one() {
    const float pairs[][2] = {
        {1.0f, 1.0f},
        {1.0f, 2.0f},
        {3.0f, 4.0f},
        {0.1f, 100.0f},
        {1.0e3f, 1.0e-3f},
    };
    for (const auto& p : pairs) {
        const float w_a = power_heuristic(p[0], p[1]);
        const float w_b = power_heuristic(p[1], p[0]);
        RR_CHECK(approx(w_a + w_b, 1.0f));
        // Each individually in [0, 1].
        RR_CHECK(w_a >= 0.0f && w_a <= 1.0f);
        RR_CHECK(w_b >= 0.0f && w_b <= 1.0f);
    }
}

// ---------- §5.5 case 8 ----------

// Purity / determinism: calling the helper twice
// with identical inputs returns bit-equal outputs.
// Anchors that the helper is a pure function of
// its arguments (no hidden global / TLS state); a
// regression that introduces non-determinism
// (e.g. caching, threading) is caught at host-
// build time. Mirrors `pathtracer_nee_tests::
// test_helper_determinism` and
// `pathtracer_bsdf_tests::test_helper_determinism`.
void test_power_heuristic_purity() {
    const float w1 = power_heuristic(0.5f, 0.7f);
    const float w2 = power_heuristic(0.5f, 0.7f);
    // memcmp on the raw float bit pattern —
    // strictest possible equality check.
    RR_CHECK(std::memcmp(&w1, &w2, sizeof(float)) == 0);

    // Cross-check: a different input produces a
    // different output (a regression that returned
    // a constant would pass the purity check above
    // but fail this).
    const float w3 = power_heuristic(0.7f, 0.5f);
    RR_CHECK(std::memcmp(&w1, &w3, sizeof(float)) != 0);
}

}  // namespace

int main() {
    test_power_heuristic_both_zero_returns_zero();
    test_power_heuristic_p_a_zero();
    test_power_heuristic_p_b_zero();
    test_power_heuristic_equal_pdfs();
    test_power_heuristic_squares_pdfs();
    test_power_heuristic_one_dominates();
    test_power_heuristic_sum_to_one();
    test_power_heuristic_purity();

    std::fprintf(stderr, "pathtracer_mis_tests: %d/%d passed\n",
                 g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

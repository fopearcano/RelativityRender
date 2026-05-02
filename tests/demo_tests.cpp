// Stage 19E.2 demo-scene validation tests.
//
// `--render-demo` is a GPU-bound CLI action whose runtime visual
// output cannot be verified on the audit host. This file
// closes that gap *for the relativistic-perception layer* by
// exercising the same camera + observer composition the kernel
// uses, against analytic ground truth, entirely host-side.
//
// What is and is not in scope:
//   - In scope: per-pixel ray direction (host-callable
//     `generate_camera_ray`) composed with the relativity math
//     leaf (host-callable `dopplerFactor`). The test asserts
//     that the demo's observer-along-(-Z) configuration produces
//     the closed-form longitudinal Doppler factor at the central
//     pixel, that beta = 0 produces D == 1 across the
//     framebuffer, that beta = 0.7 produces D > 1 at center, and
//     that D varies monotonically across the framebuffer in the
//     way Lorentz boost geometry predicts.
//   - Out of scope: the GPU shading kernel; the AOV save path;
//     the actual PPM byte stream. Those run on hardware the
//     audit host doesn't have.
//
// This test is what lets the demo's CLI behaviour be claimed
// scientifically (not just visually plausibly): on a CUDA host,
// the kernel reads the same `GpuCamera` POD `Camera::to_gpu()`
// hands the test, runs the same `generate_camera_ray` /
// `dopplerFactor` formulas (those routines are RR_HD inline and
// compile byte-identically host-vs-device), and writes the
// result. So every assertion below pins a property the GPU
// output must also satisfy (modulo float rounding on different
// fmuladd / rsqrt paths).
//
// The demo's exact scene (kept in lockstep with `run_render_demo`
// in `src/main.cpp`):
//   - camera: default `rr::camera::Camera`, aspect set from
//     cfg.width / cfg.height.
//   - observer velocity: (0, 0, -|beta|).
//   - sphere geometry / material / environment light: not
//     consulted here (they affect Beauty, not Doppler).

#include "camera/Camera.h"
#include "camera/CameraRay.h"
#include "math/Vec3.h"
#include "relativity/RelativityMath.h"

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

constexpr float kEps      = 1.0e-5f;
constexpr float kEpsLoose = 5.0e-4f;  // for high-beta cases

bool approx(float a, float b, float eps = kEps) {
    return std::fabs(a - b) <= eps;
}

// Demo's scene composition. Mirrors `run_render_demo` in main.cpp.
// Any time the production action's camera defaults / observer axis
// change, this helper changes too — a regression in either side
// would surface as a test failure.
rr::camera::GpuCamera demo_camera(int width, int height) {
    rr::camera::Camera cam;
    cam.set_aspect(static_cast<float>(width) / static_cast<float>(height));
    return cam.to_gpu();
}

rr::math::Vec3 demo_observer_velocity(float beta_mag) {
    return rr::math::Vec3{0.0f, 0.0f, -beta_mag};
}

// Closed-form longitudinal Doppler factor (same helper used in
// tests/relativity_tests.cpp).
double doppler_longitudinal(double beta) {
    return std::sqrt((1.0 + beta) / (1.0 - beta));
}

// ---------- 1. beta = 0 reproduces the classical (non-relativistic) render ----------
//
// The strongest property the demo's beta = 0 path must satisfy:
// for every pixel, the Doppler factor is exactly 1 (so the
// Beauty AOV is the un-shifted base shading and the Doppler AOV
// is a flat 1.0 image). This is the property the user prompt's
// requirement #1 ("beta = 0 render should behave like normal
// camera mode") rests on; the math leaf's
// test_identity_at_zero_beta already proved D == 1 in isolation,
// and this test confirms the same identity holds *under the
// demo's exact camera composition* — i.e. the camera-to-observer
// plumbing doesn't accidentally introduce any beta.

void test_demo_beta_zero_is_classical() {
    using rr::math::Vec3;
    using rr::relativity::dopplerFactor;

    const int width  = 64;
    const int height = 36;
    const auto cam   = demo_camera(width, height);
    const Vec3 beta  = demo_observer_velocity(0.0f);  // exactly zero

    // Sample a coarse but representative pixel grid (corners,
    // centre, off-axis points).
    struct Px { int x, y; };
    const Px pixels[] = {
        {       0,        0},
        {width-1,         0},
        {       0,  height-1},
        {width-1,   height-1},
        {width/2,   height/2},
        {width/4,   height/4},
        {3*width/4, 3*height/4},
    };

    for (const Px& p : pixels) {
        const auto ray = rr::camera::generate_camera_ray(
            cam, p.x, p.y, width, height);
        const float D = dopplerFactor(beta, ray.direction);
        RR_CHECK(approx(D, 1.0f, kEps));
    }
}

// ---------- 2. beta = 0.7 visibly changes the output (forward blueshift) ----------
//
// Requirement #2 ("beta = 0.7 or beta = 0.9 should visibly
// change the output"). The test asserts the central pixel's
// Doppler factor matches the closed-form longitudinal blueshift
// for a sphere on the camera's forward axis: at center the
// camera ray is exactly the forward direction (-Z), and the
// observer's velocity is also along -Z, so beta · direction =
// +|beta| and D = sqrt((1+|beta|)/(1-|beta|)).
//
// We test 0.7 and 0.9 explicitly so both prompt-stated values
// are pinned, plus a 0.0 -> 0.7 -> 0.9 monotonicity check at
// the central pixel.

void test_demo_beta_zero_seven_blueshift() {
    using rr::math::Vec3;
    using rr::relativity::dopplerFactor;

    // Use ODD dimensions so the pixel at (width/2, height/2) is
    // exactly the centre of the image plane. For an even-resolution
    // image the "central" pixel is offset by half a pixel (the
    // pixel-centre rule samples at integer + 0.5), which would
    // tilt the ray a fraction of a degree off the forward axis
    // and break a strict longitudinal closed-form match. The
    // production demo runs at 1280x720 (even); the resulting
    // half-pixel offset is visually undetectable but breaks an
    // unconditional analytic test, hence the odd-dim fixture
    // here.
    const int width  = 65;
    const int height = 37;
    const auto cam   = demo_camera(width, height);
    const int  cx    = width  / 2;   // 32  -> pixel-centre = 32.5
    const int  cy    = height / 2;   // 18  -> pixel-centre = 18.5

    // Central pixel direction is (0, 0, -1) within float rounding.
    const auto ray = rr::camera::generate_camera_ray(
        cam, cx, cy, width, height);
    RR_CHECK(approx(ray.direction.x,  0.0f, kEps));
    RR_CHECK(approx(ray.direction.y,  0.0f, kEps));
    RR_CHECK(approx(ray.direction.z, -1.0f, kEps));

    // beta = 0.7 -> D_center = sqrt(1.7/0.3) ~ 2.380.
    {
        const float beta_mag = 0.7f;
        const Vec3  beta_vec = demo_observer_velocity(beta_mag);
        const float D        = dopplerFactor(beta_vec, ray.direction);
        const float D_ref    = static_cast<float>(
            doppler_longitudinal(static_cast<double>(beta_mag)));
        RR_CHECK(D > 1.0f);                                  // blueshift
        RR_CHECK(approx(D, D_ref, kEpsLoose * D_ref));       // analytic
    }

    // beta = 0.9 -> D_center = sqrt(1.9/0.1) ~ 4.359.
    {
        const float beta_mag = 0.9f;
        const Vec3  beta_vec = demo_observer_velocity(beta_mag);
        const float D        = dopplerFactor(beta_vec, ray.direction);
        const float D_ref    = static_cast<float>(
            doppler_longitudinal(static_cast<double>(beta_mag)));
        RR_CHECK(D > 1.0f);
        RR_CHECK(approx(D, D_ref, kEpsLoose * D_ref));
    }

    // Strict monotonicity at the central pixel: D(0) < D(0.7) <
    // D(0.9). If a future change accidentally inverts the sign
    // convention, this test fails immediately.
    {
        const float D0   = dopplerFactor(demo_observer_velocity(0.0f),
                                         ray.direction);
        const float D07  = dopplerFactor(demo_observer_velocity(0.7f),
                                         ray.direction);
        const float D09  = dopplerFactor(demo_observer_velocity(0.9f),
                                         ray.direction);
        RR_CHECK(approx(D0, 1.0f, kEps));
        RR_CHECK(D07 > D0);
        RR_CHECK(D09 > D07);
    }
}

// ---------- 3. forward beaming: corner pixels have lower D than the centre ----------
//
// Requirement #2 (continued). The demo's observer points along
// -Z; the camera looks along -Z. The central pixel ray is
// parallel to the boost axis, so its Doppler factor is the
// maximum across the frame. Corner pixels tilt off-axis (their
// direction has non-trivial x / y components), so beta · dir is
// smaller and D is therefore smaller. This test pins the
// "forward beaming" pattern that makes the relativistic effect
// visible.

void test_demo_beta_corner_dimmer_than_center() {
    using rr::math::Vec3;
    using rr::relativity::dopplerFactor;

    const int width  = 64;
    const int height = 36;
    const auto cam   = demo_camera(width, height);
    const Vec3 beta  = demo_observer_velocity(0.7f);

    const auto centre = rr::camera::generate_camera_ray(
        cam, width / 2, height / 2, width, height);
    const float D_center = dopplerFactor(beta, centre.direction);

    // Each corner pixel must produce a strictly smaller D than
    // the central pixel.
    struct Px { int x, y; };
    const Px corners[] = {
        {0,         0        },
        {width - 1, 0        },
        {0,         height - 1},
        {width - 1, height - 1},
    };
    for (const Px& p : corners) {
        const auto ray = rr::camera::generate_camera_ray(
            cam, p.x, p.y, width, height);
        const float D = dopplerFactor(beta, ray.direction);
        RR_CHECK(D > 0.0f);            // strictly positive
        RR_CHECK(std::isfinite(D));
        RR_CHECK(D < D_center);        // forward beaming
    }
}

// ---------- 4. demo output is deterministic across betas ----------
//
// Requirement #4 ("Output files should be deterministic enough
// for smoke tests"). At the host-validation layer this means:
// the same (camera, beta, pixel) triple always produces the
// same Doppler factor (no RNG, no time-derived state, no
// floating-point drift between calls). The test re-runs the
// computation twice and asserts byte equality. (The PPM-bytes
// determinism is downstream of GPU rounding and lives in a
// CUDA-host run.)

void test_demo_doppler_is_deterministic() {
    using rr::math::Vec3;
    using rr::relativity::dopplerFactor;

    const int width  = 32;
    const int height = 18;
    const auto cam   = demo_camera(width, height);

    const float betas[] = {0.0f, 0.3f, 0.7f, 0.9f, 0.99f};

    for (float beta_mag : betas) {
        const Vec3 beta = demo_observer_velocity(beta_mag);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto ray = rr::camera::generate_camera_ray(
                    cam, x, y, width, height);
                const float D1 = dopplerFactor(beta, ray.direction);
                const float D2 = dopplerFactor(beta, ray.direction);
                RR_CHECK(D1 == D2);                  // bit-identical
                RR_CHECK(std::isfinite(D1));
                RR_CHECK(D1 > 0.0f);
            }
        }
    }
}

// ---------- 5. demo's beta range honours clampBeta ----------
//
// Requirement: out-of-range |beta| (>= 1) is clamped to
// 0.999999 by the action (the dispatcher in main.cpp passes the
// user value through clampBeta before composing the observer
// velocity). The unit-test #6 in tests/relativity_tests.cpp
// already pins clampBeta in isolation; this test pins the
// composed contract: under the demo's exact observer pattern,
// any |beta| >= 1 input still yields a finite + positive
// Doppler factor at every pixel.

void test_demo_clamps_invalid_beta() {
    using rr::math::Vec3;
    using rr::relativity::clampBeta;
    using rr::relativity::dopplerFactor;

    const int width  = 16;
    const int height = 9;
    const auto cam   = demo_camera(width, height);

    const float invalid_inputs[] = {
        1.0f, 1.5f, 2.0f, 100.0f,
        -1.0f, -2.5f, -100.0f,
    };

    for (float user_beta : invalid_inputs) {
        const float clamped = clampBeta(user_beta, 0.999999f);
        RR_CHECK(clamped >= 0.0f);
        RR_CHECK(clamped <= 0.999999f);

        const Vec3 beta = demo_observer_velocity(clamped);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto ray = rr::camera::generate_camera_ray(
                    cam, x, y, width, height);
                const float D = dopplerFactor(beta, ray.direction);
                RR_CHECK(std::isfinite(D));
                RR_CHECK(D > 0.0f);
            }
        }
    }
}

}  // namespace

int main() {
    test_demo_beta_zero_is_classical();
    test_demo_beta_zero_seven_blueshift();
    test_demo_beta_corner_dimmer_than_center();
    test_demo_doppler_is_deterministic();
    test_demo_clamps_invalid_beta();

    if (g_failed == 0) {
        std::printf("demo_tests: %d / %d passed\n", g_total, g_total);
        return 0;
    }
    std::fprintf(stderr,
                 "demo_tests: %d / %d FAILED\n", g_failed, g_total);
    return 1;
}

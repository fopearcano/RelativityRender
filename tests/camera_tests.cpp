// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.

#include "camera/Camera.h"
#include "camera/CameraRay.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"

#include <cmath>
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

using rr::camera::Camera;
using rr::camera::generate_camera_ray;
using rr::camera::GpuCamera;
using rr::math::Vec3;

void test_default_camera() {
    Camera cam;
    RR_CHECK(cam.position() == Vec3(0, 0, 0));
    RR_CHECK(nearly_equal(cam.forward(), Vec3{0, 0, -1}));
    RR_CHECK(nearly_equal(cam.up(),      Vec3{0, 1,  0}));
    RR_CHECK(nearly_equal(cam.right(),   Vec3{1, 0,  0}));
    RR_CHECK(cam.vertical_fov_degrees() == 45.0f);
    RR_CHECK(nearly_equal(cam.vertical_fov_radians(), 45.0f * rr::math::kPi / 180.0f));
    RR_CHECK(cam.near_plane() == 0.1f);
    RR_CHECK(cam.far_plane()  == 1000.0f);
}

void test_basis_orthonormal_after_look_at() {
    using rr::math::cross;
    using rr::math::dot;
    using rr::math::length;

    Camera cam;
    cam.look_at(Vec3{2, 3, 5}, Vec3{0, 0, 0});

    RR_CHECK(cam.position() == Vec3(2, 3, 5));
    RR_CHECK(nearly_equal(length(cam.forward()), 1.0f));
    RR_CHECK(nearly_equal(length(cam.up()),      1.0f));
    RR_CHECK(nearly_equal(length(cam.right()),   1.0f));
    RR_CHECK(nearly_equal(dot(cam.forward(), cam.right()), 0.0f));
    RR_CHECK(nearly_equal(dot(cam.forward(), cam.up()),    0.0f));
    RR_CHECK(nearly_equal(dot(cam.right(),   cam.up()),    0.0f));
    // Right-handed: forward x right == -up (camera looks toward -Z when
    // forward = -Z, so cross(forward, right) gives up flipped).
    // The convention we use in `recompute_basis` is right = cross(forward, up_hint),
    // up = cross(right, forward). Verify.
    RR_CHECK(nearly_equal(cross(cam.right(), cam.forward()), cam.up()));
}

void test_look_at_degenerate_keeps_orientation() {
    Camera cam;
    cam.look_at(Vec3{1, 2, 3}, Vec3{1, 2, 3});  // eye == target
    RR_CHECK(cam.position() == Vec3(1, 2, 3));
    // Orientation falls back to default forward.
    RR_CHECK(nearly_equal(cam.forward(), Vec3{0, 0, -1}));
}

void test_look_at_parallel_up_falls_back() {
    using rr::math::dot;
    Camera cam;
    cam.look_at(Vec3{0, 0, 0}, Vec3{0, 1, 0}, /*up_hint=*/Vec3{0, 1, 0});
    // forward == +Y, parallel to up_hint; the basis should still be
    // orthonormal, populated from the fallback axis.
    RR_CHECK(nearly_equal(cam.forward(), Vec3{0, 1, 0}));
    RR_CHECK(nearly_equal(dot(cam.forward(), cam.right()), 0.0f));
    RR_CHECK(nearly_equal(dot(cam.forward(), cam.up()),    0.0f));
}

void test_set_vfov_is_clamped() {
    Camera cam;
    cam.set_vertical_fov_degrees(0.0f);    // clamped up
    RR_CHECK(cam.vertical_fov_degrees() > 0.0f);
    cam.set_vertical_fov_degrees(360.0f);  // clamped down
    RR_CHECK(cam.vertical_fov_degrees() < 180.0f);
}

void test_to_gpu_matches_class_state() {
    Camera cam;
    cam.set_vertical_fov_degrees(60.0f);
    cam.set_aspect(2.0f);

    const auto g = cam.to_gpu();
    RR_CHECK(g.position == cam.position());
    RR_CHECK(g.forward  == cam.forward());
    RR_CHECK(g.up       == cam.up());
    RR_CHECK(g.right    == cam.right());
    RR_CHECK(nearly_equal(g.aspect, 2.0f));
    RR_CHECK(nearly_equal(g.tan_half_vfov,
                          std::tan(0.5f * cam.vertical_fov_radians())));
}

void test_generate_camera_ray_default_camera() {
    Camera cam;                  // origin, looking -Z
    cam.set_aspect(1.0f);        // square image
    const auto g = cam.to_gpu();

    constexpr int W = 64;
    constexpr int H = 64;

    // Centre pixel direction should be very close to the forward axis.
    const auto centre = generate_camera_ray(g, W / 2, H / 2, W, H);
    RR_CHECK(centre.origin == cam.position());
    // generate_camera_ray samples at (x+0.5, y+0.5), so picking W/2,H/2
    // sits one half-pixel off the exact centre on an even-sized image -
    // bound the deviation rather than asserting equality.
    RR_CHECK(abs_f(centre.direction.x) < 0.05f);
    RR_CHECK(abs_f(centre.direction.y) < 0.05f);
    RR_CHECK(centre.direction.z < -0.99f);

    // Top-left pixel should look up and to the left (in camera space):
    //   x component < 0, y component > 0, z component < 0.
    const auto tl = generate_camera_ray(g, 0, 0, W, H);
    RR_CHECK(tl.direction.x < 0.0f);
    RR_CHECK(tl.direction.y > 0.0f);
    RR_CHECK(tl.direction.z < 0.0f);

    // Bottom-right pixel: x > 0, y < 0, z < 0.
    const auto br = generate_camera_ray(g, W - 1, H - 1, W, H);
    RR_CHECK(br.direction.x > 0.0f);
    RR_CHECK(br.direction.y < 0.0f);
    RR_CHECK(br.direction.z < 0.0f);

    // All directions should be unit-length.
    RR_CHECK(nearly_equal(rr::math::length(centre.direction), 1.0f));
    RR_CHECK(nearly_equal(rr::math::length(tl.direction),     1.0f));
    RR_CHECK(nearly_equal(rr::math::length(br.direction),     1.0f));
}

void test_aspect_widens_horizontal_fov() {
    Camera narrow;
    narrow.set_aspect(1.0f);
    Camera wide;
    wide.set_aspect(2.0f);
    const auto gn = narrow.to_gpu();
    const auto gw = wide.to_gpu();

    // Left-edge pixel ray should reach further left horizontally on the
    // wider camera at the same image resolution.
    constexpr int W = 100;
    constexpr int H = 100;
    const auto ln = generate_camera_ray(gn, 0, H / 2, W, H);
    const auto lw = generate_camera_ray(gw, 0, H / 2, W, H);
    RR_CHECK(lw.direction.x < ln.direction.x);  // more negative = farther left
}

}

int main() {
    test_default_camera();
    test_basis_orthonormal_after_look_at();
    test_look_at_degenerate_keeps_orientation();
    test_look_at_parallel_up_falls_back();
    test_set_vfov_is_clamped();
    test_to_gpu_matches_class_state();
    test_generate_camera_ray_default_camera();
    test_aspect_widens_horizontal_fov();

    std::printf("camera_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

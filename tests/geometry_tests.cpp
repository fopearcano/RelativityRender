// Hand-rolled assertion runner. The real test framework comes with
// the M2 deferred items.
//
// `cuda/CudaIntersection.cuh` is RR_HD inline and contains no CUDA
// runtime calls, so we include it from a regular C++ TU and exercise
// the exact code the kernel runs.

#include "camera/Camera.h"
#include "camera/CameraRay.h"
#include "cuda/CudaIntersection.cuh"
#include "geometry/Sphere.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "renderer/Hit.h"

#include <cstdio>

namespace {

int g_total  = 0;
int g_failed = 0;

float abs_f(float a) { return a < 0.0f ? -a : a; }

bool nearly_equal(float a, float b, float eps = 1.0e-4f) {
    const float scale  = 1.0f > abs_f(a) ? 1.0f : abs_f(a);
    const float scale2 = scale > abs_f(b) ? scale : abs_f(b);
    return abs_f(a - b) <= eps * scale2;
}

bool nearly_equal(rr::math::Vec3 a, rr::math::Vec3 b, float eps = 1.0e-4f) {
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

using rr::camera::CameraRay;
using rr::cuda::intersect_sphere;
using rr::geometry::Sphere;
using rr::math::Vec3;

CameraRay make_ray(Vec3 o, Vec3 d) {
    // The kernel uses normalized directions, so do the same here.
    return CameraRay{o, rr::math::normalize(d)};
}

void test_miss_when_ray_points_away() {
    const Sphere s{Vec3{0, 0, -3}, 1.0f};
    const auto   r = make_ray(Vec3{0, 0, 0}, Vec3{0, 0, 1});  // wrong direction
    const auto   h = intersect_sphere(r, s, 0.0f, 1.0e30f);
    RR_CHECK(!h.hit);
}

void test_centre_ray_hits_front_of_sphere() {
    const Sphere s{Vec3{0, 0, -3}, 1.0f};
    const auto   r = make_ray(Vec3{0, 0, 0}, Vec3{0, 0, -1});
    const auto   h = intersect_sphere(r, s, 0.0f, 1.0e30f);

    RR_CHECK(h.hit);
    // Front of the sphere along -Z is at z = -2 (centre -3, radius 1).
    RR_CHECK(nearly_equal(h.t, 2.0f));
    RR_CHECK(nearly_equal(h.position, Vec3{0, 0, -2}));
    // Outward normal at the front of the sphere is +Z.
    RR_CHECK(nearly_equal(h.normal, Vec3{0, 0, 1}));
    RR_CHECK(nearly_equal(rr::math::length(h.normal), 1.0f));
}

void test_grazing_miss() {
    const Sphere s{Vec3{0, 0, -3}, 1.0f};
    // Aim well outside the sphere on the +X side.
    const auto   r = make_ray(Vec3{0, 0, 0}, Vec3{5, 0, -3});
    const auto   h = intersect_sphere(r, s, 0.0f, 1.0e30f);
    RR_CHECK(!h.hit);
}

void test_t_min_and_t_max_clip() {
    const Sphere s{Vec3{0, 0, -3}, 1.0f};
    const auto   r = make_ray(Vec3{0, 0, 0}, Vec3{0, 0, -1});

    // Hit at t = 2; clip with t_max < 2 should miss.
    auto h = intersect_sphere(r, s, 0.0f, 1.5f);
    RR_CHECK(!h.hit);

    // t_min greater than the near root forces fallback to the far root
    // (t = 4 here), which is also clipped if t_max is below it.
    h = intersect_sphere(r, s, 2.5f, 3.5f);
    RR_CHECK(!h.hit);

    // Permissive bounds find the near root again.
    h = intersect_sphere(r, s, 0.0f, 1.0e30f);
    RR_CHECK(h.hit);
    RR_CHECK(nearly_equal(h.t, 2.0f));
}

void test_inside_sphere_hits_far_root() {
    const Sphere s{Vec3{0, 0, 0}, 2.0f};
    // Origin inside, direction +X. Near root is negative; far root is +2.
    const auto r = make_ray(Vec3{0, 0, 0}, Vec3{1, 0, 0});
    const auto h = intersect_sphere(r, s, 0.0f, 1.0e30f);

    RR_CHECK(h.hit);
    RR_CHECK(nearly_equal(h.t, 2.0f));
    RR_CHECK(nearly_equal(h.position, Vec3{2, 0, 0}));
    RR_CHECK(nearly_equal(h.normal,   Vec3{1, 0, 0}));
}

void test_centre_pixel_of_default_camera_hits_test_sphere() {
    // Reproduces the per-pixel work of the M8 kernel on the host: build
    // the same default camera, generate the centre-pixel ray, intersect
    // against the same hard-coded test sphere, and check the geometry.
    rr::camera::Camera cam;          // origin, looking -Z, +Y up
    cam.set_aspect(1.0f);            // square test image
    const auto g = cam.to_gpu();

    constexpr int W = 256;
    constexpr int H = 256;

    const auto ray = rr::camera::generate_camera_ray(g, W / 2, H / 2, W, H);

    const Sphere sphere{Vec3{0, 0, -3}, 1.0f};
    const auto   h = intersect_sphere(ray, sphere, 0.0f, 1.0e30f);

    RR_CHECK(h.hit);
    // Centre pixel is essentially straight ahead, so we hit close to the
    // front of the sphere; bound rather than equal because the centre
    // pixel sits a half-pixel off-axis on an even-sized image.
    RR_CHECK(h.t > 1.9f && h.t < 2.1f);
    RR_CHECK(h.normal.z > 0.95f);    // normal points roughly back at the camera
}

void test_corner_pixel_misses_test_sphere() {
    rr::camera::Camera cam;
    cam.set_aspect(1.0f);
    const auto g = cam.to_gpu();

    constexpr int W = 64;
    constexpr int H = 64;

    const auto ray = rr::camera::generate_camera_ray(g, 0, 0, W, H);

    const Sphere sphere{Vec3{0, 0, -3}, 1.0f};
    const auto   h = intersect_sphere(ray, sphere, 0.0f, 1.0e30f);

    RR_CHECK(!h.hit);
}

void test_make_miss_is_default_state() {
    const auto m = rr::renderer::make_miss();
    RR_CHECK(!m.hit);
    RR_CHECK(m.t == 0.0f);
    RR_CHECK(m.position == Vec3{0, 0, 0});
    RR_CHECK(m.normal   == Vec3{0, 0, 0});
}

void test_make_sphere_factory_matches_aggregate() {
    const auto s = rr::geometry::make_sphere(Vec3{1, 2, 3}, 4.0f);
    RR_CHECK(s.center == Vec3{1, 2, 3});
    RR_CHECK(s.radius == 4.0f);
    RR_CHECK(s.material_index == -1);   // factory uses the "no material" sentinel
}

void test_sphere_intersection_propagates_material_index() {
    using rr::cuda::intersect_sphere;
    const Sphere s_default{Vec3{0, 0, -3}, 1.0f};                  // material_index = -1
    const Sphere s_tagged {Vec3{0, 0, -3}, 1.0f, /*material*/ 7};

    const auto r = make_ray(Vec3{0, 0, 0}, Vec3{0, 0, -1});

    auto h = intersect_sphere(r, s_default, 0.0f, 1.0e30f);
    RR_CHECK(h.hit);
    RR_CHECK(h.material_index == -1);

    h = intersect_sphere(r, s_tagged, 0.0f, 1.0e30f);
    RR_CHECK(h.hit);
    RR_CHECK(h.material_index == 7);

    // Aggregate init with only `{center, radius}` still works -
    // material_index defaults to -1.
    const Sphere s_agg{Vec3{0, 0, -3}, 1.0f};
    h = intersect_sphere(r, s_agg, 0.0f, 1.0e30f);
    RR_CHECK(h.material_index == -1);
}

// --- Triangle (Moller-Trumbore) -----------------------------------------

void test_triangle_centre_hit() {
    using rr::cuda::intersect_triangle;
    // CCW front-face winding; triangle in plane z = -3.
    const Vec3 v0{-1.0f, -1.0f, -3.0f};
    const Vec3 v1{ 1.0f, -1.0f, -3.0f};
    const Vec3 v2{ 0.0f,  1.0f, -3.0f};

    const auto r = make_ray(Vec3{0, 0, 0}, Vec3{0, 0, -1});
    const auto h = intersect_triangle(r, v0, v1, v2, 0.0f, 1.0e30f);

    RR_CHECK(h.hit);
    RR_CHECK(nearly_equal(h.t, 3.0f));
    RR_CHECK(nearly_equal(h.position, Vec3{0, 0, -3}));
    // Front-face normal of CCW (v0 -> v1 -> v2) on a plane facing
    // the camera should be +Z.
    RR_CHECK(nearly_equal(h.normal, Vec3{0, 0, 1}));
}

void test_triangle_outside_misses() {
    using rr::cuda::intersect_triangle;
    const Vec3 v0{-1.0f, -1.0f, -3.0f};
    const Vec3 v1{ 1.0f, -1.0f, -3.0f};
    const Vec3 v2{ 0.0f,  1.0f, -3.0f};

    // Aim well outside the triangle on the +X side.
    const auto r = make_ray(Vec3{0, 0, 0}, Vec3{5, 0, -3});
    const auto h = intersect_triangle(r, v0, v1, v2, 0.0f, 1.0e30f);
    RR_CHECK(!h.hit);
}

void test_triangle_parallel_ray_misses() {
    using rr::cuda::intersect_triangle;
    const Vec3 v0{-1.0f, -1.0f, -3.0f};
    const Vec3 v1{ 1.0f, -1.0f, -3.0f};
    const Vec3 v2{ 0.0f,  1.0f, -3.0f};

    // Ray parallel to the triangle plane (along +X).
    const auto r = make_ray(Vec3{0, 0, -3}, Vec3{1, 0, 0});
    const auto h = intersect_triangle(r, v0, v1, v2, 0.0f, 1.0e30f);
    RR_CHECK(!h.hit);
}

void test_triangle_double_sided_hit_from_back() {
    using rr::cuda::intersect_triangle;
    const Vec3 v0{-1.0f, -1.0f, -3.0f};
    const Vec3 v1{ 1.0f, -1.0f, -3.0f};
    const Vec3 v2{ 0.0f,  1.0f, -3.0f};

    // Ray approaches from behind the triangle (camera at -10 z,
    // looking +Z). The MT routine is double-sided, so it still hits.
    const auto r = make_ray(Vec3{0, 0, -10}, Vec3{0, 0, 1});
    const auto h = intersect_triangle(r, v0, v1, v2, 0.0f, 1.0e30f);
    RR_CHECK(h.hit);
    RR_CHECK(nearly_equal(h.t, 7.0f));
}

void test_triangle_t_min_and_t_max_clip() {
    using rr::cuda::intersect_triangle;
    const Vec3 v0{-1.0f, -1.0f, -3.0f};
    const Vec3 v1{ 1.0f, -1.0f, -3.0f};
    const Vec3 v2{ 0.0f,  1.0f, -3.0f};

    const auto r = make_ray(Vec3{0, 0, 0}, Vec3{0, 0, -1});
    auto h = intersect_triangle(r, v0, v1, v2, 0.0f, 2.5f);
    RR_CHECK(!h.hit);                         // t = 3, clipped by t_max
    h = intersect_triangle(r, v0, v1, v2, 3.5f, 1.0e30f);
    RR_CHECK(!h.hit);                         // t = 3, below t_min
    h = intersect_triangle(r, v0, v1, v2, 0.0f, 1.0e30f);
    RR_CHECK(h.hit);
    RR_CHECK(nearly_equal(h.t, 3.0f));
}

void test_triangle_winding_flips_normal() {
    using rr::cuda::intersect_triangle;
    const Vec3 v0{-1.0f, -1.0f, -3.0f};
    const Vec3 v1{ 1.0f, -1.0f, -3.0f};
    const Vec3 v2{ 0.0f,  1.0f, -3.0f};

    const auto r = make_ray(Vec3{0, 0, 0}, Vec3{0, 0, -1});

    const auto ccw = intersect_triangle(r, v0, v1, v2, 0.0f, 1.0e30f);
    const auto cw  = intersect_triangle(r, v0, v2, v1, 0.0f, 1.0e30f);

    RR_CHECK(ccw.hit);
    RR_CHECK(cw.hit);
    // Reversing the winding must flip the geometric normal.
    RR_CHECK(nearly_equal(ccw.normal, -cw.normal));
}

}

int main() {
    test_miss_when_ray_points_away();
    test_centre_ray_hits_front_of_sphere();
    test_grazing_miss();
    test_t_min_and_t_max_clip();
    test_inside_sphere_hits_far_root();
    test_centre_pixel_of_default_camera_hits_test_sphere();
    test_corner_pixel_misses_test_sphere();
    test_make_miss_is_default_state();
    test_make_sphere_factory_matches_aggregate();
    test_sphere_intersection_propagates_material_index();

    test_triangle_centre_hit();
    test_triangle_outside_misses();
    test_triangle_parallel_ray_misses();
    test_triangle_double_sided_hit_from_back();
    test_triangle_t_min_and_t_max_clip();
    test_triangle_winding_flips_normal();

    std::printf("geometry_tests: %d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

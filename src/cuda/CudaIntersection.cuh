#pragma once

// Ray-primitive intersection routines.
//
// Despite the `.cuh` extension this header is host- and device-callable:
// every entry point is `RR_HD inline` and includes only the cross-target
// math / camera / geometry / renderer headers. That lets host tests
// exercise the same code path the GPU kernels run, so validating the
// math on the host validates the device behaviour by construction.
//
// Stage 7C adds `intersect_triangle` (Moller-Trumbore) alongside the
// pre-existing `intersect_sphere`. Both are RR_HD inline so host
// tests and device kernels exercise the same code paths.

#include "camera/CameraRay.h"
#include "geometry/Sphere.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "renderer/Hit.h"

#include <cmath>  // sqrtf is host- and device-callable on every supported toolchain

namespace rr::cuda {

// Ray-sphere intersection.
//
// Solves the quadratic `a t^2 + 2 b t + c = 0` with
//   a = d . d,   b = oc . d,   c = oc . oc - r^2
// where `oc = ray.origin - sphere.center`. Returns the nearest hit in
// the open interval (`t_min`, `t_max`); falls back to the far root when
// the near one is out of range, which is the natural behaviour for rays
// originating inside the sphere.
//
// `ray.direction` does not need to be unit length; the returned `t` is
// in the ray's own parameter scale and `position` is `origin + t*dir`.
// `normal` is always the outward unit normal at the hit.
RR_HD inline rr::renderer::Hit intersect_sphere(const rr::camera::CameraRay& ray,
                                                const rr::geometry::Sphere&  sphere,
                                                float t_min, float t_max) {
    using rr::math::Vec3;
    using rr::math::dot;

    rr::renderer::Hit out{};

    const Vec3  oc   = ray.origin - sphere.center;
    const float a    = dot(ray.direction, ray.direction);
    const float b    = dot(oc, ray.direction);
    const float c    = dot(oc, oc) - sphere.radius * sphere.radius;
    const float disc = b * b - a * c;

    if (disc < 0.0f || a <= 0.0f) {
        return out;  // miss (or degenerate ray)
    }

    const float sqrt_disc = sqrtf(disc);

    float t = (-b - sqrt_disc) / a;
    if (t <= t_min || t >= t_max) {
        t = (-b + sqrt_disc) / a;
        if (t <= t_min || t >= t_max) {
            return out;
        }
    }

    out.hit            = true;
    out.t              = t;
    out.position       = ray.origin + ray.direction * t;
    // Inverse-radius multiply rather than `normalize`: `radius > 0` is
    // a precondition, and this avoids a redundant `length` call.
    out.normal         = (out.position - sphere.center) * (1.0f / sphere.radius);
    out.material_index = sphere.material_index;

    // Spherical UV mapping. `u` is the longitude around +Y in [0, 1);
    // `v` is the latitude with v = 0 at the south pole and v = 1 at
    // the north pole - matching the texture-system v-up convention
    // (UV (0, 0) maps to the texture's bottom-left). The kernel does
    // not consume `uv` at this stage; populating it here keeps the
    // intersection routine ready for the texture stage without any
    // extra branches at hit time.
    {
        const rr::math::Vec3& n = out.normal;
        const float ny = n.y < -1.0f ? -1.0f : (n.y > 1.0f ? 1.0f : n.y);
        out.uv.x = atan2f(n.x, n.z) * (1.0f / (2.0f * rr::math::kPi)) + 0.5f;
        out.uv.y = 1.0f - acosf(ny) * (1.0f / rr::math::kPi);
    }
    return out;
}

// Ray-triangle intersection (Moller-Trumbore).
//
// Treats the triangle as double-sided: both back- and front-face hits
// are accepted (the |det| check rejects only edge-parallel rays).
// `normal` is returned as the geometric face normal of the
// counter-clockwise winding order `(v0, v1, v2)` - i.e. the front-face
// outward normal. Callers that want a one-sided result can compare
// `dot(normal, ray.direction)` and discard back-face hits.
//
// `ray.direction` does not need to be unit length; `t` is in the ray's
// own parameter scale.
RR_HD inline rr::renderer::Hit intersect_triangle(const rr::camera::CameraRay& ray,
                                                  rr::math::Vec3 v0,
                                                  rr::math::Vec3 v1,
                                                  rr::math::Vec3 v2,
                                                  float t_min, float t_max) {
    using rr::math::Vec3;
    using rr::math::cross;
    using rr::math::dot;
    using rr::math::normalize;

    rr::renderer::Hit out{};

    const Vec3  e1  = v1 - v0;
    const Vec3  e2  = v2 - v0;
    const Vec3  p   = cross(ray.direction, e2);
    const float det = dot(e1, p);

    // Ray parallel (or near-parallel) to the triangle plane.
    constexpr float kEps = 1.0e-8f;
    if (det > -kEps && det < kEps) return out;

    const float inv_det = 1.0f / det;
    const Vec3  s       = ray.origin - v0;
    const float u       = dot(s, p) * inv_det;
    if (u < 0.0f || u > 1.0f) return out;

    const Vec3  q = cross(s, e1);
    const float v = dot(ray.direction, q) * inv_det;
    if (v < 0.0f || (u + v) > 1.0f) return out;

    const float t = dot(e2, q) * inv_det;
    if (t <= t_min || t >= t_max) return out;

    out.hit      = true;
    out.t        = t;
    out.position = ray.origin + ray.direction * t;
    out.normal   = normalize(cross(e1, e2));
    // Surface the barycentric coordinates so the kernel can
    // interpolate per-vertex attributes (UVs, normals, ...) from
    // the mesh's vertex array. The third coord is implicitly
    // `1 - bary_u - bary_v`. `material_index` stays at -1 (the
    // Hit default); callers populate it from the mesh's
    // `material_id` after the intersection.
    out.bary_u   = u;
    out.bary_v   = v;
    return out;
}

}  // namespace rr::cuda

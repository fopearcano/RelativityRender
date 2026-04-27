#pragma once

// Ray-primitive intersection routines.
//
// Despite the `.cuh` extension this header is host- and device-callable:
// every entry point is `RR_HD inline` and includes only the cross-target
// math / camera / geometry / renderer headers. That lets the host-side
// `geometry_tests` exercise the same code path the GPU kernels run, so
// validating the math on the host validates the device behaviour by
// construction.

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

    out.hit      = true;
    out.t        = t;
    out.position = ray.origin + ray.direction * t;
    // Inverse-radius multiply rather than `normalize`: `radius > 0` is
    // a precondition, and this avoids a redundant `length` call.
    out.normal   = (out.position - sphere.center) * (1.0f / sphere.radius);
    return out;
}

}

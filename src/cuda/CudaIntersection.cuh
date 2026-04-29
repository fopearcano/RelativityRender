#pragma once

// Ray-primitive intersection routines.
//
// Despite the `.cuh` extension this header is host- and device-callable:
// every entry point is `RR_HD inline` and includes only the cross-target
// math / camera / geometry / renderer headers. That lets host tests
// exercise the same code path the GPU kernels run, so validating the
// math on the host validates the device behaviour by construction.
//
// Stage 8 ships only `intersect_sphere`. Triangle intersection joins
// this header in the mesh stage (Module 12 of the master order).

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

}  // namespace rr::cuda

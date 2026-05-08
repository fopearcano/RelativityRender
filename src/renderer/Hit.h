#pragma once

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec2.h"
#include "math/Vec3.h"

namespace rr::renderer {

// Result of a single ray-primitive intersection. Plain data; usable on
// host and device. The renderer extends this in later milestones with
// extra primitive ids; the M11 wiring already needs `material_index`
// so the shader can look up the hit material in the scene's material
// array, and the M16 texture wiring needs `uv` (texture coordinates)
// plus the triangle barycentrics so the shader can interpolate
// per-vertex attributes.
struct Hit {
    bool           hit            = false;  // true iff a valid intersection was found
    float          t              = 0.0f;   // ray parameter at the hit (origin + t*direction)
    rr::math::Vec3 position       = {};     // world-space hit position
    rr::math::Vec3 normal         = {};     // outward unit normal at the hit point
    int            material_index = -1;     // -1 = "no material assigned"; renderer uses defaults

    // Surface UV at the hit point. Triangle hits leave this at the
    // intersection-routine default and rely on the kernel to
    // interpolate from per-vertex UVs using `bary_u` / `bary_v`;
    // sphere hits compute a spherical UV directly.
    rr::math::Vec2 uv             = {};

    // Triangle barycentric coordinates, populated by
    // `intersect_triangle`. The third coord is `1 - bary_u - bary_v`.
    // Unused for non-triangle primitives.
    float          bary_u         = 0.0f;
    float          bary_v         = 0.0f;
};

// `RR_HD` factory for the miss case - explicit, makes intent clear in
// device kernels and host tests alike.
RR_HD inline Hit make_miss() {
    return Hit{};
}

}


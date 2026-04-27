#pragma once

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"

namespace rr::renderer {

// Result of a single ray-primitive intersection. Plain data; usable on
// host and device. The renderer extends this in later milestones with
// extra primitive ids; the M11 wiring already needs `material_index`
// so the shader can look up the hit material in the scene's material
// array.
struct Hit {
    bool           hit            = false;  // true iff a valid intersection was found
    float          t              = 0.0f;   // ray parameter at the hit (origin + t*direction)
    rr::math::Vec3 position       = {};     // world-space hit position
    rr::math::Vec3 normal         = {};     // outward unit normal at the hit point
    int            material_index = -1;     // -1 = "no material assigned"; renderer uses defaults
};

// `RR_HD` factory for the miss case - explicit, makes intent clear in
// device kernels and host tests alike.
RR_HD inline Hit make_miss() {
    return Hit{};
}

}

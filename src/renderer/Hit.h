#pragma once

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"

namespace rr::renderer {

// Result of a single ray-primitive intersection. Plain data; usable on
// host and device. The renderer extends this in later milestones with
// material / primitive ids, but those joins live with the path tracer
// (M14) - keeping `Hit` minimal here avoids leaking later concerns into
// the geometry layer.
struct Hit {
    bool           hit      = false;  // true iff a valid intersection was found
    float          t        = 0.0f;   // ray parameter at the hit (origin + t*direction)
    rr::math::Vec3 position = {};     // world-space hit position
    rr::math::Vec3 normal   = {};     // outward unit normal at the hit point
};

// `RR_HD` factory for the miss case - explicit, makes intent clear in
// device kernels and host tests alike.
RR_HD inline Hit make_miss() {
    return Hit{};
}

}

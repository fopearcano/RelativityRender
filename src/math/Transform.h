#pragma once

#include "math/Vec3.h"

namespace rr::math {

// Local-to-parent transform, the canonical home.
//
// Stores position / Euler rotation / non-uniform scale as plain data.
// Conversion to a 4x4 matrix is deferred to the consumer (GPU upload
// path, scene-file serializer, animation rig) so the data model
// doesn't bake in a math choice (column- vs row-major, Euler order,
// quaternion form) before we know who reads it.
//
// Defaults are the identity transform. `rr::scene::Transform` is a
// type alias for this struct preserved for back-compatibility with
// the scene module's earlier API.
struct Transform {
    rr::math::Vec3 position               = {0.0f, 0.0f, 0.0f};
    rr::math::Vec3 euler_rotation_radians = {0.0f, 0.0f, 0.0f};
    rr::math::Vec3 scale                  = {1.0f, 1.0f, 1.0f};

    static Transform identity() { return Transform{}; }
};

}

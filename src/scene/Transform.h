#pragma once

#include "math/Vec3.h"

namespace rr::scene {

// Local-to-parent transform for a scene object.
//
// Stores position / Euler rotation / non-uniform scale as plain data.
// Conversion to a 4x4 matrix is the consumer's responsibility - the
// scene graph only owns the source-of-truth values, and the matrix
// builder lives next to whichever pass needs it (GPU upload at M10,
// scene file at M13). This avoids bundling a math choice (column- vs
// row-major, Euler order, quaternion form) into the data model
// before we know who consumes it.
//
// Defaults are the identity transform so a default-constructed
// `SceneObject` is positioned at the world origin.
struct Transform {
    rr::math::Vec3 position               = {0.0f, 0.0f, 0.0f};
    rr::math::Vec3 euler_rotation_radians = {0.0f, 0.0f, 0.0f};
    rr::math::Vec3 scale                  = {1.0f, 1.0f, 1.0f};

    static Transform identity() { return Transform{}; }
};

}

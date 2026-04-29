#pragma once

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"

namespace rr::geometry {

// Plain-data sphere primitive, host- and device-readable. Kept as a
// trivial aggregate so it can be passed to kernels by value (no
// constructors, no allocations, no ABI surprises). The full geometry
// system (triangle meshes, instancing, AS-build inputs) lands in M10;
// this is the minimum primitive needed to validate GPU intersection.
//
// `material_index` is the per-sphere index into the scene's material
// array (`-1` means "no material assigned"; the renderer falls back
// to neutral defaults). Aggregate initialization with only `{center,
// radius}` continues to work - the trailing index defaults to -1.
struct Sphere {
    rr::math::Vec3 center;
    float          radius;
    int            material_index = -1;
};

// Convenience factory; usable from host and device. Kept out of the
// struct to preserve the aggregate-init form `Sphere{c, r}` that the
// rest of the code already uses.
RR_HD inline Sphere make_sphere(rr::math::Vec3 center, float radius) {
    return Sphere{center, radius, -1};
}

}

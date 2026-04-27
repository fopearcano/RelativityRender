#pragma once

#include "geometry/Triangle.h"
#include "math/Transform.h"
#include "math/Vec2.h"
#include "math/Vec3.h"

#include <cstddef>
#include <vector>

namespace rr::geometry {

// Per-vertex attributes for a triangle mesh. Position is required;
// `normal` and `uv` default to zero so callers that only have
// positions (raw point data) can still construct a mesh and fill
// the missing attributes later. The attribute set is intentionally
// minimal at this milestone; tangents and vertex colours arrive
// alongside the texture / shading systems (M11 / M16).
struct Vertex {
    rr::math::Vec3 position;
    rr::math::Vec3 normal = {0.0f, 0.0f, 0.0f};
    rr::math::Vec2 uv     = {0.0f, 0.0f};
};

// Indexed triangle mesh. Plain data with a few helpers; the GPU
// upload path (next M10 slice) consumes the contiguous arrays
// directly via `GpuBuffer<Vertex>` and `GpuBuffer<Triangle>`.
//
// `material_id` is an integer index into a scene-side material list;
// `-1` means "use the renderer default". `transform` places the mesh
// in world space - the actual matrix conversion is the consumer's
// responsibility (see `math::Transform`).
struct Mesh {
    std::vector<Vertex>    vertices;
    std::vector<Triangle>  triangles;
    int                    material_id = -1;
    rr::math::Transform    transform;

    [[nodiscard]] std::size_t vertex_count()   const { return vertices.size(); }
    [[nodiscard]] std::size_t triangle_count() const { return triangles.size(); }

    // A mesh is empty when it has no vertices OR no triangles.
    // Either alone is meaningless for rendering.
    [[nodiscard]] bool empty() const;

    // Reset to a default-constructed state.
    void clear();

    // Pre-size the internal vectors. Does not change the reported
    // counts; only avoids reallocations during a known-size build.
    void reserve(std::size_t vertex_capacity, std::size_t triangle_capacity);
};

}

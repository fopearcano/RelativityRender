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
// minimal at this stage; tangents and vertex colours arrive
// alongside the texture / shading systems in their own master-order
// modules.
struct Vertex {
    rr::math::Vec3 position;
    rr::math::Vec3 normal = {0.0f, 0.0f, 0.0f};
    rr::math::Vec2 uv     = {0.0f, 0.0f};
};

// Axis-aligned bounding box. The `min` corner has the smallest x/y/z
// component values among the contained points; `max` has the largest.
// `valid()` is false for the default-constructed (empty) AABB so
// callers can detect "no points" without sentinel values.
struct AABB {
    rr::math::Vec3 min  = {0.0f, 0.0f, 0.0f};
    rr::math::Vec3 max  = {0.0f, 0.0f, 0.0f};
    bool           valid = false;
};

// Indexed triangle mesh. Plain data with a few helpers; the GPU
// upload path (Stage 7B / master module 12 GPU half) consumes the
// contiguous arrays directly via `GpuBuffer<Vertex>` and
// `GpuBuffer<Triangle>`.
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

    // Local-space axis-aligned bounding box of the vertex positions.
    // Returns an `AABB` with `valid = false` when the mesh has no
    // vertices. The transform is intentionally NOT applied here -
    // world-space bounds require a matrix conversion that the
    // `Transform` POD does not commit to. World-space bounds will
    // join when a real consumer (BVH build, frustum cull) needs
    // them.
    [[nodiscard]] AABB local_bounds() const;
};

}

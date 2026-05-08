#include "geometry/Mesh.h"

namespace rr::geometry {

bool Mesh::empty() const {
    return vertices.empty() || triangles.empty();
}

void Mesh::clear() {
    vertices.clear();
    triangles.clear();
    material_id = -1;
    transform   = rr::math::Transform::identity();
}

void Mesh::reserve(std::size_t vertex_capacity, std::size_t triangle_capacity) {
    vertices.reserve(vertex_capacity);
    triangles.reserve(triangle_capacity);
}

AABB Mesh::local_bounds() const {
    AABB b;
    if (vertices.empty()) {
        return b;
    }

    b.min   = vertices.front().position;
    b.max   = b.min;
    b.valid = true;

    for (std::size_t i = 1; i < vertices.size(); ++i) {
        const auto& p = vertices[i].position;
        if (p.x < b.min.x) b.min.x = p.x;
        if (p.y < b.min.y) b.min.y = p.y;
        if (p.z < b.min.z) b.min.z = p.z;
        if (p.x > b.max.x) b.max.x = p.x;
        if (p.y > b.max.y) b.max.y = p.y;
        if (p.z > b.max.z) b.max.z = p.z;
    }

    return b;
}

}

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

}

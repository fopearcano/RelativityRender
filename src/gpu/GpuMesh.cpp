#include "gpu/GpuMesh.h"

namespace rr::gpu {

bool GpuMesh::upload_vertices(const rr::geometry::Vertex* host, std::size_t count) {
    if (count == 0) {
        vertices_.reset();
        vertex_count_ = 0;
        return true;
    }
    if (host == nullptr) {
        return false;
    }
    if (!vertices_.upload(host, count)) {
        vertices_.reset();
        vertex_count_ = 0;
        return false;
    }
    vertex_count_ = count;
    return true;
}

bool GpuMesh::upload_triangles(const rr::geometry::Triangle* host, std::size_t count) {
    if (count == 0) {
        triangles_.reset();
        triangle_count_ = 0;
        return true;
    }
    if (host == nullptr) {
        return false;
    }
    if (!triangles_.upload(host, count)) {
        triangles_.reset();
        triangle_count_ = 0;
        return false;
    }
    triangle_count_ = count;
    return true;
}

void GpuMesh::set_metadata(int material_id, const rr::math::Transform& transform) {
    material_id_ = material_id;
    transform_   = transform;
}

bool GpuMesh::upload_from(const rr::geometry::Mesh& mesh) {
    bool ok = true;
    ok = upload_vertices(mesh.vertices.data(),  mesh.vertices.size())  && ok;
    ok = upload_triangles(mesh.triangles.data(), mesh.triangles.size()) && ok;
    // Metadata is host-only and always safe to write; we set it even
    // on a failed upload so callers can inspect the partial state.
    set_metadata(mesh.material_id, mesh.transform);
    return ok;
}

}

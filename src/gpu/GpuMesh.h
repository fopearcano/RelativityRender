#pragma once

#include "geometry/Mesh.h"        // for Vertex / Triangle / Mesh definitions
#include "geometry/Triangle.h"
#include "gpu/GpuBuffer.h"
#include "math/Transform.h"

#include <cstddef>

namespace rr::gpu {

// Backend-agnostic owner for a single mesh's GPU resources.
//
// Holds two device-resident buffers - one for vertices, one for
// triangle indices - plus the small per-mesh metadata (material id +
// world-space transform). Allocation is dynamic; there is no
// compile-time vertex / triangle cap. The arrays grow / shrink to
// match the host data on every `upload_*` call.
//
// `GpuMesh` is move-only, like `GpuBuffer<T>`. When `RR_HAS_CUDA` is
// not defined the metadata setters and empty uploads still succeed
// (they are pure host writes); non-empty vertex / triangle uploads
// fail predictably and leave the corresponding count at zero, in
// line with the rest of the GPU layer's "honest absence" behaviour.
//
// Kernel integration (a `CudaMeshView` consumer that loops over
// triangles) is the next M10 slice; this milestone only wires the
// upload path.
class GpuMesh {
public:
    GpuMesh() = default;
    ~GpuMesh() = default;

    GpuMesh(const GpuMesh&)            = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&&) noexcept            = default;
    GpuMesh& operator=(GpuMesh&&) noexcept = default;

    // Replace the device vertex array with `count` elements copied
    // from `host`. `count == 0` clears the buffer (always succeeds);
    // a non-zero count requires a working GPU backend.
    bool upload_vertices(const rr::geometry::Vertex* host, std::size_t count);

    // Same as above for the index buffer.
    bool upload_triangles(const rr::geometry::Triangle* host, std::size_t count);

    // Pure host write: store the per-mesh metadata. Always succeeds.
    void set_metadata(int material_id, const rr::math::Transform& transform);

    // Convenience: push vertices, push indices, set metadata. Returns
    // the AND of every step. Metadata is set even when an upload
    // fails so callers can inspect the partial state for debugging.
    bool upload_from(const rr::geometry::Mesh& mesh);

    [[nodiscard]] std::size_t          vertex_count()   const noexcept { return vertex_count_; }
    [[nodiscard]] std::size_t          triangle_count() const noexcept { return triangle_count_; }
    [[nodiscard]] int                  material_id()    const noexcept { return material_id_; }
    [[nodiscard]] const rr::math::Transform& transform() const noexcept { return transform_; }

    // True iff both arrays have at least one element. A vertex-only
    // or triangle-only upload is not renderable.
    [[nodiscard]] bool has_data() const noexcept {
        return vertex_count_ > 0 && triangle_count_ > 0;
    }

    // Device pointers for the renderer to read. Both return nullptr
    // when no upload has happened.
    [[nodiscard]] const rr::geometry::Vertex*   device_vertices()  const noexcept {
        return vertices_.device_ptr();
    }
    [[nodiscard]] const rr::geometry::Triangle* device_triangles() const noexcept {
        return triangles_.device_ptr();
    }

private:
    rr::gpu::GpuBuffer<rr::geometry::Vertex>   vertices_;
    rr::gpu::GpuBuffer<rr::geometry::Triangle> triangles_;
    std::size_t                                vertex_count_   = 0;
    std::size_t                                triangle_count_ = 0;
    int                                        material_id_    = -1;
    rr::math::Transform                        transform_{};
};

}

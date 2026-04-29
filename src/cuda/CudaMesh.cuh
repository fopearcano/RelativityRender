#pragma once

// CUDA-side view of an `rr::gpu::GpuMesh`. Only safe to include from
// `.cu` files because it pulls in `cuda_runtime.h`.
//
// `CudaMeshView` is the launch-argument POD that the eventual mesh
// kernel will receive by value. It carries device pointers to the
// vertex / index buffers, the array sizes, the material id, and the
// world transform of the mesh. Defined here as a stable contract
// even though no kernel consumes it yet (per the milestone rule:
// "no rendering yet, no BVH yet"). When the kernel slice lands the
// signature does not have to change.
//
// Future revisions will likely move from a single `CudaMeshView` to
// a small array of them inside `CudaSceneView` so a scene's meshes
// can be iterated by the kernel; that is intentionally out of scope
// here.

#include <cuda_runtime.h>

#include "geometry/Mesh.h"        // for rr::geometry::Vertex
#include "geometry/Triangle.h"
#include "math/Transform.h"

namespace rr::cuda {

struct CudaMeshView {
    const rr::geometry::Vertex*   vertices       = nullptr;  // device pointer
    const rr::geometry::Triangle* triangles      = nullptr;  // device pointer
    int                           vertex_count   = 0;
    int                           triangle_count = 0;
    int                           material_id    = -1;
    rr::math::Transform           transform;                 // identity by default
};

}

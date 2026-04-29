#pragma once

// CUDA-side view of `rr::gpu::GpuScene`. Only safe to include from
// `.cu` files because it pulls in `<cuda_runtime.h>`.
//
// `CudaSceneView` is the launch-argument POD the renderer kernel
// receives by value. It carries the camera / observer / relativity
// parameters directly (small PODs that fit in the launch-argument
// buffer) and a device pointer + count for the sphere array.
//
// Future entity types (light buffers, texture views, material array)
// join this struct so the kernel signature does not change with
// every new module. Stage 7C extends the view with a single mesh
// slot (`CudaMeshView`); multi-mesh support is a future slice.

#include "camera/CameraRay.h"
#include "cuda/CudaMesh.cuh"
#include "geometry/Sphere.h"
#include "relativity/RelativityParams.h"

#include <cuda_runtime.h>

namespace rr::cuda {

struct CudaSceneView {
    rr::camera::GpuCamera             camera;
    rr::relativity::Observer          observer;
    rr::relativity::RelativityParams  params;
    const rr::geometry::Sphere*       spheres      = nullptr;  // device pointer
    int                               sphere_count = 0;

    // Single-mesh slot. `mesh.triangle_count == 0` means "no mesh
    // contributes triangles to this scene". Multi-mesh support
    // promotes this to an array (or device-resident handle list)
    // in a later slice.
    CudaMeshView                      mesh;
};

}

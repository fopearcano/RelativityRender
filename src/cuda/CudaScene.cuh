#pragma once

// CUDA-side view of `rr::gpu::GpuScene`. Only safe to include from
// `.cu` files because it pulls in `<cuda_runtime.h>`.
//
// `CudaSceneView` is the launch-argument POD the renderer kernel
// receives by value. It carries the camera / observer / relativity
// parameters directly (small PODs that fit in the launch-argument
// buffer) and a device pointer + count for the sphere array.
//
// Future entity types (mesh handles, light buffers, texture views,
// material array) join this struct so the kernel signature does not
// change with every new module. Stage 6B keeps it minimal: camera +
// relativity + spheres only.

#include "camera/CameraRay.h"
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
};

}

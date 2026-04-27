#pragma once

// CUDA-side view of a `rr::gpu::GpuScene`. Only safe to include from
// `.cu` files because it pulls in `cuda_runtime.h`.
//
// `CudaSceneView` is the launch-argument POD the renderer kernel
// receives by value. It carries the camera / observer / relativity
// parameters directly and a device pointer + count for the sphere
// array. Future entity types (mesh handles, light buffers) join the
// same struct so the kernel signature does not change with every
// new module.

#include <cuda_runtime.h>

#include "camera/CameraRay.h"
#include "geometry/Sphere.h"
#include "relativity/RelativityParams.h"

namespace rr::cuda {

struct CudaSceneView {
    rr::camera::GpuCamera             camera;
    rr::relativity::Observer          observer;
    rr::relativity::RelativityParams  params;
    const rr::geometry::Sphere*       spheres       = nullptr;  // device pointer
    int                               sphere_count  = 0;
};

// Host-callable launch wrapper for the scene-render kernel.
// Defined in CudaTestKernel.cu. Per pixel, the kernel:
//   1. generate_camera_ray(scene.camera, ...);
//   2. (if params.enable_aberration) aberrateDirection;
//   3. closest-hit loop over scene.spheres / scene.sphere_count;
//   4. base shade (normal-as-color on hit, sky on miss);
//   5. (if params.enable_doppler) applyDopplerColor;
//   6. (if params.enable_searchlight) D^4 boost (lerp-mixed by
//      params.searchlight_strength);
//   7. framebuffer write.
// All per-ray work is on the GPU; the host only configures + launches.
void launch_render_scene(float* device_pixels, int width, int height,
                         CudaSceneView scene,
                         cudaStream_t stream = 0);

}

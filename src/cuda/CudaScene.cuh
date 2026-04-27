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
#include "cuda/CudaLight.cuh"
#include "cuda/CudaMaterial.cuh"
#include "cuda/CudaMesh.cuh"
#include "geometry/Sphere.h"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
#include "relativity/RelativityParams.h"

namespace rr::cuda {

struct CudaSceneView {
    rr::camera::GpuCamera             camera;
    rr::relativity::Observer          observer;
    rr::relativity::RelativityParams  params;
    const rr::geometry::Sphere*       spheres       = nullptr;  // device pointer
    int                               sphere_count  = 0;

    // Single-mesh slot. `mesh.triangle_count == 0` means "no mesh
    // contributes triangles to this scene". Multi-mesh support
    // promotes this to an array (or device-resident handle list)
    // alongside the M11 material system; a single slot is enough to
    // demonstrate the M10 closest-hit logic across primitive types.
    CudaMeshView                      mesh;

    // Material array. Sphere `material_index` and `mesh.material_id`
    // are integer indices into `materials[0 .. material_count - 1]`;
    // values outside that range fall back to a neutral default in
    // the kernel. `nullptr` + `material_count == 0` is allowed and
    // means "no materials uploaded - everything uses the default".
    const rr::material::MaterialParams* materials      = nullptr;
    int                                  material_count = 0;

    // Light array. Iterated per hit (and once per pixel for the
    // environment-fallback colour). `nullptr` + `light_count == 0`
    // is allowed and means "no lights uploaded - the kernel
    // falls back to its default ambient + sky-gradient response".
    const rr::lighting::Light*           lights         = nullptr;
    int                                  light_count    = 0;
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

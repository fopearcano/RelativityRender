#pragma once

// CUDA-side view of `rr::gpu::GpuScene`. Only safe to include from
// `.cu` files because it pulls in `<cuda_runtime.h>`.
//
// `CudaSceneView` is the launch-argument POD the renderer kernel
// receives by value. It carries the camera / observer / relativity
// parameters directly (small PODs that fit in the launch-argument
// buffer) and a device pointer + count for the sphere array.
//
// Future entity types (texture views) join this struct so the
// kernel signature does not change with every new module. Stage 9B
// extends the view with the lights array; multi-mesh + texture
// support is a future slice.

#include "camera/CameraRay.h"
#include "cuda/CudaMesh.cuh"
#include "cuda/CudaTexture.cuh"   // DeviceTextureView for the texture array slot
#include "geometry/Sphere.h"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
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

    // Material array. `Sphere::material_index` and `Mesh::material_id`
    // are integer indices into `materials[0 .. material_count - 1]`;
    // values outside that range fall back to a neutral default in
    // the kernel. `nullptr` + `material_count == 0` is allowed and
    // means "no materials uploaded - everything uses the default".
    const rr::material::MaterialParams* materials      = nullptr;
    int                                 material_count = 0;

    // Light array. Iterated per hit (Stage 9B reads it without
    // shadow-ray visibility tests; shadows land later). `nullptr`
    // + `light_count == 0` is allowed and means "no lights
    // uploaded - the kernel falls through to the facing-ratio
    // shade for backwards compatibility with the unlit
    // diagnostics".
    const rr::lighting::Light*        lights        = nullptr;
    int                               light_count   = 0;

    // Texture array. Stage 13B.3 wiring (master order #18). The
    // kernel reads `textures[mat.baseColorTextureId]` whenever
    // `mat.useBaseColorTexture` is true and the id is in
    // `[0, texture_count)`; otherwise it falls back to
    // `mat.baseColor`. `nullptr` + `texture_count == 0` is
    // allowed and means "no textures uploaded - every material
    // uses its flat baseColor", preserving backward compatibility
    // with every existing CLI action.
    const rr::cuda::DeviceTextureView* textures      = nullptr;
    int                                texture_count = 0;
};

}

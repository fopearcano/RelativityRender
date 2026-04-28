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
#include "cuda/CudaAOV.cuh"
#include "cuda/CudaLight.cuh"
#include "cuda/CudaMaterial.cuh"
#include "cuda/CudaMaterialGraph.cuh"
#include "cuda/CudaMesh.cuh"
#include "cuda/CudaTexture.cuh"
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

    // Texture array. `MaterialParams::base_color_texture_id` indexes
    // into this list; valid ids are in
    // `[0, texture_count)`. `nullptr` + `texture_count == 0` is
    // allowed and means "no textures uploaded - materials always
    // use their constant `baseColor`".
    const rr::cuda::TextureView*         textures       = nullptr;
    int                                  texture_count  = 0;

    // M21 material graphs. One `CudaMaterialGraphView` per
    // material id; the kernel reads
    // `material_graph_views[material_index]` and runs
    // `evaluateMaterial(view)` to produce the per-hit
    // baseColor / emissionColor / emissionStrength. Valid
    // ids are in `[0, material_graph_view_count)`. `nullptr`
    // + `material_graph_view_count == 0` is allowed and
    // means "no graph upload happened - the kernel falls
    // back to the legacy MaterialParams reads".
    const rr::cuda::CudaMaterialGraphView* material_graph_views = nullptr;
    int                                    material_graph_view_count = 0;
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

// Host-callable launch wrapper for the path-tracer kernel. Defined
// in CudaTestKernel.cu. Per pixel: traces `spp` independent paths
// with cosine-weighted Lambertian bounces up to `max_depth`,
// accumulates emission + environment-fallback radiance, applies
// relativistic Doppler / searchlight to the integrated value, and
// writes the average to the framebuffer. The accumulation buffer
// is per-thread (a Vec3 register sum); the framebuffer holds the
// final mean. `seed_offset` is added to the per-sample RNG seed so
// callers can advance it between progressive launches.
void launch_path_trace(float* device_pixels, int width, int height,
                       CudaSceneView scene,
                       int spp, int max_depth,
                       unsigned int seed_offset,
                       cudaStream_t stream = 0);

// M17 AOV launcher. Same shading pipeline as `launch_render_scene`,
// but instead of writing only the beauty buffer the kernel pokes
// each requested AOV slot in `aov_pack` at the appropriate stage:
//   - Albedo            : raw base colour (post-texture sample,
//                         before lighting + relativity).
//   - Normal            : `0.5*N + 0.5` for the closest hit (sky on
//                         miss).
//   - Depth             : ray `t` for the closest hit (0 on miss).
//   - DopplerFactor     : raw `D` from the primary photon direction.
//   - SearchlightFactor : `D^4` from the same `D`.
//   - Beauty            : final shaded + relativity-applied colour.
// Pointers left null in `aov_pack` cause the corresponding AOV
// slot to be skipped.
void launch_render_aovs(int width, int height,
                        CudaSceneView scene,
                        CudaAOVPack   aov_pack,
                        cudaStream_t  stream = 0);

}

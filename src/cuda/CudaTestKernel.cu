// CUDA diagnostic kernels.
//
// Future kernels (path tracing, AOVs, ...) join this file - or
// sibling .cu files - in their own dedicated stages. Today the
// device runs:
//   - k_gradient_rgba32f       (Stage 6)
//   - k_camera_rays_visualize  (Stage 7)
//   - k_sphere_visualize       (Stage 8)
//   - k_sphere_relativistic    (Stage 10)
//   - k_render_scene           (Stage 6B - multi-sphere over CudaSceneView)

#include "cuda/CudaIntersection.cuh"
#include "cuda/CudaKernels.cuh"
#include "cuda/CudaScene.cuh"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
#include "relativity/RelativityMath.cuh"

#include "math/Vec3.h"
#include "renderer/Hit.h"

#include <cmath>  // sqrtf for the point-light distance / falloff

namespace rr::cuda {

// ---------- Stage 6: UV gradient ----------

namespace {

// One thread = one pixel. Output is the Rgba32F layout used by
// rr::image::Image (interleaved, top-left origin, row stride
// width * 4 floats). The gradient is computed entirely on the
// device - no CPU pixel loop touches this data.
__global__ void k_gradient_rgba32f(float* pixels, int width, int height) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const float u = (width  > 1) ? float(x) / float(width  - 1) : 0.0f;
    const float v = (height > 1) ? float(y) / float(height - 1) : 0.0f;

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = u;
    pixels[idx + 1] = v;
    pixels[idx + 2] = 0.0f;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_gradient_rgba32f<<<grid, block, 0, stream>>>(device_pixels, width, height);
}

// ---------- Stage 7: Camera-ray visualisation ----------

namespace {

// One thread = one pixel. The GPU generates the primary pinhole ray
// via the same `RR_HD generate_camera_ray` helper that host tests
// run, then encodes the (already-normalised) ray direction as RGB by
// mapping each component from [-1, 1] to [0, 1]. Alpha is 1. The
// kernel does all per-pixel work; the host never touches per-ray
// state.
__global__ void k_camera_rays_visualize(float* pixels, int width, int height,
                                        rr::camera::GpuCamera cam) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const auto ray = rr::camera::generate_camera_ray(cam, x, y, width, height);

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = 0.5f * ray.direction.x + 0.5f;
    pixels[idx + 1] = 0.5f * ray.direction.y + 0.5f;
    pixels[idx + 2] = 0.5f * ray.direction.z + 0.5f;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

void launch_camera_rays_visualize(float* device_pixels, int width, int height,
                                  rr::camera::GpuCamera cam,
                                  cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_camera_rays_visualize<<<grid, block, 0, stream>>>(device_pixels,
                                                         width, height, cam);
}

// ---------- Stage 8: Single-sphere intersection ----------

namespace {

// One thread = one pixel. The GPU generates the primary pinhole ray,
// runs `intersect_sphere` against the supplied `sphere` POD, and
// writes the framebuffer.
//
// Hit:  shade with `0.5*n + 0.5` (the canonical "normal as color"
//       diagnostic; reveals geometry without committing to a material
//       system).
// Miss: a simple vertical sky gradient parameterised by the ray's
//       y-direction. Lerp from white at the horizon (y=0) to a soft
//       blue at zenith (y=1), so the image does not collapse to a
//       single colour outside the sphere.
//
// The host never touches per-pixel state.
__global__ void k_sphere_visualize(float* pixels, int width, int height,
                                   rr::camera::GpuCamera cam,
                                   rr::geometry::Sphere  sphere) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const auto ray = rr::camera::generate_camera_ray(cam, x, y, width, height);
    const auto hit = rr::cuda::intersect_sphere(ray, sphere,
                                                /*t_min=*/0.0f,
                                                /*t_max=*/1.0e30f);

    float r, g, b;
    if (hit.hit) {
        // Normal-as-color shade. Each component is in [-1, 1] -> map
        // to [0, 1].
        r = 0.5f * hit.normal.x + 0.5f;
        g = 0.5f * hit.normal.y + 0.5f;
        b = 0.5f * hit.normal.z + 0.5f;
    } else {
        // Sky gradient: white at horizon, soft blue overhead. `t` is
        // remapped from [-1, 1] to [0, 1] so it works regardless of
        // ray normalisation by the camera.
        const float t = 0.5f * (ray.direction.y + 1.0f);
        r = (1.0f - t) * 1.0f + t * 0.5f;
        g = (1.0f - t) * 1.0f + t * 0.7f;
        b = (1.0f - t) * 1.0f + t * 1.0f;
    }

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = r;
    pixels[idx + 1] = g;
    pixels[idx + 2] = b;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

void launch_sphere_visualize(float* device_pixels, int width, int height,
                             rr::camera::GpuCamera   cam,
                             rr::geometry::Sphere    sphere,
                             cudaStream_t            stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_sphere_visualize<<<grid, block, 0, stream>>>(device_pixels,
                                                    width, height,
                                                    cam, sphere);
}

// ---------- Stage 10: Relativistic single-sphere render ----------

namespace {

// One thread = one pixel. Runs the full relativistic perception
// pipeline on the device:
//   1. Generate the primary camera ray.
//   2. (If enabled) Lorentz-aberrate the ray direction in the
//      observer's frame.
//   3. Intersect the (possibly aberrated) ray against `sphere`.
//   4. Base shade: 0.5 * normal + 0.5 on hit; vertical sky gradient
//      on miss.
//   5. Compute the Doppler factor D for the (possibly aberrated)
//      ray direction once and reuse it.
//   6. (If enabled) apply the artistic Doppler colour shift.
//   7. (If enabled) scale by `1 + (D^4 - 1) * searchlight_strength`
//      to model relativistic beaming. The lerp form keeps the
//      strength knob a true [0, 1] dial: 0 -> no beaming,
//      1 -> full D^4.
//   8. Write framebuffer.
__global__ void k_sphere_relativistic(float* pixels, int width, int height,
                                      rr::camera::GpuCamera           cam,
                                      rr::relativity::Observer        observer,
                                      rr::relativity::RelativityParams params,
                                      rr::geometry::Sphere            sphere) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    using rr::math::Vec3;

    // Stage 18A.3: precompute the launch-invariant relativity
    // scalars once per thread. `aberrateDirection` and
    // `dopplerFactor` both consume `length(beta_vec)` and
    // `gamma(beta_mag)`; without this snapshot both functions
    // would recompute them, paying four `sqrt`s per pixel for
    // values that depend only on the per-launch observer
    // velocity.
    const auto rel = rr::relativity::precompute_relativity(observer.velocity);

    // 1. Camera ray.
    auto ray = rr::camera::generate_camera_ray(cam, x, y, width, height);

    // 2. Aberration.
    if (params.enable_aberration) {
        ray.direction = rr::relativity::aberrateDirection(rel,
                                                          ray.direction);
    }

    // 3. Sphere intersection.
    const auto hit = rr::cuda::intersect_sphere(ray, sphere,
                                                /*t_min=*/0.0f,
                                                /*t_max=*/1.0e30f);

    // 4. Base shade.
    Vec3 color;
    if (hit.hit) {
        color = Vec3{0.5f * hit.normal.x + 0.5f,
                     0.5f * hit.normal.y + 0.5f,
                     0.5f * hit.normal.z + 0.5f};
    } else {
        const float t = 0.5f * (ray.direction.y + 1.0f);
        color = Vec3{(1.0f - t) * 1.0f + t * 0.5f,
                     (1.0f - t) * 1.0f + t * 0.7f,
                     (1.0f - t) * 1.0f + t * 1.0f};
    }

    // 5. Doppler factor for the (possibly aberrated) photon
    //    direction in the scene frame. Computed once and reused.
    const float D = rr::relativity::dopplerFactor(rel, ray.direction);

    // 6. Doppler colour shift (artistic approximation).
    if (params.enable_doppler) {
        color = rr::relativity::applyDopplerColor(color, D,
                                                  params.doppler_color_strength);
    }

    // 7. Searchlight / relativistic beaming.
    if (params.enable_searchlight) {
        const float D4    = rr::relativity::searchlightFactor(D);
        const float scale = 1.0f + (D4 - 1.0f) * params.searchlight_strength;
        color = color * scale;
    }

    // 8. Framebuffer write.
    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = color.x;
    pixels[idx + 1] = color.y;
    pixels[idx + 2] = color.z;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

void launch_sphere_relativistic(float* device_pixels, int width, int height,
                                rr::camera::GpuCamera           cam,
                                rr::relativity::Observer        observer,
                                rr::relativity::RelativityParams params,
                                rr::geometry::Sphere            sphere,
                                cudaStream_t                    stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_sphere_relativistic<<<grid, block, 0, stream>>>(
        device_pixels, width, height, cam, observer, params, sphere);
}

// ---------- Stage 6B: multi-sphere scene render ------------------

namespace {

// One thread = one pixel. Same eight-step relativistic pipeline as
// k_sphere_relativistic, but the closest-hit step is a loop over
// the uploaded sphere array instead of a single primitive. The
// per-frame state (camera, observer, params) and the device pointer
// + count for the sphere array travel together inside `scene`,
// passed by value as the launch argument.
__global__ void k_render_scene(float* pixels, int width, int height,
                               rr::cuda::CudaSceneView scene) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    using rr::math::Vec3;

    // Stage 18A.3: snapshot the per-launch relativity invariants
    // (|beta|, gamma) once per thread. The aberration step (§2)
    // and the Doppler-factor step (§5) both used to recompute
    // these via internal `length` + `gamma` calls; the precompute
    // halves the per-pixel `sqrt` count through the relativity
    // stack and shortens the dependent chain through the kernel.
    const auto rel = rr::relativity::precompute_relativity(
        scene.observer.velocity);

    // 1. Camera ray.
    auto ray = rr::camera::generate_camera_ray(scene.camera,
                                               x, y, width, height);

    // 2. Aberration in the observer's frame.
    if (scene.params.enable_aberration) {
        ray.direction = rr::relativity::aberrateDirection(
            rel, ray.direction);
    }

    // 3. Closest-hit loop. `t_max` tightens as candidates are
    //    accepted so each later candidate only needs to beat the
    //    running best. Sphere and triangle primitives compete for
    //    the same nearest-hit slot.
    rr::renderer::Hit best;
    float             t_max = 1.0e30f;

    for (int i = 0; i < scene.sphere_count; ++i) {
        const auto h = rr::cuda::intersect_sphere(ray, scene.spheres[i],
                                                  /*t_min=*/0.0f, t_max);
        if (h.hit) {
            best  = h;
            t_max = h.t;
        }
    }

    // Naive triangle loop. Vertex transforms are not applied at this
    // stage: vertex positions are taken as-is from the uploaded
    // buffer (effectively world-space). Per-mesh transforms join
    // alongside the material system in a later stage.
    const auto& mesh = scene.mesh;
    for (int i = 0; i < mesh.triangle_count; ++i) {
        const auto tri = mesh.triangles[i];
        const auto v0  = mesh.vertices[tri.v0].position;
        const auto v1  = mesh.vertices[tri.v1].position;
        const auto v2  = mesh.vertices[tri.v2].position;
        const auto h   = rr::cuda::intersect_triangle(ray, v0, v1, v2,
                                                      /*t_min=*/0.0f, t_max);
        if (h.hit) {
            best                = h;
            best.material_index = mesh.material_id;
            // Stage 13B.3: interpolate per-vertex UVs at the hit
            // point so a textured material can sample at the
            // correct surface coordinate. The triangle's
            // barycentric weights are populated by
            // `intersect_triangle`; the third weight is implicit.
            const float w0 = 1.0f - h.bary_u - h.bary_v;
            const auto uv0 = mesh.vertices[tri.v0].uv;
            const auto uv1 = mesh.vertices[tri.v1].uv;
            const auto uv2 = mesh.vertices[tri.v2].uv;
            best.uv = uv0 * w0 + uv1 * h.bary_u + uv2 * h.bary_v;
            t_max               = h.t;
        }
    }

    // 4. Base shade.
    //    Hit:  read MaterialParams from scene.materials[best.material_index]
    //          if in range, else fall back to the neutral default.
    //          Then either:
    //            (a) evaluate direct lighting from scene.lights when
    //                light_count > 0 (point + directional contributions
    //                summed without shadows; environment lights
    //                contribute as ambient; emission added on top), OR
    //            (b) fall through to the Stage 8B facing-ratio shade
    //                when no lights are uploaded, so the unlit demos
    //                (--render-scene / --render-mesh-scene /
    //                --render-material-scene) still produce visible
    //                output.
    //    Miss: vertical sky gradient (unchanged).
    // Stage 14A.3: hoist `albedo` out of the hit branch so the
    // albedo AOV can read it on miss as well (sentinel zero); the
    // existing hit-side albedo computation is unchanged.
    Vec3 albedo   = Vec3{0.0f, 0.0f, 0.0f};
    Vec3 color;
    if (best.hit) {
        rr::math::Vec3 emission = rr::math::Vec3{0.0f, 0.0f, 0.0f};
        // Pre-14A.3 default for hits with no material assigned.
        albedo = Vec3{0.8f, 0.8f, 0.8f};

        if (best.material_index >= 0
         && best.material_index < scene.material_count
         && scene.materials != nullptr) {
            const auto& mat = scene.materials[best.material_index];

            // Stage 13B.3: when the material has a texture binding
            // and the id is in range, sample at the hit's UV;
            // otherwise fall back to the flat baseColor. Out-of-
            // range ids deliberately fall back rather than
            // resolving to the safe-fallback magenta inside
            // `sampleTextureNearest` - a kernel-level guard keeps
            // the failure mode "use baseColor" rather than "render
            // magenta", which is gentler for authoring mistakes.
            if (mat.useBaseColorTexture
             && mat.baseColorTextureId >= 0
             && mat.baseColorTextureId < scene.texture_count
             && scene.textures != nullptr) {
                albedo = rr::cuda::sampleTextureNearest(
                    scene.textures[mat.baseColorTextureId], best.uv);
            } else {
                albedo = mat.baseColor;
            }
            emission = mat.emissionColor * mat.emissionStrength;
        }

        if (scene.light_count > 0 && scene.lights != nullptr) {
            // Direct lighting evaluation. No shadows yet (Stage 9B
            // explicitly defers visibility tests); each light's
            // contribution is added unconditionally.
            Vec3 direct  = Vec3{0.0f, 0.0f, 0.0f};
            Vec3 ambient = Vec3{0.0f, 0.0f, 0.0f};
            bool has_env = false;

            for (int li = 0; li < scene.light_count; ++li) {
                const rr::lighting::Light& L = scene.lights[li];
                const Vec3 light_color = L.color * L.intensity;

                switch (L.type) {
                    case rr::lighting::LightType::Directional: {
                        // Photons travel along `L.direction`; "to-light"
                        // is its negation.
                        const Vec3  to_light = -L.direction;
                        const float ndotl    = rr::math::dot(best.normal,
                                                             to_light);
                        const float lambert  = ndotl > 0.0f ? ndotl : 0.0f;
                        direct = direct + light_color * lambert;
                        break;
                    }
                    case rr::lighting::LightType::Point: {
                        const Vec3  delta = L.position - best.position;
                        const float d2    = rr::math::dot(delta, delta);
                        // Inverse-square falloff with an epsilon
                        // floor so pixels near the singular point
                        // do not blow up.
                        const float falloff_inv = (d2 > 1.0e-4f) ? d2 : 1.0e-4f;
                        const float dist        = sqrtf(falloff_inv);
                        const Vec3  to_light    = delta * (1.0f / dist);
                        const float ndotl       = rr::math::dot(best.normal,
                                                                to_light);
                        const float lambert     = ndotl > 0.0f ? ndotl : 0.0f;
                        direct = direct + light_color * (lambert / falloff_inv);
                        break;
                    }
                    case rr::lighting::LightType::Environment: {
                        ambient = ambient + light_color;
                        has_env = true;
                        break;
                    }
                    case rr::lighting::LightType::Area:
                        // PLACEHOLDER. Real area-light sampling
                        // joins the path tracer; ignored at this
                        // stage.
                        break;
                }
            }

            // Implicit ambient floor when no Environment light is
            // present, so a scene with only point / directional
            // lights does not collapse to black at glancing angles.
            if (!has_env) {
                ambient = ambient + Vec3{0.05f, 0.05f, 0.05f};
            }

            color = albedo * (direct + ambient) + emission;
        } else {
            // No lights uploaded: keep the Stage 8B facing-ratio
            // shade so existing unlit CLI actions still produce a
            // recognisable image.
            const float ndotv   = rr::math::dot(best.normal, -ray.direction);
            const float facing  = ndotv > 0.0f ? ndotv : 0.0f;
            constexpr float kAmbient = 0.15f;
            const float shade   = kAmbient + (1.0f - kAmbient) * facing;
            color = albedo * shade + emission;
        }
    } else {
        const float t = 0.5f * (ray.direction.y + 1.0f);
        color = Vec3{(1.0f - t) * 1.0f + t * 0.5f,
                     (1.0f - t) * 1.0f + t * 0.7f,
                     (1.0f - t) * 1.0f + t * 1.0f};
    }

    // 5. Doppler factor for the (possibly aberrated) ray direction.
    //    Uses the Stage 18A.3 precomputed snapshot so the per-pixel
    //    cost is a `dot` + a few flops, not another `sqrt` pair.
    const float D = rr::relativity::dopplerFactor(rel, ray.direction);

    // 6. Doppler colour shift.
    if (scene.params.enable_doppler) {
        color = rr::relativity::applyDopplerColor(
            color, D, scene.params.doppler_color_strength);
    }

    // 7. Searchlight / beaming. Stage 14A.3: compute D^4 unconditionally
    // so the searchlight_factor AOV always sees the raw physical value
    // regardless of whether the beauty pass actually applies the
    // beaming scale.
    const float D4 = rr::relativity::searchlightFactor(D);
    if (scene.params.enable_searchlight) {
        const float scale = 1.0f + (D4 - 1.0f) * scene.params.searchlight_strength;
        color = color * scale;
    }

    // 8. Framebuffer write.
    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = color.x;
    pixels[idx + 1] = color.y;
    pixels[idx + 2] = color.z;
    pixels[idx + 3] = 1.0f;

    // 9. Stage 14A.3: per-pixel AOV writes. Each `scene.aovs.*`
    // pointer is null when the corresponding pass is not requested
    // for this launch; the kernel skips its write. Indexing matches
    // the host-side `GpuAOVBuffer`'s `width * height *
    // component_count` layout: `pix_idx_3` for 3-channel passes,
    // `pix_idx_1` for 1-channel passes.
    //
    // Encoding choices (see `cuda/CudaAOV.cuh`'s header comment):
    //   - normal: `0.5 * n + 0.5` for hits so the saved PPM is
    //     directly viewable without a CPU remap; (0, 0, 0) on
    //     miss.
    //   - depth:  `1.0 / (1.0 + t)` for hits so closer surfaces
    //     are brighter and the value is bounded in [0, 1] for
    //     PPM viewing; 0 on miss.
    //   - other passes: raw values.
    const int pix_idx_1 = y * width + x;
    const int pix_idx_3 = pix_idx_1 * 3;

    if (scene.aovs.beauty != nullptr) {
        scene.aovs.beauty[pix_idx_3 + 0] = color.x;
        scene.aovs.beauty[pix_idx_3 + 1] = color.y;
        scene.aovs.beauty[pix_idx_3 + 2] = color.z;
    }
    if (scene.aovs.normal != nullptr) {
        const Vec3 n_enc = best.hit
            ? Vec3{best.normal.x * 0.5f + 0.5f,
                   best.normal.y * 0.5f + 0.5f,
                   best.normal.z * 0.5f + 0.5f}
            : Vec3{0.0f, 0.0f, 0.0f};
        scene.aovs.normal[pix_idx_3 + 0] = n_enc.x;
        scene.aovs.normal[pix_idx_3 + 1] = n_enc.y;
        scene.aovs.normal[pix_idx_3 + 2] = n_enc.z;
    }
    if (scene.aovs.depth != nullptr) {
        const float depth_vis =
            best.hit ? (1.0f / (1.0f + best.t)) : 0.0f;
        scene.aovs.depth[pix_idx_1] = depth_vis;
    }
    if (scene.aovs.albedo != nullptr) {
        scene.aovs.albedo[pix_idx_3 + 0] = albedo.x;
        scene.aovs.albedo[pix_idx_3 + 1] = albedo.y;
        scene.aovs.albedo[pix_idx_3 + 2] = albedo.z;
    }
    if (scene.aovs.doppler_factor != nullptr) {
        scene.aovs.doppler_factor[pix_idx_1] = D;
    }
    if (scene.aovs.searchlight_factor != nullptr) {
        scene.aovs.searchlight_factor[pix_idx_1] = D4;
    }
}

}  // namespace

void launch_render_scene(float* device_pixels, int width, int height,
                         rr::cuda::CudaSceneView scene,
                         cudaStream_t            stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_render_scene<<<grid, block, 0, stream>>>(
        device_pixels, width, height, scene);
}

}  // namespace rr::cuda

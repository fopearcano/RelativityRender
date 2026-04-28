#include "cuda/CudaIntersection.cuh"
#include "cuda/CudaKernels.cuh"
#include "cuda/CudaLight.cuh"
#include "cuda/CudaMaterial.cuh"
#include "cuda/CudaScene.cuh"
#include "lighting/Light.h"
#include "material/MaterialTypes.h"
#include "math/Vec3.h"
#include "pathtracer/RNG.cuh"
#include "pathtracer/Sampling.cuh"
#include "relativity/RelativityMath.cuh"
#include "renderer/Hit.h"

#include <cmath>  // sqrtf is host- and device-callable

namespace rr::cuda {

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

}

void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_gradient_rgba32f<<<grid, block, 0, stream>>>(device_pixels, width, height);
}

namespace {

// Generates a primary ray per pixel and encodes the (normalised)
// direction as RGB by mapping each component from [-1, 1] to [0, 1].
// Alpha is 1. The kernel does all per-pixel work; the host only
// launches and downloads.
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

}

void launch_camera_rays_visualize(float* device_pixels, int width, int height,
                                  rr::camera::GpuCamera cam,
                                  cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_camera_rays_visualize<<<grid, block, 0, stream>>>(device_pixels, width, height, cam);
}

namespace {

// Generate primary ray, intersect against the sphere, shade. The whole
// per-pixel pipeline runs on the device; the host only launches and
// downloads.
//
// Hit  -> RGB encodes the outward normal (`0.5*n + 0.5`).
// Miss -> simple vertical sky gradient parameterised by `dir.y`.
__global__ void k_sphere_visualize(float* pixels, int width, int height,
                                   rr::camera::GpuCamera cam,
                                   rr::geometry::Sphere  sphere) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const auto ray = rr::camera::generate_camera_ray(cam, x, y, width, height);

    // Generous t range; M14 will replace this with proper ray epsilons
    // and a real far plane, but for a single primitive any positive
    // distance is fine.
    const auto hit = rr::cuda::intersect_sphere(ray, sphere, /*t_min=*/0.0f,
                                                /*t_max=*/1.0e30f);

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (hit.hit) {
        r = 0.5f * hit.normal.x + 0.5f;
        g = 0.5f * hit.normal.y + 0.5f;
        b = 0.5f * hit.normal.z + 0.5f;
    } else {
        // dir.y is in [-1, 1]; map to [0, 1] for a top-light, dark-floor
        // sky reminiscent of an open studio backdrop.
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

}

void launch_sphere_visualize(float* device_pixels, int width, int height,
                             rr::camera::GpuCamera cam,
                             rr::geometry::Sphere  sphere,
                             cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_sphere_visualize<<<grid, block, 0, stream>>>(device_pixels, width, height,
                                                   cam, sphere);
}

namespace {

// Composes the full relativistic perception pipeline on the device:
// generate ray -> aberrate -> intersect -> base shade -> Doppler colour
// -> beaming. The whole per-pixel path is GPU-only.
//
// The pipeline orders aberration *before* intersection so the geometry
// is hit-tested with the ray as observed by the boosted frame (i.e.
// the apparent sphere position is what gets sampled), and Doppler /
// searchlight come *after* shading so they modify the perceived
// radiance rather than the underlying surface state.
__global__ void k_sphere_relativistic(float* pixels, int width, int height,
                                      rr::camera::GpuCamera           cam,
                                      rr::relativity::Observer        observer,
                                      rr::relativity::RelativityParams params,
                                      rr::geometry::Sphere            sphere) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    using rr::math::Vec3;

    // 1. Camera ray.
    auto ray = rr::camera::generate_camera_ray(cam, x, y, width, height);

    // 2. Aberration.
    if (params.enable_aberration) {
        ray.direction = rr::relativity::aberrateDirection(observer.velocity,
                                                          ray.direction);
    }

    // 3. Sphere intersection.
    const auto hit = rr::cuda::intersect_sphere(ray, sphere, /*t_min=*/0.0f,
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

    // Doppler factor for the (possibly aberrated) photon direction in
    // the scene frame. Computed once and reused for steps 5 and 6.
    const float D = rr::relativity::dopplerFactor(observer.velocity,
                                                  ray.direction);

    // 5. Doppler colour shift (artistic approximation).
    if (params.enable_doppler) {
        color = rr::relativity::applyDopplerColor(color, D,
                                                  params.doppler_color_strength);
    }

    // 6. Searchlight / relativistic beaming.
    if (params.enable_searchlight) {
        const float D4    = rr::relativity::searchlightFactor(D);
        const float scale = 1.0f + (D4 - 1.0f) * params.searchlight_strength;
        color = color * scale;
    }

    // 7. Framebuffer write.
    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = color.x;
    pixels[idx + 1] = color.y;
    pixels[idx + 2] = color.z;
    pixels[idx + 3] = 1.0f;
}

}

void launch_sphere_relativistic(float* device_pixels, int width, int height,
                                rr::camera::GpuCamera           cam,
                                rr::relativity::Observer        observer,
                                rr::relativity::RelativityParams params,
                                rr::geometry::Sphere            sphere,
                                cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_sphere_relativistic<<<grid, block, 0, stream>>>(
        device_pixels, width, height, cam, observer, params, sphere);
}

namespace {

// Scene-aware variant of k_sphere_relativistic. Reads the camera /
// observer / params from a CudaSceneView passed by value, and runs a
// closest-hit loop over the uploaded sphere array instead of a single
// hard-coded primitive. Everything else (aberration, Doppler colour,
// searchlight) matches the M9 single-sphere pipeline.
__global__ void k_render_scene(float* pixels, int width, int height,
                               rr::cuda::CudaSceneView scene) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    using rr::math::Vec3;

    // 1. Camera ray.
    auto ray = rr::camera::generate_camera_ray(scene.camera, x, y, width, height);

    // 2. Aberration in the observer frame.
    if (scene.params.enable_aberration) {
        ray.direction = rr::relativity::aberrateDirection(scene.observer.velocity,
                                                          ray.direction);
    }

    // 3. Closest-hit loop over both primitive types. `t_max`
    //    tightens as we accept hits so later candidates only need to
    //    beat the running best - this lets sphere and triangle
    //    primitives compete for the same nearest-hit slot.
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

    // Naive triangle loop. Vertex transforms are not applied: vertex
    // positions are taken as-is from the uploaded buffer (effectively
    // world-space). Per-mesh transforms join the kernel alongside the
    // material system in M11.
    const auto& mesh = scene.mesh;
    for (int i = 0; i < mesh.triangle_count; ++i) {
        const auto tri = mesh.triangles[i];
        const auto v0  = mesh.vertices[tri.v0].position;
        const auto v1  = mesh.vertices[tri.v1].position;
        const auto v2  = mesh.vertices[tri.v2].position;
        auto h = rr::cuda::intersect_triangle(ray, v0, v1, v2,
                                              /*t_min=*/0.0f, t_max);
        if (h.hit) {
            // Triangles inherit the per-mesh material id.
            h.material_index = mesh.material_id;
            // Interpolate per-vertex UVs from the barycentrics
            // intersect_triangle returned. The third weight is
            // implicitly `1 - bary_u - bary_v`.
            const auto uv0 = mesh.vertices[tri.v0].uv;
            const auto uv1 = mesh.vertices[tri.v1].uv;
            const auto uv2 = mesh.vertices[tri.v2].uv;
            const float w0 = 1.0f - h.bary_u - h.bary_v;
            h.uv = uv0 * w0 + uv1 * h.bary_u + uv2 * h.bary_v;
            best             = h;
            t_max            = h.t;
        }
    }

    // 4. Base shade.
    //    Single pass over the uploaded lights.  Per-hit lights
    //    (Point / Directional) accumulate into `lighting`; an
    //    Environment light is recorded as `env_color` for the
    //    ambient + miss-fallback path.  Area lights are skipped at
    //    this milestone (sampling lands with the path tracer).
    //
    //    No shadow rays - this is direct lighting only.  The path
    //    tracer (M14) adds occlusion + indirect bounces.
    Vec3 lighting  = Vec3{0.0f, 0.0f, 0.0f};
    Vec3 env_color = Vec3{0.5f, 0.65f, 0.85f};  // pale-blue default sky
    bool has_env   = false;

    rr::material::MaterialParams mat;
    const bool have_material =
        best.hit
        && best.material_index >= 0
        && best.material_index < scene.material_count
        && scene.materials != nullptr;
    if (have_material) {
        mat = scene.materials[best.material_index];
    }

    // M16: when a material binds a texture, sample it at the hit
    // UV and replace `mat.baseColor` for this hit. Out-of-range
    // ids and a missing texture array fall through to the
    // material's constant baseColor.
    Vec3 albedo = mat.baseColor;
    if (best.hit
        && mat.base_color_texture_id >= 0
        && mat.base_color_texture_id < scene.texture_count
        && scene.textures != nullptr) {
        albedo = rr::cuda::sample_texture(scene.textures[mat.base_color_texture_id],
                                          best.uv);
    }

    for (int i = 0; i < scene.light_count; ++i) {
        const rr::lighting::Light L = scene.lights[i];

        if (L.type == rr::lighting::LightType::Environment) {
            env_color = L.color * L.intensity;
            has_env   = true;
            continue;
        }
        if (!best.hit)                                 continue;
        if (L.type == rr::lighting::LightType::Area)   continue;  // placeholder

        Vec3  wi;       // unit vector from hit toward the light
        float E_lum;    // scalar attenuation (1 for directional, 1/r^2 for point)
        bool  use_light = false;

        if (L.type == rr::lighting::LightType::Directional) {
            // L.direction is the propagation direction; the
            // surface-to-light vector is its negation.
            wi        = -L.direction;
            E_lum     = 1.0f;
            use_light = true;
        } else if (L.type == rr::lighting::LightType::Point) {
            const Vec3  to_light = L.position - best.position;
            const float d2       = rr::math::dot(to_light, to_light);
            if (d2 > 1.0e-12f) {
                const float inv_d = 1.0f / sqrtf(d2);
                wi        = to_light * inv_d;
                E_lum     = 1.0f / d2;     // inverse-square falloff
                use_light = true;
            }
        }

        if (!use_light) continue;

        float ndotl = rr::math::dot(best.normal, wi);
        if (ndotl <= 0.0f) continue;       // backface; no contribution

        const Vec3 Li = L.color * (L.intensity * E_lum);
        lighting = lighting + Li * ndotl;
    }

    Vec3 color;
    if (best.hit) {
        // Lambertian diffuse for direct lights + ambient (env tint
        // or default), plus self-emission. The /pi normalisation is
        // skipped at this milestone (no path-traced light transport
        // yet); the relativistic searchlight scale wraps the result
        // anyway, so the absolute brightness is artistic.
        const Vec3 diffuse  = albedo * (lighting + env_color);
        const Vec3 emission = mat.emissionColor * mat.emissionStrength;
        color = diffuse + emission;
    } else if (has_env) {
        // Environment light overrides the hard-coded sky on a miss.
        color = env_color;
    } else {
        // Default vertical sky gradient when no Environment light
        // is present.
        const float t = 0.5f * (ray.direction.y + 1.0f);
        color = Vec3{(1.0f - t) * 1.0f + t * 0.5f,
                     (1.0f - t) * 1.0f + t * 0.7f,
                     (1.0f - t) * 1.0f + t * 1.0f};
    }

    // Doppler factor from the (possibly aberrated) photon direction.
    const float D = rr::relativity::dopplerFactor(scene.observer.velocity,
                                                  ray.direction);

    // 5. Doppler colour shift (artistic placeholder).
    if (scene.params.enable_doppler) {
        color = rr::relativity::applyDopplerColor(color, D,
                                                  scene.params.doppler_color_strength);
    }

    // 6. Relativistic beaming.
    if (scene.params.enable_searchlight) {
        const float D4    = rr::relativity::searchlightFactor(D);
        const float scale = 1.0f + (D4 - 1.0f) * scene.params.searchlight_strength;
        color = color * scale;
    }

    // 7. Framebuffer write.
    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = color.x;
    pixels[idx + 1] = color.y;
    pixels[idx + 2] = color.z;
    pixels[idx + 3] = 1.0f;
}

}

void launch_render_scene(float* device_pixels, int width, int height,
                         rr::cuda::CudaSceneView scene,
                         cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_render_scene<<<grid, block, 0, stream>>>(device_pixels, width, height, scene);
}

namespace {

// Closest-hit search across spheres + the single mesh slot. The
// path tracer uses this on every bounce, so we factor it out of
// `k_render_scene`'s body. `t_min` is the per-bounce epsilon that
// keeps a freshly-spawned bounce ray from hitting its own origin
// surface again.
__device__ rr::renderer::Hit
trace_closest(const rr::cuda::CudaSceneView& scene,
              const rr::camera::CameraRay&   ray,
              float                          t_min) {
    rr::renderer::Hit best;
    float             t_max = 1.0e30f;

    for (int i = 0; i < scene.sphere_count; ++i) {
        const auto h = rr::cuda::intersect_sphere(ray, scene.spheres[i],
                                                  t_min, t_max);
        if (h.hit) { best = h; t_max = h.t; }
    }
    const auto& mesh = scene.mesh;
    for (int i = 0; i < mesh.triangle_count; ++i) {
        const auto tri = mesh.triangles[i];
        const auto v0  = mesh.vertices[tri.v0].position;
        const auto v1  = mesh.vertices[tri.v1].position;
        const auto v2  = mesh.vertices[tri.v2].position;
        auto h = rr::cuda::intersect_triangle(ray, v0, v1, v2, t_min, t_max);
        if (h.hit) {
            h.material_index = mesh.material_id;
            // Interpolate per-vertex UVs from the barycentrics
            // intersect_triangle returned (M16).
            const auto uv0 = mesh.vertices[tri.v0].uv;
            const auto uv1 = mesh.vertices[tri.v1].uv;
            const auto uv2 = mesh.vertices[tri.v2].uv;
            const float w0 = 1.0f - h.bary_u - h.bary_v;
            h.uv = uv0 * w0 + uv1 * h.bary_u + uv2 * h.bary_v;
            best  = h;
            t_max = h.t;
        }
    }
    return best;
}

// Environment / sky-fallback colour. Mirrors the M12 kernel: an
// uploaded `Environment` light wins; otherwise the existing
// vertical sky gradient.
__device__ rr::math::Vec3
sky_color(const rr::cuda::CudaSceneView& scene,
          rr::math::Vec3                  ray_dir) {
    using rr::math::Vec3;
    for (int i = 0; i < scene.light_count; ++i) {
        const rr::lighting::Light L = scene.lights[i];
        if (L.type == rr::lighting::LightType::Environment) {
            return L.color * L.intensity;
        }
    }
    const float t = 0.5f * (ray_dir.y + 1.0f);
    return Vec3{(1.0f - t) * 1.0f + t * 0.5f,
                (1.0f - t) * 1.0f + t * 0.7f,
                (1.0f - t) * 1.0f + t * 1.0f};
}

// Look up the material for a hit; falls back to the renderer's
// neutral default when the index is out of range.
__device__ rr::material::MaterialParams
lookup_material(const rr::cuda::CudaSceneView& scene, int material_index) {
    rr::material::MaterialParams mat;
    if (material_index >= 0
        && material_index < scene.material_count
        && scene.materials != nullptr) {
        mat = scene.materials[material_index];
    }
    return mat;
}

// One full path. Generates a primary ray, applies aberration, and
// bounces up to `max_depth` times accumulating emission +
// environment radiance through cosine-weighted Lambertian
// scattering. Returns the (relativistic-effects-applied) radiance
// for this single sample.
__device__ rr::math::Vec3
trace_one_path(const rr::cuda::CudaSceneView& scene,
               int x, int y, int width, int height,
               int max_depth,
               rr::pathtracer::RNG& rng) {
    using rr::math::Vec3;

    auto ray = rr::camera::generate_camera_ray(scene.camera, x, y, width, height);

    if (scene.params.enable_aberration) {
        ray.direction = rr::relativity::aberrateDirection(scene.observer.velocity,
                                                          ray.direction);
    }
    const Vec3 primary_dir = ray.direction;

    Vec3 throughput{1.0f, 1.0f, 1.0f};
    Vec3 radiance  {0.0f, 0.0f, 0.0f};

    for (int bounce = 0; bounce < max_depth; ++bounce) {
        // 1e-3f is a generous self-intersection epsilon; tighter
        // values land within Moller-Trumbore's noise floor on
        // some triangles. The path tracer is not yet tuned for
        // the long-range / large-scale-diff cases that need a
        // smaller value.
        const auto hit = trace_closest(scene, ray, /*t_min=*/1.0e-3f);

        if (!hit.hit) {
            // Environment fallback: light-only contribution, then
            // path terminates.
            radiance = radiance + throughput * sky_color(scene, ray.direction);
            break;
        }

        const auto mat = lookup_material(scene, hit.material_index);

        // Emission from the hit surface (allows the path tracer to
        // pick up emissive geometry directly, including the M11
        // emissive quad).
        radiance = radiance + throughput
                            * (mat.emissionColor * mat.emissionStrength);

        // Lambertian diffuse bounce. Lambert: f_r = baseColor / pi;
        // PDF for cosine-weighted hemisphere is cos(theta) / pi;
        // throughput update simplifies to
        //   throughput *= baseColor * cos(theta) / pi * pi / cos(theta)
        //              =  baseColor.
        // M16: when the material binds a texture, sample it at the
        // hit's UV and use the result as `baseColor` for this
        // bounce.
        Vec3 albedo = mat.baseColor;
        if (mat.base_color_texture_id >= 0
            && mat.base_color_texture_id < scene.texture_count
            && scene.textures != nullptr) {
            albedo = rr::cuda::sample_texture(
                scene.textures[mat.base_color_texture_id], hit.uv);
        }

        Vec3 n = hit.normal;
        if (rr::math::dot(n, ray.direction) > 0.0f) {
            n = -n;  // shading normal faces the incoming ray
        }
        const Vec3 wi = rr::pathtracer::sample_hemisphere_cosine(n, rng);

        throughput = throughput * albedo;

        // Move the new ray's origin slightly off the surface so
        // the next intersection's t_min epsilon doesn't reject it.
        ray.origin    = hit.position + n * 1.0e-3f;
        ray.direction = wi;

        // Cheap early-out: once throughput is essentially black,
        // no later bounce can contribute. (Russian roulette is the
        // proper unbiased version; that's the next M14 slice.)
        if (throughput.x <= 1.0e-6f
            && throughput.y <= 1.0e-6f
            && throughput.z <= 1.0e-6f) {
            break;
        }
    }

    // Apply relativistic effects to the integrated radiance, using
    // the primary ray direction (the apparent direction the
    // observer is looking in this frame).
    const float D = rr::relativity::dopplerFactor(scene.observer.velocity,
                                                  primary_dir);
    if (scene.params.enable_doppler) {
        radiance = rr::relativity::applyDopplerColor(
            radiance, D, scene.params.doppler_color_strength);
    }
    if (scene.params.enable_searchlight) {
        const float D4    = rr::relativity::searchlightFactor(D);
        const float scale = 1.0f + (D4 - 1.0f) * scene.params.searchlight_strength;
        radiance = radiance * scale;
    }
    return radiance;
}

// Path-tracing kernel. Per pixel: trace `spp` independent paths,
// average them, write to the framebuffer. The "accumulation
// buffer" is the per-thread `accum` Vec3 register; the framebuffer
// stores the mean. Across multiple kernel launches a caller can
// blend the framebuffer themselves for true progressive
// refinement; the kernel itself is single-launch.
__global__ void
k_path_trace(float* pixels, int width, int height,
             rr::cuda::CudaSceneView scene,
             int spp, int max_depth, unsigned int seed_offset) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    using rr::math::Vec3;
    Vec3 accum{0.0f, 0.0f, 0.0f};

    const int n = spp > 0 ? spp : 1;
    for (int s = 0; s < n; ++s) {
        rr::pathtracer::RNG rng = rr::pathtracer::make_rng(
            static_cast<unsigned int>(x),
            static_cast<unsigned int>(y),
            seed_offset + static_cast<unsigned int>(s));

        accum = accum + trace_one_path(scene,
                                       x, y, width, height,
                                       max_depth > 0 ? max_depth : 1,
                                       rng);
    }

    const Vec3 mean = accum * (1.0f / static_cast<float>(n));

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = mean.x;
    pixels[idx + 1] = mean.y;
    pixels[idx + 2] = mean.z;
    pixels[idx + 3] = 1.0f;
}

}

void launch_path_trace(float* device_pixels, int width, int height,
                       rr::cuda::CudaSceneView scene,
                       int spp, int max_depth,
                       unsigned int seed_offset,
                       cudaStream_t stream) {
    if (!device_pixels || width <= 0 || height <= 0) return;
    if (spp       <= 0) spp       = 1;
    if (max_depth <= 0) max_depth = 1;

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_path_trace<<<grid, block, 0, stream>>>(device_pixels, width, height, scene,
                                             spp, max_depth, seed_offset);
}

}



// Stage 11C minimal diffuse GPU path tracer.
//
// Per pixel, per launch:
//
//   1. Seed `pathtracer::Rng` from (x, y, sample_index, seed).
//   2. Generate the primary ray with sub-pixel jitter sampled
//      from `next_vec2(rng)` (anti-aliasing falls out of the spp
//      loop for free).
//   3. Closest-hit walk over the scene's spheres + (single) mesh
//      slot, identical in shape to `k_render_scene`.
//   4. Add `material.emissionColor * emissionStrength`
//      modulated by the running throughput.
//   5. Sample a cosine-weighted hemisphere direction
//      (`pathtracer::sample_cosine_hemisphere`), align it to the
//      surface normal via a small orthonormal basis, multiply
//      throughput by the diffuse albedo (the cos / pi factor
//      cancels exactly under cos-weighted sampling for a
//      Lambert BRDF), advance the ray, repeat until we exhaust
//      `max_bounces` or miss.
//   6. On miss, add `env_color * env_intensity * throughput`.
//
// The output is one sample per pixel; the host orchestration
// (`PathTracer::render`) loops `samples_per_pixel` times and
// accumulates through `rr::renderer::AccumulationBuffer`.
//
// Lights uploaded via `GpuScene::upload_lights` are visible in
// the launch arg but NOT directly sampled - explicit no-MIS-yet
// per the Stage 11C prompt; only emissive surfaces contribute
// illumination, so make sure your scenes have at least one
// emissive surface or rely on `env_intensity > 0`.

#include "cuda/CudaPathTracer.cuh"

#include "cuda/CudaIntersection.cuh"
#include "cuda/CudaScene.cuh"
#include "gpu/GpuScene.h"
#include "pathtracer/DirectLight.cuh"   // NEE.2 helper + epsilons
#include "pathtracer/Bsdf.cuh"          // MIS.5: bsdf_pdf for the BSDF-side PDF inside power_heuristic
#include "pathtracer/Mis.h"             // MIS.5: power_heuristic helper
#include "pathtracer/RNG.cuh"
#include "pathtracer/Sampling.cuh"

#include "math/MathUtils.h"             // kInvPi
#include "math/Vec3.h"
#include "renderer/Hit.h"

#include <cuda_runtime.h>

#include <cstddef>
#include <cstdint>

namespace rr::cuda {

namespace {

using rr::math::Vec3;

// Build an orthonormal basis (T, B, N) from a unit normal `n`,
// then rotate `local` (where +Z is `n` in tangent space) into
// world space. Used to take the hemisphere samples produced by
// `pathtracer::sample_cosine_hemisphere` (always +Z facing) and
// re-orient them around an arbitrary hit normal.
//
// The tangent is derived by crossing `n` against whichever
// world axis it is least parallel to; this avoids the
// degenerate cross product that arises when `n` is exactly
// (0, 0, 1) for a fixed reference axis.
__device__ inline Vec3 align_to_normal(Vec3 local, Vec3 n) {
    using rr::math::cross;
    using rr::math::normalize;
    Vec3 helper = (fabsf(n.z) < 0.999f) ? Vec3{0.0f, 0.0f, 1.0f}
                                        : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 t = normalize(cross(helper, n));
    Vec3 b = cross(n, t);
    return t * local.x + b * local.y + n * local.z;
}

// Closest-hit walk over `scene` for `ray` in `(0, t_max)`. The
// shape mirrors `k_render_scene`'s closest-hit step (sphere loop
// then triangle loop, tightening `t_max` as candidates are
// accepted). The hit's `material_index` for sphere primitives
// comes through `intersect_sphere`; for mesh hits we rewrite it
// to the mesh's `material_id` so the shading path always reads
// from the same materials array.
__device__ inline rr::renderer::Hit closest_hit(const rr::camera::CameraRay& ray,
                                                const CudaSceneView&         scene,
                                                float                        t_max) {
    rr::renderer::Hit best;
    for (int i = 0; i < scene.sphere_count; ++i) {
        const auto h = intersect_sphere(ray, scene.spheres[i],
                                        /*t_min=*/0.0f, t_max);
        if (h.hit) {
            best  = h;
            t_max = h.t;
        }
    }
    const auto& mesh = scene.mesh;
    for (int i = 0; i < mesh.triangle_count; ++i) {
        const auto tri = mesh.triangles[i];
        const auto v0  = mesh.vertices[tri.v0].position;
        const auto v1  = mesh.vertices[tri.v1].position;
        const auto v2  = mesh.vertices[tri.v2].position;
        const auto h   = intersect_triangle(ray, v0, v1, v2,
                                            /*t_min=*/0.0f, t_max);
        if (h.hit) {
            best                = h;
            best.material_index = mesh.material_id;
            t_max               = h.t;
        }
    }
    return best;
}

// Look up material params by index, falling back to a neutral
// default when `idx` is out of range. The default mirrors
// `MaterialParams{}`'s diffuse-grey baseline so unmaterialed
// primitives still bounce sensibly.
__device__ inline rr::material::MaterialParams material_for(
        int                                    idx,
        const rr::material::MaterialParams*    materials,
        int                                    material_count) {
    if (idx >= 0 && idx < material_count && materials != nullptr) {
        return materials[idx];
    }
    return rr::material::MaterialParams{};
}

// Generate a primary pinhole ray with sub-pixel jitter sampled
// from `(jx, jy)` in [0, 1)^2. Mirrors
// `rr::camera::generate_camera_ray` but with the +0.5 pixel-
// centre offset replaced by the jitter so the spp loop produces
// anti-aliased samples for free.
__device__ inline rr::camera::CameraRay
generate_primary_ray(const rr::camera::GpuCamera& cam,
                     int x, int y, int width, int height,
                     float jx, float jy) {
    using rr::math::normalize;
    const float fx = static_cast<float>(x) + jx;
    const float fy = static_cast<float>(y) + jy;
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);
    const float u = (2.0f * fx / fw - 1.0f) * cam.aspect * cam.tan_half_vfov;
    const float v = (1.0f - 2.0f * fy / fh) * cam.tan_half_vfov;
    const Vec3 dir = normalize(cam.forward + cam.right * u + cam.up * v);
    return rr::camera::CameraRay{cam.position, dir};
}

// NEE.2 any-hit shadow ray. Returns 0.0f if any primitive
// intersects `ray` in (kShadowEps, t_max), 1.0f otherwise.
// Mirrors `closest_hit`'s scene walk (sphere loop then
// triangle loop) but bails on the first hit; the actual `t`
// and material are not consulted. Called by the kernel only
// inside the `enable_nee` guard, so when NEE is off this
// helper is never invoked.
__device__ inline float trace_shadow_ray_pt(const rr::camera::CameraRay& ray,
                                            const CudaSceneView&         scene,
                                            float                        t_max) {
    constexpr float t_min = rr::pathtracer::kShadowEps;
    for (int i = 0; i < scene.sphere_count; ++i) {
        const auto h = intersect_sphere(ray, scene.spheres[i],
                                        t_min, t_max);
        if (h.hit) return 0.0f;
    }
    const auto& mesh = scene.mesh;
    for (int i = 0; i < mesh.triangle_count; ++i) {
        const auto tri = mesh.triangles[i];
        const auto v0  = mesh.vertices[tri.v0].position;
        const auto v1  = mesh.vertices[tri.v1].position;
        const auto v2  = mesh.vertices[tri.v2].position;
        const auto h   = intersect_triangle(ray, v0, v1, v2,
                                            t_min, t_max);
        if (h.hit) return 0.0f;
    }
    return 1.0f;
}

// One sample per pixel.
__global__ void k_pathtrace_sample(float*           pixels,
                                   int              width,
                                   int              height,
                                   CudaSceneView    scene,
                                   int              max_bounces,
                                   unsigned int     seed,
                                   unsigned int     sample_index,
                                   Vec3             env_color,
                                   float            env_intensity,
                                   float            firefly_clamp,
                                   bool             enable_nee) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    rr::pathtracer::Rng rng = rr::pathtracer::make_pixel_rng(
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
        /*frame_index=*/sample_index,
        static_cast<std::uint64_t>(seed));

    // Sub-pixel jitter for AA.
    const rr::math::Vec2 jitter = rr::pathtracer::next_vec2(rng);
    rr::camera::CameraRay ray = generate_primary_ray(
        scene.camera, x, y, width, height, jitter.x, jitter.y);

    Vec3 throughput{1.0f, 1.0f, 1.0f};
    Vec3 radiance{0.0f, 0.0f, 0.0f};

    for (int bounce = 0; bounce < max_bounces; ++bounce) {
        const auto hit = closest_hit(ray, scene, /*t_max=*/1.0e30f);
        if (!hit.hit) {
            // Environment fallback. Multiply by throughput to
            // attenuate by every diffuse interaction we already
            // walked through.
            const Vec3 env = env_color * env_intensity;
            radiance = radiance + Vec3{throughput.x * env.x,
                                       throughput.y * env.y,
                                       throughput.z * env.z};
            break;
        }

        const auto m = material_for(hit.material_index,
                                    scene.materials,
                                    scene.material_count);

        // PT-P.15: short-circuit the emission add on non-emissive
        // surfaces. The kernel-side branch is uniform per-warp
        // (every pixel hitting the same surface reads the same
        // MaterialParams), so the cost is one uniform compare and
        // the savings on the common path are 6 multiplies + 3 adds
        // per hit per bounce. The `emissionColor * emissionStrength`
        // factorisation is the convention declared in
        // `MaterialTypes.h::is_emissive`; both must be non-zero for
        // the material to contribute light. The non-emissive path
        // is bit-identical with the pre-PT-P.15 `+= 0` arithmetic
        // because IEEE-754 addition of zero is exact.
        if (rr::material::is_emissive(m)) {
            // Emission: the hit acts as a light source. Add its
            // contribution before generating the next bounce so even
            // bounce == max_bounces - 1 picks it up.
            const Vec3 emission =
                Vec3{m.emissionColor.x * m.emissionStrength,
                     m.emissionColor.y * m.emissionStrength,
                     m.emissionColor.z * m.emissionStrength};
            radiance = radiance + Vec3{throughput.x * emission.x,
                                       throughput.y * emission.y,
                                       throughput.z * emission.z};
        }

        // NEE.2 skeleton. When `enable_nee == false` (the
        // PathTraceConfig default for every existing caller),
        // this branch is never entered: no shadow ray is
        // traced, no extra RNG draw is performed, the
        // cos-weighted hemisphere sample below pulls from
        // the same RNG stream as the pre-NEE build, and the
        // per-pixel write is bit-exact with the pre-NEE
        // arithmetic. The IEEE-754 byte-identity argument is
        // the same as PT-P.21 / PT-P.24's: with the branch
        // un-executed, no FP add ever touches the radiance
        // accumulator at default.
        //
        // When entered (a future slice flips
        // `PathTraceConfig::enable_nee` via a CLI flag —
        // the current slice has none), the kernel picks one
        // light uniformly via
        // `pathtracer::sample_direct_light_uniform`, traces
        // a single any-hit shadow ray with `trace_shadow_ray_pt`,
        // and adds the visibility-modulated, BRDF-modulated,
        // throughput-modulated direct contribution to
        // `radiance`. No MIS yet — the existing emission
        // term and this new NEE term are summed naively
        // (the v1 "no double-count window" argument from
        // `docs/PATH_TRACER_NEE_TASK.md` §1 holds because
        // Point + Directional lights have no mesh).
        //
        // The branch is uniform per-warp (every pixel in a
        // launch reads the same `enable_nee` /
        // `scene.light_count`), so no warp divergence is
        // introduced at default.
        if (enable_nee && scene.light_count > 0) {
            const float u_select = rr::pathtracer::next_float(rng);
            const auto sample = rr::pathtracer::sample_direct_light_uniform(
                scene.lights, scene.light_count,
                hit.position, hit.normal, u_select);
            if (sample.pdf_inv > 0.0f) {
                // Offset the shadow-ray origin off the
                // surface along the normal to dodge
                // self-intersection at the receiver end
                // (matches the diffuse-bounce origin
                // offset just below). The light-end
                // epsilon comes from subtracting
                // `kShadowEps` from `sample.distance`;
                // for Directional lights the helper
                // already encoded `kDirectionalShadowTMax`
                // so the subtraction is irrelevant
                // (1e30 - 1e-3 == 1e30 in float).
                const float t_max =
                    sample.distance - rr::pathtracer::kShadowEps;
                const rr::camera::CameraRay shadow{
                    hit.position + hit.normal * 1.0e-4f,
                    sample.wi};
                const float vis =
                    trace_shadow_ray_pt(shadow, scene, t_max);
                if (vis > 0.0f) {
                    const float cos_th =
                        rr::math::dot(hit.normal, sample.wi);
                    if (cos_th > 0.0f) {
                        // MIS.5: compute the MIS weight on the
                        // NEE-side estimator. Veach 1995 §10.3
                        // delta-light convention: when
                        // `sample.is_delta == true` (v1 Point +
                        // Directional lights, populated by
                        // `sample_direct_light_uniform` per
                        // MIS.3), the BSDF sampler can never
                        // reach this light (zero measure on
                        // the unit sphere); the NEE-side
                        // weight is exactly 1.0f. Multiplying
                        // the contribution by `1.0f` is the
                        // IEEE-754 identity multiplication
                        // (per IEEE-754 §6: `x * 1.0f == x`
                        // for any finite non-NaN x), so the
                        // per-pixel arithmetic is bit-
                        // identical with the pre-MIS.5
                        // baseline at v1 light-type scope.
                        //
                        // For non-delta lights (future area-
                        // light arc), the weight is computed
                        // via the Veach β=2 power heuristic
                        // from `sample.pdf_solid_angle` (MIS.3)
                        // and the BSDF PDF at `sample.wi`
                        // (`bsdf_pdf` from MIS.2). The
                        // `power_heuristic` call (MIS.4) is
                        // unreachable at v1 because every v1
                        // light sets `is_delta == true`.
                        const float mis_weight_nee = sample.is_delta
                            ? 1.0f
                            : rr::pathtracer::power_heuristic(
                                  sample.pdf_solid_angle,
                                  rr::pathtracer::bsdf_pdf(
                                      m, sample.wi, hit.normal));

                        // Lambert BRDF: baseColor / pi.
                        const Vec3 brdf = m.baseColor * rr::math::kInvPi;
                        const float k =
                            cos_th * vis * sample.pdf_inv * mis_weight_nee;
                        const Vec3 contrib = Vec3{
                            throughput.x * sample.li_unattenuated.x * brdf.x,
                            throughput.y * sample.li_unattenuated.y * brdf.y,
                            throughput.z * sample.li_unattenuated.z * brdf.z}
                            * k;
                        radiance = radiance + contrib;
                    }
                }
            }
        }

        // If this was the last allowed bounce, stop - no point
        // sampling a direction we are not going to trace.
        if (bounce + 1 >= max_bounces) break;

        // Diffuse Lambert bounce. cos-weighted sampling makes the
        // PDF cos(theta) / pi and the integrand cos(theta) * (albedo / pi);
        // their ratio is the albedo, so throughput *= albedo.
        const rr::math::Vec2 u2 = rr::pathtracer::next_vec2(rng);
        const Vec3 local_dir =
            rr::pathtracer::sample_cosine_hemisphere(u2);
        const Vec3 world_dir = align_to_normal(local_dir, hit.normal);

        // Component-wise Hadamard product for the colour update.
        throughput = Vec3{throughput.x * m.baseColor.x,
                          throughput.y * m.baseColor.y,
                          throughput.z * m.baseColor.z};

        // Offset the ray origin off the surface to dodge
        // self-intersection when the next ray re-walks the same
        // primitive list.
        ray.origin    = hit.position + hit.normal * 1.0e-4f;
        ray.direction = world_dir;
    }

    // PT-P.24: per-channel firefly clamp on the per-sample
    // radiance. Strict `>` gating: when `firefly_clamp == 0.0f`
    // (the PathTraceConfig default), the branch is not taken
    // and `radiance` is unchanged; the resulting per-pixel
    // write is byte-identical with the pre-PT-P.24 arithmetic.
    // When `firefly_clamp > 0.0f`, each channel is clamped via
    // `fminf` before being written. The branch is uniform
    // per-warp (every pixel in a launch reads the same
    // `firefly_clamp`), so no warp divergence is introduced.
    // The OptiX path-trace raygen mirrors this clamp at the
    // same point in its integrator (per-sample radiance,
    // pre-accumulation) so the two backends' outputs remain
    // convergent at non-zero clamp.
    if (firefly_clamp > 0.0f) {
        radiance.x = fminf(radiance.x, firefly_clamp);
        radiance.y = fminf(radiance.y, firefly_clamp);
        radiance.z = fminf(radiance.z, firefly_clamp);
    }

    const int idx = (y * width + x) * 4;
    pixels[idx + 0] = radiance.x;
    pixels[idx + 1] = radiance.y;
    pixels[idx + 2] = radiance.z;
    pixels[idx + 3] = 1.0f;
}

}  // namespace

bool launch_pathtrace_sample(float*                   device_sample_pixels,
                             int                      width,
                             int                      height,
                             const rr::gpu::GpuScene& scene,
                             int                      max_bounces,
                             unsigned int             seed,
                             unsigned int             sample_index,
                             rr::math::Vec3           env_color,
                             float                    env_intensity,
                             float                    firefly_clamp,
                             bool                     enable_nee) {
    // PT-P.24: defence-in-depth on the host validator's
    // `firefly_clamp >= 0.0f` rejection. The host-side
    // `PathTracer::render` already returns an error message
    // for negative values; the launcher catches a caller that
    // bypasses that validator.
    if (device_sample_pixels == nullptr || width <= 0 || height <= 0
     || max_bounces < 0 || firefly_clamp < 0.0f) {
        return false;
    }

    // Build the device-side launch view from the GpuScene's
    // accessor methods. Same pattern as
    // `CudaRenderer::render_scene`.
    CudaSceneView view;
    view.camera   = scene.gpu_camera();
    view.observer = scene.observer();
    view.params   = scene.params();
    view.spheres       = scene.device_spheres();
    view.sphere_count  = static_cast<int>(scene.sphere_count());
    {
        const auto& m = scene.mesh();
        view.mesh.vertices       = m.device_vertices();
        view.mesh.triangles      = m.device_triangles();
        view.mesh.vertex_count   = static_cast<int>(m.vertex_count());
        view.mesh.triangle_count = static_cast<int>(m.triangle_count());
        view.mesh.material_id    = m.material_id();
        view.mesh.transform      = m.transform();
    }
    view.materials      = scene.device_materials();
    view.material_count = static_cast<int>(scene.material_count());
    view.lights         = scene.device_lights();
    view.light_count    = static_cast<int>(scene.light_count());

    const dim3 block(16, 16);
    const dim3 grid((width  + block.x - 1) / block.x,
                    (height + block.y - 1) / block.y);

    k_pathtrace_sample<<<grid, block>>>(device_sample_pixels, width, height,
                                        view, max_bounces, seed,
                                        sample_index,
                                        env_color, env_intensity,
                                        firefly_clamp,
                                        enable_nee);
    if (cudaGetLastError() != cudaSuccess) {
        (void)cudaGetLastError();
        return false;
    }
    return true;
}

}  // namespace rr::cuda

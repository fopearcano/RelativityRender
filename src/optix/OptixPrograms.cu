// Stage 17A.3 / 17A.4 / 17A.5 / 20G / 20H OptiX device programs
// per `docs/OPTIX_BACKEND_PLAN.md` §20. Compiled to PTX by the
// build system (`nvcc --ptx`) and embedded into rr_optix as a
// host-side C-string via `cmake/EmbedPtxAsHeader.cmake`.
//
// Stage 17A.3 shipped a `__raygen__pinhole` that wrote a flat
// colour and an empty `__miss__radiance`. Stage 17A.4 extended
// the surface with closest-hit shading (`0.5 * normal + 0.5`)
// and a vertical sky-gradient miss. Stage 17A.5 layers the
// relativistic camera model on top of the same pipeline:
// - `__raygen__pinhole` aberrates the primary ray direction
//   via `rr::relativity::aberrateDirection` before
//   `optixTrace`, gated on `params.enable_aberration`. The
//   aberrated direction is what the closest-hit / miss
//   programs see via `optixGetWorldRayDirection()`, exactly
//   the way the CUDA path applies aberration before
//   intersection.
// - `__closesthit__radiance` and `__miss__radiance` apply the
//   Doppler colour shift (`applyDopplerColor`) and the
//   relativistic-beaming searchlight scale
//   (`1 + (D^4 - 1) * searchlight_strength`) to the base
//   shade before encoding into payload registers.
//
// Stage 20G adds material-flat shading via the SBT hit-record
// `HitGroupData::shading_mode`; the relativistic stack composes
// uniformly over both shading modes.
//
// Stage 20H moves the Doppler-factor computation OUT of the
// closest-hit / miss programs and INTO the raygen. The raygen
// computes D = dopplerFactor(rel, aberrated_dir) once per
// pixel, packs it into payload register 3 as INPUT to
// optixTrace, and the shaders read it via
// optixGetPayload_3() instead of recomputing. Output is
// byte-identical to the Stage 17A.5 form because OptiX 7+
// guarantees `optixGetWorldRayDirection()` in the called
// shader equals the direction passed to `optixTrace` from
// the raygen — so the D value the shader would have
// computed locally is the same D the raygen passes.
//
// Stage 17A.5 still NO bounce loop, NO RNG, NO any-hit, NO
// intersection program (built-in triangle intersection is
// used). The `numPayloadValues = 4` setting in the host-side
// pipeline compile options matches the 4 `optixSetPayload_N` /
// `optixGetPayload_N` slots used here (RGB + D).
//
// All per-pixel work runs on the GPU; the host's only role is
// uploading launch parameters + downloading the framebuffer.

#include <optix.h>

#include "camera/CameraRay.h"
#include "cuda/CudaTexture.cuh"        // Stage 20M: nearest-neighbour sampler
#include "math/Vec2.h"                 // Stage 20M: UV interpolation
#include "math/Vec3.h"
#include "optix/OptixLaunchParams.h"
#include "optix/OptixSBT.h"           // Stage 20G: HitGroupData
#include "pathtracer/RNG.h"           // Stage 20I: RNG seed helper
#include "pathtracer/Sampling.h"      // Stage 20I: cos-hemisphere
#include "relativity/RelativityMath.h"

extern "C" __constant__ rr::optix::OptixLaunchParams optixLaunchParams;

// ---- payload helpers (3 RGB floats + 1 Doppler factor) ----------

namespace {

__device__ __forceinline__ void set_payload_rgb(float r, float g, float b) {
    optixSetPayload_0(__float_as_uint(r));
    optixSetPayload_1(__float_as_uint(g));
    optixSetPayload_2(__float_as_uint(b));
}

__device__ __forceinline__ rr::math::Vec3 read_payload_rgb(unsigned int p0,
                                                           unsigned int p1,
                                                           unsigned int p2) {
    return rr::math::Vec3{__uint_as_float(p0),
                          __uint_as_float(p1),
                          __uint_as_float(p2)};
}

// Stage 20H: Doppler factor lives in payload register 3. The
// raygen sets it as INPUT before `optixTrace`; the shaders
// read it via `optixGetPayload_3()` instead of recomputing.
__device__ __forceinline__ float read_payload_doppler() {
    return __uint_as_float(optixGetPayload_3());
}

// Stage 20H: apply Doppler colour shift + searchlight beaming
// to a base colour using a precomputed D. Functionally
// equivalent to the 2-arg version below, just skips the
// per-shader D recomputation. The math leaf's
// `applyDopplerColor` and `searchlightFactor` calls are
// identical.
__device__ __forceinline__ rr::math::Vec3
apply_doppler_and_searchlight_with_D(rr::math::Vec3 base_color,
                                     float          D) {
    using rr::math::Vec3;

    const auto& par = optixLaunchParams.params;

    Vec3 color = base_color;

    // Doppler colour shift (artistic approximation).
    if (par.enable_doppler) {
        color = rr::relativity::applyDopplerColor(color, D,
                                                  par.doppler_color_strength);
    }

    // Searchlight / relativistic beaming. `lerp(1, D^4, strength)`
    // keeps `searchlight_strength` a true [0, 1] dial: 0 -> no
    // beaming, 1 -> full D^4.
    if (par.enable_searchlight) {
        const float D4    = rr::relativity::searchlightFactor(D);
        const float scale = 1.0f + (D4 - 1.0f) * par.searchlight_strength;
        color = color * scale;
    }

    return color;
}

// Apply the Stage 17A.5 Doppler + searchlight stack to a base
// colour, using the photon's direction in the scene frame
// (i.e. the aberrated ray the raygen passed to `optixTrace`).
// Stage 20H wraps the new with-D form by recomputing D from
// the launch-params observer; kept for any future shader that
// does not have access to the cached D.
__device__ __forceinline__ rr::math::Vec3
apply_doppler_and_searchlight(rr::math::Vec3 base_color,
                              rr::math::Vec3 ray_dir_world) {
    const auto& obs = optixLaunchParams.observer;

    // Stage 18A.3: precompute the launch-invariant relativity
    // scalars (|beta|, gamma) once per program invocation.
    const auto rel = rr::relativity::precompute_relativity(obs.velocity);

    // Doppler factor for the (possibly aberrated) photon
    // direction in the scene frame. Computed once and reused.
    const float D = rr::relativity::dopplerFactor(rel, ray_dir_world);

    return apply_doppler_and_searchlight_with_D(base_color, D);
}

}  // namespace

// ---- raygen ------------------------------------------------------

extern "C" __global__ void __raygen__pinhole() {
    const uint3 idx = optixGetLaunchIndex();
    const int   x   = static_cast<int>(idx.x);
    const int   y   = static_cast<int>(idx.y);

    const int W = optixLaunchParams.width;
    const int H = optixLaunchParams.height;
    if (x >= W || y >= H) return;

    float* fb = optixLaunchParams.framebuffer;
    if (fb == nullptr) return;

    const int pix = (y * W + x) * 4;

    // Stage 17A.4: trace branch when a GAS is bound; Stage
    // 17A.3 flat-colour fallback when scene_handle == 0.
    if (optixLaunchParams.scene_handle == 0) {
        fb[pix + 0] = optixLaunchParams.flat_color_r;
        fb[pix + 1] = optixLaunchParams.flat_color_g;
        fb[pix + 2] = optixLaunchParams.flat_color_b;
        fb[pix + 3] = 1.0f;
        return;
    }

    // Generate primary ray via the same RR_HD helper the CUDA
    // path uses.
    auto ray = rr::camera::generate_camera_ray(
        optixLaunchParams.camera, x, y, W, H);

    // Stage 17A.5: Lorentz-aberrate the ray direction in the
    // observer's frame before tracing. Identity at |beta| = 0
    // and at default-constructed `RelativityParams`. Same gate
    // and helper the CUDA path uses (RelativityMath.h).
    //
    // Stage 18A.3: feed the precomputed `(|beta|, gamma)`
    // snapshot into `aberrateDirection` so the per-pixel `sqrt`
    // count drops by one (the `length(beta_vec)` reduction).
    const auto rel = rr::relativity::precompute_relativity(
        optixLaunchParams.observer.velocity);
    if (optixLaunchParams.params.enable_aberration) {
        ray.direction = rr::relativity::aberrateDirection(
            rel, ray.direction);
    }

    // Stage 20H: compute Doppler factor for the (possibly
    // aberrated) ray direction in the scene frame, ONCE in
    // raygen. Pack into payload register 3 as INPUT to
    // optixTrace; the shaders read it via
    // `optixGetPayload_3()` instead of recomputing.
    // At |beta| = 0 this is 1.0 (identity); at non-zero
    // beta the closest-hit / miss apply the Doppler colour
    // shift + searchlight scale using this cached value.
    const float D = rr::relativity::dopplerFactor(rel, ray.direction);

    // Trace. 4 payload registers: [0..2] = output RGB,
    // [3] = input Doppler factor D.
    unsigned int p0 = 0u, p1 = 0u, p2 = 0u;
    unsigned int p3 = __float_as_uint(D);
    optixTrace(
        optixLaunchParams.scene_handle,
        make_float3(ray.origin.x,    ray.origin.y,    ray.origin.z),
        make_float3(ray.direction.x, ray.direction.y, ray.direction.z),
        /*tmin=*/0.0f, /*tmax=*/1.0e30f,
        /*time=*/0.0f, OptixVisibilityMask(255),
        OPTIX_RAY_FLAG_NONE,
        /*sbtOffset=*/0, /*sbtStride=*/0, /*missSbtIndex=*/0,
        p0, p1, p2, p3);

    const auto rgb = read_payload_rgb(p0, p1, p2);
    fb[pix + 0] = rgb.x;
    fb[pix + 1] = rgb.y;
    fb[pix + 2] = rgb.z;
    fb[pix + 3] = 1.0f;
}

// ---- miss --------------------------------------------------------

extern "C" __global__ void __miss__radiance() {
    using rr::math::Vec3;

    // Vertical sky gradient that visually matches the CUDA
    // path's miss shade: t = 0.5*(dir.y + 1); colour =
    // lerp(white, light-blue, t). The direction here is the
    // (possibly aberrated) ray direction in the scene frame,
    // exactly what the CUDA path's miss case sees after
    // applying aberration to the camera ray.
    const float3 dir = optixGetWorldRayDirection();
    const float  t   = 0.5f * (dir.y + 1.0f);
    Vec3 color{(1.0f - t) * 1.0f + t * 0.5f,
               (1.0f - t) * 1.0f + t * 0.7f,
               (1.0f - t) * 1.0f + t * 1.0f};

    // Stage 17A.5 / 20H: Doppler colour shift + searchlight
    // beaming applied to the sky base colour. D is read from
    // payload register 3 (set by raygen) instead of being
    // recomputed; output is byte-identical because OptiX 7+
    // guarantees `optixGetWorldRayDirection()` equals the
    // direction the raygen passed to `optixTrace`. At
    // |beta| = 0 the helper is identity so the Stage 17A.4
    // sky matches byte-for-byte.
    const float D = read_payload_doppler();
    color = apply_doppler_and_searchlight_with_D(color, D);

    set_payload_rgb(color.x, color.y, color.z);
}

// ---- miss (shadow) ----------------------------------------------
//
// Stage 20L shadow-ray miss program. Bound to miss SBT record 1
// (radiance miss is bound to record 0). Shadow rays trace with
// OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT
// + missSbtIndex = 1. If the ray escapes (no geometry hit), this
// program runs and sets payload register 0 = 1 (visible). If
// the ray hits anything, neither this miss nor the radiance
// closest-hit fire (closest-hit is disabled by the ray flag);
// payload register 0 stays at the initial value of 0
// (occluded) the raygen / closest-hit caller wrote before the
// trace.

extern "C" __global__ void __miss__shadow() {
    optixSetPayload_0(1u);
}

// ---- closest-hit -------------------------------------------------

extern "C" __global__ void __closesthit__radiance() {
    using rr::math::Vec3;

    // Stage 20G: read per-record material data from the SBT.
    // Layout populated by OptixPipeline::create() (default
    // HitGroupData{} -> shading_mode = 0) and optionally
    // re-uploaded by OptixPipeline::set_hit_material(...)
    // (mode = 1, picked mesh's material params). The default
    // shading_mode = 0 preserves Stage 17A.4's normal-as-
    // color output byte-for-byte for every render entry that
    // does not call set_hit_material(...).
    const auto* hg = static_cast<const rr::optix::HitGroupData*>(
        optixGetSbtDataPointer());

    Vec3 color;
    if (hg != nullptr && hg->shading_mode == 2) {
        // Stage 20K direct lighting: evaluate albedo *
        // (sum of point + directional contributions +
        // ambient) + emission. No shadow rays (matches the
        // CUDA `k_render_scene` Stage 9B precedent: "shadows
        // are deferred"). No textures (Stage 20K rule).
        // Mirrors the CUDA `k_render_scene` direct-lighting
        // shape exactly.
        const float3 ro = optixGetWorldRayOrigin();
        const float3 rd = optixGetWorldRayDirection();
        const float  th = optixGetRayTmax();
        const Vec3   pos{ro.x + rd.x * th,
                         ro.y + rd.y * th,
                         ro.z + rd.z * th};

        // Geometric normal (same form as the path tracer's
        // closest-hit; Stage 20I computed it from
        // optixGetTriangleVertexData).
        const OptixTraversableHandle gas2     = optixGetGASTraversableHandle();
        const unsigned int           pidx     = optixGetPrimitiveIndex();
        const unsigned int           sgi      = optixGetSbtGASIndex();
        float3 verts[3];
        optixGetTriangleVertexData(gas2, pidx, sgi,
                                   /*time=*/0.0f, verts);
        const float3 e1 = make_float3(verts[1].x - verts[0].x,
                                      verts[1].y - verts[0].y,
                                      verts[1].z - verts[0].z);
        const float3 e2 = make_float3(verts[2].x - verts[0].x,
                                      verts[2].y - verts[0].y,
                                      verts[2].z - verts[0].z);
        float3 n_dl = make_float3(e1.y * e2.z - e1.z * e2.y,
                                  e1.z * e2.x - e1.x * e2.z,
                                  e1.x * e2.y - e1.y * e2.x);
        const float len2_dl = n_dl.x * n_dl.x + n_dl.y * n_dl.y
                            + n_dl.z * n_dl.z;
        const float inv_dl  = (len2_dl > 0.0f) ? rsqrtf(len2_dl) : 0.0f;
        n_dl.x *= inv_dl; n_dl.y *= inv_dl; n_dl.z *= inv_dl;
        Vec3 normal{n_dl.x, n_dl.y, n_dl.z};

        // Material data from the SBT hit-record.
        const Vec3 albedo{hg->params.baseColor.x,
                          hg->params.baseColor.y,
                          hg->params.baseColor.z};
        const Vec3 emission{hg->params.emissionColor.x
                                * hg->params.emissionStrength,
                            hg->params.emissionColor.y
                                * hg->params.emissionStrength,
                            hg->params.emissionColor.z
                                * hg->params.emissionStrength};

        Vec3 direct  = Vec3{0.0f, 0.0f, 0.0f};
        Vec3 ambient = Vec3{0.0f, 0.0f, 0.0f};
        bool has_env = false;

        const int n_lights = optixLaunchParams.light_count;
        const auto* lights = optixLaunchParams.lights;
        // Stage 20L: cache the shadow-rays toggle once + a
        // shadow-origin offset along the surface normal to
        // dodge self-intersection. Shadow rays use the
        // single existing ray type but pass
        // OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT |
        // OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, plus
        // missSbtIndex = 1 so the dedicated
        // __miss__shadow program runs when the ray escapes.
        const bool  enable_shadows = optixLaunchParams.enable_shadows;
        const Vec3  shadow_origin{pos.x + normal.x * 1.0e-3f,
                                  pos.y + normal.y * 1.0e-3f,
                                  pos.z + normal.z * 1.0e-3f};

        // Helper lambdas in OptiX device code aren't supported;
        // we inline a small "is_visible" routine per light type
        // below.

        if (n_lights > 0 && lights != nullptr) {
            for (int li = 0; li < n_lights; ++li) {
                const rr::lighting::Light L = lights[li];
                const Vec3 light_color{L.color.x * L.intensity,
                                       L.color.y * L.intensity,
                                       L.color.z * L.intensity};

                if (L.type == rr::lighting::LightType::Directional) {
                    // Photons travel along L.direction; "to-light"
                    // is its negation. (Mirrors CUDA k_render_scene
                    // line 429.)
                    const Vec3 to_light{-L.direction.x,
                                        -L.direction.y,
                                        -L.direction.z};
                    const float ndotl = normal.x * to_light.x
                                      + normal.y * to_light.y
                                      + normal.z * to_light.z;
                    const float lambert = ndotl > 0.0f ? ndotl : 0.0f;

                    // Stage 20L: shadow ray for directional
                    // light. Effectively-infinite tmax. Skip
                    // entirely when ndotl <= 0 (light is
                    // behind the surface; no need to trace).
                    bool visible = true;
                    if (enable_shadows && lambert > 0.0f) {
                        unsigned int v = 0u;  // 0 = occluded
                        optixTrace(
                            optixLaunchParams.scene_handle,
                            make_float3(shadow_origin.x,
                                        shadow_origin.y,
                                        shadow_origin.z),
                            make_float3(to_light.x,
                                        to_light.y,
                                        to_light.z),
                            /*tmin=*/1.0e-3f,
                            /*tmax=*/1.0e30f,
                            /*time=*/0.0f,
                            OptixVisibilityMask(255),
                            OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT
                              | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,
                            /*sbtOffset=*/0,
                            /*sbtStride=*/0,
                            /*missSbtIndex=*/1,
                            v);
                        visible = (v != 0u);
                    }
                    if (visible) {
                        direct.x += light_color.x * lambert;
                        direct.y += light_color.y * lambert;
                        direct.z += light_color.z * lambert;
                    }
                } else if (L.type == rr::lighting::LightType::Point) {
                    // Inverse-square falloff with epsilon floor.
                    const Vec3 delta{L.position.x - pos.x,
                                     L.position.y - pos.y,
                                     L.position.z - pos.z};
                    const float d2  = delta.x * delta.x
                                    + delta.y * delta.y
                                    + delta.z * delta.z;
                    const float falloff_inv = (d2 > 1.0e-4f)
                                            ? d2 : 1.0e-4f;
                    const float dist = sqrtf(falloff_inv);
                    const Vec3 to_light{delta.x / dist,
                                        delta.y / dist,
                                        delta.z / dist};
                    const float ndotl = normal.x * to_light.x
                                      + normal.y * to_light.y
                                      + normal.z * to_light.z;
                    const float lambert = ndotl > 0.0f ? ndotl : 0.0f;
                    const float scale   = lambert / falloff_inv;

                    // Stage 20L: shadow ray for point light.
                    // tmax = distance to light - epsilon so
                    // the ray terminates at the light's
                    // position rather than continuing past
                    // it. Skip when lambert <= 0.
                    bool visible = true;
                    if (enable_shadows && lambert > 0.0f) {
                        unsigned int v = 0u;
                        optixTrace(
                            optixLaunchParams.scene_handle,
                            make_float3(shadow_origin.x,
                                        shadow_origin.y,
                                        shadow_origin.z),
                            make_float3(to_light.x,
                                        to_light.y,
                                        to_light.z),
                            /*tmin=*/1.0e-3f,
                            /*tmax=*/dist - 1.0e-3f,
                            /*time=*/0.0f,
                            OptixVisibilityMask(255),
                            OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT
                              | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,
                            /*sbtOffset=*/0,
                            /*sbtStride=*/0,
                            /*missSbtIndex=*/1,
                            v);
                        visible = (v != 0u);
                    }
                    if (visible) {
                        direct.x += light_color.x * scale;
                        direct.y += light_color.y * scale;
                        direct.z += light_color.z * scale;
                    }
                } else if (L.type == rr::lighting::LightType::Environment) {
                    // Environment ambient is not shadowed in
                    // Stage 20L (it is a directionless flat
                    // term; tracing visibility per env light
                    // requires hemisphere sampling, which
                    // belongs in the path tracer).
                    ambient.x += light_color.x;
                    ambient.y += light_color.y;
                    ambient.z += light_color.z;
                    has_env = true;
                }
                // LightType::Area: PLACEHOLDER per Stage 9B; ignored.
            }
        }

        // Implicit ambient floor when no Environment light is
        // present (matches CUDA k_render_scene line 467-469).
        if (!has_env) {
            ambient.x += 0.05f;
            ambient.y += 0.05f;
            ambient.z += 0.05f;
        }

        color.x = albedo.x * (direct.x + ambient.x) + emission.x;
        color.y = albedo.y * (direct.y + ambient.y) + emission.y;
        color.z = albedo.z * (direct.z + ambient.z) + emission.z;
    } else if (hg != nullptr && hg->shading_mode == 1) {
        // Material flat shading: baseColor + emissionColor *
        // emissionStrength. Stage 20M: when the material has
        // `useBaseColorTexture` AND
        // `baseColorTextureId` is in range AND the launch
        // params carry a textures array + per-vertex UV
        // buffers, sample the texture at the interpolated UV
        // instead of using the flat `params.baseColor`. The
        // emission term is unchanged.
        Vec3 base{hg->params.baseColor.x,
                  hg->params.baseColor.y,
                  hg->params.baseColor.z};
        if (hg->params.useBaseColorTexture
         && hg->params.baseColorTextureId >= 0
         && hg->params.baseColorTextureId
                < optixLaunchParams.texture_count
         && optixLaunchParams.textures      != nullptr
         && optixLaunchParams.mesh_uvs      != nullptr
         && optixLaunchParams.mesh_indices  != nullptr) {
            // Interpolate UV via barycentrics. OptiX returns
            // (b1, b2) from `optixGetTriangleBarycentrics`;
            // b0 = 1 - b1 - b2.
            const float2 bary = optixGetTriangleBarycentrics();
            const float  b1   = bary.x;
            const float  b2   = bary.y;
            const float  b0   = 1.0f - b1 - b2;

            const unsigned int prim_idx = optixGetPrimitiveIndex();
            const auto tri = optixLaunchParams.mesh_indices[prim_idx];

            const auto uv0 = optixLaunchParams.mesh_uvs[tri.v0];
            const auto uv1 = optixLaunchParams.mesh_uvs[tri.v1];
            const auto uv2 = optixLaunchParams.mesh_uvs[tri.v2];

            const rr::math::Vec2 uv{
                uv0.x * b0 + uv1.x * b1 + uv2.x * b2,
                uv0.y * b0 + uv1.y * b1 + uv2.y * b2};

            // Stage 13B.2 nearest-neighbour sampler. The
            // returned magenta on invalid view propagates so
            // the failure is visible in the framebuffer.
            const auto& view =
                optixLaunchParams.textures[hg->params.baseColorTextureId];
            base = rr::cuda::sampleTextureNearest(view, uv);
        }
        color.x = base.x
                + hg->params.emissionColor.x * hg->params.emissionStrength;
        color.y = base.y
                + hg->params.emissionColor.y * hg->params.emissionStrength;
        color.z = base.z
                + hg->params.emissionColor.z * hg->params.emissionStrength;
    } else {
        // Stage 17A.4 default: normal-as-color shading.
        // Recover the triangle's three world-space vertex
        // positions from the GAS metadata.
        const OptixTraversableHandle gas      = optixGetGASTraversableHandle();
        const unsigned int prim_idx           = optixGetPrimitiveIndex();
        const unsigned int sbt_gas_idx        = optixGetSbtGASIndex();
        float3 verts[3];
        optixGetTriangleVertexData(gas, prim_idx, sbt_gas_idx,
                                   /*time=*/0.0f, verts);

        // Geometric normal = normalize(cross(v1 - v0, v2 - v0)).
        const float3 e1 = make_float3(verts[1].x - verts[0].x,
                                      verts[1].y - verts[0].y,
                                      verts[1].z - verts[0].z);
        const float3 e2 = make_float3(verts[2].x - verts[0].x,
                                      verts[2].y - verts[0].y,
                                      verts[2].z - verts[0].z);
        float3 n = make_float3(e1.y * e2.z - e1.z * e2.y,
                               e1.z * e2.x - e1.x * e2.z,
                               e1.x * e2.y - e1.y * e2.x);
        const float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
        const float inv  = (len2 > 0.0f) ? rsqrtf(len2) : 0.0f;
        n.x *= inv; n.y *= inv; n.z *= inv;

        // Same encoding as the CUDA path's normal-as-colour
        // shade: 0.5 * n + 0.5 maps [-1, 1] -> [0, 1].
        color.x = 0.5f * n.x + 0.5f;
        color.y = 0.5f * n.y + 0.5f;
        color.z = 0.5f * n.z + 0.5f;
    }

    // Stage 17A.5 / 20H: Doppler colour shift + searchlight
    // beaming applied to the base shade. D is read from
    // payload register 3 (set by raygen) instead of being
    // recomputed via `optixGetWorldRayDirection()`. Output is
    // byte-identical because the raygen-cached D matches what
    // the closest-hit would compute locally.
    const float D = read_payload_doppler();
    color = apply_doppler_and_searchlight_with_D(color, D);

    set_payload_rgb(color.x, color.y, color.z);
}

// =================================================================
// Stage 20I: minimum-viable OptiX path tracer.
// =================================================================
//
// The path-tracer entry-point family
// (__raygen__pathtrace / __miss__pathtrace /
// __closesthit__pathtrace) is bound by `OptixPipeline::create`
// when `OptixPipelineOptions::path_tracer == true`. Both the
// existing radiance entry-point family and this path-tracer
// family live in the same compiled PTX module; the SBT records
// the host builds bind whichever triple it picked.
//
// Conceptually mirrors `src/cuda/CudaPathTracer.cu`:
//   1. raygen: seed an RNG from (x, y, sample, seed); for each
//      sample run a bounce loop. Each bounce calls optixTrace
//      against the same single-mesh GAS the renderer built.
//   2. closest-hit: fills payload with hit position, geometric
//      normal, and (Stage 20G) the SBT-supplied baseColor as
//      albedo.
//   3. miss: fills payload with the environment radiance
//      (Stage 17A.4 vertical sky gradient).
//   4. raygen accumulates: throughput *= albedo per bounce;
//      radiance += throughput * env on miss.
//   5. After max_bounces or miss, the sample's contribution is
//      added to the per-pixel accumulator and divided by spp
//      at the end.
//
// At default observer (|beta| = 0) Doppler / searchlight
// degenerate to identity, so the relativistic stack composes
// transparently. Stage 20I applies Doppler / searchlight to
// the final per-pixel radiance using the PRIMARY (post-
// aberration) ray direction; per-bounce relativistic effects
// are deferred (the user's "match CUDA conceptually" rule;
// the CUDA path tracer also applies relativistic effects to
// the primary ray's outgoing radiance, not per-bounce).
//
// Path-tracer payload layout (10 registers):
//   p0     status (0 = hit, 1 = miss)
//   p1..p3 hit position xyz (only meaningful on hit)
//   p4..p6 hit normal xyz (hit) OR miss radiance xyz (miss)
//   p7..p9 hit albedo rgb (only meaningful on hit)

namespace {

constexpr unsigned int kPtStatusHit  = 0u;
constexpr unsigned int kPtStatusMiss = 1u;

__device__ __forceinline__ void pt_set_hit(rr::math::Vec3 pos,
                                           rr::math::Vec3 normal,
                                           rr::math::Vec3 albedo) {
    optixSetPayload_0(kPtStatusHit);
    optixSetPayload_1(__float_as_uint(pos.x));
    optixSetPayload_2(__float_as_uint(pos.y));
    optixSetPayload_3(__float_as_uint(pos.z));
    optixSetPayload_4(__float_as_uint(normal.x));
    optixSetPayload_5(__float_as_uint(normal.y));
    optixSetPayload_6(__float_as_uint(normal.z));
    optixSetPayload_7(__float_as_uint(albedo.x));
    optixSetPayload_8(__float_as_uint(albedo.y));
    optixSetPayload_9(__float_as_uint(albedo.z));
}

__device__ __forceinline__ void pt_set_miss(rr::math::Vec3 env_radiance) {
    optixSetPayload_0(kPtStatusMiss);
    optixSetPayload_1(0u);
    optixSetPayload_2(0u);
    optixSetPayload_3(0u);
    optixSetPayload_4(__float_as_uint(env_radiance.x));
    optixSetPayload_5(__float_as_uint(env_radiance.y));
    optixSetPayload_6(__float_as_uint(env_radiance.z));
    optixSetPayload_7(0u);
    optixSetPayload_8(0u);
    optixSetPayload_9(0u);
}

// Build a world-space direction from a tangent-space sample
// against a world-space surface normal. Mirrors
// `align_to_normal` in `src/cuda/CudaPathTracer.cu` so the
// OptiX path's bounce directions match the CUDA path's for
// the same RNG sequence.
__device__ __forceinline__ rr::math::Vec3
pt_align_to_normal(rr::math::Vec3 local, rr::math::Vec3 n) {
    using rr::math::Vec3;
    using rr::math::cross;
    using rr::math::normalize;
    Vec3 helper = (fabsf(n.z) < 0.999f) ? Vec3{0.0f, 0.0f, 1.0f}
                                        : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 t = normalize(cross(helper, n));
    Vec3 b = cross(n, t);
    return t * local.x + b * local.y + n * local.z;
}

// Stage 17A.4 sky gradient (used by both __miss__radiance and
// __miss__pathtrace). Factored out so both programs see the
// same environment.
__device__ __forceinline__ rr::math::Vec3
pt_environment_radiance(rr::math::Vec3 dir_world) {
    const float t = 0.5f * (dir_world.y + 1.0f);
    return rr::math::Vec3{(1.0f - t) * 1.0f + t * 0.5f,
                          (1.0f - t) * 1.0f + t * 0.7f,
                          (1.0f - t) * 1.0f + t * 1.0f};
}

}  // namespace

// ---- raygen (path tracer) ----------------------------------------

extern "C" __global__ void __raygen__pathtrace() {
    using rr::math::Vec3;

    const uint3 idx = optixGetLaunchIndex();
    const int   x   = static_cast<int>(idx.x);
    const int   y   = static_cast<int>(idx.y);

    const int W = optixLaunchParams.width;
    const int H = optixLaunchParams.height;
    if (x >= W || y >= H) return;

    float* fb = optixLaunchParams.framebuffer;
    if (fb == nullptr) return;

    const int pix = (y * W + x) * 4;

    if (optixLaunchParams.scene_handle == 0) {
        // No GAS bound: write a flat colour and return. The
        // path-tracer has nothing to trace.
        fb[pix + 0] = optixLaunchParams.flat_color_r;
        fb[pix + 1] = optixLaunchParams.flat_color_g;
        fb[pix + 2] = optixLaunchParams.flat_color_b;
        fb[pix + 3] = 1.0f;
        return;
    }

    const int spp         = max(1, optixLaunchParams.spp);
    const int max_bounces = max(1, optixLaunchParams.max_bounces);

    // Stage 18A.3: precomputed relativity invariants reused
    // across the spp loop.
    const auto rel = rr::relativity::precompute_relativity(
        optixLaunchParams.observer.velocity);

    Vec3 rgb_sum{0.0f, 0.0f, 0.0f};
    Vec3 primary_dir{0.0f, 0.0f, -1.0f};

    for (int sample = 0; sample < spp; ++sample) {
        // Stage 20J: combine the host-supplied
        // `optixLaunchParams.sample_index` with the in-raygen
        // loop counter so the same RNG sequence is produced
        // regardless of whether the host runs ONE launch with
        // spp = N (sample_index = 0) or N launches with
        // spp = 1 (sample_index = 0..N-1). This makes the
        // Stage 20I single-launch path and the Stage 20J
        // progressive multi-launch path bit-identical for the
        // same total sample count.
        const std::uint32_t combined_seed_idx =
            optixLaunchParams.sample_index +
            static_cast<std::uint32_t>(sample);
        rr::pathtracer::Rng rng = rr::pathtracer::make_pixel_rng(
            static_cast<unsigned int>(x),
            static_cast<unsigned int>(y),
            combined_seed_idx,
            static_cast<std::uint64_t>(optixLaunchParams.seed));

        auto ray = rr::camera::generate_camera_ray(
            optixLaunchParams.camera, x, y, W, H);

        if (optixLaunchParams.params.enable_aberration) {
            ray.direction = rr::relativity::aberrateDirection(
                rel, ray.direction);
        }
        primary_dir = ray.direction;

        Vec3 throughput{1.0f, 1.0f, 1.0f};
        Vec3 radiance{0.0f, 0.0f, 0.0f};

        for (int bounce = 0; bounce < max_bounces; ++bounce) {
            unsigned int p0=0u, p1=0u, p2=0u, p3=0u, p4=0u,
                         p5=0u, p6=0u, p7=0u, p8=0u, p9=0u;
            optixTrace(
                optixLaunchParams.scene_handle,
                make_float3(ray.origin.x,    ray.origin.y,    ray.origin.z),
                make_float3(ray.direction.x, ray.direction.y, ray.direction.z),
                /*tmin=*/1.0e-3f, /*tmax=*/1.0e30f,
                /*time=*/0.0f, OptixVisibilityMask(255),
                OPTIX_RAY_FLAG_NONE,
                /*sbtOffset=*/0, /*sbtStride=*/0, /*missSbtIndex=*/0,
                p0, p1, p2, p3, p4, p5, p6, p7, p8, p9);

            if (p0 == kPtStatusMiss) {
                const Vec3 env{__uint_as_float(p4),
                               __uint_as_float(p5),
                               __uint_as_float(p6)};
                radiance.x += throughput.x * env.x;
                radiance.y += throughput.y * env.y;
                radiance.z += throughput.z * env.z;
                break;
            }

            const Vec3 hit_pos{__uint_as_float(p1),
                               __uint_as_float(p2),
                               __uint_as_float(p3)};
            const Vec3 hit_n  {__uint_as_float(p4),
                               __uint_as_float(p5),
                               __uint_as_float(p6)};
            const Vec3 albedo {__uint_as_float(p7),
                               __uint_as_float(p8),
                               __uint_as_float(p9)};

            if (bounce + 1 >= max_bounces) break;

            const auto u2 = rr::pathtracer::next_vec2(rng);
            const Vec3 local_dir =
                rr::pathtracer::sample_cosine_hemisphere(u2);
            const Vec3 world_dir = pt_align_to_normal(local_dir, hit_n);

            throughput.x *= albedo.x;
            throughput.y *= albedo.y;
            throughput.z *= albedo.z;

            ray.origin    = hit_pos + hit_n * 1.0e-4f;
            ray.direction = world_dir;
        }

        rgb_sum.x += radiance.x;
        rgb_sum.y += radiance.y;
        rgb_sum.z += radiance.z;
    }

    const float inv_spp = 1.0f / static_cast<float>(spp);
    Vec3 rgb{rgb_sum.x * inv_spp,
             rgb_sum.y * inv_spp,
             rgb_sum.z * inv_spp};

    // Stage 17A.5 / 20H: apply Doppler colour shift +
    // searchlight beaming using the primary post-aberration
    // direction. Identity at |beta| = 0.
    {
        const float D = rr::relativity::dopplerFactor(rel, primary_dir);
        rgb = apply_doppler_and_searchlight_with_D(rgb, D);
    }

    fb[pix + 0] = rgb.x;
    fb[pix + 1] = rgb.y;
    fb[pix + 2] = rgb.z;
    fb[pix + 3] = 1.0f;
}

// ---- miss (path tracer) ------------------------------------------

extern "C" __global__ void __miss__pathtrace() {
    const float3 dir = optixGetWorldRayDirection();
    const rr::math::Vec3 dir_v{dir.x, dir.y, dir.z};
    pt_set_miss(pt_environment_radiance(dir_v));
}

// ---- closest-hit (path tracer) -----------------------------------

extern "C" __global__ void __closesthit__pathtrace() {
    using rr::math::Vec3;

    const float3 ro = optixGetWorldRayOrigin();
    const float3 rd = optixGetWorldRayDirection();
    const float  th = optixGetRayTmax();
    const Vec3   pos{ro.x + rd.x * th,
                     ro.y + rd.y * th,
                     ro.z + rd.z * th};

    const OptixTraversableHandle gas      = optixGetGASTraversableHandle();
    const unsigned int prim_idx           = optixGetPrimitiveIndex();
    const unsigned int sbt_gas_idx        = optixGetSbtGASIndex();
    float3 verts[3];
    optixGetTriangleVertexData(gas, prim_idx, sbt_gas_idx,
                               /*time=*/0.0f, verts);
    const float3 e1 = make_float3(verts[1].x - verts[0].x,
                                  verts[1].y - verts[0].y,
                                  verts[1].z - verts[0].z);
    const float3 e2 = make_float3(verts[2].x - verts[0].x,
                                  verts[2].y - verts[0].y,
                                  verts[2].z - verts[0].z);
    float3 n = make_float3(e1.y * e2.z - e1.z * e2.y,
                           e1.z * e2.x - e1.x * e2.z,
                           e1.x * e2.y - e1.y * e2.x);
    const float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
    const float inv  = (len2 > 0.0f) ? rsqrtf(len2) : 0.0f;
    n.x *= inv; n.y *= inv; n.z *= inv;
    Vec3 normal{n.x, n.y, n.z};

    // Flip the normal so it faces against the incident ray.
    if (n.x * rd.x + n.y * rd.y + n.z * rd.z > 0.0f) {
        normal = Vec3{-normal.x, -normal.y, -normal.z};
    }

    Vec3 albedo{0.8f, 0.8f, 0.8f};
    const auto* hg = static_cast<const rr::optix::HitGroupData*>(
        optixGetSbtDataPointer());
    if (hg != nullptr) {
        albedo.x = hg->params.baseColor.x;
        albedo.y = hg->params.baseColor.y;
        albedo.z = hg->params.baseColor.z;
    }

    pt_set_hit(pos, normal, albedo);
}

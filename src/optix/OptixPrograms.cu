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
#include "math/Vec3.h"
#include "optix/OptixLaunchParams.h"
#include "optix/OptixSBT.h"           // Stage 20G: HitGroupData
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
    if (hg != nullptr && hg->shading_mode == 1) {
        // Material flat shading: baseColor + emissionColor *
        // emissionStrength. Closest-hit does not consult
        // textures (Stage 20G: "no textures yet"), so
        // useBaseColorTexture / baseColorTextureId are
        // intentionally ignored here.
        color.x = hg->params.baseColor.x
                + hg->params.emissionColor.x * hg->params.emissionStrength;
        color.y = hg->params.baseColor.y
                + hg->params.emissionColor.y * hg->params.emissionStrength;
        color.z = hg->params.baseColor.z
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

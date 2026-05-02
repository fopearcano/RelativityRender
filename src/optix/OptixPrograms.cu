// Stage 17A.3 / 17A.4 OptiX device programs per
// `docs/OPTIX_BACKEND_PLAN.md` §20. Compiled to PTX by the
// build system (`nvcc --ptx`) and embedded into rr_optix as a
// host-side C-string via `cmake/EmbedPtxAsHeader.cmake`.
//
// Stage 17A.3 shipped a `__raygen__pinhole` that wrote a flat
// colour and an empty `__miss__radiance`. Stage 17A.4 extends
// the surface:
// - `__raygen__pinhole` now branches on
//   `optixLaunchParams.scene_handle`: when 0 it writes the
//   flat colour (Stage 17A.3 backwards compatibility); when
//   non-zero it generates a primary ray via
//   `rr::camera::generate_camera_ray` (the same RR_HD inline
//   the CUDA path uses) and calls `optixTrace`.
// - `__miss__radiance` now writes a vertical sky gradient
//   (matching the CUDA `--render-triangle` miss shade) into
//   the 3 payload registers.
// - `__closesthit__radiance` (new) reads the triangle's
//   geometric normal via `optixGetTriangleVertexData(...)`,
//   normalises it, and writes `0.5 * n + 0.5` into the 3
//   payload registers - matching the CUDA path's
//   normal-as-colour shading.
//
// Stage 17A.4 still NO bounce loop, NO RNG, NO materials,
// NO any-hit, NO intersection program (built-in triangle
// intersection is used). The `numPayloadValues = 3` setting
// in the host-side pipeline compile options matches the 3
// `optixSetPayload_N` slots used here.
//
// All per-pixel work runs on the GPU; the host's only role is
// uploading launch parameters + downloading the framebuffer.

#include <optix.h>

#include "camera/CameraRay.h"
#include "math/Vec3.h"
#include "optix/OptixLaunchParams.h"

extern "C" __constant__ rr::optix::OptixLaunchParams optixLaunchParams;

// ---- payload helpers (3 RGB floats → 3 payload registers) -------

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
    const auto ray = rr::camera::generate_camera_ray(
        optixLaunchParams.camera, x, y, W, H);

    // Trace. 3 payload registers hold the resulting RGB.
    unsigned int p0 = 0u, p1 = 0u, p2 = 0u;
    optixTrace(
        optixLaunchParams.scene_handle,
        make_float3(ray.origin.x,    ray.origin.y,    ray.origin.z),
        make_float3(ray.direction.x, ray.direction.y, ray.direction.z),
        /*tmin=*/0.0f, /*tmax=*/1.0e30f,
        /*time=*/0.0f, OptixVisibilityMask(255),
        OPTIX_RAY_FLAG_NONE,
        /*sbtOffset=*/0, /*sbtStride=*/0, /*missSbtIndex=*/0,
        p0, p1, p2);

    const auto rgb = read_payload_rgb(p0, p1, p2);
    fb[pix + 0] = rgb.x;
    fb[pix + 1] = rgb.y;
    fb[pix + 2] = rgb.z;
    fb[pix + 3] = 1.0f;
}

// ---- miss --------------------------------------------------------

extern "C" __global__ void __miss__radiance() {
    // Vertical sky gradient that visually matches the CUDA
    // path's miss shade: t = 0.5*(dir.y + 1); colour =
    // lerp(white, light-blue, t).
    const float3 dir = optixGetWorldRayDirection();
    const float  t   = 0.5f * (dir.y + 1.0f);
    const float  r   = (1.0f - t) * 1.0f + t * 0.5f;
    const float  g   = (1.0f - t) * 1.0f + t * 0.7f;
    const float  b   = (1.0f - t) * 1.0f + t * 1.0f;
    set_payload_rgb(r, g, b);
}

// ---- closest-hit -------------------------------------------------

extern "C" __global__ void __closesthit__radiance() {
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

    // Same encoding as the CUDA path's normal-as-colour shade:
    // 0.5 * n + 0.5 maps [-1, 1] -> [0, 1].
    set_payload_rgb(0.5f * n.x + 0.5f,
                    0.5f * n.y + 0.5f,
                    0.5f * n.z + 0.5f);
}

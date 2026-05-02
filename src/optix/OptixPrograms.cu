// Stage 17A.3 OptiX device programs per
// `docs/OPTIX_BACKEND_PLAN.md` §20. Compiled to PTX by the
// build system (`nvcc --ptx`) and embedded into rr_optix as a
// host-side C-string via `cmake/EmbedPtxAsHeader.cmake`.
//
// Stage 17A.3 scope: a `__raygen__` that writes a flat colour
// to the framebuffer + an empty `__miss__`. NO `optixTrace`,
// NO closest-hit, NO any-hit, NO intersection. The raygen is
// the only program that does work; the miss exists only to
// satisfy the SBT layout the pipeline-creation code wires up.
//
// All per-pixel work runs on the GPU; the host's only role is
// uploading launch parameters + downloading the framebuffer.
// This matches the master rule "All per-pixel/per-ray rendering
// must happen on GPU".

#include <optix.h>

#include "optix/OptixLaunchParams.h"

extern "C" __constant__ rr::optix::OptixLaunchParams optixLaunchParams;

extern "C" __global__ void __raygen__pinhole() {
    // Stage 17A.3 raygen: pinhole-style launch index → pixel
    // → flat-colour write. No camera math yet (the camera POD
    // joins the launch params in a follow-up sub-stage when
    // optixTrace lands).
    const uint3 idx = optixGetLaunchIndex();
    const int   x   = static_cast<int>(idx.x);
    const int   y   = static_cast<int>(idx.y);

    const int W = optixLaunchParams.width;
    const int H = optixLaunchParams.height;
    if (x >= W || y >= H) return;

    float* fb = optixLaunchParams.framebuffer;
    if (fb == nullptr) return;

    const int pix = (y * W + x) * 4;
    fb[pix + 0] = optixLaunchParams.flat_color_r;
    fb[pix + 1] = optixLaunchParams.flat_color_g;
    fb[pix + 2] = optixLaunchParams.flat_color_b;
    fb[pix + 3] = 1.0f;
}

extern "C" __global__ void __miss__radiance() {
    // Required for SBT layout but unreached at this stage
    // because the raygen does not call optixTrace. Kept empty;
    // future sub-stages add the real sky-tint logic on top of
    // this hook.
}

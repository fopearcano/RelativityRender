#pragma once

#include <cstdint>

// Stage 17A.3 OptiX launch-parameters POD per
// `docs/OPTIX_BACKEND_PLAN.md` §23. The struct is shared by host
// and device code: the host (`OptixPipeline`) populates an
// instance, copies it to a device-resident buffer, and passes the
// device pointer through `optixLaunch`'s `pipelineParams`
// argument; the device (`OptixPrograms.cu`) reads it from the
// `optixLaunchParams` `__constant__` symbol.
//
// Stage 17A.3 scope: the minimum set the raygen needs to write a
// flat colour to a framebuffer. NO scene_handle yet (no
// optixTrace), NO RNG state (no path tracer), NO AOV pointers
// (Stage 14A.3's CudaSceneView lives in the CUDA path; OptiX
// integration joins later). Subsequent 17A+ sub-stages grow this
// POD as new responsibilities land.
//
// Header-only, host-friendly: no `<optix.h>` / `<cuda_runtime.h>`
// include - the device-side `.cu` includes those itself before
// it pulls this header.

namespace rr::optix {

struct OptixLaunchParams {
    // Output framebuffer: width * height * 4 floats, channel-
    // interleaved row-major top-left origin (matches every
    // existing `Image::PixelFormat::Rgba32F` buffer in the
    // project).
    float* framebuffer = nullptr;

    // Framebuffer dimensions in pixels.
    std::int32_t width  = 0;
    std::int32_t height = 0;

    // Flat RGB colour the Stage 17A.3 raygen writes to every
    // pixel. Alpha is hard-coded to 1.0f in the kernel (the
    // launch params struct is layout-friendly without an alpha
    // slot). Future sub-stages replace this with real radiance
    // computed per-ray.
    float        flat_color_r = 1.0f;
    float        flat_color_g = 0.0f;
    float        flat_color_b = 1.0f;
};

}  // namespace rr::optix

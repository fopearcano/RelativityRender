#pragma once

// Host-callable launcher declaration for the Stage 11C minimal
// diffuse GPU path tracer.
//
// The launcher takes `const rr::gpu::GpuScene&` (host-friendly,
// no CUDA-runtime includes) rather than the device-side
// `CudaSceneView`. The `.cu` file builds the view internally
// from the GpuScene's accessor methods before launching the
// kernel. Doing the bridge in the `.cu` keeps this header pure
// host C++, so `pathtracer/PathTracer.cpp` can include it
// without forcing nvcc on that translation unit (mirrors
// `cuda/CudaAccumulation.cuh`'s host-friendly stance).
//
// The kernel produces ONE sample per pixel per launch; the
// `PathTracer` host orchestration loops `samples_per_pixel`
// times and accumulates the result through the Stage 11B
// `AccumulationBuffer`. Splitting the spp loop host-side keeps
// the kernel short and reuses the progressive-accumulation
// infrastructure unchanged.

#include "math/Vec3.h"

namespace rr::gpu { class GpuScene; }

namespace rr::cuda {

// Render one sample per pixel into `device_sample_pixels` (an
// Rgba32F device buffer of width*height pixels).
//
//   `scene`            already-uploaded GpuScene. The launcher
//                      reads its accessor methods on the host
//                      to build the kernel's launch argument.
//   `max_bounces`      bounce budget. >= 0; 0 produces a black
//                      frame, 1 traces only the primary ray
//                      (emission + environment hits accounted
//                      for, no diffuse bounce generated).
//   `seed`             global RNG seed mixed into every pixel's
//                      `make_pixel_rng`.
//   `sample_index`     mixed in alongside `seed` so each spp
//                      iteration produces a decorrelated sample
//                      stream.
//   `env_color`        environment-fallback radiance. Multiplied
//                      by `env_intensity` and the running
//                      throughput on a miss.
//   `env_intensity`    scalar multiplier on `env_color`.
//
// Returns false on launch failure (drains the sticky
// `cudaGetLastError` so a later real CUDA call sees a clean
// state). All per-pixel work happens on the device; the host
// never iterates over rays.
[[nodiscard]] bool launch_pathtrace_sample(float*                   device_sample_pixels,
                                           int                      width,
                                           int                      height,
                                           const rr::gpu::GpuScene& scene,
                                           int                      max_bounces,
                                           unsigned int             seed,
                                           unsigned int             sample_index,
                                           rr::math::Vec3           env_color,
                                           float                    env_intensity);

}  // namespace rr::cuda

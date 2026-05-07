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
//   `firefly_clamp`    PT-P.24: per-channel firefly clamp on
//                      the per-sample radiance. 0.0f disables
//                      the clamp (default; every
//                      PathTraceConfig{} value passes 0.0f);
//                      > 0 produces a `fminf(radiance.x|y|z,
//                      firefly_clamp)` per channel before the
//                      per-pixel write. See
//                      `PathTraceConfig::firefly_clamp` for
//                      the authoring contract; the OptiX
//                      backend mirrors the same clamp via
//                      `OptixLaunchParams::firefly_clamp`.
//                      Negative values cause launch failure.
//   `enable_nee`       NEE.2: enable explicit direct-light
//                      sampling at every bounce vertex.
//                      `false` (default for every existing
//                      caller via `PathTraceConfig::enable_nee
//                      = false`) keeps the kernel emission +
//                      environment-only — the kernel-side
//                      guard `if (enable_nee && light_count
//                      > 0)` is not entered, no shadow ray
//                      is traced, no extra RNG draw is
//                      performed, and the per-pixel
//                      arithmetic is byte-identical with the
//                      pre-NEE build. `true` invokes
//                      `pathtracer::sample_direct_light_uniform`
//                      against the scene's `lights` array
//                      and traces an any-hit shadow ray per
//                      vertex; the visibility-modulated
//                      contribution is added to `radiance`
//                      before the firefly-clamp + per-pixel
//                      write. See `PathTraceConfig::enable_nee`
//                      for the full design contract.
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
                                           float                    env_intensity,
                                           float                    firefly_clamp,
                                           bool                     enable_nee);

}  // namespace rr::cuda

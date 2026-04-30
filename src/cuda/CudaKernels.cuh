#pragma once

// Kernel-side helpers and host-callable launch wrappers shared across
// `.cu` translation units in the CUDA backend. This header pulls in
// `<cuda_runtime.h>`, so it is only safe to include from `.cu` files.

#include "camera/CameraRay.h"     // GpuCamera + RR_HD generate_camera_ray
#include "cuda/CudaScene.cuh"     // CudaSceneView (passed by value to k_render_scene)
#include "geometry/Sphere.h"      // Sphere POD passed by value to single-sphere launchers
#include "relativity/RelativityParams.h"  // Observer + RelativityParams PODs

#include <cuda_runtime.h>

namespace rr::cuda {

// Host-callable launcher for the gradient diagnostic kernel.
// Defined in CudaTestKernel.cu. Writes width*height Rgba32F pixels
// into `device_pixels` (channel-interleaved, row-major, top-left
// origin):
//   R = u = x / (width  - 1)
//   G = v = y / (height - 1)
//   B = 0
//   A = 1
// All per-pixel work happens on the device; the host only allocates,
// launches, and downloads.
void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream = 0);

// Host-callable launcher for the camera-ray visualisation kernel.
// Defined in CudaTestKernel.cu. For each pixel, the GPU generates the
// primary pinhole ray via `rr::camera::generate_camera_ray(cam, ...)`
// and encodes the (already-normalised) direction as RGB by mapping
// each component from [-1, 1] to [0, 1]. Alpha is 1. Every per-pixel
// step happens on the device; the host only uploads the GpuCamera
// POD as a launch argument, launches, and downloads.
void launch_camera_rays_visualize(float* device_pixels, int width, int height,
                                  rr::camera::GpuCamera cam,
                                  cudaStream_t stream = 0);

// Host-callable launcher for the sphere-intersection kernel. Defined
// in CudaTestKernel.cu. For each pixel: generate the primary ray on
// the device, intersect against `sphere`, and write either a
// normal-as-color shade (`0.5*n + 0.5`) on hit or a simple vertical
// sky gradient on miss. The CPU never touches per-pixel state - it
// only uploads the camera + sphere PODs as launch arguments.
void launch_sphere_visualize(float* device_pixels, int width, int height,
                             rr::camera::GpuCamera   cam,
                             rr::geometry::Sphere    sphere,
                             cudaStream_t            stream = 0);

// Host-callable launcher for the relativistic single-sphere kernel.
// Defined in CudaTestKernel.cu. Per pixel, the device runs the full
// relativistic perception pipeline:
//   1. Generate the primary camera ray (rr::camera::generate_camera_ray).
//   2. (If `params.enable_aberration`) apply Lorentz aberration to the
//      ray's direction in the observer's frame.
//   3. Intersect the (possibly aberrated) ray against `sphere`.
//   4. Compute a base shade: `0.5*n + 0.5` on hit; vertical sky
//      gradient on miss.
//   5. Compute the Doppler factor D for the (possibly aberrated)
//      ray direction.
//   6. (If `params.enable_doppler`) apply the artistic Doppler
//      colour shift, modulated by `params.doppler_color_strength`.
//   7. (If `params.enable_searchlight`) scale the colour by
//      `1 + (D^4 - 1) * params.searchlight_strength` (relativistic
//      beaming).
//   8. Write the framebuffer.
// The CPU never touches per-ray state.
void launch_sphere_relativistic(float* device_pixels, int width, int height,
                                rr::camera::GpuCamera           cam,
                                rr::relativity::Observer        observer,
                                rr::relativity::RelativityParams params,
                                rr::geometry::Sphere            sphere,
                                cudaStream_t                    stream = 0);

// Host-callable launcher for the multi-sphere scene-render kernel.
// Defined in CudaTestKernel.cu. Per pixel the device runs the full
// relativistic pipeline (aberration -> closest-hit loop over the
// uploaded sphere array -> base shade -> Doppler colour ->
// searchlight beaming) and writes Rgba32F. The CPU never touches
// per-ray state; the launch argument `scene` carries the camera /
// observer / params plus a device pointer + count for the spheres.
void launch_render_scene(float* device_pixels, int width, int height,
                         CudaSceneView scene,
                         cudaStream_t  stream = 0);

// Host-callable launcher for the Stage 11A RNG / sampling
// validation kernel. Defined in `CudaRngTestKernel.cu`. Splits
// the framebuffer into four quadrants, each driven by one of the
// four `pathtracer::*` primitives:
//
//   TL: next_float                  -> grayscale white noise
//   TR: next_vec2                   -> r=u.x, g=u.y, b=0
//   BL: sample_uniform_hemisphere   -> dir encoded as colour
//   BR: sample_cosine_hemisphere    -> dir encoded as colour
//
// `global_seed` mixes through `make_pixel_rng` per pixel so
// re-running with a different seed produces a fresh noise field.
// All per-pixel work happens on the device.
void launch_rng_test_visualize(float* device_pixels, int width, int height,
                               unsigned int global_seed,
                               cudaStream_t stream = 0);

}

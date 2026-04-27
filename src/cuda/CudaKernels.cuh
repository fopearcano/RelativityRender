#pragma once

// Kernel-side helpers and host-callable launch wrappers shared across
// `.cu` translation units in the CUDA backend. This header is only safe
// to include from `.cu` files because it pulls in `cuda_runtime.h`.

#include <cuda_runtime.h>

#include "camera/CameraRay.h"
#include "geometry/Sphere.h"
#include "relativity/RelativityParams.h"

namespace rr::cuda {

// Host-callable launch wrapper for the gradient test kernel.
// Defined in CudaTestKernel.cu. Writes width*height Rgba32F pixels into
// `device_pixels` (channel-interleaved, R = u, G = v, B = 0, A = 1).
void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream = 0);

// Host-callable launch wrapper for the camera-ray visualisation kernel.
// Defined in CudaTestKernel.cu. For each pixel, generates the primary
// pinhole ray from `cam` via `rr::camera::generate_camera_ray` and
// encodes the (normalised) direction into RGB by mapping each
// component from [-1, 1] to [0, 1]. Alpha is 1.
void launch_camera_rays_visualize(float* device_pixels, int width, int height,
                                  rr::camera::GpuCamera cam,
                                  cudaStream_t stream = 0);

// Host-callable launch wrapper for the sphere-intersection kernel.
// Defined in CudaTestKernel.cu. For each pixel: generate the primary
// ray, intersect against `sphere`, and write either a normal-as-color
// shade (`0.5*n + 0.5`) on hit or a vertical-gradient sky on miss.
// All per-ray work happens on the GPU.
void launch_sphere_visualize(float* device_pixels, int width, int height,
                             rr::camera::GpuCamera cam,
                             rr::geometry::Sphere  sphere,
                             cudaStream_t stream = 0);

// Host-callable launch wrapper for the relativistic-sphere kernel.
// Defined in CudaTestKernel.cu. Per pixel:
//   1. Generate the primary camera ray.
//   2. (If `params.enable_aberration`) apply Lorentz aberration to the
//      ray's direction in the observer's frame.
//   3. Intersect the (possibly aberrated) ray against `sphere`.
//   4. Compute base shading (`0.5*n + 0.5` on hit; sky gradient on
//      miss).
//   5. (If `params.enable_doppler`) apply the artistic Doppler colour
//      shift, modulated by `params.doppler_color_strength`.
//   6. (If `params.enable_searchlight`) scale the colour by
//      `lerp(1, D^4, params.searchlight_strength)` (relativistic
//      beaming).
//   7. Write the framebuffer.
// The CPU never touches per-ray state - the entire pipeline runs on
// the device.
void launch_sphere_relativistic(float* device_pixels, int width, int height,
                                rr::camera::GpuCamera           cam,
                                rr::relativity::Observer        observer,
                                rr::relativity::RelativityParams params,
                                rr::geometry::Sphere            sphere,
                                cudaStream_t stream = 0);

}



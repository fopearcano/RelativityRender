#pragma once

// Kernel-side helpers and host-callable launch wrappers shared across
// `.cu` translation units in the CUDA backend. This header pulls in
// `<cuda_runtime.h>`, so it is only safe to include from `.cu` files.

#include "camera/CameraRay.h"  // GpuCamera + RR_HD generate_camera_ray

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

}

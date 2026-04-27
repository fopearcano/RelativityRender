#pragma once

// Kernel-side helpers and host-callable launch wrappers shared across
// `.cu` translation units in the CUDA backend. This header is only safe
// to include from `.cu` files because it pulls in `cuda_runtime.h`.

#include <cuda_runtime.h>

#include "camera/CameraRay.h"
#include "geometry/Sphere.h"

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

}



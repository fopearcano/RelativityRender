#include "cuda/CudaRenderer.h"

#include "cuda/CudaKernels.cuh"
#include "gpu/GpuBuffer.h"
#include "image/Color.h"

#include <cuda_runtime.h>

#include <cstddef>
#include <string>
#include <utility>

namespace rr::cuda {

namespace {

std::string cuda_error_string(cudaError_t e) {
    const char* s = cudaGetErrorString(e);
    return s ? std::string(s) : std::string("unknown CUDA error");
}

}

namespace {

// Shared kernel-driven render scaffold: validate dims, allocate the
// device buffer, run `launch_kernel` (which is expected to enqueue one
// or more kernels writing the Rgba32F framebuffer), drain CUDA errors,
// and download into a host Image. `launch_kernel` does the per-pixel
// work; the host never iterates over pixels here.
template <typename Launch>
CudaRenderer::Result run_kernel_render(int width, int height, Launch&& launch_kernel) {
    CudaRenderer::Result result;

    if (width <= 0 || height <= 0) {
        result.message = "invalid dimensions";
        return result;
    }

    (void)cudaGetLastError();  // clear any sticky error

    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    const std::size_t float_count = pixel_count * 4;  // Rgba32F

    rr::gpu::GpuBuffer<float> dev;
    if (!dev.allocate(float_count)) {
        result.message = "device allocation failed";
        return result;
    }

    launch_kernel(dev.device_ptr(), width, height);

    if (const auto launch_err = cudaGetLastError(); launch_err != cudaSuccess) {
        result.message = "kernel launch failed: " + cuda_error_string(launch_err);
        return result;
    }
    if (const auto sync_err = cudaDeviceSynchronize(); sync_err != cudaSuccess) {
        result.message = "kernel sync failed: " + cuda_error_string(sync_err);
        (void)cudaGetLastError();
        return result;
    }

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (!dev.download(img.data(), img.size_in_floats())) {
        result.message = "device->host copy failed";
        return result;
    }

    result.image = std::move(img);
    result.ok    = true;
    return result;
}

}

CudaRenderer::Result CudaRenderer::render_gradient(int width, int height) {
    return run_kernel_render(width, height,
        [](float* device_pixels, int w, int h) {
            launch_gradient_rgba32f(device_pixels, w, h, /*stream=*/nullptr);
        });
}

CudaRenderer::Result CudaRenderer::render_camera_rays(const rr::camera::Camera& camera,
                                                     int width, int height) {
    const auto cam = camera.to_gpu();
    return run_kernel_render(width, height,
        [cam](float* device_pixels, int w, int h) {
            launch_camera_rays_visualize(device_pixels, w, h, cam, /*stream=*/nullptr);
        });
}

}


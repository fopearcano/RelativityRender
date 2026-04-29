#include "cuda/CudaRenderer.h"

#include "gpu/GpuBuffer.h"
#include "image/Image.h"

#include <cuda_runtime.h>

#include <cstddef>
#include <string>
#include <utility>

namespace rr::cuda {

// Forward-declared launcher; implementation is in `CudaGradientKernel.cu`
// where the `__global__` kernel + grid layout live. Kept off this TU so
// `CudaRenderer.cu` compiles in plain C++ mode (no `<<<...>>>` syntax).
void launch_gradient_rgba32f(float* device_pixels, int width, int height,
                             cudaStream_t stream);

namespace {

std::string cuda_error_string(cudaError_t e) {
    const char* s = cudaGetErrorString(e);
    return s ? std::string(s) : std::string("unknown CUDA error");
}

}

CudaRenderer::Result CudaRenderer::render_gradient(int width, int height) {
    Result result;

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

    launch_gradient_rgba32f(dev.device_ptr(), width, height, /*stream=*/nullptr);

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

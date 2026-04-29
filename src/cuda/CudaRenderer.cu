#include "cuda/CudaRenderer.h"

#include "camera/Camera.h"
#include "cuda/CudaKernels.cuh"
#include "cuda/CudaScene.cuh"
#include "geometry/Sphere.h"
#include "gpu/GpuBuffer.h"
#include "gpu/GpuScene.h"
#include "image/Image.h"
#include "relativity/RelativityParams.h"

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

// Shared kernel-driven render scaffold: validate dims, allocate the
// device buffer, run `launch_kernel` (which is expected to enqueue
// one or more kernels writing the Rgba32F framebuffer), drain CUDA
// errors, and download into a host Image. `launch_kernel` does the
// per-pixel work; the host never iterates over pixels here.
template <typename Launch>
CudaRenderer::Result run_kernel_render(int width, int height,
                                       Launch&& launch_kernel) {
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

}  // namespace

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
            launch_camera_rays_visualize(device_pixels, w, h, cam,
                                         /*stream=*/nullptr);
        });
}

CudaRenderer::Result CudaRenderer::render_sphere(const rr::camera::Camera&   camera,
                                                 const rr::geometry::Sphere& sphere,
                                                 int width, int height) {
    if (sphere.radius <= 0.0f) {
        Result r;
        r.message = "sphere radius must be positive";
        return r;
    }
    const auto cam = camera.to_gpu();
    return run_kernel_render(width, height,
        [cam, sphere](float* device_pixels, int w, int h) {
            launch_sphere_visualize(device_pixels, w, h, cam, sphere,
                                    /*stream=*/nullptr);
        });
}

CudaRenderer::Result CudaRenderer::render_relativistic_sphere(
        const rr::camera::Camera&             camera,
        const rr::relativity::Observer&       observer,
        const rr::relativity::RelativityParams& params,
        const rr::geometry::Sphere&           sphere,
        int width, int height) {
    if (sphere.radius <= 0.0f) {
        Result r;
        r.message = "sphere radius must be positive";
        return r;
    }
    const auto cam = camera.to_gpu();
    // Capture observer / params by value into the launch lambda so
    // the kernel sees a self-contained POD bundle.
    const auto obs = observer;
    const auto par = params;
    return run_kernel_render(width, height,
        [cam, obs, par, sphere](float* device_pixels, int w, int h) {
            launch_sphere_relativistic(device_pixels, w, h,
                                       cam, obs, par, sphere,
                                       /*stream=*/nullptr);
        });
}

CudaRenderer::Result CudaRenderer::render_scene(const rr::gpu::GpuScene& scene,
                                                int width, int height) {
    // Snapshot the GpuScene's host-resident state into a launch-arg
    // POD. The sphere + mesh device pointers / counts are read off
    // the GpuScene; no copy of the device buffers happens here.
    rr::cuda::CudaSceneView view;
    view.camera        = scene.gpu_camera();
    view.observer      = scene.observer();
    view.params        = scene.params();
    view.spheres       = scene.device_spheres();
    view.sphere_count  = static_cast<int>(scene.sphere_count());

    // Mesh slot. When the GpuScene has no mesh uploaded the counts
    // are zero and the kernel's triangle loop is a no-op.
    const auto& m            = scene.mesh();
    view.mesh.vertices       = m.device_vertices();
    view.mesh.triangles      = m.device_triangles();
    view.mesh.vertex_count   = static_cast<int>(m.vertex_count());
    view.mesh.triangle_count = static_cast<int>(m.triangle_count());
    view.mesh.material_id    = m.material_id();
    view.mesh.transform      = m.transform();

    // Materials array. Empty (`material_count == 0`) means the
    // kernel falls through to its neutral-default fallback.
    view.materials      = scene.device_materials();
    view.material_count = static_cast<int>(scene.material_count());

    // Lights array. Empty (`light_count == 0`) means the kernel
    // falls through to its facing-ratio shade for backwards
    // compatibility with the unlit demos.
    view.lights      = scene.device_lights();
    view.light_count = static_cast<int>(scene.light_count());

    return run_kernel_render(width, height,
        [view](float* device_pixels, int w, int h) {
            launch_render_scene(device_pixels, w, h, view, /*stream=*/nullptr);
        });
}

}  // namespace rr::cuda

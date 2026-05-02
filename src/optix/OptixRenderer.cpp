#include "optix/OptixRenderer.h"

#include "optix/OptixAccel.h"
#include "optix/OptixBackend.h"
#include "optix/OptixPipeline.h"

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <cuda_runtime.h>
    #include <optix.h>
    #include <optix_stubs.h>

    #include "camera/Camera.h"
    #include "gpu/GpuTiming.h"
    #include "math/Vec3.h"
    #include "optix/OptixLaunchParams.h"
    #include "relativity/RelativityParams.h"

    #include <cstddef>
    #include <cstdint>
    #include <cstring>
    #include <string>
    #include <utility>
#endif

namespace rr::optix {

OptixRenderer::Result OptixRenderer::render() noexcept {
    Result r;
    r.ok = false;
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
    r.message =
        "OptixRenderer::render is the Stage 12B.2 placeholder; use "
        "render_test (Stage 17A.3) for the minimum-viable OptiX "
        "dispatch. Subsequent 17A+ sub-stages replace this with the "
        "full scene-render entry point.";
#else
    r.message =
        "OptiX backend not compiled in (rebuild with "
        "-DRELATIVITYRENDER_ENABLE_OPTIX=ON to opt into the OptiX "
        "path - SDK + CUDA Toolkit also required for runtime use).";
#endif
    return r;
}

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

OptixRenderer::Result
OptixRenderer::render_test(int width, int height) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_test: invalid dimensions";
        return r;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        r.message = "OptixRenderer::render_test: backend init failed: "
                  + backend.last_error();
        return r;
    }

    OptixPipeline pipeline;
    {
        const auto pr = pipeline.create(backend);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_test: " + pr.error_message;
            return r;
        }
    }

    // Allocate the device-resident framebuffer
    // (width * height * 4 floats, channel-interleaved).
    const std::size_t framebuffer_floats =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height) * 4u;
    const std::size_t framebuffer_bytes  = framebuffer_floats * sizeof(float);

    void* d_framebuffer = nullptr;
    if (::cudaMalloc(&d_framebuffer, framebuffer_bytes) != cudaSuccess) {
        r.message = "OptixRenderer::render_test: cudaMalloc(framebuffer) failed";
        return r;
    }

    // Populate launch params on the host, then upload to the
    // device-side buffer the pipeline owns.
    OptixLaunchParams params{};
    params.framebuffer  = static_cast<float*>(d_framebuffer);
    params.width        = width;
    params.height       = height;
    params.flat_color_r = 1.0f;
    params.flat_color_g = 0.0f;
    params.flat_color_b = 1.0f;

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &params, sizeof(params), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_framebuffer);
            r.message = std::string("OptixRenderer::render_test: ")
                      + "cudaMemcpy(launch params) failed: "
                      + ::cudaGetErrorString(e);
            return r;
        }
    }

    // Launch. Stage 18A.1: GpuTimer brackets `optixLaunch` so the
    // OptiX path reports kernel time the same way the CUDA path
    // does. Events on the default stream; elapsed time is read
    // back after the existing `cudaDeviceSynchronize()`.
    rr::gpu::GpuTimer timer;
    {
        const auto* sbt = static_cast<const ::OptixShaderBindingTable*>(
            pipeline.shader_binding_table());
        timer.start();
        const ::OptixResult res = ::optixLaunch(
            static_cast<::OptixPipeline>(pipeline.pipeline_handle()),
            /*stream=*/0,
            reinterpret_cast<::CUdeviceptr>(pipeline.launch_params_device_ptr()),
            pipeline.launch_params_size_bytes(),
            sbt,
            static_cast<unsigned>(width),
            static_cast<unsigned>(height),
            /*depth=*/1u);
        timer.stop();
        if (res != OPTIX_SUCCESS) {
            ::cudaFree(d_framebuffer);
            r.message = std::string("OptixRenderer::render_test: "
                                    "optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return r;
        }
    }

    // Synchronise + download.
    if (::cudaDeviceSynchronize() != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        r.message = "OptixRenderer::render_test: cudaDeviceSynchronize failed";
        return r;
    }
    r.gpu_time_ms = timer.elapsed_ms();

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (::cudaMemcpy(img.data(), d_framebuffer, framebuffer_bytes,
                     cudaMemcpyDeviceToHost) != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        r.message = "OptixRenderer::render_test: cudaMemcpy(d->h) failed";
        return r;
    }

    ::cudaFree(d_framebuffer);
    r.image   = std::move(img);
    r.ok      = true;
    r.message = "OptiX test render complete.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_triangle(int width, int height) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_triangle: invalid dimensions";
        return r;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        r.message = "OptixRenderer::render_triangle: backend init failed: "
                  + backend.last_error();
        return r;
    }

    OptixPipeline pipeline;
    {
        const auto pr = pipeline.create(backend);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_triangle: " + pr.error_message;
            return r;
        }
    }

    // Hard-coded single triangle, matching the CUDA path's
    // build_demo_triangle_mesh (main.cpp:1391) byte-for-byte:
    //   v0 = ( 0.0,    1.0, -3.0)
    //   v1 = (-0.866, -0.5, -3.0)
    //   v2 = ( 0.866, -0.5, -3.0)
    // CCW from camera => geometric normal points toward +Z, so
    // normal-as-colour shading produces (0.5, 0.5, 1.0) for the
    // whole triangle (light blue).
    static const float kVertices[3 * 3] = {
         0.0f,    1.0f, -3.0f,
        -0.866f, -0.5f, -3.0f,
         0.866f, -0.5f, -3.0f,
    };
    static const std::uint32_t kIndices[3] = { 0u, 1u, 2u };

    void* d_vertices = nullptr;
    if (::cudaMalloc(&d_vertices, sizeof(kVertices)) != cudaSuccess) {
        r.message = "OptixRenderer::render_triangle: cudaMalloc(vertices) failed";
        return r;
    }
    if (::cudaMemcpy(d_vertices, kVertices, sizeof(kVertices),
                     cudaMemcpyHostToDevice) != cudaSuccess) {
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_triangle: cudaMemcpy(vertices) failed";
        return r;
    }

    void* d_indices = nullptr;
    if (::cudaMalloc(&d_indices, sizeof(kIndices)) != cudaSuccess) {
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_triangle: cudaMalloc(indices) failed";
        return r;
    }
    if (::cudaMemcpy(d_indices, kIndices, sizeof(kIndices),
                     cudaMemcpyHostToDevice) != cudaSuccess) {
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_triangle: cudaMemcpy(indices) failed";
        return r;
    }

    // Build the GAS via the Stage 17A.2 helper.
    BuildGasResult gas_result;
    {
        MeshGasInput gi{};
        gi.device_vertices = d_vertices;
        gi.vertex_count    = 3;
        gi.device_indices  = d_indices;
        gi.triangle_count  = 1;
        gas_result = build_mesh_gas(backend, gi);
        if (!gas_result.ok) {
            ::cudaFree(d_indices);
            ::cudaFree(d_vertices);
            r.message = "OptixRenderer::render_triangle: " + gas_result.error_message;
            return r;
        }
    }
    // The vertex / index buffers are part of the GAS's
    // referenced data; OptiX docs say the build keeps a
    // device-side reference to them. For a static GAS that is
    // never rebuilt, the inputs must remain alive through
    // every launch. Stage 17A.4 keeps them alive through this
    // function's scope; release at the end.

    // Set up the camera POD. Default Camera + aspect override
    // = same view as the CUDA --render-triangle handler.
    rr::camera::Camera camera;
    camera.set_aspect(static_cast<float>(width)
                    / static_cast<float>(height));
    const rr::camera::GpuCamera gpu_cam = camera.to_gpu();

    // Allocate the device-resident framebuffer
    // (width * height * 4 floats, channel-interleaved).
    const std::size_t framebuffer_floats =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height) * 4u;
    const std::size_t framebuffer_bytes  = framebuffer_floats * sizeof(float);

    void* d_framebuffer = nullptr;
    if (::cudaMalloc(&d_framebuffer, framebuffer_bytes) != cudaSuccess) {
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_triangle: cudaMalloc(framebuffer) failed";
        return r;
    }

    OptixLaunchParams params{};
    params.framebuffer  = static_cast<float*>(d_framebuffer);
    params.width        = width;
    params.height       = height;
    params.flat_color_r = 1.0f;  // unused (scene_handle != 0)
    params.flat_color_g = 0.0f;
    params.flat_color_b = 1.0f;
    params.camera       = gpu_cam;
    params.scene_handle = gas_result.gas.handle();

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &params, sizeof(params), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_framebuffer);
            ::cudaFree(d_indices);
            ::cudaFree(d_vertices);
            r.message = std::string("OptixRenderer::render_triangle: ")
                      + "cudaMemcpy(launch params) failed: "
                      + ::cudaGetErrorString(e);
            return r;
        }
    }

    // Launch. Stage 18A.1 instrumentation matching `render_test`.
    rr::gpu::GpuTimer timer;
    {
        const auto* sbt = static_cast<const ::OptixShaderBindingTable*>(
            pipeline.shader_binding_table());
        timer.start();
        const ::OptixResult res = ::optixLaunch(
            static_cast<::OptixPipeline>(pipeline.pipeline_handle()),
            /*stream=*/0,
            reinterpret_cast<::CUdeviceptr>(pipeline.launch_params_device_ptr()),
            pipeline.launch_params_size_bytes(),
            sbt,
            static_cast<unsigned>(width),
            static_cast<unsigned>(height),
            /*depth=*/1u);
        timer.stop();
        if (res != OPTIX_SUCCESS) {
            ::cudaFree(d_framebuffer);
            ::cudaFree(d_indices);
            ::cudaFree(d_vertices);
            r.message = std::string("OptixRenderer::render_triangle: "
                                    "optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return r;
        }
    }

    if (::cudaDeviceSynchronize() != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_triangle: cudaDeviceSynchronize failed";
        return r;
    }
    r.gpu_time_ms = timer.elapsed_ms();

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (::cudaMemcpy(img.data(), d_framebuffer, framebuffer_bytes,
                     cudaMemcpyDeviceToHost) != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_triangle: cudaMemcpy(d->h) failed";
        return r;
    }

    ::cudaFree(d_framebuffer);
    ::cudaFree(d_indices);
    ::cudaFree(d_vertices);

    r.image   = std::move(img);
    r.ok      = true;
    r.message = "OptiX triangle render complete.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_relativistic(int width, int height) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_relativistic: invalid dimensions";
        return r;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        r.message = "OptixRenderer::render_relativistic: backend init failed: "
                  + backend.last_error();
        return r;
    }

    OptixPipeline pipeline;
    {
        const auto pr = pipeline.create(backend);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_relativistic: " + pr.error_message;
            return r;
        }
    }

    // Stage 17A.5: same single-triangle fixture as
    // `render_triangle` (and the CUDA `--render-triangle`
    // handler) so the only variable between the two OptiX
    // outputs is the relativistic state. Visual diff between
    // `output/optix_triangle.ppm` and
    // `output/optix_relativity.ppm` therefore isolates the
    // effect of aberration + Doppler + searchlight.
    static const float kVertices[3 * 3] = {
         0.0f,    1.0f, -3.0f,
        -0.866f, -0.5f, -3.0f,
         0.866f, -0.5f, -3.0f,
    };
    static const std::uint32_t kIndices[3] = { 0u, 1u, 2u };

    void* d_vertices = nullptr;
    if (::cudaMalloc(&d_vertices, sizeof(kVertices)) != cudaSuccess) {
        r.message = "OptixRenderer::render_relativistic: cudaMalloc(vertices) failed";
        return r;
    }
    if (::cudaMemcpy(d_vertices, kVertices, sizeof(kVertices),
                     cudaMemcpyHostToDevice) != cudaSuccess) {
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_relativistic: cudaMemcpy(vertices) failed";
        return r;
    }

    void* d_indices = nullptr;
    if (::cudaMalloc(&d_indices, sizeof(kIndices)) != cudaSuccess) {
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_relativistic: cudaMalloc(indices) failed";
        return r;
    }
    if (::cudaMemcpy(d_indices, kIndices, sizeof(kIndices),
                     cudaMemcpyHostToDevice) != cudaSuccess) {
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_relativistic: cudaMemcpy(indices) failed";
        return r;
    }

    BuildGasResult gas_result;
    {
        MeshGasInput gi{};
        gi.device_vertices = d_vertices;
        gi.vertex_count    = 3;
        gi.device_indices  = d_indices;
        gi.triangle_count  = 1;
        gas_result = build_mesh_gas(backend, gi);
        if (!gas_result.ok) {
            ::cudaFree(d_indices);
            ::cudaFree(d_vertices);
            r.message = "OptixRenderer::render_relativistic: " + gas_result.error_message;
            return r;
        }
    }

    rr::camera::Camera camera;
    camera.set_aspect(static_cast<float>(width)
                    / static_cast<float>(height));
    const rr::camera::GpuCamera gpu_cam = camera.to_gpu();

    // Stage 17A.5: observer state. beta = 0.5 along -Z (the
    // camera's default forward direction) -> approaching the
    // triangle -> blueshift + forward aberration + searchlight
    // brightening. The chosen magnitude mirrors `--render-aovs`
    // (Stage 14A.3): strong enough that the relativistic
    // effects are clearly visible, but well clear of the high-
    // beta numerical regime.
    rr::relativity::Observer observer;
    observer.velocity = rr::math::Vec3{0.0f, 0.0f, -0.5f};
    rr::relativity::RelativityParams params;  // all effects enabled at strength 1

    const std::size_t framebuffer_floats =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height) * 4u;
    const std::size_t framebuffer_bytes  = framebuffer_floats * sizeof(float);

    void* d_framebuffer = nullptr;
    if (::cudaMalloc(&d_framebuffer, framebuffer_bytes) != cudaSuccess) {
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_relativistic: cudaMalloc(framebuffer) failed";
        return r;
    }

    OptixLaunchParams lp{};
    lp.framebuffer  = static_cast<float*>(d_framebuffer);
    lp.width        = width;
    lp.height       = height;
    lp.flat_color_r = 1.0f;  // unused (scene_handle != 0)
    lp.flat_color_g = 0.0f;
    lp.flat_color_b = 1.0f;
    lp.camera       = gpu_cam;
    lp.scene_handle = gas_result.gas.handle();
    lp.observer     = observer;
    lp.params       = params;

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &lp, sizeof(lp), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_framebuffer);
            ::cudaFree(d_indices);
            ::cudaFree(d_vertices);
            r.message = std::string("OptixRenderer::render_relativistic: ")
                      + "cudaMemcpy(launch params) failed: "
                      + ::cudaGetErrorString(e);
            return r;
        }
    }

    rr::gpu::GpuTimer timer;
    {
        const auto* sbt = static_cast<const ::OptixShaderBindingTable*>(
            pipeline.shader_binding_table());
        timer.start();
        const ::OptixResult res = ::optixLaunch(
            static_cast<::OptixPipeline>(pipeline.pipeline_handle()),
            /*stream=*/0,
            reinterpret_cast<::CUdeviceptr>(pipeline.launch_params_device_ptr()),
            pipeline.launch_params_size_bytes(),
            sbt,
            static_cast<unsigned>(width),
            static_cast<unsigned>(height),
            /*depth=*/1u);
        timer.stop();
        if (res != OPTIX_SUCCESS) {
            ::cudaFree(d_framebuffer);
            ::cudaFree(d_indices);
            ::cudaFree(d_vertices);
            r.message = std::string("OptixRenderer::render_relativistic: "
                                    "optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return r;
        }
    }

    if (::cudaDeviceSynchronize() != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_relativistic: cudaDeviceSynchronize failed";
        return r;
    }
    r.gpu_time_ms = timer.elapsed_ms();

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (::cudaMemcpy(img.data(), d_framebuffer, framebuffer_bytes,
                     cudaMemcpyDeviceToHost) != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_relativistic: cudaMemcpy(d->h) failed";
        return r;
    }

    ::cudaFree(d_framebuffer);
    ::cudaFree(d_indices);
    ::cudaFree(d_vertices);

    r.image   = std::move(img);
    r.ok      = true;
    r.message = "OptiX relativistic render complete.";
    return r;
}

#else   // RELATIVITYRENDER_OPTIX_SDK_FOUND

OptixRenderer::Result
OptixRenderer::render_test(int /*width*/, int /*height*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_test requires the OptiX SDK; rebuild "
        "with -DRELATIVITYRENDER_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_triangle(int /*width*/, int /*height*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_triangle requires the OptiX SDK; "
        "rebuild with -DRELATIVITYRENDER_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_relativistic(int /*width*/, int /*height*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_relativistic requires the OptiX "
        "SDK; rebuild with -DRELATIVITYRENDER_ENABLE_OPTIX=ON "
        "and pass -DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> "
        "is available. The CUDA path is unaffected.";
    return r;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

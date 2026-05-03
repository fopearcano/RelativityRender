#include "optix/OptixRenderer.h"

#include "optix/OptixAccel.h"
#include "optix/OptixBackend.h"
#include "optix/OptixPipeline.h"

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <cuda_runtime.h>
    #include <optix.h>
    #include <optix_stubs.h>

    #include "camera/Camera.h"
    #include "geometry/Mesh.h"           // Stage 20F: Mesh / Vertex / Triangle
    #include "gpu/GpuTiming.h"
    #include "math/Vec3.h"
    #include "optix/OptixLaunchParams.h"
    #include "relativity/RelativityParams.h"
    #include "scene/Scene.h"             // Stage 20F: Scene / SceneMesh

    #include <cstddef>
    #include <cstdint>
    #include <cstring>
    #include <string>
    #include <utility>
    #include <vector>
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
        "-DRR_ENABLE_OPTIX=ON to opt into the OptiX "
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
OptixRenderer::render_relativistic(int width, int height,
                                   float beta_magnitude) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_relativistic: invalid dimensions";
        return r;
    }

    // Stage 20H: clamp the artist-supplied |beta| at <= 0.999999
    // before constructing the observer velocity. Negative
    // inputs fold to magnitude per the existing clampBeta
    // contract (see tests/relativity_tests.cpp #6). Default
    // beta_magnitude = 0.5 preserves Stage 17A.5 output.
    const float beta_clamped = rr::relativity::clampBeta(
        beta_magnitude, /*max_beta=*/0.999999f);

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

    // Stage 17A.5: observer state along -Z (the camera's
    // default forward direction) -> approaching the triangle
    // -> blueshift + forward aberration + searchlight
    // brightening. Stage 20H: caller-supplied magnitude;
    // default 0.5 mirrors the original Stage 17A.5 fixture so
    // existing `--render-optix-relativity` (no `--beta`) is
    // byte-identical pre-/post-slice. `beta_clamped` is the
    // post-clampBeta value computed at the top of this
    // function.
    rr::relativity::Observer observer;
    observer.velocity = rr::math::Vec3{0.0f, 0.0f, -beta_clamped};
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

OptixRenderer::Result
OptixRenderer::render_raygen(int width, int height) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_raygen: invalid dimensions";
        return r;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        r.message = "OptixRenderer::render_raygen: backend init failed: "
                  + backend.last_error();
        return r;
    }

    OptixPipeline pipeline;
    {
        const auto pr = pipeline.create(backend);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_raygen: " + pr.error_message;
            return r;
        }
    }

    // Stage 20C: build a tiny triangle GAS placed BEHIND the
    // camera (z = +5). The default `rr::camera::Camera` looks
    // along -Z, so every primary ray fired from the origin goes
    // toward -Z and never hits this triangle. `optixTrace`
    // therefore always falls through to the miss program; the
    // closest-hit program (still bound in the SBT since Stage
    // 17A.4) is dormant for this entry. The miss program runs
    // per pixel and produces the project's vertical sky-gradient
    // environment colour.
    static const float kVertices[3 * 3] = {
         0.00f,  0.50f, +5.0f,
        -0.43f, -0.25f, +5.0f,
         0.43f, -0.25f, +5.0f,
    };
    static const std::uint32_t kIndices[3] = { 0u, 1u, 2u };

    void* d_vertices = nullptr;
    if (::cudaMalloc(&d_vertices, sizeof(kVertices)) != cudaSuccess) {
        r.message = "OptixRenderer::render_raygen: cudaMalloc(vertices) failed";
        return r;
    }
    if (::cudaMemcpy(d_vertices, kVertices, sizeof(kVertices),
                     cudaMemcpyHostToDevice) != cudaSuccess) {
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_raygen: cudaMemcpy(vertices) failed";
        return r;
    }

    void* d_indices = nullptr;
    if (::cudaMalloc(&d_indices, sizeof(kIndices)) != cudaSuccess) {
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_raygen: cudaMalloc(indices) failed";
        return r;
    }
    if (::cudaMemcpy(d_indices, kIndices, sizeof(kIndices),
                     cudaMemcpyHostToDevice) != cudaSuccess) {
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_raygen: cudaMemcpy(indices) failed";
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
            r.message = "OptixRenderer::render_raygen: " + gas_result.error_message;
            return r;
        }
    }

    rr::camera::Camera camera;
    camera.set_aspect(static_cast<float>(width)
                    / static_cast<float>(height));
    const rr::camera::GpuCamera gpu_cam = camera.to_gpu();

    const std::size_t framebuffer_floats =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height) * 4u;
    const std::size_t framebuffer_bytes  = framebuffer_floats * sizeof(float);

    void* d_framebuffer = nullptr;
    if (::cudaMalloc(&d_framebuffer, framebuffer_bytes) != cudaSuccess) {
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_raygen: cudaMalloc(framebuffer) failed";
        return r;
    }

    OptixLaunchParams params{};
    params.framebuffer  = static_cast<float*>(d_framebuffer);
    params.width        = width;
    params.height       = height;
    params.camera       = gpu_cam;
    params.scene_handle = gas_result.gas.handle();
    // Default `observer` + `params` (|beta| = 0). Default
    // `accum_buffer = nullptr` + `sample_index = 0` (Stage
    // 20B placeholders; not consumed by the existing raygen).

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &params, sizeof(params), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_framebuffer);
            ::cudaFree(d_indices);
            ::cudaFree(d_vertices);
            r.message = std::string("OptixRenderer::render_raygen: ")
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
            r.message = std::string("OptixRenderer::render_raygen: "
                                    "optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return r;
        }
    }

    if (::cudaDeviceSynchronize() != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_raygen: cudaDeviceSynchronize failed";
        return r;
    }
    r.gpu_time_ms = timer.elapsed_ms();

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (::cudaMemcpy(img.data(), d_framebuffer, framebuffer_bytes,
                     cudaMemcpyDeviceToHost) != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_vertices);
        r.message = "OptixRenderer::render_raygen: cudaMemcpy(d->h) failed";
        return r;
    }

    ::cudaFree(d_framebuffer);
    ::cudaFree(d_indices);
    ::cudaFree(d_vertices);

    r.image   = std::move(img);
    r.ok      = true;
    r.message = "OptiX raygen render complete.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_mesh_scene(const rr::scene::Scene& scene,
                                 int width, int height) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_mesh_scene: invalid dimensions";
        return r;
    }

    // Find the first visible non-empty mesh in scene.meshes.
    // Mirrors the CUDA path's --render-full-scene selection
    // (src/main.cpp run_render_full_scene). Multi-mesh OptiX
    // upload + IAS lands in a later slice.
    const rr::geometry::Mesh* picked = nullptr;
    for (const auto& sm : scene.meshes) {
        if (!sm.object.visible) continue;
        if (sm.geometry.empty()) continue;
        picked = &sm.geometry;
        break;
    }
    if (picked == nullptr) {
        r.message = "OptixRenderer::render_mesh_scene: scene contains no "
                    "visible non-empty mesh.";
        return r;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        r.message = "OptixRenderer::render_mesh_scene: backend init failed: "
                  + backend.last_error();
        return r;
    }

    OptixPipeline pipeline;
    {
        const auto pr = pipeline.create(backend);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_mesh_scene: " + pr.error_message;
            return r;
        }
    }

    // Extract positions to a tightly-packed `float3` buffer.
    // The Mesh's `Vertex` POD is 32 bytes (position + normal +
    // uv), but `build_mesh_gas` requires
    // `vertexStrideInBytes = 12` (per OptixAccel.cpp:143). The
    // adaptation lives on the host so the GAS builder's
    // contract stays narrow. This is host-side data layout
    // adaptation (allowed under the master rule "CPU may
    // upload data to GPU"), not per-pixel rendering.
    std::vector<float> flat_positions;
    flat_positions.reserve(picked->vertices.size() * 3u);
    for (const auto& v : picked->vertices) {
        flat_positions.push_back(v.position.x);
        flat_positions.push_back(v.position.y);
        flat_positions.push_back(v.position.z);
    }

    // Indices: Triangle is 3 x uint32_t == 12 bytes, layout-
    // compatible with the flat `uint32_t[3*N]` form
    // `build_mesh_gas` expects (per Triangle.h header
    // comment).
    const std::size_t n_vertices  = picked->vertices.size();
    const std::size_t n_triangles = picked->triangles.size();

    void* d_positions = nullptr;
    {
        const std::size_t bytes = flat_positions.size() * sizeof(float);
        if (::cudaMalloc(&d_positions, bytes) != cudaSuccess) {
            r.message = "OptixRenderer::render_mesh_scene: "
                        "cudaMalloc(positions) failed";
            return r;
        }
        if (::cudaMemcpy(d_positions, flat_positions.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_mesh_scene: "
                        "cudaMemcpy(positions) failed";
            return r;
        }
    }

    void* d_indices = nullptr;
    {
        const std::size_t bytes =
            n_triangles * sizeof(rr::geometry::Triangle);
        if (::cudaMalloc(&d_indices, bytes) != cudaSuccess) {
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_mesh_scene: "
                        "cudaMalloc(indices) failed";
            return r;
        }
        if (::cudaMemcpy(d_indices, picked->triangles.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_mesh_scene: "
                        "cudaMemcpy(indices) failed";
            return r;
        }
    }

    BuildGasResult gas_result;
    {
        MeshGasInput gi{};
        gi.device_vertices = d_positions;
        gi.vertex_count    = n_vertices;
        gi.device_indices  = d_indices;
        gi.triangle_count  = n_triangles;
        gas_result = build_mesh_gas(backend, gi);
        if (!gas_result.ok) {
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_mesh_scene: "
                      + gas_result.error_message;
            return r;
        }
    }

    // Use the scene's camera (Stage 20F: "use existing
    // .rrscene loader" + the render_settings the parser
    // populates). Aspect is set from the requested
    // dimensions so a CLI override of width/height still
    // matches the framebuffer shape.
    rr::camera::Camera camera = scene.camera;
    camera.set_aspect(static_cast<float>(width)
                    / static_cast<float>(height));
    const rr::camera::GpuCamera gpu_cam = camera.to_gpu();

    const std::size_t framebuffer_floats =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height) * 4u;
    const std::size_t framebuffer_bytes  = framebuffer_floats * sizeof(float);

    void* d_framebuffer = nullptr;
    if (::cudaMalloc(&d_framebuffer, framebuffer_bytes) != cudaSuccess) {
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "OptixRenderer::render_mesh_scene: "
                    "cudaMalloc(framebuffer) failed";
        return r;
    }

    OptixLaunchParams params{};
    params.framebuffer  = static_cast<float*>(d_framebuffer);
    params.width        = width;
    params.height       = height;
    params.camera       = gpu_cam;
    params.scene_handle = gas_result.gas.handle();
    // Default `observer` + `params` (|beta| = 0): closest-hit
    // emits straight normal-as-color, miss emits the gradient
    // sky. Stage 20F rule "no materials beyond basic color"
    // is satisfied by leaving the relativistic stack at
    // identity. `accum_buffer` + `sample_index` left at
    // Stage 20B defaults (nullptr / 0): no path tracing.

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &params, sizeof(params), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_framebuffer);
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = std::string("OptixRenderer::render_mesh_scene: ")
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
            ::cudaFree(d_positions);
            r.message = std::string("OptixRenderer::render_mesh_scene: "
                                    "optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return r;
        }
    }

    if (::cudaDeviceSynchronize() != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "OptixRenderer::render_mesh_scene: "
                    "cudaDeviceSynchronize failed";
        return r;
    }
    r.gpu_time_ms = timer.elapsed_ms();

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (::cudaMemcpy(img.data(), d_framebuffer, framebuffer_bytes,
                     cudaMemcpyDeviceToHost) != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "OptixRenderer::render_mesh_scene: "
                    "cudaMemcpy(d->h) failed";
        return r;
    }

    ::cudaFree(d_framebuffer);
    ::cudaFree(d_indices);
    ::cudaFree(d_positions);

    r.image   = std::move(img);
    r.ok      = true;
    r.message = "OptiX mesh-scene render complete.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_material_scene(const rr::scene::Scene& scene,
                                     int width, int height) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_material_scene: invalid dimensions";
        return r;
    }

    // Same first-non-empty-mesh selection as render_mesh_scene.
    // Multi-mesh / IAS support deferred to a later slice.
    const rr::geometry::Mesh* picked = nullptr;
    for (const auto& sm : scene.meshes) {
        if (!sm.object.visible) continue;
        if (sm.geometry.empty()) continue;
        picked = &sm.geometry;
        break;
    }
    if (picked == nullptr) {
        r.message = "OptixRenderer::render_material_scene: scene contains "
                    "no visible non-empty mesh.";
        return r;
    }

    // Stage 20G: look up the picked mesh's material via
    // material_id. Fall back to a default-constructed
    // MaterialParams (light-grey baseColor, no emission) when
    // the id is -1 or out of range. Either way the closest-
    // hit will use shading_mode = 1 so the rendered output
    // visibly reflects the chosen material data.
    rr::material::MaterialParams material_params{};
    if (picked->material_id >= 0
     && static_cast<std::size_t>(picked->material_id) < scene.materials.size()) {
        material_params = scene.materials[picked->material_id].params;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        r.message = "OptixRenderer::render_material_scene: "
                    "backend init failed: "
                  + backend.last_error();
        return r;
    }

    OptixPipeline pipeline;
    {
        const auto pr = pipeline.create(backend);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_material_scene: "
                      + pr.error_message;
            return r;
        }
    }

    // Stage 20G: tell the pipeline's hit-group SBT record to
    // emit material flat shading (`baseColor + emission`)
    // instead of the default normal-as-color. This is the
    // SBT-side material linkage the prompt asks for.
    {
        const auto pr = pipeline.set_hit_material(
            material_params, /*shading_mode=*/1);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_material_scene: "
                      + pr.error_message;
            return r;
        }
    }

    // Position extraction + index upload identical to
    // render_mesh_scene (Stage 20F). The Vertex POD is 32
    // bytes (position + normal + uv); build_mesh_gas needs
    // a tightly-packed float3 buffer (12 bytes / vertex).
    std::vector<float> flat_positions;
    flat_positions.reserve(picked->vertices.size() * 3u);
    for (const auto& v : picked->vertices) {
        flat_positions.push_back(v.position.x);
        flat_positions.push_back(v.position.y);
        flat_positions.push_back(v.position.z);
    }

    const std::size_t n_vertices  = picked->vertices.size();
    const std::size_t n_triangles = picked->triangles.size();

    void* d_positions = nullptr;
    {
        const std::size_t bytes = flat_positions.size() * sizeof(float);
        if (::cudaMalloc(&d_positions, bytes) != cudaSuccess) {
            r.message = "OptixRenderer::render_material_scene: "
                        "cudaMalloc(positions) failed";
            return r;
        }
        if (::cudaMemcpy(d_positions, flat_positions.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_material_scene: "
                        "cudaMemcpy(positions) failed";
            return r;
        }
    }

    void* d_indices = nullptr;
    {
        const std::size_t bytes =
            n_triangles * sizeof(rr::geometry::Triangle);
        if (::cudaMalloc(&d_indices, bytes) != cudaSuccess) {
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_material_scene: "
                        "cudaMalloc(indices) failed";
            return r;
        }
        if (::cudaMemcpy(d_indices, picked->triangles.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_material_scene: "
                        "cudaMemcpy(indices) failed";
            return r;
        }
    }

    BuildGasResult gas_result;
    {
        MeshGasInput gi{};
        gi.device_vertices = d_positions;
        gi.vertex_count    = n_vertices;
        gi.device_indices  = d_indices;
        gi.triangle_count  = n_triangles;
        gas_result = build_mesh_gas(backend, gi);
        if (!gas_result.ok) {
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_material_scene: "
                      + gas_result.error_message;
            return r;
        }
    }

    rr::camera::Camera camera = scene.camera;
    camera.set_aspect(static_cast<float>(width)
                    / static_cast<float>(height));
    const rr::camera::GpuCamera gpu_cam = camera.to_gpu();

    const std::size_t framebuffer_floats =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height) * 4u;
    const std::size_t framebuffer_bytes  = framebuffer_floats * sizeof(float);

    void* d_framebuffer = nullptr;
    if (::cudaMalloc(&d_framebuffer, framebuffer_bytes) != cudaSuccess) {
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "OptixRenderer::render_material_scene: "
                    "cudaMalloc(framebuffer) failed";
        return r;
    }

    OptixLaunchParams params{};
    params.framebuffer  = static_cast<float*>(d_framebuffer);
    params.width        = width;
    params.height       = height;
    params.camera       = gpu_cam;
    params.scene_handle = gas_result.gas.handle();
    // Default `observer` + `params` (|beta| = 0): closest-hit
    // emits material-flat shading; Doppler / searchlight stack
    // composes as identity. Stage 20G rule "no path tracing":
    // accum_buffer + sample_index left at Stage 20B defaults.

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &params, sizeof(params), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_framebuffer);
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = std::string("OptixRenderer::render_material_scene: ")
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
            ::cudaFree(d_positions);
            r.message = std::string("OptixRenderer::render_material_scene: "
                                    "optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return r;
        }
    }

    if (::cudaDeviceSynchronize() != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "OptixRenderer::render_material_scene: "
                    "cudaDeviceSynchronize failed";
        return r;
    }
    r.gpu_time_ms = timer.elapsed_ms();

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (::cudaMemcpy(img.data(), d_framebuffer, framebuffer_bytes,
                     cudaMemcpyDeviceToHost) != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "OptixRenderer::render_material_scene: "
                    "cudaMemcpy(d->h) failed";
        return r;
    }

    ::cudaFree(d_framebuffer);
    ::cudaFree(d_indices);
    ::cudaFree(d_positions);

    r.image   = std::move(img);
    r.ok      = true;
    r.message = "OptiX material-scene render complete.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_pathtrace(const rr::scene::Scene& scene,
                                int width, int height,
                                int spp, int max_bounces,
                                unsigned int seed) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_pathtrace: invalid dimensions";
        return r;
    }
    if (spp < 1 || max_bounces < 1) {
        r.message = "OptixRenderer::render_pathtrace: spp and "
                    "max_bounces must each be >= 1";
        return r;
    }

    // Same first-non-empty-mesh selection as the Stage 20F /
    // 20G renderers. Multi-mesh / IAS deferred.
    const rr::geometry::Mesh* picked = nullptr;
    for (const auto& sm : scene.meshes) {
        if (!sm.object.visible) continue;
        if (sm.geometry.empty()) continue;
        picked = &sm.geometry;
        break;
    }
    if (picked == nullptr) {
        r.message = "OptixRenderer::render_pathtrace: scene contains "
                    "no visible non-empty mesh.";
        return r;
    }

    // Stage 20G material lookup. The path-tracer closest-hit
    // reads `params.baseColor` as albedo; ignores
    // `shading_mode`. We still call set_hit_material with
    // mode=1 so the SBT record carries the params; mode is
    // a no-op for the path-tracer hit-record.
    rr::material::MaterialParams material_params{};
    if (picked->material_id >= 0
     && static_cast<std::size_t>(picked->material_id) < scene.materials.size()) {
        material_params = scene.materials[picked->material_id].params;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        r.message = "OptixRenderer::render_pathtrace: backend init failed: "
                  + backend.last_error();
        return r;
    }

    // Stage 20I: build the pipeline with path_tracer = true so
    // the SBT binds the __raygen__pathtrace / __miss__pathtrace
    // / __closesthit__pathtrace entries.
    OptixPipeline pipeline;
    {
        OptixPipelineOptions opts;
        opts.path_tracer = true;
        const auto pr = pipeline.create(backend, opts);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_pathtrace: " + pr.error_message;
            return r;
        }
    }

    // Plumb the picked material's params into the hit-group
    // SBT record. Mode is irrelevant for the path-tracer
    // closest-hit (it always uses params.baseColor).
    {
        const auto pr = pipeline.set_hit_material(
            material_params, /*shading_mode=*/1);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_pathtrace: " + pr.error_message;
            return r;
        }
    }

    // Position extraction + index upload identical to
    // render_mesh_scene (Stage 20F / 20G).
    std::vector<float> flat_positions;
    flat_positions.reserve(picked->vertices.size() * 3u);
    for (const auto& v : picked->vertices) {
        flat_positions.push_back(v.position.x);
        flat_positions.push_back(v.position.y);
        flat_positions.push_back(v.position.z);
    }

    const std::size_t n_vertices  = picked->vertices.size();
    const std::size_t n_triangles = picked->triangles.size();

    void* d_positions = nullptr;
    {
        const std::size_t bytes = flat_positions.size() * sizeof(float);
        if (::cudaMalloc(&d_positions, bytes) != cudaSuccess) {
            r.message = "OptixRenderer::render_pathtrace: "
                        "cudaMalloc(positions) failed";
            return r;
        }
        if (::cudaMemcpy(d_positions, flat_positions.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_pathtrace: "
                        "cudaMemcpy(positions) failed";
            return r;
        }
    }

    void* d_indices = nullptr;
    {
        const std::size_t bytes =
            n_triangles * sizeof(rr::geometry::Triangle);
        if (::cudaMalloc(&d_indices, bytes) != cudaSuccess) {
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_pathtrace: "
                        "cudaMalloc(indices) failed";
            return r;
        }
        if (::cudaMemcpy(d_indices, picked->triangles.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_pathtrace: "
                        "cudaMemcpy(indices) failed";
            return r;
        }
    }

    BuildGasResult gas_result;
    {
        MeshGasInput gi{};
        gi.device_vertices = d_positions;
        gi.vertex_count    = n_vertices;
        gi.device_indices  = d_indices;
        gi.triangle_count  = n_triangles;
        gas_result = build_mesh_gas(backend, gi);
        if (!gas_result.ok) {
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = "OptixRenderer::render_pathtrace: "
                      + gas_result.error_message;
            return r;
        }
    }

    rr::camera::Camera camera = scene.camera;
    camera.set_aspect(static_cast<float>(width)
                    / static_cast<float>(height));
    const rr::camera::GpuCamera gpu_cam = camera.to_gpu();

    const std::size_t framebuffer_floats =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height) * 4u;
    const std::size_t framebuffer_bytes  = framebuffer_floats * sizeof(float);

    void* d_framebuffer = nullptr;
    if (::cudaMalloc(&d_framebuffer, framebuffer_bytes) != cudaSuccess) {
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "OptixRenderer::render_pathtrace: "
                    "cudaMalloc(framebuffer) failed";
        return r;
    }

    OptixLaunchParams params{};
    params.framebuffer  = static_cast<float*>(d_framebuffer);
    params.width        = width;
    params.height       = height;
    params.camera       = gpu_cam;
    params.scene_handle = gas_result.gas.handle();
    params.spp          = spp;
    params.max_bounces  = max_bounces;
    params.seed         = seed;
    // Default `observer` + `params` (|beta| = 0) keep the
    // Doppler / searchlight stack at identity. Caller-driven
    // observer setup lives in a future slice.

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &params, sizeof(params), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_framebuffer);
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = std::string("OptixRenderer::render_pathtrace: ")
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
            ::cudaFree(d_positions);
            r.message = std::string("OptixRenderer::render_pathtrace: "
                                    "optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return r;
        }
    }

    if (::cudaDeviceSynchronize() != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "OptixRenderer::render_pathtrace: "
                    "cudaDeviceSynchronize failed";
        return r;
    }
    r.gpu_time_ms = timer.elapsed_ms();

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (::cudaMemcpy(img.data(), d_framebuffer, framebuffer_bytes,
                     cudaMemcpyDeviceToHost) != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "OptixRenderer::render_pathtrace: "
                    "cudaMemcpy(d->h) failed";
        return r;
    }

    ::cudaFree(d_framebuffer);
    ::cudaFree(d_indices);
    ::cudaFree(d_positions);

    r.image   = std::move(img);
    r.ok      = true;
    r.message = "OptiX path-trace render complete.";
    return r;
}

#else   // RELATIVITYRENDER_OPTIX_SDK_FOUND

OptixRenderer::Result
OptixRenderer::render_test(int /*width*/, int /*height*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_test requires the OptiX SDK; rebuild "
        "with -DRR_ENABLE_OPTIX=ON and pass "
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
        "rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_relativistic(int /*width*/, int /*height*/,
                                   float /*beta_magnitude*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_relativistic requires the OptiX "
        "SDK; rebuild with -DRR_ENABLE_OPTIX=ON "
        "and pass -DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> "
        "is available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_raygen(int /*width*/, int /*height*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_raygen requires the OptiX SDK; "
        "rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_mesh_scene(const rr::scene::Scene& /*scene*/,
                                 int /*width*/,
                                 int /*height*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_mesh_scene requires the OptiX SDK; "
        "rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_material_scene(const rr::scene::Scene& /*scene*/,
                                     int /*width*/,
                                     int /*height*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_material_scene requires the OptiX "
        "SDK; rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_pathtrace(const rr::scene::Scene& /*scene*/,
                                int /*width*/,
                                int /*height*/,
                                int /*spp*/,
                                int /*max_bounces*/,
                                unsigned int /*seed*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_pathtrace requires the OptiX SDK; "
        "rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

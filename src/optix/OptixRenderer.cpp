#include "optix/OptixRenderer.h"

#include "lighting/Light.h"          // Stage 20N: complete type for
                                     // `std::vector<Light>&` in both
                                     // SDK-found impls and audit-host
                                     // stubs.
#include "optix/OptixAccel.h"
#include "optix/OptixBackend.h"
#include "optix/OptixPipeline.h"
#include "texture/ImageTexture.h"   // Stage 20M: complete type for
                                     // `std::vector<ImageTexture>&` in
                                     // both SDK-found impls and audit-
                                     // host stubs.

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <cuda_runtime.h>
    #include <optix.h>
    #include <optix_stubs.h>

    #include "camera/Camera.h"
    #include "cuda/CudaAccumulation.cuh"  // Stage 20J: launch_accum_*
    #include "cuda/CudaTexture.cuh"       // Stage 20M: DeviceTextureView
    #include "geometry/Mesh.h"            // Stage 20F: Mesh / Vertex / Triangle
    #include "gpu/GpuTiming.h"
    #include "math/Vec2.h"                // Stage 20M: per-vertex UVs
    #include "math/Vec3.h"
    #include "optix/OptixLaunchParams.h"
    #include "relativity/RelativityParams.h"
    #include "scene/Scene.h"              // Stage 20F: Scene / SceneMesh
    #include "texture/ImageTexture.h"     // Stage 20M: texture upload

    #include <algorithm>                  // Stage 20J: std::max_element
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

OptixRenderer::PathtraceProgressiveResult
OptixRenderer::render_pathtrace_progressive(
    const rr::scene::Scene& scene,
    int width, int height,
    int max_bounces,
    unsigned int seed,
    const std::vector<int>& checkpoint_samples) noexcept {
    PathtraceProgressiveResult R;

    if (width <= 0 || height <= 0) {
        R.message = "OptixRenderer::render_pathtrace_progressive: "
                    "invalid dimensions";
        return R;
    }
    if (max_bounces < 1) {
        R.message = "OptixRenderer::render_pathtrace_progressive: "
                    "max_bounces must be >= 1";
        return R;
    }
    if (checkpoint_samples.empty()) {
        R.message = "OptixRenderer::render_pathtrace_progressive: "
                    "checkpoint_samples is empty";
        return R;
    }
    for (int n : checkpoint_samples) {
        if (n < 1) {
            R.message = "OptixRenderer::render_pathtrace_progressive: "
                        "checkpoint_samples must contain values >= 1";
            return R;
        }
    }
    // Largest checkpoint = total samples to accumulate.
    const int max_spp = *std::max_element(checkpoint_samples.begin(),
                                          checkpoint_samples.end());

    // First-non-empty-mesh selection (mirrors Stage 20F / 20G /
    // 20I).
    const rr::geometry::Mesh* picked = nullptr;
    for (const auto& sm : scene.meshes) {
        if (!sm.object.visible) continue;
        if (sm.geometry.empty()) continue;
        picked = &sm.geometry;
        break;
    }
    if (picked == nullptr) {
        R.message = "OptixRenderer::render_pathtrace_progressive: "
                    "scene contains no visible non-empty mesh.";
        return R;
    }

    rr::material::MaterialParams material_params{};
    if (picked->material_id >= 0
     && static_cast<std::size_t>(picked->material_id) < scene.materials.size()) {
        material_params = scene.materials[picked->material_id].params;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        R.message = "OptixRenderer::render_pathtrace_progressive: "
                    "backend init failed: "
                  + backend.last_error();
        return R;
    }

    OptixPipeline pipeline;
    {
        OptixPipelineOptions opts;
        opts.path_tracer = true;
        const auto pr = pipeline.create(backend, opts);
        if (!pr.ok) {
            R.message = "OptixRenderer::render_pathtrace_progressive: "
                      + pr.error_message;
            return R;
        }
    }
    {
        const auto pr = pipeline.set_hit_material(
            material_params, /*shading_mode=*/1);
        if (!pr.ok) {
            R.message = "OptixRenderer::render_pathtrace_progressive: "
                      + pr.error_message;
            return R;
        }
    }

    // Position extraction + index upload (Stage 20F / 20G / 20I shape).
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
            R.message = "render_pathtrace_progressive: cudaMalloc(positions) failed";
            return R;
        }
        if (::cudaMemcpy(d_positions, flat_positions.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_positions);
            R.message = "render_pathtrace_progressive: cudaMemcpy(positions) failed";
            return R;
        }
    }
    void* d_indices = nullptr;
    {
        const std::size_t bytes =
            n_triangles * sizeof(rr::geometry::Triangle);
        if (::cudaMalloc(&d_indices, bytes) != cudaSuccess) {
            ::cudaFree(d_positions);
            R.message = "render_pathtrace_progressive: cudaMalloc(indices) failed";
            return R;
        }
        if (::cudaMemcpy(d_indices, picked->triangles.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            R.message = "render_pathtrace_progressive: cudaMemcpy(indices) failed";
            return R;
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
            R.message = "render_pathtrace_progressive: "
                      + gas_result.error_message;
            return R;
        }
    }

    rr::camera::Camera camera = scene.camera;
    camera.set_aspect(static_cast<float>(width)
                    / static_cast<float>(height));
    const rr::camera::GpuCamera gpu_cam = camera.to_gpu();

    // Stage 20J: allocate three Rgba32F device buffers:
    //   d_framebuffer = per-launch single-sample radiance
    //   d_accumulator = running sum across all samples so far
    //   d_display     = scaled (running-sum / sample-count)
    //                   buffer the host downloads at each
    //                   checkpoint.
    const std::size_t pixel_count =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height);
    const std::size_t float_count = pixel_count * 4u;
    const std::size_t buffer_bytes = float_count * sizeof(float);

    void* d_framebuffer = nullptr;
    void* d_accumulator = nullptr;
    void* d_display     = nullptr;

    auto cleanup = [&]() {
        if (d_display)     ::cudaFree(d_display);
        if (d_accumulator) ::cudaFree(d_accumulator);
        if (d_framebuffer) ::cudaFree(d_framebuffer);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
    };

    if (::cudaMalloc(&d_framebuffer, buffer_bytes) != cudaSuccess) {
        cleanup();
        R.message = "render_pathtrace_progressive: cudaMalloc(framebuffer) failed";
        return R;
    }
    if (::cudaMalloc(&d_accumulator, buffer_bytes) != cudaSuccess) {
        cleanup();
        R.message = "render_pathtrace_progressive: cudaMalloc(accumulator) failed";
        return R;
    }
    if (::cudaMalloc(&d_display, buffer_bytes) != cudaSuccess) {
        cleanup();
        R.message = "render_pathtrace_progressive: cudaMalloc(display) failed";
        return R;
    }

    // Reset the accumulator. Stage 11B's `launch_accum_clear`
    // forwards to `cudaMemset` on the device; the buffer
    // starts in the documented zero state every progressive
    // path-trace invocation expects.
    if (!rr::cuda::launch_accum_clear(static_cast<float*>(d_accumulator),
                                       float_count)) {
        cleanup();
        R.message = "render_pathtrace_progressive: launch_accum_clear failed";
        return R;
    }

    rr::gpu::GpuTimer timer;

    for (int sample_index = 0; sample_index < max_spp; ++sample_index) {
        // Populate launch params for THIS sample. spp = 1 so
        // the raygen's inner loop runs once; sample_index
        // controls the RNG seed via Stage 20J's combined
        // sample_index + inner-loop-counter formula.
        OptixLaunchParams params{};
        params.framebuffer  = static_cast<float*>(d_framebuffer);
        params.width        = width;
        params.height       = height;
        params.camera       = gpu_cam;
        params.scene_handle = gas_result.gas.handle();
        params.spp          = 1;
        params.max_bounces  = max_bounces;
        params.seed         = seed;
        params.sample_index =
            static_cast<std::uint32_t>(sample_index);

        {
            const ::cudaError_t e = ::cudaMemcpy(
                pipeline.launch_params_device_ptr(),
                &params, sizeof(params), cudaMemcpyHostToDevice);
            if (e != cudaSuccess) {
                cleanup();
                R.message = std::string("render_pathtrace_progressive: ")
                          + "cudaMemcpy(launch params) failed: "
                          + ::cudaGetErrorString(e);
                return R;
            }
        }

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
                cleanup();
                R.message = std::string("render_pathtrace_progressive: "
                                        "optixLaunch failed: ")
                          + ::optixGetErrorName(res);
                return R;
            }
        }
        if (::cudaDeviceSynchronize() != cudaSuccess) {
            cleanup();
            R.message = "render_pathtrace_progressive: "
                        "cudaDeviceSynchronize failed";
            return R;
        }
        R.total_gpu_time_ms += timer.elapsed_ms();

        // Accumulate. Stage 18A.4 first-sample fast path on
        // sample 0 (cudaMemcpy D2D); scalar/float4 add for
        // sample >= 1.
        const bool ok_acc = (sample_index == 0)
            ? rr::cuda::launch_accum_first_sample(
                  static_cast<float*>(d_accumulator),
                  static_cast<const float*>(d_framebuffer),
                  float_count)
            : rr::cuda::launch_accum_add(
                  static_cast<float*>(d_accumulator),
                  static_cast<const float*>(d_framebuffer),
                  float_count);
        if (!ok_acc) {
            cleanup();
            R.message = "render_pathtrace_progressive: "
                        "launch_accum_first_sample / _add failed";
            return R;
        }

        // Checkpoint? Resolve + download + record.
        const int samples_done = sample_index + 1;
        bool is_checkpoint = false;
        for (int c : checkpoint_samples) {
            if (c == samples_done) { is_checkpoint = true; break; }
        }
        if (is_checkpoint) {
            const float inv_samples =
                1.0f / static_cast<float>(samples_done);
            if (!rr::cuda::launch_accum_resolve(
                    static_cast<const float*>(d_accumulator),
                    static_cast<float*>(d_display),
                    float_count, inv_samples)) {
                cleanup();
                R.message = "render_pathtrace_progressive: "
                            "launch_accum_resolve failed";
                return R;
            }

            rr::image::Image img(width, height,
                                 rr::image::PixelFormat::Rgba32F);
            if (::cudaMemcpy(img.data(), d_display, buffer_bytes,
                             cudaMemcpyDeviceToHost) != cudaSuccess) {
                cleanup();
                R.message = "render_pathtrace_progressive: "
                            "cudaMemcpy(d->h, display) failed";
                return R;
            }

            PathtraceCheckpoint cp;
            cp.sample_count = samples_done;
            cp.image        = std::move(img);
            R.checkpoints.push_back(std::move(cp));
        }
    }

    cleanup();
    R.ok      = true;
    R.message = "OptiX path-trace progressive render complete.";
    return R;
}

OptixRenderer::Result
OptixRenderer::render_direct_lighting(const rr::scene::Scene& scene,
                                      int width, int height,
                                      bool enable_shadows) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_direct_lighting: invalid dimensions";
        return r;
    }

    // Same first-non-empty-mesh selection as Stage 20F / 20G /
    // 20I / 20J. Multi-mesh / IAS deferred.
    const rr::geometry::Mesh* picked = nullptr;
    for (const auto& sm : scene.meshes) {
        if (!sm.object.visible) continue;
        if (sm.geometry.empty()) continue;
        picked = &sm.geometry;
        break;
    }
    if (picked == nullptr) {
        r.message = "OptixRenderer::render_direct_lighting: scene contains "
                    "no visible non-empty mesh.";
        return r;
    }

    rr::material::MaterialParams material_params{};
    if (picked->material_id >= 0
     && static_cast<std::size_t>(picked->material_id) < scene.materials.size()) {
        material_params = scene.materials[picked->material_id].params;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        r.message = "OptixRenderer::render_direct_lighting: "
                    "backend init failed: "
                  + backend.last_error();
        return r;
    }

    OptixPipeline pipeline;
    {
        // Use the radiance entry-point family (path_tracer = false)
        // since direct lighting evaluates at the primary hit and
        // does not require the path-tracer raygen.
        OptixPipelineOptions opts;
        opts.path_tracer = false;
        const auto pr = pipeline.create(backend, opts);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_direct_lighting: "
                      + pr.error_message;
            return r;
        }
    }
    {
        // Stage 20K: shading_mode = 2 selects the direct-
        // lighting branch in __closesthit__radiance.
        const auto pr = pipeline.set_hit_material(
            material_params, /*shading_mode=*/2);
        if (!pr.ok) {
            r.message = "OptixRenderer::render_direct_lighting: "
                      + pr.error_message;
            return r;
        }
    }

    // Position extraction + index upload (Stage 20F shape).
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
            r.message = "render_direct_lighting: cudaMalloc(positions) failed";
            return r;
        }
        if (::cudaMemcpy(d_positions, flat_positions.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_positions);
            r.message = "render_direct_lighting: cudaMemcpy(positions) failed";
            return r;
        }
    }
    void* d_indices = nullptr;
    {
        const std::size_t bytes =
            n_triangles * sizeof(rr::geometry::Triangle);
        if (::cudaMalloc(&d_indices, bytes) != cudaSuccess) {
            ::cudaFree(d_positions);
            r.message = "render_direct_lighting: cudaMalloc(indices) failed";
            return r;
        }
        if (::cudaMemcpy(d_indices, picked->triangles.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = "render_direct_lighting: cudaMemcpy(indices) failed";
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
            r.message = "render_direct_lighting: " + gas_result.error_message;
            return r;
        }
    }

    // Stage 20K: upload lights to a device-resident buffer.
    // The closest-hit reads `optixLaunchParams.lights` directly;
    // we keep this buffer alive across the launch.
    void*       d_lights     = nullptr;
    const int   light_count  = static_cast<int>(scene.lights.size());
    if (light_count > 0) {
        const std::size_t bytes =
            static_cast<std::size_t>(light_count) * sizeof(rr::lighting::Light);
        if (::cudaMalloc(&d_lights, bytes) != cudaSuccess) {
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = "render_direct_lighting: cudaMalloc(lights) failed";
            return r;
        }
        // Copy light POD union per scene-light entry.
        std::vector<rr::lighting::Light> light_pods;
        light_pods.reserve(scene.lights.size());
        for (const auto& sl : scene.lights) {
            light_pods.push_back(sl.data);
        }
        if (::cudaMemcpy(d_lights, light_pods.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            ::cudaFree(d_lights);
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = "render_direct_lighting: cudaMemcpy(lights) failed";
            return r;
        }
    }
    // light_count == 0 leaves d_lights == nullptr; the closest-
    // hit's "no lights uploaded" branch hits the implicit
    // ambient floor + emission, matching the CUDA path's
    // safety-net behaviour.

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
        if (d_lights) ::cudaFree(d_lights);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "render_direct_lighting: cudaMalloc(framebuffer) failed";
        return r;
    }

    OptixLaunchParams params{};
    params.framebuffer  = static_cast<float*>(d_framebuffer);
    params.width        = width;
    params.height       = height;
    params.camera       = gpu_cam;
    params.scene_handle = gas_result.gas.handle();
    params.lights       =
        static_cast<const rr::lighting::Light*>(d_lights);
    params.light_count  = light_count;
    params.enable_shadows = enable_shadows;  // Stage 20L
    // Default observer + relativity params (|beta| = 0); the
    // direct-lighting closest-hit threads its result through
    // the existing Stage 17A.5 / 20H Doppler-and-searchlight
    // helper which is identity at zero beta.

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &params, sizeof(params), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_framebuffer);
            if (d_lights) ::cudaFree(d_lights);
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = std::string("render_direct_lighting: ")
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
            if (d_lights) ::cudaFree(d_lights);
            ::cudaFree(d_indices);
            ::cudaFree(d_positions);
            r.message = std::string("render_direct_lighting: "
                                    "optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return r;
        }
    }

    if (::cudaDeviceSynchronize() != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        if (d_lights) ::cudaFree(d_lights);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "render_direct_lighting: cudaDeviceSynchronize failed";
        return r;
    }
    r.gpu_time_ms = timer.elapsed_ms();

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (::cudaMemcpy(img.data(), d_framebuffer, framebuffer_bytes,
                     cudaMemcpyDeviceToHost) != cudaSuccess) {
        ::cudaFree(d_framebuffer);
        if (d_lights) ::cudaFree(d_lights);
        ::cudaFree(d_indices);
        ::cudaFree(d_positions);
        r.message = "render_direct_lighting: cudaMemcpy(d->h) failed";
        return r;
    }

    ::cudaFree(d_framebuffer);
    if (d_lights) ::cudaFree(d_lights);
    ::cudaFree(d_indices);
    ::cudaFree(d_positions);

    r.image   = std::move(img);
    r.ok      = true;
    r.message = "OptiX direct-lighting render complete.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_textured_material(
    const rr::scene::Scene& scene,
    const std::vector<rr::texture::ImageTexture>& textures,
    int width, int height) noexcept {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptixRenderer::render_textured_material: invalid dimensions";
        return r;
    }

    // First-non-empty-mesh selection (mirrors Stage 20F shape).
    const rr::geometry::Mesh* picked = nullptr;
    for (const auto& sm : scene.meshes) {
        if (!sm.object.visible) continue;
        if (sm.geometry.empty()) continue;
        picked = &sm.geometry;
        break;
    }
    if (picked == nullptr) {
        r.message = "render_textured_material: scene contains no "
                    "visible non-empty mesh.";
        return r;
    }

    // Pick the material (defaults to a fallback if material_id is
    // out of range). The material's `useBaseColorTexture` flag +
    // `baseColorTextureId` field decide whether the closest-hit
    // samples a texture or falls back to flat baseColor.
    rr::material::MaterialParams material_params{};
    if (picked->material_id >= 0
     && static_cast<std::size_t>(picked->material_id) < scene.materials.size()) {
        material_params = scene.materials[picked->material_id].params;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        r.message = "render_textured_material: backend init failed: "
                  + backend.last_error();
        return r;
    }

    OptixPipeline pipeline;
    {
        OptixPipelineOptions opts;
        opts.path_tracer = false;
        const auto pr = pipeline.create(backend, opts);
        if (!pr.ok) {
            r.message = "render_textured_material: " + pr.error_message;
            return r;
        }
    }
    {
        // shading_mode = 1 = material-flat. Stage 20M extends
        // that branch to optionally sample a texture.
        const auto pr = pipeline.set_hit_material(
            material_params, /*shading_mode=*/1);
        if (!pr.ok) {
            r.message = "render_textured_material: " + pr.error_message;
            return r;
        }
    }

    // Position extraction + index upload (Stage 20F shape).
    std::vector<float> flat_positions;
    flat_positions.reserve(picked->vertices.size() * 3u);
    for (const auto& v : picked->vertices) {
        flat_positions.push_back(v.position.x);
        flat_positions.push_back(v.position.y);
        flat_positions.push_back(v.position.z);
    }
    // Stage 20M: also extract per-vertex UVs into a parallel
    // Vec2 buffer the closest-hit will index by triangle vertex
    // indices.
    std::vector<rr::math::Vec2> flat_uvs;
    flat_uvs.reserve(picked->vertices.size());
    for (const auto& v : picked->vertices) {
        flat_uvs.push_back(v.uv);
    }

    const std::size_t n_vertices  = picked->vertices.size();
    const std::size_t n_triangles = picked->triangles.size();

    // Device buffers: positions (for GAS) + indices (for GAS +
    // for closest-hit UV lookup) + UVs (for closest-hit) +
    // per-texture pixel buffers + DeviceTextureView array +
    // framebuffer. All allocations registered into a vector
    // for the unified cleanup path.
    std::vector<void*> device_allocs;
    auto cleanup = [&]() {
        for (auto* p : device_allocs) {
            if (p) ::cudaFree(p);
        }
        device_allocs.clear();
    };
    auto register_alloc = [&](void* p) -> void* {
        device_allocs.push_back(p);
        return p;
    };

    void* d_positions = nullptr;
    {
        const std::size_t bytes = flat_positions.size() * sizeof(float);
        if (::cudaMalloc(&d_positions, bytes) != cudaSuccess) {
            r.message = "render_textured_material: cudaMalloc(positions) failed";
            return r;
        }
        register_alloc(d_positions);
        if (::cudaMemcpy(d_positions, flat_positions.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            cleanup();
            r.message = "render_textured_material: cudaMemcpy(positions) failed";
            return r;
        }
    }

    void* d_indices = nullptr;
    {
        const std::size_t bytes =
            n_triangles * sizeof(rr::geometry::Triangle);
        if (::cudaMalloc(&d_indices, bytes) != cudaSuccess) {
            cleanup();
            r.message = "render_textured_material: cudaMalloc(indices) failed";
            return r;
        }
        register_alloc(d_indices);
        if (::cudaMemcpy(d_indices, picked->triangles.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            cleanup();
            r.message = "render_textured_material: cudaMemcpy(indices) failed";
            return r;
        }
    }

    void* d_uvs = nullptr;
    {
        const std::size_t bytes = flat_uvs.size() * sizeof(rr::math::Vec2);
        if (::cudaMalloc(&d_uvs, bytes) != cudaSuccess) {
            cleanup();
            r.message = "render_textured_material: cudaMalloc(uvs) failed";
            return r;
        }
        register_alloc(d_uvs);
        if (::cudaMemcpy(d_uvs, flat_uvs.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            cleanup();
            r.message = "render_textured_material: cudaMemcpy(uvs) failed";
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
            cleanup();
            r.message = "render_textured_material: " + gas_result.error_message;
            return r;
        }
    }

    // Stage 20M: upload textures. For each entry build a
    // per-texture pixel buffer + record a DeviceTextureView
    // entry pointing at it. Then upload the array of views to
    // a single device buffer.
    std::vector<rr::cuda::DeviceTextureView> view_host;
    view_host.reserve(textures.size());
    for (const auto& tex : textures) {
        rr::cuda::DeviceTextureView v{};
        v.width  = tex.width();
        v.height = tex.height();
        v.format = tex.format();

        const auto& bytes = tex.pixels();
        if (bytes.empty()) {
            // Zero-pixel texture: leave pixels = nullptr so the
            // sampler's `device_texture_view_valid` check
            // returns false and the magenta fallback fires.
            view_host.push_back(v);
            continue;
        }

        void* d_pixels = nullptr;
        if (::cudaMalloc(&d_pixels, bytes.size()) != cudaSuccess) {
            cleanup();
            r.message = "render_textured_material: cudaMalloc(texture pixels) failed";
            return r;
        }
        register_alloc(d_pixels);
        if (::cudaMemcpy(d_pixels, bytes.data(), bytes.size(),
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            cleanup();
            r.message = "render_textured_material: cudaMemcpy(texture pixels) failed";
            return r;
        }
        v.pixels = static_cast<const std::byte*>(d_pixels);
        view_host.push_back(v);
    }

    void* d_views = nullptr;
    if (!view_host.empty()) {
        const std::size_t bytes =
            view_host.size() * sizeof(rr::cuda::DeviceTextureView);
        if (::cudaMalloc(&d_views, bytes) != cudaSuccess) {
            cleanup();
            r.message = "render_textured_material: cudaMalloc(view array) failed";
            return r;
        }
        register_alloc(d_views);
        if (::cudaMemcpy(d_views, view_host.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            cleanup();
            r.message = "render_textured_material: cudaMemcpy(view array) failed";
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
        cleanup();
        r.message = "render_textured_material: cudaMalloc(framebuffer) failed";
        return r;
    }
    register_alloc(d_framebuffer);

    OptixLaunchParams params{};
    params.framebuffer  = static_cast<float*>(d_framebuffer);
    params.width        = width;
    params.height       = height;
    params.camera       = gpu_cam;
    params.scene_handle = gas_result.gas.handle();
    params.mesh_uvs     =
        static_cast<const rr::math::Vec2*>(d_uvs);
    params.mesh_indices =
        static_cast<const rr::geometry::Triangle*>(d_indices);
    params.textures     =
        static_cast<const rr::cuda::DeviceTextureView*>(d_views);
    params.texture_count = static_cast<std::int32_t>(view_host.size());

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &params, sizeof(params), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            cleanup();
            r.message = std::string("render_textured_material: ")
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
            cleanup();
            r.message = std::string("render_textured_material: "
                                    "optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return r;
        }
    }

    if (::cudaDeviceSynchronize() != cudaSuccess) {
        cleanup();
        r.message = "render_textured_material: cudaDeviceSynchronize failed";
        return r;
    }
    r.gpu_time_ms = timer.elapsed_ms();

    rr::image::Image img(width, height, rr::image::PixelFormat::Rgba32F);
    if (::cudaMemcpy(img.data(), d_framebuffer, framebuffer_bytes,
                     cudaMemcpyDeviceToHost) != cudaSuccess) {
        cleanup();
        r.message = "render_textured_material: cudaMemcpy(d->h) failed";
        return r;
    }

    cleanup();

    r.image   = std::move(img);
    r.ok      = true;
    r.message = "OptiX textured-material render complete.";
    return r;
}

OptixRenderer::AovResult
OptixRenderer::render_aovs(
    const rr::scene::Scene& scene,
    const std::vector<rr::lighting::Light>& lights,
    int width, int height) noexcept {
    AovResult R;

    if (width <= 0 || height <= 0) {
        R.message = "OptixRenderer::render_aovs: invalid dimensions";
        return R;
    }

    const rr::geometry::Mesh* picked = nullptr;
    for (const auto& sm : scene.meshes) {
        if (!sm.object.visible) continue;
        if (sm.geometry.empty()) continue;
        picked = &sm.geometry;
        break;
    }
    if (picked == nullptr) {
        R.message = "render_aovs: scene contains no visible non-empty mesh.";
        return R;
    }

    rr::material::MaterialParams material_params{};
    if (picked->material_id >= 0
     && static_cast<std::size_t>(picked->material_id) < scene.materials.size()) {
        material_params = scene.materials[picked->material_id].params;
    }

    OptixBackend backend;
    if (!backend.initialize()) {
        R.message = "render_aovs: backend init failed: "
                  + backend.last_error();
        return R;
    }

    OptixPipeline pipeline;
    {
        OptixPipelineOptions opts;
        opts.path_tracer = false;
        const auto pr = pipeline.create(backend, opts);
        if (!pr.ok) {
            R.message = "render_aovs: " + pr.error_message;
            return R;
        }
    }
    {
        const auto pr = pipeline.set_hit_material(
            material_params, /*shading_mode=*/2);
        if (!pr.ok) {
            R.message = "render_aovs: " + pr.error_message;
            return R;
        }
    }

    // Position extraction + index upload (Stage 20F shape).
    std::vector<float> flat_positions;
    flat_positions.reserve(picked->vertices.size() * 3u);
    for (const auto& v : picked->vertices) {
        flat_positions.push_back(v.position.x);
        flat_positions.push_back(v.position.y);
        flat_positions.push_back(v.position.z);
    }

    const std::size_t n_vertices  = picked->vertices.size();
    const std::size_t n_triangles = picked->triangles.size();

    // Track all device allocations for unified cleanup.
    std::vector<void*> device_allocs;
    auto cleanup = [&]() {
        for (auto* p : device_allocs) {
            if (p) ::cudaFree(p);
        }
        device_allocs.clear();
    };

    void* d_positions = nullptr;
    {
        const std::size_t bytes = flat_positions.size() * sizeof(float);
        if (::cudaMalloc(&d_positions, bytes) != cudaSuccess) {
            R.message = "render_aovs: cudaMalloc(positions) failed";
            return R;
        }
        device_allocs.push_back(d_positions);
        if (::cudaMemcpy(d_positions, flat_positions.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            cleanup();
            R.message = "render_aovs: cudaMemcpy(positions) failed";
            return R;
        }
    }
    void* d_indices = nullptr;
    {
        const std::size_t bytes =
            n_triangles * sizeof(rr::geometry::Triangle);
        if (::cudaMalloc(&d_indices, bytes) != cudaSuccess) {
            cleanup();
            R.message = "render_aovs: cudaMalloc(indices) failed";
            return R;
        }
        device_allocs.push_back(d_indices);
        if (::cudaMemcpy(d_indices, picked->triangles.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            cleanup();
            R.message = "render_aovs: cudaMemcpy(indices) failed";
            return R;
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
            cleanup();
            R.message = "render_aovs: " + gas_result.error_message;
            return R;
        }
    }

    // Lights upload (Stage 20K shape).
    void* d_lights = nullptr;
    const int light_count = static_cast<int>(lights.size());
    if (light_count > 0) {
        const std::size_t bytes = lights.size() * sizeof(rr::lighting::Light);
        if (::cudaMalloc(&d_lights, bytes) != cudaSuccess) {
            cleanup();
            R.message = "render_aovs: cudaMalloc(lights) failed";
            return R;
        }
        device_allocs.push_back(d_lights);
        if (::cudaMemcpy(d_lights, lights.data(), bytes,
                         cudaMemcpyHostToDevice) != cudaSuccess) {
            cleanup();
            R.message = "render_aovs: cudaMemcpy(lights) failed";
            return R;
        }
    }

    rr::camera::Camera camera = scene.camera;
    camera.set_aspect(static_cast<float>(width)
                    / static_cast<float>(height));
    const rr::camera::GpuCamera gpu_cam = camera.to_gpu();

    // Allocate the framebuffer + the six per-AOV device buffers.
    // Component counts mirror `rr::renderer::aov_component_count`:
    //   beauty / normal / albedo : 3 floats / pixel
    //   depth / doppler_factor / searchlight_factor : 1 float / pixel
    const std::size_t pixel_count =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height);
    const std::size_t framebuffer_floats = pixel_count * 4u;
    const std::size_t aov3_floats        = pixel_count * 3u;
    const std::size_t aov1_floats        = pixel_count;

    auto alloc_aov = [&](std::size_t floats, void*& out) -> bool {
        if (::cudaMalloc(&out, floats * sizeof(float)) != cudaSuccess) {
            return false;
        }
        device_allocs.push_back(out);
        return true;
    };

    void* d_framebuffer = nullptr;
    void* d_aov_beauty  = nullptr;
    void* d_aov_normal  = nullptr;
    void* d_aov_depth   = nullptr;
    void* d_aov_albedo  = nullptr;
    void* d_aov_doppler = nullptr;
    void* d_aov_search  = nullptr;
    if (!alloc_aov(framebuffer_floats, d_framebuffer)
     || !alloc_aov(aov3_floats,        d_aov_beauty)
     || !alloc_aov(aov3_floats,        d_aov_normal)
     || !alloc_aov(aov1_floats,        d_aov_depth)
     || !alloc_aov(aov3_floats,        d_aov_albedo)
     || !alloc_aov(aov1_floats,        d_aov_doppler)
     || !alloc_aov(aov1_floats,        d_aov_search)) {
        cleanup();
        R.message = "render_aovs: cudaMalloc(framebuffer / AOV buffers) failed";
        return R;
    }

    // Stage 14A.3 / CUDA --render-aovs precedent: set a non-
    // zero observer velocity so the DopplerFactor /
    // SearchlightFactor AOVs show visible variation across the
    // framebuffer rather than a flat 1.0. beta = 0.5 along -Z
    // mirrors the CUDA dispatcher's choice exactly.
    rr::relativity::Observer aov_observer;
    aov_observer.velocity = rr::math::Vec3{0.0f, 0.0f, -0.5f};

    OptixLaunchParams params{};
    params.framebuffer  = static_cast<float*>(d_framebuffer);
    params.width        = width;
    params.height       = height;
    params.camera       = gpu_cam;
    params.scene_handle = gas_result.gas.handle();
    params.observer     = aov_observer;
    params.lights       =
        static_cast<const rr::lighting::Light*>(d_lights);
    params.light_count  = light_count;
    params.aov_beauty             = static_cast<float*>(d_aov_beauty);
    params.aov_normal             = static_cast<float*>(d_aov_normal);
    params.aov_depth              = static_cast<float*>(d_aov_depth);
    params.aov_albedo             = static_cast<float*>(d_aov_albedo);
    params.aov_doppler_factor     = static_cast<float*>(d_aov_doppler);
    params.aov_searchlight_factor = static_cast<float*>(d_aov_search);

    {
        const ::cudaError_t e = ::cudaMemcpy(
            pipeline.launch_params_device_ptr(),
            &params, sizeof(params), cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            cleanup();
            R.message = std::string("render_aovs: ")
                      + "cudaMemcpy(launch params) failed: "
                      + ::cudaGetErrorString(e);
            return R;
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
            cleanup();
            R.message = std::string("render_aovs: optixLaunch failed: ")
                      + ::optixGetErrorName(res);
            return R;
        }
    }
    if (::cudaDeviceSynchronize() != cudaSuccess) {
        cleanup();
        R.message = "render_aovs: cudaDeviceSynchronize failed";
        return R;
    }
    R.gpu_time_ms = timer.elapsed_ms();

    // Download each AOV. Scalar AOVs (depth, doppler,
    // searchlight) are downloaded as 1-float-per-pixel and
    // replicated to RGB for direct PPM viewing — same shape
    // the CUDA path's `save_aov_to_ppm` helper does on the
    // host. We do the replication here so the AovResult's
    // images are all Rgb32F-uniform, ready for `save_ppm`.
    auto download_3 = [&](void* d_buf,
                          rr::image::Image& img) -> bool {
        img = rr::image::Image(width, height,
                               rr::image::PixelFormat::Rgb32F);
        return ::cudaMemcpy(img.data(), d_buf,
                            aov3_floats * sizeof(float),
                            cudaMemcpyDeviceToHost) == cudaSuccess;
    };
    auto download_1_replicate = [&](void* d_buf,
                                    rr::image::Image& img) -> bool {
        std::vector<float> scalars(aov1_floats);
        if (::cudaMemcpy(scalars.data(), d_buf,
                         aov1_floats * sizeof(float),
                         cudaMemcpyDeviceToHost) != cudaSuccess) {
            return false;
        }
        img = rr::image::Image(width, height,
                               rr::image::PixelFormat::Rgb32F);
        float* dst = img.data();
        for (std::size_t i = 0; i < aov1_floats; ++i) {
            dst[i * 3 + 0] = scalars[i];
            dst[i * 3 + 1] = scalars[i];
            dst[i * 3 + 2] = scalars[i];
        }
        return true;
    };

    if (!download_3(d_aov_beauty, R.beauty)
     || !download_3(d_aov_normal, R.normal)
     || !download_3(d_aov_albedo, R.albedo)
     || !download_1_replicate(d_aov_depth,   R.depth)
     || !download_1_replicate(d_aov_doppler, R.doppler_factor)
     || !download_1_replicate(d_aov_search,  R.searchlight_factor)) {
        cleanup();
        R.message = "render_aovs: cudaMemcpy(d->h, AOV buffer) failed";
        return R;
    }

    cleanup();
    R.ok      = true;
    R.message = "OptiX AOVs render complete.";
    return R;
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

OptixRenderer::PathtraceProgressiveResult
OptixRenderer::render_pathtrace_progressive(
    const rr::scene::Scene& /*scene*/,
    int /*width*/,
    int /*height*/,
    int /*max_bounces*/,
    unsigned int /*seed*/,
    const std::vector<int>& /*checkpoint_samples*/) noexcept {
    PathtraceProgressiveResult r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_pathtrace_progressive requires the "
        "OptiX SDK; rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_textured_material(
    const rr::scene::Scene& /*scene*/,
    const std::vector<rr::texture::ImageTexture>& /*textures*/,
    int /*width*/,
    int /*height*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_textured_material requires the "
        "OptiX SDK; rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::Result
OptixRenderer::render_direct_lighting(const rr::scene::Scene& /*scene*/,
                                      int /*width*/,
                                      int /*height*/,
                                      bool /*enable_shadows*/) noexcept {
    Result r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_direct_lighting requires the OptiX "
        "SDK; rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

OptixRenderer::AovResult
OptixRenderer::render_aovs(
    const rr::scene::Scene& /*scene*/,
    const std::vector<rr::lighting::Light>& /*lights*/,
    int /*width*/,
    int /*height*/) noexcept {
    AovResult r;
    r.ok = false;
    r.message =
        "OptixRenderer::render_aovs requires the OptiX SDK; "
        "rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

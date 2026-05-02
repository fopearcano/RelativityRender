#include "optix/OptixRenderer.h"

#include "optix/OptixBackend.h"
#include "optix/OptixPipeline.h"

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <cuda_runtime.h>
    #include <optix.h>
    #include <optix_stubs.h>

    #include "optix/OptixLaunchParams.h"

    #include <cstddef>
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

    // Launch.
    {
        const auto* sbt = static_cast<const ::OptixShaderBindingTable*>(
            pipeline.shader_binding_table());
        const ::OptixResult res = ::optixLaunch(
            static_cast<::OptixPipeline>(pipeline.pipeline_handle()),
            /*stream=*/0,
            reinterpret_cast<::CUdeviceptr>(pipeline.launch_params_device_ptr()),
            pipeline.launch_params_size_bytes(),
            sbt,
            static_cast<unsigned>(width),
            static_cast<unsigned>(height),
            /*depth=*/1u);
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

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

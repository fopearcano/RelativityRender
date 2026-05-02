#include "pathtracer/PathTracer.h"

#include "gpu/GpuBuffer.h"
#include "gpu/GpuScene.h"
#include "gpu/GpuTiming.h"
#include "renderer/AccumulationBuffer.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaPathTracer.cuh"
#endif

#include <cstddef>
#include <string>
#include <utility>

namespace rr::pathtracer {

PathTraceResult PathTracer::render(const rr::gpu::GpuScene& scene,
                                   int width, int height,
                                   const PathTraceConfig& cfg) const {
    PathTraceResult result;

    if (width <= 0 || height <= 0) {
        result.message = "invalid dimensions";
        return result;
    }
    if (cfg.samples_per_pixel <= 0) {
        result.message = "samples_per_pixel must be > 0";
        return result;
    }
    if (cfg.max_bounces < 0) {
        result.message = "max_bounces must be >= 0";
        return result;
    }
    if (cfg.environment_intensity < 0.0f) {
        result.message = "environment_intensity must be >= 0";
        return result;
    }

#ifndef RR_HAS_CUDA
    (void)scene;
    result.message =
        "PathTracer::render requires CUDA. Rebuild with "
        "-DRR_ENABLE_CUDA=ON on a host with the CUDA Toolkit and a "
        "CUDA-capable GPU.";
    return result;
#else
    // Allocate accumulation + per-sample buffers. The accumulator
    // stores per-channel sums; the sample buffer holds one frame
    // of radiance estimates that the host loop pumps into the
    // accumulator.
    rr::renderer::AccumulationBuffer accum;
    if (!accum.resize(width, height) || !accum.valid()) {
        result.message = "accumulation buffer allocation failed";
        return result;
    }

    const std::size_t float_count =
        static_cast<std::size_t>(width) * height * 4u;

    rr::gpu::GpuBuffer<float> sample;
    if (!sample.allocate(float_count)) {
        result.message = "sample buffer allocation failed";
        return result;
    }

    // Stage 18A.1 GPU timing: a single GpuTimer pair brackets the
    // full spp loop (per-sample path-trace kernel + per-sample
    // accumulate kernel). Both kernels enqueue on the default
    // stream, so the start / stop events bound the sum of every
    // GPU-side kernel run for this render. The events are read
    // back after the loop (which already implicitly synchronises
    // via `accumulate_sample` / `resolve_to_image`), so the per-
    // launch cost is one async marker write per sample-kernel
    // boundary rather than per-pixel work.
    rr::gpu::GpuTimer timer;
    timer.start();
    for (int s = 0; s < cfg.samples_per_pixel; ++s) {
        if (!rr::cuda::launch_pathtrace_sample(
                sample.device_ptr(), width, height,
                scene,
                cfg.max_bounces,
                cfg.seed,
                static_cast<unsigned int>(s),
                cfg.environment_color,
                cfg.environment_intensity)) {
            result.message =
                "pathtrace sample-kernel launch failed at iteration "
                + std::to_string(s);
            return result;
        }
        if (!accum.accumulate_sample(sample.device_ptr())) {
            result.message =
                "accumulate_sample failed at iteration "
                + std::to_string(s);
            return result;
        }
    }
    timer.stop();

    rr::image::Image img = accum.resolve_to_image();
    if (img.empty()) {
        result.message = "resolve_to_image returned empty";
        return result;
    }

    // `resolve_to_image` performs a device-to-host download that
    // implicitly synchronises the stream, so by the time we read
    // `elapsed_ms` the stop event's timestamp is already
    // available - no extra wait.
    result.gpu_time_ms = timer.elapsed_ms();

    result.image = std::move(img);
    result.ok    = true;
    return result;
#endif
}

}  // namespace rr::pathtracer

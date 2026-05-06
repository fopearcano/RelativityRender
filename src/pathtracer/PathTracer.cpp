#include "pathtracer/PathTracer.h"

#include "core/Logger.h"
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
    // PT-P.9 soft upper cap. Callers asking for absurdly large
    // spp budgets get a single warning + clamp to
    // `kSamplesPerPixelCap` rather than a launcher loop that
    // issues that many kernel launches. Mirrors the PT-P.6
    // max-bounces clamp shape. Behaviour for
    // `samples_per_pixel in [1, kSamplesPerPixelCap]` is
    // byte-identical with the pre-PT-P.9 path tracer.
    int effective_samples_per_pixel = cfg.samples_per_pixel;
    if (effective_samples_per_pixel > kSamplesPerPixelCap) {
        rr::core::Logger::warning(
            "PathTraceConfig::samples_per_pixel=" +
            std::to_string(cfg.samples_per_pixel) +
            " exceeds the recommended cap of " +
            std::to_string(kSamplesPerPixelCap) +
            "; clamping. Set explicitly via the dispatcher CLI "
            "when very long sample budgets are needed.");
        effective_samples_per_pixel = kSamplesPerPixelCap;
    }
    if (cfg.max_bounces < 0) {
        result.message = "max_bounces must be >= 0";
        return result;
    }
    // PT-P.6 soft upper cap. Callers asking for absurdly long
    // paths get a single warning + clamp to `kMaxBouncesCap`
    // rather than a kernel that does not crash but takes many
    // times longer than the default. Mirrors the warn-and-clamp
    // shape `validate_material_texture_ids` (TEX-P.2 / TEX-P.5)
    // uses. Behaviour for `max_bounces in [0, kMaxBouncesCap]`
    // is byte-identical with the pre-PT-P.6 path tracer.
    int effective_max_bounces = cfg.max_bounces;
    if (effective_max_bounces > kMaxBouncesCap) {
        rr::core::Logger::warning(
            "PathTraceConfig::max_bounces=" +
            std::to_string(cfg.max_bounces) +
            " exceeds the recommended cap of " +
            std::to_string(kMaxBouncesCap) +
            "; clamping. Set explicitly via the dispatcher CLI "
            "when long bounce paths are needed.");
        effective_max_bounces = kMaxBouncesCap;
    }
    if (cfg.environment_intensity < 0.0f) {
        result.message = "environment_intensity must be >= 0";
        return result;
    }
    // PT-P.24: lower-bound rejection on the firefly_clamp field
    // (PT-P.21 placeholder; PT-P.24 wires it through both
    // backends). Default 0.0f is the no-clamp path; positive
    // values trigger the per-channel `fminf` clamp the kernel
    // applies before each per-pixel write. Negative values are
    // not meaningful (the integrator's per-channel radiance is
    // non-negative); we reject them with the same diagnostic
    // shape the env-intensity check uses.
    if (cfg.firefly_clamp < 0.0f) {
        result.message = "firefly_clamp must be >= 0";
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
    for (int s = 0; s < effective_samples_per_pixel; ++s) {
        if (!rr::cuda::launch_pathtrace_sample(
                sample.device_ptr(), width, height,
                scene,
                effective_max_bounces,
                cfg.seed,
                static_cast<unsigned int>(s),
                cfg.environment_color,
                cfg.environment_intensity,
                cfg.firefly_clamp)) {
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

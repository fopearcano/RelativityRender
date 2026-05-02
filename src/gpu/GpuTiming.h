#pragma once

#include <string>

// Stage 18A.1 GPU timing surface, host-friendly. Pure host C++; the
// `<cuda_runtime.h>` dependency is hidden inside `GpuTiming.cpp` (which
// forwards to `cuda/CudaTiming.h` when `RR_HAS_CUDA` is defined). Code
// that consumes this header - the renderers (`CudaRenderer`,
// `OptixRenderer`, `PathTracer`) and the CLI handlers - does not need
// to be compiled by nvcc.
//
// The timer wraps a pair of CUDA events. `start()` and `stop()` enqueue
// the events on the default stream (cheap - they just record a marker;
// the GPU writes the timestamp asynchronously). `elapsed_ms()`
// synchronises on the stop event and returns the GPU-side time between
// the two markers, in milliseconds. Without CUDA support the methods
// are no-ops and `elapsed_ms()` returns 0.

namespace rr::gpu {

// Move-only RAII owner for a CUDA-event pair. Construction allocates
// the pair via `cudaEventCreate`; failure is signalled by `valid()`
// returning false (subsequent calls become no-ops, `elapsed_ms()`
// returns 0). The destructor frees the events.
//
// Stage 18A.1 instrumentation: the renderers wrap their kernel-launch
// region in a `GpuTimer` and surface the elapsed time through their
// result struct. Adding events on the default stream does not change
// the rendered pixels and adds essentially zero per-launch overhead -
// the runtime cost is one async timestamp write per record + one
// in-cache synchronise / read after the existing
// `cudaDeviceSynchronize()`.
class GpuTimer {
public:
    GpuTimer() noexcept;
    ~GpuTimer();
    GpuTimer(const GpuTimer&)            = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;
    GpuTimer(GpuTimer&&) noexcept;
    GpuTimer& operator=(GpuTimer&&) noexcept;

    // Were the underlying CUDA events allocated successfully? Always
    // `false` on a no-CUDA build.
    [[nodiscard]] bool valid() const noexcept;

    // Record the start / stop markers on the default stream. No-op
    // when `valid()` is false.
    void start() noexcept;
    void stop()  noexcept;

    // Synchronise on the stop event and return the elapsed GPU time
    // in milliseconds between `start()` and `stop()`. Returns 0 on
    // failure, on a no-CUDA build, or if the events were not
    // recorded in the right order.
    [[nodiscard]] float elapsed_ms() noexcept;

private:
    // Opaque `cudaEvent_t` handles when CUDA is enabled; null
    // otherwise. Stored as `void*` so this header stays free of
    // `<cuda_runtime.h>`.
    void* start_event_ = nullptr;
    void* stop_event_  = nullptr;
};

// Format a single-line "[GPU] <label>: render time = X.XXX ms;
// primary rays = N (WxH); rays/sec = Y.YY M" string suitable for
// `Logger::info`. Pure host C++; works with or without CUDA.
//
// Returns an empty string when `gpu_time_ms <= 0`, so callers can
// skip the log line if timing was not measured (e.g. on a no-CUDA
// build or after an early-failure exit before `stop()` recorded).
[[nodiscard]] std::string format_gpu_timing_line(const char* label,
                                                 int width, int height,
                                                 float gpu_time_ms);

}  // namespace rr::gpu

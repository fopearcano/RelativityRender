#include "gpu/GpuTiming.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaTiming.h"
#endif

#include <cstdio>
#include <utility>

namespace rr::gpu {

#ifdef RR_HAS_CUDA

GpuTimer::GpuTimer() noexcept {
    start_event_ = rr::cuda::cuda_event_create();
    stop_event_  = rr::cuda::cuda_event_create();
    // If either allocation failed, free the survivor so `valid()`
    // reports false consistently (rather than half-valid).
    if (!start_event_ || !stop_event_) {
        if (stop_event_)  rr::cuda::cuda_event_destroy(stop_event_);
        if (start_event_) rr::cuda::cuda_event_destroy(start_event_);
        start_event_ = nullptr;
        stop_event_  = nullptr;
    }
}

GpuTimer::~GpuTimer() {
    if (stop_event_)  rr::cuda::cuda_event_destroy(stop_event_);
    if (start_event_) rr::cuda::cuda_event_destroy(start_event_);
}

GpuTimer::GpuTimer(GpuTimer&& other) noexcept
    : start_event_(other.start_event_),
      stop_event_(other.stop_event_) {
    other.start_event_ = nullptr;
    other.stop_event_  = nullptr;
}

GpuTimer& GpuTimer::operator=(GpuTimer&& other) noexcept {
    if (this != &other) {
        if (stop_event_)  rr::cuda::cuda_event_destroy(stop_event_);
        if (start_event_) rr::cuda::cuda_event_destroy(start_event_);
        start_event_       = other.start_event_;
        stop_event_        = other.stop_event_;
        other.start_event_ = nullptr;
        other.stop_event_  = nullptr;
    }
    return *this;
}

bool GpuTimer::valid() const noexcept {
    return start_event_ != nullptr && stop_event_ != nullptr;
}

void GpuTimer::start() noexcept {
    if (!valid()) return;
    rr::cuda::cuda_event_record(start_event_);
}

void GpuTimer::stop() noexcept {
    if (!valid()) return;
    rr::cuda::cuda_event_record(stop_event_);
}

float GpuTimer::elapsed_ms() noexcept {
    if (!valid()) return 0.0f;
    return rr::cuda::cuda_event_elapsed_ms(start_event_, stop_event_);
}

#else  // RR_HAS_CUDA

GpuTimer::GpuTimer() noexcept                     = default;
GpuTimer::~GpuTimer()                             = default;
GpuTimer::GpuTimer(GpuTimer&&) noexcept           = default;
GpuTimer& GpuTimer::operator=(GpuTimer&&) noexcept = default;

bool  GpuTimer::valid() const noexcept { return false; }
void  GpuTimer::start() noexcept       {}
void  GpuTimer::stop()  noexcept       {}
float GpuTimer::elapsed_ms() noexcept  { return 0.0f; }

#endif  // RR_HAS_CUDA

std::string format_gpu_timing_line(const char* label,
                                   int width, int height,
                                   float gpu_time_ms) {
    if (gpu_time_ms <= 0.0f) return {};

    const long long pixels = static_cast<long long>(width)
                           * static_cast<long long>(height);
    const double seconds   = static_cast<double>(gpu_time_ms) * 1.0e-3;
    const double rays_sec  = (seconds > 0.0)
                           ? static_cast<double>(pixels) / seconds
                           : 0.0;

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[GPU] %s: render time = %.3f ms; primary rays = %lld "
                  "(%dx%d); rays/sec = %.2f M",
                  label ? label : "render",
                  static_cast<double>(gpu_time_ms),
                  pixels, width, height, rays_sec * 1.0e-6);
    return std::string(buf);
}

}  // namespace rr::gpu

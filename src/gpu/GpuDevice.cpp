#include "gpu/GpuDevice.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaContext.h"
#endif

#include <string>

namespace rr::gpu {

std::string GpuDevice::compute_capability_string() const {
    return std::to_string(compute_capability_major) + "."
         + std::to_string(compute_capability_minor);
}

std::string GpuDevice::total_memory_human() const {
    constexpr std::size_t kMiB = 1024ull * 1024ull;
    return std::to_string(total_memory_bytes / kMiB) + " MiB";
}

bool gpu_backend_available() noexcept {
#ifdef RR_HAS_CUDA
    return true;
#else
    return false;
#endif
}

std::string gpu_backend_name() {
#ifdef RR_HAS_CUDA
    return "CUDA";
#else
    return "(none)";
#endif
}

std::vector<GpuDevice> enumerate_devices() {
#ifdef RR_HAS_CUDA
    return rr::cuda::query_devices();
#else
    return {};
#endif
}

}

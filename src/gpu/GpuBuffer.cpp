#include "gpu/GpuBuffer.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaBuffer.h"
#endif

namespace rr::gpu::detail {

void* gpu_alloc(std::size_t bytes) {
#ifdef RR_HAS_CUDA
    return rr::cuda::cuda_alloc(bytes);
#else
    (void)bytes;
    return nullptr;
#endif
}

void gpu_free(void* device_ptr) noexcept {
#ifdef RR_HAS_CUDA
    rr::cuda::cuda_free(device_ptr);
#else
    (void)device_ptr;
#endif
}

bool gpu_copy_host_to_device(void* device_dst, const void* host_src, std::size_t bytes) {
#ifdef RR_HAS_CUDA
    return rr::cuda::cuda_copy_h2d(device_dst, host_src, bytes);
#else
    (void)device_dst; (void)host_src; (void)bytes;
    return false;
#endif
}

bool gpu_copy_device_to_host(void* host_dst, const void* device_src, std::size_t bytes) {
#ifdef RR_HAS_CUDA
    return rr::cuda::cuda_copy_d2h(host_dst, device_src, bytes);
#else
    (void)host_dst; (void)device_src; (void)bytes;
    return false;
#endif
}

}

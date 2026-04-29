#include "cuda/CudaBuffer.h"

#include <cuda_runtime.h>

namespace rr::cuda {

namespace {

// Reset the sticky CUDA last-error flag. Called on the failure paths so
// a later real CUDA call doesn't observe a stale error.
void clear_last_error() noexcept {
    (void)cudaGetLastError();
}

}

void* cuda_alloc(std::size_t bytes) {
    if (bytes == 0) return nullptr;
    void* ptr = nullptr;
    if (cudaMalloc(&ptr, bytes) != cudaSuccess) {
        clear_last_error();
        return nullptr;
    }
    return ptr;
}

void cuda_free(void* device_ptr) noexcept {
    if (!device_ptr) return;
    if (cudaFree(device_ptr) != cudaSuccess) {
        clear_last_error();
    }
}

bool cuda_copy_h2d(void* device_dst, const void* host_src, std::size_t bytes) {
    if (bytes == 0) return true;
    if (!device_dst || !host_src) return false;
    if (cudaMemcpy(device_dst, host_src, bytes, cudaMemcpyHostToDevice) != cudaSuccess) {
        clear_last_error();
        return false;
    }
    return true;
}

bool cuda_copy_d2h(void* host_dst, const void* device_src, std::size_t bytes) {
    if (bytes == 0) return true;
    if (!host_dst || !device_src) return false;
    if (cudaMemcpy(host_dst, device_src, bytes, cudaMemcpyDeviceToHost) != cudaSuccess) {
        clear_last_error();
        return false;
    }
    return true;
}

}

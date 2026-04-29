#include "cuda/CudaContext.h"

#include <cuda_runtime.h>

#include <cstddef>
#include <utility>

namespace rr::cuda {

std::vector<gpu::GpuDevice> query_devices() {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count <= 0) {
        // Reset the sticky last-error flag so a later (real) CUDA call
        // doesn't observe a leftover initialization error.
        cudaGetLastError();
        return {};
    }

    std::vector<gpu::GpuDevice> devices;
    devices.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        cudaDeviceProp props{};
        if (cudaGetDeviceProperties(&props, i) != cudaSuccess) {
            cudaGetLastError();
            continue;
        }

        gpu::GpuDevice d;
        d.index                    = i;
        d.name                     = props.name;
        d.compute_capability_major = props.major;
        d.compute_capability_minor = props.minor;
        d.total_memory_bytes       = props.totalGlobalMem;
        d.multiprocessor_count     = props.multiProcessorCount;
        devices.push_back(std::move(d));
    }

    return devices;
}

}

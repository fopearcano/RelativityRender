#include "cuda/CudaTiming.h"

#include <cuda_runtime.h>

namespace rr::cuda {

namespace {

// Reset the sticky CUDA last-error flag so a later real CUDA call
// does not observe an error left over from a timer-side failure.
void clear_last_error() noexcept {
    (void)cudaGetLastError();
}

}  // namespace

void* cuda_event_create() noexcept {
    cudaEvent_t e = nullptr;
    if (cudaEventCreate(&e) != cudaSuccess) {
        clear_last_error();
        return nullptr;
    }
    return reinterpret_cast<void*>(e);
}

void cuda_event_destroy(void* event) noexcept {
    if (!event) return;
    if (cudaEventDestroy(reinterpret_cast<cudaEvent_t>(event)) != cudaSuccess) {
        clear_last_error();
    }
}

void cuda_event_record(void* event) noexcept {
    if (!event) return;
    if (cudaEventRecord(reinterpret_cast<cudaEvent_t>(event), /*stream=*/0)
        != cudaSuccess) {
        clear_last_error();
    }
}

float cuda_event_elapsed_ms(void* start_event, void* stop_event) noexcept {
    if (!start_event || !stop_event) return 0.0f;
    auto s = reinterpret_cast<cudaEvent_t>(start_event);
    auto e = reinterpret_cast<cudaEvent_t>(stop_event);

    // Ensure the GPU has finished recording the stop event before
    // we read the elapsed time. The renderers already call
    // `cudaDeviceSynchronize()` after the kernel launch, so this
    // is typically a fast in-cache check; we keep it here so the
    // timer is correct regardless of caller behaviour.
    if (cudaEventSynchronize(e) != cudaSuccess) {
        clear_last_error();
        return 0.0f;
    }
    float ms = 0.0f;
    if (cudaEventElapsedTime(&ms, s, e) != cudaSuccess) {
        clear_last_error();
        return 0.0f;
    }
    return ms;
}

}  // namespace rr::cuda

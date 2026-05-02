#pragma once

// Stage 18A.1 GPU timing primitives. Thin host-side wrappers around
// `cudaEvent_t` lifecycle / record / elapsed-time queries, exposed
// as `void*` so this header is pure host C++ (no `<cuda_runtime.h>`
// required at the include site). The implementation in
// `CudaTiming.cpp` includes the runtime header and reinterpret-casts
// the void pointers back to `cudaEvent_t`. Mirrors the
// `cuda_alloc` / `cuda_free` / `cuda_copy_*` shape in
// `cuda/CudaBuffer.h`.
//
// Every function is `noexcept`; failure is signalled via a null
// return / a 0 elapsed time so callers do not need to thread CUDA
// error strings through their result types. Sticky last-error flags
// are cleared on the failure paths so a subsequent CUDA call does
// not observe a stale error from the timer.

namespace rr::cuda {

// Allocate a default-flag CUDA event. Returns `nullptr` on failure.
[[nodiscard]] void* cuda_event_create() noexcept;

// Free an event allocated via `cuda_event_create`. Tolerates
// `nullptr` so RAII destructors stay simple.
void cuda_event_destroy(void* event) noexcept;

// Enqueue an event-record on the default stream (stream 0). Tolerates
// `nullptr`. Cheap: just a marker insertion onto the stream; the GPU
// records the timestamp asynchronously.
void cuda_event_record(void* event) noexcept;

// Synchronise on `stop_event` (so the underlying timestamp is ready)
// and return the elapsed milliseconds between `start_event` and
// `stop_event`. Returns 0 if either pointer is null or the
// underlying CUDA call failed.
[[nodiscard]] float cuda_event_elapsed_ms(void* start_event,
                                          void* stop_event) noexcept;

}  // namespace rr::cuda

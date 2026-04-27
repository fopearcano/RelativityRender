#pragma once

#include <cstddef>

// CUDA byte-level memory primitives consumed by `rr::gpu::GpuBuffer<T>`.
// Compiled only when `-DRR_ENABLE_CUDA=ON`. The header is intentionally
// CUDA-Runtime-free: `cuda_runtime.h` is included only by the matching
// `.cpp`, so callers (including templated header-only consumers in
// `rr::gpu::`) do not need the CUDA toolchain on their include path.
//
// Errors are surfaced as boolean returns or `nullptr`. The CUDA sticky
// last-error flag is reset before each call returns failure, so a
// caller's later real CUDA call observes a clean state.

namespace rr::cuda {

[[nodiscard]] void* cuda_alloc(std::size_t bytes);
void                cuda_free (void* device_ptr) noexcept;

[[nodiscard]] bool cuda_copy_h2d(void* device_dst, const void* host_src,   std::size_t bytes);
[[nodiscard]] bool cuda_copy_d2h(void* host_dst,   const void* device_src, std::size_t bytes);

}

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rr::gpu {

// Backend-agnostic description of a single GPU. Populated by the
// concrete backend (CUDA today; OptiX shares the same CUDA-side device
// info; future backends slot in the same way).
struct GpuDevice {
    int          index                    = -1;
    std::string  name;
    int          compute_capability_major = 0;
    int          compute_capability_minor = 0;
    std::size_t  total_memory_bytes       = 0;   // VRAM
    int          multiprocessor_count     = 0;   // streaming multiprocessors

    [[nodiscard]] std::string compute_capability_string() const;
    [[nodiscard]] std::string total_memory_human() const;          // "24576 MiB"
};

// True when a GPU backend was compiled in (currently: CUDA, gated by
// the `RR_ENABLE_CUDA` CMake option). When false, `enumerate_devices`
// returns an empty list and the renderer should report this honestly.
[[nodiscard]] bool gpu_backend_available() noexcept;

// "CUDA" when CUDA is compiled in, "(none)" otherwise.
[[nodiscard]] std::string gpu_backend_name();

// Enumerate every visible device for the compiled-in backend. Returns
// empty if no backend is compiled, the runtime fails to initialize, or
// no devices are present.
[[nodiscard]] std::vector<GpuDevice> enumerate_devices();

}

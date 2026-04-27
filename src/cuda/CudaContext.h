#pragma once

#include "gpu/GpuDevice.h"

#include <vector>

// CUDA backend. This header is only meaningful when the project is built
// with `-DRR_ENABLE_CUDA=ON`; the matching translation unit is added to
// the build only in that case (see top-level CMakeLists.txt). Code
// outside the CUDA backend should call into `rr::gpu::` instead - it
// compiles whether or not CUDA is enabled.
namespace rr::cuda {

// Enumerate visible CUDA devices via the CUDA Runtime API. Returns
// empty when:
//   - the runtime fails to initialize (e.g. no driver),
//   - no CUDA-capable device is present, or
//   - per-device property queries fail.
[[nodiscard]] std::vector<gpu::GpuDevice> query_devices();

}

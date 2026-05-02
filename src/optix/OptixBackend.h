#pragma once

#include <string>

// Stage 17A.1 owner of the real OptiX device context. Per
// `docs/OPTIX_BACKEND_PLAN.md` §18, this is the analogue of
// `cuda/CudaContext.{h,cpp}` for the OptiX backend.
//
// Stages 12B.1-12B.5 shipped a file skeleton with only the static
// `isCompiled()` / `isSdkFound()` queries; Stage 17A.1 adds the
// non-static lifecycle (`initialize`, `shutdown`, accessors) so a
// caller can actually create + destroy an `OptixDeviceContext`.
//
// The header deliberately does NOT include `<optix.h>`. The handle
// is exposed as a `void*` (real type: `OptixDeviceContext`) so
// downstream consumers that don't need to call OptiX-typed APIs
// directly can include this header cleanly. The .cpp reinterprets
// the pointer back to the typed handle; pipeline / SBT / AS code
// that actually consumes the context will include `<optix.h>` and
// reinterpret on the consumer side.
//
// Stage 17A.1 scope: context init + interop only. NO pipelines,
// NO modules, NO SBTs, NO renders. Subsequent 17A+ sub-stages
// build the launch lifecycle on top.
//
// Two layers of compile-time gating:
// - `RELATIVITYRENDER_ENABLE_OPTIX` - the user toggled the OptiX
//   backend on at CMake time. When this is undefined, rr_optix is
//   not compiled at all.
// - `RELATIVITYRENDER_OPTIX_SDK_FOUND` - the SDK headers were
//   located during configure. When this is defined the .cpp
//   includes `<optix.h>` and calls real OptiX APIs; when it is
//   not defined the .cpp compiles a stub that returns failure
//   from `initialize()` with a clear `last_error()` message. This
//   keeps the audit-host build (`-DRELATIVITYRENDER_ENABLE_OPTIX=
//   ON` without an SDK install) compiling.

namespace rr::optix {

class OptixBackend {
public:
    // ---- compile-time queries (always available) -------------------

    // True iff the project was compiled with
    // `RELATIVITYRENDER_ENABLE_OPTIX` defined.
    [[nodiscard]] static bool isCompiled() noexcept;

    // True iff the CMake configure stage located an OptiX SDK
    // install (Stage 12B.4 detection). When false,
    // `initialize()` returns false with a "SDK not found" error.
    [[nodiscard]] static bool isSdkFound() noexcept;

    // ---- lifecycle (Stage 17A.1) ----------------------------------

    OptixBackend() noexcept;
    ~OptixBackend();

    OptixBackend(const OptixBackend&)            = delete;
    OptixBackend& operator=(const OptixBackend&) = delete;
    OptixBackend(OptixBackend&&) noexcept;
    OptixBackend& operator=(OptixBackend&&) noexcept;

    // Initialise the OptiX runtime + create an `OptixDeviceContext`
    // bound to the current CUDA primary context. CUDA <-> OptiX
    // interop is wired here: `cudaFree(0)` primes the CUDA primary
    // context on the current device, then
    // `optixDeviceContextCreate(0, ...)` inherits it (the standard
    // OptiX 7+ pattern). Returns true on success; on failure
    // populates `last_error()` and leaves the backend in its
    // pre-init state. Idempotent: calling on an already-initialised
    // backend is a no-op success.
    [[nodiscard]] bool initialize() noexcept;

    // Destroy the `OptixDeviceContext` (if any) and reset internal
    // state. Idempotent. The destructor invokes this automatically.
    void shutdown() noexcept;

    [[nodiscard]] bool               isInitialized() const noexcept;
    [[nodiscard]] void*              device_context() const noexcept;
    [[nodiscard]] const std::string& last_error()    const noexcept;

private:
    void*       context_     = nullptr;  // real type: OptixDeviceContext
    bool        initialized_ = false;
    std::string last_error_;
};

}  // namespace rr::optix

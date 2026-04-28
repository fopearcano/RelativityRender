#pragma once

#include <string>

// OptiX backend lifecycle + status surface.
//
// `rr_optix` is compiled into the build whether or not the OptiX SDK
// is reachable, so this header is host-only and exposes no OptiX
// types. The CMake option `RELATIVITYRENDER_ENABLE_OPTIX` controls
// whether the SDK is detected and `RR_HAS_OPTIX` is defined.
//
//   RR_HAS_OPTIX defined  -> SDK headers were on the include path
//                            at compile time. `init()` calls real
//                            `optixInit` / `optixDeviceContextCreate`.
//   RR_HAS_OPTIX undefined -> SDK was not built in. Every entry
//                            point is an honest no-op that returns
//                            failure and a clear diagnostic.
//
// Implementation work for the actual OptiX pipeline (program
// modules, AS / SBT, raygen / miss / closest-hit) lands in later
// M15 slices on top of this scaffold.

namespace rr::optix {

// True iff this build was compiled with
// `RELATIVITYRENDER_ENABLE_OPTIX=ON`. "Available" here means the SDK
// was reachable at compile time; it does **not** imply the runtime
// can initialise (no driver, version mismatch, etc.).
[[nodiscard]] bool optix_backend_available() noexcept;

// Short backend name. `"OptiX"` when compiled in, `"(none)"`
// otherwise.
[[nodiscard]] std::string optix_backend_name();

// One-line human-readable status, suitable for logging at startup.
// Probes the runtime when compiled in (briefly opens and closes a
// device context). Returns one of:
//   "OptiX backend: not compiled in (rebuild with RELATIVITYRENDER_ENABLE_OPTIX=ON)"
//   "OptiX backend: compiled in, runtime initialised OK"
//   "OptiX backend: compiled in, runtime init failed: <reason>"
[[nodiscard]] std::string optix_backend_status_line();

// Lifecycle wrapper around an OptiX device context. Construct ->
// `init()` -> use -> destruct (which calls `shutdown()` if needed).
// Move-only; every entry point is safe to call when the OptiX SDK
// is not compiled in (the OFF path is just no-ops).
class OptixBackend {
public:
    OptixBackend() = default;
    ~OptixBackend();

    OptixBackend(const OptixBackend&)            = delete;
    OptixBackend& operator=(const OptixBackend&) = delete;
    OptixBackend(OptixBackend&&) noexcept;
    OptixBackend& operator=(OptixBackend&&) noexcept;

    // Initialise the OptiX runtime + create a device context.
    // Returns false on any failure (SDK not compiled, no driver,
    // no CUDA context, version mismatch, ...). `last_error()`
    // carries a description.
    [[nodiscard]] bool init();

    // Tear down the device context. Safe on a never-initialised
    // backend and on a moved-from backend.
    void shutdown() noexcept;

    [[nodiscard]] bool                 is_initialized() const noexcept;
    [[nodiscard]] const std::string&   last_error()     const noexcept { return last_error_; }

private:
    void*       context_        = nullptr;  // OptixDeviceContext (opaque to callers)
    bool        is_initialized_ = false;
    std::string last_error_;
};

}

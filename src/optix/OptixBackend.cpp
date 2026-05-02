#include "optix/OptixBackend.h"

#include <cstdio>
#include <utility>

// Stage 17A.1: pull the SDK headers + CUDA runtime in only when
// the SDK was detected at configure time. The audit-host
// fallback (ENABLE_OPTIX=ON, SDK not found) compiles the stub
// branch below and `initialize()` returns false with a clear
// last_error() message.
#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <cuda_runtime.h>
    #include <optix.h>
    // Defines the global OptiX function-table symbol. Must appear
    // exactly once per program; this TU is the only consumer of
    // OptiX runtime entry points so it is the right home.
    #include <optix_function_table_definition.h>
    // Provides the implementation of optixInit() and the inline
    // wrappers that dispatch through g_optixFunctionTable.
    #include <optix_stubs.h>
#endif

namespace rr::optix {

// ---- static queries (always compiled) ------------------------------

bool OptixBackend::isCompiled() noexcept {
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
    return true;
#else
    return false;
#endif
}

bool OptixBackend::isSdkFound() noexcept {
#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    return true;
#else
    return false;
#endif
}

// ---- log callback (only when the SDK is available) -----------------

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

namespace {

// Forwarded as the `logCallbackFunction` to OptiX. OptiX passes the
// severity level (1=fatal, 2=error, 3=warning, 4=print) and a tag.
// We render to stderr so admins see the runtime's own diagnostics
// even when the higher-level CLI handler ignores them.
void log_callback(unsigned int level, const char* tag,
                  const char* message, void* /*cbdata*/) {
    const char* label = "INFO";
    switch (level) {
        case 1: label = "FATAL";   break;
        case 2: label = "ERROR";   break;
        case 3: label = "WARNING"; break;
        case 4: label = "PRINT";   break;
        default: break;
    }
    std::fprintf(stderr, "[OptiX:%s][%s] %s\n", label,
                 tag     ? tag     : "?",
                 message ? message : "");
}

}  // namespace

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

// ---- lifecycle (state members; bodies SDK-gated) -------------------

OptixBackend::OptixBackend() noexcept = default;

OptixBackend::~OptixBackend() {
    shutdown();
}

OptixBackend::OptixBackend(OptixBackend&& other) noexcept
    : context_(other.context_),
      initialized_(other.initialized_),
      last_error_(std::move(other.last_error_)) {
    other.context_     = nullptr;
    other.initialized_ = false;
}

OptixBackend& OptixBackend::operator=(OptixBackend&& other) noexcept {
    if (this != &other) {
        shutdown();
        context_     = other.context_;
        initialized_ = other.initialized_;
        last_error_  = std::move(other.last_error_);
        other.context_     = nullptr;
        other.initialized_ = false;
    }
    return *this;
}

bool OptixBackend::isInitialized() const noexcept {
    return initialized_;
}

void* OptixBackend::device_context() const noexcept {
    return context_;
}

const std::string& OptixBackend::last_error() const noexcept {
    return last_error_;
}

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

bool OptixBackend::initialize() noexcept {
    if (initialized_) {
        return true;
    }
    last_error_.clear();

    // CUDA <-> OptiX interop: ensure the CUDA primary context exists
    // on the current device. `cudaFree(0)` is the canonical idiom
    // for forcing CUDA-runtime initialisation; OptiX requires a
    // valid CUDA context before `optixDeviceContextCreate(0, ...)`.
    {
        const cudaError_t cerr = ::cudaFree(0);
        if (cerr != cudaSuccess) {
            last_error_  = "cudaFree(0) failed: ";
            last_error_ += ::cudaGetErrorString(cerr);
            std::fprintf(stderr,
                         "[OptiX:ERROR] init failed: %s\n",
                         last_error_.c_str());
            return false;
        }
    }

    // Initialise the OptiX function table. Safe to call repeatedly
    // (idempotent per the SDK docs); we still call it explicitly so
    // first-time startup has well-defined ordering.
    {
        const ::OptixResult r = ::optixInit();
        if (r != OPTIX_SUCCESS) {
            last_error_  = "optixInit() failed: ";
            last_error_ += ::optixGetErrorName(r);
            std::fprintf(stderr,
                         "[OptiX:ERROR] init failed: %s\n",
                         last_error_.c_str());
            return false;
        }
    }

    // Create the device context. Passing `0` for the `CUcontext`
    // argument tells OptiX to attach to the current CUDA primary
    // context (the one cudaFree(0) above ensured exists). This is
    // the standard CUDA <-> OptiX interop pattern documented in
    // the OptiX 7+ SDK samples.
    ::OptixDeviceContextOptions opts{};
    opts.logCallbackFunction = &log_callback;
    opts.logCallbackLevel    = 4;  // PRINT level (most detailed)
    ::OptixDeviceContext ctx = nullptr;
    {
        const ::OptixResult r =
            ::optixDeviceContextCreate(/*cuContext=*/0, &opts, &ctx);
        if (r != OPTIX_SUCCESS) {
            last_error_  = "optixDeviceContextCreate() failed: ";
            last_error_ += ::optixGetErrorName(r);
            std::fprintf(stderr,
                         "[OptiX:ERROR] init failed: %s\n",
                         last_error_.c_str());
            return false;
        }
    }

    context_     = ctx;
    initialized_ = true;
    std::fprintf(stderr,
                 "[OptiX:INFO] OptixDeviceContext created.\n");
    return true;
}

void OptixBackend::shutdown() noexcept {
    if (context_ != nullptr) {
        // Best-effort destroy. OptiX docs say
        // optixDeviceContextDestroy is no-op-safe on nullptr but
        // we already gate on the pointer here.
        ::optixDeviceContextDestroy(
            static_cast<::OptixDeviceContext>(context_));
        context_ = nullptr;
        std::fprintf(stderr,
                     "[OptiX:INFO] OptixDeviceContext destroyed.\n");
    }
    initialized_ = false;
}

#else   // RELATIVITYRENDER_OPTIX_SDK_FOUND

// Audit-host fallback: ENABLE_OPTIX is on but the SDK headers are
// not available. The class still compiles; `initialize()` reports
// the documented failure state; `shutdown()` is a no-op state
// reset.

bool OptixBackend::initialize() noexcept {
    last_error_ =
        "OptiX SDK not found at build time; rebuild with "
        "-DRELATIVITYRENDER_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so the OptiX context can "
        "be created. The CUDA path is unaffected.";
    std::fprintf(stderr,
                 "[OptiX:ERROR] init failed: %s\n",
                 last_error_.c_str());
    return false;
}

void OptixBackend::shutdown() noexcept {
    initialized_ = false;
    context_     = nullptr;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

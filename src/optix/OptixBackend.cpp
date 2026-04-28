#include "optix/OptixBackend.h"

#include <sstream>
#include <utility>

#ifdef RR_HAS_OPTIX
    // CUDA Runtime is needed for `cudaFree(nullptr)` to instantiate a
    // CUDA context that OptiX can sit on top of.
    #include <cuda_runtime.h>
    #include <optix.h>
    #include <optix_function_table_definition.h>
    #include <optix_stubs.h>
#endif

namespace rr::optix {

bool optix_backend_available() noexcept {
#ifdef RR_HAS_OPTIX
    return true;
#else
    return false;
#endif
}

std::string optix_backend_name() {
#ifdef RR_HAS_OPTIX
    return "OptiX";
#else
    return "(none)";
#endif
}

std::string optix_backend_status_line() {
#ifdef RR_HAS_OPTIX
    OptixBackend probe;
    if (probe.init()) {
        return "OptiX backend: compiled in, runtime initialised OK";
    }
    std::ostringstream os;
    os << "OptiX backend: compiled in, runtime init failed: "
       << probe.last_error();
    return os.str();
#else
    return "OptiX backend: not compiled in "
           "(rebuild with RELATIVITYRENDER_ENABLE_OPTIX=ON)";
#endif
}

OptixBackend::~OptixBackend() {
    shutdown();
}

OptixBackend::OptixBackend(OptixBackend&& other) noexcept
    : context_(other.context_),
      is_initialized_(other.is_initialized_),
      last_error_(std::move(other.last_error_)) {
    other.context_        = nullptr;
    other.is_initialized_ = false;
}

OptixBackend& OptixBackend::operator=(OptixBackend&& other) noexcept {
    if (this != &other) {
        shutdown();
        context_              = other.context_;
        is_initialized_       = other.is_initialized_;
        last_error_           = std::move(other.last_error_);
        other.context_        = nullptr;
        other.is_initialized_ = false;
    }
    return *this;
}

bool OptixBackend::init() {
#ifdef RR_HAS_OPTIX
    if (is_initialized_) return true;

    // Make sure a CUDA context exists in this thread; OptiX builds
    // its device context on top of one and refuses if there is no
    // current context.
    if (cudaFree(nullptr) != cudaSuccess) {
        last_error_ = "no CUDA context available "
                      "(no driver / no device / cudaFree failed)";
        return false;
    }

    if (optixInit() != OPTIX_SUCCESS) {
        last_error_ = "optixInit() failed "
                      "(driver does not provide OptiX, or version too old)";
        return false;
    }

    OptixDeviceContextOptions options = {};
    OptixDeviceContext        ctx     = nullptr;
    if (optixDeviceContextCreate(/*cu_ctx=*/nullptr, &options, &ctx)
            != OPTIX_SUCCESS) {
        last_error_ = "optixDeviceContextCreate() failed";
        return false;
    }

    context_        = ctx;
    is_initialized_ = true;
    last_error_.clear();
    return true;
#else
    last_error_ = "OptiX backend not compiled in";
    return false;
#endif
}

void OptixBackend::shutdown() noexcept {
#ifdef RR_HAS_OPTIX
    if (context_) {
        optixDeviceContextDestroy(static_cast<OptixDeviceContext>(context_));
        context_ = nullptr;
    }
#endif
    is_initialized_ = false;
}

bool OptixBackend::is_initialized() const noexcept {
    return is_initialized_;
}

}

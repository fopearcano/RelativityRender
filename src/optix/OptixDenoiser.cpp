#include "optix/OptixDenoiser.h"
#include "optix/OptixBackend.h"

#include <utility>

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <optix.h>
    #include <optix_stubs.h>

    #include <cstdio>
    #include <string>
#endif

namespace rr::optix {

// ---- always-compiled members --------------------------------------

OptixDenoiser::~OptixDenoiser() {
    shutdown();
}

OptixDenoiser::OptixDenoiser(OptixDenoiser&& other) noexcept
    : denoiser_(other.denoiser_),
      initialized_(other.initialized_),
      last_error_(std::move(other.last_error_)) {
    other.denoiser_    = nullptr;
    other.initialized_ = false;
}

OptixDenoiser& OptixDenoiser::operator=(OptixDenoiser&& other) noexcept {
    if (this != &other) {
        shutdown();
        denoiser_           = other.denoiser_;
        initialized_        = other.initialized_;
        last_error_         = std::move(other.last_error_);
        other.denoiser_     = nullptr;
        other.initialized_  = false;
    }
    return *this;
}

bool OptixDenoiser::is_initialized() const noexcept {
    return initialized_;
}

void* OptixDenoiser::denoiser_handle() const noexcept {
    return denoiser_;
}

const std::string& OptixDenoiser::last_error() const noexcept {
    return last_error_;
}

// ---- initialize / shutdown (SDK-gated bodies) ---------------------

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

bool OptixDenoiser::initialize(OptixBackend& backend) noexcept {
    if (initialized_) {
        return true;
    }
    last_error_.clear();

    if (!backend.isInitialized()) {
        last_error_ =
            "OptixDenoiser::initialize: backend is not initialized; "
            "call OptixBackend::initialize() first.";
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser init failed: %s\n",
                     last_error_.c_str());
        return false;
    }

    auto* ctx = static_cast<::OptixDeviceContext>(backend.device_context());
    if (ctx == nullptr) {
        last_error_ =
            "OptixDenoiser::initialize: OptixDeviceContext is null even "
            "though backend.isInitialized() is true; internal error.";
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser init failed: %s\n",
                     last_error_.c_str());
        return false;
    }

    // Stage 19B.1 denoiser options. Pinned per the Stage 19A.2
    // input contract:
    //
    // - guideAlbedo  = 1: Albedo guide layer required (DENOISER
    //   _PLAN §8.1.2). The Stage 14A AOV pipeline already
    //   produces a Beauty-aligned Albedo buffer that 19B+
    //   sub-stages will hand to optixDenoiserInvoke.
    //
    // - guideNormal  = 1: Normal guide layer required (DENOISER
    //   _PLAN §8.1.3). Same source as Albedo; world-space XYZ,
    //   matching the OptiX denoiser's expected layout.
    //
    // - denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY: the
    //   project's Beauty pass alpha is always 1 (per
    //   Image::save_ppm's existing behaviour); there is no
    //   alpha noise to remove. COPY is the OptiX-recommended
    //   choice when alpha is a constant pass-through.
    ::OptixDenoiserOptions opts{};
    opts.guideAlbedo  = 1u;
    opts.guideNormal  = 1u;
    opts.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY;

    // Model kind: HDR. The path tracer produces unbounded
    // radiance values (DENOISER_PLAN §8.1.1); LDR would clip
    // values >1 to white. AOV mode is reserved for explicit
    // per-AOV denoising (future); the temporal models
    // (TEMPORAL / TEMPORAL_AOV) require a motion-vector AOV
    // that the renderer does not produce yet (DENOISER_PLAN
    // §8.2.2).
    constexpr ::OptixDenoiserModelKind kModel =
        OPTIX_DENOISER_MODEL_KIND_HDR;

    ::OptixDenoiser denoiser = nullptr;
    {
        const ::OptixResult res = ::optixDenoiserCreate(
            ctx, kModel, &opts, &denoiser);
        if (res != OPTIX_SUCCESS) {
            last_error_ = std::string("optixDenoiserCreate failed: ")
                        + ::optixGetErrorName(res);
            std::fprintf(stderr,
                         "[OptiX:ERROR] denoiser init failed: %s\n",
                         last_error_.c_str());
            return false;
        }
    }

    denoiser_    = denoiser;
    initialized_ = true;
    std::fprintf(stderr,
                 "[OptiX:INFO] OptixDenoiser created "
                 "(HDR model, guideAlbedo=1, guideNormal=1, "
                 "denoiseAlpha=COPY).\n");
    return true;
}

void OptixDenoiser::shutdown() noexcept {
    if (denoiser_ != nullptr) {
        // Best-effort destroy. The SDK docs say
        // optixDenoiserDestroy is safe on a non-null handle;
        // we guard on the pointer here.
        ::optixDenoiserDestroy(static_cast<::OptixDenoiser>(denoiser_));
        denoiser_ = nullptr;
        std::fprintf(stderr,
                     "[OptiX:INFO] OptixDenoiser destroyed.\n");
    }
    initialized_ = false;
}

#else   // RELATIVITYRENDER_OPTIX_SDK_FOUND

// Audit-host fallback. ENABLE_OPTIX is on but the SDK headers
// are not available; the class compiles + links cleanly,
// `initialize()` reports the documented failure state, and
// `shutdown()` is a no-op state reset.

bool OptixDenoiser::initialize(OptixBackend& /*backend*/) noexcept {
    last_error_ =
        "OptixDenoiser::initialize requires the OptiX SDK; "
        "rebuild with -DRELATIVITYRENDER_ENABLE_OPTIX=ON and "
        "pass -DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return false;
}

void OptixDenoiser::shutdown() noexcept {
    initialized_ = false;
    denoiser_    = nullptr;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

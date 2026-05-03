#include "optix/OptixDenoiser.h"
#include "optix/OptixBackend.h"

#include <utility>

// Stage 21B.1 / 21B.2 / 21B.3 - minimal class shell with
// explicit compile guards.
//
// Two-layer macro contract (established project-wide by
// Stage 12B.4 + Stage 17A.1's rr_optix audit-host
// fallback):
//
// - `RELATIVITYRENDER_ENABLE_OPTIX` is defined whenever
//   `-DRR_ENABLE_OPTIX=ON` was passed at configure time;
//   it gates the "class is active" vs "OptiX disabled at
//   build time" method bodies (Stage 21B.2).
// - `RELATIVITYRENDER_OPTIX_SDK_FOUND` is defined ONLY
//   when CMake additionally located <optix.h> at configure
//   time; it gates the actual SDK header includes
//   (Stage 21B.3) and, in subsequent Stage 21B sub-stages,
//   the real SDK function calls. The audit-host fallback
//   (ENABLE on, SDK_FOUND off) compiles cleanly without
//   <optix.h> and reports "not implemented" / "SDK not
//   found" via `last_error()`.
//
// Stage 21B.3 only adds the SDK header includes inside the
// SDK_FOUND gate; no SDK function is actually called yet.
// Subsequent sub-stages add the real wiring per the Stage
// 21A plan.
//
// Trivial members (constructor, destructor, move ops,
// getters, `shutdown`) are unconditional - they do not
// depend on OptiX in any way.

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    // SDK headers pulled in only when CMake located
    // <optix.h> at configure time. The OFF build never
    // sees these (rr_optix is not built); the ON build
    // sees them only when a real OptiX SDK is present.
    // Stage 21B.3 imports them so subsequent sub-stages
    // can call SDK functions without further include
    // changes.
    #include <optix.h>
    #include <optix_stubs.h>
#endif

namespace rr::optix {

OptixDenoiser::~OptixDenoiser() {
    shutdown();
}

OptixDenoiser::OptixDenoiser(OptixDenoiser&& other) noexcept
    : denoiser_(other.denoiser_),
      initialized_(other.initialized_),
      input_images_(other.input_images_),
      inputs_set_(other.inputs_set_),
      input_width_(other.input_width_),
      input_height_(other.input_height_),
      input_beauty_components_(other.input_beauty_components_),
      last_error_(std::move(other.last_error_)) {
    other.denoiser_                = nullptr;
    other.initialized_             = false;
    other.input_images_            = nullptr;
    other.inputs_set_              = false;
    other.input_width_             = 0;
    other.input_height_            = 0;
    other.input_beauty_components_ = 0;
}

OptixDenoiser& OptixDenoiser::operator=(OptixDenoiser&& other) noexcept {
    if (this != &other) {
        shutdown();
        denoiser_                = other.denoiser_;
        initialized_             = other.initialized_;
        input_images_            = other.input_images_;
        inputs_set_              = other.inputs_set_;
        input_width_             = other.input_width_;
        input_height_            = other.input_height_;
        input_beauty_components_ = other.input_beauty_components_;
        last_error_              = std::move(other.last_error_);
        other.denoiser_                = nullptr;
        other.initialized_             = false;
        other.input_images_            = nullptr;
        other.inputs_set_              = false;
        other.input_width_             = 0;
        other.input_height_            = 0;
        other.input_beauty_components_ = 0;
    }
    return *this;
}

bool OptixDenoiser::is_initialized() const noexcept { return initialized_; }
bool OptixDenoiser::inputs_set()     const noexcept { return inputs_set_; }
int  OptixDenoiser::input_width()    const noexcept { return input_width_; }
int  OptixDenoiser::input_height()   const noexcept { return input_height_; }
void* OptixDenoiser::denoiser_handle() const noexcept { return denoiser_; }
const std::string& OptixDenoiser::last_error() const noexcept { return last_error_; }

void OptixDenoiser::shutdown() noexcept {
#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    // Stage 21B.4: paired with `optixDenoiserCreate` in
    // `initialize()`. Best-effort destroy on a non-null
    // handle; the SDK guarantees this is safe. The audit-
    // host fallback (no SDK) never produces a non-null
    // handle so this block has no effect there.
    if (denoiser_ != nullptr) {
        ::optixDenoiserDestroy(static_cast<::OptixDenoiser>(denoiser_));
    }
#endif
    initialized_             = false;
    denoiser_                = nullptr;
    input_images_            = nullptr;
    inputs_set_              = false;
    input_width_             = 0;
    input_height_            = 0;
    input_beauty_components_ = 0;
}

#ifdef RELATIVITYRENDER_ENABLE_OPTIX

// ---- ON branch: prepared for OptiX usage --------------------------
// Stage 21B.4 wires the first real SDK call: `initialize()`
// creates the underlying OptixDenoiser handle and stores it
// in `denoiser_`. Subsequent Stage 21B sub-stages add
// memory-resource queries (`optixDenoiserComputeMemoryResources`),
// per-resolution setup (`optixDenoiserSetup`), input
// binding, and the actual `optixDenoiserInvoke` per the
// Stage 21A plan.

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

bool OptixDenoiser::initialize(OptixBackend& backend) noexcept {
    if (initialized_) {
        return true;  // idempotent
    }
    last_error_.clear();

    if (!backend.isInitialized()) {
        last_error_ =
            "OptixDenoiser::initialize: backend is not initialized; "
            "call OptixBackend::initialize() first.";
        return false;
    }

    auto* ctx = static_cast<::OptixDeviceContext>(backend.device_context());
    if (ctx == nullptr) {
        last_error_ =
            "OptixDenoiser::initialize: OptixDeviceContext is null even "
            "though backend.isInitialized() is true; internal error.";
        return false;
    }

    // Stage 21A.2 / 21A.3 contract: HDR model, guideAlbedo +
    // guideNormal pinned. Beauty alpha pass-through (COPY)
    // because the project's Beauty AOV alpha is always 1.
    ::OptixDenoiserOptions opts{};
    opts.guideAlbedo  = 1u;
    opts.guideNormal  = 1u;
    opts.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY;

    constexpr ::OptixDenoiserModelKind kModel =
        OPTIX_DENOISER_MODEL_KIND_HDR;

    ::OptixDenoiser denoiser = nullptr;
    const ::OptixResult res = ::optixDenoiserCreate(
        ctx, kModel, &opts, &denoiser);
    if (res != OPTIX_SUCCESS) {
        last_error_ = std::string("optixDenoiserCreate failed: ")
                    + ::optixGetErrorName(res);
        return false;
    }

    denoiser_    = denoiser;
    initialized_ = true;
    return true;
}

#else  // RELATIVITYRENDER_OPTIX_SDK_FOUND

// Audit-host fallback: ENABLE_OPTIX is on but the SDK was
// not located at configure time. The `optixDenoiserCreate`
// function pointer is not available; report the documented
// error.
bool OptixDenoiser::initialize(OptixBackend& /*backend*/) noexcept {
    last_error_ =
        "OptixDenoiser::initialize requires the OptiX SDK; "
        "rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return false;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

bool OptixDenoiser::set_inputs(const Inputs& /*inputs*/) noexcept {
    last_error_ =
        "OptixDenoiser::set_inputs: not implemented in Stage 21B.1.";
    return false;
}

bool OptixDenoiser::invoke(const Output& /*output*/) noexcept {
    last_error_ =
        "OptixDenoiser::invoke: not implemented in Stage 21B.1.";
    return false;
}

#else  // RELATIVITYRENDER_ENABLE_OPTIX

// ---- OFF branch: OptiX disabled at build time ---------------------
// rr_optix is not built when `RR_ENABLE_OPTIX=OFF` per the
// Stage 12B.3 contract, so this branch is never reached in
// the default OFF build. It exists so the file is compilable
// in either mode (master rule 2: keep every step compilable).

bool OptixDenoiser::initialize(OptixBackend& /*backend*/) noexcept {
    last_error_ =
        "OptixDenoiser::initialize: OptiX disabled at build time. "
        "Rebuild with -DRR_ENABLE_OPTIX=ON to enable the denoiser.";
    return false;
}

bool OptixDenoiser::set_inputs(const Inputs& /*inputs*/) noexcept {
    last_error_ =
        "OptixDenoiser::set_inputs: OptiX disabled at build time. "
        "Rebuild with -DRR_ENABLE_OPTIX=ON to enable the denoiser.";
    return false;
}

bool OptixDenoiser::invoke(const Output& /*output*/) noexcept {
    last_error_ =
        "OptixDenoiser::invoke: OptiX disabled at build time. "
        "Rebuild with -DRR_ENABLE_OPTIX=ON to enable the denoiser.";
    return false;
}

#endif  // RELATIVITYRENDER_ENABLE_OPTIX

}  // namespace rr::optix

#include "optix/OptixDenoiser.h"
#include "optix/OptixBackend.h"

#include <utility>

// Stage 21B.1 / 21B.2 - minimal class shell with explicit
// `RELATIVITYRENDER_ENABLE_OPTIX` compile guards. No
// `<optix.h>`, no SDK calls, no functionality. Every method
// that will eventually wrap a real OptiX call gets two
// branches:
//
// - `#ifdef RELATIVITYRENDER_ENABLE_OPTIX` -> the method is
//   "prepared for OptiX usage": Stage 21B.1 stub that
//   reports "not implemented in Stage 21B.1" and returns
//   `false`. Subsequent Stage 21B sub-stages replace each
//   stub with the corresponding SDK call per the Stage 21A
//   plan.
// - `#else` -> the OFF branch: the method reports "OptiX
//   disabled at build time" and returns `false`. Provided
//   so the file is compilable in either mode (rr_optix is
//   only built when ON, but the source is mode-agnostic per
//   master rule 2).
//
// Trivial members (constructor, destructor, move ops,
// getters, `shutdown`) are unconditional - they do not
// depend on OptiX in any way.

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
// Subsequent Stage 21B sub-stages replace each stub below
// with the corresponding OptiX SDK call per the Stage 21A
// plan.

bool OptixDenoiser::initialize(OptixBackend& /*backend*/) noexcept {
    last_error_ =
        "OptixDenoiser::initialize: not implemented in Stage 21B.1 "
        "(class skeleton only; OptiX SDK wiring lands in subsequent "
        "Stage 21B sub-stages).";
    return false;
}

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

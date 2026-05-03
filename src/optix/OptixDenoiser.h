#pragma once

#include <string>

// Stage 21B.1 - OptixDenoiser wrapper class skeleton.
//
// This file is intentionally minimal: a class shell whose
// methods all return false / no-op with a documented "not
// implemented" message. No <optix.h> include, no SDK calls,
// no functionality. Subsequent Stage 21B sub-stages add the
// real OptiX wiring per the Stage 21A planning arc.
//
// Compiles with -DRR_ENABLE_OPTIX=OFF (the file is simply
// not built; rr_optix is disabled per the Stage 12B.3
// contract) and with -DRR_ENABLE_OPTIX=ON (this header is
// SDK-free; the .cpp compiles cleanly without the OptiX
// SDK and the audit-host fallback path is the only path).

namespace rr::optix {

class OptixBackend;

class OptixDenoiser {
public:
    struct Inputs {
        const float* beauty_device     = nullptr;
        int          beauty_components = 3;
        const float* albedo_device     = nullptr;
        const float* normal_device     = nullptr;
        int          width             = 0;
        int          height            = 0;
    };

    struct Output {
        float* device = nullptr;
        int    width  = 0;
        int    height = 0;
    };

    OptixDenoiser() noexcept = default;
    ~OptixDenoiser();

    OptixDenoiser(const OptixDenoiser&)            = delete;
    OptixDenoiser& operator=(const OptixDenoiser&) = delete;
    OptixDenoiser(OptixDenoiser&&) noexcept;
    OptixDenoiser& operator=(OptixDenoiser&&) noexcept;

    [[nodiscard]] bool initialize(OptixBackend& backend) noexcept;
    [[nodiscard]] bool set_inputs(const Inputs& inputs) noexcept;
    [[nodiscard]] bool invoke(const Output& output)     noexcept;
    void               shutdown() noexcept;

    [[nodiscard]] bool               is_initialized() const noexcept;
    [[nodiscard]] bool               inputs_set()     const noexcept;
    [[nodiscard]] int                input_width()    const noexcept;
    [[nodiscard]] int                input_height()   const noexcept;
    [[nodiscard]] void*              denoiser_handle() const noexcept;
    [[nodiscard]] const std::string& last_error()      const noexcept;

private:
    void*       denoiser_                = nullptr;
    bool        initialized_             = false;
    void*       input_images_            = nullptr;
    bool        inputs_set_              = false;
    int         input_width_             = 0;
    int         input_height_            = 0;
    int         input_beauty_components_ = 0;
    std::string last_error_;
};

}  // namespace rr::optix

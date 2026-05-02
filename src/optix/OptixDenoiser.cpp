#include "optix/OptixDenoiser.h"
#include "optix/OptixBackend.h"

#include <utility>

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <optix.h>
    #include <optix_stubs.h>

    #include <cstdio>
    #include <new>      // std::nothrow
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
      input_images_(other.input_images_),
      inputs_set_(other.inputs_set_),
      input_width_(other.input_width_),
      input_height_(other.input_height_),
      last_error_(std::move(other.last_error_)) {
    other.denoiser_     = nullptr;
    other.initialized_  = false;
    other.input_images_ = nullptr;
    other.inputs_set_   = false;
    other.input_width_  = 0;
    other.input_height_ = 0;
}

OptixDenoiser& OptixDenoiser::operator=(OptixDenoiser&& other) noexcept {
    if (this != &other) {
        shutdown();
        denoiser_           = other.denoiser_;
        initialized_        = other.initialized_;
        input_images_       = other.input_images_;
        inputs_set_         = other.inputs_set_;
        input_width_        = other.input_width_;
        input_height_       = other.input_height_;
        last_error_         = std::move(other.last_error_);
        other.denoiser_     = nullptr;
        other.initialized_  = false;
        other.input_images_ = nullptr;
        other.inputs_set_   = false;
        other.input_width_  = 0;
        other.input_height_ = 0;
    }
    return *this;
}

bool OptixDenoiser::is_initialized() const noexcept {
    return initialized_;
}

void* OptixDenoiser::denoiser_handle() const noexcept {
    return denoiser_;
}

bool OptixDenoiser::inputs_set() const noexcept {
    return inputs_set_;
}

int OptixDenoiser::input_width() const noexcept {
    return input_width_;
}

int OptixDenoiser::input_height() const noexcept {
    return input_height_;
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

// Stage 19B.2: bind the renderer's AOV buffers to the
// denoiser's input slots. Builds the three `::OptixImage2D`
// descriptors (Beauty FLOAT4, Albedo FLOAT3, Normal FLOAT3)
// from the raw device pointers + dimensions and stashes the
// triplet in private state for the next sub-stage's
// `optixDenoiserInvoke`. No CUDA / OptiX work is launched
// here; the conversion is a host-side pointer + metadata
// pack.
bool OptixDenoiser::set_inputs(const Inputs& inputs) noexcept {
    last_error_.clear();

    // Validation. All three device pointers must be non-null
    // and the framebuffer dims must be positive. The
    // descriptor sizes (vertical / horizontal) are uniform
    // across Beauty + guide layers per the OptiX denoiser
    // contract.
    if (inputs.beauty_device == nullptr) {
        last_error_ =
            "OptixDenoiser::set_inputs: beauty_device is null";
        return false;
    }
    if (inputs.albedo_device == nullptr) {
        last_error_ =
            "OptixDenoiser::set_inputs: albedo_device is null";
        return false;
    }
    if (inputs.normal_device == nullptr) {
        last_error_ =
            "OptixDenoiser::set_inputs: normal_device is null";
        return false;
    }
    if (inputs.width <= 0 || inputs.height <= 0) {
        last_error_ =
            "OptixDenoiser::set_inputs: invalid dimensions "
            "(width and height must be > 0)";
        return false;
    }

    // Drop any prior descriptor allocation so re-binding is
    // safe. The `delete[]` handles a null left-over fine.
    if (input_images_ != nullptr) {
        delete[] static_cast<::OptixImage2D*>(input_images_);
        input_images_ = nullptr;
    }
    inputs_set_   = false;
    input_width_  = 0;
    input_height_ = 0;

    // Build the OptixImage2D[3] triplet on the host. Slot
    // order [Beauty, Albedo, Normal] is the canonical 19B
    // ordering; the next sub-stage's invoke wires slot 0 ->
    // OptixDenoiserLayer.input and slots 1/2 ->
    // OptixDenoiserGuideLayer.albedo / .normal.
    const auto w = static_cast<unsigned int>(inputs.width);
    const auto h = static_cast<unsigned int>(inputs.height);
    constexpr unsigned int kFloat4Bytes = 4u * sizeof(float);
    constexpr unsigned int kFloat3Bytes = 3u * sizeof(float);

    auto* images = new (std::nothrow) ::OptixImage2D[3];
    if (images == nullptr) {
        last_error_ =
            "OptixDenoiser::set_inputs: host allocation for "
            "OptixImage2D[3] failed";
        return false;
    }

    // Slot 0: Beauty (Rgba32F, 4 floats / pixel; the path
    // tracer's resolve buffer per DENOISER_PLAN §8.3.1
    // route B). Format FLOAT4 so alpha passes through under
    // OPTIX_DENOISER_ALPHA_MODE_COPY.
    images[0].data = reinterpret_cast<::CUdeviceptr>(
        const_cast<float*>(inputs.beauty_device));
    images[0].width              = w;
    images[0].height             = h;
    images[0].rowStrideInBytes   = w * kFloat4Bytes;
    images[0].pixelStrideInBytes = kFloat4Bytes;
    images[0].format             = OPTIX_PIXEL_FORMAT_FLOAT4;

    // Slot 1: Albedo (linear-space RGB, 3 floats / pixel;
    // Stage 14A AOV layout). Format FLOAT3.
    images[1].data = reinterpret_cast<::CUdeviceptr>(
        const_cast<float*>(inputs.albedo_device));
    images[1].width              = w;
    images[1].height             = h;
    images[1].rowStrideInBytes   = w * kFloat3Bytes;
    images[1].pixelStrideInBytes = kFloat3Bytes;
    images[1].format             = OPTIX_PIXEL_FORMAT_FLOAT3;

    // Slot 2: Normal (world-space XYZ, 3 floats / pixel;
    // Stage 14A AOV layout). Format FLOAT3. The OptiX
    // denoiser explicitly documents normal input as
    // world-space, components in [-1, 1] - the project's
    // `Hit::normal` already satisfies that.
    images[2].data = reinterpret_cast<::CUdeviceptr>(
        const_cast<float*>(inputs.normal_device));
    images[2].width              = w;
    images[2].height             = h;
    images[2].rowStrideInBytes   = w * kFloat3Bytes;
    images[2].pixelStrideInBytes = kFloat3Bytes;
    images[2].format             = OPTIX_PIXEL_FORMAT_FLOAT3;

    input_images_ = images;
    inputs_set_   = true;
    input_width_  = inputs.width;
    input_height_ = inputs.height;

    std::fprintf(stderr,
                 "[OptiX:INFO] OptixDenoiser inputs bound: "
                 "Beauty %dx%d FLOAT4, Albedo %dx%d FLOAT3, "
                 "Normal %dx%d FLOAT3.\n",
                 inputs.width, inputs.height,
                 inputs.width, inputs.height,
                 inputs.width, inputs.height);
    return true;
}

void OptixDenoiser::shutdown() noexcept {
    if (input_images_ != nullptr) {
        // Free the host-side descriptor array. The DEVICE
        // pointers it referenced are NOT owned here (the
        // caller's GpuAOVBuffer / resolve buffer is the
        // owner).
        delete[] static_cast<::OptixImage2D*>(input_images_);
        input_images_ = nullptr;
    }
    inputs_set_   = false;
    input_width_  = 0;
    input_height_ = 0;

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

bool OptixDenoiser::set_inputs(const Inputs& /*inputs*/) noexcept {
    // Stage 19B.2 audit-host fallback. Same posture as
    // initialize(): the input descriptors require <optix.h>'s
    // OptixImage2D struct, which is not available without
    // the SDK. Document the failure and leave inputs unset.
    last_error_ =
        "OptixDenoiser::set_inputs requires the OptiX SDK; "
        "rebuild with -DRELATIVITYRENDER_ENABLE_OPTIX=ON and "
        "pass -DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return false;
}

void OptixDenoiser::shutdown() noexcept {
    // Audit-host fallback never produces a populated state,
    // so shutdown has nothing to free. All pointers stay
    // null.
    initialized_  = false;
    denoiser_     = nullptr;
    input_images_ = nullptr;
    inputs_set_   = false;
    input_width_  = 0;
    input_height_ = 0;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

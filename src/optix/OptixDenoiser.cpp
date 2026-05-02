#include "optix/OptixDenoiser.h"
#include "optix/OptixBackend.h"

#include <utility>

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <cuda_runtime.h>
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
    if (inputs.beauty_components != 3 && inputs.beauty_components != 4) {
        last_error_ =
            "OptixDenoiser::set_inputs: beauty_components must be "
            "3 (FLOAT3, AOV-pipeline default) or 4 (FLOAT4, "
            "path-tracer resolve)";
        return false;
    }

    // Drop any prior descriptor allocation so re-binding is
    // safe. The `delete[]` handles a null left-over fine.
    if (input_images_ != nullptr) {
        delete[] static_cast<::OptixImage2D*>(input_images_);
        input_images_ = nullptr;
    }
    inputs_set_       = false;
    input_width_      = 0;
    input_height_     = 0;
    input_beauty_components_ = 0;

    // Build the OptixImage2D[3] triplet on the host. Slot
    // order [Beauty, Albedo, Normal] is the canonical 19B
    // ordering; `invoke()` wires slot 0 ->
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

    // Slot 0: Beauty. FLOAT3 (3 floats / pixel; the AOV
    // pipeline's Beauty buffer per DENOISER_PLAN §8.3.1
    // route A) by default; FLOAT4 (the path-tracer's
    // resolve output, route B) when beauty_components == 4.
    // Under FLOAT4 alpha passes through unchanged per
    // OPTIX_DENOISER_ALPHA_MODE_COPY (set in 19B.1
    // OptixDenoiserOptions).
    const unsigned int beauty_pixel_bytes =
        (inputs.beauty_components == 4) ? kFloat4Bytes : kFloat3Bytes;
    const ::OptixPixelFormat beauty_format =
        (inputs.beauty_components == 4)
            ? OPTIX_PIXEL_FORMAT_FLOAT4
            : OPTIX_PIXEL_FORMAT_FLOAT3;

    images[0].data = reinterpret_cast<::CUdeviceptr>(
        const_cast<float*>(inputs.beauty_device));
    images[0].width              = w;
    images[0].height             = h;
    images[0].rowStrideInBytes   = w * beauty_pixel_bytes;
    images[0].pixelStrideInBytes = beauty_pixel_bytes;
    images[0].format             = beauty_format;

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

    input_images_            = images;
    inputs_set_              = true;
    input_width_             = inputs.width;
    input_height_            = inputs.height;
    input_beauty_components_ = inputs.beauty_components;

    std::fprintf(stderr,
                 "[OptiX:INFO] OptixDenoiser inputs bound: "
                 "Beauty %dx%d FLOAT%d, Albedo %dx%d FLOAT3, "
                 "Normal %dx%d FLOAT3.\n",
                 inputs.width, inputs.height, inputs.beauty_components,
                 inputs.width, inputs.height,
                 inputs.width, inputs.height);
    return true;
}

// Stage 19B.3: run the denoiser end-to-end against the
// bound inputs and the caller-supplied output buffer.
// Sequence: optixDenoiserComputeMemoryResources ->
// cudaMalloc state + scratch -> optixDenoiserSetup ->
// optixDenoiserInvoke -> cudaDeviceSynchronize -> cudaFree
// state + scratch. The state + scratch buffers are
// function-scope; they are allocated fresh on every call
// and freed before return so the denoiser's host-side
// footprint stays at the OptixDenoiser handle plus the
// OptixImage2D[3] descriptor array. A future slice may
// cache them across invocations when dimensions are
// stable.
bool OptixDenoiser::invoke(const Output& output) noexcept {
    last_error_.clear();

    // Pre-condition checks. Mismatch is documented in the
    // header doc-comment.
    if (!initialized_) {
        last_error_ =
            "OptixDenoiser::invoke: denoiser is not initialised; "
            "call initialize(backend) first.";
        return false;
    }
    if (!inputs_set_) {
        last_error_ =
            "OptixDenoiser::invoke: inputs are not bound; "
            "call set_inputs(...) first.";
        return false;
    }
    if (output.device == nullptr) {
        last_error_ =
            "OptixDenoiser::invoke: output.device is null";
        return false;
    }
    if (output.width <= 0 || output.height <= 0) {
        last_error_ =
            "OptixDenoiser::invoke: invalid output dimensions "
            "(width and height must be > 0)";
        return false;
    }
    if (output.width != input_width_ || output.height != input_height_) {
        last_error_ =
            "OptixDenoiser::invoke: output dimensions "
            "must match the bound input dimensions";
        return false;
    }

    auto* denoiser = static_cast<::OptixDenoiser>(denoiser_);
    auto* images   = static_cast<::OptixImage2D*>(input_images_);
    const auto w   = static_cast<unsigned int>(input_width_);
    const auto h   = static_cast<unsigned int>(input_height_);

    // 1. Compute memory resources for the bound dims.
    ::OptixDenoiserSizes sizes{};
    {
        const ::OptixResult res =
            ::optixDenoiserComputeMemoryResources(denoiser, w, h, &sizes);
        if (res != OPTIX_SUCCESS) {
            last_error_ =
                std::string("optixDenoiserComputeMemoryResources failed: ")
              + ::optixGetErrorName(res);
            return false;
        }
    }

    // 2. Allocate state + scratch device buffers.
    void* d_state   = nullptr;
    void* d_scratch = nullptr;
    {
        const ::cudaError_t e = ::cudaMalloc(&d_state, sizes.stateSizeInBytes);
        if (e != cudaSuccess) {
            last_error_ =
                std::string("cudaMalloc(denoiser state) failed: ")
              + ::cudaGetErrorString(e);
            return false;
        }
    }
    // OptiX reports two scratch sizes: one for the no-overlap
    // (single-tile) fast path, one for the larger overlap
    // path used in tiled invocations. 19B.3 invokes the whole
    // framebuffer as one tile, so the no-overlap size is the
    // correct allocation.
    {
        const ::cudaError_t e =
            ::cudaMalloc(&d_scratch, sizes.withoutOverlapScratchSizeInBytes);
        if (e != cudaSuccess) {
            ::cudaFree(d_state);
            last_error_ =
                std::string("cudaMalloc(denoiser scratch) failed: ")
              + ::cudaGetErrorString(e);
            return false;
        }
    }

    // 3. Setup the per-resolution state.
    {
        const ::OptixResult res = ::optixDenoiserSetup(
            denoiser,
            /*stream=*/0,
            w, h,
            reinterpret_cast<::CUdeviceptr>(d_state),
            sizes.stateSizeInBytes,
            reinterpret_cast<::CUdeviceptr>(d_scratch),
            sizes.withoutOverlapScratchSizeInBytes);
        if (res != OPTIX_SUCCESS) {
            ::cudaFree(d_scratch);
            ::cudaFree(d_state);
            last_error_ =
                std::string("optixDenoiserSetup failed: ")
              + ::optixGetErrorName(res);
            return false;
        }
    }

    // 4. Build the per-launch params + guide layer + render
    //    layer. denoiseAlpha lives in the OptixDenoiserOptions
    //    pinned at create time (19B.1); the params struct's
    //    field of the same name is left zero-initialised
    //    (== OPTIX_DENOISER_ALPHA_MODE_COPY, matching the
    //    options).
    ::OptixDenoiserParams params{};
    params.blendFactor = 0.0f;  // 0 = full denoise; 1 = passthrough

    // Slot 0 = Beauty (the input layer); slots 1 / 2 are
    // the guide layers (Albedo / Normal) per DENOISER_PLAN
    // §8.3.
    ::OptixDenoiserGuideLayer guide{};
    guide.albedo = images[1];
    guide.normal = images[2];
    // flow / previousOutputInternalGuideLayer /
    // outputInternalGuideLayer left default-zero - temporal
    // mode is a future slice (DENOISER_PLAN §2.2).

    // Output layer descriptor. Format matches the bound
    // Beauty input's beauty_components (FLOAT3 -> FLOAT3,
    // FLOAT4 -> FLOAT4) per the header's Output doc-comment.
    constexpr unsigned int kFloat4Bytes = 4u * sizeof(float);
    constexpr unsigned int kFloat3Bytes = 3u * sizeof(float);
    const unsigned int output_pixel_bytes =
        (input_beauty_components_ == 4) ? kFloat4Bytes : kFloat3Bytes;
    const ::OptixPixelFormat output_format =
        (input_beauty_components_ == 4)
            ? OPTIX_PIXEL_FORMAT_FLOAT4
            : OPTIX_PIXEL_FORMAT_FLOAT3;

    ::OptixDenoiserLayer layer{};
    layer.input  = images[0];  // Beauty (noisy)
    layer.output.data = reinterpret_cast<::CUdeviceptr>(output.device);
    layer.output.width              = w;
    layer.output.height             = h;
    layer.output.rowStrideInBytes   = w * output_pixel_bytes;
    layer.output.pixelStrideInBytes = output_pixel_bytes;
    layer.output.format             = output_format;
    // previousOutput left default - no temporal accumulation.

    // 5. Invoke the denoiser.
    {
        const ::OptixResult res = ::optixDenoiserInvoke(
            denoiser,
            /*stream=*/0,
            &params,
            reinterpret_cast<::CUdeviceptr>(d_state),
            sizes.stateSizeInBytes,
            &guide,
            &layer,
            /*numLayers=*/1u,
            /*inputOffsetX=*/0u,
            /*inputOffsetY=*/0u,
            reinterpret_cast<::CUdeviceptr>(d_scratch),
            sizes.withoutOverlapScratchSizeInBytes);
        if (res != OPTIX_SUCCESS) {
            ::cudaFree(d_scratch);
            ::cudaFree(d_state);
            last_error_ =
                std::string("optixDenoiserInvoke failed: ")
              + ::optixGetErrorName(res);
            return false;
        }
    }

    // 6. Synchronise so the host knows the output buffer is
    //    fully written before the caller proceeds to download
    //    / save.
    if (::cudaDeviceSynchronize() != cudaSuccess) {
        ::cudaFree(d_scratch);
        ::cudaFree(d_state);
        last_error_ =
            "OptixDenoiser::invoke: cudaDeviceSynchronize failed";
        return false;
    }

    // 7. Free the state + scratch buffers. The output buffer
    //    is the caller's; we do not free it.
    ::cudaFree(d_scratch);
    ::cudaFree(d_state);

    std::fprintf(stderr,
                 "[OptiX:INFO] OptixDenoiser invoked: "
                 "%ux%u, output FLOAT%d.\n",
                 w, h, input_beauty_components_);
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
    inputs_set_              = false;
    input_width_             = 0;
    input_height_            = 0;
    input_beauty_components_ = 0;

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
        "rebuild with -DRR_ENABLE_OPTIX=ON and "
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
        "rebuild with -DRR_ENABLE_OPTIX=ON and "
        "pass -DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return false;
}

bool OptixDenoiser::invoke(const Output& /*output*/) noexcept {
    // Stage 19B.3 audit-host fallback. The invoke path is
    // pure OptiX-runtime + CUDA-runtime work (compute-memory
    // -resources, setup, invoke, cudaMalloc / cudaFree); none
    // of those are available without the SDK. Document the
    // failure; no GPU work is launched.
    last_error_ =
        "OptixDenoiser::invoke requires the OptiX SDK; "
        "rebuild with -DRR_ENABLE_OPTIX=ON and "
        "pass -DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return false;
}

void OptixDenoiser::shutdown() noexcept {
    // Audit-host fallback never produces a populated state,
    // so shutdown has nothing to free. All pointers stay
    // null.
    initialized_             = false;
    denoiser_                = nullptr;
    input_images_            = nullptr;
    inputs_set_              = false;
    input_width_             = 0;
    input_height_            = 0;
    input_beauty_components_ = 0;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

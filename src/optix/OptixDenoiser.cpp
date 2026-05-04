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

    // Stage 21B.5: log success / failure of denoiser init
    // via the established rr_optix pattern
    // (`std::fprintf(stderr, "[OptiX:INFO|ERROR] ...")`,
    // matching `OptixBackend.cpp`).
    #include <cstdio>
#endif

namespace rr::optix {

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

// ---- Stage 21C.2: AOV -> OptixImage2D descriptor helpers
//
// File-static helpers that map a `OptixDenoiser::Inputs` /
// `OptixDenoiser::Output` POD into the `::OptixImage2D`
// descriptor the SDK expects. Each helper is pure (no
// global state, no allocation, no side effects); the
// caller passes the AOV and gets back a value-typed
// descriptor that can be slotted into a `::OptixImage2D[3]`
// for the input layer or used directly as the output
// descriptor.
//
// Layout contract (matches the Stage 21A.3 / 21A.4 plan +
// the Stage 21C.1 struct doc-comments):
//
//   - Beauty: linear-RGB radiance, FLOAT3 (3 floats /
//     pixel) or FLOAT4 (4 floats / pixel) depending on
//     `beauty_components`. The OptiX denoiser maps each
//     to `OPTIX_PIXEL_FORMAT_FLOAT3` /
//     `OPTIX_PIXEL_FORMAT_FLOAT4` respectively.
//   - Albedo: linear-RGB base colour, FLOAT3 (always 3
//     floats / pixel; the renderer's `AOVType::Albedo`
//     buffer layout).
//   - Normal: encoded shading normal, FLOAT3 (always 3
//     floats / pixel; encoded `0.5 n + 0.5` per the AOV
//     pipeline convention).
//   - Output: same component count as the bound Beauty
//     input (`beauty_components`); FLOAT3 or FLOAT4
//     accordingly.
//
// Stage 21C.2 only ships the helpers; the Stage 21C.x
// implementation that wires them into `set_inputs()` /
// `invoke()` lands in subsequent sub-stages. The
// `[[maybe_unused]]` attributes silence the "function
// declared but not used" warnings under stricter compiler
// flags until the wiring lands.
namespace {

constexpr unsigned int kFloat3Bytes = 3u * sizeof(float);
constexpr unsigned int kFloat4Bytes = 4u * sizeof(float);

[[maybe_unused]] ::OptixImage2D
make_beauty_image(const OptixDenoiser::Inputs& inputs) noexcept {
    const unsigned int pixel_bytes =
        (inputs.beauty_components == 4) ? kFloat4Bytes : kFloat3Bytes;
    const ::OptixPixelFormat fmt =
        (inputs.beauty_components == 4)
            ? OPTIX_PIXEL_FORMAT_FLOAT4
            : OPTIX_PIXEL_FORMAT_FLOAT3;

    ::OptixImage2D img{};
    img.data = reinterpret_cast<::CUdeviceptr>(
        const_cast<float*>(inputs.beauty_device));
    img.width              = static_cast<unsigned int>(inputs.width);
    img.height             = static_cast<unsigned int>(inputs.height);
    img.rowStrideInBytes   = static_cast<unsigned int>(inputs.width)
                           * pixel_bytes;
    img.pixelStrideInBytes = pixel_bytes;
    img.format             = fmt;
    return img;
}

[[maybe_unused]] ::OptixImage2D
make_albedo_image(const OptixDenoiser::Inputs& inputs) noexcept {
    ::OptixImage2D img{};
    img.data = reinterpret_cast<::CUdeviceptr>(
        const_cast<float*>(inputs.albedo_device));
    img.width              = static_cast<unsigned int>(inputs.width);
    img.height             = static_cast<unsigned int>(inputs.height);
    img.rowStrideInBytes   = static_cast<unsigned int>(inputs.width)
                           * kFloat3Bytes;
    img.pixelStrideInBytes = kFloat3Bytes;
    img.format             = OPTIX_PIXEL_FORMAT_FLOAT3;
    return img;
}

[[maybe_unused]] ::OptixImage2D
make_normal_image(const OptixDenoiser::Inputs& inputs) noexcept {
    ::OptixImage2D img{};
    img.data = reinterpret_cast<::CUdeviceptr>(
        const_cast<float*>(inputs.normal_device));
    img.width              = static_cast<unsigned int>(inputs.width);
    img.height             = static_cast<unsigned int>(inputs.height);
    img.rowStrideInBytes   = static_cast<unsigned int>(inputs.width)
                           * kFloat3Bytes;
    img.pixelStrideInBytes = kFloat3Bytes;
    img.format             = OPTIX_PIXEL_FORMAT_FLOAT3;
    return img;
}

[[maybe_unused]] ::OptixImage2D
make_output_image(const OptixDenoiser::Output& output,
                  int beauty_components) noexcept {
    const unsigned int pixel_bytes =
        (beauty_components == 4) ? kFloat4Bytes : kFloat3Bytes;
    const ::OptixPixelFormat fmt =
        (beauty_components == 4)
            ? OPTIX_PIXEL_FORMAT_FLOAT4
            : OPTIX_PIXEL_FORMAT_FLOAT3;

    ::OptixImage2D img{};
    img.data               = reinterpret_cast<::CUdeviceptr>(output.device);
    img.width              = static_cast<unsigned int>(output.width);
    img.height             = static_cast<unsigned int>(output.height);
    img.rowStrideInBytes   = static_cast<unsigned int>(output.width)
                           * pixel_bytes;
    img.pixelStrideInBytes = pixel_bytes;
    img.format             = fmt;
    return img;
}

// Stage 21C.3: prepare a beauty-only `::OptixDenoiserLayer`
// from a beauty AOV + caller-supplied output buffer. "Beauty
// only" = no albedo / normal guide layer; the function
// ignores `inputs.albedo_device` and `inputs.normal_device`.
//
// Returns a value-typed layer with:
// - `input`           = the beauty AOV descriptor (built via
//                       `make_beauty_image`).
// - `previousOutput`  = zero-initialised (unused outside the
//                       temporal denoiser models the project
//                       does not target per the Stage 21A.9
//                       v1-scope decision).
// - `output`          = the output descriptor (built via
//                       `make_output_image`).
//
// The layer is the building block the eventual
// `optixDenoiserInvoke` call will hand to the SDK as the
// `layers` array. Stage 21C.3 only ships the helper; no
// invoke happens yet. The function is `noexcept` and pure
// (no allocation, no global state, no side effects);
// `[[maybe_unused]]` silences the unused-function warning
// until the next sub-stage wires it.
[[maybe_unused]] ::OptixDenoiserLayer
prepareBeautyOnlyInput(const OptixDenoiser::Inputs&  inputs,
                       const OptixDenoiser::Output&  output) noexcept {
    ::OptixDenoiserLayer layer{};
    layer.input          = make_beauty_image(inputs);
    layer.previousOutput = ::OptixImage2D{};  // unused (no temporal)
    layer.output         = make_output_image(output,
                                             inputs.beauty_components);
    return layer;
}

// Stage 21C.4: guided denoiser input. Same Beauty +
// Output shape as `prepareBeautyOnlyInput`, plus the
// `OptixDenoiserGuideLayer` carrying the Albedo + Normal
// guide images. This matches the denoiser's init-time
// options pinned at Stage 21B.4 (`guideAlbedo = 1`,
// `guideNormal = 1`), so the resulting structs are what
// the eventual `optixDenoiserInvoke` call will hand to
// the SDK.
//
// The `flow`, `previousOutputInternalGuideLayer`,
// `outputInternalGuideLayer`, and (newer SDK) flow-
// trustworthiness fields stay zero-initialised because
// the project's denoiser model is HDR (non-temporal) per
// the Stage 21A.9 v1-scope decision; the SDK ignores
// them outside the temporal models.
struct GuidedDenoiserInput {
    ::OptixDenoiserLayer       layer;
    ::OptixDenoiserGuideLayer  guide;
};

[[maybe_unused]] GuidedDenoiserInput
prepareGuidedInput(const OptixDenoiser::Inputs&  inputs,
                   const OptixDenoiser::Output&  output) noexcept {
    GuidedDenoiserInput out{};

    // Beauty + output (same shape as the beauty-only path).
    out.layer.input          = make_beauty_image(inputs);
    out.layer.previousOutput = ::OptixImage2D{};
    out.layer.output         = make_output_image(output,
                                                 inputs.beauty_components);

    // Guide layer: Albedo + Normal. The other guide-layer
    // fields stay zero-initialised (temporal denoiser
    // territory; not used by the HDR model).
    out.guide.albedo = make_albedo_image(inputs);
    out.guide.normal = make_normal_image(inputs);
    return out;
}

}  // namespace

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

OptixDenoiser::~OptixDenoiser() {
    // Stage 21B.10: every cleanup step lives in
    // `shutdown()`; the destructor is a single delegating
    // call so destruction during stack unwind, after a
    // failed `initialize` / `set_inputs`, or after an
    // explicit `shutdown` are all the same code path.
    // See the doc-comment block above `shutdown` for the
    // no-leak / no-crash invariants.
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
      state_size_(other.state_size_),
      scratch_size_(other.scratch_size_),
      last_error_(std::move(other.last_error_)) {
    other.denoiser_                = nullptr;
    other.initialized_             = false;
    other.input_images_            = nullptr;
    other.inputs_set_              = false;
    other.input_width_             = 0;
    other.input_height_            = 0;
    other.input_beauty_components_ = 0;
    other.state_size_              = 0;
    other.scratch_size_            = 0;
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
        state_size_              = other.state_size_;
        scratch_size_            = other.scratch_size_;
        last_error_              = std::move(other.last_error_);
        other.denoiser_                = nullptr;
        other.initialized_             = false;
        other.input_images_            = nullptr;
        other.inputs_set_              = false;
        other.input_width_             = 0;
        other.input_height_            = 0;
        other.input_beauty_components_ = 0;
        other.state_size_              = 0;
        other.scratch_size_            = 0;
    }
    return *this;
}

bool OptixDenoiser::is_initialized() const noexcept { return initialized_; }
bool OptixDenoiser::inputs_set()     const noexcept { return inputs_set_; }
int  OptixDenoiser::input_width()    const noexcept { return input_width_; }
int  OptixDenoiser::input_height()   const noexcept { return input_height_; }
void* OptixDenoiser::denoiser_handle() const noexcept { return denoiser_; }
const std::string& OptixDenoiser::last_error() const noexcept { return last_error_; }

// Stage 21B.10 cleanup contract.
//
// The destructor delegates to `shutdown()`. `shutdown()`
// releases every resource the class can hold, in a fixed
// order:
//
//   1. (SDK_FOUND only) `optixDenoiserDestroy` on the
//      OptiX handle, when non-null. The SDK documents
//      this call as safe on a non-null handle; we add a
//      null guard for defensive symmetry. After the call
//      we log `[OptiX:INFO] OptixDenoiser destroyed.`
//      matching the Stage 21B.5 init-log pattern.
//   2. Reset every scalar / pointer member to its
//      default-constructed state.
//   3. `state_buffer_.reset()` and
//      `scratch_buffer_.reset()` to free the device-
//      side state + scratch buffers (also free
//      automatically via `GpuBuffer`'s destructor; the
//      explicit reset keeps the cleanup contract
//      symmetric with `set_inputs`'s allocate calls).
//
// Invariants:
//
// - **No leak**: every resource paired with an allocate
//   has a matching free in `shutdown`. The destructor
//   always reaches the cleanup path because `shutdown`
//   is `noexcept` and the destructor body is a single
//   call to it.
// - **No crash**: `optixDenoiserDestroy` is null-guarded;
//   `GpuBuffer.reset()` is documented as safe on an
//   empty / moved-from / never-allocated buffer; every
//   member assignment is a trivial store. Calling
//   `shutdown()` repeatedly (or after a failed
//   `initialize()` / `set_inputs()`) is a documented
//   no-op.
// - **`noexcept`**: the destructor inherits `noexcept`
//   from `~OptixDenoiser()`'s default exception
//   specification (no member type throws on destruction;
//   `GpuBuffer`'s destructor is `noexcept`); `shutdown`
//   is explicitly `noexcept`.
void OptixDenoiser::shutdown() noexcept {
#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    // Stage 21B.4: paired with `optixDenoiserCreate` in
    // `initialize()`. Stage 21B.10: log the destruction
    // for parity with the init log added in Stage 21B.5.
    if (denoiser_ != nullptr) {
        ::optixDenoiserDestroy(static_cast<::OptixDenoiser>(denoiser_));
        std::fprintf(stderr,
                     "[OptiX:INFO] OptixDenoiser destroyed.\n");
    }
#endif
    initialized_             = false;
    denoiser_                = nullptr;
    input_images_            = nullptr;
    inputs_set_              = false;
    input_width_             = 0;
    input_height_            = 0;
    input_beauty_components_ = 0;
    state_size_              = 0;
    scratch_size_            = 0;

    // Stage 21B.7: free state + scratch device buffers.
    // GpuBuffer's destructor will also free them, but
    // the explicit reset here keeps lifetime symmetric
    // with `set_inputs`'s allocate calls and ensures
    // re-using the OptixDenoiser via a second
    // `initialize -> set_inputs` cycle starts from
    // empty buffers.
    state_buffer_.reset();
    scratch_buffer_.reset();
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
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser init failed: %s\n",
                     last_error_.c_str());
        return false;
    }

    denoiser_    = denoiser;
    initialized_ = true;
    std::fprintf(stderr,
                 "[OptiX:INFO] OptixDenoiser created "
                 "(HDR model, guideAlbedo=1, guideNormal=1, "
                 "denoiseAlpha=COPY).\n");
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

// ---- set_inputs: Stage 21B.6 memory-query split -----------
// Stage 21B.6 wires the second real SDK call:
// `optixDenoiserComputeMemoryResources` queries the
// per-resolution state + scratch sizes for the framebuffer
// dimensions provided in `inputs` and stores them in
// `state_size_` / `scratch_size_`. NO buffer allocation
// happens here (per the user's "do not allocate yet"
// rule); the descriptor binding (OptixImage2D triplet)
// also lands in a subsequent sub-stage.

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

bool OptixDenoiser::set_inputs(const Inputs& inputs) noexcept {
    if (!initialized_) {
        last_error_ =
            "OptixDenoiser::set_inputs: denoiser is not initialized; "
            "call initialize(backend) first.";
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser set_inputs failed: %s\n",
                     last_error_.c_str());
        return false;
    }
    if (inputs.beauty_device == nullptr
     || inputs.albedo_device == nullptr
     || inputs.normal_device == nullptr) {
        last_error_ =
            "OptixDenoiser::set_inputs: every device pointer "
            "(beauty / albedo / normal) must be non-null.";
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser set_inputs failed: %s\n",
                     last_error_.c_str());
        return false;
    }
    if (inputs.width <= 0 || inputs.height <= 0) {
        last_error_ =
            "OptixDenoiser::set_inputs: width and height must be "
            "positive.";
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser set_inputs failed: %s\n",
                     last_error_.c_str());
        return false;
    }
    if (inputs.beauty_components != 3 && inputs.beauty_components != 4) {
        last_error_ =
            "OptixDenoiser::set_inputs: beauty_components must be 3 "
            "(FLOAT3) or 4 (FLOAT4).";
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser set_inputs failed: %s\n",
                     last_error_.c_str());
        return false;
    }

    auto* denoiser = static_cast<::OptixDenoiser>(denoiser_);
    const auto w   = static_cast<unsigned int>(inputs.width);
    const auto h   = static_cast<unsigned int>(inputs.height);

    ::OptixDenoiserSizes sizes{};
    const ::OptixResult res =
        ::optixDenoiserComputeMemoryResources(denoiser, w, h, &sizes);
    if (res != OPTIX_SUCCESS) {
        last_error_ =
            std::string("optixDenoiserComputeMemoryResources failed: ")
          + ::optixGetErrorName(res);
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser set_inputs failed: %s\n",
                     last_error_.c_str());
        return false;
    }

    // Stage 21B.6: snapshot the queried sizes.
    state_size_   = sizes.stateSizeInBytes;
    scratch_size_ = sizes.withoutOverlapScratchSizeInBytes;

    // Stage 21B.7: allocate state + scratch device buffers
    // via the project's `rr::gpu::GpuBuffer` utility (which
    // forwards to `cudaMalloc` under the hood; see
    // `src/gpu/GpuBuffer.{h,cpp}`). On failure, free both
    // sides so the class doesn't keep a partial allocation
    // around, populate `last_error_`, and return false.
    if (!state_buffer_.allocate(state_size_)) {
        state_buffer_.reset();
        scratch_buffer_.reset();
        last_error_ = std::string(
            "OptixDenoiser::set_inputs: failed to allocate "
            "denoiser state buffer (")
          + std::to_string(state_size_) + " bytes).";
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser set_inputs failed: %s\n",
                     last_error_.c_str());
        return false;
    }
    if (!scratch_buffer_.allocate(scratch_size_)) {
        state_buffer_.reset();
        scratch_buffer_.reset();
        last_error_ = std::string(
            "OptixDenoiser::set_inputs: failed to allocate "
            "denoiser scratch buffer (")
          + std::to_string(scratch_size_) + " bytes).";
        std::fprintf(stderr,
                     "[OptiX:ERROR] denoiser set_inputs failed: %s\n",
                     last_error_.c_str());
        return false;
    }

    // Stage 21B.8: per-resolution setup. Initialises the
    // state buffer for the bound dimensions; the scratch
    // buffer is also bound here so subsequent
    // `optixDenoiserInvoke` calls can use it. Default
    // stream (0) keeps the call synchronous from the
    // host's perspective. No invoke happens here.
    {
        const ::OptixResult res = ::optixDenoiserSetup(
            denoiser,
            /*stream=*/0,
            w, h,
            reinterpret_cast<::CUdeviceptr>(state_buffer_.device_ptr()),
            state_size_,
            reinterpret_cast<::CUdeviceptr>(scratch_buffer_.device_ptr()),
            scratch_size_);
        if (res != OPTIX_SUCCESS) {
            state_buffer_.reset();
            scratch_buffer_.reset();
            last_error_ =
                std::string("optixDenoiserSetup failed: ")
              + ::optixGetErrorName(res);
            std::fprintf(stderr,
                         "[OptiX:ERROR] denoiser set_inputs failed: %s\n",
                         last_error_.c_str());
            return false;
        }
    }

    // The OptixImage2D descriptor triplet is still not built
    // here (`input_images_` stays null); the descriptor
    // binding lands in a subsequent sub-stage.
    input_width_             = inputs.width;
    input_height_            = inputs.height;
    input_beauty_components_ = inputs.beauty_components;
    inputs_set_              = true;
    last_error_.clear();

    std::fprintf(stderr,
                 "[OptiX:INFO] OptixDenoiser setup complete: "
                 "width=%u height=%u stateSize=%zu scratchSize=%zu.\n",
                 w, h, state_size_, scratch_size_);
    return true;
}

#else  // RELATIVITYRENDER_OPTIX_SDK_FOUND

bool OptixDenoiser::set_inputs(const Inputs& /*inputs*/) noexcept {
    last_error_ =
        "OptixDenoiser::set_inputs requires the OptiX SDK; "
        "rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return false;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

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

#include "optix/OptixPipeline.h"
#include "optix/OptixBackend.h"

#include <cstdio>
#include <utility>

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <cuda_runtime.h>
    #include <optix.h>
    #include <optix_stubs.h>

    // The build-time PTX → header pipeline (cmake/EmbedPtx
    // AsHeader.cmake) drops this file in
    // ${CMAKE_CURRENT_BINARY_DIR}; rr_optix's PRIVATE include
    // dir routes the lookup.
    #include "OptixPrograms_embedded_ptx.h"

    #include "optix/OptixLaunchParams.h"
    #include "optix/OptixSBT.h"

    #include <cstddef>
    #include <cstring>
    #include <string>
#endif

namespace rr::optix {

// ---- members (always compiled) -----------------------------------

OptixPipeline::~OptixPipeline() {
    reset();
}

OptixPipeline::OptixPipeline(OptixPipeline&& other) noexcept
    : module_(other.module_),
      prog_raygen_(other.prog_raygen_),
      prog_miss_(other.prog_miss_),
      pipeline_(other.pipeline_),
      sbt_record_buf_(other.sbt_record_buf_),
      sbt_descriptor_(other.sbt_descriptor_),
      launch_params_(other.launch_params_),
      launch_params_size_(other.launch_params_size_) {
    other.module_         = nullptr;
    other.prog_raygen_    = nullptr;
    other.prog_miss_      = nullptr;
    other.pipeline_       = nullptr;
    other.sbt_record_buf_ = nullptr;
    other.sbt_descriptor_ = nullptr;
    other.launch_params_  = nullptr;
    other.launch_params_size_ = 0;
}

OptixPipeline& OptixPipeline::operator=(OptixPipeline&& other) noexcept {
    if (this != &other) {
        reset();
        module_         = other.module_;
        prog_raygen_    = other.prog_raygen_;
        prog_miss_      = other.prog_miss_;
        pipeline_       = other.pipeline_;
        sbt_record_buf_ = other.sbt_record_buf_;
        sbt_descriptor_ = other.sbt_descriptor_;
        launch_params_  = other.launch_params_;
        launch_params_size_ = other.launch_params_size_;
        other.module_         = nullptr;
        other.prog_raygen_    = nullptr;
        other.prog_miss_      = nullptr;
        other.pipeline_       = nullptr;
        other.sbt_record_buf_ = nullptr;
        other.sbt_descriptor_ = nullptr;
        other.launch_params_  = nullptr;
        other.launch_params_size_ = 0;
    }
    return *this;
}

bool OptixPipeline::valid() const noexcept {
    return pipeline_         != nullptr
        && sbt_descriptor_   != nullptr
        && launch_params_    != nullptr;
}

void* OptixPipeline::pipeline_handle() const noexcept {
    return pipeline_;
}

const void* OptixPipeline::shader_binding_table() const noexcept {
    return sbt_descriptor_;
}

void* OptixPipeline::launch_params_device_ptr() const noexcept {
    return launch_params_;
}

std::size_t OptixPipeline::launch_params_size_bytes() const noexcept {
    return launch_params_size_;
}

// ---- create + reset (SDK-gated bodies) ---------------------------

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

OptixPipelineResult OptixPipeline::create(OptixBackend& backend) {
    OptixPipelineResult r;

    if (!backend.isInitialized()) {
        r.error_message =
            "OptixPipeline::create: backend is not initialized; "
            "call OptixBackend::initialize() first.";
        return r;
    }

    auto* ctx = static_cast<::OptixDeviceContext>(backend.device_context());
    if (ctx == nullptr) {
        r.error_message =
            "OptixPipeline::create: OptixDeviceContext is null even "
            "though backend.isInitialized() is true; internal error.";
        return r;
    }

    reset();

    // Module compile options. Debug level is set to NONE for
    // release-quality codegen; subsequent sub-stages can wire
    // up debug-info pass-through if needed.
    ::OptixModuleCompileOptions module_opts{};
    module_opts.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
    module_opts.optLevel         = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    module_opts.debugLevel       = OPTIX_COMPILE_DEBUG_LEVEL_NONE;

    ::OptixPipelineCompileOptions pipeline_opts{};
    pipeline_opts.usesMotionBlur                   = 0;
    pipeline_opts.traversableGraphFlags            =
        OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
    pipeline_opts.numPayloadValues                 = 0;
    pipeline_opts.numAttributeValues               = 2;
    pipeline_opts.exceptionFlags                   = OPTIX_EXCEPTION_FLAG_NONE;
    pipeline_opts.pipelineLaunchParamsVariableName = "optixLaunchParams";

    // 1. Compile the embedded PTX into a module.
    ::OptixModule module = nullptr;
    {
        char log[2048];
        std::size_t log_size = sizeof(log);
        const ::OptixResult res = ::optixModuleCreate(
            ctx, &module_opts, &pipeline_opts,
            g_optix_programs_ptx, g_optix_programs_ptx_size,
            log, &log_size, &module);
        if (res != OPTIX_SUCCESS) {
            r.error_message = std::string("optixModuleCreate failed: ")
                            + ::optixGetErrorName(res)
                            + " | " + std::string(log, log_size);
            return r;
        }
    }

    // 2. Program groups (raygen + miss).
    ::OptixProgramGroupOptions pg_opts{};

    ::OptixProgramGroupDesc raygen_desc{};
    raygen_desc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    raygen_desc.raygen.module            = module;
    raygen_desc.raygen.entryFunctionName = "__raygen__pinhole";

    ::OptixProgramGroupDesc miss_desc{};
    miss_desc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    miss_desc.miss.module                = module;
    miss_desc.miss.entryFunctionName     = "__miss__radiance";

    ::OptixProgramGroup raygen_pg = nullptr;
    {
        char log[2048]; std::size_t log_size = sizeof(log);
        const ::OptixResult res = ::optixProgramGroupCreate(
            ctx, &raygen_desc, /*num_program_groups=*/1, &pg_opts,
            log, &log_size, &raygen_pg);
        if (res != OPTIX_SUCCESS) {
            ::optixModuleDestroy(module);
            r.error_message = std::string("optixProgramGroupCreate(raygen) failed: ")
                            + ::optixGetErrorName(res)
                            + " | " + std::string(log, log_size);
            return r;
        }
    }
    ::OptixProgramGroup miss_pg = nullptr;
    {
        char log[2048]; std::size_t log_size = sizeof(log);
        const ::OptixResult res = ::optixProgramGroupCreate(
            ctx, &miss_desc, /*num_program_groups=*/1, &pg_opts,
            log, &log_size, &miss_pg);
        if (res != OPTIX_SUCCESS) {
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("optixProgramGroupCreate(miss) failed: ")
                            + ::optixGetErrorName(res)
                            + " | " + std::string(log, log_size);
            return r;
        }
    }

    // 3. Link pipeline.
    ::OptixPipeline pipeline = nullptr;
    {
        ::OptixProgramGroup pgs[] = { raygen_pg, miss_pg };
        ::OptixPipelineLinkOptions link_opts{};
        link_opts.maxTraceDepth = 1;  // Stage 17A.3: no trace, but
                                      // OptiX requires >= 1.

        char log[2048]; std::size_t log_size = sizeof(log);
        const ::OptixResult res = ::optixPipelineCreate(
            ctx, &pipeline_opts, &link_opts,
            pgs, sizeof(pgs) / sizeof(pgs[0]),
            log, &log_size, &pipeline);
        if (res != OPTIX_SUCCESS) {
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("optixPipelineCreate failed: ")
                            + ::optixGetErrorName(res)
                            + " | " + std::string(log, log_size);
            return r;
        }
    }

    // 4. SBT records (raygen + miss).
    RaygenSbtRecord raygen_record{};
    {
        const ::OptixResult res = ::optixSbtRecordPackHeader(raygen_pg, &raygen_record);
        if (res != OPTIX_SUCCESS) {
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("optixSbtRecordPackHeader(raygen) failed: ")
                            + ::optixGetErrorName(res);
            return r;
        }
    }
    MissSbtRecord miss_record{};
    {
        const ::OptixResult res = ::optixSbtRecordPackHeader(miss_pg, &miss_record);
        if (res != OPTIX_SUCCESS) {
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("optixSbtRecordPackHeader(miss) failed: ")
                            + ::optixGetErrorName(res);
            return r;
        }
    }

    // Upload records to a single device buffer:
    // [raygen_record][miss_record]. Strides are fixed at the
    // record sizes; the SBT descriptor points at the
    // appropriate offsets.
    constexpr std::size_t kRaygenSize = sizeof(RaygenSbtRecord);
    constexpr std::size_t kMissSize   = sizeof(MissSbtRecord);
    void* d_records = nullptr;
    {
        const ::cudaError_t e = ::cudaMalloc(&d_records, kRaygenSize + kMissSize);
        if (e != cudaSuccess) {
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("cudaMalloc(SBT records) failed: ")
                            + ::cudaGetErrorString(e);
            return r;
        }
    }
    {
        const ::cudaError_t e = ::cudaMemcpy(
            d_records, &raygen_record, kRaygenSize, cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_records);
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("cudaMemcpy(raygen record) failed: ")
                            + ::cudaGetErrorString(e);
            return r;
        }
    }
    {
        const ::cudaError_t e = ::cudaMemcpy(
            static_cast<char*>(d_records) + kRaygenSize,
            &miss_record, kMissSize, cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_records);
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("cudaMemcpy(miss record) failed: ")
                            + ::cudaGetErrorString(e);
            return r;
        }
    }

    // Allocate + populate the host-side SBT descriptor.
    auto* sbt = new ::OptixShaderBindingTable{};
    sbt->raygenRecord                 = reinterpret_cast<::CUdeviceptr>(d_records);
    sbt->missRecordBase               = reinterpret_cast<::CUdeviceptr>(
        static_cast<char*>(d_records) + kRaygenSize);
    sbt->missRecordStrideInBytes      = static_cast<unsigned>(kMissSize);
    sbt->missRecordCount              = 1;
    // No hit / callable / exception groups in Stage 17A.3.

    // 5. Launch-params buffer.
    void* d_launch_params = nullptr;
    {
        const ::cudaError_t e = ::cudaMalloc(&d_launch_params,
                                             sizeof(OptixLaunchParams));
        if (e != cudaSuccess) {
            delete sbt;
            ::cudaFree(d_records);
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("cudaMalloc(launch params) failed: ")
                            + ::cudaGetErrorString(e);
            return r;
        }
    }

    // Commit ownership.
    module_             = module;
    prog_raygen_        = raygen_pg;
    prog_miss_          = miss_pg;
    pipeline_           = pipeline;
    sbt_record_buf_     = d_records;
    sbt_descriptor_     = sbt;
    launch_params_      = d_launch_params;
    launch_params_size_ = sizeof(OptixLaunchParams);

    std::fprintf(stderr,
                 "[OptiX:INFO] Pipeline built: 1 module, 2 program "
                 "groups (raygen + miss), %zu-byte SBT, %zu-byte "
                 "launch-params buffer.\n",
                 kRaygenSize + kMissSize, sizeof(OptixLaunchParams));
    r.ok = true;
    return r;
}

void OptixPipeline::reset() noexcept {
    if (launch_params_ != nullptr) {
        ::cudaFree(launch_params_);
        launch_params_ = nullptr;
    }
    if (sbt_descriptor_ != nullptr) {
        delete static_cast<::OptixShaderBindingTable*>(sbt_descriptor_);
        sbt_descriptor_ = nullptr;
    }
    if (sbt_record_buf_ != nullptr) {
        ::cudaFree(sbt_record_buf_);
        sbt_record_buf_ = nullptr;
    }
    if (pipeline_ != nullptr) {
        ::optixPipelineDestroy(static_cast<::OptixPipeline>(pipeline_));
        pipeline_ = nullptr;
    }
    if (prog_miss_ != nullptr) {
        ::optixProgramGroupDestroy(static_cast<::OptixProgramGroup>(prog_miss_));
        prog_miss_ = nullptr;
    }
    if (prog_raygen_ != nullptr) {
        ::optixProgramGroupDestroy(static_cast<::OptixProgramGroup>(prog_raygen_));
        prog_raygen_ = nullptr;
    }
    if (module_ != nullptr) {
        ::optixModuleDestroy(static_cast<::OptixModule>(module_));
        module_ = nullptr;
    }
    launch_params_size_ = 0;
}

#else   // RELATIVITYRENDER_OPTIX_SDK_FOUND

OptixPipelineResult OptixPipeline::create(OptixBackend& /*backend*/) {
    OptixPipelineResult r;
    r.error_message =
        "OptixPipeline::create requires the OptiX SDK; rebuild "
        "with -DRELATIVITYRENDER_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

void OptixPipeline::reset() noexcept {
    // Audit-host fallback never produces a populated pipeline,
    // so reset() has nothing to free. All pointers stay null.
    module_             = nullptr;
    prog_raygen_        = nullptr;
    prog_miss_          = nullptr;
    pipeline_           = nullptr;
    sbt_record_buf_     = nullptr;
    sbt_descriptor_     = nullptr;
    launch_params_      = nullptr;
    launch_params_size_ = 0;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

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
      prog_miss_shadow_(other.prog_miss_shadow_),
      prog_hitgroup_(other.prog_hitgroup_),
      pipeline_(other.pipeline_),
      sbt_record_buf_(other.sbt_record_buf_),
      sbt_descriptor_(other.sbt_descriptor_),
      launch_params_(other.launch_params_),
      launch_params_size_(other.launch_params_size_) {
    other.module_         = nullptr;
    other.prog_raygen_      = nullptr;
    other.prog_miss_        = nullptr;
    other.prog_miss_shadow_ = nullptr;
    other.prog_hitgroup_    = nullptr;
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
        prog_raygen_      = other.prog_raygen_;
        prog_miss_        = other.prog_miss_;
        prog_miss_shadow_ = other.prog_miss_shadow_;
        prog_hitgroup_    = other.prog_hitgroup_;
        pipeline_       = other.pipeline_;
        sbt_record_buf_ = other.sbt_record_buf_;
        sbt_descriptor_ = other.sbt_descriptor_;
        launch_params_  = other.launch_params_;
        launch_params_size_ = other.launch_params_size_;
        other.module_         = nullptr;
        other.prog_raygen_      = nullptr;
        other.prog_miss_        = nullptr;
        other.prog_miss_shadow_ = nullptr;
        other.prog_hitgroup_    = nullptr;
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

OptixPipelineResult OptixPipeline::create(OptixBackend& backend,
                                          OptixPipelineOptions opts) {
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
    // Stage 17A.4: 3 payload registers carry the closest-hit /
    // miss RGB result back to the raygen.
    // Stage 20H: bumped to 4. Register 3 carries the per-ray
    // Doppler factor D computed once in the raygen (after
    // aberration), read by both __closesthit__radiance and
    // __miss__radiance to apply Doppler color + searchlight
    // without recomputing D in each shader.
    // Stage 20I: bumped to 10 to fit the path-tracer payload
    // layout used by the new __raygen__pathtrace family. The
    // existing radiance programs only use registers [0..3]
    // (RGB + D); the higher registers stay unused for them.
    // The path-tracer programs use:
    //   p0     status (0 = hit, 1 = miss)
    //   p1..p3 hit position xyz (hit only; unused on miss)
    //   p4..p6 hit normal xyz (hit) OR miss radiance xyz
    //   p7..p9 hit albedo xyz (hit only; unused on miss)
    pipeline_opts.numPayloadValues                 = 10;
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
    // Stage 20I: pick entry function names based on
    // `opts.path_tracer`. Both program-group sets live in the
    // same compiled PTX module; only the SBT-bound names
    // differ.
    const char* const k_raygen_name = opts.path_tracer
                                    ? "__raygen__pathtrace"
                                    : "__raygen__pinhole";
    const char* const k_miss_name   = opts.path_tracer
                                    ? "__miss__pathtrace"
                                    : "__miss__radiance";
    const char* const k_ch_name     = opts.path_tracer
                                    ? "__closesthit__pathtrace"
                                    : "__closesthit__radiance";

    ::OptixProgramGroupOptions pg_opts{};

    ::OptixProgramGroupDesc raygen_desc{};
    raygen_desc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    raygen_desc.raygen.module            = module;
    raygen_desc.raygen.entryFunctionName = k_raygen_name;

    ::OptixProgramGroupDesc miss_desc{};
    miss_desc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    miss_desc.miss.module                = module;
    miss_desc.miss.entryFunctionName     = k_miss_name;

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

    // Stage 20L: second miss program group bound to
    // __miss__shadow. Built unconditionally so consumers
    // that opt into shadow rays (Stage 20L
    // `enable_shadows`) reference missSbtIndex = 1; the
    // existing radiance / path-tracer entries continue to
    // use missSbtIndex = 0 unchanged.
    ::OptixProgramGroupDesc miss_shadow_desc{};
    miss_shadow_desc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    miss_shadow_desc.miss.module            = module;
    miss_shadow_desc.miss.entryFunctionName = "__miss__shadow";

    ::OptixProgramGroup miss_shadow_pg = nullptr;
    {
        char log[2048]; std::size_t log_size = sizeof(log);
        const ::OptixResult res = ::optixProgramGroupCreate(
            ctx, &miss_shadow_desc, /*num_program_groups=*/1, &pg_opts,
            log, &log_size, &miss_shadow_pg);
        if (res != OPTIX_SUCCESS) {
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("optixProgramGroupCreate(miss_shadow) failed: ")
                            + ::optixGetErrorName(res)
                            + " | " + std::string(log, log_size);
            return r;
        }
    }

    // Stage 17A.4: hit-group program group (closest-hit only).
    ::OptixProgramGroupDesc hitgroup_desc{};
    hitgroup_desc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    hitgroup_desc.hitgroup.moduleCH            = module;
    hitgroup_desc.hitgroup.entryFunctionNameCH = k_ch_name;
    // No any-hit (moduleAH / entryFunctionNameAH stay null).
    // No intersection (moduleIS / entryFunctionNameIS stay null;
    // built-in triangle intersection is used).

    ::OptixProgramGroup hitgroup_pg = nullptr;
    {
        char log[2048]; std::size_t log_size = sizeof(log);
        const ::OptixResult res = ::optixProgramGroupCreate(
            ctx, &hitgroup_desc, /*num_program_groups=*/1, &pg_opts,
            log, &log_size, &hitgroup_pg);
        if (res != OPTIX_SUCCESS) {
            ::optixProgramGroupDestroy(miss_shadow_pg);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("optixProgramGroupCreate(hitgroup) failed: ")
                            + ::optixGetErrorName(res)
                            + " | " + std::string(log, log_size);
            return r;
        }
    }

    // 3. Link pipeline (now with 4 program groups).
    ::OptixPipeline pipeline = nullptr;
    {
        ::OptixProgramGroup pgs[] = { raygen_pg, miss_pg,
                                      miss_shadow_pg, hitgroup_pg };
        ::OptixPipelineLinkOptions link_opts{};
        // Stage 20L: bumped to 2 so the closest-hit can
        // recursively call optixTrace for shadow rays.
        // Existing entries that do not trace shadows are
        // unaffected (the increase only changes pipeline
        // state setup; per-trace cost on non-recursive paths
        // is unchanged).
        link_opts.maxTraceDepth = 2;

        char log[2048]; std::size_t log_size = sizeof(log);
        const ::OptixResult res = ::optixPipelineCreate(
            ctx, &pipeline_opts, &link_opts,
            pgs, sizeof(pgs) / sizeof(pgs[0]),
            log, &log_size, &pipeline);
        if (res != OPTIX_SUCCESS) {
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
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
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("optixSbtRecordPackHeader(miss) failed: ")
                            + ::optixGetErrorName(res);
            return r;
        }
    }
    // Stage 20L: second miss record bound to __miss__shadow.
    MissSbtRecord miss_shadow_record{};
    {
        const ::OptixResult res = ::optixSbtRecordPackHeader(
            miss_shadow_pg, &miss_shadow_record);
        if (res != OPTIX_SUCCESS) {
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message =
                std::string("optixSbtRecordPackHeader(miss_shadow) failed: ")
              + ::optixGetErrorName(res);
            return r;
        }
    }
    HitGroupSbtRecord hitgroup_record{};
    {
        const ::OptixResult res = ::optixSbtRecordPackHeader(hitgroup_pg, &hitgroup_record);
        if (res != OPTIX_SUCCESS) {
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("optixSbtRecordPackHeader(hitgroup) failed: ")
                            + ::optixGetErrorName(res);
            return r;
        }
    }

    // Upload records to a single device buffer.
    // Stage 17A.4 layout: [raygen][miss_radiance][hitgroup].
    // Stage 20L extended layout:
    //   [raygen][miss_radiance][miss_shadow][hitgroup].
    // Strides are fixed at the record sizes; the SBT
    // descriptor's missRecordCount becomes 2 so consumers
    // can pass missSbtIndex = 0 (radiance) or 1 (shadow).
    constexpr std::size_t kRaygenSize   = sizeof(RaygenSbtRecord);
    constexpr std::size_t kMissSize     = sizeof(MissSbtRecord);
    constexpr std::size_t kHitGroupSize = sizeof(HitGroupSbtRecord);
    constexpr std::size_t kTotalSize    = kRaygenSize
                                        + kMissSize * 2u
                                        + kHitGroupSize;

    void* d_records = nullptr;
    {
        const ::cudaError_t e = ::cudaMalloc(&d_records, kTotalSize);
        if (e != cudaSuccess) {
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
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
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
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
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("cudaMemcpy(miss record) failed: ")
                            + ::cudaGetErrorString(e);
            return r;
        }
    }
    // Stage 20L: second miss record (shadow) at offset
    // kRaygenSize + kMissSize.
    {
        const ::cudaError_t e = ::cudaMemcpy(
            static_cast<char*>(d_records) + kRaygenSize + kMissSize,
            &miss_shadow_record, kMissSize, cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_records);
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("cudaMemcpy(miss_shadow record) failed: ")
                            + ::cudaGetErrorString(e);
            return r;
        }
    }
    {
        const ::cudaError_t e = ::cudaMemcpy(
            static_cast<char*>(d_records) + kRaygenSize + kMissSize * 2u,
            &hitgroup_record, kHitGroupSize, cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
            ::cudaFree(d_records);
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
            ::optixProgramGroupDestroy(miss_pg);
            ::optixProgramGroupDestroy(raygen_pg);
            ::optixModuleDestroy(module);
            r.error_message = std::string("cudaMemcpy(hitgroup record) failed: ")
                            + ::cudaGetErrorString(e);
            return r;
        }
    }

    // Allocate + populate the host-side SBT descriptor.
    // Stage 20L: missRecordCount = 2 covers both
    // missSbtIndex = 0 (radiance) and 1 (shadow); the shadow
    // record sits at offset kRaygenSize + kMissSize and the
    // hitgroup record now starts at kRaygenSize + 2 * kMissSize.
    auto* sbt = new ::OptixShaderBindingTable{};
    sbt->raygenRecord                 = reinterpret_cast<::CUdeviceptr>(d_records);
    sbt->missRecordBase               = reinterpret_cast<::CUdeviceptr>(
        static_cast<char*>(d_records) + kRaygenSize);
    sbt->missRecordStrideInBytes      = static_cast<unsigned>(kMissSize);
    sbt->missRecordCount              = 2;
    sbt->hitgroupRecordBase           = reinterpret_cast<::CUdeviceptr>(
        static_cast<char*>(d_records) + kRaygenSize + kMissSize * 2u);
    sbt->hitgroupRecordStrideInBytes  = static_cast<unsigned>(kHitGroupSize);
    sbt->hitgroupRecordCount          = 1;

    // 5. Launch-params buffer.
    void* d_launch_params = nullptr;
    {
        const ::cudaError_t e = ::cudaMalloc(&d_launch_params,
                                             sizeof(OptixLaunchParams));
        if (e != cudaSuccess) {
            delete sbt;
            ::cudaFree(d_records);
            ::optixPipelineDestroy(pipeline);
            ::optixProgramGroupDestroy(hitgroup_pg);
            ::optixProgramGroupDestroy(miss_shadow_pg);
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
    prog_miss_shadow_   = miss_shadow_pg;
    prog_hitgroup_      = hitgroup_pg;
    pipeline_           = pipeline;
    sbt_record_buf_     = d_records;
    sbt_descriptor_     = sbt;
    launch_params_      = d_launch_params;
    launch_params_size_ = sizeof(OptixLaunchParams);

    std::fprintf(stderr,
                 "[OptiX:INFO] Pipeline built: 1 module, 3 program "
                 "groups (raygen + miss + hitgroup), %zu-byte SBT, "
                 "%zu-byte launch-params buffer.\n",
                 kTotalSize, sizeof(OptixLaunchParams));
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
    if (prog_hitgroup_ != nullptr) {
        ::optixProgramGroupDestroy(static_cast<::OptixProgramGroup>(prog_hitgroup_));
        prog_hitgroup_ = nullptr;
    }
    if (prog_miss_shadow_ != nullptr) {
        ::optixProgramGroupDestroy(static_cast<::OptixProgramGroup>(prog_miss_shadow_));
        prog_miss_shadow_ = nullptr;
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

OptixPipelineResult
OptixPipeline::set_hit_material(
    const rr::material::MaterialParams& params,
    int shading_mode) noexcept {
    OptixPipelineResult r;
    if (!valid()) {
        r.error_message =
            "OptixPipeline::set_hit_material: pipeline is not "
            "valid; call create() first and check the return.";
        return r;
    }

    // Compute the in-buffer offset of the hit-group record's
    // `data` field. Layout in `sbt_record_buf_` is:
    //   [raygen][miss][hitgroup_record]
    // and inside `hitgroup_record`:
    //   [header bytes ... offsetof(HitGroupSbtRecord, data) ... HitGroupData]
    // Use `offsetof` rather than `OPTIX_SBT_RECORD_HEADER_SIZE`
    // so any alignment padding the compiler inserts is honored.
    constexpr std::size_t kRaygenSize    = sizeof(RaygenSbtRecord);
    constexpr std::size_t kMissSize      = sizeof(MissSbtRecord);
    constexpr std::size_t kDataOffset    =
        offsetof(HitGroupSbtRecord, data);
    constexpr std::size_t kDataBytes     = sizeof(HitGroupData);

    HitGroupData host_data{};
    host_data.params       = params;
    host_data.shading_mode = shading_mode;

    char* dst = static_cast<char*>(sbt_record_buf_)
              + kRaygenSize + kMissSize + kDataOffset;

    const ::cudaError_t e = ::cudaMemcpy(
        dst, &host_data, kDataBytes, cudaMemcpyHostToDevice);
    if (e != cudaSuccess) {
        r.error_message =
            std::string("OptixPipeline::set_hit_material: "
                        "cudaMemcpy(HitGroupData) failed: ")
          + ::cudaGetErrorString(e);
        return r;
    }

    r.ok = true;
    return r;
}

#else   // RELATIVITYRENDER_OPTIX_SDK_FOUND

OptixPipelineResult OptixPipeline::create(OptixBackend& /*backend*/,
                                          OptixPipelineOptions /*opts*/) {
    OptixPipelineResult r;
    r.error_message =
        "OptixPipeline::create requires the OptiX SDK; rebuild "
        "with -DRR_ENABLE_OPTIX=ON and pass "
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
    prog_miss_shadow_   = nullptr;
    prog_hitgroup_      = nullptr;
    pipeline_           = nullptr;
    sbt_record_buf_     = nullptr;
    sbt_descriptor_     = nullptr;
    launch_params_      = nullptr;
    launch_params_size_ = 0;
}

OptixPipelineResult
OptixPipeline::set_hit_material(
    const rr::material::MaterialParams& /*params*/,
    int /*shading_mode*/) noexcept {
    OptixPipelineResult r;
    r.error_message =
        "OptixPipeline::set_hit_material requires the OptiX "
        "SDK; rebuild with -DRR_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so the SBT records "
        "exist. The CUDA path is unaffected.";
    return r;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

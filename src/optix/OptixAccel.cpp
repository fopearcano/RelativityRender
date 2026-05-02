#include "optix/OptixAccel.h"
#include "optix/OptixBackend.h"

#include <cstdio>
#include <utility>

// Stage 17A.2: pull the SDK + CUDA-runtime headers in only when
// the SDK was located at configure time. The audit-host
// fallback (ENABLE_OPTIX=ON, SDK not found) compiles the stub
// branch below and `build_mesh_gas` returns failure with a
// clear error message.
#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    #include <cuda_runtime.h>
    #include <optix.h>
    #include <optix_stubs.h>
#endif

namespace rr::optix {

// ---- OptixGas members (always compiled) ----------------------------

OptixGas::~OptixGas() {
    reset();
}

OptixGas::OptixGas(OptixGas&& other) noexcept
    : handle_(other.handle_),
      device_buffer_(other.device_buffer_),
      output_size_bytes_(other.output_size_bytes_) {
    other.handle_            = 0;
    other.device_buffer_     = nullptr;
    other.output_size_bytes_ = 0;
}

OptixGas& OptixGas::operator=(OptixGas&& other) noexcept {
    if (this != &other) {
        reset();
        handle_            = other.handle_;
        device_buffer_     = other.device_buffer_;
        output_size_bytes_ = other.output_size_bytes_;
        other.handle_            = 0;
        other.device_buffer_     = nullptr;
        other.output_size_bytes_ = 0;
    }
    return *this;
}

bool OptixGas::empty() const noexcept {
    return device_buffer_ == nullptr;
}

std::uint64_t OptixGas::handle() const noexcept {
    return handle_;
}

void* OptixGas::device_buffer() const noexcept {
    return device_buffer_;
}

std::size_t OptixGas::output_size_bytes() const noexcept {
    return output_size_bytes_;
}

void OptixGas::assign(std::uint64_t handle,
                     void*          device_buffer,
                     std::size_t    output_size_bytes) noexcept {
    reset();
    handle_            = handle;
    device_buffer_     = device_buffer;
    output_size_bytes_ = output_size_bytes;
}

void OptixGas::reset() noexcept {
#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND
    // The audit-host fallback never produces a non-null
    // device_buffer_ (build_mesh_gas always errors out without
    // the SDK), so the `cudaFree` call is only meaningful in
    // the SDK-found build.
    if (device_buffer_ != nullptr) {
        ::cudaFree(device_buffer_);
    }
#endif
    handle_            = 0;
    device_buffer_     = nullptr;
    output_size_bytes_ = 0;
}

// ---- build_mesh_gas (SDK-found body) -------------------------------

#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND

BuildGasResult build_mesh_gas(OptixBackend&       backend,
                              const MeshGasInput& input) {
    BuildGasResult r;

    if (!backend.isInitialized()) {
        r.error_message =
            "build_mesh_gas: backend is not initialized; call "
            "OptixBackend::initialize() first.";
        return r;
    }
    if (input.vertex_count == 0 || input.triangle_count == 0) {
        r.error_message =
            "build_mesh_gas: empty mesh (vertex_count or "
            "triangle_count is zero).";
        return r;
    }
    if (input.device_vertices == nullptr
     || input.device_indices  == nullptr) {
        r.error_message =
            "build_mesh_gas: device_vertices or device_indices "
            "is null.";
        return r;
    }

    auto* ctx = static_cast<::OptixDeviceContext>(backend.device_context());
    if (ctx == nullptr) {
        r.error_message =
            "build_mesh_gas: OptixDeviceContext is null even "
            "though backend.isInitialized() is true; "
            "internal error.";
        return r;
    }

    // Triangle build input. The vertex pointer must be passed
    // through an array (one entry per motion key); for a static
    // GAS the array has length 1, so we point at a local
    // CUdeviceptr that lives until the build calls return.
    ::CUdeviceptr d_vertices =
        reinterpret_cast<::CUdeviceptr>(input.device_vertices);
    ::CUdeviceptr d_indices  =
        reinterpret_cast<::CUdeviceptr>(input.device_indices);

    static const unsigned int s_triangle_flags[] = {
        OPTIX_GEOMETRY_FLAG_NONE,
    };

    ::OptixBuildInput build_input{};
    build_input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;

    auto& tri = build_input.triangleArray;
    tri.vertexFormat        = OPTIX_VERTEX_FORMAT_FLOAT3;
    tri.vertexStrideInBytes = 3u * sizeof(float);
    tri.numVertices         = static_cast<unsigned int>(input.vertex_count);
    tri.vertexBuffers       = &d_vertices;
    tri.indexFormat         = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    tri.indexStrideInBytes  = 3u * sizeof(unsigned int);
    tri.numIndexTriplets    = static_cast<unsigned int>(input.triangle_count);
    tri.indexBuffer         = d_indices;
    tri.flags               = s_triangle_flags;
    tri.numSbtRecords       = 1;

    // Build options: static / no compaction. The
    // ALLOW_COMPACTION flag would let a future sub-stage compact
    // the GAS; today we keep the build path minimal.
    ::OptixAccelBuildOptions options{};
    options.buildFlags = OPTIX_BUILD_FLAG_NONE;
    options.operation  = OPTIX_BUILD_OPERATION_BUILD;
    options.motionOptions.numKeys = 1;  // static (no motion blur)

    // Compute build buffer sizes.
    ::OptixAccelBufferSizes sizes{};
    {
        const ::OptixResult res = ::optixAccelComputeMemoryUsage(
            ctx, &options, &build_input,
            /*numBuildInputs=*/1u, &sizes);
        if (res != OPTIX_SUCCESS) {
            r.error_message =
                std::string("optixAccelComputeMemoryUsage failed: ")
              + ::optixGetErrorName(res);
            return r;
        }
    }

    // Allocate temp buffer (build scratch).
    void* d_temp = nullptr;
    if (sizes.tempSizeInBytes > 0) {
        const ::cudaError_t e =
            ::cudaMalloc(&d_temp, sizes.tempSizeInBytes);
        if (e != cudaSuccess) {
            r.error_message =
                std::string("cudaMalloc(temp) failed: ")
              + ::cudaGetErrorString(e);
            return r;
        }
    }

    // Allocate output buffer (kept for the GAS's lifetime).
    void* d_output = nullptr;
    {
        const ::cudaError_t e =
            ::cudaMalloc(&d_output, sizes.outputSizeInBytes);
        if (e != cudaSuccess) {
            if (d_temp) ::cudaFree(d_temp);
            r.error_message =
                std::string("cudaMalloc(output) failed: ")
              + ::cudaGetErrorString(e);
            return r;
        }
    }

    // Build.
    ::OptixTraversableHandle handle = 0;
    {
        const ::OptixResult res = ::optixAccelBuild(
            ctx,
            /*stream=*/0,
            &options,
            &build_input,
            /*numBuildInputs=*/1u,
            reinterpret_cast<::CUdeviceptr>(d_temp),
            sizes.tempSizeInBytes,
            reinterpret_cast<::CUdeviceptr>(d_output),
            sizes.outputSizeInBytes,
            &handle,
            /*emittedProperties=*/nullptr,
            /*numEmittedProperties=*/0);
        if (res != OPTIX_SUCCESS) {
            ::cudaFree(d_output);
            if (d_temp) ::cudaFree(d_temp);
            r.error_message =
                std::string("optixAccelBuild failed: ")
              + ::optixGetErrorName(res);
            return r;
        }
    }

    // Free temp; output buffer + handle stay with the GAS.
    if (d_temp) ::cudaFree(d_temp);

    r.gas.assign(static_cast<std::uint64_t>(handle),
                 d_output,
                 sizes.outputSizeInBytes);
    r.ok = true;

    std::fprintf(
        stderr,
        "[OptiX:INFO] GAS built: %zu vertices, %zu triangles, "
        "%zu bytes.\n",
        input.vertex_count, input.triangle_count,
        sizes.outputSizeInBytes);
    return r;
}

#else   // RELATIVITYRENDER_OPTIX_SDK_FOUND

// Audit-host fallback. ENABLE_OPTIX is on but the SDK headers
// are not available; the function compiles + links cleanly but
// always reports a "SDK not found" error.

BuildGasResult build_mesh_gas(OptixBackend&       /*backend*/,
                              const MeshGasInput& /*input*/) {
    BuildGasResult r;
    r.error_message =
        "build_mesh_gas requires the OptiX SDK; rebuild with "
        "-DRELATIVITYRENDER_ENABLE_OPTIX=ON and pass "
        "-DOPTIX_ROOT=/path/to/optix-sdk so <optix.h> is "
        "available. The CUDA path is unaffected.";
    return r;
}

#endif  // RELATIVITYRENDER_OPTIX_SDK_FOUND

}  // namespace rr::optix

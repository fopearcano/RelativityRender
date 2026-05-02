#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Stage 17A.2 triangle-GAS builder for the OptiX backend. Per
// `docs/OPTIX_BACKEND_PLAN.md` §22, this header declares the
// build helpers that consume already-uploaded vertex / index
// buffers and produce an `OptixTraversableHandle` plus the
// device-side acceleration-structure storage. Stage 17A.2 ships
// triangle geometry only; sphere GAS / IAS / multi-mesh land in
// subsequent 17A+ sub-stages.
//
// Two-layer compile-time gating mirrors `OptixBackend`:
// - When `RELATIVITYRENDER_ENABLE_OPTIX` is undefined the rr_optix
//   library is not built at all (per Stage 12B.3).
// - When `RELATIVITYRENDER_OPTIX_SDK_FOUND` is undefined the
//   `build_mesh_gas` body returns a "SDK not found" error and
//   the audit-host build still compiles cleanly.
//
// The header deliberately avoids `<optix.h>`. The traversable
// handle is exposed as `std::uint64_t` (the underlying type of
// `OptixTraversableHandle`); device buffers are `void*`.
// Consumers that need typed handles reinterpret in their own
// `.cpp` after including `<optix.h>`.
//
// Stage 17A.2 scope: build only. NO IAS, NO SBT, NO pipelines,
// NO `optixLaunch`. The returned handle is opaque storage today;
// subsequent sub-stages thread it through `optixLaunchParams
// .scene_handle` per §15.1.

namespace rr::optix {

class OptixBackend;

// Inputs for a single-mesh triangle GAS build.
//
// All pointers must be device-resident. The caller owns the
// underlying memory (typically `rr::gpu::GpuBuffer<...>` /
// `rr::gpu::GpuMesh::device_vertices()`); `build_mesh_gas`
// reads from these pointers during the build but never frees
// them.
//
// Layout assumptions:
// - `device_vertices`: contiguous float3 array (3 floats per
//   vertex, X/Y/Z, no padding). `vertex_count` is the number
//   of vertices, NOT the number of floats. Bytes the GAS
//   reads = `vertex_count * 12`.
// - `device_indices`: contiguous uint3 array (3 uint32 per
//   triangle, vertex indices into the vertex array).
//   `triangle_count` is the number of triangles. Bytes the
//   GAS reads = `triangle_count * 12`.
//
// Per the Stage 17A.2 "static scene only" rule, the resulting
// GAS is built once and never updated. A future sub-stage that
// needs runtime updates can add an `update_mesh_gas(...)`
// sibling using `OPTIX_BUILD_OPERATION_UPDATE`.
struct MeshGasInput {
    const void* device_vertices = nullptr;
    std::size_t vertex_count    = 0;
    const void* device_indices  = nullptr;
    std::size_t triangle_count  = 0;
};

// Move-only owner for a built triangle GAS.
//
// Holds the device-resident acceleration-structure buffer and
// the `OptixTraversableHandle` (exposed here as `uint64_t` to
// keep the header SDK-free) that downstream sub-stages will
// pass to `optixLaunch` through `optixLaunchParams.scene_handle`.
//
// The destructor frees the device buffer; `reset()` is the
// explicit form. After a move-from, the source has the empty
// (default-constructed) state.
class OptixGas {
public:
    OptixGas() noexcept = default;
    ~OptixGas();

    OptixGas(const OptixGas&)            = delete;
    OptixGas& operator=(const OptixGas&) = delete;
    OptixGas(OptixGas&&) noexcept;
    OptixGas& operator=(OptixGas&&) noexcept;

    // True iff the GAS owns no device buffer / has no
    // traversable handle (default-constructed or moved-from).
    [[nodiscard]] bool          empty()             const noexcept;

    // The OptixTraversableHandle for this GAS. Returns 0 for an
    // empty GAS. Real type is `OptixTraversableHandle` (a
    // `uint64_t` typedef in the SDK).
    [[nodiscard]] std::uint64_t handle()            const noexcept;

    // The device-resident acceleration-structure buffer.
    // Returns nullptr for an empty GAS. The buffer is freed by
    // `reset()` / the destructor; do not free externally.
    [[nodiscard]] void*         device_buffer()     const noexcept;

    // Size in bytes of `device_buffer()`. Useful for
    // diagnostics.
    [[nodiscard]] std::size_t   output_size_bytes() const noexcept;

    // Free the device allocation. Idempotent. Safe to call on
    // an empty / moved-from GAS.
    void reset() noexcept;

    // Internal: populate the owner with a freshly-built GAS.
    // Called by `build_mesh_gas` on success. Public so the
    // build function (a free function in the same namespace)
    // can write the fields without `friend` declarations.
    void assign(std::uint64_t handle,
                void*         device_buffer,
                std::size_t   output_size_bytes) noexcept;

private:
    std::uint64_t handle_            = 0;
    void*         device_buffer_     = nullptr;
    std::size_t   output_size_bytes_ = 0;
};

// Result of a `build_mesh_gas` call.
//
// On success: `ok == true`, `gas` owns the device buffer +
// traversable handle, `error_message` is empty.
// On failure: `ok == false`, `gas` is empty, `error_message`
// describes the cause. Any device memory allocated during a
// partial failure is freed before return; no leaks.
struct BuildGasResult {
    bool        ok = false;
    OptixGas    gas;
    std::string error_message;
};

// Build a single-mesh triangle GAS using the OptiX 7+ build
// pipeline (`optixAccelComputeMemoryUsage` + `optixAccelBuild`).
//
// Preconditions:
// - `backend.isInitialized() == true`. Otherwise the result is
//   `ok=false` with a "backend not initialized" error.
// - `input.vertex_count > 0` AND `input.triangle_count > 0`.
//   Empty meshes return `ok=false` with an "empty mesh" error.
// - `input.device_vertices` and `input.device_indices` are
//   non-null and point to device memory.
//
// The build runs synchronously on stream 0 (default). Static
// scene only - no compaction, no rebuild path.
//
// On a no-SDK build (audit-host fallback), the function always
// returns `ok=false` with an "OptiX SDK not found at build
// time" error. The class still compiles + links so consumers
// can be written ahead of SDK availability.
[[nodiscard]] BuildGasResult build_mesh_gas(
    OptixBackend&       backend,
    const MeshGasInput& input);

}  // namespace rr::optix

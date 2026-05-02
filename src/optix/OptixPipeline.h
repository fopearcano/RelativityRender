#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Stage 17A.3 OptiX pipeline + SBT lifecycle. Per
// `docs/OPTIX_BACKEND_PLAN.md` §19, this is the host-side
// owner of:
// - The OptiX `OptixModule` compiled from the embedded PTX
//   (Stage 17A.3 ships `OptixPrograms.cu` -> `OptixPrograms.ptx`
//   -> `g_optix_programs_ptx` C-string via
//   `cmake/EmbedPtxAsHeader.cmake`).
// - The `OptixProgramGroup` array (raygen + miss for Stage
//   17A.3; closest-hit + any-hit + intersection join in
//   subsequent sub-stages).
// - The `OptixPipeline` linked from the program groups.
// - The Shader Binding Table records on the device, plus the
//   `OptixShaderBindingTable` descriptor passed to
//   `optixLaunch`.
//
// Stage 17A.3 scope: minimum viable pipeline. NO closest-hit,
// NO any-hit, NO intersection. Raygen writes a flat colour
// directly to the framebuffer; miss exists only to satisfy SBT
// layout.
//
// Two-layer macro gating (mirrors `OptixBackend` /
// `OptixAccel`):
// - `RELATIVITYRENDER_ENABLE_OPTIX` undefined -> rr_optix is
//   not built at all (Stage 12B.3 gating).
// - `RELATIVITYRENDER_OPTIX_SDK_FOUND` undefined ->
//   `create()` returns failure with a "SDK not found"
//   message; the audit-host build still compiles cleanly.
//
// Public surface avoids `<optix.h>`; opaque handles
// (`std::uint64_t` / `void*`) are reinterpreted on the
// consumer side after including the SDK header.

namespace rr::optix {

class OptixBackend;

// Result of a pipeline-build step: success flag +
// human-readable error message. The pipeline itself is owned
// by the `OptixPipeline` instance; this struct just signals
// outcome.
struct OptixPipelineResult {
    bool        ok = false;
    std::string error_message;
};

// Move-only owner for the OptiX module + program groups +
// pipeline + SBT records. Created via `create()`; destroyed
// automatically on scope exit (or via explicit `reset()`).
//
// Public accessors expose the pieces downstream sub-stages
// need: the pipeline handle (passed to `optixLaunch`), the
// SBT pointer, and the device-resident launch-params buffer
// the host populates per launch.
class OptixPipeline {
public:
    OptixPipeline() noexcept = default;
    ~OptixPipeline();

    OptixPipeline(const OptixPipeline&)            = delete;
    OptixPipeline& operator=(const OptixPipeline&) = delete;
    OptixPipeline(OptixPipeline&&) noexcept;
    OptixPipeline& operator=(OptixPipeline&&) noexcept;

    // Build the pipeline + SBT against the given backend.
    // The backend must be initialised
    // (`backend.isInitialized() == true`); otherwise the
    // result is `ok=false` with a clear error.
    //
    // Stage 17A.3's SBT has exactly two records (raygen +
    // miss); subsequent sub-stages grow the program-group set
    // and the corresponding SBT records.
    [[nodiscard]] OptixPipelineResult create(OptixBackend& backend);

    // Free every device + host resource owned by the
    // pipeline. Idempotent. Destructor calls this.
    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept;

    // The OptixPipeline handle (real type:
    // `OptixPipeline_t*`); returned as `void*` to keep this
    // header SDK-free. Consumers reinterpret in their own
    // .cpp.
    [[nodiscard]] void*         pipeline_handle() const noexcept;

    // Pointer to the on-device `OptixShaderBindingTable`
    // descriptor (NOT the device pointer to the SBT records;
    // the descriptor itself is host-resident but the records
    // it references are on-device). Stage 17A.3 stores it
    // inside the pipeline owner; the host passes its
    // `address` (re-cast appropriately) to `optixLaunch`.
    [[nodiscard]] const void*   shader_binding_table() const noexcept;

    // Device pointer to the launch-params upload buffer the
    // host fills per launch and `optixLaunch` reads via
    // `pipelineParams`. Size is `sizeof(OptixLaunchParams)`.
    // The backend owns the buffer; consumers do not free it.
    [[nodiscard]] void*         launch_params_device_ptr() const noexcept;
    [[nodiscard]] std::size_t   launch_params_size_bytes() const noexcept;

private:
    // Opaque internal state. The .cpp casts these back to
    // typed OptiX handles when the SDK is available; on the
    // audit-host fallback they stay null and `valid()`
    // returns false.
    void*       module_         = nullptr;  // OptixModule
    void*       prog_raygen_    = nullptr;  // OptixProgramGroup
    void*       prog_miss_      = nullptr;  // OptixProgramGroup
    // Stage 17A.4: hit-group program group (closest-hit only;
    // any-hit + intersection programs are not used).
    void*       prog_hitgroup_  = nullptr;  // OptixProgramGroup
    void*       pipeline_       = nullptr;  // OptixPipeline_t*
    // Device buffer holding [raygen][miss][hitgroup] records.
    void*       sbt_record_buf_ = nullptr;
    void*       sbt_descriptor_ = nullptr;  // host-side OptixShaderBindingTable
    void*       launch_params_  = nullptr;  // device buffer for OptixLaunchParams
    std::size_t launch_params_size_ = 0;
};

}  // namespace rr::optix

#pragma once

#include "material/MaterialTypes.h"

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

// Stage 20I: pipeline build options. Selects which entry
// function names get bound when `OptixPipeline::create()`
// builds the program groups.
//
// Default (`path_tracer = false`) preserves the Stage 17A.3+
// surface used by every existing OptiX render entry:
//   raygen      = __raygen__pinhole
//   miss        = __miss__radiance
//   closesthit  = __closesthit__radiance
//
// `path_tracer = true` switches to the Stage 20I family:
//   raygen      = __raygen__pathtrace
//   miss        = __miss__pathtrace
//   closesthit  = __closesthit__pathtrace
//
// Both program-group sets live in the same compiled PTX
// (src/optix/OptixPrograms.cu); the SBT records bind whichever
// triple this option selects.
struct OptixPipelineOptions {
    bool path_tracer = false;
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
    // Stage 17A.3's SBT has exactly three records (raygen +
    // miss + closest-hit). Stage 20I adds the
    // `OptixPipelineOptions` argument to select between the
    // existing radiance entry points and the new path-tracer
    // entry points. Both sets live in the same compiled PTX;
    // the SBT records bind whichever triple is selected.
    [[nodiscard]] OptixPipelineResult create(
        OptixBackend& backend,
        OptixPipelineOptions opts = OptixPipelineOptions{});

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

    // Stage 20G: re-upload the data portion of the hit-group
    // SBT record with new `MaterialParams` + `shading_mode`.
    // The header bytes (packed by `optixSbtRecordPackHeader`
    // during `create()`) are preserved; only the per-record
    // `HitGroupData` is overwritten.
    //
    // Default state after `create()` is `HitGroupData{}` -
    // `shading_mode = 0` (closest-hit emits normal-as-color)
    // - so existing OptiX render entries that never call this
    // method retain their Stage 17A.4 / 17A.5 visual output.
    // The new `--render-optix-material-scene` action calls
    // this with the picked mesh's material + `mode = 1`
    // (closest-hit emits `baseColor + emissionColor *
    // emissionStrength`).
    //
    // Returns success only when the pipeline is `valid()` and
    // the per-record data slot was successfully re-uploaded.
    // No-op + returns failure on invalid pipeline / audit-
    // host fallback.
    [[nodiscard]] OptixPipelineResult set_hit_material(
        const rr::material::MaterialParams& params,
        int shading_mode = 1) noexcept;

private:
    // Opaque internal state. The .cpp casts these back to
    // typed OptiX handles when the SDK is available; on the
    // audit-host fallback they stay null and `valid()`
    // returns false.
    void*       module_         = nullptr;  // OptixModule
    void*       prog_raygen_    = nullptr;  // OptixProgramGroup
    void*       prog_miss_      = nullptr;  // OptixProgramGroup
    // Stage 20L: second miss program group bound to
    // __miss__shadow. Built unconditionally even when
    // path_tracer == true; consumers that do not trace
    // shadow rays simply do not reference missSbtIndex = 1.
    void*       prog_miss_shadow_ = nullptr; // OptixProgramGroup
    // Stage 17A.4: hit-group program group (closest-hit only;
    // any-hit + intersection programs are not used).
    void*       prog_hitgroup_  = nullptr;  // OptixProgramGroup
    void*       pipeline_       = nullptr;  // OptixPipeline_t*
    // Device buffer holding [raygen][miss_radiance][miss_shadow][hitgroup] records.
    void*       sbt_record_buf_ = nullptr;
    void*       sbt_descriptor_ = nullptr;  // host-side OptixShaderBindingTable
    void*       launch_params_  = nullptr;  // device buffer for OptixLaunchParams
    std::size_t launch_params_size_ = 0;
};

}  // namespace rr::optix

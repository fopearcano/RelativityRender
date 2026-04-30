#pragma once

// Stage 12B.2 placeholder for the OptiX device-context lifecycle owner.
//
// Per OPTIX_BACKEND_PLAN.md §18, this is the analogue of
// `cuda/CudaContext.{h,cpp}` for the OptiX backend - the eventual
// owner of `OptixDeviceContext` creation + destruction, log-callback
// registration, and the runtime availability query that the
// higher-level OptiX renderer uses to gate its dispatch.
//
// Stage 12B.2's role is the *file skeleton only*: this header
// declares the class and a single static `isCompiled()` query.
// `isCompiled()` reports whether the project was built with
// `-DRELATIVITYRENDER_ENABLE_OPTIX=ON` (Stage 12B.1's option flag,
// threaded through as a compile definition by `CMakeLists.txt`).
//
// What this header is NOT yet:
// - Does not include `<optix.h>` or any other OptiX SDK header.
// - Does not declare `initialize` / `shutdown` / `device_context()`
//   accessors (those land alongside the actual OptixDeviceContext
//   wiring in a subsequent 12B sub-stage).
// - Reporting `isCompiled() == true` does not imply the OptiX SDK
//   is available, the runtime can initialise, or any device is
//   visible. Today the macro means only "the project was built
//   with the OptiX flag toggled on"; until subsequent slices wire
//   the SDK detection, that is the strongest claim that can be
//   made honestly.

namespace rr::optix {

class OptixBackend {
public:
    // Returns true iff the project was compiled with the
    // `RELATIVITYRENDER_ENABLE_OPTIX` macro defined (i.e., the
    // operator passed `-DRELATIVITYRENDER_ENABLE_OPTIX=ON` to
    // `cmake`). Returns false otherwise. Pure preprocessor query;
    // no OptiX runtime calls, no device probing.
    [[nodiscard]] static bool isCompiled() noexcept;
};

}  // namespace rr::optix

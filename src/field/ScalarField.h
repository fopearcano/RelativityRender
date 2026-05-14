#pragma once

// Scalar field sampler (see `docs/FIELD_INTERPRETATION_LAYER.md`
// §3.1). Phase 1's first concrete field-sampler shape.
//
// What lives here this slice (FIELD.1)
// ------------------------------------
// - The `ScalarField` POD itself: a constant-value scalar
//   field. The simplest real sampler - `sample(f, event)`
//   returns `f.value` regardless of the chart event. Real,
//   complete, minimal; not a stub.
// - The `sample(ScalarField, Vec4)` free function. Takes the
//   chart event in `Vec4` form (time first, spatial second
//   per the manifold convention) and returns the field value
//   at that event. For the constant field shipped here, the
//   event coordinate is intentionally unused.
//
// What does NOT live here this slice
// ----------------------------------
// - **No texture-backed scalar field.** A future
//   `TextureScalarField` will sample from a 3D / 4D texture
//   in chart coordinates; it lands with its own slice when
//   the texture system gains chart-aware sampling.
// - **No procedural / function-backed scalar field.** A
//   future `ProceduralScalarField` carrying a `RR_HD` evaluator
//   pointer will land alongside its first artist-facing
//   procedural (e.g. a 4D Perlin scalar).
// - **No Klein-Gordon evolver.** The design doc §3.1 and §7
//   pin this firmly: a wavefunction analogue `φ(x)` is input
//   data, never the solution of an equation the renderer
//   integrates.
// - **No chart-aware sampling.** The sampler does not consume
//   a `ManifoldTransform`; future curved-chart samplers will
//   widen the call site, not break the contract for the
//   constant-field case.

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec4.h"

namespace rr::field {

struct ScalarField {
    // Constant value the field returns at every chart event.
    // Default `0.0f` makes the field a documented no-op when a
    // Phase 1 module's strength is non-zero - i.e. the module
    // emits no luminous / chromatic / volumetric contribution
    // unless the artist sets a non-zero `value` *and* a
    // non-zero strength on the module.
    float value = 0.0f;
};

// Samples the scalar field at chart event `event`. For the
// constant `ScalarField` POD shipped this slice, `event` is
// intentionally unused. The function exists so future
// scalar-field shapes (texture / procedural / etc.) can be
// added without changing the call site - they replace this
// overload with their own implementations under the same
// signature.
RR_HD inline float sample(const ScalarField& f,
                          rr::math::Vec4 /*event*/) {
    return f.value;
}

// Returns the default zero-valued constant scalar field. The
// "no perceptual contribution" anchor for the design-doc §3
// hint table; matches the factory convention of the manifold
// modules (`euclidean_chart()`, `minkowski_metric()`,
// `rest_frame()`, ...).
RR_HD inline ScalarField zero_scalar_field() {
    return ScalarField{};
}

}

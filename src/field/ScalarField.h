#pragma once

// Scalar field model for the Field Interpretation Layer (see
// `docs/FIELD_INTERPRETATION_LAYER.md` §3.1). Promoted to its
// FIELD.2 shape: two POD types for the two scalar-field kinds
// the design doc enumerates today (constant + sampled-
// placeholder), each carrying advisory value-range metadata,
// and a uniform `evaluate(...)` API that accepts both spatial
// (`Vec3`) and spacetime (`Vec4`) chart events.
//
// What lives here this slice (FIELD.2)
// ------------------------------------
// - `ConstantScalarField` — uniform-value scalar field. The
//   simplest real sampler; not a stub. Carries the field's
//   constant `value` plus advisory `min_value` / `max_value`
//   range metadata.
// - `SampledScalarField` — *placeholder* for a sampled
//   scalar field (texture / grid / procedural backend, all
//   future). Carries the domain-bounding box, a
//   `default_value` returned outside the box / when no
//   backend is wired, and the same advisory range metadata.
//   This slice ships only the metadata POD; no concrete
//   in-domain sampling backend is implemented.
// - `evaluate(field, Vec3)` and `evaluate(field, Vec4)`
//   overloads for both types. Today both return the field's
//   constant / default value regardless of `position`; the
//   `evaluate(ConstantScalarField, ...)` overload is already
//   real (a constant really is constant everywhere), and
//   `evaluate(SampledScalarField, ...)` is honest about
//   returning `default_value` until a backend lands.
// - `zero_constant_scalar_field()` /
//   `zero_sampled_scalar_field()` factories.
//
// What does NOT live here this slice
// ----------------------------------
// - **No texture / grid backend.** The `SampledScalarField`
//   POD carries the metadata a future backend will consume
//   (domain bounding box, default value, range advisory);
//   the backend itself lands with a later slice when the
//   chart-aware texture pipeline is ready.
// - **No procedural / function-pointer backend.** A future
//   `ProceduralScalarField` carrying a `RR_HD` evaluator
//   pointer lands alongside its first artist-facing
//   procedural.
// - **No chart-aware sampling.** Neither `evaluate` overload
//   consumes a `ManifoldTransform`; future curved-chart
//   samplers will widen call sites without breaking the
//   simple constant / sampled cases.
// - **No Klein-Gordon / Schrödinger / Dirac evolver.** The
//   design doc §7 non-goal stands: scalar fields are *input
//   data*, never evolved by the renderer.
// - **No renderer integration.** Nothing in `src/cuda/`,
//   `src/optix/`, `src/pathtracer/`, `src/renderer/`, or any
//   other tree consumes these types this slice.
//
// Value-range metadata
// --------------------
// Both POD types carry `min_value` and `max_value` floats
// declaring the field's *expected* output range. The
// metadata is **advisory**:
//
//   - For `ConstantScalarField` the range is whatever the
//     artist declares; the field's actual output is exactly
//     `value`, which need not lie inside `[min, max]`. The
//     declared range is what downstream Phase 1 modules
//     (`FieldInterpreter`) read to scale chromatic /
//     luminous output.
//   - For `SampledScalarField` the range is the artist's
//     contract with the eventual backend: "values returned
//     by this sampler will lie in `[min, max]`". The
//     renderer treats values outside the range as the
//     artist's responsibility.
//
// Defaults are `min_value = 0.0f`, `max_value = 1.0f` — the
// natural normalised-unit range that Phase 1 chromatic
// kernels assume in the absence of explicit overrides.

#include "math/MathUtils.h"  // RR_HD
#include "math/Vec3.h"
#include "math/Vec4.h"

namespace rr::field {

// Uniform-value scalar field. `value` is returned at every
// chart event; the range metadata is advisory (see header
// comment "Value-range metadata").
struct ConstantScalarField {
    // Constant value the field returns everywhere.
    float value     = 0.0f;

    // Advisory minimum of the field's expected range.
    float min_value = 0.0f;

    // Advisory maximum of the field's expected range.
    float max_value = 1.0f;
};

// Sampled-scalar-field *placeholder*. Carries the metadata a
// future backend (texture / grid / procedural) will consume,
// without shipping the backend itself. `evaluate(...)`
// returns `default_value` everywhere this slice; when the
// backend lands the in-domain evaluation will replace that.
struct SampledScalarField {
    // Bounding box of the sampled domain in chart coordinates.
    // Outside this box the field returns `default_value`.
    // Defaults are an empty box at the chart origin — the
    // artist must override these for a real sampled field.
    rr::math::Vec3 domain_min = {0.0f, 0.0f, 0.0f};
    rr::math::Vec3 domain_max = {0.0f, 0.0f, 0.0f};

    // Value returned outside the domain box, or when no
    // backend is wired (this entire slice). Default `0.0f`.
    float default_value = 0.0f;

    // Advisory range metadata (see header comment).
    float min_value     = 0.0f;
    float max_value     = 1.0f;
};

// Evaluate the constant scalar field at a spatial position.
// Returns `f.value` regardless of `position` — a constant
// field is constant by construction.
RR_HD inline float evaluate(const ConstantScalarField& f,
                            rr::math::Vec3 /*position*/) {
    return f.value;
}

// Evaluate the constant scalar field at a spacetime event.
// Returns `f.value` regardless of `event` — a constant field
// is constant by construction.
RR_HD inline float evaluate(const ConstantScalarField& f,
                            rr::math::Vec4 /*event*/) {
    return f.value;
}

// Evaluate the sampled scalar field at a spatial position.
// This slice returns `f.default_value` everywhere
// (placeholder; no backend wired); a future slice will
// replace this with an in-domain backend lookup that falls
// back to `default_value` outside `[domain_min, domain_max]`.
RR_HD inline float evaluate(const SampledScalarField& f,
                            rr::math::Vec3 /*position*/) {
    return f.default_value;
}

// Evaluate the sampled scalar field at a spacetime event.
// Same placeholder behaviour as the `Vec3` overload — the
// time component is intentionally unused at this slice and
// future backends are free to consume it (e.g. a 4D-evolved
// texture) or ignore it (a static 3D grid).
RR_HD inline float evaluate(const SampledScalarField& f,
                            rr::math::Vec4 /*event*/) {
    return f.default_value;
}

// Returns the default zero-valued constant scalar field
// (`value = 0`, range `[0, 1]`). The "no perceptual
// contribution" anchor for the design-doc §3.1 hint table.
RR_HD inline ConstantScalarField zero_constant_scalar_field() {
    return ConstantScalarField{};
}

// Returns the default sampled scalar field: empty domain at
// chart origin, `default_value = 0`, range `[0, 1]`. The
// safest placeholder — `evaluate(...)` returns `0` until the
// artist sets a non-empty domain and a future backend slice
// wires in real sampling.
RR_HD inline SampledScalarField zero_sampled_scalar_field() {
    return SampledScalarField{};
}

}

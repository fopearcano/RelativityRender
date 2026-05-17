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

// FIELD-I.2 — tagged-union scalar-field config POD.
//
// `ScalarFieldConfig` is the artist-facing single-POD
// configuration for a scalar field, gating its evaluator
// behind a `kind` enum + per-kind parameter slots. This is
// the operator-authoring surface the FIELD-I.* arc consumes
// for Phase 1; the legacy `ConstantScalarField` +
// `SampledScalarField` PODs above remain valid for
// programmatic / kernel-internal use, but the tagged config
// here is what scene files + CLI flags + the
// `FieldInterpreter` payload reference.
//
// Default state: `kind = Constant`, `enabled = false`,
// `strength = 0`, all parameter slots at their natural
// no-op defaults. Every default-default invocation produces
// zero contribution at every evaluator call site.
//
// Three concrete kinds (per
// `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md` §2.2):
//
//   - `Constant`: returns `constant_value` regardless of
//     position. The simplest sampler; not a stub.
//   - `Radial`: returns
//     `lerp(min_value, max_value,
//           smoothstep01(t))`
//     where `t = pow(saturate((|position - center| -
//     min_radius) / (max_radius - min_radius)), falloff)`.
//     Produces a radial smoothstep envelope from
//     `min_value` (at `|delta| <= min_radius`) to
//     `max_value` (at `|delta| >= max_radius`), with the
//     `falloff` exponent reshaping the transition.
//     Mirrors the SCHW.1 / PENROSE.2 radial-symmetric
//     authoring pattern.
//   - `ProceduralPlaceholder`: reserved-but-inert per the
//     FIELD-I.1 plan §2.2. Returns `0.0f` everywhere; a
//     future FIELD-I.* sub-slice (or FIELD-I.5 / .6
//     kernel-bridge) lifts this into a concrete procedural
//     evaluator (e.g. 3D sinusoidal lattice). Selecting
//     this kind today produces a "no-output-this-slice"
//     diagnostic per master rule #3 (the `*Placeholder`
//     naming convention from MANIFOLD.1's
//     `KruskalLikePlaceholder` / `KerrLikePlaceholder`
//     enumerators).
enum class ScalarFieldKind {
    Constant              = 0,
    Radial,
    ProceduralPlaceholder,
};

// Saturating `smoothstep` between `a` and `b`. Returns `0`
// when `x <= a`, `1` when `x >= b`, and the standard
// `3t^2 - 2t^3` smoothstep cubic in between. Local helper
// for the `Radial` kind; not exposed beyond
// `evaluate(ScalarFieldConfig, ...)`.
RR_HD inline float scalar_field_smoothstep(float a, float b, float x) {
    if (!(b > a)) return 0.0f;  // degenerate range; returns 0
    const float t_raw = (x - a) / (b - a);
    const float t = t_raw < 0.0f ? 0.0f : (t_raw > 1.0f ? 1.0f : t_raw);
    return t * t * (3.0f - 2.0f * t);
}

// Single-POD tagged-union scalar-field config.
struct ScalarFieldConfig {
    // Master switch. `false` (default) disables the
    // evaluator entirely; `evaluate(...)` returns `0`
    // regardless of every other field.
    bool enabled = false;

    // Artist-controlled strength multiplier applied at the
    // evaluator's output. Default `0.0f` makes the
    // evaluator return `0` even when `enabled = true`.
    // Lets future scene-file authors keep a field "wired
    // but quiet" until they explicitly dial it up.
    float strength = 0.0f;

    // Active kind selector. Default `Constant`. Switching
    // kinds at authoring time does NOT reset the other
    // parameter slots; the artist may pre-author all
    // parameter slots and toggle the kind to compare
    // behaviours.
    ScalarFieldKind kind = ScalarFieldKind::Constant;

    // Center / origin used by the `Radial` kind as the
    // reference point for the radial distance computation.
    // Default origin `(0, 0, 0)`.
    rr::math::Vec3 center = {0.0f, 0.0f, 0.0f};

    // Inner radius of the `Radial` smoothstep envelope.
    // For positions `|delta| <= min_radius`, the evaluator
    // returns `min_value`. Default `0.0f`.
    float min_radius = 0.0f;

    // Outer radius of the `Radial` smoothstep envelope.
    // For positions `|delta| >= max_radius`, the evaluator
    // returns `max_value`. Default `1.0f`. Required
    // `max_radius > min_radius` for a non-degenerate
    // envelope; the evaluator returns `0` on degenerate
    // configs (defence-in-depth against artist authoring
    // errors).
    float max_radius = 1.0f;

    // Smoothstep falloff exponent. The radial parameter
    // `t = (|delta| - min_radius) / (max_radius -
    // min_radius)` is raised to this power before the
    // smoothstep cubic. Default `1.0f` (no reshaping).
    // Values > 1 push the transition outward; values
    // in `(0, 1)` push it inward. Negative / zero values
    // are clamped to `1.0f` (defence-in-depth).
    float falloff = 1.0f;

    // Output value at `|delta| <= min_radius` for the
    // `Radial` kind. Default `0.0f`.
    float min_value = 0.0f;

    // Output value at `|delta| >= max_radius` for the
    // `Radial` kind. Default `1.0f`. Also the upper bound
    // for the smoothstep interpolation.
    float max_value = 1.0f;

    // Output value for the `Constant` kind, returned at
    // every position. Default `0.0f`. Unused by the
    // `Radial` and `ProceduralPlaceholder` kinds.
    float constant_value = 0.0f;
};

// Evaluate the tagged-union scalar-field config at a
// spatial position. Returns `0.0f` when the field is
// disabled or strength is zero (the no-op anchor). When
// enabled, dispatches on `kind`:
//
//   - `Constant`              → `strength * constant_value`
//   - `Radial`                → `strength * lerp(min_value,
//                                  max_value,
//                                  smoothstep(min_radius,
//                                             max_radius,
//                                             |position - center|^falloff))`
//     (see header comment for the falloff exponent's
//      semantics; degenerate `max_radius <= min_radius`
//      configurations return `0`)
//   - `ProceduralPlaceholder` → `0.0f`
//                              (reserved-but-inert per
//                               master rule #3; future
//                               FIELD-I.* sub-slice
//                               replaces this branch with
//                               a concrete procedural
//                               evaluator)
//
// `RR_HD inline` so the same evaluator runs on both host
// (audit-host tests) and device (future FIELD-I.5 /
// FIELD-I.6 kernel bridges).
RR_HD inline float evaluate(const ScalarFieldConfig& c,
                            rr::math::Vec3 position) {
    if (!c.enabled || c.strength == 0.0f) return 0.0f;
    switch (c.kind) {
        case ScalarFieldKind::Constant:
            return c.strength * c.constant_value;
        case ScalarFieldKind::Radial: {
            // Defence-in-depth: degenerate envelope returns 0.
            if (!(c.max_radius > c.min_radius)) return 0.0f;
            const rr::math::Vec3 delta{position.x - c.center.x,
                                       position.y - c.center.y,
                                       position.z - c.center.z};
            const float r2 = delta.x*delta.x + delta.y*delta.y + delta.z*delta.z;
            // Avoid sqrt-of-zero edge cases by checking the
            // squared distance against min_radius^2 first.
            const float min2 = c.min_radius * c.min_radius;
            if (r2 <= min2) return c.strength * c.min_value;
            const float max2 = c.max_radius * c.max_radius;
            if (r2 >= max2) return c.strength * c.max_value;
            // In-band: compute the radial position once and
            // apply the falloff exponent.
            const float r = sqrtf(r2);
            const float t_lin = (r - c.min_radius)
                              / (c.max_radius - c.min_radius);
            // Clamp falloff to a safe positive value per the
            // doc comment.
            const float falloff_safe = (c.falloff > 0.0f)
                ? c.falloff : 1.0f;
            // pow(t, falloff_safe) reshapes the transition.
            // Floor t at 0 to handle FP rounding above (r2
            // > min2 but r < min_radius numerically).
            const float t_pow = (t_lin <= 0.0f)
                ? 0.0f
                : ((falloff_safe == 1.0f) ? t_lin
                                          : powf(t_lin, falloff_safe));
            // smoothstep(0, 1, t_pow).
            const float s = scalar_field_smoothstep(0.0f, 1.0f, t_pow);
            return c.strength
                 * (c.min_value + s * (c.max_value - c.min_value));
        }
        case ScalarFieldKind::ProceduralPlaceholder:
            // Reserved-but-inert per master rule #3 (no
            // fake stubs); future FIELD-I.* sub-slice will
            // replace this branch with a concrete
            // procedural evaluator.
            return 0.0f;
    }
    return 0.0f;
}

// Evaluate the tagged-union scalar-field config at a
// spacetime event. The time component is intentionally
// ignored at this slice — the three Phase 1 kinds
// (Constant / Radial / ProceduralPlaceholder) are all
// spatially-dependent only. The overload exists so future
// time-dependent field kinds (4D-evolved textures, etc.)
// can land without widening every call site.
RR_HD inline float evaluate(const ScalarFieldConfig& c,
                            rr::math::Vec4 event) {
    return evaluate(c, rr::math::Vec3{event.y, event.z, event.w});
}

// Returns the default disabled scalar-field config
// (`enabled = false`, `strength = 0`, `kind = Constant`,
// all parameters at their natural no-op defaults). The
// safe anchor: `evaluate(disabled_scalar_field_config(),
// position)` returns `0.0f` at every position. Matches the
// factory convention of the other field-layer types.
RR_HD inline ScalarFieldConfig disabled_scalar_field_config() {
    return ScalarFieldConfig{};
}

}

#pragma once

// Field-mapping descriptor (see
// `docs/FIELD_INTERPRETATION_LAYER.md` §4 and §6). Wires a
// field-type input to one or more renderer output channels
// with per-target strengths.
//
// FIELD.3 promotes the FIELD.1 single-channel form
// (`output_channel` + single `strength`) to the multi-channel
// mapping POD: a single `FieldMapping` can contribute to
// color / emission / distortion / density / diagnostic AOV
// simultaneously, each with its own strength scalar. The
// FIELD.1 single-channel surface is removed; per-target
// strengths replace it.
//
// What lives here this slice (FIELD.3)
// ------------------------------------
// - `FieldOutputChannel` enum preserved from FIELD.1: the
//   six entries match design-doc §4.1–§4.6 exactly and
//   document the channel namespace.
// - `FieldMapping` POD with the five per-target strength
//   fields (matching the task brief's
//   `colorMultiplier` / `emission` / `distortionStrength` /
//   `alphaDensity` / `diagnosticAOV`), an `input_type`
//   field selecting which `FieldType` the mapping consumes,
//   and a per-mapping `output_clamp`. Default is the safe
//   no-op (all strengths zero, clamp 1, scalar input).
// - `target_strength(mapping, channel)` accessor that
//   dispatches a `FieldOutputChannel` to the matching POD
//   field. ChromaticShift returns `0.0f` — no per-target
//   field exists for it this slice (master rule #3: the
//   enum slot is reserved but no kernel consumes it yet).
// - `disabled_field_mapping()` factory.
//
// What does NOT live here this slice
// ----------------------------------
// - **No actual shading change.** Data model only; no
//   interpretation kernel consumes these fields yet, no AOV
//   is written, no beauty-pass modulation runs. Master
//   rule #3 holds: the per-target fields are real data the
//   future kernels will read; they do not pretend to drive
//   behaviour that has not landed.
// - **No chromatic-shift per-target strength.** The §4.5
//   channel is reserved by the enum but has no
//   `chromatic_shift_strength` field this slice. A future
//   slice that introduces a chromatic-shift kernel may
//   widen this POD without breaking the existing
//   per-target API.
// - **No renderer integration.** `rr_field` still has no
//   consumer outside its own headers; the per-mapping
//   `output_clamp` is honoured by future renderer-side
//   composition code, not by the data POD itself.
// - **No per-channel composition policy.** The renderer's
//   existing AOV / accumulation surface
//   (`src/renderer/AOV.h`, `GpuAOVBuffer`) is still the
//   natural target; this slice does not ship parallel
//   composition machinery.

#include "field/FieldType.h"
#include "math/MathUtils.h"  // RR_HD

namespace rr::field {

// Identifies a renderer output channel. The six entries
// match `docs/FIELD_INTERPRETATION_LAYER.md` §4.1–§4.6
// exactly. `ChromaticShift` is preserved as a documented
// future slot; the FIELD.3 `FieldMapping` POD only carries
// strengths for the five mapping targets the task brief
// names.
enum class FieldOutputChannel {
    Color           = 0,  // §4.1, beauty-pass chromatic modulation.
    Emission,             // §4.2, additive luminance along the ray.
    Distortion,           // §4.3, small clamped ray-direction perturbation.
    Density,              // §4.4, Beer-Lambert optical-depth contribution.
    ChromaticShift,       // §4.5, Doppler-like spectral remap (no per-target strength this slice).
    DiagnosticAOV,        // §4.6, dedicated AOV pass; no beauty modification.
};

// Per-mapping descriptor. Carries the input field-type and
// per-target strengths for the five renderer output channels
// a single mapping can contribute to. Setting multiple
// non-zero strengths makes a mapping contribute to multiple
// channels simultaneously; the default state (all strengths
// `0.0f`) is the safe no-op.
//
// Mapping-target ↔ design-doc channel ↔ task-brief name:
//
//   - `color_multiplier`     §4.1 `Color`          ↔ `colorMultiplier`
//   - `emission`             §4.2 `Emission`       ↔ `emission`
//   - `distortion_strength`  §4.3 `Distortion`     ↔ `distortionStrength`
//   - `alpha_density`        §4.4 `Density`        ↔ `alphaDensity`
//   - `diagnostic_aov`       §4.6 `DiagnosticAOV`  ↔ `diagnosticAOV`
//
// (The §4.5 `ChromaticShift` channel has no per-target field;
//  the enum slot is preserved for future slices.)
//
// `output_clamp` is the per-mapping clamp applied per channel
// after composition. Default `1.0f`. A mapping can cap its
// own contribution without depending on the renderer's
// global AOV clamp.
struct FieldMapping {
    FieldType input_type = FieldType::Scalar;

    float color_multiplier    = 0.0f;
    float emission            = 0.0f;
    float distortion_strength = 0.0f;
    float alpha_density       = 0.0f;
    float diagnostic_aov      = 0.0f;

    float output_clamp        = 1.0f;
};

// Returns the per-target strength of a `FieldMapping` for the
// given `FieldOutputChannel`. The five mapping targets return
// their matching POD fields; `ChromaticShift` returns `0.0f`
// — no per-target field exists for it this slice.
RR_HD inline float target_strength(const FieldMapping& m,
                                   FieldOutputChannel target) {
    switch (target) {
        case FieldOutputChannel::Color:          return m.color_multiplier;
        case FieldOutputChannel::Emission:       return m.emission;
        case FieldOutputChannel::Distortion:     return m.distortion_strength;
        case FieldOutputChannel::Density:        return m.alpha_density;
        case FieldOutputChannel::ChromaticShift: return 0.0f;
        case FieldOutputChannel::DiagnosticAOV:  return m.diagnostic_aov;
    }
    return 0.0f;
}

// Returns the default no-op mapping (all per-target
// strengths `0.0f`, clamp `1.0f`, scalar input). Matches the
// factory convention of the other field-layer types.
RR_HD inline FieldMapping disabled_field_mapping() {
    return FieldMapping{};
}

// --------------------------------------------------------------
// FIELD-I.4 — single-target tagged-form mapping config
// --------------------------------------------------------------
//
// `FieldMappingConfig` is the FIELD-I.* arc's operator-
// authoring surface for mapping a single scalar-field sample
// into a single output channel. Parallel to FIELD-I.2's
// `ScalarFieldConfig` tagged-union form on `ScalarField.h`:
//
//   - `ScalarFieldConfig` answers "what is the field's value at
//      this position?" (single field, single kind via
//      `ScalarFieldKind`).
//   - `FieldMappingConfig` answers "how does that value
//      contribute to a single render channel?" (single target
//      via `FieldMappingTarget` + shaping parameters).
//
// The Phase 1 pipeline composes them:
//
//     sample  = evaluate(scalar_field_config, position);
//     contrib = evaluate_mapping(mapping_config, sample);
//     renderer writes `contrib` to the channel identified by
//     `mapping_config.target`.
//
// Distinct from the FIELD.3 `FieldMapping` POD above. The
// FIELD.3 form carries five per-channel strengths for a
// multi-channel mapping (one `FieldMapping` → contributions to
// color / emission / distortion / density / diagnostic AOV
// simultaneously). The FIELD-I.4 form is single-target with
// richer shaping parameters (strength + bias + clamp range +
// clamp toggle). Both coexist; the FIELD-I.4 form is the
// FIELD-I.* arc's authoring + kernel-bridge surface; the
// FIELD.3 form is preserved for future multi-channel
// authoring use cases.
//
// Default state is the documented no-op anchor:
// `disabled_field_mapping_config()` returns
// `FieldMappingConfig{}` byte-for-byte; every
// `evaluate_mapping(...)` call site returns `0.0f` because
// `target = None` short-circuits the evaluator.

// Identifies the single render channel a `FieldMappingConfig`
// contributes to. Four values:
//
//   - `None`             — default no-op anchor. The
//                          evaluator returns `0.0f`
//                          regardless of any other parameter.
//                          A default-constructed
//                          `FieldMappingConfig{}` carries this
//                          target so default authoring is a
//                          guaranteed no-op.
//   - `ColorMultiplier`  — multiplicatively modulates the
//                          beauty-pass per-pixel color (design-
//                          doc §4.1; matches the FIELD.3
//                          `Color` enumerator).
//   - `Emission`         — additively contributes to the
//                          beauty-pass per-pixel emission
//                          (design-doc §4.2; matches the
//                          FIELD.3 `Emission` enumerator).
//   - `DiagnosticAOV`    — writes the mapped value to a
//                          dedicated AOV pass; no beauty-pass
//                          modulation (design-doc §4.6;
//                          matches the FIELD.3 `DiagnosticAOV`
//                          enumerator).
//
// The other FIELD.3 channels (`Distortion`, `Density`,
// `ChromaticShift`) are intentionally NOT exposed here per
// the FIELD-I.1 plan §2.3: Phase 1 ships only the three
// target channels above. Future FIELD-I.* sub-slices may
// widen this enum without breaking the default-no-op anchor.
enum class FieldMappingTarget {
    None            = 0,
    ColorMultiplier,
    Emission,
    DiagnosticAOV,
};

// Single-target tagged-form mapping config. Carries the
// active target selector + the five shaping parameters from
// the FIELD-I.4 task brief.
//
// Fields
// ------
//   - `target`        active target selector. Default `None`
//                     (no-op anchor; `evaluate_mapping(...)`
//                     returns `0.0f` regardless of every other
//                     field).
//   - `strength`      multiplicative scale applied to the raw
//                     scalar sample. Default `0.0f` so even
//                     `target != None` produces `bias` (or
//                     clamped `bias`) at the output until the
//                     artist explicitly dials strength up
//                     ("wired but quiet" anchor).
//   - `bias`          additive offset applied after the
//                     strength multiplication. Default `0.0f`.
//                     Useful for re-centering a `[-1, 1]`
//                     procedural sample around a positive
//                     emission baseline, or for offsetting a
//                     color multiplier away from neutral.
//   - `min_value`     lower clamp bound. Default `0.0f`. Only
//                     consulted when `clamp_output = true`.
//   - `max_value`     upper clamp bound. Default `1.0f`. Only
//                     consulted when `clamp_output = true`.
//                     Required `max_value >= min_value` for a
//                     non-degenerate clamp range; the
//                     evaluator returns `min_value` on
//                     degenerate configs
//                     (`max_value < min_value`) as defence-in-
//                     depth against artist authoring errors.
//   - `clamp_output`  master toggle for the clamp stage.
//                     Default `false` (the strength + bias
//                     shaping is enough for the common
//                     authoring case; clamping is opt-in for
//                     authors who need a hard output range
//                     guarantee).
//
// The evaluator pipeline (when `target != None`):
//
//     mapped = strength * sample + bias;
//     if (clamp_output) {
//         mapped = clamp(mapped, min_value, max_value);
//     }
//     return mapped;
//
// When `target == None`, the evaluator short-circuits and
// returns `0.0f` regardless of any other field — this is the
// documented no-op anchor.
struct FieldMappingConfig {
    FieldMappingTarget target       = FieldMappingTarget::None;
    float              strength     = 0.0f;
    float              bias         = 0.0f;
    float              min_value    = 0.0f;
    float              max_value    = 1.0f;
    bool               clamp_output = false;
};

// Apply a `FieldMappingConfig` to a raw scalar-field sample
// and return the per-channel contribution. The evaluator
// pipeline is documented on the `FieldMappingConfig` struct
// above. When `target == None` (the default), returns `0.0f`
// regardless of any other field.
RR_HD inline float evaluate_mapping(const FieldMappingConfig& m,
                                    float sample) {
    if (m.target == FieldMappingTarget::None) {
        return 0.0f;
    }
    float mapped = m.strength * sample + m.bias;
    if (m.clamp_output) {
        // Defence-in-depth: degenerate range (max < min)
        // collapses to `min_value` so the clamp is well-
        // defined even on inverted artist input.
        const float lo = m.min_value;
        const float hi = m.max_value >= m.min_value
                             ? m.max_value
                             : m.min_value;
        if (mapped < lo) mapped = lo;
        if (mapped > hi) mapped = hi;
    }
    return mapped;
}

// Returns the default no-op mapping config
// (`FieldMappingConfig{}` byte-for-byte). Matches the factory
// convention of the other FIELD-I.* tagged-form configs
// (`disabled_scalar_field_config()` at `ScalarField.h`).
RR_HD inline FieldMappingConfig disabled_field_mapping_config() {
    return FieldMappingConfig{};
}

}

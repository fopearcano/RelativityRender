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

}

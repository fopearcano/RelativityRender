#pragma once

// Field-mapping descriptor (see
// `docs/FIELD_INTERPRETATION_LAYER.md` §4). Ties a field-type
// input (the sampler shape from `FieldType.h`) to one of the
// renderer's output channels (color / emission / distortion /
// density / chromatic shift / diagnostic AOV), with an
// artist-facing strength scalar and an output clamp.
//
// What lives here this slice (FIELD.1)
// ------------------------------------
// - `FieldOutputChannel` enum naming the six output channels
//   the design doc §4 reserves.
// - `FieldMapping` POD wiring `input_type` -> `output_channel`
//   with a `strength` scalar and `output_clamp`. Default
//   value is "scalar -> diagnostic AOV, strength 0, clamp 1.0"
//   - the safest no-op mapping: even when an interpretation
//   module references it, strength 0 emits no contribution
//   into any beauty channel.
//
// What does NOT live here this slice
// ----------------------------------
// - **No interpretation kernel.** The function that takes a
//   sampled field value and returns the per-channel
//   contribution lives on a `FieldInterpreter` instance (see
//   `field/FieldInterpreter.h`), not on the mapping POD.
// - **No per-channel composition logic.** The renderer's
//   existing AOV / accumulation surface (`src/renderer/AOV.h`,
//   `GpuAOVBuffer`) is the natural target; Phase 1 does not
//   ship parallel machinery.
// - **No CLI / scene-file integration.** This slice is the
//   data model only; CLI plumbing and `.rrscene` field-
//   mapping entries land with FIELD.3+ per the design doc
//   §9 milestone order.

#include "field/FieldType.h"
#include "math/MathUtils.h"  // RR_HD

namespace rr::field {

// Identifies which renderer output channel a Phase 1 module
// emits its contribution into. The six entries match
// `docs/FIELD_INTERPRETATION_LAYER.md` §4.1-§4.6 exactly.
enum class FieldOutputChannel {
    Color           = 0,  // §4.1, beauty-pass chromatic modulation.
    Emission,             // §4.2, additive luminance along the ray.
    Distortion,           // §4.3, small clamped ray-direction perturbation.
    Density,              // §4.4, Beer-Lambert optical-depth contribution.
    ChromaticShift,       // §4.5, Doppler-like spectral remap.
    DiagnosticAOV,        // §4.6, dedicated AOV pass; no beauty modification.
};

// Per-mapping descriptor. Describes:
//
//   - which field-sampler shape the module reads
//     (`input_type`),
//   - which output channel it emits into (`output_channel`),
//   - the artist-facing strength scalar (`strength`),
//   - a per-output clamp (`output_clamp`) so an individual
//     mapping can cap its contribution without depending on
//     the renderer's global AOV clamp.
//
// Default value is the safe no-op:
//
//   `input_type     = FieldType::Scalar`
//   `output_channel = FieldOutputChannel::DiagnosticAOV`
//   `strength       = 0.0f`
//   `output_clamp   = 1.0f`
//
// Diagnostic AOV is the safest default channel because it
// never touches the beauty pass (design-doc §4.6); strength
// `0.0f` means no contribution even when the artist points
// a mapping at the diagnostic surface.
struct FieldMapping {
    FieldType          input_type     = FieldType::Scalar;
    FieldOutputChannel output_channel = FieldOutputChannel::DiagnosticAOV;
    float              strength       = 0.0f;
    float              output_clamp   = 1.0f;
};

// Returns the default no-op mapping. Matches the factory
// convention of the manifold modules.
RR_HD inline FieldMapping disabled_field_mapping() {
    return FieldMapping{};
}

}

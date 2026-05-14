#pragma once

// Field-type identity tag for the Field Interpretation Layer
// (see `docs/FIELD_INTERPRETATION_LAYER.md` §3). Names the
// kinds of non-light field a Phase 1 interpretation module is
// allowed to consume.
//
// Only `Scalar` has a concrete sampler shipped today (see
// `field/ScalarField.h`). The other entries reserve the slots
// the design doc enumerates so future slices can add their
// samplers without changing the tag enum's ABI; selecting an
// entry without a concrete sampler is documented as
// "no-output-this-slice" (master rule #3 - no stubs pretending
// to be complete systems).

namespace rr::field {

enum class FieldType {
    Scalar                         = 0,
    Vector,                              // reserved; no sampler this slice.
    Tensor,                              // reserved; no sampler this slice.
    Curvature,                           // reserved; no sampler this slice.
    ProbabilityAmplitudePlaceholder,     // reserved; no sampler this slice.
};

}

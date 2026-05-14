#pragma once

// Field-interpretation module descriptor (see
// `docs/FIELD_INTERPRETATION_LAYER.md` §6). The Phase 1
// counterpart of the chart-aware seam in the Manifold Core:
// a per-render configuration record that tells the renderer
// "consume this field sampler, route it through this
// mapping, emit it into this channel at this strength" -
// without committing to a kernel implementation at this
// slice.
//
// What lives here this slice (FIELD.1)
// ------------------------------------
// - The `FieldInterpreter` POD: a metadata record carrying
//   the module's identity (`name`), enabled state, the
//   mapping it implements, and an artist-controlled
//   `strength` override.
// - `disabled_field_interpreter()` factory.
//
// What does NOT live here this slice
// ----------------------------------
// - **No interpretation kernel.** The per-sample `f` that
//   maps a field value into an output contribution (design-
//   doc §4.2's `L_field(x_step) = κ_emission · f(...)`)
//   is a future per-module decision; the skeleton ships only
//   the metadata record.
// - **No `ManifoldTransform` consumption.** A real
//   interpretation kernel will eventually read the Manifold
//   Core's published surface (chart / metric / observer
//   frame / geodesic samples - design-doc §5.1); the
//   skeleton does not include the manifold headers so that
//   `rr_field` stays a leaf library this slice and so the
//   first real consumer slice can land the dep along with
//   the kernel that needs it.
// - **No renderer integration.** The renderer's existing
//   AOV / path-tracer / shading pipeline does not consume a
//   `FieldInterpreter` this slice.
// - **No region predicates.** The optional chart-region
//   gate the design doc §6.1 mentions (e.g. "only fire
//   inside `r < 10`") lands with the slice that introduces
//   a chart-region descriptor on the Manifold Core.

#include "field/FieldMapping.h"
#include "math/MathUtils.h"  // RR_HD

namespace rr::field {

// Per-render metadata for a single Phase 1 interpretation
// module. Default value is the safe no-op: a disabled module
// with the disabled field mapping, named "disabled". An
// artist enables a module by flipping `enabled = true`,
// pointing the mapping at the desired channel / strength /
// clamp, and (eventually) wiring a kernel via a separate
// slice.
//
// Fields
// ------
//   - `name`     human-readable module identifier. Stored as
//                `const char*` so the struct stays trivially
//                copyable; callers pass string literals with
//                static storage duration. Default
//                `"disabled"` makes the no-op default
//                self-describing in AOV / log output.
//   - `enabled`  master switch. `false` (default) means the
//                module is bypassed entirely; the renderer
//                emits no contribution from it regardless of
//                the rest of the struct.
//   - `mapping`  the `FieldMapping` the module implements
//                (input field type -> output channel +
//                strength + clamp). Default is
//                `disabled_field_mapping()` (strength 0,
//                diagnostic AOV).
//   - `strength` artist-controlled strength override
//                multiplied into `mapping.strength` at
//                evaluation time. Default `0.0f` so the
//                module emits no contribution even when
//                `enabled = true`. Lets future scene-file
//                authors keep a module "wired but quiet"
//                until they explicitly turn it up.
//
// The composition `effective_strength = mapping.strength *
// strength` is the renderer's per-sample scaling; both must
// be non-zero for the module to contribute anything.
struct FieldInterpreter {
    const char*  name     = "disabled";
    bool         enabled  = false;
    FieldMapping mapping  = {};
    float        strength = 0.0f;
};

// Returns the default disabled interpretation module
// (`FieldInterpreter{}`). Matches the factory convention of
// the manifold modules (`disabled_manifold_mode()`, etc.).
RR_HD inline FieldInterpreter disabled_field_interpreter() {
    return FieldInterpreter{};
}

// Returns the artist-facing effective strength of a Phase 1
// module: the product of the mapping's intrinsic strength and
// the module's artist override. Zero when either factor is
// zero or when the module is disabled. The renderer is
// expected to multiply the per-sample contribution by this
// value before writing it into the target AOV / beauty
// channel.
RR_HD inline float effective_strength(const FieldInterpreter& m) {
    return m.enabled ? (m.mapping.strength * m.strength) : 0.0f;
}

}

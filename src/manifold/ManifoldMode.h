#pragma once

// Manifold rendering mode configuration (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.5 / §4 / §5).
// Carries the artist-facing toggles the renderer reads per
// render to decide *how* the Manifold Core engages: whether it
// is engaged at all, which chart family is active, how strongly
// the chart's deformation influences the image, whether to
// overlay chart-level diagnostics, and which philosophical
// branch of the pivot is in force (Phase 2 coordinate-deformation
// per architecture-doc §4 vs Phase 1 field-perception per §5).
//
// History
// -------
// The name `ManifoldMode` was first used by the Manifold Core
// Skeleton slice for an enum of chart identities. MANIFOLD.1
// promoted that enum to `CoordinateChartType` (in
// `manifold/CoordinateChart.h`) and demoted this header to a
// `using ManifoldMode = CoordinateChartType;` compatibility
// alias. MANIFOLD.6 reuses the `ManifoldMode` name for its
// production purpose: the per-render config struct that
// composes a `CoordinateChartType` selection with the
// artist-facing toggles below.
//
// Callers that were reading the MANIFOLD.1 alias
// `ManifoldMode::Euclidean` should now read
// `CoordinateChartType::Euclidean` directly. No code path
// currently consumes either spelling, so the transition is
// mechanical; new code should use `CoordinateChartType` for the
// enum and `ManifoldMode` for the config struct.
//
// Status (MANIFOLD.6): data model only. No renderer code path
// reads any of these fields yet. The default value
// (`enabled = false`, `chart = Euclidean`, `strength = 0`, no
// debug overlay, both Phase-2 axioms set) reproduces today's
// renderer behaviour bit-for-bit.

#include "manifold/CoordinateChart.h"
#include "math/MathUtils.h"  // RR_HD

namespace rr::manifold {

// `ManifoldMode` is the per-render config that decides how the
// Manifold Core engages with the renderer. Every field is
// optional in spirit: setting `enabled = false` (the default)
// disables the entire Manifold Core surface, and the renderer
// falls back to its pre-pivot behaviour bit-for-bit regardless
// of what the other fields say.
//
// Field-by-field
// --------------
//   - `enabled`
//       Master switch. `false` (default) means the Manifold
//       Core is entirely disabled and the renderer behaves
//       exactly as it did before the pivot. `true` means the
//       renderer should consult the rest of this struct.
//
//   - `chart`
//       Which `CoordinateChartType` family is active. Only
//       `Euclidean` (default) has a concrete implementation;
//       the others are reserved-but-inert per architecture-doc
//       §4.2.
//
//   - `strength`
//       Interpolation factor between identity-rendering (`0`)
//       and full manifold-rendering (`1`). Lets an artist dial
//       down the chart's effect without lying about which
//       chart is selected. Default `0` means "no chart effect"
//       even when `enabled = true`. Values outside `[0, 1]` are
//       not clamped by this POD; the renderer is free to
//       extrapolate (e.g. for stylised over-deformation) at
//       the artist's risk.
//
//   - `debug_visualization`
//       Overlay chart-level diagnostics on the beauty pass
//       (chart boundaries, geodesic colouring, curvature
//       scalars, observer-frame markers). Default `false`.
//       The future implementation lives in the
//       Perceptual-Field-Interpretation sibling
//       (architecture-doc §6).
//
//   - `preserve_light_speed_normally`
//       Phase 2 axiom (architecture-doc §4.1): light propagates
//       at the chart-local speed `c` and the coordinate system
//       is what bends. Default `true`. Setting this to `false`
//       selects the Phase 1 stance where the renderer is
//       allowed to interpret a non-light field as the
//       chromatic / luminous source instead of the EM field
//       (architecture-doc §6.2).
//
//   - `transform_coordinates_instead_of_light`
//       Phase 2 axiom sibling (architecture-doc §4.1): deform
//       the coordinate chart rather than the light field
//       itself. Default `true`. Setting this AND
//       `preserve_light_speed_normally` to `false` together
//       selects the full Phase 1 field-perception stance
//       (architecture-doc §5).
//
// Default-constructed value
// -------------------------
// `enabled = false`, `chart = Euclidean`, `strength = 0`,
// `debug_visualization = false`, `preserve_light_speed_normally
// = true`, `transform_coordinates_instead_of_light = true`.
// This is the "preserves current output by default" anchor:
// the renderer must produce bit-identical output to the
// pre-pivot baseline when handed `ManifoldMode{}`.
struct ManifoldMode {
    bool                enabled                                = false;
    CoordinateChartType chart                                  = CoordinateChartType::Euclidean;
    float               strength                               = 0.0f;
    bool                debug_visualization                    = false;
    bool                preserve_light_speed_normally          = true;
    bool                transform_coordinates_instead_of_light = true;
};

// Returns the default disabled mode (`ManifoldMode{}`). This is
// the canonical "no output change" factory; matches the factory
// convention of the other manifold modules
// (`euclidean_chart()`, `minkowski_metric()`, `rest_frame()`,
// `default_geodesic_state()`, `identity_transform()`).
RR_HD inline ManifoldMode disabled_manifold_mode() {
    return ManifoldMode{};
}

}

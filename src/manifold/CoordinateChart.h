#pragma once

// Coordinate chart on the rendered manifold (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.1). A chart maps an
// open region of the rendered manifold to an open subset of R^n;
// the renderer picks one chart as the *active observation chart*
// per render. This header carries the chart's identity (which
// family), its geometric pose (origin / scale), the units one
// chart-coordinate unit corresponds to in scene space, and a
// future-use parameter bag for curved-space modes the Manifold
// Core has not yet implemented.
//
// Status: skeleton stage (MANIFOLD.1). No real GR math; no
// renderer integration; no behavioural change. Only the
// `Euclidean` chart family has a concrete meaning today; the
// `*Like` and `*LikePlaceholder` families are reserved slots
// per architecture-doc §4.2 and §8 (non-goals: "physically exact
// Kerr ray tracing", "full GR solver"). Selecting any entry
// other than `Euclidean` carries no defined behaviour beyond
// holding the tag.

#include "math/MathUtils.h"
#include "math/Vec3.h"

namespace rr::manifold {

// `CoordinateChartType` identifies which family of coordinate
// chart this descriptor configures.
//
// The conservative naming follows master rule #3 ("no fake stubs
// pretending to be complete systems") and architecture-doc §8:
//
//   - `*Like`            reserves a slot for a future closed-form
//                        metric inspired by the named spacetime
//                        but not certified against a reference
//                        geodesic solver and not "physically
//                        exact" per the renderer's non-goal list.
//   - `*LikePlaceholder` reserves a slot with no metric and no
//                        planned implementation beyond holding
//                        the tag.
//
// `Euclidean` is the only family with a concrete implementation
// today; it reproduces the renderer's pre-pivot behaviour on
// flat scenes bit-for-bit.
enum class CoordinateChartType {
    Euclidean              = 0,
    SchwarzschildLike,
    KruskalLikePlaceholder,
    PenroseLike,
    KerrLikePlaceholder,
};

// Units metadata for the chart's coordinate axes. Declares what
// `1` chart-coordinate unit means in scene units. The Manifold
// Core does not enforce unit consistency at this slice; this tag
// exists so future chart implementations can validate metric-
// scale assumptions and so AOVs / diagnostic output can label
// the active chart honestly.
enum class ChartUnits {
    SceneNatural        = 0,  // 1 chart unit == 1 scene unit; c = 1.
    Meters,                   // SI metres.
    SchwarzschildRadius,      // r_s = 2 G M / c^2 == 1 in chart units.
    Geometrized,              // c = G = 1.
};

// Placeholder parameter bag for the future curved-space chart
// modes (architecture-doc §4.2 reserved slots). Each field is a
// forward-looking slot; no current code path reads any of them.
// The slot exists so the next slice in the Manifold Core pivot
// does not have to widen the `CoordinateChart` ABI when it
// introduces a curved-chart implementation.
//
//   - `mass`                   canonical mass parameter for
//                              `SchwarzschildLike` and the M
//                              parameter of `KerrLikePlaceholder`;
//                              units are dictated by
//                              `CoordinateChart::units`.
//   - `spin`                   dimensionless Kerr spin
//                              `a = J / (M c)`, expected in
//                              `[0, 1)` for sub-extremal Kerr.
//                              Zero on a `KerrLikePlaceholder`
//                              chart reduces structurally to the
//                              spherically symmetric case.
//   - `compactification_scale` `PenroseLike`'s
//                              conformal-compactification
//                              parameter; controls how aggressively
//                              asymptotic infinity is compressed
//                              onto a finite boundary in chart
//                              coordinates. Default `1.0` is the
//                              identity compactification.
//   - `reserved`               future fourth slot (e.g.
//                              cosmological constant for
//                              de-Sitter-like charts, charge for
//                              Reissner-Nordström-like extensions).
//                              No defined meaning today.
//
// All numeric fields are zero-init by default. The one exception
// is `compactification_scale`, which defaults to `1.0` because
// zero is not a meaningful identity value for that one.
struct CoordinateChartParameters {
    float mass                   = 0.0f;
    float spin                   = 0.0f;
    float compactification_scale = 1.0f;
    float reserved               = 0.0f;
};

// Coordinate chart descriptor handed to the renderer. The default
// value is the Euclidean chart in scene-natural units at the
// scene origin with unit scale - the same "no behaviour change"
// degenerate case the Manifold Core Skeleton slice pinned.
//
//   - `type`   chart family (see `CoordinateChartType`).
//   - `name`   human-readable identifier carried into AOVs /
//              logs / diagnostic output. Stored as `const char*`
//              so the struct stays trivially copyable; callers
//              pass string literals with static storage duration.
//   - `scale`  size of one chart-coordinate unit in `units`. For
//              the Euclidean chart this is a pure scene-unit
//              multiplier (`1.0` is the identity); for future
//              curved charts this carries the metric's
//              characteristic length (e.g. the Schwarzschild
//              radius for `SchwarzschildLike`).
//   - `origin` chart origin in scene coordinates. The chart's
//              forward map sends this point to chart-space `0`.
//   - `units`  see `ChartUnits`.
//   - `params` placeholder parameters for future curved-space
//              modes (see `CoordinateChartParameters`).
//
// The chart's forward / inverse / domain helpers
// (architecture-doc §3.1) are NOT realised on this struct: the
// Euclidean chart's versions of all three are no-ops, and master
// rule #3 forbids stubbing the curved-chart versions before they
// have a real implementation. The helpers land with the future
// curved-chart slices that need them.
struct CoordinateChart {
    CoordinateChartType       type    = CoordinateChartType::Euclidean;
    const char*               name    = "euclidean";
    float                     scale   = 1.0f;
    rr::math::Vec3            origin  = {0.0f, 0.0f, 0.0f};
    ChartUnits                units   = ChartUnits::SceneNatural;
    CoordinateChartParameters params  = {};
};

// Returns the Euclidean chart - the degenerate case that
// reproduces today's renderer behaviour. Canonical helper.
RR_HD inline CoordinateChart euclidean_chart() {
    return CoordinateChart{};
}

// Backward-compat alias. The Manifold Core Skeleton slice
// exposed the degenerate-case factory under the older
// `identity_chart` spelling tied to `ManifoldMode::Identity`.
// MANIFOLD.1 renamed the enum to `CoordinateChartType::Euclidean`
// and the canonical helper to `euclidean_chart`; this alias is
// retained so any caller (or future test) written against the
// older name still compiles. Both helpers return the same
// default-constructed `CoordinateChart{}` value.
RR_HD inline CoordinateChart identity_chart() {
    return CoordinateChart{};
}

}

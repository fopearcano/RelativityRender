#pragma once

// Coordinate chart on the rendered manifold (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.1). A chart maps an
// open region of the manifold to an open subset of R^n; the scene
// may carry several charts covering overlapping regions and the
// renderer picks one as the *active observation chart* per render.
//
// This header ships the chart's *identity surface* only - the
// `ManifoldMode` tag the renderer uses for caching, AOV labelling,
// and chart-aware dispatch. The forward / inverse maps and the
// chart's domain predicate are part of the contract but are not
// realised here this slice: the only chart with a complete
// implementation today is `ManifoldMode::Identity`, whose forward
// and inverse maps are both the identity function and whose domain
// predicate is "always in". Those reduce to no-ops in code and
// therefore need no per-chart member function this slice.
//
// Later slices that introduce real curved charts will extend this
// type with chart-specific parameters (e.g. Schwarzschild mass) and
// will add the forward / inverse / domain helpers as `RR_HD inline`
// members or free functions tag-dispatched on `mode`.

#include "manifold/ManifoldMode.h"

namespace rr::manifold {

struct CoordinateChart {
    ManifoldMode mode = ManifoldMode::Identity;
};

// Returns the Identity chart - the degenerate case that reproduces
// today's renderer behaviour bit-for-bit on Minkowski scenes.
inline constexpr CoordinateChart identity_chart() {
    return CoordinateChart{ManifoldMode::Identity};
}

}

#pragma once

// The Manifold Core's chart-identity tag was originally named
// `ManifoldMode` during the Manifold Core Skeleton slice. The
// MANIFOLD.1 slice promoted the enum to `CoordinateChartType`
// (defined in `manifold/CoordinateChart.h`) with the more
// conservative "*Like" / "*LikePlaceholder" naming convention,
// matching master rule #3 ("no fake stubs pretending to be
// complete systems") and architecture-doc §8 non-goals.
//
// This header keeps the `ManifoldMode` name as a type alias of
// `CoordinateChartType` so any future code or test written
// against the older spelling still compiles. The canonical name
// for new code is `CoordinateChartType` and the canonical home
// for the enum is `manifold/CoordinateChart.h`. Note that the
// enum's value names also changed:
//
//   Identity         -> Euclidean
//   Schwarzschild    -> SchwarzschildLike
//   KruskalSzekeres  -> KruskalLikePlaceholder
//   Penrose          -> PenroseLikePlaceholder
//   Kerr             -> KerrLikePlaceholder
//
// Callers that previously read `ManifoldMode::Identity` must read
// `ManifoldMode::Euclidean` (the same enumerator via the alias).

#include "manifold/CoordinateChart.h"

namespace rr::manifold {

using ManifoldMode = CoordinateChartType;

}

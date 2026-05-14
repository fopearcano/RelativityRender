#pragma once

// Aggregate the renderer consumes to describe the active manifold
// state for one render (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.5, "spacetime
// deformation layer"). Bundles the active chart, the metric on
// that chart, and the observer frame on that chart into a single
// passive POD.
//
// A default-constructed `ManifoldTransform{}` describes the
// Identity chart with the flat Minkowski metric and the
// scene-rest observer frame - i.e. today's renderer behaviour
// expressed in the new ontology. This is the "no behaviour change"
// contract of the Manifold Core Skeleton slice: any code path that
// constructs the default value of this type recovers the existing
// renderer's interpretation.
//
// The architecture doc §3.5 describes additional state the
// spacetime deformation layer eventually owns: the set of charts
// on the scene, the metric associated with each chart, and the
// chart-transition rules. Those land with the chart-aware-seam
// slice and the first non-trivial chart (Schwarzschild); the
// aggregate stays a POD until then.

#include "manifold/CoordinateChart.h"
#include "manifold/MetricTensor.h"
#include "manifold/ObserverFrame.h"

namespace rr::manifold {

struct ManifoldTransform {
    CoordinateChart chart;
    MetricTensor    metric;
    ObserverFrame   observer;
};

// Returns the Identity / Minkowski / rest-frame transform. This is
// the manifold-core surface the existing renderer reproduces
// bit-for-bit; future slices will gain `schwarzschild_transform`,
// `kerr_transform`, etc.
inline ManifoldTransform identity_transform() {
    return ManifoldTransform{};
}

}

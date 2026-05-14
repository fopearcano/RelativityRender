#pragma once

// State of a null geodesic at a single affine-parameter value (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.4). The future
// geodesic integrator advances `(position, momentum)` step by step
// along the active metric; this header ships only the state POD
// the integrator will consume.
//
// On the Identity chart, `position` is the photon's chart-space
// event and `momentum` is its propagation direction; the integrator
// is the straight-line step `position += step * momentum` and
// `momentum` is passed through unchanged. On curved charts (future
// slices), `momentum` represents the spatial part of the photon's
// four-momentum in chart coordinates and the integrator updates it
// per the geodesic equation `dp^mu/dλ = -Γ^mu_{αβ} p^α p^β`.
//
// Termination / chart-boundary handling is expressed via
// `GeodesicStatus`; the architecture doc §3.4 lists the three
// states a per-step advance is allowed to return.

#include "math/Vec3.h"

namespace rr::manifold {

struct GeodesicState {
    rr::math::Vec3 position = {0.0f, 0.0f, 0.0f};
    rr::math::Vec3 momentum = {0.0f, 0.0f, 1.0f};
};

// Per-step advance result. `InFlight` keeps the integrator going;
// `ChartBoundary` signals that the geodesic left the active chart's
// domain and the spacetime deformation layer must hand off to
// another chart; `Terminated` signals geometry hit, scene-bound
// exit, or horizon-proxy crossing.
enum class GeodesicStatus {
    InFlight       = 0,
    ChartBoundary,
    Terminated,
};

}

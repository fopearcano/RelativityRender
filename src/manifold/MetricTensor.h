#pragma once

// Rank-(0,2) metric tensor at a single chart event (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.2). Components
// `g_{mu nu}` are stored as a flat 4x4 row-major array in
// mostly-plus signature `(-, +, +, +)`. The metric is what carries
// "spacetime deformation": a flat Minkowski metric reproduces
// today's renderer; a Schwarzschild / Kerr metric (future slices)
// is what makes the geodesic integrator bend rays.
//
// Christoffel symbols (architecture-doc §3.2) are NOT stored here
// this slice. They are a derived quantity computed by the future
// geodesic-integrator slice from `g[]` either analytically (for
// Schwarzschild / Kerr) or by finite-differencing (for arbitrary
// metrics). Pre-computing them on a per-event basis only makes
// sense once an integrator consumes them, so they land with the
// integrator.

#include "math/MathUtils.h"  // RR_HD

namespace rr::manifold {

struct MetricTensor {
    // Row-major 4x4. `g[4*i + j]` is `g_{i j}`. The default value
    // is the mostly-plus Minkowski metric `diag(-1, +1, +1, +1)`.
    float g[16] = {
        -1.0f,  0.0f,  0.0f,  0.0f,
         0.0f,  1.0f,  0.0f,  0.0f,
         0.0f,  0.0f,  1.0f,  0.0f,
         0.0f,  0.0f,  0.0f,  1.0f,
    };
};

// Returns the flat Minkowski metric in mostly-plus signature.
// The Identity chart's metric. The only metric the Manifold Core
// implements concretely this slice.
RR_HD inline MetricTensor minkowski_metric() {
    return MetricTensor{};
}

}

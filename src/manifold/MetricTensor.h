#pragma once

// Rank-(0,2) metric tensor at a single chart event (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.2). Components
// `g_{mu nu}` are stored as a flat 4x4 row-major array. The metric
// is what carries "spacetime deformation": a flat Minkowski metric
// reproduces today's renderer; a future Schwarzschild / Kerr
// metric is what will make the geodesic integrator bend rays.
//
// Signature convention
// --------------------
// RelativityRender uses the **mostly-plus** signature
// `(-, +, +, +)` everywhere - the time component (`g_{00}`) is
// negative and the three spatial components (`g_{11}`, `g_{22}`,
// `g_{33}`) are positive. This is the convention used in
// Misner-Thorne-Wheeler "Gravitation" and in most modern
// numerical-relativity codes; the alternative "mostly-minus"
// `(+, -, -, -)` convention common in particle-physics texts is
// NOT used here. The line element reads
//
//     ds^2 = g_{mu nu} dx^mu dx^nu
//
// with the flat Minkowski metric (Euclidean chart, scene rest)
//
//     g = diag(-1, +1, +1, +1).
//
// Under this convention:
//
//   - a *null* geodesic (photon worldline) satisfies
//         g_{mu nu} p^mu p^nu = 0;
//   - a *timelike* worldline (massive observer) satisfies
//         g_{mu nu} u^mu u^nu = -1
//     when `u^mu` is normalised to unit proper time;
//   - a *spacelike* separation gives `g_{mu nu} dx^mu dx^nu > 0`.
//
// Future curved-chart implementations must keep this signature;
// `is_minkowski` below is one of the cheap sanity gates that
// verifies they do.
//
// Christoffel symbols
// -------------------
// `Gamma^lambda_{mu nu}` are NOT stored on this POD. They are a
// derived quantity computed by the future geodesic integrator
// from `g[]`, either analytically (closed form for Schwarzschild
// / Kerr) or by finite-differencing (for arbitrary metrics).
// Pre-computing them on a per-event basis only makes sense once
// an integrator consumes them; they land with the integrator
// slice (architecture-doc §3.2 / §3.4).
//
// What lives here this slice (MANIFOLD.2)
// ---------------------------------------
// - The `MetricTensor` POD itself with row-major `g[16]` storage,
//   default-initialised to the mostly-plus Minkowski metric.
// - `at(i, j)` const + non-const accessors that paper over the
//   `4*i + j` index arithmetic.
// - `minkowski_metric()` factory - the Euclidean chart's metric.
// - `identity_metric()` factory - the 4D Euclidean identity
//   `diag(+1, +1, +1, +1)`. Provided as a starting point for
//   tests and future finite-difference probes; this is NOT a
//   physical spacetime metric (positive-definite signature).
// - Validation helpers: `is_symmetric`, `is_finite`,
//   `is_minkowski`, `is_diagonal`.
// - `determinant(t)`: closed-form 4x4 Laplace expansion. Used
//   today as a sanity scalar (degenerate metric detection) and
//   tomorrow as a building block for the future inverse-metric
//   helper.
//
// What does NOT live here this slice
// ----------------------------------
// - The inverse metric `g^{mu nu}` (next slice).
// - Christoffel symbols, the Riemann or Ricci tensor, the
//   Kretschmann scalar, the Einstein tensor, or any solver of
//   the Einstein field equations. Master rule #3 ("no fake stubs
//   pretending to be complete systems") and architecture-doc §8
//   non-goal "full GR solver" both rule those out for this slice.
// - Any rendering integration. The Manifold Core's chart-aware
//   seam into the path tracer (architecture-doc §10 step 2)
//   lands separately.

#include "math/MathUtils.h"  // RR_HD

#include <cmath>             // std::isfinite

namespace rr::manifold {

struct MetricTensor {
    // Row-major 4x4 storage. `g[4*i + j]` is `g_{i j}`. Default
    // value is the mostly-plus Minkowski metric
    // `diag(-1, +1, +1, +1)`. Symmetry (`g[4*i + j] ==
    // g[4*j + i]`) is an invariant; the renderer never produces
    // an asymmetric metric directly, and `is_symmetric` below is
    // the cheap sanity gate for artist-supplied or
    // finite-differenced metrics.
    float g[16] = {
        -1.0f,  0.0f,  0.0f,  0.0f,
         0.0f,  1.0f,  0.0f,  0.0f,
         0.0f,  0.0f,  1.0f,  0.0f,
         0.0f,  0.0f,  0.0f,  1.0f,
    };

    // Component accessor `g_{i j}`. The `4*i + j` index
    // arithmetic stays inside the struct so call sites can speak
    // in `(i, j)` and the underlying flat layout is a private
    // detail.
    RR_HD constexpr float  at(int i, int j) const { return g[4 * i + j]; }
    RR_HD constexpr float& at(int i, int j)       { return g[4 * i + j]; }
};

// Returns the flat mostly-plus Minkowski metric
// `diag(-1, +1, +1, +1)`. This is the metric on the Euclidean
// chart (`CoordinateChartType::Euclidean`) and the only metric
// the Manifold Core implements concretely today.
RR_HD inline MetricTensor minkowski_metric() {
    return MetricTensor{};  // default member init is mostly-plus Minkowski.
}

// Returns the 4D-Euclidean identity metric
// `diag(+1, +1, +1, +1)`. This is NOT a physical spacetime
// metric - its signature is positive-definite `(+, +, +, +)`
// rather than the Lorentzian `(-, +, +, +)` the renderer
// consumes - and the Manifold Core's geodesic integrator does
// NOT treat it as a physical metric. The factory exists as:
//
//   - a clean symmetric positive-definite baseline for tests
//     that probe `at(...)` / `determinant(...)` / `is_diagonal`,
//   - the starting point for future finite-difference probes
//     that need a known non-zero baseline.
//
// `determinant(identity_metric()) == +1.0` exactly.
RR_HD inline MetricTensor identity_metric() {
    return MetricTensor{
        {
             1.0f, 0.0f, 0.0f, 0.0f,
             0.0f, 1.0f, 0.0f, 0.0f,
             0.0f, 0.0f, 1.0f, 0.0f,
             0.0f, 0.0f, 0.0f, 1.0f,
        }
    };
}

// Returns `true` if `t` is symmetric to within `tolerance`, i.e.
// `|g_{i j} - g_{j i}| <= tolerance` for every `i < j`. Symmetry
// is a mandatory invariant of any rank-(0,2) metric tensor; this
// helper is the cheap sanity gate that future artist-supplied or
// finite-differenced metrics will run before being consumed.
RR_HD inline bool is_symmetric(const MetricTensor& t,
                               float tolerance = 1.0e-6f) {
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            const float diff     = t.at(i, j) - t.at(j, i);
            const float abs_diff = diff < 0.0f ? -diff : diff;
            if (abs_diff > tolerance) return false;
        }
    }
    return true;
}

// Returns `true` if every component of `t` is finite (no NaN, no
// `inf`). A non-finite metric will diverge a geodesic integrator;
// this helper is the cheap sanity gate the future Schwarzschild /
// Kerr metrics will run at horizon-adjacent events.
RR_HD inline bool is_finite(const MetricTensor& t) {
    for (int k = 0; k < 16; ++k) {
        if (!std::isfinite(t.g[k])) return false;
    }
    return true;
}

// Returns `true` if `t` is component-wise within `tolerance` of
// the canonical mostly-plus Minkowski metric
// `diag(-1, +1, +1, +1)`. Useful for tests that verify the
// Euclidean chart's metric reduces to the textbook flat metric
// and for regressions that pin "no behaviour change" slices.
RR_HD inline bool is_minkowski(const MetricTensor& t,
                               float tolerance = 1.0e-6f) {
    const MetricTensor m = minkowski_metric();
    for (int k = 0; k < 16; ++k) {
        const float diff     = t.g[k] - m.g[k];
        const float abs_diff = diff < 0.0f ? -diff : diff;
        if (abs_diff > tolerance) return false;
    }
    return true;
}

// Returns `true` if every off-diagonal component of `t` is within
// `tolerance` of zero. Many physically interesting metrics
// (Schwarzschild in standard coordinates, Kerr in
// Boyer-Lindquist, FLRW in synchronous coordinates) are
// diagonal; this helper is the natural fast-path gate for future
// integrators that can exploit that structure.
RR_HD inline bool is_diagonal(const MetricTensor& t,
                              float tolerance = 1.0e-6f) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (i == j) continue;
            const float v     = t.at(i, j);
            const float abs_v = v < 0.0f ? -v : v;
            if (abs_v > tolerance) return false;
        }
    }
    return true;
}

// 4x4 determinant via Laplace cofactor expansion along the first
// row. Closed-form arithmetic, no GR machinery. Used today as:
//
//   - a sanity scalar (a near-zero determinant signals a
//     degenerate / singular metric, e.g. a coordinate singularity
//     a curved chart has crossed),
//   - a regression anchor (the Minkowski metric returns exactly
//     `-1.0` and the 4D-Euclidean identity metric returns exactly
//     `+1.0`).
//
// Exposed so future slices that need it - notably the
// inverse-metric helper (next slice) and any volume-element
// computation `sqrt(-det g)` - have a stable surface. The
// Manifold Core's current code paths do NOT consume this value.
//
// Implementation: ~24 multiplications + ~12 additions per the
// standard Laplace expansion. Single-precision throughout; for
// high-curvature regimes a future double-precision overload may
// land alongside the inverse-metric slice.
RR_HD inline float determinant(const MetricTensor& t) {
    const float g00 = t.g[ 0], g01 = t.g[ 1], g02 = t.g[ 2], g03 = t.g[ 3];
    const float g10 = t.g[ 4], g11 = t.g[ 5], g12 = t.g[ 6], g13 = t.g[ 7];
    const float g20 = t.g[ 8], g21 = t.g[ 9], g22 = t.g[10], g23 = t.g[11];
    const float g30 = t.g[12], g31 = t.g[13], g32 = t.g[14], g33 = t.g[15];

    // 3x3 minors of the first row (cofactors with the (-1)^{i+j}
    // sign folded into the return statement below).
    const float c00 = g11 * (g22 * g33 - g23 * g32)
                    - g12 * (g21 * g33 - g23 * g31)
                    + g13 * (g21 * g32 - g22 * g31);
    const float c01 = g10 * (g22 * g33 - g23 * g32)
                    - g12 * (g20 * g33 - g23 * g30)
                    + g13 * (g20 * g32 - g22 * g30);
    const float c02 = g10 * (g21 * g33 - g23 * g31)
                    - g11 * (g20 * g33 - g23 * g30)
                    + g13 * (g20 * g31 - g21 * g30);
    const float c03 = g10 * (g21 * g32 - g22 * g31)
                    - g11 * (g20 * g32 - g22 * g30)
                    + g12 * (g20 * g31 - g21 * g30);

    return g00 * c00 - g01 * c01 + g02 * c02 - g03 * c03;
}

}

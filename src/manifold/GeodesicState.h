#pragma once

// State of a geodesic at a single affine-parameter value (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.4). The future
// geodesic integrator advances `(position4, momentum4)` step by
// step along the active metric; per-step accounting (affine
// parameter accumulation, optical-depth integration, diagnostic
// curvature sampling) accumulates on the same POD; the per-step
// `GeodesicStatus` enum below describes the integrator's return
// value.
//
// What lives here this slice (MANIFOLD.4)
// ---------------------------------------
// - The `GeodesicState` POD with six fields: `position4`,
//   `momentum4`, `affine_parameter`, `valid`,
//   `accumulated_optical_depth`, `diagnostic_curvature`. Each
//   field is documented in-place; the time / depth / curvature
//   placeholders are zero-initialised since no integrator
//   advances them yet.
// - The `GeodesicStatus` enum, preserved unchanged from the
//   Manifold Core Skeleton slice. Describes the integrator's
//   per-step return value, *not* a struct field.
// - A `default_geodesic_state()` factory that returns the
//   shipping default - a unit-energy photon at the chart origin
//   propagating along +z, satisfying the null condition on the
//   Euclidean chart's Minkowski metric.
//
// What does NOT live here this slice
// ----------------------------------
// - **No integrator.** Master rule #3 ("no fake stubs pretending
//   to be complete systems"). Stepping happens in the future
//   chart-aware-seam slice (architecture-doc §10 step 2).
// - **No null / timelike validators.** Those depend on a metric;
//   they belong on the integrator's per-step entry point, not on
//   the data POD. The natural analogue of
//   `is_normalised_timelike(ObserverFrame, MetricTensor)`
//   (MANIFOLD.3) for null geodesics will land alongside the
//   integrator.
// - **No optical-depth or curvature evaluation.** The two
//   placeholder fields are storage-only; no current code path
//   reads or writes them.
// - **No renderer integration.** Nothing in `src/cuda/`,
//   `src/optix/`, `src/pathtracer/`, `src/renderer/`, or any
//   other module consumes `GeodesicState` this slice.
//
// Convention reminder
// -------------------
// Mostly-plus signature `(-, +, +, +)` (see `MetricTensor.h`).
// Under this convention:
//
//   - photons satisfy `g_{mu nu} p^mu p^nu = 0` (null);
//   - massive particles satisfy `g_{mu nu} p^mu p^nu = -m^2`
//     (timelike, with `p^mu = m * u^mu` and `u^mu` the
//     four-velocity normalised to unit proper time).

#include "math/MathUtils.h"
#include "math/Vec4.h"

namespace rr::manifold {

struct GeodesicState {
    // 4D event in chart coordinates. Components are
    // `(x^0, x^1, x^2, x^3)`; for the Euclidean chart this reads
    // `(t, x, y, z)` in scene-natural units (c = 1). Default is
    // the chart-space origin.
    rr::math::Vec4 position4 = {0.0f, 0.0f, 0.0f, 0.0f};

    // 4-momentum `p^mu` in chart coordinates. For a photon
    // (null geodesic) `p^mu` satisfies `g_{mu nu} p^mu p^nu = 0`;
    // on the Euclidean chart's mostly-plus Minkowski metric this
    // reads `-E^2 + |p|^2 = 0`, i.e. `E = |p|`. The default is
    // the unit-energy +z photon `(E, p_x, p_y, p_z) = (1, 0, 0, 1)`
    // which satisfies the null condition exactly.
    //
    // For timelike geodesics (massive observer worldlines)
    // `p^mu = m * u^mu` satisfies `g_{mu nu} p^mu p^nu = -m^2`.
    // The data POD does not enforce the null/timelike
    // condition; future integrator-side validators will gate
    // it.
    rr::math::Vec4 momentum4 = {1.0f, 0.0f, 0.0f, 1.0f};

    // Affine parameter `lambda` along the geodesic. For a null
    // geodesic `lambda` is the integrator's parameter (not
    // proper time - proper time is zero everywhere on a null
    // worldline). For a timelike geodesic `lambda` is the
    // proper time `tau` (up to scale). Cumulative since the
    // integrator's reference event. Placeholder this slice; no
    // integrator advances it.
    float affine_parameter = 0.0f;

    // Validity flag. `true` if the state is internally
    // consistent and the integrator may advance it; `false` if
    // the integrator has rejected the state as unrecoverable
    // (e.g. crossed a coordinate singularity the active chart
    // cannot resolve, accumulated NaN, or violated the
    // chart-specific physical constraints by more than the
    // integrator's tolerance). Independent of `GeodesicStatus`:
    // a state with `valid = false` may still carry useful
    // diagnostic information for the renderer to log.
    //
    // The default is `true` because a freshly-constructed POD
    // is well-formed (the default `momentum4` satisfies the
    // null condition on the Euclidean chart's metric); the
    // integrator clears the flag.
    bool valid = true;

    // Accumulated optical depth `tau_opt` along the geodesic
    // since the integrator's reference event. Future
    // volumetric / atmospheric rendering will integrate
    //     d(tau_opt) / d(lambda) = sigma_a(x(lambda))
    // along the geodesic, and the renderer evaluates the
    // Beer-Lambert attenuation `I = I_0 * exp(-tau_opt)` at the
    // eye. Placeholder this slice; no integrator advances it,
    // and no `sigma_a` field exists yet (that lives on the
    // future volume / medium descriptor, not here).
    float accumulated_optical_depth = 0.0f;

    // Diagnostic curvature scalar at the current event - a
    // chart-dependent invariant the future integrator may
    // record per step for AOV output / debugging. The expected
    // population is the Kretschmann scalar
    //   `K = R_{mu nu rho sigma} R^{mu nu rho sigma}`
    // (positive, finite away from singularities, divergent at
    // physical singularities) or the Ricci scalar `R`. Zero on
    // the Euclidean chart everywhere. Placeholder this slice;
    // no integrator computes it, and no curvature machinery
    // ships - architecture-doc §8 non-goal "full GR solver"
    // forbids it at this stage.
    float diagnostic_curvature = 0.0f;
};

// Per-step advance result from the future geodesic integrator
// (architecture-doc §3.4). Preserved unchanged from the Manifold
// Core Skeleton slice. This is the integrator's return type, not
// a `GeodesicState` field (the struct's `valid` flag is the
// closest in-state analog).
//
//   - `InFlight`       advance the next step.
//   - `ChartBoundary`  geodesic left the active chart's domain;
//                      hand off to a different chart via the
//                      spacetime deformation layer (§3.5).
//   - `Terminated`     geometry hit, scene-bound exit, or
//                      horizon-proxy crossing.
enum class GeodesicStatus {
    InFlight       = 0,
    ChartBoundary,
    Terminated,
};

// Returns the default geodesic state: a unit-energy photon at
// the chart origin propagating along +z. Satisfies the null
// condition on the Euclidean chart's Minkowski metric:
//     `g_{mu nu} p^mu p^nu = -E^2 + p_z^2 = -1 + 1 = 0`.
// `valid = true`; all other placeholder accumulators are zero.
// This is the only `GeodesicState` value the Manifold Core
// implements concretely today; the future integrator advances
// states derived from this default.
RR_HD inline GeodesicState default_geodesic_state() {
    return GeodesicState{};
}

}

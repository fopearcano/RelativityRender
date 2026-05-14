#pragma once

// Orthonormal observer frame at a chart event (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3). Carries the
// observer's spacetime position, its four-velocity, a 3-velocity
// `beta` sibling that mirrors the existing
// `rr::relativity::Observer` so the legacy SR helpers can be fed
// without re-derivation, the spatial tetrad legs, and two scalar
// worldline-time placeholders (`proper_time`, `coordinate_time`)
// the future geodesic integrator will advance.
//
// What lives here this slice (MANIFOLD.3)
// ---------------------------------------
// - The `ObserverFrame` POD itself, with the seven fields above
//   plus a default value that describes the scene-rest observer
//   on the Euclidean chart (matching the Manifold Core Skeleton
//   slice's "no behaviour change" baseline).
// - `rest_frame()` factory.
// - `observer_frame_from(rr::relativity::Observer)` -
//   constructs an `ObserverFrame` from the existing
//   3-velocity-only `Observer`, deriving `velocity4` as
//   `gamma * (1, beta_x, beta_y, beta_z)` via the existing
//   `rr::relativity::gamma` / `clampBeta` helpers.
// - `to_relativity_observer(ObserverFrame)` - inverse bridge
//   for the round-trip; preserves `beta` exactly and discards
//   the rest of the frame state.
// - `is_normalised_timelike(frame, metric, tolerance)` - cheap
//   sanity gate that the four-velocity satisfies
//   `g_{mu nu} u^mu u^nu = -1` under the supplied metric (per
//   architecture-doc §3.2 signature convention, mostly-plus).
//
// What does NOT live here this slice
// ----------------------------------
// - **No Camera replacement.** The existing `src/camera/Camera`
//   class continues to own position / orientation / FOV; this
//   slice does not touch it. A future Minkowski-chart-wrap slice
//   (architecture-doc §10 step 1) will introduce an adaptor
//   that consumes a `Camera` + this `ObserverFrame` and produces
//   the chart-aware primary-ray state, but that is a separate
//   commit.
// - **No render-behaviour change.** Nothing in `src/cuda/`,
//   `src/optix/`, `src/pathtracer/`, or `src/renderer/` consumes
//   `ObserverFrame`; the kernel-side aberration / Doppler /
//   searchlight path still feeds on `rr::relativity::Observer`
//   exactly as it does today.
// - **No geodesic integrator.** `proper_time` and
//   `coordinate_time` are zero-initialised placeholders; no code
//   path advances them (architecture-doc §3.4 / §10 step 2).
// - **No full tetrad transport.** The spatial tetrad legs are
//   stored as `Vec3` (Euclidean-chart spatial part only); a
//   future curved-chart slice will promote them to `Vec4` and
//   add parallel-transport machinery.
//
// Why the existing relativity module shows up here
// ------------------------------------------------
// Per architecture-doc §7.2 the special-relativistic helpers in
// `src/relativity/` are the *Minkowski + constant-velocity-frame
// specialisation* of the observer-frame contract. The bridge
// helpers below make that subsumption concrete: an existing
// `rr::relativity::Observer` round-trips through
// `observer_frame_from(...)` / `to_relativity_observer(...)`
// without any loss in the 3-velocity carried, and the derived
// `velocity4` reproduces the Lorentz boost that
// `aberrateDirection` / `dopplerFactor` / `searchlightFactor`
// already implement.

#include "manifold/MetricTensor.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "relativity/RelativityMath.h"    // gamma / clampBeta
#include "relativity/RelativityParams.h"  // Observer

namespace rr::manifold {

struct ObserverFrame {
    // 4D position in chart coordinates. Components are
    // `(x^0, x^1, x^2, x^3)`; for the Euclidean chart this reads
    // `(t, x, y, z)` in scene-natural units (c = 1). Default is
    // the chart-space origin (scene-rest observer at scene
    // origin).
    rr::math::Vec4 position4 = {0.0f, 0.0f, 0.0f, 0.0f};

    // 4-velocity `u^mu` in chart coordinates. For the scene-rest
    // observer on the Euclidean chart this is `(1, 0, 0, 0)` -
    // one unit of coordinate time per unit proper time, no
    // spatial motion. In mostly-plus signature a timelike
    // worldline satisfies `g_{mu nu} u^mu u^nu = -1` when
    // `u^mu` is normalised to unit proper time;
    // `is_normalised_timelike` below is the cheap sanity gate.
    rr::math::Vec4 velocity4 = {1.0f, 0.0f, 0.0f, 0.0f};

    // 3-velocity `beta = v / c`, mirroring the existing
    // `rr::relativity::Observer::velocity`. Kept as a sibling of
    // `velocity4` so the existing src/relativity/ helpers
    // (`aberrateDirection`, `dopplerFactor`, `searchlightFactor`,
    // `applyDopplerColor`) can be fed without re-derivation. On
    // the Euclidean chart the two velocity fields are related by
    //   `velocity4 = gamma * (1, beta_x, beta_y, beta_z)`,
    //   `gamma = 1 / sqrt(1 - |beta|^2)`.
    // Use `observer_frame_from(...)` to populate both
    // consistently from an `rr::relativity::Observer`.
    rr::math::Vec3 beta = {0.0f, 0.0f, 0.0f};

    // Spatial tetrad legs `e_1` / `e_2` / `e_3` in chart
    // coordinates (spatial part only this slice; the time
    // component of each spatial leg is implicitly zero for the
    // Euclidean chart's rest observer). The timelike leg `e_0`
    // is the four-velocity `velocity4` above. Defaults form the
    // right-handed world basis the existing pinhole camera
    // produces.
    rr::math::Vec3 right   = {1.0f, 0.0f, 0.0f};
    rr::math::Vec3 up      = {0.0f, 1.0f, 0.0f};
    rr::math::Vec3 forward = {0.0f, 0.0f, 1.0f};

    // Proper time `tau` along the observer's worldline.
    // Cumulative proper time since a reference epoch (e.g. the
    // camera-start event). Placeholder this slice; no integrator
    // advances it yet. For a null geodesic (photon) proper time
    // is zero everywhere - this field is meaningful only for
    // timelike observers.
    float proper_time = 0.0f;

    // Coordinate time `t` in the active chart. For timelike
    // worldlines on the Euclidean chart this equals
    // `position4.x` (the 0-component of `position4` in our
    // convention); on non-Euclidean charts the relationship is
    // chart-dependent. Placeholder this slice; no integrator
    // advances it yet.
    float coordinate_time = 0.0f;
};

// Returns the scene-rest observer frame on the Euclidean chart:
// `position4 = origin`; `velocity4 = (1, 0, 0, 0)`; `beta = 0`;
// spatial tetrad = world basis; both time placeholders = 0.
// This is the only frame the Manifold Core implements concretely
// today.
RR_HD inline ObserverFrame rest_frame() {
    return ObserverFrame{};
}

// Builds an `ObserverFrame` from the existing
// `rr::relativity::Observer` (which carries only the 3-velocity
// `beta`). The new frame's
//
//   - `beta`            is the input velocity, with magnitude
//                       capped at `0.999999f` via
//                       `rr::relativity::clampBeta` so `gamma`
//                       stays finite. Direction is preserved.
//   - `velocity4`       is `gamma * (1, beta_x, beta_y, beta_z)`
//                       with `gamma` from
//                       `rr::relativity::gamma(|beta|_capped)`.
//   - `position4`       is the chart origin.
//   - spatial tetrad    is the world basis.
//   - both time fields  are zero.
//
// Identity at `|beta| = 0`. Round-trip via
// `to_relativity_observer(observer_frame_from(o))` preserves
// `o.velocity` to within the `clampBeta` cap.
RR_HD inline ObserverFrame observer_frame_from(
        const rr::relativity::Observer& obs) {
    using rr::math::Vec3;
    using rr::math::Vec4;
    using rr::math::length;
    using rr::relativity::clampBeta;
    using rr::relativity::gamma;

    const Vec3  in     = obs.velocity;
    const float in_mag = length(in);
    const float capped = clampBeta(in_mag, 0.999999f);
    const Vec3  beta3  = (in_mag > 1.0e-12f)
                         ? in * (capped / in_mag)
                         : in;
    const float g = gamma(capped);

    ObserverFrame f{};
    f.beta      = beta3;
    f.velocity4 = Vec4{g, g * beta3.x, g * beta3.y, g * beta3.z};
    return f;
}

// Returns the `rr::relativity::Observer` equivalent of an
// `ObserverFrame`'s 3-velocity. Round-trip via
// `observer_frame_from(...)` preserves `beta` exactly (modulo
// the `clampBeta` cap on the outbound trip). The remaining
// `ObserverFrame` state (`position4`, tetrad, time placeholders)
// is discarded - the legacy `Observer` type has no slot for it.
RR_HD inline rr::relativity::Observer to_relativity_observer(
        const ObserverFrame& f) {
    return rr::relativity::Observer{f.beta};
}

// Returns `true` if the four-velocity `f.velocity4` is
// normalised to unit proper time under `metric`, i.e.
//   `|g_{mu nu} u^mu u^nu - (-1)| <= tolerance`.
// For a 4-velocity built from `observer_frame_from(...)` on the
// Minkowski metric this identity holds analytically: the
// construction `u = gamma * (1, beta_x, beta_y, beta_z)` gives
//   `g_{mu nu} u^mu u^nu = -gamma^2 (1 - |beta|^2) = -1`.
// The helper exists so future curved-chart slices can verify
// their per-event four-velocity normalisation against their
// chart's metric, and so tests of `observer_frame_from(...)`
// have a stable gate.
//
// Tolerance defaults to `1.0e-4f`; floats accumulate ~6 decimal
// digits of error through `gamma^2 * (1 - |beta|^2)` at the
// high-beta end of `clampBeta`.
RR_HD inline bool is_normalised_timelike(
        const ObserverFrame& f,
        const MetricTensor&  metric,
        float                tolerance = 1.0e-4f) {
    const float u[4] = {
        f.velocity4.x,
        f.velocity4.y,
        f.velocity4.z,
        f.velocity4.w,
    };
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            s += metric.at(i, j) * u[i] * u[j];
        }
    }
    const float diff     = s + 1.0f;   // `s` should be exactly -1.
    const float abs_diff = diff < 0.0f ? -diff : diff;
    return abs_diff <= tolerance;
}

}

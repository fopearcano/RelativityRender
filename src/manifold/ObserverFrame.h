#pragma once

// Orthonormal observer frame at a chart event (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3). Carries the
// observer's spacetime position, its four-velocity, a 3-velocity
// `beta` sibling that mirrors the existing
// `rr::relativity::Observer` so the legacy SR helpers can be fed
// without re-derivation, the spatial tetrad legs, two scalar
// worldline-time placeholders (`proper_time`, `coordinate_time`)
// the future geodesic integrator will advance, and a perception-
// mode tag (added at OBSERVER.2) that identifies which
// observer-frame transforms the renderer applies.
//
// What lives here this slice (MANIFOLD.3 + OBSERVER.2)
// ----------------------------------------------------
// - The `PerceptionMode` enum (OBSERVER.2): identifies how an
//   `ObserverFrame` is to be consumed by the renderer
//   (`Identity` = no-op default; `ConstantVelocityMinkowski` =
//   today's SR specialisation; `CurvedChartGeodesicPlaceholder` =
//   reserved-but-inert slot for the future GR-aware arc).
// - The `ObserverFrame` POD itself, with the seven MANIFOLD.3
//   fields plus the new `perception_mode` field, defaults that
//   describe the scene-rest observer on the Euclidean chart
//   under `PerceptionMode::Identity` (matching the Manifold Core
//   Skeleton slice's "no behaviour change" baseline).
// - `rest_frame()` factory (MANIFOLD.3).
// - `default_perception_mode()` factory (OBSERVER.2) returning
//   `PerceptionMode::Identity`.
// - `observer_frame_from(rr::relativity::Observer)` -
//   constructs an `ObserverFrame` from the existing
//   3-velocity-only `Observer`, deriving `velocity4` as
//   `gamma * (1, beta_x, beta_y, beta_z)` via the existing
//   `rr::relativity::gamma` / `clampBeta` helpers. The
//   perception-mode field defaults to `Identity`; the caller
//   (OBSERVER.4 camera adapter) decides which perception mode
//   to apply when threading the frame to the kernel.
// - `to_relativity_observer(ObserverFrame)` - inverse bridge
//   for the round-trip; preserves `beta` exactly and discards
//   the rest of the frame state (including `perception_mode`).
// - `is_normalised_timelike(frame, metric, tolerance)` - cheap
//   sanity gate that the four-velocity satisfies
//   `g_{mu nu} u^mu u^nu = -1` under the supplied metric (per
//   architecture-doc §3.2 signature convention, mostly-plus).
// - `is_orthonormal_tetrad(frame, tolerance)` (OBSERVER.2) -
//   cheap sanity gate that the spatial tetrad legs are pairwise
//   orthogonal AND unit-length.
// - `is_finite_observer_frame(frame)` (OBSERVER.2) - sanity
//   gate that every scalar field is finite (no NaN, no inf).
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

#include <cmath>                          // std::isfinite

namespace rr::manifold {

// Identifies which observer-frame transforms the renderer is to
// apply to a given `ObserverFrame`. Parallel to
// `CoordinateChartType` (the two enums together identify
// "where space maps" + "how the observer perceives it" per the
// architecture-doc §3 ontology).
//
// The renderer this slice (OBSERVER.2) does not yet read this
// field at any kernel call site - the field is reserved-but-
// declared so subsequent OBSERVER.* slices (CLI bridge at
// OBSERVER.3, camera adapter at OBSERVER.4, GPU payloads at
// OBSERVER.5 / OBSERVER.6) can populate it without an ABI
// bump on `ObserverFrame`.
//
// Default value (`Identity`) is the bit-for-bit no-op anchor:
// every existing CLI action produces byte-identical output
// against an `ObserverFrame` whose perception mode is
// `Identity`.
enum class PerceptionMode {
    // Scene-rest observer; no aberration, no Doppler, no
    // searchlight. Matches the pre-pivot Euclidean camera
    // bit-for-bit. The renderer's default.
    Identity = 0,

    // Constant-velocity Minkowski observer - the existing
    // `rr::relativity::Observer` + `RelativityParams`
    // specialisation. Applies the existing
    // `aberrateDirection` / `dopplerFactor` /
    // `searchlightFactor` helpers, keyed on
    // `ObserverFrame::beta` instead of
    // `rr::relativity::Observer::velocity`. The math helper
    // bodies are preserved verbatim; output is byte-identical
    // to today's renderer for the same input observer / params.
    ConstantVelocityMinkowski,

    // Reserved for the future curved-chart observer-frame slice
    // (parallel-transport tetrad along a geodesic worldline).
    // Selecting this mode is a structural passthrough until the
    // corresponding implementation slice lands - the kernel
    // treats it as `Identity` for byte-identity preservation.
    CurvedChartGeodesicPlaceholder,
};

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

    // Perception-mode tag (OBSERVER.2). Identifies how the
    // renderer is to consume this frame; see `PerceptionMode`
    // above. Default `Identity` is the bit-for-bit no-op
    // anchor. Reserved-but-declared this slice - no kernel call
    // site reads it yet; subsequent OBSERVER.* slices (the CLI
    // bridge at OBSERVER.3, the camera-to-observer adapter at
    // OBSERVER.4, the CUDA / OptiX payload bridges at
    // OBSERVER.5 / OBSERVER.6) will populate + consume it.
    PerceptionMode perception_mode = PerceptionMode::Identity;
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

// Returns the default perception mode (`PerceptionMode::Identity`).
// OBSERVER.2 factory mirroring the architecture-doc §3 ontology's
// "no behaviour change by default" anchor: every existing CLI
// action produces byte-identical output against an `ObserverFrame`
// whose perception mode is `Identity`. Subsequent OBSERVER.*
// slices (CLI bridge / camera adapter) use this factory rather
// than hard-coding the enum value at every call site.
RR_HD inline PerceptionMode default_perception_mode() {
    return PerceptionMode::Identity;
}

// Returns `true` if the spatial tetrad legs `right` / `up` /
// `forward` form an orthonormal triad in chart coordinates,
// i.e. all three pairwise dot products are within `tolerance`
// of zero AND all three leg lengths are within `tolerance` of
// unity. Default tolerance `1.0e-4f` matches the
// `is_normalised_timelike` precedent (floats accumulate ~6
// decimal digits of error through `sqrt` + `dot`).
//
// For the default `rest_frame()` (world-basis tetrad) the
// helper returns `true` analytically: pairwise dot products are
// exactly zero, leg lengths are exactly one. Future curved-
// chart slices that parallel-transport the tetrad along a
// worldline can use this helper to gate the integrator's per-
// step normalisation.
RR_HD inline bool is_orthonormal_tetrad(
        const ObserverFrame& f,
        float                tolerance = 1.0e-4f) {
    using rr::math::dot;
    using rr::math::length;

    const float d_ru = dot(f.right,   f.up);
    const float d_uf = dot(f.up,      f.forward);
    const float d_fr = dot(f.forward, f.right);

    const float abs_ru = d_ru < 0.0f ? -d_ru : d_ru;
    const float abs_uf = d_uf < 0.0f ? -d_uf : d_uf;
    const float abs_fr = d_fr < 0.0f ? -d_fr : d_fr;
    if (abs_ru > tolerance) return false;
    if (abs_uf > tolerance) return false;
    if (abs_fr > tolerance) return false;

    const float len_r = length(f.right);
    const float len_u = length(f.up);
    const float len_f = length(f.forward);

    const float dr = len_r - 1.0f;
    const float du = len_u - 1.0f;
    const float df = len_f - 1.0f;
    const float abs_dr = dr < 0.0f ? -dr : dr;
    const float abs_du = du < 0.0f ? -du : du;
    const float abs_df = df < 0.0f ? -df : df;
    if (abs_dr > tolerance) return false;
    if (abs_du > tolerance) return false;
    if (abs_df > tolerance) return false;

    return true;
}

// Returns `true` if every scalar field on `f` is finite (no
// NaN, no inf). Cheap defence-in-depth gate the OBSERVER.4
// camera adapter and OBSERVER.5 / OBSERVER.6 GPU payload
// bridges will run before threading a frame to the kernel,
// mirroring the `is_finite(MetricTensor)` precedent in
// `MetricTensor.h`. The `perception_mode` enum field is not
// checked - by C++ rules an enum-class value built from any
// concrete enumerator is by definition valid.
RR_HD inline bool is_finite_observer_frame(const ObserverFrame& f) {
    const float scalars[] = {
        f.position4.x, f.position4.y, f.position4.z, f.position4.w,
        f.velocity4.x, f.velocity4.y, f.velocity4.z, f.velocity4.w,
        f.beta.x,      f.beta.y,      f.beta.z,
        f.right.x,     f.right.y,     f.right.z,
        f.up.x,        f.up.y,        f.up.z,
        f.forward.x,   f.forward.y,   f.forward.z,
        f.proper_time,
        f.coordinate_time,
    };
    constexpr int N = sizeof(scalars) / sizeof(scalars[0]);
    for (int i = 0; i < N; ++i) {
        if (!std::isfinite(scalars[i])) return false;
    }
    return true;
}

}

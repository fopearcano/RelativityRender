#pragma once

// OBSERVER.6 — host-side adapter that builds an `ObserverFrame`
// from the existing scene-side camera state + legacy SR
// `rr::relativity::Observer` + the OBSERVER.4 `ObserverConfig`
// overlay. See `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7
// OBSERVER.6 (renumbered from the original §7 OBSERVER.4 after
// the OBSERVER.3 + OBSERVER.5 audit-slot insertions).
//
// What lives here this slice
// --------------------------
// - `build_observer_frame_from_camera(...)` — the adapter
//   itself. Takes a `rr::camera::GpuCamera` (the device-friendly
//   camera POD; callers convert via the existing
//   `rr::camera::Camera::to_gpu()` method), a
//   `rr::relativity::Observer` (the scene-author SR observer
//   carrying the 3-velocity `beta`), and an
//   `rr::manifold::ObserverConfig` (the CLI-overlay POD landed
//   at OBSERVER.4). Returns an `ObserverFrame` whose
//   construction path depends on
//   `config.perception_mode` per the OBSERVER.1 plan §3.6 /
//   §7 OBSERVER.6 contract:
//     - `Identity`                       — return
//                                          `rest_frame()`
//                                          byte-for-byte
//                                          (the no-op
//                                          anchor); the
//                                          camera + observer
//                                          + non-mode config
//                                          fields are
//                                          ignored.
//     - `ConstantVelocityMinkowski`     — full construction
//                                          from camera +
//                                          observer + config
//                                          (see below).
//     - `CurvedChartGeodesicPlaceholder` — return
//                                          `rest_frame()`
//                                          with the
//                                          `perception_mode`
//                                          tag preserved so
//                                          downstream
//                                          kernels can
//                                          distinguish.
//                                          Engages no chart-
//                                          aware behaviour
//                                          this slice
//                                          (placeholder;
//                                          architecture-doc
//                                          §8 non-goals).
//
// Beta resolution policy (ConstantVelocityMinkowski path)
// -------------------------------------------------------
//   1. `config.beta_magnitude != 0` AND
//      `length(config.direction) > 0`:
//      use `clampBeta(beta_magnitude) * normalize(direction)`.
//   2. `config.beta_magnitude != 0` AND
//      `length(config.direction) == 0`:
//      fall back to `camera.forward` (the documented sentinel
//      behaviour from the `ObserverConfig::direction`
//      doc-comment + the existing `--render-demo` precedent).
//   3. `config.beta_magnitude == 0`:
//      use `observer.velocity` (the legacy SR observer's
//      3-velocity from the scene-loader's `apply_relativity`
//      path). Default-zero observer carries no velocity.
//   The resulting beta vector is defensively passed through
//   `clampBeta` a second time at the magnitude level so a
//   non-trivial legacy `observer.velocity` cannot push `|beta|`
//   past `0.999999` (the existing SR cap; mirrors the
//   `observer_frame_from(Observer)` precedent in
//   `ObserverFrame.h`).
//
// Frame construction (ConstantVelocityMinkowski path)
// ---------------------------------------------------
//   - `position4 = (0, gc.position.x, gc.position.y, gc.position.z)`
//     — observer at the camera's 3D position; the
//     time-component is the camera-start epoch (t = 0).
//   - `velocity4 = (gamma, gamma * beta.x, gamma * beta.y,
//                    gamma * beta.z)`
//     — the 4-velocity derived from the resolved 3-velocity
//     `beta` and `gamma = 1 / sqrt(1 - |beta|^2)` via the
//     existing `rr::relativity::gamma` helper. Analytically
//     timelike-normalised under Minkowski:
//     `g_{mu nu} u^mu u^nu = -gamma^2 (1 - |beta|^2) = -1`.
//   - `beta = resolved 3-velocity`.
//   - Tetrad legs `right` / `up` / `forward` = the camera's
//     basis. The pinhole `Camera::recompute_basis` already
//     produces an orthonormal triad so the resulting frame
//     passes `is_orthonormal_tetrad(...)` analytically.
//   - `proper_time = config.proper_time` — the CLI-supplied
//     pre-set placeholder (reserved-but-stored per OBSERVER.4
//     scope; no integrator advances it).
//   - `coordinate_time = position4.x` — Euclidean chart:
//     coordinate-time equals the position4 0-component
//     (= 0 by construction here).
//   - `perception_mode = config.perception_mode`
//     (`ConstantVelocityMinkowski` on this path).
//
// What does NOT live here this slice
// ----------------------------------
// - **No kernel consumption.** The CUDA / OptiX kernels still
//   feed on the legacy `rr::relativity::Observer` +
//   `RelativityParams` types. Kernel-side reads of
//   `ObserverFrame::perception_mode` / `beta` /  tetrad land
//   at OBSERVER.7 (CUDA payload bridge) and OBSERVER.8 (OptiX
//   payload bridge) under the renumbered ladder.
// - **No GPU launch-params field.** `CudaSceneView` /
//   `OptixLaunchParams` are unchanged this slice.
// - **No full GR tetrad solver / parallel transport / geodesic
//   ODE.** The
//   `CurvedChartGeodesicPlaceholder` path is a literal
//   placeholder; selecting it is structurally a passthrough.
//   Architecture-doc §8 non-goals + OBSERVER.1 plan §8 stand.
// - **No new RelativityParams flags.** The existing six flags
//   are preserved verbatim. The adapter consumes
//   `Observer::velocity` only; the `RelativityParams` knobs
//   continue to be threaded to the kernel via the existing
//   call paths.
// - **No Camera ABI change.** The host-side
//   `rr::camera::Camera` class is unchanged. The adapter takes
//   the device-friendly `GpuCamera` POD (one-time
//   `camera.to_gpu()` conversion at the dispatcher's call
//   site).
// - **No path-tracer migration.** The CUDA / OptiX path-tracer
//   kernels continue to feed on the legacy types. OBSERVER.*
//   scope is the `--render-aovs` / `--render-optix-aovs` arc
//   per the OBSERVER.1 plan §8.

#include "camera/CameraRay.h"             // GpuCamera
#include "manifold/ObserverFrame.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "relativity/RelativityMath.h"    // gamma / clampBeta
#include "relativity/RelativityParams.h"  // Observer

namespace rr::manifold {

inline ObserverFrame build_observer_frame_from_camera(
        const rr::camera::GpuCamera&     gc,
        const rr::relativity::Observer&  observer,
        const ObserverConfig&            config) {
    using rr::math::Vec3;
    using rr::math::Vec4;
    using rr::math::length;
    using rr::relativity::clampBeta;
    using rr::relativity::gamma;

    constexpr float kMaxBeta = 0.999999f;
    constexpr float kEps     = 1.0e-12f;

    // Identity perception mode: byte-identity no-op anchor.
    // The OBSERVER.7+ kernel arms short-circuit on this mode
    // so the camera + observer + non-mode config fields are
    // not threaded to the device-side path.
    if (config.perception_mode == PerceptionMode::Identity) {
        return rest_frame();
    }

    // CurvedChartGeodesicPlaceholder: structural passthrough
    // until the future GR-aware arc lands (OBSERVER.1 plan §8
    // non-goals). Return rest_frame() byte-for-byte EXCEPT
    // preserve the perception_mode tag so downstream kernels
    // can distinguish if needed.
    if (config.perception_mode ==
            PerceptionMode::CurvedChartGeodesicPlaceholder) {
        ObserverFrame placeholder = rest_frame();
        placeholder.perception_mode =
                PerceptionMode::CurvedChartGeodesicPlaceholder;
        return placeholder;
    }

    // ConstantVelocityMinkowski: full construction.
    //
    // Resolve the 3-velocity beta per the documented priority:
    //  - explicit CLI beta wins (with direction fallback to
    //    camera forward on zero-sentinel direction);
    //  - else legacy SR observer.velocity carries through.
    Vec3 beta_vec{0.0f, 0.0f, 0.0f};
    if (config.beta_magnitude != 0.0f) {
        Vec3        dir     = config.direction;
        const float dir_len = length(dir);
        if (dir_len > kEps) {
            dir = dir * (1.0f / dir_len);  // normalise
        } else {
            // Zero-sentinel direction; fall back to the
            // camera's forward axis (the existing
            // --render-demo precedent + the ObserverConfig
            // doc-comment contract).
            dir = gc.forward;
        }
        const float capped = clampBeta(config.beta_magnitude, kMaxBeta);
        beta_vec           = dir * capped;
    } else {
        beta_vec = observer.velocity;
    }

    // Defensive magnitude clamp: a non-trivial legacy
    // observer.velocity (e.g. set by `apply_relativity` from a
    // scene file) may exceed `kMaxBeta`. Mirrors the
    // `observer_frame_from(Observer)` precedent in
    // ObserverFrame.h.
    const float beta_mag   = length(beta_vec);
    const float capped_mag = clampBeta(beta_mag, kMaxBeta);
    const Vec3  final_beta = (beta_mag > kEps)
            ? beta_vec * (capped_mag / beta_mag)
            : beta_vec;  // exact zero stays exact zero
    const float g = gamma(capped_mag);

    ObserverFrame f{};
    f.position4 = Vec4{0.0f, gc.position.x, gc.position.y, gc.position.z};
    f.velocity4 = Vec4{g, g * final_beta.x, g * final_beta.y, g * final_beta.z};
    f.beta      = final_beta;
    f.right     = gc.right;
    f.up        = gc.up;
    f.forward   = gc.forward;
    f.proper_time     = config.proper_time;
    f.coordinate_time = f.position4.x;       // Euclidean chart: t = position4.x
    f.perception_mode = PerceptionMode::ConstantVelocityMinkowski;
    return f;
}

}

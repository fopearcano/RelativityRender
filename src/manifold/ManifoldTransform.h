#pragma once

// Aggregate the renderer consumes to describe the active manifold
// state for one render (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.5, "spacetime
// deformation layer"), plus the coordinate-transform helpers that
// move positions and directions between world space and the
// active chart's coordinates.
//
// What lives here this slice (MANIFOLD.5)
// ---------------------------------------
// - The `ManifoldTransform` POD itself (preserved from the
//   Manifold Core Skeleton slice): `{CoordinateChart chart,
//   MetricTensor metric, ObserverFrame observer}`.
// - `identity_transform()` factory (preserved). The default
//   value of `ManifoldTransform{}` describes today's renderer
//   behaviour (Euclidean chart, Minkowski metric, scene-rest
//   observer).
// - Four transform helpers, each shipped in `Vec3` (spatial-
//   only, matches the existing renderer's world-space
//   primitives) and `Vec4` (spacetime, future curved-chart
//   integrator) overloads:
//     - `world_to_chart(transform, p)` — forward map.
//     - `chart_to_world(transform, p)` — inverse map.
//     - `transform_direction(transform, d)` — Jacobian of
//       `world_to_chart` applied to a direction. Translation-
//       invariant; magnitude is *not* preserved.
//     - `transform_ray_like_direction(transform, d)` — same as
//       `transform_direction` but with unit-length /
//       null-condition preservation appropriate for ray
//       directions.
//
// Initial behaviour (this slice)
// ------------------------------
// Euclidean identity transform only. On the *default*
// `ManifoldTransform` (Euclidean chart, `origin = (0, 0, 0)`,
// `scale = 1.0`) every helper is the identity map — this is the
// "preserves current output by default" contract the task brief
// requires.
//
// On a non-default Euclidean chart (`origin != 0` or
// `scale != 1`) the helpers apply the chart's affine map:
//
//     world_to_chart(p)             = (p - origin) / scale
//     chart_to_world(p)             = origin + p * scale
//     transform_direction(d)        = d / scale
//     transform_ray_like_direction(d) = normalize(d / scale)
//                                      [Vec3, ray-direction
//                                       unit-length convention]
//                                     = d / scale
//                                      [Vec4, null-condition
//                                       preserving because
//                                       g_{mu nu} (αp)^mu (αp)^nu
//                                       = α^2 g_{mu nu} p^mu p^nu]
//
// On non-Euclidean chart families (`SchwarzschildLike`,
// `KruskalLikePlaceholder`, `PenroseLikePlaceholder`,
// `KerrLikePlaceholder`) every helper is the passthrough
// (returns its input). Master rule #3 ("no fake stubs pretending
// to be complete systems") forbids shipping a curved-chart
// forward map before its slice lands; the passthrough is
// honest-but-degenerate and is the only behaviour the future
// curved-chart slices will replace at this seam.
//
// What does NOT live here this slice
// ----------------------------------
// - **No curved-space math.** Schwarzschild / Kruskal / Penrose
//   / Kerr forward maps are deferred to their respective chart
//   slices (architecture-doc §10 steps 3–6).
// - **No CUDA / OptiX integration.** The helpers are
//   `RR_HD inline` so they compile on device, but no kernel
//   consumes them this slice.
// - **No Camera replacement.** `generate_camera_ray` in
//   `src/camera/CameraRay.h` still produces world-space rays
//   exactly as it does today; the Minkowski-chart-wrap slice
//   (architecture-doc §10 step 1) is what will introduce the
//   adaptor that threads `world_to_chart` /
//   `transform_ray_like_direction` through the path-tracer
//   ray-gen step.
// - **No metric / observer consumption.** The transforms read
//   `t.chart` only; `t.metric` and `t.observer` are present on
//   the aggregate so the spacetime-deformation-layer surface is
//   complete, but the helpers do not branch on them.
//
// Convention reminder (from `MetricTensor.h`)
// -------------------------------------------
// Mostly-plus signature `(-, +, +, +)`. `Vec4` layout for
// spacetime events in this module is `(x^0, x^1, x^2, x^3)` —
// time first, spatial second. So for a `Vec4 p`:
//   - `p.x` is the time component `x^0 = t` (or `E` for a
//     4-momentum);
//   - `p.y`, `p.z`, `p.w` are the spatial components
//     `x^1`, `x^2`, `x^3` (or `p_x`, `p_y`, `p_z` for a
//     4-momentum).
// The chart's spatial `origin` (`CoordinateChart::origin`, a
// `Vec3`) maps onto `(p.y, p.z, p.w)`; there is no chart-time
// origin field at this slice.

#include "manifold/CoordinateChart.h"
#include "manifold/MetricTensor.h"
#include "manifold/ObserverFrame.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "math/Vec4.h"

namespace rr::manifold {

struct ManifoldTransform {
    CoordinateChart chart;
    MetricTensor    metric;
    ObserverFrame   observer;
};

// Returns the Identity / Minkowski / rest-frame transform - the
// degenerate case that reproduces today's renderer bit-for-bit
// when threaded through the four transform helpers below. The
// chart defaults to `Euclidean` with `origin = (0, 0, 0)` and
// `scale = 1.0`, so every helper is the identity map on this
// transform.
inline ManifoldTransform identity_transform() {
    return ManifoldTransform{};
}

// ---- spatial (Vec3) overloads ----

// Forward chart map for a spatial position. On the Euclidean
// chart:
//   `chart_pos = (world_pos - origin) / scale`.
// Identity at default (`origin = 0`, `scale = 1`). Passthrough on
// non-Euclidean chart families (curved forward maps land with
// their slices).
RR_HD inline rr::math::Vec3 world_to_chart(
        const ManifoldTransform& t, rr::math::Vec3 world_pos) {
    if (t.chart.type == CoordinateChartType::Euclidean) {
        return (world_pos - t.chart.origin) * (1.0f / t.chart.scale);
    }
    return world_pos;
}

// Inverse chart map for a spatial position. On the Euclidean
// chart:
//   `world_pos = origin + chart_pos * scale`.
// Identity at default. Passthrough on non-Euclidean chart
// families.
RR_HD inline rr::math::Vec3 chart_to_world(
        const ManifoldTransform& t, rr::math::Vec3 chart_pos) {
    if (t.chart.type == CoordinateChartType::Euclidean) {
        return t.chart.origin + chart_pos * t.chart.scale;
    }
    return chart_pos;
}

// Spatial direction Jacobian for `world_to_chart`. Translation
// is dropped (directions are translation-invariant); scaling
// remains:
//   `chart_dir = world_dir / scale`.
// Identity at default. Passthrough on non-Euclidean chart
// families. Magnitude is *not* preserved; for ray directions
// that need unit length, use `transform_ray_like_direction`.
RR_HD inline rr::math::Vec3 transform_direction(
        const ManifoldTransform& t, rr::math::Vec3 world_dir) {
    if (t.chart.type == CoordinateChartType::Euclidean) {
        return world_dir * (1.0f / t.chart.scale);
    }
    return world_dir;
}

// Spatial ray-direction Jacobian with unit-length renormalisation:
//   `chart_dir = normalize(world_dir / scale)`.
// For uniform scaling this reduces to `normalize(world_dir)`, so
// a unit-length `world_dir` round-trips unchanged regardless of
// `scale`. Identity at default on a unit input. Returns
// `normalize(world_dir)` on non-Euclidean chart families
// (preserves unit length but does not apply any curved-chart
// transform - that lands with the chart's slice).
RR_HD inline rr::math::Vec3 transform_ray_like_direction(
        const ManifoldTransform& t, rr::math::Vec3 world_dir) {
    using rr::math::normalize;
    if (t.chart.type == CoordinateChartType::Euclidean) {
        return normalize(world_dir * (1.0f / t.chart.scale));
    }
    return normalize(world_dir);
}

// ---- spacetime (Vec4) overloads ----

// Forward chart map for a 4D event. On the Euclidean chart the
// time component (`p.x`) is preserved unchanged (no chart-time
// origin or scale this slice); the spatial components
// (`p.y`, `p.z`, `p.w`) are transformed by the spatial
// `world_to_chart` rule:
//   `chart_pos.x      = world_pos.x`              [time, x^0]
//   `chart_pos.{yzw}  = (world_pos.{yzw} - origin.{xyz}) / scale`
// Identity at default. Passthrough on non-Euclidean families.
RR_HD inline rr::math::Vec4 world_to_chart(
        const ManifoldTransform& t, rr::math::Vec4 world_pos4) {
    if (t.chart.type == CoordinateChartType::Euclidean) {
        const float inv_scale = 1.0f / t.chart.scale;
        return rr::math::Vec4{
            world_pos4.x,
            (world_pos4.y - t.chart.origin.x) * inv_scale,
            (world_pos4.z - t.chart.origin.y) * inv_scale,
            (world_pos4.w - t.chart.origin.z) * inv_scale,
        };
    }
    return world_pos4;
}

// Inverse chart map for a 4D event. Inverse of the Vec4
// `world_to_chart`. Identity at default. Passthrough on
// non-Euclidean families.
RR_HD inline rr::math::Vec4 chart_to_world(
        const ManifoldTransform& t, rr::math::Vec4 chart_pos4) {
    if (t.chart.type == CoordinateChartType::Euclidean) {
        return rr::math::Vec4{
            chart_pos4.x,
            t.chart.origin.x + chart_pos4.y * t.chart.scale,
            t.chart.origin.y + chart_pos4.z * t.chart.scale,
            t.chart.origin.z + chart_pos4.w * t.chart.scale,
        };
    }
    return chart_pos4;
}

// 4D direction Jacobian for `world_to_chart`. Time component
// `d.x` is invariant (the chart has no time scale this slice);
// spatial components `(d.y, d.z, d.w)` are divided by `scale`:
//   `chart_dir.x      = world_dir.x`
//   `chart_dir.{yzw}  = world_dir.{yzw} / scale`
// Identity at default. Passthrough on non-Euclidean families.
// Does NOT preserve the null condition for arbitrary `scale`
// (the time component is unscaled while the spatial part
// shrinks); use `transform_ray_like_direction` for null-
// preservation when feeding the future geodesic integrator.
RR_HD inline rr::math::Vec4 transform_direction(
        const ManifoldTransform& t, rr::math::Vec4 world_dir4) {
    if (t.chart.type == CoordinateChartType::Euclidean) {
        const float inv_scale = 1.0f / t.chart.scale;
        return rr::math::Vec4{
            world_dir4.x,
            world_dir4.y * inv_scale,
            world_dir4.z * inv_scale,
            world_dir4.w * inv_scale,
        };
    }
    return world_dir4;
}

// 4D ray-like direction with null-condition preservation. On
// the Euclidean chart this applies uniform 4D scaling:
//   `chart_dir = world_dir / scale`.
// Uniform scaling preserves the null condition because
//   `g_{mu nu} (alpha p)^mu (alpha p)^nu = alpha^2
//    g_{mu nu} p^mu p^nu`
// — if `world_dir` is null `(g(p, p) = 0)` then `chart_dir` is
// null too. The photon's worldline is preserved up to affine-
// parameter reparameterisation. Identity at default. Passthrough
// on non-Euclidean chart families.
RR_HD inline rr::math::Vec4 transform_ray_like_direction(
        const ManifoldTransform& t, rr::math::Vec4 world_dir4) {
    if (t.chart.type == CoordinateChartType::Euclidean) {
        return world_dir4 * (1.0f / t.chart.scale);
    }
    return world_dir4;
}

}

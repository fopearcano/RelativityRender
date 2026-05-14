# src/manifold — Manifold Core

This directory is the source-level home of the **Manifold Core**
defined in
[`docs/MANIFOLD_RENDERING_ARCHITECTURE.md`](../../docs/MANIFOLD_RENDERING_ARCHITECTURE.md)
§3. It introduces the coordinate-chart / metric-tensor /
observer-frame / geodesic-state ontology that will eventually
replace straight-line ray propagation in the path tracer.

## Status

**Skeleton stage.** Header-only contract surfaces; no renderer
integration; no CUDA / OptiX dependencies; no behavioural change.
The existing CUDA / OptiX path tracer is unaffected. The existing
special-relativistic helpers in `src/relativity/` are unaffected
(they are *subsumed* by, not consumed from, this directory — see
architecture-doc §7.2).

## Files

| File | Architecture-doc section | Role |
|------|--------------------------|------|
| `CoordinateChart.h`    | §3.1            | Canonical home of `CoordinateChartType` (`Euclidean`, `SchwarzschildLike`, `KruskalLikePlaceholder`, `PenroseLike`, `KerrLikePlaceholder`) plus `ChartUnits`, `CoordinateChartParameters`, the `CoordinateChart` POD (type / name / scale / origin / units / params), and the `euclidean_chart()` / `identity_chart()` helpers. |
| `ManifoldMode.h`       | §3.5, §4, §5    | Per-render config struct `ManifoldMode` (`enabled`, `chart`, `strength`, `debug_visualization`, `preserve_light_speed_normally`, `transform_coordinates_instead_of_light`) plus the `disabled_manifold_mode()` factory. Default is "disabled, Euclidean, strength 0, no output change" — bit-identical to the pre-pivot renderer. Repurposed at MANIFOLD.6; the prior MANIFOLD.1 `using ManifoldMode = CoordinateChartType` alias is removed (no consumers existed). |
| `MetricTensor.h`       | §3.2            | 4x4 row-major `g_{mu nu}` POD in mostly-plus `(-, +, +, +)` signature plus `at(i, j)` accessors, the `minkowski_metric()` and `identity_metric()` factories, validation helpers (`is_symmetric`, `is_finite`, `is_minkowski`, `is_diagonal`), and a closed-form 4x4 `determinant`. Promoted to its MANIFOLD.2 shape. Christoffel symbols / inverse metric still deferred. |
| `ObserverFrame.h`      | §3.3, §7.2      | 4D position + 4-velocity + 3-velocity (`beta`) + spatial tetrad (`right` / `up` / `forward`) + proper-time / coordinate-time placeholders. `rest_frame()` returns the scene-rest observer; `observer_frame_from(rr::relativity::Observer)` / `to_relativity_observer(ObserverFrame)` bridge to the legacy SR module per §7.2 subsumption; `is_normalised_timelike(frame, metric)` validates `g_{μν} u^μ u^ν = -1`. Promoted to its MANIFOLD.3 shape. |
| `GeodesicState.h`      | §3.4            | Geodesic state POD (`position4`, `momentum4`, `affine_parameter`, `valid`, `accumulated_optical_depth`, `diagnostic_curvature`) plus the `GeodesicStatus` enum (`InFlight` / `ChartBoundary` / `Terminated`) and a `default_geodesic_state()` factory returning the unit-energy +z photon that satisfies the null condition on Minkowski. Promoted to its MANIFOLD.4 shape. |
| `ManifoldTransform.h`  | §3.5            | Aggregate `{CoordinateChart, MetricTensor, ObserverFrame}` POD plus `identity_transform()` and the four coordinate-transform helpers `world_to_chart` / `chart_to_world` / `transform_direction` / `transform_ray_like_direction`, each in `Vec3` (spatial-only) and `Vec4` (spacetime) overloads. Euclidean identity at default; affine `(p - origin) / scale` on a non-default Euclidean chart; passthrough on non-Euclidean families. Promoted to its MANIFOLD.5 shape. |
| `SchwarzschildLikeWarp.h` | §3.1 / SCHW.1 | Closed-form artistic Schwarzschild-like coordinate-warp math leaf (`docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md`): `SchwarzschildLikeWarpParams` POD (`r_s` / `warp_strength` / `falloff` / `clamp_radius`) + `schwarzschild_like_validate_params(...)` + `schwarzschild_like_world_to_chart(...)` (forward radial warp) + `schwarzschild_like_chart_to_world(...)` (Newton-Raphson inverse, bounded iteration) + `schwarzschild_like_warp_ray_direction(...)` (optional primary-ray warp, hard-capped at ±0.5 bending). RR_HD inline; bounded by construction (Euclidean fallback at `warp_strength = 0` or `r_s = 0`; `clamp_radius > 0` guards NaN/Inf). SCHW.2+ wires this into `ManifoldTransform.h`; not yet consumed by the renderer. |

## Test coverage

The manifold layer is exercised end-to-end by
`tests/manifold_identity_tests.cpp` (`manifold_identity_tests` ctest
target, MANIFOLD.7). The test verifies the "default no-op" contract:
every shipping factory produces the documented degenerate-case POD,
all four `ManifoldTransform` helpers are the identity on the default
transform, and `ManifoldMode{}` reproduces the pre-pivot renderer
behaviour bit-for-bit. 112 hand-rolled `RR_CHECK` assertions across
eight test groups; wired into the audit-host `ctest` set (12/12 from
MANIFOLD.7 onward).

## What is intentionally NOT here this slice

Per master rule #3 ("do not implement fake stubs pretending to be
complete systems") and the non-goals enumerated in architecture-doc
§8, this directory does NOT contain:

- a geodesic integrator (lands at the chart-aware-seam slice),
- Schwarzschild / Kruskal-Szekeres / Penrose / Kerr metric data,
- Christoffel symbols (computed by the future integrator on demand,
  not stored on `MetricTensor`),
- any CUDA / OptiX kernel,
- any path-tracer integration,
- any deletion of `src/relativity/`,
- any test binary (the headers are POD-only; tests join when the
  first behavioural slice consumes them).

The directory exists today only so future slices have a clean home;
every type defined here is a real, complete implementation of the
**Euclidean / Minkowski / rest-frame** degenerate case. The
`CoordinateChartType` enum's non-`Euclidean` entries are documented
as inert placeholders, not stubs — selecting one has no defined
behaviour until its chart milestone lands. The conservative `*Like`
/ `*LikePlaceholder` naming (vs. the bare `Schwarzschild` / `Kerr`
names the Skeleton slice originally used) makes this honesty
visible at every call site (master rule #3, architecture-doc §8
non-goal "physically exact Kerr ray tracing").

## CMake

The directory is exposed as the `rr_manifold` INTERFACE library
mirroring `rr_relativity`. It links against `rr_math` only and is
*not* linked into any other target this slice. The library exists
so future slices have a stable include path; today nothing in
`rr_scene`, `rr_gpu`, `rr_optix`, `rr_pathtracer`, `rr_renderer`,
the main executable, or any test binary newly depends on it.

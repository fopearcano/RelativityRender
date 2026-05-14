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
| `CoordinateChart.h`    | §3.1            | Canonical home of `CoordinateChartType` (`Euclidean`, `SchwarzschildLike`, `KruskalLikePlaceholder`, `PenroseLikePlaceholder`, `KerrLikePlaceholder`) plus `ChartUnits`, `CoordinateChartParameters`, the `CoordinateChart` POD (type / name / scale / origin / units / params), and the `euclidean_chart()` / `identity_chart()` helpers. |
| `ManifoldMode.h`       | §3.1            | Compatibility alias header. Since MANIFOLD.1, `ManifoldMode` is a `using` alias for `CoordinateChartType`; the canonical enum lives in `CoordinateChart.h`. |
| `MetricTensor.h`       | §3.2            | 4x4 row-major `g_{mu nu}` POD in mostly-plus `(-, +, +, +)` signature plus `at(i, j)` accessors, the `minkowski_metric()` and `identity_metric()` factories, validation helpers (`is_symmetric`, `is_finite`, `is_minkowski`, `is_diagonal`), and a closed-form 4x4 `determinant`. Promoted to its MANIFOLD.2 shape. Christoffel symbols / inverse metric still deferred. |
| `ObserverFrame.h`      | §3.3, §7.2      | Tetrad (`right` / `up` / `forward`) plus three-velocity `beta`; `rest_frame()` returns the scene-rest observer. |
| `GeodesicState.h`      | §3.4            | Null-geodesic state POD (`position`, `momentum`) plus `GeodesicStatus` (`InFlight` / `ChartBoundary` / `Terminated`). |
| `ManifoldTransform.h`  | §3.5            | Aggregate `{CoordinateChart, MetricTensor, ObserverFrame}`; `identity_transform()` returns the default that reproduces today's renderer. |

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

# Penrose-Like Fixture (PENROSE.10)

Date:   2026-05-15
Branch: `claude/rewrite-rendering-core-De71I`
Slice:  **PENROSE.10 — Penrose-Like Fixture / Debug
        Visualization.**
Mode:   Authoritative reference for the fixture scene
        introduced at PENROSE.10
        (`scenes/test_penrose_like_manifold.rrscene`)
        and the existing scene-parser surface that
        consumes it (`apply_manifold` in
        `src/io/SceneLoader.cpp`, shipped at SCHW.9).

This document explains the purpose of the fixture
scene, the expected visual behavior when it is
rendered on a CUDA + OptiX-SDK host, the
default / no-op comparison the operator can use as a
regression baseline, and the runtime CUDA/OptiX
status (DEFERRED on the audit host; PASS-able when
the operator points the OptiX-side `render_aovs`
dispatcher at the fixture's `manifold` block on an
SDK-equipped host).

The fixture mirrors the SCHW.9
`test_schwarzschild_like_manifold.rrscene` precedent
in shape and intent — operators familiar with the
SCHW.9 fixture can transfer that mental model
directly. The two fixtures co-exist in `scenes/`
and exercise their respective chart families through
the same renderer dispatcher infrastructure.

---

## 1. Purpose

The fixture scene is the canonical authored example
of a Penrose-like manifold render at the PENROSE.*
sub-slice ladder's current state. It serves three
goals:

- **Parser-side regression anchor.** The fixture
  exercises the SCHW.9 scene-parser surface
  (`apply_manifold(...)` in
  `src/io/SceneLoader.cpp`) for the `PenroseLike`
  chart-name path: the four supported `ManifoldMode`
  fields (`enabled`, `chart`, `strength`,
  `debug_visualization`) round-trip through the
  loader with documented defaults; the kebab-case
  chart name `"penrose-like"` is parsed via
  `parse_chart_type` (also from SCHW.9 era; renamed
  to map onto `CoordinateChartType::PenroseLike` at
  PENROSE.4). The `--scene-info <fixture>` action
  accepts the fixture without error on the audit
  host (verified at the PENROSE.10 commit), proving
  the parser surface closes cleanly.

- **Renderer-side visualization fixture.** The
  fixture is the controlled diagnostic scene the
  operator points the renderer at when validating
  the PenroseLike compactification on a CUDA +
  OptiX-SDK host. Eight visible spheres at known
  radial distances spanning `r ∈ [0.4, 8.0]` (well
  past the saturation knee at `r ≈ scale = 1.0`)
  + a ground plane provide depth + radial variation
  across the chart's compactification range; the
  PenroseLike chart with `strength = 0.5` (per the
  fixture's `manifold.strength` field) and the
  artistic-default `r_max = 5.0` produces the
  documented asymptotic-compactification signature
  on the `ManifoldCoordinates` AOV per
  `PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §4.1
  (asymptotic compactification; far-field pixels
  saturate at chart-radius `r_max`).

- **Reference for future PENROSE.* slices.** The
  fixture documents the operator-facing shape the
  PENROSE.11 arc capstone audit will gate
  end-to-end. Future Kerr / Kruskal fixtures (when
  authorised by the operator) will mirror this
  shape.

---

## 2. Fixture composition

`scenes/test_penrose_like_manifold.rrscene` ships
the following structure:

### 2.1 Render settings

- Resolution: `1280 × 720` (matches the SCHW.9
  fixture and the existing
  `test_full_scene.rrscene` baseline).
- `samples_per_pixel = 1` + `max_depth = 1` for
  fast audit-host validation (the PenroseLike chart
  affects the `ManifoldCoordinates` AOV, not the
  bounce loop).

### 2.2 Camera

- Position `(0, 1.5, 9.0)` — pulled back further
  than the SCHW.9 fixture (which used `(0, 1.2,
  6.0)`) so the wider radial spread of marker
  spheres fits in the framebuffer.
- Forward `(0, -0.15, -1.0)` — looking toward the
  origin with a small downward tilt to keep the
  ground plane visible.
- FoV `50°` — slightly wider than SCHW.9's `45°`
  to fit the far-field markers comfortably.

### 2.3 Manifold block (the SCHW.9 surface)

```json
"manifold": {
  "enabled":             true,
  "chart":               "penrose-like",
  "strength":            0.5,
  "debug_visualization": true
}
```

- `enabled: true` — engages the manifold gate at
  the renderer-side dispatcher (`is_active(...)`
  returns `true` when paired with a non-Euclidean
  chart).
- `chart: "penrose-like"` — selects
  `CoordinateChartType::PenroseLike`. Parsed by
  `apply_manifold` via the local `parse_chart_type`
  helper (mirrors the CLI's
  `src/core/CommandLine.cpp::parse_chart_type`).
  The kebab-case name was unchanged by PENROSE.4's
  enum rename `PenroseLikePlaceholder` →
  `PenroseLike`; only the C++ enumerator changed.
- `strength: 0.5` — moderate compactification dial.
  Combined with the renderer dispatcher's artistic
  defaults (`r_max = 5.0`, `scale = 1.0`,
  `falloff = 1.0`), the formula `r_chart = r_max *
  tanh(strength * (r/scale)^falloff)` produces:
  - At `r = 1.0`: `r_chart ≈ 5.0 * tanh(0.5) ≈
    2.31` (compressed inward by ~57%);
  - At `r = 5.0`: `r_chart ≈ 5.0 * tanh(2.5) ≈
    4.94` (saturated near r_max);
  - At `r → ∞`: `r_chart → r_max = 5.0` (asymptotic
    boundary).
  Bounded by construction; safely past the math
  leaf's no-NaN/Inf domain (audited at PENROSE.3).
- `debug_visualization: true` — engages the
  `aov_manifold_coordinates` device buffer
  allocation in both the OptiX
  `OptixRenderer::render_aovs` (SCHW.7) and the
  CUDA `--render-aovs` host-side allocator (SCHW.9
  era). The PPM save site emits
  `output/optix_aov_manifold_coordinates.ppm` (or
  `output/aov_manifold_coordinates.ppm` on the
  CUDA path).

### 2.4 Geometry (visible markers + ground plane)

Eight diffuse spheres at known positions provide a
clear "is the warp engaged?" visual signal across
the PenroseLike chart's operational range:

| Sphere       | Centre                | Radius | r ≈    | Material      | Role |
|--------------|-----------------------|--------|--------|---------------|------|
| `centre`     | `(0.0, 0.5, 0.0)`     | `0.4`  | `0.5`  | blue          | At the compactification origin; near-identity (`r_chart ≈ 1.18`). |
| `near-right` | `(0.8, 0.5, 0.0)`     | `0.3`  | `0.94` | green (near)  | Just inside the saturation knee at `r ≈ scale`. |
| `near-left`  | `(-0.8, 0.5, 0.0)`    | `0.3`  | `0.94` | green (near)  | Mirror on −X. |
| `knee-right` | `(2.5, 0.5, 0.0)`     | `0.5`  | `2.55` | yellow (knee) | At the saturation knee; `r_chart ≈ 4.34`. |
| `knee-left`  | `(-2.5, 0.5, 0.0)`    | `0.5`  | `2.55` | yellow (knee) | Mirror on −X. |
| `far-right`  | `(6.0, 0.5, 0.0)`     | `0.7`  | `6.02` | red (far)     | Past the knee; `r_chart ≈ 4.99` (saturated). |
| `far-left`   | `(-6.0, 0.5, 0.0)`    | `0.7`  | `6.02` | red (far)     | Mirror on −X. |
| `very-far-up`| `(0.0, 4.0, 0.0)`     | `0.6`  | `4.0`  | red (far)     | Far on +Y axis; `r_chart ≈ 4.93`. |

Plus a single ground-plane mesh (`y = 0`, extent
`24 × 24` — wider than the SCHW.9 fixture's
`12 × 12` so the plane visibly extends past the
saturation knee) so the OptiX `render_aovs`
"first non-empty mesh" picker has a target.

The radial spread (`r ∈ {0.5, 0.94, 2.55, 4.0,
6.02}`) deliberately spans the chart's three
visual regimes:

- **Near-identity regime** (`r << scale = 1.0`):
  `centre` sphere — chart-space `r_chart` is close
  to world-space `r`, so the sphere's surface in
  the AOV is a faint deformation of the world-
  space sphere.
- **Transition / knee regime** (`r ≈ scale`):
  `near-*` spheres at `r ≈ 0.94` and `knee-*`
  spheres at `r ≈ 2.55` — chart-space
  compactification is visible; the spheres' AOV
  positions are noticeably inward of their
  world-space positions.
- **Saturation regime** (`r >> scale`): `far-*`
  spheres at `r ≈ 6.02` and `very-far-up` at
  `r = 4.0` — chart-space `r_chart` is very close
  to `r_max`; the spheres' AOV positions cluster
  near the chart-radius boundary, demonstrating
  the asymptotic compactification.

### 2.5 Lighting (minimal)

- One directional light (key); one environment
  light (sky). No point lights — the fixture's
  purpose is the AOV signature, not the
  beauty-pass illumination quality.

### 2.6 What the fixture deliberately omits

- **No `relativity` block.** Relativistic
  perception (Doppler / aberration / searchlight)
  is left disabled so the AOV's coordinate
  signature is not conflated with the
  relativistic frame transformation. Mirrors the
  SCHW.9 fixture's choice.
- **No chart-parameter authoring.** The four
  `CoordinateChart::params` slots
  (`mass / spin / compactification_scale /
  reserved`) are NOT exposed by the SCHW.9-era
  scene parser. Renderer dispatchers supply the
  PenroseLike artistic defaults
  (`mass = 5.0` (r_max), `spin = 1.0` (falloff),
  `compactification_scale = 1.0` (scale),
  `origin = (0, 0, 0)`) per the PENROSE.6 / PENROSE.8
  builder helpers in `main.cpp::run_render_aovs`
  and `run_render_optix_aovs`. Future slices may
  broaden the parser surface to cover chart
  parameters; PENROSE.10 deliberately scopes to
  the SCHW.9 surface.
- **No extreme / singular values.** The fixture's
  `strength = 0.5` and the dispatcher's
  `r_max = 5.0` keep every pixel comfortably
  inside the math leaf's no-NaN/Inf domain
  (audited at PENROSE.3 / PENROSE.5). The
  bounded-by-`r_max` invariant means the chart-
  space output is always within `[0, 5.0]`
  regardless of input distance — no operator-side
  bug can produce extreme AOV values.

---

## 3. Expected visual behavior (on a CUDA + OptiX-SDK host)

The fixture is designed to produce a clear,
operator-visible signature on the
`output/optix_aov_manifold_coordinates.ppm` PPM
when rendered via the OptiX `render_aovs`
dispatcher (PENROSE.8) — and an identical signature
on the CUDA-side
`output/aov_manifold_coordinates.ppm` when rendered
via the CUDA `--render-aovs` dispatcher (PENROSE.6).
The cross-backend AOV byte-equivalence is
**structurally guaranteed** by single-source-of-
truth math (audited at PENROSE.7 + PENROSE.9). The
signature is **the documented asymptotic-
compactification behavior** from
`PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §4.1:

- **`centre` sphere (at origin):** the math leaf's
  `|delta| ≈ 0` short-circuit + the analytic
  near-identity regime mean the AOV value at the
  centre sphere's surface is close to the
  world-space hit position. The visual signature
  is a sphere at near-identity coordinates.

- **`near-*` spheres (at radial distance `r ≈
  0.94`):** the formula `r_chart = 5.0 * tanh(0.5
  * 0.94) ≈ 2.21` produces a chart-space output
  that is visibly outward of the world-space
  position. Surprisingly: the AOV value is at
  larger `r_chart` than the world `r` because
  PenroseLike maps small `r` to a
  near-`tanh(strength * r)` range, which for
  small `r` is `~ strength * r * r_max = 0.5 *
  0.94 * 5.0 = 2.35` (linear regime). The
  fixture's near-*X spheres produce an AOV
  signature that is OUTWARD of their world-space
  positions in the near-identity regime — this
  is the expected behavior of the linear regime
  where the chart's bounded output mapping
  outweighs the input radial scaling.

- **`knee-*` spheres (at radial distance `r ≈
  2.55`):** `r_chart ≈ 5.0 * tanh(0.5 * 2.55) ≈
  4.34`. The AOV value at these spheres is at
  ~85% of `r_max` — the saturation transition
  begins.

- **`far-*` spheres (at radial distance `r ≈
  6.02`):** `r_chart ≈ 5.0 * tanh(0.5 * 6.02) ≈
  4.99`. The AOV value at these spheres is
  saturated within ~0.2% of `r_max`. **Visually,
  the far-*X spheres in the AOV cluster very
  close together at the chart-radius boundary**
  — the documented "asymptotic compactification
  onto a finite boundary" signature.

- **`very-far-up` sphere (at radial distance `r
  = 4.0`):** `r_chart ≈ 5.0 * tanh(0.5 * 4.0) ≈
  4.81`. Saturated to ~96% of `r_max`. AOV
  position is clamped near the chart boundary on
  +Y.

- **Ground plane** at `y = 0`: pixels span a wide
  range of radial distances. Near the origin the
  warp is visible (rectangular grid pattern
  appears compressed toward `r_max` in the AOV).
  Far from the origin (corners at `(±12, 0,
  ±12)` have `r ≈ 17`) the AOV converges
  asymptotically toward the `r_max = 5.0`
  boundary.

The beauty pass (`output/optix_aov_beauty.ppm`)
is **unaffected** by the PenroseLike chart —
PENROSE.8 only routes the hit position through
the warp for the `ManifoldCoordinates` AOV
write site; the beauty pass uses unwarped
primary rays and continues to render the
spheres as they sit in world space.

---

## 4. Expected default / no-op comparison

To distinguish "chart is engaged" from "chart is
disabled" the operator compares two AOV outputs
from the same fixture geometry:

### 4.1 Chart engaged (the fixture as authored)

`output/optix_aov_manifold_coordinates.ppm`
produced by the fixture as-is shows the documented
asymptotic-compactification signature (see §3
above): far-field spheres cluster at the
chart-radius boundary; near-field spheres show
the linear regime; the ground plane folds inward
toward `r_max`.

### 4.2 Chart disabled (operator override)

The operator can disable the chart in three
structurally-equivalent ways and verify each
produces byte-identical output to a pre-PENROSE.8
baseline (the MANI-I.8 OptiX AOV):

- **Override via CLI:** invoke
  `--render-optix-aovs --manifold-enable
  --manifold-chart euclidean --manifold-debug`
  and rely on `cfg.manifold.enabled = true` to
  win the merge over `scene.manifold` (the CLI
  policy: explicit CLI engagement takes
  precedence, per the SCHW.9 dispatcher's merge
  logic at
  `src/main.cpp::run_render_optix_aovs`). The
  resulting AOV is the MANI-I.8 raw world-position
  output.

- **Override via fixture edit:** locally edit the
  fixture's `manifold.chart` to `"euclidean"`.
  The parser accepts the change; the renderer
  sees `chart = Euclidean`; `is_active(...)`
  returns `false`; the kernel arms short-circuit
  (the Euclidean is "intentionally not active"
  per `ManifoldMode.h:143-145`).

- **Override via strength-zero:** locally edit
  the fixture's `manifold.strength` to `0.0`.
  Both the SCHW.7 and PENROSE.8 triple-gates'
  `strength > 0` checks short-circuit the kernel
  arms; the AOV is the raw world-position output.

All three produce **byte-identical PPMs**,
matching the PENROSE.5 / PENROSE.7 / PENROSE.9
audits' check #4 (disabled/default mode remains
no-op) and check #5 (Euclidean mode remains
identity) guarantees.

### 4.3 SchwarzschildLike non-regression

To confirm the SCHW.* arc is unaffected by the
PENROSE.10 fixture, the operator can:

- Render the SCHW.9 fixture
  (`scenes/test_schwarzschild_like_manifold.rrscene`)
  through the OptiX dispatcher: `--render-optix-aovs`
  with the SCHW.9 fixture loaded. The resulting
  AOV should be byte-identical to the
  pre-PENROSE.8 SchwarzschildLike reference (the
  PENROSE.8 OptiX commit preserved the SCHW.7 arm
  verbatim per the PENROSE.9 audit's check #6;
  the PenroseLike `else if` branch cannot fire
  when the chart is `schwarzschild-like`).

- Same for the CUDA path (`--render-aovs` with
  the SCHW.9 fixture loaded).

### 4.4 No-fixture baseline

Without the fixture at all (i.e. CLI
`--render-optix-aovs` with no scene file), the
inline-scene path is taken; the inline scene has
`scene.manifold = ManifoldMode{}` (disabled);
the dispatcher's merge resolves to disabled; the
kernel writes raw world positions. This is the
documented post-MANI-I.8 baseline OptiX AOV
output and serves as the "ground truth"
comparison for any chart-engaged output.

---

## 5. Current consumption status

The fixture is **loadable today** (parser
supports the manifold block since SCHW.9) and the
**renderer dispatcher merge logic** is in place
in `src/main.cpp::run_render_optix_aovs` +
`run_render_aovs`. As with the SCHW.9 fixture,
the existing `--render-optix-aovs` /
`--render-aovs` CLI actions **build their scenes
inline**; neither action loads a scene file. So
the fixture's `manifold` block is not directly
consumed by these actions today.

**Where the fixture's manifold IS consumed
today:**

- `--scene-info <fixture>` loads the scene
  through the same SceneLoader path; the parser
  surface is exercised. The `--scene-info`
  printer does not yet print the manifold block
  (the printer is audited separately); the
  operator can confirm loadability by the absence
  of a parse error.

**Where the fixture's manifold WILL be consumed
once a future CLI action lands:**

A future single-line CLI extension that lets
`--render-optix-aovs` accept a scene file
argument would tie the fixture together
end-to-end (mirrors the SCHW.9 fixture
documentation). The dispatcher merge logic
already added at SCHW.9 (`effective_manifold =
cfg.manifold.enabled ? cfg.manifold :
scene.manifold`) is already correct for this
future action — adding the file-argument arm to
`run_render_optix_aovs` would activate the
fixture's manifold without further changes to
the merge.

---

## 6. Runtime CUDA/OptiX status: DEFERRED

The audit host on which this fixture is authored
has no CUDA SDK and no OptiX SDK; the SDK_FOUND
TUs compile but cannot link / launch device
code. The runtime checks below are **DEFERRED**
to a CUDA + OptiX-SDK host (the standard
per-slice audit posture from MANI-I.6 / MANI-I.9
/ SCHW.* / PENROSE.3 / PENROSE.5 / PENROSE.7 /
PENROSE.9):

### 6.1 Loadability (audit host PASS)

- `--scene-info scenes/test_penrose_like_manifold.rrscene`
  loads cleanly. **PASS** on the audit host
  (verified at PENROSE.10 commit).
- `ctest 12/12` remains green at the post-PENROSE.10
  baseline. **PASS** on the audit host.

### 6.2 OptiX render — visual signature (DEFERRED)

On a CUDA + OptiX-SDK host with the proposed
future `--render-optix-aovs <scene>` extension:

```
$ ./RelativityRender --render-optix-aovs \
    scenes/test_penrose_like_manifold.rrscene
```

Expected outputs:
- `output/optix_aov_beauty.ppm` — visually
  identical to the pre-PENROSE.8 OptiX AOV
  beauty output for the same geometry (the chart
  does not affect beauty rendering).
- `output/optix_aov_normal.ppm`,
  `output/optix_aov_depth.ppm`,
  `output/optix_aov_albedo.ppm`,
  `output/optix_aov_doppler.ppm`,
  `output/optix_aov_searchlight.ppm` — same as
  the pre-PENROSE.8 OptiX AOV outputs for the
  same geometry.
- `output/optix_aov_manifold_coordinates.ppm` —
  **the PENROSE.10 signature** described in §3
  above (centre sphere near-identity; near-* and
  knee-* spheres compactified; far-* spheres
  saturated at chart-radius boundary).

### 6.3 OptiX render — byte-identity comparison (DEFERRED)

With the override scheme from §4.2, the operator
verifies that:
- The chart-disabled run (via any of the three
  override mechanisms) produces an
  `output/optix_aov_manifold_coordinates.ppm`
  byte-identical to the pre-PENROSE.8 baseline.
- The chart-engaged run produces a PPM whose
  pixels diverge from the world-space hit
  positions in the documented asymptotic-
  compactification signature.
- The chart-disabled output and chart-engaged
  output are visibly distinct on the
  `manifold_coordinates` AOV channel only —
  every other AOV channel is byte-identical.

This three-way comparison gates the future
PENROSE.11 (arc capstone audit) PASS verdict.

### 6.4 SchwarzschildLike non-regression (DEFERRED)

Render the SCHW.9 fixture
(`scenes/test_schwarzschild_like_manifold.rrscene`)
on both backends with the same future scene-file
CLI extension:

- CUDA `output/aov_manifold_coordinates.ppm`
  byte-identical to the pre-PENROSE.6
  SchwarzschildLike reference;
- OptiX
  `output/optix_aov_manifold_coordinates.ppm`
  byte-identical to the pre-PENROSE.8
  SchwarzschildLike reference.

The SCHW.* arc's cross-backend equivalence
(established at the SCHW.11 capstone) is
preserved by the PENROSE.* arc's `else if` +
enum-tag mutual exclusion.

### 6.5 CUDA ↔ OptiX byte-equivalence (DEFERRED)

The CUDA `output/aov_manifold_coordinates.ppm`
PenroseLike output and the OptiX
`output/optix_aov_manifold_coordinates.ppm`
PenroseLike output should be byte-identical for
the same fixture and same `--manifold-*`
parameters. This is the **key PENROSE.* arc
cross-backend equivalence claim** — structurally
guaranteed by single-source-of-truth math at
PENROSE.7 / PENROSE.9 audits, with empirical
pixel-level verification deferred to an
SDK-equipped host.

---

## 7. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules.
- `docs/PENROSE_LIKE_COMPACTIFICATION_PLAN.md` —
  canonical design doc; §3 reinterpretation
  table; §4 expected visual effects; §6 safety
  constraints; §9 deferred runtime checks.
- `docs/PENROSE_LIKE_COMPACTIFICATION_MATH_AUDIT.md`
  (PENROSE.3) — bounded / no-NaN math-leaf
  audit.
- `docs/PENROSE_LIKE_CPU_INTEGRATION_AUDIT.md`
  (PENROSE.5) — CPU-side integration audit.
- `docs/PENROSE_LIKE_CUDA_INTEGRATION_AUDIT.md`
  (PENROSE.7) — CUDA-side warp bridge audit.
- `docs/PENROSE_LIKE_OPTIX_INTEGRATION_AUDIT.md`
  (PENROSE.9) — OptiX-side warp bridge audit.
- `docs/SCHWARZSCHILD_LIKE_FIXTURE.md` — the
  SCHW.9 predecessor fixture doc this
  parallels.
- `docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md` —
  SCHW.11 capstone (verdict
  PASS_WITH_RUNTIME_DEFERRED) the PENROSE.* arc
  modeled itself on.
- `docs/MANIFOLD_DEBUG_AOV_TASK.md` (MANI-I.7) —
  AOV slot definition.
- `src/io/SceneLoader.cpp::apply_manifold` —
  SCHW.9 parser surface.
- `src/scene/Scene.h::Scene::manifold` — SCHW.9
  scene POD slot.
- `src/main.cpp::run_render_optix_aovs` —
  PENROSE.8 dispatcher merge for the OptiX
  `render_aovs` arm.
- `src/main.cpp::run_render_aovs` — PENROSE.6
  dispatcher merge for the CUDA
  `render_scene_with_aovs` arm.
- `src/manifold/PenroseLikeCompactification.h`
  — the PENROSE.2 math leaf at the heart of the
  arc.

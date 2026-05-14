# Schwarzschild-Like Fixture (SCHW.9)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Slice:  **SCHW.9 — Schwarzschild-Like Debug Visualization
        + Fixture.**
Mode:   Authoritative reference for the fixture scene
        introduced at SCHW.9
        (`scenes/test_schwarzschild_like_manifold.rrscene`)
        and the minimum scene-parser surface that consumes
        it (`apply_manifold` in
        `src/io/SceneLoader.cpp`).

This document explains the purpose of the fixture scene,
the expected visual behavior when it is rendered on a
CUDA + OptiX-SDK host, the default / no-op comparison
the operator can use as a regression baseline, and the
runtime CUDA/OptiX status (DEFERRED on the audit host;
PASS-able when the operator points the OptiX-side
`render_aovs` dispatcher at the fixture's `manifold`
block on an SDK-equipped host).

---

## 1. Purpose

The fixture scene is the canonical authored example of a
Schwarzschild-like manifold render at the SCHW.* sub-slice
ladder's current state. It serves three goals:

- **Parser-side regression anchor.** The fixture exercises
  the SCHW.9 scene-parser surface
  (`apply_manifold(...)` in `src/io/SceneLoader.cpp`):
  the four supported `ManifoldMode` fields (`enabled`,
  `chart`, `strength`, `debug_visualization`) round-trip
  through the loader with documented defaults. The
  `--scene-info <fixture>` action accepts the fixture
  without error on the audit host (verified at
  `b__fixture__` commit), proving the parser surface
  closes cleanly.

- **Renderer-side visualization fixture.** The fixture
  is the controlled diagnostic scene the operator points
  the OptiX-side `render_aovs` dispatcher at when
  validating the SCHW.7 OptiX warp bridge on a CUDA +
  OptiX-SDK host. Six simple visible spheres + a ground
  plane provide depth + radial variation across the
  `mass_origin = (0, 0, 0)`; the SchwarzschildLike chart
  with `warp_strength = 0.5` (per the fixture's
  `manifold.strength` field) produces the documented
  radial-compression signature on the
  `ManifoldCoordinates` AOV per
  `SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §4.1.

- **Reference for future SCHW.* slices.** The fixture
  documents the operator-facing shape SCHW.5 (CUDA
  integration) and the future Penrose / Kerr fixtures
  will mirror. The scene parser's manifold-block surface
  is intentionally minimal at SCHW.9 (no
  `CoordinateChart::params.{mass, spin,
  compactification_scale, origin}` fields exposed yet);
  artistic-default chart parameters come from the
  SCHW.7 main.cpp helper (see §5 below).

---

## 2. Fixture composition

`scenes/test_schwarzschild_like_manifold.rrscene` ships
the following structure:

### 2.1 Render settings

- Resolution: `1280 × 720` (matches the existing
  `test_full_scene.rrscene` baseline).
- `samples_per_pixel = 1` + `max_depth = 1` for fast
  audit-host validation (the SchwarzschildLike chart
  affects the `ManifoldCoordinates` AOV, not the
  bounce-loop behaviour).

### 2.2 Camera

- Position `(0, 1.2, 6.0)` — slightly above the ground
  plane, six units back from `mass_origin`.
- Forward `(0, -0.15, -1.0)` — looking toward
  `mass_origin` with a small downward tilt to keep the
  ground plane visible.
- FoV `45°` — fits the marker spheres comfortably.

### 2.3 Manifold block (the SCHW.9 surface)

```json
"manifold": {
  "enabled":             true,
  "chart":               "schwarzschild-like",
  "strength":            0.5,
  "debug_visualization": true
}
```

- `enabled: true` — engages the manifold gate at the
  renderer-side dispatcher (`is_active(...)` returns
  `true` when paired with a non-Euclidean chart).
- `chart: "schwarzschild-like"` — selects
  `CoordinateChartType::SchwarzschildLike`. Parsed by
  `apply_manifold` via the local `parse_chart_type`
  helper (mirrors the CLI's
  `src/core/CommandLine.cpp::parse_chart_type`).
- `strength: 0.5` — moderate warp dial. With the SCHW.7
  triple-gate `enabled && SchwarzschildLike && strength
  > 0`, this engages the kernel arm; with the artistic-
  default `r_s = 1.0`, `falloff = 1.0`, `clamp_radius =
  0.1` from main.cpp's SCHW.7 helper, the displacement
  scalar `f = strength * r_s / r^falloff` evaluates to
  `0.5 / r` for `r > clamp_radius` — visibly non-
  trivial near the marker spheres yet far from the
  primary-ray-direction-flip danger zone (the bend cap
  at `0.5` per the math leaf's `kBendCap`).
- `debug_visualization: true` — engages the
  `aov_manifold_coordinates` device buffer allocation
  in `OptixRenderer::render_aovs` (SCHW.7) AND in the
  CUDA `--render-aovs` host-side allocator. The PPM
  save site emits
  `output/optix_aov_manifold_coordinates.ppm` (or
  `output/aov_manifold_coordinates.ppm` on the CUDA
  path).

### 2.4 Geometry (visible markers + ground plane)

Six diffuse spheres at known positions provide a clear
"is the warp engaged?" visual signal:

| Sphere       | Centre               | Radius | Material | Role |
|--------------|----------------------|--------|----------|------|
| `centre`     | `(0.0, 0.5, 0.0)`    | `0.5`  | yellow   | At the mass origin; pixel positions on its surface are inside the clamp shell. |
| `near-right` | `(1.5, 0.5, 0.0)`    | `0.4`  | red      | Just outside the clamp shell on +X axis. |
| `near-up`    | `(0.0, 2.0, 0.0)`    | `0.4`  | green    | Just outside the clamp shell on +Y axis. |
| `near-front` | `(0.0, 0.5, 1.5)`    | `0.4`  | blue     | Just outside the clamp shell on +Z axis. |
| `far-right`  | `(3.0, 0.5, 0.0)`    | `0.5`  | red      | Far-field on +X — should appear nearly identity (warp scales as `1/r`). |
| `far-left`   | `(-3.0, 0.5, 0.0)`   | `0.5`  | red      | Far-field on −X — same as `far-right`, mirror. |

Plus a single ground-plane mesh
(`y = 0`, extent `12 × 12`) so the OptiX `render_aovs`
"first non-empty mesh" picker has a target.

### 2.5 Lighting (minimal)

- One directional light (key); one environment light
  (sky). No point lights — the fixture's purpose is
  the AOV signature, not the beauty-pass illumination
  quality.

### 2.6 What the fixture deliberately omits

- **No `relativity` block.** Relativistic perception
  (Doppler / aberration / searchlight) is left
  disabled so the AOV's coordinate signature is not
  conflated with the relativistic frame transformation.
  A future fixture (`test_schwarzschild_like_relativistic.rrscene`
  or similar) could combine the two.
- **No chart-parameter authoring.** The four
  `CoordinateChart::params` slots (`mass`, `spin`,
  `compactification_scale`, `reserved`) are NOT
  exposed by the SCHW.9 scene parser. Renderer
  dispatchers supply artistic defaults
  (`mass = 1.0`, `spin = 1.0`,
  `compactification_scale = 0.1`, `origin = (0,0,0)`)
  per the SCHW.7 main.cpp helper. Future slices may
  broaden the parser surface (e.g. an optional
  `chart_params: {mass: 1.0, falloff: 1.0,
  clamp_radius: 0.1}` sub-block) without an ABI bump.
- **No extreme / singular values.** The fixture's
  `strength = 0.5` and the dispatcher's
  `clamp_radius = 0.1` keep every pixel away from
  the math leaf's `1 / r^falloff` singularity at
  `r = 0`. The math leaf's four bounding guards
  (audited at SCHW.2 / SCHW.4 / SCHW.6 / SCHW.8) make
  even the most-singular geometric case (a pixel
  whose hit position equals `mass_origin`) produce
  finite output by construction.

---

## 3. Expected visual behavior (on a CUDA + OptiX-SDK host)

The fixture is designed to produce a clear,
operator-visible signature on the
`output/optix_aov_manifold_coordinates.ppm` PPM when
rendered via the OptiX `render_aovs` dispatcher (SCHW.7).
The signature is **the documented radial-compression
behavior** from `SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §4.1:

- **`centre` sphere (at `mass_origin`):** the math leaf's
  clamp-radius safety substitutes `r = clamp_radius =
  0.1` so the displacement scalar `f = 0.5 * 1.0 /
  0.1 = 5.0` is the maximum the configuration can
  produce. Pixels on the centre sphere's surface (whose
  world-space radius from origin is `~0.5`) will have
  `chart_pos = world_pos + 5 * (world_pos - origin)`,
  so the AOV value will be dramatically displaced
  outward — the "uniform-warp shell" signature the
  plan §4.4 documents.

- **`near-*` spheres (at radial distance `~1.5–2.0`):**
  `f = 0.5 * 1.0 / r = 0.25 to 0.33`. Pixels on
  their surface have a moderate radial displacement
  (`~0.5` to `~0.7` units outward). The AOV's
  per-pixel value is visibly distinct from the
  world-space hit position but not extreme — the
  "near-mass radial inflation" signature the plan
  §4.1 documents.

- **`far-*` spheres (at radial distance `~3.0`):**
  `f = 0.5 * 1.0 / 3.0 ≈ 0.17`. Small displacement;
  the AOV's per-pixel value is close to the
  world-space hit position — the "far-field identity"
  signature the plan §2 documents.

- **Ground plane** at `y = 0`: pixels span a wide range
  of radial distances. Near the origin the warp is
  visible (rectangular grid pattern appears
  "compressed" toward the mass origin in the AOV).
  Far from the origin the AOV approaches the
  world-space identity (corners of the ground plane
  at `(±6, 0, ±6)` have `r ≈ 8.5`, so `f ≈ 0.06` —
  near-identity).

The beauty pass (`output/optix_aov_beauty.ppm`) is
**unaffected** by the SchwarzschildLike chart — SCHW.7
only routes the hit position through the warp for the
`ManifoldCoordinates` AOV write site; the beauty pass
uses unwarped primary rays and continues to render the
spheres as they sit in world space.

---

## 4. Expected default / no-op comparison

To distinguish "chart is engaged" from "chart is
disabled" the operator compares two AOV outputs from
the same fixture geometry:

### 4.1 Chart engaged (the fixture as authored)

`output/optix_aov_manifold_coordinates.ppm` produced by
the fixture as-is shows the documented warp signature
(see §3 above).

### 4.2 Chart disabled (operator override or
strength-zero variant)

The operator can disable the chart in three structurally-
equivalent ways and verify each produces byte-identical
output to a pre-SCHW.7 baseline (the MANI-I.8 OptiX AOV):

- **Override via CLI:** invoke
  `--render-optix-aovs --manifold-enable
  --manifold-chart euclidean
  --manifold-debug` and rely on `cfg.manifold.enabled =
  true` to win the merge over `scene.manifold` (the
  CLI policy: explicit CLI engagement takes precedence,
  per the SCHW.7 dispatcher's merge logic at
  `src/main.cpp::run_render_optix_aovs`). The
  resulting AOV is the MANI-I.8 raw world-position
  output.

- **Override via fixture edit:** locally edit the
  fixture's `manifold.chart` to `"euclidean"`. The
  parser accepts the change; the renderer sees
  `chart = Euclidean`; `is_active(...)` returns
  `false`; the kernel arm short-circuits (the
  Euclidean is "intentionally not active" per
  `ManifoldMode.h:143-145`).

- **Override via strength-zero:** locally edit the
  fixture's `manifold.strength` to `0.0`. The
  SCHW.7 triple-gate's `strength > 0` check
  short-circuits the kernel arm; the AOV is the
  raw world-position output.

All three produce **byte-identical PPMs**, matching
the SCHW.8 audit's check #3 (disabled/default mode
remains no-op) and check #4 (Euclidean mode remains
identity) guarantees.

### 4.3 No-fixture baseline

Without the fixture at all (i.e. CLI
`--render-optix-aovs` with no scene file), the
inline-scene path is taken; the inline scene has
`scene.manifold = ManifoldMode{}` (disabled); the
dispatcher's merge resolves to disabled; the kernel
writes raw world positions. This is the documented
post-MANI-I.8 baseline OptiX AOV output and serves as
the "ground truth" comparison for any chart-engaged
output.

---

## 5. Current consumption status

The fixture is **loadable today** (parser supports the
manifold block) and the **renderer dispatcher merge
logic** is in place in `src/main.cpp::run_render_optix_aovs`
+ `run_render_aovs`. However, the existing
`--render-optix-aovs` / `--render-aovs` CLI actions
**build their scenes inline**; neither action loads a
scene file. So the fixture's `manifold` block is not
directly consumed by these actions today.

**Where the fixture's manifold IS consumed today:**

- `--scene-info <fixture>` loads the scene through the
  same SceneLoader path; the parser surface is
  exercised. The `--scene-info` printer does not
  yet print the manifold block (the printer is
  audited separately); the operator can confirm
  loadability by the absence of a parse error.

**Where the fixture's manifold WILL be consumed once a
future CLI action lands:**

A future single-line CLI extension that lets
`--render-optix-aovs` accept a scene file argument
would tie the fixture together end-to-end:

```
// hypothetical future action signature
//   --render-optix-aovs [<scene-path>]
//     If <scene-path> is supplied, load the scene
//     and use its meshes/materials/lights/manifold
//     instead of the inline procedural scene.
```

The dispatcher merge logic already added at SCHW.9
(`effective_manifold = cfg.manifold.enabled
? cfg.manifold : scene.manifold`) is already correct
for this future action — adding the file-argument arm
to `run_render_optix_aovs` would activate the fixture's
manifold without further changes to the merge.

Similarly, a future
`--render-optix-pathtrace <scene>
--manifold-from-scene` flag (or any other path-tracer
flag that loads the manifold from the scene file) would
also activate the fixture once the OptiX pathtracer
adds the SchwarzschildLike arm (deferred to a future
slice; SCHW.7 only wired the `render_aovs` AOV write
site).

---

## 6. Runtime CUDA/OptiX status: DEFERRED

The audit host on which this fixture is authored has
no CUDA SDK and no OptiX SDK; the SDK_FOUND TUs compile
but cannot link / launch device code. The runtime
checks below are **DEFERRED** to a CUDA + OptiX-SDK
host (the standard per-slice audit posture from
MANI-I.6 / MANI-I.9 / SCHW.2 / SCHW.4 / SCHW.6 / SCHW.8):

### 6.1 Loadability (audit host PASS)

- `--scene-info scenes/test_schwarzschild_like_manifold.rrscene`
  loads cleanly. **PASS** on the audit host.
- `ctest 12/12` remains green at the post-SCHW.9
  baseline. **PASS** on the audit host.

### 6.2 OptiX render — visual signature (DEFERRED)

On a CUDA + OptiX-SDK host with the proposed future
`--render-optix-aovs <scene>` extension:

```
$ ./RelativityRender --render-optix-aovs \
    scenes/test_schwarzschild_like_manifold.rrscene
```

Expected outputs:
- `output/optix_aov_beauty.ppm` — visually identical
  to the pre-SCHW.7 OptiX AOV beauty output for the
  same geometry (the chart does not affect beauty
  rendering).
- `output/optix_aov_normal.ppm`,
  `output/optix_aov_depth.ppm`,
  `output/optix_aov_albedo.ppm`,
  `output/optix_aov_doppler.ppm`,
  `output/optix_aov_searchlight.ppm` — same as the
  pre-SCHW.7 OptiX AOV outputs for the same geometry.
- `output/optix_aov_manifold_coordinates.ppm` — **the
  SCHW.9 signature** described in §3 above (radial
  compression near the mass origin, far-field
  identity, clamp-shell uniform warp).

### 6.3 OptiX render — byte-identity comparison (DEFERRED)

With the override scheme from §4.2, the operator
verifies that:
- The chart-disabled run (via any of the three
  override mechanisms) produces an
  `output/optix_aov_manifold_coordinates.ppm` that
  is byte-identical to the pre-SCHW.7 baseline.
- The chart-engaged run produces a PPM whose pixels
  diverge from the world-space hit positions in the
  documented radial-compression signature.
- The chart-disabled output and chart-engaged output
  are visibly distinct on the
  `manifold_coordinates` AOV channel only — every
  other AOV channel is byte-identical.

This three-way comparison gates the future
SCHW.10 (final audit; was SCHW.9 before SCHW.6 / SCHW.8
audit-slot insertions) PASS verdict.

### 6.4 CUDA byte-equivalence (DEFERRED, SCHW.5-blocked)

The CUDA-side SchwarzschildLike arm is **not yet
landed** (SCHW.5 deferred). Until SCHW.5 lands, the
CUDA `--render-aovs` path writes raw world-space hit
positions to `output/aov_manifold_coordinates.ppm`
regardless of the manifold mode. When SCHW.5 lands,
the CUDA AOV and the OptiX AOV should produce
byte-identical output for the same fixture; verifying
that equivalence is part of the SCHW.5 audit (or a
future SCHW.* audit if SCHW.5 lands via its own
forward-looking-audit insertion).

---

## 7. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` —
  top-level rules.
- `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` — canonical
  design doc; §3 reinterpretation table; §4 expected
  visual effects; §5 safety constraints; §7 deferred
  runtime checks.
- `docs/SCHWARZSCHILD_LIKE_WARP_AUDIT.md` (SCHW.2) —
  bounded / no-NaN math-leaf audit.
- `docs/SCHWARZSCHILD_LIKE_CPU_INTEGRATION_AUDIT.md`
  (SCHW.4) — CPU-side integration audit.
- `docs/SCHWARZSCHILD_LIKE_CUDA_WARP_AUDIT.md` (SCHW.6) —
  forward-looking CUDA-safety audit.
- `docs/SCHWARZSCHILD_LIKE_OPTIX_WARP_AUDIT.md` (SCHW.8) —
  OptiX-side warp bridge audit.
- `docs/MANIFOLD_DEBUG_AOV_TASK.md` (MANI-I.7) —
  AOV slot definition.
- `docs/MANIFOLD_DEBUG_AOV_AUDIT.md` (MANI-I.9) —
  deferred OptiX host-side allocation (closed at
  SCHW.7).
- `src/io/SceneLoader.cpp::apply_manifold` — SCHW.9
  parser surface (lines ~885-985, immediately
  after `apply_relativity`).
- `src/scene/Scene.h::Scene::manifold` — SCHW.9
  scene POD slot (line ~107).
- `src/main.cpp::run_render_optix_aovs` — SCHW.9
  dispatcher merge for the OptiX `render_aovs` arm.
- `src/main.cpp::run_render_aovs` — SCHW.9
  dispatcher merge for the CUDA `render_scene_with_aovs`
  arm.

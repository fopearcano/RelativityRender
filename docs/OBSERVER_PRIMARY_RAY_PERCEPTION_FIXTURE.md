# Observer Primary-Ray Perception Fixture (OBS-PERCEPT.9)

Date:   2026-05-18
Branch: `claude/rewrite-rendering-core-De71I`
Fixture file: `scenes/test_observer_primary_ray_perception.rrscene`
Related arc plan: `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
                  (OBS-PERCEPT.1)
Related impl audits: `docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md`
                     (OBS-PERCEPT.4)
                     `docs/OBSERVER_PRIMARY_RAY_OPTIX_AUDIT.md`
                     (OBS-PERCEPT.6)
Related debug-AOV task brief:
`docs/OBSERVER_PERCEPTION_DEBUG_AOV_TASK.md`
(OBS-PERCEPT.7)
Related precedent fixtures:
- `docs/OBSERVER_FRAME_FIXTURE.md` (OBS-F.2) —
  the observer fixture this OBS-PERCEPT.9 fixture
  shares geometry + camera framing with.
- `docs/FIELD_SCALAR_FIXTURE.md` (FIELD-I.13).
- `docs/FIELD_SCALAR_BEAUTY_FIXTURES.md`
  (FIELD-BEAUTY.7).

This document is the companion to
`scenes/test_observer_primary_ray_perception.rrscene`
— the operator-facing fixture scene that exercises
the OBS-PERCEPT.* arc's CUDA + OptiX primary-ray
aberration kernel arms (landed at OBS-PERCEPT.3 +
OBS-PERCEPT.5; audited at OBS-PERCEPT.4 +
OBS-PERCEPT.6). The fixture authors a non-trivial
**oblique** beta direction that distinguishes
visually from the OBS-F.2 fixture's axis-aligned
direction.

The fixture is the canonical SDK-host validation
surface for the OBS-PERCEPT.4 + OBS-PERCEPT.6
audits' runtime-deferred PPM verification scenarios
(§3.11 of each audit). It pairs with the
forthcoming OBS-PERCEPT.* debug AOV impl slice
(per the OBS-PERCEPT.7 task brief; the OBS-PERCEPT.8
slice landed the AOV data-model entries
`AOVType::ObserverAberrationMagnitude = 9` +
`AOVType::ObserverDirection = 10` for the future
kernel-arm bridge slice's consumption) to provide
visual verification of the perception transform's
per-pixel effect.

---

## 1. Purpose

The fixture exercises the OBS-PERCEPT.* arc's
unified
`apply_observer_primary_ray_aberration(observer_frame,
direction)` helper (landed at OBS-PERCEPT.3 in
`src/manifold/ObserverFrame.h:553+`; consumed by
both backend kernels per OBS-PERCEPT.3 + .5). The
fixture's parameters are sufficient to produce a
**visually verifiable** per-pixel aberration
pattern when the future SDK-host validation pass
runs the fixture with
`--observer-perception-mode relativistic`.

### 1.1 Three goals

- **Parser smoke test.** Loading the fixture via
  `RelativityRender --scene-info
  scenes/test_observer_primary_ray_perception.rrscene`
  exercises the existing `apply_relativity(...)`
  parser at `src/io/SceneLoader.cpp:731+` against
  the oblique `velocityDirection = [0.6, -0.8,
  0.0]` authoring (vs the OBS-F.2 fixture's
  axis-aligned `[0, 0, -1]`). Confirms the
  scene-loader normalization + the host-side
  observer-velocity computation (the parsed
  `observer.velocity = 0.5 × [0.6, -0.8, 0.0] =
  [0.3, -0.4, 0.0]` is verified by the
  `--scene-info` log line `observer_velocity :
  [0.300000, -0.400000, 0.000000]`).
- **Perception-transform runtime template.** When
  the future SDK-host validation pass runs the
  fixture, the operator invokes:
    - `RelativityRender --render-aovs
      --observer-perception-mode relativistic
      scenes/test_observer_primary_ray_perception.rrscene`
      (CUDA path) → produces a per-pixel
      beauty-modulated PPM whose visible
      aberration pattern matches the documented
      §3 expected signature.
    - `RelativityRender --render-optix-aovs
      --observer-perception-mode relativistic
      scenes/test_observer_primary_ray_perception.rrscene`
      (OptiX path) → produces the same PPM
      byte-identically (per the OBS-PERCEPT.6
      five-axis symmetry argument).
- **Oblique-direction distinction.** The fixture's
  beta direction `[0.6, -0.8, 0.0]` is oblique in
  the XY plane (the camera's screen-relative
  axes); this differs from the OBS-F.2 fixture's
  axis-aligned `[0, 0, -1]` (along the camera's
  forward axis). The oblique direction makes the
  per-pixel aberration's directional dependence
  more visually distinct:
    - In OBS-F.2: the beta is along the camera's
      view-axis; pixels mostly experience
      symmetric longitudinal aberration.
    - In OBS-PERCEPT.9: the beta is transverse +
      tilted; pixels experience asymmetric
      aberration depending on their angular
      position relative to the beta direction;
      visible distortion across the framebuffer.

### 1.2 Honest scope boundaries

The fixture is **runtime-validation-ready** but
SDK-host-dependent: the OBS-PERCEPT.4 +
OBS-PERCEPT.6 audits' runtime-deferred portions
are exercised against this fixture once an SDK
host is available. On the audit host today, the
fixture's value is the parser smoke test + the
documented expected visual signature.

The fixture **does NOT** engage the
`scalar_field` / `field_mapping` blocks
(FIELD-I.* + FIELD-BEAUTY.* arc surfaces). The
OBS-PERCEPT.* arc's perception transform is
orthogonal to the field-interpretation arc family
(per OBS-PERCEPT.1 §4); this fixture isolates the
observer-perception variable from every other
field-related variable.

The fixture **does NOT** engage the `manifold`
block (SCHW.* / PENROSE.* / MANI-I.* arc
surfaces). Per the operator's OBS-PERCEPT.9
brief: "no manifold chart required unless already
useful". For pure observer-frame perception
validation, the Euclidean default chart is
sufficient; this fixture isolates the
perception transform from every chart-related
variable.

---

## 2. Composition

The fixture composes three layers:

### 2.1 Geometry layer (lifted verbatim from OBS-F.2)

The geometry layer mirrors the OBS-F.2
`test_observer_frame.rrscene` precedent exactly,
providing the same six-sphere + ground-plane
composition. This gives:

- **Centre sphere** at `[0, 0.5, 0]` (radius 0.5,
  material-w).
- **Five marker spheres** (near-right, near-left,
  above, in-front, far-centre) at angular
  positions covering longitudinal, transverse,
  and oblique view directions.
- **Ground plane** at y = 0 spanning [-6, 6] in
  x + z.
- **Two lights** (directional key + environment
  sky).

The fixture deliberately reuses OBS-F.2 geometry
so that the renderer infrastructure exercised by
both fixtures is identical; any future runtime
divergence between them is attributable to the
**relativity block** difference, not to geometry
differences.

### 2.2 Camera layer (60° FOV — wider than OBS-F.2's 45°)

The fixture sets `camera.fovDegrees = 60.0`,
wider than the OBS-F.2 fixture's `45.0`. The
wider FOV brings more transverse pixels into
view, making the oblique-direction aberration's
asymmetric pattern more visible across the
framebuffer.

Camera position + forward + up vectors are
identical to OBS-F.2 (`[0.0, 1.2, 6.0]` /
`[0.0, -0.1, -1.0]` / `[0.0, 1.0, 0.0]`). The
camera looks slightly down toward the scene
centre; the marker spheres at various angular
positions fill the framebuffer.

### 2.3 Relativity block (oblique beta direction)

The fixture's `relativity` block authors:

```json
"relativity": {
  "enabled":             true,
  "betaVelocity":        0.5,
  "velocityDirection":   [0.6, -0.8, 0.0],
  "aberrationStrength":  1.0,
  "dopplerStrength":     1.0,
  "searchlightStrength": 1.0
}
```

Key parameters:

- **`enabled = true`**: master switch open.
- **`betaVelocity = 0.5`**: |beta| = 0.5 (half
  the speed of light in c-units). Same magnitude
  as OBS-F.2; safely below the OBSERVER.6 clamp
  shell (`max_beta = 0.999999f`).
- **`velocityDirection = [0.6, -0.8, 0.0]`**:
  oblique unit vector in the XY-plane
  (`0.6² + 0.8² = 1.0`; the Z-component is
  zero). Distinguishes from OBS-F.2's
  axis-aligned `[0, 0, -1]` (along the camera's
  forward axis). Computed observer velocity
  vector: `0.5 × [0.6, -0.8, 0.0] = [0.3,
  -0.4, 0.0]`.
- **`aberrationStrength = 1.0` /
  `dopplerStrength = 1.0` /
  `searchlightStrength = 1.0`**: standard
  artist-facing strengths (mirrors OBS-F.2).

The fixture's beta direction lies in the
camera's screen-relative XY plane (`X = right`;
`Y = up` from the camera's local basis). Beta
points to screen-positive-X + screen-negative-Y;
the per-pixel aberration's directional
dependence is most pronounced for pixels in the
screen's upper-left + lower-right quadrants.

### 2.4 No manifold block

The fixture omits the `manifold` scene block per
the operator's brief: "no manifold chart required
unless already useful". The Euclidean default
chart is sufficient for pure observer-perception
validation; engaging a chart-warping chart
(SchwarzschildLike, PenroseLike) would introduce
a second variable that confounds the
perception-transform diagnostic. Future fixture
slices may layer the perception fixture onto a
chart-aware fixture to exercise the
composability claim from OBS-PERCEPT.1 §4.

### 2.5 No `scalar_field` / `field_mapping` blocks

The fixture omits the FIELD-I.13 +
FIELD-BEAUTY.7 scene blocks. The OBS-PERCEPT.*
arc is orthogonal to the field arcs;
the fixture isolates the observer variable.

---

## 3. Expected visual signature

When the future SDK-host validation pass runs the
fixture with
`--observer-perception-mode relativistic`, the
expected per-pixel beauty PPM output:

### 3.1 Aberration pattern

- **Pixels along the beta direction `[0.6, -0.8,
  0.0]`** (which projects to screen-positive-X +
  screen-negative-Y, i.e. the lower-right
  quadrant of the framebuffer): smaller
  aberration magnitude (longitudinal pixels;
  beta is nearly along the ray direction).
- **Pixels in the upper-left quadrant**
  (screen-negative-X + screen-positive-Y, i.e.
  along `[-0.6, 0.8, 0.0]` — opposite to beta):
  also smaller aberration (the boost is nearly
  anti-parallel; symmetric to the
  longitudinal-parallel case).
- **Pixels in the perpendicular axis** (along
  `[0, 0, ±1]` or any direction perpendicular
  to the beta in 3D): larger aberration
  magnitude (transverse pixels experience the
  worst-case transverse Lorentz boost).
- **Visible pattern symmetry**: the aberration
  magnitude pattern has a reflection symmetry
  about the beta-direction axis (a 2D pattern
  symmetric across the line through screen
  centre along the projection of beta to
  screen).

The wider 60° camera FOV (vs OBS-F.2's 45°)
makes more of this directional-aberration
pattern visible across the framebuffer.

### 3.2 Doppler + searchlight pattern

- **Pixels along the beta direction** (where
  the observer is moving "toward" the source):
  blueshift + brightening.
- **Pixels opposite the beta direction** (where
  the observer is moving "away from" the
  source): redshift + dimming.
- The pattern is asymmetric in the camera's
  screen-relative coordinates because the beta
  direction is oblique.

### 3.3 Default invocation (no `--observer-perception-mode`)

```
RelativityRender --render-aovs scenes/test_observer_primary_ray_perception.rrscene
```

(WITHOUT `--observer-perception-mode relativistic`)

- The `--observer-perception-mode default`
  (Identity) gate closes the OBS-PERCEPT.3 +
  OBS-PERCEPT.5 unified helper's outer gate; the
  legacy `aberrateDirection(rel, ...)` path
  fires using `observer.velocity = [0.3, -0.4,
  0.0]` from the scene's `relativity` block.
  This is the post-OBS-P.2 baseline behaviour —
  legacy SR aberration applied; per-pixel
  pattern matches the pre-OBS-PERCEPT.3
  per-pixel pattern byte-identically.
- Aberration + Doppler + searchlight all apply
  using the legacy `observer.velocity` path.
  Same visual signature as
  `--observer-perception-mode relativistic`
  (the OBS-PERCEPT.3 helper's math leaf is the
  same `aberrateDirection(beta, direction)` math
  as the legacy path; the only difference is
  the input source: `observer.velocity` vs
  `observer_frame.beta`).
- Cross-backend bit-identity guaranteed
  structurally.

### 3.4 With `--observer-perception-mode relativistic`

```
RelativityRender --render-aovs --observer-perception-mode relativistic scenes/test_observer_primary_ray_perception.rrscene
```

- The outer gate opens; the OBS-PERCEPT.3 +
  OBS-PERCEPT.5 unified helper fires. The
  helper reads from `observer_frame.beta`
  (which the OBSERVER.6 adapter builds from
  `scene.observer.velocity` because no
  `--observer-beta` CLI flag overrides). The
  per-pixel aberration is byte-identical to
  the §3.3 default invocation (both paths
  consume the same beta value via different
  payload fields).
- The visible signature matches §3.1 + §3.2.

### 3.5 OptiX path

```
RelativityRender --render-optix-aovs --observer-perception-mode relativistic scenes/test_observer_primary_ray_perception.rrscene
```

- Produces byte-identical beauty PPM to the
  CUDA-side invocation (per the OBS-PERCEPT.6
  five-axis cross-backend symmetry argument).
- The SDK-host validation pass `cmp`-verifies
  byte-identity between
  `output/aov_beauty.ppm` and
  `output/optix_aov_beauty.ppm`.

---

## 4. Cross-backend equivalence

Both backends + the fixture produce byte-
identical beauty PPMs by construction. The
cross-backend guarantees inherit from the
OBS-PERCEPT.6 §3.7 five-axis symmetry argument:

- **Same `ObserverFrame` POD** consumed by both
  arms (`CudaSceneView::observer_frame` =
  `OptixLaunchParams::observer_frame` type;
  populated identically by the same OBSERVER.6
  adapter on both backends).
- **Same shared helper** (the
  `apply_observer_primary_ray_aberration(...)`
  helper at `ObserverFrame.h:553+` invoked
  identically on both backends).
- **Same math leaf** (the
  `rr::relativity::aberrateDirection(beta_vec,
  direction)` math leaf at
  `RelativityMath.h:112+`).
- **Same dispatch shape** (the kernel-arm
  dispatch's outer `params.enable_aberration` +
  inner `perception_active` discipline on both
  backends).
- **Same operator semantics** (identical
  per-pixel `ray.direction = ...` math result
  on both backends).

---

## 5. Audit-host smoke transcript

The OBS-PERCEPT.9 landing commit's audit-host
smoke verifies the fixture loads cleanly:

```
$ RelativityRender --scene-info \
    scenes/test_observer_primary_ray_perception.rrscene
[INFO] scene file: scenes/test_observer_primary_ray_perception.rrscene
[INFO]   version           : 1.0.0
[INFO]   render_settings   :
[INFO]     width             : 1280
[INFO]     height            : 720
[INFO]     samples_per_pixel : 1
[INFO]     max_depth         : 1
[INFO]     output_path       : output/observer_primary_ray_perception_fixture.ppm
[INFO]   camera            :
[INFO]     position          : [0.000000, 1.200000, 6.000000]
[INFO]     forward           : [0.000000, -0.099504, -0.995037]
[INFO]     up                : [0.000000, 0.995037, -0.099504]
[INFO]     fov_degrees       : 60.000000
...
[INFO]   relativity:
[INFO]     observer_velocity     : [0.300000, -0.400000, 0.000000]
[INFO]     |beta|                : 0.500000
[INFO]     enable_aberration     : true
[INFO]     enable_doppler        : true
[INFO]     enable_searchlight    : true
[INFO]     doppler_color_strength: 1.000000
[INFO]     searchlight_strength  : 1.000000
[INFO]     max_beta              : 0.999999
[INFO]   materials         : count 6
[INFO]   spheres           : count 6
[INFO]   meshes            : count 1
[INFO]   lights            : count 2
```

Empirically verified:
- Camera forward / up vectors normalised
  (forward magnitude pre-normalisation =
  `sqrt(0² + 0.1² + 1.0²) ≈ 1.005`; the
  scene-loader normalises during parse).
- Observer velocity computed as `betaVelocity ×
  velocityDirection = 0.5 × [0.6, -0.8, 0.0] =
  [0.3, -0.4, 0.0]`.
- `|beta| = 0.5` correctly extracted.
- Camera FOV = 60° (matches the fixture file).
- All seven `relativity` parameters read
  successfully.

---

## 6. Runtime SDK-host validation checks (DEFERRED)

The following runtime scenarios will be exercised
by the future SDK-host audit pass on a CUDA +
OptiX-SDK host:

### 6.1 Default-invocation byte identity (CUDA + OptiX)

Run on each backend WITHOUT
`--observer-perception-mode relativistic`:

```
RelativityRender --render-aovs scenes/test_observer_primary_ray_perception.rrscene
RelativityRender --render-optix-aovs scenes/test_observer_primary_ray_perception.rrscene
```

Verify:
- `aov_beauty.ppm` /
  `optix_aov_beauty.ppm` show the legacy
  `aberrateDirection(rel, ...)` per-pixel pattern
  using `observer.velocity = [0.3, -0.4, 0.0]`.
- The two backends' PPMs are byte-identical
  (`cmp` exit status = 0).
- The PPMs are byte-identical to the
  pre-OBS-PERCEPT.3 baseline on the same scene
  (the legacy else-branch preserves behaviour
  for default Identity mode).

### 6.2 Relativistic-mode byte identity (CUDA + OptiX)

Run on each backend WITH
`--observer-perception-mode relativistic`:

```
RelativityRender --render-aovs --observer-perception-mode relativistic scenes/test_observer_primary_ray_perception.rrscene
RelativityRender --render-optix-aovs --observer-perception-mode relativistic scenes/test_observer_primary_ray_perception.rrscene
```

Verify:
- The two backends' beauty PPMs are byte-
  identical (cross-backend bit-identity via
  the OBS-PERCEPT.6 five-axis symmetry).
- The relativistic-mode PPM is byte-identical
  to the §6.1 default-mode PPM on the same
  scene (the OBS-PERCEPT.3 helper's math leaf
  is the same `aberrateDirection(beta, dir)`
  as the legacy path; the only difference is
  the input source: `observer_frame.beta` vs
  `observer.velocity`; in the fixture, both
  values are equal because the OBSERVER.6
  adapter builds `observer_frame.beta` from
  the legacy `observer.velocity` in the
  absence of an explicit `--observer-beta` CLI
  override).

### 6.3 Override beta via CLI (CUDA + OptiX)

Run on each backend WITH the CLI overriding
the scene's beta:

```
RelativityRender --render-aovs --observer-perception-mode relativistic --observer-beta 0.5 --observer-direction 1,0,0 scenes/test_observer_primary_ray_perception.rrscene
```

Verify:
- The OBSERVER.6 adapter's CLI-override
  policy overrides the scene's `relativity`
  block beta direction.
- `observer_frame.beta = [0.5, 0, 0]` (the
  CLI's direction unit-normalised + scaled by
  CLI's beta magnitude).
- The PPM shows the per-pixel pattern matching
  the axis-aligned `[1, 0, 0]` direction
  (longitudinal pixels along screen-X
  experience minimum aberration; transverse
  pixels along screen-Y maximum).

### 6.4 Diagnostic AOV runtime (DEFERRED — requires future kernel-arm slice)

Run with `--observer-debug` once the future
kernel-arm bridge slice lands (consuming the
OBS-PERCEPT.8 data-model entries):

```
RelativityRender --render-aovs --observer-perception-mode relativistic --observer-debug scenes/test_observer_primary_ray_perception.rrscene
```

Verify the three diagnostic AOV PPMs (per the
OBS-PERCEPT.7 task brief §3 expected behaviour):
- `aov_observer_beta.ppm`: flat colour
  encoding `(0.3, -0.4, 0.0)` (the
  observer_frame.beta value; OBSERVER.13 AOV
  preserves baseline).
- `aov_observer_aberration_magnitude.ppm`:
  smooth per-pixel grayscale gradient
  matching §3.1 expected pattern (small at
  beta-aligned + beta-opposite pixels; large
  at transverse pixels).
- `aov_observer_direction.ppm`: flat colour
  encoding `normalize([0.3, -0.4, 0.0]) =
  [0.6, -0.8, 0.0]` (the unit-length direction
  vector along beta).

This scenario is DEFERRED twice: once for
SDK-host runtime, once for the kernel-arm
bridge slice that hasn't landed yet.

---

## 7. References

### 7.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (master rule #3 + #11 + #12 + #16 apply to
  the fixture's design).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §7.2 (the observer-frame Lorentz boost
  concept the OBS-PERCEPT.* arc operationalises;
  the fixture's relativity block authors the
  operator-facing beta + direction that the
  OBS-PERCEPT.6 adapter folds into the
  `ObserverFrame` payload).

### 7.2 OBS-PERCEPT.* arc references

- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1).
- `docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`
  (OBS-PERCEPT.2).
- `docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md`
  (OBS-PERCEPT.4) — the runtime-deferred SDK-
  host scenarios (§3.11) this fixture
  addresses.
- `docs/OBSERVER_PRIMARY_RAY_OPTIX_AUDIT.md`
  (OBS-PERCEPT.6) — the cross-backend symmetry
  argument (§3.7) underpinning §3.5 of this
  companion.
- `docs/OBSERVER_PERCEPTION_DEBUG_AOV_TASK.md`
  (OBS-PERCEPT.7) — the diagnostic-AOV task
  brief whose runtime checks (§8) this fixture
  partially addresses (the AOVs require the
  future kernel-arm bridge slice; the
  OBS-PERCEPT.8 data-model entries are in but
  the kernel arms are not).

### 7.3 Precedent fixture references

- `docs/OBSERVER_FRAME_FIXTURE.md` (OBS-F.2 —
  the precedent fixture whose geometry +
  camera-position layer is reused verbatim;
  the OBS-PERCEPT.9 fixture differs only in
  the **camera FOV** (60° vs 45°) + the
  **beta direction** (oblique `[0.6, -0.8,
  0.0]` vs axis-aligned `[0, 0, -1]`)).
- `docs/FIELD_SCALAR_FIXTURE.md` (FIELD-I.13
  — the precedent fixture+companion-doc shape
  this OBS-PERCEPT.9 companion mirrors).
- `docs/FIELD_SCALAR_BEAUTY_FIXTURES.md`
  (FIELD-BEAUTY.7 — the precedent
  two-fixture+companion-doc shape for the
  parallel arc family).

### 7.4 Source surface exercised

- `src/io/SceneLoader.cpp` — the
  `apply_relativity(...)` parser at line 731+
  consumes the fixture's `relativity` block
  verbatim (no parser extension needed).
- `src/manifold/ObserverFrame.h` — the
  `ObserverFrame` POD + the OBS-PERCEPT.3
  shared helper at `:553+` the future
  kernel-arm bridge slice consumes.
- `src/cuda/CudaTestKernel.cu` /
  `src/cuda/CudaPathTracer.cu` — the CUDA
  kernel arms at OBS-PERCEPT.3 sites; will
  process the fixture's payload at the
  future SDK-host validation pass.
- `src/optix/OptixPrograms.cu` — the OptiX
  kernel arms at OBS-PERCEPT.5 sites; same
  shape.

### 7.5 Source surface NOT exercised

- `src/field/` — orthogonal arc family;
  fixture omits.
- `src/manifold/` (other than ObserverFrame.h)
  — manifold chart-warp not exercised; fixture
  omits the `manifold` block.
- `src/scene/` / `src/io/` parsers for
  `scalar_field` / `field_mapping` blocks
  (FIELD-I.13 + FIELD-BEAUTY.7) — fixture
  omits those blocks.

### 7.6 Cross-fixture distinction

| Fixture                                                            | Beta direction        | FOV  | Engaged blocks                                  |
|--------------------------------------------------------------------|-----------------------|------|-------------------------------------------------|
| `test_observer_frame.rrscene` (OBS-F.2)                            | `[0, 0, -1]` axis     | 45°  | relativity                                      |
| `test_observer_primary_ray_perception.rrscene` (OBS-PERCEPT.9)     | `[0.6, -0.8, 0]` XY   | 60°  | relativity                                      |
| `test_scalar_field_diagnostic.rrscene` (FIELD-I.13)                | (no relativity)       | 45°  | scalar_field                                    |
| `test_scalar_field_color_multiplier.rrscene` (FIELD-BEAUTY.7)      | (no relativity)       | 45°  | scalar_field + field_mapping                    |
| `test_scalar_field_emission.rrscene` (FIELD-BEAUTY.7)              | (no relativity)       | 45°  | scalar_field + field_mapping                    |
| `test_schwarzschild_like_manifold.rrscene` (SCHW.9)                | (no relativity)       | 45°  | manifold                                        |
| `test_penrose_like_manifold.rrscene` (PENROSE.10)                  | (no relativity)       | 45°  | manifold                                        |

The fixture family preserves the one-variable-
difference principle: each fixture isolates one
arc-family's authoring surface; cross-arc
composition fixtures are reserved for future
slices.

# ObserverFrame Fixture (OBS-F.2)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Slice:  **OBS-F.2 — ObserverFrame Fixture
        Implementation.**
Mode:   Authoritative reference for the fixture
        scene introduced at OBS-F.2
        (`scenes/test_observer_frame.rrscene`)
        and the operator-facing CLI invocations
        that exercise it.

This document explains the purpose of the OBS-F.2
fixture scene, the expected visual behaviour when
the operator runs the fixture on a CUDA +
OptiX-SDK host, the default / no-op comparison
the operator can use as a regression baseline,
the cross-backend equivalence the OBSERVER.* +
OBS-P.* arcs structurally guarantee, the
audit-host smoke-test transcript demonstrating
the fixture loads cleanly on a no-SDK host, and
the runtime CUDA/OptiX status (DEFERRED on the
audit host; PASS-able when the operator runs
the fixture on an SDK-equipped host per the §6
runtime-validation checks).

---

## 1. Purpose

The fixture scene is the canonical authored
example of an ObserverFrame-driven render at
the OBSERVER.* + OBS-P.* sub-slice ladder's
current state. It serves three goals:

- **Parser-side regression anchor.** The
  fixture exercises ONLY the pre-existing
  `.rrscene` parser surface (the legacy
  `relativity` block from Stage 19E.1 + the
  camera / materials / spheres / meshes /
  lights blocks). NO new block, NO new
  field, NO `.rrscene` schema extension.
  The `apply_relativity(...)` helper at
  `src/io/SceneLoader.cpp` parses the
  fixture's `relativity.betaVelocity` +
  `velocityDirection` into
  `scene.observer.velocity` = `(0, 0, -0.5)`
  (verified at the audit-host smoke test in
  §5). The OBSERVER.6 adapter at
  `build_observer_frame_from_camera(...)`
  then routes this scene-authored velocity
  into `observer_frame.beta` when the
  operator engages
  `--observer-perception-mode relativistic`
  at invocation time.

- **OBSERVER.* + OBS-P.* arc runtime-
  validation fixture.** When the operator
  runs `--render-aovs
  --observer-perception-mode relativistic
  scenes/test_observer_frame.rrscene` (CUDA)
  or `--render-optix-aovs
  --observer-perception-mode relativistic
  scenes/test_observer_frame.rrscene`
  (OptiX) on an SDK host, the data path is
  end-to-end:
    - The scene-loader parses the fixture's
      `relativity` block into
      `scene.observer.velocity`.
    - The CLI parser sets
      `cfg.observer.perception_mode =
      ConstantVelocityMinkowski`.
    - The OBSERVER.6 adapter at the
      dispatcher resolves
      `build_observer_frame_from_camera(camera,
      scene.observer, cfg.observer)`. With
      `cfg.observer.beta_magnitude == 0`
      (CLI default), the adapter falls
      through to the legacy
      `observer.velocity` source per the
      OBSERVER.7 audit check #3 priority;
      `observer_frame.beta` carries the
      scene-authored `(0, 0, -0.5)` 3-velocity.
    - The OBSERVER.8 / OBSERVER.10 payload
      bridges thread `observer_frame` to
      both backends' launch params.
    - The OBS-P.2 kernel ternary at every
      gated call site (C-1, C-2 on CUDA;
      O-1, O-2, O-3 on OptiX) returns
      `true` because
      `observer_frame.perception_mode ==
      ConstantVelocityMinkowski`; the
      ternary's gated branch reads
      `observer_frame.beta`.
    - `precompute_relativity(observer_frame.beta)`
      produces the `(|beta|=0.5, gamma=
      1.1547)` snapshot.
    - `aberrateDirection` / `dopplerFactor`
      / `searchlightFactor` /
      `applyDopplerColor` consume the
      snapshot exactly as today's renderer
      consumes the snapshot derived from
      `scene.observer.velocity` directly.
    - The resulting Beauty pass exhibits
      the documented forward-blueshift +
      forward-aberration + searchlight-
      beaming signature on the
      forward-facing geometry.
    - The OBSERVER.13 `observer_beta`
      debug AOV (when `--observer-debug`
      is added) writes `(0, 0, -0.5)` per
      hit pixel; `(0, 0, 0)` per miss
      pixel.

- **Convergence-equivalence anchor for OBS-P.3
  audit's check #8 at SDK runtime.** When the
  operator runs the fixture WITHOUT
  `--observer-perception-mode relativistic`,
  the OBS-P.2 kernel ternary falls into the
  legacy fallback branch (the default
  `cfg.observer.perception_mode == Identity`
  routes the adapter to `rest_frame()`; the
  kernel reads `scene.observer.velocity`
  directly via the legacy SR path). The
  legacy path's Beauty PPM is **convergence-
  equivalent** to the gated-path Beauty PPM
  for the same fixture — same beta value
  reaches the SR helpers via either source
  path; same per-pixel output modulo
  single-precision rounding at the
  `precompute_relativity` step.

The fixture is **the load-bearing example** of
the OBSERVER.6 adapter's beta-resolution
priority (CLI overlay > zero-direction
fallback > legacy `Observer.velocity`)
routing scene-authored state onto the
gated path without any `.rrscene` schema
extension. Future arcs may add an
`observer` scene block to author
`PerceptionMode` directly from the scene
file; not in scope for OBS-F.

---

## 2. Fixture composition

`scenes/test_observer_frame.rrscene` ships the
following structure:

### 2.1 Render settings

- Resolution: `1280×720` (matches the existing
  SCHW.9 + PENROSE.10 fixture baselines).
- `samples_per_pixel = 1` + `max_depth = 1` for
  fast audit-host validation. The OBS-P.2
  migration's effect is on the SR helpers
  inside the closest-hit / miss arms, not on
  the bounce loop; `spp = 1` is sufficient
  for the AOV pipeline runtime check.
- `output_path = "output/observer_frame_fixture.ppm"`
  — descriptive name; doesn't collide with
  any existing fixture's output path.

### 2.2 Camera

- Position `(0, 1.2, 6.0)` — slightly above
  the ground plane, six units back from the
  centre sphere.
- Forward `(0, -0.1, -1.0)` — looking along
  `-Z` with a small downward tilt to keep
  the ground plane visible.
- Up `(0, 1.0, 0.0)` — Y-up world convention.
- FoV `45°` — fits the six sphere markers
  + the ground plane comfortably.

The camera's forward axis `(0, 0, -1)`
intentionally **aligns with the observer's
velocity direction** `(0, 0, -1)` from the
`relativity` block below. This produces the
documented forward-motion visual signature
(blueshift on the centre sphere + forward
aberration shrinking the field of view +
searchlight beaming brightening the
forward-facing geometry).

Aspect ratio is implicit from the render
settings (`1280/720 = 16/9 = 1.778`).

### 2.3 Relativity block (the OBS-F.2 surface)

```json
"relativity": {
  "enabled":             true,
  "betaVelocity":        0.5,
  "velocityDirection":   [0.0, 0.0, -1.0],
  "aberrationStrength":  1.0,
  "dopplerStrength":     1.0,
  "searchlightStrength": 1.0
}
```

This is the **only fixture-specific block** in
the scene file. The scene-loader's
`apply_relativity(...)` helper parses each
field per the Stage 19E.1 schema:

- `enabled = true` engages the existing
  Stage 19E.2 / SCHW.* SR pipeline (the
  `RelativityParams::enable_*` flags
  default-true; preserved verbatim).
- `betaVelocity = 0.5` is the observer's
  3-velocity magnitude in c-units. Below the
  `clampBeta(beta, 0.999999)` cap with a
  comfortable safety margin.
- `velocityDirection = [0.0, 0.0, -1.0]` is
  the unit-length direction (mirrors the
  `--render-demo` precedent). The
  scene-loader computes
  `scene.observer.velocity = velocityDirection
  * betaVelocity = (0, 0, -0.5)`.
- `aberrationStrength = 1.0` + `dopplerStrength
  = 1.0` + `searchlightStrength = 1.0` set
  the corresponding `RelativityParams`
  fields to their full-effect defaults.

The audit-host `--scene-info` smoke test
confirms the parsed state (§5 below):
`observer_velocity = [0.000000, 0.000000,
-0.500000]`; `|beta| = 0.500000`;
`enable_aberration = true`;
`enable_doppler = true`;
`enable_searchlight = true`;
`doppler_color_strength = 1.000000`;
`searchlight_strength = 1.000000`;
`max_beta = 0.999999`.

### 2.4 Materials (6 entries)

| ID | Name        | Base Colour          |
|----|-------------|----------------------|
| 0  | ground      | (0.55, 0.55, 0.60)   — neutral grey |
| 1  | marker-r    | (0.90, 0.20, 0.20)   — red |
| 2  | marker-g    | (0.20, 0.85, 0.30)   — green |
| 3  | marker-b    | (0.20, 0.30, 0.95)   — blue |
| 4  | marker-y    | (0.95, 0.85, 0.30)   — yellow |
| 5  | marker-w    | (0.95, 0.95, 0.95)   — white |

Bright, distinct colours so the Doppler
colour shift is visible per-sphere when the
observer is moving toward them on the
gated path.

### 2.5 Spheres (6 entries)

| Name        | Centre              | Radius | Material |
|-------------|---------------------|--------|----------|
| centre      | ( 0.0, 0.5,  0.0)   | 0.5    | 5 white  |
| near-right  | ( 1.5, 0.5,  0.0)   | 0.4    | 1 red    |
| near-left   | (-1.5, 0.5,  0.0)   | 0.4    | 2 green  |
| above       | ( 0.0, 2.0,  0.0)   | 0.4    | 3 blue   |
| in-front    | ( 0.0, 0.5,  1.5)   | 0.4    | 4 yellow |
| far-centre  | ( 0.0, 0.5, -3.0)   | 0.6    | 5 white  |

The six-sphere layout provides:

- A **centre marker** at `(0, 0.5, 0)` for the
  camera's primary subject.
- **Forward** (`in-front`) + **backward**
  (`far-centre`) spheres along the camera's
  Z axis to make the forward-aberration +
  forward-blueshift signature visible
  per-sphere when comparing gated vs legacy
  invocations.
- **Lateral** (`near-left`, `near-right`)
  spheres for off-axis aberration sampling.
- An **above** sphere for vertical-aberration
  reference.

### 2.6 Meshes (1 entry)

`ground-plane` — a 12×12 quad on the `y = 0`
plane, neutral-grey material. Two triangles
(four vertices). Mirrors the SCHW.9 +
PENROSE.10 ground-plane shape verbatim.

### 2.7 Lights (2 entries)

- `key` (directional): `direction = (-0.3,
  -0.7, -0.5)`, colour `(1.00, 0.95, 0.85)`,
  intensity `0.9`. Warm key light from the
  upper-front-left.
- `sky` (environment): colour `(0.35, 0.45,
  0.60)`, intensity `0.4`. Cool ambient sky
  tint.

Matches the SCHW.9 fixture's lighting setup
verbatim, providing consistent baseline
lighting for cross-fixture visual comparisons.

### 2.8 No manifold block

The fixture **does NOT** author a `manifold`
block. The existing default `ManifoldMode{}`
(disabled / Euclidean / strength = 0 /
debug off) is what the scene gets. This:

- Keeps the fixture **isolated** to observer-
  frame behaviour — no chart-arc interaction
  to muddy the visual signature.
- Confirms the OBSERVER.* arc's per-launch
  observer-frame field is **orthogonal** to
  the per-launch manifold field (both are
  carried on the same `CudaSceneView` /
  `OptixLaunchParams` payload; both are
  independently gated; engaging one does
  not engage the other).

An operator who wants to exercise OBS-P.2
alongside a manifold chart can compose
flags at invocation time:

```
--render-aovs --observer-perception-mode relativistic
              --manifold-enable
              --manifold-chart schwarzschild-like
              --manifold-strength 0.5
              --manifold-debug
              scenes/test_observer_frame.rrscene
```

The fixture itself does not author this
composition.

### 2.9 No observer block

The fixture **does NOT** author an
`observer` scene block. The OBSERVER.4
`--observer-*` CLI flag surface is the
operator-facing perception-mode authoring
path; the `relativity` block authors the
legacy SR observer velocity that the
OBSERVER.6 adapter routes onto
`observer_frame.beta` when the operator
engages `--observer-perception-mode
relativistic` at invocation time.

A future arc may add an `observer` scene
block (separate task brief; mirrors how
SCHW.9 added a `manifold` block). The
OBS-F arc deliberately avoids this scope
to stay minimum-scope per master rule #3.

---

## 3. Expected visual signature

### 3.1 Default invocation (no perception-mode flag)

Command:
```
RelativityRender --render-aovs
                 scenes/test_observer_frame.rrscene
```

What happens at the dispatcher:

- The scene-loader parses
  `scene.observer.velocity = (0, 0, -0.5)`.
- The CLI parser leaves
  `cfg.observer.perception_mode = Identity`
  (the default).
- The OBSERVER.6 adapter returns
  `rest_frame()` byte-for-byte (Identity
  path; verified at OBSERVER.7 audit check
  #2). `observer_frame.beta == (0, 0, 0)`.

What happens at the kernel (on an SDK host):

- The OBS-P.2 ternary at every gated site
  returns `false` (perception_mode !=
  ConstantVelocityMinkowski).
- The legacy fallback branch reads
  `scene.observer.velocity = (0, 0, -0.5)`.
- `precompute_relativity((0, 0, -0.5))` →
  `gamma = 1.1547`; `beta_mag = 0.5`.
- The downstream SR helpers
  (`aberrateDirection`, `dopplerFactor`,
  `searchlightFactor`, `applyDopplerColor`)
  apply the documented effects to every
  primary ray:
    - Forward aberration shrinks the field of
      view in the direction of motion (`-Z`).
    - Doppler blueshift increases the
      effective intensity on the
      forward-facing geometry (`in-front`
      sphere + the forward portion of the
      ground plane).
    - Doppler colour shift tints the
      forward-facing materials slightly
      cooler.
    - Searchlight beaming brightens the
      forward-facing pixels by a factor of
      `D^4 ≈ 1.5` at the centre +
      `in-front` spheres.

What's visible in `output/aov_beauty.ppm`:

- Forward-aberrated field of view (slightly
  zoomed-in look toward `-Z`).
- Brighter centre + `in-front` regions
  (searchlight boost).
- Cooler colour on forward-facing
  geometry (Doppler blueshift); warmer
  on rear-facing (`far-centre` sphere is
  slightly redshifted relative to its
  baseline white material).
- The other markers (`near-right`,
  `near-left`, `above`) experience smaller
  effects because their relative motion
  toward the observer is partial.

### 3.2 Gated invocation (with --observer-perception-mode relativistic)

Command:
```
RelativityRender --render-aovs
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame.rrscene
```

What happens at the dispatcher:

- The scene-loader parses
  `scene.observer.velocity = (0, 0, -0.5)`
  identically.
- The CLI parser sets
  `cfg.observer.perception_mode =
  ConstantVelocityMinkowski`.
- The OBSERVER.6 adapter's
  `ConstantVelocityMinkowski` branch
  resolves the beta priority: `cfg.observer.
  beta_magnitude == 0` (no `--observer-beta`
  flag) → falls through to the legacy
  `observer.velocity` source per the
  OBSERVER.7 audit check #3 priority.
- `observer_frame.beta` is populated with
  the legacy `(0, 0, -0.5)` value.

What happens at the kernel:

- The OBS-P.2 ternary at every gated site
  returns `true` (perception_mode ==
  ConstantVelocityMinkowski).
- The gated branch reads
  `observer_frame.beta = (0, 0, -0.5)`.
- `precompute_relativity((0, 0, -0.5))` →
  the **same** `gamma = 1.1547`,
  `beta_mag = 0.5` snapshot.
- The downstream SR helpers apply the
  **same** effects (the helpers themselves
  are unchanged; the only difference is
  the source field the beta read from).

What's visible in `output/aov_beauty.ppm`:

- **Convergence-equivalent** to §3.1's
  Beauty PPM. Both invocations route the
  same `(0, 0, -0.5)` beta to the same
  `precompute_relativity` helper to the
  same SR helpers; the per-pixel output
  is identical modulo single-precision
  rounding at the field-read step.

This is the OBS-P.3 audit's check #8
**cross-source convergence-equivalence at
runtime**. The deferred SDK-host pass (per
§6 below) verifies the byte-for-byte
similarity via `cmp` (allowing for small
single-precision rounding noise, see §4.2).

### 3.3 With OBSERVER.13 debug AOV

Command:
```
RelativityRender --render-aovs
                 --observer-debug
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame.rrscene
```

What happens at the kernel (in addition to
§3.2's behaviour):

- The OBSERVER.13 `observer_beta` AOV write
  arm fires at every hit pixel,
  writing `observer_frame.beta` (the
  gated-path `(0, 0, -0.5)` value)
  directly. Miss pixels write `(0, 0, 0)`.

What's visible in
`output/aov_observer_beta.ppm`:

- Every hit pixel decodes to `(0, 0, -0.5)`
  per channel — a flat colour across the
  visible geometry.
- In PPM encoding (8-bit per channel, [0,
  255] range), the negative Z component
  is clamped to 0 (since PPM doesn't
  support negative values directly); the
  per-pixel triple is roughly `(0, 0, 0)`
  in the PPM. The float-channel encoding
  preserves the negative; the visible PPM
  may appear dark.
- Miss pixels (sky / above the ground
  plane) are also `(0, 0, 0)` per the
  OBSERVER.13 miss convention.

Note: when the operator wants to **see**
the beta visually as a non-zero RGB
colour, they can author a positive
`velocityDirection` (e.g. `[1, 0, 0]` for
+X motion); the AOV's hit pixels then
decode to `(0.5, 0, 0)` — a flat red
across all visible geometry — making the
diagnostic visually obvious. The OBS-F.2
fixture uses `(0, 0, -1)` for forward-
motion physical correctness; the
OBSERVER.12 task brief §3.3
visualisation example uses `(1, 0, 0)`
for visual clarity of the debug AOV.

### 3.4 With OptiX (cross-backend equivalence)

Command:
```
RelativityRender --render-optix-aovs
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame.rrscene
```

What happens at the kernel (OptiX path):

- The OBS-P.2 ternary at the OptiX-side O-1
  + O-2 + O-3 sites returns `true`; reads
  `optixLaunchParams.observer_frame.beta`.
- Identical math leaf
  (`precompute_relativity` /
  `aberrateDirection` etc.) consumes the
  same beta.
- The OptiX `__closesthit__` + `__miss__`
  programs produce the same per-pixel
  Beauty values as the CUDA equivalent.

What's visible in
`output/optix_aov_beauty.ppm`:

- **Pixel-bit-identical** to the CUDA-side
  `output/aov_beauty.ppm` for the same
  fixture-mode invocation (the OBS-P.3
  audit's check #8 cross-backend semantic
  alignment at runtime).
- Verifiable via `cmp output/aov_beauty.ppm
  output/optix_aov_beauty.ppm` returning
  exit status `0` on the SDK host.

---

## 4. Cross-backend equivalence

### 4.1 Structural guarantee

The OBSERVER.* + OBS-P.* arcs establish
cross-backend semantic equivalence at four
points (verified at OBSERVER.11 audit check
#3 + OBSERVER.14 audit check #6 + OBS-P.3
audit check #8):

1. **Same shared type.** Both backends
   carry the same `rr::manifold::ObserverFrame`
   POD with the same field layout; both
   read `observer_frame.beta` as a `Vec3`
   triple.
2. **Same upstream adapter.** Both backend
   dispatchers invoke
   `build_observer_frame_from_camera(...)`
   with byte-identical arguments
   (`scene.camera.to_gpu()`, `scene.observer`,
   `cfg.observer`).
3. **Same kernel-side ternary.** The OBS-P.2
   ternary's shape (the `==
   ConstantVelocityMinkowski` comparison
   + the per-arm `?:` select + the
   downstream `precompute_relativity(...)`
   helper invocation) is byte-identical at
   every site (verified by `git diff`
   showing the same ternary text on both
   backends).
4. **Same downstream math.** Both backends
   call the same `RR_HD inline`
   `precompute_relativity` /
   `aberrateDirection` / `dopplerFactor`
   / `searchlightFactor` /
   `applyDopplerColor` helpers from
   `src/relativity/RelativityMath.h`.

### 4.2 Convergence-equivalence vs byte-identity

For the **default invocation** (§3.1) vs
the **gated invocation** (§3.2), the
expected equivalence is
**convergence-equivalent modulo
single-precision rounding**. The two paths
read the same beta value from two
different sources:

- §3.1 path: `scene.observer.velocity =
  (0, 0, -0.5)` → `precompute_relativity`
  directly.
- §3.2 path: `scene.observer.velocity` →
  OBSERVER.6 adapter → `observer_frame.beta`
  = `(0, 0, -0.5)` → `precompute_relativity`.

The OBSERVER.6 adapter's beta-resolution
in the "legacy fallback within
ConstantVelocityMinkowski" sub-case copies
the legacy `Observer.velocity` field
verbatim onto `observer_frame.beta` (no
math; pure assignment). So
`scene.observer.velocity` and
`observer_frame.beta` are byte-identical
`Vec3` values, and
`precompute_relativity(...)` produces
byte-identical snapshots. The Beauty PPM
should therefore be **byte-identical**
between the two invocations on the same
SDK host, not just convergence-equivalent.

For **cross-backend equivalence** between
CUDA + OptiX (§3.4 vs §3.2), the
expected equivalence is **also
byte-identical** by the same
single-source-of-truth math argument
(verified at OBSERVER.11 + OBS-P.3
audits).

For **path-tracer convergence** at high
spp (`--render-pathtrace` or
`--render-optix-pathtrace` with the
fixture), the expected equivalence is
**convergence-equivalent** because the
path tracer's per-sample RNG paths are
the same on both invocations (the
perception-mode ternary fires once per
launch; the bounce-loop RNG state is
unaffected by which beta source the
kernel read from); the accumulated
radiance converges to the same value at
high spp.

### 4.3 OBSERVER.13 AOV cross-backend equivalence

The OBSERVER.13 `observer_beta` AOV write
arm reads
`view.observer_frame.beta` (CUDA) /
`optixLaunchParams.observer_frame.beta`
(OptiX) directly — no perception
transform; pure field copy. With the
same fixture invocation on both
backends, the AOV PPMs are
**pixel-bit-identical** (verified at
OBSERVER.14 audit check #6).

---

## 5. Audit-host smoke-test transcript

The audit host (no CUDA SDK, no OptiX SDK)
cannot exercise the kernel-side OBS-P.2
ternary at runtime, but CAN verify:

- The fixture is valid JSON + parses
  through `apply_relativity(...)` cleanly.
- The fixture's parsed state matches the
  documented contents.
- The CLI surface (`--scene-info`,
  `--observer-perception-mode`,
  `--observer-debug`) recognises the
  fixture path + flags.
- The dispatcher's pre-guard log lines
  fire correctly even in the no-SDK
  fallback branch.

### 5.1 `--scene-info` parse verification

Command (run on the audit host at the
OBS-F.2 landing commit `5f8cabc`):

```
RelativityRender --scene-info
                 scenes/test_observer_frame.rrscene
```

Selected output (full transcript ~50
lines covering camera + relativity +
materials + spheres + meshes + lights):

```
[INFO] scene file: scenes/test_observer_frame.rrscene
[INFO]   version           : 1.0.0
[INFO]   render_settings:
[INFO]     width             : 1280
[INFO]     height            : 720
[INFO]     samples_per_pixel : 1
[INFO]     max_depth         : 1
[INFO]     output_path       : output/observer_frame_fixture.ppm
[INFO]   camera:
[INFO]     position          : [0.000000, 1.200000, 6.000000]
[INFO]     forward           : [0.000000, -0.099504, -0.995037]
[INFO]     up                : [0.000000, 0.995037, -0.099504]
[INFO]     fov_degrees       : 45.000000
[INFO]     aspect            : 1.777778
[INFO]   relativity:
[INFO]     observer_velocity     : [0.000000, 0.000000, -0.500000]
[INFO]     |beta|                : 0.500000
[INFO]     enable_aberration     : true
[INFO]     enable_doppler        : true
[INFO]     enable_searchlight    : true
[INFO]     doppler_color_strength: 1.000000
[INFO]     searchlight_strength  : 1.000000
[INFO]     max_beta              : 0.999999
[INFO]   materials:
[INFO]     count             : 6
[INFO]   spheres:
[INFO]     count             : 6
...
```

The transcript confirms:

- **JSON parsing succeeds** with no errors.
- The `relativity` block resolves
  `observer_velocity = (0, 0, -0.5)`
  exactly as designed (the scene-loader
  computes `velocityDirection *
  betaVelocity = (0, 0, -1) * 0.5 = (0,
  0, -0.5)`).
- `|beta| = 0.5` is below the `clampBeta(
  0.999999)` cap.
- The three `enable_*` flags + the three
  strengths populate to the expected
  defaults.
- The camera's `forward` and `up` are
  re-orthogonalised by the host-side
  `Camera::look_at` / basis-recompute
  logic (the input `forward = (0, -0.1,
  -1)` is normalised; the input `up`
  is reprojected onto the plane
  orthogonal to forward).
- 6 materials + 6 spheres + 1 mesh (the
  ground plane) + 2 lights load.

### 5.2 CLI surface smoke tests

Commands (all run on the audit host):

```
RelativityRender --render-aovs
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame.rrscene
```

Output:
```
[INFO] scene file: scenes/test_observer_frame.rrscene
[INFO] aovs manifold mode: disabled (chart=euclidean, ...)
[INFO] aovs observer config: constant-velocity-minkowski (|beta|=0.000000, dir=[0.000000, 0.000000, 0.000000], tau=0.000000)
[ERROR] --render-aovs requires CUDA. ...
```

Confirms:

- The fixture-load + scene-info log
  fires.
- The OBSERVER.4 manifold-mode +
  OBSERVER.13 observer-config log lines
  fire BEFORE the CUDA-required error
  (audit-host smoke-test visible per the
  MANI-CONSUME.1 precedent).
- The observer config shows
  `constant-velocity-minkowski (|beta|=0,
  dir=[0,0,0], tau=0)` — the CLI's
  `cfg.observer` state. The
  `|beta|=0.0` is correct because no
  `--observer-beta` was passed; the
  OBSERVER.6 adapter's downstream
  fallback path reads the scene's
  `relativity.betaVelocity = 0.5`
  (separate path; not reflected in this
  log line which echoes the CLI
  overlay state only).

### 5.3 Composability with --observer-debug

```
RelativityRender --render-aovs
                 --observer-debug
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame.rrscene
```

Output (audit-host):
```
[INFO] aovs manifold mode: disabled ...
[INFO] aovs observer config: constant-velocity-minkowski (|beta|=0.000000, ...)
[ERROR] --render-aovs requires CUDA. ...
```

Same dispatcher behaviour; the
`--observer-debug` flag is parsed and
stored on `cfg.observer.debug_visualization`
(verified at OBSERVER.14 audit check #1)
but the actual AOV buffer allocation +
PPM save would happen only on an SDK
host.

---

## 6. Runtime SDK-host validation checks

The OBS-F.1 task brief §6.4 enumerated 7
deferred SDK-host runtime checks. Each must
be exercised on a CUDA + OptiX-SDK host
before the OBS-F.* arc closes (the
OBS-F.3 audit's check #10 runtime status
converts from DEFERRED → PASS when these
all pass). All checks use the OBS-F.2
fixture exclusively; no other scene is
required.

### 6.1 Default-mode byte-identity (CUDA + OptiX)

Run:
```
RelativityRender --render-aovs
                 scenes/test_observer_frame.rrscene
```
AND the matching `--render-optix-aovs ...`
invocation.

Verify:

- `output/aov_beauty.ppm` + every other
  existing AOV PPM byte-identical to
  the pre-OBSERVER.* + pre-OBS-P.*
  reference. The kernel falls into the
  OBS-P.2 legacy fallback branch +
  reads scene-authored `observer.velocity`
  exactly as today's renderer would.
- Same for the OptiX path.

### 6.2 Opt-in path engagement (CUDA)

Run:
```
RelativityRender --render-aovs
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame.rrscene
```

Verify:

- The Beauty PPM exhibits visible
  forward-blueshift + forward-aberration
  + searchlight beaming on the
  forward-facing geometry (the centre +
  `in-front` spheres + the forward
  portion of the ground plane).
- The OBSERVER.6 adapter's beta-resolution
  fallback path (CLI overlay zero →
  legacy `observer.velocity`) routes
  correctly: `observer_frame.beta = (0,
  0, -0.5)`.
- The OBS-P.2 kernel ternary engages the
  gated branch (verifiable by adding
  a temporary `printf`-based debug log
  if needed; the kernel reads
  `observer_frame.beta` not
  `observer.velocity` on this path).

### 6.3 Cross-source convergence-equivalence

Run §6.1's CUDA invocation AND §6.2's
CUDA invocation. Compare:

```
cmp output/aov_beauty.ppm
    output/aov_beauty.ppm-gated-snapshot
```

(The operator captures the §6.1 output
first as a snapshot, then runs §6.2 and
compares.)

Verify: **byte-identical** (per §4.2's
argument — both paths route the same
`(0, 0, -0.5)` beta to the same
`precompute_relativity` helper).

If the operator observes
single-precision floating-point
discrepancies (~LSB-level differences
on a small fraction of pixels), the
discrepancy is documented as
"acceptable convergence-equivalence
modulo single-precision rounding"
because the float-evaluation order
inside `precompute_relativity` is the
same on both paths (the assertion is
not about float associativity).

### 6.4 OBSERVER.13 debug-AOV consistency

Run:
```
RelativityRender --render-aovs
                 --observer-debug
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame.rrscene
```

Verify:

- `output/aov_observer_beta.ppm` exists.
- Every hit pixel decodes (after PPM
  encoding's [0, 1]-clamp on negative
  values) to a value consistent with
  the float-channel encoding of
  `(0.0, 0.0, -0.5)`. The negative Z
  may visually appear as `(0, 0, 0)`
  in the 8-bit RGB encoding because
  the standard PPM clamp drops
  negative values to 0.
- Miss pixels (sky regions) write
  `(0, 0, 0)`.

For visual clarity of the debug AOV's
non-trivial value, the operator can
override the direction to a positive
component via CLI:

```
RelativityRender --render-aovs
                 --observer-debug
                 --observer-perception-mode relativistic
                 --observer-beta 0.5
                 --observer-direction 1,0,0
                 scenes/test_observer_frame.rrscene
```

This invocation's CLI overlay
**overrides** the scene-authored beta
(per OBSERVER.6 adapter's
beta-resolution priority); every hit
pixel decodes to `(0.5, 0.0, 0.0)` — a
flat red across all visible geometry,
making the diagnostic visually
obvious (the OBSERVER.14 audit check
#4 documented expected signature).

### 6.5 Cross-backend AOV equivalence

Run §6.2's invocation on both backends.
Compare:

```
cmp output/aov_beauty.ppm
    output/optix_aov_beauty.ppm
```

Verify exit status `0` (cross-backend
**byte-identity** at the Beauty pass).

Similarly:

```
cmp output/aov_observer_beta.ppm
    output/optix_aov_observer_beta.ppm
```

Verify exit status `0` (the OBSERVER.13
debug AOV is also byte-identical).

### 6.6 OptiX path-trace convergence

Run:
```
RelativityRender --render-optix-pathtrace
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame.rrscene
```

(The Stage 20J progressive path-tracer
runs at 1-spp + 16-spp checkpoints by
default.)

Verify:

- `output/optix_pathtrace_spp1.ppm` +
  `output/optix_pathtrace_spp16.ppm`
  produced.
- The 16-spp checkpoint converges to a
  Beauty similar to the §6.5
  AOV-pipeline Beauty (modulo path-
  tracer / direct-lighting differences;
  not pixel-identical, just visually
  consistent on the centre + ground
  regions).
- No NaN / Inf / negative-radiance
  pixels (the SR helpers' output is
  bounded by construction for finite
  beta < 1).

### 6.7 RelativityParams orthogonality

Modify the fixture (or use a separate
fixture variant) to set
`aberrationStrength = 0.0` (which
maps to `RelativityParams::enable_aberration
= false`). Re-run:

```
RelativityRender --render-aovs
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame_no_aberration.rrscene
```

Verify:

- The Beauty PPM shows NO forward-
  aberration effect (the field of view
  is unchanged from the no-relativity
  baseline).
- Doppler + searchlight effects still
  apply (those flags weren't disabled).

This verifies the OBS-P.3 audit's
check #6 `RelativityParams` orthogonality
contract at runtime: the perception-mode
gate selects WHICH beta the SR helpers
receive; the `RelativityParams` flags
select WHETHER the helpers are called
at all.

---

## 7. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3
  ("no fake stubs") satisfied (the
  fixture is a real authored scene
  exercising real pre-existing parser
  surface + real pre-existing kernel
  reads); #1 + #2 + #5 preserved.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §3.3 Observer Frame + §6 GPU
  integration strategy — defines the
  contract the fixture exercises.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
  §6 GPU integration strategy + §8
  non-goals — defines the data path
  the fixture validates end-to-end.
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15) §10 risk #2 — the
  capstone that catalogued the fixture
  follow-up as deferred; OBS-F.2 is
  the implementation lifting that
  deferral.
- `docs/OBSERVER_FRAME_FIXTURE_TASK.md`
  (OBS-F.1) — the task brief OBS-F.2
  consumed as its canonical reference.
- `docs/OBSERVER_DEBUG_AOV_TASK.md`
  (OBSERVER.12) §5 — the original
  deferred fixture-task reference
  this companion doc lifts.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — established the
  OBSERVER.13 `observer_beta` AOV
  surface the fixture exercises
  via `--observer-debug`.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_TASK.md`
  (OBS-P.1) — established the OBS-P.2
  kernel-side guard shape this
  fixture exercises end-to-end.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3) — audited the OBS-P.2
  migration's convergence-equivalence
  property; §6.3 above is the SDK-host
  runtime verification of OBS-P.3's
  check #8 cross-source equivalence.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — established the
  OBSERVER.6 adapter's beta-resolution
  priority that the fixture relies on
  (legacy `Observer.velocity` source
  reached via the
  `ConstantVelocityMinkowski` + zero
  CLI-overlay path).
- `docs/SCHWARZSCHILD_LIKE_FIXTURE.md`
  (SCHW.9 companion) — the precedent
  fixture-doc shape this
  OBSERVER_FRAME_FIXTURE.md mirrors.
- `docs/PENROSE_LIKE_FIXTURE.md`
  (PENROSE.10 companion) — second
  precedent fixture-doc shape.
- `docs/STAGE_19E_AUDIT.md` /
  `docs/RELATIVITY_AUDIT.md` (Stage
  19E.1) — established the
  `relativity` scene-block parser
  surface (`apply_relativity` in
  `src/io/SceneLoader.cpp`) that the
  fixture uses verbatim.
- `scenes/test_observer_frame.rrscene`
  (new at OBS-F.2) — the fixture
  this doc accompanies.
- `scenes/test_schwarzschild_like_manifold.rrscene`
  (SCHW.9 fixture) — the precedent
  fixture-scene shape (camera +
  materials + spheres + meshes +
  lights + one extra block of relevant
  state).
- `scenes/test_penrose_like_manifold.rrscene`
  (PENROSE.10 fixture) — second
  precedent fixture-scene shape.
- `scenes/test_relativity.rrscene`
  (Stage 19E.1) — the minimal
  17-line relativity-only precedent;
  the OBS-F.2 fixture is a fuller
  scene that combines a similar
  `relativity` block with full
  geometry / camera / lights /
  materials.
- `src/io/SceneLoader.cpp` —
  `apply_relativity(...)` parses
  the fixture's `relativity` block;
  unchanged at OBS-F.2.
- `src/scene/Scene.h` — the
  `Scene::observer` +
  `Scene::relativity` fields the
  fixture populates via the
  scene-loader; unchanged at
  OBS-F.2.
- `src/manifold/CameraObserverAdapter.h`
  — the OBSERVER.6 adapter that
  routes the fixture's
  scene-authored beta onto
  `observer_frame.beta` on the
  gated path; unchanged at
  OBS-F.2.
- `src/cuda/CudaTestKernel.cu` /
  `src/optix/OptixPrograms.cu` —
  the OBS-P.2-migrated kernel
  sites the fixture exercises on
  an SDK host; unchanged at
  OBS-F.2.
- `src/renderer/AOV.h` /
  `src/renderer/AOV.cpp` — the
  OBSERVER.13 `AOVType::ObserverBeta`
  enumerator + factory the fixture
  exercises via `--observer-debug`;
  unchanged at OBS-F.2.
- `docs/BUILD_PLAN.md` — the
  OBS-F.2 entry lands when the impl
  commits.

# ObserverFrame Fixture — Task Definition (OBS-F.1)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this task definition; the fixture +
        companion doc land in a subsequent slice that
        consumes this doc as its canonical brief.

This document defines the work for **OBS-F.1 —
ObserverFrame fixture task**, the first slice of the
**Observer Fixture (OBS-F) arc** that lifts the
deferred fixture follow-up the OBSERVER.12 task
brief §5 + OBSERVER.15 capstone §10 risk #2
catalogued. The fixture is a dedicated `.rrscene`
authored scene that exercises non-default
`ObserverFrame` fields without changing any
existing default scene's output — enabling SDK-host
runtime validation of the OBS-P.2 kernel-side
perception-transform migration without requiring
new CLI flag invocations.

Prerequisite slices already green (the OBS-F.1
brief consumes their state by reference; the
fixture verifies their end-to-end runtime behaviour
on an SDK host):

- **OBSERVER.1**  — Planning slice (`eee9d6b`).
- **OBSERVER.4**  — Config / CLI bridge (`16600dc`).
- **OBSERVER.6**  — Camera-to-observer adapter
  (`e2cde15`).
- **OBSERVER.8**  — CUDA observer payload bridge
  (`12f4942`).
- **OBSERVER.10** — OptiX observer payload bridge
  (`977ff73`).
- **OBSERVER.13** — Observer debug AOV
  implementation (`b34e265`).
- **OBSERVER.15** — Observer-Frame arc capstone
  audit (`2e3a9e3`) verdict
  PASS_WITH_RUNTIME_DEFERRED. §10 risk #2
  catalogued the fixture follow-up as the
  optional next slot.
- **OBS-P.1**   — Kernel-side perception transform
  migration task (`fc2d83e`).
- **OBS-P.2**   — Kernel-side perception transform
  migration implementation (`c729b53`).
- **OBS-P.3**   — Kernel-side perception transform
  migration audit (`dc2869e`) verdict
  PASS_WITH_RUNTIME_DEFERRED.

Adjacent precedents this task brief mirrors:

- **`docs/SCHWARZSCHILD_LIKE_FIXTURE.md`** (SCHW.9
  companion) — the precedent fixture-doc shape for
  a manifold-aware scene + its companion doc.
- **`docs/PENROSE_LIKE_FIXTURE.md`** (PENROSE.10
  companion) — the precedent fixture-doc shape for
  the second-chart-family scene.
- **`scenes/test_schwarzschild_like_manifold.rrscene`**
  (SCHW.9 fixture) — the precedent scene shape
  the OBS-F fixture mirrors (camera + materials +
  geometry + lights + one extra block).
- **`scenes/test_relativity.rrscene`** — the
  precedent minimal relativity fixture (17 lines)
  the OBS-F fixture extends.
- **`docs/OBSERVER_DEBUG_AOV_TASK.md`** (OBSERVER.12
  task brief §5) — the deferred fixture-task
  reference that OBS-F.1 finally lifts.

---

## 1. Exact goal

**Add an isolated `.rrscene` fixture
(`scenes/test_observer_frame.rrscene`) that
exercises non-default `ObserverFrame` fields via
the operator-facing CLI surface, without
modifying any default scene's output or
requiring a `.rrscene` schema extension.**

The fixture is the authoritative reference scene
for the OBS-P.2 kernel-side perception-transform
migration: when an SDK-equipped host runs
`--render-aovs --observer-perception-mode
relativistic scenes/test_observer_frame.rrscene`,
the OBSERVER.6 adapter resolves the scene-authored
observer velocity into
`view.observer_frame.beta`, the OBS-P.2 kernel
ternary engages the gated path, and the kernel
emits a Beauty pass with visible aberration /
Doppler / searchlight effects.

The fixture serves three goals:

- **Parser-side regression anchor.** The fixture
  exercises ONLY the pre-existing `.rrscene`
  surface (the legacy `relativity` block from
  Stage 19E.1 + the camera / materials /
  geometry / lights blocks). No new block, no
  new field. `--scene-info <fixture>` on the
  audit host loads the fixture cleanly (the
  scene-loader's
  `apply_relativity(...)` + `apply_camera(...)`
  + `apply_materials(...)` + `apply_meshes(...)`
  + `apply_spheres(...)` + `apply_lights(...)`
  helpers all accept the fixture without error;
  verified at the OBS-F.2 implementation slice).
- **OBSERVER.* arc runtime-validation fixture.**
  When the operator runs the fixture on an SDK
  host via `--render-aovs
  --observer-perception-mode relativistic
  scenes/test_observer_frame.rrscene`, the
  OBSERVER.6 adapter falls through to the
  scene-authored `observer.velocity`
  (because `cfg.observer.beta_magnitude == 0`
  at the CLI default), populates
  `observer_frame.beta` with the resolved
  3-velocity, and the OBS-P.2 kernel ternary
  engages the gated path. The resulting Beauty
  pass exhibits the documented aberration /
  Doppler / searchlight effects; the OBSERVER.13
  `observer_beta` AOV (when `--observer-debug`
  is added) writes the same beta vector
  bit-identically across both backends.
- **Convergence-equivalence anchor for OBS-P.2
  audit.** When the operator runs the legacy
  invocation `--render-aovs
  scenes/test_observer_frame.rrscene` (WITHOUT
  `--observer-perception-mode relativistic`),
  the OBS-P.2 kernel ternary falls into the
  legacy fallback branch (the `Identity`
  default routes through), reads the same
  scene-authored `observer.velocity`, and
  produces convergence-equivalent output to
  the gated-path invocation above. This is the
  OBS-P.3 audit's check #8 cross-backend
  semantic alignment verification at runtime —
  the same beta value reached via either source
  path produces the same per-pixel Beauty
  output.

The fixture **does NOT** require a `.rrscene`
schema extension at OBS-F.2. The OBSERVER.4
`--observer-*` CLI flag surface is the
operator-facing perception-mode authoring path;
the `.rrscene` `relativity` block authors the
legacy SR observer velocity that the adapter
consumes on the no-CLI-overlay path. Future arcs
may add an `observer` scene block (separate task
brief; mirrors how SCHW.9 added a `manifold`
block); not in scope for OBS-F.

---

## 2. Fixture requirements

The implementation slice (OBS-F.2) must author
the following fixture content:

### 2.1 Observer velocity (the load-bearing requirement)

The `relativity` block must author:

- **`enabled = true`** — engages the existing
  Stage 19E.1 / 19E.2 SR pipeline (kernel-side
  flag guards `RelativityParams::enable_*`
  default-true; preserved verbatim by OBS-P.2).
- **`betaVelocity > 0`** — non-zero observer
  speed. Recommended value `0.5` (the
  documented `--render-demo` precedent + the
  OBSERVER.12 task brief §3.3 non-default
  visualisation example). Below the
  `clampBeta(beta, 0.999999)` cap with a
  comfortable safety margin; produces visible
  aberration without numerical instability.
- **`velocityDirection = [0.0, 0.0, -1.0]`** —
  finite, non-zero, unit-length 3-vector along
  the camera's forward axis (the
  `--render-demo` precedent: observer moves
  forward at speed `betaVelocity`). The
  fixture's camera (§2.4) is oriented so this
  produces the expected forward-blueshift +
  forward-aberration signature on the Beauty
  pass.
- **`aberrationStrength = 1.0`** — full
  aberration effect (the `RelativityParams`
  default).
- **`dopplerStrength = 1.0`** — full Doppler
  effect.
- **`searchlightStrength = 1.0`** — full
  searchlight effect.

The OBSERVER.6 adapter's beta-resolution
priority (verified at OBSERVER.7 audit check
#3) routes this scene-authored velocity into
`observer_frame.beta` on the
`--observer-perception-mode relativistic`
gated path. The kernel reads
`observer_frame.beta` per the OBS-P.2 ternary.

### 2.2 Perception mode

The fixture does NOT author a `.rrscene`
`observer` block (no schema extension at
OBS-F.2). The operator engages
`PerceptionMode::ConstantVelocityMinkowski` via
the OBSERVER.4 `--observer-perception-mode
relativistic` CLI flag at invocation time:

```
RelativityRender --render-aovs
                 --observer-perception-mode relativistic
                 scenes/test_observer_frame.rrscene
```

Without the flag the operator gets the legacy
fallback path (the kernel reads
`scene.observer.velocity` directly; the SR
helpers apply the same effects with the same
beta value → convergence-equivalent output by
construction).

### 2.3 Simple geometry

Mirroring the SCHW.9 / PENROSE.10 fixture
structure with minor adjustments for SR
visualisation:

- **A small sphere palette** at a representative
  radial layout: a central sphere + 4-6
  surrounding spheres at varying distances /
  angles. Recommended: 5-6 visible spheres
  total, materials with bright distinct
  colours (red / green / blue / yellow / white)
  so the Doppler colour shift is visible
  per-sphere.
- **A ground plane** (single mesh; quad
  triangulated from 4 vertices), neutral grey
  material — the existing SCHW.9 / PENROSE.10
  ground-plane shape carries over.
- **Total scene complexity ≤ 20 primitives**
  (the SCHW.9 fixture ships 6 spheres + 1
  ground; OBS-F can be similar or simpler).
  Keeps SDK-host runtime times short for the
  audit-pass verification (target: render in
  < 1 second on a modern GPU).

### 2.4 Safe camera framing

- **Position**: 6-8 units behind the central
  sphere along `+Z` (mirroring SCHW.9's
  `position = [0.0, 1.2, 6.0]`).
- **Forward**: `[0.0, 0.0, -1.0]` (looking
  along `-Z`). Aligns with `velocityDirection`
  so the observer is moving forward toward the
  scene — produces the documented forward-
  blueshift + forward-aberration signature.
  Recommended: a slight downward tilt
  (`forward = [0.0, -0.1, -1.0]`) to keep the
  ground plane in frame.
- **Up**: `[0.0, 1.0, 0.0]` (Y-up world
  convention).
- **FoV**: `45°` (the SCHW.9 / PENROSE.10
  precedent).
- **Aspect ratio** implicit from render
  settings (`1280×720` → 16:9).

### 2.5 Resolution + render settings

- **Resolution**: `1280×720` (matches SCHW.9 +
  PENROSE.10 baselines).
- **samples_per_pixel**: `1` (single-pass for
  fast SDK-host validation; the OBS-P.2
  migration's effect is on the SR helpers
  inside the closest-hit / miss arms, not on
  the bounce loop — `spp = 1` is sufficient).
- **max_depth**: `1` (matches the SCHW.9
  baseline).
- **output_path**: `output/observer_frame_fixture.ppm`
  (descriptive name for the fixture-render
  PPM).

### 2.6 No manifold chart required

The fixture does NOT author a `manifold` block.
The existing default `ManifoldMode{}`
(disabled / Euclidean / strength = 0 / debug
off) is what the scene gets. This:

- Keeps the fixture **isolated** to observer-
  frame behaviour — no chart-arc interaction
  to muddy the visual signature.
- Confirms the OBSERVER.* arc's per-launch
  observer-frame field is orthogonal to the
  per-launch manifold field (both are carried
  on the same `CudaSceneView` /
  `OptixLaunchParams` payload; both are
  independently gated; engaging one does not
  engage the other).

An operator who wants to exercise the
OBS-P.2 migration alongside a manifold chart
can compose flags at invocation time
(`--observer-perception-mode relativistic
--manifold-enable --manifold-chart
schwarzschild-like ...`); the fixture itself
does not author the manifold engagement.

---

## 3. Expected behavior

The implementation slice must satisfy three
load-bearing behavioural invariants:

### 3.1 Default scenes unchanged

Every existing default scene
(`test_camera.rrscene`,
`test_full_scene.rrscene`,
`test_lights.rrscene`,
`test_materials.rrscene`,
`test_mesh.rrscene`,
`test_relativity.rrscene`,
`test_render_settings.rrscene`,
`test_spheres.rrscene`,
`test_textured_material.rrscene`,
`test_schwarzschild_like_manifold.rrscene`,
`test_penrose_like_manifold.rrscene`) is
byte-unchanged. The OBS-F.2 implementation
slice adds ONE new file
(`scenes/test_observer_frame.rrscene`) +
ONE new companion doc
(`docs/OBSERVER_FRAME_FIXTURE.md`) + a
`BUILD_PLAN.md` entry. No existing scene
file is modified.

### 3.2 Fixture clearly exercises observer-frame consumption

When the operator runs the fixture with
`--observer-perception-mode relativistic` on
an SDK-equipped host:

- The OBSERVER.6 adapter at the dispatcher
  resolves
  `build_observer_frame_from_camera(camera,
  scene.observer, cfg.observer)` →
  `ObserverFrame` with
  `perception_mode == ConstantVelocityMinkowski`
  AND `beta == scene.observer.velocity`
  (the scene-authored
  `velocityDirection × betaVelocity`).
- The OBS-P.2 kernel ternary at every gated
  call site (C-1, C-2 on CUDA; O-1, O-2,
  O-3 on OptiX) returns `true`; the kernel
  reads `observer_frame.beta` instead of the
  legacy `observer.velocity`.
- The downstream SR helpers
  (`precompute_relativity` →
  `aberrateDirection` / `dopplerFactor` /
  `searchlightFactor` / `applyDopplerColor`)
  consume the gated beta; the Beauty pass
  exhibits visible aberration + Doppler
  blueshift + searchlight beaming on the
  forward-facing geometry.
- The OBSERVER.13 `observer_beta` debug AOV
  (when `--observer-debug` is added) writes
  the scene-authored beta vector at every
  hit pixel; the AOV is convergence-
  equivalent across CUDA + OptiX backends.

When the operator runs the fixture WITHOUT
`--observer-perception-mode relativistic`:

- `cfg.observer.perception_mode` stays at
  the default `Identity`.
- The OBSERVER.6 adapter returns
  `rest_frame()` byte-for-byte (the
  Identity-path short-circuit; OBSERVER.7
  audit check #2).
- The OBS-P.2 kernel ternary falls into the
  legacy fallback branch; the kernel reads
  the scene-authored `observer.velocity`
  directly via the legacy SR path.
- The Beauty pass output is
  **convergence-equivalent** to the gated-
  path invocation above (same beta value
  reaches the SR helpers via the legacy
  path; same per-pixel arithmetic; same
  output modulo single-precision floating-
  point rounding at the per-pixel level).
  This is the OBS-P.3 audit's check #8
  cross-source-equivalence at runtime.

### 3.3 Runtime visual / AOV validation DEFERRED until CUDA / OptiX host

The audit host (no CUDA, no OptiX SDK) can
verify:

- The fixture loads cleanly via
  `--scene-info scenes/test_observer_frame.rrscene`.
- The fixture parses through the existing
  `apply_*(...)` helpers with no errors.
- The fixture's parsed `Scene` state shape
  matches the documented contents (the
  audit slice OBS-F.3 may extend
  `tests/io_tests.cpp` if useful; not
  required at OBS-F.2 per minimum-scope
  rule).
- The companion doc
  (`docs/OBSERVER_FRAME_FIXTURE.md`)
  documents the expected visual signature
  at every operator-facing invocation
  variant (with / without
  `--observer-perception-mode
  relativistic`; with / without
  `--observer-debug`; with / without
  `--render-aovs` vs
  `--render-optix-aovs`).

The audit host CANNOT verify:

- The Beauty PPM byte-content (requires CUDA
  + OptiX device launches).
- The OBSERVER.13 `observer_beta` AOV PPM's
  per-pixel beta encoding.
- Cross-backend `cmp` of the two AOV PPMs
  (the OBS-P.3 audit's check #8 deferred
  cross-backend equivalence test at
  runtime).
- The convergence-equivalence between the
  gated-path and legacy-path invocations.

These runtime checks are **DEFERRED** to a
CUDA + OptiX-SDK host pass per the standard
OBSERVER.9 / OBSERVER.11 / OBSERVER.14 /
OBSERVER.15 / OBS-P.3 deferral pattern.
After the SDK host pass runs the OBS-F.2
fixture, the verdict for the entire
OBSERVER.* + OBS-P.* + OBS-F.* arc family
converts from PASS_WITH_RUNTIME_DEFERRED →
PASS.

---

## 4. Files likely involved

The implementation slice is expected to touch
the following files. Numbers in parentheses
are rough net-line estimates from the SCHW.9
+ PENROSE.10 fixture precedents.

| Layer | File | Why |
|-------|------|-----|
| Fixture scene | `scenes/test_observer_frame.rrscene` (~80 lines) | New JSON scene file. Mirror of `test_schwarzschild_like_manifold.rrscene`'s shape with: (a) no `manifold` block; (b) a non-trivial `relativity` block (§2.1); (c) 5-6 spheres + 1 ground plane + 2 lights (mirrors SCHW.9 / PENROSE.10 visual layout). |
| Companion doc | `docs/OBSERVER_FRAME_FIXTURE.md` (~400-500 lines) | New markdown doc mirroring `docs/SCHWARZSCHILD_LIKE_FIXTURE.md` / `docs/PENROSE_LIKE_FIXTURE.md` in structure: §1 Purpose; §2 Fixture composition; §3 Expected visual signature (per operator-invocation variant); §4 Cross-backend equivalence; §5 Audit-host smoke-test transcript; §6 Runtime SDK-host validation checks (the 4 checks from §3.3 above); §7 References. |
| Docs | `docs/BUILD_PLAN.md` (~150-200 lines) | OBS-F.2 entry mirroring the OBSERVER.* per-slice entry shape. |
| Parser | none expected | The fixture exercises only the pre-existing `apply_relativity(...)` + camera / materials / geometry / lights helpers. NO new scene-loader code, NO new schema field, NO `.rrscene` version bump. |
| Source code | none expected | No source-code change at OBS-F.2; the fixture is data + documentation only. |
| Tests | optional (`tests/io_tests.cpp` extension; ~20 lines) | OPTIONAL: if the operator wants a host-side parser test, add 1-2 RR_CHECK assertions verifying the fixture loads cleanly + carries the expected `relativity` block values. Not required at OBS-F.2 per minimum-scope rule; can land at OBS-F.3 audit slice if useful. |
| CMake | none expected | No new target; no library change; no link-line change. The fixture is a data file the existing `--scene-info` action loads via the existing `rr_io` library. |

**Estimated total net-line delta:** ~500-700
lines, comparable to SCHW.9 + PENROSE.10
fixture pairs (each shipped a similar
fixture-scene + companion-doc pair).

---

## 5. What must not be touched

Per master rule #3 and the operator's
OBS-F.1 brief, the implementation slice MUST
NOT:

- **No CUDA / OptiX kernel changes.**
  Operator brief explicitly forbids. Every
  `.cu` / `.cuh` / `OptixPrograms.cu` /
  `OptixRenderer.cpp` file is byte-
  unchanged. The OBS-P.2 kernel surface
  carries forward verbatim.
- **No new perception model.** The three
  existing `PerceptionMode` enumerators
  (`Identity` / `ConstantVelocityMinkowski`
  / `CurvedChartGeodesicPlaceholder`)
  preserved verbatim.
- **No new manifold math.** The
  fixture explicitly does NOT author a
  `manifold` block; the SCHW.* / PENROSE.*
  arc surface is unchanged.
- **No `.rrscene` schema extension.** The
  fixture uses only pre-existing scene-
  block fields (`render_settings`,
  `camera`, `relativity`, `materials`,
  `spheres`, `meshes`, `lights`). No new
  `observer` scene block at OBS-F (a
  future arc may add one with its own
  task-brief discipline).
- **No new CLI flag.** The
  OBSERVER.4 `--observer-*` surface + the
  OBSERVER.13 `--observer-debug` flag
  comprise the entire CLI surface the
  operator needs.
- **No new AOV.** The OBSERVER.13
  `observer_beta` debug AOV is the only
  observer-related AOV; the fixture
  exercises it through the existing
  `--observer-debug` gate without any
  new code.
- **No `RelativityParams` field changes.**
  The existing six flags
  (`enable_aberration` / `enable_doppler`
  / `enable_searchlight` /
  `doppler_color_strength` /
  `searchlight_strength` / `max_beta`)
  preserved verbatim. The fixture sets
  them all to their defaults (effectively
  via the `relativity` block's
  `aberrationStrength` /
  `dopplerStrength` / `searchlightStrength`
  fields).
- **No source-code change at OBS-F.2.**
  The fixture is data + documentation
  only. If the OBS-F.3 audit slice
  decides to add 1-2 parser-test
  RR_CHECK assertions to
  `tests/io_tests.cpp`, that's a
  separate scope decision.
- **No `BUILD_PLAN.md` historical-record
  rewrite.** Every prior entry stays
  as-is.
- **No `MODULE_MAP.md` update.** The
  OBS-F arc is documentation +
  data-fixture only; no module-map
  status change.
- **No alteration of the OBSERVER.1
  plan / OBSERVER.15 capstone audit /
  OBS-P.1 task / OBS-P.3 audit.** All
  prior arc documents preserved verbatim
  as historical snapshots.
- **No Kerr / Kruskal work.**
- **No new chart family.**
- **No C4D / server / UI / node-editor
  touch.**

---

## 6. PASS criteria

The implementation slice's acceptance gate
is satisfied when ALL of the following hold:

### 6.1 Structural

- [ ] `scenes/test_observer_frame.rrscene`
      (new) exists and contains the
      documented structure per §2 above
      (one `relativity` block with the
      specified fields + camera +
      materials + geometry + lights;
      no `manifold` block; no
      `observer` block).
- [ ] The fixture is valid JSON (parses
      via the existing `rr::io::load(...)`
      helper without error).
- [ ] The fixture's `relativity.enabled`
      is `true`; `betaVelocity > 0`;
      `velocityDirection` is finite +
      non-zero.
- [ ] The fixture's camera + materials +
      geometry + lights blocks follow the
      SCHW.9 / PENROSE.10 fixture shape.
- [ ] No new file beyond the fixture
      scene + companion doc +
      BUILD_PLAN.md entry.
- [ ] No source-code change.
- [ ] No CMake change.

### 6.2 Behavioural

- [ ] Every existing default scene's
      output is byte-unchanged
      (verified by `git diff` filtered
      against `scenes/` showing only the
      new `test_observer_frame.rrscene`
      file; no modification to existing
      `.rrscene` files).
- [ ] `--scene-info
      scenes/test_observer_frame.rrscene`
      loads cleanly on the audit host
      (verified at OBS-F.2 landing or
      OBS-F.3 audit; no parse error).
- [ ] The audit-host build remains
      green at the post-OBS-P.3
      baseline (`ctest 12/12 PASS`;
      `relativity_tests: 841/841`;
      `manifold_identity_tests: 408/408`;
      `cli_tests: 274/274`;
      `renderer_tests: 27/27`).
- [ ] The companion doc
      (`docs/OBSERVER_FRAME_FIXTURE.md`)
      documents the expected visual
      signature for at least 4 operator
      invocation variants (with /
      without
      `--observer-perception-mode
      relativistic`; with / without
      `--observer-debug`; both CUDA
      and OptiX paths).

### 6.3 Documentation

- [ ] `docs/OBSERVER_FRAME_FIXTURE.md`
      added with the documented 7-section
      structure (mirrors SCHW.9 /
      PENROSE.10 companion docs).
- [ ] `docs/BUILD_PLAN.md` OBS-F.2 entry
      added (mirrors the OBSERVER.* /
      OBS-P.* per-slice entry shape:
      "What ships / What does NOT ship /
      Acceptance / Module status
      changes").

### 6.4 Runtime SDK-host (DEFERRED)

The audit-host build cannot directly
verify the fixture's runtime visual
signature. The runtime checks below are
DEFERRED behind the audit host's existing
no-CUDA / no-OptiX-SDK fallback, matching
the OBSERVER.9 / OBSERVER.11 / OBSERVER.14
/ OBSERVER.15 / OBS-P.3 deferral pattern.

Each deferred check must be exercised on
a CUDA + OptiX-SDK host before the OBS-F
arc closes (the OBS-F.3 audit's check #10
runtime status converts from DEFERRED →
PASS when the SDK host pass runs):

- [ ] **Default-mode byte-identity**
      (CUDA + OptiX): `--render-aovs
      scenes/test_observer_frame.rrscene`
      (no `--observer-perception-mode`)
      produces Beauty + every existing
      AOV PPM byte-identical to the
      pre-OBSERVER.* + pre-OBS-P.*
      reference (the kernel falls into
      the legacy fallback branch +
      reads scene-authored
      `observer.velocity` exactly as
      today's renderer would).
- [ ] **Opt-in path engagement**:
      `--render-aovs
      --observer-perception-mode
      relativistic
      scenes/test_observer_frame.rrscene`
      produces Beauty PPM with visible
      aberration + Doppler blueshift +
      searchlight beaming (the documented
      visual signature of the fixture's
      `betaVelocity = 0.5` +
      `velocityDirection = [0, 0, -1]`
      authored state).
- [ ] **Cross-source convergence-
      equivalence**: the gated-path and
      legacy-path invocations above
      produce convergence-equivalent
      Beauty PPMs (modulo single-
      precision rounding; both paths
      route the same beta value to the
      SR helpers).
- [ ] **OBSERVER.13 debug-AOV
      consistency**: `--render-aovs
      --observer-debug
      --observer-perception-mode
      relativistic
      scenes/test_observer_frame.rrscene`
      produces
      `output/aov_observer_beta.ppm`
      whose hit pixels decode to
      `(0.0, 0.0, -0.5)` per channel
      (the scene-authored
      `velocityDirection × betaVelocity`
      = `(0, 0, -1) × 0.5 = (0, 0,
      -0.5)`).
- [ ] **Cross-backend AOV equivalence**:
      the CUDA-side
      `output/aov_observer_beta.ppm`
      and the OptiX-side
      `output/optix_aov_observer_beta.ppm`
      are pixel-bit-identical for the
      same fixture-mode invocation
      (verified by `cmp` exit status
      `0`).
- [ ] **OptiX path-trace
      convergence**:
      `--render-optix-pathtrace
      --observer-perception-mode
      relativistic
      scenes/test_observer_frame.rrscene`
      at the standard 16-spp
      checkpoint produces a Beauty
      PPM whose pixel histogram
      converges to the gated-path
      AOV invocation's Beauty above.
- [ ] **`RelativityParams`
      orthogonality**: with the
      fixture's
      `aberrationStrength = 0.0`
      manually edited in (or via a
      future CLI flag), aberration is
      skipped on the Beauty pass; the
      operator can verify that the
      `RelativityParams` flags
      continue to gate the SR helpers
      independently of the OBS-P.2
      `perception_mode` gate
      (orthogonality contract from
      OBS-P.1 §4.3 + OBS-P.3 audit
      check #6 carry-forward).

---

## 7. Cross-references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no
  fake stubs") satisfied (the fixture is a
  real authored scene exercising real
  pre-existing parser surface +
  pre-existing kernel reads); #1 ("Build
  incrementally") + #2 ("Keep every step
  compilable") + #5 ("No CPU ray tracing as
  production path") preserved.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §3.3 Observer Frame — defines the
  contract the fixture exercises.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
  §6 GPU integration strategy + §8
  non-goals — defines the data path the
  fixture validates end-to-end on an SDK
  host.
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15) — capstone §10 risk #2
  catalogued the fixture follow-up; this
  OBS-F.1 task lifts the deferral.
- `docs/OBSERVER_DEBUG_AOV_TASK.md`
  (OBSERVER.12) — §5 deferred fixture
  follow-up; this OBS-F.1 task is the
  implementation of that deferred item.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — audit verified the
  OBSERVER.13 `observer_beta` AOV
  surface the fixture exercises via
  `--observer-debug`.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_TASK.md`
  (OBS-P.1) — the kernel-side migration
  task brief that established the
  guarded read-site swap pattern.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3) — audit verified the OBS-P.2
  migration produces convergence-
  equivalent output between the gated
  path and the legacy fallback. The
  fixture's `--observer-perception-mode
  relativistic` invocation engages the
  gated path; the no-flag invocation
  engages the legacy fallback path.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — establishes the
  adapter's beta-resolution priority
  (CLI overlay > zero-direction
  fallback > legacy
  `Observer.velocity`). The fixture's
  `relativity` block populates the
  legacy path; the OBSERVER.6 adapter
  resolves it onto
  `observer_frame.beta` on the
  gated path.
- `docs/SCHWARZSCHILD_LIKE_FIXTURE.md`
  (SCHW.9 companion) — the precedent
  fixture-doc shape the OBS-F.2
  companion doc mirrors.
- `docs/PENROSE_LIKE_FIXTURE.md`
  (PENROSE.10 companion) — the second
  precedent fixture-doc shape.
- `scenes/test_schwarzschild_like_manifold.rrscene`
  (SCHW.9 fixture) — the precedent
  fixture-scene shape the OBS-F.2
  fixture mirrors (camera + materials
  + geometry + lights + one block of
  relevant state).
- `scenes/test_relativity.rrscene` —
  the minimal relativity precedent
  (17 lines); OBS-F's fixture is a
  fuller scene that includes the
  relativity block.
- `scenes/test_penrose_like_manifold.rrscene`
  (PENROSE.10 fixture) — second
  precedent scene with the same
  shape.
- `src/io/SceneLoader.cpp` —
  `apply_relativity(...)` (Stage
  19E.1) parses the fixture's
  `relativity` block. No
  modification needed.
- `src/scene/Scene.h` — the
  `Scene::observer` +
  `Scene::relativity` fields the
  fixture populates via the
  scene-loader. No modification
  needed.
- `src/manifold/CameraObserverAdapter.h`
  — the OBSERVER.6 adapter that
  routes the fixture's
  scene-authored beta into
  `observer_frame.beta` on the
  gated path.
- `src/cuda/CudaTestKernel.cu` /
  `src/optix/OptixPrograms.cu` —
  the OBS-P.2-migrated kernel
  sites the fixture exercises on
  an SDK host. No
  modification needed.
- `docs/BUILD_PLAN.md` — the
  OBS-F.2 implementation slice's
  entry lands when the impl
  commits.

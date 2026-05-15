# Observer-Frame Rendering Plan (OBSERVER.1)

Date:   2026-05-15
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this design document; the implementation
        lands in subsequent OBSERVER.* sub-slices that
        consume this doc as their canonical brief.

This document is the design for the **observer-frame
rendering** arc — the next layer of the manifold-
rendering pivot after the SchwarzschildLike + PenroseLike
chart arcs closed (PASS_WITH_RUNTIME_DEFERRED capstone
verdicts at `docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md` and
`docs/PENROSE_LIKE_ARC_AUDIT.md`, consumption-gap closed
at `docs/MANIFOLD_CONSUMPTION_GAP_AUDIT.md`).

The observer-frame layer is the **second axis of the
manifold-rendering ontology** from
`docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3:

- **§3.1 Coordinate Chart** — answers *"where does
  space map?"*. Landed at MANI-I.10 (SchwarzschildLike)
  + MANI-I.11 (PenroseLike).
- **§3.3 Observer Frame** — answers *"how does an
  observer perceive it?"*. **This arc.** The
  `ObserverFrame` POD already exists at MANIFOLD.3
  (`src/manifold/ObserverFrame.h`) but no kernel call
  site consumes it; the renderer still feeds on the
  legacy `rr::relativity::Observer` directly. The
  arc's job is to wire `ObserverFrame` into the
  dispatch path so the legacy Observer becomes a
  thin compatibility shim atop the Manifold Core's
  observer-frame contract.

Per the operator's task brief, this is **documentation
only**. No source code, no implementation, no test
binary changes. The implementation lands in the
OBSERVER.2 → OBSERVER.8 sub-slice ladder defined in §7
below.

---

## 1. Purpose

**Observer-frame rendering turns coordinate
deformation into experienced perception.** The
manifold-chart arcs (SCHW.* + PENROSE.*) deformed
the *chart-space coordinates* a scene maps onto;
those deformations show up on the
`ManifoldCoordinates` debug AOV but **do not change
what an observer sees** in the beauty pass. The
observer-frame arc bridges that gap: it gives the
renderer a first-class model of "the observer's
tetrad-local measurements", with the chart-space
coordinate transforms feeding into observer-frame
operations rather than terminating at an AOV write.

The arc separates two concerns that the existing
codebase mixes:

- **"Where space maps"** — the chart's
  `world_to_chart` / `chart_to_world` maps; the
  SCHW.* / PENROSE.* charts; the future Kerr /
  Kruskal charts. This is **chart-space geometry**.

- **"How an observer perceives it"** — the
  observer's 4-velocity + tetrad + perception-mode
  flags; the aberration / Doppler / searchlight
  transforms that convert chart-space ray state
  into tetrad-local quantities the operator sees in
  pixels. This is **observer-frame perception**.

Today the second concern is split awkwardly:

- `rr::relativity::Observer` (in `src/relativity/`)
  carries the 3-velocity `beta` field;
- `rr::relativity::RelativityParams` (in
  `src/relativity/`) carries the per-effect enable
  flags + strengths;
- `rr::manifold::ObserverFrame` (in
  `src/manifold/`) carries the 4-velocity +
  tetrad + proper/coordinate time but is
  **unconsumed** by any renderer call site;
- the kernels (`CudaTestKernel.cu`,
  `OptixPrograms.cu`) read the legacy `Observer` +
  `RelativityParams` directly and call
  `aberrateDirection` / `dopplerFactor` /
  `searchlightFactor` from
  `src/relativity/RelativityMath.h`.

The OBSERVER.* arc consolidates these into a single
`ObserverFrame`-driven pipeline:
`rr::relativity::Observer` becomes a thin
compatibility shim (already provided by
`observer_frame_from(...)` / `to_relativity_observer(...)`
at MANIFOLD.3); the kernels read
`launch_params.observer_frame` instead of
`launch_params.observer` + `launch_params.params`;
the existing aberration / Doppler / searchlight
math is reinterpreted as the
Minkowski + constant-velocity-frame specialisation of
the observer-frame tetrad boost
(architecture-doc §7.2).

The architectural goal is symmetric with the
manifold-chart arcs:

- **SchwarzschildLike / PenroseLike charts** are
  *artistic, bounded, master-rule-#3-honest*
  coordinate transforms — not physical solvers.
- **Observer frames** in this arc are *artistic,
  bounded, master-rule-#3-honest* perception
  transforms — not physical curved-spacetime
  observers. The constant-velocity Minkowski
  observer is the only frame the arc implements
  concretely; the curved-chart observer frame
  (where the tetrad must be parallel-transported
  along a geodesic) is **deferred** to a future
  GR-aware arc that the architecture-doc §8
  non-goals continue to disclaim.

---

## 2. Current state (post-MANI-CONSUME.2)

The renderer's manifold infrastructure is
**structurally complete** for the two
implemented chart families:

- **Manifold charts exist.** SchwarzschildLike +
  PenroseLike both have wired CPU seam
  (`ManifoldTransform.h`), CUDA kernel arms
  (`CudaTestKernel.cu`), OptiX kernel arms
  (`OptixPrograms.cu`), fixture scenes, and
  per-slice + per-arc audits. Both arcs' capstones
  returned **PASS_WITH_RUNTIME_DEFERRED**.

- **Scene-driven manifold consumption works.** The
  SCHW.9 scene parser surface + the MANI-CONSUME.1
  dispatcher extension together let the operator
  drop a manifold-enabled `.rrscene` into
  `--render-aovs <path>` / `--render-optix-aovs
  <path>` and have the chart-aware AOV write arm
  engage automatically. Verified at MANI-CONSUME.2
  audit. Both capstones' largest deferred items
  are CLOSED.

- **Runtime host validation remains deferred.** The
  audit host has no CUDA SDK and no OptiX SDK; the
  SDK_FOUND TUs compile cleanly but cannot link /
  launch device code. The plan-level §7 / §9
  fixture-render suites are deferred to an
  SDK-equipped host. MANI-I.12 (final cross-host
  audit) per the integration plan §11 is the
  documented next step for runtime SDK
  verification.

- **`ObserverFrame` POD exists but is
  unconsumed.** Landed at MANIFOLD.3
  (`docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md`). The
  POD carries the seven fields the architecture-doc
  §3.3 specifies (`position4`, `velocity4`, `beta`,
  tetrad `right`/`up`/`forward`, `proper_time`,
  `coordinate_time`). Bridge helpers
  `observer_frame_from(rr::relativity::Observer)`
  and `to_relativity_observer(ObserverFrame)` map
  losslessly between the new contract and the
  legacy SR type. The
  `is_normalised_timelike(frame, metric, tol)`
  helper validates `g_{μν} u^μ u^ν = -1`. **No
  kernel call site reads any `ObserverFrame`
  field today.**

- **Legacy `rr::relativity::Observer` +
  `RelativityParams` are kernel-consumed.** The
  CUDA + OptiX kernels read the legacy types via
  `scene.observer` + `scene.relativity` (and the
  parallel OptiX launch-params slots). Six
  scene-aware actions thread these through:
  `--render-pathtrace`, `--render-mesh-scene`,
  `--render-material-scene`, `--render-direct-lighting`,
  `--render-aovs`, `--render-optix-aovs` (the last
  two now via the MANI-CONSUME.1 scene-file
  loading path).

The arc's job is to migrate the kernel-side reads
from the legacy types onto `ObserverFrame` without
breaking any of these six actions. The bridge
helpers at MANIFOLD.3 make the migration mechanical:
on the default path the legacy `Observer` round-trips
through `observer_frame_from(...) →
ObserverFrame → to_relativity_observer(...)` with
no loss; the kernels can read `frame.beta` instead
of `observer.velocity` and continue calling the
existing `aberrateDirection` / `dopplerFactor` /
`searchlightFactor` helpers with the result.

---

## 3. Observer-frame concepts

The arc consumes the seven `ObserverFrame` POD
fields from MANIFOLD.3 verbatim. Each concept maps
onto a field:

### 3.1 Observer position

`ObserverFrame::position4` (Vec4). The observer's
4D position in chart coordinates: `(x⁰, x¹, x², x³)`,
where `x⁰` is the time component (or `E` for a
4-momentum) and the spatial trio matches the
chart's `(y, z, w)` convention. For the
constant-velocity Minkowski observer the default
`(0, 0, 0, 0)` is the scene-rest observer at the
chart origin; future curved-chart observers will
move along a worldline by advancing
`position4` per chart-time step.

The arc's MVP consumes `position4` as the
**camera origin** when the operator wants the
observer-frame perception centered on a non-origin
location. Initially this matches the existing
`Camera::position`; future slices may decouple the
two.

### 3.2 Local tetrad / local basis

`ObserverFrame::right` / `up` / `forward` (Vec3
each). The orthonormal spatial tetrad legs `e₁` /
`e₂` / `e₃` in chart coordinates. The timelike leg
`e₀` is the 4-velocity `velocity4` above. Default
values form the right-handed world basis the
existing pinhole camera produces:
`right = (1, 0, 0)`, `up = (0, 1, 0)`,
`forward = (0, 0, 1)`.

The arc's MVP consumes the tetrad as the **camera
basis**: the kernel reads `frame.right` / `up` /
`forward` to construct primary-ray directions in
tetrad-local coordinates. Aberration (per §5.1
below) then boosts these directions by the
observer's 4-velocity to obtain chart-space ray
directions. Today the equivalent computation
happens in `aberrateDirection(...)` against
`Camera::direction(x, y)`; the arc threads the
tetrad through explicitly so future curved-chart
observers (which carry a non-trivial parallel-
transported tetrad) work via the same path.

### 3.3 Velocity / beta direction

`ObserverFrame::velocity4` (Vec4) and
`ObserverFrame::beta` (Vec3). The 4-velocity is the
canonical observer state; the 3-velocity `beta` is
a sibling field mirroring the legacy
`rr::relativity::Observer::velocity` so the
existing SR helpers can be fed without
re-derivation. On the Minkowski chart the two are
related by
`velocity4 = γ · (1, βₓ, β_y, β_z)` with
`γ = 1 / √(1 − |β|²)`. The
`observer_frame_from(...)` helper at MANIFOLD.3
populates both consistently.

The arc's MVP consumes `beta` for the existing
`aberrateDirection` / `dopplerFactor` /
`searchlightFactor` helpers (the
Minkowski-constant-velocity specialisation) and
exposes `velocity4` for the future curved-chart
slices that need the 4D version.

### 3.4 Proper time placeholder

`ObserverFrame::proper_time` (float). Cumulative
proper time τ along the observer's worldline since
a reference epoch (typically the camera-start
event). Zero-initialised; no integrator advances it
in the OBSERVER.* arc. Reserved for the future
geodesic-integrator arc.

The arc's MVP **does not consume** proper_time —
it's a forward-looking slot. The field is
preserved through the GPU launch params + AOV
write paths so future slices can populate it
without an ABI bump.

### 3.5 Coordinate time placeholder

`ObserverFrame::coordinate_time` (float). The
chart's t-coordinate at the observer's current
worldline parameter. For timelike worldlines on
the Euclidean chart this equals
`position4.x`; on non-Euclidean charts the
relationship is chart-dependent. Zero-initialised;
no integrator advances it in the OBSERVER.* arc.

Same as proper_time — reserved for the future
geodesic-integrator arc.

### 3.6 Perception mode

**New concept** introduced by this arc (not in the
MANIFOLD.3 POD; lands as a new field at OBSERVER.2).
The perception-mode enum identifies *which*
observer-frame transforms the renderer applies:

```
enum class PerceptionMode {
    // Default: scene-rest observer, no aberration,
    // no Doppler, no searchlight. Matches the
    // pre-pivot Euclidean camera bit-for-bit.
    Identity = 0,
    // Constant-velocity Minkowski observer — the
    // current `rr::relativity::Observer` /
    // `RelativityParams` specialisation. Applies
    // aberration + Doppler + searchlight per the
    // existing SR helpers.
    ConstantVelocityMinkowski,
    // Reserved for future curved-chart observer
    // frames (placeholder; selecting one is
    // structurally a passthrough until the
    // corresponding slice lands).
    CurvedChartGeodesicPlaceholder,
};
```

The arc's MVP implements **Identity** (the no-op
default that preserves every existing CLI action's
output bit-for-bit) and
**ConstantVelocityMinkowski** (the
reinterpretation of the existing SR helpers via
the observer-frame tetrad boost; output
**byte-identical** to today's renderer for the
same input observer / params).

The perception-mode enum is parallel to
`CoordinateChartType` — the two enums together
identify "where space maps" + "how the observer
perceives it" per the architecture-doc §3
ontology.

---

## 4. Relationship to existing camera

The existing `rr::camera::Camera` class (in
`src/camera/Camera.h`) is the **image-plane
generator**: it produces per-pixel primary-ray
directions in **camera-local** coordinates given a
position / orientation / FOV. The OBSERVER.* arc
preserves this responsibility.

**The Camera remains the image-plane generator;
the ObserverFrame becomes the physical /
perceptual context.** The two are parallel
concepts that compose at the kernel-seam:

- **Camera** answers: *"For pixel (x, y) at FOV
  φ, what direction does my image-plane sampler
  point?"* → produces a unit-length
  `Vec3 camera_direction` in
  camera-local coordinates.

- **ObserverFrame** answers: *"How does the
  observer-frame perception transform my
  camera-local direction into a chart-space ray
  direction the path tracer / kernel arm can
  consume?"* → produces a chart-space
  `Vec3 chart_direction` via tetrad rotation +
  aberration boost.

The two compose:
`chart_direction = aberrate(rotate_to_tetrad(camera_direction, frame), frame.beta)`.

Today the equivalent computation happens inline in
each kernel: the kernel reads
`Camera::direction(x, y)` (which is already in
world/chart coordinates because the existing
Camera bakes its position + orientation into the
sampler), then calls `aberrateDirection(...)`
against `observer.velocity`. The arc keeps the
inline structure but threads the tetrad explicitly
through the frame.

**Camera may eventually be attached to
ObserverFrame.** A future slice could promote
`Camera::position` to `ObserverFrame::position4`'s
spatial part, and `Camera::up / right / forward`
to the tetrad legs. This consolidation is **not in
scope for OBSERVER.***; it would be a Camera-side
refactor with its own per-slice gate. The current
arc establishes the contract — when the operator
wants the future consolidation, the existing
Camera continues to work via a thin adapter
(per §7.4 below).

---

## 5. Relationship to existing relativity params

The existing `rr::relativity::RelativityParams`
(in `src/relativity/RelativityParams.h`) carries
the per-effect enable flags + strengths:
`enable_aberration`, `enable_doppler`,
`enable_searchlight`, `doppler_color_strength`,
`searchlight_strength`, `max_beta`. These flags
are consumed at the kernel level by the existing
`aberrateDirection` / `dopplerFactor` /
`searchlightFactor` calls.

The OBSERVER.* arc **reframes these as observer-
frame effect flags**, not coordinate-chart flags.
Specifically:

### 5.1 `betaVelocity` / `velocityDirection` →
`ObserverFrame::beta`

The legacy `Observer::velocity` field becomes the
`ObserverFrame::beta` sibling. The
`observer_frame_from(...)` helper at MANIFOLD.3
already does this lossless conversion. The
`betaVelocity` + `velocityDirection` scene-file
shorthands (parsed at `apply_relativity` in
`src/io/SceneLoader.cpp`) continue to author the
`Observer::velocity` vector verbatim; an
OBSERVER.* dispatcher-side bridge builds the
`ObserverFrame` from it on every render call
(default: rest frame; non-default: the configured
beta).

### 5.2 Aberration / Doppler / searchlight →
observer-frame perception effects

The three SR helpers are reinterpreted as the
Minkowski + constant-velocity-frame
specialisation of the observer-frame tetrad
boost (architecture-doc §7.2):

- **aberration** of a ray direction in the
  observer's tetrad → produces the chart-space
  ray direction along which the photon was
  emitted in the observer's rest frame;
- **Doppler factor** for the photon's frequency
  → ratio of observer-measured frequency to
  emitter-measured frequency;
- **searchlight factor** for the photon's
  intensity → bolometric I/ν³ invariant along
  the worldline.

The existing helper bodies are **preserved
verbatim** — the arc reinterprets the input
(`Observer::velocity`) as the
`ObserverFrame::beta` sibling field, but the
math is unchanged. Reuse of single-source-of-
truth math (mirroring the SCHW.* / PENROSE.* arc
pattern) guarantees the kernel-side output is
byte-identical to today.

### 5.3 RelativityParams flags → PerceptionMode +
per-effect strength dials

The arc's MVP (OBSERVER.2-OBSERVER.6) **does not
broaden** the SR effect surface. The existing
flags + strengths continue to drive the helpers:

- `params.enable_aberration` (bool) → checked at
  the kernel-seam's tetrad-boost site;
- `params.enable_doppler` (bool) → checked at
  the Doppler-factor site;
- `params.enable_searchlight` (bool) → checked
  at the searchlight-factor site;
- `params.doppler_color_strength` (float) →
  scales the Doppler effect's chromatic
  application;
- `params.searchlight_strength` (float) → scales
  the searchlight effect's intensity
  application;
- `params.max_beta` (float) → caps the observer's
  beta magnitude before the helpers run.

A future OBSERVER.* addendum (after the
PerceptionMode contract is established) could
promote these flags into the `ObserverFrame` POD
itself or into a sibling `PerceptionParams` POD;
that consolidation is **out of scope** for the
current arc, which preserves the existing
RelativityParams shape verbatim to avoid
breaking the six scene-aware actions that
currently consume it.

---

## 6. GPU integration strategy

The arc threads `ObserverFrame` into the
CUDA + OptiX kernel-seam alongside the existing
manifold payload (added at SCHW.7 / SCHW.5 +
PENROSE.6 / PENROSE.8). Three layers:

### 6.1 Per-launch ObserverFrame payload

Both backends already accept
`rr::manifold::ManifoldMode` + `CoordinateChart`
on their launch params (SCHW.7 OptiX + SCHW.5
CUDA). The arc adds a parallel
`rr::manifold::ObserverFrame observer_frame`
field on the same launch-params PODs:

- **OptiX:** `OptixLaunchParams::observer_frame{}`
  field at the end of the struct, default-
  constructed to the scene-rest observer.
- **CUDA:** `CudaSceneView::observer_frame{}`
  field next to the existing
  `manifold_mode` + `coordinate_chart` fields,
  same default.
- **AOVTargets (CUDA host bridge):**
  `AOVTargets::observer_frame{}` field threading
  the host-side dispatcher state to the
  `CudaSceneView`.

Default-constructed `ObserverFrame{}` is the
scene-rest observer on the Euclidean chart (per
MANIFOLD.3 `rest_frame()` semantics) — the
**byte-identity anchor**. When the kernel reads
the default frame, the perception transforms
reduce to identity and the output is byte-
identical to today.

### 6.2 Dispatcher-side observer-frame construction

Each scene-aware action builds an
`ObserverFrame` from the existing scene-side
`Observer` + `RelativityParams` state via
`observer_frame_from(scene.observer)`. The
resulting frame is threaded into the GPU launch
params alongside the manifold payload.

The construction mirrors the MANI-CONSUME.1
dispatcher-merge pattern:
`effective_observer_frame = observer_frame_from(
  cfg_observer_wins_else_scene_observer)`.
CLI overrides preserved verbatim.

### 6.3 Kernel-side perception application

The kernel reads `launch_params.observer_frame`
and applies the perception transforms. For the
**Identity** mode the kernel short-circuits and
calls the existing SR helpers unchanged (with
`frame.beta` as the input — which equals
`scene.observer.velocity` by construction). For
**ConstantVelocityMinkowski** mode the kernel
calls the existing helpers but explicitly via
the tetrad-boost framing (mathematically
identical; the explicit framing makes future
curved-chart observers tractable).

The arc's MVP does not introduce new kernel-side
math — it threads the frame through and reuses
the existing helpers. Cross-backend AOV byte-
equivalence is structurally guaranteed by
single-source-of-truth math (mirrors the
SCHW.* / PENROSE.* arc pattern).

---

## 7. Proposed implementation slices

Seven sub-slices in the OBSERVER.* sub-slice
ladder. Per-slice gate audits may be inserted
between impl slices as operator cadence permits
(mirroring the SCHW.* / PENROSE.* arc pattern
where audit slots were inserted at SCHW.4 /
SCHW.6 / SCHW.8 / SCHW.10 + PENROSE.3 / PENROSE.5
/ PENROSE.7 / PENROSE.9 / PENROSE.11). The
ladder below numbers the **implementation**
slices only; audit slots are added in-band as
the operator requests.

### OBSERVER.2 — Data model audit / update (impl, math-leaf + POD)

- **Scope:** verify the existing `ObserverFrame`
  POD at `src/manifold/ObserverFrame.h`
  (MANIFOLD.3) is sufficient for the arc's
  needs, and add the `PerceptionMode` enum to
  `src/manifold/ObserverFrame.h` (or a sibling
  header `src/manifold/PerceptionMode.h` if the
  enum doesn't fit MANIFOLD.3's scope). Add
  validator helpers analogous to
  `is_normalised_timelike(...)`:
  - `RR_HD inline bool is_orthonormal_tetrad(
      const ObserverFrame&, float tolerance)` —
    verifies `right · up = up · forward =
    forward · right = 0` and `|right| = |up| =
    |forward| = 1`.
  - `RR_HD inline PerceptionMode default_perception_mode()`
    factory returning `PerceptionMode::Identity`.
- **Acceptance:**
  - Audit-host build green; ctest 12/12 PASS;
    `manifold_identity_tests` grows by a handful
    of new RR_CHECKs verifying the validator +
    factory + `PerceptionMode` enum
    properties.
  - No renderer code path consumes the new
    enum yet (kernel reads happen at
    OBSERVER.5 / OBSERVER.6).
- **What does NOT ship:** kernel-side
  consumption; CLI surface (deferred to
  OBSERVER.3); dispatcher-side bridge (deferred
  to OBSERVER.4); GPU payload (deferred to
  OBSERVER.5 / OBSERVER.6).

### OBSERVER.3 — Config / CLI bridge (impl, host-only)

- **Scope:** add `rr::manifold::PerceptionMode
  perception` field to `rr::core::Config` next
  to the existing `manifold` field; add a
  `--perception-mode <name>` CLI flag parsed by
  the same `CommandLine.cpp` infrastructure
  that parses `--manifold-chart`. Accepted
  values:
  - `identity` (default; pre-OBSERVER.* anchor);
  - `constant-velocity-minkowski` (the
    SR-helpers reinterpretation);
  - `curved-chart-geodesic` (reserved-but-inert
    placeholder; selecting one is structurally
    a passthrough).
  Plus a scene-loader extension at
  `apply_observer_frame` (parallel to
  `apply_manifold` from SCHW.9) that parses an
  optional `perception` block on the
  `.rrscene` schema:
  ```json
  "perception": {
    "mode": "constant-velocity-minkowski",
    "...": "(future fields, e.g. observer
            position overrides)"
  }
  ```
- **Acceptance:**
  - Audit-host build green; ctest 12/12 PASS.
  - `cli_tests` gains assertions verifying the
    new flag parses correctly + maps each
    kebab-case name onto the right
    `PerceptionMode` enumerator.
  - `--scene-info <fixture>` displays the
    parsed perception block (or `disabled` /
    `identity` by default).
- **What does NOT ship:** kernel consumption
  (deferred to OBSERVER.5 / OBSERVER.6);
  observer-position scene authoring (the
  perception block is enum + flags only at
  this slice; position overrides are a future
  addendum).

### OBSERVER.4 — Camera-to-observer adapter (impl, host-only)

- **Scope:** add a host-side
  `build_observer_frame_from_camera(...)`
  helper that constructs an `ObserverFrame`
  from the existing scene-side
  `rr::camera::Camera` + `rr::relativity::Observer`
  + the `PerceptionMode`. Mirrors the SCHW.9
  / PENROSE.6 / PENROSE.8 dispatcher merge
  pattern:
  - On `PerceptionMode::Identity`:
    return `rest_frame()` (the byte-identity
    anchor).
  - On `PerceptionMode::ConstantVelocityMinkowski`:
    return
    `observer_frame_from(scene.observer)` AND
    populate the tetrad legs from
    `camera.right()` / `up()` / `forward()`.
    Returns an `ObserverFrame` with non-trivial
    `beta` + `velocity4` + tetrad fields when
    the operator's scene has non-zero
    observer velocity.
  - On `PerceptionMode::CurvedChartGeodesicPlaceholder`:
    return `rest_frame()` (placeholder; engages
    no chart-aware behaviour until the future
    GR-aware arc lands).
- **Acceptance:**
  - Audit-host build green; ctest 12/12 PASS.
  - `manifold_identity_tests` gains assertions
    on the adapter:
    - Identity mode returns `rest_frame()`.
    - ConstantVelocityMinkowski mode with zero
      camera velocity round-trips via
      `to_relativity_observer` to the input
      Observer.
    - Tetrad orthonormality holds for the
      resulting frame
      (`is_orthonormal_tetrad(...)`).
- **What does NOT ship:** kernel consumption
  (deferred to OBSERVER.5 / OBSERVER.6); GPU
  launch-params field (deferred to OBSERVER.5
  / OBSERVER.6); the adapter exists at the
  host-only seam.

### OBSERVER.5 — CUDA payload bridge (impl, CUDA-side)

- **Scope:** wire the `ObserverFrame` payload
  into the CUDA kernel.
  - Add `rr::manifold::ObserverFrame observer_frame{}`
    field to `CudaSceneView` (sibling of
    `manifold_mode` + `coordinate_chart`).
  - Add matching `AOVTargets::observer_frame{}`
    field on `CudaRenderer.h`.
  - `CudaRenderer::render_scene_with_aovs`
    threads the field into the view (one new
    line: `view.observer_frame =
    targets.observer_frame`).
  - `main.cpp::run_render_aovs` calls the
    OBSERVER.4 adapter and assigns the result
    to `targets.observer_frame`.
  - Kernel-side: the existing
    `aberrateDirection(...)` /
    `dopplerFactor(...)` /
    `searchlightFactor(...)` call sites in
    `CudaTestKernel.cu` are augmented to read
    `scene.observer_frame.beta` (instead of
    `scene.observer.velocity`) AND
    `scene.observer_frame.perception_mode`
    via the new enum, but the underlying math
    helpers are unchanged. **No new kernel
    math.**
- **Acceptance:**
  - Audit-host build green; ctest 12/12 PASS.
  - Default-mode byte-identity: with
    `PerceptionMode::Identity` (the default),
    every existing CLI action's output is
    byte-identical to the pre-OBSERVER.5
    baseline.
  - ConstantVelocityMinkowski mode produces
    byte-identical output to today's
    aberration + Doppler + searchlight render
    (the math helpers are reused unchanged;
    the kernel just reads `frame.beta`
    instead of `observer.velocity`).
- **What does NOT ship:** OptiX wiring
  (deferred to OBSERVER.6); the CUDA path-
  tracer's bounce-loop reads remain on the
  legacy types (the path-tracer migration is
  a separate future arc).

### OBSERVER.6 — OptiX payload bridge (impl, OptiX-side)

- **Scope:** mirror OBSERVER.5 on the OptiX
  side.
  - Add `rr::manifold::ObserverFrame observer_frame{}`
    field to `OptixLaunchParams`.
  - `OptixRenderer::render_aovs` gains an
    `observer_frame` trailing defaulted
    parameter (mirroring SCHW.7's pattern).
  - `main.cpp::run_render_optix_aovs` calls
    the OBSERVER.4 adapter and assigns to
    the trailing parameter.
  - Kernel-side: same as OBSERVER.5 — the
    existing helper call sites in
    `OptixPrograms.cu` read from
    `optixLaunchParams.observer_frame.beta`
    + perception_mode.
- **Acceptance:**
  - Audit-host build green; ctest 12/12 PASS.
  - Default-mode byte-identity preserved on
    both backends.
  - ConstantVelocityMinkowski mode produces
    byte-identical output to today's OptiX
    render.
  - CUDA ↔ OptiX byte-equivalence for the
    same observer-frame input (structurally
    guaranteed by single-source-of-truth math
    + identical parameter encoding;
    pixel-level verification deferred to SDK
    host).
- **What does NOT ship:** denoiser integration
  for any future observer-frame AOV (the
  denoiser still consumes Beauty / Albedo /
  Normal only).

### OBSERVER.7 — Observer debug AOV (impl, AOV + fixture)

- **Scope:** add a debug AOV that visualizes
  the resolved observer-frame state per pixel,
  analogous to the `ManifoldCoordinates` AOV
  (MANI-I.8). The AOV writes the **boosted
  primary-ray direction** in tetrad-local
  coordinates so the operator can see how the
  observer-frame perception transforms the
  per-pixel rays. Plus a fixture scene
  `scenes/test_observer_frame.rrscene` with a
  non-trivial observer velocity + a
  `perception` block.
  - Add `AOVType::ObserverFrameDirection`
    enum entry.
  - Add the device-pointer slot to
    `DeviceAOVView` + `AOVTargets` +
    `OptixLaunchParams::aov_observer_frame_direction`.
  - Kernel arms (CUDA + OptiX) write the
    boosted direction per hit pixel.
  - Host dispatchers allocate the AOV buffer
    only when `--observer-debug` is set
    (parallel to SCHW.7's
    `--manifold-debug` gate).
  - Fixture scene + companion doc
    (`docs/OBSERVER_FRAME_FIXTURE.md`)
    document the expected visual signature.
- **Acceptance:**
  - Audit-host build green; ctest 12/12 PASS.
  - Default-mode no-op preserved (the AOV
    buffer stays null when the operator
    doesn't pass `--observer-debug`).
  - Fixture loads cleanly via
    `--scene-info` on the audit host.
  - Cross-backend AOV byte-equivalence
    structurally guaranteed (same math leaf
    + same parameter encoding).
- **What does NOT ship:** the path tracer's
  bounce-loop reads remain on the legacy
  types; only the `--render-aovs` /
  `--render-optix-aovs` actions write the
  new AOV.

### OBSERVER.8 — Arc capstone audit (docs only)

- **Scope:** per-arc capstone verdict mirroring
  the SCHW.11 + PENROSE.12 capstone shapes.
  Synthesises the prior per-slice audits
  (OBSERVER.2-OBSERVER.7) into a single
  arc-level verdict.
- **Verdict options:** PASS /
  PASS_WITH_RUNTIME_DEFERRED / REPAIR /
  BLOCKED.
- **Acceptance:** audit-host build green;
  ctest 12/12; verdict authorises proceeding
  to one of:
  - MANI-I.12 (final cross-host audit when an
    SDK host is available);
  - chart-parameter scene authoring (lifts
    the SCHW.* / PENROSE.* deferred item);
  - primary-ray direction warp at raygen
    (closes the cosmetic deferral from both
    chart capstones);
  - manifold-orthogonal work (Field
    Interpretation Layer Phase 1, denoiser
    integration with chart-aware AOVs).

---

## 8. Non-goals

Until a future OBSERVER.* addendum lifts them
explicitly, the slice does **not** introduce,
claim, or plan:

- **A full GR tetrad solver.** The arc's
  curved-chart observer-frame slot
  (`PerceptionMode::CurvedChartGeodesicPlaceholder`)
  is reserved-but-inert. No parallel transport;
  no Christoffel symbols on the tetrad legs; no
  geodesic ODE advancing `proper_time` or
  `coordinate_time`. The Minkowski + constant-
  velocity-frame specialisation is the only
  perception mode the arc implements
  concretely.

- **Kerr / Kruskal charts.** The MANI-I.10 /
  MANI-I.11 chart families (SchwarzschildLike +
  PenroseLike) are the only ones the manifold
  layer has implemented; the OBSERVER.* arc
  composes with whichever chart is active but
  does not introduce new chart families.
  Architecture-doc §8 non-goals
  ("physically exact Kerr ray tracing", "full
  GR solver") stand verbatim.

- **C4D / preview-UI / server / node-editor
  integration.** Architecture-doc §8 non-goals;
  operator brief explicitly forbids.

- **Path-tracer bounce-loop migration.** The
  CUDA / OptiX path-tracer kernels
  (`CudaPathTracer.cu`,
  `OptixPrograms.cu`'s pathtrace family)
  continue to feed on the legacy
  `Observer` + `RelativityParams` types. The
  arc's CPU seam + kernel-side migration is
  scoped to the `--render-aovs` /
  `--render-optix-aovs` AOV writers (which
  engage the `ManifoldCoordinates` AOV today).
  Path-tracer migration is a future arc.

- **Behavior change by default.** Every
  existing CLI action without
  `--perception-mode` (or with
  `--perception-mode identity`) produces
  byte-identical output to the
  pre-OBSERVER.* baseline. Verified at each
  per-slice audit and at OBSERVER.8 capstone.

- **Camera ABI change.** The existing
  `rr::camera::Camera` class is unchanged
  by this arc. The OBSERVER.4 adapter reads
  the Camera's existing public surface
  (`position`, `right`, `up`, `forward`,
  `fov_degrees`, etc.) to populate the
  tetrad — Camera-side refactor is a future
  arc.

- **New RelativityParams flags.** The
  existing six flags
  (`enable_aberration` / `enable_doppler` /
  `enable_searchlight` /
  `doppler_color_strength` /
  `searchlight_strength` / `max_beta`)
  are preserved verbatim. No new flag on
  `RelativityParams` this arc; future
  consolidation may promote them onto a
  sibling `PerceptionParams` POD, but that
  is a separate arc.

- **Denoiser integration.** The Stage 19B.4
  / 21D OptiX denoiser continues to consume
  Beauty / Albedo / Normal only. The new
  observer-frame debug AOV is not threaded
  through the denoiser.

- **`.rrscene` schema bump.** The
  `perception` block (added at OBSERVER.3)
  is an optional top-level key alongside
  the existing optional `relativity` and
  `manifold` blocks; v1 compatibility is
  preserved by the standard optional-key
  contract.

---

## 9. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") is the load-bearing invariant for
  this arc.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` —
  §3.3 Observer Frame; §7.2 the current
  `src/relativity/` module's subsumption;
  §8 non-goals; §10 step 1 (the Minkowski
  chart wrap this arc's MVP completes).
- `docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md` —
  MANIFOLD.3 audit that landed the
  `ObserverFrame` POD.
- `docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md` —
  SCHW.11 capstone (verdict
  PASS_WITH_RUNTIME_DEFERRED); the
  precedent arc-pattern this arc mirrors.
- `docs/PENROSE_LIKE_ARC_AUDIT.md` —
  PENROSE.12 capstone (verdict
  PASS_WITH_RUNTIME_DEFERRED); the second
  precedent arc.
- `docs/MANIFOLD_CONSUMPTION_GAP_AUDIT.md` —
  MANI-CONSUME.2 audit that closed both
  chart arcs' largest deferred items.
- `docs/MANIFOLD_INTEGRATION_PLAN.md` §11 —
  the MANI-I.12 final cross-host audit slot
  (the OBSERVER.* arc complements this by
  providing the observer-frame migration
  that MANI-I.12 will gate as part of the
  final verdict).
- `src/manifold/ObserverFrame.h` — the POD
  this arc consumes; landed at MANIFOLD.3.
- `src/manifold/CoordinateChart.h` — sibling
  POD that the SCHW.* / PENROSE.* arcs use;
  composes with `ObserverFrame` per
  architecture-doc §3.
- `src/manifold/ManifoldMode.h` —
  `is_active(...)` helper pattern this arc
  mirrors with an `is_engaged(perception_mode)`
  equivalent at OBSERVER.2.
- `src/relativity/RelativityMath.h` — the
  `aberrateDirection` / `dopplerFactor` /
  `searchlightFactor` / `applyDopplerColor`
  helpers this arc reinterprets as the
  Minkowski + constant-velocity-frame
  specialisation of the observer-frame
  tetrad boost (unchanged; consumed via
  the new frame).
- `src/relativity/RelativityParams.h` — the
  `Observer` + `RelativityParams` types this
  arc bridges to `ObserverFrame` via
  `observer_frame_from(...)`.
- `src/camera/Camera.h` — the image-plane
  generator the OBSERVER.4 adapter reads;
  unchanged by this arc.
- `src/cuda/CudaScene.cuh` — the launch-
  params POD this arc extends with the
  `observer_frame{}` field at OBSERVER.5.
- `src/cuda/CudaRenderer.h` — the
  `AOVTargets` struct this arc extends at
  OBSERVER.5.
- `src/cuda/CudaTestKernel.cu` — the
  CUDA-side `--render-aovs` kernel arm
  this arc threads the observer frame
  through at OBSERVER.5.
- `src/optix/OptixLaunchParams.h` — the
  OptiX launch-params POD this arc
  extends at OBSERVER.6.
- `src/optix/OptixRenderer.h/.cpp` — the
  OptiX `render_aovs` host bridge this
  arc extends at OBSERVER.6.
- `src/optix/OptixPrograms.cu` — the
  OptiX-side `--render-optix-aovs` kernel
  arm this arc threads the observer frame
  through at OBSERVER.6.
- `src/main.cpp::run_render_aovs` /
  `run_render_optix_aovs` — the
  dispatchers this arc extends at
  OBSERVER.5 / OBSERVER.6 (mirrors the
  MANI-CONSUME.1 scene-load + manifold-
  mode log pattern with a parallel
  perception-mode log).
- `tests/manifold_identity_tests.cpp` —
  the 312 RR_CHECK test suite this arc
  extends at OBSERVER.2 / OBSERVER.4 +
  the OBSERVER.7 fixture-load check.

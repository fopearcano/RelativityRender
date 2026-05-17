# Observer-Space Perception Transform Plan (OBS-PERCEPT.1)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this design document; the implementation
        lands in subsequent OBS-PERCEPT.* sub-slices
        that consume this doc as their canonical brief.

This document is the design for **the OBS-PERCEPT.*
arc** — the first true observer-dependent perception
transform layer in the renderer. It closes the
OBSERVER.15 capstone audit's
`PASS_WITH_RUNTIME_DEFERRED` future-kernel-migration
risk #1 (the "subsequent slice will gate the SR-helper
call sites on `observer_frame.perception_mode` and key
the existing aberration / Doppler / searchlight helpers
on `observer_frame.beta` instead of the legacy
`observer.velocity`") and operationalises the
architecture doc §7.2 concept ("the existing helpers
become the Minkowski + constant-velocity-frame
specialisation of observer-frame Lorentz boost of the
tetrad → aberration of the primary direction in
tetrad-local coordinates").

The OBS-PERCEPT.* arc is the natural next architectural
arc after the FIELD-BEAUTY.* arc closure (FIELD-BEAUTY.8
capstone, `bda382c`). The OBSERVER.* foundation
(`docs/OBSERVER_FRAME_ARC_AUDIT.md`) shipped the data
model + adapter + payload bridges + debug AOV + fixture;
the OBS-P.* migration
(`docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`)
half-stepped onto the `observer_frame.beta` payload via
a guarded ternary; OBS-PERCEPT.* takes the full step:
unify the SR pipeline into a **perception transform
layer** keyed on the ObserverFrame, separate from the
manifold (which warps space, not perception).

Per the operator's task brief, this is
**documentation only**. No source code, no
implementation, no test binary changes. The
implementation lands in the OBS-PERCEPT.2 →
OBS-PERCEPT.7 sub-slice ladder defined in §7 below.

---

## 1. Purpose

**The OBS-PERCEPT.* arc introduces a unified
perception transform layer that transforms ray-space
perception according to the observer's frame state.
It separates the geometric manifold (which warps
space) from the perceived directional space (which
warps how the observer sees light from that warped
space).**

### 1.1 Two-arc separation

The renderer's pre-OBS-PERCEPT.* SR pipeline is
distributed across multiple kernel sites:

1. **Primary-ray aberration** — `aberrateDirection`
   call in the kernel before intersection.
2. **Per-pixel Doppler color shift** — `dopplerFactor`
   + `applyDopplerColor` call after shading.
3. **Per-pixel searchlight intensity scaling** —
   `searchlightFactor` + scaling after color
   modulation.

Today (post-OBS-P.2) the inputs to these three sites
are gated on `perception_mode` via a guarded ternary
(`observer_frame.beta` on
`ConstantVelocityMinkowski`; `observer.velocity`
otherwise). The math leaves are shared between
backends via single-source-of-truth RR_HD inline
helpers (`src/relativity/RelativityMath.h`).

The post-OBS-PERCEPT.* SR pipeline unifies the three
sites into a **single perception transform abstraction**:

- **Inputs:** the per-launch `ObserverFrame` POD (the
  source of truth) + per-pixel ray direction +
  per-pixel post-shading color.
- **Outputs:** the boosted ray direction (for
  intersection) + the post-shift color (for
  framebuffer write).
- **Single composition point** that future
  perception extensions (light-cone clipping,
  retarded-time approximation, headlight cones) can
  hook into without modifying the per-pixel kernel
  code at every site.

### 1.2 Why "observer-space"

The transform operates on **the observer's
tetrad-local coordinate frame** — the natural
coordinate system for observer-relative perception.
The architecture doc §7.2 establishes this:

> "the existing special-relativistic helpers become
> the Minkowski + constant-velocity-frame
> specialisation of observer-frame Lorentz boost of
> the tetrad → aberration of the primary direction in
> tetrad-local coordinates"

In the constant-velocity Minkowski regime (the OBS-
PERCEPT.* arc's initial scope), the tetrad-local
coordinate frame is identical to the standard
observer-rest-frame coordinates. The existing
`aberrateDirection` math is the Lorentz boost of the
primary ray's direction into the observer's tetrad-
local coordinates. The existing `dopplerFactor` is
the frequency ratio of the photon four-momentum
along the observer's worldline. The existing
`searchlightFactor` is the bolometric `I/ν³`
invariant.

The OBS-PERCEPT.* arc preserves these mathematical
specialisations + lifts them into a single
abstraction layer that:

- Reads from the per-launch `ObserverFrame` payload
  exclusively (no fallback to `observer.velocity`).
- Composes naturally with the manifold layer (which
  defines the geometric chart) — the observer's
  tetrad-local frame is anchored at the chart-local
  hit position.
- Generalises to future perception modes (e.g.
  curved-chart geodesic-keyed perception) without
  changing the per-site kernel code.

### 1.3 Relationship to the OBSERVER.* + OBS-P.* arcs

| Arc           | Role                                                                |
|---------------|---------------------------------------------------------------------|
| OBSERVER.*    | Data model + adapter + payload bridges + debug AOV + fixture        |
| OBS-P.*       | Migrated kernel-side reads onto `observer_frame.beta` via ternary  |
| OBS-F.*       | Fixture authoring the relativity block + camera observer velocity   |
| OBS-PERCEPT.* | **THIS ARC** — unified perception transform layer on the tetrad     |

The OBSERVER.* foundation arc landed the load-bearing
infrastructure: `ObserverFrame` POD with a tetrad
(right / up / forward axes), `PerceptionMode` enum,
camera-to-observer adapter, both backends' launch
payload threading, debug AOV (`ObserverBeta`), and a
fixture. OBS-PERCEPT.* consumes this infrastructure
exclusively — it does not add new ObserverFrame
fields or extend the payload; it just lifts the SR
helpers into a perception-transform abstraction that
reads from the existing payload.

The OBS-P.* migration arc landed the half-step:
gated five SR-helper call sites on the
`perception_mode` enum via a guarded ternary. OBS-
PERCEPT.* is the full step: the ternary disappears
once the perception transform becomes the sole SR
pipeline path; the `observer.velocity` legacy field
remains for backwards compatibility with
non-perception-mode CLI flows but the kernel arms
read from `observer_frame.beta` only.

---

## 2. Initial scope

The OBS-PERCEPT.* arc scope is deliberately narrow
per the operator's brief:

### 2.1 Constant-velocity Minkowski observer only

The arc implements ONLY the
`PerceptionMode::ConstantVelocityMinkowski` slot. The
other two enumerators (`Identity` /
`CurvedChartGeodesicPlaceholder`) remain reserved
per the existing `ObserverFrame.h` enum comment:

- **Identity**: no perception transform applied; the
  perception layer is bypassed. Default
  `ObserverFrame{}` carries this mode; preserves
  byte-identical output to the pre-OBS-PERCEPT.*
  baseline.
- **ConstantVelocityMinkowski**: the OBS-PERCEPT.*
  arc's load-bearing implementation target. The
  observer moves with constant 3-velocity `beta`
  through Minkowski space; the transform applies
  Lorentz boost to the primary ray direction +
  frequency-ratio + bolometric scaling per pixel.
- **CurvedChartGeodesicPlaceholder**: reserved per
  master rule #3. Selecting it today produces
  "no-output-this-slice" (the transform falls back
  to Identity behaviour); future arcs lift this into
  a geodesic-tetrad-transport implementation.

The arc implements only one of three enumerator
slots; the other two are explicitly out of scope.

### 2.2 Directional aberration

The OBS-PERCEPT.* arc's primary-ray transform
applies the FIELD-tested
`rr::relativity::aberrateDirection(direction, beta)`
math to the ray direction BEFORE intersection. The
existing math is preserved verbatim; only the
**invocation site** and the **input source** change:

- **Today (post-OBS-P.2)**: every kernel arm calls
  `aberrateDirection(ray.dir, ternary)` with the
  ternary `(perception_mode == ConstantVelocityMinkowski)
  ? observer_frame.beta : observer.velocity`.
- **Post-OBS-PERCEPT.3**: a unified
  `apply_observer_perception_transform(...)` helper
  (or equivalent abstraction) computes the boosted
  direction; the kernel calls this single helper
  per ray.

### 2.3 Doppler basis shift

The arc's post-shading color transform applies
`dopplerFactor(rel, direction)` +
`applyDopplerColor(color, D, strength)` to the
per-pixel color AFTER shading. Same migration
pattern as §2.2 — the math is preserved verbatim;
the call site is unified.

### 2.4 Searchlight intensity modulation

The arc's post-Doppler intensity transform applies
`searchlightFactor(D)` + linear scaling. Same
migration pattern.

### 2.5 No acceleration

The arc does NOT implement accelerating observers
(non-constant `beta`). The observer's 3-velocity is
constant within a launch; the
`ObserverFrame::position4` / `velocity4` POD fields
remain reserved-but-inert per the OBSERVER.2 audit.

### 2.6 No GR geodesics

The arc does NOT implement geodesic ray propagation
(the `CurvedChartGeodesicPlaceholder` mode's
underlying GR mechanism). Future arcs may lift this;
out of scope here.

---

## 3. Relationship to existing relativistic code

### 3.1 Legacy effects become observer-space transforms

The current `src/relativity/` module's helpers —
`aberrateDirection`, `dopplerFactor`,
`searchlightFactor`, `applyDopplerColor`,
`PrecomputedRelativity` — become the Minkowski +
constant-velocity-frame specialisation of the
unified perception transform per the architecture
doc §7.2 framing:

- **`aberrateDirection`** → tetrad-local Lorentz
  boost of the ray direction. The math is byte-
  identical; the input source becomes
  `observer_frame.beta` exclusively (no
  `observer.velocity` fallback).
- **`dopplerFactor`** → frequency ratio of the
  photon four-momentum along the constant-velocity
  worldline. Same math; same input source change.
- **`searchlightFactor`** → bolometric `I/ν³`
  invariant along the worldline. Same.
- **`applyDopplerColor`** → linear chromatic
  modulation; consumed inside the unified
  transform.
- **`PrecomputedRelativity`** → per-launch
  snapshot of `(beta, gamma, ...)` derived from
  the `ObserverFrame`. The OBS-PERCEPT.* arc
  may extend this struct to include the
  tetrad-local basis vectors if the
  optimisation matters.

### 3.2 ObserverFrame becomes runtime source of truth

The arc lifts the kernel-side SR-helper input
source from the post-OBS-P.2 guarded ternary
(`(perception_mode == ConstantVelocityMinkowski)
? observer_frame.beta : observer.velocity`) to the
unconditional `observer_frame.beta` read. The legacy
`observer.velocity` field on
`rr::relativity::Observer` is preserved on the
host-side scene + payload structures (`Scene`,
`GpuScene`, `CudaSceneView`, `OptixLaunchParams`) so
that backwards-compatible code paths and the
`--render-relativistic` CLI action (which doesn't
pass `--observer-perception-mode`) continue to read
the legacy field.

Within the OBS-PERCEPT.* kernel arm, every read of
`observer_frame.beta` is unconditional — there is no
fallback to `observer.velocity` AFTER the perception
transform engages. The `Identity` perception mode is
the natural no-op anchor (the perception transform
short-circuits at the outer gate when
`perception_mode == Identity`); on
`ConstantVelocityMinkowski` the transform fires; on
the reserved `CurvedChartGeodesicPlaceholder` the
transform falls back to `Identity` behaviour (no
transform) for honesty per master rule #3.

### 3.3 No new SR math leaves

The arc does NOT introduce new SR math helpers. The
existing `aberrateDirection` / `dopplerFactor` /
`searchlightFactor` are sufficient; the OBS-
PERCEPT.* arc composes them into the unified
transform but does not add new specialisations.
Future arcs (e.g. OBS-PERCEPT-GR.* for curved-chart
geodesic perception) may extend the math leaves;
out of scope here.

---

## 4. Relationship to manifold system

The OBS-PERCEPT.* arc is **orthogonal** to the
Manifold Core (SCHW.* / PENROSE.* / MANIFOLD.*).
Both layers are composable in the rendering
pipeline:

### 4.1 Manifold warps space

The Manifold Core defines the chart-local coordinate
deformation. When the SchwarzschildLike chart is
engaged, the world-space hit position is warped to
the chart-local space via
`schwarzschild_like_world_to_chart(...)`. The
manifold operates BEFORE intersection (the chart
mediates ray propagation) and BEFORE shading (the
chart's coordinate system anchors the material
evaluation).

### 4.2 Observer transforms perception basis

The Observer-Space Perception Transform Layer
defines the per-observer view of the rendered
scene. It operates ON TOP of the manifold-rendered
result: the manifold tells you what's at the
chart-space position; the observer tells you how
you perceive light arriving from that position.

The perception transform's tetrad-local basis is
anchored at the **chart-local hit position** — not
the world-space position. When the manifold warps
space, the perception transform's basis vectors
move with the manifold-warped position. This is
the natural composition: the chart defines the
geometry; the observer defines the perception of
that geometry.

### 4.3 Both layers composable

The renderer's per-pixel pipeline post-
OBS-PERCEPT.3:

```
1. Generate primary ray (camera-relative, world space)
2. [OBS-PERCEPT] Apply perception transform to ray
   direction (Lorentz boost in tetrad-local coords)
3. [MANIFOLD] Apply manifold warp to ray
   (chart-local space transformation)
4. Intersect chart-local ray against scene geometry
5. Compute color from material + lights at chart-
   local hit position
6. [OBS-PERCEPT] Apply perception transform to color
   (Doppler shift + searchlight scaling)
7. Framebuffer write
```

Both layers default to no-op (the manifold's
`disabled_manifold_mode()` produces Euclidean
output; the observer's `Identity` perception mode
produces unboosted output). With both at defaults,
the pipeline is byte-identical to the
pre-arc-family baseline.

When the operator engages BOTH layers (e.g.
`--manifold-enable --manifold-chart schwarzschild-like
--observer-perception-mode relativistic --observer-beta
0.5 --observer-direction 1,0,0`), the rendered image
shows:

- The SchwarzschildLike chart's curved space (chart
  warp visible in the ManifoldCoordinates AOV).
- The observer's relativistic perception (aberration
  visible in the angular distribution; Doppler color
  shift; searchlight beaming).

The composition is associative: the perception
transform always operates in the observer's tetrad-
local frame at the chart-local hit position; the
manifold transform always operates on the chart-
local coordinate space; the two never interfere.

### 4.4 No new manifold-observer coupling

The arc does NOT introduce new coupling between the
manifold and observer layers. The
`CurvedChartGeodesicPlaceholder` perception mode (a
future arc) is the natural integration point for
observer-frame-keyed geodesic transport in curved
charts. The OBS-PERCEPT.* arc stays on the
constant-velocity Minkowski specialisation; the
manifold + observer layers compose orthogonally
without new abstractions.

---

## 5. GPU integration strategy

### 5.1 Primary-ray basis transform

The OBS-PERCEPT.* arc's load-bearing kernel
modification is at the **primary-ray generation
stage** of each kernel arm:

- **CUDA `k_render_scene`** (`CudaTestKernel.cu`):
  apply the perception transform to the ray
  direction immediately after `generate_camera_ray(...)`.
  Replaces the existing `aberrateDirection(...)`
  call with a unified
  `apply_observer_perception_transform_to_ray(...)`
  helper (or equivalent).
- **OptiX `__raygen__pinhole`** (`OptixPrograms.cu`):
  same shape on the OptiX side.
- **OptiX `__raygen__pathtrace`**
  (`OptixPrograms.cu`): same shape for the
  pathtracer raygen.
- **CUDA `k_pathtrace_sample`**
  (`CudaPathTracer.cu`): same shape for the CUDA
  pathtracer.

All four sites already invoke the SR helpers via
the OBS-P.2 guarded ternary today; the OBS-
PERCEPT.* arc consolidates these calls into the
unified perception-transform abstraction.

### 5.2 Optional secondary-ray policy

The arc OPTIONALLY extends the perception transform
to secondary rays (the pathtracer's bounce
recursion). Two design options:

- **Option A (RECOMMENDED for initial scope)**:
  Apply the perception transform to the primary ray
  ONLY. Secondary rays propagate in their natural
  Minkowski frame (no per-bounce re-application of
  the observer's boost). This matches the existing
  pre-OBS-PERCEPT.* behaviour where
  `aberrateDirection` is applied at the camera
  level only.
- **Option B (DEFERRED)**: Apply the perception
  transform to every bounce. This corresponds to a
  "fully observer-relativistic" path tracer where
  every secondary ray is boosted into the
  observer's frame. Higher physical fidelity but
  significantly more expensive; out of scope for
  the OBS-PERCEPT.* arc's initial scope (deferred
  to a follow-up FRAME-PROPAGATION.* arc if
  authorised).

The OBS-PERCEPT.3 / .4 implementation slices ship
Option A by default. The kernel arm doc-comments
document Option B as deferred future work.

### 5.3 Default observer remains no-op

The arc preserves the
`PerceptionMode::Identity` default's no-op anchor.
The kernel arm gates on:

```cpp
if (observer_frame.perception_mode ==
    PerceptionMode::ConstantVelocityMinkowski) {
    // apply perception transform
} else {
    // Identity: no transform applied
}
```

The default `ObserverFrame{}` carries
`perception_mode = Identity`; the kernel
short-circuits; every existing `--render-*`
invocation without `--observer-perception-mode`
preserves byte-identical PPM output.

This matches the FIELD-BEAUTY.3 / .5 +
OBSERVER.13 / FIELD-I.9 / FIELD-I.11 precedent
(default-state byte-identity via outer gate).

### 5.4 Shared math leaves on both backends

The OBS-PERCEPT.* arc reuses the existing
`src/relativity/RelativityMath.h` helpers verbatim.
Both backends call the same RR_HD inline
`aberrateDirection` / `dopplerFactor` /
`searchlightFactor`. The unified
`apply_observer_perception_transform_to_ray(...)`
helper (if introduced as a separate function) lives
in `src/relativity/` and is RR_HD inline; both
backends emit equivalent PTX/SASS.

Cross-backend bit-identity for the perception
transform is structurally guaranteed by
construction (mirrors the FIELD-BEAUTY.6 §3.7
five-axis symmetry argument applied to the
perception transform layer).

---

## 6. Safety constraints

The OBS-PERCEPT.* arc preserves the OBSERVER.6 +
OBS-P.2 + FIELD-I.* safety discipline:

### 6.1 Bounded transforms

The Lorentz boost is bounded by the
`PrecomputedRelativity` snapshot's clamped `beta`
(per OBSERVER.6's `clampBeta` second-pass at
`max_beta = 0.999999f`). The Doppler factor `D` is
bounded by `[gamma * (1 - |beta|), gamma * (1 +
|beta|)]`; for the clamped beta this is structurally
finite. The searchlight factor `D⁴` is bounded by
`D⁴_max ≤ ~1e6` at the clamp shell (verified at
OBSERVER.6 audit's clampBeta-safety test).

### 6.2 Beta clamp

The OBSERVER.6 `clampBeta(|beta|, max_beta)`
discipline is preserved verbatim. The
`PerceptionTransform` consumer reads the clamped
`beta` from the `PrecomputedRelativity` snapshot
that OBSERVER.6 produces; no per-kernel re-clamp
required.

### 6.3 No NaN / Inf

The unified perception transform is structurally
NaN/Inf-safe at every step (the SR math leaves are
already NaN/Inf-safe per `tests/relativity_tests.cpp`'s
841 RR_CHECK assertions; the OBS-PERCEPT.* arc
composes them without adding new arithmetic).

### 6.4 Default-off byte identity

Every existing `--render-*` invocation without
`--observer-perception-mode relativistic` preserves
byte-identical PPM output to the pre-OBS-PERCEPT.*
baseline. The kernel arm's outer gate
(`perception_mode == ConstantVelocityMinkowski`)
short-circuits on the default `Identity` mode;
neither the unified transform nor any new SR-helper
call fires.

### 6.5 Single source of truth

The `ObserverFrame` POD is the SINGLE source of
truth for the perception transform's parameters.
The kernel arm reads `observer_frame.beta` /
`observer_frame.{right, up, forward}` (the tetrad);
no fallback to `observer.velocity` AFTER the
perception transform engages. This eliminates the
post-OBS-P.2 ternary's ambiguity; the perception
transform is unambiguously defined by the
`ObserverFrame`.

---

## 7. Proposed slices

Six sub-slices in the OBS-PERCEPT.* ladder. Per-slice
gate audits may be inserted between impl slices as
operator cadence permits (mirroring the OBSERVER.* /
OBS-P.* / OBS-F.* / FIELD-I.* / FIELD-BEAUTY.* arc
patterns where audit slots were inserted in-band).
The ladder below numbers the **implementation**
slices only; audit slots are added in-band as the
operator requests.

### OBS-PERCEPT.2 — Primary-ray transform task (docs only)

- **Scope:** write
  `docs/OBSERVER_SPACE_PRIMARY_RAY_TRANSFORM_TASK.md`
  — the operator-facing task brief the OBS-PERCEPT.3
  implementation slice will read. Defines the
  primary-ray transform's exact surface (the
  unified helper signature; the kernel arm's
  insertion point per-backend; the input source
  semantics; the legacy `observer.velocity` fallback
  policy on the path-tracer side; the cross-backend
  symmetry requirements).
- **Acceptance:** documentation-only; mirrors the
  OBSERVER.12 + FIELD-I.6 + FIELD-BEAUTY.1 task
  brief shape verbatim.
- **What does NOT ship:** any source modification;
  any test extension; any fixture authoring; any
  CLI flag.

### OBS-PERCEPT.3 — CUDA implementation (impl, kernel arm)

- **Scope:** land the unified perception-transform
  helper + the CUDA kernel arm in `k_render_scene`
  (and optionally `k_pathtrace_sample` per
  OBS-PERCEPT.2 §5.2 Option A vs B). Replaces the
  post-OBS-P.2 guarded ternary at the primary-ray
  generation site with the unified helper. The
  existing aberration + Doppler + searchlight sites
  in the kernel either consume the unified
  helper's output or migrate to call the unified
  helper themselves.
- **Acceptance:**
  - Audit-host build green; ctest 13/13 PASS.
  - Default-state byte-identity preserved (no
    `--observer-perception-mode` flag → kernel arm
    short-circuits at the Identity gate;
    pre-OBS-PERCEPT.* PPM output preserved).
  - On `--observer-perception-mode relativistic`,
    the kernel arm fires the unified transform;
    output diverges from the legacy
    `observer.velocity` path bit-identically with
    the post-OBS-P.2 guarded-ternary
    `observer_frame.beta` path. (Verified at the
    OBS-PERCEPT.4 audit's cross-backend comparison
    after the OptiX side lands.)
- **What does NOT ship:** OptiX-side wiring
  (deferred to OBS-PERCEPT.4); debug AOV
  (deferred to OBS-PERCEPT.5); fixture (deferred
  to OBS-PERCEPT.6); arc capstone (deferred to
  OBS-PERCEPT.7).

### OBS-PERCEPT.4 — OptiX implementation (impl, OptiX program arm)

- **Scope:** mirror OBS-PERCEPT.3 on the OptiX
  path. Apply the unified perception transform to
  the primary ray in `__raygen__pinhole` (and
  optionally `__raygen__pathtrace` per Option A vs
  B). The OptiX-side arm reads from
  `optixLaunchParams.observer_frame.beta` /
  `tetrad`; the CUDA-side arm reads from the
  symmetric `view.observer_frame.*` payload.
  Cross-backend math equivalence via the shared
  RR_HD inline math leaves.
- **Acceptance:**
  - Audit-host build green; ctest 13/13 PASS.
  - OptiX-ON-no-SDK build clean (14/14 ctest PASS).
  - Cross-backend bit-identity structurally
    guaranteed (FIELD-BEAUTY.6 five-axis symmetry
    pattern applied); empirical SDK-host
    verification deferred to the future arc-wide
    SDK-host runtime pass.
- **What does NOT ship:** CUDA modifications
  (preserved from OBS-PERCEPT.3); debug AOV
  (deferred); fixture (deferred).

### OBS-PERCEPT.5 — Debug AOV (impl, AOV data model + kernel write)

- **Scope:** add a `ObserverPerceptionDelta` debug
  AOV. Writes the per-pixel `(boosted_dir -
  pre_boost_dir)` vector (or an equivalent
  visualisation of the transform's effect: the
  Doppler factor `D` per pixel; the searchlight
  scaling `D⁴ * strength` per pixel; the aberration
  angular delta in arc-degrees) to a 3-channel
  Vec3 / 1-channel float AOV. Mirrors the
  OBSERVER.13 `ObserverBeta` AOV's read-only
  diagnostic shape verbatim; gated on
  `--observer-debug` (or a new modifier flag
  `--observer-perception-debug` if the brief
  authorises).
- **Acceptance:**
  - Audit-host build green; ctest 13/13 PASS.
  - `renderer_tests` grows by ~4 assertions
    (enumerator value + name + component count +
    factory).
  - Default-off byte-identity (the AOV pointer
    stays nullptr unless the gate flag is set).
- **What does NOT ship:** runtime PPM verification
  (deferred to fixture + arc-wide SDK-host
  runtime pass).

### OBS-PERCEPT.6 — Fixture (impl, scene + parser)

- **Scope:** add a fixture scene
  `scenes/test_observer_perception.rrscene` (or
  similar) that authors a non-trivial observer
  velocity + ConstantVelocityMinkowski perception
  mode + visible scene geometry. Reuses the
  OBS-F.2 / FIELD-I.13 / FIELD-BEAUTY.7 geometry
  pattern. Adds a companion doc
  `docs/OBSERVER_PERCEPTION_FIXTURE.md` with the
  standard 7-section structure (Purpose /
  Composition / Expected visual signature /
  Cross-backend equivalence / Audit-host smoke
  transcript / Runtime SDK-host validation /
  References).
- **Acceptance:**
  - Audit-host build green; ctest 13/13 PASS.
  - `--scene-info` loads the fixture cleanly.
  - Default-off byte-identity preserved.
- **What does NOT ship:** SDK-host PPM verification
  (deferred); CLI engagement (deferred); the
  fixture relies on the existing `--observer-*`
  CLI surface that OBSERVER.4 already shipped.

### OBS-PERCEPT.7 — Arc capstone audit (docs only)

- **Scope:** arc-level capstone verdict mirroring
  the OBSERVER.15 + FIELD-BEAUTY.8 capstone audit
  shapes. Verifies the OBS-PERCEPT.* arc's
  structural completeness on the audit-host side;
  documents the SDK-host runtime-deferred
  scenarios; recommends the next safe stage
  (typically the combined arc-wide SDK-host
  runtime pass that closes both OBS-PERCEPT.* and
  the parallel FIELD-I.* + FIELD-BEAUTY.* arc
  family on one operator-cadence-bound effort).
- **Acceptance:** documentation-only verdict.

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

---

## 8. Explicit non-goals

The OBS-PERCEPT.* arc deliberately excludes:

### 8.1 No Kerr / Kruskal

The arc does NOT implement the reserved Kerr or
Kruskal chart families' perception. The
`PerceptionMode::CurvedChartGeodesicPlaceholder`
slot remains reserved-but-inert; selecting it
today falls back to `Identity` behaviour (no
transform). Future arcs (e.g. OBS-PERCEPT-GR.* or
OBS-PERCEPT-KERR.*) may lift this.

### 8.2 No accelerating observers

The observer's 3-velocity is constant within a
launch. The `ObserverFrame::position4` /
`velocity4` POD fields (reserved at OBSERVER.2)
remain inert. The `proper_time` / `coordinate_time`
fields likewise. Future arcs (e.g.
OBS-PERCEPT-ACCEL.*) may extend.

### 8.3 No full GR tetrad transport

The arc does NOT implement parallel transport of
the observer's tetrad along a curved-space geodesic.
The tetrad's basis vectors are anchored at the
chart-local position via the OBSERVER.6 adapter's
existing world-basis convention; no per-step
re-orthonormalization or curvature-dependent rotation.
Future arcs that engage curved-chart perception
(`CurvedChartGeodesicPlaceholder`) would extend.

### 8.4 No quantum observer effects

The arc does NOT introduce quantum-state
interactions with the observer (e.g.
observer-dependent wavefunction collapse, Unruh
effect visualization). The `FieldType` enum's
`ProbabilityAmplitudePlaceholder` slot is
reserved per the FIELD-I.1 plan §2.4; the
OBS-PERCEPT.* arc does NOT extend it. Quantum
effects remain out of scope.

### 8.5 No path-tracer per-bounce perception

The arc ships Option A (primary-ray-only
perception transform). Per-bounce perception
(Option B from §5.2) is deferred to a follow-up
FRAME-PROPAGATION.* arc if authorised. The OBS-
PERCEPT.* path-tracer kernel arm applies the
transform at the primary ray only; secondary
rays propagate in their natural Minkowski frame.

### 8.6 No new CLI flag families

The arc reuses the OBSERVER.4 `--observer-*` CLI
surface verbatim. No new `--observer-perception-*`
flag family this arc; the existing
`--observer-perception-mode` flag is the gate. If
a debug AOV requires a separate
`--observer-perception-debug` modifier, the
OBS-PERCEPT.5 task brief authorises it; otherwise
the existing `--observer-debug` gate suffices.

### 8.7 No new ObserverFrame POD fields

The arc consumes the existing OBSERVER.2-shipped
`ObserverFrame` POD fields verbatim
(`perception_mode`, `beta`, `right`, `up`,
`forward`, `position4`, `velocity4`,
`proper_time`, `coordinate_time`). No new fields
added; no field-offset changes.

### 8.8 No C4D / server / UI / node-editor

Standard discipline carried forward from every
prior arc; the OBS-PERCEPT.* arc does not touch
any DCC / server / UI / node-editor surface.

### 8.9 No legacy `observer.velocity` removal

The legacy `rr::relativity::Observer::velocity`
field is preserved verbatim for backwards
compatibility with non-perception-mode CLI flows.
The OBS-PERCEPT.* kernel arm reads
`observer_frame.beta` exclusively, but the host-
side payload structures continue to carry
`observer.velocity` so that the
`--render-relativistic` CLI action (without
`--observer-perception-mode`) continues to use
the legacy field.

---

## 9. References

### 9.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the arc preserves
  master rule #1, #3, #11, #12, #16 throughout).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §7.2
  (the architecture-doc anchor for the
  "observer-frame Lorentz boost of the tetrad"
  concept; the OBS-PERCEPT.* arc operationalises
  this §7.2 framing).

### 9.2 OBSERVER.* + OBS-P.* + OBS-F.* arc references

- `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
  (OBSERVER.1 — the canonical OBSERVER.* arc
  plan; OBS-PERCEPT.* is the next step beyond the
  OBSERVER.* foundation).
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3).
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5).
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7).
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9).
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11).
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md` (OBSERVER.14).
- `docs/OBSERVER_FRAME_ARC_AUDIT.md` (OBSERVER.15
  — the capstone whose §10 risk #1 the
  OBS-PERCEPT.* arc closes).
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3 — the precedent kernel-migration
  audit; OBS-PERCEPT.3 / .4 build on the
  OBS-P.2 guarded-ternary half-step).
- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md` (OBS-F.3
  — the precedent fixture audit; OBS-PERCEPT.6
  follows the same fixture pattern).

### 9.3 Parallel-arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1 — the parallel field-interpretation
  arc; the OBS-PERCEPT.* + FIELD-I.* arcs
  coexist as orthogonal perceptual layers
  above the manifold).
- `docs/FIELD_SCALAR_BEAUTY_MAPPING_AUDIT.md`
  (FIELD-BEAUTY.8 — the immediate precedent
  capstone audit; OBS-PERCEPT.* opens parallel
  to FIELD-BEAUTY.* with a similar
  cross-backend symmetry discipline).

### 9.4 Source surface to be exercised (post-OBS-PERCEPT.* arc)

The OBS-PERCEPT.* arc will exercise (read-only)
or modify:

- **`src/relativity/RelativityMath.h`** — the
  single-source-of-truth math leaves
  (`aberrateDirection`, `dopplerFactor`,
  `searchlightFactor`, `applyDopplerColor`,
  `PrecomputedRelativity`). The OBS-PERCEPT.* arc
  preserves these verbatim; may add a new
  `apply_observer_perception_transform_to_ray(...)`
  helper that composes them.
- **`src/manifold/ObserverFrame.h`** — the
  source-of-truth POD. The OBS-PERCEPT.* arc
  reads `perception_mode`, `beta`, and the
  tetrad fields (`right`, `up`, `forward`)
  exclusively; no new fields added.
- **`src/cuda/CudaTestKernel.cu`** — the
  primary-hit kernel arm. OBS-PERCEPT.3 unifies
  the SR-helper calls into a single perception
  transform invocation.
- **`src/cuda/CudaPathTracer.cu`** — the
  CUDA path-tracer kernel arm. OBS-PERCEPT.3
  optionally extends to this kernel (Option A
  primary-ray-only; Option B per-bounce
  deferred).
- **`src/optix/OptixPrograms.cu`** — the OptiX
  closest-hit + miss + raygen programs.
  OBS-PERCEPT.4 mirrors OBS-PERCEPT.3 on the
  OptiX side.
- **`src/renderer/AOV.h` + `AOV.cpp`** —
  OBS-PERCEPT.5 may add a new AOV enumerator
  for the perception-transform debug visualisation.
- **`scenes/test_observer_perception.rrscene`**
  — OBS-PERCEPT.6 fixture (new).

### 9.5 Non-touched surfaces

- The Manifold Core (`src/manifold/`) other than
  reading `ObserverFrame`.
- The `src/field/` tree (orthogonal arc family).
- The `src/io/SceneLoader.cpp` parser (no new
  scene-block; the existing `relativity` +
  `observer-via-CLI` surfaces suffice).
- The `rr::core::Config` (no new Config field).
- All DCC / server / UI / node-editor surfaces.

---

## 10. Constraints carried forward

From `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
— these apply to every OBS-PERCEPT.* slice:

- **Build incrementally.** Each sub-slice ships
  with the project compilable; no half-finished
  intermediate state.
- **No fake stubs.** The
  `CurvedChartGeodesicPlaceholder` perception
  mode's no-transform fallback is honest (master
  rule #3: the kernel falls back to Identity
  behaviour today; future arcs lift this with
  documented contracts).
- **No CPU per-pixel or per-ray work.** The
  perception transform is exclusively a GPU
  kernel-arm modification; the host side only
  threads the `ObserverFrame` payload.
- **Core modules never depend on Cinema 4D, UI,
  node editor, or any DCC.** The OBS-PERCEPT.*
  arc preserves this (no dependency on `src/server/`,
  `bridges/`, `tools/`).
- **Update BUILD_PLAN.md after every
  implementation.** Each per-slice impl ships
  with a per-slice BUILD_PLAN.md entry.

---

## 11. Verdict

This is a **planning slice**; it produces no
verdict. The OBS-PERCEPT.7 arc capstone audit
(documentation-only; the last slice in the
ladder) will produce the arc-level
`PASS / PASS_WITH_RUNTIME_DEFERRED / REPAIR /
BLOCKED` verdict.

Each per-slice impl (OBS-PERCEPT.3 + OBS-PERCEPT.4
+ OBS-PERCEPT.5 + OBS-PERCEPT.6) is followed by an
operator-cadence per-slice audit per the standing
discipline (FIELD-I.10 / FIELD-I.12 / FIELD-I.14 /
FIELD-BEAUTY.4 / FIELD-BEAUTY.6 / FIELD-BEAUTY.8
precedent applied at the arc scope).

The OBS-PERCEPT.1 plan slice itself is
documentation-only; it ships
`docs/OBSERVER_SPACE_PERCEPTION_PLAN.md` (this
doc) + the BUILD_PLAN.md entry. No source code
modification. No fixture authoring. No CLI flag.
No kernel-arm implementation. The implementation
lands in the OBS-PERCEPT.2 → OBS-PERCEPT.7
sub-slice ladder defined in §7.

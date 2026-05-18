# Observer Doppler / Searchlight Migration — Task Definition (OBS-DOP.1)

Date:   2026-05-18
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this task definition; the implementation
        lands in subsequent slices (OBS-DOP.2 CUDA,
        OBS-DOP.3 OptiX, …) that consume this doc as
        their canonical brief.

This document opens the **OBS-DOP.\*** arc — the
migration of the per-pixel **Doppler color shift** and
**searchlight intensity modulation** call sites onto
the unified observer-space perception transform layer
that the OBS-PERCEPT.\* arc established for the
**directional aberration** site. The OBS-DOP.\* arc is
the natural continuation of OBS-PERCEPT.\*: that arc
unified the primary-ray aberration call sites onto the
`rr::manifold::apply_observer_primary_ray_aberration(observer_frame,
direction)` RR_HD inline helper (at
`src/manifold/ObserverFrame.h:553+`) and left the
post-shading Doppler / searchlight sites running the
post-OBS-P.2 guarded ternary (`(perception_mode ==
ConstantVelocityMinkowski) ? observer_frame.beta :
observer.velocity`) verbatim. The OBS-PERCEPT.10
capstone audit (`docs/OBSERVER_PERCEPTION_ARC_AUDIT.md`)
check #7 documented this preservation honestly and
deferred the consolidation to a "future OBS-PERCEPT.\*
sub-slice"; this OBS-DOP.\* arc IS that sub-slice
family.

Per the operator's task brief, this is
**documentation only**. No source code, no test, no
fixture, no CLI flag, no behavioural change. The
implementation lands in the OBS-DOP.2 → OBS-DOP.5
sub-slice ladder defined in §9 below.

Prerequisite slices already green (the OBS-DOP.\* arc
consumes these verbatim; no modification):

- **OBSERVER.1 – OBSERVER.15** — Observer Frame
  foundation arc capstone
  (`docs/OBSERVER_FRAME_ARC_AUDIT.md`). The
  `ObserverFrame` POD + `PerceptionMode` enum +
  `ObserverConfig` + the camera-to-observer adapter
  + both backends' launch-payload threading + the
  `ObserverBeta` debug AOV all preserved verbatim.
- **OBS-P.1 – OBS-P.3** — Kernel-side perception
  migration with guarded ternary
  (`docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`).
  The 5 migrated call sites' OBS-P.2 ternary continues
  to feed the `rel` snapshot consumed by the Doppler /
  searchlight blocks; OBS-DOP.\* consolidates the
  Doppler / searchlight site consumers into the
  unified perception abstraction.
- **OBS-F.1 – OBS-F.3** — Observer-frame fixture +
  audit (`docs/OBSERVER_FRAME_FIXTURE.md`). Reused
  by OBS-DOP.\* runtime validation (alongside
  OBS-PERCEPT.9).
- **OBS-PERCEPT.1 – OBS-PERCEPT.10** — Primary-ray
  perception transform arc capstone
  (`docs/OBSERVER_PERCEPTION_ARC_AUDIT.md`). The
  unified-helper pattern + the three-gate logic +
  the five-axis cross-backend symmetry framework +
  the OBS-PERCEPT.9 fixture all carry forward.

Adjacent precedents this task brief mirrors:

- **`docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`**
  (OBS-PERCEPT.2) — the canonical task-brief shape
  this document mirrors (the same 9-section
  operator-facing layout with the operator's 5
  required topics elevated to §1-§5).
- **`docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_TASK.md`**
  (OBS-P.1) — the precedent migration task brief
  that defined the OBS-P.2 guarded-ternary half-step
  the OBS-DOP.\* arc completes.
- **`docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md`** (the
  FIELD-BEAUTY arc's task brief precedent) +
  **`docs/MANIFOLD_DEBUG_AOV_TASK.md`** (MANI-I.7).

---

## 1. Exact goal

**Migrate the per-pixel Doppler color shift +
searchlight intensity modulation runtime reads onto
the `ObserverFrame` POD, behind a unified three-gate
activation contract identical to the OBS-PERCEPT.3
primary-ray aberration helper's contract. The
existing math leaves
(`rr::relativity::dopplerFactor(rel, dir)`,
`rr::relativity::applyDopplerColor(rgb, D, strength)`,
`rr::relativity::searchlightFactor(D)`) are preserved
verbatim; the operative change is the **input source
discipline** and the **activation gate discipline**.

### 1.1 Doppler / searchlight runtime reads from ObserverFrame

The OBS-DOP.\* arc lifts the kernel-side Doppler /
searchlight input source from the post-OBS-P.2
guarded ternary (`(perception_mode ==
ConstantVelocityMinkowski) ? observer_frame.beta :
observer.velocity`) to the unconditional
`observer_frame.beta` read INSIDE a unified helper.
The helper reads `observer_frame.perception_mode` +
`observer_frame.beta` (3 floats) from the launch
payload; no fallback to `observer.velocity` AFTER the
unified helper engages. The legacy
`rr::relativity::Observer::velocity` field is
preserved verbatim on the host-side `Scene` +
`GpuScene` + `CudaSceneView` + `OptixLaunchParams`
structures so that the dispatch's else-branch (when
`perception_mode != ConstantVelocityMinkowski`)
continues to read the legacy field exactly as today
— the byte-identity anchor for every pre-OBS-DOP.\*
invocation.

### 1.2 Activation gated by ConstantVelocityMinkowski + |beta| > 0

The unified helper applies the **same three-gate
logic** the OBS-PERCEPT.3 helper applied at the
primary-ray aberration site:

1. **Outer gate**: `observer_frame.perception_mode
   == PerceptionMode::ConstantVelocityMinkowski`.
   Default `Identity` closes the gate; the reserved
   `CurvedChartGeodesicPlaceholder` closes it per
   master rule #3 (placeholder honesty).
2. **Inner gate**: `|beta|² > 0` (NaN-safe squared-
   magnitude check; matches OBS-PERCEPT.3's helper at
   `ObserverFrame.h:565-571`). Squared form avoids
   the `sqrt` cost; the explicit
   `!(beta2 > 0.0f)` form catches NaN beta
   components defence-in-depth on top of the
   OBSERVER.6 adapter's pre-clamping.
3. **Safe-clamp**: relies on the OBSERVER.6
   adapter's `clampBeta(|beta|, max_beta = 0.999999f)`
   pre-clamping (no kernel re-clamp).

When BOTH gates open, the unified helper invokes the
preserved math leaves (`dopplerFactor` /
`applyDopplerColor` / `searchlightFactor`) with
`observer_frame.beta` as the canonical beta source.
When either gate closes, the helper returns the
**identity result** (input color unchanged for the
Doppler color shift; unity factor for the searchlight
scaling) — matching the math leaves' existing
identity behaviour at `|beta| = 0`.

### 1.3 Three-effect scope

The OBS-DOP.\* arc consolidates THREE post-shading
sites onto the unified abstraction:

- **`dopplerFactor(rel, direction)`** — per-pixel
  Doppler factor `D`. Currently called once per
  pixel + reused for both the color shift and the
  searchlight scaling.
- **`applyDopplerColor(rgb, D, strength)`** —
  artistic-approximation Doppler color shift.
  Modulates the input RGB toward warm/cool tints
  based on `D` + a per-launch strength scalar.
- **`searchlightFactor(D)`** + **linear scaling**
  — bolometric `D⁴` intensity scaling. Multiplies
  the per-pixel color by `1 + (D⁴ - 1) * strength`.

The unified helper composition preserves the
`dopplerFactor`-computed-once + reused-twice pattern
(the per-pixel `D` is shared between the color shift
and the searchlight scaling on both CUDA and OptiX
today; the OBS-DOP.\* helper preserves this).

### 1.4 Math leaves preserved verbatim

The OBS-DOP.\* arc does NOT modify any
`src/relativity/RelativityMath.h` math leaf. The
existing `dopplerFactor(rel, dir)` /
`dopplerFactor(beta_vec, dir)` /
`applyDopplerColor(rgb, D, strength)` /
`searchlightFactor(D)` / `precompute_relativity(beta_vec)`
helpers stay verbatim. The OBS-DOP.\* unified helper
**composes** the existing leaves; it does NOT
introduce new SR math specialisations.

The OBS-DOP.\* arc preserves the OBSERVER.6 +
OBS-P.2 + OBS-PERCEPT.3 single-source-of-truth math
discipline: both backends (CUDA + OptiX) invoke the
same RR_HD inline math leaves through the unified
helper, producing structurally byte-identical PTX/SASS
arithmetic.

---

## 2. Required behavior

The OBS-DOP.\* implementation slices MUST satisfy
the following four load-bearing invariants. Each
inherits from the OBS-PERCEPT.3 helper's contract
verbatim, applied to the Doppler / searchlight
scope.

### 2.1 Default observer remains no-op

When the operator passes `--observer-perception-mode
default` (or no `--observer-perception-mode` flag —
the default `PerceptionMode::Identity`), the unified
helper's outer gate closes; the kernel's dispatch
falls back to the **legacy** Doppler / searchlight
math (which consumes the OBS-P.2 ternary's
`observer.velocity` fallback through the existing
`rel` snapshot). Every existing CLI invocation
WITHOUT `--observer-perception-mode relativistic`
preserves byte-identical PPM output to the
pre-OBS-DOP.\* baseline:

- `--render-pathtrace`
- `--render-optix-pathtrace`
- `--render-scene`
- `--render-mesh-scene`
- `--render-material-scene`
- `--render-direct-lighting`
- `--render-aovs`
- `--render-optix-aovs`
- `--render-relativistic`
- `--render-aovs --denoise`
- `--render-aovs --manifold-debug`
- `--render-aovs --observer-debug`
- every diagnostic render

The default `ObserverFrame{}` carries `perception_mode
= Identity`; the unified helper's outer gate closes;
the dispatch's else-branch fires; the legacy
`dopplerFactor(rel, dir)` + `applyDopplerColor(rgb,
D, strength)` + `searchlightFactor(D)` chain runs
reading the OBS-P.2 ternary's `observer.velocity`
fallback path; byte-identical output to the
post-OBS-PERCEPT.10 baseline.

This mirrors the OBS-PERCEPT.3 dispatch structure at
`CudaTestKernel.cu:248-258` verbatim:

```cpp
if (params.enable_doppler) {           // legacy flag-guard preserved
    if (perception_active) {
        color = rr::manifold::apply_observer_doppler_color(
            observer_frame, color, ray.direction,
            params.doppler_color_strength);
    } else {
        color = rr::relativity::applyDopplerColor(   // legacy fallback
            color, D, params.doppler_color_strength);
    }
}
```

The exact helper signature is the OBS-DOP.2 / OBS-DOP.3
implementer's call (§7 documents the design space);
the dispatch shape must match the OBS-PERCEPT.3
precedent verbatim.

### 2.2 `beta = 0` remains no-op

When the operator passes `--observer-perception-mode
relativistic` BUT `--observer-beta 0` (or no
`--observer-beta` flag — default `0.0`), the unified
helper's outer gate OPENS but the inner gate
(`|beta|² > 0`) CLOSES. The helper returns the
**identity result** at each site:

- `apply_observer_doppler_color(observer_frame, rgb,
  dir, strength)` returns `rgb` unchanged.
- `apply_observer_searchlight_scale(observer_frame,
  dir, strength)` returns `1.0f` (or returns the
  input color unchanged, depending on the chosen
  helper signature per §7).

The framebuffer output is byte-identical to the
`--observer-perception-mode default` invocation on
the same scene + the same beta=0 input. This is a
**new explicit invariant** the OBS-DOP.\* arc
introduces; the pre-OBS-DOP.\* behaviour (post-
OBS-P.2) has the same practical effect (the math
leaves' identity at `|beta| = 0` is verified by
`tests/relativity_tests.cpp`) but the OBS-DOP.\*
explicit inner gate makes the invariant a
documented contract verifiable by a single
kernel-arm conditional rather than a
math-leaf-incidental behaviour.

### 2.3 Existing legacy config remains adapter/input only

The legacy `rr::relativity::Observer::velocity` field
is preserved verbatim on the host-side payload
structures (`Scene::observer`, `GpuScene::observer`,
`CudaSceneView::observer`,
`OptixLaunchParams::observer`). The OBS-P.2 ternary
fallback path at the OBS-DOP.\* sites continues to
read `observer.velocity` exactly as today when the
outer gate closes. The OBSERVER.6
`build_observer_frame_from_camera(...)` adapter
continues to consume `rr::relativity::Observer` as
one of its three inputs (per OBSERVER.7 audit check
#1) — the adapter's beta-resolution priority (CLI
overlay > zero-direction-sentinel fallback > legacy
`observer.velocity`) is preserved verbatim.

The `RelativityParams::enable_doppler` /
`enable_searchlight` flag-guards continue to gate
**whether** the helper is called; the OBS-DOP.\*
unified helper internally applies the three-gate
observer-frame logic when the kernel-side flag-guard
opens. The
`RelativityParams::doppler_color_strength` /
`searchlight_strength` scalars continue to flow
through the legacy `RelativityParams` payload exactly
as today.

Three observations confirm legacy preservation:
- `RelativityParams` types are NOT modified by the
  arc (mirrors OBS-P.3 audit check #6).
- The OBSERVER.6 adapter's input contract is NOT
  modified.
- The kernel's legacy fallback dispatch branch reads
  the legacy fields directly when the outer gate
  closes.

This is the documented OBS-P.1 §2.4 "load-bearing
byte-identity anchor" applied at the OBS-DOP.\* scope.

### 2.4 CUDA and OptiX semantics must match

Both backends consume the **same** unified helper
through the **same** dispatch shape with the **same**
math leaves and **same** gate semantics. The
OBS-PERCEPT.6 audit's §3.7 **five-axis cross-backend
symmetry** framework applies verbatim:

| Axis | CUDA | OptiX |
|------|------|-------|
| POD type | `view.observer_frame` (CudaSceneView) | `optixLaunchParams.observer_frame` |
| Shared helper | `rr::manifold::apply_observer_doppler_color(...)` / `apply_observer_searchlight_scale(...)` | (same) |
| Dispatch shape | `if (params.enable_doppler/searchlight) { if (perception_active) { unified helper } else { legacy math leaf } }` | (same) |
| Math leaf | `rr::relativity::dopplerFactor(rel, dir)` + `applyDopplerColor(rgb, D, strength)` + `searchlightFactor(D)` | (same) |
| Gate semantics | `perception_mode == ConstantVelocityMinkowski` + `|beta|² > 0` | (same) |

Cross-backend bit-identity for the Doppler /
searchlight transform is **structurally guaranteed by
construction** (same RR_HD inline code on both CUDA
and OptiX). Empirical SDK-host verification (cmp
`aov_beauty.ppm` vs `optix_aov_beauty.ppm` on the
OBS-PERCEPT.9 fixture) is deferred to the combined
FIELD-\* + OBS-PERCEPT CLI bridge slice's audit on a
CUDA + OptiX-SDK host (per OBS-PERCEPT.10 §4.2 (a)).

---

## 3. Scope

The OBS-DOP.\* arc is deliberately narrow:

### 3.1 Doppler factor

The per-pixel `D = dopplerFactor(rel, direction)`
computation. Currently called once per pixel on both
backends (the result is shared between the color
shift and the searchlight scaling). The OBS-DOP.\*
arc preserves the once-per-pixel compute discipline;
the unified helper either:

- **Option A**: computes `D` internally on each call
  (the kernel pays two `dot` + branch per pixel —
  one inside `apply_observer_doppler_color`, one
  inside `apply_observer_searchlight_scale`).
- **Option B**: accepts the precomputed `D` from the
  caller (the kernel computes `D` once outside the
  helpers; passes it as an argument).
- **Option C**: introduces a combined
  `apply_observer_post_shading_perception(observer_frame,
  rgb, direction, doppler_strength, searchlight_strength)
  → Vec3` that computes `D` once internally and
  returns the post-Doppler post-searchlight color.

OBS-DOP.2 picks the design (§7 enumerates the trade-
offs). The OBS-PERCEPT.3 helper precedent + the OptiX
`apply_doppler_and_searchlight_with_D(...)` helper at
`OptixPrograms.cu:99-124` (which already accepts the
precomputed `D`) suggests **Option B** is the natural
shape.

### 3.2 Searchlight factor

The `D⁴ = searchlightFactor(D)` computation +
`scale = 1.0f + (D⁴ - 1.0f) * searchlight_strength`
linear modulation. The unified helper applies the
three-gate logic + returns the post-scaling
factor (or the post-scaled color, per the chosen
signature).

### 3.3 Color / intensity modulation

Both the Doppler color shift (`applyDopplerColor(rgb,
D, strength)`) AND the searchlight intensity scaling
(`color * scale`) are post-shading modulations
applied per pixel on the post-aberration color. The
OBS-DOP.\* arc consolidates BOTH modulations onto the
unified abstraction.

### 3.4 No new physics model

The OBS-DOP.\* arc does NOT introduce new physics:

- **No new Doppler model.** The existing
  `applyDopplerColor` artistic approximation is
  preserved verbatim; the future spectral pipeline
  remains the canonical lift point.
- **No new searchlight model.** The existing
  bolometric `D⁴` form is preserved; the monochromatic
  `D³` form remains an unused but documented
  alternative.
- **No spectral pipeline.** The
  `applyDopplerColor` helper's `tanh(0.5 * log(D))`
  artistic remap stays in place.
- **No new aberration helper.** OBS-PERCEPT.3 already
  shipped the primary-ray aberration helper; the
  OBS-DOP.\* arc consumes it indirectly through the
  pre-existing dispatch shape but does NOT modify it.
- **No frame-dragging / gravitational Doppler.** The
  CurvedChartGeodesicPlaceholder slot remains
  reserved per master rule #3.
- **No retarded-time approximation.** That belongs
  to a future arc family.

### 3.5 No quantum effects

The arc does NOT introduce quantum-state Doppler
visualisations (e.g. spectral broadening from
relativistic Maxwell-Jüttner distributions,
observer-dependent wavefunction collapse). The
`FieldType::ProbabilityAmplitudePlaceholder` slot
remains reserved per the FIELD-I.1 plan §2.4; the
OBS-DOP.\* arc does NOT extend it.

### 3.6 No path-tracer per-bounce Doppler

The arc applies the unified perception transform at
the post-shading site of each kernel arm — the
existing site where Doppler / searchlight currently
fires. Per-bounce Doppler reapplication (Option B
from the OBS-PERCEPT.1 plan §5.2 applied to Doppler)
is OUT OF SCOPE; future FRAME-PROPAGATION.\* arc may
lift this when authorised. The OBS-DOP.\* path-
tracer Doppler / searchlight kernel arm applies the
transform at the existing single site per kernel
program; secondary rays propagate in their natural
Minkowski frame (matching the pre-OBS-DOP.\*
baseline exactly).

---

## 4. What must not be touched

Per master rule #3 + #12 and the operator's
OBS-DOP.1 brief, the OBS-DOP.\* implementation
slices MUST NOT:

### 4.1 No new manifold math

The OBS-DOP.\* arc does NOT touch any
`src/manifold/` source file other than the
**`src/manifold/ObserverFrame.h`** header (extended
by the unified helper definitions; mirrors the
OBS-PERCEPT.3 precedent's location choice). Specifically:

- No new `world_to_chart(...)` / `chart_to_world(...)`
  helpers.
- No new `CoordinateChart` modifications.
- No extension of `SchwarzschildLikeWarp` /
  `PenroseLikeCompactification` math leaves.
- No new `MetricTensor` / `GeodesicState` field.

The Doppler / searchlight transforms operate
ORTHOGONAL to the manifold layer per the OBS-PERCEPT.1
plan §4. The unified helper(s) live in
`ObserverFrame.h` because of the same dependency
constraint OBS-PERCEPT.3 honored: the helper takes
`ObserverFrame` (in `rr_manifold`) AND invokes
`rr::relativity::*` math leaves (in `rr_relativity`).
Since `rr_manifold` already PUBLIC-depends on
`rr_relativity` (per `CMakeLists.txt:363`), the helper
must live in `rr_manifold` (not `rr_relativity`) to
preserve the dependency direction.

### 4.2 No secondary-ray policy change unless already present

The pre-OBS-DOP.\* baseline applies Doppler /
searchlight at:

- **CUDA `k_render_scene`** post-shading (line 656+).
- **CUDA `k_sphere_relativistic`** post-shading (line 277+).
- **OptiX `__raygen__pinhole`** + threaded through
  payload register 3 to `__closesthit__radiance` +
  `__miss__radiance` (consumed in the shared
  `apply_doppler_and_searchlight_with_D(...)` helper
  at `OptixPrograms.cu:99-124`).
- **OptiX `__raygen__pathtrace`** post-spp-averaging
  (line 1538+).

The OBS-DOP.\* arc preserves each of these exact
site locations verbatim — the unified helper replaces
the per-site call shape ONLY at the sites where the
pre-baseline already applied Doppler / searchlight. No
new sites are added; no existing site is removed. The
path-tracer's bounce-loop body is byte-identical
across the arc (verified by `git diff
PRE_OBS_DOP..POST_OBS_DOP -- src/cuda/CudaPathTracer.cu
src/optix/OptixPrograms.cu` returning zero changes
inside the bounce-loop bodies on both backends; this
matches the OBS-PERCEPT.6 audit's check #7 verbatim).

The OBS-PERCEPT.1 plan §5.2 Option A (primary-ray-
only) is preserved verbatim for Doppler / searchlight
as well — the existing single application per spp /
per primary-pixel pattern continues; no per-bounce
Doppler / searchlight reapplication.

### 4.3 No field interpretation changes

The OBS-DOP.\* arc does NOT touch any `src/field/`
source file. The FIELD-I.\* + FIELD-BEAUTY.\* arc
family's surfaces (`ScalarField.h`, `FieldMapping.h`,
`FieldInterpreter.h`, `FieldType.h`, the
`FieldMappingTarget` enum, etc.) are preserved
verbatim. The OBS-DOP.\* arc is parallel to the
FIELD-\* arc family per the OBS-PERCEPT.1 plan §1.3.

The `FieldMapping` consumer (the
`FieldMappingTarget::ColorMultiplier` /
`Emission` modulations) runs BEFORE the post-shading
Doppler / searchlight site in the existing pipeline;
OBS-DOP.\* preserves this ordering (the per-pixel
color reaching the Doppler site is the post-
field-mapping color).

### 4.4 No C4D / server / UI / node-editor touch

Standard discipline carried forward from every prior
arc; the OBS-DOP.\* arc does not touch any DCC /
server / UI / node-editor surface (no
`src/server/` / `bridges/` / `tools/` change).

### 4.5 No new CLI flag

The existing `--observer-perception-mode
relativistic` (from OBSERVER.4) is the load-bearing
gate. No new `--observer-perception-doppler` or
`--observer-perception-searchlight` flag this arc;
the gate is the perception-mode flag. The existing
`--observer-beta` + `--observer-direction` flags
(from OBSERVER.4) continue to control the observer
beta input. The existing `--render-relativistic`
action stays available (unchanged behaviour; uses
the legacy Doppler / searchlight fallback path
because it does NOT engage
`--observer-perception-mode`).

### 4.6 No new ObserverFrame POD field

The OBSERVER.2-shipped `ObserverFrame` POD's nine
fields (`perception_mode`, `beta`, `right`, `up`,
`forward`, `position4`, `velocity4`, `proper_time`,
`coordinate_time`) are read as-is. No new fields
added; no field-offset changes; no signature-
extension churn.

### 4.7 No legacy `observer.velocity` removal

The legacy `rr::relativity::Observer::velocity` field
is preserved verbatim on the host-side payload
structures. The OBS-DOP.\* unified helper reads
`observer_frame.beta` exclusively when the gates open,
but the legacy fallback dispatch branch reads
`observer.velocity` directly when the outer gate
closes. The `--render-relativistic` CLI action (which
does NOT pass `--observer-perception-mode`) continues
to use the legacy field path with byte-identical
output to the pre-OBS-DOP.\* baseline.

### 4.8 No new debug AOV

The OBS-PERCEPT.8 slice landed the
`AOVType::ObserverAberrationMagnitude = 9` +
`AOVType::ObserverDirection = 10` data-model entries;
the kernel-arm bridge for those AOVs is the
renumbered OBS-PERCEPT.11 slot. The OBS-DOP.\* arc
does NOT add a new Doppler-magnitude or searchlight-
factor diagnostic AOV — that would belong to a
follow-up `OBS-DOP-AOV.*` arc if authorised. The
existing `ObserverBeta` AOV (OBSERVER.13) continues
to visualise the observer-frame beta payload
verbatim.

### 4.9 No fixture authoring

The OBS-PERCEPT.9 fixture
(`scenes/test_observer_primary_ray_perception.rrscene`)
is sufficient for the OBS-DOP.\* arc's runtime
verification. No new fixture this arc; the OBS-F.2 +
OBS-PERCEPT.9 fixture pair covers the runtime
scenarios per §5.

---

## 5. PASS criteria

The OBS-DOP.\* arc's acceptance gate is satisfied when
ALL of the following hold across the OBS-DOP.2 +
OBS-DOP.3 + OBS-DOP.4 + OBS-DOP.5 sub-slices.
Per-sub-slice acceptance criteria are detailed
inside each sub-slice task brief (created at
sub-slice insertion); this section enumerates the
arc-wide structural + behavioural + test + runtime
gates that the arc must satisfy.

### 5.1 Structural

- [ ] A unified Doppler helper
      `rr::manifold::apply_observer_doppler_color(...)`
      (or equivalent abstraction; final signature
      OBS-DOP.2's call per §7) exists in
      `src/manifold/ObserverFrame.h` (RR_HD inline).
      The helper takes the `ObserverFrame` POD + the
      ray direction + the existing
      `RelativityParams::doppler_color_strength` (or
      a passed strength scalar) + the input color and
      applies the three-gate logic (§1.2 +
      §2.1-§2.3) internally.
- [ ] A unified searchlight helper
      `rr::manifold::apply_observer_searchlight_scale(...)`
      (or equivalent) exists in the same file with
      the same RR_HD inline + three-gate-logic
      contract.
- [ ] The CUDA `k_render_scene` post-shading site
      consumes the unified helpers (lines 656-675;
      replaces the existing direct
      `applyDopplerColor` + `searchlightFactor`
      calls with the dispatch pattern from §2.1).
- [ ] The CUDA `k_sphere_relativistic` post-shading
      site consumes the unified helpers (lines
      277-292; same dispatch shape).
- [ ] The OptiX `__raygen__pinhole` site +
      `__closesthit__radiance` / `__miss__radiance`
      consumers (via the shared
      `apply_doppler_and_searchlight_with_D(...)`
      helper at `OptixPrograms.cu:99-124`) consume
      the unified helpers. The helper at line 99-124
      may be retained as a CUDA-host-side bridging
      shim that calls the unified
      `apply_observer_doppler_color(...)` +
      `apply_observer_searchlight_scale(...)`, OR
      may be removed and the unified helpers called
      directly at the closesthit / miss sites
      (OBS-DOP.3 implementer's choice).
- [ ] The OptiX `__raygen__pathtrace` post-spp site
      consumes the unified helpers (line 1538+;
      same dispatch shape).
- [ ] Secondary bounce rays in
      `CudaPathTracer.cu::k_pathtrace_sample` are
      NOT modified for Doppler / searchlight (per
      §3.6 + §4.2). The CUDA path-tracer has no
      pre-existing Doppler / searchlight call sites
      per the OBS-P.3 audit's scope correction
      (5 sites instead of 6).
- [ ] No `src/manifold/` modification other than
      `ObserverFrame.h` (mirrors OBS-PERCEPT.3 audit
      check on `src/manifold/` `git diff` zero-hit
      verification).
- [ ] No `src/field/` / `src/core/Config.h` /
      `src/core/CommandLine.cpp` / `src/io/` /
      `src/scene/` / `src/main.cpp` modification.
- [ ] No `src/relativity/` math leaf modification
      (the existing `dopplerFactor` +
      `applyDopplerColor` + `searchlightFactor`
      helpers stay verbatim).
- [ ] No new CLI flag.
- [ ] No new ObserverFrame POD field.
- [ ] No new test binary (the existing
      `relativity_tests` + `manifold_identity_tests`
      targets gain new RR_CHECK assertions on the
      unified helpers; final count per sub-slice).

### 5.2 Behavioural

- [ ] **Default-state byte identity**. Every
      `--render-*` invocation WITHOUT
      `--observer-perception-mode relativistic`
      produces pixel-bit-identical PPM output to the
      pre-OBS-DOP.\* baseline. This includes:
      `--render-pathtrace`,
      `--render-optix-pathtrace`,
      `--render-scene`, `--render-mesh-scene`,
      `--render-material-scene`,
      `--render-direct-lighting`,
      `--render-aovs`, `--render-optix-aovs`,
      `--render-relativistic` (the legacy SR action
      with non-zero `observer.velocity`),
      `--render-aovs --observer-debug`, every
      diagnostic render. (The unified helper's outer
      gate closes; the dispatch's else-branch fires;
      the legacy `dopplerFactor` /
      `applyDopplerColor` / `searchlightFactor`
      chain runs reading the OBS-P.2 ternary's
      fallback `observer.velocity` path exactly as
      today.)
- [ ] **Zero-beta byte identity**. On
      `--observer-perception-mode relativistic
      --observer-beta 0 <fixture>`, the output is
      byte-identical to the
      `--observer-perception-mode default
      <fixture>` invocation. (The unified helper's
      inner gate closes; the helper returns
      identity.)
- [ ] **Non-zero-beta consistency**. On
      `--observer-perception-mode relativistic
      --observer-beta 0.5 --observer-direction
      1,0,0 <fixture>`, the framebuffer shows
      Doppler + searchlight modulation
      structurally equivalent to the
      post-OBS-PERCEPT.10 baseline on the same CLI
      invocation. (The unified helper composes the
      same math leaves with `observer_frame.beta`
      as the source; the OBS-P.2 ternary's gated
      path produced byte-identical output.)
- [ ] **OBS-PERCEPT.9 fixture + relativistic
      perception**. The
      `scenes/test_observer_primary_ray_perception.rrscene`
      fixture + `--observer-perception-mode
      relativistic --render-aovs` produces a
      beauty PPM showing the oblique beta
      direction's Doppler color shift +
      searchlight beaming asymmetry. Cross-fixture
      comparison against OBS-F.2's axis-aligned
      beta direction shows visibly different
      Doppler patterns.
- [ ] **Cross-backend bit-identity** (CUDA vs
      OptiX) for the unified helper's output is
      structurally guaranteed because both backends
      invoke the same RR_HD inline helper + the
      same math leaves; verified empirically at the
      future combined CLI bridge slice's SDK-host
      audit (per OBS-PERCEPT.10 §4.2 (a)).

### 5.3 Test surface

- [ ] `ctest` reports `13/13 passed` on the
      audit-host build at every OBS-DOP.\* sub-slice
      landing.
- [ ] `manifold_identity_tests` grows by ~8 new
      RR_CHECK assertions covering the unified
      helpers' behaviour:
        - `apply_observer_doppler_color` on
          `Identity` mode returns the input color
          unchanged.
        - On `CurvedChartGeodesicPlaceholder` mode +
          non-zero beta returns the input color
          unchanged (placeholder honesty).
        - On `ConstantVelocityMinkowski` mode +
          `beta = 0` returns the input color
          unchanged.
        - On `ConstantVelocityMinkowski` + non-zero
          beta + non-trivial direction returns a
          modulated color matching the legacy
          `applyDopplerColor(rgb, dopplerFactor(rel,
          dir), strength)` chain bit-for-bit.
        - Same 4-assertion battery for
          `apply_observer_searchlight_scale`.
- [ ] `relativity_tests` count unchanged unless
      the OBS-DOP.2 implementer adds shared-helper
      composition tests there (operator discretion;
      not required).
- [ ] `cli_tests` count unchanged (no new CLI flag).
- [ ] `renderer_tests` count unchanged.
- [ ] `field_tests` count unchanged.
- [ ] **OptiX-ON-no-SDK** build clean (14/14 ctest
      PASS) at every OBS-DOP.\* sub-slice landing.

### 5.4 Documentation

- [ ] `docs/BUILD_PLAN.md` per-sub-slice entries
      added at OBS-DOP.2, OBS-DOP.3, OBS-DOP.4,
      OBS-DOP.5 landings (each mirrors the
      existing OBS-PERCEPT.\* + OBSERVER.\* +
      FIELD-I.\* + FIELD-BEAUTY.\* per-slice
      entries' "What ships / What does NOT ship /
      Acceptance / Module status changes" rubric).
- [ ] Per-sub-slice audit docs landed at the
      operator-cadence-bound audit slots (mirrors
      the OBS-PERCEPT.\* arc's audit-slot insertion
      discipline).
- [ ] An arc-level capstone audit doc
      (`docs/OBSERVER_DOPPLER_SEARCHLIGHT_ARC_AUDIT.md`)
      lands at OBS-DOP.5 (or the appropriately
      renumbered capstone slot per the audit-slot
      insertion ladder) with verdict
      PASS / PASS_WITH_RUNTIME_DEFERRED / REPAIR /
      BLOCKED, mirroring the OBS-PERCEPT.10 +
      FIELD-BEAUTY.8 capstone audit shapes.

### 5.5 Runtime CUDA / OptiX deferral

- The audit-host build (no CUDA SDK, no OptiX SDK)
  cannot directly verify the kernel arms' empirical
  PPM outputs. The runtime checks above are
  STRUCTURALLY satisfied by the unified-helper
  composition + the preserved math-leaf identity +
  the OBS-PERCEPT.10 capstone's five-axis cross-
  backend symmetry framework applied to the OBS-DOP.\*
  scope; the empirical SDK-host PPM-cmp verification
  is DEFERRED to a future combined FIELD-\* +
  OBS-PERCEPT + OBS-DOP CLI bridge slice's audit on
  a CUDA + OptiX-SDK host.

- The deferred runtime scenarios are:
    - **§5.5.1** Default-state byte identity.
      Run `--render-aovs <every fixture>` pre +
      post; `cmp` PPMs byte-by-byte. Expected:
      byte-identical (the unified helper's outer
      gate closes; the dispatch's else-branch fires
      the legacy chain reading the OBS-P.2 ternary
      fallback `observer.velocity` path).
    - **§5.5.2** Zero-beta byte identity. Run
      `--render-aovs --observer-perception-mode
      relativistic --observer-beta 0 <fixture>`;
      `cmp` against `--observer-perception-mode
      default` PPMs. Expected: byte-identical (the
      inner gate closes; the helper returns
      identity).
    - **§5.5.3** Non-zero-beta consistency. Run
      `--render-aovs --observer-perception-mode
      relativistic --observer-beta 0.5
      --observer-direction 1,0,0 <fixture>` pre +
      post; `cmp` PPMs. Expected: byte-identical
      because the unified helper composes the
      same math leaves with the same `observer_frame.beta`
      source the OBS-P.2 ternary used in the gated
      path.
    - **§5.5.4** OBS-PERCEPT.9 fixture runtime.
      Run the fixture (oblique beta direction
      `[0.6, -0.8, 0.0]` + FOV 60°) with
      relativistic perception engaged; verify
      visible Doppler color shift + searchlight
      beaming asymmetry matches the expected
      observer's view of the scene from that
      oblique beta.
    - **§5.5.5** Cross-backend cmp. Run both
      `--render-aovs --observer-perception-mode
      relativistic <fixture>` and
      `--render-optix-aovs --observer-perception-mode
      relativistic <fixture>`; `cmp` the resulting
      `aov_beauty.ppm` vs `optix_aov_beauty.ppm`.
      Expected: byte-identical PPM (five-axis
      symmetry → same RR_HD inline code on both
      backends).
    - **§5.5.6** Path-tracer post-spp Doppler /
      searchlight. Run `--render-pathtrace
      --observer-perception-mode relativistic
      <fixture>` pre + post; `cmp` PPMs. Verifies
      the OBS-DOP.\* arc's preservation of the
      single-application-per-pixel pattern at the
      pathtracer's post-spp-averaging site.
    - **§5.5.7** Doppler debug AOV interaction.
      The OBSERVER.13 `ObserverBeta` AOV's PPM
      output is unchanged regardless of which
      kernel arm path engages (verifies the AOV
      remains a read-only view of the payload, not
      a function of the perception transform).

---

## 6. Activation logic

The OBS-DOP.\* arc's three-gate activation logic
mirrors the OBS-PERCEPT.3 helper at
`ObserverFrame.h:553+` verbatim, applied to the
Doppler / searchlight site.

### 6.1 Outer gate — perception_mode

```cpp
if (obs_frame.perception_mode !=
        PerceptionMode::ConstantVelocityMinkowski) {
    return /* identity result */;
}
```

Closes on `Identity` (default `ObserverFrame{}`); on
`CurvedChartGeodesicPlaceholder` (reserved per master
rule #3). Opens only on `ConstantVelocityMinkowski`.

### 6.2 Inner gate — |beta|² > 0

```cpp
const rr::math::Vec3 beta = obs_frame.beta;
const float beta2 = beta.x * beta.x
                  + beta.y * beta.y
                  + beta.z * beta.z;
if (!(beta2 > 0.0f)) {
    return /* identity result */;
}
```

Squared-magnitude check avoids `sqrt` cost; the
explicit `!(beta2 > 0.0f)` form catches NaN beta
components defence-in-depth on top of the OBSERVER.6
adapter's pre-clamping.

### 6.3 Math leaf invocation

When BOTH gates open, the unified helper invokes the
preserved math leaves:

```cpp
const auto rel = rr::relativity::precompute_relativity(beta);
const float D = rr::relativity::dopplerFactor(rel, direction);

// For the Doppler color shift helper:
return rr::relativity::applyDopplerColor(rgb, D, strength);

// For the searchlight scale helper:
const float D4 = rr::relativity::searchlightFactor(D);
return 1.0f + (D4 - 1.0f) * strength;
```

The math leaves are RR_HD inline; same code emitted on
both backends; cross-backend bit-identity by
construction.

### 6.4 Three-layer no-op anchor

Mirrors the OBS-PERCEPT.3 three-layer anchor
verbatim:

- **Layer 1 — helper inner gate** (the OBS-DOP.\*
  contract): explicit `!(beta2 > 0.0f)` short-
  circuit.
- **Layer 2 — math leaf identity** (pre-existing):
  `dopplerFactor` at `beta_mag = 0` returns
  `1.0f`; `applyDopplerColor` at `D = 1`
  returns `rgb` unchanged; `searchlightFactor`
  at `D = 1` returns `1.0f`. Defence-in-depth.
- **Layer 3 — OBSERVER.6 adapter** (host-side):
  emits `observer_frame.beta = (0, 0, 0)` exactly
  on zero-beta inputs.

Identical anchor on both backends; both consume the
same shared helper. Every existing `--render-*`
invocation without `--observer-perception-mode
relativistic` preserves byte-identical PPM output to
the pre-OBS-DOP.\* baseline.

### 6.5 Dispatch shape

The kernel-side dispatch shape (per the OBS-PERCEPT.3
precedent at `CudaTestKernel.cu:248-258`) is:

```cpp
const bool perception_active =
    (observer_frame.perception_mode ==
        PerceptionMode::ConstantVelocityMinkowski);

if (params.enable_doppler) {
    if (perception_active) {
        color = rr::manifold::apply_observer_doppler_color(
            observer_frame, color, ray.direction,
            params.doppler_color_strength);
    } else {
        color = rr::relativity::applyDopplerColor(
            color, D, params.doppler_color_strength);
    }
}

if (params.enable_searchlight) {
    float scale;
    if (perception_active) {
        scale = rr::manifold::apply_observer_searchlight_scale(
            observer_frame, ray.direction,
            params.searchlight_strength);
    } else {
        const float D4 = rr::relativity::searchlightFactor(D);
        scale = 1.0f + (D4 - 1.0f) * params.searchlight_strength;
    }
    color = color * scale;
}
```

The `params.enable_doppler` + `params.enable_searchlight`
flag-guards continue to gate WHETHER the helper /
legacy path is called; the unified helper internally
applies the three-gate observer-frame logic. The
`perception_active` boolean is reused from the
OBS-P.2 ternary (no recomputation; same scope-uniform
value).

---

## 7. Files likely involved

The implementation slices are expected to touch the
following files. Numbers in parentheses are rough
net-line estimates from comparable past slices
(OBS-PERCEPT.3 + OBS-PERCEPT.5).

| Layer | File | Why |
|-------|------|-----|
| Manifold helper | `src/manifold/ObserverFrame.h` (+50 to +100) | Add `apply_observer_doppler_color(observer_frame, rgb, direction, strength) → Vec3` + `apply_observer_searchlight_scale(observer_frame, direction, strength) → float` RR_HD inline helpers. Each internally checks the three-gate logic + invokes the preserved math leaves. Mirrors the OBS-PERCEPT.3 helper's location + signature shape. Final API design (one combined helper vs two split helpers vs precomputed-D variant) is the OBS-DOP.2 implementer's call per §3.1. |
| CUDA kernel | `src/cuda/CudaTestKernel.cu` (+20 to +40) | Consolidate the two post-shading sites onto the unified helpers: `k_render_scene` Doppler / searchlight block (lines 656-675); `k_sphere_relativistic` Doppler / searchlight block (lines 277-292). Each site adopts the §6.5 dispatch shape. |
| OptiX programs | `src/optix/OptixPrograms.cu` (+30 to +60) | Consolidate the OptiX sites: `__raygen__pinhole` Doppler factor compute + payload-register-3 thread; `__closesthit__radiance` / `__miss__radiance` consumers (via the shared `apply_doppler_and_searchlight_with_D(...)` helper at lines 99-124); `__raygen__pathtrace` post-spp Doppler / searchlight (line 1538+). The shared `apply_doppler_and_searchlight_with_D(...)` may be retained or removed at the OBS-DOP.3 implementer's discretion. |
| Tests | `tests/manifold_identity_tests.cpp` (+~70 to +120) | Add ~8 RR_CHECK assertions for the unified helpers' three-gate behaviour (mirrors OBS-PERCEPT.3's 13-RR_CHECK extension at the OBS-PERCEPT.3 landing). Identity / placeholder / zero-beta / non-zero-beta cases for each helper. |
| Tests | `tests/relativity_tests.cpp` (+~30 OPTIONAL) | OPTIONAL: add composition tests at the math-leaf level if the OBS-DOP.2 implementer wants to pin the legacy-fallback chain's behaviour explicitly. Not required. |
| Docs | `docs/BUILD_PLAN.md` | Per-sub-slice entries. |
| Docs | `docs/OBSERVER_DOPPLER_SEARCHLIGHT_CUDA_AUDIT.md` (OBS-DOP audit) | Per-sub-slice audit doc landed at the operator-cadence-bound audit slot. |
| Docs | `docs/OBSERVER_DOPPLER_SEARCHLIGHT_OPTIX_AUDIT.md` (OBS-DOP audit) | Per-sub-slice audit doc for the OptiX impl. |
| Docs | `docs/OBSERVER_DOPPLER_SEARCHLIGHT_ARC_AUDIT.md` (capstone) | Arc-level capstone verdict mirroring the OBS-PERCEPT.10 + FIELD-BEAUTY.8 capstone shapes. |
| CMake | none expected | The new helpers live in the already-included `manifold/ObserverFrame.h`; no new ctest target. |

The exact file inventory + line-count budget is the
OBS-DOP.2 / OBS-DOP.3 implementer's choice; this
table is the operator-facing prediction based on the
OBS-PERCEPT.3 / OBS-PERCEPT.5 precedents (~558 net
source lines + ~243 test lines across the two impl
slices).

### 7.1 Helper signature design space

The OBS-DOP.2 implementer picks one of three options
for the helper API. Each option preserves the
three-gate logic + math-leaf composition; they differ
in how the per-pixel `D` value flows:

**Option A — fully self-contained helpers.** Each
helper computes `precompute_relativity(beta) +
dopplerFactor(rel, dir)` internally:

```cpp
rr::math::Vec3 apply_observer_doppler_color(
    const ObserverFrame& obs_frame,
    rr::math::Vec3 rgb,
    rr::math::Vec3 direction,
    float strength);

float apply_observer_searchlight_scale(
    const ObserverFrame& obs_frame,
    rr::math::Vec3 direction,
    float strength);
```

Pros: clean API; no caller-side bookkeeping.
Cons: when both effects are enabled, the kernel pays
the `precompute_relativity` + `dopplerFactor` cost
TWICE per pixel (once inside each helper).

**Option B — caller computes D, helpers consume it
(RECOMMENDED).** The kernel computes `D = dopplerFactor(rel,
dir)` once outside the helpers (preserving the
existing once-per-pixel discipline); the helpers
accept the pre-computed `D`:

```cpp
rr::math::Vec3 apply_observer_doppler_color(
    const ObserverFrame& obs_frame,
    rr::math::Vec3 rgb,
    float D,
    float strength);

float apply_observer_searchlight_scale(
    const ObserverFrame& obs_frame,
    float D,
    float strength);
```

Pros: matches the OptiX `apply_doppler_and_searchlight_with_D(...)`
shim's existing once-per-pixel discipline; preserves
the optimal SR helper cost. Cons: requires
`perception_active` + the pre-computed `D` at the
call site (which is already the existing pattern, so
no new bookkeeping).

**Option C — combined helper.** One helper returns
the post-modulation color:

```cpp
rr::math::Vec3 apply_observer_post_shading_perception(
    const ObserverFrame& obs_frame,
    rr::math::Vec3 rgb,
    rr::math::Vec3 direction,
    float doppler_strength,
    float searchlight_strength,
    bool enable_doppler,
    bool enable_searchlight);
```

Pros: single call site; consolidates the dispatch
internally. Cons: helper carries the
`enable_doppler` / `enable_searchlight` flags;
moves the `RelativityParams` boolean-bool surface
into the helper signature (mirror of the
`RelativityParams` flag-guards at the call site).
Less compositional than Options A / B.

**Recommended**: Option B. Mirrors the OptiX
`apply_doppler_and_searchlight_with_D(...)` shape's
once-per-pixel discipline; preserves the OBS-P.2
ternary's `D` reuse pattern; keeps the helper
signature minimal. The OBS-DOP.2 implementer makes
the final call.

---

## 8. Cross-backend symmetry framework

The OBS-DOP.\* arc inherits the OBS-PERCEPT.6
audit's §3.7 **five-axis cross-backend symmetry
framework** verbatim, applied to the Doppler /
searchlight scope:

| Axis | Required matching |
|------|-------------------|
| **POD type** | Both backends read the same `rr::manifold::ObserverFrame` POD (via `view.observer_frame` on CUDA, `optixLaunchParams.observer_frame` on OptiX). |
| **Shared helper** | Both backends consume the same `rr::manifold::apply_observer_doppler_color(...)` + `apply_observer_searchlight_scale(...)` RR_HD inline functions. |
| **Dispatch shape** | Both backends use the §6.5 dispatch (`if (params.enable_*) { if (perception_active) { unified helper } else { legacy math leaf } }`). |
| **Math leaf** | Both backends invoke `rr::relativity::dopplerFactor(rel, dir)` + `applyDopplerColor(rgb, D, strength)` + `searchlightFactor(D)` through the unified helper. |
| **Gate semantics** | Both backends check `perception_mode == ConstantVelocityMinkowski` (outer gate) + `|beta|² > 0` (inner gate). |

Cross-backend bit-identity is **structurally
guaranteed by construction**. Empirical SDK-host
verification (cmp `aov_beauty.ppm` vs
`optix_aov_beauty.ppm` on the OBS-PERCEPT.9 +
OBS-F.2 fixtures) is deferred to the future
combined FIELD-\* + OBS-PERCEPT + OBS-DOP CLI bridge
slice's audit on a CUDA + OptiX-SDK host.

---

## 9. Sub-slice ladder

The OBS-DOP.\* arc's proposed sub-slice ladder.
Per-slice gate audits may be inserted between impl
slices as operator cadence permits (mirroring the
OBS-PERCEPT.\* arc's audit-slot insertion discipline
where audit slots are inserted in-band — the OBS-DOP
ladder below numbers the **implementation** slices
only; audit slots are added in-band as the operator
requests).

### OBS-DOP.2 — CUDA implementation (impl, kernel arm)

- **Scope:** land the unified Doppler / searchlight
  helpers in `src/manifold/ObserverFrame.h` + the
  CUDA kernel arms in `CudaTestKernel.cu`. Replaces
  the post-OBS-P.2 + post-OBS-PERCEPT.3 direct
  `applyDopplerColor` / `searchlightFactor` calls at
  the two CUDA post-shading sites with the §6.5
  dispatch pattern.
- **Acceptance:** §5.1 + §5.2 (CUDA subset) + §5.3
  (audit-host PASS) + §5.4 (per-sub-slice BUILD_PLAN
  entry) + §5.5 (CUDA-side runtime deferral).
- **What does NOT ship:** OptiX-side wiring
  (deferred to OBS-DOP.3); debug AOV (out of arc
  scope per §4.8); fixture (out of arc scope per
  §4.9 — OBS-PERCEPT.9 reused); arc capstone
  (deferred to OBS-DOP.5).

### OBS-DOP.3 — OptiX implementation (impl, OptiX program arm)

- **Scope:** mirror OBS-DOP.2 on the OptiX path.
  Apply the unified helpers to the four OptiX
  Doppler / searchlight sites
  (`__raygen__pinhole` + `__closesthit__radiance`
  + `__miss__radiance` + `__raygen__pathtrace`).
  Cross-backend math equivalence via the shared
  RR_HD inline helpers + the §8 five-axis
  symmetry framework.
- **Acceptance:** §5.1 + §5.2 (OptiX subset) + §5.3
  (OptiX-ON-no-SDK PASS) + §5.4 + §5.5 (OptiX-side
  runtime deferral).
- **What does NOT ship:** CUDA modifications
  (preserved from OBS-DOP.2); debug AOV (out of
  arc scope); fixture (out of arc scope);
  arc capstone (deferred).

### OBS-DOP.4 — Per-slice audit slots (docs only)

- **Scope:** per-slice audit gates for OBS-DOP.2 +
  OBS-DOP.3, mirroring the OBS-PERCEPT.4 +
  OBS-PERCEPT.6 per-slice audit doc shapes (each
  ~900 lines; eleven-row evidence table; runtime
  status row; verdict variant). Inserted in-band as
  operator cadence requires; the audit-slot
  insertion ladder is the standing discipline (the
  numbering on this section is illustrative;
  actual slot numbers shift with the insertion
  pattern).
- **Acceptance:** documentation-only verdicts.

### OBS-DOP.5 — Arc capstone audit (docs only)

- **Scope:** arc-level capstone verdict mirroring
  the OBS-PERCEPT.10 + FIELD-BEAUTY.8 capstone
  audit shapes. Verifies the OBS-DOP.\* arc's
  structural completeness on the audit-host side;
  documents the SDK-host runtime-deferred
  scenarios per §5.5; recommends the next safe
  stage (the combined arc-wide SDK-host runtime
  pass per OBS-PERCEPT.10 §4.2 (a) extended to
  also close OBS-DOP.\* runtime-deferred verdicts).
- **Acceptance:** documentation-only verdict.

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the operator's
cadence requires. The combined FIELD-\* + OBS-PERCEPT
+ OBS-DOP CLI bridge slice (per OBS-PERCEPT.10 §4.2
(a) extended) becomes the canonical converging-
leverage closure for the entire field + observer arc
family's runtime-deferred verdict tail once OBS-DOP.\*
lands.

---

## 10. Explicit non-goals

The OBS-DOP.\* arc deliberately excludes:

### 10.1 No spectral Doppler

The `applyDopplerColor` artistic-approximation form
(`tanh(0.5 * log(D)) * strength` monotone remap) is
preserved verbatim. A physically correct spectral
Doppler shift requires a spectral pipeline
(per-band frequency shifting + re-projection onto
the renderer's color primaries); that pipeline lands
at M16 / M17 per the existing roadmap. OBS-DOP.\*
does NOT extend `applyDopplerColor`.

### 10.2 No frame-dragging Doppler

The `CurvedChartGeodesicPlaceholder` perception mode
remains reserved per master rule #3 — selecting it
today closes the outer gate; the helper returns
identity. Future arcs (e.g. OBS-DOP-GR.* or
OBS-PERCEPT-CURVED.*) may extend.

### 10.3 No accelerating observers

The observer's 3-velocity is constant within a
launch. The `ObserverFrame::position4` /
`velocity4` POD fields (reserved at OBSERVER.2)
remain inert. The `proper_time` /
`coordinate_time` fields likewise.

### 10.4 No retarded-time approximation

The renderer's per-pixel hit-position is consumed
at the **present** observer time (the per-launch
`ObserverFrame` snapshot). No retarded-time
projection; future arcs (e.g.
OBS-DOP-RETARDED.*) may lift this.

### 10.5 No per-bounce Doppler / searchlight

The arc preserves the existing single-application
pattern at each kernel arm (the existing primary-
ray post-shading site on the rasterizers; the
existing post-spp-averaging site on the path
tracers). Per-bounce Doppler / searchlight
reapplication is OUT OF SCOPE.

### 10.6 No quantum observer effects

The arc does NOT introduce quantum-state Doppler
visualisations (e.g. spectral broadening from
Maxwell-Jüttner distributions, observer-dependent
wavefunction collapse). The
`FieldType::ProbabilityAmplitudePlaceholder` slot
is reserved per the FIELD-I.1 plan §2.4; the
OBS-DOP.\* arc does NOT extend it.

### 10.7 No new CLI flag family

The arc reuses the OBSERVER.4 `--observer-*` CLI
surface verbatim. No new `--observer-perception-doppler`
/ `--observer-perception-searchlight` flag family;
the existing `--observer-perception-mode
relativistic` flag is the load-bearing gate.

### 10.8 No new ObserverFrame POD fields

The OBSERVER.2-shipped `ObserverFrame` POD fields
are read as-is; no new fields added; no
field-offset changes.

### 10.9 No C4D / server / UI / node-editor

Standard discipline carried forward from every
prior arc.

### 10.10 No legacy `observer.velocity` removal

The legacy field is preserved verbatim on the
host-side payload structures; the OBS-DOP.\*
fallback dispatch branch reads it directly when
the outer gate closes.

### 10.11 No new diagnostic AOV

The OBSERVER.13 `ObserverBeta` AOV continues to
visualise the observer-frame beta payload verbatim;
no new Doppler-magnitude / searchlight-factor
diagnostic AOV. A follow-up `OBS-DOP-AOV.*` arc
may lift this if authorised.

### 10.12 No fixture extension

The OBS-F.2 + OBS-PERCEPT.9 fixtures are sufficient
for the OBS-DOP.\* arc's runtime validation. No
new `.rrscene` fixture this arc.

---

## 11. Cross-references

### 11.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #1
  ("Build incrementally") + #3 ("no fake stubs") +
  #11 ("explicit, testable interfaces") + #12
  ("do not overbuild a later system before the
  current layer works") + #16 ("default-off /
  reasoning-traceable defaults") apply.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §7.2
  — the architecture-doc anchor for the
  observer-frame Lorentz boost of the tetrad →
  aberration / Doppler / searchlight unified
  abstraction concept. The OBS-DOP.\* arc
  consolidates the Doppler / searchlight
  application's input source onto the unified
  abstraction the architecture-doc §7.2 framing
  authorises.

### 11.2 OBS-PERCEPT.\* arc references

- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1) — the canonical
  observer-space perception plan §1.1 enumerates
  the three SR pipeline sites (aberration + Doppler
  + searchlight); OBS-PERCEPT.\* arc consolidated
  the first; OBS-DOP.\* arc consolidates the
  remaining two.
- `docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`
  (OBS-PERCEPT.2) — the precedent task brief
  shape this document mirrors verbatim (8-section
  operator-facing layout adapted to the operator's
  5 required topics + 5 supporting sections).
- `docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md`
  (OBS-PERCEPT.4) — the precedent CUDA-side
  per-slice audit shape OBS-DOP.4 (CUDA audit
  slot) will mirror.
- `docs/OBSERVER_PRIMARY_RAY_OPTIX_AUDIT.md`
  (OBS-PERCEPT.6) — the precedent OptiX-side
  per-slice audit shape OBS-DOP.4 (OptiX audit
  slot) will mirror; the §3.7 five-axis
  cross-backend symmetry framework is inherited
  verbatim.
- `docs/OBSERVER_PERCEPTION_ARC_AUDIT.md`
  (OBS-PERCEPT.10) — the arc-level capstone
  audit OBS-DOP.5 will mirror. The OBS-PERCEPT.10
  check #7 honestly deferred the Doppler /
  searchlight consolidation to a "future
  OBS-PERCEPT.\* sub-slice"; this OBS-DOP.\* arc
  is that sub-slice family.
- `docs/OBSERVER_PRIMARY_RAY_PERCEPTION_FIXTURE.md`
  (OBS-PERCEPT.9 companion) — the OBS-PERCEPT.9
  fixture's runtime-deferred SDK-host validation
  surface; OBS-DOP.\* arc consumes this fixture
  verbatim for runtime validation per §5.5.4 +
  §5.5.5.

### 11.3 OBSERVER.\* + OBS-P.\* + OBS-F.\* arc references

- `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
  (OBSERVER.1) — the canonical OBSERVER.\* arc
  plan; the OBS-DOP.\* implementation consumes the
  OBSERVER.6 adapter + OBSERVER.8 payload +
  OBSERVER.10 OptiX payload verbatim.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the `ObserverFrame{}` default-
  constructed POD's contract OBS-DOP.\* relies on
  for the §6.4 Layer 3 anchor.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the adapter's beta-resolution
  priority + zero-beta + clamp-safety contracts
  underpinning the §6.4 Layer 3 anchor.
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — the `CudaSceneView::observer_frame`
  carry-only field the CUDA OBS-DOP.\* kernel
  arms read.
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11) — the `OptixLaunchParams::observer_frame`
  carry-only field the OptiX OBS-DOP.\* programs
  read.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — the `ObserverBeta` AOV's
  read-only contract; OBS-DOP.\* preserves
  verbatim per §4.8.
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15) — the capstone whose §10 risk #1
  the OBS-PERCEPT.\* arc closed for the primary-
  ray aberration site; the OBS-DOP.\* arc closes
  the same risk for the post-shading Doppler /
  searchlight sites.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3) — the precedent kernel-migration
  audit; the OBS-P.2 guarded-ternary at the
  Doppler / searchlight sites is the load-
  bearing baseline the OBS-DOP.\* arc consolidates.
  Check #5 noted the CUDA path tracer has no
  pre-existing Doppler / searchlight call sites
  (5 sites instead of 6); OBS-DOP.\* preserves
  this scope correction per §4.2.
- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md` (OBS-F.3)
  — the precedent fixture audit; the OBS-F.2
  fixture + OBS-PERCEPT.9 fixture are the
  canonical runtime-deferred SDK-host validation
  surfaces per §5.5.

### 11.4 Parallel-arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1) — the parallel field-interpretation
  arc; the OBS-DOP.\* + FIELD-I.\* + FIELD-BEAUTY.\*
  + OBS-PERCEPT.\* arcs coexist as orthogonal
  perceptual layers above the manifold.
- `docs/FIELD_SCALAR_BEAUTY_MAPPING_AUDIT.md`
  (FIELD-BEAUTY.8) — the precedent capstone audit
  shape; OBS-DOP.5 capstone will mirror.

### 11.5 Source surface to be exercised (post-OBS-DOP.\* arc)

The OBS-DOP.\* arc will exercise (read-only) or
modify:

- **`src/relativity/RelativityMath.h`** — the
  single-source-of-truth math leaves
  (`dopplerFactor`, `applyDopplerColor`,
  `searchlightFactor`, `precompute_relativity`).
  Read-only across the arc; the math is preserved
  verbatim.
- **`src/manifold/ObserverFrame.h`** — modified by
  OBS-DOP.2 (add the unified Doppler / searchlight
  helpers; mirrors the OBS-PERCEPT.3 helper
  addition's location).
- **`src/cuda/CudaTestKernel.cu`** — modified by
  OBS-DOP.2 (the two post-shading sites adopt the
  §6.5 dispatch pattern).
- **`src/cuda/CudaPathTracer.cu`** — unchanged
  (no pre-existing Doppler / searchlight site;
  matches OBS-P.3 scope correction).
- **`src/optix/OptixPrograms.cu`** — modified by
  OBS-DOP.3 (the four Doppler / searchlight sites
  adopt the §6.5 dispatch pattern; the shared
  `apply_doppler_and_searchlight_with_D(...)`
  helper at lines 99-124 may be retained or
  removed at OBS-DOP.3's discretion).
- **`tests/manifold_identity_tests.cpp`** —
  modified by OBS-DOP.2 (~8 RR_CHECK assertions on
  the unified helpers).

### 11.6 Non-touched surfaces

- The Manifold Core (`src/manifold/`) other than
  `ObserverFrame.h`.
- The `src/field/` tree (orthogonal arc family).
- The `src/io/SceneLoader.cpp` parser (no new
  scene-block; the existing `relativity` block
  + the OBSERVER.4-shipped `--observer-*` CLI
  surface suffice).
- The `rr::core::Config` (no new Config field).
- The `rr::relativity::Observer` /
  `rr::relativity::RelativityParams` types (read-
  only across the arc).
- All DCC / server / UI / node-editor surfaces.

---

## 12. Constraints carried forward

From `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
— these apply to every OBS-DOP.\* slice:

- **Build incrementally.** Each sub-slice ships
  with the project compilable; no half-finished
  intermediate state.
- **No fake stubs.** The unified helpers' three-
  gate logic is fully wired with real arithmetic +
  real branches; the
  `CurvedChartGeodesicPlaceholder` perception
  mode's no-transform fallback is honest (master
  rule #3: the helper short-circuits to identity;
  future arcs lift with documented contracts).
- **No CPU per-pixel or per-ray work.** The
  Doppler / searchlight transform is exclusively a
  GPU kernel-arm modification; the host side only
  threads the `ObserverFrame` payload.
- **Core modules never depend on Cinema 4D, UI,
  node editor, or any DCC.** The OBS-DOP.\* arc
  preserves this (no dependency on `src/server/`,
  `bridges/`, `tools/`).
- **Update BUILD_PLAN.md after every
  implementation.** Each per-slice impl ships with
  a per-slice BUILD_PLAN.md entry.
- **Explicit, testable interfaces.** The unified
  helpers' three-gate logic is empirically pinned
  by RR_CHECK assertions on `manifold_identity_tests.cpp`
  (per §5.3); mirrors the FIELD-I.3 / FIELD-I.5 /
  OBS-PERCEPT.3 test-pinning precedent.

---

## 13. Verdict

This is a **task-definition slice**; it produces no
verdict. The OBS-DOP.5 arc capstone audit
(documentation-only; the last slice in the ladder)
will produce the arc-level
`PASS / PASS_WITH_RUNTIME_DEFERRED / REPAIR /
BLOCKED` verdict, mirroring the OBS-PERCEPT.10 +
FIELD-BEAUTY.8 capstone shapes.

Each per-slice impl (OBS-DOP.2 + OBS-DOP.3) is
followed by an operator-cadence per-slice audit per
the standing in-band audit-slot insertion
discipline (OBS-PERCEPT.4 + OBS-PERCEPT.6 + FIELD-I.10
/ .12 / .14 + FIELD-BEAUTY.4 / .6 precedent applied
at the OBS-DOP arc scope).

The OBS-DOP.1 task-brief slice itself is
documentation-only; it ships
`docs/OBSERVER_DOPPLER_SEARCHLIGHT_TASK.md` (this
doc) + the BUILD_PLAN.md entry. No source code
modification. No fixture authoring. No CLI flag.
No kernel-arm implementation. The implementation
lands in the OBS-DOP.2 → OBS-DOP.5 sub-slice
ladder defined in §9.

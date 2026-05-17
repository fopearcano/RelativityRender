# Primary-Ray Perception Transform — Task Definition (OBS-PERCEPT.2)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this task definition; the implementation
        lands in a subsequent slice (OBS-PERCEPT.3) that
        consumes this doc as its canonical brief.

This document defines the work for **OBS-PERCEPT.3 —
CUDA implementation of the primary-ray perception
transform**. It is the operator-facing brief the
implementation slice will read to decide the exact
surface, the acceptance gates, and the non-goals.

Per the OBS-PERCEPT.1 plan
(`docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`) §7, the
OBS-PERCEPT.3 slice is the first **active** OBS-PERCEPT.*
implementation. It lands the directional aberration
component of the unified perception transform — the
Lorentz boost of the primary camera ray's direction
into the observer's tetrad-local frame. The Doppler
basis shift + searchlight intensity modulation
components are scoped to follow-up sub-slices within
OBS-PERCEPT.3 (or a separate OBS-PERCEPT.3a sub-slice
if the operator authorises a finer-grain split); per
this task brief, OBS-PERCEPT.3 ships directional
aberration as its load-bearing primary-ray transform.

Prerequisite slices already green:

- **OBS-PERCEPT.1** — Arc plan (`8db1f9c`).
- **OBSERVER.1 – OBSERVER.15** — Observer Frame
  foundation arc capstone (`d92a82f` / OBSERVER.15
  audit at `docs/OBSERVER_FRAME_ARC_AUDIT.md`).
- **OBS-P.1 – OBS-P.3** — Kernel-side perception
  migration with guarded ternary
  (`docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`).
- **OBS-F.1 – OBS-F.3** — Observer-frame fixture +
  audit (`docs/OBSERVER_FRAME_FIXTURE.md`).

Adjacent precedents this task brief mirrors:

- **`docs/MANIFOLD_DEBUG_AOV_TASK.md`** (MANI-I.7) +
  **`docs/OBSERVER_DEBUG_AOV_TASK.md`** (OBSERVER.12)
  + **`docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`**
  (FIELD-I.6) — the canonical task-brief shape this
  document follows (8-section operator-facing
  layout).
- **`docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_TASK.md`**
  (OBS-P.1) — the precedent kernel-side migration
  task brief; OBS-PERCEPT.3's structural mirror.

---

## 1. Exact goal

**Apply observer-frame constant-velocity directional
aberration to the primary camera ray's direction, in
both CUDA's primary-hit `k_render_scene` kernel and
the OBS-P.2 + OBS-P.2-extended pathtracer entry's
camera-ray-generation site, before intersection. The
aberration math is the existing
`rr::relativity::aberrateDirection(...)` math leaf
verbatim; the operative change is the **input source
discipline** and the **activation gate discipline**.

The unified transform:

1. **Reads the observer beta source exclusively
   from `observer_frame.beta`** when the perception
   mode is `ConstantVelocityMinkowski`. The post-OBS-P.2
   guarded ternary
   `(perception_mode == ConstantVelocityMinkowski)
   ? observer_frame.beta : observer.velocity`
   continues to operate on every existing call site,
   but OBS-PERCEPT.3 consolidates the primary-ray
   site's input source into a dedicated
   `apply_primary_ray_aberration(...)` helper (or
   equivalent abstraction) that takes the
   `ObserverFrame` POD directly and reads its `beta`
   + `perception_mode` fields internally.

2. **Activates only when `perception_mode ==
   ConstantVelocityMinkowski AND |beta| > 0`** —
   the inner gate adds a `|beta| > 0` short-circuit
   that the existing aberration call sites do NOT
   currently apply explicitly. This guarantees zero-
   beta = no-op even when `perception_mode ==
   ConstantVelocityMinkowski`. The math leaf's
   identity at `beta = 0` is already a no-op
   structurally (verified by
   `tests/relativity_tests.cpp`); the explicit
   `|beta| > 0` gate makes this a documented
   contract rather than a math-leaf-incidental
   behaviour.

3. **Honours the OBSERVER.6 `clampBeta` safety
   discipline** by relying on the
   `PrecomputedRelativity` snapshot (which the
   OBSERVER.6 adapter produces with `clampBeta` at
   `max_beta = 0.999999f`). The kernel arm does
   NOT re-clamp; the snapshot is the load-bearing
   safety contract.

The slice is **CUDA-only**. The OptiX-side mirror
lands at OBS-PERCEPT.4 per the OBS-PERCEPT.1 plan
§7 ladder; per-bounce / secondary-ray perception is
the deferred Option B per the plan §5.2 (out of
scope here).

---

## 2. Activation

Three load-bearing activation conditions, all
required:

### 2.1 `observer_frame.perception_mode == ConstantVelocityMinkowski`

The OBS-PERCEPT.* arc's outer gate. When the
operator passes `--observer-perception-mode default`
(or no `--observer-perception-mode` flag — the
default `PerceptionMode::Identity`), the kernel
arm short-circuits and the existing OBS-P.2
guarded ternary's `else` branch fires
(`observer.velocity` is read, identical to the
pre-OBSERVER.* legacy path). When the operator
passes `--observer-perception-mode relativistic`,
the kernel arm activates the perception transform.

### 2.2 `|beta| > 0`

The OBS-PERCEPT.* arc's inner gate. The kernel
arm reads
`rel.beta_magnitude_squared` (from the
`PrecomputedRelativity` snapshot, computed by the
OBSERVER.6 adapter or the kernel-side
`precompute_relativity(...)` helper) and checks
`rel.beta_magnitude_squared > 0.0f` before
invoking `aberrateDirection(rel, ray.direction)`.
A non-zero check on the squared magnitude avoids
the `sqrt` cost and is exact at `beta = 0`.

The OBS-P.2 ternary today does NOT add this gate
— it always invokes `aberrateDirection(...)` when
`params.enable_aberration` is true. The math leaf
returns the input direction unchanged at `beta =
0`, so the result is the same; but the OBS-PERCEPT.3
explicit gate makes the contract documented rather
than incidental.

### 2.3 `beta must be safely clamped`

The OBSERVER.6 adapter clamps `|beta|` to
`max_beta = 0.999999f` via `clampBeta(...)`
during `build_observer_frame_from_camera(...)`.
The `ObserverFrame::beta` field on the launch
payload carries the already-clamped value. The
kernel arm DOES NOT re-clamp; it consumes the
clamped `beta` from the payload directly. This
preserves the OBSERVER.6 + OBS-P.2 safety
contract verbatim.

In `precompute_relativity(...)` (the per-launch
snapshot computation), the input `beta_vec` is
already clamped; the snapshot's `gamma_factor` is
finite by construction. The kernel arm's
`aberrateDirection(rel, dir)` call is structurally
NaN/Inf-safe (verified by
`tests/relativity_tests.cpp`'s ~841 RR_CHECK
assertions, including the `clampBeta`-shell
edge-case battery from OBSERVER.6).

---

## 3. Default invariants

The OBS-PERCEPT.3 implementation MUST satisfy three
load-bearing default invariants:

### 3.1 Default observer remains no-op

Every existing CLI action — `--render-pathtrace`,
`--render-optix-pathtrace`, `--render-scene`,
`--render-mesh-scene`, `--render-material-scene`,
`--render-direct-lighting`, `--render-aovs`,
`--render-optix-aovs`, `--render-relativistic`,
`--render-aovs --denoise`, `--render-aovs
--manifold-debug`, `--render-aovs --observer-debug`,
and every diagnostic render — produces pixel-bit-
identical output to the pre-OBS-PERCEPT.3 baseline
when invoked WITHOUT `--observer-perception-mode
relativistic`.

The default `ObserverFrame{}` carries
`perception_mode = Identity`; the kernel arm's
outer gate (`perception_mode ==
ConstantVelocityMinkowski`) closes; the OBS-P.2
guarded ternary's else-branch fires (reading
`observer.velocity`); behaviour is identical to
the post-OBS-P.2 baseline.

### 3.2 `beta = 0` remains no-op (under `ConstantVelocityMinkowski`)

When the operator passes
`--observer-perception-mode relativistic` BUT
`--observer-beta 0` (or no `--observer-beta` flag —
default `0.0`), the inner gate (`|beta| > 0`)
closes; no aberration is applied. The framebuffer
output is byte-identical to the
`--observer-perception-mode default` invocation
on the same scene.

This is the new explicit invariant the OBS-PERCEPT.3
slice introduces. The pre-OBS-PERCEPT.3 behaviour
(post-OBS-P.2) is the same in practice — the
existing aberration math at `beta = 0` is identity
— but the OBS-PERCEPT.3 explicit gate makes the
invariant a documented contract verifiable by a
single kernel-arm conditional rather than a
math-leaf-incidental behaviour.

### 3.3 Existing default scenes remain byte-identical

Every `.rrscene` fixture in `scenes/`
(pre-OBS-PERCEPT.3) loads + renders to the same
PPM bytes it produced post-OBS-P.2. The OBS-PERCEPT.3
slice MUST verify this empirically on a CUDA SDK
host before claiming acceptance. The audit-host
build's clean compile + ctest 13/13 PASS
demonstrates structural correctness; the SDK-host
empirical PPM-cmp pass is the runtime verification.

Special attention to:
- `test_observer_frame.rrscene` (OBS-F.2 fixture)
  — authors `relativity.betaVelocity = 0.5,
  velocityDirection = (0,0,-1)`. With
  `--observer-perception-mode default` the
  observer is at rest; with
  `--observer-perception-mode relativistic` the
  observer reads beta = (0, 0, -0.5) from the
  adapter. Pre-OBS-PERCEPT.3 + post-OBS-PERCEPT.3
  behaviours must match on the same CLI invocation.

---

## 4. CUDA-first scope

### 4.1 Primary rays only

The OBS-PERCEPT.3 slice modifies CUDA primary-ray
generation sites ONLY:

- **`src/cuda/CudaTestKernel.cu::k_render_scene`**
  primary-ray site at line ~235 (the call site
  immediately after `generate_camera_ray(...)`).
  This is the canonical primary-ray site for the
  CUDA `--render-*` actions (other than the
  pathtracer).
- **`src/cuda/CudaTestKernel.cu::k_sphere_relativistic`**
  primary-ray site at line ~364. This is the
  `--render-relativistic` action's kernel. The
  OBS-PERCEPT.3 slice applies the same gate
  discipline here.

The OBS-PERCEPT.3 slice does NOT extend to:

- Secondary bounce rays in `CudaPathTracer.cu`
  (the path-tracer's bounce recursion).
- The OptiX-side primary-ray sites
  (`OptixPrograms.cu::__raygen__pinhole` and
  `__raygen__pathtrace`).

The path-tracer secondary rays are explicitly
deferred per the OBS-PERCEPT.1 plan §5.2 Option A
(primary-ray-only) RECOMMENDED policy. The OptiX
sites mirror at OBS-PERCEPT.4.

### 4.2 No secondary bounce transform yet

The OBS-PERCEPT.3 slice does NOT extend the
perception transform to secondary bounce rays
(the path-tracer's NEE / BSDF / MIS pipeline).
Per the OBS-PERCEPT.1 plan §5.2 Option A: the
perception transform applies at the primary ray
ONLY; secondary rays propagate in their natural
Minkowski frame. Future arc (OBS-PERCEPT-BOUNCE.*
or similar) lifts to Option B (per-bounce
perception) when authorised.

### 4.3 No new Doppler / searchlight math yet

The OBS-PERCEPT.3 slice does NOT modify the
Doppler color shift or the searchlight intensity
scaling. The existing OBS-P.2 guarded ternary
continues to feed `dopplerFactor(rel,
ray.direction)` + `applyDopplerColor(...)` +
`searchlightFactor(D)` + linear scaling per the
post-OBS-P.2 pipeline.

Future sub-slices within OBS-PERCEPT.3 (or
OBS-PERCEPT.3a / .3b) may lift the Doppler +
searchlight call sites into the unified perception
transform. Per this task brief, OBS-PERCEPT.3
ships the **directional aberration** component
only.

---

## 5. Future OptiX mirror

The OBS-PERCEPT.4 slice (per OBS-PERCEPT.1 plan §7)
will mirror OBS-PERCEPT.3 on the OptiX path. The
following invariants MUST be preserved:

### 5.1 Same math leaf

Both backends call the same RR_HD inline
`rr::relativity::aberrateDirection(rel,
direction)` helper from
`src/relativity/RelativityMath.h`. No per-backend
math variation; cross-backend bit-identity by
construction.

### 5.2 Same activation rules

Both backends apply the same three-condition
activation gate (§2 above):

1. `observer_frame.perception_mode ==
   ConstantVelocityMinkowski`
2. `|beta| > 0`
3. Pre-clamped beta from the OBSERVER.6 adapter

The OptiX-side reads `optixLaunchParams.observer_frame.*`
instead of `view.observer_frame.*` (the CUDA-side
field); the POD content is identical (same
`ObserverFrame` type; same adapter output).

### 5.3 Same default-state byte identity

Both backends preserve byte-identical default-
state PPM output (the OptiX `--render-optix-aovs`
+ `--render-optix-pathtrace` actions must produce
byte-identical PPM bytes on
`--observer-perception-mode default` vs the
pre-OBS-PERCEPT.4 baseline).

### 5.4 Same kernel-arm placement

Both backends apply the transform at the same
position in the per-pixel pipeline: AFTER
`generate_camera_ray(...)` / equivalent OptiX
ray-from-camera computation; BEFORE intersection
(the chart-aware seam) / OptiX `optixTrace(...)`.

The OptiX `__raygen__pinhole` + `__raygen__pathtrace`
programs already invoke `aberrateDirection(...)`
at the equivalent site (verified at OBS-P.2 +
FIELD-I.11 audits); the OBS-PERCEPT.4 slice
consolidates this site into the unified
abstraction.

---

## 6. Files likely involved

The implementation slice is expected to touch the
following files. Numbers in parentheses are rough
net-line estimates from comparable past slices
(OBS-P.2 + FIELD-BEAUTY.3).

| Layer | File | Why |
|-------|------|-----|
| Relativity math | `src/relativity/RelativityMath.h` (+30 OPTIONAL) | Optional: add a new `apply_observer_primary_ray_aberration(observer_frame, ray_dir) → Vec3` RR_HD inline helper that internally checks `perception_mode == ConstantVelocityMinkowski` AND `|beta| > 0` and either returns `aberrateDirection(rel, dir)` or `dir` unchanged. Pure refactor; the existing `aberrateDirection(rel, ...)` helpers stay verbatim. Alternative: skip the new helper and inline the gate logic at each kernel call site (~40 lines per site). The new-helper approach is the cleaner / more testable / more aligned with the OBS-PERCEPT.1 plan §5.4 "shared math leaves on both backends" framing. |
| CUDA kernel | `src/cuda/CudaTestKernel.cu` (+10) | Consolidate the two primary-ray sites (line ~239 `k_render_scene` + line ~364 `k_sphere_relativistic`) onto the new unified helper. Each site becomes `ray.direction = rr::relativity::apply_observer_primary_ray_aberration(observer_frame, ray.direction);` (or equivalent). The existing OBS-P.2 ternary + `perception_active` check are removed at the primary-ray sites (and preserved at the Doppler / searchlight sites which are out of scope here). |
| CUDA pathtracer | `src/cuda/CudaPathTracer.cu` (+10) | Same consolidation at the primary-ray site in `k_pathtrace_sample` (the entry's `generate_camera_ray(...)` follow-up). Secondary rays are NOT modified. |
| Tests | `tests/relativity_tests.cpp` (+15) | Add ~4 RR_CHECK assertions covering the new helper's behaviour: (a) on `Identity` mode returns the input direction unchanged; (b) on `ConstantVelocityMinkowski` mode + `beta = 0` returns the input direction unchanged; (c) on `ConstantVelocityMinkowski` + non-zero beta returns the post-aberration direction; (d) the post-aberration direction is normalised + `dot(post, post) ≈ 1.0` within `kEps`. |
| Docs | `docs/BUILD_PLAN.md` | OBS-PERCEPT.3 entry. |
| Docs | `docs/OBS_PERCEPT_3_AUDIT.md` (slice audit; doc-only, lands at OBS-PERCEPT.3 audit slot, not at OBS-PERCEPT.3 impl) | The per-slice audit gate the operator may insert in-band per the OBS-PERCEPT.1 plan §7 ladder discipline. |
| CMake | none expected | The new helper lives in the already-included `relativity/RelativityMath.h`. No new ctest target; the new RR_CHECK assertions extend the existing `relativity_tests` binary. |

The exact file inventory + line-count budget is
the OBS-PERCEPT.3 implementer's choice; this table
is the operator-facing prediction.

---

## 7. What must not be touched

Per master rule #3 and the operator's OBS-PERCEPT.2
brief, the OBS-PERCEPT.3 implementation slice MUST
NOT:

### 7.1 No new manifold math

The OBS-PERCEPT.3 slice does NOT touch any
`src/manifold/` source file other than reading
the existing `ObserverFrame` POD via the launch
payload. No new `world_to_chart(...)` /
`chart_to_world(...)` helpers; no new
`CoordinateChart` modifications; no extension of
`SchwarzschildLikeWarp` /
`PenroseLikeCompactification` math leaves. The
perception transform operates ORTHOGONAL to the
manifold layer per the OBS-PERCEPT.1 plan §4.

### 7.2 No field interpretation changes

The OBS-PERCEPT.3 slice does NOT touch any
`src/field/` source file. The FIELD-I.* +
FIELD-BEAUTY.* arc family's surfaces
(`ScalarField.h`, `FieldMapping.h`,
`FieldInterpreter.h`, `FieldType.h`) are preserved
verbatim. The OBS-PERCEPT.* arc is a parallel arc
family per the OBS-PERCEPT.1 plan §1.3.

### 7.3 No C4D / server / UI / node-editor touch

Standard discipline carried forward from every
prior arc; the OBS-PERCEPT.3 slice does not touch
any DCC / server / UI / node-editor surface.

### 7.4 No full GR tetrad solver

The OBS-PERCEPT.3 slice does NOT implement parallel
transport of the observer's tetrad along a
curved-space geodesic. The
`PerceptionMode::CurvedChartGeodesicPlaceholder`
slot remains reserved per master rule #3. Future
arcs (OBS-PERCEPT-GR.* or similar) lift this when
authorised.

### 7.5 No legacy `observer.velocity` removal

The legacy `rr::relativity::Observer::velocity`
field is preserved verbatim. The host-side
`Scene::observer` field continues to carry the
legacy 3-velocity. The OBS-P.2 guarded-ternary
fallback path at the Doppler / searchlight sites
(deferred per §4.3) continues to read
`observer.velocity` until those sites also
migrate to the unified abstraction.

### 7.6 No new CLI flag

The existing `--observer-perception-mode
relativistic` (from OBSERVER.4) is the load-
bearing gate. No new `--observer-perception-aberration`
or similar flag this slice; the gate is the
perception-mode flag.

### 7.7 No new ObserverFrame POD field

The OBSERVER.2-shipped `ObserverFrame` POD's nine
fields (`perception_mode`, `beta`, `right`, `up`,
`forward`, `position4`, `velocity4`,
`proper_time`, `coordinate_time`) are read as-is.
No new fields added; no field-offset changes; no
SCHW.7 / OBSERVER.13 trailing-param ABI extension
required.

### 7.8 No debug AOV

The OBS-PERCEPT.5 slice (per the OBS-PERCEPT.1
plan §7) lands the perception-transform debug AOV.
This OBS-PERCEPT.3 slice does NOT add a new AOV
enumerator or a new kernel write arm. The existing
`ObserverBeta` AOV (OBSERVER.13) continues to
visualise the observer-frame beta payload
verbatim; the new debug AOV (if any) lands at
OBS-PERCEPT.5.

### 7.9 No fixture authoring

The OBS-PERCEPT.6 slice lands the fixture. This
OBS-PERCEPT.3 slice consumes the existing OBS-F.2
fixture (`scenes/test_observer_frame.rrscene`) for
its acceptance verification but does NOT modify
the fixture or author new ones.

---

## 8. PASS criteria

The OBS-PERCEPT.3 implementation slice's acceptance
gate is satisfied when ALL of the following hold:

### 8.1 Structural

- [ ] A unified `apply_observer_primary_ray_aberration(...)`
      helper (or equivalent abstraction) exists in
      `src/relativity/RelativityMath.h` (RR_HD
      inline). The helper takes the
      `ObserverFrame` payload + the ray direction
      and applies the three-gate logic
      (§2 activation + §3 invariants) internally.
- [ ] The CUDA `k_render_scene` primary-ray site
      consumes the unified helper (no inline
      `perception_active` ternary at the
      primary-ray site).
- [ ] The CUDA `k_sphere_relativistic` primary-ray
      site consumes the unified helper.
- [ ] The CUDA `k_pathtrace_sample` primary-ray
      site consumes the unified helper.
- [ ] Secondary bounce rays in `k_pathtrace_sample`
      are NOT modified (per §4.2).
- [ ] The OptiX-side primary-ray sites
      (`__raygen__pinhole` + `__raygen__pathtrace`)
      are NOT touched (per §4.1 + §5).
- [ ] No `src/manifold/` / `src/field/` /
      `src/core/Config.h` /
      `src/core/CommandLine.cpp` / `src/io/` /
      `src/scene/` / `src/main.cpp` modification.
- [ ] No new CLI flag.
- [ ] No new ObserverFrame POD field.
- [ ] No new test binary (the existing
      `relativity_tests` target gains ~4 new
      RR_CHECK assertions).

### 8.2 Behavioural

- [ ] `--render-pathtrace` /
      `--render-optix-pathtrace` /
      `--render-scene` / `--render-mesh-scene` /
      `--render-material-scene` /
      `--render-direct-lighting` / `--render-aovs`
      / `--render-optix-aovs` /
      `--render-relativistic` / every diagnostic
      render WITHOUT `--observer-perception-mode
      relativistic` produces pixel-bit-identical
      output to the pre-OBS-PERCEPT.3 baseline.
      (The `--observer-perception-mode default`
      → `PerceptionMode::Identity` → outer gate
      closed → OBS-P.2 else-branch fires →
      `observer.velocity` path → existing
      `aberrateDirection(rel, dir)` math is
      unchanged.)
- [ ] On `--observer-perception-mode relativistic
      --observer-beta 0` (explicit zero beta), the
      output is byte-identical to the
      `--observer-perception-mode default`
      invocation on the same scene. (New inner
      gate `|beta| > 0` closes.)
- [ ] On `--observer-perception-mode relativistic
      --observer-beta 0.5 --observer-direction
      1,0,0` (non-zero beta), the primary ray is
      aberrated per the
      `aberrateDirection(rel, dir)` math leaf
      output. The result is byte-identical to the
      post-OBS-P.2 + post-FIELD-BEAUTY.3 baseline
      on the same CLI invocation (the unified
      helper's mathematical content is
      `aberrateDirection(rel, dir)` verbatim
      when both gates open).
- [ ] The OBS-F.2 fixture
      (`scenes/test_observer_frame.rrscene`) +
      `--observer-perception-mode relativistic
      --render-aovs` produces the same beauty
      PPM the post-OBS-P.2 + post-FIELD-BEAUTY.3
      run produces.
- [ ] The cross-backend (CUDA vs OptiX) primary-
      ray output is structurally guaranteed
      byte-identical because both backends still
      invoke `aberrateDirection(rel, dir)` via
      the same RR_HD inline math leaf; the
      OBS-PERCEPT.3 unified helper composition
      preserves this guarantee.

### 8.3 Test surface

- [ ] `ctest` reports `13/13 passed` on the
      audit-host build.
- [ ] `relativity_tests` grows by ~4 new
      RR_CHECK assertions covering the unified
      helper's behaviour:
        - `apply_observer_primary_ray_aberration`
          on `Identity` mode returns the input
          direction unchanged.
        - On `ConstantVelocityMinkowski` mode +
          `beta = 0` returns the input direction
          unchanged.
        - On `ConstantVelocityMinkowski` + non-
          zero beta returns the post-aberration
          direction.
        - Post-aberration direction is
          normalised (`dot(d, d) ≈ 1.0` within
          `1.0e-5f`).
- [ ] `cli_tests` count unchanged (no new CLI
      flag).
- [ ] `renderer_tests` count unchanged.
- [ ] `field_tests` count unchanged.
- [ ] `manifold_identity_tests` count unchanged.

### 8.4 Documentation

- [ ] `docs/BUILD_PLAN.md` OBS-PERCEPT.3 entry
      added (mirrors the existing OBS-PERCEPT.*
      + OBSERVER.* + FIELD-I.* + FIELD-BEAUTY.*
      per-slice entries' "What ships / What
      does NOT ship / Acceptance / Module
      status changes" rubric).
- [ ] OPTIONAL:
      `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
      §7 OBS-PERCEPT.3 entry rewritten with
      the landed-surface description (mirrors
      what MANI-I.5 did to the integration
      plan §6).

### 8.5 Runtime CUDA / OptiX deferral

- The audit-host build (no CUDA SDK, no OptiX
  SDK) cannot directly verify the kernel
  arm's empirical PPM output. The runtime
  checks above are STRUCTURALLY satisfied by
  the unified-helper composition + the
  preserved math-leaf identity; the empirical
  SDK-host PPM-cmp verification is DEFERRED
  to a future OBS-PERCEPT.7 arc-wide
  SDK-host runtime pass (per the OBS-PERCEPT.1
  plan §7) or to the next combined arc-wide
  CLI-bridge SDK-host pass.

- The deferred runtime scenarios are:
    - **§8.5.1** Default-state byte identity.
      Run `--render-aovs <every fixture>`
      pre + post; `cmp` PPMs byte-by-byte.
    - **§8.5.2** Zero-beta byte identity.
      Run `--render-aovs --observer-perception-mode
      relativistic --observer-beta 0 <fixture>`;
      `cmp` against `--observer-perception-mode
      default` PPMs.
    - **§8.5.3** Non-zero-beta consistency.
      Run `--render-aovs --observer-perception-mode
      relativistic --observer-beta 0.5
      --observer-direction 1,0,0 <fixture>`
      pre + post; `cmp` PPMs (expected
      byte-identical because the unified helper
      composes `aberrateDirection(rel, dir)`
      verbatim).
    - **§8.5.4** OBS-F.2 fixture runtime.
      Run the fixture with relativistic
      perception engaged; verify visible
      aberration in the framebuffer matches
      the OBSERVER.13 `aov_observer_beta.ppm`
      diagnostic's flat-colour `(0, 0, -0.5)`
      anchor.
    - **§8.5.5** Path-tracer primary-ray
      verification. Run `--render-pathtrace`
      pre + post + the OBS-F.2 fixture +
      relativistic perception; `cmp` PPMs.
      Secondary rays are NOT exercised here
      (per §4.2).

---

## 9. Cross-references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") + #1 ("Build incrementally") + #11
  ("explicit, testable interfaces") + #12 ("do
  not overbuild a later system before the
  current layer works") + #16 ("default-off /
  reasoning-traceable defaults") apply.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §7.2
  — the architecture-doc anchor for the
  observer-frame Lorentz boost of the tetrad
  → aberration concept.
- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1) — the canonical OBS-PERCEPT.*
  arc plan that authorised this OBS-PERCEPT.3
  scope.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
  (OBSERVER.1) — the OBSERVER.* foundation arc's
  plan; the OBS-PERCEPT.3 implementation
  consumes the OBSERVER.6 adapter + OBSERVER.8
  payload + OBSERVER.10 OptiX payload (deferred
  to OBS-PERCEPT.4) verbatim.
- `docs/OBSERVER_FRAME_ARC_AUDIT.md` (OBSERVER.15)
  — the capstone audit whose §10 risk #1 this
  OBS-PERCEPT.3 slice closes for the CUDA
  primary-ray path.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3) — the precedent kernel-migration
  audit; the OBS-PERCEPT.3 slice consolidates
  the post-OBS-P.2 guarded-ternary at the
  primary-ray sites into a unified helper.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_TASK.md`
  (OBS-P.1) — the precedent kernel-migration
  task brief; the OBS-PERCEPT.3 task brief
  follows the same per-site discipline.
- `docs/OBSERVER_FRAME_FIXTURE.md` (OBS-F.2)
  — the precedent fixture the OBS-PERCEPT.3
  acceptance gate (§8.5) consumes for runtime
  verification.
- `src/relativity/RelativityMath.h` — the
  math-leaf surface the OBS-PERCEPT.3 unified
  helper composes (the existing
  `aberrateDirection(rel, dir)` +
  `precompute_relativity(beta)` helpers stay
  verbatim).
- `src/manifold/ObserverFrame.h` — the
  `ObserverFrame` POD + `PerceptionMode` enum
  the unified helper reads.
- `src/cuda/CudaTestKernel.cu` — the CUDA
  primary-ray sites the OBS-PERCEPT.3 slice
  modifies.
- `src/cuda/CudaPathTracer.cu` — the CUDA
  path-tracer primary-ray site.
- `docs/BUILD_PLAN.md` — the OBS-PERCEPT.3 entry
  will land alongside the impl slice.

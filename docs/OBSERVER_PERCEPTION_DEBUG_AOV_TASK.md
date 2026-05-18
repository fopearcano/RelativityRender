# Observer Perception Debug AOV — Task Definition (OBS-PERCEPT.7)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this task definition; the implementation
        lands in a subsequent slice (OBS-PERCEPT.9, the
        renumbered ladder slot for the debug-AOV impl)
        that consumes this doc as its canonical brief.

This document defines the work for **OBS-PERCEPT.9 —
observer perception debug AOV implementation** (the
renumbered next impl slot after the OBS-PERCEPT.7
task brief + the OBS-PERCEPT.8 task-brief audit; the
OBS-PERCEPT.8 audit slot may be inserted in-band per
the standing discipline). It is the operator-facing
brief the implementation slice will read to decide
the exact surface, the acceptance gates, and the
non-goals.

Per the OBS-PERCEPT.1 plan §7, the OBS-PERCEPT.7 task
brief authorises the perception-transform diagnostic
AOV(s) that visualise the observer-frame transform's
per-pixel effect on the primary ray + the
perception-mode state. The OBS-PERCEPT.5 OptiX bridge
+ the OBS-PERCEPT.6 audit closed the per-backend
runtime-deferred risk #1 from the OBSERVER.15
capstone; the diagnostic AOV(s) make the
perception-transform's per-pixel effect *visible*
without requiring SDK-host PPM-cmp tooling.

Prerequisite slices already green:

- **OBS-PERCEPT.1** — Arc plan (`8db1f9c`).
- **OBS-PERCEPT.2** — Primary-ray transform task
  (`0bf2bb8`).
- **OBS-PERCEPT.3** — CUDA implementation
  (`b653e48`).
- **OBS-PERCEPT.4** — CUDA audit (`40bb476`).
- **OBS-PERCEPT.5** — OptiX implementation
  (`1dbeb23`).
- **OBS-PERCEPT.6** — OptiX audit (`3d125ad`).

Adjacent precedents this task brief mirrors:

- **`docs/MANIFOLD_DEBUG_AOV_TASK.md`** (MANI-I.7) —
  the manifold debug AOV task brief that shipped
  the `ManifoldCoordinates = 6` AOV via the
  `--render-aovs --manifold-debug` two-flag
  composition.
- **`docs/OBSERVER_DEBUG_AOV_TASK.md`** (OBSERVER.12)
  — the precedent observer-debug AOV task brief
  whose §2.2 `observerDirection (FUTURE)` slot the
  OBS-PERCEPT.7 task brief LIFTS into a concrete
  proposal alongside the new
  `observerAberrationMagnitude` AOV.
- **`docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`**
  (FIELD-I.6) — the FIELD-I.* diagnostic AOV task
  brief; OBS-PERCEPT.7 mirrors its 8-section
  layout verbatim.

---

## 1. Exact goal

**Expose the observer-frame primary-ray perception
transform's diagnostics as optional per-pixel AOV
outputs, gated on the existing `--observer-debug`
CLI flag (already shipped at OBSERVER.13), so an
operator can *see* the transform's per-pixel effect
without changing beauty rendering, ray generation,
or shading behaviour.**

The OBS-PERCEPT.* arc landed the active CUDA + OptiX
primary-ray aberration arms at OBS-PERCEPT.3 +
OBS-PERCEPT.5; the OBS-PERCEPT.4 + .6 audits
verified the structural correctness. The remaining
gap is **per-pixel runtime diagnostic visibility**
— operators currently have to rely on PPM-cmp
tooling to see whether the perception transform
fires per pixel. The diagnostic AOVs make this
visible directly.

Three diagnostic AOV channels ship at OBS-PERCEPT.9:

- **`ObserverAberrationMagnitude`** (NEW; 1
  float/pixel) — the magnitude of the per-pixel
  aberration delta (the angular difference between
  the pre-boost and post-boost primary-ray
  direction). Zero on Identity mode + zero on
  beta=0 (the no-op anchors); non-zero on
  ConstantVelocityMinkowski + non-zero beta (the
  perception-engaging anchor). The most
  informative single-channel diagnostic for "did
  the perception transform fire on this pixel?"
- **`ObserverBeta`** (EXISTING; 3 floats/pixel) —
  preserved verbatim from OBSERVER.13. Writes
  `observer_frame.beta` directly to the AOV. Flat
  per-pixel value (the observer is per-launch);
  visually confirms the per-launch observer
  payload reached the kernel intact.
- **`ObserverDirection`** (NEW; 3 floats/pixel) —
  the LIFTED OBSERVER.12 §2.2 deferred-FUTURE slot.
  Writes `normalize(observer_frame.beta)` (the
  unit-length direction along beta). On Identity
  or zero-beta, writes `(0, 0, 0)` (the sentinel
  "no direction" anchor). Useful for visualising
  the observer's motion direction independently
  of magnitude.

All three AOVs are **opt-in via the existing
`--observer-debug` CLI flag** (from OBSERVER.13);
no new CLI flag this slice. The two new AOVs
piggyback on the existing observer-debug gate to
keep the operator-facing surface compact.

All three AOVs are **read-only diagnostics**: the
kernel reads the observer payload (or computes the
aberration delta from the pre-boost direction
snapshot) and writes the diagnostic value. The
kernel does NOT modify the perception transform's
behavior; the AOVs visualise the *existing*
transform's per-pixel state.

---

## 2. Proposed AOVs

Three diagnostic AOV channels. The OBS-PERCEPT.9
implementation ships all three together because:

- They share the same `--observer-debug` gate
  (orthogonal kernel-arm activation; cheap to
  ship together).
- They visualise complementary aspects of the
  same per-launch observer state (magnitude +
  flat-payload + direction); shipping individually
  would create three sub-slices with the same
  kernel + AOV plumbing.
- The cross-backend symmetry argument (FIELD-BEAUTY.6
  §3.7 + OBS-PERCEPT.6 §3.7 five-axis pattern)
  scales naturally to three new AOVs in one slice.

### 2.1 `observerAberrationMagnitude` (NEW)

- **Component count:** 1 float / pixel
  (single-channel scalar; replicated to RGB at
  save time per the existing `Depth` /
  `DopplerFactor` / `SearchlightFactor` /
  `FieldScalar` 1-channel AOV encoding
  precedent).
- **Encoding:** per-pixel value = `magnitude(post_dir
  - pre_dir)` where `pre_dir` is the camera-
  generated ray direction (immediately after
  `generate_camera_ray(...)` / `generate_primary_ray(...)`)
  and `post_dir` is the direction returned by
  `rr::manifold::apply_observer_primary_ray_aberration(observer_frame,
  pre_dir)`. The magnitude is computed as
  `sqrt(dx² + dy² + dz²)` where `(dx, dy, dz) =
  post_dir - pre_dir`. The output is bounded in
  `[0, 2]` (the worst-case difference between two
  unit vectors).
- **Identity / zero-beta neutral value:** `0.0`
  at every pixel. The
  `apply_observer_primary_ray_aberration(...)`
  helper returns the input direction unchanged
  on both no-op anchors (Identity outer gate +
  zero-beta inner gate), so `post_dir = pre_dir`
  → `magnitude = 0`. Saved PPM is flat black.
- **Non-default visualisation:** with
  `--observer-perception-mode relativistic
  --observer-beta 0.5 --observer-direction
  1,0,0`, the magnitude varies smoothly across
  the framebuffer. Pixels whose primary-ray
  direction is aligned with the beta direction
  experience smaller aberration (the boost is
  longitudinal); pixels whose direction is
  transverse experience larger aberration. The
  saved PPM shows a smoothly-varying grayscale
  pattern that visualises the per-pixel
  Lorentz boost strength.
- **Why this is the load-bearing new AOV:**
  it's the single most informative diagnostic
  for the OBS-PERCEPT.* arc — directly answers
  "did the perception transform fire on this
  pixel, and by how much?" Mirrors the
  FIELD-I.7 `FieldScalar` AOV's role for the
  FIELD-I.* arc.

### 2.2 `ObserverBeta` (EXISTING — preserved verbatim)

- **Component count:** 3 floats / pixel (Vec3).
- **Encoding:** per-pixel value =
  `view.observer_frame.beta` (CUDA) /
  `optixLaunchParams.observer_frame.beta`
  (OptiX). Identical to OBSERVER.13's contract.
- **Status:** SHIPPED at OBSERVER.13 (`AOVType::ObserverBeta
  = 7`). The OBS-PERCEPT.9 implementation slice
  does NOT modify this AOV's data-model surface,
  kernel write arm, factory, or doc-comment;
  preserved byte-identically. The OBS-PERCEPT.7
  task brief documents the AOV here for
  completeness of the diagnostic-AOV family
  story — operators engaging
  `--observer-debug` get all three diagnostics
  (the existing `ObserverBeta` + the two new
  AOVs) via the same gate.

### 2.3 `observerDirection` (NEW — lifted from OBSERVER.12 §2.2)

- **Component count:** 3 floats / pixel (Vec3).
- **Encoding:** per-pixel value =
  `normalize(view.observer_frame.beta)` (CUDA) /
  `normalize(optixLaunchParams.observer_frame.beta)`
  (OptiX). When `|beta| > 0`, the direction is
  the unit-length 3-vector along beta. When
  `|beta| == 0` (the Identity default OR the
  explicit zero-beta non-default state), the
  encoding is `(0, 0, 0)` (the sentinel "no
  direction" anchor — the
  `ObserverConfig::direction` doc-comment's
  zero-sentinel convention).
- **Identity / zero-beta neutral value:**
  `(0, 0, 0)` at every pixel (flat black PPM).
- **Non-default visualisation:** with a non-zero
  beta, the AOV writes a unit-length RGB-encoded
  direction at every pixel. Diagnostic for "what
  direction is the observer moving in?" when an
  artist has authored an oblique observer
  velocity (e.g. `--observer-direction
  0.6,-0.8,0.0`).
- **Lifted from OBSERVER.12:** the OBSERVER.12
  task brief documented this slot as
  `observerDirection (FUTURE)`; the
  implementation deferred because the
  `ObserverBeta` slot's magnitude already
  encodes the direction modulo magnitude.
  OBS-PERCEPT.7 lifts the deferral because the
  perception-transform debug AOV family is the
  natural home for direction-specific
  diagnostics — and the OBS-PERCEPT.9 impl slice
  ships the AOV plumbing for the new
  `ObserverAberrationMagnitude` already, so
  adding `observerDirection` alongside is a
  near-zero-marginal-cost extension.

### 2.4 Naming convention

The new AOVs become:

- **`AOVType::ObserverAberrationMagnitude`**
  (enumerator value `= 9`, appended after
  `FieldScalar = 8`). The `aov_type_name(...)`
  mapping is `"observer_aberration_magnitude"`
  (snake_case; PPM filename
  `output/aov_observer_aberration_magnitude.ppm`
  / `output/optix_aov_observer_aberration_magnitude.ppm`).
  Factory function:
  `AOV::make_observer_aberration_magnitude(std::string
  name = {})`.
- **`AOVType::ObserverDirection`** (enumerator
  value `= 10`, appended after
  `ObserverAberrationMagnitude = 9`). The
  `aov_type_name(...)` mapping is
  `"observer_direction"`. Factory:
  `AOV::make_observer_direction(...)`.

Both new enumerator values continue the existing
`AOVType` enum's append-at-end discipline (verified
at FIELD-I.7 + OBSERVER.13 + MANI-I.8 + every prior
AOV-extension slice). The existing nine enumerator
values (Beauty=0 ... FieldScalar=8) are preserved
verbatim; the existing seven AOV kernel arms +
factories are unchanged.

---

## 3. Expected behaviour

The OBS-PERCEPT.9 implementation slice must satisfy
three load-bearing behavioural invariants:

### 3.1 Beauty output unchanged unless perception transform is active

The two NEW AOVs do NOT modify the beauty pass under
any circumstances. The kernel-arm writes are
**post-shading + post-aberration**: the diagnostic
values are written AFTER the
`apply_observer_primary_ray_aberration(...)` helper
fires, AFTER the Doppler / searchlight modulation
on the beauty color, AFTER the framebuffer write.
The diagnostic AOVs are pure read-only sinks; the
beauty pass is byte-identical to the OBS-PERCEPT.6
audit baseline regardless of whether the AOVs are
requested.

The clause "unless perception transform is active"
in the operator's brief refers to the OBS-PERCEPT.3
+ .5 arms: when
`--observer-perception-mode relativistic
--observer-beta 0.5 ...` is engaged, the beauty
pass DIVERGES from the pre-OBS-PERCEPT.3 baseline
(the aberration changes the per-pixel color). This
divergence is the OBS-PERCEPT.* arc's intended
behavioural addition; the OBS-PERCEPT.9 debug-AOV
slice does NOT add NEW beauty modulation on top.

### 3.2 Default observer produces neutral diagnostics

When the operator engages `--observer-debug` (or
`--render-aovs --observer-debug` / `--render-optix-aovs
--observer-debug`) WITHOUT
`--observer-perception-mode relativistic` (i.e. on
the default Identity mode), all three AOVs produce
their documented neutral values:

- `ObserverAberrationMagnitude`: `0.0` at every
  pixel (the aberration helper short-circuits to
  identity; `post_dir = pre_dir`; magnitude = 0).
- `ObserverBeta`: `(0, 0, 0)` at every pixel (the
  default `observer_frame.beta` from the
  OBSERVER.6 adapter's `rest_frame()` Identity
  path).
- `ObserverDirection`: `(0, 0, 0)` at every pixel
  (the zero-magnitude beta produces the sentinel
  "no direction" anchor).

All three saved PPMs are flat black. This confirms
visually that the kernel saw the default Identity-
mode payload + the perception transform's no-op
anchor fired correctly.

### 3.3 AOV only generated when requested

The new AOV slots are gated on the SAME two-flag
composition the OBSERVER.13 `ObserverBeta` AOV uses:

1. The operator passes `--render-aovs` (CUDA) OR
   `--render-optix-aovs` (OptiX).
2. The operator passes `--observer-debug` (an
   existing CLI flag landed at OBSERVER.13 — the
   `cfg.observer.debug_visualization = true`
   modifier).

Either gate by itself produces no new file. Both
together cause the renderer to allocate the new
per-pass device buffers, fill them from the kernel,
and save the resulting PPMs alongside the existing
AOV PPMs. The composition mirrors the OBSERVER.13
two-flag composition verbatim; no new CLI flag this
slice.

A separate dedicated CLI action
(`--render-observer-perception-debug-aov` or
similar) is NOT shipped at OBS-PERCEPT.9. The
two-flag composition is the only entry point.

---

## 4. CUDA / OptiX interaction

The OBS-PERCEPT.9 implementation slice's kernel-side
scope is strictly **read-only** on the observer
payload + the aberration helper:

### 4.1 Read observer payload + report transform magnitude / beta / direction

- **CUDA path** (`k_render_scene` /
  `k_sphere_relativistic` /
  `k_pathtrace_sample`): the kernel arm writes
  the three AOVs alongside the existing `ObserverBeta`
  arm (OBSERVER.13). The kernel-arm structure
  mirrors the existing OBSERVER.13 + FIELD-I.9
  diagnostic-AOV write arms verbatim:

  ```cpp
  if (scene.aovs.observer_aberration_magnitude != nullptr) {
      const Vec3 pre_dir = ...;  // snapshot before helper
      const Vec3 post_dir = ray.direction;  // after helper
      const Vec3 delta = post_dir - pre_dir;
      const float mag = sqrtf(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
      scene.aovs.observer_aberration_magnitude[pix_idx_1] = mag;
  }

  if (scene.aovs.observer_direction != nullptr) {
      const Vec3 beta = scene.observer_frame.beta;
      const float beta_mag = sqrtf(beta.x*beta.x + beta.y*beta.y + beta.z*beta.z);
      const Vec3 dir = (beta_mag > 0.0f)
          ? Vec3{beta.x / beta_mag, beta.y / beta_mag, beta.z / beta_mag}
          : Vec3{0.0f, 0.0f, 0.0f};
      scene.aovs.observer_direction[pix_idx_3 + 0] = dir.x;
      scene.aovs.observer_direction[pix_idx_3 + 1] = dir.y;
      scene.aovs.observer_direction[pix_idx_3 + 2] = dir.z;
  }
  ```

  The pre-boost direction snapshot requires saving
  `ray.direction` BEFORE the
  `apply_observer_primary_ray_aberration(...)`
  call. The OBS-PERCEPT.3 dispatch site at
  `CudaTestKernel.cu:248-258` (and the two other
  CUDA sites) currently overwrites `ray.direction`
  in-place; the OBS-PERCEPT.9 impl slice adds a
  local `pre_aberration_direction` snapshot
  before the dispatch.

- **OptiX path** (`__raygen__pinhole` /
  `__raygen__pathtrace`): same shape on the OptiX
  side. The pre-boost snapshot is captured
  similarly before the OBS-PERCEPT.5 dispatch at
  `OptixPrograms.cu:239-249` + `:1268-1277`. The
  diagnostic-AOV write arms are inserted at the
  same per-pixel position as the OBSERVER.13
  `observer_beta` write arm (per
  `OptixPrograms.cu:920-928` precedent).

### 4.2 Report transform magnitude / beta / direction

The three AOV writes per pixel:

- **`ObserverAberrationMagnitude`**: requires the
  pre-boost snapshot (see §4.1). Computed as
  `sqrt(|post_dir - pre_dir|²)`. The kernel
  performs the sqrt only when the AOV pointer is
  non-null (the pointer-gate avoids the cost in
  the default-no-AOV-requested case).
- **`ObserverBeta`**: reads
  `view.observer_frame.beta` (CUDA) /
  `optixLaunchParams.observer_frame.beta`
  (OptiX) directly. Identical to OBSERVER.13's
  existing arm; no NEW math.
- **`ObserverDirection`**: reads the same beta
  field + computes `normalize(beta)` (with the
  zero-magnitude sentinel branch). The sqrt cost
  is paid once per thread (the `beta_mag`
  computation); the per-pixel cost is the
  division + the branch.

### 4.3 Do not introduce new perception math

The OBS-PERCEPT.9 slice does NOT introduce any new
perception-transform math. The aberration helper
at `ObserverFrame.h:553+` is preserved verbatim;
the new diagnostic AOVs READ the helper's
input (`pre_dir`) + output (`post_dir`) +
compute the magnitude/direction/beta from existing
state. No new branches in the perception transform;
no new gates; no new clamps.

### 4.4 Cross-backend math consistency

Both backends MUST produce byte-identical diagnostic
PPMs for the same input config + scene + perception
mode. The cross-backend equivalence is structurally
guaranteed:

- Both backends consume the same `ObserverFrame`
  POD (identical fields, identical defaults).
- Both backends invoke the same
  `apply_observer_primary_ray_aberration(...)`
  helper (same RR_HD inline math leaf).
- Both backends compute the magnitude via the same
  `sqrt(dx² + dy² + dz²)` formula.
- Both backends compute the normalised direction
  via the same `(beta / |beta|)` formula.

The cross-backend bit-identity check is the
canonical SDK-host validation (cmp
`output/aov_observer_aberration_magnitude.ppm`
between CUDA + OptiX). Mirrors the FIELD-BEAUTY.6
§3.7 five-axis symmetry framework applied to the
new diagnostic AOVs.

---

## 5. Files likely involved

The implementation slice (OBS-PERCEPT.9) is
expected to touch the following files. Numbers in
parentheses are rough net-line estimates from
comparable past slices (OBSERVER.13 + FIELD-I.7 +
FIELD-I.9 + FIELD-I.11).

| Layer | File | Why |
|-------|------|-----|
| AOV data model | `src/renderer/AOV.h` (+50) | Adds `AOVType::ObserverAberrationMagnitude = 9` + `AOVType::ObserverDirection = 10` enumerators; `make_observer_aberration_magnitude(...)` + `make_observer_direction(...)` factory declarations. Doc-comment blocks mirror the FIELD-I.7 + OBSERVER.13 patterns. |
| AOV data model | `src/renderer/AOV.cpp` (+25) | `aov_component_count` cases: `ObserverAberrationMagnitude → 1`; `ObserverDirection → 3`. `aov_type_name` cases: `"observer_aberration_magnitude"` + `"observer_direction"`. Factory implementations. |
| Tests | `tests/renderer_tests.cpp` (+90) | 6 new test functions covering the two new AOV enumerator values + names + component counts + factories. Mirrors the FIELD-I.7 + OBSERVER.13 test patterns. Total +6 RR_CHECK assertions (3 per AOV × 2 AOVs); renderer_tests grows from 35 → 41 (or similar). |
| CUDA AOV view | `src/cuda/CudaAOV.cuh` (+12) | New `float* observer_aberration_magnitude = nullptr` + `float* observer_direction = nullptr` slots on `DeviceAOVView`. |
| CUDA renderer | `src/cuda/CudaRenderer.h` (+30) | New `float* observer_aberration_magnitude` + `float* observer_direction` fields on `AOVTargets`. Doc-comment documents the OBSERVER.13 gate-sharing convention (`cfg.observer.debug_visualization` controls allocation of all three observer-debug AOVs together). |
| CUDA renderer | `src/cuda/CudaRenderer.cu` (+15) | Two new threading lines: `view.aovs.observer_aberration_magnitude = targets.observer_aberration_magnitude;` + sibling for `observer_direction`. |
| CUDA kernel | `src/cuda/CudaTestKernel.cu` (+60) | At each of the three primary-ray sites (`k_render_scene` + `k_sphere_relativistic` — `k_pathtrace_sample` is on `CudaPathTracer.cu`): (a) capture `pre_aberration_direction` snapshot before the OBS-PERCEPT.3 dispatch; (b) after the dispatch, add the two new AOV write arms (gated on the per-AOV pointer; computing magnitude + direction). The existing OBSERVER.13 `observer_beta` arm is preserved verbatim. |
| CUDA path-tracer | `src/cuda/CudaPathTracer.cu` (+30) | Same shape at the path-tracer primary-ray site (`k_pathtrace_sample`). Captures pre-aberration snapshot before the OBS-PERCEPT.3 helper call; writes the two new AOVs after. |
| OptiX launch params | `src/optix/OptixLaunchParams.h` (+25) | Two new trailing `float* aov_observer_aberration_magnitude = nullptr;` + `float* aov_observer_direction = nullptr;` fields. Appended after `aov_field_scalar` (FIELD-I.11). |
| OptiX renderer | `src/optix/OptixRenderer.h` (+30) | Two new `rr::image::Image observer_aberration_magnitude;` + `rr::image::Image observer_direction;` fields on `AovResult`. Doc-comments document the `--observer-debug` gate-sharing convention. |
| OptiX renderer | `src/optix/OptixRenderer.cpp` (+80) | SDK body: allocate the two new device buffers when `observer_debug` is `true` (the existing OBSERVER.13 gate flag — reuses the same `bool` trailing parameter; no new trailing param needed); thread pointers through `OptixLaunchParams`; download into `AovResult`. Stub fallback signature unchanged (no new trailing param). |
| OptiX programs | `src/optix/OptixPrograms.cu` (+80) | At each of the two raygen sites: capture pre-aberration snapshot; after the OBS-PERCEPT.5 dispatch, add the two new AOV write arms (per-pixel gated on the per-AOV pointer). Closest-hit / miss / shadow programs unchanged. |
| CLI dispatcher | `src/main.cpp` (+15) | `run_render_aovs` + `run_render_optix_aovs` allocate the two new AOV buffers when `cfg.observer.debug_visualization` is true (the existing OBSERVER.13 gate; the dispatcher allocates the existing `observer_beta` buffer + the two new ones together). New PPM save sites for `output/aov_observer_aberration_magnitude.ppm` / `output/aov_observer_direction.ppm` (CUDA) + `output/optix_aov_observer_aberration_magnitude.ppm` / `output/optix_aov_observer_direction.ppm` (OptiX). |
| Docs | `docs/BUILD_PLAN.md` | OBS-PERCEPT.9 entry. |
| Docs | OBS-PERCEPT.10 audit doc (lands at the next audit slot, not at OBS-PERCEPT.9 impl) | The per-slice audit gate the operator may insert in-band. |
| CMake | none expected | The new AOVs use the existing `rr_renderer` + `rr_gpu` + `rr_optix` wiring; no new link added. |

---

## 6. What must not be touched

Per master rule #3 and the operator's OBS-PERCEPT.7
brief, the OBS-PERCEPT.9 implementation slice MUST
NOT:

### 6.1 No new perception math

The OBS-PERCEPT.9 slice does NOT modify the
`apply_observer_primary_ray_aberration(...)` helper
at `ObserverFrame.h:553+`. The OBS-PERCEPT.3 + .5
arms are preserved verbatim. The new diagnostic
AOVs READ the helper's per-pixel input + output;
they do NOT add new perception-transform branches
or new clamp / gate logic.

### 6.2 No new ObserverFrame POD field

The existing OBSERVER.2-shipped POD fields are
read as-is. No new fields. No ABI extension.

### 6.3 No new CLI flag

The existing `--observer-debug` flag (from
OBSERVER.13) is the load-bearing gate for all
three observer-debug AOVs (`ObserverBeta` +
`ObserverAberrationMagnitude` +
`ObserverDirection`). All three are allocated
together when the gate is set. No new
`--observer-perception-debug` flag this slice.

### 6.4 No change to `ObserverBeta` AOV

The OBSERVER.13 `ObserverBeta = 7` enumerator +
`make_observer_beta(...)` factory + kernel write
arm + dispatcher allocation gate are preserved
verbatim. The OBS-PERCEPT.9 slice adds two NEW
AOV enumerators (values 9 + 10); the existing
`ObserverBeta` is untouched.

### 6.5 No modification to existing AOV slots

The existing nine AOV slots (Beauty=0 ...
FieldScalar=8) preserve their enumerator values,
component counts, names, factories, kernel write
arms, AOV view pointers, AOVTargets fields,
OptixLaunchParams pointers, and dispatcher
allocation gates verbatim. The new enumerators
(9 + 10) are appended at the END of the enum;
no field-offset shifts.

### 6.6 No new scene-file schema

No parser change to `src/io/SceneLoader.cpp`. The
OBS-F.2 fixture continues to author observer
state via the `relativity` block; no new
`observer_debug_*` scene block needed.

### 6.7 No new manifold math

No `src/manifold/` modifications beyond the
existing `ObserverFrame.h` (which the new AOV
kernel arms READ via the existing payload).
The `apply_observer_primary_ray_aberration(...)`
helper is unchanged.

### 6.8 No field interpretation changes

No `src/field/` modifications. The FIELD-I.* +
FIELD-BEAUTY.* arc family's surfaces are
preserved verbatim.

### 6.9 No new perception transform behaviour

The diagnostic AOVs are READ-ONLY sinks. They do
NOT modify the perception transform's per-pixel
behaviour; they visualise the existing transform's
state.

### 6.10 No C4D / server / UI / node-editor touch

Standard discipline carried forward from every
prior arc; the OBS-PERCEPT.9 slice does not
touch any DCC / server / UI / node-editor
surface.

### 6.11 No legacy `observer.velocity` removal

The legacy `rr::relativity::Observer::velocity`
field is preserved on the host-side payload.

### 6.12 No path-tracer secondary-ray diagnostic

The new AOVs visualise the PRIMARY-ray
transform only. Secondary bounce rays in
`k_pathtrace_sample` are NOT exercised by the
diagnostic write arm (consistent with the
OBS-PERCEPT.* arc's Option A primary-ray-only
scope per OBS-PERCEPT.1 §5.2).

---

## 7. PASS criteria

The OBS-PERCEPT.9 implementation slice's
acceptance gate is satisfied when ALL of the
following hold:

### 7.1 Structural

- [ ] `AOVType::ObserverAberrationMagnitude` and
      `AOVType::ObserverDirection` enumerators
      exist at the end of the `AOVType` enum
      (values `= 9` and `= 10` respectively).
- [ ] `aov_component_count(AOVType::ObserverAberrationMagnitude)
      == 1` and
      `aov_component_count(AOVType::ObserverDirection)
      == 3`.
- [ ] `aov_type_name(AOVType::ObserverAberrationMagnitude)
      == "observer_aberration_magnitude"` and
      `aov_type_name(AOVType::ObserverDirection)
      == "observer_direction"`.
- [ ] `AOV::make_observer_aberration_magnitude(...)`
      and `AOV::make_observer_direction(...)`
      factories exist and produce well-formed
      `AOV` instances.
- [ ] `DeviceAOVView::observer_aberration_magnitude
      = nullptr` and
      `DeviceAOVView::observer_direction = nullptr`
      slots exist (CUDA).
- [ ] `AOVTargets::observer_aberration_magnitude
      = nullptr` and
      `AOVTargets::observer_direction = nullptr`
      fields exist (CUDA).
- [ ] `OptixLaunchParams::aov_observer_aberration_magnitude
      = nullptr` and
      `OptixLaunchParams::aov_observer_direction
      = nullptr` fields exist (appended after
      `aov_field_scalar`).
- [ ] OptiX device-side programs gate writes on
      the per-AOV pointer != nullptr.
- [ ] CUDA `CudaTestKernel.cu` +
      `CudaPathTracer.cu` AOV-aware kernels gate
      writes on the per-AOV pointer != nullptr.
- [ ] Pre-aberration direction snapshot captured
      at all three CUDA primary-ray sites
      (`k_render_scene` + `k_sphere_relativistic`
      + `k_pathtrace_sample`) and both OptiX
      raygen sites (`__raygen__pinhole` +
      `__raygen__pathtrace`).
- [ ] `--render-aovs --observer-debug` (CUDA path)
      emits `output/aov_observer_aberration_magnitude.ppm`
      + `output/aov_observer_direction.ppm`
      alongside the existing
      `aov_observer_beta.ppm`.
- [ ] `--render-optix-aovs --observer-debug`
      (OptiX path) emits the corresponding
      `optix_aov_*.ppm` files.

### 7.2 Behavioural

- [ ] `--render-aovs` / `--render-optix-aovs`
      WITHOUT `--observer-debug` emits exactly
      the same AOV PPM set it emitted
      pre-OBS-PERCEPT.9 (no new files; no
      missing files; no changed files).
- [ ] Beauty output of every existing CLI action
      is pixel-bit-identical to the
      pre-OBS-PERCEPT.9 baseline.
- [ ] On default observer (no
      `--observer-perception-mode relativistic`
      OR `--observer-beta 0`),
      `aov_observer_aberration_magnitude.ppm`
      decodes to `0.0` at every pixel within
      `1.0e-5f` tolerance.
- [ ] On default observer,
      `aov_observer_direction.ppm` decodes to
      `(0, 0, 0)` at every pixel within
      `1.0e-5f` tolerance.
- [ ] On `--observer-perception-mode relativistic
      --observer-beta 0.5 --observer-direction
      1,0,0`, `aov_observer_aberration_magnitude.ppm`
      shows a smoothly-varying grayscale pattern
      (non-zero magnitude on transverse pixels;
      decreasing-to-zero magnitude on longitudinal
      pixels along the `(1, 0, 0)` direction).
- [ ] On the same invocation,
      `aov_observer_direction.ppm` shows a flat
      colour matching the `normalize((0.5, 0, 0))
      = (1, 0, 0)` direction encoded as RGB.
- [ ] CUDA-side and OptiX-side PPMs are byte-
      identical for both new AOVs on the same
      input config (cross-backend bit-identity
      empirically verified by `cmp`).
- [ ] The existing `aov_observer_beta.ppm` PPM
      (CUDA + OptiX) is byte-identical to the
      pre-OBS-PERCEPT.9 baseline (the OBSERVER.13
      arm is preserved verbatim).

### 7.3 Test surface

- [ ] `ctest` reports `13/13 passed` on the
      audit-host build (unchanged from
      OBS-PERCEPT.6; no new ctest target).
- [ ] `renderer_tests` grows by 6 new RR_CHECK
      assertions (3 per AOV × 2 AOVs; enum value
      + name + component count + factory).
- [ ] `cli_tests` count unchanged (no new CLI
      flag).
- [ ] `manifold_identity_tests` count unchanged
      (the OBS-PERCEPT.* helper is unmodified).
- [ ] `field_tests` count unchanged.

### 7.4 Documentation

- [ ] `docs/BUILD_PLAN.md` OBS-PERCEPT.9 entry
      added.
- [ ] OPTIONAL: `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
      §7 OBS-PERCEPT.9 entry rewritten with
      landed-surface description.

---

## 8. Runtime-deferred CUDA / OptiX checks

The audit-host build cannot directly verify the
AOVs' pixel content. The runtime checks below are
DEFERRED behind the audit host's existing
no-CUDA-SDK + no-OptiX-SDK fallback, matching the
existing MANI-I.7 / OBSERVER.12 / FIELD-I.6
deferral pattern.

Each deferred check must be exercised on a CUDA +
OptiX-SDK host before the OBS-PERCEPT.11 arc
capstone audit closes the OBS-PERCEPT.* arc:

### 8.1 Default observer neutral PPMs

Run on each backend (CUDA + OptiX):
```
RelativityRender --render-aovs --observer-debug
                 scenes/test_observer_frame.rrscene
RelativityRender --render-optix-aovs --observer-debug
                 scenes/test_observer_frame.rrscene
```

Verify:
- `aov_observer_aberration_magnitude.ppm` /
  `optix_aov_observer_aberration_magnitude.ppm`
  exist; decode to `0.0` at every pixel within
  `1.0e-5f`.
- `aov_observer_direction.ppm` /
  `optix_aov_observer_direction.ppm` exist;
  decode to `(0, 0, 0)` at every pixel within
  `1.0e-5f`.
- `aov_observer_beta.ppm` /
  `optix_aov_observer_beta.ppm` exist;
  decode to `(0, 0, 0)` at every pixel
  (preserves OBSERVER.13 baseline).
- `aov_beauty.ppm` byte-identical to the
  pre-OBS-PERCEPT.9 reference.

### 8.2 Non-default perception magnitude visualisation

Run on both backends:
```
RelativityRender --render-aovs --observer-debug
                 --observer-perception-mode relativistic
                 --observer-beta 0.5
                 --observer-direction 1,0,0
                 scenes/test_observer_frame.rrscene
```

Verify:
- `aov_observer_aberration_magnitude.ppm` shows
  a smoothly-varying grayscale pattern:
    - Pixels along the `(1, 0, 0)` direction
      (longitudinal): magnitude ≈ 0 (the boost
      is along the direction; no transverse
      aberration).
    - Pixels transverse to `(1, 0, 0)`:
      magnitude > 0 (the worst-case transverse
      pixels at perpendicular angles; magnitude
      should be the largest visible value).
- The visible pattern's symmetry matches the
  observer's direction vector (`(1, 0, 0)`).

### 8.3 Non-default direction visualisation

Same invocation as §8.2. Verify:
- `aov_observer_direction.ppm` shows a flat
  colour encoding `normalize((0.5, 0, 0)) =
  (1, 0, 0)`. PPM RGB pixel value = `(255, 0,
  0)` (R = max; G/B = 0).
- Flat across the entire framebuffer (the
  direction is per-launch, not per-pixel).

### 8.4 Oblique direction visualisation

Run on both backends:
```
RelativityRender --render-aovs --observer-debug
                 --observer-perception-mode relativistic
                 --observer-beta 0.5
                 --observer-direction 0.6,-0.8,0.0
                 scenes/test_observer_frame.rrscene
```

Verify:
- `aov_observer_direction.ppm` shows a flat
  colour encoding `normalize((0.3, -0.4, 0))`
  ≈ `(0.6, -0.8, 0)`. PPM RGB pixel value =
  `(153, 0, 0)` for the R channel (0.6 × 255 ≈
  153; the -0.8 G channel clamps to 0 in the
  PPM 8-bit signed-to-unsigned encoding; the
  alpha encoding details land at the
  implementation slice).

### 8.5 Cross-backend equivalence

Run §8.2 + §8.3 + §8.4 on both backends. Compare:

- `cmp output/aov_observer_aberration_magnitude.ppm
  output/optix_aov_observer_aberration_magnitude.ppm`
  → exit status `0` (byte-identical).
- `cmp output/aov_observer_direction.ppm
  output/optix_aov_observer_direction.ppm`
  → exit status `0`.
- `cmp output/aov_observer_beta.ppm
  output/optix_aov_observer_beta.ppm`
  → exit status `0` (preserves OBSERVER.13
  cross-backend equivalence baseline).

Cross-backend bit-identity is structurally
guaranteed by the OBS-PERCEPT.6 §3.7 five-axis
symmetry argument applied to the new diagnostic
AOVs (same POD; same helper output snapshot;
same magnitude computation; same direction
normalization).

### 8.6 Composability with other debug AOVs

Run on both backends:
```
RelativityRender --render-aovs
                 --observer-debug
                 --manifold-debug
                 --field-debug
                 --observer-perception-mode relativistic
                 --observer-beta 0.5
                 --observer-direction 1,0,0
                 --manifold-enable
                 --manifold-chart schwarzschild-like
                 --manifold-strength 0.5
                 --field-enable
                 --field-kind radial
                 scenes/test_schwarzschild_like_manifold.rrscene
```

Verify all debug AOVs are emitted independently:
- `aov_observer_aberration_magnitude.ppm` —
  perception transform diagnostic.
- `aov_observer_direction.ppm` — observer
  direction diagnostic.
- `aov_observer_beta.ppm` — observer beta
  diagnostic.
- `aov_manifold_coordinates.ppm` — manifold
  chart diagnostic.
- `aov_field_scalar.ppm` — field-sample
  diagnostic.

The five debug AOVs are orthogonal; all can be
active at the same launch. The four pre-OBS-PERCEPT.9
debug AOVs preserve their pre-existing visual
signatures.

### 8.7 Off-path bit-identity

Run on both backends:
```
RelativityRender --render-aovs
                 scenes/test_observer_frame.rrscene
```
(WITHOUT `--observer-debug`)

Verify:
- Exactly six AOV PPMs are produced (the
  standard Beauty / Normal / Depth / Albedo /
  DopplerFactor / SearchlightFactor); no
  observer-debug PPMs (no
  `aov_observer_aberration_magnitude.ppm`, no
  `aov_observer_direction.ppm`, no
  `aov_observer_beta.ppm`) — exactly matching
  the pre-OBS-PERCEPT.9 + post-OBSERVER.13
  baseline.
- All six PPMs byte-identical to the
  pre-OBS-PERCEPT.9 reference.

---

## 9. Cross-references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 + #11 +
  #12 + #16 apply.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §7.2
  — the observer-frame Lorentz boost concept
  the OBS-PERCEPT.* arc operationalises.
- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1).
- `docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`
  (OBS-PERCEPT.2).
- `docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md`
  (OBS-PERCEPT.4) — the OBS-PERCEPT.3 audit
  whose runtime-deferred SDK-host scenarios
  this OBS-PERCEPT.9 debug-AOV slice would help
  visualise.
- `docs/OBSERVER_PRIMARY_RAY_OPTIX_AUDIT.md`
  (OBS-PERCEPT.6) — the OBS-PERCEPT.5 audit;
  same role on the OptiX side.
- `docs/OBSERVER_DEBUG_AOV_TASK.md` (OBSERVER.12)
  — the precedent task brief whose §2.2
  `observerDirection (FUTURE)` slot this
  OBS-PERCEPT.7 brief LIFTS. The OBS-PERCEPT.9
  implementation extends the OBSERVER.12 +
  OBSERVER.13 + OBSERVER.14 diagnostic-AOV
  family.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — the per-slice audit for
  the OBSERVER.13 `ObserverBeta` AOV; the
  OBS-PERCEPT.9 audit (renumbered OBS-PERCEPT.10)
  will follow this shape.
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6) — the precedent diagnostic-AOV
  task brief shape.
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_AUDIT.md`
  (FIELD-I.8) — the precedent diagnostic-AOV
  audit; OBS-PERCEPT.10 audit shape.
- `src/renderer/AOV.h` / `AOV.cpp` — the AOV
  data-model surface the new
  `ObserverAberrationMagnitude` /
  `ObserverDirection` enumerators + factories
  extend.
- `src/manifold/ObserverFrame.h` — the
  `ObserverFrame` POD + `PerceptionMode` enum
  + `apply_observer_primary_ray_aberration(...)`
  helper the diagnostic AOVs read (no
  modifications).
- `src/cuda/CudaAOV.cuh` /
  `src/cuda/CudaRenderer.h` /
  `src/cuda/CudaRenderer.cu` /
  `src/cuda/CudaTestKernel.cu` /
  `src/cuda/CudaPathTracer.cu` — the CUDA-side
  surface the new AOV slots + kernel arms
  extend.
- `src/optix/OptixLaunchParams.h` /
  `src/optix/OptixRenderer.h` /
  `src/optix/OptixRenderer.cpp` /
  `src/optix/OptixPrograms.cu` — the OptiX-side
  surface the new AOV slots + raygen arms
  extend.
- `src/main.cpp` — the dispatchers
  (`run_render_aovs` + `run_render_optix_aovs`)
  the new AOV allocation + save sites extend.
- `scenes/test_observer_frame.rrscene` — the
  precedent OBS-F.2 fixture the OBS-PERCEPT.9
  acceptance gate (§7.2) + the SDK-host
  deferred runtime checks (§8) consume for
  runtime verification.
- `docs/BUILD_PLAN.md` — the OBS-PERCEPT.7
  entry will land alongside this task brief.

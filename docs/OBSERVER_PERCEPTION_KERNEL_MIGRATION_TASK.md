# Kernel-Side Perception Transform Migration — Task Definition (OBS-P.1)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this task definition; the implementation
        lands in a subsequent slice that consumes this
        doc as its canonical brief.

This document defines the work for **OBS-P.1 —
kernel-side perception-transform migration**, the
first slice of the **Observer Perception (OBS-P) arc**
that follows the closed **Observer-Frame foundation
arc** (OBSERVER.1 → OBSERVER.15; see
`docs/OBSERVER_FRAME_ARC_AUDIT.md` for the capstone
verdict `PASS_WITH_RUNTIME_DEFERRED`). It is the
operator-facing brief the implementation slice will
read to decide the exact surface, the acceptance
gates, and the non-goals.

The OBS-P arc lifts the kernel-side perception
pipeline (aberration / Doppler / searchlight) off
the legacy `rr::relativity::Observer::velocity` /
`scene.observer.velocity` reads and onto the new
`ObserverFrame::beta` field that the OBSERVER.8 +
OBSERVER.10 payload bridges already carry into both
backends. The migration is a **guarded read-site
swap** at every existing perception call site —
not a sweep, not a refactor, not new math.

Prerequisite slices already green (Observer-Frame
foundation arc):

- **OBSERVER.1**  — Planning slice (`eee9d6b`).
- **OBSERVER.2**  — Data model (`85496a5`).
- **OBSERVER.3**  — Data model audit (`bf57c9e`).
- **OBSERVER.4**  — Config / CLI bridge (`16600dc`).
- **OBSERVER.5**  — Config / CLI bridge audit
  (`27ec0d9`).
- **OBSERVER.6**  — Camera-to-observer adapter
  (`e2cde15`).
- **OBSERVER.7**  — Camera-to-observer adapter audit
  (`a0215c0`).
- **OBSERVER.8**  — CUDA observer payload bridge
  (`12f4942`).
- **OBSERVER.9**  — CUDA observer payload audit
  (`e5fe441`).
- **OBSERVER.10** — OptiX observer payload bridge
  (`977ff73`).
- **OBSERVER.11** — OptiX observer payload audit
  (`c739c56`).
- **OBSERVER.12** — Observer debug AOV task
  definition (`e6d6ffc`).
- **OBSERVER.13** — Observer debug AOV
  implementation (`b34e265`).
- **OBSERVER.14** — Observer debug AOV audit
  (`4d5be32`).
- **OBSERVER.15** — Arc capstone audit
  (`2e3a9e3`) verdict
  PASS_WITH_RUNTIME_DEFERRED;
  recommended next stage = OBS-P.1.

Adjacent precedents this task brief mirrors:

- **`docs/OBSERVER_DEBUG_AOV_TASK.md`**
  (OBSERVER.12) — the precedent task-definition
  doc; OBS-P.1 mirrors its operator-section
  ordering verbatim.
- **`docs/MANIFOLD_DEBUG_AOV_TASK.md`**
  (MANI-I.7) — the original task-definition
  template (planning slice's discipline).
- **`docs/OBSERVER_FRAME_ARC_AUDIT.md`**
  (OBSERVER.15) §9.1 — the recommended next
  stage analysis that authorises OBS-P.1 as
  the operator-prompted continuation.

---

## 1. Exact goal

**Gate the existing CUDA + OptiX aberration /
Doppler / searchlight kernel call sites on
`observer_frame.perception_mode ==
ConstantVelocityMinkowski`, and when the gate is
true, read the observer 3-velocity from
`observer_frame.beta` instead of the legacy
`observer.velocity` (CUDA) /
`optixLaunchParams.observer.velocity` (OptiX).**

The migration is a **per-call-site guarded read
swap** at every existing perception call site. No
new math, no new SR helpers, no new kernel
arithmetic, no new arms — just a guard +
read-site swap.

On the default `PerceptionMode::Identity` mode
(produced by every CLI invocation without
`--observer-perception-mode relativistic`), the
guard returns `false` and the kernel skips the
perception transform entirely. **This is the
critical invariant**: the Identity-mode path
must produce byte-identical output to the
pre-OBS-P.1 baseline because today's renderer
always runs the perception transform but with a
zero `observer.velocity` (which is the legacy
default → all SR helpers collapse to identity
values). Both interpretations produce the same
pixel output mathematically; the guard ensures
the runtime path is also bit-equivalent (or, at
worst, convergence-equivalent at the
single-precision rounding boundary).

On `ConstantVelocityMinkowski` mode, the guard
returns `true` and the kernel reads
`observer_frame.beta` for every perception
helper. The existing `aberrateDirection` /
`dopplerFactor` / `searchlightFactor` /
`applyDopplerColor` helpers take the new beta
unchanged (they accept any `Vec3` 3-velocity).
Output is **convergence-equivalent** to the
pre-OBS-P.1 baseline when the operator passes
the same beta value via either the legacy path
(`Scene::relativity.observer.velocity`) OR the
new path
(`--observer-perception-mode relativistic
--observer-beta X --observer-direction Y,Z,W`);
the **single-source-of-truth invariant** the
OBSERVER.6 adapter establishes guarantees both
paths route to the same final beta vector.

On `CurvedChartGeodesicPlaceholder` mode, the
guard returns `false` (same as `Identity`) —
the placeholder is reserved-but-inert per the
OBSERVER.1 plan §3.6 + §8 non-goals. Future
curved-chart slices will lift this guard;
OBS-P.1 explicitly does NOT.

The migration **resolves the OBSERVER.15
capstone's #1 remaining risk** (kernel-side
perception-transform migration deferred). After
OBS-P.1 lands and is audited, the OBSERVER.*
arc's `**Wired**` module-map promotion becomes
PASS-able.

---

## 2. Required migration

The implementation slice must perform the
following read-site swaps at the **six identified
kernel call sites** (3 CUDA + 3 OptiX). Each call
site is wrapped in a `perception_mode` guard;
inside the guard, the legacy `observer.velocity`
read is replaced with `observer_frame.beta`.

### 2.1 CUDA kernel call sites

| # | File | Line | Function | Current read | Migrated read |
|---|------|------|----------|--------------|---------------|
| C-1 | `src/cuda/CudaTestKernel.cu` | ~212 | `k_relativistic_sphere` | `precompute_relativity(observer.velocity)` | guarded; reads `observer_frame.beta` |
| C-2 | `src/cuda/CudaTestKernel.cu` | ~309 | `k_render_scene` | `precompute_relativity(scene.observer.velocity)` | guarded; reads `scene.observer_frame.beta` |
| C-3 | `src/cuda/CudaPathTracer.cu` | ~443 / per-bounce | `k_pathtrace_sample` | `scene.observer.velocity` (via `view.observer = scene.observer()` at host setup) | guarded; reads `scene.observer_frame.beta` per-launch |

For C-3 the migration also requires that the
`launch_pathtrace_sample(...)` launcher in
`CudaPathTracer.cuh` accept the observer-frame as
a trailing parameter (mirrors the MANI-I.5
`manifold_mode` trailing-arg precedent). The
OBSERVER.8 audit noted this extension was
deferred at that slice; OBS-P.1 lifts the
deferral as the natural consequence of the
kernel-side read landing here.

### 2.2 OptiX kernel call sites

| # | File | Line | Function | Current read | Migrated read |
|---|------|------|----------|--------------|---------------|
| O-1 | `src/optix/OptixPrograms.cu` | ~139 | `__miss__radiance` | `precompute_relativity(obs.velocity)` | guarded; reads `optixLaunchParams.observer_frame.beta` |
| O-2 | `src/optix/OptixPrograms.cu` | ~190 | `__raygen__pinhole` | `precompute_relativity(optixLaunchParams.observer.velocity)` | guarded; reads `optixLaunchParams.observer_frame.beta` |
| O-3 | `src/optix/OptixPrograms.cu` | ~1034 | `__raygen__pathtrace` | `precompute_relativity(optixLaunchParams.observer.velocity)` | guarded; reads `optixLaunchParams.observer_frame.beta` |

### 2.3 Guard shape

The guard at every site has the same documented
form, mirroring the SCHW.5 / PENROSE.6 triple-gate
pattern (where the manifold kernel arms are gated
on `is_active(manifold_mode) && chart ==
SchwarzschildLike && strength > 0`):

```
// OBS-P.1 perception-mode gate (CUDA).
const bool perception_active =
    (scene.observer_frame.perception_mode ==
        rr::manifold::PerceptionMode::ConstantVelocityMinkowski);

const rr::math::Vec3 beta_source = perception_active
    ? scene.observer_frame.beta
    : scene.observer.velocity;  // legacy fallback

const auto rel = rr::relativity::precompute_relativity(beta_source);
```

OptiX has the same shape with
`optixLaunchParams.observer_frame.perception_mode`
and `optixLaunchParams.observer_frame.beta` in
place of the CUDA reads.

**Why a per-site ternary instead of a single
top-level branch:** the perception_mode tag is
per-launch (set once at the dispatcher), so
the ternary's branch-predictor behaviour is
warp-uniform — every thread in every warp takes
the same path. The cost is one comparison + one
select per warp; the savings vs. a top-level
function-level branch on the same expression are
negligible. The ternary keeps the per-call-site
swap maximally local (no function refactor) and
matches the SCHW.5 / PENROSE.6 triple-gate
precedent's per-arm placement.

### 2.4 Legacy fallback

The `: scene.observer.velocity` (CUDA) /
`: optixLaunchParams.observer.velocity` (OptiX)
fallback branch in the ternary above is the
**load-bearing byte-identity anchor**: every
existing CLI action that doesn't engage
`--observer-perception-mode relativistic` falls
into this branch and the kernel reads the
legacy field exactly as today. The pre-OBS-P.1
behaviour is preserved for every non-opt-in
invocation byte-for-byte.

### 2.5 Identity mode behaviour with zero beta

When `perception_mode == Identity` (the default),
the gate returns `false` and the ternary falls
into the legacy fallback branch. The legacy
`observer.velocity` is `(0, 0, 0)` by default
(scene-rest observer); the SR helpers collapse
to identity values (`aberrateDirection` returns
the input direction unchanged; `dopplerFactor`
returns `1`; `searchlightFactor` returns `1`;
`applyDopplerColor` returns the input colour
unchanged). The kernel output is bit-identical
to the pre-OBS-P.1 baseline.

When `perception_mode == Identity` AND the
operator has authored a non-zero
`scene.observer.velocity` (legacy SR path), the
gate returns `false` and the ternary still
reads the legacy non-zero velocity. The pre-
OBS-P.1 behaviour is preserved exactly:
existing scene-author SR observers continue to
work through the legacy code path with no
behaviour change. **The OBS-P.1 migration is
opt-in** — it only takes effect when the
operator passes `--observer-perception-mode
relativistic`.

---

## 3. Default invariant

The implementation slice must preserve two
load-bearing invariants:

### 3.1 Default observer perception mode is no-op

When the operator does not pass
`--observer-perception-mode` (or explicitly
passes `--observer-perception-mode default`),
`cfg.observer.perception_mode` is
`PerceptionMode::Identity`. The OBSERVER.6
adapter at `build_observer_frame_from_camera(...)`
returns `rest_frame()` byte-for-byte on the
Identity path (verified at OBSERVER.7 audit
check #2). The resulting
`observer_frame.beta == (0, 0, 0)` and
`observer_frame.perception_mode == Identity`.

At the kernel, the OBS-P.1 guard sees
`perception_mode == Identity` → falls into
the legacy fallback branch → reads the legacy
`scene.observer.velocity` (or
`optixLaunchParams.observer.velocity` on
OptiX). The kernel-side behaviour is
**bit-identical** to the pre-OBS-P.1 baseline.

### 3.2 Existing default scenes remain byte-identical

Every existing default scene — every
`scenes/*.rrscene` file in the repository
that does NOT author an OBS-P.1-relevant
`--observer-*` configuration — must produce
byte-identical render output to the
pre-OBS-P.1 baseline on every action:

- `--render-pathtrace <scene>`
- `--render-mesh-scene <scene>`
- `--render-material-scene <scene>`
- `--render-direct-lighting <scene>`
- `--render-aovs [<scene>]`
- `--render-optix-aovs [<scene>]`
- `--render-optix-pathtrace <scene>`
- `--render-relativistic`
- `--render-scene`
- every Stage 19E.2+ demo action
- every existing `--render-aovs --denoise`
  invocation

The invariant is structurally guaranteed by §2.4
(the legacy fallback path is preserved exactly).
Runtime SDK-host verification (the seven §3 +
§9 capstone audit checks at OBSERVER.15) will
confirm the byte-identity at the PPM level.

---

## 4. Compatibility

The implementation slice must preserve the
existing surfaces and contracts:

### 4.1 Legacy RelativityParams remains an adapter/config input

`rr::relativity::Observer` + `RelativityParams`
remain valid input types throughout the
renderer. The OBSERVER.6 adapter
`build_observer_frame_from_camera(GpuCamera,
Observer, ObserverConfig)` continues to take
the legacy `Observer` as its second argument
(verified at OBSERVER.7 audit check #1). The
adapter's beta-resolution priority (CLI overlay
> direction-sentinel fallback > legacy
`observer.velocity`) is preserved verbatim per
the OBSERVER.6 doc-comment + the OBSERVER.7
audit check #3.

**The OBS-P.1 migration does NOT remove
`scene.observer.velocity` from the CudaSceneView
/ OptixLaunchParams** — both fields stay on the
launch payload because:

- the **scene-loader path** populates
  `scene.observer.velocity` from the scene
  file's `relativity` block; existing scenes
  with author-supplied SR observer velocities
  continue to load + render exactly as today;
- the **legacy fallback branch** of the OBS-P.1
  guard reads the field directly (§2.4 above);
- the **OBSERVER.6 adapter** consumes the
  field as input and produces an `ObserverFrame`
  whose `.beta` is then carried by the
  observer-frame payload — i.e. the legacy
  field is the **source of truth for the
  scene-author SR observer path**, while
  `observer_frame.beta` is the **source of
  truth for the operator-overlay-driven
  perception path**.

This dual-source design is the documented
OBSERVER.6 beta-resolution priority extended
to the kernel: the operator's
`--observer-perception-mode relativistic` flag
**lifts** the perception transform onto the
new observer-frame; absent the flag, the
legacy path is preserved bit-for-bit.

### 4.2 ObserverFrame becomes the runtime source of truth (when gate is true)

When `perception_mode == ConstantVelocityMinkowski`,
the kernel reads `observer_frame.beta` directly.
This makes the `ObserverFrame` the runtime
source of truth for the perception pipeline on
the opt-in path.

The dispatcher-side
`build_observer_frame_from_camera(...)` invocation
at `main.cpp::run_render_aovs` +
`run_render_optix_aovs` + the path-trace
dispatchers (landed at OBSERVER.8 + OBSERVER.10)
already populates `observer_frame.beta` from
the OBSERVER.6 adapter's three perception-mode
construction paths. After OBS-P.1, the **entire
data path is end-to-end**:

```
CLI / config → ObserverConfig
            → build_observer_frame_from_camera()
            → ObserverFrame (target / pcfg / launch params)
            → kernel-side perception_mode gate
            → kernel reads observer_frame.beta
            → SR helpers consume observer_frame.beta
            → per-pixel aberration / Doppler / searchlight
```

No new ABI surface; no new RelativityParams
field; no new launch-params field; no new
helper function. The OBS-P.1 migration is a
**read-site swap with a per-call-site guard**
plus a one-line `launch_pathtrace_sample(...)`
trailing-arg extension to thread
`observer_frame` to the CUDA path-tracer
kernel.

### 4.3 `RelativityParams` flags remain orthogonal

The existing `RelativityParams` flags
(`enable_aberration` / `enable_doppler` /
`enable_searchlight` / `doppler_color_strength`
/ `searchlight_strength` / `max_beta`) are
preserved verbatim. They continue to gate
their respective SR helpers (the existing
`if (params.enable_aberration) { ... }`
guards inside each SR helper call site stay
unchanged). The OBS-P.1 `perception_mode`
guard is a **new outer layer** that selects
WHICH beta the SR helpers receive; the
existing flags determine WHETHER the helpers
are called at all.

The two gates compose:
- `perception_mode == ConstantVelocityMinkowski`
  AND `params.enable_aberration == true` →
  aberration runs with
  `observer_frame.beta`.
- `perception_mode == ConstantVelocityMinkowski`
  AND `params.enable_aberration == false` →
  aberration skipped (existing
  `RelativityParams` gate wins).
- `perception_mode == Identity` AND
  `params.enable_aberration == true` →
  aberration runs with the legacy
  `scene.observer.velocity` (existing path).
- `perception_mode == Identity` AND
  `params.enable_aberration == false` →
  aberration skipped (existing path).

Same composition for Doppler + searchlight.
The pre-OBS-P.1 behaviour (the second + fourth
cases above) is preserved exactly.

### 4.4 Cross-backend symmetry

CUDA + OptiX must remain semantically aligned
per the OBSERVER.11 audit check #3 contract.
The migration at OBS-P.1 must apply the SAME
guard shape + the SAME beta source ternary
at every parallel call site on both backends:

- CUDA's `k_relativistic_sphere` (C-1) and
  OptiX's `__raygen__pinhole` (O-2) — both
  primary-ray aberration sites.
- CUDA's `k_render_scene` (C-2) and OptiX's
  `__closesthit__radiance` / `__miss__radiance`
  (O-1) — both per-hit Doppler /
  searchlight sites.
- CUDA's `k_pathtrace_sample` (C-3) and
  OptiX's `__raygen__pathtrace` (O-3) — both
  path-tracer perception sites.

The cross-backend math equivalence is
structurally guaranteed by single-source-of-
truth: both backends call the same
`precompute_relativity` /
`aberrateDirection` / `dopplerFactor` /
`searchlightFactor` helpers from
`rr::relativity::RelativityMath.h` (the
header-only `RR_HD inline` math leaf).
Output is byte-equivalent when the input
beta is byte-equivalent — which it IS by
construction because both backends consume
the same `ObserverFrame::beta` from the same
OBSERVER.6 adapter output.

---

## 5. Files likely involved

The implementation slice is expected to touch
the following files. Numbers in parentheses
are rough net-line estimates from comparable
past slices (SCHW.5, PENROSE.6, OBSERVER.13).

| Layer | File | Why |
|-------|------|-----|
| CUDA kernel | `src/cuda/CudaTestKernel.cu` (+15) | Add OBS-P.1 perception-mode guard + ternary at C-1 (`k_relativistic_sphere`) and C-2 (`k_render_scene`). |
| CUDA kernel | `src/cuda/CudaPathTracer.cu` (+10) | Add the same guard + ternary at C-3 (`k_pathtrace_sample`). |
| CUDA launcher | `src/cuda/CudaPathTracer.cuh` (+10) | Extend `launch_pathtrace_sample(...)` with a trailing `observer_frame` parameter (mirrors MANI-I.5's `manifold_mode` precedent). |
| CUDA launcher | `src/cuda/CudaPathTracer.cu` (+5) | Pass the trailing `observer_frame` argument into the kernel launch. |
| PathTracer host | `src/pathtracer/PathTracer.cpp` (+5) | Thread `cfg.observer_frame` into the `launch_pathtrace_sample(...)` call (mirrors the existing `cfg.manifold` thread). |
| OptiX kernel | `src/optix/OptixPrograms.cu` (+30) | Add OBS-P.1 perception-mode guards + ternaries at O-1 (`__miss__radiance`), O-2 (`__raygen__pinhole`), O-3 (`__raygen__pathtrace`). |
| Tests | `tests/relativity_tests.cpp` (+30) | Host-side: extend the existing `precompute_relativity` / `aberrateDirection` / `dopplerFactor` / `searchlightFactor` test surface with assertions on the perception-mode ternary's branch-equivalence — i.e. for the same input beta the kernel reads the same value via either source path. |
| Tests | `tests/manifold_identity_tests.cpp` (+50) | Host-side: extend with assertions on the cross-source equivalence — `precompute_relativity(observer_frame.beta)` from an `ObserverFrame` built via `build_observer_frame_from_camera(...)` with a non-trivial CLI overlay produces the same `gamma` / `length` as `precompute_relativity(scene.observer.velocity)` with the same beta value (single-source-of-truth verification at the host-side; SDK-host kernel verification is deferred per §7 below). |
| Docs | `docs/BUILD_PLAN.md` | OBS-P.1 entry mirroring the OBSERVER.* per-slice entry shape. |
| Docs (optional) | `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §10 (LANDED-update on slice merge) | The plan's §10 OBSERVER.* sub-slice ladder may be extended with the OBS-P.* sub-arc; not required at this slice. |
| CMake | none expected | The migration is a source-edit-only change; the `rr_gpu` / `rr_optix` / `rr_pathtracer` libraries' link graphs are unchanged. |

**Estimated total net-line delta**: ~155
lines, comparable to PENROSE.6 + PENROSE.8
(the two-backend chart-arm migration that
landed at the same scope).

---

## 6. What must not be touched

Per master rule #3 and the operator's OBS-P.1
brief, the implementation slice MUST NOT:

- **No new manifold math.** The OBSERVER.* arc
  did not introduce any new manifold transforms;
  OBS-P.1 explicitly inherits this constraint.
  The existing SCHW.* + PENROSE.* chart arms in
  `CudaTestKernel.cu` / `OptixPrograms.cu`
  remain untouched (the `manifold_coordinates`
  AOV write arms are the only manifold-touching
  kernel sites; they continue to write the
  chart-warped position; they do not need an
  OBS-P.1 guard).
- **No Kerr / Kruskal chart implementation.**
  Those families remain at MANIFOLD.1's
  `*LikePlaceholder` reserved-but-inert state.
  OBS-P.1 scope is the perception-transform
  migration onto the existing two chart
  families (Euclidean + the active chart on
  any opt-in scene).
- **No `CurvedChartGeodesicPlaceholder`
  perception mode activation.** The third
  enumerator stays reserved-but-inert per the
  OBSERVER.1 plan §3.6 + §8 non-goals + the
  OBSERVER.6 adapter's structural passthrough
  contract. OBS-P.1's guard returns `false`
  for this mode (same as `Identity`); the
  legacy fallback branch is taken.
- **No broad renderer refactor.** The
  migration is a per-call-site read swap
  with a per-call-site guard. No function
  signature changes (except the
  `launch_pathtrace_sample(...)` trailing-
  arg extension, which is the documented
  carry-only addition from OBSERVER.8). No
  SR-helper signature changes (the helpers
  continue to take a `Vec3` beta as input).
  No new dispatcher entry points.
- **No new CLI flag.** The OBSERVER.4
  `--observer-*` flag surface is complete;
  OBS-P.1 just makes the
  `--observer-perception-mode relativistic`
  flag (already parsing into
  `cfg.observer.perception_mode ==
  ConstantVelocityMinkowski`) take effect
  at the kernel.
- **No new launch-params field.** The
  OBSERVER.8 + OBSERVER.10 carry-only
  fields (`view.observer_frame` /
  `optixLaunchParams.observer_frame`) are
  the ONLY new launch-params surface; the
  OBS-P.1 migration reads from them but
  does not add new fields.
- **No new AOV.** The OBSERVER.13
  `observer_beta` AOV is the only debug
  visualisation slot; OBS-P.1 reuses it
  unchanged (the AOV still writes
  `observer_frame.beta` regardless of
  `perception_mode`, mirroring the
  read-only debug contract from
  OBSERVER.13).
- **No `RelativityParams` field changes.**
  The existing six flags
  (`enable_aberration` / `enable_doppler`
  / `enable_searchlight` /
  `doppler_color_strength` /
  `searchlight_strength` / `max_beta`)
  are preserved verbatim. No new flag
  added; no existing flag's semantics
  changed.
- **No `.rrscene` schema change.** The
  scene-file `relativity` block continues
  to populate `scene.observer.velocity`
  exactly as today. The
  `--observer-perception-mode` flag is CLI-
  only; no scene-file `observer` block is
  added at this slice (a separate future
  arc may add scene-file authoring).
- **No denoiser integration change.** The
  Stage 19B.4 / 21D denoiser continues to
  consume Beauty / Albedo / Normal only.
  The OBS-P.1 migration may shift the
  Beauty pass output when the operator
  engages
  `--observer-perception-mode relativistic`,
  but the denoiser path is unchanged.
- **No path-tracer fixture / convergence
  test addition.** Convergence-equivalent
  output is structurally guaranteed by
  single-source-of-truth math; runtime
  fixture verification is DEFERRED to a
  SDK host per §7 below.
- **No `BUILD_PLAN.md` historical-record
  rewrite.** Every prior OBSERVER.1-OBSERVER.15
  entry stays as-is.
- **No `MODULE_MAP.md` update.** The
  `**Wired**` promotion lands at the
  OBS-P.* arc capstone audit, not at
  OBS-P.1 itself.
- **No C4D / server / UI / node-editor
  touch.** Operator brief explicitly
  forbids.
- **No alteration of the OBSERVER.1
  plan or the OBSERVER.15 capstone
  audit.** Both preserved verbatim;
  OBS-P.1 consumes the OBSERVER.15
  recommendation as its canonical
  starting point.

---

## 7. PASS criteria

The implementation slice's acceptance gate is
satisfied when ALL of the following hold:

### 7.1 Structural

- [ ] All six identified kernel call sites
      (C-1, C-2, C-3 + O-1, O-2, O-3) carry
      the OBS-P.1 guard + ternary per §2.3.
- [ ] The guard shape at every site uses the
      same `perception_mode ==
      ConstantVelocityMinkowski` comparison
      against the `rr::manifold::PerceptionMode`
      enum.
- [ ] The legacy fallback branch reads from
      `scene.observer.velocity` (CUDA) /
      `optixLaunchParams.observer.velocity`
      (OptiX) at every site — the byte-
      identity anchor for the Identity-mode
      path.
- [ ] The `launch_pathtrace_sample(...)`
      launcher signature is extended with a
      trailing `rr::manifold::ObserverFrame
      observer_frame = {}` parameter
      (mirroring MANI-I.5's `manifold_mode`
      precedent).
- [ ] `src/pathtracer/PathTracer.cpp` threads
      `cfg.observer_frame` into the
      `launch_pathtrace_sample(...)` call.
- [ ] No new RelativityParams field; no new
      launch-params field; no new helper
      function; no new CLI flag; no new ABI
      surface beyond the
      `launch_pathtrace_sample(...)`
      trailing-arg extension.
- [ ] `CurvedChartGeodesicPlaceholder` mode
      falls into the legacy fallback branch
      at every site (the guard returns
      `false` for any mode other than
      `ConstantVelocityMinkowski`).

### 7.2 Behavioural

- [ ] **Default-off byte-identity**: every
      existing CLI action without
      `--observer-perception-mode relativistic`
      produces byte-identical output to the
      pre-OBS-P.1 baseline. Verified by
      `cmp` against a pinned PPM reference
      on a CUDA + OptiX-SDK host (deferred
      runtime check per §7.4 below).
- [ ] **Opt-in convergence-equivalence**:
      with `--observer-perception-mode
      relativistic --observer-beta X
      --observer-direction Y,Z,W` and a
      scene-author SR observer of the same
      effective beta, output is convergence-
      equivalent to the legacy-only invocation
      with `scene.observer.velocity = X *
      normalize(Y,Z,W)`. Verified on the SDK
      host.
- [ ] **Cross-backend semantic alignment**:
      both backends produce convergence-
      equivalent output for the same OBS-P.1-
      driven invocation (verified by `cmp`
      with float tolerance, OR by inspection
      of the cross-backend pixel histogram
      at the SDK-host audit).
- [ ] **`RelativityParams` orthogonality**:
      with
      `--observer-perception-mode relativistic`
      AND a scene that sets
      `RelativityParams::enable_aberration ==
      false`, aberration is skipped (the
      existing flag wins). Same for Doppler
      + searchlight. Verified on the SDK
      host.
- [ ] **No new render output file**: no new
      AOV / PPM is emitted as a side-effect
      of OBS-P.1. The existing seven AOV
      PPMs + the optional eighth
      (`aov_manifold_coordinates.ppm` /
      `aov_observer_beta.ppm`) set is
      unchanged.

### 7.3 Test surface

- [ ] `ctest` reports `12/12 passed` on the
      audit-host build.
- [ ] `manifold_identity_tests` reports its
      pre-OBS-P.1 count + at least 4 new
      assertions covering the host-side
      cross-source beta equivalence: for the
      same operator-CLI-driven beta value,
      both `precompute_relativity(observer_frame.beta)`
      and `precompute_relativity(scene.observer.velocity)`
      produce the same `gamma` / `length` /
      `direction_squared`; the OBSERVER.6
      adapter's beta-resolution priority
      (CLI overlay > zero-direction fallback
      > legacy) reaches a known final beta;
      `is_finite_observer_frame` /
      `is_orthonormal_tetrad` /
      `is_normalised_timelike` all hold on
      the adapter's output through the
      perception-mode mappings.
- [ ] `relativity_tests` reports its
      pre-OBS-P.1 count + at least 2 new
      assertions covering the
      perception-mode ternary's
      branch-equivalence on representative
      input pairs.
- [ ] A standalone `g++ -std=c++20 -Isrc
      -Wall -Wextra -Werror` build of the
      modified host TUs (PathTracer.cpp,
      tests TUs) compiles cleanly.

### 7.4 Runtime SDK-host (DEFERRED)

The audit-host build (no CUDA, no OptiX SDK)
cannot directly verify the kernel-side
guarded reads. The runtime checks below are
DEFERRED behind the audit host's existing
no-CUDA / no-OptiX-SDK fallback, matching the
OBSERVER.9 / OBSERVER.11 / OBSERVER.14 /
SCHW.5 / PENROSE.6 / MANI-CONSUME.1 deferral
pattern.

Each deferred check must be exercised on a
CUDA + OptiX-SDK host before the OBS-P.* arc's
audit slot closes (the slice's per-slice audit
mirrors the OBSERVER.* per-slice audits in
shape):

- [ ] **Default-off byte-identity** (CUDA):
      `--render-scene <default-scene>` /
      `--render-mesh-scene <default-scene>`
      / `--render-material-scene
      <default-scene>` /
      `--render-direct-lighting
      <default-scene>` / `--render-aovs
      [<default-scene>]` /
      `--render-pathtrace <default-scene>`
      / `--render-relativistic` / every
      Stage 19E.2+ demo action — each
      produces PPM output byte-identical to
      the pre-OBS-P.1 reference (pinned
      goldens in `tests/goldens/` if the
      operator authorises pinning).
- [ ] **Default-off byte-identity** (OptiX):
      `--render-optix-aovs
      [<default-scene>]` /
      `--render-optix-pathtrace
      <default-scene>` — each produces PPM
      output byte-identical to the
      pre-OBS-P.1 reference.
- [ ] **Opt-in convergence-equivalence**
      (CUDA): `--render-aovs
      --observer-perception-mode
      relativistic --observer-beta 0.5
      --observer-direction 0,0,-1
      <relativity-fixture-scene>` produces
      convergence-equivalent Beauty PPM to
      the legacy invocation with the same
      scene's authored `betaVelocity = 0.5`
      along -Z.
- [ ] **Opt-in convergence-equivalence**
      (OptiX): the matching
      `--render-optix-aovs ...` invocation
      produces convergence-equivalent
      Beauty PPM to its legacy counterpart.
- [ ] **Cross-backend convergence**: the
      OBS-P.1-driven invocations on both
      backends produce convergence-
      equivalent output (`cmp` with float
      tolerance, OR pixel-histogram
      inspection).
- [ ] **OBSERVER.13 debug AOV unchanged**:
      `--render-aovs --observer-debug
      --observer-perception-mode
      relativistic --observer-beta 0.5
      --observer-direction 1,0,0` produces
      the documented
      `(0.5, 0.0, 0.0)`-at-every-hit-pixel
      AOV (verified at OBSERVER.14 audit
      check #4; the OBS-P.1 migration does
      NOT change the AOV's per-pixel
      write — the AOV writes
      `observer_frame.beta` regardless of
      `perception_mode`).
- [ ] **`RelativityParams` orthogonality**
      (both backends): a scene with
      `params.enable_aberration = false`
      AND `--observer-perception-mode
      relativistic` AND
      `--observer-beta 0.5` produces an
      aberration-free Beauty pass (the
      existing flag wins; observer-frame's
      beta is read but the SR helper is
      skipped per the existing
      `if (params.enable_aberration)` guard).
- [ ] **Path-tracer convergence** at fixed
      spp on a representative scene: with
      `--observer-perception-mode
      relativistic` the accumulated
      radiance converges to the same value
      as the legacy invocation at high spp,
      modulo single-precision rounding.
- [ ] **No new error / warning** on every
      audit-host smoke test (the existing
      log lines fire correctly; no new
      log noise is introduced).

---

## 8. Cross-references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") + #1 ("Build incrementally") + #2
  ("Keep every step compilable") + #5 ("No CPU
  ray tracing as production path") all apply.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3
  Observer Frame + §6 GPU integration strategy
  + §7.2 SR-helper subsumption — defines the
  reinterpretation contract the OBS-P.1
  migration realises (the SR helpers are the
  Minkowski + constant-velocity-frame
  specialisation of the observer-frame
  contract).
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §3,
  §6, §8 non-goals — the OBSERVER.1 plan
  brief that authorised the OBS-P arc's
  scope; OBS-P.1's "no perception
  transform in the legacy default path"
  invariant is the natural completion of
  the OBSERVER.1 plan's "no behaviour
  change by default" non-goal.
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15) §9.1 — the recommended
  next stage that authorises OBS-P.1;
  documents the migration as a "guarded
  read-site swap with no new ABI".
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the adapter audit
  establishing the beta-resolution
  priority that OBS-P.1 reads from
  (verified at audit check #3).
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — the CUDA payload audit
  establishing the carry-only contract
  OBS-P.1 lifts (the
  `view.observer_frame.beta` field is in
  place; this slice reads it).
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11) — the OptiX payload audit
  establishing the same contract on
  OptiX.
- `docs/OBSERVER_DEBUG_AOV_TASK.md`
  (OBSERVER.12) — the precedent task-
  definition doc this OBS-P.1 task
  brief mirrors in structure.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — establishes the
  observer-frame's `beta` reads via
  the new debug AOV; OBS-P.1 lifts
  the same reads onto the production
  perception path.
- `docs/MANIFOLD_DEBUG_AOV_TASK.md`
  (MANI-I.7) — the original
  task-definition template.
- `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md`
  §3 + §5 + §6 — the precedent
  triple-gate kernel-arm guard
  pattern OBS-P.1's perception-mode
  ternary mirrors in shape.
- `src/manifold/ObserverFrame.h` — the
  `PerceptionMode` enum + the seven-
  field `ObserverFrame` POD the
  guard reads.
- `src/manifold/CameraObserverAdapter.h`
  — the OBSERVER.6 adapter producing
  the `ObserverFrame` the guard
  consumes.
- `src/relativity/RelativityMath.h` —
  the SR helpers
  (`precompute_relativity`,
  `aberrateDirection`,
  `dopplerFactor`,
  `searchlightFactor`,
  `applyDopplerColor`) the guard
  feeds. The helpers themselves are
  unchanged.
- `src/relativity/RelativityParams.h`
  — the `Observer` + `RelativityParams`
  types that remain valid inputs.
- `src/cuda/CudaTestKernel.cu`
  (~lines 212, 309) — C-1, C-2
  call sites the OBS-P.1 guard
  wraps.
- `src/cuda/CudaPathTracer.cu`
  (~line 443) — C-3 call site.
- `src/cuda/CudaPathTracer.cuh` —
  the launcher signature OBS-P.1
  extends.
- `src/cuda/CudaScene.cuh` — carries
  the `observer_frame` field the
  CUDA kernel reads.
- `src/pathtracer/PathTracer.cpp` —
  the host bridge that threads
  `cfg.observer_frame` to the
  launcher.
- `src/optix/OptixPrograms.cu`
  (~lines 139, 190, 1034) — O-1,
  O-2, O-3 call sites.
- `src/optix/OptixLaunchParams.h` —
  carries the `observer_frame`
  field the OptiX programs read.
- `tests/relativity_tests.cpp` —
  the existing SR-helper test
  surface OBS-P.1 extends with
  ternary-equivalence assertions.
- `tests/manifold_identity_tests.cpp`
  — the existing manifold + observer
  test surface OBS-P.1 extends with
  cross-source beta-equivalence
  assertions.
- `docs/BUILD_PLAN.md` — the
  OBS-P.1 implementation slice's
  entry lands when the impl
  commits.

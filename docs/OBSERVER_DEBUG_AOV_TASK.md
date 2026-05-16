# Observer Debug AOV — Task Definition (OBSERVER.12)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this task definition; the implementation
        lands in a subsequent slice that consumes this
        doc as its canonical brief.

This document defines the work for **OBSERVER.12 —
observer debug AOV** under the renumbered
`docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7 OBSERVER.12
(renumbered from the original §7 OBSERVER.7 after the
OBSERVER.3 / OBSERVER.5 / OBSERVER.7 / OBSERVER.9 /
OBSERVER.11 audit-slot insertions). It is the
operator-facing brief the implementation slice will
read to decide the exact surface, the acceptance
gates, and the non-goals.

Prerequisite slices already green:

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

Adjacent precedents this task brief mirrors:

- **`docs/MANIFOLD_DEBUG_AOV_TASK.md`** (MANI-I.7) —
  the manifold debug AOV task brief that shipped the
  `ManifoldCoordinates` AOV via the
  `--render-aovs --manifold-debug` two-flag
  composition. OBSERVER.12 mirrors its three-section
  shape (1 AOV recommended; matching CUDA + OptiX
  kernel arms; `--observer-debug` gate parallel to
  `--manifold-debug`).
- **`docs/MANIFOLD_DEBUG_AOV_AUDIT.md`** (MANI-I.9)
  — the per-slice audit doc for MANI-I.8 (the
  manifold debug AOV impl); the OBSERVER.12 impl
  slice's audit will follow this shape.

---

## 1. Exact goal

**Expose ObserverFrame diagnostics as an optional
per-pixel AOV output, gated on a new
`--observer-debug` CLI flag, so an operator can
*see* the resolved observer-frame state the
OBSERVER.8 + OBSERVER.10 launch boundaries carry
into the kernels — without changing beauty
rendering, ray generation, or shading behaviour.**

The new AOV writes a 3-component (Vec3) per-pixel
value to a dedicated render pass. The AOV is
*optional*: it is only allocated and written when
the operator requests it via the two-flag gate
`--render-aovs --observer-debug` (CUDA) /
`--render-optix-aovs --observer-debug` (OptiX). The
beauty pass and every existing AOV (Beauty /
Normal / Depth / Albedo / DopplerFactor /
SearchlightFactor / ManifoldCoordinates) are
byte-identical to the pre-OBSERVER.12 baseline
regardless of whether the new AOV is requested.

On the default `PerceptionMode::Identity` mode
(the documented no-op anchor; produced by every
CLI invocation without `--observer-*` flags) the
AOV's per-pixel value is the documented **neutral
diagnostic value** for the visualisation channel
chosen (see §2 + §3 below) — a flat colour that
confirms "the kernel saw the observer payload and
the payload was the no-op anchor". When the
operator engages a non-default perception mode
(via `--observer-perception-mode relativistic +
--observer-beta ... + --observer-direction ...`),
the AOV's per-pixel value changes to reflect the
non-default payload — a visual confirmation that
the OBSERVER.6 adapter → OBSERVER.8/10 launch
boundary data path reached the kernel intact.

The AOV is a **read-only diagnostic**: the kernel
reads the observer payload (the OBSERVER.10 audit
verified the payload's structural availability)
and writes the read value to the AOV. The kernel
does NOT yet apply any observer-perception
transform (aberration / Doppler / searchlight
re-keying on `observer_frame.beta`); that is a
separate future slice.

---

## 2. Proposed AOVs

The task brief offers three candidate AOV channels.
The implementation slice ships ONE as the MVP
(per the MANI-I.7 precedent that proposed three
manifold channels but shipped only
`ManifoldCoordinates`). The other two are
documented as forward-looking placeholders; their
implementation is gated behind a future
OBSERVER.* slice if the operator wants them.

### 2.1 `observerBeta` (RECOMMENDED for OBSERVER.12)

- **Component count:** 3 floats / pixel (Vec3).
- **Encoding:** per-pixel value =
  `view.observer_frame.beta` (CUDA path) /
  `optixLaunchParams.observer_frame.beta` (OptiX
  path). The Vec3 is written **directly** —
  three floats per pixel encoding the observer's
  3-velocity in c-units.
- **Identity neutral value:** `(0.0, 0.0, 0.0)`
  (the `rest_frame()` anchor; matches the
  OBSERVER.6 adapter's Identity-mode return value
  verbatim).
- **Non-default visualisation:** with
  `--observer-perception-mode relativistic
  --observer-beta 0.5 --observer-direction
  1,0,0`, every pixel writes
  `(0.5, 0.0, 0.0)` — a flat red-tinted colour
  in the PPM (because the encoder maps the X
  component onto the R channel). This confirms
  visually that (a) the kernel saw the
  non-default payload AND (b) the OBSERVER.6
  adapter's CLI → ObserverFrame::beta mapping
  produced the expected `(0.5, 0, 0)` vector.
- **Why recommended:** the most informative
  single channel for "what observer state did
  the kernel actually see". Confirms the
  OBSERVER.6 → OBSERVER.8/10 → kernel data
  path end-to-end. Mirrors the MANI-I.8
  `ManifoldCoordinates` AOV's role
  (per-pixel chart coordinate; the most
  informative single channel for "what is the
  chart doing to this pixel").

### 2.2 `observerDirection` (FUTURE)

- **Component count:** 3 floats / pixel (Vec3).
- **Encoding:** per-pixel value = the normalised
  observer direction
  `normalize(view.observer_frame.beta)` (CUDA) /
  `normalize(optixLaunchParams.observer_frame.beta)`
  (OptiX). When `|beta| > 0`, the direction is
  the unit-length 3-vector along beta; when
  `|beta| == 0` (the Identity default), the
  encoding is `(0, 0, 0)` (the sentinel "no
  direction" anchor; matches the
  `ObserverConfig::direction` doc-comment's
  zero-sentinel convention).
- **Identity neutral value:** `(0.0, 0.0, 0.0)`.
- **Non-default visualisation:** with a
  non-zero beta, the AOV writes a unit-length
  RGB-encoded direction at every pixel.
  Diagnostic for "what direction is the
  observer moving in?" when an artist has
  authored an oblique observer velocity (e.g.
  `--observer-direction 0.6,-0.8,0.0`).
- **Why deferred:** redundant with
  `observerBeta` modulo magnitude — an
  operator can derive `observerDirection` from
  `observerBeta` post-process via
  `observerBeta / |observerBeta|`. Adding a
  separate AOV slot doubles the device-buffer
  cost without doubling the diagnostic value at
  this slice. Ship if the future arc adds
  per-pixel observer state (e.g. moving
  observers across the framebuffer).

### 2.3 `observerPerceptionMode` (FUTURE)

- **Component count:** 1 float / pixel (replicated
  to RGB for PPM compatibility, mirroring the
  existing `Depth` / `DopplerFactor` /
  `SearchlightFactor` 1-channel AOV encoding).
- **Encoding:** per-pixel value =
  `static_cast<float>(view.observer_frame.perception_mode)`
  (CUDA) /
  `static_cast<float>(optixLaunchParams.observer_frame.perception_mode)`
  (OptiX). The enum tag is encoded as a flat
  float per pixel: `Identity = 0.0`,
  `ConstantVelocityMinkowski = 1.0`,
  `CurvedChartGeodesicPlaceholder = 2.0`.
- **Identity neutral value:** `0.0` (the
  `Identity` enumerator's value; matches the
  `default_perception_mode()` factory output).
- **Non-default visualisation:** with
  `--observer-perception-mode relativistic`,
  every pixel writes `1.0` — a flat mid-grey
  in the PPM. With
  `--observer-perception-mode default`, every
  pixel writes `0.0` — flat black. Diagnostic
  for "which perception mode is engaged?".
- **Why deferred:** the perception-mode tag is
  per-launch constant; an operator can read it
  from the existing host-side echo log line
  (`aovs observer config: ...` /
  `optix-aovs observer config: ...` landed at
  OBSERVER.8 + OBSERVER.10) without a per-pixel
  AOV. The log line provides the same
  information at zero device-buffer cost.

### 2.4 Naming convention

The recommended `observerBeta` AOV becomes
**`AOVType::ObserverBeta`** (enumerator value
`= 7`, appended after the MANI-I.8
`ManifoldCoordinates = 6`). The
`aov_type_name(...)` mapping is
**`"observer_beta"`** (snake_case, mirroring
the existing `doppler_factor` /
`searchlight_factor` / `manifold_coordinates`
convention). The factory function is
**`AOV::make_observer_beta(std::string name = {})`**.

The integration plan's OBSERVER.12 box's
informal label may use `ObserverFrameState` or
similar; the canonical name the implementation
slice will use is `ObserverBeta` (matching the
existing `ManifoldCoordinates` PascalCase
convention).

---

## 3. Expected behaviour

The implementation slice must satisfy three
load-bearing behavioural invariants:

### 3.1 Beauty output unchanged

Every existing CLI action — `--render-pathtrace`,
`--render-optix-pathtrace`, `--render-scene`,
`--render-mesh-scene`, `--render-material-scene`,
`--render-direct-lighting`, `--render-aovs`,
`--render-optix-aovs`, `--render-relativistic`,
`--render-aovs --denoise`, `--render-aovs
--manifold-debug`, and every diagnostic render —
produces pixel-bit-identical beauty output to the
pre-OBSERVER.12 baseline regardless of:

- whether the new AOV is requested;
- the operator's choice of `--observer-beta`
  value;
- the operator's choice of `--observer-direction`
  value;
- the operator's choice of `--observer-proper-time`
  value;
- the operator's choice of `--observer-perception-mode`
  value.

The new AOV writes ONLY to its dedicated per-pass
framebuffer. The Beauty pass kernel arithmetic
stays unchanged. The existing seven AOV slots
(Beauty / Normal / Depth / Albedo / DopplerFactor
/ SearchlightFactor / ManifoldCoordinates) write
the same per-pixel values as the pre-OBSERVER.12
baseline.

### 3.2 Debug AOV only active when requested

The new AOV slot is gated on TWO conditions, both
of which must hold for the AOV pass's device
buffer to be allocated and the kernel arm to fire:

1. The operator passes `--render-aovs` (CUDA
   path) OR `--render-optix-aovs` (OptiX path).
2. The operator passes `--observer-debug` — a
   new CLI flag introduced by the OBSERVER.12
   impl slice. Parallel to the existing
   `--manifold-debug` modifier flag (MANI-I.1);
   presence-only switch; no value consumed.

Either gate by itself produces no new file. Both
together cause the renderer to allocate the new
per-pass device buffer, fill it from the kernel,
and save the resulting PPM alongside the existing
six (or seven, when `--manifold-debug` is also
set) AOV PPMs.

A separate dedicated CLI action
(`--render-observer-debug-aov` or similar) is
NOT shipped at OBSERVER.12. The two-flag
composition above is the only entry point. This
mirrors the MANI-I.7 design decision verbatim.

### 3.3 Default observer produces neutral diagnostic values

When the new AOV pass is requested but the
`ObserverConfig` is at its default state
(`perception_mode == Identity`,
`beta_magnitude == 0`, `direction == (0,0,0)`,
`proper_time == 0`), the per-pixel value the AOV
writes is the **neutral diagnostic value** for
the visualisation channel chosen (see §2 above
for per-AOV neutral values). For the recommended
MVP `observerBeta` AOV the neutral value is
`(0.0, 0.0, 0.0)` — every pixel in the saved
PPM is flat black.

The neutral value is documented and verifiable
against a closed-form reference at every pixel:

- For `observerBeta`: the per-pixel value
  equals `view.observer_frame.beta` (CUDA) /
  `optixLaunchParams.observer_frame.beta`
  (OptiX). On the Identity default, that value
  is `(0, 0, 0)` analytically (the OBSERVER.6
  adapter's Identity path returns `rest_frame()`
  byte-for-byte; the OBSERVER.7 audit verified
  this).
- The check is verifiable by `cmp`-ing the AOV
  PPM against a reference image pinned at the
  implementation slice; the reference is
  generated once on a CUDA + OptiX-SDK host and
  lives in the ctest `goldens/` set (matches the
  MANI-I.7 → MANI-I.8 → MANI-I.9 audit-chain
  precedent).

---

## 4. CUDA / OptiX interaction

The implementation slice's kernel-side scope is
strictly **read-only** on the observer payload:

### 4.1 Read observer payload only

- **CUDA path** (`k_render_scene` in
  `src/cuda/CudaTestKernel.cu`): the
  `ManifoldCoordinates` AOV-write arm gets a
  sibling **`ObserverBeta` AOV-write arm** that
  reads `view.observer_frame.beta` (the
  OBSERVER.8 carry-only field) and writes it to
  the `view.aovs.observer_beta` device pointer
  if non-null. The read site uses the existing
  `view.observer_frame` field that landed at
  OBSERVER.8 — no new launch-params field is
  required.
- **OptiX path** (`OptixPrograms.cu`'s
  closest-hit + miss arms): same shape. The
  arm reads
  `optixLaunchParams.observer_frame.beta` (the
  OBSERVER.10 carry-only field) and writes it
  to `optixLaunchParams.aov_observer_beta` if
  non-null. The read uses the existing
  `optixLaunchParams.observer_frame` field that
  landed at OBSERVER.10.

### 4.2 No perception transform yet

The implementation slice MUST NOT engage any
observer-perception transform on the read value.
Specifically:

- The kernel MUST NOT call `aberrateDirection(ray.dir,
  observer_frame.beta)` to compute a boosted
  ray direction. The recommended
  `observerBeta` AOV writes the raw `beta`
  vector verbatim — no aberration, no Lorentz
  boost, no tetrad rotation.
- The kernel MUST NOT call
  `dopplerFactor(...)` /
  `searchlightFactor(...)` on the observer-
  frame's beta. The recommended AOV is
  pre-transform.
- The kernel MUST NOT gate any existing
  SR-helper call site on
  `observer_frame.perception_mode`. The existing
  aberration / Doppler / searchlight pipeline
  continues to feed on `scene.observer.velocity`
  (the legacy SR observer) exactly as today.
- The kernel MUST NOT read `observer_frame.right`
  / `up` / `forward` (the tetrad legs). The
  MVP AOV scope is just `beta`.

### 4.3 Cross-backend math consistency

Both backends MUST use the same read site —
`view.observer_frame.beta` (CUDA) and
`optixLaunchParams.observer_frame.beta` (OptiX)
are byte-equivalent at the launch boundary
because both are populated from the same
`build_observer_frame_from_camera(...)`
adapter output (verified at OBSERVER.7 audit
check #3 + OBSERVER.11 audit check #3). The
cross-backend AOV equivalence is structurally
guaranteed:

- The OBSERVER.6 adapter's output is identical
  across both backends (single-source-of-truth
  POD; same `cfg.observer` input).
- The kernel-side read is a direct field
  access — no per-backend transformation.
- The AOV-write encoding is the same Vec3 →
  3-float layout on both backends.

Therefore the CUDA-side
`aov_observer_beta.ppm` and the OptiX-side
`optix_aov_observer_beta.ppm` must be
pixel-bit-identical for the same input
`cfg.observer`. Runtime verification is
DEFERRED to an SDK-host audit pass per §8 below.

---

## 5. Files likely involved

The implementation slice is expected to touch
the following files (host + CUDA + OptiX).
Numbers in parentheses are rough net-line
estimates from comparable past slices.

| Layer | File | Why |
|-------|------|-----|
| AOV data model | `src/renderer/AOV.h` (+15) | New `AOVType::ObserverBeta` enumerator + `make_observer_beta(...)` factory. |
| AOV data model | `src/renderer/AOV.cpp` (+15) | `aov_component_count` → 3 for the new type; `aov_type_name` → `"observer_beta"`; factory body. |
| CLI parser | `src/core/CommandLine.cpp` (+25) | New `--observer-debug` modifier flag (presence-only; sets `r.config.observer.debug_visualization = true` — see Config field below). Help text entry. |
| CLI config | `src/manifold/ObserverFrame.h` (+10) | New `bool debug_visualization = false` field on `ObserverConfig` (sibling of `perception_mode`). Default `false` preserves the pre-OBSERVER.12 byte-identity for every existing `--render-aovs` invocation. |
| CUDA AOV view | `src/cuda/CudaAOV.cuh` (+5) | New `float* observer_beta = nullptr` slot on `DeviceAOVView` (sibling of `manifold_coordinates`). |
| CUDA renderer | `src/cuda/CudaRenderer.h` (+10) | New `float* observer_beta = nullptr` field on `AOVTargets` (sibling of `manifold_coordinates`). |
| CUDA renderer | `src/cuda/CudaRenderer.cu` (+5) | One-line thread inside `render_scene_with_aovs`: `view.aovs.observer_beta = targets.observer_beta;`. |
| CUDA kernel | `src/cuda/CudaTestKernel.cu` (+20) | Sibling AOV-write arm next to the existing `ManifoldCoordinates` arm. Closest-hit + miss arms gated on `view.aovs.observer_beta != nullptr`. Hit: `view.aovs.observer_beta[idx*3..idx*3+2] = view.observer_frame.beta`. Miss: `(0, 0, 0)`. |
| OptiX launch params | `src/optix/OptixLaunchParams.h` (+10) | New trailing `float* aov_observer_beta = nullptr;` field, with the same null-means-skip doc-comment as the existing AOV slots. |
| OptiX kernel | `src/optix/OptixPrograms.cu` (+20) | Closest-hit + miss arms write to the new pointer when non-null. Hit: `optixLaunchParams.aov_observer_beta[idx*3..idx*3+2] = optixLaunchParams.observer_frame.beta`. Miss: `(0, 0, 0)`. |
| OptiX renderer | `src/optix/OptixRenderer.h` (+10) | New `rr::image::Image observer_beta;` field on `AovResult` (sibling of `manifold_coordinates`). |
| OptiX renderer | `src/optix/OptixRenderer.cpp` (+30) | Allocate the per-pass device buffer when the AOV is requested via the `cfg.observer.debug_visualization` flag; pass the pointer through `OptixLaunchParams`; download + save into `AovResult::observer_beta` at the end of `render_aovs(...)`. |
| CLI dispatcher | `src/main.cpp` (+30) | `run_render_aovs` (CUDA) + `run_render_optix_aovs` (OptiX) honour the `--observer-debug` gate; emit `output/aov_observer_beta.ppm` (CUDA) / `output/optix_aov_observer_beta.ppm` (OptiX) when both `--render-aovs` / `--render-optix-aovs` AND `--observer-debug` hold. |
| Tests | `tests/renderer_tests.cpp` (+30) | Host-side: assertion that `AOVType::ObserverBeta` has `component_count == 3` and `name == "observer_beta"`; assertion that `make_observer_beta` produces a valid `AOV`. |
| Tests | `tests/cli_tests.cpp` (+30) | Host-side: assertions covering the new `--observer-debug` flag (default off; presence-only flips the bit; combines with `--render-aovs` / `--render-optix-aovs` cleanly; default-off across N non-observer argv vectors mirroring the existing OBSERVER.4 `test_observer_default_off_with_other_flags` pattern). |
| Fixture | `scenes/test_observer_frame.rrscene` (~80 line scene) | A small scene with a non-trivial observer velocity authored via the existing `relativity` block (OR a new `observer` block — see §6 non-goal). Used at the OBSERVER.12 audit slot to verify the AOV's non-default visualisation matches the documented golden. |
| Companion doc | `docs/OBSERVER_FRAME_FIXTURE.md` (~150 lines) | Per the OBSERVER.1 plan §7 wording — documents the fixture scene's expected visual signature (the per-pixel beta colour map, the expected PPM pixel-row counts, the SDK-host golden-pin procedure). |
| Docs | `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7 OBSERVER.12 (LANDED-update on slice merge) | Optional rewrite with the actual landed surface (mirrors what MANI-I.5 did to the integration plan §6). |
| Docs | `docs/BUILD_PLAN.md` | OBSERVER.12 entry. |
| CMake | none expected | The new AOV uses the existing `rr_renderer` library wiring; `rr_optix` / `rr_gpu` already link `rr_manifold` since MANI-I.5; the new field on `ObserverConfig` lives in the already-included `manifold/ObserverFrame.h`. |

---

## 6. What must not be touched

Per master rule #3 and the operator's OBSERVER.12
brief, the implementation slice MUST NOT:

- **Modify the Beauty pass kernel arithmetic.**
  The existing closest-hit / miss / raygen
  programs' shading code paths stay unchanged.
  The new AOV write is gated behind a
  `if (view.aovs.observer_beta != nullptr)`
  check (CUDA) /
  `if (optixLaunchParams.aov_observer_beta != nullptr)`
  check (OptiX) that the existing kernel arms
  already use for the seven pre-existing AOV
  slots.
- **Modify the existing seven AOV slots'
  layouts** or their `AOVType` enumerator
  values. The new enumerator MUST be appended
  at the END of the `AOVType` enum (value `= 7`,
  after `ManifoldCoordinates = 6`) to preserve
  every pre-OBSERVER.12 value. The existing
  seven slots' kernel write paths stay
  bit-identical.
- **Apply any observer-perception transform.**
  The operator brief is explicit: "read
  observer payload only; no perception
  transform yet". The kernel MUST NOT call
  `aberrateDirection` / `dopplerFactor` /
  `searchlightFactor` on
  `observer_frame.beta`. The existing
  aberration / Doppler / searchlight pipeline
  continues to feed on `scene.observer.velocity`
  (the legacy SR observer) exactly as today.
- **Gate any non-AOV kernel call site on
  `observer_frame.perception_mode`.** The
  perception-mode tag is read-and-write-AOV-
  only this slice. The existing six scene-aware
  actions (`--render-pathtrace`,
  `--render-mesh-scene`, `--render-material-scene`,
  `--render-direct-lighting`, `--render-aovs`,
  `--render-optix-aovs`) continue to ignore
  the field for their non-debug-AOV paths.
- **Add per-pixel observer state.** The
  `ObserverFrame` is per-launch (set once at
  the dispatcher). The MVP `observerBeta` AOV
  writes the same value at every pixel within
  a launch. A future slice may introduce
  per-pixel observer state (moving observers
  across the framebuffer); not this slice.
- **Touch the `.rrscene` scene-file format.**
  No parser change, no writer change, no schema
  bump. The new AOV is request-gated by CLI
  flags only. A future slice may add an
  `observer` block to `.rrscene` for scene-
  authoring; not this slice.
- **Touch `src/server/`, `bridges/`, or
  `tools/`.** No C4D / server / UI /
  node-editor surface change.
- **Add a new `--render-*` action.** The
  two-flag composition (`--render-aovs
  --observer-debug` / `--render-optix-aovs
  --observer-debug`) is the entry point. A new
  action would create CLI surface duplication
  (mirrors the MANI-I.7 design decision).
- **Modify the OptiX denoiser path.** The
  denoiser consumes Beauty / Albedo / Normal
  only; it must continue to do so. The new
  AOV slot is denoiser-ignored.
- **Modify `OptixLaunchParams` field offsets
  that predate OBSERVER.10.** The new
  `aov_observer_beta` pointer field is
  appended at the END of the POD (immediately
  after `aov_manifold_coordinates`).
- **Change the existing `--render-aovs` /
  `--render-optix-aovs` PPM filenames or the
  existing `output/aov_*.ppm` /
  `output/optix_aov_*.ppm` set's
  enumeration.** The new file is *additional*,
  not a replacement.
- **Ship `observerDirection` or
  `observerPerceptionMode`.** §2 above lists
  these as FUTURE; OBSERVER.12 ships only
  `observerBeta`. The other two require their
  own task brief + audit gate if/when the
  operator wants them.
- **Engage the kernel-side OBSERVER.* arc's
  full migration.** The OBSERVER.10 audit's
  forward-looking note ("a future slice will
  gate kernel-side reads on the
  perception_mode tag") still applies; the
  full migration is a separate non-debug-AOV
  slice. OBSERVER.12 is scoped to the
  diagnostic AOV ONLY.

---

## 7. PASS criteria

The implementation slice's acceptance gate is
satisfied when ALL of the following hold:

### 7.1 Structural

- [ ] `AOVType::ObserverBeta` enumerator exists
      at the end of the `AOVType` enum (value
      `= 7`).
- [ ] `aov_component_count(AOVType::ObserverBeta)
      == 3`.
- [ ] `aov_type_name(AOVType::ObserverBeta) ==
      "observer_beta"`.
- [ ] `AOV::make_observer_beta(...)` factory
      exists and produces a well-formed `AOV`
      with `type() == ObserverBeta` and
      `name() == "observer_beta"` (or the
      caller-supplied name).
- [ ] `ObserverConfig::debug_visualization`
      `bool` field exists; default `false`.
- [ ] `--observer-debug` CLI flag parses
      cleanly (presence-only; no value
      consumed); flips
      `r.config.observer.debug_visualization`
      to `true`.
- [ ] `--help` includes the new
      `--observer-debug` flag entry.
- [ ] `DeviceAOVView::observer_beta = nullptr`
      slot exists (CUDA).
- [ ] `AOVTargets::observer_beta = nullptr`
      field exists (CUDA).
- [ ] `OptixLaunchParams::aov_observer_beta =
      nullptr` field exists at the end of the
      POD.
- [ ] OptiX device-side programs
      (`__closesthit__` / `__miss__` for the
      AOV-aware ray types) gate writes on
      `aov_observer_beta != nullptr`.
- [ ] CUDA `CudaTestKernel.cu` AOV-aware
      kernels do the same (closest-hit + miss
      arms gated).
- [ ] `--render-aovs --observer-debug` emits
      `output/aov_observer_beta.ppm` (CUDA
      path) and `--render-optix-aovs
      --observer-debug` emits
      `output/optix_aov_observer_beta.ppm`
      (OptiX path) alongside the existing
      AOV PPMs.

### 7.2 Behavioural

- [ ] `--render-aovs` / `--render-optix-aovs`
      **without** `--observer-debug` emits
      exactly the same AOV PPM set it emitted
      pre-OBSERVER.12 (no new file, no missing
      file, no changed file).
- [ ] Beauty output of every existing CLI
      action is pixel-bit-identical to the
      pre-OBSERVER.12 baseline.
- [ ] The existing seven AOV PPMs (Beauty /
      Normal / Depth / Albedo / DopplerFactor
      / SearchlightFactor / ManifoldCoordinates
      when applicable) are pixel-bit-identical
      to the pre-OBSERVER.12 baseline for every
      existing `--render-aovs` /
      `--render-optix-aovs` invocation.
- [ ] On the default Identity perception mode
      (`cfg.observer.perception_mode ==
      Identity`), `aov_observer_beta.ppm`'s
      per-pixel value matches `(0, 0, 0)` to
      within `1.0e-5f` per channel for at
      least 99% of pixels (the remaining 1%
      tolerance covers hit-misses at
      edge-of-frame anti-aliasing; the value
      is structurally `(0, 0, 0)` so any
      non-zero pixel is a hard fail).
- [ ] On `--observer-perception-mode
      relativistic --observer-beta 0.5
      --observer-direction 1,0,0`, every hit
      pixel writes `(0.5, 0.0, 0.0)` to within
      `1.0e-5f` per channel. Miss pixels write
      `(0, 0, 0)`.
- [ ] On `--observer-perception-mode
      relativistic --observer-beta 0.5
      --observer-direction 1,0,0`, the
      `output/aov_observer_beta.ppm` (CUDA) and
      `output/optix_aov_observer_beta.ppm`
      (OptiX) files are byte-identical (single-
      source-of-truth math: both backends read
      the same `view.observer_frame.beta` /
      `optixLaunchParams.observer_frame.beta`
      field which was populated from the same
      `build_observer_frame_from_camera(...)`
      adapter output).

### 7.3 Test surface

- [ ] `ctest` reports `12/12 passed` on the
      audit-host build.
- [ ] `cli_tests` reports its pre-OBSERVER.12
      count + at least N new assertions
      covering: `--observer-debug` flag
      presence (default off, flag presence
      flips bit, combines with other flags,
      missing-value semantics N/A because
      presence-only).
- [ ] `renderer_tests` reports its
      pre-OBSERVER.12 assertion count + at
      least 4 new OBSERVER.12 assertions
      covering: enum value, `aov_component_count`,
      `aov_type_name`, factory output.
- [ ] A `g++ -std=c++20 -Isrc -Wall -Wextra
      -Werror` standalone build of an
      `AOV::make_observer_beta`-consuming TU
      compiles cleanly.

### 7.4 Documentation

- [ ] `docs/BUILD_PLAN.md` OBSERVER.12 entry
      added (mirrors the existing OBSERVER.*
      entries' "What ships / What does NOT
      ship / Acceptance / Module status
      changes" rubric).
- [ ] `docs/OBSERVER_FRAME_FIXTURE.md`
      companion doc added per §5 above (~150
      lines documenting the fixture scene's
      expected visual signature).
- [ ] `scenes/test_observer_frame.rrscene`
      fixture file added per §5 above.
- [ ] OPTIONAL: `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
      §7 OBSERVER.12 entry rewritten with the
      landed-surface description (mirrors what
      MANI-I.5 did to the integration plan §6).

---

## 8. Runtime-deferred CUDA / OptiX checks

The audit-host build (no CUDA, no OptiX SDK)
cannot directly verify the AOV's pixel content.
The runtime checks below are DEFERRED behind the
audit host's existing no-CUDA / no-OptiX-SDK
fallback, matching the existing MANI-I.7 /
SCHW.5 / PENROSE.6 / OBSERVER.8 / OBSERVER.10
deferral pattern (per
`docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md` §2 check
#8 / `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md` §2
check #9 rubric).

Each deferred check must be exercised on a CUDA +
OptiX-SDK host before the OBSERVER.13 arc
capstone audit closes the OBSERVER.* programme:

### 8.1 Neutral diagnostic on the Identity default (CUDA path)

Run:
```
RelativityRender --render-aovs --observer-debug
                 scenes/test_observer_frame.rrscene
```

Verify:
- `output/aov_observer_beta.ppm` exists.
- For every hit pixel `(x, y)`, the pixel's
  RGB value (decoded from the PPM's encoding)
  matches `(0.0, 0.0, 0.0)` within `1.0e-5f`
  per channel — because the fixture scene
  authors a default `cfg.observer.perception_mode
  == Identity` (or the fixture uses the
  scene-file `observer` block once it lands;
  not in scope for OBSERVER.12).
- `output/aov_beauty.ppm` is byte-identical to
  the pre-OBSERVER.12 reference (a pinned PPM
  in `tests/goldens/`).

### 8.2 Neutral diagnostic on the Identity default (OptiX path)

Run:
```
RelativityRender --render-optix-aovs --observer-debug
                 scenes/test_observer_frame.rrscene
```

Verify:
- `output/optix_aov_observer_beta.ppm` exists.
- The CUDA-side and OptiX-side
  `aov_observer_beta.ppm` files are
  pixel-bit-identical (the
  `view.observer_frame.beta` /
  `optixLaunchParams.observer_frame.beta`
  fields are populated by the same
  `build_observer_frame_from_camera(...)`
  adapter on both backends).
- `output/optix_aov_beauty.ppm` is byte-
  identical to the pre-OBSERVER.12 reference.

### 8.3 Non-default visualisation (both backends)

Run:
```
RelativityRender --render-aovs --observer-debug
                 --observer-perception-mode relativistic
                 --observer-beta 0.5
                 --observer-direction 1,0,0
                 scenes/test_observer_frame.rrscene
```
AND the same with `--render-optix-aovs`.

Verify:
- Both CUDA and OptiX
  `aov_observer_beta.ppm` files contain hit
  pixels whose RGB value decodes to
  `(0.5, 0.0, 0.0)` within `1.0e-5f` per
  channel.
- Miss pixels write `(0, 0, 0)`.
- The two backends' output files are
  pixel-bit-identical.

### 8.4 Off-path bit-identity (both backends)

Run:
```
RelativityRender --render-aovs
                 scenes/test_observer_frame.rrscene
```
(WITHOUT `--observer-debug`)

Verify:
- Exactly six (or seven, when
  `--manifold-debug` is set) AOV PPMs are
  produced; no `observer_beta.ppm` is
  emitted.
- All PPMs are byte-identical to the
  pre-OBSERVER.12 reference.

### 8.5 Composability with `--manifold-debug`

Run:
```
RelativityRender --render-aovs --observer-debug
                 --manifold-debug
                 --manifold-enable
                 --manifold-chart schwarzschild-like
                 --manifold-strength 0.5
                 scenes/test_schwarzschild_like_manifold.rrscene
```

Verify:
- Both `output/aov_manifold_coordinates.ppm`
  AND `output/aov_observer_beta.ppm` are
  emitted (the two debug-AOV gates are
  orthogonal; both can be active at the same
  time).
- The Beauty / Normal / Depth / Albedo /
  DopplerFactor / SearchlightFactor PPMs are
  byte-identical to the pre-OBSERVER.12
  baseline.

### 8.6 Cross-backend equivalence

Run the §8.3 invocation on both backends.
Compare `cmp output/aov_observer_beta.ppm
output/optix_aov_observer_beta.ppm` — exit
status MUST be `0` (byte-identical).

This is the structural cross-backend
equivalence check that the OBSERVER.11 audit's
check #3 anticipates. The check passes by
construction because (a) both backends consume
the same `rr::manifold::ObserverFrame` POD,
(b) both backends invoke the same
`build_observer_frame_from_camera(...)`
adapter with byte-identical arguments, (c)
the AOV write is a direct field copy with no
backend-specific arithmetic.

---

## 9. Cross-references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") + #1 ("Build incrementally") apply.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §3.3 Observer Frame — defines the
  ObserverFrame contract the AOV reads.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §6,
  §7 OBSERVER.12 (renumbered from §7
  OBSERVER.7 after the OBSERVER.3 / OBSERVER.5
  / OBSERVER.7 / OBSERVER.9 / OBSERVER.11
  audit-slot insertions) — the OBSERVER.1
  plan brief that authorised the debug AOV.
- `docs/MANIFOLD_DEBUG_AOV_TASK.md` (MANI-I.7)
  — the precedent task brief this OBSERVER.12
  task brief mirrors verbatim in structure.
- `docs/MANIFOLD_DEBUG_AOV_AUDIT.md` (MANI-I.9)
  — the per-slice audit doc for the precedent
  manifold-debug-AOV impl; the OBSERVER.12
  impl slice's audit will follow this shape.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the `ObserverFrame` POD's
  structural audit; carry-forward of the
  field semantics the AOV reads.
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5) — the upstream CLI bridge's
  audit; the OBSERVER.12 new
  `--observer-debug` flag extends this CLI
  surface.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the adapter's audit;
  carry-forward of the three-mode
  construction guarantee. OBSERVER.12 ships
  an AOV that visualises the adapter's
  output without engaging any perception
  transform.
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — the CUDA-side payload audit;
  the OBSERVER.12 CUDA kernel arm reads from
  `view.observer_frame.beta` (the audited
  field).
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11) — the OptiX-side payload
  audit; the OBSERVER.12 OptiX kernel arm
  reads from `optixLaunchParams.observer_frame.beta`
  (the audited field).
- `src/renderer/AOV.h` / `AOV.cpp` — the AOV
  data-model surface the new
  `AOVType::ObserverBeta` enumerator + the
  `make_observer_beta(...)` factory extend.
- `src/manifold/ObserverFrame.h` — the
  `ObserverConfig` POD that the new
  `debug_visualization` bool field extends;
  the `ObserverFrame` POD whose `beta` field
  the kernel reads.
- `src/cuda/CudaAOV.cuh` /
  `src/cuda/CudaRenderer.h` /
  `src/cuda/CudaRenderer.cu` /
  `src/cuda/CudaTestKernel.cu` — the
  CUDA-side surface the new AOV slot
  extends.
- `src/optix/OptixLaunchParams.h` /
  `src/optix/OptixRenderer.h` /
  `src/optix/OptixRenderer.cpp` /
  `src/optix/OptixPrograms.cu` — the
  OptiX-side surface the new AOV slot
  extends.
- `src/main.cpp` — the dispatchers
  (`run_render_aovs` + `run_render_optix_aovs`)
  the new `--observer-debug` gate threads
  through; the new PPM save sites the AOV
  emits to.
- `scenes/test_schwarzschild_like_manifold.rrscene`
  — the existing MANI-I.7 / SCHW.9 precedent
  fixture; the OBSERVER.12 fixture scene
  `scenes/test_observer_frame.rrscene` may
  reuse the same overall scene-file shape.
- `docs/BUILD_PLAN.md` — the OBSERVER.12 entry
  will land alongside the impl slice.

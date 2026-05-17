# Scalar Field Diagnostic AOV — Task Definition (FIELD-I.6)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this task definition; the implementation
        lands in a subsequent slice (the renumbered
        FIELD-I.7) that consumes this doc as its
        canonical brief.

This document defines the work for **FIELD-I.6 — scalar
field diagnostic AOV task** under the FIELD-I.* arc
(authorised by the FIELD-I.1 plan §2.3 / §5; renumbered
ladder slot per the FIELD-I.5 audit's §4.1). It is the
operator-facing brief the implementation slice will
read to decide the exact surface, the acceptance
gates, and the non-goals.

This task brief is the FIELD-I.* analogue of the
MANI-I.7 `MANIFOLD_DEBUG_AOV_TASK.md` + OBSERVER.12
`OBSERVER_DEBUG_AOV_TASK.md` precedents: a scope-locked
brief that ships **one diagnostic AOV**, gated behind a
new presence-only CLI modifier, without changing beauty
rendering or any existing kernel arithmetic.

Prerequisite slices already green:

- **FIELD.1** — Field skeleton (`src/field/` headers
  with `FieldType` / `ConstantScalarField` /
  `SampledScalarField` / `FieldMapping` /
  `FieldInterpreter` PODs).
- **FIELD.2** — ScalarField promotion (the existing
  `ConstantScalarField` / `SampledScalarField`
  PODs).
- **FIELD.3** — FieldMapping promotion (the multi-
  channel `FieldMapping` POD on
  `src/field/FieldMapping.h`).
- **FIELD-I.1** — Field Interpretation Phase 1 plan
  (`4b0d482`).
- **FIELD-I.2** — Scalar field model (`40c387b`;
  added `ScalarFieldKind` enum +
  `ScalarFieldConfig` tagged-union POD +
  `evaluate(...)` / `disabled_scalar_field_config()`
  helpers).
- **FIELD-I.3** — Scalar field model audit
  (`3df70d9`).
- **FIELD-I.4** — Field mapping config (`683a16d`;
  added `FieldMappingTarget` enum +
  `FieldMappingConfig` POD +
  `evaluate_mapping(...)` /
  `disabled_field_mapping_config()` helpers).
- **FIELD-I.5** — Field mapping config audit
  (`ba79e6e`).

Adjacent precedents this task brief mirrors:

- **`docs/MANIFOLD_DEBUG_AOV_TASK.md`** (MANI-I.7) —
  the manifold debug AOV task brief that shipped the
  `ManifoldCoordinates = 6` AOV via the
  `--render-aovs --manifold-debug` two-flag
  composition. FIELD-I.6 mirrors its three-section
  shape (1 MVP AOV; matching CUDA + OptiX kernel
  arms; new presence-only `--field-debug` gate
  parallel to `--manifold-debug` / `--observer-debug`).
- **`docs/OBSERVER_DEBUG_AOV_TASK.md`** (OBSERVER.12)
  — the observer debug AOV task brief that shipped
  the `ObserverBeta = 7` AOV via the same two-flag
  composition. FIELD-I.6 mirrors its 8-section
  layout verbatim.
- **`docs/MANIFOLD_DEBUG_AOV_AUDIT.md`** (MANI-I.9)
  + **`docs/OBSERVER_DEBUG_AOV_AUDIT.md`**
  (OBSERVER.14) — the precedent per-slice audits
  the FIELD-I.7 impl slice's audit (the renumbered
  FIELD-I.9 audit slot) will follow.

---

## 1. Exact goal

**Expose the per-pixel raw scalar-field sample
(`evaluate(ScalarFieldConfig, hit_position)`) as an
optional diagnostic AOV output, gated on a new
`--field-debug` CLI flag, so an operator can *see*
the scalar field's spatial pattern across the
framebuffer — without changing beauty rendering, ray
generation, shading behaviour, or any existing AOV
output.**

The new AOV writes a 1-component (single float)
per-pixel value to a dedicated render pass. The AOV
is *optional*: it is only allocated and written when
the operator requests it via the two-flag gate
`--render-aovs --field-debug` (CUDA) /
`--render-optix-aovs --field-debug` (OptiX). The
beauty pass and every existing AOV (Beauty / Normal
/ Depth / Albedo / DopplerFactor / SearchlightFactor
/ ManifoldCoordinates / ObserverBeta) are
byte-identical to the pre-FIELD-I.7 baseline
regardless of whether the new AOV is requested.

On the default `ScalarFieldConfig{enabled = false}`
state (the documented no-op anchor; produced by
every CLI invocation without `--field-*` flags) the
AOV's per-pixel value is `0.0` at every pixel — a
flat black PPM. This is the documented "field-
disabled → neutral / zero diagnostic" anchor.

When the operator engages a non-trivial field (via
the future `--field-enable --field-kind <kind>
--field-strength <value> ...` CLI surface; the
exact CLI shape lands at the same impl slice — see
§5 below), the AOV's per-pixel value reflects the
field's spatial pattern at the per-pixel hit
position. A radial field centred at the world
origin with `min_radius = 1.0`, `max_radius = 5.0`,
`min_value = 0.0`, `max_value = 1.0`, `strength =
1.0` produces a smoothstep falloff visible in the
AOV PPM: black at the centre, white at radius ≥ 5,
gradient in between.

The AOV is a **read-only diagnostic**: the kernel
reads the per-pixel hit position, samples the
`ScalarFieldConfig` payload via the FIELD-I.2
`evaluate(...)` helper, and writes the raw scalar
value to the AOV. The kernel does NOT apply any
`FieldMappingConfig` (FIELD-I.4) transform — no
strength multiplication, no bias, no clamp, no
target-channel routing. The AOV writes the RAW
field sample; the future `FieldMappingConfig` →
beauty-modulation pipeline is a separate later
slice.

---

## 2. Proposed AOV

The implementation slice ships ONE AOV channel
(`fieldScalar`); no alternatives are enumerated as
candidates this brief. This mirrors the operator's
brief — "Proposed AOV: fieldScalar" — which gives a
single channel name and constrains the
visualisation to optional grayscale / normalised.

### 2.1 `fieldScalar` (the FIELD-I.6 MVP)

- **Component count:** 1 float / pixel (single-
  channel scalar). Mirrors the existing `Depth =
  2` / `DopplerFactor = 4` / `SearchlightFactor =
  5` 1-channel AOV encoding precedent. The PPM
  encoder replicates the single float to RGB at
  save time (mirrors the existing 1-channel AOV
  save paths verbatim).
- **Encoding:** per-pixel value =
  `evaluate(view.scalar_field_config,
  hit_position)` (CUDA path) /
  `evaluate(optixLaunchParams.scalar_field_config,
  hit_position)` (OptiX path). Both backends
  consume the same `evaluate(ScalarFieldConfig,
  Vec3) → float` helper from
  `src/field/ScalarField.h` (RR_HD inline; single-
  source-of-truth math leaf — see §4 below for
  the cross-backend equivalence argument).
- **Disabled-field neutral value:** `0.0` (the
  `disabled_scalar_field_config()` factory's
  `evaluate(...)` output at every position; the
  FIELD-I.2 default `ScalarFieldConfig{enabled =
  false, strength = 0.0f}` short-circuits the
  evaluator to 0). Empirically verified at the
  FIELD-I.3 audit's check #2 (3-layer no-op
  anchor).
- **Non-default visualisation (illustrative):** with
  the future `--field-enable --field-kind radial
  --field-min-radius 1.0 --field-max-radius 5.0
  --field-strength 1.0` (the exact CLI surface
  lands at the same impl slice — see §5), every hit
  pixel writes the smoothstep-interpolated value
  at the pixel's world-space hit position. Pixels
  inside the inner sphere (`|hit_pos| ≤ 1.0`)
  write `0.0`; pixels outside the outer sphere
  (`|hit_pos| ≥ 5.0`) write `1.0`; pixels between
  write the smoothstep cubic between. Miss pixels
  write `0.0`.
- **Grayscale / normalised visualisation:** the
  raw scalar is written verbatim to the AOV
  device buffer. The PPM encoder applies the
  standard `[0, 1] → [0, 255]` linear encoding
  (mirrors the existing `DopplerFactor` /
  `SearchlightFactor` encoding precedent); values
  outside `[0, 1]` are clamped at encode time per
  the existing PPM 8-bit encoder. No tone-
  mapping, no normalisation, no per-frame
  rescaling — the AOV is the raw `evaluate(...)`
  output, with the operator authoring the field's
  `min_value` / `max_value` to fit the `[0, 1]`
  grayscale window.
- **Why this is the only proposed channel:** the
  operator's brief explicitly names exactly one
  AOV (`fieldScalar`). Unlike OBSERVER.12 (where
  three AOV alternatives were enumerated with
  one MVP recommended), the FIELD-I.6 brief is
  scope-locked to a single channel. Future
  FIELD-I.* slices may add multi-channel field
  AOVs (vector / tensor / chromatic-shift
  diagnostic) — see §6 for the deferred surface
  list — but the FIELD-I.6 task ships only the
  one MVP.

### 2.2 Naming convention

The MVP AOV becomes **`AOVType::FieldScalar`**
(enumerator value `= 8`, appended at the END of
the `AOVType` enum after the OBSERVER.13
`ObserverBeta = 7`). The `aov_type_name(...)`
mapping is **`"field_scalar"`** (snake_case,
mirroring the existing `doppler_factor` /
`searchlight_factor` / `manifold_coordinates` /
`observer_beta` convention). The factory function
is **`AOV::make_field_scalar(std::string name =
{})`**.

The integration plan's FIELD-I.6 box's informal
label uses `fieldScalar` (camelCase, matching the
operator's brief); the canonical C++ identifier
the impl slice uses is `FieldScalar` (matching
the existing `ManifoldCoordinates` / `ObserverBeta`
PascalCase enumerator-naming precedent).

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
--manifold-debug`, `--render-aovs --observer-debug`,
and every diagnostic render — produces pixel-bit-
identical beauty output to the pre-FIELD-I.7
baseline regardless of:

- whether the new AOV is requested;
- the operator's choice of (future) `--field-*`
  flag values;
- the operator's choice of `--manifold-*` /
  `--observer-*` flag values.

The new AOV writes ONLY to its dedicated per-pass
framebuffer. The Beauty pass kernel arithmetic stays
unchanged. The existing EIGHT AOV slots (Beauty /
Normal / Depth / Albedo / DopplerFactor /
SearchlightFactor / ManifoldCoordinates /
ObserverBeta) write the same per-pixel values as
the pre-FIELD-I.7 baseline.

### 3.2 Default field disabled = neutral / zero diagnostic

When the new AOV pass is requested but the
`ScalarFieldConfig` is at its default state
(`enabled == false`, `strength == 0.0f`,
`kind == Constant`, all other defaults), the
per-pixel value the AOV writes is **`0.0`** at
every pixel (hit + miss). The saved PPM is flat
black.

The neutral value is documented and verifiable
against a closed-form reference at every pixel:

- For `fieldScalar`: the per-pixel value equals
  `evaluate(view.scalar_field_config, hit_pos)`
  (CUDA) / `evaluate(optixLaunchParams.scalar_field_config,
  hit_pos)` (OptiX). On the disabled-field
  default, the evaluator's `enabled = false`
  short-circuit returns `0.0f` regardless of
  position (verified at FIELD-I.3 audit's check
  #2 via `test_evaluate_disabled_returns_zero`
  + `test_evaluate_enabled_but_zero_strength_returns_zero`).
- The check is verifiable by `cmp`-ing the AOV
  PPM against a pre-generated all-black reference
  pinned at the implementation slice (mirrors the
  OBSERVER.13 + MANI-I.8 audit-chain precedent).

### 3.3 Debug AOV only generated when requested

The new AOV slot is gated on TWO conditions, both
of which must hold for the AOV pass's device
buffer to be allocated and the kernel arm to fire:

1. The operator passes `--render-aovs` (CUDA
   path) OR `--render-optix-aovs` (OptiX path).
2. The operator passes `--field-debug` — a new
   CLI flag introduced by the FIELD-I.7 impl
   slice. Parallel to the existing
   `--manifold-debug` (MANI-I.7) /
   `--observer-debug` (OBSERVER.12) modifier
   flags; presence-only switch; no value
   consumed.

Either gate by itself produces no new file. Both
together cause the renderer to allocate the new
per-pass device buffer, fill it from the kernel,
and save the resulting PPM alongside the existing
six (or seven / eight, when `--manifold-debug` /
`--observer-debug` are also set) AOV PPMs.

A separate dedicated CLI action
(`--render-field-debug-aov` or similar) is NOT
shipped at FIELD-I.7. The two-flag composition
above is the only entry point. This mirrors the
MANI-I.7 + OBSERVER.12 design decisions verbatim.

---

## 4. CUDA / OptiX interaction

The implementation slice's kernel-side scope is
strictly **read-only** on the field config payload:

### 4.1 Read scalar field config only

- **CUDA path** (`k_render_scene` in
  `src/cuda/CudaTestKernel.cu` + any sibling
  AOV-aware kernel arms): the `ManifoldCoordinates`
  + `ObserverBeta` AOV-write arms get a sibling
  **`FieldScalar` AOV-write arm** that reads
  `view.scalar_field_config` (a new launch-payload
  field on `CudaSceneView`) and computes
  `evaluate(view.scalar_field_config, hit_pos)`,
  writing the result to the
  `view.aovs.field_scalar` device pointer if
  non-null. The read uses the new
  `view.scalar_field_config` field (a new POD
  field on `CudaSceneView` parallel to the
  MANI-I.8 `view.manifold_mode` + OBSERVER.8
  `view.observer_frame` payload-field
  precedents).
- **OptiX path** (`OptixPrograms.cu`'s
  closest-hit + miss arms): same shape. The
  arm reads `optixLaunchParams.scalar_field_config`
  (a new POD field on `OptixLaunchParams` parallel
  to OBSERVER.10's `observer_frame` payload-field
  precedent), computes
  `evaluate(optixLaunchParams.scalar_field_config,
  hit_pos)`, and writes the result to
  `optixLaunchParams.aov_field_scalar` if non-null.

### 4.2 No field-to-beauty mapping yet

The implementation slice MUST NOT engage the
FIELD-I.4 `FieldMappingConfig` →
`evaluate_mapping(...)` → beauty-modulation
pipeline. Specifically:

- The kernel MUST NOT read `view.field_mapping_config`
  / `optixLaunchParams.field_mapping_config`
  (those fields do not exist this slice; their
  introduction is reserved for a separate later
  FIELD-I.* slice).
- The kernel MUST NOT modulate the per-pixel
  beauty color / emission / chromatic-shift /
  distortion based on the field sample. The
  beauty pass's per-pixel arithmetic continues
  to ignore the field entirely.
- The kernel MUST NOT call `evaluate_mapping(...)`
  on the field sample. The AOV writes the RAW
  `evaluate(ScalarFieldConfig, hit_pos)` output;
  the future `evaluate_mapping(...)` →
  beauty-pipeline integration is a separate
  later slice.
- The kernel MUST NOT route the field sample to
  any `FieldMappingTarget` enumerator
  (`ColorMultiplier` / `Emission` /
  `DiagnosticAOV`). The AOV's write site is the
  ONLY consumer of the field sample this slice;
  the target-channel routing is reserved.
- The kernel MUST NOT use the field sample to
  perturb ray direction (`Distortion` target),
  modulate alpha density (`Density` target), or
  shift chromatic frequency (`ChromaticShift`
  target). All FIELD-I.4 `FieldMappingTarget`
  channels are inert this slice.

### 4.3 Cross-backend math consistency

Both backends MUST use the same per-pixel scalar
sample at the same hit position. The single-
source-of-truth math leaf is the FIELD-I.2
`evaluate(ScalarFieldConfig, Vec3) → float`
helper at `src/field/ScalarField.h` (RR_HD inline;
host + device callable). The cross-backend AOV
equivalence is structurally guaranteed:

- The new `scalar_field_config` field on
  `CudaSceneView` + `OptixLaunchParams` is
  populated from the same host-side `cfg.scalar_field_config`
  (a new field on `rr::core::Config` — see §5
  below).
- The kernel-side read is a direct field access
  followed by a call to the same RR_HD inline
  `evaluate(...)` helper — no per-backend math
  divergence.
- The AOV-write encoding is the same single-
  float-to-PPM byte sequence on both backends.

Therefore the CUDA-side `aov_field_scalar.ppm`
and the OptiX-side `optix_aov_field_scalar.ppm`
must be pixel-bit-identical for the same input
`cfg.scalar_field_config` and the same scene.
Runtime verification is DEFERRED to an SDK-host
audit pass per §8 below.

---

## 5. Files likely involved

The implementation slice is expected to touch
the following files (host + CUDA + OptiX).
Numbers in parentheses are rough net-line
estimates from comparable past slices (OBSERVER.13
+ MANI-I.8). The impl slice may compress by ~30%
if it reuses the MANI-I.8 + OBSERVER.13 wiring
patterns verbatim.

| Layer                 | File                                              | Why |
|-----------------------|---------------------------------------------------|-----|
| AOV data model        | `src/renderer/AOV.h` (+15)                        | New `AOVType::FieldScalar = 8` enumerator + `make_field_scalar(...)` factory declaration. |
| AOV data model        | `src/renderer/AOV.cpp` (+15)                      | `aov_component_count` → 1 for the new type; `aov_type_name` → `"field_scalar"`; factory body. |
| Core config           | `src/core/Config.h` (+5)                          | New `rr::field::ScalarFieldConfig scalar_field_config{}` field on `rr::core::Config` (sibling of `observer` + `manifold`). |
| CLI parser            | `src/core/CommandLine.cpp` (+30 — minimum)        | New `--field-debug` modifier flag (presence-only; sets `r.config.field_debug_visualization = true`; new `bool` flag on Config or on a new `FieldConfig` wrapper). Plus the MINIMAL `--field-*` field-authoring surface needed so the AOV has something to visualise: at minimum `--field-enable`, `--field-kind <constant|radial|procedural>`, `--field-constant-value <float>` (Constant), `--field-center <x,y,z>` / `--field-min-radius` / `--field-max-radius` / `--field-falloff` / `--field-min-value` / `--field-max-value` (Radial), and `--field-strength <float>` (the universal multiplier). The full per-channel mapping CLI (`--field-color-strength` etc.) is reserved for a SEPARATE later slice. |
| CUDA scene view       | `src/cuda/CudaScene.cuh` (+5)                     | New `rr::field::ScalarFieldConfig scalar_field_config{}` field on `CudaSceneView` (sibling of `manifold_mode` + `observer_frame`). |
| CUDA AOV view         | `src/cuda/CudaAOV.cuh` (+5)                       | New `float* field_scalar = nullptr` slot on `DeviceAOVView` (sibling of `observer_beta`). |
| CUDA renderer         | `src/cuda/CudaRenderer.h` (+10)                   | New `float* field_scalar = nullptr` field on `AOVTargets` (sibling of `observer_beta`). |
| CUDA renderer         | `src/cuda/CudaRenderer.cu` (+5)                   | One-line thread inside `render_scene_with_aovs`: `view.aovs.field_scalar = targets.field_scalar;`. |
| CUDA kernel           | `src/cuda/CudaTestKernel.cu` (+20)                | Sibling AOV-write arm next to the existing `ObserverBeta` arm. Closest-hit + miss arms gated on `view.aovs.field_scalar != nullptr`. Hit: `view.aovs.field_scalar[idx] = rr::field::evaluate(view.scalar_field_config, hit_pos);`. Miss: `view.aovs.field_scalar[idx] = 0.0f;`. |
| OptiX launch params   | `src/optix/OptixLaunchParams.h` (+10)             | New trailing `rr::field::ScalarFieldConfig scalar_field_config{}` field + `float* aov_field_scalar = nullptr;` field, with the same null-means-skip doc-comment as the existing AOV slots. |
| OptiX kernel          | `src/optix/OptixPrograms.cu` (+20)                | Closest-hit + miss arms write to the new pointer when non-null. Hit: `optixLaunchParams.aov_field_scalar[idx] = rr::field::evaluate(optixLaunchParams.scalar_field_config, hit_pos);`. Miss: `optixLaunchParams.aov_field_scalar[idx] = 0.0f;`. |
| OptiX renderer        | `src/optix/OptixRenderer.h` (+10)                 | New `rr::image::Image field_scalar;` field on `AovResult` (sibling of `observer_beta`). |
| OptiX renderer        | `src/optix/OptixRenderer.cpp` (+30)               | Allocate the per-pass device buffer when the AOV is requested via the new `cfg.field_debug_visualization` flag; pass the pointer through `OptixLaunchParams`; download + save into `AovResult::field_scalar` at the end of `render_aovs(...)`. The `render_aovs(...)` signature gains a trailing defaulted `ScalarFieldConfig scalar_field_config = disabled_scalar_field_config()` argument (mirrors the OBSERVER.10 / OBSERVER.13 trailing-defaulted-parameter ABI-extension pattern). |
| CLI dispatcher        | `src/main.cpp` (+30)                              | `run_render_aovs` (CUDA) + `run_render_optix_aovs` (OptiX) honour the `--field-debug` gate; thread `cfg.scalar_field_config` into the launch payload; emit `output/aov_field_scalar.ppm` (CUDA) / `output/optix_aov_field_scalar.ppm` (OptiX) when both `--render-aovs` / `--render-optix-aovs` AND `--field-debug` hold. |
| Tests                 | `tests/renderer_tests.cpp` (+30)                  | Host-side: assertion that `AOVType::FieldScalar` has `component_count == 1` and `name == "field_scalar"`; assertion that `make_field_scalar` produces a valid `AOV`. Mirrors `test_observer_13_observer_beta_aov_type` shape. |
| Tests                 | `tests/cli_tests.cpp` (+50)                       | Host-side: assertions covering the new `--field-debug` flag (default off; presence-only flips the bit; combines with `--render-aovs` / `--render-optix-aovs` cleanly; combines with `--manifold-debug` / `--observer-debug` cleanly; default-off across N non-field argv vectors mirroring the existing OBSERVER.4 `test_observer_default_off_with_other_flags` pattern) + assertions covering the minimal `--field-*` CLI surface (default off; flag presence flips bits; round-trips through Config). |
| Fixture               | `scenes/test_field_diagnostic.rrscene` (~80 line scene) | Optional fixture for the FIELD-I.7 audit slot. A small scene with a non-trivial Radial field authored at the world origin. Reuses the OBS-F.2 (`test_observer_frame.rrscene`) overall shape for the geometry; adds NO `field` scene block (the field is engaged via CLI flags only — no `.rrscene` schema bump per §6). |
| Companion doc         | `docs/FIELD_DIAGNOSTIC_FIXTURE.md` (~150 lines)   | Per the FIELD-I.1 plan §5's FIELD-I.7 fixture-scene slot. Documents the fixture scene's expected visual signature (the per-pixel scalar sample colour map, the expected PPM byte counts, the SDK-host golden-pin procedure). |
| Docs                  | `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md` §5 FIELD-I.6 (LANDED-update on slice merge) | Optional rewrite with the actual landed surface (mirrors what MANI-I.5 did to the integration plan §6). |
| Docs                  | `docs/BUILD_PLAN.md`                              | FIELD-I.7 entry (the next renumbered impl slot after the FIELD-I.6 docs-only task brief). |
| CMake                 | none expected                                     | The new AOV uses the existing `rr_renderer` library wiring; `rr_optix` / `rr_gpu` already link the necessary modules (`rr_field` is the new transitive dep — the impl slice adds `target_link_libraries(rr_gpu PUBLIC rr_field)` + sibling on `rr_optix` if not already wired). |

---

## 6. What must not be touched

Per master rule #3 and the operator's FIELD-I.6
brief, the implementation slice MUST NOT:

- **Modify the Beauty pass kernel arithmetic.**
  The existing closest-hit / miss / raygen
  programs' shading code paths stay unchanged.
  The new AOV write is gated behind an
  `if (view.aovs.field_scalar != nullptr)` check
  (CUDA) /
  `if (optixLaunchParams.aov_field_scalar !=
  nullptr)` check (OptiX) that the existing
  kernel arms already use for the eight pre-existing
  AOV slots.
- **Modify the existing eight AOV slots'
  layouts** or their `AOVType` enumerator
  values. The new enumerator MUST be appended
  at the END of the `AOVType` enum (value `= 8`,
  after `ObserverBeta = 7`) to preserve every
  pre-FIELD-I.7 value. The existing eight slots'
  kernel write paths stay bit-identical.
- **Engage the FIELD-I.4 mapping pipeline.**
  The operator brief is explicit: "no field-to-
  beauty mapping yet". The kernel MUST NOT call
  `evaluate_mapping(...)` on the field sample.
  The kernel MUST NOT consume any
  `FieldMappingConfig` payload. The beauty pass
  MUST NOT modulate any per-pixel arithmetic on
  the field sample. The future mapping-pipeline
  integration is a separate later FIELD-I.*
  slice (TBD; reserved for FIELD-I.10 / .12 /
  later under the FIELD-I.5 renumbered ladder).
- **Gate any non-AOV kernel call site on
  `scalar_field_config.enabled`.** The field
  config is read-and-write-AOV-only this slice.
  The existing scene-aware actions
  (`--render-pathtrace`, `--render-mesh-scene`,
  `--render-material-scene`,
  `--render-direct-lighting`, `--render-aovs`,
  `--render-optix-aovs`) continue to ignore the
  field config for their non-debug-AOV paths.
- **Add per-pixel field state.** The
  `ScalarFieldConfig` is per-launch (set once at
  the dispatcher). The MVP `fieldScalar` AOV
  writes `evaluate(config, hit_pos)` at every
  pixel within a launch; the only per-pixel
  variation comes from the `hit_pos` argument.
  A future slice may introduce per-pixel field
  parameter overrides; not this slice.
- **Touch the `.rrscene` scene-file format.**
  No parser change, no writer change, no schema
  bump. The new AOV is request-gated by CLI
  flags only; the field is authored by CLI
  flags only this slice. A future slice may add
  a `field` / `fieldInterpreter` block to
  `.rrscene` for scene-authoring; not this
  slice.
- **Touch `src/server/`, `bridges/`, or
  `tools/`.** No C4D / server / UI /
  node-editor surface change.
- **Add a new `--render-*` action.** The
  two-flag composition (`--render-aovs
  --field-debug` / `--render-optix-aovs
  --field-debug`) is the entry point. A new
  action would create CLI surface duplication
  (mirrors the MANI-I.7 + OBSERVER.12 design
  decisions).
- **Modify the OptiX denoiser path.** The
  denoiser consumes Beauty / Albedo / Normal
  only; it must continue to do so. The new
  AOV slot is denoiser-ignored.
- **Modify `OptixLaunchParams` field offsets
  that predate OBSERVER.13.** The new
  `scalar_field_config` POD field + the
  `aov_field_scalar` pointer field are
  appended at the END of the POD (immediately
  after `aov_observer_beta`).
- **Change the existing `--render-aovs` /
  `--render-optix-aovs` PPM filenames or the
  existing `output/aov_*.ppm` /
  `output/optix_aov_*.ppm` set's
  enumeration.** The new file is *additional*,
  not a replacement.
- **Ship multi-channel field AOVs.** No
  vector-field AOV (3-channel direction
  encoding), no tensor-field AOV (9-channel
  flat-encoded tensor), no chromatic-shift
  diagnostic AOV (3-channel Doppler-shifted
  spectrum encoding). FIELD-I.6 / .7 ships
  only the single-float `fieldScalar` channel.
  Per the FIELD-I.1 plan §2.1 the FIELD-I.*
  arc is scalar-only.
- **Ship the full FIELD-I.4 `FieldMappingConfig`
  CLI surface.** No `--field-color-strength` /
  `--field-emission-strength` /
  `--field-aov-strength` / `--field-bias` /
  `--field-clamp-output` / `--field-mapping-target`
  flag. The mapping config remains
  unauthored-via-CLI this slice; the renumbered
  next bridge slice ships the mapping CLI.
- **Engage the kernel-side FIELD-I.* arc's
  full mapping migration.** The AOV is a
  pre-mapping read; the future bridge slice
  is what lifts the field config + mapping
  config onto the beauty-pass arithmetic.
  FIELD-I.7 is scoped to the diagnostic AOV
  ONLY.
- **Touch the legacy FIELD.3 multi-channel
  `FieldMapping` POD or its `target_strength(...)`
  accessor.** Preserved verbatim. The new
  AOV slot consumes only the FIELD-I.2
  `ScalarFieldConfig`, not the FIELD.3 form.

---

## 7. PASS criteria

The implementation slice's acceptance gate is
satisfied when ALL of the following hold:

### 7.1 Structural

- [ ] `AOVType::FieldScalar` enumerator exists
      at the end of the `AOVType` enum (value
      `= 8`).
- [ ] `aov_component_count(AOVType::FieldScalar)
      == 1`.
- [ ] `aov_type_name(AOVType::FieldScalar) ==
      "field_scalar"`.
- [ ] `AOV::make_field_scalar(...)` factory
      exists and produces a well-formed `AOV`
      with `type() == FieldScalar` and
      `name() == "field_scalar"` (or the
      caller-supplied name).
- [ ] `rr::core::Config::scalar_field_config`
      `ScalarFieldConfig` field exists; default
      `disabled_scalar_field_config()`.
- [ ] `rr::core::Config::field_debug_visualization`
      `bool` field (or equivalent on a new
      `FieldConfig` wrapper) exists; default
      `false`.
- [ ] `--field-debug` CLI flag parses cleanly
      (presence-only; no value consumed); flips
      `r.config.field_debug_visualization` to
      `true`.
- [ ] `--field-enable` CLI flag parses cleanly
      (presence-only); flips
      `r.config.scalar_field_config.enabled` to
      `true`.
- [ ] `--field-kind <constant|radial|procedural>`
      CLI flag parses cleanly; sets
      `r.config.scalar_field_config.kind` to
      the matching `ScalarFieldKind` enumerator
      (`procedural` maps to
      `ProceduralPlaceholder` per master rule
      #3).
- [ ] The minimal `--field-strength <float>` +
      `--field-constant-value <float>` (Constant)
      + `--field-center <x,y,z>` /
      `--field-min-radius <float>` /
      `--field-max-radius <float>` /
      `--field-falloff <float>` /
      `--field-min-value <float>` /
      `--field-max-value <float>` (Radial) CLI
      flags all parse cleanly and round-trip
      through Config.
- [ ] `--help` includes the new `--field-debug`
      + `--field-*` flag entries.
- [ ] `DeviceAOVView::field_scalar = nullptr`
      slot exists (CUDA).
- [ ] `AOVTargets::field_scalar = nullptr` field
      exists (CUDA).
- [ ] `CudaSceneView::scalar_field_config` POD
      field exists; default
      `disabled_scalar_field_config()`.
- [ ] `OptixLaunchParams::scalar_field_config`
      POD field exists at the end of the POD
      (after `observer_frame`); default
      `disabled_scalar_field_config()`.
- [ ] `OptixLaunchParams::aov_field_scalar =
      nullptr` field exists at the end of the
      POD.
- [ ] OptiX device-side programs
      (`__closesthit__` / `__miss__` for the
      AOV-aware ray types) gate writes on
      `aov_field_scalar != nullptr`.
- [ ] CUDA `CudaTestKernel.cu` AOV-aware
      kernels do the same (closest-hit + miss
      arms gated).
- [ ] `--render-aovs --field-debug` emits
      `output/aov_field_scalar.ppm` (CUDA path)
      and `--render-optix-aovs --field-debug`
      emits `output/optix_aov_field_scalar.ppm`
      (OptiX path) alongside the existing AOV
      PPMs.

### 7.2 Behavioural

- [ ] `--render-aovs` / `--render-optix-aovs`
      **without** `--field-debug` emits exactly
      the same AOV PPM set it emitted
      pre-FIELD-I.7 (no new file, no missing
      file, no changed file).
- [ ] Beauty output of every existing CLI
      action is pixel-bit-identical to the
      pre-FIELD-I.7 baseline.
- [ ] The existing eight AOV PPMs (Beauty /
      Normal / Depth / Albedo / DopplerFactor /
      SearchlightFactor / ManifoldCoordinates
      when applicable / ObserverBeta when
      applicable) are pixel-bit-identical to the
      pre-FIELD-I.7 baseline for every existing
      `--render-aovs` / `--render-optix-aovs`
      invocation.
- [ ] On the default disabled-field config
      (no `--field-enable`),
      `aov_field_scalar.ppm`'s per-pixel value
      matches `0.0` to within `1.0e-5f` for at
      least 99% of pixels (the remaining 1%
      tolerance covers hit-misses at
      edge-of-frame anti-aliasing; the value is
      structurally `0.0` so any non-zero pixel
      is a hard fail).
- [ ] On `--field-enable --field-kind constant
      --field-constant-value 0.5
      --field-strength 1.0`, every hit pixel
      writes `0.5` to within `1.0e-5f`. Miss
      pixels write `0.0`.
- [ ] On `--field-enable --field-kind radial
      --field-min-radius 1.0 --field-max-radius
      5.0 --field-min-value 0.0 --field-max-value
      1.0 --field-strength 1.0`, hit pixels at
      world-space `|hit_pos| ≤ 1.0` write `0.0`;
      pixels at `|hit_pos| ≥ 5.0` write `1.0`;
      pixels in between write the smoothstep
      cubic to within `1.0e-5f`. Miss pixels
      write `0.0`.
- [ ] On any non-default field engagement, the
      `output/aov_field_scalar.ppm` (CUDA) and
      `output/optix_aov_field_scalar.ppm`
      (OptiX) files are byte-identical (single-
      source-of-truth math: both backends use
      the same RR_HD inline
      `evaluate(ScalarFieldConfig, Vec3)`
      helper).

### 7.3 Test surface

- [ ] `ctest` reports `13/13 passed` on the
      audit-host build (unchanged from the
      FIELD-I.5 baseline; no new ctest target).
- [ ] `cli_tests` reports its pre-FIELD-I.7
      count + at least N new assertions
      covering: `--field-debug` flag presence
      (default off, flag presence flips bit,
      combines with `--render-aovs` /
      `--render-optix-aovs` cleanly, combines
      with `--manifold-debug` / `--observer-debug`
      cleanly); the minimal `--field-*`
      authoring flags (default off, flag
      presence sets fields, round-trips
      through Config).
- [ ] `renderer_tests` reports its
      pre-FIELD-I.7 assertion count + at
      least 4 new FIELD-I.7 assertions
      covering: enum value (= 8),
      `aov_component_count` (= 1),
      `aov_type_name` (= `"field_scalar"`),
      factory output.
- [ ] A `g++ -std=c++20 -Isrc -Wall -Wextra
      -Werror` standalone build of an
      `AOV::make_field_scalar`-consuming TU
      compiles cleanly.

### 7.4 Documentation

- [ ] `docs/BUILD_PLAN.md` FIELD-I.7 entry
      added (mirrors the existing FIELD-I.*
      entries' "What ships / What does NOT
      ship / Acceptance / Module status
      changes" rubric).
- [ ] `docs/FIELD_DIAGNOSTIC_FIXTURE.md`
      companion doc added per §5 above (~150
      lines documenting the fixture scene's
      expected visual signature).
- [ ] `scenes/test_field_diagnostic.rrscene`
      fixture file added per §5 above
      (OPTIONAL; may be deferred to the
      separate FIELD-I.* fixture slice if the
      operator prefers to keep the AOV slice
      narrowly scoped).
- [ ] OPTIONAL:
      `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
      §5 FIELD-I.6 entry rewritten with the
      landed-surface description (mirrors
      what MANI-I.5 did to the integration
      plan §6).

---

## 8. Runtime-deferred CUDA / OptiX checks

The audit-host build (no CUDA, no OptiX SDK)
cannot directly verify the AOV's pixel content.
The runtime checks below are DEFERRED behind the
audit host's existing no-CUDA / no-OptiX-SDK
fallback, matching the existing MANI-I.7 /
SCHW.5 / PENROSE.6 / OBSERVER.8 / OBSERVER.10 /
OBSERVER.13 deferral pattern.

Each deferred check must be exercised on a CUDA +
OptiX-SDK host before the FIELD-I.* arc capstone
audit closes the FIELD-I.* programme:

### 8.1 Neutral diagnostic on disabled-field default (CUDA path)

Run:
```
RelativityRender --render-aovs --field-debug
                 scenes/test_field_diagnostic.rrscene
```

Verify:
- `output/aov_field_scalar.ppm` exists.
- For every hit pixel `(x, y)`, the pixel's
  decoded grayscale value matches `0.0` within
  `1.0e-5f` — because no `--field-enable` flag
  was passed, so `cfg.scalar_field_config.enabled
  == false` and the `evaluate(...)` short-circuit
  returns `0.0f` at every position.
- `output/aov_beauty.ppm` is byte-identical to
  the pre-FIELD-I.7 reference (a pinned PPM in
  `tests/goldens/`).

### 8.2 Neutral diagnostic on disabled-field default (OptiX path)

Run:
```
RelativityRender --render-optix-aovs --field-debug
                 scenes/test_field_diagnostic.rrscene
```

Verify:
- `output/optix_aov_field_scalar.ppm` exists.
- The CUDA-side and OptiX-side
  `aov_field_scalar.ppm` files are
  pixel-bit-identical (the
  `view.scalar_field_config` /
  `optixLaunchParams.scalar_field_config`
  fields are populated by the same host-side
  `cfg.scalar_field_config` on both backends;
  the kernel-side `evaluate(...)` call uses
  the same RR_HD inline helper).
- `output/optix_aov_beauty.ppm` is byte-
  identical to the pre-FIELD-I.7 reference.

### 8.3 Non-default Constant field visualisation (both backends)

Run:
```
RelativityRender --render-aovs --field-debug
                 --field-enable --field-kind constant
                 --field-constant-value 0.5
                 --field-strength 1.0
                 scenes/test_field_diagnostic.rrscene
```
AND the same with `--render-optix-aovs`.

Verify:
- Both CUDA and OptiX
  `aov_field_scalar.ppm` files contain hit
  pixels whose decoded grayscale value matches
  `0.5` within `1.0e-5f`.
- Miss pixels write `0.0`.
- The two backends' output files are
  pixel-bit-identical.

### 8.4 Non-default Radial field visualisation (both backends)

Run:
```
RelativityRender --render-aovs --field-debug
                 --field-enable --field-kind radial
                 --field-min-radius 1.0 --field-max-radius 5.0
                 --field-min-value 0.0 --field-max-value 1.0
                 --field-strength 1.0
                 scenes/test_field_diagnostic.rrscene
```
AND the same with `--render-optix-aovs`.

Verify:
- Both CUDA and OptiX `aov_field_scalar.ppm`
  files contain a visible smoothstep pattern:
  flat black inside the central sphere of
  radius 1.0, flat white outside the outer
  sphere of radius 5.0, gradient in between.
- The two backends' output files are
  pixel-bit-identical.
- The Beauty / Normal / Depth / Albedo /
  DopplerFactor / SearchlightFactor PPMs are
  byte-identical to the pre-FIELD-I.7 baseline
  (the field engagement does NOT modulate the
  beauty pass).

### 8.5 Off-path bit-identity (both backends)

Run:
```
RelativityRender --render-aovs
                 scenes/test_field_diagnostic.rrscene
```
(WITHOUT `--field-debug`)

Verify:
- Exactly six (or seven / eight, when
  `--manifold-debug` / `--observer-debug` are
  also set) AOV PPMs are produced; no
  `field_scalar.ppm` is emitted.
- All PPMs are byte-identical to the
  pre-FIELD-I.7 reference.

### 8.6 Composability with `--manifold-debug` + `--observer-debug`

Run:
```
RelativityRender --render-aovs
                 --field-debug --field-enable --field-kind radial
                 --manifold-debug --manifold-enable
                 --manifold-chart schwarzschild-like
                 --manifold-strength 0.5
                 --observer-debug --observer-perception-mode relativistic
                 --observer-beta 0.5 --observer-direction 1,0,0
                 scenes/test_schwarzschild_like_manifold.rrscene
```

Verify:
- `output/aov_manifold_coordinates.ppm` AND
  `output/aov_observer_beta.ppm` AND
  `output/aov_field_scalar.ppm` are all
  emitted (the three debug-AOV gates are
  orthogonal; all three can be active at the
  same time).
- The Beauty / Normal / Depth / Albedo /
  DopplerFactor / SearchlightFactor PPMs are
  byte-identical to the pre-FIELD-I.7
  baseline (the three debug AOVs are
  read-only on their respective payloads;
  none of them modulate the beauty pass).

### 8.7 Cross-backend equivalence

Run the §8.3 + §8.4 invocations on both
backends. Compare `cmp output/aov_field_scalar.ppm
output/optix_aov_field_scalar.ppm` — exit status
MUST be `0` (byte-identical).

This is the structural cross-backend equivalence
check that the FIELD-I.7 audit's check #3 will
anticipate. The check passes by construction
because (a) both backends consume the same
`rr::field::ScalarFieldConfig` POD, (b) both
backends invoke the same RR_HD inline
`evaluate(ScalarFieldConfig, Vec3) → float`
helper from `src/field/ScalarField.h`, (c) the
AOV write is a direct single-float assignment
with no backend-specific arithmetic.

---

## 9. Cross-references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") + #1 ("Build incrementally") apply.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §6 — defines the Field Interpretation Layer
  as an OPTIONAL extension above the Manifold
  Core; the FIELD-I.6 diagnostic AOV is the
  read-only window into Phase 1's field-
  sampling pipeline.
- `docs/FIELD_INTERPRETATION_LAYER.md` §3 + §4
  + §6 — the pre-FIELD-I.* design doc the
  FIELD-I.* arc consumes verbatim; the
  diagnostic-AOV channel (`§4.6
  DiagnosticAOV`) is the design-doc anchor for
  the FIELD-I.7 impl.
- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  §2.3 + §5 (FIELD-I.6 entry, renumbered from
  FIELD-I.4 in the FIELD-I.5 audit's §4.1
  ladder) — the canonical FIELD-I.* arc plan
  that authorised the diagnostic-AOV scope.
- `docs/FIELD_SCALAR_MODEL_AUDIT.md`
  (FIELD-I.3) — the FIELD-I.2
  `ScalarFieldConfig` POD's structural audit;
  carry-forward of the field semantics the AOV
  reads (the default-disabled no-op anchor at
  check #2 underpins the FIELD-I.6 §3.2
  neutral-zero anchor).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md`
  (FIELD-I.5) — the FIELD-I.4
  `FieldMappingConfig` POD's structural audit;
  carry-forward of the explicit "no mapping
  applied this slice" non-goal (the FIELD-I.6
  AOV reads the RAW field sample; the mapping
  pipeline is a separate later slice).
- `docs/MANIFOLD_DEBUG_AOV_TASK.md` (MANI-I.7)
  — the precedent task brief this FIELD-I.6
  task brief mirrors structurally (the
  `ManifoldCoordinates` diagnostic AOV's
  two-flag composition + single-MVP-AOV
  scoping).
- `docs/MANIFOLD_DEBUG_AOV_AUDIT.md` (MANI-I.9)
  — the per-slice audit doc for the precedent
  manifold-debug-AOV impl; the FIELD-I.7 impl
  slice's audit (the renumbered FIELD-I.9
  audit slot) will follow this shape.
- `docs/OBSERVER_DEBUG_AOV_TASK.md` (OBSERVER.12)
  — the second precedent task brief this
  FIELD-I.6 task brief mirrors structurally
  (the `ObserverBeta` diagnostic AOV's same
  two-flag-composition + single-MVP-AOV
  pattern; the 8-section layout is taken
  verbatim from this precedent).
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md` (OBSERVER.14)
  — the per-slice audit doc for the precedent
  observer-debug-AOV impl; the FIELD-I.7
  impl slice's audit will mirror this
  shape.
- `src/renderer/AOV.h` / `AOV.cpp` — the AOV
  data-model surface the new
  `AOVType::FieldScalar = 8` enumerator + the
  `make_field_scalar(...)` factory extend.
- `src/field/ScalarField.h` — the FIELD-I.2
  `ScalarFieldConfig` POD + the
  `evaluate(ScalarFieldConfig, Vec3) → float`
  RR_HD inline helper the kernel reads.
- `src/field/FieldMapping.h` — the FIELD-I.4
  `FieldMappingConfig` POD + the
  `evaluate_mapping(...)` helper, NOT
  consumed by the FIELD-I.7 kernel arm
  (per §6 non-goal).
- `src/core/Config.h` — the `rr::core::Config`
  POD the new `scalar_field_config` + the
  `field_debug_visualization` flag fields
  extend.
- `src/core/CommandLine.cpp` — the CLI
  parser the new `--field-debug` modifier +
  the minimal `--field-*` authoring flags
  extend.
- `src/cuda/CudaScene.cuh` /
  `src/cuda/CudaAOV.cuh` /
  `src/cuda/CudaRenderer.h` /
  `src/cuda/CudaRenderer.cu` /
  `src/cuda/CudaTestKernel.cu` — the
  CUDA-side surface the new AOV slot +
  scalar-field-config field extend.
- `src/optix/OptixLaunchParams.h` /
  `src/optix/OptixRenderer.h` /
  `src/optix/OptixRenderer.cpp` /
  `src/optix/OptixPrograms.cu` — the
  OptiX-side surface the new AOV slot +
  scalar-field-config field extend.
- `src/main.cpp` — the dispatchers
  (`run_render_aovs` + `run_render_optix_aovs`)
  the new `--field-debug` gate threads
  through; the new PPM save sites the AOV
  emits to.
- `scenes/test_observer_frame.rrscene` (OBS-F.2)
  — the existing precedent fixture; the
  FIELD-I.7 fixture scene
  `scenes/test_field_diagnostic.rrscene` may
  reuse the same overall scene-file shape
  (geometry + camera + lighting; no
  scene-block extension required).
- `docs/BUILD_PLAN.md` — the FIELD-I.6 docs-
  only entry (mirroring this task brief)
  lands at the same commit as this task brief;
  the FIELD-I.7 impl entry lands at the impl
  slice's commit.

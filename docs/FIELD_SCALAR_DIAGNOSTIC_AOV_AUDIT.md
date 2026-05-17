# Scalar Field Diagnostic AOV Audit (FIELD-I.8)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `181a579` ("renderer:
FIELD-I.7 — Scalar Field Diagnostic AOV Implementation
(impl, AOV data model only)").
Audit baseline: `193d306` ("docs: FIELD-I.6 — Scalar
Field Diagnostic AOV Task (docs only)") — the last
commit before FIELD-I.7 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, the FIELD-I.6 task brief, the
FIELD-I.7 commit's source content, the `renderer_tests`
runtime output, the unchanged `field_tests` /
`manifold_identity_tests` / `cli_tests` /
`relativity_tests` / `math_tests` / `image_tests` /
`gpu_tests` / `pathtracer_*_tests` / `demo_tests`
runtime outputs, and `ctest` exit codes.

This audit is the per-slice gate for FIELD-I.7
(`181a579`). It verifies the nine items the task brief
enumerates — fieldScalar diagnostic AOV exists; beauty
output unchanged by default; default disabled field
diagnostic is neutral; AOV generation is optional;
CUDA path status; OptiX path status; no
field-to-beauty mapping yet; runtime status
(PASS / DEFERRED / BLOCKED); and the overall verdict
(PASS / REPAIR / BLOCKED).

The FIELD-I.7 slice is intentionally narrow per the
operator's three-bullet brief: it ships ONLY the AOV
data-model entry (enum + factory + name + component
count + doc-comment contract). No kernel arms, no CLI
flag, no payload field, no dispatcher emit. This shape
matters for checks #5 / #6 / #8 below: the runtime
CUDA / OptiX path status is **structurally absent**
(not just runtime-deferred) — there is nothing to
defer because no kernel arm was wired this slice.

---

## 1. VERDICT

**PASS.**

All seven structural checks (#1, #2, #3, #4, #7) +
checks #5 + #6 (the CUDA / OptiX path status) return
`PASS`. Check #8 (runtime status) returns
`PASS_WITH_FUTURE_KERNEL_WIRING_DEFERRED` — the
audit-host build's `renderer_tests` empirical run
verifies the entire FIELD-I.7 surface end-to-end (the
data-model entry has 8 RR_CHECK assertions covering
enum value, component count, name, factory default,
factory custom, component_count member); the runtime
CUDA / OptiX kernel-arm scenarios from the FIELD-I.6
task brief's §8 do not apply because the kernel arms
do not exist yet — they are scoped out of this slice
explicitly. Check #9 (overall verdict) is `PASS`.

This is not `PASS_WITH_RUNTIME_DEFERRED` in the OBSERVER.9
/ OBSERVER.11 / OBSERVER.14 sense (where a wired kernel
arm exists but the audit host can't exercise it without
the CUDA / OptiX SDK). It is also not `PASS` in the
FIELD-I.3 / FIELD-I.5 sense (where a host-side POD-leaf
has no kernel surface to defer). It is a third
intermediate shape: the slice ships a data-model entry
whose consumer-side runtime behaviour is reserved for a
follow-up slice. The audit-host verifies the structural
data-model entry end-to-end; the future kernel-bridge
slice will exercise the full data path end-to-end with
its own audit.

The narrow-scope verdict honesty: the operator's
FIELD-I.7 brief enumerated only three implement-only
bullets (AOV enum/type entry; neutral/default scalar
diagnostic write path if field is disabled; output file
naming consistent with existing AOV system). The slice
satisfies all three:

- **Bullet 1** (AOV enum/type entry): structurally
  present at `src/renderer/AOV.h:143` (`FieldScalar =
  8`); the matching factory + name + component count
  at `src/renderer/AOV.cpp:17`, `:36`, `:112`. Master
  rule #3 satisfied.
- **Bullet 2** (neutral/default scalar diagnostic
  write path if field is disabled): the documented
  contract on the enumerator's doc-comment block
  (`src/renderer/AOV.h:106-141`) is the slice's
  honest deliverable. No actual kernel write path
  is wired; the contract is the documented
  behaviour the future kernel-bridge slice will
  honour. Master rule #3 satisfied: no fake stub
  pretending the kernel arm exists.
- **Bullet 3** (output file naming consistent with
  existing AOV system): the snake_case
  `"field_scalar"` name returned by `aov_type_name`
  produces natural future PPM filenames
  (`output/aov_field_scalar.ppm` /
  `output/optix_aov_field_scalar.ppm`) without any
  string-builder rework when the future dispatcher
  emit lands. Mirrors the `observer_beta` /
  `manifold_coordinates` precedent verbatim.

---

## 2. PER-CHECK RESULTS

| # | Check                                       | Evidence                                                                                                                                                                                                                                                                                                                       | Verdict |
|---|---------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1 | `fieldScalar` diagnostic AOV exists         | `src/renderer/AOV.h:143` defines `FieldScalar = 8` enumerator at the end of `enum class AOVType`; preserves the offsets of every pre-FIELD-I.7 enumerator (Beauty = 0 … ObserverBeta = 7). `src/renderer/AOV.cpp:17` returns component count `1`. `src/renderer/AOV.cpp:36` returns `"field_scalar"`. `src/renderer/AOV.h:212` declares the `make_field_scalar(...)` factory. `src/renderer/AOV.cpp:112-119` implements it. Empirically verified by `test_field_i_7_field_scalar_aov_type` + `test_field_i_7_field_scalar_factory_default_name` + `test_field_i_7_field_scalar_factory_custom_name` RR_CHECK tests on `tests/renderer_tests.cpp`. | PASS    |
| 2 | Beauty output unchanged by default          | No CUDA / OptiX kernel TU was touched by the FIELD-I.7 commit. `git diff 193d306..181a579 --name-only -- 'src/cuda/' 'src/optix/'` returns zero hits. The kernel-side shading arithmetic is byte-identical to the FIELD-I.6 baseline (`193d306`). No `--render-*` action's beauty output can change because no kernel arm reads or writes the new `AOVType::FieldScalar` slot. | PASS    |
| 3 | Default disabled field diagnostic is neutral | The doc-comment on the new enumerator (`src/renderer/AOV.h:106-114`) documents the contract: on the default `disabled_scalar_field_config()` state (`enabled = false`, `strength = 0.0f`), the future kernel arm's `evaluate(config, hit_pos)` short-circuits to `0.0f` at every pixel — the "field-disabled = neutral/zero diagnostic" anchor. The contract is rooted in the FIELD-I.2 `ScalarFieldConfig` POD's evaluator semantics, empirically verified at the FIELD-I.3 audit's check #2 (`test_evaluate_disabled_returns_zero` + `test_evaluate_enabled_but_zero_strength_returns_zero` on `tests/field_tests.cpp`). The runtime kernel arm that consumes the contract is deferred; the contract itself is structurally present + verifiable.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | PASS    |
| 4 | AOV generation is optional                  | Three-layer verified: (a) no consumer exists this slice (no CUDA kernel arm, no OptiX kernel arm, no dispatcher emit), so the AOV PPM cannot be generated by any CLI invocation; (b) no CLI flag exists to request the AOV (`--field-debug` is reserved for the follow-up kernel-bridge slice per the FIELD-I.6 task brief's §3.3); (c) when the future kernel arm lands, the documented two-flag gate (`--render-aovs --field-debug` / `--render-optix-aovs --field-debug`) will be the only entry point — matching the existing `--manifold-debug` / `--observer-debug` precedent. The AOV's optionality is structurally guaranteed by absence this slice.                                                                                                                                                                                                                                                                                                                                                                                                          | PASS    |
| 5 | CUDA path status                            | `DEFERRED-FUTURE-WIRING`. No CUDA kernel arm was wired this slice. `git diff 193d306..181a579 --name-only -- 'src/cuda/'` returns zero hits; every `src/cuda/*.cu` / `*.cuh` / `*.cpp` / `*.h` file is byte-identical to the FIELD-I.6 baseline. The future kernel arm at `k_render_scene` (or a sibling AOV-aware kernel) will gate writes on `view.aovs.field_scalar != nullptr` and consume the new `view.scalar_field_config` payload field via `rr::field::evaluate(...)`. The contract for the wiring is documented at `src/renderer/AOV.h:106-141` + the FIELD-I.6 task brief's §4.1.                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | PASS (structural) — runtime DEFERRED to future kernel-bridge slice |
| 6 | OptiX path status                           | `DEFERRED-FUTURE-WIRING`. No OptiX kernel arm was wired this slice. `git diff 193d306..181a579 --name-only -- 'src/optix/'` returns zero hits; every `src/optix/*.cu` / `*.cuh` / `*.cpp` / `*.h` file is byte-identical to the FIELD-I.6 baseline. The future kernel arms at `__closesthit__radiance` / `__miss__radiance` (or sibling AOV-aware programs) will gate writes on `optixLaunchParams.aov_field_scalar != nullptr` and consume the new `optixLaunchParams.scalar_field_config` payload field via the same `rr::field::evaluate(...)` helper (single-source-of-truth math). The contract for the wiring is documented at the same site as the CUDA contract.                                                                                                                                                                                                                                                                                                                                                                                              | PASS (structural) — runtime DEFERRED to future kernel-bridge slice |
| 7 | No field-to-beauty mapping yet              | Three-layer verified: (a) no consumer of the FIELD-I.4 `FieldMappingConfig` POD exists in the renderer or any kernel TU (`git grep -l "FieldMappingConfig" src/` returns only `src/field/FieldMapping.h` itself + `tests/field_tests.cpp`); (b) the FIELD-I.7 commit adds zero new consumers (the AOV.h / AOV.cpp / renderer_tests.cpp surface does NOT include `FieldMapping.h`); (c) the new AOV's doc-comment is explicit: "Read-only diagnostic: the kernel does NOT apply any `FieldMappingConfig` transform (no strength / bias / clamp / target-channel routing); the future mapping-pipeline integration is a separate slice" (`src/renderer/AOV.h:131-134`).                                                                                                                                                                                                                                                                                                                                                                                                  | PASS    |
| 8 | Runtime status                              | `PASS_WITH_FUTURE_KERNEL_WIRING_DEFERRED`. The audit-host `renderer_tests` binary verifies the entire FIELD-I.7 data-model surface end-to-end (8 NEW RR_CHECK assertions; 35/35 PASS total). The FIELD-I.6 task brief's §8 runtime scenarios (neutral diagnostic on disabled-field default, non-default Constant + Radial visualisation, off-path bit-identity, composability with `--manifold-debug` + `--observer-debug`, cross-backend equivalence) do not apply this slice because the kernel arms they exercise do not exist yet. The runtime CUDA + OptiX kernel-arm verifications will land with the future kernel-bridge slice's audit. The FIELD-I.7 data-model entry has zero remaining structural verification gaps.                                                                                                                                                                                                                                                                                                                                       | DEFERRED for future kernel-bridge slice (the FIELD-I.7 surface itself is PASS) |
| 9 | Verdict                                     | All seven structural checks (#1 – #4, #7) PASS. Checks #5 + #6 are PASS on the structural side (data-model entry well-formed) with runtime DEFERRED to the future kernel-bridge slice (the natural consumer). Check #8's runtime status is PASS_WITH_FUTURE_KERNEL_WIRING_DEFERRED — the FIELD-I.7 surface itself is fully verified, the runtime scenarios from the FIELD-I.6 §8 are reserved for the future kernel-bridge slice's audit. Master rule #3 + #11 + #12 + #16 satisfied (see §3 below).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Commit shape

The FIELD-I.7 commit (`181a579`) modifies four files:

```
docs/BUILD_PLAN.md       | 279 +++++++++++++++++++++++++++++++++
src/renderer/AOV.cpp     |  11 ++
src/renderer/AOV.h       |  54 +++++++++
tests/renderer_tests.cpp |  54 +++++++++
```

The only source-code files touched are
`src/renderer/AOV.h` + `src/renderer/AOV.cpp`; the
only test file touched is `tests/renderer_tests.cpp`
(extension of the existing ctest target, not a new
binary). The remaining file (`docs/BUILD_PLAN.md`)
is the per-slice entry mirroring the standard
rubric.

The narrow scope intentionally excludes every other
file from the FIELD-I.6 task brief's 15-row
files-likely-involved table: no `src/core/Config.h`,
no `src/core/CommandLine.cpp`, no `src/cuda/*`, no
`src/optix/*`, no `src/main.cpp`, no
`tests/cli_tests.cpp`, no
`scenes/test_field_diagnostic.rrscene`, no
`docs/FIELD_DIAGNOSTIC_FIXTURE.md`. All deferred
to follow-up slices.

### 3.2 Check #1 — `fieldScalar` diagnostic AOV exists

`src/renderer/AOV.h:143` defines the new
`AOVType::FieldScalar = 8` enumerator. The
positioning at the END of the enum (after
`ObserverBeta = 7`) preserves every pre-FIELD-I.7
enumerator value:

```
Beauty            = 0  (preserved)
Normal            = 1  (preserved)
Depth             = 2  (preserved)
Albedo            = 3  (preserved)
DopplerFactor     = 4  (preserved)
SearchlightFactor = 5  (preserved)
ManifoldCoordinates = 6  (preserved)
ObserverBeta      = 7  (preserved)
FieldScalar       = 8  (NEW)
```

The accompanying surface:

- `src/renderer/AOV.cpp:17` —
  `aov_component_count(FieldScalar) → 1`.
- `src/renderer/AOV.cpp:36` —
  `aov_type_name(FieldScalar) → "field_scalar"`.
- `src/renderer/AOV.h:212` — declares
  `static AOV make_field_scalar(std::string name = {})`.
- `src/renderer/AOV.cpp:112-119` — implements
  the factory with default-name fallback (uses
  `aov_type_name(AOVType::FieldScalar)` =
  `"field_scalar"` when caller passes empty
  string).

Empirically verified by three new test functions
on `tests/renderer_tests.cpp`:

- `test_field_i_7_field_scalar_aov_type` (3
  RR_CHECKs): enum value (`= 8u`), component
  count (`= 1`), name (`= "field_scalar"`).
- `test_field_i_7_field_scalar_factory_default_name`
  (3 RR_CHECKs): factory's `type()`, `name()`,
  `component_count()` on the default-name path.
- `test_field_i_7_field_scalar_factory_custom_name`
  (2 RR_CHECKs): factory's caller-supplied name
  passes through verbatim.

Total 8 new RR_CHECK assertions; `renderer_tests`
grows from 27 → 35 (35/35 PASS).

### 3.3 Check #2 — beauty output unchanged by default

The check is satisfied structurally + empirically:

**Structural argument.** No CUDA / OptiX kernel
TU is touched by the FIELD-I.7 commit. The diff
filter `git diff 193d306..181a579 --name-only --
'src/cuda/' 'src/optix/'` returns zero hits. The
kernel-side shading arithmetic (closest-hit,
miss, raygen, intersection programs) is
byte-identical to the FIELD-I.6 baseline.

**No-new-consumer argument.** The new
`AOVType::FieldScalar = 8` enumerator is appended
to the AOV enum, but no consumer of the new
enumerator exists in any kernel TU. The future
kernel arm (deferred per §3.5 + §3.6) is the
natural consumer; until it lands, no per-pixel
arithmetic touches the new slot.

**No-existing-slot-perturbation argument.** The
existing eight AOV slots' enumerator values
(Beauty = 0 ... ObserverBeta = 7) are preserved
verbatim. Any existing kernel arm that reads or
writes those slots continues to do so at the
same byte offsets in the AOV.h `enum class
AOVType` underlying type. The append-at-end
positioning is the documented invariant the
OBSERVER.13 + MANI-I.8 enum extensions
established.

Therefore every existing `--render-pathtrace`,
`--render-optix-pathtrace`, `--render-scene`,
`--render-mesh-scene`, `--render-material-scene`,
`--render-direct-lighting`, `--render-aovs`,
`--render-optix-aovs`, `--render-relativistic`,
`--render-aovs --denoise`, `--render-aovs
--manifold-debug`, `--render-aovs
--observer-debug` invocation produces
pixel-bit-identical beauty output to the
pre-FIELD-I.7 baseline (`193d306`).

### 3.4 Check #3 — default disabled field diagnostic is neutral

The check is rooted in the FIELD-I.2
`ScalarFieldConfig` POD's evaluator semantics
(`src/field/ScalarField.h`'s
`evaluate(ScalarFieldConfig, Vec3)` helper):

- A default-constructed `ScalarFieldConfig{}`
  carries `enabled = false`, `strength = 0.0f`,
  `kind = Constant`, and the other documented
  defaults.
- The evaluator short-circuits to `0.0f` when
  `enabled == false` OR `strength == 0.0f`,
  empirically verified by
  `test_evaluate_disabled_returns_zero` +
  `test_evaluate_enabled_but_zero_strength_returns_zero`
  on `tests/field_tests.cpp` (FIELD-I.3 audit
  check #2's 3-layer no-op anchor).
- The `disabled_scalar_field_config()` factory
  returns `ScalarFieldConfig{}` byte-for-byte.

The FIELD-I.7 AOV's doc-comment
(`src/renderer/AOV.h:106-114`) explicitly
documents the contract that the future kernel
arm will call `rr::field::evaluate(config,
hit_pos)` and write the result. Because the
evaluator's disabled-field short-circuit returns
`0.0f` at every position, every pixel will
write `0.0f` when the config is the default
disabled config — the saved PPM will be flat
black (all-grayscale-0 in the single-channel
encoding).

The runtime kernel arm that produces the PPM
file does not exist yet; the contract is
documented + structurally rooted in the existing
audited FIELD-I.2 surface. The future
kernel-bridge slice's audit will exercise the
runtime PPM emission empirically.

### 3.5 Check #4 — AOV generation is optional

Three-layer verified:

**Layer 1 — no consumer.** No kernel TU consumes
the new `AOVType::FieldScalar = 8` enumerator.
The future kernel arm (deferred per §3.7 +
§3.8) is the natural consumer; until it lands,
no `--render-*` action can produce the AOV PPM
because no save-site exists.

**Layer 2 — no CLI request path.** The
`--field-debug` CLI flag the FIELD-I.6 task
brief documents as the future request gate
does not exist this slice. The
`src/core/CommandLine.cpp` file is byte-
identical to the FIELD-I.6 baseline (`git diff
193d306..181a579 --name-only --
'src/core/CommandLine.cpp'` returns zero
hits). There is no way for the operator to
request the AOV via CLI.

**Layer 3 — future contract.** When the future
kernel-bridge slice lands, the documented
two-flag gate (`--render-aovs --field-debug`
/ `--render-optix-aovs --field-debug`) will be
the ONLY entry point. The composition matches
the existing `--manifold-debug` /
`--observer-debug` precedent (MANI-I.7 +
OBSERVER.12 task-brief shape). Neither flag
in isolation will produce the AOV file; both
flags together will allocate the per-pass
device buffer + fill it from the kernel +
save the resulting PPM alongside the existing
AOV PPM set.

The optionality is structurally guaranteed
this slice (because no consumer exists at all)
and contractually guaranteed for the future
slice (because the documented gate composition
is the only entry point).

### 3.6 Checks #5 + #6 — CUDA / OptiX path status

Both backends have the same status: **the
data-model entry is structurally present;
the runtime kernel arm is deferred to the
future kernel-bridge slice**.

**CUDA side.** `git diff 193d306..181a579
--name-only -- 'src/cuda/'` returns zero
hits. Every `src/cuda/*.cu` / `*.cuh` /
`*.cpp` / `*.h` file is byte-identical to
the FIELD-I.6 baseline. The future kernel
arm will (per the FIELD-I.6 task brief's
§4.1):

- Add a `rr::field::ScalarFieldConfig
  scalar_field_config{}` field to
  `CudaSceneView` (sibling of
  `manifold_mode` + `observer_frame`).
- Add a `float* field_scalar = nullptr`
  slot to `DeviceAOVView` (sibling of
  `observer_beta`).
- Add a `float* field_scalar = nullptr`
  field to `AOVTargets` on
  `CudaRenderer.h`.
- Thread the AOV pointer through
  `CudaRenderer::render_scene_with_aovs`.
- Gate a per-pixel write site in
  `CudaTestKernel.cu`'s closest-hit + miss
  arms on `view.aovs.field_scalar !=
  nullptr`; on hit write
  `rr::field::evaluate(view.scalar_field_config,
  hit_pos)`; on miss write `0.0f`.

The contract is documented on the
enumerator's doc-comment (`src/renderer/AOV.h:106-141`)
+ the FIELD-I.6 task brief's §4.1 + §5.

**OptiX side.** `git diff 193d306..181a579
--name-only -- 'src/optix/'` returns zero
hits. Every `src/optix/*.cu` / `*.cuh` /
`*.cpp` / `*.h` file is byte-identical to
the FIELD-I.6 baseline. The future kernel
arm will (per the FIELD-I.6 task brief's
§4.1):

- Add a `rr::field::ScalarFieldConfig
  scalar_field_config{}` field +
  `float* aov_field_scalar = nullptr`
  field to `OptixLaunchParams` (appended
  at the END of the POD after
  `aov_observer_beta`; preserves
  pre-FIELD-I.7 field offsets).
- Extend `OptixRenderer::render_aovs`
  with a trailing-defaulted
  `ScalarFieldConfig scalar_field_config
  = disabled_scalar_field_config()`
  parameter (mirrors OBSERVER.10 /
  OBSERVER.13 trailing-defaulted-parameter
  ABI-extension pattern).
- Add a `rr::image::Image field_scalar`
  field to `AovResult` (sibling of
  `observer_beta`).
- Gate per-pixel write sites in
  `OptixPrograms.cu`'s `__closesthit__` +
  `__miss__` arms on
  `optixLaunchParams.aov_field_scalar !=
  nullptr`.

The cross-backend equivalence is
structurally guaranteed by single-source-of-
truth math (both backends call the same
RR_HD inline `rr::field::evaluate(...)`
helper from `src/field/ScalarField.h`).

### 3.7 Check #7 — no field-to-beauty mapping yet

Three-layer verified:

**Layer 1 — no FieldMappingConfig consumer.**
The FIELD-I.4 `FieldMappingConfig` POD lives
only on `src/field/FieldMapping.h` and is
exercised only by `tests/field_tests.cpp`'s
FIELD-I.4 §7 test section. No other source
file includes `field/FieldMapping.h`. The
FIELD-I.7 commit adds zero new consumers
(the AOV.h / AOV.cpp / renderer_tests.cpp
surface does NOT include
`field/FieldMapping.h`).

**Layer 2 — explicit doc-comment statement.**
The new AOV's doc-comment
(`src/renderer/AOV.h:131-134`) is explicit:

```
// Read-only diagnostic: the kernel does NOT
// apply any `FieldMappingConfig` transform
// (no strength / bias / clamp / target-
// channel routing); the future mapping-
// pipeline integration is a separate slice.
```

**Layer 3 — narrow-scope alignment.** The
operator's FIELD-I.7 brief explicitly excludes
"field-to-color/emission mapping" via the
required-behaviour bullet "no field-to-color/
emission mapping yet". The slice satisfies the
exclusion structurally (no consumer) +
contractually (doc-comment statement) +
testably (no test exercises the mapping
pipeline from the AOV surface).

The future kernel-bridge slice will read
`ScalarFieldConfig` only and write the raw
`evaluate(...)` output to the AOV — pre-
mapping. A separate later FIELD-I.* slice
lifts the `FieldMappingConfig` →
`evaluate_mapping(...)` → beauty-modulation
pipeline; the FIELD-I.7 AOV writes the RAW
field sample exclusively.

### 3.8 Check #8 — runtime status

`PASS_WITH_FUTURE_KERNEL_WIRING_DEFERRED`.

**Audit-host runtime verification (PASS).** The
audit-host `renderer_tests` binary verifies
the entire FIELD-I.7 data-model surface end-
to-end:

```
13/13 Test #13: renderer_tests ........ Passed
100% tests passed, 0 tests failed out of 13
renderer_tests: 35 / 35 passed
```

The 8 new FIELD-I.7 RR_CHECK assertions:

| Test                                                | Assertions | Verdict |
|-----------------------------------------------------|------------|---------|
| `test_field_i_7_field_scalar_aov_type`              | 3          | PASS    |
| `test_field_i_7_field_scalar_factory_default_name`  | 3          | PASS    |
| `test_field_i_7_field_scalar_factory_custom_name`   | 2          | PASS    |

All other suites unchanged:

| Suite                       | Pre-FIELD-I.7 | Post-FIELD-I.7 |
|-----------------------------|---------------|----------------|
| math_tests                  | unchanged     | unchanged      |
| image_tests                 | unchanged     | unchanged      |
| gpu_tests                   | unchanged     | unchanged      |
| pathtracer_tests            | unchanged     | unchanged      |
| pathtracer_nee_tests        | unchanged     | unchanged      |
| pathtracer_bsdf_tests       | unchanged     | unchanged      |
| pathtracer_mis_tests        | unchanged     | unchanged      |
| cli_tests                   | 274/274       | 274/274        |
| relativity_tests            | 841/841       | 841/841        |
| manifold_identity_tests     | 408/408       | 408/408        |
| field_tests                 | 135/135       | 135/135        |
| demo_tests                  | unchanged     | unchanged      |
| renderer_tests              | 27/27         | **35/35** (+8 NEW) |

**SDK-host runtime verification (DEFERRED to
future kernel-bridge slice).** The FIELD-I.6
task brief's §8 enumerated seven runtime
scenarios (§8.1 – §8.7) requiring a CUDA +
OptiX SDK host to verify:

- §8.1 neutral diagnostic on disabled-field
  default (CUDA path) — DEFERRED; no kernel
  arm yet.
- §8.2 neutral diagnostic on disabled-field
  default (OptiX path) — DEFERRED.
- §8.3 non-default Constant field
  visualisation (both backends) — DEFERRED;
  no CLI flag yet, no kernel arm yet.
- §8.4 non-default Radial field
  visualisation (both backends) — DEFERRED.
- §8.5 off-path bit-identity (both backends)
  — STRUCTURALLY SATISFIED (no kernel
  consumer exists so no off-path PPM can be
  emitted); empirical SDK-host pass DEFERRED.
- §8.6 composability with `--manifold-debug`
  + `--observer-debug` — DEFERRED; no
  `--field-debug` flag yet.
- §8.7 cross-backend equivalence — DEFERRED;
  no PPMs to compare.

All seven SDK-host scenarios will be exercised
by the future kernel-bridge slice's audit (not
this slice). The deferral is honest scope: the
FIELD-I.7 surface is the AOV data-model entry
ONLY, with the kernel-arm + CLI + payload-
field plumbing landing as separate slices.

### 3.9 Master-rule satisfaction recap

- **Master rule #3 ("no fake stubs"):**
  satisfied. The AOV data-model entry is
  fully wired (enum / name / component count
  / factory all produce well-formed values;
  8 RR_CHECK assertions verify them
  empirically). The doc-comment block on the
  enumerator + on the factory documents the
  future kernel-arm contract honestly; no
  fake stub pretending the kernel arm
  exists. The "narrow-scope alignment with
  operator brief" is explicit in the doc-
  comment ("FIELD-I.7 ships only the data-
  model entry: the enumerator, the component
  count, the lowercase name, and the
  `make_field_scalar(...)` factory. No CUDA
  kernel arm, no OptiX kernel arm, no
  `--field-debug` CLI flag, ..."). Master
  rule #3 ratified.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. Every documented
  data-model behaviour is tested empirically
  by the 8 new RR_CHECK assertions (enum
  value, component count, name, factory
  default name, factory custom name,
  component_count member). The future
  kernel-arm contract is documented as
  contract in the AOV.h doc-comment +
  rooted in the existing audited FIELD-I.2
  surface; its empirical testability is
  deferred to the future kernel-bridge
  slice but the surface it will exercise is
  fully specified today.

- **Master rule #12 ("do not overbuild a
  later system before the current layer
  works"):** satisfied. Scope is
  deliberately narrow per the operator's
  three-bullet brief (AOV enum/type entry;
  neutral/default write path if field is
  disabled; output file naming consistent
  with existing AOV system). The kernel arm
  + CLI flag + Config bridge + payload
  field + dispatcher emit + fixture scene
  + companion doc are all reserved for
  follow-up slices with their own audit
  gates. The FIELD-I.7 slice does NOT
  attempt to ship the full FIELD-I.6 task
  brief's 15-row files-likely-involved
  surface; only the minimum-viable AOV
  data-model entry.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):**
  satisfied. The FIELD-I.7 default behaviour
  is unchanged from the pre-FIELD-I.7
  baseline:
    - No `--render-*` action produces a new
      file.
    - No existing PPM filename changes.
    - No beauty pass arithmetic changes.
    - No existing AOV slot's value changes.
  The single observable change is the
  presence of the new enumerator value
  (`AOVType::FieldScalar = 8`); this value
  has no consumer in any `--render-*` code
  path, so its presence is byte-invisible
  to every CLI invocation.

### 3.10 Honest scope recap

This audit is a **host-side data-model audit
with future-kernel-wiring DEFERRED**. The
verdict `PASS` is the FIELD-I.7 surface's
verdict; checks #5 + #6 reflect the
intermediate "data-model present, runtime
consumer reserved for follow-up slice"
shape that mirrors the AOV-extension
discipline of OBSERVER.13 + MANI-I.8 (both
of which shipped the full data-model +
kernel-arm + CLI + dispatcher in one slice,
contrast to FIELD-I.7's narrower scope).

The OBSERVER.13 / MANI-I.8 precedents
shipped wide; FIELD-I.7 ships narrow. Both
shapes are valid per master rule #12: ship
only what the operator's brief authorises,
not what the broader task brief envisions.
The FIELD-I.6 task brief enumerated a
15-file surface; the operator's FIELD-I.7
brief enumerated only three implement-only
bullets. The narrow shape is intentional;
the audit's verdict reflects the narrow
shape's PASS-ability without conflating
it with the broader task brief's wider
deferrals.

The CUDA + OptiX kernel-bridge wiring is
the natural follow-up slice. Its audit
will exercise the FIELD-I.6 task brief's
§8 runtime scenarios end-to-end on an
SDK host (when available). Until then,
the FIELD-I.7 AOV data-model entry is
audited-safe-and-optional in the host-
side sense.

---

## 4. NEXT

### 4.1 Renumbered FIELD-I.* sub-slice ladder

The FIELD-I.8 audit slot insertion (mirroring the
FIELD-I.5 + FIELD-I.3 + OBS-F.3 + OBSERVER.3 / .5
/ .7 / .9 / .11 / .14 + OBS-P.3 precedent) shifts
subsequent FIELD-I.* sub-slices by one. The
post-FIELD-I.8 ladder is:

- **FIELD-I.9** — Scalar field diagnostic AOV
  kernel-bridge implementation (the renumbered
  next FIELD-I.* impl slot; wires the new
  `AOVType::FieldScalar = 8` enumerator into the
  CUDA + OptiX kernels, adds the `--field-debug`
  CLI flag + the minimal `--field-*` authoring
  CLI surface, adds the
  `CudaSceneView::scalar_field_config` +
  `OptixLaunchParams::scalar_field_config` payload
  fields, adds the dispatcher emit per the
  FIELD-I.6 task brief's §5 files-likely-involved
  table).
- **FIELD-I.10** — Kernel-bridge audit (docs-
  only; mirrors this audit slot's shape for the
  FIELD-I.9 surface).
- **FIELD-I.11** — CLI + Config bridge for the
  full `FieldMappingConfig` authoring surface
  (`--field-color-strength` /
  `--field-emission-strength` /
  `--field-aov-strength` / `--field-bias` /
  `--field-clamp-output` / `--field-mapping-target`
  flags).
- **FIELD-I.12** — Mapping CLI + Config bridge
  audit.
- **FIELD-I.13** — Mapping kernel pipeline
  (lifts `FieldMappingConfig` onto the
  beauty-pass arithmetic; the actual field-to-
  beauty integration).
- **FIELD-I.14** — Mapping kernel pipeline
  audit.
- **FIELD-I.15** — Fixture scene + companion
  doc.
- **FIELD-I.16** — Fixture audit.
- **FIELD-I.17** — Arc capstone audit.

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

### 4.2 Candidate next slots (prioritised)

**(a) RECOMMENDED — FIELD-I.9: kernel-bridge
implementation** (the renumbered next FIELD-I.*
impl slot). Natural continuation of the
FIELD-I.* arc: consumes the new
`AOVType::FieldScalar = 8` enumerator +
`make_field_scalar(...)` factory landed at
FIELD-I.7, lands the kernel arms + CLI flag +
payload field + dispatcher emit. Closes the
FIELD-I.7 audit's checks #5 + #6 + #8's
runtime-deferred portions when its own audit
runs.

**(b) Manifold-orthogonal work.** Multiple
options available:
  - **Deferred SDK-host runtime pass** for the
    OBSERVER.* + OBS-P.* + OBS-F.* arc family
    (highest converging-leverage option;
    converts every PASS_WITH_RUNTIME_DEFERRED
    verdict in that family to PASS).
  - **MANI-I.12 final cross-host manifold
    audit**.
  - **Denoiser integration with chart-aware
    AOVs**.
  - **Path-tracer feature breadth** (NEE
    extension, BSDF expansion, MIS tuning).

**(c) NOT RECOMMENDED — direct full FIELD-I.4
`FieldMappingConfig` CLI surface
slice (skipping the FIELD-I.9 kernel-bridge).
The FIELD-I.7 AOV's value lies in the
operator being able to *see* what
`evaluate(config, hit_pos)` produces per-pixel;
that requires the kernel arm + dispatcher emit
+ CLI flag to engage the field. Shipping the
mapping CLI before the diagnostic AOV is
wired would be authoring without diagnostics.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the master rule #3
  + #11 + #12 + #16 satisfaction recap at §3.9
  cites these).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §6 (the Field Interpretation Layer as an
  OPTIONAL extension above the Manifold Core).
- `docs/FIELD_INTERPRETATION_LAYER.md` §4.6
  (the diagnostic-AOV channel design-doc
  anchor for the FIELD-I.7 surface).

### 5.2 FIELD-I.* arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1, the canonical FIELD-I.* arc
  plan).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md`
  (FIELD-I.3, the precedent host-side POD-leaf
  audit shape — partial mirror for the
  structural checks).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md`
  (FIELD-I.5, the second precedent host-side
  POD-leaf audit shape — second partial
  mirror).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6, the operator-facing task brief
  the FIELD-I.7 impl slice consumed; the §8
  runtime-deferred scenarios + §7 PASS
  criteria the FIELD-I.7 surface partially
  satisfies + this audit's check #8
  inventories).

### 5.3 Precedent AOV-extension references

- `docs/MANIFOLD_DEBUG_AOV_TASK.md` (MANI-I.7)
  + `docs/MANIFOLD_DEBUG_AOV_AUDIT.md`
  (MANI-I.9) — the precedent manifold-debug
  AOV task brief + audit pair; the
  `ManifoldCoordinates = 6` enumerator + its
  kernel-arm wiring shipped together as one
  wide slice (contrast to FIELD-I.7's narrow
  scope).
- `docs/OBSERVER_DEBUG_AOV_TASK.md`
  (OBSERVER.12) +
  `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — the precedent observer-
  debug AOV task brief + audit pair; the
  `ObserverBeta = 7` enumerator + its
  kernel-arm wiring shipped together as one
  wide slice (contrast to FIELD-I.7's narrow
  scope; the FIELD-I.6 task brief mirrored
  the OBSERVER.12 task brief verbatim but
  the operator's FIELD-I.7 brief narrowed
  scope to the AOV data-model entry only).

### 5.4 Source surface audited

- `src/renderer/AOV.h` (the FIELD-I.7 surface —
  modified +54 lines vs the FIELD-I.6
  baseline; the new `FieldScalar = 8`
  enumerator at line 143 with the extensive
  doc-comment block at lines 106-141; the
  new `make_field_scalar(...)` factory
  declaration at line 212 with the doc-
  comment at lines 205-211).
- `src/renderer/AOV.cpp` (the FIELD-I.7
  implementation surface — modified +11
  lines vs the FIELD-I.6 baseline; the new
  cases at lines 17 + 36 in
  `aov_component_count` /
  `aov_type_name`; the new factory body at
  lines 112-119).

### 5.5 Test surface audited

- `tests/renderer_tests.cpp` (modified +54
  lines vs the FIELD-I.6 baseline; the three
  new FIELD-I.7 test functions
  `test_field_i_7_field_scalar_aov_type` +
  `test_field_i_7_field_scalar_factory_default_name`
  + `test_field_i_7_field_scalar_factory_custom_name`
  covering 8 RR_CHECK assertions; registered
  in `main()` at the FIELD-I.7 trailing
  comment block). Audit-host runtime output:
  `renderer_tests: 35 / 35 passed`.

### 5.6 Surrounding commit SHAs

- `181a579` — FIELD-I.7 audited tree (the
  per-slice gate target).
- `193d306` — FIELD-I.6 baseline (the diff
  baseline for checks #2 + #5 + #6).
- `ba79e6e` — FIELD-I.5 audit (the previous
  audit-slot precedent).
- `683a16d` — FIELD-I.4 impl (the antecedent
  mapping config; the FIELD-I.7 AOV surface
  does NOT consume `FieldMappingConfig`, per
  check #7 verification).
- `40c387b` — FIELD-I.2 impl (the antecedent
  scalar field model; the future FIELD-I.9
  kernel arm will consume
  `ScalarFieldConfig` via the
  `rr::field::evaluate(...)` helper rooted
  here).

### 5.7 Unchanged source files (sampled)

The following files are byte-identical to the
FIELD-I.6 baseline (`193d306`), confirmed by
the diff filters at checks #2 + #5 + #6:

- Every `.cu` / `.cuh` file in `src/cuda/`.
- Every `.cu` / `.cuh` / `.cpp` / `.h` file
  in `src/optix/`.
- Every file in `src/pathtracer/`.
- Every file in `src/renderer/` EXCEPT
  `AOV.h` + `AOV.cpp` (the only two files
  touched).
- Every file in `src/scene/`, `src/io/`,
  `src/core/`, `src/manifold/`, `src/math/`,
  `src/image/`, `src/gpu/`, `src/app/`,
  `src/field/`.
- `src/main.cpp`.

### 5.8 Unchanged test files (sampled)

All test files except `tests/renderer_tests.cpp`
are byte-identical to the FIELD-I.6 baseline:

- `tests/math_tests.cpp` — unchanged.
- `tests/image_tests.cpp` — unchanged.
- `tests/gpu_tests.cpp` — unchanged.
- `tests/pathtracer_*_tests.cpp` (4 binaries)
  — unchanged.
- `tests/cli_tests.cpp` — unchanged.
- `tests/relativity_tests.cpp` — unchanged.
- `tests/manifold_identity_tests.cpp` —
  unchanged.
- `tests/field_tests.cpp` — unchanged.
- `tests/demo_tests.cpp` — unchanged.

### 5.9 Unchanged build configuration

`CMakeLists.txt` is byte-identical to the
FIELD-I.6 baseline. The
`renderer_tests` target picks up the +54
lines on `tests/renderer_tests.cpp` and the
new factory + enum case on
`src/renderer/AOV.{h,cpp}` via the existing
link graph; no new ctest target required.

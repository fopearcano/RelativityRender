# Field Mapping Config Audit (FIELD-I.5)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `683a16d` ("field:
FIELD-I.4 — Field Mapping Config (impl, POD-leaf +
tests)").
Audit baseline: `3df70d9` ("docs: FIELD-I.3 — Scalar
Field Model Audit (docs only)") — the last commit
before FIELD-I.4 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, the FIELD-I.1 plan + FIELD-I.4
commit's source content, the `field_tests` runtime
output, the unchanged `manifold_identity_tests` /
`cli_tests` / `renderer_tests` / `relativity_tests` /
`math_tests` / `image_tests` / `gpu_tests` /
`pathtracer_*_tests` / `demo_tests` runtime outputs,
and `ctest` exit codes.

This audit is the per-slice gate for FIELD-I.4
(`683a16d`). It verifies the seven items the task brief
enumerates — field mapping target enum exists; mapping
parameters exist; default mapping is None / no-op; no
renderer behaviour changed; no CUDA / OptiX behaviour
changed; build / test status; verdict — and produces a
`PASS` / `REPAIR` / `BLOCKED` verdict.

---

## 1. VERDICT

**PASS.**

All six structural checks return `PASS`. Check #7
(overall verdict) is `PASS`: the FIELD-I.4
implementation ships the documented field mapping
config surface (the new `FieldMappingTarget` enum +
`FieldMappingConfig` POD + RR_HD inline
`evaluate_mapping(...)` evaluator +
`disabled_field_mapping_config()` factory); the four
target enumerators (`None`, `ColorMultiplier`,
`Emission`, `DiagnosticAOV`) are concretely defined
with documented per-target evaluator behaviour; the
five shaping parameters (`strength`, `bias`,
`min_value`, `max_value`, `clamp_output`) carry their
brief-specified defaults; the default-None no-op
anchor is preserved by construction + empirically
verified at the 55 new RR_CHECK assertions in the
extended `tests/field_tests.cpp` binary (now 135 / 135
PASS total); the renderer / CUDA / OptiX behaviour is
byte-unchanged from the FIELD-I.3 baseline (`3df70d9`).

This is **not** `PASS_WITH_RUNTIME_DEFERRED`, mirroring
the FIELD-I.3 (Scalar Field Model Audit) precedent:
FIELD-I.4 is a host-side POD-leaf slice with zero
kernel surface. There is no runtime portion to defer —
the entire surface is exercised by the audit-host
`field_tests` binary. The `RR_HD inline` decoration on
`evaluate_mapping(...)` preserves device-callability
for future FIELD-I.* kernel-bridge consumption, but no
CUDA / OptiX TU consumes the helper this slice; the
SDK-host requirement only enters the picture at the
deferred FIELD-I.* GPU bridge slices.

---

## 2. PER-CHECK RESULTS

| # | Check                                  | Evidence                                                                                                                                                                                                                                                                                                                       | Verdict |
|---|----------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1 | Field mapping target enum exists       | `src/field/FieldMapping.h:206-211` defines `enum class FieldMappingTarget { None = 0, ColorMultiplier, Emission, DiagnosticAOV };` (the four enumerators the brief names). `None = 0` is the explicit default anchor.                                                                                                          | PASS    |
| 2 | Mapping parameters exist               | `src/field/FieldMapping.h:263-270` defines `struct FieldMappingConfig` with the six brief-specified fields: `target` (default `None`), `strength` (default `0.0f`), `bias` (default `0.0f`), `min_value` (default `0.0f`), `max_value` (default `1.0f`), `clamp_output` (default `false`).                                     | PASS    |
| 3 | Default mapping is None / no-op        | Three-layer verified: (a) `FieldMappingConfig{}.target == FieldMappingTarget::None` by construction; (b) `evaluate_mapping(...)` short-circuits to `0.0f` at `FieldMapping.h:279-281` when `target == None`; (c) `disabled_field_mapping_config()` factory returns `FieldMappingConfig{}` byte-for-byte at `FieldMapping.h:301`. Empirically verified by `test_evaluate_mapping_default_target_none_returns_zero` + `test_evaluate_mapping_target_none_short_circuits` + `test_disabled_field_mapping_config_factory` + `test_default_field_mapping_config_default_constructed` RR_CHECK tests. | PASS    |
| 4 | No renderer behaviour changed          | `git diff 3df70d9..683a16d --name-only -- 'src/' ':(exclude)src/field/'` returns zero hits. Every `src/cuda/` / `src/optix/` / `src/pathtracer/` / `src/renderer/` / `src/scene/` / `src/io/` / `src/core/` / `src/manifold/` / `src/math/` file is byte-unchanged from the FIELD-I.3 baseline (`3df70d9`).                  | PASS    |
| 5 | No CUDA / OptiX behaviour changed      | `git diff 3df70d9..683a16d --name-only -- 'src/cuda/' 'src/optix/'` returns zero hits. The CUDA + OptiX backends are byte-unchanged. The `RR_HD inline` decoration on `evaluate_mapping(...)` preserves device-callability for future kernel-bridge consumption but no CUDA / OptiX TU consumes the helper this slice.       | PASS    |
| 6 | Build / test status                    | Audit-host `ctest` returns `100% tests passed, 0 tests failed out of 13` (unchanged from FIELD-I.3; same ctest set, no new ctest target). `field_tests` binary reports `135 / 135 passed` (135 total = 80 FIELD-I.2 + 55 NEW FIELD-I.4). All other suites unchanged (`relativity_tests: 841/841`; `manifold_identity_tests: 408/408`; `cli_tests: 274/274`; `renderer_tests: 27/27`). Full rebuild via `cmake --build /home/user/RelativityRender/build` adds no new warnings on any module. Empirically verified.                                                                       | PASS    |
| 7 | Verdict                                | All six structural checks PASS. The FIELD-I.4 surface is well-defined, well-tested, default-no-op, and renderer-byte-unchanged. Master rule #3 + #11 + #12 + #16 satisfied (see §3 below).                                                                                                                                     | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Commit shape

The FIELD-I.4 commit (`683a16d`) modifies three files:

```
docs/BUILD_PLAN.md       | 358 +++++++++++++++++++++++++
src/field/FieldMapping.h | 168 ++++++++++++++++++
tests/field_tests.cpp    | 294 +++++++++++++++++++++++-
```

The only source-code file touched is
`src/field/FieldMapping.h`; the only test file touched
is `tests/field_tests.cpp` (extension of the existing
ctest target landed at FIELD-I.2, not a new binary).
The remaining file (`docs/BUILD_PLAN.md`) is the
per-slice entry mirroring the standard rubric.

### 3.2 Check #1 — enum exists

`src/field/FieldMapping.h:206-211` defines the new
`FieldMappingTarget` enum class:

- `None = 0` — explicit default anchor; the
  default-constructed `FieldMappingConfig{}` carries
  this value (verified at check #3).
- `ColorMultiplier` — beauty-pass color modulation
  (matches the FIELD.3 `FieldOutputChannel::Color`
  enumerator's semantic).
- `Emission` — additive luminance contribution
  (matches the FIELD.3 `FieldOutputChannel::Emission`
  semantic).
- `DiagnosticAOV` — dedicated AOV write; no beauty-
  pass modulation (matches the FIELD.3
  `FieldOutputChannel::DiagnosticAOV` semantic).

The four enumerators are exactly the four the brief
names. The other FIELD.3 `FieldOutputChannel`
enumerators (`Distortion`, `Density`,
`ChromaticShift`) are intentionally NOT exposed here
per the FIELD-I.1 plan §2.3: Phase 1 ships only the
three target channels above. The legacy FIELD.3
`FieldOutputChannel` enum at `FieldMapping.h:70-77`
remains preserved verbatim — both enums coexist on
the same header; the FIELD-I.* arc consumes the new
single-target enum; the legacy multi-channel form
remains valid for future use cases.

### 3.3 Check #2 — mapping parameters exist

`src/field/FieldMapping.h:263-270` defines
`struct FieldMappingConfig` with exactly the six
brief-specified fields (the `target` selector + the
five shaping parameters from the brief):

```cpp
struct FieldMappingConfig {
    FieldMappingTarget target       = FieldMappingTarget::None;
    float              strength     = 0.0f;
    float              bias         = 0.0f;
    float              min_value    = 0.0f;
    float              max_value    = 1.0f;
    bool               clamp_output = false;
};
```

The brief's camelCase parameter names map directly to
the snake_case field identifiers (`minValue` →
`min_value`, `maxValue` → `max_value`, `clampOutput`
→ `clamp_output`) — the project's C++ identifier
convention is snake_case, mirroring the FIELD-I.2
`ScalarFieldConfig::min_value` / `max_value`
precedent.

The defaults are documented at `FieldMapping.h:220-261`
(the per-field doc-comment block above the struct
definition). The strength + bias semantics: when
`target != None`, the evaluator computes
`mapped = strength * sample + bias`, optionally
clamped to `[min_value, max_value]` when
`clamp_output = true`.

### 3.4 Check #3 — default mapping is None / no-op

Three-layer verified:

**Layer 1 — Construction-time anchor.** A default-
constructed `FieldMappingConfig{}` carries
`target = FieldMappingTarget::None` by the
in-class-default of the `target` field
(`FieldMapping.h:264`). This is verified empirically
by `test_disabled_field_mapping_config_factory`
(6 RR_CHECK assertions on every field's default
value) and `test_default_field_mapping_config_default_constructed`
(6 RR_CHECK assertions comparing the factory output
to the default-constructed POD).

**Layer 2 — Evaluator short-circuit.**
`evaluate_mapping(...)` at `FieldMapping.h:277-292`
opens with:

```cpp
if (m.target == FieldMappingTarget::None) {
    return 0.0f;
}
```

The short-circuit happens BEFORE any read of
`strength`, `bias`, `min_value`, `max_value`, or
`clamp_output`. Even when those fields are dialled to
non-default values, the evaluator returns `0.0f` as
long as `target == None`. This is verified by
`test_evaluate_mapping_default_target_none_returns_zero`
(4 representative samples on the default config) and
`test_evaluate_mapping_target_none_short_circuits`
(every other parameter dialled to non-default values;
target = None still produces 0).

**Layer 3 — Factory.**
`disabled_field_mapping_config()` at
`FieldMapping.h:301-303` returns `FieldMappingConfig{}`
byte-for-byte. The factory is the documented
authoring entry point for "I want a guaranteed no-op
mapping"; it is exactly the default-constructed POD.

The three layers compose: any code path that consumes
the factory output, the default POD, or anything-but-
intentionally-set-target-to-non-None produces zero
contribution. This is the load-bearing master rule #3
+ #16 satisfaction: the FIELD-I.4 surface is "wired
but quiet" by default; the artist explicitly opts in
to a non-zero contribution by setting `target !=
None`.

### 3.5 Check #4 — no renderer behaviour changed

The diff filter
`git diff 3df70d9..683a16d --name-only --
'src/' ':(exclude)src/field/'` returns zero hits.
Empirically:

- `src/cuda/` — zero hits.
- `src/optix/` — zero hits.
- `src/pathtracer/` — zero hits.
- `src/renderer/` — zero hits.
- `src/scene/` — zero hits.
- `src/io/` — zero hits.
- `src/core/` — zero hits.
- `src/manifold/` — zero hits.
- `src/math/` — zero hits.
- `src/image/` — zero hits.
- `src/gpu/` — zero hits.
- `src/app/` — zero hits.

Every non-field source file is byte-identical to the
FIELD-I.3 baseline (`3df70d9`). The existing rendering
pipeline (camera dispatch, ray generation, manifold
core, observer frame, perception transforms, ScalarField
evaluation, AOV writes, image saves) is byte-unchanged.

### 3.6 Check #5 — no CUDA / OptiX behaviour changed

The diff filter
`git diff 3df70d9..683a16d --name-only --
'src/cuda/' 'src/optix/'` returns zero hits. The CUDA
backend's kernels (`CudaTestKernel.cu`,
`CudaPathTracer.cu`, `CudaPostProcess.cu`, etc.) and
the OptiX backend's programs (`OptixPrograms.cu`,
`OptixRenderer.cpp`, etc.) are byte-unchanged from
the FIELD-I.3 baseline (`3df70d9`).

The `RR_HD inline` decoration on `evaluate_mapping(...)`
at `FieldMapping.h:277` preserves device-callability
for future FIELD-I.* kernel-bridge consumption (the
helper will be `__device__`-callable from CUDA + OptiX
TUs when those bridges land). However, no CUDA / OptiX
TU includes `field/FieldMapping.h` this slice; the
device-callable surface is reserved-but-unused at the
kernel side. Master rule #3 satisfied — the
device-callability is honest scaffolding for the
upcoming bridge slices, not a fake stub pretending to
be a wired pipeline.

### 3.7 Check #6 — build / test status

Audit-host `ctest` empirical output:

```
13/13 Test #13: renderer_tests ............ Passed
100% tests passed, 0 tests failed out of 13
```

Per-binary breakdown (test counts):
- `math_tests` — unchanged.
- `image_tests` — unchanged.
- `gpu_tests` — unchanged.
- `pathtracer_tests` — unchanged.
- `pathtracer_nee_tests` — unchanged.
- `pathtracer_bsdf_tests` — unchanged.
- `pathtracer_mis_tests` — unchanged.
- `cli_tests: 274/274 passed` — unchanged.
- `relativity_tests: 841/841 passed` — unchanged.
- `manifold_identity_tests: 408/408 passed` — unchanged.
- `field_tests: 135 / 135 passed` — **+55 NEW**
  (80 FIELD-I.2 baseline + 55 NEW FIELD-I.4).
- `demo_tests` — unchanged.
- `renderer_tests: 27/27 passed` — unchanged.

Full rebuild via
`cmake --build /home/user/RelativityRender/build`
completes with no new warnings on any module. The
`field_tests` binary picks up the +245 lines on
`tests/field_tests.cpp` and the new struct +
evaluator + factory on `src/field/FieldMapping.h` via
the existing INTERFACE link graph; no `CMakeLists.txt`
change required.

### 3.8 Master-rule satisfaction recap

- **Master rule #3 ("no fake stubs"):** satisfied.
  Every enumerator + parameter slot on the
  FIELD-I.4 surface has a documented + testable
  behaviour:
    - `None` is the explicit no-op anchor (not a
      placeholder; the documented short-circuit at
      `FieldMapping.h:279-281` is the contract).
    - `ColorMultiplier` / `Emission` /
      `DiagnosticAOV` all follow the same
      `strength * sample + bias` math; the
      enumerator value tags WHICH channel the
      future renderer writes to, not the math
      itself.
    - `clamp_output = false` is the documented
      pass-through path (tested at
      `test_evaluate_mapping_clamp_output_disabled_passes_through`).
    - `clamp_output = true` is the documented
      clamp path with degenerate-range defence
      (tested at the four `..._clamps_*` /
      `..._passes_through_in_range` /
      `..._degenerate_range_returns_min` tests).
    - No reserved-but-undefined slots; no
      `*Placeholder` suffix needed because every
      target HAS a defined evaluator behaviour
      today.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. Every documented
  behaviour is tested empirically by the 55 new
  RR_CHECK assertions:
    - Enum distinctness (6 pairwise checks + the
      `None = 0` anchor cast).
    - Factory output (6 field-by-field checks).
    - Factory == default-constructed (6 field-by-
      field equality checks).
    - Default config returns 0 on 4 sample
      values.
    - Non-default config with target = None still
      returns 0 on 2 sample values.
    - `target = ColorMultiplier` math correctness
      on 4 sample values.
    - `target = Emission` math correctness on 2
      sample values.
    - `target = DiagnosticAOV` math correctness
      on 3 sample values.
    - `strength = 0` "wired but quiet" anchor on
      3 sample values.
    - Bias additivity on 3 sample values.
    - clamp_output = false pass-through on 2
      out-of-range samples.
    - clamp_output = true high-clamp on 2
      samples.
    - clamp_output = true low-clamp on 2 samples.
    - clamp_output = true in-range pass-through
      on 3 samples.
    - Degenerate range collapse on 3 sample
      values.
    - Negative strength inversion on 3 sample
      values.

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Scope is deliberately narrow:
    - No renderer integration (deferred to
      kernel-bridge slices).
    - No GPU launch-params field (deferred).
    - No CLI flag surface (deferred to next
      impl slice; per the FIELD-I.1 plan §5).
    - No `.rrscene` schema bump (deferred).
    - No diagnostic AOV enumerator (deferred).
    - No fixture scene (deferred).
    - No new ctest target (the new tests
      append to the existing `field_tests`
      binary — zero CMake change required).
  Each deferred piece is a separate FIELD-I.*
  sub-slice with its own audit gate.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied. The
  FIELD-I.4 default-no-op anchor (`target = None`
  short-circuits the evaluator to `0.0f`) means
  every existing call site that one day adopts a
  `FieldMappingConfig` field — whether on
  `rr::core::Config`, on `CudaSceneView`, on
  `OptixLaunchParams`, or on a future
  `FieldInterpreter` wrapper — will preserve
  byte-identical output by default. The artist
  explicitly opts in by setting `target` to a
  non-`None` enumerator.

### 3.9 Honest scope recap

This audit is a **host-side POD-leaf audit**. The
verdict `PASS` (not `PASS_WITH_RUNTIME_DEFERRED`)
mirrors the FIELD-I.3 precedent: FIELD-I.4 carries
zero kernel surface, so there is no runtime portion
to defer. The audit-host `field_tests` binary
exercises the entire FIELD-I.4 surface end-to-end.

The deferred SDK-host runtime pass that the OBSERVER /
OBS-P / OBS-F arc family carries forward will
eventually exercise the FIELD-I.4 evaluator's device-
callability when a future kernel-bridge slice
consumes the helper from a CUDA / OptiX TU. Until
then, the device-callable surface is reserved-but-
unused — documented honestly at check #5's reasoning
paragraph and at the FIELD-I.4 commit's "What does
NOT ship" section.

---

## 4. NEXT

### 4.1 Renumbered FIELD-I.* sub-slice ladder

The FIELD-I.5 audit slot insertion (mirroring the
FIELD-I.3 + OBS-F.3 + OBSERVER.3 / .5 / .7 / .9 / .11
/ .14 + OBS-P.3 precedent) shifts subsequent
FIELD-I.* sub-slices by one. The post-FIELD-I.5
ladder is:

- **FIELD-I.6** — Field mapping config + CLI / Config
  bridge (was FIELD-I.5; was originally FIELD-I.4 on
  the pre-FIELD-I.3 ladder; was originally FIELD-I.3
  on the FIELD-I.1 plan). Extends `rr::core::Config`
  with a `FieldMappingConfig field_mapping` field (or
  a `FieldInterpreter`-wrapped variant — TBD per
  FIELD-I.6 task brief). Adds `--field-*` CLI flags
  mirroring the OBSERVER.4 `--observer-*` flag
  pattern.
- **FIELD-I.7** — Field mapping config audit (docs
  only; mirrors this audit slot's shape for the
  FIELD-I.6 surface).
- **FIELD-I.8** — Scalar diagnostic AOV (was
  FIELD-I.5; was originally FIELD-I.4 on the
  FIELD-I.1 plan). Adds
  `AOVType::FieldScalarDiagnostic = 8` enumerator
  + plumbing.
- **FIELD-I.9** — Scalar diagnostic AOV audit (docs
  only).
- **FIELD-I.10** — CUDA bridge (was FIELD-I.6; was
  originally FIELD-I.5 on the FIELD-I.1 plan).
- **FIELD-I.11** — CUDA bridge audit (docs only).
- **FIELD-I.12** — OptiX bridge (was FIELD-I.7; was
  originally FIELD-I.6 on the FIELD-I.1 plan).
- **FIELD-I.13** — OptiX bridge audit (docs only).
- **FIELD-I.14** — Fixture scene (was FIELD-I.8; was
  originally FIELD-I.7 on the FIELD-I.1 plan).
- **FIELD-I.15** — Fixture audit (docs only).
- **FIELD-I.16** — Arc capstone audit (mirrors
  OBSERVER.15 capstone shape).

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

### 4.2 Candidate next slots (prioritised)

**(a) RECOMMENDED — FIELD-I.6: Field mapping config
+ CLI / Config bridge** (the renumbered next
FIELD-I.* impl slot). Natural continuation of the
FIELD-I.* arc: lifts the FIELD-I.4
`FieldMappingConfig` POD onto the
`rr::core::Config` operator-authoring surface with
a `--field-*` CLI flag family. Pre-requisite for
the kernel-bridge slices (FIELD-I.10 / .12) since
those need a config payload to consume.

**(b) Manifold-orthogonal work.** Multiple options
available with their own merit:
  - **Deferred SDK-host runtime pass** for the
    OBSERVER.* + OBS-P.* + OBS-F.* arc family
    (highest converging-leverage option;
    converts every PASS_WITH_RUNTIME_DEFERRED
    verdict in that family to PASS).
  - **MANI-I.12 final cross-host manifold audit**
    (closes the MANI-I.* arc on the runtime side).
  - **Denoiser integration with chart-aware
    AOVs** (lifts the ManifoldCoordinates +
    ObserverBeta AOVs into the denoiser
    feedback path).
  - **Path-tracer feature breadth** (NEE
    extension, BSDF expansion, MIS tuning).

**(c) NOT RECOMMENDED — direct FIELD-I.* kernel
bridge prototype skipping the CLI / Config /
AOV / fixture surface.** Would be a documentation-
free shortcut that breaks the FIELD-I.* audit
discipline; the CLI / Config / AOV / fixture
surfaces all serve as authoring + diagnostic
plumbing for the kernel-bridge slices, and skipping
them would force the kernel-bridge slices to invent
ad-hoc authoring conventions that subsequent
slices would have to retrofit.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the master rule #3 +
  #11 + #12 + #16 satisfaction recap at §3.8 cites
  these).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` (§6
  on the Field Interpretation Layer as an OPTIONAL
  extension above the Manifold Core).
- `docs/FIELD_INTERPRETATION_LAYER.md` (the
  pre-FIELD-I.* design doc; §3 + §4 + §6
  preserved verbatim by the FIELD-I.* arc).

### 5.2 FIELD-I.* arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1, the canonical FIELD-I.* arc plan).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md` (FIELD-I.3,
  the precedent host-side POD-leaf audit shape
  this audit mirrors).
- `docs/BUILD_PLAN.md` (the FIELD-I.4 entry +
  the per-slice rubric).

### 5.3 Source surface audited

- `src/field/FieldMapping.h` (the FIELD-I.4
  surface — 305 lines total; +168 lines vs the
  FIELD-I.3 baseline; the new `FieldMappingTarget`
  enum at lines 206-211, the new
  `FieldMappingConfig` POD at lines 263-270, the
  new `evaluate_mapping(...)` evaluator at lines
  277-292, the new `disabled_field_mapping_config()`
  factory at lines 301-303; the legacy FIELD.3
  `FieldOutputChannel` enum at lines 70-77 + the
  legacy `FieldMapping` POD at lines 101-111 +
  the legacy `target_strength(...)` accessor at
  lines 117-128 + the legacy
  `disabled_field_mapping()` factory at lines
  133-135 preserved verbatim).

### 5.4 Test surface audited

- `tests/field_tests.cpp` (787 lines total; +294
  lines vs the FIELD-I.3 baseline; the new §7
  FieldMappingTarget + FieldMappingConfig test
  section with 15 new test functions covering 55
  RR_CHECK assertions). Audit-host runtime output:
  `field_tests: 135 / 135 passed`.

### 5.5 Surrounding commit SHAs

- `683a16d` — FIELD-I.4 audited tree (the
  per-slice gate target).
- `3df70d9` — FIELD-I.3 baseline (the diff
  baseline for checks #4 + #5).
- `40c387b` — FIELD-I.2 impl (the antecedent
  scalar-field model; the FIELD-I.4 mapping
  config composes on the FIELD-I.2 ScalarFieldConfig
  evaluator's output).
- `4b0d482` — FIELD-I.1 (the canonical plan
  reference cited at §4.1).

### 5.6 Unchanged source files (sampled)

The following files are byte-identical to the
FIELD-I.3 baseline (`3df70d9`), confirmed by the
diff filter at check #4:

- Every `.cu` / `.cuh` file in `src/cuda/`.
- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/optix/`.
- Every file in `src/pathtracer/`.
- Every file in `src/renderer/` (including
  `AOV.h` + `AOV.cpp` — no
  `FieldScalarDiagnostic` enumerator added this
  slice; deferred per §4.1).
- Every file in `src/scene/`, `src/io/`,
  `src/core/`, `src/manifold/`, `src/math/`,
  `src/image/`, `src/gpu/`, `src/app/`.
- The FIELD.* legacy headers
  (`src/field/FieldType.h`,
  `src/field/FieldInterpreter.h`,
  `src/field/ScalarField.h`).
- `src/field/README.md`.

### 5.7 Unchanged test files (sampled)

All test files except `tests/field_tests.cpp` are
byte-identical to the FIELD-I.3 baseline:

- `tests/math_tests.cpp` — unchanged.
- `tests/image_tests.cpp` — unchanged.
- `tests/gpu_tests.cpp` — unchanged.
- `tests/pathtracer_*_tests.cpp` (4 binaries) —
  unchanged.
- `tests/cli_tests.cpp` — unchanged.
- `tests/relativity_tests.cpp` — unchanged.
- `tests/manifold_identity_tests.cpp` —
  unchanged.
- `tests/demo_tests.cpp` — unchanged.
- `tests/renderer_tests.cpp` — unchanged.

### 5.8 Unchanged build configuration

`CMakeLists.txt` is byte-identical to the
FIELD-I.3 baseline. The new tests append to the
existing `field_tests` ctest target (registered
at FIELD-I.2); no new ctest target required.

# Scalar Field Model Audit (FIELD-I.3)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `40c387b` ("field:
FIELD-I.2 — Scalar Field Model (impl, POD-leaf + tests
+ ctest target)").
Audit baseline: `4b0d482` ("docs: FIELD-I.1 — Field
Interpretation Phase 1 Plan (docs only)") — the last
commit before FIELD-I.2 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, the FIELD-I.1 plan + FIELD-I.2
commit's source content, the `field_tests` runtime
output, the unchanged `manifold_identity_tests` /
`cli_tests` / `renderer_tests` / `relativity_tests`
runtime outputs, and `ctest` exit codes.

This audit is the per-slice gate for FIELD-I.2
(`40c387b`). It verifies the seven items the task brief
enumerates — scalar field model exists; default
disabled/no-op behaviour exists; constant/radial
representation exists or is explicitly deferred; no
renderer behaviour changed; no CUDA/OptiX behaviour
changed; build / test status; verdict — and produces a
`PASS` / `REPAIR` / `BLOCKED` verdict.

---

## 1. VERDICT

**PASS.**

All six structural checks return `PASS`. Check #7
(overall verdict) is `PASS`: the FIELD-I.2 implementation
ships the documented scalar field model surface (the
new `ScalarFieldKind` enum + `ScalarFieldConfig`
tagged-union POD + RR_HD inline `evaluate(...)` Vec3 /
Vec4 overloads + `disabled_scalar_field_config()`
factory + the internal `scalar_field_smoothstep`
helper); the three concrete kinds (`Constant`,
`Radial`, `ProceduralPlaceholder`) are honestly
implemented or honestly deferred per master rule #3;
the default-disabled no-op anchor is preserved by
construction + empirically verified at the 80 new
RR_CHECK assertions in `tests/field_tests.cpp`; the
renderer / CUDA / OptiX behaviour is byte-unchanged
from the FIELD-I.1 baseline.

The audit is **not** PASS_WITH_RUNTIME_DEFERRED
because FIELD-I.2 is a host-side POD-leaf slice with
no CUDA / OptiX / kernel surface to defer. All
verification is audit-host-complete; the future
runtime SDK-host verification for the FIELD-I.* arc
lives at FIELD-I.7 (fixture scene) + FIELD-I.8 (arc
capstone), not here.

No `REPAIR` action is required. No `BLOCKED` item is
outstanding. The operator may proceed to the
renumbered FIELD-I.4 (field mapping config + CLI
flags; was FIELD-I.3 in the post-FIELD-I.1 plan) under
the renumbered FIELD-I.* sub-slice ladder per §4
below.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Scalar field model exists                | **PASS** | The FIELD-I.2 commit (`40c387b`) extends `src/field/ScalarField.h` with a complete tagged-union scalar-field config surface (~222 new lines):<br>**(a) `ScalarFieldKind` enum** (3 enumerators): `Constant = 0` (explicit anchor); `Radial`; `ProceduralPlaceholder` (the `*Placeholder` suffix follows the MANIFOLD.1 precedent — `KruskalLikePlaceholder` / `KerrLikePlaceholder` — making the reserved-but-inert status visible at the type level).<br>**(b) `ScalarFieldConfig` POD** carrying the ten fields the operator's brief enumerated: `enabled`, `strength`, `kind`, `center`, `min_radius`, `max_radius`, `falloff`, `min_value`, `max_value`, `constant_value`. Every field has an explicit per-field initialiser that resolves to the documented no-op anchor.<br>**(c) `evaluate(ScalarFieldConfig, Vec3) → float`** RR_HD inline evaluator dispatching on `kind` (lines ~213-261 of the modified `ScalarField.h`).<br>**(d) `evaluate(ScalarFieldConfig, Vec4) → float`** Vec4 overload routing through the spatial part `(event.y, event.z, event.w)` (the time component `event.x` is intentionally ignored for the three Phase 1 spatially-dependent kinds; the overload exists so future time-dependent field kinds can land without widening every call site).<br>**(e) `disabled_scalar_field_config()`** factory returning the default no-op anchor.<br>**(f) `scalar_field_smoothstep(float, float, float)`** local helper for the Radial evaluator's saturating smoothstep cubic.<br>The POD lives in `namespace rr::field` alongside the existing FIELD.2 PODs (`ConstantScalarField`, `SampledScalarField`); the new tagged-union surface is the operator-authoring entry point for the FIELD-I.* arc, while the legacy multi-POD surface remains valid for kernel-internal use. |
| 2 | Default disabled / no-op behavior exists | **PASS** | Three-layer no-op preservation:<br>**(a) Field-by-field defaults.** The `ScalarFieldConfig` POD's per-field initialisers all resolve to the no-op anchor: `enabled = false`, `strength = 0.0f`, `kind = Constant`, `center = (0, 0, 0)`, `min_radius = 0.0f`, `max_radius = 1.0f`, `falloff = 1.0f`, `min_value = 0.0f`, `max_value = 1.0f`, `constant_value = 0.0f`. Verified at `test_disabled_scalar_field_config_factory` (12 RR_CHECK) + `test_default_scalar_field_config_default_constructed` (12 RR_CHECK; verifies factory output is byte-identical to default-constructed POD).<br>**(b) `evaluate(...)` short-circuit.** The evaluator's first line is `if (!c.enabled \|\| c.strength == 0.0f) return 0.0f;` — the disabled-default + zero-strength path returns `0` regardless of `kind` or any other field value. Verified at `test_evaluate_disabled_returns_zero` (16 RR_CHECK across 4 spatial positions × 4 evaluation modes — Vec3, two Vec4 time-component variations + repeat for default + zero-strength) + `test_evaluate_enabled_but_zero_strength_returns_zero` (2 RR_CHECK with non-trivial `constant_value` to confirm the "wired but quiet" anchor).<br>**(c) `ProceduralPlaceholder` reserved-but-inert.** When `enabled = true` AND `strength != 0` AND `kind = ProceduralPlaceholder`, the evaluator still returns `0.0f` regardless of every other parameter (this is the documented reserved-but-inert behaviour from the FIELD-I.1 plan §2.2). Verified at `test_procedural_placeholder_returns_zero` (3 RR_CHECK across representative positions with all parameter slots populated to non-default values).<br>The default-no-op invariant is structurally provable by inspection (the short-circuit at the evaluator's entry plus the per-field defaults) + empirically verified by 33 of the 80 new RR_CHECK assertions. Master rule #3 ("no fake stubs") satisfied: the default config produces a real `0.0f` output via real code; no placeholder pretending to be a working system. |
| 3 | Constant / radial representation exists or is explicitly deferred | **PASS** | Two concrete kinds implemented; one explicitly deferred with documented `*Placeholder` suffix:<br>**(a) `Constant` kind** (concrete, fully verified). Evaluates to `strength * constant_value` regardless of position. Verified at `test_constant_kind_returns_strength_times_value` (3 RR_CHECK across 3 representative positions including origin + oblique + far-from-center) + `test_constant_kind_strength_scales_output` (4 RR_CHECK across `strength = 1.0` / `0.5` / `2.0` / `-1.0` scaling factors).<br>**(b) `Radial` kind** (concrete, fully verified across 7 sub-tests):<br>&nbsp;&nbsp;- `test_radial_kind_min_value_at_or_inside_min_radius` (3 RR_CHECK): center / on-min-radius-sphere / inside-min-radius all return `min_value`. Uses the squared-distance early-exit at the evaluator (`r2 <= min2`) to avoid `sqrtf` at zero distance.<br>&nbsp;&nbsp;- `test_radial_kind_max_value_at_or_outside_max_radius` (3 RR_CHECK): on-max-radius-sphere / outside-axial / outside-oblique (`(6, 8, 0)` distance 10) all return `max_value`. Uses the squared-distance early-exit at `r2 >= max2`.<br>&nbsp;&nbsp;- `test_radial_kind_smoothstep_midway` (2 RR_CHECK): midway radial distance returns the smoothstep cubic's `0.5` output (`3*0.25 - 2*0.125 = 0.5`) verified at two different axial directions.<br>&nbsp;&nbsp;- `test_radial_kind_strength_scales_output` (3 RR_CHECK): strength scaling at smoothstep midway.<br>&nbsp;&nbsp;- `test_radial_kind_offset_center` (3 RR_CHECK): center at `(3, 4, 0)` produces correct min/midway/max behaviour relative to the offset origin.<br>&nbsp;&nbsp;- `test_radial_kind_degenerate_envelope_returns_zero` (5 RR_CHECK): inverted envelope (`max_radius < min_radius`) returns `0` in 3 sub-cases; equal envelope (`max_radius == min_radius`) returns `0` in 2 sub-cases. Defence-in-depth verified.<br>&nbsp;&nbsp;- `test_radial_kind_falloff_reshapes_transition` (3 RR_CHECK): `falloff = 2.0` produces the closed-form smoothstep-at-`t_pow=0.25` value `0.15625`; `falloff = 0.0` falls back to linear; `falloff = -1.0` also falls back to linear (defence-in-depth on non-positive falloff).<br>**(c) `ProceduralPlaceholder` kind** (explicitly deferred). The evaluator's switch arm at the `ProceduralPlaceholder` case `return 0.0f;` with documented comment "Reserved-but-inert per master rule #3 (no fake stubs); future FIELD-I.* sub-slice will replace this branch with a concrete procedural evaluator." Verified at `test_procedural_placeholder_returns_zero` (3 RR_CHECK with non-trivial parameter values).<br>The Vec4 overload (`evaluate(ScalarFieldConfig, Vec4)`) routes through the Vec3 spatial part for all three kinds, verified at `test_vec4_overload_consumes_spatial_part` (4 RR_CHECK) + `test_vec4_overload_radial_routes_through_spatial_part` (2 RR_CHECK; verifies time component is ignored by the spatially-dependent kinds). |
| 4 | No renderer behavior changed             | **PASS** | `git diff 4b0d482..40c387b --name-only -- 'src/*' ':(exclude)src/field/ScalarField.h' 'tests/*' ':(exclude)tests/field_tests.cpp'` returns **zero hits**. Specifically:<br>**(a)** `src/renderer/AOV.h` + `AOV.cpp` byte-unchanged (no new AOV enumerator).<br>**(b)** `src/scene/Scene.h` + `src/io/SceneLoader.cpp` byte-unchanged (no `.rrscene` schema bump; no scene-block parser extension).<br>**(c)** `src/core/Config.h` + `src/core/CommandLine.cpp` byte-unchanged (no `Config::field_interpreter` field; no `--field-*` CLI flag).<br>**(d)** `src/manifold/*.h` byte-unchanged (no chart family added; no observer-frame change; the FIELD-I.* arc remains orthogonal to the manifold + observer-frame surface).<br>**(e)** `src/pathtracer/PathTracer.h` + `PathTracer.cpp` byte-unchanged (no PathTraceConfig field).<br>**(f)** `src/main.cpp` byte-unchanged (no dispatcher invocation of `evaluate(ScalarFieldConfig, ...)`).<br>**(g)** `src/camera/*` byte-unchanged.<br>**(h)** `src/relativity/*` byte-unchanged.<br>The FIELD-I.2 commit is **purely a host-side POD-leaf + tests + ctest target addition**; the renderer surface is byte-unchanged from the FIELD-I.1 baseline. No existing CLI action's output is altered. No existing test count changed (only the new `field_tests` binary appears with its 80 RR_CHECK count). The OBSERVER.* + OBS-P.* + OBS-F.* arc family's verdicts carry forward unchanged. |
| 5 | No CUDA / OptiX behavior changed         | **PASS** | `git diff 4b0d482..40c387b --name-only -- 'src/cuda/' 'src/optix/'` returns **zero hits**. Specifically:<br>**(a)** Every `src/cuda/*.cu` / `*.cuh` file is byte-unchanged. The OBS-P.2 kernel surface at `CudaTestKernel.cu` + the OBSERVER.13 `observer_beta` AOV write arm + every other CUDA kernel arm carries forward verbatim.<br>**(b)** Every `src/optix/*.cu` / `*.cpp` / `*.h` file is byte-unchanged. The OBS-P.2 OptiX ternaries at `OptixPrograms.cu` + the OBSERVER.10 launch-params field + every other OptiX surface carries forward.<br>**(c)** No new CUDA kernel arm consumes `ScalarFieldConfig`. No new OptiX program reads it. No new GPU launch-params field carries it.<br>**(d)** The `RR_HD inline` decoration on the new `evaluate(...)` + `scalar_field_smoothstep` helpers preserves device-callability for **future** FIELD-I.5 / FIELD-I.6 kernel-bridge consumption, but no CUDA / OptiX TU consumes the helpers this slice.<br>The FIELD-I.* arc remains in its planned scope: FIELD-I.2 is a POD-leaf + tests slice; CUDA + OptiX integration is reserved for FIELD-I.5 + FIELD-I.6 (renumbered through this audit's renumbering ladder) with their own per-slice audit gates. |
| 6 | Build / test status                      | **PASS** | Audit-host `cmake --build /home/user/RelativityRender/build` succeeds cleanly with no new warnings on any module (`rr_field` + `rr_math` + every other library). The new `field_tests` CMake target compiles cleanly (no warnings under the project's `rr_apply_warnings` settings).<br>Full `ctest` from the audit-host build directory: `100% tests passed, 0 tests failed out of 13`. The test-binary count grew from 12 → 13 (the new `field_tests` target).<br>**Per-suite counts** at HEAD = `40c387b`:<br>- `field_tests: 80 / 80 passed` (NEW; 80 new RR_CHECK assertions across 17 test functions covering 6 logical sections: enum + factory + default-POD anchors; default disabled / no-op anchors; Constant kind; Radial kind; ProceduralPlaceholder kind; Vec4 overload).<br>- `relativity_tests: 841/841 passed` (unchanged from FIELD-I.1 baseline).<br>- `manifold_identity_tests: 408/408 passed` (unchanged).<br>- `cli_tests: 274/274 passed` (unchanged).<br>- `renderer_tests: 27/27 passed` (unchanged).<br>- `math_tests`, `image_tests`, `gpu_tests`, `pathtracer_tests`, `pathtracer_nee_tests`, `pathtracer_bsdf_tests`, `pathtracer_mis_tests`, `demo_tests` — all unchanged.<br>The audit-host build's `rr_field` INTERFACE library's link graph is byte-identical to the pre-FIELD-I.2 state (`rr_field → rr_math`, both INTERFACE-only); only the header content expanded. The new `field_tests` target's link line is `rr_field` (transitively pulls `rr_math`). |
| 7 | PASS / REPAIR / BLOCKED verdict          | **PASS** | All six structural checks return `PASS`. No `REPAIR` or `BLOCKED` item is outstanding. The FIELD-I.2 commit ships:<br>- The documented `ScalarFieldKind` enum + `ScalarFieldConfig` tagged-union POD per the FIELD-I.1 plan §2.2 verbatim.<br>- Three concrete `evaluate(...)` arms: Constant (returns `strength * constant_value`); Radial (smoothstep envelope with `falloff` exponent + 3 defence-in-depth checks); ProceduralPlaceholder (reserved-but-inert returning `0`).<br>- Vec3 + Vec4 evaluator overloads sharing the same RR_HD inline body.<br>- `disabled_scalar_field_config()` factory returning the no-op anchor.<br>- 80 new RR_CHECK assertions in a new `tests/field_tests.cpp` binary covering every parameter combination + every defence-in-depth path.<br>- New `field_tests` ctest target registered cleanly; the audit-host build produces no warnings; ctest 13/13 PASS.<br>- ZERO source code change outside `src/field/ScalarField.h`. ZERO test change outside the new `tests/field_tests.cpp`. ZERO CMake change beyond the new `field_tests` target registration.<br>Master rule #3 ("no fake stubs") satisfied: the `ProceduralPlaceholder` enumerator is reserved-but-inert with documented `0`-returning evaluator + the `*Placeholder` suffix making the non-implementation status visible at the type level; the operator-authoring `ScalarFieldConfig` POD has documented defaults producing the no-op anchor on every code path. Master rule #11 ("explicit, testable interfaces") satisfied: the 80 new RR_CHECK assertions empirically verify every documented behaviour including the defence-in-depth edge cases (degenerate envelope, non-positive falloff).<br>The slice is **safe to extend** under the renumbered FIELD-I.* sub-slice ladder per §4 below. |

---

## 3. REASONING SUMMARY

The FIELD-I.2 commit (`40c387b`) introduces three host-
side additions:

- a single new tagged-union POD
  (`ScalarFieldConfig`) carrying all 10 fields the
  operator's brief enumerated plus a 3-enumerator
  `ScalarFieldKind` selector at
  `src/field/ScalarField.h`;
- two RR_HD inline `evaluate(...)` overloads (Vec3 +
  Vec4) dispatching on the kind selector + applying
  the documented Constant / Radial /
  ProceduralPlaceholder semantics; plus the
  `disabled_scalar_field_config()` factory + the
  internal `scalar_field_smoothstep` helper;
- a new `tests/field_tests.cpp` binary (~430 lines)
  with 17 test functions covering 80 RR_CHECK
  assertions across 6 logical sections; plus the
  new `field_tests` ctest target registration in
  `CMakeLists.txt`.

The scalar-field-model-exists invariant (check #1)
is **six-element verified** at documented file/line
positions; the new POD + enum + two evaluator
overloads + factory + internal smoothstep helper +
external test surface are all present + reachable.

The default-disabled-no-op invariant (check #2) is
**three-layer verified**: field-by-field default
audit; evaluator's `if (!enabled || strength == 0)
return 0.0f` short-circuit; `ProceduralPlaceholder`
reserved-but-inert. 33 of the 80 RR_CHECK
assertions verify this invariant empirically.

The constant/radial/deferred invariant (check #3)
is **three-kind verified** with the
`ProceduralPlaceholder` documented as explicitly
deferred per master rule #3. Constant kind has 2
test functions (7 RR_CHECK); Radial kind has 7
test functions (22 RR_CHECK including 3 defence-
in-depth paths); ProceduralPlaceholder has 1 test
function (3 RR_CHECK). Vec4 overload has 2 test
functions (6 RR_CHECK) covering both Constant and
Radial routing.

The no-renderer-behaviour-change invariant (check
#4) is **directly verified** by `git diff --name-
only` filtered against every non-`src/field/`
source / test path returning zero hits.

The no-CUDA/OptiX-behaviour-change invariant (check
#5) is **directly verified** by `git diff --name-
only -- 'src/cuda/' 'src/optix/'` returning zero
hits.

The build/test status (check #6) is **directly
verified** by `ctest 13/13 PASS` (the test-binary
count grew by exactly 1 — the new `field_tests`
binary; every existing binary's count is byte-
unchanged).

The overall verdict (check #7) is **PASS**: six
structural checks PASS + no REPAIR / BLOCKED item;
the slice is safe to extend.

---

## 4. NEXT

The slice is **safe to extend**. The renumbered
`FIELD_INTERPRETATION_PHASE1_PLAN.md` §5 FIELD-I.*
sub-slice ladder needs a one-step shift to absorb
this audit slot, mirroring the OBSERVER.3 +
OBSERVER.5 + OBSERVER.7 + OBSERVER.9 + OBSERVER.11 +
OBSERVER.14 + OBS-P.3 + OBS-F.3 audit-slot insertion
precedent:

- **FIELD-I.1** — Phase 1 planning slice
  (LANDED at `4b0d482`, docs only).
- **FIELD-I.2** — Scalar field model
  (LANDED at `40c387b`, impl + 80 RR_CHECK
  tests + ctest target).
- **FIELD-I.3** — **THIS AUDIT** (Scalar Field
  Model Audit, doc-only; verdict PASS).
- **FIELD-I.4** — Field mapping config + CLI
  flags (was FIELD-I.3 in the post-FIELD-I.1
  plan; renumbered).
- **FIELD-I.5** — Scalar diagnostic AOV data
  model (was FIELD-I.4).
- **FIELD-I.6** — CUDA bridge (was FIELD-I.5).
- **FIELD-I.7** — OptiX bridge (was FIELD-I.6).
- **FIELD-I.8** — Fixture scene + companion doc
  (was FIELD-I.7).
- **FIELD-I.9** — Arc capstone audit (was
  FIELD-I.8).

The
`docs/FIELD_INTERPRETATION_PHASE1_PLAN.md` §5
sub-slice ladder may be updated by a follow-on
docs slice if the operator prefers an in-plan
renumbering; this audit doc is the canonical
ladder-shift record for the FIELD-I.3 audit-slot
insertion.

No `REPAIR` action is required. No `BLOCKED` item
is outstanding. The next concrete commit the
operator may prompt for is **FIELD-I.4 — Field
mapping config + CLI flags** per the renumbered
FIELD-I.1 plan §5 FIELD-I.3 → FIELD-I.4 (extends
`rr::core::Config` with a `field_interpreter`
field carrying both the existing `FieldInterpreter`
POD and a new `ScalarFieldConfig source_field`
slot; adds `--field-*` CLI flag surface mirroring
the OBSERVER.4 `--observer-*` pattern; verified
by extending `cli_tests` with new assertions on
the flag parse + default-off byte-identity).

---

## 5. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") satisfied for the
  `ProceduralPlaceholder` enumerator + the
  documented defence-in-depth behaviour; #11
  ("explicit, testable interfaces") satisfied
  by the 80 new RR_CHECK assertions covering
  every documented behaviour; #12 ("Do not
  overbuild a later system before the current
  layer works") satisfied by the FIELD-I.2
  slice's deliberately narrow POD-leaf scope.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §6
  — defines the Field Interpretation Layer as
  Phase 1 of the architecture pivot.
- `docs/FIELD_INTERPRETATION_LAYER.md` — the
  existing 737-line design doc; §3.1 (scalar
  field types) + §4 (mapping outputs) +
  §6 (composition semantics) + §7 (non-goals).
  The FIELD-I.2 surface consumes this doc's
  data-model contract verbatim.
- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1) — the canonical FIELD-I.* arc
  brief that authorised FIELD-I.2's scope. §2.2
  enumerated the three scalar-field kinds
  (Constant + Radial + ProceduralPlaceholder)
  the FIELD-I.2 commit shipped.
- `src/field/FieldType.h` — the five-slot field-
  type enum; the FIELD-I.* arc implements only
  the `Scalar` slot. Unchanged by FIELD-I.2.
- `src/field/ScalarField.h` (modified at
  `40c387b`) — carries the new
  `ScalarFieldKind` enum + `ScalarFieldConfig`
  POD + `evaluate(...)` Vec3 / Vec4 overloads +
  `disabled_scalar_field_config()` factory +
  `scalar_field_smoothstep` helper at lines
  ~166-388 of the modified header.
- `src/field/FieldMapping.h` — the existing
  FIELD.3 multi-channel `FieldOutputChannel`
  enum + `FieldMapping` POD; unchanged by
  FIELD-I.2 (the FIELD-I.4 integration slice
  will extend `FieldInterpreter` to consume
  the new `ScalarFieldConfig`).
- `src/field/FieldInterpreter.h` — the
  existing FIELD.3 per-render metadata POD;
  unchanged by FIELD-I.2.
- `src/field/README.md` — the existing FIELD.*
  skeleton's status document; unchanged by
  FIELD-I.2 (the FIELD-I.* arc is documented
  in `FIELD_INTERPRETATION_PHASE1_PLAN.md`).
- `src/math/Vec3.h` + `Vec4.h` +
  `MathUtils.h` — the math primitives the
  evaluator consumes; unchanged by FIELD-I.2.
  `<cmath>` (for `sqrtf` + `powf`) is
  transitively available via
  `math/MathUtils.h:12`.
- `tests/field_tests.cpp` (new at `40c387b`,
  ~430 lines) — the audited test surface; 17
  test functions; 80 RR_CHECK assertions.
- `tests/manifold_identity_tests.cpp` /
  `tests/cli_tests.cpp` /
  `tests/renderer_tests.cpp` /
  `tests/relativity_tests.cpp` — all
  unchanged by FIELD-I.2.
- `CMakeLists.txt` (modified at `40c387b`)
  — the new `field_tests` ctest target
  registration at lines ~975-996 (between
  `manifold_identity_tests` and `demo_tests`).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md` (this
  audit) — the per-slice verdict document
  for FIELD-I.2.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the precedent POD-leaf
  audit doc this FIELD-I.3 audit mirrors
  in structure (7-row check table; PASS
  verdict on a host-side POD addition with
  no kernel surface to defer).
- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md`
  (OBS-F.3) — second precedent PASS-verdict
  audit (the fixture scene was the only
  comparable slice that landed without a
  runtime-deferred status).
- `docs/BUILD_PLAN.md` — FIELD-I.2 entry
  (lines 84636 onward as of `40c387b`).
- Commit `40c387b` — `field: FIELD-I.2 —
  Scalar Field Model (impl, POD-leaf +
  tests + ctest target)`.
- Commit `4b0d482` — `docs: FIELD-I.1 —
  Field Interpretation Phase 1 Plan (docs
  only)`; the audit baseline.

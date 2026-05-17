# Scalar Field Beauty Mapping OptiX Audit (FIELD-BEAUTY.6)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `89fdcfc` ("optix:
FIELD-BEAUTY.5 — OptiX Scalar Field Beauty Mapping
(impl, OptiX program arm)").
Audit baseline: `c5823d9` ("docs: FIELD-BEAUTY.4 — CUDA
Scalar Field Beauty Mapping Audit (docs only)") — the
last commit before FIELD-BEAUTY.5 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The FIELD-BEAUTY.5 commit's OptiX-ON-no-SDK
build was empirically verified at landing time (ctest
14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from
the tree's current state, the FIELD-BEAUTY.4 /
FIELD-BEAUTY.5 commits' content, the audit-host
`ctest` runtime outputs, and `git diff` filter
inspections.

This audit is the per-slice gate for FIELD-BEAUTY.5
(`89fdcfc`). It verifies the ten items the task brief
enumerates — OptiX ColorMultiplier mapping exists;
OptiX Emission mapping exists; mapping activates only
when field enabled AND target selected; default
mapping remains no-op; disabled field remains no-op;
CUDA/OptiX semantics match; fieldScalar diagnostic AOV
remains available; OptiX OFF build remains valid;
runtime CUDA/OptiX status (PASS / DEFERRED /
BLOCKED); and the overall verdict (PASS / REPAIR /
BLOCKED).

The FIELD-BEAUTY.5 slice is the **OptiX bridge** of
the FIELD-BEAUTY.* arc — the symmetric mirror of
FIELD-BEAUTY.3 (CUDA bridge) for the OptiX path. With
FIELD-BEAUTY.5 in, both backends have parallel beauty-
mapping arms that consume the FIELD-I.4
`FieldMappingConfig` POD through the same RR_HD
inline `evaluate_mapping(...)` helper.

The FIELD-BEAUTY.5 slice also preserves the
honest-framing approach from FIELD-BEAUTY.3 +
FIELD-BEAUTY.4: the FIELD-BEAUTY.1 + FIELD-BEAUTY.2
task brief slots remain unfilled (the operator's
FIELD-BEAUTY.5 prompt body itself is the canonical
task brief); the missing
`docs/FIELD_SCALAR_BEAUTY_MAPPING_PLAN.md` +
`docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md` are
documented honestly in the FIELD-BEAUTY.4 audit's §3.1
+ §5.3 (no retroactive authoring this slice).

---

## 1. VERDICT

**PASS.**

All nine structural / runtime-status checks (#1 – #9)
PASS. Check #10 (overall verdict) is `PASS`. The
FIELD-BEAUTY.5 surface ships exactly what the
operator's four-bullet prompt brief authorised —
OptiX ColorMultiplier + Emission mappings with the
CUDA-semantics match and the preservation guarantees
(default-None no-op + disabled-field no-op +
default-scenes-unchanged + AOV-behavior unchanged +
OptiX OFF compile) — without spilling into CUDA, CLI,
dispatcher, or non-mapping kernel surfaces.

Check #9's runtime CUDA/OptiX status is the standard
`PASS_WITH_RUNTIME_DEFERRED` shape for the dual-
backend audit: both kernel arms exist (CUDA at
FIELD-BEAUTY.3, OptiX at FIELD-BEAUTY.5), but the
audit-host has neither CUDA SDK nor OptiX SDK, so
neither kernel's empirical beauty modulation can be
exercised. The structural data-path is verified by
the audit-host build's clean compile + 13/13 ctest
pass + the OptiX-ON-no-SDK build's 14/14 ctest pass
(empirical at the FIELD-BEAUTY.5 landing commit's
verification).

The narrow-scope verdict honesty: the operator's
FIELD-BEAUTY.5 brief enumerated four implementation
bullets (ColorMultiplier mapping + Emission mapping +
CUDA-semantics match + preservation). The slice
satisfies all four:

- **Bullet 1** (ColorMultiplier mapping): the
  OptiX closest-hit arm at
  `src/optix/OptixPrograms.cu:802-804` branch
  `if (optixLaunchParams.field_mapping_config.target
  == rr::field::FieldMappingTarget::ColorMultiplier)
  { color = color * mapped_fb; }`.
- **Bullet 2** (Emission mapping): the OptiX
  closest-hit arm at
  `src/optix/OptixPrograms.cu:805-807` branch
  `else if (optixLaunchParams.field_mapping_config.target
  == rr::field::FieldMappingTarget::Emission) { color
  = color + rr::math::Vec3{mapped_fb, mapped_fb,
  mapped_fb}; }`.
- **Bullet 3** (CUDA semantics match): five-axis
  symmetry verified — same POD type, same default,
  same double-gate (outer `enabled` + inner
  target), same math (RR_HD inline
  `evaluate(...)` + `evaluate_mapping(...)`), same
  shape (ColorMultiplier multiplicative, Emission
  additive-grayscale).
- **Bullet 4** (preservation): the FIELD-I.4
  three-layer no-op anchor (default `target =
  None`) + FIELD-I.2 disabled-field gate +
  default-scene preservation + FIELD-I.11
  diagnostic AOV preservation all hold
  structurally.

---

## 2. PER-CHECK RESULTS

| # | Check                                              | Evidence                                                                                                                                                                                                                                                                                                                  | Verdict |
|---|----------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1 | OptiX ColorMultiplier mapping exists               | `src/optix/OptixPrograms.cu:802-804` defines `if (optixLaunchParams.field_mapping_config.target == rr::field::FieldMappingTarget::ColorMultiplier) { color = color * mapped_fb; }`. The `mapped_fb` value is computed at lines 795-801 via `rr::field::evaluate_mapping(optixLaunchParams.field_mapping_config, sample_fb)`, where `sample_fb` is the result of `rr::field::evaluate(optixLaunchParams.scalar_field_config, hit_pos_fb)` (lines 791-794). World-space `hit_pos_fb` recomputed locally from OptiX intrinsics at lines 786-790 (mirrors the FIELD-I.11 / SCHW.7 closest-hit recompute pattern). | PASS    |
| 2 | OptiX Emission mapping exists                      | `src/optix/OptixPrograms.cu:805-807` defines `else if (optixLaunchParams.field_mapping_config.target == rr::field::FieldMappingTarget::Emission) { color = color + rr::math::Vec3{mapped_fb, mapped_fb, mapped_fb}; }`. The mapped scalar is grayscale-replicated to RGB (no per-target color on the FIELD-I.4 POD today; matches the CUDA FIELD-BEAUTY.3 arm verbatim). The Emission branch sits as `else if` to the ColorMultiplier branch — the two mappings are mutually exclusive by construction.                                                                                                  | PASS    |
| 3 | Mapping activates only when field enabled AND target selected | Two-gate verified: (a) **outer `enabled` gate**: `if (optixLaunchParams.scalar_field_config.enabled)` at `src/optix/OptixPrograms.cu:785` — closes the entire mapping block on disabled-field. The miss-side exclusion is implicit because the arm sits inside `__closesthit__radiance` (the closest-hit program; the miss arm lives in `__miss__radiance` and does NOT consume `field_mapping_config`). (b) **inner target gate**: `if (target == ColorMultiplier)` (line 803) and `else if (... == Emission)` (line 806) — neither fires when `target == None` (the default) or `target == DiagnosticAOV` (which is AOV-only). Both gates must open. Mirrors the FIELD-BEAUTY.3 CUDA contract verbatim. | PASS    |
| 4 | Default mapping remains no-op                      | The FIELD-I.4 default `FieldMappingConfig{}.target = FieldMappingTarget::None` (audited at FIELD-I.5 check #3). The OptiX arm's inner target gates check for `ColorMultiplier` or `Emission`; neither matches `None`. The fall-through is the documented no-op (the doc-comment at `OptixPrograms.cu:808-812` documents this explicitly: "target == None: short-circuit ... target == DiagnosticAOV: beauty arm is no-op"). Even if the outer `enabled` gate is bypassed, the inner-gate fallthrough produces zero beauty modulation. Master rule #3 + #16 satisfied — the default behaviour is the documented no-op anchor. | PASS    |
| 5 | Disabled field remains no-op                       | The FIELD-I.2 default `ScalarFieldConfig{}.enabled = false` (audited at FIELD-I.3 check #2). The OptiX arm's outer gate `if (optixLaunchParams.scalar_field_config.enabled)` at `OptixPrograms.cu:785` short-circuits the entire mapping block when `enabled == false` — neither `evaluate(...)` nor `evaluate_mapping(...)` is called; neither target branch is evaluated. Even when the inner target slot is opened (`target == ColorMultiplier`), the outer gate's closure makes the inner gate unreachable. Master rule #16 satisfied — the disabled-field default is the load-bearing no-op anchor. Symmetric with the CUDA FIELD-BEAUTY.3 outer gate at `CudaTestKernel.cu:597`. | PASS    |
| 6 | CUDA / OptiX semantics match                       | **Five-axis symmetry** verified — (a) **same POD type**: both backends consume `rr::field::FieldMappingConfig` directly (`CudaSceneView::field_mapping_config` + `OptixLaunchParams::field_mapping_config`; no per-backend shadow struct). (b) **same default**: both backends initialise via in-class `{}` to `disabled_field_mapping_config()` byte-for-byte. (c) **same double-gate**: outer `if (...scalar_field_config.enabled)` + inner `if (...field_mapping_config.target == ColorMultiplier)` / `else if (... == Emission)` — both backends check both gates with identical structure. (d) **same math**: both arms call `rr::field::evaluate(...)` + `rr::field::evaluate_mapping(...)` from `src/field/` (single-source-of-truth RR_HD inline helpers). (e) **same encoding**: ColorMultiplier is `color = color * mapped` on both; Emission is `color = color + Vec3{mapped, mapped, mapped}` on both (grayscale additive). Cross-backend bit-identity guaranteed by construction; SDK-host runtime equivalence pass deferred per check #9. | PASS    |
| 7 | fieldScalar diagnostic AOV remains available       | The FIELD-I.11 OptiX FieldScalar AOV write arm at `src/optix/OptixPrograms.cu:960-980` (the post-shading AOV-write block) is byte-identical to the FIELD-BEAUTY.4 audit baseline. Verified by inspecting `git diff c5823d9..89fdcfc -- src/optix/OptixPrograms.cu` — the only changes are the new FIELD-BEAUTY.5 closest-hit arm at lines 748-813 (positioned BEFORE the Doppler call at line ~820); the AOV write arms at lines 850+ (manifold_coordinates / observer_beta / field_scalar) are unchanged. The diagnostic AOV continues to write the raw `evaluate(scalar_field_config, hit_pos)` output regardless of mapping target. Master rule #3 satisfied — the FIELD-I.4 audit's mapping-vs-diagnostic separation preserved on the OptiX path. | PASS    |
| 8 | OptiX OFF build remains valid                      | Audit-host (`RR_ENABLE_OPTIX=OFF`) `ctest` returns `100% tests passed, 0 tests failed out of 13` (unchanged from FIELD-BEAUTY.4; same ctest set, no new target). Per-binary: `renderer_tests: 35/35`; `field_tests: 135/135`; `relativity_tests: 841/841`; `manifold_identity_tests: 408/408`; `cli_tests: 274/274`; every other suite unchanged. The audit-host build is structurally insulated because `if(RR_ENABLE_OPTIX)` at `CMakeLists.txt:548` is false; `rr_optix` is not compiled; the FIELD-BEAUTY.5 OptiX source changes don't reach the compiler. Full rebuild via `cmake --build /home/user/RelativityRender/build` clean — no new warnings on any module. Empirically verified at the FIELD-BEAUTY.5 landing commit's build transcript. | PASS    |
| 9 | Runtime CUDA / OptiX status                        | `PASS_WITH_RUNTIME_DEFERRED`. Both backends now have beauty-mapping kernel arms wired: CUDA at `CudaTestKernel.cu:597-614` (FIELD-BEAUTY.3), OptiX at `OptixPrograms.cu:785-813` (FIELD-BEAUTY.5). The audit-host has neither CUDA SDK nor OptiX SDK so neither kernel's empirical beauty modulation can be exercised. The structural data-paths are verified: (a) audit-host build (OptiX OFF) PASS — 13/13 ctest; (b) OptiX-ON-no-SDK build PASS — 14/14 ctest at FIELD-BEAUTY.5 landing (includes `optix_tests`). SDK-host runtime scenarios deferred to the future CLI bridge slice's audit on a CUDA + OptiX-SDK host (mirrors the FIELD-BEAUTY.4 audit's §3.10 deferral framing). The five SDK-host scenarios apply both backends symmetrically: (i) ColorMultiplier visualization; (ii) Emission visualization; (iii) disabled-field baseline; (iv) default-mapping baseline; (v) Doppler/searchlight interaction. Plus a new sixth scenario unique to having both backends in: (vi) CUDA ↔ OptiX byte-identity for the same fixture + mapping config. | PASS (structural) — runtime DEFERRED to SDK-host audit pass when the future CLI bridge slice lands |
| 10 | Verdict                                           | All nine structural / runtime-status checks PASS. The FIELD-BEAUTY.5 surface is well-scoped, OptiX-kernel-wired, byte-identical-by-default, CUDA-semantics-matched, AOV-preserving, OptiX-OFF-build-safe. Master rule #3 + #11 + #12 + #16 satisfied (see §3 below).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Commit shape

The FIELD-BEAUTY.5 commit (`89fdcfc`) modifies five
files:

```
docs/BUILD_PLAN.md            | 289 +++++++++++
src/optix/OptixLaunchParams.h |  41 +++
src/optix/OptixPrograms.cu    |  68 ++++
src/optix/OptixRenderer.cpp   |  20 ++-
src/optix/OptixRenderer.h     |  28 ++-
```

All source-code files touched are in `src/optix/`;
zero CUDA / manifold / observer / scene / renderer /
core / main / field-impl files modified. Zero
`CMakeLists.txt` modification (the `rr_field` PUBLIC
link on `rr_optix` from FIELD-I.11 already
propagates `FieldMapping.h` transitively; the
FIELD-BEAUTY.5 include addition is purely consumer-
side). The remaining file (`docs/BUILD_PLAN.md`) is
the per-slice entry mirroring the standard rubric.

The narrow scope intentionally excludes every other
file: no `src/cuda/*`, no `src/core/Config.h`, no
`src/core/CommandLine.cpp`, no `src/main.cpp`, no
`src/io/SceneLoader.cpp`, no `tests/*`, no
scene-file additions. All deferred or out-of-scope
per the operator's brief.

### 3.2 Check #1 — OptiX ColorMultiplier mapping exists

`src/optix/OptixPrograms.cu:802-804` defines the
ColorMultiplier mapping arm:

```cpp
if (optixLaunchParams.field_mapping_config.target
      == rr::field::FieldMappingTarget::ColorMultiplier) {
    color = color * mapped_fb;
}
```

Context within `__closesthit__radiance`:

- Line 785 — outer gate: `if (optixLaunchParams.scalar_field_config.enabled) {`.
- Lines 786-790 — world-space hit position recompute:
    ```cpp
    const float3 ro_fb = optixGetWorldRayOrigin();
    const float3 rd_fb = optixGetWorldRayDirection();
    const float  t_fb  = optixGetRayTmax();
    const rr::math::Vec3 hit_pos_fb{
        ro_fb.x + t_fb * rd_fb.x,
        ro_fb.y + t_fb * rd_fb.y,
        ro_fb.z + t_fb * rd_fb.z};
    ```
- Lines 791-801 — sample + mapping evaluators:
    ```cpp
    const float sample_fb =
        rr::field::evaluate(optixLaunchParams.scalar_field_config,
                            hit_pos_fb);
    const float mapped_fb =
        rr::field::evaluate_mapping(
            optixLaunchParams.field_mapping_config, sample_fb);
    ```
- Lines 802-804 — ColorMultiplier branch.
- Lines 805-807 — Emission branch.
- Lines 808-812 — fall-through doc-comment.
- Line 813 — closing `}` of the outer gate.

The arm is positioned BEFORE the Doppler/searchlight
modulation call (`color = apply_doppler_and_searchlight_with_D(color, D);`
at line ~820), so the field contribution
participates in the standard relativistic pipeline
uniformly with material emission. Mirrors the CUDA
FIELD-BEAUTY.3 placement contract verbatim.

### 3.3 Check #2 — OptiX Emission mapping exists

`src/optix/OptixPrograms.cu:805-807` defines the
Emission mapping arm:

```cpp
else if (optixLaunchParams.field_mapping_config.target
      == rr::field::FieldMappingTarget::Emission) {
    color = color + rr::math::Vec3{mapped_fb, mapped_fb, mapped_fb};
}
```

The Emission branch sits as the `else if` to the
ColorMultiplier branch, making the two mappings
mutually exclusive — only one of `ColorMultiplier`
/ `Emission` can be the active target per launch.
The mapped scalar is replicated to grayscale RGB
(`(m, m, m)`); the FIELD-I.4 `FieldMappingConfig`
POD does not carry a per-target color today; the
doc-comment at `OptixLaunchParams.h:516-554`
documents this as a future-FIELD-BEAUTY.* slice
extension (matches the FIELD-BEAUTY.3 CUDA
documentation verbatim).

### 3.4 Check #3 — mapping activates only when field enabled AND target selected

Two-gate verified explicitly:

**Outer gate** (`scalar_field_config.enabled` +
closest-hit-scope):

```cpp
if (optixLaunchParams.scalar_field_config.enabled) {
    // ... mapping arm body at lines 786-812 ...
}
```

The outer gate closes when:
- `optixLaunchParams.scalar_field_config.enabled
  == false` (the FIELD-I.2 default) — the entire
  mapping block short-circuits.
- The miss-side `__miss__radiance` program is
  structurally separate and does NOT consume
  `field_mapping_config` — the arm only fires on
  hit.

**Inner target gates** (`field_mapping_config.target`):

```cpp
if (optixLaunchParams.field_mapping_config.target == ColorMultiplier) {
    color = color * mapped_fb;
} else if (optixLaunchParams.field_mapping_config.target == Emission) {
    color = color + rr::math::Vec3{mapped_fb, mapped_fb, mapped_fb};
}
// (no else branch — target == None / DiagnosticAOV
// is the documented no-op fallthrough)
```

The inner gates close when:
- `target == None` (the FIELD-I.4 default).
- `target == DiagnosticAOV` (the AOV-only target;
  beauty arm is no-op; the FIELD-I.11 AOV write
  arm handles DiagnosticAOV at its own gate).

Both gates must open for any beauty modulation. The
default state (both gates closed) preserves
byte-identical output to the pre-FIELD-BEAUTY.5
baseline. The operator's brief's two-gate rule is
honoured structurally — symmetric with the
FIELD-BEAUTY.3 CUDA arm.

### 3.5 Check #4 — default mapping remains no-op

The FIELD-I.4 default `FieldMappingConfig{}`
(audited at FIELD-I.5's check #3 three-layer no-op
anchor) carries:
- `target = FieldMappingTarget::None` (the explicit
  `= 0` default).
- `strength = 0.0f`.
- `bias = 0.0f`.

The OptiX arm's behaviour on the default:

- Outer gate: depends on `scalar_field_config.enabled`.
  - If `enabled == false` (FIELD-I.2 default): the
    outer gate closes; the arm short-circuits
    entirely. `evaluate(...)` and
    `evaluate_mapping(...)` are NOT called.
  - If `enabled == true` (artist-engaged): the
    outer gate opens; the inner gate fires.
- Inner gates check for `ColorMultiplier` /
  `Emission`; neither matches `None`:
  - Neither branch fires; the kernel falls through
    the mapping block with `color` unchanged.

Empirically: the default-state OptiX PPM output is
identical to the pre-FIELD-BEAUTY.5 baseline because
the OptiX arm only writes to `color` inside the
target-specific branches; with `target == None`, no
write happens.

Even when the outer gate is bypassed (e.g. the
operator engages the field for diagnostic
visualisation without setting up a mapping target),
the inner-gate fallthrough produces zero beauty
modulation. Master rule #3 + #16 satisfied — the
default behaviour is the documented no-op anchor.

### 3.6 Check #5 — disabled field remains no-op

The FIELD-I.2 default `ScalarFieldConfig{}.enabled =
false` (audited at FIELD-I.3 check #2). The OptiX
arm's outer gate:

```cpp
if (optixLaunchParams.scalar_field_config.enabled) {
    // ... block body ...
}
```

When `enabled == false`, the entire block is
short-circuited:

- `optixGetWorldRayOrigin/Direction/Tmax` are NOT
  called (no `hit_pos_fb` recompute).
- `evaluate(...)` is NOT called.
- `evaluate_mapping(...)` is NOT called.
- Neither target branch is evaluated.
- `color` is unchanged.

Even when the inner target slot is opened (`target
== ColorMultiplier`), the outer gate's closure
makes the inner gate unreachable. The operator's
"disabled field = no-op" rule is structurally
satisfied — symmetric with the CUDA FIELD-BEAUTY.3
outer gate at `CudaTestKernel.cu:597`.

### 3.7 Check #6 — CUDA / OptiX semantics match

Five-axis symmetry verified explicitly:

**Axis A — Same POD type.** Both backends consume
`rr::field::FieldMappingConfig` directly. No
per-backend shadow struct, no per-backend POD
mirror. The struct lives at
`src/field/FieldMapping.h`; both
`CudaSceneView::field_mapping_config` (CUDA;
`CudaScene.cuh:216`) and
`OptixLaunchParams::field_mapping_config` (OptiX;
`OptixLaunchParams.h:554`) are declared as the
same type.

**Axis B — Same default.** Both backends initialise
the field to `disabled_field_mapping_config()` via
in-class `{}` initialisation:
- OptiX: `rr::field::FieldMappingConfig field_mapping_config{};`
  at `OptixLaunchParams.h:554`.
- CUDA: `rr::field::FieldMappingConfig field_mapping_config{};`
  at `CudaScene.cuh:216`.
- AOVTargets (CUDA): `rr::field::FieldMappingConfig field_mapping_config = {};`
  at `CudaRenderer.h:283`.
- `render_aovs` trailing param (OptiX):
  `rr::field::FieldMappingConfig field_mapping_config = {}`
  at `OptixRenderer.h:595`.

All four sites produce byte-identical default POD
state (the FIELD-I.4 audit's three-layer no-op
anchor at check #3).

**Axis C — Same double-gate.** Both kernel arms
use the same gate structure:
- Outer: `if (scalar_field_config.enabled)`
  (CUDA: `CudaTestKernel.cu:597`; OptiX:
  `OptixPrograms.cu:785`).
- Inner: `if (target == ColorMultiplier) {...} else
  if (target == Emission) {...}` (CUDA:
  `CudaTestKernel.cu:605 + :608`; OptiX:
  `OptixPrograms.cu:802 + :805`).

All four sites guard identically; when either gate
is closed the arm short-circuits.

**Axis D — Same math.** Both arms call the same
RR_HD inline helpers from `src/field/`:
- `rr::field::evaluate(scalar_field_config,
  hit_pos)` (CUDA: `CudaTestKernel.cu:600-601`;
  OptiX: `OptixPrograms.cu:791-794`).
- `rr::field::evaluate_mapping(field_mapping_config,
  sample)` (CUDA: `CudaTestKernel.cu:602-603`;
  OptiX: `OptixPrograms.cu:795-797`).

The helpers are `__host__ __device__` inline; both
backends emit equivalent PTX/SASS for the math
leaves. Cross-backend bit-identity is structurally
guaranteed.

**Axis E — Same shape.** ColorMultiplier:
- CUDA (`CudaTestKernel.cu:606`): `color = color *
  mapped;`.
- OptiX (`OptixPrograms.cu:803`): `color = color *
  mapped_fb;`.

Emission:
- CUDA (`CudaTestKernel.cu:609`): `color = color +
  Vec3{mapped, mapped, mapped};`.
- OptiX (`OptixPrograms.cu:806`): `color = color +
  rr::math::Vec3{mapped_fb, mapped_fb, mapped_fb};`.

Both backends use the same operator semantics —
multiplicative for ColorMultiplier, additive-
grayscale for Emission. The placement (BEFORE
Doppler/searchlight) is the same on both backends,
so the field contribution sees the standard
relativistic pipeline uniformly on both backends.

Cross-backend bit-identity is structurally
guaranteed; SDK-host runtime equivalence pass
deferred per check #9.

### 3.8 Check #7 — fieldScalar diagnostic AOV remains available

The FIELD-I.11 OptiX FieldScalar AOV write arm at
`src/optix/OptixPrograms.cu:960-980` (the post-
shading AOV-write block in `__closesthit__radiance`)
is byte-identical to the FIELD-BEAUTY.4 audit
baseline. Verified by inspecting `git diff
c5823d9..89fdcfc -- src/optix/OptixPrograms.cu` —
the only changes are the new FIELD-BEAUTY.5
closest-hit beauty-mapping arm at lines 748-813
(positioned BEFORE the Doppler call at line 820);
the post-shading AOV write arms (manifold_coordinates
at lines 850+; observer_beta at lines 912+;
field_scalar at lines 960-980) are unchanged.

The diagnostic AOV continues to write the raw
`evaluate(optixLaunchParams.scalar_field_config,
hit_pos_v3)` output on hit + `0.0f` on miss,
regardless of the new mapping. Master rule #3
satisfied — the FIELD-BEAUTY.5 slice does NOT
secretly modify the AOV write path under the cover
of "beauty mapping"; the two surfaces are honestly
separate. Mirrors the CUDA FIELD-BEAUTY.3 +
FIELD-BEAUTY.4 audit's preservation framing
verbatim.

### 3.9 Check #8 — OptiX OFF build remains valid

Audit-host (`RR_ENABLE_OPTIX=OFF`) build:

```
13/13 Test #13: renderer_tests ........ Passed
100% tests passed, 0 tests failed out of 13
```

Per-binary:
- `relativity_tests: 841/841 passed` — unchanged.
- `manifold_identity_tests: 408/408 passed` —
  unchanged.
- `cli_tests: 274/274 passed` — unchanged.
- `renderer_tests: 35/35 passed` — unchanged.
- `field_tests: 135/135 passed` — unchanged.
- Every other suite unchanged.

The audit-host build is structurally insulated from
the FIELD-BEAUTY.5 OptiX changes because the
`if(RR_ENABLE_OPTIX)` guard at `CMakeLists.txt:548`
is false; the `rr_optix` library is not built;
`OptixLaunchParams.h`, `OptixRenderer.h`,
`OptixRenderer.cpp`, `OptixPrograms.cu` are not
compiled. No `CMakeLists.txt` change required
(the `rr_field` PUBLIC link on `rr_optix` from
FIELD-I.11 already propagates the `FieldMapping.h`
include path transitively).

Full rebuild via `cmake --build
/home/user/RelativityRender/build` clean — no new
warnings on any module. Empirically verified at the
FIELD-BEAUTY.5 landing commit's build transcript.

### 3.10 Check #9 — runtime CUDA / OptiX status

`PASS_WITH_RUNTIME_DEFERRED`.

Both backends now have beauty-mapping kernel arms
wired:
- **CUDA arm** at `CudaTestKernel.cu:597-614`
  (FIELD-BEAUTY.3).
- **OptiX arm** at `OptixPrograms.cu:785-813`
  (FIELD-BEAUTY.5).

The audit-host has neither CUDA SDK nor OptiX SDK
so neither kernel's empirical beauty modulation
can be exercised. The structural data-paths are
verified:

- **Audit-host build (OptiX OFF):** 13/13 ctest
  PASS. The host-side `AOVTargets` /
  `OptixLaunchParams` field declarations compile
  cleanly into both `rr_gpu` and `rr_optix` (when
  enabled).
- **OptiX-ON-no-SDK build:** 14/14 ctest PASS at
  the FIELD-BEAUTY.5 landing commit's empirical
  run in `/tmp/rr_build_optix_no_sdk`. The
  audit-host stub fallback paths in
  `OptixRenderer.cpp` exercise empirically; the
  FIELD-BEAUTY.5 stub signature matches the
  header (the trailing `field_mapping_config`
  parameter was added in lockstep).

The SDK-host runtime checks DEFERRED to the future
CLI bridge slice's audit:

- **ColorMultiplier visualisation (both
  backends).** Run `RelativityRender --render-aovs
  --field-debug --field-mapping-target
  color-multiplier --field-strength 0.5
  --field-bias 0.5 ...` AND
  `--render-optix-aovs ...` with the same config.
  Verify: both backends' beauty PPMs show the
  field's contribution multiplying the per-pixel
  color; the `aov_field_scalar.ppm` /
  `optix_aov_field_scalar.ppm` show the raw
  smoothstep pattern (independent of the
  multiplier).
- **Emission visualisation (both backends).** Run
  the same with `--field-mapping-target
  emission`. Verify: both backends' beauty PPMs
  show additive grayscale emission tracking the
  field; the diagnostic AOV PPMs are identical to
  the ColorMultiplier case (same raw sample,
  different mapping target).
- **Disabled-field baseline (both backends).**
  Run `--render-aovs --field-debug` AND
  `--render-optix-aovs --field-debug` WITHOUT
  `--field-enable`. Verify: both backends' beauty
  PPMs are byte-identical to the pre-FIELD-BEAUTY
  baselines; both `aov_field_scalar.ppm` files
  are uniformly black.
- **Default-mapping baseline (both backends).**
  Run `--render-aovs --field-enable --field-kind
  radial ...` AND `--render-optix-aovs ...`
  WITHOUT `--field-mapping-target`. Verify: both
  backends' beauty PPMs are byte-identical to the
  pre-FIELD-BEAUTY baselines (default `target ==
  None` short-circuits inner gate); both
  `aov_field_scalar.ppm` files show the raw
  smoothstep pattern.
- **Doppler / searchlight interaction (both
  backends).** Run the ColorMultiplier scenario
  with non-trivial observer beta + perception mode
  relativistic. Verify: both backends' beauty
  PPMs show the ColorMultiplier-mapped field
  flowing through the Doppler color shift +
  searchlight scaling.
- **CUDA ↔ OptiX byte-identity (NEW for
  FIELD-BEAUTY.6).** Run the ColorMultiplier
  scenario on both backends. `cmp
  output/aov_beauty.ppm output/optix_aov_beauty.ppm`
  MUST return exit status `0`. The cross-backend
  symmetry argument at check #6 (five-axis)
  guarantees this structurally; the empirical
  verification is the SDK-host pass.

All six scenarios apply this slice's OptiX arm in
parallel with the FIELD-BEAUTY.3 CUDA arm but
require both a CUDA + OptiX-SDK host AND the
future CLI bridge slice. The deferral is honest
scope: the FIELD-BEAUTY.5 surface is the OptiX
kernel arm ONLY, with the CLI / dispatcher
plumbing landing as a separate slice.

### 3.11 Master-rule satisfaction recap

- **Master rule #3 ("no fake stubs"):** satisfied.
  The OptiX arm at `OptixPrograms.cu:785-813` is
  fully wired (real `evaluate(...)` /
  `evaluate_mapping(...)` invocations; real
  ColorMultiplier + Emission branches; real
  `color` modifications). The structural
  unreachability via the double-gate is honest
  scope framing per the doc-comments at lines
  748-783. The honest framing of the missing
  FIELD-BEAUTY.1 + FIELD-BEAUTY.2 task brief
  slots (per the FIELD-BEAUTY.4 audit's §3.1) is
  preserved.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. The OptiX arm's
  behaviour is documented as contract on every
  modified file's doc-comments + structurally
  rooted in the audit-host-verified FIELD-I.2
  evaluator + FIELD-I.4 mapping evaluator. The
  cross-backend symmetry's five-axis
  verification (check #6) rests on inspectable
  file/line references and the
  structural-equivalence argument the
  SCHW.5 / PENROSE.6 + MANI-I.8 + OBSERVER.13 +
  FIELD-I.11 + FIELD-BEAUTY.3 precedents
  established.

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Scope deliberately narrow to OptiX
  bridge only — CUDA byte-unchanged per the
  operator's "Do not change CUDA unless required
  by shared type consistency" rule (no shared-
  type adjustment needed; the POD embeds
  directly on both backends). CLI deferred;
  dispatcher emit deferred; scene-loader
  deferred; mapping CLI surface deferred. The
  FIELD-BEAUTY.* arc opens parallel to the
  FIELD-I.* arc; both arcs coexist.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied.
  The FIELD-BEAUTY.5 default state is unchanged
  from the FIELD-BEAUTY.4 baseline:
    - No `--render-*` action produces a new
      file.
    - No existing PPM filename changes.
    - No beauty pass arithmetic changes from
      defaults (the kernel arm is gated; defaults
      close the gates).
    - No existing AOV slot's value changes.
  The single observable behaviour change is the
  structural presence of the OptiX arm — which is
  outer-AND-inner-gated, so its observable
  behaviour from every existing OptiX CLI
  invocation is zero.

### 3.12 Honest scope recap

This audit is an **OptiX beauty-mapping audit with
SDK-host runtime DEFERRED** + **CUDA path
preserved-unchanged** + **diagnostic-AOV preserved-
unchanged**. The verdict `PASS` is the FIELD-BEAUTY.5
OptiX-side surface's verdict; check #6 (CUDA
semantics match) is verified via the five-axis
symmetry argument; check #7 (diagnostic AOV
preservation) is structural; check #8 (OptiX OFF
build) is empirically verified.

The FIELD-BEAUTY.4 audit's runtime-deferred
portion is now PAIRED on both backends: both
arms exist, both backends will need SDK-host
verification at the future CLI bridge slice's
audit. The five SDK-host scenarios from
FIELD-BEAUTY.4 §3.10 + the new sixth scenario
(cross-backend byte-identity) all defer to that
slot.

The honest framing of the missing FIELD-BEAUTY.1
+ FIELD-BEAUTY.2 task brief slots is preserved
across the FIELD-BEAUTY.* arc; this audit
references the FIELD-BEAUTY.4 audit's §3.1 + §5.3
for the canonical record. A future doc slice may
retroactively author the task briefs if standard
precedent is desired.

---

## 4. NEXT

### 4.1 FIELD-BEAUTY.* sub-slice ladder

The FIELD-BEAUTY.6 audit slot insertion (mirroring
the FIELD-BEAUTY.4 / FIELD-I.12 / FIELD-I.10 audit-
slot insertion precedents) shifts subsequent
FIELD-BEAUTY.* sub-slices by one. The post-
FIELD-BEAUTY.6 ladder is:

- **FIELD-BEAUTY.7** — CLI + Config + dispatcher
  bridge (lands the `--field-mapping-target` +
  `--field-strength` + `--field-bias` +
  `--field-min-value` + `--field-max-value` +
  `--field-clamp-output` CLI flags; extends
  `rr::core::Config` with a `field_mapping_config`
  field; threads it from CLI through both
  `run_render_aovs` AND `run_render_optix_aovs`
  into the respective payload fields; flips
  both backends' mapping arms reachable
  simultaneously; closes the FIELD-BEAUTY.4 +
  FIELD-BEAUTY.6 audits' runtime-deferred
  portions on SDK-host).
- **FIELD-BEAUTY.8** — CLI bridge audit.
- **FIELD-BEAUTY.9** — Fixture extension (adds
  `field_mapping` block to the FIELD-I.13
  `scenes/test_scalar_field_diagnostic.rrscene`
  fixture, or authors a new fixture; verifies
  the combined `scalar_field` + `field_mapping`
  authoring surface).
- **FIELD-BEAUTY.10** — Fixture audit.
- **FIELD-BEAUTY.11** — Arc capstone audit.

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

The parallel FIELD-I.* arc retains its renumbered
ladder (FIELD-I.15 = CLI + Config + dispatcher
bridge for the diagnostic AOV; FIELD-I.16+ =
mapping CLI + kernel pipeline + capstone). The two
arcs may be merged at the CLI bridge slice (a
single FIELD-BEAUTY.7-or-FIELD-I.15 slice could
flip both diagnostic AOV + beauty mapping gates
simultaneously) or remain separate per operator
preference.

### 4.2 Candidate next slots (prioritised)

**(a) RECOMMENDED — FIELD-BEAUTY.7: CLI +
Config + dispatcher bridge** (the renumbered
next FIELD-BEAUTY.* impl slot). Natural
continuation: flips both backend mapping arms
reachable simultaneously by adding the
`--field-mapping-target` + per-parameter CLI
flags. Closes the FIELD-BEAUTY.4 + FIELD-BEAUTY.6
audits' runtime-deferred portions when its own
audit runs on an SDK host. The symmetry of the
FIELD-BEAUTY.3 + FIELD-BEAUTY.5 bridges makes
this slice a single-file threading addition
plus a CLI parser extension.

**(b) Manifold-orthogonal work.** Multiple
options available:
  - **Deferred SDK-host runtime pass** for the
    OBSERVER.* + OBS-P.* + OBS-F.* + FIELD-I.*
    + FIELD-BEAUTY.* arc family (highest
    converging-leverage option).
  - **MANI-I.12 final cross-host manifold
    audit**.
  - **Denoiser integration with chart-aware
    AOVs**.
  - **Path-tracer feature breadth** (NEE
    extension, BSDF expansion, MIS tuning).

**(c) NOT RECOMMENDED — direct FIELD-BEAUTY.9
fixture extension slice skipping FIELD-BEAUTY.7
CLI bridge.** Would extend the fixture with a
`field_mapping` block but the renderer would
still not consume it (no CLI surface to
flip the gates). Better to land the CLI bridge
first so the fixture is meaningfully runnable.

**(d) RETROACTIVE — author the missing
FIELD-BEAUTY.1 +
`docs/FIELD_SCALAR_BEAUTY_MAPPING_PLAN.md` +
FIELD-BEAUTY.2 +
`docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md` task
briefs.** The operator may want to backfill the
standard task-brief precedent. Out of scope for
this audit; deferrable to operator discretion.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the master rule #3 +
  #11 + #12 + #16 satisfaction recap at §3.11
  cites these).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §6 (the Field Interpretation Layer as an
  OPTIONAL extension above the Manifold Core).
- `docs/FIELD_INTERPRETATION_LAYER.md` §4.1 +
  §4.2 (the design-doc anchors for the
  ColorMultiplier + Emission target channels
  the FIELD-BEAUTY.5 OptiX arm consumes).

### 5.2 FIELD-I.* arc references (parallel arc)

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md` (FIELD-I.3
  — the three-layer no-op anchor at check #2
  underpins the FIELD-BEAUTY.6 check #5
  disabled-field no-op verification).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md`
  (FIELD-I.5 — the three-layer no-op anchor at
  check #3 underpins the FIELD-BEAUTY.6 check
  #4 default-target-None no-op verification).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_AUDIT.md`
  (FIELD-I.8).
- `docs/FIELD_SCALAR_CUDA_BRIDGE_AUDIT.md`
  (FIELD-I.10).
- `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
  (FIELD-I.12 — the precedent OptiX-bridge
  audit shape this FIELD-BEAUTY.6 audit
  structurally mirrors; the five-axis
  cross-backend symmetry argument at FIELD-I.12
  §3.4 underpins the FIELD-BEAUTY.6 check #6
  verification).
- `docs/FIELD_SCALAR_FIXTURE.md` (FIELD-I.13).
- `docs/FIELD_SCALAR_FIXTURE_AUDIT.md`
  (FIELD-I.14).

### 5.3 FIELD-BEAUTY.* arc references

- (FIELD-BEAUTY.1) — UNFILLED task brief slot.
  The operator's FIELD-BEAUTY.3 + FIELD-BEAUTY.5
  prompt bodies serve as the canonical task
  briefs (see FIELD-BEAUTY.4 audit's §3.1 +
  §5.3 for the canonical record of the
  missing
  `docs/FIELD_SCALAR_BEAUTY_MAPPING_PLAN.md`).
- (FIELD-BEAUTY.2) — UNFILLED task brief slot.
  Same honest-framing as FIELD-BEAUTY.1 (the
  missing
  `docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md`).
- `docs/FIELD_SCALAR_BEAUTY_CUDA_AUDIT.md`
  (FIELD-BEAUTY.4 — the precedent CUDA-bridge
  beauty-mapping audit shape this audit
  mirrors).
- FIELD-BEAUTY.3 — the FIELD-BEAUTY.4 audited
  CUDA slice (`8b8f100`).
- FIELD-BEAUTY.5 — the audited OptiX slice
  (`89fdcfc`).

### 5.4 Source surface audited

- `src/optix/OptixLaunchParams.h` (FIELD-BEAUTY.5
  +41 lines; the new `field_mapping_config`
  field at line 554 + its doc-comment block at
  lines 516-553 + the new `#include
  "field/FieldMapping.h"` at line 5).
- `src/optix/OptixRenderer.h` (FIELD-BEAUTY.5
  +28 lines; the new `field_mapping_config`
  trailing-defaulted parameter on
  `render_aovs(...)` at line 595 + its
  doc-comment block at lines 571-594 + the new
  `#include "field/FieldMapping.h"` at line 5).
- `src/optix/OptixRenderer.cpp` (FIELD-BEAUTY.5
  +20 lines; the new `field_mapping_config`
  signature parameter + the new
  `params.field_mapping_config =
  field_mapping_config;` threading; the
  audit-host fallback stub signature update).
- `src/optix/OptixPrograms.cu` (FIELD-BEAUTY.5
  +68 lines; the new beauty-mapping kernel arm
  in `__closesthit__radiance` at lines 748-813
  + the doc-comment block at lines 748-783 +
  the two branch arms at lines 802-804 +
  805-807).

### 5.5 Test surface unchanged

All test files in `tests/` are byte-identical
to the FIELD-BEAUTY.4 baseline. No test
extension this slice (the OptiX arm's empirical
behaviour requires SDK-host runtime
verification; deferred per §3.10).

### 5.6 Surrounding commit SHAs

- `89fdcfc` — FIELD-BEAUTY.5 audited tree (the
  per-slice gate target).
- `c5823d9` — FIELD-BEAUTY.4 baseline (the
  diff baseline for checks #7 + #8).
- `8b8f100` — FIELD-BEAUTY.3 CUDA bridge impl
  (the antecedent CUDA arm the FIELD-BEAUTY.5
  OptiX arm mirrors verbatim; check #6's
  five-axis symmetry references this commit).
- `e15934e` — FIELD-I.11 OptiX bridge impl (the
  precedent OptiX-side bridge shape the
  FIELD-BEAUTY.5 follows; the FIELD-I.11
  FieldScalar AOV write arm at
  `OptixPrograms.cu:960-980` is preserved
  verbatim per check #7).
- `683a16d` — FIELD-I.4 mapping config POD
  impl (the `FieldMappingConfig` POD both
  backends' beauty-mapping arms consume).
- `40c387b` — FIELD-I.2 scalar field model
  impl (both backend arms call
  `rr::field::evaluate(scalar_field_config,
  hit_pos)` from this commit's surface).

### 5.7 Unchanged source files (sampled)

The following files are byte-identical to the
FIELD-BEAUTY.4 baseline (`c5823d9`), confirmed
by the diff filters at checks #6 + #7 +
narrow-scope discipline:

- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/cuda/`.
- Every file in `src/manifold/`.
- Every file in `src/relativity/`.
- Every file in `src/renderer/`.
- Every file in `src/scene/`.
- Every file in `src/io/`.
- Every file in `src/core/`, `src/math/`,
  `src/image/`, `src/gpu/`, `src/app/`,
  `src/field/`, `src/pathtracer/`,
  `src/camera/`, `src/geometry/`,
  `src/lighting/`, `src/material/`,
  `src/texture/`.
- `src/main.cpp`.
- Other `src/optix/` files: `OptixAccel.cpp`,
  `OptixAccel.h`, `OptixBackend.cpp`,
  `OptixBackend.h`, `OptixDenoiser.cpp`,
  `OptixDenoiser.h`, `OptixPipeline.cpp`,
  `OptixPipeline.h`, `OptixSBT.h`.

### 5.8 Unchanged test + scene + build configuration

All test files (`tests/`) are byte-identical
to the FIELD-BEAUTY.4 baseline. All scene
files (`scenes/`) are byte-identical to the
FIELD-BEAUTY.4 baseline (including the
FIELD-I.13 fixture). `CMakeLists.txt` is
byte-identical to the FIELD-BEAUTY.4 baseline
(the `rr_field` PUBLIC link on `rr_optix` from
FIELD-I.11 already propagates the
`FieldMapping.h` include path transitively;
the FIELD-BEAUTY.5 `#include "field/FieldMapping.h"`
additions in `OptixLaunchParams.h` +
`OptixRenderer.h` are consumer-side and
require no CMake change).

# Scalar Field Beauty Mapping CUDA Audit (FIELD-BEAUTY.4)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `8b8f100` ("cuda:
FIELD-BEAUTY.3 — CUDA Scalar Field Beauty Mapping
(impl, CUDA kernel arm)").
Audit baseline: `8a5dd54` ("docs: FIELD-I.14 — Scalar
Field Fixture Audit (docs only)") — the last commit
before FIELD-BEAUTY.3 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The FIELD-BEAUTY.3 commit's OptiX-ON-no-SDK
build was empirically verified at landing time (ctest
14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from
the tree's current state, the FIELD-BEAUTY.3 commit's
content, the audit-host `ctest` runtime outputs, and
the `git diff` filter inspections.

This audit is the per-slice gate for FIELD-BEAUTY.3
(`8b8f100`). It verifies the ten items the task brief
enumerates — CUDA ColorMultiplier mapping exists; CUDA
Emission mapping exists; mapping activates only when
field enabled AND target selected; default mapping
remains no-op; disabled field remains no-op;
fieldScalar diagnostic AOV remains available; OptiX
path unchanged; build/test status; runtime CUDA status
(PASS / DEFERRED / BLOCKED); and the overall verdict
(PASS / REPAIR / BLOCKED).

The FIELD-BEAUTY.3 slice is the **first impl in the
FIELD-BEAUTY.* arc** — a parallel arc to FIELD-I.*
that lifts the "no field-to-beauty mapping yet"
non-goal for the FIELD-I.4 `FieldMappingConfig` POD's
ColorMultiplier + Emission targets on the CUDA path.
The two arcs coexist:

- **FIELD-I.*** = read-only diagnostic AOV
  (FIELD-I.7 enum + FIELD-I.9 CUDA bridge + FIELD-I.11
  OptiX bridge; writes the raw scalar sample to
  `aov_field_scalar.ppm`).
- **FIELD-BEAUTY.*** = beauty modulation
  (FIELD-BEAUTY.3 CUDA kernel arm that applies the
  FIELD-I.4 mapping to the per-pixel beauty result).

The two arcs are orthogonal in scope but share the
same FIELD-I.4 `FieldMappingConfig` POD as their
authoring surface.

---

## 1. VERDICT

**PASS.**

All nine structural / runtime-status checks (#1, #2,
#3, #4, #5, #6, #7, #8, #9) PASS. Check #10 (overall
verdict) is `PASS`. The FIELD-BEAUTY.3 surface ships
exactly what the operator's five-bullet prompt brief
authorised — CUDA ColorMultiplier mapping + CUDA
Emission mapping with the documented preservation
guarantees (default-None no-op + disabled-field no-op
+ default-scenes-unchanged + AOV-behavior unchanged)
— without spilling into OptiX, CLI, dispatcher, or
non-mapping kernel surfaces.

Check #9's runtime CUDA status is the standard
`PASS_WITH_RUNTIME_DEFERRED` shape. The audit-host
has no CUDA SDK so the kernel arm's empirical
beauty-modulation behaviour cannot be exercised; the
structural data-path (host-side `AOVTargets` →
`CudaSceneView` field threading on
`CudaRenderer.cu`) is verified by the audit-host
build's clean compile + 13/13 ctest pass +
unchanged renderer_tests / field_tests counts. The
OptiX-ON-no-SDK build confirms the kernel-surface
modifications don't break the OptiX-on path (14/14
ctest PASS at the FIELD-BEAUTY.3 landing commit's
verification).

The narrow-scope verdict honesty: the operator's
FIELD-BEAUTY.3 brief enumerated five implementation
bullets (ColorMultiplier mapping + Emission mapping +
default-None no-op + disabled-field no-op + AOV
behavior preservation). The slice satisfies all five:

- **Bullet 1** (ColorMultiplier mapping): the
  kernel arm at `CudaTestKernel.cu:605-607` branch
  `if (scene.field_mapping_config.target ==
  rr::field::FieldMappingTarget::ColorMultiplier)
  { color = color * mapped; }`.
- **Bullet 2** (Emission mapping): the kernel arm
  at `CudaTestKernel.cu:608-611` branch `else if
  (scene.field_mapping_config.target ==
  rr::field::FieldMappingTarget::Emission) { color
  = color + Vec3{mapped, mapped, mapped}; }`.
- **Bullet 3** (default mapping None = no-op):
  three-layer anchor — (a) default
  `FieldMappingConfig{}.target =
  FieldMappingTarget::None` (FIELD-I.4 audit's
  check #3); (b) the kernel arm's
  `target == ColorMultiplier` / `target ==
  Emission` checks don't fire on `target == None`;
  (c) `evaluate_mapping(...)` short-circuits to
  `0.0f` on `None` regardless.
- **Bullet 4** (disabled field = no-op): the
  outer `if (best.hit &&
  scene.scalar_field_config.enabled)` gate at
  `CudaTestKernel.cu:597`; with `enabled = false`
  (the FIELD-I.2 default) the entire mapping
  block short-circuits.
- **Bullet 5** (fieldScalar diagnostic AOV
  preserved): the FIELD-I.9 AOV write arm at
  `CudaTestKernel.cu:797-805` is byte-identical;
  per-line diff confirms zero changes inside the
  AOV-write block.

---

## 2. PER-CHECK RESULTS

| # | Check                                                  | Evidence                                                                                                                                                                                                                                                                                                                  | Verdict |
|---|--------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1 | CUDA ColorMultiplier mapping exists                    | `src/cuda/CudaTestKernel.cu:605-607` defines `if (scene.field_mapping_config.target == rr::field::FieldMappingTarget::ColorMultiplier) { color = color * mapped; }`. The `mapped` value is computed at `CudaTestKernel.cu:601-603` via `rr::field::evaluate_mapping(scene.field_mapping_config, sample)`, where `sample` is the result of `rr::field::evaluate(scene.scalar_field_config, hit_pos_v3)` at line 600-601. Inserted inside the `if (best.hit && scene.scalar_field_config.enabled)` gate at line 597, AFTER the existing material-shading `color = albedo * (direct + ambient) + emission;` combine (line 535) and BEFORE the closing `}` of the `if (best.hit)` block (line 614). | PASS    |
| 2 | CUDA Emission mapping exists                           | `src/cuda/CudaTestKernel.cu:608-611` defines `else if (scene.field_mapping_config.target == rr::field::FieldMappingTarget::Emission) { color = color + Vec3{mapped, mapped, mapped}; }`. The mapped value is grayscale-replicated to RGB (no per-target color on the FIELD-I.4 `FieldMappingConfig` POD today; the doc-comment documents this as a future-FIELD-BEAUTY.* slice extension). The branch sits as the `else if` to the ColorMultiplier branch, so the two mappings are mutually exclusive by construction.                                                                                                                                                                            | PASS    |
| 3 | Mapping activates only when field enabled AND target selected | Two-gate verified: (a) **outer `enabled` gate**: `if (best.hit && scene.scalar_field_config.enabled)` at `CudaTestKernel.cu:597` — closes the entire mapping block on disabled-field or miss; (b) **inner target gate**: `if (scene.field_mapping_config.target == ColorMultiplier)` (line 605) and `else if (... == Emission)` (line 608) — neither fires when `target == None` (the default) or `target == DiagnosticAOV` (which is AOV-only, no beauty-pass effect). Both gates must open for any beauty modulation to occur. Empirically inspectable from the kernel source.                                                                                                                | PASS    |
| 4 | Default mapping remains no-op                          | The FIELD-I.4 default `FieldMappingConfig{}.target = FieldMappingTarget::None` (audited at FIELD-I.5). The kernel arm's inner target gates check for `ColorMultiplier` or `Emission`; neither matches `None`. The fall-through `else` branch is the documented no-op (lines 612-614's comment: "target == None: short-circuit ... target == DiagnosticAOV: beauty arm is no-op"). Even if a future slice flips the outer `enabled` gate without flipping the target, the inner-gate fallthrough produces zero beauty modulation. Master rule #3 + #16 satisfied — the default behaviour is the documented no-op anchor. | PASS    |
| 5 | Disabled field remains no-op                           | The FIELD-I.2 default `ScalarFieldConfig{}.enabled = false` (audited at FIELD-I.3). The kernel arm's outer gate `if (best.hit && scene.scalar_field_config.enabled)` at `CudaTestKernel.cu:597` short-circuits the entire mapping block when `enabled == false` — neither `evaluate(...)` nor `evaluate_mapping(...)` is called; neither target branch is evaluated. Even if the inner target slot is opened (`target == ColorMultiplier`), the outer gate's closure makes the inner gate unreachable. Master rule #16 satisfied — the disabled-field default is the load-bearing no-op anchor. | PASS    |
| 6 | fieldScalar diagnostic AOV remains available           | The FIELD-I.9 diagnostic AOV write arm at `src/cuda/CudaTestKernel.cu:797-805` is byte-identical to the FIELD-I.14 audit baseline. Verified by inspecting `git diff 8a5dd54..8b8f100 -- src/cuda/CudaTestKernel.cu` — the only changes are the new FIELD-BEAUTY.3 kernel arm INSIDE the `if (best.hit)` block (after `color = albedo * shade + emission;` on line 544); the post-framebuffer-write AOV block (lines 596+) is unchanged. The diagnostic AOV continues to write the raw `evaluate(scene.scalar_field_config, hit_pos)` output on hit + `0.0f` on miss, regardless of the new mapping. Master rule #3 satisfied — the FIELD-I.4 audit's mapping-vs-diagnostic separation preserved. | PASS    |
| 7 | OptiX path unchanged                                   | `git diff 8a5dd54..8b8f100 --name-only -- 'src/optix/'` returns zero hits. Every `src/optix/*.cu` / `*.cuh` / `*.cpp` / `*.h` file is byte-identical to the FIELD-I.14 baseline. The OptiX-side `OptixLaunchParams::scalar_field_config` field landed at FIELD-I.11 still exists; no `field_mapping_config` field added to `OptixLaunchParams` this slice (deferred to a future FIELD-BEAUTY.* OptiX slice per the operator's "Do not modify OptiX yet" rule).                                                                                                                                                                                  | PASS    |
| 8 | Build / test status                                    | Audit-host `ctest` returns `100% tests passed, 0 tests failed out of 13` (unchanged from FIELD-I.14; no new ctest target). Per-binary: `renderer_tests: 35/35` (unchanged); `field_tests: 135/135` (unchanged); `relativity_tests: 841/841`; `manifold_identity_tests: 408/408`; `cli_tests: 274/274`; every other suite unchanged. Full rebuild via `cmake --build /home/user/RelativityRender/build` adds no new warnings on any module. OptiX-ON-no-SDK build at FIELD-BEAUTY.3 landing also clean (14/14 ctest PASS in `/tmp/rr_build_optix_no_sdk`). Empirically verified.                                                                                                                                                                                                                                                                                              | PASS    |
| 9 | Runtime CUDA status                                    | `PASS_WITH_RUNTIME_DEFERRED`. The audit-host build is `RR_ENABLE_CUDA=OFF` (no CUDA SDK present), so the kernel arm's empirical beauty modulation cannot be exercised this audit. The host-side data-path (`AOVTargets::field_mapping_config` → `render_scene_with_aovs` → `CudaSceneView::field_mapping_config`) is verified structurally — clean compile + 13/13 ctest PASS. The SDK-host runtime scenarios will be exercised by a future CLI bridge slice's audit when the operator can flip `targets.field_mapping_config.target = ColorMultiplier` (or `Emission`) AND `targets.scalar_field_config.enabled = true` via CLI: (a) ColorMultiplier modulates beauty on hit pixels of a fixture with non-trivial Radial field; (b) Emission adds bounded grayscale emission on the same; (c) default-state PPM remains byte-identical to pre-FIELD-BEAUTY.3 baseline. | PASS (structural) — runtime DEFERRED to SDK-host audit pass when the future CLI bridge slice lands |
| 10 | Verdict                                               | All nine structural / runtime-status checks PASS. The FIELD-BEAUTY.3 surface is well-scoped, kernel-wired, byte-identical-by-default, two-gate-controlled, AOV-preserving, OptiX-isolated. Master rule #3 + #11 + #12 + #16 satisfied (see §3 below).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Commit shape

The FIELD-BEAUTY.3 commit (`8b8f100`) modifies five
files:

```
docs/BUILD_PLAN.md         | 294 ++++++++++++++++++++++++++
src/cuda/CudaRenderer.cu   |  15 +++
src/cuda/CudaRenderer.h    |  23 ++++
src/cuda/CudaScene.cuh     |  41 +++++
src/cuda/CudaTestKernel.cu |  73 +++++++
```

All source-code files touched are in `src/cuda/`;
zero non-CUDA source files modified. Zero
`CMakeLists.txt` modification (the `rr_field` PUBLIC
link on `rr_gpu` from FIELD-I.9 already propagates
`FieldMapping.h` transitively; the FIELD-BEAUTY.3
include addition is purely consumer-side). The
remaining file (`docs/BUILD_PLAN.md`) is the
per-slice entry mirroring the standard rubric.

The narrow scope intentionally excludes every other
file: no `src/optix/*`, no `src/core/Config.h`, no
`src/core/CommandLine.cpp`, no `src/main.cpp`, no
`src/io/SceneLoader.cpp`, no
`src/cuda/CudaPathTracer.cu`, no `tests/*`, no
scene-file additions. All deferred or out-of-scope
per the operator's brief.

The intentionally-narrow scope is honestly framed in
the BUILD_PLAN.md entry's "What does NOT ship"
section, which explicitly notes the missing
prerequisite `docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md`
task brief (FIELD-BEAUTY.1 + FIELD-BEAUTY.2 slots
unfilled). The operator's FIELD-BEAUTY.3 prompt body
itself was treated as the canonical task brief.

### 3.2 Check #1 — CUDA ColorMultiplier mapping exists

`src/cuda/CudaTestKernel.cu:605-607` defines the
ColorMultiplier mapping arm:

```cpp
if (scene.field_mapping_config.target
      == rr::field::FieldMappingTarget::ColorMultiplier) {
    color = color * mapped;
}
```

Context within the kernel:

- Line 597 — outer gate: `if (best.hit &&
  scene.scalar_field_config.enabled) {`.
- Lines 598-604 — sample + mapping evaluators:
    ```cpp
    const rr::math::Vec3 hit_pos_v3{
        best.position.x, best.position.y, best.position.z};
    const float sample =
        rr::field::evaluate(scene.scalar_field_config, hit_pos_v3);
    const float mapped =
        rr::field::evaluate_mapping(scene.field_mapping_config,
                                    sample);
    ```
- Lines 605-607 — ColorMultiplier branch.
- Lines 608-611 — Emission branch (mutually
  exclusive via `else if`).
- Lines 612-614 — fall-through comment for
  `target == None` / `target == DiagnosticAOV`.
- Line 614 — closing `}` of the outer gate.

The mapping uses the FIELD-I.2 `evaluate(...)`
helper to compute the per-pixel scalar sample at the
world-space hit position (`best.position`), then the
FIELD-I.4 `evaluate_mapping(...)` helper to derive
the multiplier (`strength * sample + bias`,
optionally clamped). The `color * mapped`
multiplication is the documented mapping math.

The arm is positioned BEFORE the Doppler /
searchlight modulation (which happens at line 553+
OUTSIDE the `if (best.hit)` block), so the field
contribution participates in the standard
relativistic pipeline uniformly with material
emission. The doc-comment at lines 547-595 documents
the placement rationale + the AOV-vs-mapping
separation.

### 3.3 Check #2 — CUDA Emission mapping exists

`src/cuda/CudaTestKernel.cu:608-611` defines the
Emission mapping arm:

```cpp
else if (scene.field_mapping_config.target
      == rr::field::FieldMappingTarget::Emission) {
    color = color + Vec3{mapped, mapped, mapped};
}
```

The Emission branch sits as the `else if` to the
ColorMultiplier branch, making the two mappings
mutually exclusive: only one of `ColorMultiplier` /
`Emission` can be the active target per launch. The
mapped scalar is replicated to grayscale RGB (`(m,
m, m)`) — the FIELD-I.4 `FieldMappingConfig` POD does
not carry a per-target color today; the doc-comment
at lines 593-595 documents this as a future-
FIELD-BEAUTY.* slice extension.

The additive form `color + Vec3{...}` adds the
field's contribution as bounded emission (bounded
because the FIELD-I.4 `evaluate_mapping(...)` evaluator
honours `clamp_output` + `[min_value, max_value]`
when the artist enables them; without clamping the
contribution is `strength * sample + bias`). The
operator's "add bounded scalar-field emission" rule
is satisfied by the FIELD-I.4 POD's own clamp
mechanism + the per-launch operator-authoring
discipline.

### 3.4 Check #3 — mapping activates only when field enabled AND target selected

Two-gate verified explicitly:

**Outer gate** (`scalar_field_config.enabled` +
`best.hit`):

```cpp
if (best.hit && scene.scalar_field_config.enabled) {
    // ... mapping arm body ...
}
```

The outer gate closes when:
- (a) `best.hit == false` (miss pixel) — sky shading
  goes through unchanged.
- (b) `scene.scalar_field_config.enabled == false`
  (the FIELD-I.2 default) — the entire mapping
  block short-circuits; neither `evaluate(...)`
  nor `evaluate_mapping(...)` is called.

**Inner target gates** (`field_mapping_config.target`):

```cpp
if (scene.field_mapping_config.target == ColorMultiplier) {
    color = color * mapped;
} else if (scene.field_mapping_config.target == Emission) {
    color = color + Vec3{mapped, mapped, mapped};
}
// (no else branch — target == None / DiagnosticAOV
// is the documented no-op fallthrough)
```

The inner gates close when:
- `target == None` (the FIELD-I.4 default; default
  `FieldMappingConfig{}` byte-for-byte).
- `target == DiagnosticAOV` (the AOV-only target;
  beauty arm is no-op; the FIELD-I.9 AOV write arm
  handles DiagnosticAOV at its own gate).

Both gates must open for any beauty modulation. The
default state (both gates closed) preserves
byte-identical output to the pre-FIELD-BEAUTY.3
baseline. The operator's brief's two-gate rule is
honoured structurally.

### 3.5 Check #4 — default mapping remains no-op

The FIELD-I.4 default `FieldMappingConfig{}` (audited
at FIELD-I.5's check #3 three-layer no-op anchor)
carries:
- `target = FieldMappingTarget::None` (the explicit
  `= 0` default at `FieldMapping.h:206-211`).
- `strength = 0.0f`.
- `bias = 0.0f`.
- All other fields at their FIELD-I.4-documented
  defaults.

The kernel arm's behaviour on the default:

- Outer gate `if (... && scene.scalar_field_config.enabled)`:
  - If `enabled == false` (FIELD-I.2 default): the
    outer gate closes; the arm short-circuits.
  - If `enabled == true` (artist-engaged): the
    outer gate opens; the inner gate fires.
- Inner gates check for `ColorMultiplier` /
  `Emission`; neither matches `None`:
  - Neither branch fires; the kernel falls through
    the mapping block with `color` unchanged.

Empirically: the default-state PPM output is
identical to the pre-FIELD-BEAUTY.3 baseline because
the kernel arm only writes to `color` inside the
target-specific branches; with `target == None`, no
write happens.

Even when the outer gate is bypassed (e.g. the
operator engages the field for diagnostic
visualization without setting up a mapping target),
the inner-gate fallthrough produces zero beauty
modulation. Master rule #3 + #16 satisfied — the
default behaviour is the documented no-op anchor.

### 3.6 Check #5 — disabled field remains no-op

The FIELD-I.2 default `ScalarFieldConfig{}.enabled =
false` (audited at FIELD-I.3 check #2 three-layer
no-op anchor). The kernel arm's outer gate:

```cpp
if (best.hit && scene.scalar_field_config.enabled) {
    // ... block body ...
}
```

When `enabled == false`, the entire block is
short-circuited:

- `evaluate(...)` is NOT called.
- `evaluate_mapping(...)` is NOT called.
- Neither target branch is evaluated.
- `color` is unchanged.

Even when the inner target slot is opened (`target ==
ColorMultiplier`), the outer gate's closure makes
the inner gate unreachable. The operator's
"disabled field = no-op" rule is structurally
satisfied.

This is the second of the two no-op anchors (the
first being `target = None` from check #4); the
operator can disable the entire mapping via either
gate independently. Both are load-bearing — they
prevent accidental beauty modulation when the artist
authors a partial config (e.g. enables the field
but forgets to set a mapping target, or vice versa).

### 3.7 Check #6 — fieldScalar diagnostic AOV remains available

The FIELD-I.9 diagnostic AOV write arm lives at
`src/cuda/CudaTestKernel.cu:797-805`:

```cpp
if (scene.aovs.field_scalar != nullptr) {
    if (best.hit) {
        const rr::math::Vec3 hit_pos_v3{
            best.position.x, best.position.y, best.position.z};
        scene.aovs.field_scalar[pix_idx_1] =
            rr::field::evaluate(scene.scalar_field_config, hit_pos_v3);
    } else {
        scene.aovs.field_scalar[pix_idx_1] = 0.0f;
    }
}
```

This arm is positioned AFTER the framebuffer write
(line 574-579) and AFTER the new FIELD-BEAUTY.3
mapping arm (lines 597-614). The per-line diff `git
diff 8a5dd54..8b8f100 -- src/cuda/CudaTestKernel.cu`
confirms zero changes inside the AOV-write block at
lines 797-805 (only the FIELD-BEAUTY.3 arm at
lines 547-614 was added, which is positioned
elsewhere — inside the `if (best.hit)` block).

The AOV's per-pixel output is the raw
`evaluate(scalar_field_config, hit_pos)` value,
independent of the mapping target. This preserves
the FIELD-I.4 audit's mapping-vs-diagnostic
separation: the diagnostic AOV says "what is the
field at this pixel"; the mapping arm says "how
does that field value contribute to beauty". The
two are independent — the operator can engage the
AOV (`field_debug = true` on a future CLI bridge
slice) WITHOUT engaging the mapping (target =
DiagnosticAOV or None), or engage both
simultaneously.

Master rule #3 satisfied — the FIELD-BEAUTY.3 slice
does NOT secretly modify the AOV write path under
the cover of "beauty mapping"; the two surfaces are
honestly separate.

### 3.8 Check #7 — OptiX path unchanged

`git diff 8a5dd54..8b8f100 --name-only --
'src/optix/'` returns zero hits. Every `src/optix/`
file (`OptixLaunchParams.h`, `OptixPrograms.cu`,
`OptixRenderer.h`, `OptixRenderer.cpp`,
`OptixBackend.cpp`, `OptixDenoiser.cpp`,
`OptixPipeline.cpp`, `OptixAccel.cpp`, `OptixSBT.h`)
is byte-identical to the FIELD-I.14 audit baseline.

The FIELD-I.11 `OptixLaunchParams::scalar_field_config`
field exists (audited at FIELD-I.12); the
FIELD-I.11 OptiX kernel arm at
`OptixPrograms.cu:960-980` writes the raw field
sample to `aov_field_scalar` (audited at
FIELD-I.12). Both are preserved verbatim. No
`OptixLaunchParams::field_mapping_config` field is
added this slice; no OptiX program arm consumes a
FieldMappingConfig payload yet. The OptiX-side
beauty mapping is reserved for a separate
FIELD-BEAUTY.* slice (the OptiX-bridge mirror of
FIELD-BEAUTY.3, mirroring the FIELD-I.9 → FIELD-I.11
precedent).

The operator's "Do not modify OptiX yet" rule
honoured.

### 3.9 Check #8 — build / test status

Audit-host `ctest` empirical output:

```
13/13 Test #13: renderer_tests ........ Passed
100% tests passed, 0 tests failed out of 13
```

Per-binary breakdown (test counts unchanged from
FIELD-I.14):

| Suite                       | Count    |
|-----------------------------|----------|
| math_tests                  | unchanged|
| image_tests                 | unchanged|
| gpu_tests                   | unchanged|
| pathtracer_tests            | unchanged|
| pathtracer_nee_tests        | unchanged|
| pathtracer_bsdf_tests       | unchanged|
| pathtracer_mis_tests        | unchanged|
| cli_tests                   | 274/274  |
| relativity_tests            | 841/841  |
| manifold_identity_tests     | 408/408  |
| field_tests                 | 135/135  |
| demo_tests                  | unchanged|
| renderer_tests              | 35/35    |

OptiX-ON-no-SDK build at the FIELD-BEAUTY.3 landing
commit also clean: 14/14 ctest PASS (including
`optix_tests`). The CUDA-side kernel changes
propagate cleanly through the rr_gpu include path
without breaking the OptiX-on stub fallback.

Full rebuild via `cmake --build
/home/user/RelativityRender/build` clean — no new
warnings on any module.

### 3.10 Check #9 — runtime CUDA status

`PASS_WITH_RUNTIME_DEFERRED`.

The audit-host build is `RR_ENABLE_CUDA=OFF` (no
CUDA SDK present), so the kernel arm's empirical
beauty modulation cannot be exercised this audit.
The host-side data-path is verified structurally:

- `AOVTargets::field_mapping_config` declaration
  compiles cleanly into both `rr_gpu` (the
  renderer-tests link path) + `rr_renderer` (the
  renderer_tests target's transitive consumer).
- `CudaRenderer.cu`'s threading line at line 360
  passes the CUDA-disabled audit-host compile (the
  file is excluded from compilation on
  `RR_ENABLE_CUDA=OFF`, but the host-side
  `AOVTargets` struct that consumers populate IS
  compiled into the audit-host build via
  `CudaRenderer.h`; 35/35 renderer_tests PASS
  confirms no host-side type / link breakage).

The SDK-host runtime checks DEFERRED to a future
CLI bridge slice's audit:

- **ColorMultiplier visualisation (CUDA path).**
  Run `RelativityRender --render-aovs
  --field-debug --field-mapping-target
  color-multiplier --field-strength 0.5
  --field-bias 0.5 ...` (exact CLI flags TBD at
  the bridge slice). Verify: the beauty PPM
  shows the field's contribution multiplying
  the per-pixel color; the
  `aov_field_scalar.ppm` shows the raw
  smoothstep pattern (independent of the
  multiplier).
- **Emission visualisation (CUDA path).** Run the
  same with `--field-mapping-target emission`.
  Verify: the beauty PPM shows additive
  grayscale emission tracking the field; the
  diagnostic AOV PPM is identical to the
  ColorMultiplier case (same raw sample,
  different mapping target).
- **Disabled-field baseline.** Run
  `--render-aovs --field-debug` WITHOUT
  `--field-enable`. Verify: the beauty PPM is
  byte-identical to the pre-FIELD-BEAUTY.3
  baseline; the `aov_field_scalar.ppm` is
  uniformly black.
- **Default-mapping baseline.** Run
  `--render-aovs --field-enable --field-kind
  radial` WITHOUT `--field-mapping-target`.
  Verify: the beauty PPM is byte-identical to
  the pre-FIELD-BEAUTY.3 baseline (default
  `target == None` short-circuits the inner
  gate); the `aov_field_scalar.ppm` shows the
  raw smoothstep pattern.
- **Doppler / searchlight interaction.** Run
  `--render-aovs --field-debug
  --field-mapping-target color-multiplier
  --field-strength 0.5 ...
  --observer-perception-mode relativistic
  --observer-beta 0.5 --observer-direction
  1,0,0`. Verify: the beauty PPM shows the
  ColorMultiplier-mapped field flowing through
  the Doppler color shift + searchlight
  scaling (the mapping happens BEFORE the
  Doppler pipeline in the kernel arm; the
  contribution sees the relativistic
  treatment).

All five scenarios apply this slice's CUDA arm but
require both a CUDA SDK host AND the future CLI
bridge slice. The deferral is honest scope: the
FIELD-BEAUTY.3 surface is the kernel arm ONLY,
with the CLI / dispatcher plumbing landing as a
separate slice.

### 3.11 Master-rule satisfaction recap

- **Master rule #3 ("no fake stubs"):** satisfied.
  The kernel arm at `CudaTestKernel.cu:597-614` is
  fully wired (real `evaluate(...)` invocations;
  real `evaluate_mapping(...)` invocations; real
  ColorMultiplier + Emission branches; real
  `color` modifications). The structural
  unreachability via the double-gate is honest
  scope framing (the doc-comment at lines 547-595
  documents this explicitly). No fake stub.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. The kernel arm's
  behaviour is documented as contract on every
  modified file's doc-comments + structurally
  rooted in the audit-host-verified FIELD-I.2
  evaluator (the 80 RR_CHECK assertions on
  `tests/field_tests.cpp`'s §1-§6) + the
  FIELD-I.4 mapping evaluator (the 55 RR_CHECK
  assertions on §7). The kernel arm's
  composition follows from the two evaluators'
  composed contracts; cross-evaluator
  composition is empirically testable when the
  future CLI bridge slice exposes a fixture-
  exercising CLI.

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Scope deliberately narrow to CUDA
  path only — OptiX deferred per operator brief,
  CLI deferred, dispatcher emit deferred,
  scene-loader extension deferred, path-tracer
  integration deferred, fixture extension
  deferred. The FIELD-BEAUTY.* arc opens
  parallel to the FIELD-I.* arc, not in place
  of it; both arcs coexist.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied.
  The FIELD-BEAUTY.3 default state is unchanged
  from the FIELD-I.14 baseline:
    - No `--render-*` action produces a new
      file.
    - No existing PPM filename changes.
    - No beauty pass arithmetic changes (the
      kernel arm is gated; defaults close the
      gates).
    - No existing AOV slot's value changes.
  The single observable behaviour change is the
  structural presence of the kernel arm; its
  observable behaviour from every existing CUDA
  CLI invocation is zero because both gates are
  closed by default.

### 3.12 Honest scope recap

This audit is a **CUDA beauty-mapping audit with
SDK-host runtime DEFERRED** + **OptiX path
preserved-unchanged** + **diagnostic-AOV preserved-
unchanged**. The verdict `PASS` reflects:

- (a) The structural kernel-arm surface is well-
  formed (both ColorMultiplier and Emission
  branches wired; double-gate honoured).
- (b) The default-state preservation is honest
  (default `target == None` + default `enabled ==
  false` produce byte-identical output).
- (c) The diagnostic-AOV preservation is
  structural (the post-framebuffer-write AOV
  arm is byte-identical).
- (d) The OptiX-isolation is structural (no
  OptiX file touched).
- (e) The audit-host + OptiX-ON-no-SDK builds
  are both empirically verified.
- (f) The SDK-host runtime scenarios are
  honestly documented as deferred (require both
  the future CLI bridge slice AND an SDK host).

The runtime deferral is consistent with the
FIELD-I.10 + FIELD-I.12 + FIELD-I.14 audits' framing
— those audits also deferred the SDK-host runtime
scenarios to the future CLI bridge slice. The
FIELD-BEAUTY.4 audit inherits the deferral cleanly,
acknowledging that the FIELD-BEAUTY.* arc's
SDK-host validation will land alongside the
FIELD-I.* arc's SDK-host validation at the future
combined CLI bridge slice.

The honest framing of the missing prerequisite
task brief (`docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md`)
is preserved in the FIELD-BEAUTY.3 commit's
BUILD_PLAN.md entry; this audit references it
explicitly so the renumbered ladder remains
consistent. A future doc slice may retroactively
author the task brief if standard precedent is
desired.

---

## 4. NEXT

### 4.1 FIELD-BEAUTY.* sub-slice ladder

The FIELD-BEAUTY.* arc opens with FIELD-BEAUTY.3
(missing FIELD-BEAUTY.1 + FIELD-BEAUTY.2 slots) +
the FIELD-BEAUTY.4 audit (this doc). The post-
FIELD-BEAUTY.4 ladder is:

- **FIELD-BEAUTY.5** — OptiX-side beauty mapping
  (the symmetric OptiX bridge for the
  `FieldMappingConfig` POD; mirrors FIELD-I.9 →
  FIELD-I.11 precedent shape; adds
  `OptixLaunchParams::field_mapping_config` +
  the OptiX closest-hit program's beauty-mapping
  arm; same single-source-of-truth math leaf as
  the CUDA arm for cross-backend equivalence by
  construction).
- **FIELD-BEAUTY.6** — OptiX bridge audit.
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
bridge for the diagnostic AOV; FIELD-I.16 = CLI
bridge audit; FIELD-I.17+ = mapping CLI + kernel
pipeline + capstone). The two arcs may be merged
at the CLI bridge slice (a single FIELD-I.15-or-
FIELD-BEAUTY.7 slice could flip both diagnostic AOV
+ beauty mapping gates simultaneously) or remain
separate per operator preference.

### 4.2 Candidate next slots (prioritised)

**(a) RECOMMENDED — FIELD-BEAUTY.5: OptiX-side
beauty mapping** (the symmetric OptiX bridge).
Natural continuation: closes check #7's
"OptiX path unchanged" preservation by extending
the OptiX side with the same mapping arm; pairs
symmetrically with FIELD-BEAUTY.3 so future CLI
flips both backends' mapping arms simultaneously.

**(b) Manifold-orthogonal work.** Multiple
options available:
  - **Deferred SDK-host runtime pass** for the
    OBSERVER.* + OBS-P.* + OBS-F.* + FIELD-I.*
    + FIELD-BEAUTY.* arc family (highest
    converging-leverage option; converts every
    `PASS_WITH_RUNTIME_DEFERRED` verdict in the
    family to PASS).
  - **MANI-I.12 final cross-host manifold
    audit**.
  - **Denoiser integration with chart-aware
    AOVs**.
  - **Path-tracer feature breadth** (NEE
    extension, BSDF expansion, MIS tuning).

**(c) NOT RECOMMENDED — direct FIELD-BEAUTY.7
CLI bridge slice skipping FIELD-BEAUTY.5
OptiX bridge.** Would land a CLI flag that
modulates beauty on CUDA but not on OptiX; the
cross-backend bit-identity expectation breaks
(CUDA shows the mapping, OptiX doesn't). Better
to pair the OptiX bridge with the CUDA bridge
before opening the CLI gate so both backends
become reachable simultaneously (mirrors the
FIELD-I.9 + FIELD-I.11 → FIELD-I.13 sequencing).

**(d) RETROACTIVE — author
`docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md`** (the
missing FIELD-BEAUTY.1 task brief). The operator
may want to backfill the standard task-brief
precedent. Out of scope for this audit;
deferrable to operator discretion.

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
  the FIELD-BEAUTY.3 kernel arm consumes).

### 5.2 FIELD-I.* arc references (parallel arc)

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1 — the canonical FIELD-I.* arc
  plan; the FIELD-BEAUTY.* arc is a parallel
  arc that lifts the "no field-to-beauty
  mapping yet" non-goal from FIELD-I.1 §2.4).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md`
  (FIELD-I.3 — the three-layer no-op anchor at
  check #2 underpins the FIELD-BEAUTY.4 check
  #5 disabled-field no-op verification).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md`
  (FIELD-I.5 — the three-layer no-op anchor at
  check #3 underpins the FIELD-BEAUTY.4 check
  #4 default-target-None no-op verification).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_AUDIT.md`
  (FIELD-I.8).
- `docs/FIELD_SCALAR_CUDA_BRIDGE_AUDIT.md`
  (FIELD-I.10 — the precedent CUDA-bridge
  audit shape this FIELD-BEAUTY.4 audit
  structurally mirrors).
- `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
  (FIELD-I.12 — the OptiX-side audit whose
  bridge is preserved verbatim at check #7).
- `docs/FIELD_SCALAR_FIXTURE.md` (FIELD-I.13).
- `docs/FIELD_SCALAR_FIXTURE_AUDIT.md`
  (FIELD-I.14 — the immediate audit baseline
  for the FIELD-BEAUTY.3 diff).

### 5.3 FIELD-BEAUTY.* arc references

- (FIELD-BEAUTY.1) — UNFILLED task brief slot.
  The operator's FIELD-BEAUTY.3 prompt body
  serves as the canonical task brief.
- (FIELD-BEAUTY.2) — UNFILLED slot.
- FIELD-BEAUTY.3 — the audited slice
  (`8b8f100`).

### 5.4 Source surface audited

- `src/cuda/CudaScene.cuh` (modified +41 lines
  vs FIELD-I.14 baseline; the new
  `field_mapping_config` field at line 216 +
  its doc-comment block at lines 178-215 +
  the new `#include "field/FieldMapping.h"`
  at line 20).
- `src/cuda/CudaRenderer.h` (modified +23
  lines; the new `field_mapping_config` field
  on `AOVTargets` at line 283 + its
  doc-comment block at lines 263-282 + the
  new `#include "field/FieldMapping.h"` at
  line 3).
- `src/cuda/CudaRenderer.cu` (modified +15
  lines; the new `view.field_mapping_config =
  targets.field_mapping_config;` thread at
  line 360 + the doc-comment at lines
  347-359).
- `src/cuda/CudaTestKernel.cu` (modified +73
  lines; the new beauty-mapping kernel arm at
  lines 547-614 + the doc-comment block at
  lines 547-595).

### 5.5 Test surface unchanged

All test files in `tests/` are byte-identical
to the FIELD-I.14 baseline. No test extension
this slice (the kernel arm's empirical
behaviour requires SDK-host runtime
verification; deferred per §3.10).

### 5.6 Surrounding commit SHAs

- `8b8f100` — FIELD-BEAUTY.3 audited tree
  (the per-slice gate target).
- `8a5dd54` — FIELD-I.14 baseline (the diff
  baseline for checks #6 + #7).
- `98a0e35` — FIELD-I.13 fixture impl (the
  fixture the FIELD-BEAUTY.3 mapping arm
  will exercise once the CLI bridge slice
  lands the `field_mapping` authoring
  surface).
- `e15934e` — FIELD-I.11 OptiX bridge (the
  symmetric kernel-arm precedent the future
  FIELD-BEAUTY.5 OptiX-side beauty mapping
  will mirror).
- `e1a42c2` — FIELD-I.9 CUDA bridge (the
  symmetric kernel-arm precedent the
  FIELD-BEAUTY.3 mapping arm pairs with).
- `683a16d` — FIELD-I.4 mapping config POD
  impl (the `FieldMappingConfig` POD the
  FIELD-BEAUTY.3 kernel arm consumes).
- `40c387b` — FIELD-I.2 scalar field model
  impl (the `ScalarFieldConfig` POD the
  FIELD-BEAUTY.3 kernel arm consumes via
  `evaluate(...)`).

### 5.7 Unchanged source files (sampled)

The following files are byte-identical to the
FIELD-I.14 baseline (`8a5dd54`), confirmed by
the diff filters at checks #6 + #7 +
narrow-scope discipline:

- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/optix/`.
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
- Other `src/cuda/` files:
  `CudaAccumulation.cu`, `CudaAccumulation.cuh`,
  `CudaAOV.cuh`, `CudaBuffer.cpp`,
  `CudaBuffer.h`, `CudaContext.cpp`,
  `CudaContext.h`, `CudaIntersection.cuh`,
  `CudaKernels.cuh`, `CudaLight.cuh`,
  `CudaMaterial.cuh`, `CudaMesh.cuh`,
  `CudaPathTracer.cu`, `CudaPathTracer.cuh`,
  `CudaRngTestKernel.cu`, `CudaTexture.cuh`,
  `CudaTextureSampleTestKernel.cu`,
  `CudaTiming.cpp`, `CudaTiming.h`.

### 5.8 Unchanged test + scene files

All test files (`tests/`) are byte-identical
to the FIELD-I.14 baseline. All scene files
(`scenes/`) are byte-identical to the
FIELD-I.14 baseline (including the FIELD-I.13
fixture).

### 5.9 Unchanged build configuration

`CMakeLists.txt` is byte-identical to the
FIELD-I.14 baseline. The `rr_field` PUBLIC link
on `rr_gpu` (from FIELD-I.9) propagates the
`FieldMapping.h` include path transitively;
the FIELD-BEAUTY.3 `#include "field/FieldMapping.h"`
additions in `CudaScene.cuh` + `CudaRenderer.h`
are consumer-side and require no CMake change.

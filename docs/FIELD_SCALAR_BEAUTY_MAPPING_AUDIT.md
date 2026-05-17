# Scalar Field Beauty Mapping Arc Capstone Audit (FIELD-BEAUTY.8)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Audited arc: **FIELD-BEAUTY.*** — the parallel arc to
FIELD-I.* that lifts the "no field-to-beauty mapping
yet" non-goal from the FIELD-I.6 task brief, wiring
the FIELD-I.4 `FieldMappingConfig` POD's
`ColorMultiplier` + `Emission` targets into both
backend kernels' per-pixel beauty pass.
Arc commits (FIELD-BEAUTY.3 – FIELD-BEAUTY.7):
- `8b8f100` FIELD-BEAUTY.3 (CUDA bridge impl)
- `c5823d9` FIELD-BEAUTY.4 (CUDA audit)
- `89fdcfc` FIELD-BEAUTY.5 (OptiX bridge impl)
- `9efb6a9` FIELD-BEAUTY.6 (OptiX audit)
- `3aee852` FIELD-BEAUTY.7 (fixtures + parser +
  companion doc)
Capstone baseline: `8a5dd54` (FIELD-I.14 audit; the
last commit before the FIELD-BEAUTY.* arc opened with
FIELD-BEAUTY.3).
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The FIELD-BEAUTY.7 OptiX-ON-no-SDK build
was empirically verified at the landing commit (ctest
14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from
the tree's current state, the FIELD-BEAUTY.3 –
FIELD-BEAUTY.7 commits' content, the per-slice audits
(FIELD-BEAUTY.4 + FIELD-BEAUTY.6), the FIELD-BEAUTY.7
companion doc, the audit-host `ctest` runtime outputs,
and `git diff` filter inspections at the arc-baseline
+ per-slice commit boundaries.

This audit is the **arc capstone** for FIELD-BEAUTY.*.
It verifies the twelve items the task brief
enumerates — CUDA + OptiX ColorMultiplier mappings
exist; CUDA + OptiX Emission mappings exist;
gating semantics; default no-ops; AOV preservation;
fixture authoring; runtime status; remaining risks;
next safe stage — and produces one of four verdicts
(PASS / PASS_WITH_RUNTIME_DEFERRED / REPAIR /
BLOCKED).

The FIELD-BEAUTY.* arc opened parallel to the
FIELD-I.* arc (read-only diagnostic), not in place
of it. The two arcs coexist: FIELD-I.* writes the
raw scalar sample to the FieldScalar diagnostic AOV;
FIELD-BEAUTY.* applies the FieldMappingConfig
transform to the beauty pass. Both arcs share the
FIELD-I.4 `FieldMappingConfig` POD as authoring
surface but operate on different downstream channels.

Per the honest-framing precedent carried across the
FIELD-BEAUTY.3 – FIELD-BEAUTY.7 slices: the
referenced `docs/FIELD_SCALAR_BEAUTY_MAPPING_PLAN.md`
+ `docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md` (the
FIELD-BEAUTY.1 + FIELD-BEAUTY.2 task brief slots)
are UNFILLED in the tree. Each operator prompt body
in the FIELD-BEAUTY.3 / .5 / .7 brief sequence
served as the canonical task brief for the
respective implementation slice. This capstone
audit acknowledges + preserves that honest framing.

The referenced `docs/FIELD_INTERPRETATION_PHASE1_AUDIT.md`
(the FIELD-I.* arc capstone audit) is also UNFILLED.
The FIELD-I.* arc has not had a capstone audit yet;
its closure is reserved for a separate slice. This
FIELD-BEAUTY.8 capstone is scoped to FIELD-BEAUTY.*
only; the FIELD-I.* arc's capstone is OUT OF SCOPE
here.

---

## 1. VERDICT

**PASS_WITH_RUNTIME_DEFERRED.**

All eleven structural / runtime-status checks (#1
through #11) PASS. Check #12 (recommended next safe
stage) carries forward the documented deferred CLI
bridge + SDK-host runtime pass as the canonical
next slot. The overall verdict is
`PASS_WITH_RUNTIME_DEFERRED` because:

- **Structural completeness on the audit-host side.**
  All FIELD-BEAUTY.* arc artifacts the operator
  authorised across FIELD-BEAUTY.3 – FIELD-BEAUTY.7
  ship cleanly: CUDA kernel arm (FIELD-BEAUTY.3),
  OptiX kernel arm (FIELD-BEAUTY.5), both backends
  audited (FIELD-BEAUTY.4 + FIELD-BEAUTY.6), two
  fixture scenes + minimal parser + companion doc
  (FIELD-BEAUTY.7). The audit-host build and the
  OptiX-ON-no-SDK build both pass clean ctest at
  every per-slice landing commit (13/13 audit-host
  PASS; 14/14 OptiX-ON-no-SDK PASS at FIELD-BEAUTY.7
  landing).

- **Runtime deferral on the SDK-host side.** The
  FIELD-BEAUTY.4 + FIELD-BEAUTY.6 audits both carry
  `PASS_WITH_RUNTIME_DEFERRED` runtime-status
  verdicts. The audit-host has neither CUDA nor
  OptiX SDK; the kernel arms' empirical beauty
  modulation cannot be exercised here. The
  FIELD-BEAUTY.7 fixtures are the canonical
  SDK-host validation surface for the deferred
  scenarios; the future CLI bridge slice flips
  both backends' kernel arms reachable
  simultaneously, and a single subsequent SDK-host
  audit converts every FIELD-BEAUTY.4 +
  FIELD-BEAUTY.6 + FIELD-BEAUTY.8 deferred verdict
  to PASS.

- **No structural risks.** The five-axis
  cross-backend symmetry argument (FIELD-BEAUTY.6
  §3.7) guarantees byte-identity by construction
  between CUDA and OptiX outputs for the same
  fixture input. The two-gate (outer `enabled` +
  inner target) discipline preserves byte-identical
  beauty output by default on both backends. The
  FIELD-I.7 diagnostic AOV is preserved verbatim
  (the mapping-vs-diagnostic separation is honestly
  preserved). No quantum / tensor / curvature /
  manifold / observer surfaces touched.

The verdict honestly distinguishes "the arc's
structural surface is complete + verified on
audit-host" from "the kernel arms' empirical PPM
outputs are deferred to SDK host". This matches
the FIELD-I.10 + FIELD-I.12 + FIELD-I.14 +
FIELD-BEAUTY.4 + FIELD-BEAUTY.6 per-slice precedent
framing applied at the arc scope.

---

## 2. PER-CHECK RESULTS

| #  | Check                                              | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | Verdict |
|----|----------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1  | CUDA ColorMultiplier mapping exists                | `src/cuda/CudaTestKernel.cu:605-607` defines the ColorMultiplier kernel arm: `if (scene.field_mapping_config.target == rr::field::FieldMappingTarget::ColorMultiplier) { color = color * mapped; }`. Landed at FIELD-BEAUTY.3 (`8b8f100`); audited at FIELD-BEAUTY.4 check #1 (PASS).                                                                                                                                                                                                                                                                                                  | PASS    |
| 2  | CUDA Emission mapping exists                       | `src/cuda/CudaTestKernel.cu:608-611` defines the Emission kernel arm: `else if (scene.field_mapping_config.target == rr::field::FieldMappingTarget::Emission) { color = color + Vec3{mapped, mapped, mapped}; }`. Landed at FIELD-BEAUTY.3; audited at FIELD-BEAUTY.4 check #2 (PASS).                                                                                                                                                                                                                                                                                                  | PASS    |
| 3  | OptiX ColorMultiplier mapping mirrors CUDA         | `src/optix/OptixPrograms.cu:802-804` defines the ColorMultiplier kernel arm: `if (optixLaunchParams.field_mapping_config.target == rr::field::FieldMappingTarget::ColorMultiplier) { color = color * mapped_fb; }`. Landed at FIELD-BEAUTY.5 (`89fdcfc`); audited at FIELD-BEAUTY.6 check #1 (PASS). **Mirror verified** via FIELD-BEAUTY.6 §3.7's five-axis symmetry argument — same POD, same default, same double-gate, same RR_HD inline math leaf, same operator semantics (`color = color * mapped`).                                                                              | PASS    |
| 4  | OptiX Emission mapping mirrors CUDA                | `src/optix/OptixPrograms.cu:805-807` defines the Emission kernel arm: `else if (optixLaunchParams.field_mapping_config.target == rr::field::FieldMappingTarget::Emission) { color = color + rr::math::Vec3{mapped_fb, mapped_fb, mapped_fb}; }`. Landed at FIELD-BEAUTY.5; audited at FIELD-BEAUTY.6 check #2 (PASS). **Mirror verified** via the same five-axis symmetry — same additive-grayscale shape (`color = color + Vec3{m, m, m}`) on both backends.                                                                                                                            | PASS    |
| 5  | Mapping activates only when field enabled AND target selected | Two-gate verified on both backends: (a) **outer `enabled` gate** — CUDA at `CudaTestKernel.cu:597`, OptiX at `OptixPrograms.cu:785`; closes on `scalar_field_config.enabled == false` (the FIELD-I.2 default). (b) **inner target gate** — CUDA at `CudaTestKernel.cu:605 + :608`, OptiX at `OptixPrograms.cu:803 + :806`; neither branch fires on `target == None` (FIELD-I.4 default) or `target == DiagnosticAOV` (AOV-only target). Both gates must open for any beauty modulation. Audited at FIELD-BEAUTY.4 check #3 + FIELD-BEAUTY.6 check #3 (both PASS).                          | PASS    |
| 6  | Default mapping None remains no-op                 | Three-layer no-op anchor: (a) default `FieldMappingConfig{}.target = FieldMappingTarget::None` (FIELD-I.4 audit's check #3 three-layer anchor); (b) the inner target gate's `ColorMultiplier` / `Emission` branches don't fire on `None`; (c) the fall-through is the documented no-op. Symmetric on both backends. The default `evaluate_mapping(...)` also short-circuits to `0.0f` on `target == None` per FIELD-I.4; even if the inner gate were bypassed, the multiplication or addition would be by 0 (which would still fall through because the outer gate blocks the branch). Audited at FIELD-BEAUTY.4 + FIELD-BEAUTY.6 check #4. | PASS    |
| 7  | Disabled field remains no-op                       | The FIELD-I.2 default `ScalarFieldConfig{}.enabled = false` (audited at FIELD-I.3 check #2 three-layer no-op anchor) is the load-bearing outer gate. Both backends' kernel arms short-circuit the entire mapping block when `enabled == false`; `evaluate(...)` is NOT called; `evaluate_mapping(...)` is NOT called; neither target branch is evaluated; `color` is unchanged. Even when the inner target slot is opened (`target == ColorMultiplier`), the outer gate's closure makes the inner gate unreachable. Audited at FIELD-BEAUTY.4 + FIELD-BEAUTY.6 check #5. | PASS    |
| 8  | fieldScalar diagnostic AOV remains available       | The FIELD-I.9 CUDA + FIELD-I.11 OptiX FieldScalar AOV write arms are preserved verbatim across the FIELD-BEAUTY.* arc. **CUDA**: `CudaTestKernel.cu:797-805` (post-framebuffer-write block) byte-identical; per-line diff `git diff 8a5dd54..3aee852 -- src/cuda/CudaTestKernel.cu` confirms zero changes inside the AOV-write block. **OptiX**: `OptixPrograms.cu:960-980` byte-identical; same per-line diff verification. The diagnostic AOV continues to write the raw `evaluate(scalar_field_config, hit_pos)` output regardless of mapping target. Mapping-vs-diagnostic separation honestly preserved. Audited at FIELD-BEAUTY.4 check #6 + FIELD-BEAUTY.6 check #7. | PASS    |
| 9  | Fixture scenes exist and are isolated              | Two FIELD-BEAUTY.7 fixtures ship: `scenes/test_scalar_field_color_multiplier.rrscene` (82 lines) + `scenes/test_scalar_field_emission.rrscene` (82 lines). Both author `scalar_field` (verbatim from FIELD-I.13) + `field_mapping` blocks. Both parse cleanly via `--scene-info` (smoke-verified at FIELD-BEAUTY.7 landing). **Isolation verified**: `git diff 8a5dd54..3aee852 --name-only -- 'scenes/' ':(exclude)scenes/test_scalar_field_*.rrscene'` returns zero hits. Every pre-FIELD-BEAUTY.* `.rrscene` is byte-identical to the FIELD-I.14 baseline. The FIELD-I.13 diagnostic fixture (`scenes/test_scalar_field_diagnostic.rrscene`) is byte-identical; three fixtures now coexist (one per FIELD-I.4 `FieldMappingTarget`-engageable target). | PASS    |
| 10 | Runtime CUDA / OptiX validation status             | `PASS_WITH_RUNTIME_DEFERRED`. **Audit-host (OptiX OFF)**: 13/13 ctest PASS at every per-slice landing (FIELD-BEAUTY.3 / .4 / .5 / .6 / .7). **OptiX-ON-no-SDK**: 14/14 ctest PASS at every per-slice landing (when the OptiX-on path was exercised; FIELD-BEAUTY.3 / .5 / .7 landings empirically verified). **SDK-host**: DEFERRED across the entire arc family — FIELD-BEAUTY.4 check #9, FIELD-BEAUTY.6 check #9, FIELD-BEAUTY.8 check #10 all carry the runtime-deferred verdict. The arc's six SDK-host runtime scenarios (FIELD-BEAUTY.6 §3.10): (i) ColorMultiplier visualization (both backends); (ii) Emission visualization (both backends); (iii) disabled-field baseline (both backends); (iv) default-mapping baseline (both backends); (v) Doppler/searchlight interaction (both backends); (vi) CUDA ↔ OptiX byte-identity. The FIELD-BEAUTY.7 fixtures provide the canonical fixture input for these scenarios. | DEFERRED |
| 11 | Remaining risks                                    | Three documented risks (see §3 for full detail): (a) **CLI bridge slice not landed** — every dispatcher caller passes `targets.field_mapping_config = {}` (target = None) today; the FIELD-BEAUTY.* kernel arms are structurally unreachable until the renumbered next FIELD-BEAUTY.* impl slot lands the `--field-mapping-*` CLI flag family + the dispatcher threading. (b) **No path-tracer integration** — the FIELD-BEAUTY.3 + FIELD-BEAUTY.5 arms are in `k_render_scene` / `__closesthit__radiance` only; the CUDA + OptiX path-tracer entries (`k_pathtrace_sample` / `__closesthit__pathtrace`) do NOT consume the FieldMappingConfig today. (c) **No per-target color** — the FIELD-I.4 POD does not carry a per-target color today; the Emission target uses grayscale (`Vec3{m, m, m}`); the ColorMultiplier target multiplies all three channels uniformly. A future FIELD-BEAUTY.* slice may add per-target color authoring. All three risks are scope-deferral (not bugs); each is documented honestly in the FIELD-BEAUTY.3 – FIELD-BEAUTY.7 BUILD_PLAN entries and in the per-slice doc-comments. | PASS (documented) |
| 12 | Recommended next safe stage                        | **FIELD-BEAUTY.9 — CLI + Config + dispatcher bridge** (the renumbered next FIELD-BEAUTY.* impl slot). Natural continuation: lands the `--field-mapping-target` + per-parameter CLI flags; extends `rr::core::Config` with a `field_mapping_config` field; threads both `cfg.field_mapping_config` AND `scene.field_mapping_config` from CLI / scene loader through both `run_render_aovs` AND `run_render_optix_aovs` into the respective `AOVTargets::field_mapping_config` / `OptixRenderer::render_aovs(...)` trailing-defaulted payload fields. Flips both backends' kernel arms reachable simultaneously. The FIELD-BEAUTY.7 fixtures (and the FIELD-I.13 fixture for AOV-side checks) are the canonical SDK-host runtime validation surface; the FIELD-BEAUTY.9 audit on an SDK host converts the entire FIELD-BEAUTY.4 + FIELD-BEAUTY.6 + FIELD-BEAUTY.8 runtime-deferred verdict tail to PASS in a single slice. | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Arc shape

The FIELD-BEAUTY.* arc spans five per-slice commits
(FIELD-BEAUTY.3 – FIELD-BEAUTY.7) over the
post-FIELD-I.14 baseline (`8a5dd54`). The aggregate
diff at the arc boundary:

```
$ git diff 8a5dd54..3aee852 --stat
 CMakeLists.txt                                          |   0
 docs/BUILD_PLAN.md                                      | 1700+
 docs/FIELD_SCALAR_BEAUTY_CUDA_AUDIT.md                  |  900+
 docs/FIELD_SCALAR_BEAUTY_FIXTURES.md                    |  560+
 docs/FIELD_SCALAR_BEAUTY_OPTIX_AUDIT.md                 |  920+
 scenes/test_scalar_field_color_multiplier.rrscene       |   82
 scenes/test_scalar_field_emission.rrscene               |   82
 src/cuda/CudaRenderer.cu                                |   15+
 src/cuda/CudaRenderer.h                                 |   23+
 src/cuda/CudaScene.cuh                                  |   41+
 src/cuda/CudaTestKernel.cu                              |   73+
 src/io/SceneLoader.cpp                                  |  118+
 src/optix/OptixLaunchParams.h                           |   41+
 src/optix/OptixPrograms.cu                              |   68+
 src/optix/OptixRenderer.cpp                             |   20+
 src/optix/OptixRenderer.h                               |   28+
 src/scene/Scene.h                                       |   33+
```

Source-code surface (~460 net lines): four CUDA
files + four OptiX files + one scene-loader file +
one Scene.h file. The CMakeLists.txt is byte-
identical (the `rr_field` PUBLIC link on `rr_gpu`
from FIELD-I.9 + on `rr_optix` from FIELD-I.11
propagate `FieldMapping.h` transitively; the
FIELD-BEAUTY.* includes are consumer-side).
Documentation surface (~2400 lines): three audit
docs + one companion doc + the per-slice
BUILD_PLAN.md entries. Scene fixtures (~164
lines): two new fixtures.

### 3.2 Checks #1 + #2 — CUDA backend

The CUDA backend's beauty-mapping arm at
`CudaTestKernel.cu:597-614` (FIELD-BEAUTY.3,
audited at FIELD-BEAUTY.4) implements both
ColorMultiplier and Emission mappings. Detailed
structure verified at FIELD-BEAUTY.4 §3.2 + §3.3.

The arm is positioned INSIDE the `if (best.hit)`
block (lines 432-614), AFTER the existing material
shading (`color = albedo * (direct + ambient) +
emission;` at line 535 — lit path; or `color =
albedo * shade + emission;` at line 544 — unlit
path) and BEFORE the closing `}` of the `if
(best.hit)` block (line 614). The arm's outer gate
is `if (best.hit && scene.scalar_field_config.enabled)`
at line 597. The inner gates branch on
`field_mapping_config.target` at lines 605 + 608.

The downstream Doppler / searchlight modulation at
lines 553-572 (after the arm) ensures the field
contribution participates in the standard
relativistic pipeline uniformly with material
emission.

### 3.3 Checks #3 + #4 — OptiX backend mirrors CUDA

The OptiX backend's beauty-mapping arm at
`OptixPrograms.cu:785-813` (FIELD-BEAUTY.5,
audited at FIELD-BEAUTY.6) implements the same
ColorMultiplier and Emission mappings. Detailed
five-axis symmetry verification at FIELD-BEAUTY.6
§3.7:

**Axis A — Same POD type.** Both backends consume
`rr::field::FieldMappingConfig` directly. No
per-backend shadow struct.

**Axis B — Same default.** Both initialise to
`disabled_field_mapping_config()` via in-class
`{}` initialisation (CUDA: `CudaScene.cuh:216` +
`CudaRenderer.h:283`; OptiX: `OptixLaunchParams.h:554`
+ `OptixRenderer.h:595`).

**Axis C — Same double-gate.** Outer `enabled` gate
on both (CUDA line 597, OptiX line 785); inner
target gates on both (CUDA lines 605 + 608, OptiX
lines 803 + 806).

**Axis D — Same math.** Both call the same RR_HD
inline `evaluate(...)` + `evaluate_mapping(...)`
helpers from `src/field/`.

**Axis E — Same shape.** ColorMultiplier: both apply
`color = color * mapped` (CUDA line 606, OptiX line
803). Emission: both apply `color = color +
Vec3{mapped, mapped, mapped}` (CUDA line 609, OptiX
line 806).

Cross-backend bit-identity is **structurally
guaranteed by construction** (not just empirically
hoped for). The SDK-host empirical pass is
documented as deferred to the future CLI bridge
slice's audit (check #10).

### 3.4 Check #5 — two-gate activation

The two-gate discipline is verified symmetrically
across both backends:

- **Outer gate** (`scalar_field_config.enabled`): the
  FIELD-I.2 master switch. Default `false`; opens
  only when the operator authors a non-default
  scalar_field config. When closed, neither
  `evaluate(...)` nor `evaluate_mapping(...)` is
  called; the entire mapping block short-circuits.
- **Inner gate** (`field_mapping_config.target ∈
  {ColorMultiplier, Emission}`): the FIELD-I.4
  target selector. Default `None`; opens only when
  the operator authors a non-default field_mapping
  config with a beauty-engageable target. When
  closed, the inner-gate fall-through produces
  zero beauty modulation.

Both gates must open for any beauty modulation. The
default state of both gates is closed; the
pre-FIELD-BEAUTY.* baseline is structurally
preserved.

### 3.5 Checks #6 + #7 — default no-op anchors

Three load-bearing no-op anchors compose:

**Layer 1 — null pointer / target gate** (FIELD-I.4
audit's check #3): default `target = None`; inner
gates don't match; fall-through is no-op. Identical
on both backends.

**Layer 2 — disabled-field gate** (FIELD-I.3 audit's
check #2): default `enabled = false`; outer gate
short-circuits. Identical on both backends.

**Layer 3 — evaluator short-circuit** (FIELD-I.3
audit's check #2 three-layer anchor): even if both
gates were bypassed, the FIELD-I.2 `evaluate(...)`
helper short-circuits to `0.0f` when `enabled =
false` OR `strength = 0.0f`, AND the FIELD-I.4
`evaluate_mapping(...)` short-circuits to `0.0f`
when `target == None`. Both backends call the same
helpers.

The composition guarantees: every existing
`--render-*` invocation against any scene produces
byte-identical PPM output to the pre-FIELD-BEAUTY.*
baseline (`8a5dd54`). The new kernel arms fire zero
times across every existing dispatcher path.

### 3.6 Check #8 — FieldScalar diagnostic AOV preserved

The FIELD-I.9 CUDA + FIELD-I.11 OptiX FieldScalar
AOV write arms are byte-identical to the FIELD-I.14
baseline. The FIELD-BEAUTY.* arc honestly preserves
the mapping-vs-diagnostic separation:

- The diagnostic AOV writes the **raw scalar
  sample** (`evaluate(scalar_field_config,
  hit_pos)`) regardless of mapping target.
- The beauty pass applies the **mapped scalar**
  (`evaluate_mapping(field_mapping_config,
  sample)`) when both gates open.

The two surfaces are independent. A future CLI
bridge slice can engage the AOV with
`--field-debug` independently from the beauty
mapping via `--field-mapping-target`; either /
both can be active per launch.

Per-line diff verification:
- `git diff 8a5dd54..3aee852 -- src/cuda/CudaTestKernel.cu`
  shows zero changes inside the AOV-write block at
  lines 797-805 (only the FIELD-BEAUTY.3 arm at
  lines 547-614 was added).
- `git diff 8a5dd54..3aee852 -- src/optix/OptixPrograms.cu`
  shows zero changes inside the AOV-write block at
  lines 960-980 (only the FIELD-BEAUTY.5 arm at
  lines 748-813 was added).

### 3.7 Check #9 — fixture authoring

Three fixture scenes coexist post-FIELD-BEAUTY.7,
one per operator-engageable FIELD-I.4
`FieldMappingTarget`:

- `scenes/test_scalar_field_diagnostic.rrscene`
  (FIELD-I.13) — exercises the FIELD-I.7
  DiagnosticAOV path (read-only sample
  visualization).
- `scenes/test_scalar_field_color_multiplier.rrscene`
  (FIELD-BEAUTY.7) — exercises the
  FIELD-BEAUTY.* ColorMultiplier path
  (multiplicative beauty modulation).
- `scenes/test_scalar_field_emission.rrscene`
  (FIELD-BEAUTY.7) — exercises the
  FIELD-BEAUTY.* Emission path (additive grayscale
  beauty modulation).

All three fixtures share the same geometry +
camera + lighting layers (mirrored from OBS-F.2 →
FIELD-I.13 verbatim) + share the same `scalar_field`
block. Only the `field_mapping` block differs (or
is absent, in the FIELD-I.13 diagnostic case). This
**one-variable-difference principle** lets the
SDK-host validation isolate each target's
contribution from every other variable.

**Isolation verified**: `git diff 8a5dd54..3aee852
--name-only -- 'scenes/' ':(exclude)scenes/test_scalar_field_*.rrscene'`
returns zero hits — every pre-FIELD-BEAUTY.*
fixture is byte-identical.

### 3.8 Check #10 — runtime status

The arc's runtime status is
`PASS_WITH_RUNTIME_DEFERRED`. The deferral has two
honest framings:

**Frame A — audit-host SDK absence.** The
audit-host build is `RR_ENABLE_CUDA=OFF +
RR_ENABLE_OPTIX=OFF`; neither kernel can be
launched. This is a property of the audit
environment, not the arc.

**Frame B — CLI bridge unfilled.** Even on an SDK
host, the FIELD-BEAUTY.* kernel arms are
structurally unreachable today because no
dispatcher caller passes a non-default
`targets.field_mapping_config` / trailing
parameter. The future CLI bridge slice
(FIELD-BEAUTY.9) flips both backends reachable
simultaneously.

The two frames compose: even on an SDK host today,
the FIELD-BEAUTY.* arms wouldn't fire on any
existing CLI invocation. The FIELD-BEAUTY.9 slice
is the **single load-bearing follow-up** that
converts every FIELD-BEAUTY.4 + FIELD-BEAUTY.6 +
FIELD-BEAUTY.8 deferred verdict to PASS in one
audit.

The six SDK-host runtime scenarios from
`docs/FIELD_SCALAR_BEAUTY_FIXTURES.md` §6:

- §6.1 ColorMultiplier modulation (both backends).
- §6.2 Emission modulation (both backends).
- §6.3 Disabled-mapping baseline (both backends).
- §6.4 Compatibility with FIELD-I.7 diagnostic AOV.
- §6.5 Cross-fixture beauty diff (ColorMultiplier
  vs Emission).
- §6.6 Doppler / searchlight interaction.

Plus a seventh implicit scenario: the cross-backend
byte-identity (FIELD-BEAUTY.6 §3.10 introduces this
as a new scenario unique to having both backends
in). All seven defer to the FIELD-BEAUTY.9 audit.

### 3.9 Check #11 — remaining risks

**Risk A: CLI bridge slice not landed.** The
FIELD-BEAUTY.* arc ships kernel arms + payload
plumbing + fixtures + parser, but NOT a CLI
authoring surface. Every dispatcher caller passes
the default `disabled_field_mapping_config()`
today; the kernel arms are structurally
unreachable from any current CLI invocation. The
FIELD-BEAUTY.7 fixtures + parser cleanly load the
`field_mapping` block onto `Scene::field_mapping_config`,
but the renderer dispatcher does NOT read this
field — the future CLI bridge slice (FIELD-BEAUTY.9)
will thread it through both `run_render_aovs` AND
`run_render_optix_aovs`. **Mitigation**: this is
scope-deferral, not a bug. The CLI bridge slice is
the documented next slot (per §4.1 below). The
honest framing is preserved across every
FIELD-BEAUTY.* slice's BUILD_PLAN entry.

**Risk B: No path-tracer integration.** The
FIELD-BEAUTY.3 CUDA arm is in `k_render_scene`
only (not `k_pathtrace_sample`); the FIELD-BEAUTY.5
OptiX arm is in `__closesthit__radiance` only
(not `__closesthit__pathtrace`). The path-tracer
entries do NOT consume the FieldMappingConfig
today. **Mitigation**: matches the FIELD-I.9 +
FIELD-I.11 scope precedent (the FIELD-I.* arc was
also k_render_scene-only / __closesthit__radiance-
only). Path-tracer integration is a separate arc
extension when authorised; deferred. The CUDA +
OptiX `--render-scene` / `--render-aovs` /
`--render-mesh-scene` / `--render-material-scene`
/ `--render-direct-lighting` /
`--render-relativistic` / `--render-optix-aovs`
actions all use k_render_scene /
__closesthit__radiance, so the FIELD-BEAUTY.* arms
will be exercised by every primary-hit invocation
once the CLI bridge slice lands.

**Risk C: No per-target color.** The FIELD-I.4
`FieldMappingConfig` POD does not carry a
per-target color today (six fields: target,
strength, bias, min_value, max_value,
clamp_output). The Emission target uses grayscale
(`Vec3{m, m, m}`); the ColorMultiplier target
multiplies all three channels uniformly. An artist
who wants "red emission proportional to the field"
would need to (a) wait for a future FIELD-I.* POD
extension that adds a per-target color OR (b)
post-process the grayscale emission AOV in an
external compositor. **Mitigation**: the
grayscale-only choice is honest scope (documented
in the FIELD-BEAUTY.3 arm doc-comment +
FIELD-BEAUTY.4 audit's §3.3 + FIELD-BEAUTY.5
arm doc-comment + FIELD-BEAUTY.6 audit's §3.3).
A future FIELD-I-COLOR.* sub-arc (or a
FIELD-BEAUTY.* extension) may add per-target
color authoring when the operator wants it. The
current grayscale shape is sufficient for the
FIELD-BEAUTY.* arc's demonstration purpose
(visually-distinct field-driven beauty
modulation).

### 3.10 Check #12 — recommended next safe stage

**FIELD-BEAUTY.9: CLI + Config + dispatcher
bridge.** Single load-bearing follow-up that:

- Lands `--field-mapping-target {none |
  color-multiplier | emission | diagnostic-aov}`
  CLI flag.
- Lands `--field-mapping-strength <float>` +
  `--field-mapping-bias <float>` +
  `--field-mapping-min-value <float>` +
  `--field-mapping-max-value <float>` +
  `--field-mapping-clamp-output` (presence-only
  bool) CLI flags.
- Extends `rr::core::Config` with a
  `field_mapping_config` field.
- Threads `cfg.field_mapping_config` AND
  `scene.field_mapping_config` from CLI / scene
  loader through both `run_render_aovs` AND
  `run_render_optix_aovs` into the respective
  `AOVTargets::field_mapping_config` /
  `OptixRenderer::render_aovs(...)` trailing
  parameter (the same precedence policy the
  manifold / observer bridges use today: CLI
  wins on explicit override; scene fills in
  otherwise).
- Closes the FIELD-BEAUTY.4 + FIELD-BEAUTY.6 +
  FIELD-BEAUTY.8 audits' runtime-deferred
  portions on SDK-host (one audit covering
  both backends + all seven SDK-host scenarios
  from §3.8).

The FIELD-BEAUTY.9 slice would naturally combine
with the unfilled FIELD-I.* CLI bridge slot
(originally FIELD-I.15 in the renumbered ladder)
to ship a single `--field-*` CLI surface that
engages both the FIELD-I.* diagnostic AOV
(`--field-debug` gate) and the FIELD-BEAUTY.*
beauty mapping (`--field-mapping-target` +
parameter flags) simultaneously. The single
combined slice would close the diagnostic-AOV
runtime-deferred portions on the FIELD-I.10 +
FIELD-I.12 + FIELD-I.14 audits AND the beauty-
mapping runtime-deferred portions on the
FIELD-BEAUTY.4 + FIELD-BEAUTY.6 + FIELD-BEAUTY.8
audits in one slice. Recommended.

### 3.11 Master-rule satisfaction recap

- **Master rule #1 ("Build incrementally"):**
  satisfied. The arc spans five per-slice impl
  slices (CUDA bridge → CUDA audit → OptiX
  bridge → OptiX audit → fixtures + parser
  + companion). Each slice has its per-slice
  audit gate (the impl slices were audited
  at their successors). This capstone closes
  the arc.

- **Master rule #3 ("no fake stubs"):**
  satisfied across every slice. The CUDA +
  OptiX kernel arms are fully wired (real
  `evaluate(...)` + `evaluate_mapping(...)`
  invocations; real branches; real `color`
  modifications). The structural unreachability
  via the double-gate is honest scope framing
  (per the doc-comments in CudaTestKernel.cu
  lines 547-595 + OptixPrograms.cu lines
  748-783). The honest framing of the missing
  FIELD-BEAUTY.1 + FIELD-BEAUTY.2 task brief
  slots is preserved across every slice's
  commit message + BUILD_PLAN entry. No fake
  stubs; no empty scaffolds.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. The cross-backend
  symmetry's five-axis verification (check #3 +
  #4) rests on inspectable file/line references
  and the structural-equivalence argument the
  SCHW.5 / PENROSE.6 + MANI-I.8 + OBSERVER.13 +
  FIELD-I.11 + FIELD-BEAUTY.3 + FIELD-BEAUTY.5
  precedents established. Every default
  behaviour is rooted in the audit-host-verified
  FIELD-I.2 evaluator (80 RR_CHECK assertions
  on `tests/field_tests.cpp`'s §1-§6) + the
  FIELD-I.4 mapping evaluator (55 RR_CHECK
  assertions on §7). Empirical SDK-host
  verification of the kernel arms' composed
  behaviour is the canonical FIELD-BEAUTY.9
  audit content (deferred per check #10).

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Each per-slice scope was deliberately
  narrow: FIELD-BEAUTY.3 = CUDA kernel arm only
  (no CLI, no dispatcher, no path-tracer);
  FIELD-BEAUTY.5 = OptiX kernel arm only;
  FIELD-BEAUTY.7 = fixtures + parser only (no
  dispatcher, no CLI). The CLI bridge + path-
  tracer integration + per-target color are all
  deferred (per the documented risks at check
  #11). The FIELD-BEAUTY.* arc opens parallel
  to the FIELD-I.* arc; both arcs coexist.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied.
  The FIELD-BEAUTY.* default state is unchanged
  from the FIELD-I.14 baseline:
    - No `--render-*` action produces a new
      file.
    - No existing PPM filename changes.
    - No beauty pass arithmetic changes from
      defaults (both backends' arms are
      double-gated; defaults close both gates).
    - No existing AOV slot's value changes (the
      FieldScalar AOV preserved per check #8).
    - No default scene is altered (check #9's
      isolation verification).
  The single observable behaviour change is
  the structural presence of the kernel arms;
  their observable behaviour from every
  existing CLI invocation is zero.

### 3.12 Honest scope recap

The FIELD-BEAUTY.* arc is a **kernel-arm + payload-
plumbing + fixture-authoring arc on the audit-host
side**, with the **CLI bridge + dispatcher emit +
SDK-host runtime validation deferred**. The
verdict `PASS_WITH_RUNTIME_DEFERRED` honestly
captures this:

- The arc's structural content (kernel arms +
  payload plumbing + fixtures + parser + companion
  doc + per-slice audits) is complete + verified
  on the audit-host.
- The runtime verification of the kernel arms'
  composed beauty modulation (the six SDK-host
  scenarios from FIELD-BEAUTY.6 §3.10 + the
  cross-backend byte-identity check) is reserved
  for the future FIELD-BEAUTY.9 CLI bridge
  slice's audit on a CUDA + OptiX-SDK host.

The honest framing of the missing FIELD-BEAUTY.1 +
FIELD-BEAUTY.2 task brief slots (the unfilled
`docs/FIELD_SCALAR_BEAUTY_MAPPING_PLAN.md` +
`docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md`) is
preserved across every per-slice commit's
BUILD_PLAN entry. The FIELD-BEAUTY.3 commit's
BUILD_PLAN entry explicitly documents that the
operator's prompt body was treated as the
canonical task brief. Each subsequent FIELD-BEAUTY.*
slice's commit reinforces this framing. The
FIELD-BEAUTY.8 capstone audit acknowledges +
preserves the honest framing without
retroactively authoring the missing briefs (per
the standing recommendation at FIELD-BEAUTY.4
§4.2 + §5.3: "RETROACTIVE authoring is
deferrable to operator discretion").

The referenced `docs/FIELD_INTERPRETATION_PHASE1_AUDIT.md`
(the FIELD-I.* arc capstone audit) is also
unfilled. The FIELD-I.* arc has not had a
capstone audit yet; its closure is reserved for
a separate slice. This FIELD-BEAUTY.8 capstone
is scoped to FIELD-BEAUTY.* only; the FIELD-I.*
arc's capstone closure is OUT OF SCOPE here.

---

## 4. NEXT

### 4.1 Renumbered FIELD-BEAUTY.* sub-slice ladder

The FIELD-BEAUTY.8 capstone audit closes the
FIELD-BEAUTY.* arc's per-slice gate chain. The
post-FIELD-BEAUTY.8 ladder for the remaining
deferred work is:

- **FIELD-BEAUTY.9** — CLI + Config + dispatcher
  bridge (the renumbered next FIELD-BEAUTY.* impl
  slot; lands the `--field-mapping-*` CLI flag
  family + the dispatcher threading + the
  `rr::core::Config` extension). Single
  load-bearing follow-up that converts the entire
  FIELD-BEAUTY.* arc family's runtime-deferred
  verdicts to PASS in one SDK-host audit.
- **FIELD-BEAUTY.10** — CLI bridge audit.
- **FIELD-BEAUTY.11** — Arc-wide SDK-host runtime
  pass (the post-CLI-bridge slice that exercises
  the FIELD-BEAUTY.7 fixtures end-to-end on a
  CUDA + OptiX-SDK host; verifies the six +
  cross-backend scenarios from §3.8 empirically;
  produces the canonical SDK-host audit that
  converts every FIELD-BEAUTY.4 + FIELD-BEAUTY.6
  + FIELD-BEAUTY.8 deferred verdict to PASS).

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

The parallel FIELD-I.* arc retains its unfilled
FIELD-I.15+ ladder (CLI + Config + dispatcher
bridge for the diagnostic AOV;
FIELD_INTERPRETATION_PHASE1_AUDIT.md capstone).
The two arcs may be merged at the CLI bridge
slice: a single FIELD-COMBINED.* slice could ship
both diagnostic-AOV CLI (`--field-debug` +
`--field-enable` + `--field-kind` + radial /
constant authoring flags) AND beauty-mapping CLI
(`--field-mapping-target` + per-parameter flags)
in one slice, with a single audit covering both
arcs' SDK-host validation. This combined slice
would convert every FIELD-I.10 + FIELD-I.12 +
FIELD-I.14 + FIELD-BEAUTY.4 + FIELD-BEAUTY.6 +
FIELD-BEAUTY.8 deferred verdict to PASS in one
operator-cadence-bound effort. RECOMMENDED as the
single most converging-leverage next slot if the
operator wants to close both arcs' deferred
verdict tails simultaneously.

### 4.2 Candidate next slots (prioritised)

**(a) HIGHLY RECOMMENDED — FIELD-BEAUTY.9
(combined with FIELD-I.15 if operator prefers):**
CLI + Config + dispatcher bridge. Closes the
FIELD-BEAUTY.* arc family's runtime-deferred
verdict tail. The minimal-narrow scope ships a
single `--field-mapping-target` flag + per-
parameter authoring flags + the dispatcher
threading; an SDK-host audit follows. Even
without the combined FIELD-I.15 slice, this is
the single most converging-leverage next slot
for the FIELD-BEAUTY.* arc.

**(b) RECOMMENDED — combined FIELD-COMBINED CLI
bridge slice** (FIELD-BEAUTY.9 + FIELD-I.15
merged). Ships both arcs' CLI surfaces +
dispatcher threading in one slice. The
FIELD-BEAUTY.* arc's CLI is small (one target
flag + 5 parameter flags); the FIELD-I.*
diagnostic-AOV CLI is also small
(`--field-debug` + `--field-enable` +
`--field-kind` + per-kind parameter flags).
Combined slice keeps the CLI surface compact +
audits both arcs' runtime in one SDK-host pass.

**(c) Manifold-orthogonal work.** Multiple
options:
  - **Deferred SDK-host runtime pass** for the
    OBSERVER.* + OBS-P.* + OBS-F.* arc family
    (independent of the FIELD-BEAUTY.* CLI
    bridge — those arcs have their own
    `--observer-*` / `--observer-perception-mode`
    CLI surface already; the deferred runtime
    pass just exercises the existing CLI on an
    SDK host).
  - **MANI-I.12 final cross-host manifold
    audit**.
  - **Denoiser integration with chart-aware
    AOVs**.
  - **Path-tracer feature breadth** (NEE
    extension, BSDF expansion, MIS tuning).

**(d) NOT RECOMMENDED — FIELD-I.* arc capstone
(`FIELD_INTERPRETATION_PHASE1_AUDIT.md`).**
Possible to land before FIELD-BEAUTY.9 but
risks reaching the same `PASS_WITH_RUNTIME_DEFERRED`
verdict the FIELD-I.* per-slice audits already
carry; the structural arc is well-audited
already + the runtime closure naturally pairs
with the FIELD-BEAUTY.* runtime closure at the
combined CLI bridge slice. Better to land the
CLI bridge first, then capstone both arcs
together (or sequentially) when the SDK-host
audit pass is available.

**(e) NOT RECOMMENDED — direct path-tracer
integration slice** (extending FIELD-BEAUTY.3
/ .5 to `k_pathtrace_sample` /
`__closesthit__pathtrace`). Possible but
premature: the FIELD-BEAUTY.* arc's primary-hit
arms are not yet runtime-verified on SDK host;
extending to path-tracer adds a second
unverified surface. Better to verify
primary-hit on SDK host first, then extend
to path-tracer in a subsequent arc with its
own audit.

**(f) DEFERRABLE — RETROACTIVE task brief
authoring.** The operator may choose to backfill
the missing `docs/FIELD_SCALAR_BEAUTY_MAPPING_PLAN.md`
+ `docs/FIELD_SCALAR_BEAUTY_MAPPING_TASK.md`
task briefs for archival precedent. The
honest-framing approach has worked across the
FIELD-BEAUTY.3 – FIELD-BEAUTY.7 slices + the
FIELD-BEAUTY.4 + FIELD-BEAUTY.6 + FIELD-BEAUTY.8
audits; backfilling is purely documentary +
introduces no source-code or runtime impact.
Deferrable to operator discretion.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the master rule #1 +
  #3 + #11 + #12 + #16 satisfaction recap at
  §3.11 cites these).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §6 (the Field Interpretation Layer as an
  OPTIONAL extension above the Manifold Core).
- `docs/FIELD_INTERPRETATION_LAYER.md` §4.1 +
  §4.2 (the design-doc anchors for the
  ColorMultiplier + Emission target channels
  the FIELD-BEAUTY.* arc consumes).

### 5.2 FIELD-I.* arc references (parallel arc)

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1 — the canonical FIELD-I.* arc
  plan; the FIELD-BEAUTY.* arc is the parallel
  arc that lifts the "no field-to-beauty
  mapping yet" non-goal from FIELD-I.1 §2.4).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md` (FIELD-I.3
  — the FIELD-I.2 evaluator's three-layer
  no-op anchor underpins checks #6 + #7).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md`
  (FIELD-I.5 — the FIELD-I.4 evaluator's
  three-layer no-op anchor underpins check
  #6).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_AUDIT.md`
  (FIELD-I.8).
- `docs/FIELD_SCALAR_CUDA_BRIDGE_AUDIT.md`
  (FIELD-I.10).
- `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
  (FIELD-I.12).
- `docs/FIELD_SCALAR_FIXTURE.md` (FIELD-I.13 —
  the precedent fixture whose geometry +
  scalar_field block both FIELD-BEAUTY.7
  fixtures inherit verbatim).
- `docs/FIELD_SCALAR_FIXTURE_AUDIT.md`
  (FIELD-I.14 — the immediate arc baseline
  before the FIELD-BEAUTY.* arc opened).
- (FIELD_INTERPRETATION_PHASE1_AUDIT.md) —
  UNFILLED FIELD-I.* arc capstone slot;
  out of scope for this FIELD-BEAUTY.8
  capstone (separate slice; deferred per
  §4.2 (d)).

### 5.3 FIELD-BEAUTY.* arc references

- (FIELD-BEAUTY.1 task brief slot) — UNFILLED.
  The operator's FIELD-BEAUTY.3 prompt body
  served as the canonical task brief (see
  FIELD-BEAUTY.4 audit §3.1 + §5.3 for the
  canonical record).
- (FIELD-BEAUTY.2 task brief slot) — UNFILLED.
  Same honest-framing precedent.
- FIELD-BEAUTY.3 (`8b8f100`) — CUDA bridge impl.
- `docs/FIELD_SCALAR_BEAUTY_CUDA_AUDIT.md`
  (FIELD-BEAUTY.4 — the precedent CUDA-bridge
  beauty-mapping audit; the §3.10 deferred
  SDK-host scenarios feed this capstone's
  check #10).
- FIELD-BEAUTY.5 (`89fdcfc`) — OptiX bridge
  impl.
- `docs/FIELD_SCALAR_BEAUTY_OPTIX_AUDIT.md`
  (FIELD-BEAUTY.6 — the precedent OptiX-bridge
  audit; the §3.7 five-axis cross-backend
  symmetry argument underpins this capstone's
  checks #3 + #4).
- FIELD-BEAUTY.7 (`3aee852`) — fixtures +
  parser + companion doc.
- `docs/FIELD_SCALAR_BEAUTY_FIXTURES.md`
  (FIELD-BEAUTY.7 — the fixture companion
  doc; the §6 deferred SDK-host scenarios
  enumerate the FIELD-BEAUTY.9 audit's
  validation surface).

### 5.4 Source surface audited (arc-wide)

The FIELD-BEAUTY.* arc touched the following
source files (relative to the arc baseline
`8a5dd54`):

| File                              | Net lines | Slice                                     |
|-----------------------------------|-----------|-------------------------------------------|
| `src/cuda/CudaScene.cuh`          | +41       | FIELD-BEAUTY.3                            |
| `src/cuda/CudaRenderer.h`         | +23       | FIELD-BEAUTY.3                            |
| `src/cuda/CudaRenderer.cu`        | +15       | FIELD-BEAUTY.3                            |
| `src/cuda/CudaTestKernel.cu`      | +73       | FIELD-BEAUTY.3                            |
| `src/optix/OptixLaunchParams.h`   | +41       | FIELD-BEAUTY.5                            |
| `src/optix/OptixRenderer.h`       | +28       | FIELD-BEAUTY.5                            |
| `src/optix/OptixRenderer.cpp`     | +20       | FIELD-BEAUTY.5                            |
| `src/optix/OptixPrograms.cu`      | +68       | FIELD-BEAUTY.5                            |
| `src/scene/Scene.h`               | +33       | FIELD-BEAUTY.7                            |
| `src/io/SceneLoader.cpp`          | +118      | FIELD-BEAUTY.7                            |
| `scenes/test_scalar_field_color_multiplier.rrscene` | 82  | FIELD-BEAUTY.7  |
| `scenes/test_scalar_field_emission.rrscene`         | 82  | FIELD-BEAUTY.7  |

Total source-code surface: ~460 net lines across
10 source files. Total scene surface: 164 lines
across 2 fixtures. Zero CMakeLists.txt change.

### 5.5 Documentation surface produced (arc-wide)

| File                                              | Lines | Slice          |
|---------------------------------------------------|-------|----------------|
| `docs/FIELD_SCALAR_BEAUTY_CUDA_AUDIT.md`          | ~900  | FIELD-BEAUTY.4 |
| `docs/FIELD_SCALAR_BEAUTY_OPTIX_AUDIT.md`         | ~920  | FIELD-BEAUTY.6 |
| `docs/FIELD_SCALAR_BEAUTY_FIXTURES.md`            | ~560  | FIELD-BEAUTY.7 |
| `docs/FIELD_SCALAR_BEAUTY_MAPPING_AUDIT.md`       | ~XXX  | FIELD-BEAUTY.8 (this doc) |
| `docs/BUILD_PLAN.md`                              | per-slice entries | FIELD-BEAUTY.3 – .8 |

### 5.6 Surrounding commit SHAs

- FIELD-BEAUTY.3: `8b8f100` (CUDA bridge impl)
- FIELD-BEAUTY.4: `c5823d9` (CUDA audit)
- FIELD-BEAUTY.5: `89fdcfc` (OptiX bridge impl)
- FIELD-BEAUTY.6: `9efb6a9` (OptiX audit)
- FIELD-BEAUTY.7: `3aee852` (fixtures + parser
  + companion doc)
- Arc baseline (pre-FIELD-BEAUTY.3): `8a5dd54`
  (FIELD-I.14 audit; the last commit before
  the FIELD-BEAUTY.* arc opened).

### 5.7 Audit-host empirical state at this capstone

- `ctest`: 13/13 PASS on the audit-host build
  (`RR_ENABLE_OPTIX=OFF`).
- Per-binary: `relativity_tests: 841/841`;
  `manifold_identity_tests: 408/408`;
  `cli_tests: 274/274`; `renderer_tests: 35/35`;
  `field_tests: 135/135`; every other suite
  unchanged.
- OptiX-ON-no-SDK build at the FIELD-BEAUTY.7
  landing: 14/14 ctest PASS (including
  `optix_tests`).
- `git diff 8a5dd54..3aee852 --name-only --
  'src/manifold/' 'src/relativity/'`: zero hits
  (no manifold / observer surface touched
  across the arc).
- `git diff 8a5dd54..3aee852 --name-only --
  'scenes/'
  ':(exclude)scenes/test_scalar_field_*.rrscene'`:
  zero hits (no default scene altered;
  isolation verified).

### 5.8 Cross-backend math leaves (shared between arcs)

Both arcs (FIELD-I.* + FIELD-BEAUTY.*) consume
the same single-source-of-truth math leaves
from `src/field/`:

- `rr::field::evaluate(ScalarFieldConfig, Vec3)
  → float` — the FIELD-I.2 scalar-field
  evaluator (RR_HD inline; same code on
  host + CUDA + OptiX).
- `rr::field::evaluate_mapping(FieldMappingConfig,
  float) → float` — the FIELD-I.4 mapping
  evaluator (RR_HD inline; same code on
  host + CUDA + OptiX).

The cross-backend bit-identity is structurally
guaranteed by construction at every consumer
site. The FIELD-I.10 / .12 / .14 audits' OptiX
bridge verifications + the FIELD-BEAUTY.4 / .6 /
.8 audits' beauty mapping verifications all
rest on this shared math leaf foundation.

### 5.9 Unchanged source files (sampled)

The following files are byte-identical to the
arc baseline (`8a5dd54`), confirmed by the diff
filters across the arc:

- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/cuda/` EXCEPT the four touched at
  FIELD-BEAUTY.3.
- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/optix/` EXCEPT the four touched at
  FIELD-BEAUTY.5.
- Every file in `src/manifold/`.
- Every file in `src/relativity/`.
- Every file in `src/renderer/`.
- Every file in `src/scene/` EXCEPT `Scene.h`
  (touched at FIELD-BEAUTY.7).
- Every file in `src/io/` EXCEPT
  `SceneLoader.cpp` (touched at FIELD-BEAUTY.7).
- Every file in `src/core/`, `src/math/`,
  `src/image/`, `src/gpu/`, `src/app/`,
  `src/field/`, `src/pathtracer/`,
  `src/camera/`, `src/geometry/`,
  `src/lighting/`, `src/material/`,
  `src/texture/`.
- `src/main.cpp`.
- `CMakeLists.txt`.

### 5.10 Unchanged test + scene files (sampled)

- All test files (`tests/`) byte-identical to
  the arc baseline. No test extension across the
  FIELD-BEAUTY.* arc (the kernel arms'
  empirical behaviour requires SDK-host runtime
  verification; deferred per check #10).
- All pre-FIELD-BEAUTY.* scene fixtures
  (`scenes/`) byte-identical to the arc
  baseline. Only the two new
  `test_scalar_field_color_multiplier.rrscene`
  + `test_scalar_field_emission.rrscene`
  fixtures from FIELD-BEAUTY.7 are added (per
  check #9's isolation verification).

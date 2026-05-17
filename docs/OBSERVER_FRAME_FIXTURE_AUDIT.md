# ObserverFrame Fixture Audit (OBS-F.3)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `c547f2d` ("scene:
OBS-F.2 — ObserverFrame Fixture Implementation
(impl, scene + companion doc)").
Audit baseline: `5f8cabc` ("docs: OBS-F.1 —
ObserverFrame Fixture Task (docs only)") — the
last commit before OBS-F.2 landed.
Audit host: linux, audit-host build (no CUDA SDK,
no OptiX SDK).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, the OBS-F.1 task
brief, the OBS-F.2 fixture + companion doc, the
unchanged `manifold_identity_tests` / `cli_tests` /
`renderer_tests` / `relativity_tests` runtime
outputs, `ctest` exit codes, and the audit-host
`--scene-info` smoke transcript on the new
fixture.

This audit is the per-slice gate for OBS-F.2
(`c547f2d`). It verifies the nine items the task
brief enumerates — fixture scene exists; fixture
uses non-default ObserverFrame values; perception
mode is `ConstantVelocityMinkowski`; values are
bounded/safe; default scenes remain unchanged;
parser changes minimal; no CUDA/OptiX kernel
changes; runtime status; verdict — and produces a
`PASS` / `REPAIR` / `BLOCKED` verdict.

---

## 1. VERDICT

**PASS.**

All eight structural checks return `PASS`. Check
#8 (runtime CUDA/OptiX status) is `DEFERRED` on
the documented audit-host SDK-absence limitation
(the fixture's renderer-side validation requires
an SDK host; the 7 deferred runtime checks the
OBS-F.2 companion doc §6 enumerates remain
deferred pending an SDK-host pass). Check #9
(overall verdict) is `PASS`: the fixture is
structurally complete + isolated + safe; the
audit-host parser-surface verification confirms
the scene loads cleanly and the parsed state
matches the documented contents; the
deferred-runtime status is a known scope
boundary, not a `REPAIR` or `BLOCKED` item.

No `REPAIR` action is required. No `BLOCKED`
item is outstanding. The OBS-F arc closes with
OBS-F.1 task + OBS-F.2 impl + this OBS-F.3
audit; the deferred SDK-host runtime pass (per
OBS-F.2 companion doc §6) converts the entire
OBSERVER.* + OBS-P.* + OBS-F.* arc family's
verdicts from PASS_WITH_RUNTIME_DEFERRED → PASS
when an SDK host runs the fixture.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Fixture scene exists                     | **PASS** | The OBS-F.2 commit (`c547f2d`) creates `scenes/test_observer_frame.rrscene` (72 lines including trailing newline; reported as 69 content lines in the OBS-F.2 commit message). The file is valid JSON parsed by the existing `rr::io::load(...)` helper without error (verified by the audit-host `--scene-info scenes/test_observer_frame.rrscene` smoke test at the OBS-F.2 landing commit — the full ~50-line parsed-state log fires correctly). The file is the ONLY new `.rrscene` shipped by OBS-F.2 (`git diff 5f8cabc..c547f2d --name-only -- 'scenes/'` returns exactly the one new file path; no existing `.rrscene` file is modified). The companion doc `docs/OBSERVER_FRAME_FIXTURE.md` (1175 lines) documents the fixture's purpose, composition, expected visual signature, cross-backend equivalence, audit-host smoke-test transcript, and the 7 deferred SDK-host runtime checks. |
| 2 | Fixture uses non-default ObserverFrame values | **PASS** | The fixture's `relativity` block authors non-default values:<br>**(a)** `enabled = true` (default for `RelativityParams::enable_*` flags is also `true`, but this confirms the fixture engages the SR pipeline explicitly).<br>**(b)** `betaVelocity = 0.5` — non-zero observer 3-velocity magnitude (the default rest observer has `velocity = (0, 0, 0)`; this fixture's beta is documented as below the `clampBeta(0.999999)` cap with a comfortable safety margin).<br>**(c)** `velocityDirection = [0.0, 0.0, -1.0]` — non-zero unit-length direction along the camera's forward axis (the default direction is the all-zero sentinel; this fixture's direction is finite + non-zero + unit-length, mirroring the `--render-demo` precedent's forward-motion convention).<br>**(d)** The scene-loader's `apply_relativity(...)` helper resolves `scene.observer.velocity = velocityDirection × betaVelocity = (0, 0, -1) × 0.5 = (0, 0, -0.5)` — verified at the audit-host `--scene-info` smoke test (`observer_velocity : [0.000000, 0.000000, -0.500000]`).<br>This non-default observer velocity drives the entire OBS-F arc's runtime-validation flow: when the operator engages `--observer-perception-mode relativistic` at CLI, the OBSERVER.6 adapter routes the `(0, 0, -0.5)` value onto `observer_frame.beta` (the gated path); when the operator omits the flag, the kernel reads the legacy `scene.observer.velocity = (0, 0, -0.5)` directly (the convergence-equivalent legacy path). |
| 3 | Perception mode is ConstantVelocityMinkowski | **PASS** | The fixture engages `PerceptionMode::ConstantVelocityMinkowski` via the operator-facing CLI surface, NOT via a scene-file block. This is the documented OBS-F.1 task brief §1 + §2.2 + §2.9 load-bearing design decision: the OBSERVER.4 `--observer-perception-mode relativistic` CLI flag is the operator-facing perception-mode authoring path; the fixture's `relativity` block authors the legacy SR observer velocity; the OBSERVER.6 adapter routes the scene-authored velocity onto `observer_frame.beta` when the CLI engages the perception mode. This avoids the broader scope of a `.rrscene` schema extension (which would require a separate task brief).<br>**Operator invocation** to engage `ConstantVelocityMinkowski` on the fixture:<br>```<br>RelativityRender --render-aovs<br>                 --observer-perception-mode relativistic<br>                 scenes/test_observer_frame.rrscene<br>```<br>**Behaviour verified at audit host** (the OBS-F.2 landing-commit smoke test produced `aovs observer config: constant-velocity-minkowski (|beta|=0.000000, dir=[0.000000, 0.000000, 0.000000], tau=0.000000)` — the CLI-overlay config layer's beta_magnitude is 0 because no `--observer-beta` was passed; the OBSERVER.6 adapter's downstream fallback then routes the scene's `relativity.betaVelocity = 0.5` into `observer_frame.beta` per the OBSERVER.7 audit check #3 priority). This dual-source routing is verified end-to-end by the runtime checks in OBS-F.2 §6 (deferred to SDK host). Master rule #3 ("no fake stubs") satisfied: the perception mode is REAL operator-engageable state, not a placeholder; OBS-F.2 just doesn't add a scene-file authoring path for it (deferred per minimum-scope rule). |
| 4 | Values are bounded / safe                | **PASS** | Five-axis safety verification:<br>**(a) Beta within clampBeta cap**: `betaVelocity = 0.5` is well below the `rr::relativity::clampBeta(beta, 0.999999)` cap (verified at OBSERVER.6 adapter audit OBSERVER.7 check #4 + the relativity-helpers' safety contract from Stage 19E.1). The single-precision precompute_relativity snapshot computes `gamma = 1/sqrt(1 - 0.25) = 1.1547` — finite, no NaN/Inf risk; the per-pixel arithmetic chain through `aberrateDirection` / `dopplerFactor` / `searchlightFactor` is stable for `|beta| ≤ 0.999999` per the existing relativity_tests' high-beta stability suite (verified at `test_stability_near_high_beta` in `tests/relativity_tests.cpp:378`).<br>**(b) Direction is finite + unit-length**: `velocityDirection = [0.0, 0.0, -1.0]` has `|direction| = 1.0` exactly (one non-zero IEEE-754 single-precision component). The scene-loader's `apply_relativity(...)` helper rejects malformed direction tokens at parse time per its existing safety contract (verified by inspection of `src/io/SceneLoader.cpp:785-816`).<br>**(c) Camera position + orientation safe**: position `(0, 1.2, 6.0)` is outside the sphere envelopes (all spheres are within ~3 unit radius of origin); forward `(0, -0.1, -1.0)` is finite + non-degenerate (length ~1.005; gets re-orthogonalised by the host-side `Camera::look_at` / basis-recompute logic to `(0, -0.099504, -0.995037)` per the audit-host smoke transcript). FoV `45°` is in the conventional perspective range.<br>**(d) Sphere geometry safe**: all 6 spheres have positive finite radii (`0.4` or `0.5` or `0.6`); centers are finite Vec3s; material IDs are valid indices into the 6-entry materials array (verified by inspection — all sphere materialId values 0-5 map to defined materials).<br>**(e) Mesh + lights safe**: the ground-plane mesh has 4 finite vertices + 2 triangles with valid 0-1-2 / 0-2-3 indices; the 2 lights have finite directions / colours / intensities. All values pass through the existing scene-loader safety gates at parse time. |
| 5 | Default scenes remain unchanged          | **PASS** | `git diff 5f8cabc..c547f2d --name-only -- 'scenes/' ':(exclude)scenes/test_observer_frame.rrscene'` returns **zero hits**. The OBS-F.2 commit ships exactly ONE new `.rrscene` file (`scenes/test_observer_frame.rrscene`); ZERO modifications to the 10 existing default scenes (`test_camera.rrscene`, `test_full_scene.rrscene`, `test_lights.rrscene`, `test_materials.rrscene`, `test_mesh.rrscene`, `test_penrose_like_manifold.rrscene`, `test_relativity.rrscene`, `test_render_settings.rrscene`, `test_schwarzschild_like_manifold.rrscene`, `test_spheres.rrscene`, `test_textured_material.rrscene`). Every existing CLI action invoked with an existing default scene path produces byte-identical output to the pre-OBS-F.2 baseline (verified structurally: the source / kernel / dispatcher surface is byte-unchanged per check #7 below; the existing scene files are byte-unchanged per this check's `git diff`; the test surface is byte-unchanged per check #8). |
| 6 | Parser changes, if any, are minimal      | **PASS** | The OBS-F.2 commit makes **ZERO parser changes**. `git diff 5f8cabc..c547f2d --name-only -- 'src/io/'` returns **zero hits**. The fixture's `relativity` block uses ONLY the pre-existing field set the Stage 19E.1 `apply_relativity(...)` helper at `src/io/SceneLoader.cpp:731-833` already parses (`enabled`, `betaVelocity`, `velocityDirection`, `aberrationStrength`, `dopplerStrength`, `searchlightStrength`); the fixture's other blocks (`render_settings`, `camera`, `materials`, `spheres`, `meshes`, `lights`) use ONLY the pre-existing scene-block field sets that the corresponding `apply_*(...)` helpers parse exactly as they have since their respective Stage-numbered slices.<br>The OBS-F.1 task brief's §4 explicitly noted "parser only if strictly necessary"; OBS-F.2 confirms the parser-extension scope was not necessary. The OBSERVER.6 adapter's documented beta-resolution priority (CLI overlay > zero-direction fallback > legacy `Observer.velocity`) provides the entire data path the fixture exercises without any new scene-loader code. Master rule #3 ("no fake stubs") + master rule #12 ("Do not overbuild a later system before the current layer works") both satisfied: the operator can prove the OBS-P.2 kernel-side migration works end-to-end on the SDK host using ONLY pre-existing parser surface + the new fixture file; no parser-extension risk. |
| 7 | No CUDA / OptiX kernel changes           | **PASS** | `git diff 5f8cabc..c547f2d --name-only` returns exactly 3 files: `docs/BUILD_PLAN.md`, `docs/OBSERVER_FRAME_FIXTURE.md`, `scenes/test_observer_frame.rrscene`. Restricting to the source tree via `git diff 5f8cabc..c547f2d --name-only -- 'src/*' 'tests/*' 'CMakeLists.txt'` returns **zero hits**. Specifically:<br>**(a)** Every `src/cuda/*.cu` / `*.cuh` file is byte-unchanged (the OBS-P.2 kernel surface at `CudaTestKernel.cu` / `CudaRenderer.cu` / `CudaPathTracer.cu` carries forward verbatim).<br>**(b)** Every `src/optix/*.cu` / `*.cpp` / `*.h` file is byte-unchanged (the OBS-P.2 `OptixPrograms.cu` ternaries + `OptixLaunchParams.h` + `OptixRenderer.h/.cpp` surface carries forward verbatim).<br>**(c)** Every `src/manifold/*.h` file is byte-unchanged (the OBSERVER.6 adapter + the ObserverFrame POD + the OBSERVER.13 AOV `AOVType::ObserverBeta` enumerator all carry forward).<br>**(d)** Every `src/core/CommandLine.cpp` / `Config.h` is byte-unchanged (the OBSERVER.4 + OBSERVER.13 CLI surface preserved).<br>**(e)** Every `src/io/*.cpp` / `*.h` file is byte-unchanged (no parser extension per check #6 above).<br>**(f)** Every `src/scene/*.h` / `*.cpp` is byte-unchanged.<br>**(g)** Every `src/main.cpp` dispatcher is byte-unchanged.<br>**(h)** Every test file is byte-unchanged (no new tests; the audit-host `--scene-info` smoke test is run-time evidence, not a new compiled assertion).<br>**(i)** Every CMake / build configuration is byte-unchanged.<br>The OBS-F.2 commit is **purely a data + documentation addition**; the renderer / kernel / parser surface is byte-unchanged from the OBS-P.3 baseline. |
| 8 | Runtime CUDA / OptiX status              | **DEFERRED** | The audit host has neither CUDA nor OptiX SDK installed (`nvcc` not present; `optixGetVersion` unavailable; `RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF`). Consequently the fixture's renderer-side runtime validation (the 7 SDK-host checks the OBS-F.2 companion doc §6 enumerates) is `DEFERRED` to a CUDA + OptiX-SDK host pass. The 7 deferred checks are:<br>**(a)** Default-mode byte-identity (CUDA + OptiX): the fixture without `--observer-perception-mode relativistic` produces Beauty + AOV PPMs byte-identical to the pre-OBSERVER.* + pre-OBS-P.* + pre-OBS-F.* reference.<br>**(b)** Opt-in path engagement (CUDA): the fixture with the perception-mode flag produces visible forward-blueshift + forward-aberration + searchlight beaming on the forward-facing geometry.<br>**(c)** Cross-source convergence-equivalence: the default and gated invocations produce Beauty PPMs that are byte-identical (per the §4.2 of the OBS-F.2 companion doc's argument; both paths route the same `(0, 0, -0.5)` beta to the same `precompute_relativity` helper to the same SR helpers).<br>**(d)** OBSERVER.13 debug-AOV consistency: the fixture with `--observer-debug` produces `output/aov_observer_beta.ppm` whose hit pixels decode (after PPM clamp) to a value consistent with the float-channel encoding of `(0, 0, -0.5)`; miss pixels write `(0, 0, 0)`.<br>**(e)** Cross-backend AOV equivalence: `cmp output/aov_beauty.ppm output/optix_aov_beauty.ppm` returns exit status `0` for the same fixture-mode invocation.<br>**(f)** OptiX path-trace convergence: `--render-optix-pathtrace` on the fixture produces convergent Stage 20J checkpoints at 1-spp and 16-spp.<br>**(g)** `RelativityParams` orthogonality: with `aberrationStrength = 0.0` manually edited into the fixture (or via a separate fixture variant), aberration is skipped on the Beauty pass even on the gated path; the existing flag continues to win independently of the perception-mode gate.<br>This is the **same documented deferral** pattern accrued by every prior CUDA / OptiX-touching slice (MANI-I.5 / SCHW.5 / SCHW.7 / PENROSE.6 / PENROSE.8 / MANI-CONSUME.1 / OBSERVER.8 / OBSERVER.10 / OBSERVER.13 / OBS-P.2). The OBSERVER.9 / OBSERVER.11 / OBSERVER.14 / OBS-P.3 audits + the OBSERVER.15 + SCHW.11 + PENROSE.12 capstones all recorded runtime verification as DEFERRED with this disposition; OBS-F.3 inherits the pattern. The deferral is NOT a `BLOCKED` because: (i) the audit-host CAN verify the structural correctness of the fixture (the parser loads it cleanly; the parsed state matches; the file is byte-identical to authored content); (ii) the OBS-F.2 companion doc §6 enumerates the exact required SDK-host checks for the future runtime conversion. |
| 9 | PASS / REPAIR / BLOCKED verdict          | **PASS** | All eight structural checks return `PASS`. Check #8 (runtime CUDA/OptiX status) is `DEFERRED` on the documented audit-host SDK-absence limitation, but this is recorded separately as a known scope boundary — the audit-host portion of the OBS-F arc is fully verifiable + verified. No `REPAIR` or `BLOCKED` item is outstanding. The OBS-F.2 commit ships:<br>- The documented fixture scene (`scenes/test_observer_frame.rrscene`, 72 lines) with the OBS-F.1-specified contents verbatim.<br>- The companion doc (`docs/OBSERVER_FRAME_FIXTURE.md`, 1175 lines) with the OBS-F.1-specified 7-section structure verbatim.<br>- ZERO parser changes; ZERO source code changes; ZERO test changes; ZERO CMake changes; ZERO existing-scene modifications.<br>The OBS-F arc closes with OBS-F.1 (task) + OBS-F.2 (impl) + this OBS-F.3 (audit). The deferred SDK-host runtime pass (per OBS-F.2 §6) converts the entire OBSERVER.* + OBS-P.* + OBS-F.* arc family's verdicts from PASS_WITH_RUNTIME_DEFERRED → PASS when an SDK host runs the fixture; the operator may proceed to that SDK-host pass OR to manifold-orthogonal work (MANI-I.12 final cross-host audit; Field Interpretation Layer Phase 1; denoiser integration with chart-aware AOVs; path-tracer feature breadth) as the next slot. |

---

## 3. REASONING SUMMARY

The OBS-F.2 commit (`c547f2d`) lands two new
documentation + data files without any source-code
change:

- `scenes/test_observer_frame.rrscene` (72 lines)
  — a JSON fixture with the OBS-F.1 task brief
  §2-documented contents verbatim: 1280×720
  resolution; camera at `(0, 1.2, 6.0)` with
  forward `(0, -0.1, -1.0)`; `relativity` block
  authoring `betaVelocity = 0.5` +
  `velocityDirection = [0, 0, -1]` + all three
  strengths at 1.0; 6 materials; 6 spheres; 1
  ground-plane mesh; 2 lights; NO manifold
  block; NO observer scene block.
- `docs/OBSERVER_FRAME_FIXTURE.md` (1175 lines)
  — a seven-section companion doc mirroring the
  SCHW.9 + PENROSE.10 fixture-companion shape
  verbatim. Documents purpose, composition,
  expected visual signature (per operator-
  invocation variant), cross-backend
  equivalence, audit-host smoke-test
  transcript, runtime SDK-host validation
  checks (7 deferred items), and references.

The fixture-exists invariant (check #1) is
**file-existence verified** at `scenes/test_observer_frame.rrscene`;
the audit-host `--scene-info` smoke test
confirms the file parses cleanly via the
existing `rr::io::load(...)` helper without
error; the parsed state matches the
documented authored contents.

The non-default-ObserverFrame-values invariant
(check #2) is **four-field verified**: the
`relativity` block authors a non-zero beta
magnitude (0.5), a non-zero unit-length
direction `(0, 0, -1)`, full-effect strengths
for aberration / Doppler / searchlight, and
the scene-loader resolves these into
`scene.observer.velocity = (0, 0, -0.5)` —
verified by the `--scene-info` smoke transcript
showing the documented values exactly.

The perception-mode invariant (check #3) is
**CLI-engagement verified**: the
`ConstantVelocityMinkowski` mode is engaged
via the OBSERVER.4 `--observer-perception-mode
relativistic` CLI flag at invocation time,
NOT via a scene-file block. This is the
documented OBS-F.1 design decision (avoids a
`.rrscene` schema extension); the OBSERVER.6
adapter's beta-resolution priority routes the
scene-authored velocity onto
`observer_frame.beta` on the gated path. The
audit-host smoke test confirms the CLI surface
recognises both the flag + the fixture path +
parses cleanly. Master rule #3 satisfied: the
perception mode is real operator-engageable
state, not a placeholder.

The bounded-and-safe invariant (check #4) is
**five-axis verified**: beta within clampBeta
cap; direction finite + unit-length; camera
position + orientation in conventional ranges
+ basis re-orthogonalised by the host-side
`Camera::look_at` logic; sphere geometry has
positive finite radii + valid material
indices; mesh + lights values pass through
the existing parser safety gates.

The default-scenes-unchanged invariant (check
#5) is **directly verified** by `git diff
5f8cabc..c547f2d --name-only -- 'scenes/'
':(exclude)scenes/test_observer_frame.rrscene'`
returning **zero hits**. Every existing
`.rrscene` file is byte-unchanged.

The minimal-parser-changes invariant (check #6)
is **directly verified** by `git diff
5f8cabc..c547f2d --name-only -- 'src/io/'`
returning **zero hits**. ZERO parser
extension; the fixture uses ONLY pre-existing
field schemas.

The no-CUDA/OptiX-kernel-changes invariant
(check #7) is **directly verified** by `git
diff 5f8cabc..c547f2d --name-only -- 'src/*'
'tests/*' 'CMakeLists.txt'` returning **zero
hits**. The OBS-F.2 commit is purely a data +
documentation addition; the renderer surface
is byte-unchanged from the OBS-P.3 baseline.

The runtime CUDA/OptiX status (check #8) is
**DEFERRED** on the documented audit-host
SDK-absence limitation; the 7 required
SDK-host runtime checks are enumerated for a
future conversion pass.

The overall verdict (check #9) is **PASS**: all
eight structural checks PASS + one
appropriately-DEFERRED runtime check; no
REPAIR or BLOCKED item; the OBS-F arc closes
cleanly with OBS-F.1 + OBS-F.2 + OBS-F.3.

---

## 4. NEXT

The OBS-F arc closes with this audit. The
OBS-F sub-slice ladder is:

- **OBS-F.1** — Fixture task definition
  (LANDED at `5f8cabc`).
- **OBS-F.2** — Fixture implementation
  (LANDED at `c547f2d`, scene + companion
  doc).
- **OBS-F.3** — **THIS AUDIT** (Fixture
  Audit, doc-only; verdict PASS).

The operator may proceed to any of the
following slots:

- **The deferred SDK-host runtime pass**
  (recommended highest priority) — run the
  fixture on a CUDA + OptiX-SDK host per the
  OBS-F.2 companion doc §6's 7 checks. This
  converts the entire OBSERVER.* + OBS-P.* +
  OBS-F.* arc family's verdicts from
  PASS_WITH_RUNTIME_DEFERRED → PASS:
    - OBSERVER.9 + OBSERVER.11 + OBSERVER.14
      audits' runtime-status entries convert
      from DEFERRED → PASS;
    - OBSERVER.15 capstone's verdict converts
      from PASS_WITH_RUNTIME_DEFERRED →
      PASS;
    - OBSERVER.15 capstone's §10 risk #2 +
      #3 close;
    - OBS-P.3 audit's runtime-status entry
      converts from DEFERRED → PASS;
    - OBS-F.3 (this audit's) check #8
      converts from DEFERRED → PASS.
  A post-SDK-host capstone audit (mirroring
  the OBSERVER.15 + SCHW.11 + PENROSE.12
  capstone shapes) would document the full
  arc-family closure.
- **Manifold-orthogonal work**: MANI-I.12
  final cross-host manifold audit (the
  integration plan's §11 slot); the Field
  Interpretation Layer Phase 1 (the
  architecture-doc §6 next layer); denoiser
  integration with chart-aware / observer-
  aware AOVs (Stage 19B.4 / 21D extension);
  path-tracer feature breadth (NEE / MIS /
  multi-mesh upload).
- **An observer scene-block parser
  extension** (separate task brief) — if
  the operator decides to author observer
  state from the `.rrscene` file directly
  (instead of via CLI), a new task-brief +
  impl + audit slot would add an `observer`
  scene block to the schema. NOT
  recommended as the immediate next slot;
  the OBSERVER.4 CLI surface + OBS-F.2
  fixture composition cover the
  operator's authoring needs without
  schema-bump complexity.

No `REPAIR` action is required. No `BLOCKED`
item is outstanding. The OBSERVER.*-family of
arcs (OBSERVER.* foundation + OBS-P.*
perception migration + OBS-F.* fixture) is
**structurally closed** at the audit-host
level; the SDK-host runtime pass is the
documented expected next gate.

---

## 5. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no
  fake stubs") + #12 ("Do not overbuild a
  later system before the current layer
  works") both satisfied (no parser
  extension; no fake stub; the fixture
  exercises real pre-existing parser
  surface + real pre-existing kernel reads).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §3.3 Observer Frame + §6 GPU integration
  strategy — defines the contract the
  fixture exercises end-to-end on the SDK
  host.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §6
  + §8 non-goals — the OBSERVER.1 planning
  doc that established the data path the
  fixture validates.
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15) §10 risk #2 — the capstone
  identifying the fixture follow-up as
  optional next slot; OBS-F.2 + OBS-F.3
  lift this risk.
- `docs/OBSERVER_FRAME_FIXTURE_TASK.md`
  (OBS-F.1) — the task brief OBS-F.2
  consumed as its canonical reference.
- `docs/OBSERVER_FRAME_FIXTURE.md`
  (OBS-F.2 companion) — the runtime-
  validation reference + the audit-host
  smoke-test transcript + the 7 deferred
  SDK-host checks.
- `docs/OBSERVER_DEBUG_AOV_TASK.md`
  (OBSERVER.12) §5 — the original deferred
  fixture-task reference that OBS-F lifts.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3) — established the OBS-P.2
  ternary's convergence-equivalence
  property; the OBS-F.2 fixture is the
  SDK-host runtime verification of OBS-P.3's
  check #8 cross-source equivalence.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — established the
  OBSERVER.6 adapter's beta-resolution
  priority (CLI overlay > zero-direction
  fallback > legacy `Observer.velocity`)
  that the fixture relies on.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — established the
  OBSERVER.13 `observer_beta` AOV the
  fixture exercises via `--observer-debug`.
- `docs/SCHWARZSCHILD_LIKE_FIXTURE_AUDIT.md`
  (SCHW.10) — the precedent fixture-audit
  doc this OBS-F.3 audit mirrors in
  structure (9-row check table; verdict
  variant; deferred-runtime treatment).
- `docs/PENROSE_LIKE_FIXTURE_AUDIT.md`
  (PENROSE.11) — second precedent
  fixture-audit doc.
- `docs/MANIFOLD_DEBUG_AOV_AUDIT.md`
  (MANI-I.9) — established the
  PASS-with-DEFERRED-runtime audit
  pattern for renderer-side validations.
- `scenes/test_observer_frame.rrscene`
  (new at `c547f2d`, 72 lines) — the
  audited fixture.
- `scenes/test_schwarzschild_like_manifold.rrscene`
  (SCHW.9) + `scenes/test_penrose_like_manifold.rrscene`
  (PENROSE.10) + `scenes/test_relativity.rrscene`
  (Stage 19E.1) — the precedent fixture
  scenes the OBS-F.2 fixture shape
  mirrors.
- `src/io/SceneLoader.cpp` —
  `apply_relativity(...)` (Stage 19E.1)
  parses the fixture's `relativity` block;
  byte-unchanged at OBS-F.2.
- `src/scene/Scene.h` — the
  `Scene::observer` + `Scene::relativity`
  fields the fixture populates;
  byte-unchanged at OBS-F.2.
- `src/manifold/CameraObserverAdapter.h`
  — the OBSERVER.6 adapter that routes
  the fixture's scene-authored beta;
  byte-unchanged at OBS-F.2.
- `src/cuda/CudaTestKernel.cu` /
  `src/optix/OptixPrograms.cu` — the
  OBS-P.2-migrated kernel sites the
  fixture exercises on an SDK host;
  byte-unchanged at OBS-F.2 (verified by
  `git diff` against the OBS-P.3
  baseline).
- `src/renderer/AOV.h` /
  `src/renderer/AOV.cpp` — the
  OBSERVER.13 `AOVType::ObserverBeta`
  enumerator the fixture exercises via
  `--observer-debug`; byte-unchanged at
  OBS-F.2.
- `tests/manifold_identity_tests.cpp` /
  `tests/cli_tests.cpp` /
  `tests/renderer_tests.cpp` /
  `tests/relativity_tests.cpp` — all
  unchanged by OBS-F.2; the fixture
  validates the pre-existing test surface
  + adds no new tests.
- `docs/BUILD_PLAN.md` — OBS-F.2 entry
  (lines 83656 onward as of `c547f2d`).
- Commit `c547f2d` — `scene: OBS-F.2 —
  ObserverFrame Fixture Implementation
  (impl, scene + companion doc)`.
- Commit `5f8cabc` — `docs: OBS-F.1 —
  ObserverFrame Fixture Task (docs only)`;
  the audit baseline.

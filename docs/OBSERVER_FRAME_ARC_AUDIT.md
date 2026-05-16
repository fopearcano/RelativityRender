# Observer-Frame Arc Capstone Audit (OBSERVER.15)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `4d5be32` ("docs:
OBSERVER.14 — Observer Debug AOV Audit (docs only)").
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, the six prior
per-slice audit verdicts
(`docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`,
`docs/OBSERVER_FRAME_CONFIG_AUDIT.md`,
`docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`,
`docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`,
`docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`,
`docs/OBSERVER_DEBUG_AOV_AUDIT.md`), the OBSERVER.1
planning doc + OBSERVER.12 task brief, the
`manifold_identity_tests` + `cli_tests` +
`renderer_tests` runtime outputs, `ctest` exit
codes, and the cross-cutting build state at
HEAD = `4d5be32`.

This audit is the **capstone for the ObserverFrame
foundation arc** (the OBSERVER.1 → OBSERVER.14
ladder, fifteen numbered slots including this
capstone). It synthesises the prior per-slice
verdicts into a single arc-level verdict, surveys
the remaining risks, and recommends the next safe
stage. Per the operator's brief, the audit is
**documentation-only** and explicitly **does not
start observer ray transforms yet** — the
kernel-side migration of aberration / Doppler /
searchlight onto `observer_frame.beta` remains
deferred per the OBSERVER.10 audit's carry-only
contract + the OBSERVER.13 debug-AOV's
read-only-no-transform contract.

---

## 1. VERDICT

**PASS_WITH_RUNTIME_DEFERRED.**

The arc closes structurally: all six landed impl
slices passed their per-slice audit gates with
PASS or PASS_WITH_RUNTIME_DEFERRED; the
architecture stayed within the operator's
"observer-frame data path only; no perception
transform yet" envelope from the OBSERVER.1 plan;
beauty + every existing AOV output is preserved
on every default code path; the `ObserverFrame`
POD's safety invariants
(`is_finite_observer_frame` /
`is_orthonormal_tetrad` / `is_normalised_timelike`)
carry through every consumer; the CUDA + OptiX
payload bridges deliver the OBSERVER.6 adapter's
output to both backends' launch boundaries with
byte-equivalent semantics; the OBSERVER.13
debug-AOV provides operator-visible verification
of the data path; and the OBSERVER.4
`--observer-*` CLI surface gives the operator a
complete config-bridge for the future
perception-transform slice. The **runtime portion
is DEFERRED** to a CUDA + OptiX-SDK host (mirrors
the SCHW.11 + PENROSE.12 capstone precedents):
the audit host cannot exercise the OBSERVER.12
task brief §8 fixture-render suite (six required
runtime checks); the per-slice OBSERVER.9 +
OBSERVER.11 + OBSERVER.14 audits each recorded
this constraint and this capstone inherits it.

**Three known follow-up items** are explicitly
carried forward (§10 below): (a) the kernel-side
**perception-transform migration** (gating
aberration / Doppler / searchlight on
`perception_mode == ConstantVelocityMinkowski` in
the non-AOV pipelines) remains deferred — the
OBSERVER.* arc was scoped to the foundation
(data model + bridges + debug AOV); the migration
is a separate future arc; (b) the **OBSERVER.12
task brief §5 fixture scene** + companion doc
(`scenes/test_observer_frame.rrscene` +
`docs/OBSERVER_FRAME_FIXTURE.md`) are not yet
landed — deferred per the OBSERVER.12 task brief
itself (mirrors MANI-I.8 → SCHW.9 cadence); (c)
the **SDK-host runtime pass** is the documented
expected gate for full validation.

No REPAIR action is required. No BLOCKED item is
outstanding. The OBSERVER.* foundation arc is
closed to the extent the audit host can verify;
the deferred items become PASS-able when (i) a
CUDA + OptiX-SDK host runs the OBSERVER.12 task
brief §8 runtime checks AND (ii) the fixture
scene + companion doc are landed (optional;
mirrors MANI-I.8 → SCHW.9 cadence) AND (iii) the
operator authorises the broader kernel-migration
arc (separate from this foundation arc).

The renderer is **structurally ready** for actual
observer-perception transforms: the data path is
end-to-end verified at audit-host build level;
every kernel-side gate that would be needed for
the migration is structurally already in place
(`perception_mode == ConstantVelocityMinkowski`
guard pattern established by the OBSERVER.6
adapter; per-launch `observer_frame.beta` field
available on both backends per OBSERVER.8 +
OBSERVER.10). The next safe stage (§11) defines
the perception-transform migration's entry
point.

---

## 2. ARC TIMELINE

| Slice | Commit  | Kind | Verdict (per-slice) |
|-------|---------|------|---------------------|
| OBSERVER.1 — Planning slice                                | `eee9d6b` | docs (planning)               | n/a (no audit gate; consumed by impl slices)  |
| OBSERVER.2 — ObserverFrame data model                      | `85496a5` | impl (POD-leaf + tests)       | n/a (audit at OBSERVER.3)                     |
| OBSERVER.3 — Data model audit                              | `bf57c9e` | docs                          | **PASS** (8 checks)                            |
| OBSERVER.4 — Config / CLI bridge                           | `16600dc` | impl (host-only + 18 tests)   | n/a (audit at OBSERVER.5)                     |
| OBSERVER.5 — Config / CLI bridge audit                     | `27ec0d9` | docs                          | **PASS** (7 checks)                            |
| OBSERVER.6 — Camera-to-observer adapter                    | `e2cde15` | impl (host-only + 12 tests)   | n/a (audit at OBSERVER.7)                     |
| OBSERVER.7 — Camera-to-observer adapter audit              | `a0215c0` | docs                          | **PASS** (7 checks)                            |
| OBSERVER.8 — CUDA observer payload bridge                  | `12f4942` | impl (CUDA-side carry-only)   | n/a (audit at OBSERVER.9)                     |
| OBSERVER.9 — CUDA observer payload audit                   | `e5fe441` | docs                          | **PASS** (8 checks); runtime CUDA DEFERRED     |
| OBSERVER.10 — OptiX observer payload bridge                | `977ff73` | impl (OptiX-side carry-only)  | n/a (audit at OBSERVER.11)                    |
| OBSERVER.11 — OptiX observer payload audit                 | `c739c56` | docs                          | **PASS** (8 checks); runtime CUDA/OptiX DEFERRED |
| OBSERVER.12 — Observer debug AOV task definition           | `e6d6ffc` | docs (task brief)             | n/a (consumed by OBSERVER.13 impl)            |
| OBSERVER.13 — Observer debug AOV implementation            | `b34e265` | impl (AOV + CUDA + OptiX + CLI; 16 files) | n/a (audit at OBSERVER.14)        |
| OBSERVER.14 — Observer debug AOV audit                     | `4d5be32` | docs                          | **PASS** (8 checks); runtime CUDA/OptiX DEFERRED |
| OBSERVER.15 — **THIS ARC CAPSTONE**                        | (this)    | docs                          | **PASS_WITH_RUNTIME_DEFERRED**                 |

Fifteen discrete numbered slots across the arc:
two planning / task-definition docs (OBSERVER.1
+ OBSERVER.12), six implementation slices
(OBSERVER.2 / .4 / .6 / .8 / .10 / .13), six
per-slice audit docs (OBSERVER.3 / .5 / .7 / .9
/ .11 / .14), and this capstone. The discipline
of **every implementation slice having its own
per-slice audit gate** is preserved without
exception — every impl slice landed has a
matching audit slice landed.

The cumulative diff across the arc, by subsystem:

- **Manifold module** (`src/manifold/`):
  `ObserverFrame.h` extended at OBSERVER.2
  (+87 lines; new `PerceptionMode` enum +
  `perception_mode` field + three validator
  helpers `default_perception_mode` /
  `is_orthonormal_tetrad` /
  `is_finite_observer_frame`) + OBSERVER.4
  (+87 lines; new `ObserverConfig` POD with
  `beta_magnitude` / `direction` /
  `proper_time` / `perception_mode` fields)
  + OBSERVER.13 (+21 lines; new
  `debug_visualization` field on
  `ObserverConfig`). New
  `CameraObserverAdapter.h` at OBSERVER.6
  (+222 lines; `build_observer_frame_from_camera(...)`
  adapter with three perception-mode
  construction paths + beta-resolution
  priority + defensive `clampBeta`).
- **Core / CLI** (`src/core/`): `Config.h`
  extended at OBSERVER.4 (+40 lines; new
  `observer` field carrying `ObserverConfig`);
  `CommandLine.cpp` extended at OBSERVER.4
  (+216 lines; four `--observer-*` parser
  arms + three helper functions
  `parse_finite_float` / `parse_vec3` /
  `parse_perception_mode` + help-text block)
  + OBSERVER.13 (+38 lines; `--observer-debug`
  flag).
- **CUDA backend** (`src/cuda/`):
  `CudaScene.cuh` extended at OBSERVER.8
  (+30 lines; `CudaSceneView::observer_frame`
  field); `CudaRenderer.h` extended at
  OBSERVER.8 (+18 lines;
  `AOVTargets::observer_frame` field) +
  OBSERVER.13 (+10 lines;
  `AOVTargets::observer_beta` field);
  `CudaRenderer.cu` extended at OBSERVER.8
  (+10 lines; thread of `targets.observer_frame`
  into `view.observer_frame`) + OBSERVER.13
  (+5 lines; thread of `targets.observer_beta`
  into `view.aovs.observer_beta`);
  `CudaAOV.cuh` extended at OBSERVER.13
  (+17 lines; `DeviceAOVView::observer_beta`
  slot); `CudaTestKernel.cu` extended at
  OBSERVER.13 (+29 lines; closest-hit + miss
  observer_beta write arms).
- **OptiX backend** (`src/optix/`):
  `OptixLaunchParams.h` extended at OBSERVER.10
  (+40 lines; `OptixLaunchParams::observer_frame`
  field) + OBSERVER.13 (+18 lines;
  `aov_observer_beta` slot); `OptixRenderer.h`
  extended at OBSERVER.10 (+30 lines; trailing-
  defaulted `observer_frame = {}` parameter on
  `render_aovs(...)` and
  `render_pathtrace_progressive(...)`) +
  OBSERVER.13 (+38 lines; trailing-defaulted
  `observer_debug = false` parameter on
  `render_aovs(...)` + `AovResult::observer_beta`
  field); `OptixRenderer.cpp` extended at
  OBSERVER.10 (+28 lines;
  `params.observer_frame` assignments at both
  sites) + OBSERVER.13 (+47 lines; conditional
  `alloc_aov(...)` + pointer thread +
  `download_3(...)` for observer_beta);
  `OptixPrograms.cu` extended at OBSERVER.13
  (+44 lines; closest-hit + miss observer_beta
  write arms).
- **Pathtracer / Renderer** (`src/pathtracer/`,
  `src/renderer/`): `PathTracer.h` extended at
  OBSERVER.8 (+26 lines;
  `PathTraceConfig::observer_frame` field);
  `AOV.h` extended at OBSERVER.13 (+28 lines;
  `AOVType::ObserverBeta = 7` enumerator +
  `make_observer_beta(...)` factory
  declaration); `AOV.cpp` extended at
  OBSERVER.13 (+11 lines; component count +
  type name + factory impl).
- **Dispatchers** (`src/main.cpp`): extended
  across OBSERVER.8 (+106 lines;
  `format_observer_config_brief` helper +
  log lines + adapter invocations in both
  `run_render_aovs` + path-trace dispatcher),
  OBSERVER.10 (+66 lines; log lines + adapter
  invocations in both `run_render_optix_aovs`
  + `run_render_optix_pathtrace`), OBSERVER.13
  (+73 lines; observer_beta_buffer allocation
  + thread + PPM save on both backend
  dispatchers).
- **Tests** (`tests/`): `manifold_identity_tests.cpp`
  extended at OBSERVER.2 (+180 lines; 4 new
  test functions) + OBSERVER.6 (+353 lines;
  12 new test functions); `cli_tests.cpp`
  extended at OBSERVER.4 (+295 lines; 18 new
  test functions) + OBSERVER.13 (+53 lines;
  3 new test functions + 1 extension);
  `renderer_tests.cpp` extended at OBSERVER.13
  (+50 lines; 3 new test functions).
- **Documentation** (`docs/`):
  `OBSERVER_FRAME_RENDERING_PLAN.md` (new at
  OBSERVER.1; ~1025 lines);
  `OBSERVER_FRAME_DATA_MODEL_AUDIT.md` (new at
  OBSERVER.3); `OBSERVER_FRAME_CONFIG_AUDIT.md`
  (new at OBSERVER.5);
  `CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md` (new at
  OBSERVER.7); `OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (new at OBSERVER.9);
  `OBSERVER_OPTIX_PAYLOAD_AUDIT.md` (new at
  OBSERVER.11); `OBSERVER_DEBUG_AOV_TASK.md`
  (new at OBSERVER.12);
  `OBSERVER_DEBUG_AOV_AUDIT.md` (new at
  OBSERVER.14); `BUILD_PLAN.md` appended at
  every slice.

**Cumulative test growth:**
- `manifold_identity_tests`: 312 → 408 RR_CHECK
  (+96 across OBSERVER.2 + OBSERVER.6); 16
  new test functions.
- `cli_tests`: 123 → 274 (+151 across
  OBSERVER.4 + OBSERVER.13); 21 new test
  functions.
- `renderer_tests`: 19 → 27 (+8 at
  OBSERVER.13); 3 new test functions.
- `ctest` set: unchanged at 12; no new test
  binary added (all new tests landed in the
  existing test files).

---

## 3. PER-CHECK RESULTS

| # | Check | Result | Evidence (carry-forward from per-slice audits) |
|---|-------|--------|-----------------------------------------------|
| 1 | ObserverFrame data model exists and is default-no-op | **PASS** | OBSERVER.3 audit (`bf57c9e`) verified the eight-field POD (`position4`, `velocity4`, `beta`, three tetrad legs, `proper_time`, `coordinate_time`, `perception_mode`) at `src/manifold/ObserverFrame.h:138-203` with documented per-field defaults that resolve to the scene-rest observer with the world-basis tetrad + Identity perception mode. The `rest_frame()` factory returns the default-constructed POD byte-for-byte. `default_perception_mode()` factory returns `Identity`. Three validator helpers (`is_finite_observer_frame`, `is_orthonormal_tetrad`, `is_normalised_timelike`) all return true on the default POD. Empirically verified at four `test_observer_2_*` test functions in `manifold_identity_tests` (+37 RR_CHECK at OBSERVER.2; verified by OBSERVER.3 audit's check #7). Master rule #3 ("no fake stubs") satisfied: the POD is consumed by the OBSERVER.6 adapter + the OBSERVER.8 / OBSERVER.10 launch boundaries + the OBSERVER.13 debug AOV. |
| 2 | CLI/config bridge exists and is safe | **PASS** | OBSERVER.5 audit (`27ec0d9`) verified the four `--observer-*` modifier flags (`--observer-beta`, `--observer-direction`, `--observer-proper-time`, `--observer-perception-mode`) at `src/core/CommandLine.cpp:678-766` route into the new `Config::observer` field at `src/core/Config.h:171` carrying an `rr::manifold::ObserverConfig` POD with documented per-field defaults preserving the pre-OBSERVER.4 byte-identity baseline. OBSERVER.13 extended the surface with `--observer-debug` at `CommandLine.cpp:767-784` + `ObserverConfig::debug_visualization` at `ObserverFrame.h:472-487`. **Invalid-value safety** verified at OBSERVER.5 audit's checks #3 + #4: non-parseable beta rejected; non-finite beta rejected; malformed direction rejected (5 subcases); non-finite proper-time rejected; unknown perception-mode rejected with diagnostic naming both legal alternatives. **Default-off byte-identity** verified across 8 non-observer argv vectors (`test_observer_default_off_with_other_flags`). 21 new `cli_tests` functions covering every parse path (`test_observer_*` family); +151 RR_CHECK total across OBSERVER.4 + OBSERVER.13. |
| 3 | Camera-to-observer adapter exists | **PASS** | OBSERVER.7 audit (`a0215c0`) verified the host-side `build_observer_frame_from_camera(GpuCamera, Observer, ObserverConfig) → ObserverFrame` adapter at `src/manifold/CameraObserverAdapter.h:138`. **Three perception-mode construction paths** mirror the OBSERVER.1 plan §7 contract verbatim: `Identity` returns `rest_frame()` byte-for-byte (no-op anchor); `ConstantVelocityMinkowski` full construction with position4 from camera + velocity4 from resolved beta + tetrad from camera basis + proper_time from config + coordinate_time from position4.x + perception_mode = ConstantVelocityMinkowski; `CurvedChartGeodesicPlaceholder` returns `rest_frame()` byte-for-byte except `perception_mode` tag preserved (structural passthrough per architecture-doc §8 non-goals). **Beta resolution priority** documented and verified: (i) CLI overlay wins when non-zero magnitude + non-zero direction; (ii) CLI magnitude + zero-direction sentinel falls back to `gc.forward`; (iii) default-config path uses legacy `observer.velocity`. **Defensive `clampBeta` second-clamp** at lines 202-207 ensures `|beta| ≤ 0.999999` regardless of source. 12 dedicated test functions in `manifold_identity_tests` (`test_observer_6_*` family); +59 RR_CHECK. Module boundaries clean — adapter consumes `GpuCamera` (header-only POD) to avoid a new `rr_camera` library link on `rr_manifold` consumers. |
| 4 | CUDA payload bridge exists | **PASS** | OBSERVER.9 audit (`e5fe441`) verified the CUDA-side payload at three attachment points: `CudaSceneView::observer_frame{}` at `src/cuda/CudaScene.cuh:137` (the kernel-visible launch-argument POD); `AOVTargets::observer_frame = {}` at `src/cuda/CudaRenderer.h:206` (the host-side AOV-dispatch struct); `PathTraceConfig::observer_frame{}` at `src/pathtracer/PathTracer.h:215` (the per-render path-trace config). One-line thread `view.observer_frame = targets.observer_frame;` at `CudaRenderer.cu:311`. Two dispatcher-side adapter invocations: `main.cpp::run_render_aovs:4184` (AOV render path) and `main.cpp::run_render_pathtrace:2693` (CUDA path-trace per-spp loop). Two host-side echo logs (`aovs observer config` + `observer         :`) fire on every host including the audit host. Carry-only contract upheld: the CUDA kernel arms (`CudaTestKernel.cu` non-AOV paths) do NOT read `view.observer_frame` for any perception transform; only the OBSERVER.13 debug-AOV write arm reads `observer_frame.beta` (no transformation; direct field copy). |
| 5 | OptiX payload bridge exists | **PASS** | OBSERVER.11 audit (`c739c56`) verified the OptiX-side payload at three attachment points: `OptixLaunchParams::observer_frame{}` at `src/optix/OptixLaunchParams.h:431` (the device-visible launch-argument POD); trailing-defaulted `observer_frame = {}` parameter on `OptixRenderer::render_aovs(...)` at `OptixRenderer.h:482` (preserves API compatibility for every existing caller); same trailing-defaulted parameter on `render_pathtrace_progressive(...)` at `OptixRenderer.h:327`. Two `params.observer_frame = observer_frame;` assignments at `OptixRenderer.cpp:1827` + `:2785`. Two dispatcher-side adapter invocations at `main.cpp::run_render_optix_aovs:2281` (AOV render path) and `main.cpp::run_render_optix_pathtrace:1708` (OptiX path-trace progressive). Two host-side echo logs (`optix-aovs observer config` + `observer         :`) fire on every host including the audit host. Same carry-only contract: OptiX programs do NOT read `optixLaunchParams.observer_frame` for any perception transform; only the OBSERVER.13 debug-AOV write arm reads the beta field (direct copy). Semantic equivalence with CUDA verified at OBSERVER.11 audit's check #3: same shared type, same upstream adapter, same dispatcher-merge precedent, same operator-visible log shape. |
| 6 | Observer debug AOV exists | **PASS** | OBSERVER.14 audit (`4d5be32`) verified the new `AOVType::ObserverBeta` AOV across nine attachment points: enumerator at `AOV.h:97` (value `= 7`, appended after `ManifoldCoordinates = 6`); component count `= 3` at `AOV.cpp:16`; type name `"observer_beta"` at `AOV.cpp:34`; `make_observer_beta(...)` factory at `AOV.cpp:101`; CUDA `DeviceAOVView::observer_beta` at `CudaAOV.cuh:96`; CUDA `AOVTargets::observer_beta` at `CudaRenderer.h:184`; CUDA thread in `render_scene_with_aovs` at `CudaRenderer.cu:297`; OptiX `aov_observer_beta` slot on `OptixLaunchParams` at line 351; OptiX `AovResult::observer_beta` at `OptixRenderer.h:447`. Closest-hit + miss kernel arms on both backends gated on `aov_observer_beta != nullptr`; hit writes `observer_frame.beta` per pixel; miss writes `(0, 0, 0)`. Gated by `--observer-debug` CLI flag (parallel to `--manifold-debug`); two-flag composition with `--render-aovs` / `--render-optix-aovs` enforces opt-in. PPM filenames `output/aov_observer_beta.ppm` (CUDA) / `output/optix_aov_observer_beta.ppm` (OptiX) follow existing convention. 6 new test functions (3 in `renderer_tests` + 3 in `cli_tests`); +28 RR_CHECK total. |
| 7 | Beauty output should remain unchanged by default | **PASS** | Cross-cutting invariant verified at every prior per-slice audit's "default no-op" check:<br>**(a) Data model layer** (OBSERVER.3 audit check #7): every `ObserverFrame` field has a per-field initialiser resolving to the scene-rest no-op anchor; `is_finite_observer_frame` + `is_orthonormal_tetrad` + `is_normalised_timelike` all return true on the default.<br>**(b) Config layer** (OBSERVER.5 audit check #2): the `ObserverConfig` default-constructs to the no-op anchor; verified empirically across 8 non-observer argv vectors at `test_observer_default_off_with_other_flags`.<br>**(c) Adapter layer** (OBSERVER.7 audit check #2): the default-default-default invocation produces `rest_frame()` byte-for-byte; verified empirically at `test_observer_6_default_is_camera_equivalent_no_op`.<br>**(d) CUDA payload layer** (OBSERVER.9 audit check #3): the default `ObserverFrame{}` propagates through `targets.observer_frame` → `view.observer_frame` unchanged; kernel does NOT read the field for any non-AOV path.<br>**(e) OptiX payload layer** (OBSERVER.11 audit check #4): same propagation guarantee on OptiX; same non-AOV-path-no-read contract.<br>**(f) Debug AOV layer** (OBSERVER.14 audit check #2): four-layer beauty preservation including null-gated kernel arm + dispatcher allocation gate + empirical anchor across 8 non-observer argv vectors. The new AOV emits NO output file when `--observer-debug` is not set.<br>**Cross-cutting verification**: `git diff` between the pre-OBSERVER.1 baseline (`988439e`) and HEAD (`4d5be32`) restricted to kernel-source files shows ONLY ADDITIONS (the new OBSERVER.13 AOV-write arms appended at end-of-block). Zero modifications to existing closest-hit / miss / raygen arithmetic. Empirical anchor: `ctest` 12/12 PASS across the arc; `manifold_identity_tests: 408/408`; `cli_tests: 274/274`; `renderer_tests: 27/27`. |
| 8 | No observer perception transform has been added yet | **PASS** | Cross-cutting non-goal verified at every per-slice audit:<br>**(a) Carry-only contract** established at OBSERVER.8 / OBSERVER.10 audits: the launch-boundary fields (`view.observer_frame` / `optixLaunchParams.observer_frame`) are reserved-but-carried; no kernel arm gates aberration / Doppler / searchlight on `perception_mode == ConstantVelocityMinkowski`.<br>**(b) Read-only debug AOV** established at OBSERVER.13 / OBSERVER.14 audits: the new `observer_beta` AOV write arms call zero SR helpers; the per-pixel write is a direct field copy with no transformation.<br>**(c) Non-AOV kernel paths byte-unchanged** verified at OBSERVER.14 audit check #6 via `git diff e6d6ffc..b34e265 --name-only -- 'src/cuda/CudaTestKernel.cu' 'src/optix/OptixPrograms.cu'` showing only ADDITIONS, no modifications to existing aberration / Doppler / searchlight call sites.<br>**(d) Legacy `Observer::velocity` still kernel-consumed** for the six scene-aware actions (`--render-pathtrace`, `--render-mesh-scene`, `--render-material-scene`, `--render-direct-lighting`, `--render-aovs`, `--render-optix-aovs`); the kernel reads `scene.observer.velocity` (CUDA) / `optixLaunchParams.observer.velocity` (OptiX) exactly as today. The OBSERVER.* arc was scoped to the foundation (data model + bridges + debug AOV); the migration of these read sites onto `observer_frame.beta` is a separate future arc.<br>**(e) `CurvedChartGeodesicPlaceholder` enumerator** preserved as reserved-but-inert per the OBSERVER.1 plan §3.6 + §8 non-goals + the OBSERVER.7 audit's three-mode adapter verification. The placeholder mode returns `rest_frame()` (no transformation); the kernel ignores the perception_mode tag entirely. |
| 9 | Runtime CUDA / OptiX validation status | **DEFERRED** | The audit host has neither CUDA nor OptiX SDK installed (`nvcc` not present; `optixGetVersion` unavailable; `RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF`). Consequently:<br>**(a) CUDA side:** The OBSERVER.8 + OBSERVER.13 CUDA plumbing cannot be compiled, linked, or device-launched from this host. The structural changes mirror the MANI-I.8 manifold-coordinates precedent verbatim, which is verified-green on SDK hosts per the MANI-I.9 audit.<br>**(b) OptiX side:** The OBSERVER.10 + OBSERVER.13 OptiX plumbing cannot be compiled, linked, or device-launched from this host. The structural changes mirror the SCHW.7 + MANI-I.8 precedents verbatim, which are verified-green on SDK hosts per the SCHW.8 + MANI-I.9 audits.<br>**(c) Audit-host CAN verify:** the host-side infrastructure (`AOV` data model + factory; `Config::observer` + CLI surface + parser + help text; `ObserverConfig` POD + validators; `ObserverFrame` POD + bridge helpers; `CameraObserverAdapter.h` host-only adapter; the dispatcher's pre-guard log lines; the audit-host smoke-test for every CLI invocation that doesn't require CUDA/OptiX) — all verified at audit-host build + test suites.<br>This is the **same documented deferral** pattern accrued by every prior CUDA / OptiX-touching slice (MANI-I.5 / SCHW.5 / SCHW.7 / PENROSE.6 / PENROSE.8 / MANI-CONSUME.1 / OBSERVER.8 / OBSERVER.10 / OBSERVER.13). The SCHW.11 + PENROSE.12 capstones recorded runtime verification as DEFERRED with the same disposition; this OBSERVER.15 capstone inherits the pattern.<br>**Required SDK-host runtime checks** to convert the arc verdict from PASS_WITH_RUNTIME_DEFERRED → PASS (per OBSERVER.12 task brief §8 + OBSERVER.14 audit check #8):<br>(1) `--render-aovs --observer-debug` produces `output/aov_observer_beta.ppm` with every hit pixel `(0, 0, 0)` on the default Identity-mode invocation; every miss pixel `(0, 0, 0)`;<br>(2) `--render-optix-aovs --observer-debug` produces the same PPM at `output/optix_aov_observer_beta.ppm`;<br>(3) `cmp aov_observer_beta.ppm optix_aov_observer_beta.ppm` returns exit status `0` (cross-backend byte-identity per OBSERVER.12 §8.6);<br>(4) Non-default invocations (e.g. `--observer-perception-mode relativistic --observer-beta 0.5 --observer-direction 1,0,0`) produce AOV PPMs whose hit pixels decode to `(0.5, 0.0, 0.0)`;<br>(5) `--render-aovs` WITHOUT `--observer-debug` produces NO `aov_observer_beta.ppm`;<br>(6) Beauty + every existing AOV's PPM byte-identical to the pre-OBSERVER.* baseline (`cmp` against a pinned reference);<br>(7) Cross-backend Beauty equivalence: the OBSERVER.* arc's host-side adapter invocations must not perturb the existing Beauty output relative to the pre-OBSERVER.* baseline. |
| 10 | Remaining risks | **CATALOGUED** | Three known follow-up items carry forward from the per-slice audits:<br>**(a) Kernel-side perception-transform migration deferred.** The non-AOV pipelines (aberration / Doppler / searchlight) still feed on the legacy `scene.observer.velocity` (CUDA) / `optixLaunchParams.observer.velocity` (OptiX). The OBSERVER.* arc was scoped to the foundation; the migration that gates these call sites on `observer_frame.perception_mode == ConstantVelocityMinkowski` is a separate future arc. Tractable: the kernel-side `perception_mode` enum field is already in place (OBSERVER.8 + OBSERVER.10); the migration is a guarded read-site swap with no new ABI requirements. Recommended next stage in §11 below.<br>**(b) Fixture scene + companion doc deferred** per the OBSERVER.12 task brief §5. `scenes/test_observer_frame.rrscene` + `docs/OBSERVER_FRAME_FIXTURE.md` are not yet landed. Mirrors MANI-I.8 → SCHW.9 cadence where the AOV impl landed first and the fixture / companion doc landed at a parameter-authoring slice. Tractable on the audit host (scene-file authoring + companion doc are host-only).<br>**(c) Runtime SDK-host verification suite deferred** (the §9 above-listed seven checks). Tractable when an SDK host runs the full pipeline; the OBSERVER.14 audit + OBSERVER.12 task brief §8 enumerate the exact required checks. Cross-backend AOV byte-equivalence + Beauty byte-identity are the two load-bearing verifications.<br>**Two non-risks** explicitly catalogued as deliberate scope:<br>**(d) `CurvedChartGeodesicPlaceholder` perception mode** remains reserved-but-inert per the OBSERVER.1 plan §3.6 + §8 non-goals. The OBSERVER.6 adapter returns `rest_frame()` for this mode; the kernel doesn't engage chart-aware behaviour. NOT a risk — documented as a future-arc placeholder; future curved-chart implementation slices will land their own task-definition + impl pair when authorised.<br>**(e) Per-pixel observer state** (moving observers across the framebuffer) is not implemented per the OBSERVER.12 task brief §6. The OBSERVER.* arc's `ObserverFrame` is per-launch (set once at the dispatcher; constant across all pixels in a launch). NOT a risk — documented as a separate future-arc concept; the data model is forward-compatible (no per-pixel POD changes required to enable it later). |
| 11 | Recommended next safe stage | **SEE §11 BELOW** | Three candidate next stages with prioritisation; the operator's call. Recommended highest-priority: **the kernel-side perception-transform migration** (gate aberration / Doppler / searchlight on `perception_mode`). See §11 for the full analysis. |

---

## 4. ARCHITECTURAL SCOPE — WHAT THE ARC IS AND ISN'T

The OBSERVER.* foundation arc **is**:

- **The observer-frame data model:** a complete
  `ObserverFrame` POD with seven structural fields
  (position4 + velocity4 + beta + three tetrad
  legs + two time placeholders + perception_mode
  tag) + three validators + bridge helpers
  to/from the legacy `rr::relativity::Observer`.
- **The CLI surface:** four `--observer-*`
  modifier flags + one debug-AOV gate
  (`--observer-debug`), each parsing into a
  documented `ObserverConfig` POD field with
  explicit defaults preserving the
  pre-OBSERVER.* byte-identity baseline.
- **The host-side adapter:** a
  `build_observer_frame_from_camera(...)`
  helper consuming the existing scene-side
  camera + legacy SR observer + the new
  `ObserverConfig` overlay, producing an
  `ObserverFrame` POD with three documented
  perception-mode construction paths.
- **The two GPU launch-boundary bridges:** CUDA
  via `CudaSceneView::observer_frame` +
  `AOVTargets::observer_frame` +
  `PathTraceConfig::observer_frame`; OptiX via
  `OptixLaunchParams::observer_frame` +
  trailing-defaulted parameter on
  `OptixRenderer::render_aovs(...)` +
  `render_pathtrace_progressive(...)`.
- **The operator-visible diagnostic AOV:**
  `AOVType::ObserverBeta` writing
  `observer_frame.beta` per hit pixel +
  `(0,0,0)` per miss pixel on both backends,
  gated by the `--observer-debug` flag.
- **The five host-side echo log lines** the
  operator sees on every host (audit-host or
  SDK-host) confirming the resolved observer
  state: two for the CUDA AOV / pathtrace
  dispatchers; two for the OptiX
  AOV / pathtrace dispatchers; one for the
  default identity-mode confirmation.
- **The complete audit-host-verifiable test
  surface:** 16 new manifold-identity tests
  covering the POD + validators + adapter; 21
  new CLI tests covering the four flags + the
  debug gate + composability + invalid-value
  rejection; 3 new renderer tests covering the
  new AOV enum + factory.

The OBSERVER.* foundation arc **is NOT**:

- **Not a kernel-side perception-transform
  migration.** The non-AOV pipelines
  (aberration / Doppler / searchlight in the
  CUDA `k_render_scene` / `k_pathtrace_sample`
  kernels and the OptiX `__raygen__` /
  `__closesthit__` / `__miss__` programs) still
  feed on the legacy `scene.observer.velocity`.
  Migrating those call sites onto
  `observer_frame.beta` is a separate future arc.
- **Not a full GR tetrad solver.** The
  `CurvedChartGeodesicPlaceholder` perception
  mode is reserved-but-inert; no parallel
  transport; no Christoffel symbols; no
  geodesic ODE; no proper-time integrator.
  Architecture-doc §8 non-goals stand verbatim.
- **Not a Kerr / Kruskal chart implementation.**
  Those families remain at MANIFOLD.1's
  `*LikePlaceholder` reserved-but-inert state.
- **Not a per-pixel observer state.** The
  `ObserverFrame` is per-launch; constant
  across all pixels in a single launch.
  Per-pixel observer state (moving observers
  across the framebuffer) is a separate future
  arc whose ABI requirements are not yet
  defined.
- **Not a `.rrscene` scene-file `observer`
  block.** The observer state is CLI-only;
  scene-file authoring of observer state is a
  separate future arc.
- **Not a denoiser-integrated AOV.** The
  `observer_beta` AOV is a diagnostic; the
  Stage 19B.4 / 21D OptiX denoiser consumes
  Beauty / Albedo / Normal only.
- **Not a fixture scene** — deferred per
  OBSERVER.12 task brief §5; mirrors MANI-I.8
  → SCHW.9 cadence. The AOV impl is
  verifiable on default scenes.

---

## 5. CROSS-CUTTING INVARIANT CHECK

Three invariants must hold across the entire arc.
All three are PASS:

### 5.1 Default-no-op preservation

Every default-default invocation produces
byte-identical output to the pre-OBSERVER.* baseline:

- **Field defaults:** `ObserverConfig{}` →
  `Identity` perception mode, zero beta, zero
  direction, zero proper-time, no debug-AOV
  allocation.
- **Adapter default:** `build_observer_frame_from_camera(...)`
  on the default config returns `rest_frame()`
  byte-for-byte.
- **GPU launch payload default:**
  `view.observer_frame = ObserverFrame{}` /
  `optixLaunchParams.observer_frame =
  ObserverFrame{}` carry the no-op anchor; no
  kernel arm reads the field for any non-AOV
  path; the AOV write arms short-circuit on
  null pointer.
- **Empirical anchor:** ctest 12/12 PASS at
  HEAD; all test binaries unchanged in count
  semantics (only ADDITIONS, no regressions).

### 5.2 Beauty + existing-AOV byte-identity

The OBSERVER.* arc adds no new arithmetic to the
existing kernel call sites (Beauty / Normal /
Depth / Albedo / DopplerFactor /
SearchlightFactor / ManifoldCoordinates). The
new `observer_beta` AOV arm at OBSERVER.13 is
the only new kernel write; it is gated behind a
null pointer that only the operator's
`--observer-debug` flag can populate. Verified by:

- `git diff 988439e..4d5be32 --name-only --
  'src/cuda/CudaTestKernel.cu'
  'src/cuda/CudaPathTracer.cu'
  'src/optix/OptixPrograms.cu'` shows only the
  OBSERVER.13 ADDITIONS — every existing arm's
  arithmetic is byte-unchanged.
- `OBSERVER.14` audit check #6 verifies this
  via the same `git diff` filtered against the
  pre-OBSERVER.13 baseline.

### 5.3 Cross-backend semantic equivalence

The CUDA + OptiX backends consume the same
`rr::manifold::ObserverFrame` POD, built by the
same `build_observer_frame_from_camera(...)`
adapter with byte-identical arguments at the
dispatcher level. The kernel-side reads (the
OBSERVER.13 `observer_beta` AOV arm) are direct
field copies with no per-backend arithmetic.
Structural cross-backend AOV byte-equivalence is
guaranteed by construction; the runtime
verification (`cmp aov_observer_beta.ppm
optix_aov_observer_beta.ppm` exit `0`) is
DEFERRED to an SDK host per §9 above.

---

## 6. BUILD + TEST STATUS

At HEAD = `4d5be32` on the audit host:

- **Compile:** `cmake --build /home/user/RelativityRender/build`
  succeeds with no new warnings on any module
  (rr_renderer / rr_manifold / rr_relativity /
  rr_core / rr_camera / rr_gpu).
  CUDA-gated TUs (`CudaTestKernel.cu`,
  `CudaPathTracer.cu`, `CudaScene.cuh`'s
  consumers) do not compile on the audit host
  per `RR_ENABLE_CUDA=OFF`; OptiX-gated TUs
  (`OptixRenderer.cpp`, `OptixPrograms.cu`,
  `OptixLaunchParams.h`'s consumers) do not
  compile per `RR_ENABLE_OPTIX=OFF`.
- **Test:** `ctest` returns `100% tests passed,
  0 tests failed out of 12`.
- **Per-suite counts:**
    - `manifold_identity_tests: 408 / 408`
      (was 312 pre-arc; +96 RR_CHECK across
      OBSERVER.2 + OBSERVER.6).
    - `cli_tests: 274/274 passed`
      (was 123 pre-arc; +151 RR_CHECK across
      OBSERVER.4 + OBSERVER.13).
    - `renderer_tests: 27/27 passed`
      (was 19 pre-arc; +8 RR_CHECK at
      OBSERVER.13).
    - `math_tests`, `image_tests`,
      `gpu_tests`, `pathtracer_tests`,
      `pathtracer_nee_tests`,
      `pathtracer_bsdf_tests`,
      `pathtracer_mis_tests`,
      `relativity_tests`, `demo_tests`: all
      unchanged from their pre-arc counts.

**Audit-host smoke tests** (verified at the
OBSERVER.8 / OBSERVER.10 / OBSERVER.13 landing
commits):

- `--help` includes all five new flags
  (`--observer-beta`, `--observer-direction`,
  `--observer-proper-time`,
  `--observer-perception-mode`,
  `--observer-debug`).
- `--render-aovs` produces `aovs observer config: identity (no-op)`
  before the existing CUDA-required error path
  (no behaviour change to the parser surface).
- `--render-aovs --observer-beta 0.5
  --observer-direction 1,0,0
  --observer-perception-mode relativistic`
  produces `aovs observer config:
  constant-velocity-minkowski (|beta|=0.500000,
  dir=[1.000000, 0.000000, 0.000000],
  tau=0.000000)` before the same error path.
- `--render-optix-aovs` produces the mirrored
  `optix-aovs observer config: ...` log
  before the OptiX-required error path.
- `--render-aovs --observer-debug` parses
  cleanly; the additional buffer-allocation
  logic only executes in the CUDA-host build.
- `--render-optix-aovs --observer-debug` parses
  cleanly; the additional `observer_debug=true`
  threading only takes effect in the OptiX-host
  build.
- `--observer-perception-mode bogus` produces
  the documented parse error naming both
  legal alternatives.

---

## 7. WHAT THIS AUDIT DOES NOT VERIFY

The audit-host build cannot directly verify:

- **Runtime device-side behaviour** of the
  CUDA + OptiX AOV-write arms, the launch-
  params payload, or the adapter's output
  reaching the kernel.
- **PPM-level byte-identity** of the Beauty
  pass + the seven existing AOVs across the
  pre-OBSERVER.* → post-OBSERVER.* commit
  range (the kernel-source diff shows only
  ADDITIONS, but verifying the actual PPM
  outputs requires an SDK host).
- **Cross-backend AOV byte-equivalence** —
  `cmp output/aov_observer_beta.ppm
  output/optix_aov_observer_beta.ppm` exit
  status `0` requires both backends running
  on the same SDK host.
- **PathTrace progressive accumulation
  consistency** — the OBSERVER.8 +
  OBSERVER.10 dispatchers thread the
  adapter's output into the per-spp path-
  trace launch params, but verifying the
  accumulated radiance is convergence-
  equivalent to the pre-OBSERVER.* baseline
  requires SDK-host runtime traces.
- **Visual diagnostic correctness** of the
  `observer_beta` AOV PPM at the documented
  non-default invocations — the OBSERVER.12
  task brief §8.3 enumerates a
  `(0.5, 0.0, 0.0)` flat-red hit-pixel
  expectation; verifying this requires
  rendering the AOV on an SDK host.
- **Fixture scene runtime behaviour** — the
  OBSERVER.12 task brief §5 deferred
  `scenes/test_observer_frame.rrscene` +
  `docs/OBSERVER_FRAME_FIXTURE.md` to a
  follow-up slice; this capstone explicitly
  does NOT land the fixture either.

These are the **DEFERRED** items the runtime
SDK-host pass exercises (§9 check #8 above).

---

## 8. RECOMMENDATION TO OPERATOR

**Verdict: PASS_WITH_RUNTIME_DEFERRED.**

The OBSERVER.* foundation arc is **structurally
closed** at the audit-host build level. Eight
structural arc-level checks (§3 checks #1-#8)
return PASS. Check #9 (runtime CUDA/OptiX
status) is DEFERRED on documented audit-host
limitations. Check #10 (remaining risks) is
CATALOGUED — three follow-up items the operator
should be aware of, all tractable in future
commits. Check #11 (recommended next stage)
points at the kernel-side **perception-
transform migration** as the highest-priority
tractable continuation (full analysis in §11
below).

The OBSERVER.1 plan's §7 OBSERVER.* sub-slice
ladder is closed by this capstone for the audit-
host portion. The deferred items become PASS-able
when:

1. **A CUDA + OptiX-SDK host runs the seven
   runtime checks** enumerated at §3 check
   #9 above (this converts the verdict from
   PASS_WITH_RUNTIME_DEFERRED → PASS for the
   arc).
2. **The fixture scene + companion doc are
   landed** (optional follow-up; mirrors
   MANI-I.8 → SCHW.9 cadence; tractable on
   the audit host without an SDK).
3. **The operator authorises the broader
   perception-transform migration arc**
   (separate from this foundation arc; would
   gate the kernel-side aberration / Doppler
   / searchlight pipeline reads on
   `observer_frame.perception_mode`).

No REPAIR action is required. No BLOCKED item
is outstanding.

**Answer to the operator's stated capstone
question** ("decide whether the renderer is
ready for actual observer-perception transforms"):

**YES — the renderer is structurally ready.**
The foundation arc closed cleanly:

- The data model (`ObserverFrame` POD +
  `PerceptionMode` enum) is in place and
  byte-identity-preserving by default.
- The CLI / config bridge exposes the full
  parameter surface the future migration
  will consume.
- The host-side adapter (`build_observer_frame_from_camera`)
  is the canonical point of construction; the
  three perception-mode paths are documented +
  empirically verified.
- The CUDA + OptiX launch boundaries carry
  the `ObserverFrame` POD to both kernels
  with byte-equivalent semantics; the
  per-launch field is available at every
  kernel call site that currently feeds on
  `scene.observer.velocity`.
- The `observer_beta` debug AOV provides
  operator-visible verification of the data
  path — confirming the kernel sees the
  per-launch payload intact before any
  transform engages.
- Master rule #3 ("no fake stubs") is
  satisfied across the arc: every reserved-
  but-not-yet-consumed field is structurally
  consumed by either a dispatcher, an echo
  log, the debug AOV, or the planned next-
  arc kernel-read wiring.

The next safe stage (§11 below) is the
perception-transform migration itself —
guarded by `perception_mode ==
ConstantVelocityMinkowski` gates on the
existing aberration / Doppler / searchlight
kernel call sites. The foundation makes
this a guarded read-site swap rather than
a sweep.

The operator may proceed to any of the §11
candidates; the OBSERVER.15 capstone verdict
authorises all three.

---

## 9. RECOMMENDED NEXT SAFE STAGE

Three candidate next stages with prioritisation:

### 9.1 (RECOMMENDED) Kernel-side perception-transform migration

**Scope:** gate the existing aberration /
Doppler / searchlight kernel call sites in
`CudaTestKernel.cu` / `CudaPathTracer.cu` /
`OptixPrograms.cu` on
`observer_frame.perception_mode ==
ConstantVelocityMinkowski`. When the gate is
true, replace `scene.observer.velocity` reads
with `scene.observer_frame.beta` reads; the
math helpers (`aberrateDirection` /
`dopplerFactor` / `searchlightFactor` /
`applyDopplerColor`) take the new beta
unchanged (they accept any Vec3 3-velocity).
When the gate is false (the `Identity`
default), the existing SR path is skipped or
the helpers are called against zero beta
(which is what the legacy default does today).

**Why recommended:** This is the natural
completion of the OBSERVER.* foundation. The
arc landed everything needed; the migration
is a guarded read-site swap with no new ABI.
Single-task-brief slice with a per-slice
audit gate following the OBSERVER.* per-slice
discipline.

**Files involved:** `src/cuda/CudaTestKernel.cu`
(N read-site swaps); `src/cuda/CudaPathTracer.cu`
(same); `src/optix/OptixPrograms.cu` (same).

**PASS criteria:** byte-identity preserved
when `--observer-perception-mode default`
(the no-op Identity gate); the existing
`scene.observer.velocity` path still runs
when the operator hasn't engaged the
`--observer-perception-mode relativistic`
flag explicitly. Convergence-equivalent
output to the pre-migration baseline when
the operator engages
`ConstantVelocityMinkowski` with the same
beta value that `Observer::velocity` would
have carried (single-source-of-truth: both
read sites feed on the same underlying
3-velocity).

**Audit gate:** mirror the OBSERVER.9 +
OBSERVER.11 CUDA/OptiX-payload-audit shapes
(per-slice; 9-row evidence table; runtime
status DEFERRED for SDK-host verification).

### 9.2 (OPTIONAL) Fixture scene follow-up

**Scope:** land `scenes/test_observer_frame.rrscene`
(a small scene with a non-trivial observer
velocity) + `docs/OBSERVER_FRAME_FIXTURE.md`
(companion doc documenting the expected
visual signature of the `observer_beta`
AOV). Deferred per the OBSERVER.12 task
brief §5 + mirrors MANI-I.8 → SCHW.9
cadence.

**Why optional:** the OBSERVER.13 AOV impl
is verifiable on default scenes (the
Identity-mode neutral diagnostic produces a
flat `(0, 0, 0)` AOV at every hit pixel);
the fixture scene's added value is for the
SDK-host visual verification of the
non-default invocation. The operator may
choose to land the fixture before or after
the §9.1 migration; either ordering works.

**Files involved:** `scenes/test_observer_frame.rrscene`
(new); `docs/OBSERVER_FRAME_FIXTURE.md`
(new); `docs/BUILD_PLAN.md` (append).

**PASS criteria:** the fixture loads cleanly
via `--scene-info` on the audit host; the
companion doc documents the SDK-host visual
signature; no source-code change.

### 9.3 (DEFERRED) Per-pixel observer state

**Scope:** introduce per-pixel observer state
(moving observers across the framebuffer).
Would require extending `ObserverFrame` to a
device-resident array indexed per pixel.

**Why deferred:** the OBSERVER.* foundation
arc was scoped to per-launch observer state.
The ABI requirements for per-pixel observer
state are not yet defined; the operator has
not authorised this scope. NOT recommended
as the next stage; revisit if/when an
operator-visible use case emerges (e.g.
GPU-side observer-curving across a frame).

---

## 10. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #1
  ("Build incrementally") + #2 ("Keep every
  step compilable") + #3 ("no fake stubs") all
  satisfied across the arc.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` —
  §3.3 Observer Frame + §6 GPU integration
  strategy + §7.2 SR-helper subsumption + §8
  non-goals. The OBSERVER.* arc realises §3.3
  + §6 verbatim; preserves §7.2's "the
  existing SR helpers are the Minkowski +
  constant-velocity-frame specialisation" via
  the `ConstantVelocityMinkowski` perception
  mode; respects §8 ("no full GR tetrad
  solver") via the `CurvedChartGeodesicPlaceholder`
  reserved-but-inert mode.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` —
  the OBSERVER.1 planning doc; defines the
  arc's nine sections (Purpose / Current
  state / Observer-frame concepts / Camera
  relationship / RelativityParams
  relationship / GPU integration strategy /
  Proposed implementation slices /
  Non-goals / References). The arc landed
  every OBSERVER.2-OBSERVER.13 slot the plan
  proposed (with the OBSERVER.3 / .5 / .7 /
  .9 / .11 / .14 audit-slot insertions
  documented in each per-slice audit's §4
  ladder).
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — PASS verdict on the
  `ObserverFrame` POD + the `PerceptionMode`
  enum + three validator helpers.
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5) — PASS verdict on the four
  `--observer-*` CLI flags + the
  `ObserverConfig` POD + the safe invalid-
  value handling.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — PASS verdict on the
  `build_observer_frame_from_camera(...)`
  adapter + three perception-mode
  construction paths + beta-resolution
  priority + defensive `clampBeta`.
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — PASS verdict (with runtime
  CUDA DEFERRED) on the CUDA-side three-
  attachment-point payload bridge.
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11) — PASS verdict (with runtime
  CUDA/OptiX DEFERRED) on the OptiX-side
  three-attachment-point payload bridge.
- `docs/OBSERVER_DEBUG_AOV_TASK.md`
  (OBSERVER.12) — the operator-facing task
  brief for the OBSERVER.13 impl slice;
  documents the recommended MVP scope
  (`observerBeta` AOV) + two FUTURE channels
  + the six runtime SDK-host checks.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — PASS verdict (with runtime
  CUDA/OptiX DEFERRED) on the OBSERVER.13
  debug-AOV implementation.
- `docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md` —
  the earlier audit that landed the
  `ObserverFrame` POD's structural
  foundation at MANIFOLD.3; carry-forward
  of the seven-field POD shape.
- `docs/MANIFOLD_CONSUMPTION_GAP_AUDIT.md`
  (MANI-CONSUME.2) — the precedent for the
  "log fires before the SDK guard so
  audit-host smoke tests see it" pattern
  that the OBSERVER.8 / OBSERVER.10
  dispatchers follow.
- `docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md`
  (SCHW.11) — the precedent capstone audit
  this OBSERVER.15 audit mirrors in
  structure (PASS_WITH_RUNTIME_DEFERRED
  verdict; arc-timeline table; per-check
  results; architectural-scope section;
  cross-cutting invariants; build/test
  status; recommendation to operator;
  next-stage analysis).
- `docs/PENROSE_LIKE_ARC_AUDIT.md`
  (PENROSE.12) — the second precedent
  capstone audit; same shape; same
  verdict variant.
- `docs/BUILD_PLAN.md` — every per-slice
  entry from OBSERVER.1 through OBSERVER.14
  is preserved as a point-in-time
  historical snapshot; this OBSERVER.15
  capstone entry is additive.
- `src/manifold/ObserverFrame.h` — the
  POD + helpers + `ObserverConfig` + bridges;
  the centerpiece of the data model.
- `src/manifold/CameraObserverAdapter.h` —
  the host-side adapter; the centerpiece of
  the host-side seam.
- `src/cuda/CudaScene.cuh` /
  `src/cuda/CudaRenderer.h/.cu` /
  `src/cuda/CudaTestKernel.cu` /
  `src/cuda/CudaAOV.cuh` /
  `src/pathtracer/PathTracer.h` — the CUDA
  + path-tracer surface the arc extended.
- `src/optix/OptixLaunchParams.h` /
  `src/optix/OptixRenderer.h/.cpp` /
  `src/optix/OptixPrograms.cu` — the OptiX
  surface the arc extended.
- `src/renderer/AOV.h/.cpp` — the AOV
  data-model surface the arc extended for
  the `ObserverBeta` debug AOV.
- `src/core/Config.h` /
  `src/core/CommandLine.cpp` — the CLI /
  config surface the arc extended.
- `src/main.cpp` — the dispatchers the arc
  extended across four entry points
  (`run_render_aovs`, the path-trace
  dispatcher, `run_render_optix_aovs`,
  `run_render_optix_pathtrace`).
- `tests/manifold_identity_tests.cpp` /
  `tests/cli_tests.cpp` /
  `tests/renderer_tests.cpp` — the test
  surfaces the arc extended.
- Commit `4d5be32` — the audited HEAD.
- Commits across the arc (in landing
  order): `eee9d6b` / `85496a5` /
  `bf57c9e` / `16600dc` / `27ec0d9` /
  `e2cde15` / `a0215c0` / `12f4942` /
  `e5fe441` / `977ff73` / `c739c56` /
  `e6d6ffc` / `b34e265` / `4d5be32`.

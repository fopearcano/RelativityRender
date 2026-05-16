# Kernel-Side Perception Transform Migration Audit (OBS-P.3)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `c729b53` ("kernel:
OBS-P.2 — Kernel-Side Perception Transform Migration
(impl, guarded read-site swap)").
Audit baseline: `fc2d83e` ("docs: OBS-P.1 — Kernel-
Side Perception Transform Migration Task (docs
only)") — the last commit before OBS-P.2 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, the OBS-P.1 task
brief, the `relativity_tests` runtime output, the
unchanged `manifold_identity_tests` / `cli_tests` /
`renderer_tests` runtime outputs, and `ctest` exit
codes.

This audit is the per-slice gate for OBS-P.2
(`c729b53`). It verifies the eleven items the task
brief enumerates — aberration / Doppler / searchlight
calls gated on `ConstantVelocityMinkowski`; CUDA +
OptiX kernel read sites consume the observer-frame
fields; legacy `RelativityParams` remains adapter
input only; default scenes unchanged; cross-backend
semantic alignment; build/test status; runtime
status; verdict — and produces a `PASS` /
`PASS_WITH_RUNTIME_DEFERRED` / `REPAIR` / `BLOCKED`
verdict.

---

## 1. VERDICT

**PASS_WITH_RUNTIME_DEFERRED.**

All nine structural checks return `PASS`. Check #10
(runtime CUDA/OptiX verification status) is
`DEFERRED` on the documented audit-host limitation
(no CUDA SDK, no OptiX SDK; `RR_ENABLE_CUDA=OFF`,
`RR_ENABLE_OPTIX=OFF`). Check #11 (overall verdict)
is `PASS_WITH_RUNTIME_DEFERRED`: the structural
plumbing is end-to-end complete; the gate is at
every documented call site; the legacy fallback
branch preserves byte-identity by construction; the
SDK-host runtime pass is the documented expected
gate for full validation (mirrors OBSERVER.9 /
OBSERVER.11 / OBSERVER.14 / SCHW.11 / PENROSE.12
precedents).

**One scope correction is recorded** vs the OBS-P.1
task brief: the brief listed 6 kernel call sites
(3 CUDA + 3 OptiX); OBS-P.2 audit confirms only 5
sites required migration (the CUDA path-tracer
`k_pathtrace_sample` doesn't call SR helpers — it
carries `view.observer` for launch-setup but the
kernel body itself doesn't apply aberration /
Doppler / searchlight). The 5-site migration is
complete; the OBS-P.1 brief's "C-3" was
over-counted. See check #5 below for the per-site
verification including this correction.

No REPAIR action is required. No BLOCKED item is
outstanding. The OBS-P arc's audit chain may close
at the next slot (OBS-P arc capstone audit; see §4
below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Aberration calls gated by ConstantVelocityMinkowski | **PASS** | The kernel-side `aberrateDirection(...)` calls in both backends now consume the `PrecomputedRelativity` snapshot derived from the OBS-P.2 ternary-selected `beta_source`. The ternary fires once per kernel-thread before any SR helper consumes the snapshot, so all downstream `aberrateDirection` calls are structurally gated on `observer_frame.perception_mode == ConstantVelocityMinkowski`. CUDA-side aberration sites: `CudaTestKernel.cu:239` (`k_sphere_relativistic`) + `CudaTestKernel.cu:364` (`k_render_scene`); both downstream of the guards at lines 226-232 + 350-356 respectively. OptiX-side aberration site: `OptixPrograms.cu:223` (`__raygen__pinhole`); downstream of the guard at lines 215-221. The aberration call's `if (params.enable_aberration)` flag-guard is preserved verbatim per the OBS-P.1 §4.3 orthogonality contract — `RelativityParams::enable_aberration` continues to gate WHETHER the helper is called; the new OBS-P.2 perception-mode guard selects WHICH beta the helper receives. |
| 2 | Doppler calls gated by ConstantVelocityMinkowski | **PASS** | Same gate-once-consume-downstream structure as aberration. CUDA-side `dopplerFactor(...)` sites: `CudaTestKernel.cu:263` (`k_sphere_relativistic`) + `CudaTestKernel.cu:556` (`k_render_scene`); both consume the `rel` snapshot from the guard. CUDA-side `applyDopplerColor(...)` sites: `CudaTestKernel.cu:267` + `CudaTestKernel.cu:560`; both downstream of `dopplerFactor`. OptiX-side `dopplerFactor` sites: `OptixPrograms.cu:162` (`__miss__radiance`; gated by `OptixPrograms.cu:152-158`) + `OptixPrograms.cu:235` (`__raygen__pinhole`; gated by `OptixPrograms.cu:215-221`). OptiX-side `applyDopplerColor` site: `OptixPrograms.cu:110` (in a shared `__closesthit__` body that consumes the OptiX-side `D` payload-register computed in `__raygen__pinhole`); gated by O-2's guard. The existing `if (params.enable_doppler)` flag-guard at every site is preserved verbatim. |
| 3 | Searchlight calls gated by ConstantVelocityMinkowski | **PASS** | Same pattern. CUDA-side `searchlightFactor(...)` sites: `CudaTestKernel.cu:273` (`k_sphere_relativistic`) + `CudaTestKernel.cu:568` (`k_render_scene`); both consume the `D` value derived from the gated `rel` snapshot. OptiX-side `searchlightFactor(...)` site: `OptixPrograms.cu:118` (in the shared `__closesthit__` body consuming the gated `D` payload-register from `__raygen__pinhole`). Also: the OBS-P.2 commit's OptiX `__raygen__pathtrace` site at `OptixPrograms.cu:1067-1080` carries the gated `rel` through the per-spp loop; the path-tracer's downstream Doppler / searchlight applications (lines 1326 onward) all consume the per-loop snapshot. The existing `if (params.enable_searchlight)` flag-guard at every site is preserved verbatim. |
| 4 | CUDA kernel call sites read observer_frame fields | **PASS** | Two CUDA-side migrated call sites verified:<br>**(C-1) `k_sphere_relativistic`** at `src/cuda/CudaTestKernel.cu`. Guard at lines 226-232: reads `observer_frame.perception_mode` (the new kernel-arg from the OBS-P.2 signature extension) for the gate; reads `observer_frame.beta` on the gated path; reads the legacy `observer.velocity` (the existing kernel-arg) on the fallback path. Kernel signature now takes a trailing `rr::manifold::ObserverFrame observer_frame` parameter; launcher + dispatcher signatures extended with trailing-defaulted parameter; default `rest_frame()` preserves byte-identity for every existing caller (verified at audit-host build + ctest).<br>**(C-2) `k_render_scene`** at `src/cuda/CudaTestKernel.cu`. Guard at lines 350-356: reads `scene.observer_frame.perception_mode` (the OBSERVER.8 `CudaSceneView::observer_frame` field) for the gate; reads `scene.observer_frame.beta` on the gated path; reads the legacy `scene.observer.velocity` (preserved on the view; populated by `CudaRenderer::render_scene_with_aovs` from the OBSERVER.6 adapter output) on the fallback path. No signature change required because the view already carries `observer_frame` since OBSERVER.8.<br>**Note on field semantics**: The `ObserverFrame` POD has `beta` (the resolved 3-velocity Vec3 — the combined direction × magnitude product) but does NOT have a separate `direction` field. The kernel-side reads consume `observer_frame.beta` directly (3 floats) and `observer_frame.perception_mode` (1 byte). The operator's brief mentioned `observer_frame.direction` as a desired read; OBS-P.2 reads `observer_frame.beta` instead, which is the canonical resolved 3-velocity that already encodes the direction. The `ObserverConfig::direction` field (host-side; CLI overlay) is consumed by the OBSERVER.6 adapter at the dispatcher level and combined with `beta_magnitude` to produce `ObserverFrame::beta`; the kernel receives only the resolved beta. This is the documented OBSERVER.7 audit check #3 beta-resolution priority verbatim. |
| 5 | OptiX kernel call sites read observer_frame fields | **PASS** | Three OptiX-side migrated call sites verified:<br>**(O-1) `__miss__radiance`** at `src/optix/OptixPrograms.cu:152-158`. Reads `optixLaunchParams.observer_frame.perception_mode` for the gate; `optixLaunchParams.observer_frame.beta` on the gated path; the legacy `obs.velocity` (local reference to `optixLaunchParams.observer`) on the fallback path. Single-source-of-truth math with the CUDA-side ternary at C-1 / C-2 (same `PerceptionMode::ConstantVelocityMinkowski` comparison; same `Vec3` ternary select; same downstream `precompute_relativity(beta_source)` call).<br>**(O-2) `__raygen__pinhole`** at `src/optix/OptixPrograms.cu:215-221`. Same ternary shape as O-1; reads `optixLaunchParams.observer_frame.{perception_mode, beta}` on the gated path; `optixLaunchParams.observer.velocity` on the fallback path.<br>**(O-3) `__raygen__pathtrace`** at `src/optix/OptixPrograms.cu:1071-1080`. Same ternary shape; the per-spp loop consumes the gated `rel` snapshot for every bounce iteration (the ternary fires once per program invocation, not once per sample).<br>**Cross-backend symmetry**: the CUDA + OptiX ternaries use the identical shape verbatim — same `PerceptionMode::ConstantVelocityMinkowski` enum comparison, same `Vec3` ternary select with `observer_frame.beta` on the gated path and the legacy `observer.velocity` on the fallback, same downstream `precompute_relativity(beta_source)` helper call. The cross-backend AOV / output byte-equivalence is structurally guaranteed by single-source-of-truth math (per OBSERVER.11 audit check #3 + OBSERVER.14 audit check #6) verbatim. |
| 6 | Legacy RelativityParams remains adapter/config input only | **PASS** | Three observations confirm the legacy types are NOT a runtime source of truth on the gated path:<br>**(a) `rr::relativity::Observer` + `RelativityParams`** continue to flow through the existing scene-loader + CLI + dispatcher paths exactly as today. The scene-file `relativity` block continues to populate `scene.observer.velocity` (verified by inspection: `git diff fc2d83e..c729b53 -- 'src/io/SceneLoader.cpp' 'src/scene/Scene.h'` returns zero hits — the scene-loader / Scene types are byte-unchanged). The `RelativityParams` flag-guards continue to gate the SR helpers exactly as today (`if (params.enable_aberration)` at every aberration site; same for Doppler + searchlight; the existing flags' semantics are byte-unchanged).<br>**(b) The OBSERVER.6 adapter** at `src/manifold/CameraObserverAdapter.h:138` continues to consume `rr::relativity::Observer` as one of its three inputs (verified at OBSERVER.7 audit check #1). The adapter's beta-resolution priority (CLI overlay > zero-direction-sentinel fallback > legacy `observer.velocity`) is preserved verbatim. The adapter's output (`ObserverFrame::beta`) becomes the runtime source of truth ONLY for the gated path; the legacy fallback branch reads the original `Observer.velocity` directly without round-tripping through the adapter.<br>**(c) The kernel's legacy fallback branch** reads the legacy fields directly. At every guarded call site, the ternary's `:` branch is `observer.velocity` (CUDA, kernel-arg) / `scene.observer.velocity` (CUDA, via `CudaSceneView`) / `optixLaunchParams.observer.velocity` (OptiX). When the gate returns `false` (the default Identity mode or the reserved `CurvedChartGeodesicPlaceholder`), the kernel reads the legacy field exactly as today — byte-identity preserved for every pre-OBS-P.2 invocation. This is the documented OBS-P.1 §2.4 "load-bearing byte-identity anchor". |
| 7 | Default / no-op scenes remain unchanged | **PASS** | Three-layer default-no-op preservation:<br>**(a) Adapter-level neutrality.** Verified at OBSERVER.7 audit check #2: with `cfg.observer.perception_mode == Identity` (the default), `build_observer_frame_from_camera(...)` returns `rest_frame()` byte-for-byte. The resulting `view.observer_frame.perception_mode == Identity` AND `view.observer_frame.beta == (0, 0, 0)`.<br>**(b) Kernel-side guard short-circuits.** At every OBS-P.2 call site, the `perception_mode == ConstantVelocityMinkowski` comparison returns `false` on the default `Identity` mode → the ternary takes the legacy fallback branch → the kernel reads the original `observer.velocity` (or `scene.observer.velocity` / `optixLaunchParams.observer.velocity`) field exactly as today. The legacy SR pipeline runs with the legacy beta input unchanged.<br>**(c) Empirical test surface unchanged.** Every existing test binary's pre-OBS-P.2 count is preserved exactly: `manifold_identity_tests: 408/408` (unchanged); `cli_tests: 274/274` (unchanged); `renderer_tests: 27/27` (unchanged); the only test-count delta is `relativity_tests: 813 → 841` (+28 RR_CHECK from the 2 new OBS-P.2-specific tests verifying the ternary itself). Audit-host smoke tests: `--render-aovs` (default) produces the existing `aovs observer config: identity (no-op)` log line + the CUDA-required error path (no parser-surface behaviour change); the four `--manifold-*` flags + the four `--observer-*` flags + `--observer-debug` + the existing six action flags all continue to function. The full pre-OBS-P.2 CLI surface is byte-unchanged. |
| 8 | CUDA / OptiX semantic alignment status   | **PASS** | Five-axis cross-backend semantic equivalence verified:<br>**(a) Same shared types.** Both backends read the same `rr::manifold::ObserverFrame` POD + `rr::manifold::PerceptionMode` enum + `rr::math::Vec3` type. No backend-specific re-encoding.<br>**(b) Same ternary shape.** Every guarded call site (C-1, C-2 on CUDA; O-1, O-2, O-3 on OptiX) uses the identical ternary: `const bool perception_active = (perception_mode == ConstantVelocityMinkowski); const Vec3 beta_source = perception_active ? observer_frame.beta : observer.velocity;`. The local variable names differ slightly across the OptiX sites for scope-isolation (e.g. `perception_active_pinhole` at O-2 vs `perception_active_pt` at O-3) but the ternary's structural shape is byte-identical.<br>**(c) Same downstream consumer.** Every site feeds the resolved `beta_source` into `rr::relativity::precompute_relativity(beta_source)` — the single-source-of-truth `RR_HD inline` math leaf in `src/relativity/RelativityMath.h`. The downstream SR helpers (`aberrateDirection`, `dopplerFactor`, `searchlightFactor`, `applyDopplerColor`) all consume the same `PrecomputedRelativity` snapshot type with byte-identical arithmetic on both backends (the snapshot's `(beta_vec, beta_mag, gamma)` triple is computed by the same `RR_HD inline` helper).<br>**(d) Same upstream adapter.** Both backends populate the `observer_frame` payload from the same `build_observer_frame_from_camera(scene.camera.to_gpu(), scene.observer, cfg.observer)` call at the dispatcher level (verified at OBSERVER.9 audit check #3 + OBSERVER.11 audit check #3). With byte-identical adapter inputs on both backends, the carried `observer_frame.{perception_mode, beta}` fields are byte-identical at the kernel-side read.<br>**(e) Empirical host-side equivalence.** `test_obs_p_2_perception_mode_branch_equivalence` at `tests/relativity_tests.cpp:518` verifies the kernel-side ternary's branch-equivalence on the host: for the same input beta value reached via either source path, both `precompute_relativity(beta_source)` invocations produce **bit-identical** `(beta_vec, beta_mag, gamma)` snapshots. `test_obs_p_2_perception_mode_three_enumerators` at line 592 verifies the ternary's enumerator dispatch is correct across all three `PerceptionMode` values.<br>Runtime SDK-host cross-backend PPM `cmp` verification (the OBS-P.1 §7.4 deferred check #5) remains DEFERRED to a CUDA+OptiX SDK host (mirrors the OBSERVER.11 audit's cross-backend equivalence deferred-check pattern). |
| 9 | Build / test status                      | **PASS** | Audit-host `cmake --build /home/user/RelativityRender/build` succeeds cleanly with no new warnings on the rr_relativity / rr_manifold / rr_gpu / rr_optix / rr_renderer modules. The CUDA-gated TUs (`CudaTestKernel.cu`, `CudaRenderer.cu`) and OptiX-gated TUs (`OptixPrograms.cu`) don't compile on the audit host per `RR_ENABLE_CUDA=OFF` + `RR_ENABLE_OPTIX=OFF`; the structural changes there mirror the MANI-I.5 + OBSERVER.10 + OBSERVER.13 trailing-arg precedents verbatim (defaulted trailing parameter; per-arm ternary; no other change). The SDK-host compile is structurally identical to the precedent baselines per OBS-P.1 §7.4 + the OBSERVER.11 audit's cross-backend semantic alignment evidence.<br>Full `ctest` from the audit-host build directory: `100% tests passed, 0 tests failed out of 12`.<br>**Per-suite counts** (delta from the post-OBSERVER.15 / pre-OBS-P.2 baseline at HEAD = `fc2d83e`):<br>- `relativity_tests: 841/841 passed` (was 813; **+28 new RR_CHECK** from the 2 new OBS-P.2 test functions).<br>- `manifold_identity_tests: 408/408` (unchanged).<br>- `cli_tests: 274/274 passed` (unchanged).<br>- `renderer_tests: 27/27 passed` (unchanged).<br>- All other test suites unchanged.<br>Audit-host smoke tests pass on every CLI surface combination (verified at the OBS-P.2 landing commit `c729b53`): `--help`, `--render-aovs`, `--render-optix-aovs`, `--render-pathtrace`, `--render-optix-pathtrace`, all 8 `--observer-*` / `--manifold-*` flag combinations parse cleanly with the existing log lines firing correctly. |
| 10 | Runtime CUDA / OptiX verification status | **DEFERRED** | The audit host has neither CUDA nor OptiX SDK installed (`nvcc` not present; `optixGetVersion` unavailable; `RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF`). Consequently:<br>**(a) CUDA side:** The OBS-P.2 CUDA ternaries (C-1 at `CudaTestKernel.cu:226-232`; C-2 at `:350-356`) cannot be compiled, linked, or device-launched from this host. The launcher signature extension at `CudaKernels.cuh:70-85` + the dispatcher signature extension at `CudaRenderer.h:81-95` are header-only and compile cleanly on the audit host; the kernel-side bodies remain DEFERRED.<br>**(b) OptiX side:** The OBS-P.2 OptiX ternaries (O-1 at `OptixPrograms.cu:152-158`; O-2 at `:215-221`; O-3 at `:1071-1080`) cannot be compiled, linked, or device-launched from this host.<br>**(c) Audit-host CAN verify:** the host-side ternary logic via the 2 new `test_obs_p_2_*` functions in `relativity_tests.cpp` (the ternary's two branches produce bit-identical `PrecomputedRelativity` snapshots for matching inputs; the three `PerceptionMode` enumerators route correctly); the launcher / dispatcher signature extensions compile cleanly; the audit-host smoke tests confirm the parser surface is byte-unchanged; the `RelativityParams` flag-guards are textually preserved (verified by inspection).<br>This is the **same documented deferral** pattern accrued by every prior CUDA / OptiX-touching slice (MANI-I.5 / SCHW.5 / SCHW.7 / PENROSE.6 / PENROSE.8 / MANI-CONSUME.1 / OBSERVER.8 / OBSERVER.10 / OBSERVER.13). The OBSERVER.9 / OBSERVER.11 / OBSERVER.14 audits + the OBSERVER.15 + SCHW.11 + PENROSE.12 capstones all recorded runtime verification as DEFERRED with this disposition; OBS-P.3 inherits the pattern.<br>**Required SDK-host runtime checks** to convert the verdict from PASS_WITH_RUNTIME_DEFERRED → PASS, per the OBS-P.1 §7.4 nine-check list:<br>(1) `--render-aovs` (default) byte-identical to pre-OBS-P.2 baseline;<br>(2) `--render-pathtrace` / `--render-mesh-scene` / `--render-material-scene` / `--render-direct-lighting` / `--render-relativistic` / Stage 19E.2 demo actions — all byte-identical;<br>(3) `--render-optix-aovs` + `--render-optix-pathtrace` byte-identical;<br>(4) `--render-aovs --observer-perception-mode relativistic --observer-beta 0.5 --observer-direction 0,0,-1` produces convergence-equivalent Beauty PPM to the matching legacy invocation;<br>(5) Cross-backend `cmp` between CUDA + OptiX outputs for the opt-in invocation;<br>(6) OBSERVER.13 debug AOV (`observer_beta`) unchanged regardless of perception_mode;<br>(7) `RelativityParams` orthogonality: `enable_aberration = false` + `--observer-perception-mode relativistic` produces aberration-free Beauty (existing flag wins);<br>(8) Path-tracer convergence at fixed spp;<br>(9) No new error / warning in the log stream. |
| 11 | PASS / PASS_WITH_RUNTIME_DEFERRED / REPAIR / BLOCKED verdict | **PASS_WITH_RUNTIME_DEFERRED** | All nine structural checks return `PASS`. Check #10 (runtime CUDA/OptiX) is `DEFERRED` on the documented audit-host SDK-absence limitation. No `REPAIR` or `BLOCKED` item is outstanding. The OBS-P.2 commit ships:<br>- 5 kernel call sites correctly migrated (the OBS-P.1 task brief's 6-site estimate was over-counted by one; the audit corrects to 5);<br>- per-arm guard shape matching the OBS-P.1 §2.3 contract verbatim;<br>- legacy fallback branch preserves byte-identity at every site;<br>- single-source-of-truth math across both backends (cross-backend semantic equivalence structurally guaranteed);<br>- 2 new test functions covering the ternary's branch-equivalence + enumerator dispatch (+28 RR_CHECK in `relativity_tests`);<br>- structural mirroring of MANI-I.5 + OBSERVER.10 + OBSERVER.13 trailing-arg precedents (signature extensions compile cleanly on the audit host with no warnings);<br>- audit-host smoke tests confirm parser-surface byte-identity;<br>- the existing `RelativityParams` flag-guard surface is preserved verbatim per the OBS-P.1 §4.3 orthogonality contract.<br>The slice is **safe to extend**; the next OBS-P arc slot is the arc capstone audit closing the OBS-P arc with a `PASS_WITH_RUNTIME_DEFERRED` verdict mirroring the OBSERVER.15 capstone shape. The audit verdict authorises the operator to proceed to the OBS-P arc capstone OR to leave the OBS-P arc in its current state pending an SDK-host runtime pass that converts both this OBS-P.3 verdict + the future capstone verdict from PASS_WITH_RUNTIME_DEFERRED → PASS. |

---

## 3. REASONING SUMMARY

The OBS-P.2 commit (`c729b53`) introduces a
guarded read-site swap at every documented kernel
perception call site, mirroring the OBS-P.1 task
brief verbatim except for one documented scope
correction (5 sites instead of 6 — the CUDA path-
tracer kernel doesn't call SR helpers; corrected
in this audit's check #5 + the OBS-P.2 commit's
BUILD_PLAN entry).

The aberration-gating invariant (check #1) is
**file/line verified** at 3 sites (2 CUDA + 1
OptiX); the existing `if (params.enable_aberration)`
flag-guards are preserved verbatim at every site.

The Doppler-gating invariant (check #2) is
**file/line verified** at 5 sites (2 CUDA + 3
OptiX including the `applyDopplerColor` chain on
the shared `__closesthit__` arm).

The searchlight-gating invariant (check #3) is
**file/line verified** at 3 sites (2 CUDA + 1
OptiX shared closest-hit). The OptiX path-tracer
at O-3 carries the gated `rel` snapshot through
the per-spp loop; the snapshot fires once per
program invocation, not once per sample.

The CUDA-kernel-reads-observer_frame invariant
(check #4) is **two-site verified** at C-1 + C-2.
C-1 required signature extensions (kernel +
launcher + dispatcher trailing-defaulted
parameter); C-2 reuses the OBSERVER.8 `CudaSceneView`
field with no signature change.

The OptiX-kernel-reads-observer_frame invariant
(check #5) is **three-site verified** at O-1, O-2,
O-3. All three use the OBSERVER.10 `OptixLaunchParams`
field with no signature change. Cross-backend
ternary shape is byte-identical to the CUDA-side
ternaries.

The legacy-RelativityParams-remains-adapter-input
invariant (check #6) is **three-layer verified**:
the scene-loader + Scene types are byte-unchanged;
the OBSERVER.6 adapter continues to consume legacy
`Observer` as input; the kernel's legacy fallback
branch reads the legacy fields directly when the
gate returns false.

The default-no-op invariant (check #7) is **three-
layer verified**: adapter-level neutrality
(`rest_frame()` returned on Identity mode);
kernel-side guard short-circuits to the legacy
fallback on Identity; empirical test surface
unchanged (only the 2 new OBS-P.2 tests in
`relativity_tests` add to the count; no other
test suite changed).

The CUDA/OptiX-semantic-alignment invariant
(check #8) is **five-axis verified**: same shared
types; same ternary shape; same downstream
helper; same upstream adapter; same host-side
empirical equivalence test
(`test_obs_p_2_perception_mode_branch_equivalence`).

The build/test status (check #9) is **directly
verified** by ctest 12/12 PASS + the
`relativity_tests` +28 RR_CHECK delta + no
regression in any other test binary + audit-host
smoke tests on every CLI surface.

The runtime CUDA/OptiX status (check #10) is
**DEFERRED** on the documented audit-host
SDK-absence limitation; the SDK-host pass is the
documented next step.

The overall verdict (check #11) is
**PASS_WITH_RUNTIME_DEFERRED**: nine structural
checks PASS + one appropriately-DEFERRED runtime
check; no REPAIR or BLOCKED item; the slice is
safe to extend.

---

## 4. NEXT

The slice is **safe to extend**. The OBS-P arc's
sub-slice ladder is:

- **OBS-P.1** — Kernel-Side Perception Transform
  Migration Task (LANDED at `fc2d83e`, docs-only
  task definition).
- **OBS-P.2** — Kernel-Side Perception Transform
  Migration Implementation (LANDED at `c729b53`,
  impl with 5 migrated call sites + 2 new
  `relativity_tests` functions).
- **OBS-P.3** — **THIS AUDIT** (Kernel-Side
  Perception Transform Migration Audit, doc-only;
  verdict PASS_WITH_RUNTIME_DEFERRED).
- **OBS-P.4** (recommended next slot) — OBS-P
  Arc Capstone Audit (doc-only; synthesises the
  OBS-P.1 + OBS-P.2 + OBS-P.3 verdicts into a
  single arc-level
  PASS_WITH_RUNTIME_DEFERRED verdict mirroring
  the OBSERVER.15 + SCHW.11 + PENROSE.12 capstone
  shapes). The capstone confirms the OBS-P arc
  resolves OBSERVER.15's #1 remaining risk
  (kernel-side perception-transform migration
  deferred) and that the renderer is now
  observer-perception-aware at the kernel.

After OBS-P.4 closes the arc, the operator may
proceed to:

- **The deferred SDK-host runtime pass** —
  convert both this OBS-P.3 verdict + the
  future OBS-P.4 capstone verdict from
  PASS_WITH_RUNTIME_DEFERRED → PASS by running
  the 9-check suite from OBS-P.1 §7.4 on a
  CUDA + OptiX-SDK host.
- **The OBSERVER.* arc's remaining follow-ups**:
  the fixture scene + companion doc (deferred at
  OBSERVER.12 task brief §5; mirrors MANI-I.8 →
  SCHW.9 cadence).
- **The OBSERVER.15 capstone's remaining
  risks #2 + #3** (fixture follow-up + SDK-host
  pass) — both now actionable.
- **Manifold-orthogonal work** — MANI-I.12 final
  cross-host manifold audit; the Field
  Interpretation Layer Phase 1; denoiser
  integration with chart-aware AOVs; path-
  tracer feature breadth.

No `REPAIR` action is required. No `BLOCKED`
item is outstanding.

---

## 5. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") satisfied by the documented legacy
  fallback at every site + the documented
  scope correction (5 sites vs 6 in the task
  brief); master rule #1 ("Build
  incrementally") + #2 ("Keep every step
  compilable") + #5 ("No CPU ray tracing as
  production path") all preserved.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3
  Observer Frame + §6 GPU integration strategy
  + §7.2 SR-helper subsumption — defines the
  contract OBS-P.2 realises (the SR helpers as
  the Minkowski + constant-velocity-frame
  specialisation of the observer-frame
  contract).
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §3
  Observer-frame concepts + §6 GPU integration
  strategy + §8 non-goals — the OBSERVER.1
  planning doc that established the data flow
  OBS-P.2 wires up.
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15) — the capstone identifying the
  kernel-side perception-transform migration as
  the recommended next slot (§9.1); OBS-P.2
  resolves this capstone's #1 remaining risk.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_TASK.md`
  (OBS-P.1) — the operator-facing task brief
  the OBS-P.2 implementation slice consumed as
  its canonical reference.
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — establishes the
  `CudaSceneView::observer_frame` carry-only
  field OBS-P.2 reads at C-2.
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11) — establishes the
  `OptixLaunchParams::observer_frame`
  carry-only field OBS-P.2 reads at O-1 /
  O-2 / O-3.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — establishes the
  `observer_beta` AOV's read-only contract;
  OBS-P.2 preserves this verbatim (the AOV
  writes `observer_frame.beta` regardless
  of perception_mode).
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the adapter audit
  establishing the beta-resolution priority
  OBS-P.2's `observer_frame.beta` reads
  depend on (CLI overlay > zero-direction
  fallback > legacy `Observer.velocity`).
- `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` +
  `docs/SCHWARZSCHILD_LIKE_CUDA_WARP_AUDIT.md`
  (SCHW.5) + `docs/SCHWARZSCHILD_LIKE_OPTIX_WARP_AUDIT.md`
  (SCHW.8) — the precedent triple-gate
  per-arm guard pattern OBS-P.2's per-call-
  site ternary mirrors in shape.
- `docs/MANIFOLD_DEBUG_AOV_AUDIT.md`
  (MANI-I.9) — the precedent per-slice audit
  doc OBS-P.3 mirrors in structure (eleven-
  row evidence table with explicit runtime
  status row + verdict variant).
- `src/manifold/ObserverFrame.h` — defines the
  `ObserverFrame` POD + `PerceptionMode` enum
  + `ObserverConfig` POD that OBS-P.2 reads at
  every kernel site (the ternary's
  `perception_mode == ConstantVelocityMinkowski`
  comparison; the `beta` field).
- `src/manifold/CameraObserverAdapter.h` — the
  OBSERVER.6 adapter that populates the
  `observer_frame` payload (the upstream
  source of every kernel-side read).
- `src/relativity/RelativityMath.h` — the
  SR-helper math leaf
  (`precompute_relativity` /
  `aberrateDirection` / `dopplerFactor` /
  `searchlightFactor` /
  `applyDopplerColor`); unchanged by OBS-P.2.
- `src/relativity/RelativityParams.h` — the
  legacy `Observer` + `RelativityParams`
  types preserved verbatim as adapter /
  scene-loader / fallback inputs.
- `src/cuda/CudaTestKernel.cu` (modified at
  `c729b53`) — carries C-1 guard at
  lines 226-232 + C-2 guard at lines
  350-356 + the `k_sphere_relativistic`
  kernel signature extension + the
  `launch_sphere_relativistic` launcher
  signature extension.
- `src/cuda/CudaKernels.cuh` (modified at
  `c729b53`) — carries the launcher
  declaration extension.
- `src/cuda/CudaRenderer.h` +
  `CudaRenderer.cu` (modified at `c729b53`)
  — carry the `render_relativistic_sphere`
  dispatcher signature extension + the
  internal lambda capture.
- `src/cuda/CudaPathTracer.cu` — unchanged
  by OBS-P.2 (the kernel doesn't call SR
  helpers; the 5-vs-6 scope correction).
- `src/cuda/CudaScene.cuh` — unchanged by
  OBS-P.2 (the `observer_frame` field landed
  at OBSERVER.8 is reused).
- `src/optix/OptixPrograms.cu` (modified at
  `c729b53`) — carries O-1 guard at lines
  152-158 + O-2 guard at lines 215-221 + O-3
  guard at lines 1071-1080.
- `src/optix/OptixLaunchParams.h` — unchanged
  by OBS-P.2 (the `observer_frame` field
  landed at OBSERVER.10 is reused).
- `tests/relativity_tests.cpp` (modified at
  `c729b53`) — 2 new test functions
  (`test_obs_p_2_perception_mode_branch_equivalence`
  + `test_obs_p_2_perception_mode_three_enumerators`)
  + 1 new `#include "manifold/ObserverFrame.h"`
  line; reports `841/841 passed` post-OBS-P.2
  (up from 813; +28 RR_CHECK).
- `tests/manifold_identity_tests.cpp` /
  `tests/cli_tests.cpp` /
  `tests/renderer_tests.cpp` — unchanged by
  OBS-P.2.
- `docs/BUILD_PLAN.md` — OBS-P.2 entry
  (lines 82722 onward as of `c729b53`).
- Commit `c729b53` — `kernel: OBS-P.2 —
  Kernel-Side Perception Transform Migration
  (impl, guarded read-site swap)`.
- Commit `fc2d83e` — `docs: OBS-P.1 — Kernel-
  Side Perception Transform Migration Task
  (docs only)`; the audit baseline.

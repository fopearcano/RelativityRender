# OptiX Observer Payload Audit (OBSERVER.11)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `977ff73` ("optix:
OBSERVER.10 — OptiX Observer Payload Bridge (impl,
OptiX-side carry-only)").
Audit baseline: `e5fe441` ("docs: OBSERVER.9 — CUDA
Observer Payload Audit (docs only)") — the last
commit before OBSERVER.10 landed.
Audit host: linux, audit-host build (no CUDA SDK,
no OptiX SDK).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, `git diff` against
the post-OBSERVER.9 baseline, the
`manifold_identity_tests` runtime output, the
`cli_tests` runtime output, `ctest` exit codes, and
audit-host smoke-test transcripts for the two new
OptiX-dispatcher log lines.

This audit is the per-slice gate for OBSERVER.10
(`977ff73`). It verifies the ten items the task
brief enumerates — OptiX observer payload exists if
needed; ObserverFrame-derived values reach OptiX
launch params; semantics match CUDA observer
payload; default observer is no-op; beta = 0
preserves current behaviour; no observer perception
transform added yet; CUDA path unchanged except
shared types if needed; OptiX OFF build remains
valid; runtime CUDA/OptiX status; verdict — and
produces a `PASS` / `REPAIR` / `BLOCKED` verdict
that gates progression to the renumbered
OBSERVER.12 (observer debug AOV) or to the
kernel-read wiring slice.

---

## 1. VERDICT

**PASS.**

All eight structural checks return `PASS`. Check
#9 (runtime CUDA/OptiX status) is `DEFERRED` on
the documented audit-host limitation (no CUDA SDK,
no OptiX SDK; `RR_ENABLE_CUDA` and
`RR_ENABLE_OPTIX` both OFF so the corresponding
translation units never compile here). Check #10
(overall verdict) is `PASS`: the structural
plumbing is complete on both backends
(OBSERVER.8 CUDA + OBSERVER.10 OptiX), the no-op-
by-default invariant is verified at audit-host
smoke tests for both `--render-aovs` (CUDA) and
`--render-optix-aovs` / `--render-optix-pathtrace`
(OptiX), kernels are untouched on both backends,
CUDA is untouched by OBSERVER.10, and the runtime
SDK verification is the documented expected gate
for the next OBSERVER.* impl slice (per the
SCHW.11 + PENROSE.12 + OBSERVER.9 capstone
precedents). No `REPAIR` or `BLOCKED` item is
outstanding. The operator may proceed to the next
OBSERVER.* slot under the renumbered ladder per §4
below.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | OptiX observer payload exists if needed  | **PASS** | The OBSERVER.10 commit (`977ff73`) adds the `rr::manifold::ObserverFrame` payload at three OptiX-facing attachment points, with documented sibling-field / trailing-defaulted-parameter placement per the SCHW.7 / MANI-I.5 precedents:<br>**(a)** `OptixLaunchParams::observer_frame{}` at `src/optix/OptixLaunchParams.h:431` — the device-visible launch-argument POD; sibling of `manifold_mode` (line 361) + `coordinate_chart` (line 391).<br>**(b)** `OptixRenderer::render_aovs(..., observer_frame = {})` trailing-defaulted parameter at `src/optix/OptixRenderer.h:482` — the AOV-dispatch entry point; sibling of the existing `manifold_mode = {}` + `coordinate_chart = {}` trailing defaults (lines 455-456).<br>**(c)** `OptixRenderer::render_pathtrace_progressive(..., observer_frame = {})` trailing-defaulted parameter at `src/optix/OptixRenderer.h:327` — the path-trace dispatch entry point; sibling of the existing `manifold_mode = {}` trailing default (line 311).<br>The OBSERVER.4 `ObserverConfig` POD (carrying the four CLI-driven fields `beta_magnitude` / `direction` / `proper_time` / `perception_mode`) is the upstream config bag the OBSERVER.6 adapter consumes; the three OptiX-facing attachment points carry the resulting `ObserverFrame` POD (the seven-field structure landed at MANIFOLD.3 + OBSERVER.2). No new OptiX-specific observer POD is introduced — the same `rr::manifold::ObserverFrame` type travels through the OBSERVER.4 CLI path, the OBSERVER.6 adapter output, the OBSERVER.8 CUDA launch boundary, and now the OBSERVER.10 OptiX launch boundary (single-source-of-truth POD; structural consistency with the CUDA-side OBSERVER.8 surface). All three doc-comments explicitly state the OptiX programs do NOT read the field this slice. |
| 2 | ObserverFrame-derived values reach OptiX launch params | **PASS** | Two documented data paths from the operator's CLI surface into the OptiX launch boundary:<br>**(a) OptiX AOV path** (`run_render_optix_aovs` at `src/main.cpp:2281-2285`): the dispatcher invokes the OBSERVER.6 adapter `build_observer_frame_from_camera(scene.camera.to_gpu(), scene.observer, cfg.observer)` and assigns the result to a local `optix_observer_frame`; passes it as the new trailing argument to `OptixRenderer::render_aovs(...)`. The `render_aovs` implementation (`src/optix/OptixRenderer.cpp:2785`) threads the value into the launch-params POD via `params.observer_frame = observer_frame;` next to the existing `params.manifold_mode = manifold_mode;` / `params.coordinate_chart = coordinate_chart;` assignment block.<br>**(b) OptiX path-trace path** (`run_render_optix_pathtrace` at `src/main.cpp:1708-1712`): the dispatcher invokes the adapter and passes the result as the new trailing argument to `OptixRenderer::render_pathtrace_progressive(...)`. The implementation (`src/optix/OptixRenderer.cpp:1827`) threads the value into the per-spp launch-params write via `params.observer_frame = observer_frame;` next to the existing `params.manifold_mode = manifold_mode;` line. The path-trace progressive loop writes the launch params POD once per sample; the `observer_frame` field rides every per-sample launch.<br>**(c) Host-side echo logs** for operator visibility: one new `Logger::info("optix-aovs observer config: " + format_observer_config_brief(cfg.observer))` line in `run_render_optix_aovs` (`src/main.cpp:2191`) fires BEFORE the `RELATIVITYRENDER_ENABLE_OPTIX` guard (mirroring MANI-CONSUME.1's manifold-mode placement); one new `Logger::info(std::string("observer         : ") + format_observer_config_brief(cfg.observer))` line in `run_render_optix_pathtrace` (`src/main.cpp:1693-1694`) fires BEFORE the renderer is invoked so audit-host smoke tests see it.<br>Verified at audit-host smoke tests: `--render-optix-aovs --observer-beta 0.3 --observer-direction 0,1,0 --observer-perception-mode relativistic --observer-proper-time 7.0` produces `optix-aovs observer config: constant-velocity-minkowski (|beta|=0.300000, dir=[0.000000, 1.000000, 0.000000], tau=7.000000)`. Default `--render-optix-aovs` produces `optix-aovs observer config: identity (no-op)`. Both fire before the existing `--render-optix-aovs requires OptiX` error path. |
| 3 | Semantics match CUDA observer payload    | **PASS** | Four-axis CUDA / OptiX semantic equivalence:<br>**(a) Same shared type.** Both backends carry the same `rr::manifold::ObserverFrame` POD (single-source-of-truth — no backend-specific re-encoding). `CudaSceneView::observer_frame{}` at `src/cuda/CudaScene.cuh:137` and `OptixLaunchParams::observer_frame{}` at `src/optix/OptixLaunchParams.h:431` both declare the field at the same type with the same default-constructed value.<br>**(b) Same upstream adapter.** Both backends invoke the same OBSERVER.6 helper `rr::manifold::build_observer_frame_from_camera(GpuCamera, Observer, ObserverConfig)` with byte-identical arguments. CUDA side: `main.cpp:4262-4265` (`scene.camera.to_gpu()`, `scene.observer`, `cfg.observer`). OptiX side: `main.cpp:2281-2284` (same three arguments; identical adapter inputs).<br>**(c) Same dispatcher-merge precedent.** Both backends thread the adapter output through the dispatcher → launch-arg → launch-params chain with one-line assignments mirroring the SCHW.5 / SCHW.7 / MANI-I.5 precedent verbatim. CUDA: `targets.observer_frame = ...` → `view.observer_frame = targets.observer_frame` (`CudaRenderer.cu:311`). OptiX: trailing-defaulted `observer_frame = {}` parameter → `params.observer_frame = observer_frame` (`OptixRenderer.cpp:1827` + `:2785`).<br>**(d) Same operator-visible log shape.** Both backends emit a `format_observer_config_brief(cfg.observer)` line BEFORE their respective host-guard so audit-host smoke tests see the same operator-selected state regardless of which backend is invoked. The format produced by `format_observer_config_brief` is identical across the two backends because both call the same `src/main.cpp:198` helper. The byte-equivalence of the two log payloads was verified at the audit-host smoke tests (the `--observer-beta 0.5 --observer-direction 1,0,0 ...` invocation produces the same `... constant-velocity-minkowski (|beta|=0.500000, dir=[...], tau=...)` shape on both `--render-aovs` and `--render-optix-aovs`).<br>The carry-only contract is also semantically identical: both backends explicitly document (in source comments + the OBSERVER.8 / OBSERVER.10 BUILD_PLAN entries) that no kernel reads the field this slice, and that a single future slice will gate kernel-side reads on the same `perception_mode` enum on BOTH backends. The kernel-read wiring slice will therefore have a single deterministic activation gate (`perception_mode == ConstantVelocityMinkowski`) across CUDA + OptiX — preserving the cross-backend byte-equivalence the SCHW.* / PENROSE.* arcs established. |
| 4 | Default observer is no-op                | **PASS** | Four-layer no-op preservation on the OptiX side (mirrors the OBSERVER.9 audit's CUDA-side proof):<br>**(a) Default `Config::observer`** is `ObserverConfig{}` (per-field initialisers; verified at OBSERVER.5 audit check #2).<br>**(b) Default `cfg.observer` → adapter call → `ObserverFrame{}` byte-for-byte.** The OBSERVER.6 adapter's `Identity` perception-mode branch returns `rest_frame()` byte-for-byte; verified at OBSERVER.7 audit check #2.<br>**(c) Default `ObserverFrame{}` propagation through the OptiX launch boundary.** Both `OptixRenderer::render_aovs` and `OptixRenderer::render_pathtrace_progressive` accept the default-constructed `observer_frame = {}` argument when the caller doesn't pass anything (the `= {}` default-value placement preserves API compatibility for every existing call site); the launch-params POD then carries the no-op anchor (`perception_mode=Identity`, `beta=0`, world-basis tetrad, both times=0) into the OptiX kernel via `params.observer_frame = observer_frame`. The OptiX programs do NOT read the field this slice (operator brief contract); even if they did, the no-op anchor is the bit-identical baseline.<br>**(d) Default-off byte-identity at the audit-host smoke test.** `--render-optix-aovs` without any `--observer-*` flag produces `optix-aovs observer config: identity (no-op)` — the operator-visible log line confirms the no-op anchor fires by default. The existing `optix-aovs manifold mode: disabled (...)` log line fires first, then the new observer log, then the existing `--render-optix-aovs requires OptiX` error path on audit host. The output sequence is additive — no existing line is rewritten or removed. |
| 5 | beta = 0 preserves current behaviour     | **PASS** | Three-layer beta=0 preservation on the OptiX side (mirrors OBSERVER.9 audit check #4 for CUDA):<br>**(a) Adapter-level (carry-forward from OBSERVER.7 audit).** For `Identity` mode the adapter returns `rest_frame()` (beta=0). For `ConstantVelocityMinkowski` with `cfg.observer.beta_magnitude == 0`, the beta-resolution priority falls through to `observer.velocity` (default `(0,0,0)`); the resulting `frame.beta == (0,0,0)`. Verified at `test_observer_6_constant_velocity_zero_beta` (`tests/manifold_identity_tests.cpp:1754`).<br>**(b) Launch-payload byte-equivalence.** The resulting `ObserverFrame` with `beta=0` carries through `optix_observer_frame` / `optix_pt_observer_frame` (local vars in main.cpp) → trailing-defaulted argument → `params.observer_frame` unchanged (per-field copy semantics; trivially-copyable POD). The OptiX programs, even if they did read the field this slice, would see `beta == (0,0,0)`.<br>**(c) OptiX programs ignore the field.** The OptiX kernel sources (`OptixPrograms.cu`) do NOT read `optixLaunchParams.observer_frame`. The existing `__raygen__` / `__miss__` / `__closesthit__` programs continue to feed on `optixLaunchParams.observer.velocity` (the legacy SR observer; the pre-OBSERVER.10 path) for any aberration / Doppler / searchlight effects. Any `cfg.observer.beta_magnitude == 0` AND `scene.observer.velocity == (0,0,0)` invocation produces byte-identical OptiX output to today's renderer.<br>The combined invariant: `beta=0` on EITHER the CLI overlay OR the legacy SR observer preserves byte-identity on the OptiX side because (i) the adapter resolves to `beta=(0,0,0)` regardless of source path (same as CUDA), (ii) the launch-params carries the resulting zero through unchanged, and (iii) the OptiX programs read the legacy types (unchanged) and ignore the new field (carry-only contract — identical to the CUDA carry-only contract). |
| 6 | No observer perception transform added yet | **PASS** | Three-layer verification of the "no perception transform" contract on the OptiX side:<br>**(a) OptiX kernel source is byte-unchanged.** `git diff e5fe441..977ff73 --name-only -- 'src/optix/OptixPrograms.cu'` returns **zero hits**. The `__raygen__` / `__miss__` / `__closesthit__` programs all still read from the legacy `optixLaunchParams.observer.velocity` (the pre-OBSERVER.10 baseline). The kernel-side aberration / Doppler / searchlight helpers (where applicable) are invoked with the legacy `observer` argument exactly as today.<br>**(b) No SBT record / payload read changed.** The OptiX SBT (Shader Binding Table) layout in `OptixSBT.h` is unchanged (`git diff e5fe441..977ff73 --name-only -- 'src/optix/OptixSBT.h'` returns zero hits). The payload read pattern is unchanged.<br>**(c) Perception-mode tag is dormant.** The adapter produces an `ObserverFrame` whose `perception_mode` field is set per the `cfg.observer.perception_mode` (the operator-selected mode); the field travels through the OptiX launch boundary on `params.observer_frame.perception_mode` AND `optixLaunchParams.observer_frame.perception_mode` (the device-side view of the same POD) BUT no OptiX program reads it. The legacy SR helpers continue to be invoked unconditionally for any non-zero scene-observer-velocity input (the pre-OBSERVER.10 behaviour on the OptiX side). Setting `--observer-perception-mode relativistic` does NOT yet engage any OptiX kernel-side gate; the log fires + the field is carried + the OptiX programs ignore it. |
| 7 | CUDA path unchanged except shared types if needed | **PASS** | `git diff e5fe441..977ff73 --name-only -- 'src/cuda/' 'src/pathtracer/' 'src/manifold/'` returns **zero hits** outside the docs/build-plan. The OBSERVER.10 commit modifies only `src/optix/` + `src/main.cpp` + `docs/BUILD_PLAN.md`. Specifically:<br>**(a) CUDA backends untouched.** `src/cuda/CudaScene.cuh`, `src/cuda/CudaRenderer.h`, `src/cuda/CudaRenderer.cu`, `src/cuda/CudaTestKernel.cu`, `src/cuda/CudaPathTracer.cuh`, `src/cuda/CudaPathTracer.cu` — all byte-identical to the post-OBSERVER.9 baseline (the OBSERVER.8 CUDA-side work is preserved exactly).<br>**(b) Path-tracer host header untouched.** `src/pathtracer/PathTracer.h` — byte-identical (the OBSERVER.8 `pcfg.observer_frame` field is preserved).<br>**(c) Manifold module untouched.** `src/manifold/CameraObserverAdapter.h`, `src/manifold/ObserverFrame.h` — both byte-identical (the OBSERVER.6 adapter + OBSERVER.4 `ObserverConfig` + OBSERVER.2 POD are preserved).<br>**(d) Shared-type consistency.** The `rr::manifold::ObserverFrame` type is consumed by BOTH the CUDA-side `CudaSceneView::observer_frame{}` and the OptiX-side `OptixLaunchParams::observer_frame{}`. The type definition itself (in `src/manifold/ObserverFrame.h`) is byte-identical post-OBSERVER.10 because OBSERVER.10 doesn't extend the POD — it only consumes it. So the "shared type" carve-out from the operator's brief ("Do not change CUDA unless required by shared type consistency") is satisfied trivially — no shared type required updating, so no CUDA file was touched. |
| 8 | OptiX OFF build remains valid            | **PASS** | Audit-host `cmake --build /home/user/RelativityRender/build` succeeds cleanly on the OptiX OFF host (`RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF` per `CMakeLists.txt:22, 38`). The OptiX-gated translation units (`OptixRenderer.cpp`, `OptixLaunchParams.h`'s consumers via `OptixRenderer.cpp` / `OptixPipeline.cpp` / `OptixPrograms.cu`) do NOT compile on this host. The `src/main.cpp` changes ARE compiled on this host because:<br>(a) The new `format_observer_config_brief(cfg.observer)` log lines are inside the `#ifndef RELATIVITYRENDER_ENABLE_OPTIX` branch's PRE-guard region (lines 2183-2191 and 1685-1694) — they execute on every host, regardless of OptiX availability.<br>(b) The new `build_observer_frame_from_camera(...)` adapter invocations + the `OptixRenderer::render_aovs(...)` / `render_pathtrace_progressive(...)` call sites live INSIDE the `#else` branch of the OptiX guard (lines 2262-2284 and 1697-1712), so they only compile on hosts with `RELATIVITYRENDER_ENABLE_OPTIX` defined. On the audit host these blocks are excluded by the preprocessor before code-gen reaches them; the `OptixRenderer::render_aovs(...)` etc. names that don't exist on the audit host (because their header is gated) are never referenced.<br>Full audit-host build output: zero warnings on the core / manifold / cuda / pathtracer modules; the audit-host build's `main.cpp` TU compiles cleanly with the new pre-guard log lines + the gated post-guard adapter calls.<br>Verified at full ctest: `100% tests passed, 0 tests failed out of 12`. All test binaries unchanged from the OBSERVER.9 baseline (`manifold_identity_tests: 408/408`; `cli_tests: 254/254`; `renderer_tests: 19/19`; all others unchanged). The OptiX OFF build produces a binary in which `--render-optix-aovs` and `--render-optix-pathtrace` run into the documented `RELATIVITYRENDER_ENABLE_OPTIX`-required error path immediately after the observer-config + manifold-mode logs fire (audit-host smoke tests verified). |
| 9 | Runtime CUDA / OptiX status              | **DEFERRED** | The audit host has neither CUDA nor OptiX SDK installed (`nvcc` not present; `optixGetVersion` unavailable; `RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF`). Consequently:<br>**(a) CUDA side:** The CUDA-side OBSERVER.8 plumbing (`view.observer_frame = targets.observer_frame` thread inside `CudaRenderer.cu`, which is gated on `RR_ENABLE_CUDA=ON`) cannot be compiled, linked, or device-launched from this host. Runtime CUDA verification is `DEFERRED` to a CUDA-SDK host; this matches the OBSERVER.9 audit's check #8 verdict verbatim (no change in runtime CUDA status from OBSERVER.10 because OBSERVER.10 didn't touch CUDA code).<br>**(b) OptiX side:** The OptiX-side OBSERVER.10 plumbing (`params.observer_frame = observer_frame` inside `OptixRenderer.cpp`'s `render_aovs` + `render_pathtrace_progressive` impls; the new trailing-defaulted parameter on both entry points; the `OptixLaunchParams::observer_frame{}` field on the launch-params POD) cannot be compiled, linked, or device-launched from this host. Runtime OptiX verification is `DEFERRED` to an OptiX-SDK host.<br>**(c) Audit-host CAN verify:** the host-side dispatchers' pre-guard log lines fire correctly (verified); the OBSERVER.6 adapter (host-only header) compiles + tests pass; the `OptixLaunchParams.h` / `OptixRenderer.h` includes are valid C++ headers that don't break audit-host TU compilation downstream (the OptiX-gated translation units that include them don't compile here, but standalone header parsing for documentation tools / IDE indexing remains clean).<br>This is the **same documented deferral** pattern accrued by every prior CUDA / OptiX-touching slice (MANI-I.5 / SCHW.5 / SCHW.7 / PENROSE.6 / PENROSE.8 / MANI-CONSUME.1 / OBSERVER.8 / OBSERVER.10). The SCHW.11 + PENROSE.12 capstones recorded runtime verification as deferred until a CUDA-SDK + OptiX-SDK host runs the full pipeline; OBSERVER.10 is no exception. The deferral is NOT a `BLOCKED` because: (i) the structural plumbing is verified PASS (checks #1-8); (ii) the no-op-by-default invariant is verified at audit-host smoke tests for both backends; (iii) the operator's OBSERVER.10 brief explicitly scopes the slice to "OptiX path only, no kernel behavior change beyond carrying data" — runtime verification would not exercise any new kernel behaviour at this slice anyway (no OptiX program reads the field).<br>**Required runtime checks for a future SDK-host audit pass** (when the operator runs the audit on a CUDA-+-OptiX-equipped host): (a) the audit-host build's `--render-optix-aovs` smoke tests reproduce the same log lines on the SDK host; (b) the `--render-optix-aovs` action produces byte-identical PPM outputs vs. the pre-OBSERVER.10 baseline for the default-observer-config invocation; (c) the `--render-optix-pathtrace` action produces convergence-equivalent checkpoints vs. the pre-OBSERVER.10 baseline; (d) the CUDA + OptiX outputs remain byte-equivalent / convergence-equivalent for the same `cfg.observer` settings (cross-backend equivalence test mirroring the SCHW.11 capstone's cross-backend check). None of these checks exercise new kernel code (per the operator brief); all four are byte-identity / convergence-identity gates verifying the carry-only plumbing did not silently leak into the kernel. |
| 10 | PASS / REPAIR / BLOCKED verdict         | **PASS** | All eight structural checks return `PASS`. Check #9 (runtime CUDA/OptiX) is `DEFERRED` on the documented audit-host SDK-absence limitation (mirrors the OBSERVER.9 / SCHW.5 / PENROSE.6 / MANI-CONSUME.1 deferral pattern). No `REPAIR` or `BLOCKED` item is outstanding. The OBSERVER.10 commit ships the documented three-attachment-point OptiX payload (OptixLaunchParams field + two trailing-defaulted entry-point parameters), the documented dispatcher-side adapter invocations (run_render_optix_aovs + run_render_optix_pathtrace), the documented host-side echo logs (audit-host smoke-verified), and zero behaviour change (OptiX programs untouched, CUDA untouched, runtime SDK verification appropriately DEFERRED). The slice is **safe to extend** to the next OBSERVER.* slot under the renumbered ladder per §4 below. |

---

## 3. REASONING SUMMARY

The OBSERVER.10 commit (`977ff73`) introduces:

- one new sibling-field addition to
  `OptixLaunchParams` at `src/optix/OptixLaunchParams.h:431`
  (matching the post-OBSERVER.8
  `CudaSceneView::observer_frame{}` field on the
  CUDA side);
- two new trailing-defaulted-parameter additions
  on `OptixRenderer` entry points:
  `render_aovs(..., observer_frame = {})` at
  `OptixRenderer.h:482` (mirrors the existing
  SCHW.7 `coordinate_chart = {}` trailing-default
  precedent); `render_pathtrace_progressive(...,
  observer_frame = {})` at `OptixRenderer.h:327`
  (mirrors the existing MANI-I.5 `manifold_mode
  = {}` trailing-default precedent);
- two new `params.observer_frame = observer_frame;`
  assignments inside the corresponding
  `OptixRenderer.cpp` implementations
  (`render_pathtrace_progressive` at line 1827;
  `render_aovs` at line 2785) — both placed as
  sibling statements next to the existing
  `params.manifold_mode = manifold_mode;`
  assignments;
- two new dispatcher-side adapter invocations in
  `src/main.cpp` (`run_render_optix_aovs` AOV
  path at line 2281; `run_render_optix_pathtrace`
  path-trace path at line 1708) calling the
  OBSERVER.6 `build_observer_frame_from_camera(...)`
  helper with the active camera + legacy SR
  observer + CLI ObserverConfig;
- two new `Logger::info` lines (the
  `optix-aovs observer config` line at
  `main.cpp:2191` and the `observer
  : ...` line at `main.cpp:1693-1694`) that
  mirror the existing manifold-mode log placement
  + the CUDA-side OBSERVER.8 log shape.

The optix-observer-payload-exists invariant
(check #1) is **three-attachment-point verified**
at documented file / line positions; all three
sibling-field / trailing-defaulted-parameter
placements mirror the SCHW.7 / MANI-I.5
precedents verbatim.

The values-reach-launch-params invariant (check
#2) is **two-data-path verified**: the OptiX AOV
path threads the adapter output through the
trailing-default argument into `params.observer_frame`;
the OptiX path-trace path threads through the same
mechanism for the progressive per-spp loop; the
host-side echo logs fire on every host (audit-
host smoke-test verified).

The semantics-match-CUDA invariant (check #3) is
**four-axis verified**: same shared type
(`rr::manifold::ObserverFrame`); same upstream
adapter (`build_observer_frame_from_camera`);
same dispatcher-merge precedent (one-line
assignment mirroring SCHW.5 / SCHW.7 / MANI-I.5);
same operator-visible log shape
(`format_observer_config_brief(cfg.observer)`
on both backends). The carry-only contract is
also identical — both backends document that no
kernel reads the field this slice.

The default-observer-no-op invariant (check #4) is
**four-layer verified**: the default
`ObserverConfig` is the no-op anchor; the
adapter's Identity path returns `rest_frame()`
byte-for-byte; the OptiX launch boundary carries
the no-op anchor; the audit-host smoke test
verifies the log fires correctly by default
(producing `optix-aovs observer config: identity
(no-op)`).

The beta=0-preserves-behaviour invariant (check
#5) is **three-layer verified**: the adapter
produces beta=(0,0,0) on every zero-beta input
path; the launch-params carries the zero through
unchanged; the OptiX programs read the legacy
types (unchanged) and ignore the new field
(carry-only contract — semantically identical
to the CUDA carry-only contract).

The no-perception-transform invariant (check #6)
is **three-layer verified**: the OptiX kernel
source `OptixPrograms.cu` is byte-unchanged
(`git diff` returns zero hits); the SBT layout
is unchanged; the perception_mode tag is carried
but dormant (no OptiX program gate reads it).

The cuda-unchanged invariant (check #7) is
**directly verified** by `git diff --name-only
-- 'src/cuda/' 'src/pathtracer/' 'src/manifold/'`
returning zero hits. The CUDA-side OBSERVER.8
plumbing is preserved verbatim; the shared
`rr::manifold::ObserverFrame` type required no
update for OBSERVER.10 (the operator's
"shared types if needed" carve-out is satisfied
trivially).

The optix-off-build invariant (check #8) is
**directly verified** by audit-host `cmake
--build` succeeding cleanly with no warnings;
the new pre-guard log lines execute on every
host; the post-guard adapter calls + OptiX
entry-point invocations are properly gated by
`#ifndef RELATIVITYRENDER_ENABLE_OPTIX` blocks.

The runtime CUDA/OptiX status (check #9) is
**DEFERRED** on the documented audit-host
SDK-absence limitation; the same deferral
pattern accrued for every prior CUDA / OptiX-
touching slice. The SDK-host pass is the
documented next step for the OBSERVER.* arc
capstone audit.

The overall verdict (check #10) is **PASS**:
eight structural checks PASS + one appropriately-
DEFERRED runtime check; no REPAIR or BLOCKED
item; the slice is safe to extend.

---

## 4. NEXT

The slice is **safe to extend**. The renumbered
`OBSERVER_FRAME_RENDERING_PLAN.md` §7 OBSERVER.*
sub-slice ladder needs a one-step shift to absorb
this audit slot, mirroring the OBSERVER.3 +
OBSERVER.5 + OBSERVER.7 + OBSERVER.9 audit-slot
insertion precedent:

- **OBSERVER.1** — Planning slice
  (LANDED at `eee9d6b`).
- **OBSERVER.2** — Data model
  (LANDED at `85496a5`).
- **OBSERVER.3** — Data model audit
  (LANDED at `bf57c9e`).
- **OBSERVER.4** — Config / CLI bridge
  (LANDED at `16600dc`).
- **OBSERVER.5** — Config / CLI bridge audit
  (LANDED at `27ec0d9`).
- **OBSERVER.6** — Camera-to-observer adapter
  (LANDED at `e2cde15`).
- **OBSERVER.7** — Camera-to-observer adapter
  audit (LANDED at `a0215c0`).
- **OBSERVER.8** — CUDA observer payload bridge
  (LANDED at `12f4942`).
- **OBSERVER.9** — CUDA observer payload audit
  (LANDED at `e5fe441`).
- **OBSERVER.10** — OptiX observer payload bridge
  (LANDED at `977ff73`).
- **OBSERVER.11** — **THIS AUDIT** (OptiX
  Observer Payload Audit, doc-only).
- **OBSERVER.12** — Observer debug AOV (was
  OBSERVER.10 in the post-OBSERVER.7 plan,
  renumbered through OBSERVER.9 + OBSERVER.11
  audit insertions).
- **OBSERVER.13** — Arc capstone audit (was
  OBSERVER.11); closes the observer-frame arc
  per the OBSERVER.1 plan §7.

The
`docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7
sub-slice ladder may be updated by a follow-on
docs slice if the operator prefers an in-plan
renumbering; this audit doc is the canonical
ladder-shift record for the OBSERVER.11
audit-slot insertion.

No `REPAIR` action is required. No `BLOCKED` item
is outstanding. The next concrete commit the
operator may prompt for is **OBSERVER.12 —
Observer debug AOV** per the renumbered
OBSERVER.1 plan §7 OBSERVER.7 → OBSERVER.12.
That slice will add a debug AOV visualising the
resolved observer-frame state per pixel (the
`ObserverFrameDirection` AOV per the OBSERVER.1
plan §7); add the device-pointer slot to
`DeviceAOVView` + `AOVTargets` +
`OptixLaunchParams::aov_observer_frame_direction`;
add CUDA + OptiX kernel arms that write the
boosted primary-ray direction in tetrad-local
coordinates per hit pixel; add host dispatcher
allocation gated on `--observer-debug` (parallel
to SCHW.7's `--manifold-debug` gate); add a
fixture scene + companion doc documenting the
expected visual signature.<br>**Note** that the
kernel-side reads of `view.observer_frame.beta`
/ `params.observer_frame.beta` for the
non-debug AOV / path-trace pipelines remain
deferred — OBSERVER.12 is scoped to the debug
AOV; the main aberration / Doppler /
searchlight pipeline migration is a separate
future slice (likely OBSERVER.13 or beyond).
The operator may also choose to land the
non-debug kernel-read wiring first; the
OBSERVER.10 audit verdict authorises either
ordering.

---

## 5. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") is the load-bearing invariant for the
  reserved-but-not-yet-consumed
  `OptixLaunchParams::observer_frame` field being
  acceptable (structurally consumed by the
  dispatcher invocations + the echo logs + the
  planned kernel-read wiring in a future slice).
  Master rule #1 ("Build incrementally") + #2
  ("Keep every step compilable") satisfied: ctest
  12/12 PASS, zero behaviour change, zero kernel
  touch, audit-host OptiX OFF build green.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3
  Observer Frame + §6 GPU integration strategy —
  defines the contract the OptiX launch boundary
  carries.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §6, §7
  OBSERVER.10 (renumbered from the original §7
  OBSERVER.6 after the OBSERVER.3 + OBSERVER.5 +
  OBSERVER.7 + OBSERVER.9 audit-slot insertions)
  — the OBSERVER.1 plan brief that authorised
  the OptiX payload bridge.
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — the precedent CUDA-side payload
  audit; this OBSERVER.11 audit mirrors its
  structure verbatim with OptiX-side evidence.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the camera-to-observer
  adapter's audit; carry-forward of the
  three-mode construction guarantee.
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5) — the upstream CLI bridge's
  audit; carry-forward of the default-no-op
  invariant.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the underlying `ObserverFrame`
  POD's structural audit; carry-forward of the
  POD invariants the launch boundary now
  propagates to OptiX.
- `docs/PENROSE_LIKE_OPTIX_INTEGRATION_AUDIT.md`
  (PENROSE.9) — the precedent OptiX-side
  integration audit doc this verdict mirrors in
  structure (sibling field placement + trailing-
  defaulted parameter + diff scope + runtime-
  OptiX-DEFERRED on audit host).
- `docs/SCHWARZSCHILD_LIKE_OPTIX_WARP_AUDIT.md`
  (SCHW.8) — the precedent OptiX-side completion
  audit; established the PASS-with-DEFERRED-
  runtime pattern this audit follows.
- `docs/MANIFOLD_CONSUMPTION_GAP_AUDIT.md`
  (MANI-CONSUME.2) — the precedent for the
  "log fires before the OptiX guard so audit-
  host smoke tests see it" pattern that the
  OBSERVER.10 `optix-aovs observer config` log
  follows.
- `src/optix/OptixLaunchParams.h` (modified at
  `977ff73`) — carries the new `observer_frame`
  field on `OptixLaunchParams` at line 431.
- `src/optix/OptixRenderer.h` (modified at
  `977ff73`) — carries the new trailing-defaulted
  `observer_frame = {}` parameter on
  `render_aovs(...)` (line 482) +
  `render_pathtrace_progressive(...)` (line 327).
- `src/optix/OptixRenderer.cpp` (modified at
  `977ff73`) — carries the two new
  `params.observer_frame = observer_frame;`
  assignments at lines 1827 + 2785.
- `src/main.cpp` (modified at `977ff73`) —
  carries the two new dispatcher-side adapter
  invocations at lines 2281 + 1708; the two new
  observer-config log lines at lines 2191 +
  1693-1694.
- `src/manifold/CameraObserverAdapter.h` —
  the OBSERVER.6 adapter the new dispatcher
  invocations call; unchanged by OBSERVER.10.
- `src/manifold/ObserverFrame.h` — the
  underlying POD; unchanged by OBSERVER.10.
- `src/cuda/CudaScene.cuh`, `src/cuda/CudaRenderer.h`,
  `src/cuda/CudaRenderer.cu`, `src/pathtracer/PathTracer.h`
  — all unchanged by OBSERVER.10 (check #7
  directly verified); the OBSERVER.8 CUDA-side
  plumbing is preserved verbatim.
- `src/optix/OptixPrograms.cu` — unchanged by
  OBSERVER.10 (check #6 directly verified); no
  OptiX program reads the new field.
- `src/optix/OptixSBT.h` — unchanged (SBT layout
  preserved).
- `tests/manifold_identity_tests.cpp` —
  unchanged by OBSERVER.10; reports
  `408/408 checks passed`.
- `tests/cli_tests.cpp` — unchanged by
  OBSERVER.10; reports `254/254 passed`.
- `tests/renderer_tests.cpp` — unchanged by
  OBSERVER.10; reports `19/19 passed`.
- `docs/BUILD_PLAN.md` — OBSERVER.10 entry
  (lines 80647 onward as of `977ff73`).
- Commit `977ff73` — `optix: OBSERVER.10 —
  OptiX Observer Payload Bridge (impl,
  OptiX-side carry-only)`.
- Commit `e5fe441` — `docs: OBSERVER.9 — CUDA
  Observer Payload Audit (docs only)`; the
  audit baseline.

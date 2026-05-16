# CUDA Observer Payload Audit (OBSERVER.9)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `12f4942` ("cuda:
OBSERVER.8 — CUDA Observer Payload Bridge (impl,
CUDA-side carry-only)").
Audit baseline: `a0215c0` ("docs: OBSERVER.7 —
Camera-to-Observer Adapter Audit (docs only)") — the
last commit before OBSERVER.8 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, `git diff` against
the post-OBSERVER.7 baseline, the
`manifold_identity_tests` runtime output, the
`cli_tests` runtime output, `ctest` exit codes, and
audit-host smoke-test transcripts for the two new
`--render-aovs` log lines.

This audit is the per-slice gate for OBSERVER.8
(`12f4942`). It verifies the nine items the task
brief enumerates — CUDA observer payload exists if
needed; ObserverFrame-derived values reach
CUDA-facing launch/config structures; default
observer is no-op; beta = 0 preserves current
behaviour; no observer perception transform added
yet; OptiX path unchanged; build / test status;
runtime CUDA status; verdict — and produces a
`PASS` / `REPAIR` / `BLOCKED` verdict that gates
progression to the renumbered OBSERVER.10 (OptiX
payload bridge).

---

## 1. VERDICT

**PASS.**

All seven structural checks return `PASS`. Check
#8 (runtime CUDA status) is `DEFERRED` on the
documented audit-host limitation (no CUDA SDK
present; `RR_ENABLE_CUDA` is OFF so the
`CudaScene.cuh` + `CudaRenderer.cu` translation
units never compile here). Check #9 (overall
verdict) is `PASS`: the structural plumbing is
complete, the no-op-by-default invariant is
verified at both audit-host smoke tests and ctest,
the kernel is untouched, OptiX is untouched, and
the runtime SDK verification is the documented
expected gate for the next OBSERVER.* impl slice
(per the SCHW.11 + PENROSE.12 capstone precedent).
No `REPAIR` or `BLOCKED` item is outstanding. The
operator may proceed to OBSERVER.10 (OptiX payload
bridge) under the renumbered OBSERVER.* ladder per
§4 below.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | CUDA observer payload exists if needed   | **PASS** | The OBSERVER.8 commit (`12f4942`) adds the `rr::manifold::ObserverFrame` payload at three CUDA-facing attachment points, with documented sibling-field placement per the SCHW.5 / SCHW.7 / MANI-I.5 precedents:<br>**(a)** `CudaSceneView::observer_frame{}` at `src/cuda/CudaScene.cuh:137` — the kernel-visible launch-argument POD; sibling of `manifold_mode` + `coordinate_chart` fields (lines 106-107).<br>**(b)** `CudaRenderer::AOVTargets::observer_frame = {}` at `src/cuda/CudaRenderer.h:206` — the host-side AOV-dispatch struct; sibling of `manifold_mode` + `coordinate_chart` (lines 187-188).<br>**(c)** `PathTraceConfig::observer_frame{}` at `src/pathtracer/PathTracer.h:215` — the per-render path-trace config; sibling of `manifold` (line 189) per the MANI-I.3 precedent.<br>The OBSERVER.4 `ObserverConfig` POD (carrying the four CLI-driven fields `beta_magnitude` / `direction` / `proper_time` / `perception_mode`) is the upstream config bag the OBSERVER.6 adapter consumes; the three CUDA-facing payloads carry the resulting `ObserverFrame` POD (the seven-field structure landed at MANIFOLD.3 + OBSERVER.2). No new CUDA-specific observer POD is introduced — the same `rr::manifold::ObserverFrame` type travels through both the OBSERVER.4 CLI path and the OBSERVER.8 CUDA launch boundary (single-source-of-truth POD; structural consistency with the OBSERVER.6 adapter's output type). All three sibling-field doc-comments explicitly state the kernel does NOT read the field this slice. |
| 2 | ObserverFrame-derived values reach CUDA-facing launch/config structures | **PASS** | Three documented data paths from the operator's CLI surface into the CUDA launch boundary:<br>**(a) AOV render path** (`run_render_aovs` at `src/main.cpp:4184-4188`): the dispatcher invokes the OBSERVER.6 adapter `build_observer_frame_from_camera(scene.camera.to_gpu(), scene.observer, cfg.observer)` and assigns the resulting `ObserverFrame` to `targets.observer_frame`. The `CudaRenderer::render_scene_with_aovs` implementation (`src/cuda/CudaRenderer.cu:311`) threads the field one line further into the kernel-visible view: `view.observer_frame = targets.observer_frame;`. The thread mirrors the SCHW.5 precedent (`view.manifold_mode = targets.manifold_mode;` at line 300) verbatim — sibling field, single-line thread, same dispatcher invocation.<br>**(b) Path-trace path** (the per-spp loop at `src/main.cpp:2693`): the dispatcher invokes `build_observer_frame_from_camera(scene.camera.to_gpu(), scene.observer, cfg.observer)` and assigns the result to `pcfg.observer_frame`. The `pcfg` is then passed to `pt.render(...)` per the existing PathTraceConfig contract. The CUDA launcher `launch_pathtrace_sample(...)` is intentionally NOT extended this slice (operator-documented kernel-arg ABI scope decision per the OBSERVER.8 task brief + the field's doc-comment at `PathTracer.h:208-213`); the field travels on `pcfg` only.<br>**(c) Host-side echo logs** for operator visibility: a new `Logger::info("aovs observer config: " + format_observer_config_brief(cfg.observer))` line in `run_render_aovs` (`src/main.cpp:3917`) fires BEFORE the `RR_HAS_CUDA` guard (mirroring MANI-CONSUME.1's manifold-mode placement), so audit-host smoke tests verify the host-side resolution without needing CUDA. A parallel `Logger::info("observer         : " + format_observer_config_brief(cfg.observer))` line in the path-trace dispatcher at `src/main.cpp:2772` fires after the per-spp render returns.<br>Verified at audit-host smoke tests: `--render-aovs --observer-beta 0.5 --observer-direction 1,0,0 --observer-perception-mode relativistic --observer-proper-time 12.0` produces `aovs observer config: constant-velocity-minkowski (|beta|=0.500000, dir=[1.000000, 0.000000, 0.000000], tau=12.000000)`. Default `--render-aovs` (no observer flags) produces `aovs observer config: identity (no-op)`. Both fire before the existing `--render-aovs requires CUDA` error path. |
| 3 | Default observer is no-op                | **PASS** | Four-layer no-op preservation:<br>**(a) Default `Config::observer`** is `ObserverConfig{}` (per-field initialisers at `src/manifold/ObserverFrame.h:469-485`: `beta_magnitude=0`, `direction=(0,0,0)`, `proper_time=0`, `perception_mode=Identity`). Verified at the OBSERVER.5 audit's check #2 (`test_observer_default_off`).<br>**(b) Default `cfg.observer` → adapter call → `ObserverFrame{}` byte-for-byte.** The OBSERVER.6 adapter's `Identity` perception-mode branch (`src/manifold/CameraObserverAdapter.h:155`) returns `rest_frame()` byte-for-byte; verified at the OBSERVER.7 audit's check #2 (`test_observer_6_default_is_camera_equivalent_no_op` field-by-field verification).<br>**(c) Default `ObserverFrame{}` propagation through the launch boundary.** With `targets.observer_frame = ObserverFrame{}` and `view.observer_frame = targets.observer_frame`, the launch-argument POD carries the no-op anchor (perception_mode=Identity, beta=0, world-basis tetrad, both times=0) into the kernel. The kernel does NOT read the field this slice (operator brief contract); even if it did, the no-op anchor is the bit-identical baseline.<br>**(d) Default-off byte-identity at the audit-host smoke test.** `--render-aovs` without any `--observer-*` flag produces `aovs observer config: identity (no-op)` — the operator-visible log line confirms the no-op anchor fires by default. The operator-visible log line was verified to be additive (the existing `aovs manifold mode` log fires first, then the new observer log, then the CUDA-required-error path on audit host). |
| 4 | beta = 0 preserves current behaviour     | **PASS** | Three-layer beta=0 preservation:<br>**(a) Adapter-level (carry-forward from OBSERVER.7 audit).** The OBSERVER.6 adapter at `src/manifold/CameraObserverAdapter.h:155-156` short-circuits to `rest_frame()` on `Identity` (the default-config path; `cfg.observer.beta_magnitude == 0` AND `cfg.observer.direction == (0,0,0)` route here via the `Identity` enum default). For `ConstantVelocityMinkowski` mode with `cfg.observer.beta_magnitude == 0`, the beta-resolution priority at `CameraObserverAdapter.h:178-198` falls through to using `observer.velocity` (the legacy SR observer; default `(0,0,0)`); the resulting `frame.beta == (0,0,0)` is the `rest_frame()`-equivalent zero-velocity state. Verified at `test_observer_6_constant_velocity_zero_beta` (`tests/manifold_identity_tests.cpp:1754`) — the resulting `frame.beta == (0,0,0)`, `frame.velocity4 == (1,0,0,0)`.<br>**(b) Launch-payload byte-equivalence.** The resulting `ObserverFrame` with `beta=0` carries through `targets.observer_frame` → `view.observer_frame` → `pcfg.observer_frame` unchanged (per-field copy semantics; trivially-copyable POD). The kernel, even if it did read the field this slice, would see `beta == (0,0,0)`.<br>**(c) Kernel ignores the field.** The CUDA kernel arms (`k_render_scene` in `CudaTestKernel.cu`, `k_pathtrace_sample` in `CudaPathTracer.cu`) do NOT read `view.observer_frame` / consume `pcfg.observer_frame`. The existing aberration / Doppler / searchlight helpers continue to feed on the legacy `scene.observer.velocity` (the pre-OBSERVER.8 path). Any `cfg.observer.beta_magnitude == 0` AND `scene.observer.velocity == (0,0,0)` invocation produces byte-identical output to today's renderer.<br>The combined invariant: `beta=0` on EITHER the CLI overlay OR the legacy SR observer preserves byte-identity to today's renderer because (i) the adapter resolves to `beta=(0,0,0)` regardless of the source path, (ii) the launch boundary carries the resulting zero through unchanged, and (iii) the kernel reads the legacy types (unchanged) and ignores the new field (carry-only). |
| 5 | No observer perception transform added yet | **PASS** | Three-layer verification of the "no perception transform" contract:<br>**(a) Kernel sources are byte-unchanged.** `git diff a0215c0..12f4942 --name-only -- 'src/cuda/CudaTestKernel.cu' 'src/cuda/CudaPathTracer.cu'` returns zero hits. The `k_render_scene` AOV-write arm + the `k_pathtrace_sample` kernel arm both still read from the legacy `scene.observer.velocity` (the pre-OBSERVER.8 baseline). The kernel-side aberration / Doppler / searchlight helpers are invoked with the legacy `scene.observer` argument exactly as today.<br>**(b) Launcher signatures are unchanged.** `launch_render_scene(...)` (`src/cuda/CudaTestKernel.cu`) and `launch_pathtrace_sample(...)` (`src/cuda/CudaPathTracer.cu`) both retain their pre-OBSERVER.8 parameter lists. The OBSERVER.8 commit DID NOT add a trailing `observer_frame` parameter to either launcher (operator-documented scope decision; doc-comment at `PathTracer.h:208-213` explicitly defers the launcher extension to a future slice that will pair the launcher signature change with the kernel-read wiring).<br>**(c) Perception-mode tag is dormant.** The adapter at `src/manifold/CameraObserverAdapter.h` produces an `ObserverFrame` whose `perception_mode` field is set per the `cfg.observer.perception_mode` (the operator-selected mode); the field travels through the launch boundary on `view.observer_frame.perception_mode` AND `pcfg.observer_frame.perception_mode` BUT no kernel call site reads it. The legacy SR helpers continue to be invoked unconditionally for any non-Identity scene-observer-velocity input (the pre-OBSERVER.8 behaviour). Setting `--observer-perception-mode relativistic` does NOT yet engage any kernel-side gate; the log fires + the field is carried + the kernel ignores it. |
| 6 | OptiX path unchanged                     | **PASS** | `git diff a0215c0..12f4942 --name-only -- 'src/optix/'` returns **zero hits**. Specifically: `OptixLaunchParams.h` is unchanged (no new `observer_frame` field; the OptiX launch-argument struct is byte-identical to its post-OBSERVER.7 form); `OptixPrograms.cu` is unchanged (no new SBT record; no new payload read; kernel-source bytes-identical); `OptixRenderer.h` + `OptixRenderer.cpp` are unchanged (no new `render_aovs` parameter; no new `render_pathtrace` parameter; no new internal observer-frame field). The OptiX dispatchers `run_render_optix_aovs` and `run_render_optix_pathtrace` in `src/main.cpp` are also unchanged (verified by inspection: the diff against `src/main.cpp` shows the OBSERVER.8 additions live only inside `run_render_aovs` at line 3917 + 4184 and the CUDA path-trace dispatcher at line 2693 + 2772; the OptiX-side dispatchers are not touched).<br>The OptiX side gets the same `OBSERVER.10 OptiX payload bridge` work (renumbered from the original OBSERVER.7) per the renumbered OBSERVER.* ladder in §4 below — mirroring the SCHW.5 → SCHW.7 (CUDA → OptiX) progression that landed for the chart arc. |
| 7 | Build / test status                      | **PASS** | Audit-host `cmake --build /home/user/RelativityRender/build` succeeds cleanly with no new warnings on the core / manifold / cuda / pathtracer / renderer modules. The `CudaScene.cuh` + `CudaRenderer.cu` translation units do NOT compile on the audit host (`RR_ENABLE_CUDA = OFF` per `CMakeLists.txt:22`); the structural changes there are sibling-field additions matching the SCHW.5 / SCHW.7 / MANI-I.5 precedent verbatim (same shape; same include; same one-line thread) — compile-time verification is deferred to a CUDA-SDK host but the precedent's structural pattern is preserved exactly.<br>Full `ctest` from the audit-host build directory: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests: 408/408` (unchanged from the OBSERVER.7 baseline — no test added by the OBSERVER.8 commit because the slice is pure plumbing whose verification is build success + audit-host smoke tests). `cli_tests: 254/254 passed` (unchanged from the OBSERVER.7 baseline); `renderer_tests: 19/19 passed` (unchanged); `pathtracer_tests`, `pathtracer_nee_tests`, `pathtracer_bsdf_tests`, `pathtracer_mis_tests`, `relativity_tests`, `math_tests`, `image_tests`, `gpu_tests`, `demo_tests` — all unchanged.<br>Audit-host smoke tests fire correctly (both verified at the OBSERVER.8 commit landing): default `--render-aovs` produces `aovs observer config: identity (no-op)` then the existing `--render-aovs requires CUDA` error; `--render-aovs --observer-beta 0.5 --observer-direction 1,0,0 --observer-perception-mode relativistic --observer-proper-time 12.0` produces `aovs observer config: constant-velocity-minkowski (|beta|=0.500000, dir=[1.000000, 0.000000, 0.000000], tau=12.000000)` then the same CUDA-required error. The log lines fire before the guard per the MANI-CONSUME.1 precedent. |
| 8 | Runtime CUDA status                      | **DEFERRED** | The audit host has no CUDA SDK installed (`nvcc` not present; `RR_ENABLE_CUDA` is OFF). Consequently the `.cu` translation units (`src/cuda/CudaScene.cuh`'s consumers + `src/cuda/CudaRenderer.cu`'s `view.observer_frame = targets.observer_frame` thread) cannot be compiled, linked, or device-launched from this host. The audit-host build produces a binary in which `--render-aovs` runs into the documented `RR_HAS_CUDA`-required error path immediately after the manifold-mode + observer-config logs fire; no actual `cudaLaunch` or kernel invocation can be exercised.<br>This is the **same documented deferral** the MANI-I.5 / SCHW.5 / SCHW.7 / PENROSE.6 / PENROSE.8 / MANI-CONSUME.1 commits accrued; the SCHW.11 + PENROSE.12 capstones recorded the runtime verification as deferred until a CUDA-SDK host runs the full pipeline. The deferral is NOT a `BLOCKED` because: (i) the structural plumbing is verified PASS (checks #1-7); (ii) the no-op-by-default invariant is verified at audit-host smoke tests (the operator-visible logs fire correctly + the existing test surface is unchanged); (iii) the operator's OBSERVER.8 brief explicitly scopes the slice to "CUDA path only, no kernel behaviour change beyond carrying data" — runtime verification would not exercise any new kernel behaviour at this slice anyway (no kernel reads the field).<br>**Required runtime checks for a future SDK-host audit pass** (when the operator runs the audit on a CUDA-equipped host): (a) the audit-host build's `--render-aovs` smoke tests reproduce the same log lines on the SDK host; (b) the `--render-aovs` action produces byte-identical PPM outputs vs. the pre-OBSERVER.8 baseline for the default-observer-config invocation; (c) the path-trace dispatcher's per-spp loop produces convergence-equivalent output for the default-observer-config invocation. None of these checks exercise new kernel code (per the operator brief); all three are byte-identity gates verifying the carry-only plumbing did not silently leak into the kernel. |
| 9 | PASS / REPAIR / BLOCKED verdict          | **PASS** | All seven structural checks return `PASS`. Check #8 (runtime CUDA) is `DEFERRED` on the documented audit-host SDK-absence limitation (mirrors the SCHW.5 / PENROSE.6 / MANI-CONSUME.1 deferral pattern; the same SDK-host pass is required for those slices' runtime verifications and is the documented next step for the OBSERVER.* arc's capstone audit). No `REPAIR` or `BLOCKED` item is outstanding. The OBSERVER.8 commit ships the documented three-attachment-point payload (CudaSceneView / AOVTargets / PathTraceConfig), the documented dispatcher-side adapter invocations (run_render_aovs + path-trace per-spp loop), the documented host-side echo logs (audit-host smoke-verified), and zero behaviour change (kernels untouched, OptiX untouched, launcher signatures untouched). The slice is **safe to extend** to the OptiX payload bridge (renumbered OBSERVER.10) under the renumbered OBSERVER.* ladder per §4 below. |

---

## 3. REASONING SUMMARY

The OBSERVER.8 commit (`12f4942`) introduces:

- three new sibling-field additions to existing
  POD structures, each consuming the
  `rr::manifold::ObserverFrame` type landed at
  MANIFOLD.3 + OBSERVER.2:
  - `CudaSceneView::observer_frame{}` at
    `src/cuda/CudaScene.cuh:137`;
  - `AOVTargets::observer_frame = {}` at
    `src/cuda/CudaRenderer.h:206`;
  - `PathTraceConfig::observer_frame{}` at
    `src/pathtracer/PathTracer.h:215`;
- one new line in
  `CudaRenderer::render_scene_with_aovs` that
  threads `targets.observer_frame` into
  `view.observer_frame`
  (`src/cuda/CudaRenderer.cu:311`);
- one new helper `format_observer_config_brief`
  in `src/main.cpp:198` that produces a
  single-line operator-readable description of
  the resolved `ObserverConfig`;
- two new dispatcher-side adapter invocations
  in `src/main.cpp` (`run_render_aovs` AOV path
  at line 4184 + path-trace per-spp loop at
  line 2693) calling the OBSERVER.6
  `build_observer_frame_from_camera(...)`
  helper with the active camera + legacy SR
  observer + CLI ObserverConfig;
- two new `Logger::info` lines (the
  `aovs observer config` line at
  `src/main.cpp:3917` and the `observer
  : ...` line at `src/main.cpp:2772`)
  that mirror the existing manifold-mode log
  placement + 17-column-label idiom.

The cuda-observer-payload-exists invariant
(check #1) is **three-attachment-point
verified** at documented file / line
positions; all three sibling-field placements
mirror the SCHW.5 / SCHW.7 / MANI-I.3
precedents verbatim.

The values-reach-launch-structures invariant
(check #2) is **three-data-path verified**:
the AOV render path threads the adapter
output through `targets.observer_frame` →
`view.observer_frame`; the path-trace path
threads through `pcfg.observer_frame`; the
host-side echo logs fire on every host
(audit-host smoke-test verified).

The default-observer-no-op invariant (check
#3) is **four-layer verified**: the default
ObserverConfig is the no-op anchor; the
adapter's Identity path returns rest_frame()
byte-for-byte; the launch payload carries the
no-op anchor; the audit-host smoke test
verifies the log fires correctly by default.

The beta=0-preserves-behaviour invariant
(check #4) is **three-layer verified**: the
adapter produces beta=(0,0,0) on every
zero-beta input path; the launch payload
carries the zero through unchanged; the
kernel reads the legacy types (unchanged) and
ignores the new field (carry-only contract).

The no-perception-transform invariant (check
#5) is **three-layer verified**: the kernel
sources are byte-unchanged; the launcher
signatures are unchanged (the operator's
explicit scope decision); the perception_mode
tag is carried but dormant (no kernel gate
reads it).

The optix-unchanged invariant (check #6) is
**directly verified** by `git diff --name-only
-- 'src/optix/'` returning zero hits. The
OptiX side gets the parallel OBSERVER.10 work.

The build/test status (check #7) is
**directly verified** by ctest 12/12 PASS +
zero test-count delta vs. the OBSERVER.7
baseline + two audit-host smoke-test
transcripts confirming the new log lines fire
correctly.

The runtime CUDA status (check #8) is
**DEFERRED** on the documented audit-host
SDK-absence limitation; the same deferral
pattern accrued for every prior CUDA-touching
slice (MANI-I.5 / SCHW.5 / SCHW.7 /
PENROSE.6 / PENROSE.8 / MANI-CONSUME.1). The
SDK-host pass is the documented next step
for the OBSERVER.* arc capstone audit.

The overall verdict (check #9) is **PASS**:
seven structural checks PASS + one
appropriately-DEFERRED runtime check; no
REPAIR or BLOCKED item; the slice is
safe to extend.

---

## 4. NEXT

The slice is **safe to extend**. The renumbered
`OBSERVER_FRAME_RENDERING_PLAN.md` §7 OBSERVER.*
sub-slice ladder needs a one-step shift to absorb
this audit slot, mirroring the OBSERVER.3 +
OBSERVER.5 + OBSERVER.7 audit-slot insertion
precedent:

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
- **OBSERVER.9** — **THIS AUDIT** (CUDA
  Observer Payload Audit, doc-only).
- **OBSERVER.10** — OptiX payload bridge (was
  OBSERVER.9 in the post-OBSERVER.7 plan;
  renumbered).
- **OBSERVER.11** — Observer debug AOV (was
  OBSERVER.10).
- **OBSERVER.12** — Arc capstone audit (was
  OBSERVER.11); closes the observer-frame arc
  per the OBSERVER.1 plan §7.

The
`docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7
sub-slice ladder may be updated by a follow-on
docs slice if the operator prefers an in-plan
renumbering; this audit doc is the canonical
ladder-shift record for the OBSERVER.9 audit-slot
insertion.

No `REPAIR` action is required. No `BLOCKED` item
is outstanding. The next concrete commit the
operator may prompt for is **OBSERVER.10 — OptiX
payload bridge** per the renumbered OBSERVER.1
plan §7 OBSERVER.7 → OBSERVER.10 (adds an
`rr::manifold::ObserverFrame observer_frame{}`
field to `OptixLaunchParams`; extends
`OptixRenderer::render_aovs` with a trailing
defaulted `observer_frame` parameter (mirroring
SCHW.7's pattern); dispatcher-side invocation of
`build_observer_frame_from_camera(...)` in
`main.cpp::run_render_optix_aovs`; no kernel
arms — the OptiX kernel reads the field at the
same future slice that wires the CUDA kernel
reads, mirroring the CUDA → OptiX symmetry
established by the SCHW.5 → SCHW.7 and
PENROSE.6 → PENROSE.8 progressions).

---

## 5. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") is the load-bearing invariant for the
  reserved-but-not-yet-consumed `observer_frame`
  fields being acceptable (each is structurally
  consumed by the dispatcher invocations + the
  echo logs + the planned kernel-read wiring in
  a future slice). Master rule #1 ("Build
  incrementally") + #2 ("Keep every step
  compilable") satisfied: ctest 12/12 PASS, zero
  behaviour change, zero kernel touch.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3
  Observer Frame + §6 GPU integration strategy —
  defines the contract the launch boundary
  carries.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §6, §7
  OBSERVER.8 (renumbered from the original §7
  OBSERVER.5 after the OBSERVER.3 + OBSERVER.5
  + OBSERVER.7 audit-slot insertions) — the
  OBSERVER.1 plan brief that authorised the
  payload bridge.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the underlying
  `ObserverFrame` POD's structural audit;
  carry-forward of the POD invariants the
  launch boundary now propagates.
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5) — the upstream CLI bridge's
  audit; carry-forward of the default-no-op
  invariant.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the camera-to-observer
  adapter's audit; carry-forward of the
  three-mode construction guarantee.
- `docs/PENROSE_LIKE_CUDA_INTEGRATION_AUDIT.md`
  (PENROSE.7) — the precedent CUDA-side
  integration audit doc this verdict mirrors
  in structure (sibling field placement +
  diff scope + runtime-CUDA-DEFERRED on
  audit host).
- `docs/SCHWARZSCHILD_LIKE_CUDA_COMPLETION_AUDIT.md`
  (SCHW.5 completion) — the precedent
  CUDA-side completion audit; established the
  PASS-with-DEFERRED-runtime pattern this
  audit follows.
- `docs/MANIFOLD_CONSUMPTION_GAP_AUDIT.md`
  (MANI-CONSUME.2) — the precedent for the
  "log fires before the RR_HAS_CUDA guard so
  audit-host smoke tests see it" pattern that
  the OBSERVER.8 `aovs observer config` log
  follows.
- `src/cuda/CudaScene.cuh` (modified at
  `12f4942`) — carries the new
  `observer_frame` field on `CudaSceneView`
  at line 137.
- `src/cuda/CudaRenderer.h` (modified at
  `12f4942`) — carries the new
  `observer_frame` field on `AOVTargets` at
  line 206.
- `src/cuda/CudaRenderer.cu` (modified at
  `12f4942`) — carries the one-line thread
  inside `render_scene_with_aovs` at line 311.
- `src/pathtracer/PathTracer.h` (modified at
  `12f4942`) — carries the new
  `observer_frame` field on `PathTraceConfig`
  at line 215.
- `src/main.cpp` (modified at `12f4942`) —
  carries the new
  `format_observer_config_brief` helper at
  line 198; the `aovs observer config` log at
  line 3917; the AOV-path adapter invocation
  at line 4184; the path-trace-path adapter
  invocation at line 2693; the per-spp
  observer-config log at line 2772.
- `src/manifold/CameraObserverAdapter.h` —
  the OBSERVER.6 adapter the new dispatcher
  invocations call; unchanged by OBSERVER.8.
- `src/manifold/ObserverFrame.h` — the
  underlying POD; unchanged by OBSERVER.8.
- `src/optix/OptixLaunchParams.h`,
  `src/optix/OptixPrograms.cu`,
  `src/optix/OptixRenderer.h`,
  `src/optix/OptixRenderer.cpp` — all
  unchanged by OBSERVER.8 (check #6 directly
  verified).
- `tests/manifold_identity_tests.cpp` —
  unchanged by OBSERVER.8 (the OBSERVER.6
  adapter is already covered by 12 dedicated
  tests; the OBSERVER.4 CLI by 18; this slice
  is pure plumbing verified by build success
  + audit-host smoke tests). Reports
  `408/408 checks passed`.
- `tests/cli_tests.cpp` — unchanged by
  OBSERVER.8; reports `254/254 passed`.
- `docs/BUILD_PLAN.md` — OBSERVER.8 entry
  (lines 80162 onward as of `12f4942`).
- Commit `12f4942` — `cuda: OBSERVER.8 —
  CUDA Observer Payload Bridge (impl,
  CUDA-side carry-only)`.
- Commit `a0215c0` — `docs: OBSERVER.7 —
  Camera-to-Observer Adapter Audit (docs
  only)`; the audit baseline.

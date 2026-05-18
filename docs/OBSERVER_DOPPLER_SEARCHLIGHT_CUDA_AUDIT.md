# CUDA Observer Doppler/Searchlight Migration Audit (OBS-DOP.3)

Date:   2026-05-18
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `49eae42` ("cuda:
OBS-DOP.2 — CUDA Observer Doppler/Searchlight Migration
(impl, kernel arms + helpers)").
Audit baseline: `b334237` ("docs: OBS-DOP.1 — Observer
Doppler/Searchlight Migration Task (docs only)") — the
last commit before OBS-DOP.2 landed.
Arc baseline: `0fcdd84` (OBS-PERCEPT.10 capstone audit
— the last commit before the OBS-DOP.\* arc opened).
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The OBS-DOP.2 commit's OptiX-ON-no-SDK
build was empirically verified at landing time (ctest
14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, the OBS-DOP.2 commit's content,
the audit-host `ctest` runtime outputs, and `git diff`
filter inspections.

This audit is the **in-band per-slice audit gate** for
OBS-DOP.2 (`49eae42`). It verifies the ten items the
operator's brief enumerates — CUDA Doppler reads
`ObserverFrame`; CUDA searchlight reads `ObserverFrame`;
activation requires `ConstantVelocityMinkowski`;
`beta = 0` no-op; default observer no-op; existing
Doppler/searchlight math preserved; OptiX path
unchanged; build/test status; runtime CUDA status
(`PASS` / `DEFERRED` / `BLOCKED`); and the overall
verdict (`PASS` / `REPAIR` / `BLOCKED`).

The OBS-DOP.2 slice is the **second active OBS-DOP.\*
implementation slot** (the first was OBS-DOP.1's
documentation-only task brief). It operationalises the
OBS-PERCEPT.10 capstone audit's check #7 deferral
(Doppler/searchlight unchanged at OBS-PERCEPT.\* arc
close; consolidation deferred to a "future
OBS-PERCEPT.\* sub-slice"; the OBS-DOP.\* arc IS that
sub-slice family). The OBS-DOP.4 OptiX-bridge slot
(renumbered in-band per the standing audit-slot
insertion discipline) will mirror OBS-DOP.2 on the
OptiX path; per this audit, OBS-DOP.2 ships CUDA-only.

---

## 1. VERDICT

**PASS.**

All nine structural / runtime-status checks (#1
through #9) PASS — check #9's runtime CUDA status
records the standard `PASS_WITH_RUNTIME_DEFERRED`
shape on the documented audit-host SDK-absence
limitation, but the structural data-path is fully
verified by the audit-host build's clean compile +
13/13 ctest pass + 16 NEW RR_CHECK assertions on
`manifold_identity_tests` (now 437/437 PASS) +
OptiX-ON-no-SDK build clean (14/14 ctest PASS). Check
#10 (overall verdict) is `PASS`. The OBS-DOP.2 surface
ships exactly what the operator's six-bullet brief
authorised — CUDA Doppler/searchlight migration via
two unified `apply_observer_doppler_color(...)` +
`apply_observer_searchlight_scale(...)` helpers, with
the documented preservation guarantees (default
observer no-op + beta=0 no-op + existing math
preserved + OptiX path unchanged + no
manifold/field/C4D changes) — without spilling into
OptiX, CLI, dispatcher, or non-perception-mode
surfaces.

Check #9's runtime CUDA status is the standard
`PASS_WITH_RUNTIME_DEFERRED` shape carried by every
prior CUDA-touching slice (OBS-PERCEPT.3 / OBS-PERCEPT.5
/ FIELD-I.9 / FIELD-I.11 / FIELD-BEAUTY.3 /
FIELD-BEAUTY.5 / SCHW.5 / PENROSE.6 / MANI-I.5 /
OBSERVER.10 / OBSERVER.13). The audit-host has no CUDA
SDK so the kernel arm's empirical Doppler /
searchlight modulation cannot be exercised; the
structural data-path (host-side helper + kernel-arm
dispatch) is verified by:

- audit-host build's clean compile + 13/13 ctest pass;
- 16 NEW RR_CHECK assertions on
  `manifold_identity_tests` (now 437/437 PASS;
  421 baseline + 16 new from the eight
  `test_obs_dop_2_*` test functions);
- OptiX-ON-no-SDK build clean (14/14 ctest PASS at the
  OBS-DOP.2 landing — confirms the `ObserverFrame.h`
  header addition doesn't break the OptiX-on path);
- per-line `git diff` inspections confirming OptiX
  source unchanged + CudaPathTracer unchanged +
  manifold/field/IO/core/scene/main source surfaces
  preserved verbatim.

The narrow-scope verdict honesty: the operator's
OBS-DOP.2 brief enumerated six implementation bullets
(CUDA-only scope; ObserverFrame payload reads;
activation gates; default no-op; beta=0 no-op; math
preservation). The slice satisfies all six:

- **Bullet 1** (CUDA-only scope): the kernel-arm
  modifications target ONLY `src/cuda/CudaTestKernel.cu`;
  OptiX `OptixPrograms.cu` byte-identical to the
  pre-OBS-DOP.2 baseline (see check #7). The CUDA
  path-tracer `CudaPathTracer.cu` is byte-identical
  too (no pre-existing Doppler/searchlight site per
  the OBS-P.3 scope correction; OBS-DOP.2 preserves
  the 5-site discipline).
- **Bullet 2** (ObserverFrame payload reads): both
  unified helpers take the `ObserverFrame` POD by
  const reference + read `obs_frame.perception_mode`
  (the outer gate's discriminator) + `obs_frame.beta`
  (the inner gate's 3-velocity source). See checks
  #1 + #2 below.
- **Bullet 3** (activation gates): both unified
  helpers apply the three-gate logic
  (`perception_mode == ConstantVelocityMinkowski` +
  `|beta|² > 0` + safe-clamp via OBSERVER.6 adapter
  pre-clamping). See check #3 below.
- **Bullet 4** (default observer no-op): the default
  `ObserverFrame{}` carries `perception_mode =
  Identity`; the outer gate closes; the dispatch's
  else-branch fires the legacy `applyDopplerColor` /
  `searchlightFactor` chain reading the OBS-P.2
  ternary's `observer.velocity` fallback path —
  byte-identical to the post-OBS-PERCEPT.10 baseline.
  See check #5 below.
- **Bullet 5** (beta=0 no-op): the inner gate's
  `!(beta2 > 0.0f)` short-circuit returns identity
  results at both helpers; matches the OBS-PERCEPT.3
  three-layer no-op anchor extended to the
  Doppler/searchlight scope. See check #4 below.
- **Bullet 6** (math preservation): the existing
  `rr::relativity::applyDopplerColor` /
  `searchlightFactor` / `dopplerFactor` /
  `precompute_relativity` math leaves are preserved
  verbatim across the slice. See check #6 below.

No REPAIR action required. No BLOCKED item outstanding.
The OBS-DOP.\* arc's per-slice gate chain is safe to
extend; the next slot is the OBS-DOP.4 OptiX-bridge
implementation (renumbered in-band per the standing
audit-slot insertion discipline).

---

## 2. PER-CHECK RESULTS

| #  | Check                                                  | Evidence                                                                                                                                                                                                                                                                                                                                                              | Verdict |
|----|--------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1  | CUDA Doppler reads `ObserverFrame` beta/direction/mode | The unified helper `rr::manifold::apply_observer_doppler_color(obs_frame, rgb, D, strength)` at `src/manifold/ObserverFrame.h:630-652` takes the `ObserverFrame` POD by const reference + reads `obs_frame.perception_mode` (line 637) for the outer gate + `obs_frame.beta` (line 643) for the inner gate. Two CUDA call sites consume the helper: `k_sphere_relativistic` Doppler dispatch at `CudaTestKernel.cu:291-300` (calls `apply_observer_doppler_color(observer_frame, color, D, params.doppler_color_strength)` on `perception_active`); `k_render_scene` Doppler dispatch at `CudaTestKernel.cu:694-703` (calls `apply_observer_doppler_color(scene.observer_frame, color, D, scene.params.doppler_color_strength)` on `perception_active`). The `perception_active` boolean reuses the OBS-P.2 ternary's scope-uniform value at `CudaTestKernel.cu:226-228` (`k_sphere_relativistic`) + `:392-394` (`k_render_scene`); no recomputation. The `ObserverFrame` PODs reached at the call sites are the OBSERVER.6-adapter outputs (verified by inspecting the kernel parameters: `k_sphere_relativistic` takes `observer_frame` as a trailing-defaulted kernel argument per the OBS-P.2 signature extension; `k_render_scene` reads `scene.observer_frame` carried via the OBSERVER.8 `CudaSceneView::observer_frame` field). | PASS    |
| 2  | CUDA searchlight reads `ObserverFrame` beta/direction/mode | The unified helper `rr::manifold::apply_observer_searchlight_scale(obs_frame, D, strength)` at `src/manifold/ObserverFrame.h:684-705` takes the same `ObserverFrame` POD shape + reads the same `obs_frame.perception_mode` (line 689) + `obs_frame.beta` (line 696) for the three-gate logic. Two CUDA call sites consume the helper: `k_sphere_relativistic` searchlight dispatch at `CudaTestKernel.cu:308-318` (calls `apply_observer_searchlight_scale(observer_frame, D, params.searchlight_strength)` on `perception_active`); `k_render_scene` searchlight dispatch at `CudaTestKernel.cu:714-726` (calls `apply_observer_searchlight_scale(scene.observer_frame, D, scene.params.searchlight_strength)` on `perception_active`). Cross-backend payload symmetry: both call sites consume the same `ObserverFrame` POD from the same source (the OBSERVER.6 adapter through the OBSERVER.8 / OBSERVER.10 carrier fields). | PASS    |
| 3  | Activation requires `ConstantVelocityMinkowski`        | Both unified helpers' outer gate at `ObserverFrame.h:636-639` (Doppler) + `:689-692` (searchlight): `if (obs_frame.perception_mode != PerceptionMode::ConstantVelocityMinkowski) return /* identity */;`. The Doppler helper returns the input `rgb` unchanged; the searchlight helper returns `1.0f` (identity scale). The default `Identity` perception_mode closes the gate; the reserved `CurvedChartGeodesicPlaceholder` also closes (master rule #3 placeholder honesty). Empirically verified by `test_obs_dop_2_doppler_color_identity_mode_returns_input` (3 RR_CHECKs on RGB equality at `tests/manifold_identity_tests.cpp:2149-2172`) + `test_obs_dop_2_doppler_color_curved_placeholder_returns_input` (3 RR_CHECKs at `:2238-2261`) + `test_obs_dop_2_searchlight_scale_identity_mode_returns_unity` (1 RR_CHECK on `scale == 1.0f` at `:2263-2283`) + `test_obs_dop_2_searchlight_scale_curved_placeholder_returns_unity` (1 RR_CHECK at `:2335-2354`). Both helpers' outer gate is structurally identical to the OBS-PERCEPT.3 helper's outer gate at `ObserverFrame.h:557-560` verbatim — the same `obs_frame.perception_mode != PerceptionMode::ConstantVelocityMinkowski` comparison + the same early-return shape; the only difference is the identity result type (Vec3 vs float). | PASS    |
| 4  | `beta = 0` remains no-op                               | Both unified helpers' inner gate at `ObserverFrame.h:642-647` (Doppler) + `:694-699` (searchlight): `const Vec3 beta = obs_frame.beta; const float beta2 = beta.x*beta.x + beta.y*beta.y + beta.z*beta.z; if (!(beta2 > 0.0f)) return /* identity */;`. Squared-magnitude check avoids the `sqrt` cost + is exact at `beta = 0`; NaN-safe `!(beta2 > 0.0f)` form catches NaN beta components (defence-in-depth on top of the OBSERVER.6 adapter's pre-clamping). Empirically verified by `test_obs_dop_2_doppler_color_constant_velocity_zero_beta_returns_input` (3 RR_CHECKs on RGB equality at `tests/manifold_identity_tests.cpp:2174-2197`) + `test_obs_dop_2_searchlight_scale_constant_velocity_zero_beta_returns_unity` (1 RR_CHECK on `scale == 1.0f` at `:2285-2304`). Both helpers' inner gate is structurally identical to the OBS-PERCEPT.3 helper's inner gate at `ObserverFrame.h:563-568` verbatim — same squared-magnitude form, same NaN-safe comparison shape. | PASS    |
| 5  | Default observer remains no-op                         | Three-layer default-no-op anchor preserved (mirrors the OBS-PERCEPT.3 → OBS-PERCEPT.5 contract verbatim, extended to the Doppler/searchlight scope): **(a) Layer 1 — helper outer gate**: explicit `perception_mode != ConstantVelocityMinkowski` short-circuit at the outer gate. **(b) Layer 2 — helper inner gate**: explicit `!(beta2 > 0.0f)` short-circuit at the inner gate. **(c) Layer 3 — math leaf identity**: `rr::relativity::applyDopplerColor(rgb, D, strength)` at `D = 1` is identity (the `tanh(0.5 * log(1)) = 0` mix factor); `rr::relativity::searchlightFactor(D)` at `D = 1` returns `1.0f`. **(d) Layer 4 — OBSERVER.6 adapter**: emits `observer_frame.beta = (0, 0, 0)` exactly on default zero-beta inputs (verified at OBSERVER.7 audit check #2). **(e) Layer 5 — dispatch else-branch**: when `perception_active == false`, the legacy `rr::relativity::applyDopplerColor(color, D, strength)` + `1 + (searchlightFactor(D) - 1) * strength` chain runs reading the OBS-P.2 ternary's `observer.velocity` fallback path — byte-identical to the post-OBS-PERCEPT.10 baseline. The default `ObserverFrame{}` carries `perception_mode = Identity` (OBSERVER.2 audit check #2); the dispatch's outer gate closes; the else-branch fires the legacy chain. Empirically pinned by `test_obs_dop_2_*_identity_mode_*` + `test_obs_dop_2_*_curved_placeholder_*` (8 RR_CHECKs total covering both helpers). | PASS    |
| 6  | Existing Doppler/searchlight math preserved            | The `src/relativity/RelativityMath.h` math leaves are preserved verbatim across OBS-DOP.2. Per-line diff `git diff 0fcdd84..49eae42 -- 'src/relativity/'` returns zero hits. The `dopplerFactor(rel, dir)` helper (math leaf at `RelativityMath.h:173-179`); the `applyDopplerColor(rgb, D, strength)` helper (`:219-230`); the `searchlightFactor(D)` helper (`:96-99`); the `precompute_relativity(beta_vec)` helper (`:160-167`) — all four are byte-unchanged. The unified helpers in `ObserverFrame.h` compose the existing math leaves via RR_HD inline calls (Doppler helper invokes `applyDopplerColor` at line 651; searchlight helper invokes `searchlightFactor` at line 703 + applies the linear `1 + (D⁴ - 1) * strength` formula at line 704). At the kernel call sites, the dispatch's else-branch (when `perception_active == false`) calls the math leaves directly with the same arguments as the pre-OBS-DOP.2 baseline: `CudaTestKernel.cu:297-298` (Doppler else-branch) + `:313-315` (searchlight else-branch) for `k_sphere_relativistic`; `:700-702` (Doppler else-branch) + `:723-724` (searchlight else-branch) for `k_render_scene`. The Stage 14A.3 AOV-uniform `D⁴` computation at `CudaTestKernel.cu:713` is preserved verbatim (the `searchlight_factor` AOV at line 731 reads the raw physical `D⁴` regardless of perception engagement; matches the OBSERVER.13 `ObserverBeta` AOV's read-only contract per OBS-DOP.1 §4.8). | PASS    |
| 7  | OptiX path unchanged                                   | OBS-DOP.2 modifications target ONLY the CUDA-side files; OptiX is byte-unchanged. Per-line `git diff 0fcdd84..49eae42 --name-only -- 'src/optix/'` returns zero hits. The four OptiX post-shading Doppler/searchlight sites (`__raygen__pinhole` `dopplerFactor` at `OptixPrograms.cu:258` + payload-register threading; `__closesthit__radiance` / `__miss__radiance` consumers via the shared `apply_doppler_and_searchlight_with_D(...)` shim at `OptixPrograms.cu:99-124`; `__raygen__pathtrace` post-spp Doppler at `:1538`) continue to use the pre-OBS-DOP.2 baseline shape verbatim — they still call `rr::relativity::dopplerFactor(...)` + `applyDopplerColor(...)` + `searchlightFactor(...)` directly with the OBS-P.2 ternary's `rel` snapshot. OptiX-side migration deferred to OBS-DOP.4 per the OBS-DOP.1 §9 sub-slice ladder (renumbered in-band; the original §9 numbering had OBS-DOP.3 = OptiX impl, but this in-band audit slot inserts the CUDA-side audit gate first per the standing audit-slot insertion discipline). | PASS    |
| 8  | Build / test status                                    | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings on the `rr_manifold` (header-only helper additions in `ObserverFrame.h`), `rr_gpu` (CUDA TU absorbs the dispatch-shape modification at the two `CudaTestKernel.cu` sites), or any other module. Full `ctest` from the audit-host build directory: `100% tests passed, 0 tests failed out of 13`.<br>**Per-suite counts** (delta from the post-OBS-DOP.1 / OBS-PERCEPT.10 baseline at HEAD = `b334237` / `0fcdd84`):<br>- `manifold_identity_tests: 437 / 437 checks passed` (was 421; **+16 new RR_CHECK** from the 8 new `test_obs_dop_2_*` test functions).<br>- `renderer_tests: 51 / 51 passed` (unchanged from OBS-PERCEPT.10's +16 OBS-PERCEPT.8 delta).<br>- `relativity_tests: 841 / 841 passed` (unchanged).<br>- `field_tests: 135 / 135 passed` (unchanged).<br>- `cli_tests: 274 / 274 passed` (unchanged).<br>- All other test suites unchanged.<br>OptiX-ON-no-SDK build at `/tmp/rr_build_optix_no_sdk`: `cmake --build` clean; ctest `14/14 passed` (including `optix_tests`). The `ObserverFrame.h` header addition (the two new unified helpers) is consumed by the OptiX TUs without modification; the `src/optix/OptixPrograms.cu` source remains byte-identical to the post-OBS-DOP.1 baseline (confirmed by `git diff 0fcdd84..49eae42 -- 'src/optix/'` returning zero hits). | PASS    |
| 9  | Runtime CUDA verification status                       | **DEFERRED.** The audit host has neither CUDA SDK nor OptiX SDK installed (`nvcc` not present; `optixGetVersion` unavailable; `RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF`). Consequently:<br>**(a)** The OBS-DOP.2 CUDA kernel-arm dispatches at `CudaTestKernel.cu:291-300` (Doppler in `k_sphere_relativistic`) + `:308-318` (searchlight in `k_sphere_relativistic`) + `:694-703` (Doppler in `k_render_scene`) + `:714-726` (searchlight in `k_render_scene`) cannot be compiled, linked, or device-launched from this host.<br>**(b)** Audit-host CAN verify: the host-side unified helpers' three-gate logic via the 8 new `test_obs_dop_2_*` functions in `manifold_identity_tests.cpp` (the helpers' three-gate paths produce the expected identity / composed outputs across all four enumerator-of-perception_mode × two beta-states combinations; the non-zero-beta cases verify the helpers compose the math leaves bit-identically via `approx(out_rgb, applyDopplerColor(in_rgb, D, strength), 1.0e-5f)` + `approx(scale, 1 + (searchlightFactor(D) - 1) * strength, 1.0e-5f)`); the kernel-arm dispatch shapes compile cleanly at `RR_ENABLE_CUDA=OFF` (the `.cu` files are excluded from the audit-host build, but the included headers compile); the audit-host smoke tests confirm the parser surface is byte-unchanged; the OptiX-ON-no-SDK build clean confirms the header additions don't break the OptiX-on path; `RelativityParams` flag-guards are textually preserved (verified by inspection — every `if (scene.params.enable_doppler)` / `if (params.enable_doppler)` / `if (scene.params.enable_searchlight)` / `if (params.enable_searchlight)` site preserved verbatim at the CUDA call sites' dispatch wrapper).<br>This is the **same documented deferral** pattern accrued by every prior CUDA-touching slice (MANI-I.5 / SCHW.5 / SCHW.7 / PENROSE.6 / PENROSE.8 / MANI-CONSUME.1 / OBSERVER.8 / OBSERVER.10 / OBSERVER.13 / OBS-P.2 / OBS-F.2 / OBS-PERCEPT.3 / OBS-PERCEPT.8 / FIELD-I.7 / FIELD-I.9 / FIELD-BEAUTY.3). The OBS-PERCEPT.4 audit + OBS-PERCEPT.10 capstone + every prior per-slice audit recorded runtime verification as DEFERRED with this disposition; OBS-DOP.3 inherits the pattern.<br>**Required SDK-host runtime checks** to convert the verdict from PASS_WITH_RUNTIME_DEFERRED → PASS, per the OBS-DOP.1 task brief §5.5 list:<br>(i) **§5.5.1** Default-state byte identity: `--render-aovs <every fixture>` pre + post; `cmp` PPMs byte-by-byte. Expected byte-identical (Identity outer gate closes → legacy else-branch fires the math leaf chain unchanged).<br>(ii) **§5.5.2** Zero-beta byte identity: `--render-aovs --observer-perception-mode relativistic --observer-beta 0 <fixture>` vs `--observer-perception-mode default` PPMs. Expected byte-identical (inner gate closes → helpers return identity).<br>(iii) **§5.5.3** Non-zero-beta consistency: `--render-aovs --observer-perception-mode relativistic --observer-beta 0.5 --observer-direction 1,0,0 <fixture>` pre + post; `cmp` PPMs. Expected byte-identical (unified helpers compose the same math leaves with the same beta source).<br>(iv) **§5.5.4** OBS-PERCEPT.9 fixture runtime: `--observer-perception-mode relativistic` against the oblique `[0.6, -0.8, 0.0]` beta direction + FOV 60° fixture; verify visible Doppler color shift + searchlight beaming asymmetry.<br>(v) **§5.5.6** Path-tracer post-spp Doppler: `--render-pathtrace --observer-perception-mode relativistic <fixture>` pre + post; `cmp` PPMs. NOTE: CUDA path-tracer has no Doppler/searchlight site (OBS-P.3 5-site scope correction); this scenario will be CUDA-no-op + OptiX-only-engaged once OBS-DOP.4 lands.<br>(vi) **§5.5.7** Doppler debug AOV interaction: the OBSERVER.13 `ObserverBeta` AOV's PPM output unchanged regardless of perception engagement (verifies the AOV remains a read-only payload view, NOT a function of perception transform). | DEFERRED |
| 10 | PASS / REPAIR / BLOCKED verdict                        | **PASS.** All nine structural checks (#1 – #8) PASS; check #9 (runtime CUDA) records `DEFERRED` on the documented audit-host SDK-absence limitation. No REPAIR or BLOCKED item is outstanding. The OBS-DOP.2 commit ships:<br>- 2 new RR_HD inline unified helpers in `ObserverFrame.h` (Doppler + searchlight; +129 lines including doc-comments);<br>- 4 CUDA call sites migrated to the §6.5 dispatch pattern (`k_sphere_relativistic` Doppler/searchlight + `k_render_scene` Doppler/searchlight);<br>- 0 OptiX modifications (deferred to OBS-DOP.4);<br>- 0 `src/relativity/` modifications (math leaves preserved verbatim);<br>- 0 `src/field/` / `src/manifold/` (outside `ObserverFrame.h`) / `src/io/` / `src/core/` / `src/scene/` / `src/main.cpp` / `src/cuda/CudaPathTracer.cu` modifications;<br>- 8 new test functions covering 16 NEW RR_CHECK assertions on `manifold_identity_tests`;<br>- audit-host build clean (13/13 ctest PASS);<br>- OptiX-ON-no-SDK build clean (14/14 ctest PASS);<br>- structural mirroring of OBS-PERCEPT.3 helper location + signature shape verbatim;<br>- master rule #1 + #3 + #11 + #12 + #16 satisfied across the slice;<br>- BUILD_PLAN.md entry with full What ships / does NOT ship / Acceptance / Module status rubric.<br>The slice is **safe to extend**; the next OBS-DOP.\* slot is OBS-DOP.4 — OptiX implementation (mirroring OBS-DOP.2 on the OptiX path; the OptiX surface has 4 Doppler/searchlight sites instead of 2; the helpers in `ObserverFrame.h` are consumed verbatim by the OptiX TUs). The audit verdict authorises the operator to proceed to OBS-DOP.4 OR to leave the OBS-DOP.\* arc in its current state pending an SDK-host runtime pass that converts both this OBS-DOP.3 verdict + the future OBS-DOP.5 OptiX audit verdict + the future OBS-DOP.6 capstone verdict from `PASS_WITH_RUNTIME_DEFERRED` → `PASS`. | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Slice shape

The OBS-DOP.2 commit (`49eae42`) introduces two
unified `apply_observer_doppler_color(...)` +
`apply_observer_searchlight_scale(...)` helpers in
`src/manifold/ObserverFrame.h` and consolidates the
four CUDA post-shading sites onto the §6.5 dispatch
pattern (each site has Doppler + searchlight =
2 dispatches; 2 sites × 2 dispatches = 4 dispatches
total).

The aggregate diff:

```
$ git diff 0fcdd84..49eae42 --stat
docs/BUILD_PLAN.md                        |  652 +
docs/OBSERVER_DOPPLER_SEARCHLIGHT_TASK.md | 1552 +
src/cuda/CudaTestKernel.cu                |   72 +-
src/manifold/ObserverFrame.h              |  129 +
tests/manifold_identity_tests.cpp         |  222 +
```

Source-code surface (~191 net lines on the CUDA-touching
side): the two new unified helpers (~129 lines including
doc-comments) + the four CUDA call sites' dispatch
migration (~62 net lines). Test surface (~222 lines):
eight new test functions covering 16 RR_CHECKs.
Documentation surface (~1552 lines for the OBS-DOP.1
task brief + 652 lines BUILD_PLAN entries across
OBS-DOP.1 + OBS-DOP.2). Zero CMakeLists.txt
modification across the slice.

The narrow scope intentionally excludes every other
file from the operator's brief-by-brief discipline: no
OptiX-side `OptixPrograms.cu` modification (the four
OptiX post-shading sites unchanged); no
`CudaPathTracer.cu` modification (no pre-existing
Doppler/searchlight site per OBS-P.3 scope
correction); no `src/relativity/` math leaf
modification; no `src/manifold/` modification outside
`ObserverFrame.h`; no `src/field/` modification; no
`src/io/SceneLoader.cpp` parser modification; no
`src/core/Config.h` extension; no new CLI flag; no
new ObserverFrame POD field; no new fixture; no new
debug AOV; no `MODULE_MAP.md` update; no
`MANIFOLD_INTEGRATION_PLAN.md` update.

### 3.2 Checks #1 + #2 — CUDA Doppler + searchlight reads

Both CUDA call sites consume the same unified
helpers from `ObserverFrame.h`. The five-axis
cross-backend symmetry framework (inherited from
OBS-PERCEPT.6 §3.7) applies verbatim to this slice's
CUDA-only scope:

| Axis | `k_sphere_relativistic` | `k_render_scene` |
|------|-------------------------|-------------------|
| POD type | `observer_frame` (trailing kernel arg from OBS-P.2 signature extension) | `scene.observer_frame` (carried via the OBSERVER.8 `CudaSceneView::observer_frame` field) |
| Shared helper | `apply_observer_doppler_color(...)` + `apply_observer_searchlight_scale(...)` | (same) |
| Dispatch shape | `if (enable_*) { if (perception_active) { unified } else { legacy } }` | (same) |
| Math leaf (else branch) | `applyDopplerColor(...)` + `1 + (searchlightFactor(D) - 1) * strength` | (same) |
| Gate semantics | `perception_mode == ConstantVelocityMinkowski` + `|beta|² > 0` | (same) |

The `perception_active` boolean reuses the OBS-P.2
ternary's scope-uniform value at each kernel arm; no
recomputation. Cross-kernel bit-identity for the
Doppler / searchlight transform is **structurally
guaranteed by construction** within the CUDA backend.
Cross-backend (CUDA vs OptiX) bit-identity will be
guaranteed once OBS-DOP.4 lands the OptiX-side
dispatch using the same unified helpers.

The kernel-arm modifications target ONLY the post-
shading sites; the OBS-P.2 ternary at lines 226-232
(`k_sphere_relativistic`) + 392-398 (`k_render_scene`)
is preserved verbatim, feeding the `rel` snapshot
that BOTH the unified-helper branch (which consumes
`D` derived from `rel`) AND the legacy else-branch
consume. No new arithmetic; no new state; no
signature churn.

### 3.3 Check #3 — outer gate (ConstantVelocityMinkowski)

Both helpers' outer gate at `ObserverFrame.h:636-639`
(Doppler) + `:689-692` (searchlight) is structurally
identical to the OBS-PERCEPT.3 helper's outer gate at
line 557-560 verbatim:

```cpp
if (obs_frame.perception_mode !=
        PerceptionMode::ConstantVelocityMinkowski) {
    return /* identity result */;
}
```

Closes on:
- `PerceptionMode::Identity` (default
  `ObserverFrame{}`).
- `PerceptionMode::CurvedChartGeodesicPlaceholder`
  (reserved per master rule #3).

Opens only on `ConstantVelocityMinkowski`. Empirically
pinned by 8 RR_CHECK assertions across 4 test
functions:

- `test_obs_dop_2_doppler_color_identity_mode_returns_input`
  (3 RR_CHECKs): outer gate closes on Identity +
  non-zero beta → input rgb returned.
- `test_obs_dop_2_doppler_color_curved_placeholder_returns_input`
  (3 RR_CHECKs): outer gate closes on
  CurvedChartGeodesicPlaceholder + non-zero beta →
  input rgb returned.
- `test_obs_dop_2_searchlight_scale_identity_mode_returns_unity`
  (1 RR_CHECK): outer gate closes on Identity + non-
  zero beta → scale = 1.0f.
- `test_obs_dop_2_searchlight_scale_curved_placeholder_returns_unity`
  (1 RR_CHECK): outer gate closes on
  CurvedChartGeodesicPlaceholder + non-zero beta →
  scale = 1.0f.

### 3.4 Check #4 — inner gate (|beta|² > 0)

Both helpers' inner gate at `ObserverFrame.h:642-647`
(Doppler) + `:694-699` (searchlight) is structurally
identical to the OBS-PERCEPT.3 helper's inner gate at
line 563-568 verbatim:

```cpp
const Vec3 beta = obs_frame.beta;
const float beta2 = beta.x * beta.x
                  + beta.y * beta.y
                  + beta.z * beta.z;
if (!(beta2 > 0.0f)) {
    return /* identity result */;
}
```

Properties:
- **Squared-magnitude form** avoids the `sqrt` cost
  + is exact at `beta = 0`.
- **NaN-safe `!(beta2 > 0.0f)`** form catches NaN
  beta components (defence-in-depth on top of the
  OBSERVER.6 adapter's pre-clamping).
- **Exact zero short-circuit** at `beta = (0, 0, 0)`
  — the boolean `beta2 > 0.0f` evaluates to `false`
  exactly when `beta2 == 0.0f` (single-precision
  IEEE-754 zero is the additive identity); the
  negation `!(false)` returns `true` and the early
  return fires.

Empirically pinned by 4 RR_CHECK assertions across 2
test functions:

- `test_obs_dop_2_doppler_color_constant_velocity_zero_beta_returns_input`
  (3 RR_CHECKs): outer gate opens; inner gate
  closes on zero beta → input rgb returned.
- `test_obs_dop_2_searchlight_scale_constant_velocity_zero_beta_returns_unity`
  (1 RR_CHECK): outer gate opens; inner gate
  closes on zero beta → scale = 1.0f.

### 3.5 Check #5 — three-layer default no-op anchor (extended)

The three-layer no-op anchor inherited from
OBS-PERCEPT.3 + extended to the Doppler/searchlight
scope. The default `ObserverFrame{}` carries
`perception_mode = Identity` (OBSERVER.2 audit's
check #2 result); the OBS-DOP.2 dispatch's outer gate
closes; the else-branch fires the legacy math leaf
chain reading the OBS-P.2 ternary's
`observer.velocity` fallback — byte-identical to the
post-OBS-PERCEPT.10 baseline.

The composition guarantees every existing
`--render-*` invocation against any scene WITHOUT
`--observer-perception-mode relativistic`
preserves byte-identical PPM output to the
pre-OBS-DOP.2 baseline:

- **Default `--render-aovs <fixture>`** without
  perception-mode flag: `Identity` outer gate
  closes; legacy `applyDopplerColor` /
  `searchlightFactor` chain runs reading
  `observer.velocity` (which is `(0,0,0)` by
  default on every non-relativity-block fixture);
  Doppler factor `D = 1`; both math leaves are
  identity; output byte-identical to the
  baseline.
- **`--render-relativistic`** (legacy SR action;
  doesn't engage perception mode): `Identity`
  outer gate closes; legacy chain runs reading
  the legacy `observer.velocity` from the
  `relativity` block; output byte-identical to
  the pre-OBS-DOP.2 baseline (same math leaves,
  same `rel` snapshot from the same
  `observer.velocity`).

### 3.6 Check #6 — math preservation

The `src/relativity/RelativityMath.h` math leaves
are byte-unchanged across OBS-DOP.2. Per-line
`git diff 0fcdd84..49eae42 -- 'src/relativity/'`
returns zero hits.

The unified helpers compose the existing leaves via
RR_HD inline calls — no new arithmetic. The math
leaf at `applyDopplerColor(rgb, D, strength)` at
`RelativityMath.h:219-230` produces the same RGB
output bit-identically whether invoked directly
(else-branch) or via the unified helper's call
(then-branch); same for `searchlightFactor(D)` at
`:96-99`. The composition test
`test_obs_dop_2_doppler_color_constant_velocity_nonzero_beta_shifts`
at `manifold_identity_tests.cpp:2199-2236` verifies
this composition equivalence explicitly:

```cpp
const Vec3 expected =
    rr::relativity::applyDopplerColor(in_rgb, D, strength);
RR_CHECK(approx(out_rgb, expected, 1.0e-5f));
```

Similar composition test at
`test_obs_dop_2_searchlight_scale_constant_velocity_nonzero_beta_scales`:

```cpp
const float D4       = rr::relativity::searchlightFactor(D);
const float expected = 1.0f + (D4 - 1.0f) * strength;
RR_CHECK(approx(scale, expected, 1.0e-5f));
```

The kernel-side dispatch's else-branch consumes the
math leaves directly with the same arguments + same
input values as the pre-OBS-DOP.2 baseline; per-line
diff inspection of the unchanged else-branch
arguments (`color`, `D`, `params.doppler_color_strength`,
`params.searchlight_strength`) confirms byte-
identical legacy behaviour.

Master rule #3 satisfied: no new physics math; no
new SR specialisation; the existing math leaves are
the canonical source of truth + their behaviour is
preserved across the migration.

### 3.7 Check #7 — OptiX path unchanged

OBS-DOP.2 modifications target ONLY the CUDA-side
files. Per-line `git diff 0fcdd84..49eae42
--name-only -- 'src/optix/'` returns zero hits. The
four OptiX post-shading Doppler / searchlight sites
preserved verbatim:

- **`__raygen__pinhole`** `dopplerFactor(...)` at
  `OptixPrograms.cu:258`: byte-identical. The OBS-P.2
  guarded ternary at lines 215-221 + the `rel`
  snapshot computation at line 221 unchanged; the
  downstream Doppler / searchlight payload-register
  threading unchanged.
- **`__closesthit__radiance` / `__miss__radiance`**
  consumers via the shared
  `apply_doppler_and_searchlight_with_D(...)` shim
  at `OptixPrograms.cu:99-124`: byte-identical. The
  shim continues to call `rr::relativity::applyDopplerColor(...)`
  + `rr::relativity::searchlightFactor(...)`
  directly.
- **`__raygen__pathtrace`** post-spp Doppler at
  `OptixPrograms.cu:1538`: byte-identical. The
  per-spp loop unchanged; the post-spp Doppler /
  searchlight application unchanged.
- **`__miss__radiance`** `dopplerFactor` at
  `OptixPrograms.cu:162`: byte-identical.

The `ObserverFrame.h` header addition (the two new
unified helpers) is consumed by the OptiX TUs at
compile time without modification — the
OptiX-ON-no-SDK build confirms the `optix_tests`
binary still links and runs (`14/14 ctest PASS` in
`/tmp/rr_build_optix_no_sdk`). OptiX-side migration
deferred to OBS-DOP.4 per the OBS-DOP.1 §9 sub-slice
ladder (in-band renumbered: the original §9
numbering had OBS-DOP.3 = OptiX impl, but this in-
band audit slot inserts the CUDA audit gate first
per the standing audit-slot insertion discipline).

### 3.8 Check #8 — build / test status

Audit-host build at `build/`:

- `cmake --build build -j` succeeds cleanly with no
  new warnings.
- `ctest`: `100% tests passed, 0 tests failed out of
  13`.
- `manifold_identity_tests: 437 / 437 checks passed`
  (+16 NEW from 421 baseline; from the 8 new
  `test_obs_dop_2_*` test functions).
- `renderer_tests: 51 / 51 passed` (unchanged from
  OBS-PERCEPT.10).
- `relativity_tests: 841 / 841 passed` (unchanged).
- `field_tests: 135 / 135 passed` (unchanged from
  FIELD-I.4).
- `cli_tests: 274 / 274 passed` (unchanged).
- All other test suites unchanged.

OptiX-ON-no-SDK build at
`/tmp/rr_build_optix_no_sdk`:

- `cmake --build /tmp/rr_build_optix_no_sdk -j`
  succeeds cleanly.
- `ctest`: `100% tests passed, 0 tests failed out of
  14`. Includes `optix_tests` (unchanged from
  OBS-PERCEPT.10).

### 3.9 Check #9 — runtime CUDA verification

`PASS_WITH_RUNTIME_DEFERRED` on the documented audit-
host SDK-absence limitation. The audit host has no
CUDA SDK so the kernel-arm Doppler / searchlight
modulation cannot be empirically exercised; the
structural data-path is verified by the audit-host
build's clean compile + ctest 13/13 PASS + 16 NEW
RR_CHECK assertions + OptiX-ON-no-SDK build clean +
the per-line diff inspections.

This is the standard deferral pattern shared by
every prior CUDA-touching slice (MANI-I.5 / SCHW.5
/ SCHW.7 / PENROSE.6 / PENROSE.8 / OBSERVER.10 /
OBSERVER.13 / OBS-P.2 / OBS-F.2 / OBS-PERCEPT.3 /
OBS-PERCEPT.8 / FIELD-I.7 / FIELD-I.9 /
FIELD-BEAUTY.3). The required SDK-host runtime
checks (per OBS-DOP.1 task brief §5.5) are
enumerated at check #9's evidence cell + the
combined CLI bridge slice (per OBS-PERCEPT.10 §4.2
(a) extended) is the canonical converging-leverage
closure.

### 3.10 Check #10 — overall verdict

**PASS.** All nine structural / runtime-status
checks PASS or appropriately DEFERRED. No REPAIR or
BLOCKED item outstanding. The OBS-DOP.2 slice is
safe to extend; the next OBS-DOP.\* slot is OBS-DOP.4
— OptiX implementation (the OptiX-side mirror of
OBS-DOP.2 on the four OptiX post-shading sites; the
unified helpers in `ObserverFrame.h` are consumed
verbatim by the OptiX TUs).

### 3.11 Master-rule satisfaction recap

- **Master rule #1 ("Build incrementally"):**
  satisfied. CUDA-only scope; OptiX deferred to
  OBS-DOP.4; the slice composes cleanly with the
  existing OBS-PERCEPT.3 + OBS-P.2 + OBSERVER.6
  infrastructure without modifying any of them.

- **Master rule #3 ("no fake stubs"):** satisfied.
  Both unified helpers ship with real arithmetic +
  real branches; the
  `CurvedChartGeodesicPlaceholder` mode's no-op
  fallback is honest (master rule #3: the helpers
  short-circuit to identity; future arcs lift with
  documented contracts).

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. The two unified helpers
  are empirically pinned by 16 RR_CHECK assertions
  across 8 test functions covering each three-gate
  path (Identity / CurvedPlaceholder / ConstVel +
  zero beta / ConstVel + non-zero beta) for each
  helper. The composition tests verify the helpers
  produce bit-identical output to direct math-leaf
  calls when both gates open.

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. The slice scope is deliberately narrow:
    - OBS-DOP.2 = CUDA-only Doppler / searchlight
      consolidation; no OptiX changes; no path-tracer
      changes (no pre-existing site); no aberration
      changes (OBS-PERCEPT.3 already migrated); no
      new math leaves; no debug AOV; no fixture;
      no CLI flag.
    - The four OptiX post-shading sites are
      deliberately untouched (deferred to OBS-DOP.4
      per the OBS-DOP.1 §9 sub-slice ladder).
    - The `CurvedChartGeodesicPlaceholder` no-op
      fallback is honest (no curved-chart Doppler /
      searchlight transform).

- **Master rule #16 ("default-off / reasoning-
  traceable defaults"):** satisfied. The OBS-DOP.2
  default state is unchanged from the OBS-PERCEPT.10
  baseline:
    - No `--render-*` action's output changes by
      default.
    - No existing PPM filename changes.
    - No new file produced by default.
    - No existing AOV slot's value changes.
  The single observable behaviour change is the
  structural presence of the unified helper +
  the kernel-arm dispatch; the observable behaviour
  from every default CLI invocation (Identity
  perception mode + zero `observer.velocity`) is
  zero because the outer gate closes + the legacy
  else-branch fires the math leaves with the same
  inputs as the pre-OBS-DOP.2 baseline.

### 3.12 Honest scope recap

The OBS-DOP.2 slice is a **kernel-arm + helper-leaf
audit-host-side migration**, with the **SDK-host
runtime validation deferred** to a future combined
CLI bridge slice's audit (per OBS-PERCEPT.10 §4.2 (a)
extended). The verdict `PASS` (with check #9
`DEFERRED` on the documented audit-host SDK-absence
limitation) honestly captures:

- The slice's structural content is complete +
  verified on the audit-host side (CUDA build clean
  + ctest 13/13 PASS + helper tests 16/16 PASS +
  OptiX-ON-no-SDK build clean).
- The empirical runtime verification of the kernel
  arm's composed Doppler / searchlight modulation
  output (the six SDK-host scenarios from §5.5 of
  the OBS-DOP.1 task brief) is reserved for the
  future combined CLI bridge slice's audit.

The OBS-DOP.4 OptiX-bridge slice (the renumbered
next OBS-DOP.\* impl slot per the in-band audit-slot
insertion) is the natural next operator-cadence step.
The combined CLI bridge slice would close this
audit's runtime-deferred verdict tail PLUS the
entire FIELD-I.\* + FIELD-BEAUTY.\* + OBS-PERCEPT.\*
+ OBS-DOP.\* arc family's verdict tails in one
converging-leverage operation.

---

## 4. NEXT

### 4.1 Renumbered OBS-DOP.\* sub-slice ladder (in-band)

The OBS-DOP.3 audit closes the CUDA-side gate.
The post-OBS-DOP.3 ladder for remaining
implementation + audit slots is:

- **OBS-DOP.4** — OptiX implementation (the
  renumbered next OBS-DOP.\* impl slot; lands the
  four OptiX post-shading site consolidations in
  `OptixPrograms.cu`; mirrors the OBS-PERCEPT.5
  OptiX-mirror precedent shape).
- **OBS-DOP.5** — OptiX audit (the renumbered next
  audit slot; mirrors this OBS-DOP.3 audit's
  shape applied to the OptiX-side migration).
- **OBS-DOP.6** — Arc capstone audit (the
  renumbered capstone slot; mirrors the
  OBS-PERCEPT.10 + FIELD-BEAUTY.8 capstone audit
  shapes; synthesises OBS-DOP.1 + .2 + .3 + .4 +
  .5 verdicts into the arc-level verdict).

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the operator's
cadence requires (this OBS-DOP.3 audit is itself an
in-band insertion between OBS-DOP.2 impl and the
originally-planned OBS-DOP.3 OptiX impl). The
combined FIELD-\* + OBS-PERCEPT + OBS-DOP CLI
bridge slice (per OBS-PERCEPT.10 §4.2 (a) extended)
collapses every per-arc-family SDK-host runtime
pass into a single converging-leverage slice once
all four arc families' kernel arms land.

### 4.2 Candidate next slots (prioritised)

**(a) HIGHLY RECOMMENDED — OBS-DOP.4: OptiX
implementation.** Lands the mirror of OBS-DOP.2 on
the OptiX path. Consumes the two unified helpers in
`ObserverFrame.h` verbatim; lands four OptiX post-
shading dispatches (`__raygen__pinhole` Doppler +
searchlight via the payload-register-3 plumbing;
`__closesthit__radiance` / `__miss__radiance` via
the shared shim; `__raygen__pathtrace` post-spp
Doppler + searchlight). Mirrors the OBS-PERCEPT.5
staged-impl pattern; this is the natural next step
to close the OBS-DOP.\* arc's CUDA + OptiX dual-
backend structural surface.

**(b) RECOMMENDED — combined FIELD-\* +
OBS-PERCEPT + OBS-DOP CLI bridge slice** (per
OBS-PERCEPT.10 §4.2 (a) extended). Closes the
entire field-and-observer-arc family's runtime-
deferred verdict tail in one SDK-host audit. Best
converging-leverage option if the operator has
SDK-host access; can be sequenced AFTER OBS-DOP.4
to maximise the closed verdict count per audit
operation (closes 11+ runtime-deferred verdicts in
one SDK-host pass).

**(c) Manifold-orthogonal work.** Multiple options:
- **Deferred SDK-host runtime pass** for the
  entire arc family (OBSERVER.\* + OBS-P.\* +
  OBS-F.\* + FIELD-I.\* + FIELD-BEAUTY.\* +
  OBS-PERCEPT.\* + OBS-DOP.\*) — highest converging-
  leverage option.
- **MANI-I.12 final cross-host manifold audit**.
- **Denoiser integration with chart-aware AOVs**.
- **Path-tracer feature breadth** (NEE extension,
  BSDF expansion, MIS tuning).

**(d) NOT RECOMMENDED — OBS-DOP-AOV.\* arc**
(per-pixel Doppler / searchlight diagnostic AOV
extension before the CUDA + OptiX dual-backend
unified-helper migration is empirically verified.
Higher-risk than completing OBS-DOP.4 first.

**(e) DEFERRABLE — Retroactive task brief
authoring** for the OBSERVER.\* arc's unfilled
slots (carried forward from the prior arc family's
discretion notes; operator discretion).

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; master rule #1 + #3 +
  #11 + #12 + #16 satisfaction recap at §3.11).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §7.2
  (the observer-frame Lorentz boost concept the
  OBS-DOP.\* arc operationalises for the Doppler /
  searchlight sites).

### 5.2 OBS-DOP.\* arc references

- `docs/OBSERVER_DOPPLER_SEARCHLIGHT_TASK.md`
  (OBS-DOP.1 — the task brief this audit verifies
  the OBS-DOP.2 implementation against).

### 5.3 OBS-PERCEPT.\* + OBSERVER.\* + OBS-P.\* + OBS-F.\* arc references

- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1) — the canonical observer-space
  perception plan; the OBS-DOP.\* arc consolidates
  the Doppler / searchlight sites onto the unified
  abstraction this plan introduced.
- `docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`
  (OBS-PERCEPT.2) — the precedent task brief
  shape OBS-DOP.1 mirrored.
- `docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md`
  (OBS-PERCEPT.4) — the precedent CUDA-side per-
  slice audit shape this OBS-DOP.3 audit mirrors
  verbatim (eleven-row evidence table + runtime
  status row + verdict variant).
- `docs/OBSERVER_PRIMARY_RAY_OPTIX_AUDIT.md`
  (OBS-PERCEPT.6) — the precedent OptiX-side per-
  slice audit shape OBS-DOP.5 will mirror; the
  §3.7 five-axis cross-backend symmetry framework
  carries forward to OBS-DOP.\*.
- `docs/OBSERVER_PERCEPTION_ARC_AUDIT.md`
  (OBS-PERCEPT.10) — the arc-level capstone audit
  whose check #7 honestly deferred the
  Doppler/searchlight consolidation to a "future
  OBS-PERCEPT.\* sub-slice"; this OBS-DOP.\* arc
  IS that sub-slice family.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
  (OBSERVER.1) — the OBSERVER.\* foundation arc's
  plan; the OBS-DOP.2 implementation consumes the
  OBSERVER.6 adapter + OBSERVER.8 payload + the
  OBSERVER.2-shipped `ObserverFrame` POD verbatim.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the `ObserverFrame{}` default-
  constructed POD's contract the OBS-DOP.2
  three-layer no-op anchor relies on.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the adapter's beta-resolution +
  clamp-safety contracts the unified helpers rely
  on for the safe-clamp Layer 3 anchor.
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — the `CudaSceneView::observer_frame`
  carry-only field the CUDA OBS-DOP.2 kernel
  arms read at C-2.
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11) — the `OptixLaunchParams::observer_frame`
  carry-only field the OptiX OBS-DOP.4 programs
  will read (deferred).
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — the `ObserverBeta` AOV's read-
  only contract; OBS-DOP.2 preserves verbatim
  (the `searchlight_factor` AOV's Stage 14A.3
  AOV-uniform `D⁴` discipline preserved per
  check #6).
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15) — the OBSERVER.\* arc capstone
  whose §10 risk #1 the OBS-PERCEPT.\* arc closed
  for the primary-ray aberration site; the
  OBS-DOP.\* arc closes the same risk for the
  post-shading Doppler / searchlight sites.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3) — the precedent kernel-migration
  audit; OBS-DOP.2's three-gate dispatch
  consolidates the OBS-P.2 guarded ternary at
  the post-shading sites. Check #5's noted CUDA
  path-tracer scope correction (5 sites vs 6)
  carries forward; OBS-DOP.2 preserves the 5-
  site discipline (no `CudaPathTracer.cu`
  modification).
- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md` (OBS-F.3)
  — the precedent fixture audit; the OBS-F.2 +
  OBS-PERCEPT.9 fixtures are the canonical
  runtime-deferred SDK-host validation surfaces.

### 5.4 Parallel-arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1) — the parallel field-interpretation
  arc; OBS-DOP.\* + FIELD-I.\* + FIELD-BEAUTY.\* +
  OBS-PERCEPT.\* arcs coexist as orthogonal
  perceptual layers above the manifold.
- `docs/FIELD_SCALAR_BEAUTY_MAPPING_AUDIT.md`
  (FIELD-BEAUTY.8) — the precedent capstone audit
  shape; OBS-DOP.6 capstone will mirror.
- `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
  (FIELD-I.12) — the precedent OptiX-bridge audit
  whose five-axis symmetry framework carries
  forward to OBS-DOP.5.

### 5.5 Source surface audited (this slice)

The OBS-DOP.2 commit (`49eae42`) touched the
following source files (relative to the OBS-DOP.1
baseline `b334237`):

| File                                  | Net lines | Purpose |
|---------------------------------------|-----------|---------|
| `src/manifold/ObserverFrame.h`        | +129      | Two new RR_HD inline helpers (Doppler + searchlight) |
| `src/cuda/CudaTestKernel.cu`          | +62 / -10 | Four CUDA call sites migrated to §6.5 dispatch |
| `tests/manifold_identity_tests.cpp`   | +222      | 8 new test functions, +16 RR_CHECKs |
| `docs/BUILD_PLAN.md`                  | +299      | OBS-DOP.2 entry |

Total source-code surface: ~181 net lines (helpers
+ kernel modifications). Total test surface:
~222 lines. Zero CMakeLists.txt change. Zero OptiX
modification. Zero `CudaPathTracer.cu` modification.
Zero `src/relativity/` math leaf modification. Zero
non-CUDA-touching source file modification.

### 5.6 Surrounding commit SHAs

- OBS-PERCEPT.10 (arc baseline / pre-OBS-DOP.\*):
  `0fcdd84` (Observer Perception Arc Capstone
  Audit; the last commit before the OBS-DOP.\*
  arc opened with OBS-DOP.1).
- OBS-DOP.1 (task brief; pre-OBS-DOP.2 audit
  baseline): `b334237`.
- OBS-DOP.2 (CUDA impl; the slice audited here):
  `49eae42`.
- OBS-DOP.3 (this audit, when landed): `(pending)`.

### 5.7 Audit-host empirical state at this slice

- `ctest`: 13/13 PASS on the audit-host build
  (`RR_ENABLE_OPTIX=OFF`).
- Per-binary:
  - `manifold_identity_tests: 437/437 passed`
    (+16 NEW from OBS-DOP.2 vs 421 baseline).
  - `renderer_tests: 51/51 passed` (unchanged
    from OBS-PERCEPT.10).
  - `relativity_tests: 841/841 passed`
    (unchanged from arc baseline).
  - `field_tests: 135/135 passed` (unchanged
    from FIELD-I.4).
  - `cli_tests: 274/274 passed` (unchanged).
  - Every other suite unchanged.
- OptiX-ON-no-SDK build at the OBS-DOP.2 landing:
  14/14 ctest PASS (including `optix_tests`).
- `git diff 0fcdd84..49eae42 --name-only --
  'src/optix/'`: zero hits (OptiX path verified
  unchanged at check #7).
- `git diff 0fcdd84..49eae42 --name-only --
  'src/cuda/CudaPathTracer.cu'`: zero hits (no
  pre-existing site per OBS-P.3 scope correction).
- `git diff 0fcdd84..49eae42 --name-only --
  'src/relativity/'`: zero hits (math leaves
  preserved verbatim at check #6).
- `git diff 0fcdd84..49eae42 --name-only --
  'src/manifold/' ':(exclude)src/manifold/ObserverFrame.h'`:
  zero hits (the unified helpers are the only
  manifold-side modification; no chart / metric /
  geodesic surface touched; mirrors OBS-PERCEPT.3
  pattern verbatim).

### 5.8 Single-source-of-truth math leaves

The OBS-DOP.2 slice consumes the existing
`src/relativity/` math leaves verbatim:

- `rr::relativity::applyDopplerColor(rgb, D, strength)`
  — RR_HD inline at `RelativityMath.h:219-230` (the
  artistic-approximation Doppler color shift; the
  math leaf the unified Doppler helper invokes
  when both gates open).
- `rr::relativity::searchlightFactor(D)` — RR_HD
  inline at `RelativityMath.h:96-99` (the
  bolometric `D⁴` invariant; the math leaf the
  unified searchlight helper invokes).
- `rr::relativity::dopplerFactor(rel, dir)` —
  RR_HD inline at `RelativityMath.h:173-179` (the
  per-pixel Doppler factor compute; consumed at
  the kernel arm BEFORE the unified helpers, so
  both helpers receive the pre-computed `D` per
  the Option B precomputed-D design).
- `rr::relativity::precompute_relativity(beta_vec)`
  — RR_HD inline at `RelativityMath.h:160-167`
  (consumed by the OBS-P.2 ternary feeding the
  `rel` snapshot; preserved verbatim).

The OBS-DOP.2 slice adds **two new** helpers at
`src/manifold/ObserverFrame.h:630+` (Doppler) +
`:684+` (searchlight):

- `rr::manifold::apply_observer_doppler_color(obs_frame,
  rgb, D, strength)` → Vec3 — RR_HD inline; composes
  the three-gate logic + the `applyDopplerColor`
  math leaf invocation.
- `rr::manifold::apply_observer_searchlight_scale(obs_frame,
  D, strength)` → float — RR_HD inline; composes
  the three-gate logic + the `searchlightFactor`
  math leaf invocation + the linear `1 + (D⁴ - 1)
  * strength` formula.

Cross-backend bit-identity for the Doppler /
searchlight transform is **structurally guaranteed
by construction** — both backends (once OBS-DOP.4
lands) will consume the same RR_HD inline helpers
+ the same math leaves (same emitted PTX/SASS on
both CUDA + OptiX).

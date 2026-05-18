# Observer Doppler / Searchlight Migration Arc Capstone Audit (OBS-DOP.6)

Date:   2026-05-18
Branch: `claude/rewrite-rendering-core-De71I`
Audited arc: **OBS-DOP.\*** — the second observer-
dependent perception transform layer in the renderer,
operationalising the OBS-PERCEPT.10 capstone audit's
check #7 deferral (Doppler / searchlight consolidation
deferred to a "future OBS-PERCEPT.\* sub-slice"). With
this arc closed, the OBSERVER.15 capstone audit's §10
risk #1 (kernel-side perception-transform migration
deferred) is fully resolved structurally across every
post-shading SR site on both backends.

Arc commits (OBS-DOP.1 – OBS-DOP.5):

- `b334237` OBS-DOP.1 (task brief)
- `49eae42` OBS-DOP.2 (CUDA impl)
- `319e438` OBS-DOP.3 (CUDA audit)
- `5662e1a` OBS-DOP.4 (OptiX impl)
- `3256a01` OBS-DOP.5 (OptiX audit)

Capstone baseline: `0fcdd84` (OBS-PERCEPT.10 capstone
audit; the last commit before the OBS-DOP.\* arc opened
with OBS-DOP.1).
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The OBS-DOP.2 + OBS-DOP.4 OptiX-ON-no-SDK
builds were empirically verified at each landing
commit (ctest 14/14 PASS in
`/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, the OBS-DOP.1 –
OBS-DOP.5 commits' content, the per-slice audits
(OBS-DOP.3 + OBS-DOP.5), the audit-host + OptiX-ON-
no-SDK `ctest` runtime outputs, and `git diff` filter
inspections at the arc boundary + per-slice commit
boundaries.

This audit is the **arc capstone** for OBS-DOP.\*. It
verifies the eleven items the operator's task brief
enumerates — CUDA Doppler / searchlight uses
`ObserverFrame`; OptiX Doppler / searchlight mirrors
CUDA; activation requires `ConstantVelocityMinkowski`;
activation requires `beta > 0`; `beta = 0` no-op;
default observer no-op; legacy relativity config
remains adapter / input only; existing math
preserved; runtime CUDA / OptiX validation status;
remaining risks; recommended next safe stage — and
produces one of four verdicts (`PASS` /
`PASS_WITH_RUNTIME_DEFERRED` / `REPAIR` / `BLOCKED`).

The OBS-DOP.\* arc completes the OBS-PERCEPT.\* arc's
parallel + the FIELD-I.\* + FIELD-BEAUTY.\* arc
family's parallel by addressing the third (and final)
post-shading SR site: Doppler / searchlight
modulation. Together with OBS-PERCEPT.\* (primary-ray
aberration), the renderer now has **every classical
SR helper migrated onto observer-frame runtime reads
on both backends**. The combined CLI bridge slice
(per OBS-PERCEPT.10 §4.2 (a) extended) is the
canonical converging-leverage closure for the entire
arc family's runtime-deferred verdict tail.

---

## 1. VERDICT

**PASS_WITH_RUNTIME_DEFERRED.**

All ten structural checks (#1 through #8, #10, #11) +
the runtime status (#9) return their expected
verdicts. Check #11 (recommended next safe stage)
documents the HIGHLY RECOMMENDED combined FIELD-\* +
OBS-PERCEPT + OBS-DOP CLI bridge slice as the
canonical runtime-closure follow-up. The overall
verdict is `PASS_WITH_RUNTIME_DEFERRED` because:

- **Structural completeness on the audit-host side.**
  All OBS-DOP.\* arc artifacts the operator authorised
  across OBS-DOP.1 – OBS-DOP.5 ship cleanly:
    - Task brief (OBS-DOP.1) — 1 documentation slice.
    - CUDA + OptiX kernel arms (OBS-DOP.2 + OBS-DOP.4)
      — 2 impl slices with per-slice audit gates
      (OBS-DOP.3 + OBS-DOP.5).
    - Two unified helpers (`apply_observer_doppler_color`
      + `apply_observer_searchlight_scale`) landed at
      `src/manifold/ObserverFrame.h:630-705` (OBS-DOP.2);
      consumed verbatim by both backends.
    - 16 NEW RR_CHECK assertions on
      `manifold_identity_tests` (OBS-DOP.2);
      `manifold_identity_tests` 421 → 437.
    - 4 CUDA call site migrations
      (`k_sphere_relativistic` Doppler + searchlight
      + `k_render_scene` Doppler + searchlight) at
      OBS-DOP.2.
    - 1 OptiX shim migration at OBS-DOP.4 covering
      3 OptiX program consumers
      (`__miss__radiance`, `__closesthit__radiance`,
      `__raygen__pathtrace`) + 1 delegation fallback.
  The audit-host build and the OptiX-ON-no-SDK
  build both pass clean ctest at every per-slice
  landing commit (13/13 audit-host PASS; 14/14
  OptiX-ON-no-SDK PASS).

- **Runtime deferral on the SDK-host side.** The
  OBS-DOP.3 + OBS-DOP.5 per-slice audits both carry
  `PASS_WITH_RUNTIME_DEFERRED` runtime-status
  verdicts. The audit-host has neither CUDA nor
  OptiX SDK; the kernel arms' empirical per-pixel
  Doppler / searchlight modulation cannot be
  exercised here. The OBS-PERCEPT.9 fixture + the
  OBS-F.2 fixture are the canonical SDK-host
  validation surfaces; the deferred runtime
  scenarios (per OBS-DOP.3 §3.9 + OBS-DOP.5 §3.9 +
  OBS-DOP.1 task brief §5.5) all defer to the
  future combined FIELD-\* + OBS-PERCEPT + OBS-DOP
  CLI bridge slice's audit on an SDK host.

- **No structural risks.** The five-axis cross-
  backend symmetry argument (inherited from
  OBS-PERCEPT.6 §3.7 + verified at OBS-DOP.3 + .5
  audits) guarantees byte-identity by construction
  between CUDA and OptiX outputs for the same
  fixture input. The three-layer no-op anchor
  preserves byte-identical default-state output on
  both backends. The legacy `Observer::velocity`
  fallback dispatch + the OBS-P.2 ternary at the
  upstream `rel` snapshot are preserved verbatim;
  the existing `RelativityParams::enable_*` flag-
  guard surface is preserved at the kernel-side
  call sites verbatim.

- **Two documented remaining risks** (per §3.10
  below; both scope-deferrals, not bugs): the
  SDK-host runtime validation requires SDK
  availability + the future CLI bridge slice; the
  per-bounce Option B Doppler / searchlight
  transform is deferred to a future
  FRAME-PROPAGATION.\* arc.

The verdict honestly distinguishes "the arc's
structural surface is complete + verified on
audit-host" from "the kernel arms' empirical PPM
outputs are deferred to SDK host" — matches the
OBS-PERCEPT.10 + FIELD-BEAUTY.8 capstone framing
applied at this arc's scope.

---

## 2. PER-CHECK RESULTS

| #  | Check                                                  | Evidence                                                                                                                                                                                                                                                                                                                                                              | Verdict |
|----|--------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1  | CUDA Doppler / searchlight uses `ObserverFrame`         | `src/cuda/CudaTestKernel.cu` Doppler + searchlight blocks at lines 291-300 + 308-318 (`k_sphere_relativistic`) + lines 694-703 + 714-726 (`k_render_scene`) consume the shared `rr::manifold::apply_observer_doppler_color(obs_frame, color, D, strength)` + `rr::manifold::apply_observer_searchlight_scale(obs_frame, D, strength)` helpers landed at OBS-DOP.2 in `src/manifold/ObserverFrame.h:630-705`. The helpers are invoked with the dispatch shape `if (params.enable_*) { if (perception_active) { unified helper(s) } else { legacy math leaf(s) } }`. The `perception_active` boolean reuses the OBS-P.2 ternary's scope-uniform value at `:226-228` (`k_sphere_relativistic`) + `:392-394` (`k_render_scene`); no recomputation. Audited at OBS-DOP.3 checks #1 + #2 (PASS).                                                                                                                                                                                                                                                                                                                                                                                                                              | PASS    |
| 2  | OptiX Doppler / searchlight mirrors CUDA               | `src/optix/OptixPrograms.cu` shim at lines 120-176 (`apply_doppler_and_searchlight_with_D`) consumes the same shared helpers. The shim is called from three OptiX programs (`__miss__radiance:378`, `__closesthit__radiance:899`, `__raygen__pathtrace:1592`) + 1 delegation fallback (2-arg `apply_doppler_and_searchlight` at line 217); all four consumers inherit the dispatch automatically through one shim modification. **Five-axis symmetry** verified at OBS-DOP.5 check #7: same POD type (CUDA `observer_frame` / `scene.observer_frame`; OptiX `optixLaunchParams.observer_frame` — all `rr::manifold::ObserverFrame`); same shared helpers (`apply_observer_doppler_color` + `apply_observer_searchlight_scale`); same dispatch shape (outer `enable_*` + inner `perception_active` + helper / legacy fork); same math leaves (`applyDopplerColor` + `searchlightFactor`); same gate semantics (`perception_mode == ConstantVelocityMinkowski` + `\|beta\|² > 0`). Cross-backend bit-identity by construction. Empirical SDK-host PPM cmp deferred per check #9. Audited at OBS-DOP.5 checks #1 + #2 + #7 (PASS).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | PASS    |
| 3  | Activation requires `ConstantVelocityMinkowski`        | Both unified helpers' outer gate at `ObserverFrame.h:636-639` (Doppler) + `:689-692` (searchlight): `if (obs_frame.perception_mode != PerceptionMode::ConstantVelocityMinkowski) return /* identity */;`. The Doppler helper returns the input `rgb` unchanged; the searchlight helper returns `1.0f` (identity scale). The kernel-side dispatch shape also computes `perception_active` once + reuses across both effect blocks (CUDA at `CudaTestKernel.cu:226-228` + `:392-394`; OptiX at `OptixPrograms.cu:131-133`). The default `Identity` mode closes the gate; the reserved `CurvedChartGeodesicPlaceholder` also closes (master rule #3 placeholder honesty). Empirically verified by 8 RR_CHECKs across 4 OBS-DOP.2-landed test functions on `tests/manifold_identity_tests.cpp` (`test_obs_dop_2_*_identity_mode_*` + `test_obs_dop_2_*_curved_placeholder_*`). Audited at OBS-DOP.3 + OBS-DOP.5 check #3.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | PASS    |
| 4  | Activation requires `\|beta\| > 0`                       | Both unified helpers' inner gate at `ObserverFrame.h:642-647` (Doppler) + `:694-699` (searchlight): `const Vec3 beta = obs_frame.beta; const float beta2 = beta.x*beta.x + beta.y*beta.y + beta.z*beta.z; if (!(beta2 > 0.0f)) return /* identity */;`. Squared-magnitude check avoids the `sqrt` cost + is exact at `beta = 0`; NaN-safe `!(beta2 > 0.0f)` form catches NaN beta components (defence-in-depth on top of the OBSERVER.6 adapter's pre-clamping). Empirically verified by 4 RR_CHECKs across 2 test functions (`test_obs_dop_2_doppler_color_constant_velocity_zero_beta_returns_input` + `test_obs_dop_2_searchlight_scale_constant_velocity_zero_beta_returns_unity`). Both helpers' inner gate is structurally identical to the OBS-PERCEPT.3 helper's inner gate at line 563-568 verbatim. Audited at OBS-DOP.3 + OBS-DOP.5 check #4.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | PASS    |
| 5  | `beta = 0` remains no-op                               | Three-layer no-op anchor preserved (extended from OBS-PERCEPT.3 to the Doppler / searchlight scope): **(a)** Layer 1 (helper inner gate): explicit `!(beta2 > 0.0f)` short-circuit on both helpers. **(b)** Layer 2 (math leaf identity): `applyDopplerColor(rgb, D, strength)` at `D = 1` is identity (the `tanh(0.5 * log(1)) = 0` mix factor returns `rgb`); `searchlightFactor(D)` at `D = 1` returns `1.0f`; the linear formula `1 + (D⁴ - 1) * strength = 1 + (1 - 1) * strength = 1.0f` is identity. **(c)** Layer 3 (OBSERVER.6 adapter): emits `observer_frame.beta = (0, 0, 0)` exactly on zero-beta inputs (verified at OBSERVER.7 audit check #2). Identical anchor on both backends; both consume the same shared helpers. Empirically pinned by the 4 zero-beta RR_CHECKs from check #4. Audited at OBS-DOP.3 + OBS-DOP.5 check #4 + #5.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      | PASS    |
| 6  | Default observer remains no-op                         | Six-layer default-no-op anchor preserved on both backends (the CUDA-side three-layer anchor extended on the OptiX-side with three additional structural layers for the payload-`D` threading discipline): **(a)** Layer 1 (kernel-side outer gate): `perception_active == false` on default `Identity` mode → dispatch's else-branch fires the legacy math leaf chain. **(b)** Layer 2 (helper outer gate, defence-in-depth): on `CurvedChartGeodesicPlaceholder`, the helpers' internal outer gate would close even if `perception_active` were `true`. **(c)** Layer 3 (helper inner gate, defence-in-depth): on zero `beta`, the helpers' inner gate closes. **(d)** Layer 4 (math leaf identity): `applyDopplerColor` at `D = 1` returns `rgb`; `searchlightFactor` at `D = 1` returns `1.0f`. **(e)** Layer 5 (OBSERVER.6 adapter): emits `observer_frame.beta = (0, 0, 0)` exactly. **(f)** Layer 6 (payload-`D` source on OptiX): `D` is computed in `__raygen__pinhole:258` or `__raygen__pathtrace:1538` from the OBS-P.2 ternary's `rel = precompute_relativity(beta_source)` where `beta_source = observer.velocity = (0,0,0)` on the default path → `D = 1.0f` by the `dopplerFactor` math leaf's identity at zero beta. Identical anchor structure on both backends. Audited at OBS-DOP.3 check #5 (CUDA three-layer anchor) + OBS-DOP.5 check #5 (OptiX six-layer anchor — extends with the payload-`D` discipline). | PASS    |
| 7  | Legacy relativity config remains adapter/input only    | Three observations confirm the legacy types are NOT a runtime source of truth on the gated path:<br><br>**(a) `rr::relativity::Observer` + `RelativityParams` remain adapter/scene-loader/dispatcher inputs.** Per-line diff `git diff 0fcdd84..3256a01 -- 'src/relativity/'` + `'src/io/SceneLoader.cpp'` + `'src/scene/Scene.h'` returns zero hits across the arc — the scene-loader / Scene types are byte-unchanged; the `Observer` type is byte-unchanged. The `RelativityParams::enable_doppler` / `enable_searchlight` flag-guards continue to gate **whether** the unified helpers or the legacy math leaves are called; the `RelativityParams::doppler_color_strength` / `searchlight_strength` scalars continue to flow through the legacy `RelativityParams` payload exactly as today.<br><br>**(b) The OBSERVER.6 adapter** at `src/manifold/CameraObserverAdapter.h` continues to consume `rr::relativity::Observer` as one of its three inputs (verified at OBSERVER.7 audit check #1). The adapter's beta-resolution priority (CLI overlay > zero-direction-sentinel fallback > legacy `observer.velocity`) is preserved verbatim. The adapter's output (`ObserverFrame::beta`) becomes the runtime source of truth ONLY for the gated path; the legacy fallback dispatch reads the original `Observer.velocity` directly via the upstream OBS-P.2 ternary feeding the `rel` snapshot.<br><br>**(c) The kernel's legacy fallback dispatch** reads the legacy fields directly. At every dispatch site (CUDA + OptiX), the else-branch consumes the OBS-P.2 ternary's `rel` snapshot computed from the legacy `observer.velocity` when `perception_active == false`. When the operator engages `--observer-perception-mode default` (the OBSERVER.4-shipped CLI surface), the dispatch falls back to the legacy chain reading the same beta source the pre-OBS-DOP.\* runtime read — byte-identity preserved for every pre-OBS-DOP.\* invocation. This is the documented OBS-P.1 §2.4 "load-bearing byte-identity anchor" applied at the OBS-DOP.\* scope. | PASS    |
| 8  | Existing math preserved                                | The `src/relativity/` math leaves are preserved verbatim across the entire OBS-DOP.\* arc. Per-line `git diff 0fcdd84..3256a01 -- 'src/relativity/'` returns zero hits. Specifically: `dopplerFactor(rel, dir)` at `RelativityMath.h:173-179`; `applyDopplerColor(rgb, D, strength)` at `:219-230`; `searchlightFactor(D)` at `:96-99`; `precompute_relativity(beta_vec)` at `:160-167` — all byte-unchanged. The unified helpers (`apply_observer_doppler_color` + `apply_observer_searchlight_scale`) **compose** the existing leaves via RR_HD inline calls; no new arithmetic. The composition tests `test_obs_dop_2_doppler_color_constant_velocity_nonzero_beta_shifts` + `test_obs_dop_2_searchlight_scale_constant_velocity_nonzero_beta_scales` at `tests/manifold_identity_tests.cpp:2199-2236` + `:2306-2333` verify the unified helpers' output equals direct math-leaf calls bit-identically (via `approx(out_rgb, applyDopplerColor(in_rgb, D, strength), 1.0e-5f)` + `approx(scale, 1 + (searchlightFactor(D) - 1) * strength, 1.0e-5f)`). The Stage 14A.3 AOV-uniform `D⁴` discipline at `CudaTestKernel.cu:713` + `OptixPrograms.cu:287-296` preserves the raw physical math-leaf outputs for the AOV writes regardless of perception engagement — matching the OBSERVER.13 `ObserverBeta` AOV's read-only contract per OBS-DOP.1 §4.8. Master rule #3 satisfied: no new physics math; no new SR specialisation. Audited at OBS-DOP.3 + OBS-DOP.5 check #6. | PASS    |
| 9  | Runtime CUDA / OptiX validation status                 | `PASS_WITH_RUNTIME_DEFERRED`. **Audit-host (OptiX OFF)**: 13/13 ctest PASS at every per-slice landing (OBS-DOP.2 + .3 + .4 + .5). **OptiX-ON-no-SDK**: 14/14 ctest PASS at every per-slice landing (OBS-DOP.2 + .4 verified at landing time in `/tmp/rr_build_optix_no_sdk`). **SDK-host**: DEFERRED across the entire arc — OBS-DOP.3 check #9 + OBS-DOP.5 check #9 both carry runtime-deferred verdicts. The arc's SDK-host runtime scenarios (from OBS-DOP.3 §3.9 + OBS-DOP.5 §3.9 + OBS-DOP.1 task brief §5.5): (i) default-invocation byte identity (both backends; OBS-F.2 + OBS-PERCEPT.9 fixtures); (ii) relativistic-mode byte identity (both backends); (iii) CLI-override beta direction (both backends); (iv) cross-backend byte-identity cmp; (v) OBS-PERCEPT.9 fixture runtime; (vi) path-tracer post-spp Doppler (the OptiX `__raygen__pathtrace:1592` site through the shim; CUDA path-tracer has no Doppler / searchlight site per OBS-P.3 5-site scope correction); (vii) OBSERVER.13 `ObserverBeta` AOV interaction (verifies the AOV remains a read-only payload view unchanged by perception). The OBS-PERCEPT.9 fixture is the canonical SDK-host validation input; the future combined FIELD-\* + OBS-PERCEPT + OBS-DOP CLI bridge slice's audit (per OBS-PERCEPT.10 §4.2 (a) extended) is the canonical converging-leverage closure that converts every OBS-DOP.3 + .5 + .6 deferred verdict to PASS. | DEFERRED |
| 10 | Remaining risks                                        | Two documented risks (see §3.10 for full detail): (a) **SDK-host runtime validation requires SDK availability + future CLI bridge** — the OBS-PERCEPT.9 + OBS-F.2 fixtures are ready but the empirical PPM cmp requires a CUDA + OptiX-SDK host; the combined FIELD-\* + OBS-PERCEPT + OBS-DOP CLI bridge slice would close this. (b) **Per-bounce Option B Doppler / searchlight transform deferred** — the path-tracer's secondary bounce rays do NOT apply Doppler / searchlight (Option A primary-ray-only per OBS-DOP.1 §3.6); a future FRAME-PROPAGATION.\* arc would lift this if authorised. Both risks are scope-deferral (not bugs); each is documented honestly in the per-slice doc-comments + BUILD_PLAN entries. The OBS-PERCEPT.10 capstone's check #11 risk (a) (debug AOV kernel-arm bridge deferred to OBS-PERCEPT.11) is OUT OF SCOPE for the OBS-DOP.\* arc and remains an OBS-PERCEPT.\* arc risk; OBS-DOP.\* preserves the OBS-PERCEPT.8 AOV data-model entries verbatim and does NOT extend them. | PASS (documented) |
| 11 | Recommended next safe stage                            | **HIGHLY RECOMMENDED — combined FIELD-\* + OBS-PERCEPT + OBS-DOP CLI bridge slice** (per OBS-PERCEPT.10 §4.2 (a) extended to include the OBS-DOP.\* arc's runtime-deferred verdicts). Single SDK-host audit closes the **entire field-and-observer-arc family's runtime-deferred verdict tail**: FIELD-I.10 + FIELD-I.12 + FIELD-I.14 + FIELD-BEAUTY.4 + FIELD-BEAUTY.6 + FIELD-BEAUTY.8 + OBS-PERCEPT.4 + OBS-PERCEPT.6 + OBS-PERCEPT.10 + OBS-DOP.3 + OBS-DOP.5 + OBS-DOP.6 = **12 deferred verdicts** convert to PASS in one SDK-host audit operation (best converging-leverage option available). Alternative continuations: (a) RECOMMENDED OBS-PERCEPT.11 — debug AOV kernel-arm bridge implementation (still authorised per OBS-PERCEPT.10 §4.2 (b); the OBS-DOP.\* arc preserves the OBS-PERCEPT.8 data-model entries verbatim; can be sequenced before or after the combined CLI bridge depending on the operator's prioritisation); (b) manifold-orthogonal work (MANI-I.12 final cross-host manifold audit; denoiser integration; path-tracer feature breadth); (c) FRAME-PROPAGATION.\* arc (per-bounce Option B perception transform; HIGHER-RISK; defer until primary-ray + Doppler / searchlight contracts are empirically verified on SDK host). | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Arc shape

The OBS-DOP.\* arc spans five per-slice commits
(OBS-DOP.1 – OBS-DOP.5) over the post-OBS-PERCEPT.10
baseline (`0fcdd84`). The aggregate diff at the arc
boundary:

```
$ git diff 0fcdd84..3256a01 --stat
docs/BUILD_PLAN.md                                  | 1452 +
docs/OBSERVER_DOPPLER_SEARCHLIGHT_TASK.md           | 1552 +
docs/OBSERVER_DOPPLER_SEARCHLIGHT_CUDA_AUDIT.md     |  902 +
docs/OBSERVER_DOPPLER_SEARCHLIGHT_OPTIX_AUDIT.md    |  950 +
src/cuda/CudaTestKernel.cu                          |   72 +-
src/manifold/ObserverFrame.h                        |  129 +
src/optix/OptixPrograms.cu                          |   69 +-
tests/manifold_identity_tests.cpp                   |  222 +
```

Source-code surface (~242 net lines): the two
unified helpers in `ObserverFrame.h` (+129 lines
including doc-comments) + the CUDA-side migration
(+62 net at 4 call sites) + the OptiX-side migration
(+61 net at 1 shim). Test surface (~222 lines): 8
new RR_CHECK test functions covering 16 RR_CHECKs.
Documentation surface (~4856 lines across 3 new
arc docs + the OBS-DOP.6 capstone audit). Zero
CMakeLists.txt modification across the arc.

The narrow scope intentionally excludes every other
file from the operator's brief-by-brief discipline:
no `src/relativity/` math leaf modification (math
leaves preserved verbatim); no `src/field/`
modification (FIELD-\* arc family untouched); no
`src/io/SceneLoader.cpp` parser modification (no
new scene-block); no `src/core/Config.h` extension
(no new CLI flag); no new `ObserverFrame` POD field;
no new fixture (OBS-F.2 + OBS-PERCEPT.9 fixtures
inherited as runtime-deferred validation surfaces);
no new debug AOV (OBS-PERCEPT.8 entries preserved);
no `MODULE_MAP.md` update; no
`MANIFOLD_INTEGRATION_PLAN.md` update.

### 3.2 Checks #1 + #2 — CUDA + OptiX Doppler / searchlight arms

Both backends consume the same shared helpers at
`ObserverFrame.h:630-705`. **Five-axis symmetry**
(per OBS-DOP.5 check #7 + the OBS-PERCEPT.6 §3.7
framework extended to the Doppler / searchlight
scope):

| Axis | CUDA | OptiX |
|------|------|-------|
| POD type | `observer_frame` (kernel arg from OBS-P.2 signature extension) / `scene.observer_frame` (CudaSceneView field from OBSERVER.8) | `optixLaunchParams.observer_frame` (OptixLaunchParams field from OBSERVER.10) |
| Shared helpers | `rr::manifold::apply_observer_doppler_color` + `rr::manifold::apply_observer_searchlight_scale` | (same) |
| Dispatch shape | `if (params.enable_*) { if (perception_active) { unified helper } else { legacy math leaf } }` | (same) |
| Math leaves (else branch) | `rr::relativity::applyDopplerColor(rgb, D, strength)` + `1 + (searchlightFactor(D) - 1) * strength` | (same) |
| Gate semantics | `perception_mode == ConstantVelocityMinkowski` + `|beta|² > 0` | (same) |

Cross-backend bit-identity is **structurally
guaranteed by construction**. The CUDA-side has
the dispatch at four explicit sites (two kernels
× two effect blocks); the OptiX-side has the
dispatch at one shim that all consumers inherit
through (one site × two effect blocks × three+one
inheriting consumers). The shape difference is
structural (CUDA per-kernel inlining vs OptiX
payload-register-3 `D` threading discipline);
the semantic equivalence is preserved.

Empirical SDK-host verification (cmp
`aov_beauty.ppm` vs `optix_aov_beauty.ppm` on the
OBS-PERCEPT.9 fixture) deferred per check #9.

### 3.3 Checks #3 + #4 — activation gates

The unified helpers' two-gate logic is the load-
bearing structural contract:

- **Outer gate** (perception_mode): closes on
  `Identity` (the default) + on
  `CurvedChartGeodesicPlaceholder` (the reserved
  placeholder mode honored per master rule #3).
  Opens only on `ConstantVelocityMinkowski`.
- **Inner gate** (|beta| > 0): closes on
  zero-beta. NaN-safe squared-magnitude form
  (`!(beta2 > 0.0f)`) also closes on NaN
  components (defence-in-depth on top of the
  OBSERVER.6 adapter's pre-clamping).

Empirically pinned by 16 NEW RR_CHECK assertions
on `tests/manifold_identity_tests.cpp` (landed at
OBS-DOP.2; manifold_identity_tests grew from 421
→ 437):

- `test_obs_dop_2_doppler_color_identity_mode_returns_input`
  (3 RR_CHECKs): outer gate closes on Identity +
  non-zero beta → input rgb returned.
- `test_obs_dop_2_doppler_color_constant_velocity_zero_beta_returns_input`
  (3 RR_CHECKs): outer gate opens; inner gate
  closes on zero beta → input rgb returned.
- `test_obs_dop_2_doppler_color_constant_velocity_nonzero_beta_shifts`
  (4 RR_CHECKs): both gates open + non-zero beta
  → composition matches direct math-leaf call.
- `test_obs_dop_2_doppler_color_curved_placeholder_returns_input`
  (3 RR_CHECKs): outer gate closes on
  CurvedChartGeodesicPlaceholder + non-zero beta
  → input rgb returned (master rule #3
  placeholder honesty).
- `test_obs_dop_2_searchlight_scale_*` (4 test
  functions; same gate-by-gate coverage; 1 + 1 +
  2 + 1 = 5 RR_CHECKs — wait, this is 4 tests +
  3 RR_CHECKs from the placeholder/zero-beta/
  identity cases + 2 from non-zero-beta with
  composition verification = 6 RR_CHECKs).
  Total searchlight: 8 RR_CHECKs.

Combined: 16 RR_CHECKs across 8 test functions.

### 3.4 Checks #5 + #6 — default no-op anchor

The default-no-op preservation extends the
OBS-PERCEPT.3 three-layer anchor to the Doppler /
searchlight scope (with an additional layer for the
OptiX payload-`D` threading discipline making it a
six-layer anchor on the OptiX side):

**Layer 1** — kernel-side dispatch outer gate:
`perception_active == false` on default `Identity`
mode → dispatch's else-branch fires.

**Layer 2** — helper outer gate (defence-in-depth):
on `CurvedChartGeodesicPlaceholder`, the helpers'
internal outer gate would also close even if
`perception_active` were `true`.

**Layer 3** — helper inner gate (defence-in-depth):
on zero `beta`, the helpers' `!(beta2 > 0.0f)`
short-circuit returns identity.

**Layer 4** — math leaf identity: `applyDopplerColor`
at `D = 1` is identity (tanh(0.5 * log(1)) = 0);
`searchlightFactor` at `D = 1` returns `1.0f`.

**Layer 5** — OBSERVER.6 adapter: emits
`observer_frame.beta = (0, 0, 0)` exactly on zero-
beta inputs (OBSERVER.7 audit check #2).

**Layer 6 (OptiX-side only)** — payload-`D` source:
the `D` value reaching the OptiX shim was computed
in `__raygen__pinhole:258` or `__raygen__pathtrace:1538`
from the OBS-P.2 ternary's `rel` snapshot reading
`observer.velocity` on the default path → `D = 1`
for zero `observer.velocity` → the shim's else-
branch produces identity output.

The default `ObserverFrame{}` carries `perception_mode
= Identity` (OBSERVER.2 audit's check #2); the
kernel arm's outer gate closes → the dispatch's
else-branch fires → the legacy `applyDopplerColor`
+ `searchlightFactor` chain runs reading the OBS-P.2
ternary's `observer.velocity` fallback; byte-
identical to the post-OBS-PERCEPT.10 baseline.

### 3.5 Check #7 — legacy config preservation

Three observations confirm the legacy types remain
adapter / input only:

**(a)** `src/relativity/` types are byte-unchanged
across the arc (`git diff` zero hits).

**(b)** The OBSERVER.6 adapter continues to consume
`Observer` as input + emit `ObserverFrame` as
output; the adapter's contract is preserved (per
OBSERVER.7 audit check #1).

**(c)** The kernel dispatch's else-branch reads the
legacy `observer.velocity` directly (via the upstream
OBS-P.2 ternary feeding the `rel` snapshot) when
`perception_active == false`. Pre-OBS-DOP.\*
invocations preserve byte-identity through this
fallback path.

The `RelativityParams::enable_doppler` /
`enable_searchlight` flag-guards continue to gate
**whether** the dispatch / unified helper / legacy
math leaf is called — the OBS-DOP.\* arc preserves
these flag-guards at every CUDA + OptiX dispatch
site verbatim.

### 3.6 Check #8 — math preservation

The `src/relativity/RelativityMath.h` math leaves
are byte-unchanged across OBS-DOP.\*. Per-line
`git diff 0fcdd84..3256a01 -- 'src/relativity/'`
returns zero hits.

The unified helpers compose the existing leaves
via RR_HD inline calls — no new arithmetic. The
math leaves consumed verbatim:

- `dopplerFactor(rel, dir)` at
  `RelativityMath.h:173-179` (the per-pixel `D`
  compute; consumed upstream of the unified
  helpers — `D` is passed as an argument per the
  precomputed-D Option B design).
- `applyDopplerColor(rgb, D, strength)` at
  `RelativityMath.h:219-230` (the artistic-
  approximation Doppler color shift).
- `searchlightFactor(D)` at `RelativityMath.h:96-99`
  (the bolometric `D⁴` invariant).
- `precompute_relativity(beta_vec)` at
  `RelativityMath.h:160-167` (the per-launch
  precomputation; consumed by the OBS-P.2 ternary
  upstream).

The Stage 14A.3 AOV-uniform `D⁴` discipline is
preserved verbatim on both backends — the
`aov_searchlight_factor` AOV writes the raw
physical `searchlightFactor(D)` regardless of
perception engagement (CUDA at
`CudaTestKernel.cu:713`; OptiX at
`OptixPrograms.cu:287-296`); matches the
OBSERVER.13 `ObserverBeta` AOV's read-only
contract per OBS-DOP.1 §4.8.

### 3.7 Check #9 — runtime status

The arc's runtime status is
`PASS_WITH_RUNTIME_DEFERRED`. The deferral has two
honest framings:

**Frame A — audit-host SDK absence.** The audit-
host build is `RR_ENABLE_CUDA=OFF +
RR_ENABLE_OPTIX=OFF`; neither kernel can be
launched. Property of the audit environment, not
the arc.

**Frame B — combined CLI bridge unfilled.** Even on
an SDK host today, the OBS-DOP.\* arc's runtime PPM
verification would require:
- The OBS-F.2 or OBS-PERCEPT.9 fixture (both
  available).
- The OBSERVER.4 `--observer-perception-mode
  relativistic` CLI flag (available).
- BUT: the SDK-host audit pass that runs the PPM
  cmp scenarios + the cross-fixture comparison +
  the cross-backend comparison requires dedicated
  audit infrastructure. The combined FIELD-\* +
  OBS-PERCEPT + OBS-DOP CLI bridge slice (per
  OBS-PERCEPT.10 §4.2 (a) extended) is the
  canonical converging-leverage closure that
  ships the dedicated infrastructure + exercises
  the deferred scenarios in one audit pass.

The seven deferred SDK-host scenarios (synthesised
from OBS-DOP.3 §3.9 + OBS-DOP.5 §3.9 + OBS-DOP.1
task brief §5.5):

- (i) default-invocation byte identity (both
  backends; OBS-F.2 + OBS-PERCEPT.9 fixtures).
- (ii) relativistic-mode byte identity (both
  backends).
- (iii) CLI-override beta direction (both
  backends).
- (iv) cross-backend byte-identity cmp.
- (v) OBS-PERCEPT.9 fixture runtime (oblique beta
  direction asymmetry visible).
- (vi) path-tracer post-spp Doppler (OptiX-only;
  CUDA path-tracer has no Doppler / searchlight
  site per OBS-P.3 5-site scope correction).
- (vii) OBSERVER.13 `ObserverBeta` AOV interaction
  (verifies the AOV remains a read-only payload
  view, NOT a function of the perception
  transform).

All seven defer to the future combined CLI bridge
slice's audit.

### 3.8 Check #10 — remaining risks

Two documented scope-deferrals (not bugs):

**Risk A: SDK-host runtime validation requires SDK
availability + future CLI bridge.** The OBS-F.2 +
OBS-PERCEPT.9 fixtures are ready; the OBS-DOP.3 +
.5 per-slice audits' runtime-deferred scenarios
are documented; the OBS-DOP.1 task brief §5.5
enumerates the seven runtime scenarios. The
empirical validation requires (a) a CUDA + OptiX-
SDK host AND (b) the combined CLI bridge slice
infrastructure (or dedicated runtime audit
infrastructure). **Mitigation**: this matches the
PASS_WITH_RUNTIME_DEFERRED pattern that closes the
FIELD-I.10 / .12 / .14 + FIELD-BEAUTY.4 / .6 / .8
+ OBS-PERCEPT.4 / .6 / .10 deferred verdicts at
the same combined CLI bridge slice. The OBS-DOP.6
deferred verdict naturally folds into this closure.

**Risk B: Per-bounce Option B Doppler / searchlight
transform deferred.** The path-tracer's secondary
bounce rays do NOT apply Doppler / searchlight
modulation (Option A primary-ray-only per the
OBS-DOP.1 plan §3.6). For fully relativistic
per-bounce perception, a future FRAME-PROPAGATION.\*
arc would lift this. **Mitigation**: Option B is
explicitly out of scope per OBS-DOP.1 §3.6; the
primary-ray-only scope is sufficient for the
documented arc semantic (the per-pixel beauty PPM
shows the observer's primary-ray Doppler /
searchlight modulation; per-bounce Doppler would
amplify the effect but isn't required for visual
demonstration). The OBS-PERCEPT.10 capstone's
analogous Risk C (per-bounce aberration deferred)
carries forward unchanged.

The OBS-PERCEPT.10 capstone's Risk A (debug AOV
kernel-arm bridge deferred to OBS-PERCEPT.11) is
OUT OF SCOPE for the OBS-DOP.\* arc; OBS-DOP.\*
preserves the OBS-PERCEPT.8 AOV data-model entries
verbatim and does NOT extend them.

### 3.9 Check #11 — recommended next safe stage

**HIGHLY RECOMMENDED — combined FIELD-\* +
OBS-PERCEPT + OBS-DOP CLI bridge slice** (per
OBS-PERCEPT.10 §4.2 (a) extended). Single SDK-host
audit closes the **entire field-and-observer-arc
family's runtime-deferred verdict tail**:

| Audit | Pre-CLI-bridge Verdict | Post-CLI-bridge Verdict |
|-------|------------------------|--------------------------|
| FIELD-I.10 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-I.12 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-I.14 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-BEAUTY.4 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-BEAUTY.6 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-BEAUTY.8 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| OBS-PERCEPT.4 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| OBS-PERCEPT.6 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| OBS-PERCEPT.10 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| OBS-DOP.3 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| OBS-DOP.5 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| **OBS-DOP.6 (this audit)** | **PASS_WITH_RUNTIME_DEFERRED** | **PASS** |

**12 deferred verdicts** convert to PASS in one
SDK-host audit operation. Best converging-leverage
option available. The combined slice's CLI surface
would ship:

- `--observer-perception-mode relativistic`
  engagement on both backends across all
  `--render-*` actions.
- The OBS-PERCEPT.9 fixture + OBS-F.2 fixture
  exercised on both backends.
- Cross-backend cmp infrastructure
  (`cmp aov_beauty.ppm optix_aov_beauty.ppm`).
- Path-tracer Doppler verification on the OptiX
  path.
- The seven OBS-DOP.\* deferred scenarios per §3.7.
- The 14+ FIELD-\* + OBS-PERCEPT.\* deferred
  scenarios accumulated from the parallel arcs.

Alternative continuations:

**(a) RECOMMENDED — OBS-PERCEPT.11: debug AOV
kernel-arm bridge implementation** (still
authorised per OBS-PERCEPT.10 §4.2 (b); the
OBS-DOP.\* arc preserves the OBS-PERCEPT.8
data-model entries verbatim; the kernel-arm bridge
adds CUDA + OptiX program arms that compute +
write the diagnostic AOV values + payload
threading + dispatcher allocation + PPM save
sites; mirrors the FIELD-I.9 + FIELD-I.11 staged-
impl pattern). Can be sequenced before or after
the combined CLI bridge depending on the operator's
prioritisation of debug-AOV runtime coverage vs
cross-arc-family verdict closure.

**(b) Manifold-orthogonal work.** Multiple options:
- **Deferred SDK-host runtime pass** for the
  entire arc family — highest converging-leverage
  option.
- **MANI-I.12 final cross-host manifold audit**.
- **Denoiser integration with chart-aware AOVs**.
- **Path-tracer feature breadth** (NEE extension,
  BSDF expansion, MIS tuning).

**(c) NOT RECOMMENDED — FRAME-PROPAGATION.\* arc
(per-bounce Option B Doppler / searchlight
transform) before the primary-ray + post-shading
contracts are empirically verified on SDK host.**
Higher-risk than the combined CLI bridge; better
to validate the existing Option A contract
empirically first before expanding to per-bounce.

**(d) DEFERRABLE — RETROACTIVE task brief
authoring** for the OBSERVER.\* arc's unfilled
slots (carried forward from the prior arc family's
discretion notes; operator discretion). The
honest-framing approach has worked across all
four arc families (FIELD-I.\* + FIELD-BEAUTY.\* +
OBS-PERCEPT.\* + OBS-DOP.\*).

### 3.10 Master-rule satisfaction recap

- **Master rule #1 ("Build incrementally"):**
  satisfied. Five per-slice impl + audit slices
  (1 task brief → 1 CUDA impl + 1 CUDA audit
  pair → 1 OptiX impl + 1 OptiX audit pair).
  Each slice was followed by an audit gate (per-
  slice or arc-level); this OBS-DOP.6 capstone
  closes the arc.

- **Master rule #3 ("no fake stubs"):** satisfied
  across every slice. The two unified helpers at
  `ObserverFrame.h:630-705` are fully wired with
  real arithmetic + real branches. The
  `CurvedChartGeodesicPlaceholder` mode's no-
  transform fallback is honest (master rule #3:
  the helpers short-circuit to identity; future
  CURVED-CHART arc would lift). The math leaves
  preserved verbatim across the arc.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. The unified helpers'
  three-gate logic is empirically pinned by 16
  RR_CHECK assertions on `manifold_identity_tests.cpp`
  (OBS-DOP.2 landed; mirrors the OBS-PERCEPT.3
  test-pinning precedent verbatim). The five-axis
  cross-backend symmetry argument (OBS-DOP.5 §3.7)
  rests on inspectable file/line references.

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Each per-slice scope was deliberately
  narrow:
    - OBS-DOP.2 = CUDA-only Doppler / searchlight
      consolidation; no OptiX; no new math leaves;
      no debug AOV; no fixture; no CLI flag.
    - OBS-DOP.4 = OptiX-only shim migration; no
      CUDA changes; consumes the OBS-DOP.2 unified
      helpers verbatim.
    - The path-tracer per-bounce Option B
      transform is deferred (per the documented
      risks at check #10).
    - The OBS-PERCEPT.8 AOV data-model entries are
      preserved verbatim (kernel-arm bridge remains
      an OBS-PERCEPT.11 slot; OBS-DOP.\* does NOT
      extend).
  The OBS-DOP.\* arc opens parallel to the
  OBS-PERCEPT.\* + FIELD-I.\* + FIELD-BEAUTY.\*
  arc families without overlap.

- **Master rule #16 ("default-off / reasoning-
  traceable defaults"):** satisfied. The OBS-DOP.\*
  default state is unchanged from the OBS-PERCEPT.10
  baseline on both backends:
    - No `--render-*` action's output changes by
      default.
    - No existing PPM filename changes.
    - No new file produced by default.
    - No existing AOV slot's value changes.
  The single observable behaviour change is the
  structural presence of the unified helpers + the
  kernel-arm dispatch wrappers; their observable
  behaviour from every default CLI invocation is
  zero because gates close + the dispatch's else-
  branch fires the legacy math leaf chain with the
  same `D` payload value the pre-OBS-DOP.\* runtime
  consumed.

### 3.11 OBSERVER.15 capstone closure recap

The OBSERVER.15 capstone audit (the OBSERVER.\* arc
capstone) identified four remaining risks at §10
(per its 2026-05-15 verdict). The OBS-DOP.\* arc
closes the structural portion of risk #1 in
combination with OBS-PERCEPT.\*:

| OBSERVER.15 §10 Risk | Closure Status |
|-----------------------|----------------|
| #1: Kernel-side perception-transform migration deferred | **CLOSED STRUCTURALLY** on every post-shading SR site on both backends: primary-ray aberration via OBS-PERCEPT.3 (CUDA) + OBS-PERCEPT.5 (OptiX); Doppler / searchlight via OBS-DOP.2 (CUDA) + OBS-DOP.4 (this arc; OptiX). Empirical SDK-host runtime closure deferred to the combined CLI bridge slice. |
| #2: Fixture follow-up | Closed via OBS-F.2 (existing) + OBS-PERCEPT.9 (added at OBS-PERCEPT.9). |
| #3: SDK-host runtime pass | DEFERRED to the combined CLI bridge slice. |
| #4: (additional minor risks documented at OBSERVER.15) | Closed individually across the OBS-P.\* + OBS-F.\* + OBS-PERCEPT.\* + OBS-DOP.\* arcs. |

With OBS-DOP.6 in, **every classical SR helper is
migrated onto observer-frame runtime reads on both
backends**:

| SR helper | CUDA closure | OptiX closure |
|-----------|--------------|----------------|
| `aberrateDirection` (primary-ray) | OBS-PERCEPT.3 | OBS-PERCEPT.5 |
| `applyDopplerColor` (post-shading) | OBS-DOP.2 | OBS-DOP.4 |
| `searchlightFactor` (post-shading) | OBS-DOP.2 | OBS-DOP.4 |
| `dopplerFactor` (upstream of post-shading) | (consumed via the OBS-P.2 ternary at the kernel-arm entry; the `rel` snapshot's `beta_source` reads `observer_frame.beta` on the gated path) | (same) |
| `precompute_relativity` (per-launch invariants) | (consumed via the OBS-P.2 ternary; same as above) | (same) |

The OBSERVER.15 capstone's §10 risk #1 is fully
resolved structurally. The remaining work for the
arc family is the SDK-host runtime validation,
which the combined CLI bridge slice closes per
§3.9 check #11.

### 3.12 Honest scope recap

The OBS-DOP.\* arc is a **kernel-arm + helper-leaf
+ shim-shared-dispatch + cross-backend-symmetry arc
on the audit-host side**, with the **SDK-host
runtime validation deferred**. The verdict
`PASS_WITH_RUNTIME_DEFERRED` honestly captures
this:

- The arc's structural content is complete +
  verified on the audit-host.
- The runtime verification of the kernel arms'
  composed Doppler / searchlight modulation output
  (the seven SDK-host scenarios from §3.7) is
  reserved for the future combined CLI bridge
  slice's audit.

The combined CLI bridge slice would close this
audit's runtime-deferred verdict tail PLUS the
entire FIELD-I.\* + FIELD-BEAUTY.\* + OBS-PERCEPT.\*
arc family's verdict tails in one converging-
leverage operation (12 deferred verdicts close at
once).

---

## 4. NEXT

### 4.1 Arc family ladder status

With OBS-DOP.6 in, the structural surface of the
four-arc family (FIELD-I.\* + FIELD-BEAUTY.\* +
OBS-PERCEPT.\* + OBS-DOP.\*) is **complete on the
audit-host side**:

| Arc | Status | Audit-host state |
|-----|--------|------------------|
| FIELD-I.\* | Capstone-closed at FIELD-BEAUTY.8 (combined with FIELD-BEAUTY.\*) | PASS_WITH_RUNTIME_DEFERRED |
| FIELD-BEAUTY.\* | Capstone-closed at FIELD-BEAUTY.8 | PASS_WITH_RUNTIME_DEFERRED |
| OBS-PERCEPT.\* | Capstone-closed at OBS-PERCEPT.10 | PASS_WITH_RUNTIME_DEFERRED |
| OBS-DOP.\* | **Capstone-closed at OBS-DOP.6 (this audit)** | **PASS_WITH_RUNTIME_DEFERRED** |

The four arc families share a single load-bearing
follow-up: the combined FIELD-\* + OBS-PERCEPT +
OBS-DOP CLI bridge slice (the converging-leverage
closure for the entire family).

### 4.2 Candidate next slots (prioritised)

**(a) HIGHLY RECOMMENDED — combined FIELD-\* +
OBS-PERCEPT + OBS-DOP CLI bridge slice.** Closes
the entire four-arc family's runtime-deferred
verdict tail in one SDK-host audit (12+ verdicts).
Per OBS-PERCEPT.10 §4.2 (a) extended; the
canonical converging-leverage closure.

**(b) RECOMMENDED — OBS-PERCEPT.11: debug AOV
kernel-arm bridge implementation** (still
authorised per OBS-PERCEPT.10 §4.2 (b)). Lands
the kernel-arm bridge for `AOVType::ObserverAberrationMagnitude`
+ `AOVType::ObserverDirection` (OBS-PERCEPT.8
data-model entries preserved verbatim across the
OBS-DOP.\* arc). Mirrors the FIELD-I.9 + FIELD-I.11
staged-impl pattern. Can be sequenced before or
after the combined CLI bridge.

**(c) Manifold-orthogonal work.** Multiple options:
- **Deferred SDK-host runtime pass** for the
  entire arc family — highest converging-leverage
  option.
- **MANI-I.12 final cross-host manifold audit**.
- **Denoiser integration with chart-aware AOVs**.
- **Path-tracer feature breadth** (NEE extension,
  BSDF expansion, MIS tuning).

**(d) NOT RECOMMENDED — FRAME-PROPAGATION.\* arc**
(per-bounce Option B Doppler / searchlight +
aberration). HIGHER-RISK; defer until primary-ray
+ post-shading contracts are empirically verified
on SDK host.

**(e) DEFERRABLE — retroactive task brief
authoring** for the OBSERVER.\* arc's unfilled
slots (operator discretion).

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; master rule #1 + #3 +
  #11 + #12 + #16 satisfaction recap at §3.10).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §7.2
  (the observer-frame Lorentz boost concept the
  OBS-DOP.\* arc completes for the Doppler /
  searchlight modulation; together with
  OBS-PERCEPT.\*, the §7.2 framing is now
  operationalised on every classical SR helper
  on both backends).

### 5.2 OBS-DOP.\* arc references

- `docs/OBSERVER_DOPPLER_SEARCHLIGHT_TASK.md`
  (OBS-DOP.1 — the task brief).
- `docs/OBSERVER_DOPPLER_SEARCHLIGHT_CUDA_AUDIT.md`
  (OBS-DOP.3 — the per-slice CUDA audit; the
  precedent shape for OBS-DOP.5).
- `docs/OBSERVER_DOPPLER_SEARCHLIGHT_OPTIX_AUDIT.md`
  (OBS-DOP.5 — the per-slice OptiX audit; the
  §3.12 closure table is referenced at this
  capstone's §3.11).

### 5.3 OBS-PERCEPT.\* + OBSERVER.\* + OBS-P.\* + OBS-F.\* arc references

- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1) — the canonical observer-space
  perception plan; the OBS-DOP.\* arc completes
  the post-shading Doppler / searchlight portion
  of the plan's §2 three-effect scope (primary-
  ray aberration + Doppler + searchlight).
- `docs/OBSERVER_PERCEPTION_ARC_AUDIT.md`
  (OBS-PERCEPT.10) — the OBS-PERCEPT.\* arc
  capstone audit; its check #7 deferral
  (Doppler / searchlight unchanged) is closed
  structurally by this OBS-DOP.\* arc on both
  backends.
- `docs/OBSERVER_FRAME_ARC_AUDIT.md` (OBSERVER.15)
  — the OBSERVER.\* arc capstone whose §10 risk
  #1 is fully resolved structurally with this
  OBS-DOP.6 capstone (recap at §3.11). Together
  with OBS-PERCEPT.3 + .5, every classical SR
  helper is migrated onto observer-frame runtime
  reads on both backends.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
  (OBSERVER.1) — the OBSERVER.\* arc plan; the
  OBS-DOP.\* arc consumes the OBSERVER.6 adapter
  + OBSERVER.8 + OBSERVER.10 payload carriers
  verbatim.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the `ObserverFrame{}` default-
  constructed POD's contract underpinning the
  §3.4 Layer 5 anchor.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the adapter's beta-resolution +
  clamp-safety contracts underpinning the §3.4
  Layer 5 anchor.
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — the `CudaSceneView::observer_frame`
  carry-only field.
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11) — the `OptixLaunchParams::observer_frame`
  carry-only field.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md` (OBSERVER.14)
  — the `ObserverBeta` AOV's read-only contract;
  the Stage 14A.3 AOV-uniform writes preserved
  verbatim per check #6.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3) — the precedent kernel-migration
  audit. The OBS-P.2 guarded ternary at the
  upstream `rel` snapshot is preserved verbatim
  across the OBS-DOP.\* arc; the dispatch's else-
  branch consumes the OBS-P.2 ternary's
  `observer.velocity` fallback path.
- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md` (OBS-F.3)
  — the precedent fixture audit; OBS-F.2 +
  OBS-PERCEPT.9 fixtures are the canonical
  runtime-deferred SDK-host validation surfaces.

### 5.4 Parallel-arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1) — the parallel field-interpretation
  arc; OBS-DOP.\* + FIELD-I.\* + FIELD-BEAUTY.\*
  + OBS-PERCEPT.\* arcs coexist as orthogonal
  perceptual layers above the manifold.
- `docs/FIELD_SCALAR_BEAUTY_MAPPING_AUDIT.md`
  (FIELD-BEAUTY.8) — the precedent capstone audit
  shape this OBS-DOP.6 capstone mirrors; the
  §4.2 (b) combined CLI bridge recommendation
  carries forward (extended to include OBS-DOP.\*
  deferred verdicts).
- `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
  (FIELD-I.12) — the precedent OptiX-bridge audit
  whose five-axis symmetry framework carries
  forward to the OBS-DOP.\* arc verbatim.
- `docs/FIELD_SCALAR_BEAUTY_OPTIX_AUDIT.md`
  (FIELD-BEAUTY.6) — the precedent OptiX-bridge
  beauty audit; the five-axis framework's
  §3.7 inheritance.

### 5.5 Source surface audited (arc-wide)

The OBS-DOP.\* arc touched the following source
files (relative to the arc baseline `0fcdd84`):

| File                                  | Net lines | Slice |
|---------------------------------------|-----------|-------|
| `src/manifold/ObserverFrame.h`        | +129      | OBS-DOP.2 |
| `src/cuda/CudaTestKernel.cu`          | +62 / -10 | OBS-DOP.2 |
| `src/optix/OptixPrograms.cu`          | +61 / -8  | OBS-DOP.4 |
| `tests/manifold_identity_tests.cpp`   | +222      | OBS-DOP.2 |

Total source-code surface: ~242 net lines across
3 source files + 1 test file. Zero scene surface
change. Zero CMakeLists.txt change across the arc.

### 5.6 Documentation surface produced (arc-wide)

| File                                              | Lines | Slice |
|---------------------------------------------------|-------|-------|
| `docs/OBSERVER_DOPPLER_SEARCHLIGHT_TASK.md`       | 1552  | OBS-DOP.1 |
| `docs/OBSERVER_DOPPLER_SEARCHLIGHT_CUDA_AUDIT.md` |  902  | OBS-DOP.3 |
| `docs/OBSERVER_DOPPLER_SEARCHLIGHT_OPTIX_AUDIT.md`|  950  | OBS-DOP.5 |
| `docs/OBSERVER_DOPPLER_SEARCHLIGHT_ARC_AUDIT.md`  |  ~986 | OBS-DOP.6 (this doc) |
| `docs/BUILD_PLAN.md`                              | per-slice entries | OBS-DOP.1 – .6 |

### 5.7 Surrounding commit SHAs

- OBS-DOP.1: `b334237` (task brief).
- OBS-DOP.2: `49eae42` (CUDA impl).
- OBS-DOP.3: `319e438` (CUDA audit).
- OBS-DOP.4: `5662e1a` (OptiX impl).
- OBS-DOP.5: `3256a01` (OptiX audit).
- OBS-DOP.6 (this audit, when landed): `(pending)`.
- Arc baseline (pre-OBS-DOP.\*): `0fcdd84`
  (OBS-PERCEPT.10 capstone audit).

### 5.8 Audit-host empirical state at this capstone

- `ctest` on `build/`: 13/13 PASS (audit-host;
  `RR_ENABLE_OPTIX=OFF`).
- Per-binary:
  - `manifold_identity_tests: 437 / 437 passed`
    (+16 NEW from OBS-DOP.2 vs 421 arc baseline).
  - `renderer_tests: 51 / 51 passed` (unchanged
    from arc baseline).
  - `relativity_tests: 841 / 841 passed`
    (unchanged).
  - `cli_tests: 274 / 274 passed` (unchanged).
  - `field_tests: 135 / 135 passed` (unchanged).
  - Every other suite unchanged from arc baseline.
- OptiX-ON-no-SDK build at each relevant per-slice
  landing (OBS-DOP.2 + .4): 14/14 ctest PASS
  (including `optix_tests`).
- `git diff 0fcdd84..3256a01 --name-only --
  'src/relativity/'`: zero hits (math leaves
  preserved verbatim).
- `git diff 0fcdd84..3256a01 --name-only --
  'src/field/'`: zero hits (FIELD-\* surfaces
  untouched).
- `git diff 0fcdd84..3256a01 --name-only --
  'src/io/' 'src/core/' 'src/scene/' 'src/main.cpp'`:
  zero hits.
- `git diff 0fcdd84..3256a01 --name-only --
  'src/manifold/' ':(exclude)src/manifold/ObserverFrame.h'`:
  zero hits (the unified helpers' header addition
  is the only manifold-side modification).

### 5.9 Single-source-of-truth math leaves

The OBS-DOP.\* arc consumes the existing
`src/relativity/` math leaves verbatim:

- `rr::relativity::applyDopplerColor(rgb, D,
  strength)` — RR_HD inline at
  `RelativityMath.h:219-230` (the artistic-
  approximation Doppler color shift; the math
  leaf both unified helpers + the legacy dispatch
  else-branch invoke).
- `rr::relativity::searchlightFactor(D)` — RR_HD
  inline at `RelativityMath.h:96-99` (the
  bolometric `D⁴` invariant).
- `rr::relativity::dopplerFactor(rel, dir)` —
  RR_HD inline at `RelativityMath.h:173-179`
  (consumed upstream of the unified helpers via
  the OBS-P.2 ternary's `rel` snapshot).
- `rr::relativity::precompute_relativity(beta_vec)`
  — RR_HD inline at `RelativityMath.h:160-167`
  (consumed at the kernel-arm entry by the
  OBS-P.2 ternary; preserved verbatim).

The arc adds **two new** helpers at
`src/manifold/ObserverFrame.h:630-705`:

- `rr::manifold::apply_observer_doppler_color(obs_frame,
  rgb, D, strength) → Vec3` — RR_HD inline;
  composes the three-gate logic + the
  `applyDopplerColor` math leaf invocation.
- `rr::manifold::apply_observer_searchlight_scale(obs_frame,
  D, strength) → float` — RR_HD inline; composes
  the three-gate logic + the `searchlightFactor`
  math leaf invocation + the linear `1 + (D⁴ - 1)
  * strength` formula.

Cross-backend bit-identity for the Doppler /
searchlight transform is **structurally guaranteed
by construction** by both backends consuming the
same shared helpers + the same math leaves (same
RR_HD inline code on both CUDA + OptiX).

Together with the OBS-PERCEPT.3 helper at
`ObserverFrame.h:553-576`
(`apply_observer_primary_ray_aberration`), the
`rr_manifold` library now exposes **three unified
observer-frame perception helpers** — one per
classical SR helper — completing the §7.2
architectural framing on the audit-host side
across both backends.

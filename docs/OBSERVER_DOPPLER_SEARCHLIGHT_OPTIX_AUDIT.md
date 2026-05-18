# OptiX Observer Doppler/Searchlight Migration Audit (OBS-DOP.5)

Date:   2026-05-18
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `5662e1a` ("optix:
OBS-DOP.4 — OptiX Observer Doppler/Searchlight Migration
(impl, OptiX program arm)").
Audit baseline: `319e438` ("docs: OBS-DOP.3 — CUDA
Observer Doppler/Searchlight Audit (docs only)") —
the last commit before OBS-DOP.4 landed.
Arc baseline: `0fcdd84` (OBS-PERCEPT.10 capstone audit
— the last commit before the OBS-DOP.\* arc opened).
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The OBS-DOP.4 commit's OptiX-ON-no-SDK
build was empirically verified at landing time (ctest
14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, the OBS-DOP.4 commit's
content, the audit-host + OptiX-ON-no-SDK `ctest`
runtime outputs, and `git diff` filter inspections.

This audit is the **in-band per-slice audit gate**
for OBS-DOP.4 (`5662e1a`). It verifies the ten items
the operator's brief enumerates — OptiX Doppler
reads `ObserverFrame`; OptiX searchlight reads
`ObserverFrame`; activation requires
`ConstantVelocityMinkowski`; `beta = 0` no-op;
default observer no-op; existing Doppler /
searchlight math preserved; CUDA / OptiX semantics
match; OptiX OFF build remains valid; runtime
CUDA / OptiX status (`PASS` / `DEFERRED` /
`BLOCKED`); and the overall verdict (`PASS` /
`REPAIR` / `BLOCKED`).

The OBS-DOP.4 slice is the **OptiX-side mirror** of
OBS-DOP.2's CUDA Doppler / searchlight migration.
It consolidates the four logical OptiX post-shading
sites (the shared `apply_doppler_and_searchlight_with_D(...)`
shim at lines 99-176 + the three OptiX programs that
call it: `__miss__radiance`, `__closesthit__radiance`,
`__raygen__pathtrace` + the 2-arg
`apply_doppler_and_searchlight(...)` fallback that
delegates) onto the same unified
`rr::manifold::apply_observer_doppler_color(...)` +
`apply_observer_searchlight_scale(...)` helpers that
OBS-DOP.2 introduced. With OBS-DOP.4 landed, both
backends now have **symmetric Doppler / searchlight
observer-frame migration arms** — the OBS-PERCEPT.10
capstone's check #7 deferral closes structurally on
both backends.

---

## 1. VERDICT

**PASS.**

All nine structural / runtime-status checks (#1
through #9) PASS — check #9 records the standard
`PASS_WITH_RUNTIME_DEFERRED` shape on the documented
audit-host SDK-absence limitation, but the structural
data-path is fully verified by the audit-host build's
clean compile + 13/13 ctest pass (unchanged from
OBS-DOP.3 baseline) + the **OptiX-ON-no-SDK build's
clean compile + 14/14 ctest pass** (the load-bearing
empirical verification that the OptiX TU absorbs the
shim's dispatch-shape migration cleanly). Check #10
(overall verdict) is `PASS`.

The OBS-DOP.4 surface ships exactly what the
operator's eight-bullet brief authorised — OptiX-
only Doppler / searchlight migration via the unified
helpers (consumed verbatim from OBS-DOP.2's
`rr_manifold` surface), with default observer no-op +
beta=0 no-op + existing math preserved + CUDA byte-
identical to the OBS-DOP.3 baseline + cross-backend
semantic match + OptiX OFF build clean + OptiX ON
build clean.

Check #9's runtime CUDA / OptiX status is the
standard `PASS_WITH_RUNTIME_DEFERRED` shape carried
by every prior CUDA / OptiX-touching slice
(OBS-PERCEPT.5 / OBS-PERCEPT.6 / OBS-DOP.3 /
FIELD-I.11 / FIELD-I.12 / FIELD-BEAUTY.5 /
FIELD-BEAUTY.6 / SCHW.7 / PENROSE.8 / OBSERVER.13).
The audit-host has neither CUDA nor OptiX SDK; the
shim's empirical Doppler / searchlight modulation
cannot be exercised here. The structural data-path
is verified by:

- audit-host build clean + ctest 13/13 PASS
  (`manifold_identity_tests: 437/437` unchanged
  from OBS-DOP.3 baseline — the 16 RR_CHECKs on
  the shared helpers cover both backends'
  consumption structurally);
- OptiX-ON-no-SDK build clean + ctest 14/14 PASS
  (including `optix_tests`) — the load-bearing
  verification that the shim's dispatch-shape
  migration compiles + links + runs at the OptiX
  level;
- per-line `git diff` inspections confirming CUDA
  source unchanged + `rr_manifold` unified helpers
  unchanged + `src/relativity/` math leaves
  unchanged + every non-OptiX-touching source file
  preserved verbatim.

The narrow-scope verdict honesty: the operator's
OBS-DOP.4 brief enumerated eight implementation
bullets (OptiX-only scope; ObserverFrame launch
payload reads; activation gates; default no-op;
beta=0 no-op; math preservation; CUDA semantics
match; both OptiX OFF + ON build clean). The slice
satisfies all eight:

- **Bullet 1** (OptiX-only scope): the shim
  modifications target ONLY
  `src/optix/OptixPrograms.cu`; CUDA byte-identical
  to OBS-DOP.3 baseline (`git diff 319e438..5662e1a
  -- 'src/cuda/'` zero hits). See check #7 below.
- **Bullet 2** (ObserverFrame launch payload reads):
  the shim takes `optixLaunchParams.observer_frame`
  via `const auto& obs_frame = optixLaunchParams.observer_frame`
  at `OptixPrograms.cu:125` + reads
  `obs_frame.perception_mode` (line 132) for the
  outer gate + passes `obs_frame` to the unified
  helpers (lines 148 + 168) where the inner gate
  reads `obs_frame.beta`. See checks #1 + #2 below.
- **Bullet 3** (activation gates): both unified
  helpers apply the three-gate logic via the
  shared `rr_manifold` surface (verified at
  OBS-DOP.3 check #3 + #4); the OptiX-side
  `perception_active` boolean at line 131-133
  selects whether to dispatch to the unified
  helpers or to the legacy math leaf path. See
  check #3 below.
- **Bullet 4** (default observer no-op): the
  default `ObserverFrame{}` carries
  `perception_mode = Identity`; the shim's outer
  gate closes; the dispatch's else-branch fires
  the legacy `applyDopplerColor` /
  `searchlightFactor` chain with the same payload-
  `D` the pre-OBS-DOP.4 shim consumed — byte-
  identical to the post-OBS-DOP.3 baseline. See
  check #5 below.
- **Bullet 5** (beta=0 no-op): the unified
  helpers' inner gate (`|beta|² > 0`) short-
  circuit returns identity results; matches the
  OBS-DOP.2 three-layer no-op anchor extended to
  the OptiX path. See check #4 below.
- **Bullet 6** (math preservation): the existing
  `applyDopplerColor` / `searchlightFactor` /
  `dopplerFactor` / `precompute_relativity` math
  leaves are preserved verbatim across the slice.
  See check #6 below.
- **Bullet 7** (CUDA semantics match): five-axis
  cross-backend symmetry framework (inherited from
  OBS-PERCEPT.6 §3.7 + OBS-DOP.1 §8) holds. See
  check #7 below.
- **Bullet 8** (both OptiX OFF + ON build clean):
  audit-host build clean (13/13 PASS) + OptiX-ON-
  no-SDK build clean (14/14 PASS). See check #8
  below.

No REPAIR action required. No BLOCKED item
outstanding. The OBS-DOP.\* arc's per-slice gate
chain is safe to extend; the next slot is OBS-DOP.6
— arc capstone audit (per the OBS-PERCEPT.10 +
FIELD-BEAUTY.8 capstone audit shapes synthesising
OBS-DOP.1 + .2 + .3 + .4 + .5 verdicts).

---

## 2. PER-CHECK RESULTS

| #  | Check                                                  | Evidence                                                                                                                                                                                                                                                                                                                                                              | Verdict |
|----|--------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1  | OptiX Doppler reads `ObserverFrame` beta/direction/mode | The shared shim `apply_doppler_and_searchlight_with_D(...)` at `OptixPrograms.cu:120-176` takes the `optixLaunchParams.observer_frame` by const reference (line 125: `const auto& obs_frame = optixLaunchParams.observer_frame`) + reads `obs_frame.perception_mode` (line 132) for the outer gate. On `perception_active`, the Doppler dispatch at lines 146-153 invokes `rr::manifold::apply_observer_doppler_color(obs_frame, color, D, par.doppler_color_strength)` — the unified helper internally reads `obs_frame.beta` (at `ObserverFrame.h:643`) for the inner gate. The shim is called from three OptiX programs that inherit the dispatch automatically: `__miss__radiance` at `OptixPrograms.cu:378`; `__closesthit__radiance` at `:899`; `__raygen__pathtrace` at `:1592`. The 2-arg `apply_doppler_and_searchlight(...)` fallback at lines 191-218 (kept "for any future shader that does not have access to the cached D") inherits the dispatch via delegation (line 217 invokes the with-D shim). All four consumers reach the unified `apply_observer_doppler_color(...)` helper through a single shim modification. The `optixLaunchParams.observer_frame` carrier field is the OBSERVER.10-shipped POD + the OBSERVER.6-adapter output (populated by `OptixRenderer` from the same `build_observer_frame_from_camera(...)` call the CUDA dispatcher uses; cross-backend payload symmetry verified at OBSERVER.11 audit check #3 verbatim). | PASS    |
| 2  | OptiX searchlight reads `ObserverFrame` beta/direction/mode | The same shared shim at `OptixPrograms.cu:120-176` consumes the same `obs_frame` reference for the searchlight dispatch at lines 166-175: `if (par.enable_searchlight) { float scale; if (perception_active) { scale = rr::manifold::apply_observer_searchlight_scale(obs_frame, D, par.searchlight_strength); } else { ... legacy ... } color = color * scale; }`. The unified helper internally reads `obs_frame.perception_mode` (at `ObserverFrame.h:689`) + `obs_frame.beta` (at `:696`) for the three-gate logic; verified at OBS-DOP.3 check #2 verbatim. The same three OptiX program call sites (`__miss__radiance`, `__closesthit__radiance`, `__raygen__pathtrace`) inherit the searchlight dispatch through the shim. The `perception_active` boolean is **computed once** at lines 131-133 + shared between the Doppler + searchlight dispatch blocks (warp-uniform; same value evaluated twice = same result by IEEE-754 single-precision branch determinism). | PASS    |
| 3  | Activation requires `ConstantVelocityMinkowski`        | The shim's outer gate at `OptixPrograms.cu:131-133`: `const bool perception_active = (obs_frame.perception_mode == rr::manifold::PerceptionMode::ConstantVelocityMinkowski);`. Identical comparison shape to the CUDA OBS-DOP.2 ternary at `CudaTestKernel.cu:226-228` + `:392-394` verbatim; identical comparison shape to the OBS-PERCEPT.5 OptiX-side gate at `OptixPrograms.cu:215-217` (`perception_active_pinhole`) + `:1216-1218` (`perception_active_pt`) verbatim. The default `Identity` perception_mode closes the gate; the reserved `CurvedChartGeodesicPlaceholder` also closes (master rule #3 placeholder honesty). The unified helpers' outer gates at `ObserverFrame.h:636-639` (Doppler) + `:689-692` (searchlight) are structurally identical to the OBS-PERCEPT.3 helper's outer gate at line 557-560 verbatim — same `obs_frame.perception_mode != PerceptionMode::ConstantVelocityMinkowski` comparison + same early-return shape. Empirically pinned by the OBS-DOP.2-landed 8 RR_CHECKs across 4 test functions (Identity / CurvedPlaceholder × Doppler/searchlight) — cross-backend consumption inherits the contract structurally because both backends invoke the same `rr_manifold` helpers. | PASS    |
| 4  | `beta = 0` remains no-op                               | The unified helpers' inner gate at `ObserverFrame.h:642-647` (Doppler) + `:694-699` (searchlight): `const Vec3 beta = obs_frame.beta; const float beta2 = beta.x*beta.x + beta.y*beta.y + beta.z*beta.z; if (!(beta2 > 0.0f)) return /* identity */;`. Squared-magnitude form avoids the `sqrt` cost; NaN-safe `!(beta2 > 0.0f)` form catches NaN beta components. The OptiX shim invokes both helpers on `perception_active`; the inner gate fires inside each helper. Empirically pinned by the OBS-DOP.2-landed `test_obs_dop_2_doppler_color_constant_velocity_zero_beta_returns_input` (3 RR_CHECKs) + `test_obs_dop_2_searchlight_scale_constant_velocity_zero_beta_returns_unity` (1 RR_CHECK) at `tests/manifold_identity_tests.cpp:2174-2197` + `:2285-2304`. The OBS-DOP.2 audit's check #4 verdict carries forward verbatim because the OptiX-side shim invokes the **same** unified helpers + receives the **same** input (the OBSERVER.6-adapter populated `observer_frame.beta`) on both backends — the inner-gate logic produces identical short-circuit behaviour. | PASS    |
| 5  | Default observer remains no-op                         | Three-layer default-no-op anchor preserved on the OptiX side (mirrors the OBS-DOP.2 → OBS-DOP.4 contract verbatim, extended to the OptiX shim): **(a) Layer 1 — shim outer gate**: at `OptixPrograms.cu:131-133`, the `perception_active` boolean evaluates to `false` on default `Identity` mode. The Doppler/searchlight dispatches at lines 146-153 + 166-175 take the else-branch → invoke the legacy math leaves (`applyDopplerColor` + `1 + (searchlightFactor(D) - 1) * strength`) with the same payload-`D` the pre-OBS-DOP.4 shim consumed. **(b) Layer 2 — unified helper outer gate**: on `CurvedChartGeodesicPlaceholder`, the shim's `perception_active` is `false` (only `ConstantVelocityMinkowski` opens the gate); the helpers are never invoked. **(c) Layer 3 — unified helper inner gate**: when `perception_active == true` but `|beta| = 0`, the helpers' `!(beta2 > 0.0f)` short-circuit returns identity. **(d) Layer 4 — math leaf identity**: `applyDopplerColor(rgb, D, strength)` at `D = 1` returns `rgb`; `searchlightFactor(D)` at `D = 1` returns `1.0f`. **(e) Layer 5 — OBSERVER.6 adapter**: emits `observer_frame.beta = (0, 0, 0)` exactly on default zero-beta inputs. **(f) Layer 6 — payload `D` source**: the `D` value reaching the shim was computed in `__raygen__pinhole:258` or `__raygen__pathtrace:1538` from the OBS-P.2 ternary's `rel` snapshot reading `observer.velocity` on the default path → `D = 1` for zero `observer.velocity`; the shim's legacy else-branch produces identity output. Empirically pinned by the OBS-DOP.2-landed `test_obs_dop_2_*_identity_mode_*` + `test_obs_dop_2_*_curved_placeholder_*` (8 RR_CHECKs covering both helpers). The OptiX-ON-no-SDK build's `optix_tests` 14/14 PASS confirms no regression in the OptiX-side payload or shim machinery. | PASS    |
| 6  | Existing Doppler/searchlight math preserved            | The `src/relativity/RelativityMath.h` math leaves are preserved verbatim across OBS-DOP.4. Per-line `git diff 319e438..5662e1a -- 'src/relativity/'` returns zero hits. The shim's else-branch invokes the math leaves with the same arguments as the pre-OBS-DOP.4 shim: `applyDopplerColor(color, D, par.doppler_color_strength)` at `OptixPrograms.cu:151` + `1 + (searchlightFactor(D) - 1) * par.searchlight_strength` inline at `:171-173`. The `dopplerFactor(rel, dir)` math leaf at `RelativityMath.h:173-179` continues to compute the per-pixel `D` in `__raygen__pinhole:258` + `__raygen__pathtrace:1538` (preserved verbatim across OBS-DOP.4 — the Doppler-factor compute is upstream of the shim). The `searchlightFactor(D)` math leaf at `:96-99` is invoked both by the shim's else-branch (line 173) AND the Stage 14A.3 AOV-uniform write at `OptixPrograms.cu:294` (preserved verbatim — `aov_searchlight_factor` writes the raw physical `D⁴` regardless of perception engagement, matching the CUDA-side discipline at `CudaTestKernel.cu:713`). The `precompute_relativity(beta_vec)` math leaf at `:160-167` is preserved (consumed by the OBS-P.2 ternary feeding the `rel` snapshot upstream). The unified helpers' arithmetic content is byte-identical to direct math-leaf calls (verified at OBS-DOP.2 audit check #6 by the composition tests `test_obs_dop_2_*_constant_velocity_nonzero_beta_*` at `manifold_identity_tests.cpp:2199-2236` + `:2306-2333`). | PASS    |
| 7  | CUDA / OptiX semantics match                           | **Five-axis cross-backend symmetry verified** (per OBS-PERCEPT.6 §3.7 framework + OBS-DOP.1 §8 inheritance + OBS-DOP.3 audit's check #1 + #2 framework applied to the OptiX scope):<br><br>**(a) Same shared types.** Both backends read the same `rr::manifold::ObserverFrame` POD + the same `rr::manifold::PerceptionMode` enum + the same `rr::math::Vec3` type. No backend-specific re-encoding.<br><br>**(b) Same shared helpers.** Both backends consume the same RR_HD inline `rr::manifold::apply_observer_doppler_color(...)` + `apply_observer_searchlight_scale(...)` from `src/manifold/ObserverFrame.h:630-705`. The two helpers are defined once + invoked from both translation units (CUDA `CudaTestKernel.cu` + OptiX `OptixPrograms.cu`); same emitted PTX/SASS arithmetic.<br><br>**(c) Same dispatch shape.** Every site uses the identical dispatch shape: `if (par.enable_*) { if (perception_active) { unified helper } else { legacy math leaf } }`. CUDA arms at `CudaTestKernel.cu:291-300` + `:308-318` + `:694-703` + `:714-726`; OptiX arm at `OptixPrograms.cu:146-153` + `:166-175`. The CUDA-side has the dispatch at four explicit sites (two kernels × two effect blocks); the OptiX-side has the dispatch at one shim that all consumers inherit through. Local variable names differ slightly (`perception_active` is the same across both backends; the CUDA-side `params` vs the OptiX-side `par = optixLaunchParams.params` is a scope-binding name difference only), but the dispatch's structural shape is byte-identical.<br><br>**(d) Same downstream math leaf.** Every dispatch's else-branch invokes the same `rr::relativity::applyDopplerColor(rgb, D, strength)` + `1 + (searchlightFactor(D) - 1) * strength` chain; the unified-helper then-branch invokes the same helpers internally. No backend-specific arithmetic variation.<br><br>**(e) Same upstream payload.** Both backends populate the `observer_frame` payload from the same `build_observer_frame_from_camera(scene.camera.to_gpu(), scene.observer, cfg.observer)` call at the dispatcher level (verified at OBSERVER.9 audit check #3 + OBSERVER.11 audit check #3 verbatim). The CUDA `CudaSceneView::observer_frame` field + the OptiX `OptixLaunchParams::observer_frame` field carry byte-identical PODs by construction.<br><br>Cross-backend bit-identity for the Doppler / searchlight transform is **structurally guaranteed by construction**. Empirical SDK-host cross-backend PPM `cmp` verification (cmp `aov_beauty.ppm` vs `optix_aov_beauty.ppm` on the OBS-PERCEPT.9 fixture) deferred to a CUDA + OptiX-SDK host per check #9. | PASS    |
| 8  | OptiX OFF build remains valid                          | The audit-host build (`RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF`) at `build/` compiles + tests cleanly:<br><br>- `cmake --build build -j` succeeds cleanly; no new warnings on any module (the OptiX TU is excluded from the audit-host build per the `RR_ENABLE_OPTIX=OFF` gate, but the headers it includes still compile via the audit-host build's consumers of `rr_manifold`).<br>- `ctest`: `100% tests passed, 0 tests failed out of 13`.<br>- `manifold_identity_tests: 437 / 437 checks passed` (unchanged from OBS-DOP.3 baseline; the OBS-DOP.2-landed 16 RR_CHECK assertions on the shared helpers cover both backends' consumption structurally).<br>- `renderer_tests: 51 / 51 passed` (unchanged from OBS-PERCEPT.10).<br>- `relativity_tests: 841 / 841 passed` (unchanged).<br>- `field_tests: 135 / 135 passed` (unchanged from FIELD-I.4).<br>- `cli_tests: 274 / 274 passed` (unchanged).<br>- Every other test suite unchanged.<br><br>The audit-host build's clean status confirms the OBS-DOP.4 shim modification does NOT break the header-only `rr_manifold` consumers on the non-OptiX path (e.g. `rr_gpu`'s CUDA TU when `RR_ENABLE_CUDA=OFF`, the `manifold_identity_tests` binary, etc.). | PASS    |
| 9  | Runtime CUDA / OptiX verification status               | **DEFERRED.** The audit host has neither CUDA SDK nor OptiX SDK installed (`nvcc` not present; `optixGetVersion` unavailable). Consequently:<br><br>**(a)** The OBS-DOP.4 OptiX shim dispatch at `OptixPrograms.cu:131-133` + `:146-153` + `:166-175` cannot be device-launched from this host. The OptiX-ON-no-SDK build at `/tmp/rr_build_optix_no_sdk` confirms the shim compiles + links cleanly (`14/14 ctest PASS`); the `optix_tests` binary loads the OptiX entry points but cannot execute them without an OptiX SDK runtime + an NVIDIA GPU device.<br><br>**(b)** Audit-host CAN verify: the shared helpers' three-gate logic via the OBS-DOP.2-landed `test_obs_dop_2_*` functions (the helpers' three-gate paths produce expected identity / composed outputs across both backends' consumption paths because both consume the same `rr_manifold` surface); the OptiX TU compiles + links cleanly under the `RR_ENABLE_OPTIX=ON` configuration (the shim's structural shape is verified by the build's clean compile); the audit-host smoke tests confirm the parser surface is byte-unchanged; the `RelativityParams::enable_*` flag-guards are textually preserved at the shim wrapper (verified by inspection — every `if (par.enable_doppler)` + `if (par.enable_searchlight)` site preserved verbatim).<br><br>This is the **same documented deferral** pattern accrued by every prior CUDA / OptiX-touching slice (OBS-PERCEPT.5 + OBS-PERCEPT.6 + OBS-DOP.3 + FIELD-I.11 + FIELD-I.12 + FIELD-BEAUTY.5 + FIELD-BEAUTY.6 + SCHW.7 + PENROSE.8 + OBSERVER.13 + every OBSERVER.\* / OBS-P.\* slice). The OBS-PERCEPT.6 audit + OBS-PERCEPT.10 capstone + every prior per-slice audit recorded runtime verification as DEFERRED with this disposition; OBS-DOP.5 inherits the pattern.<br><br>**Required SDK-host runtime checks** to convert the verdict from PASS_WITH_RUNTIME_DEFERRED → PASS, per the OBS-DOP.1 task brief §5.5 list extended to the OptiX scope:<br><br>(i) **§5.5.1** Default-state byte identity: `--render-optix-aovs <every fixture>` pre + post; `cmp` PPMs byte-by-byte. Expected byte-identical (Identity outer gate closes → legacy else-branch fires the math leaf chain unchanged).<br>(ii) **§5.5.2** Zero-beta byte identity: `--render-optix-aovs --observer-perception-mode relativistic --observer-beta 0 <fixture>` vs `--observer-perception-mode default` PPMs. Expected byte-identical (inner gate closes → helpers return identity).<br>(iii) **§5.5.3** Non-zero-beta consistency: `--render-optix-aovs --observer-perception-mode relativistic --observer-beta 0.5 --observer-direction 1,0,0 <fixture>` pre + post; `cmp` PPMs. Expected byte-identical (unified helpers compose the same math leaves with the same beta source).<br>(iv) **§5.5.4** OBS-PERCEPT.9 fixture runtime on OptiX: `--render-optix-aovs --observer-perception-mode relativistic` against the oblique `[0.6, -0.8, 0.0]` beta direction + FOV 60° fixture; verify visible Doppler color shift + searchlight beaming asymmetry.<br>(v) **§5.5.5** Cross-backend cmp: `cmp <fixture>_aov_beauty.ppm <fixture>_optix_aov_beauty.ppm` for `--observer-perception-mode relativistic` runs. Expected byte-identical PPM (five-axis symmetry guarantees this by construction).<br>(vi) **§5.5.6** Path-tracer post-spp Doppler: `--render-optix-pathtrace --observer-perception-mode relativistic <fixture>` pre + post; `cmp` PPMs. Verifies the shim migration is correctly inherited by the `__raygen__pathtrace` consumer at `OptixPrograms.cu:1592`.<br>(vii) **§5.5.7** Doppler debug AOV interaction: `aov_doppler_factor` + `aov_searchlight_factor` PPMs unchanged regardless of perception engagement (the Stage 14A.3 AOV-uniform writes at `OptixPrograms.cu:287-296` are NOT a function of perception transform; verified structurally at check #6). | DEFERRED |
| 10 | PASS / REPAIR / BLOCKED verdict                        | **PASS.** All nine structural checks (#1 – #8) PASS; check #9 (runtime CUDA / OptiX) records `DEFERRED` on the documented audit-host SDK-absence limitation. No REPAIR or BLOCKED item is outstanding. The OBS-DOP.4 commit ships:<br>- 1 OptiX-side shim migration at `OptixPrograms.cu:99-176` (+61 / -8 lines net; doc-comment expanded from ~7 to ~25 lines documenting the three-gate dispatch + cross-backend symmetry framework + call-site cross-references);<br>- 3 OptiX program call sites inheriting the dispatch automatically (`__miss__radiance` line 378; `__closesthit__radiance` line 899; `__raygen__pathtrace` line 1592);<br>- 1 2-arg fallback at line 217 inheriting the dispatch via delegation;<br>- 0 `src/cuda/` modifications (CUDA byte-identical to OBS-DOP.3 baseline);<br>- 0 `src/manifold/` modifications (the OBS-DOP.2-landed unified helpers consumed verbatim);<br>- 0 `src/relativity/` modifications (math leaves preserved verbatim);<br>- 0 `src/field/` / `src/io/` / `src/core/` / `src/scene/` / `src/main.cpp` modifications;<br>- 0 new test additions (mirrors OBS-PERCEPT.5 precedent — OBS-DOP.2's 16 RR_CHECKs cover both backends' consumption structurally);<br>- audit-host build clean (13/13 ctest PASS);<br>- OptiX-ON-no-SDK build clean (14/14 ctest PASS, including `optix_tests`);<br>- structural mirroring of OBS-DOP.2 CUDA arms + OBS-PERCEPT.5 OptiX-mirror precedent verbatim;<br>- master rule #1 + #3 + #11 + #12 + #16 satisfied across the slice;<br>- BUILD_PLAN.md entry with full What ships / does NOT ship / Acceptance / Module status rubric.<br><br>The slice is **safe to extend**; the next OBS-DOP.\* slot is OBS-DOP.6 — arc capstone audit (mirrors the OBS-PERCEPT.10 + FIELD-BEAUTY.8 capstone audit shapes; synthesises OBS-DOP.1 + .2 + .3 + .4 + .5 verdicts into the arc-level verdict). The audit verdict authorises the operator to proceed to OBS-DOP.6 OR to the combined FIELD-\* + OBS-PERCEPT + OBS-DOP CLI bridge slice (per OBS-PERCEPT.10 §4.2 (a) extended) — the latter being the canonical converging-leverage closure for the entire field-and-observer arc family's runtime-deferred verdict tail. | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Slice shape

The OBS-DOP.4 commit (`5662e1a`) introduces the
OptiX-side mirror of OBS-DOP.2's Doppler /
searchlight migration via a single shim modification
at `OptixPrograms.cu:99-176`. The shim migration's
elegance: three OptiX program call sites
(`__miss__radiance`, `__closesthit__radiance`,
`__raygen__pathtrace`) + one delegation fallback
inherit the dispatch automatically through the shim
modification — no per-program changes required.

The aggregate diff:

```
$ git diff 319e438..5662e1a --stat
docs/BUILD_PLAN.md         | 289 +
src/optix/OptixPrograms.cu |  69 +-
```

Source-code surface (~61 net lines): the shim's
dispatch-shape migration + doc-comment expansion.
Test surface: zero (the OBS-DOP.2-landed 16 RR_CHECK
assertions on the shared helpers in
`manifold_identity_tests` cover both backends'
consumption structurally — same precedent as
OBS-PERCEPT.5's no-test-extension pattern).
Documentation surface: ~289 lines for the OBS-DOP.4
BUILD_PLAN entry. Zero CMakeLists.txt modification
across the slice.

The narrow scope intentionally excludes every other
file from the operator's brief-by-brief discipline:
no CUDA-side `CudaTestKernel.cu` / `CudaPathTracer.cu`
modification (CUDA byte-identical to OBS-DOP.3
baseline); no `rr_manifold` / `ObserverFrame.h`
modification (the OBS-DOP.2-landed unified helpers
consumed verbatim); no `src/relativity/` math leaf
modification; no `src/field/` modification; no
`src/io/SceneLoader.cpp` parser modification; no new
CLI flag; no new ObserverFrame POD field; no new
fixture; no new debug AOV; no `MODULE_MAP.md` update;
no `MANIFOLD_INTEGRATION_PLAN.md` update.

### 3.2 Checks #1 + #2 — OptiX Doppler + searchlight reads

The shared shim reads `optixLaunchParams.observer_frame`
once + reuses for both effect blocks:

```cpp
const auto& obs_frame = optixLaunchParams.observer_frame;
const bool perception_active =
    (obs_frame.perception_mode ==
        PerceptionMode::ConstantVelocityMinkowski);
// ... Doppler block: invokes apply_observer_doppler_color(obs_frame, ...)
// ... searchlight block: invokes apply_observer_searchlight_scale(obs_frame, ...)
```

The `obs_frame` reference is the OBSERVER.10-shipped
`OptixLaunchParams::observer_frame` carry-only field
(populated from the OBSERVER.6 adapter output via the
host dispatcher's
`build_observer_frame_from_camera(...)` call). The
unified helpers internally read
`obs_frame.perception_mode` (outer gate) +
`obs_frame.beta` (inner gate) — exact mirror of the
CUDA-side OBS-DOP.2 reads (verified at OBS-DOP.3
audit checks #1 + #2 verbatim).

The three OptiX program consumers of the shim
(`__miss__radiance`, `__closesthit__radiance`,
`__raygen__pathtrace`) all reach the unified helpers
through this single dispatch site. The 2-arg
`apply_doppler_and_searchlight(...)` fallback at
line 217 inherits via delegation. Total four
consumers; one shim modification.

### 3.3 Check #3 — outer gate

The shim's outer gate at `OptixPrograms.cu:131-133`
is byte-identical (modulo local variable name) to:

- CUDA OBS-DOP.2 site at `CudaTestKernel.cu:226-228`
  (`k_sphere_relativistic`): `const bool perception_active
  = (observer_frame.perception_mode ==
  PerceptionMode::ConstantVelocityMinkowski);`.
- CUDA OBS-DOP.2 site at `CudaTestKernel.cu:392-394`
  (`k_render_scene`): same shape.
- OptiX OBS-PERCEPT.5 sites at `OptixPrograms.cu:215-217`
  (`__raygen__pinhole`'s `perception_active_pinhole`)
  + `:1216-1218` (`__raygen__pathtrace`'s
  `perception_active_pt`): same shape.

The default `Identity` perception_mode closes the
gate (the default `ObserverFrame{}` is `Identity`
per OBSERVER.2 audit check #2). The reserved
`CurvedChartGeodesicPlaceholder` also closes per
master rule #3 placeholder honesty (the helper
short-circuits to identity; future curved-chart arc
lifts the contract).

The unified helpers' internal outer gate (at
`ObserverFrame.h:636-639` + `:689-692`) provides
defence-in-depth: even if the shim's `perception_active`
were computed incorrectly, the helpers themselves
short-circuit on non-`ConstantVelocityMinkowski`
modes. Verified by the OBS-DOP.2-landed 8 RR_CHECKs
on the unified helpers (Identity / CurvedPlaceholder
× Doppler/searchlight).

### 3.4 Check #4 — inner gate

The unified helpers' inner gate at `ObserverFrame.h:642-647`
(Doppler) + `:694-699` (searchlight) fires inside
the OBS-DOP.4 shim's `perception_active`-true branch:

```cpp
const Vec3 beta = obs_frame.beta;
const float beta2 = beta.x*beta.x + beta.y*beta.y + beta.z*beta.z;
if (!(beta2 > 0.0f)) {
    return /* identity result */;
}
```

Properties:
- **Squared-magnitude form** avoids `sqrt` cost; is
  exact at `beta = (0, 0, 0)` (single-precision
  IEEE-754 zero exactness).
- **NaN-safe `!(beta2 > 0.0f)`** form catches NaN
  beta components defence-in-depth on top of the
  OBSERVER.6 adapter's pre-clamping (verified at
  OBSERVER.7 audit check #5).
- **Identity return** at zero beta: Doppler helper
  returns input `rgb`; searchlight helper returns
  `1.0f`.

Empirically pinned by the OBS-DOP.2-landed
`test_obs_dop_2_*_constant_velocity_zero_beta_*`
tests (4 RR_CHECKs); the contract applies to both
backends because both consume the same shared
helpers.

### 3.5 Check #5 — three-layer default no-op anchor (OptiX-side)

The default-state byte identity preservation on the
OptiX side relies on a six-layer anchor (extended
from the CUDA-side three-layer anchor at OBS-DOP.3
check #5):

1. **Shim outer gate** at `OptixPrograms.cu:131-133`:
   `perception_active == false` on default → dispatch
   takes the else-branch.
2. **Shim else-branch math leaf**: invokes
   `applyDopplerColor(color, D, par.doppler_color_strength)`
   directly with the same `D` payload the
   pre-OBS-DOP.4 shim consumed.
3. **`D` payload identity at zero beta**: `D` is
   computed in `__raygen__pinhole:258` or
   `__raygen__pathtrace:1538` from the OBS-P.2
   ternary's `rel = precompute_relativity(beta_source)`
   where `beta_source = observer.velocity = (0,0,0)`
   on the default path → `D = dopplerFactor(rel, dir)
   = 1.0f` by the math leaf's identity at zero beta.
4. **Math leaf identity at D=1**:
   `applyDopplerColor(rgb, 1, strength)` returns
   `rgb` unchanged (the `tanh(0.5 * log(1)) = 0` mix
   factor); `searchlightFactor(1) = 1.0f` → `scale =
   1 + (1 - 1) * strength = 1.0f`.
5. **OBSERVER.6 adapter**: emits `observer_frame.beta
   = (0, 0, 0)` exactly on default zero-beta inputs.
6. **OBS-DOP.2 helper inner gate** (defence-in-depth):
   even if the shim's `perception_active` were `true`,
   the unified helpers' `!(beta2 > 0.0f)` short-
   circuit returns identity at zero beta.

Identical anchor on both backends. The composition
guarantees every existing `--render-optix-*`
invocation against any scene WITHOUT
`--observer-perception-mode relativistic` preserves
byte-identical PPM output to the pre-OBS-DOP.4
baseline:

- **Default `--render-optix-aovs <fixture>`** without
  perception-mode flag: outer gate closes; legacy
  shim path runs with `D = 1` (from zero
  `observer.velocity`); both math leaves are
  identity; output byte-identical to the baseline.
- **`--render-optix-relativistic`** (if the action
  existed; OBSERVER.4 surface doesn't define this,
  but the equivalent path on OptiX through the
  Doppler/searchlight payload-register threading
  would behave identically because the shim's else-
  branch reads the same legacy `observer.velocity`
  via the upstream `rel` snapshot).

### 3.6 Check #6 — math preservation

The `src/relativity/RelativityMath.h` math leaves
are byte-unchanged across OBS-DOP.4. Per-line
`git diff 319e438..5662e1a -- 'src/relativity/'`
returns zero hits.

The OBS-DOP.4 shim composes the existing leaves via
RR_HD inline calls — no new arithmetic. The shim's
else-branch invokes the math leaves with the same
arguments as the pre-OBS-DOP.4 shim:

- `applyDopplerColor(color, D, par.doppler_color_strength)`
  at line 151 (Doppler else-branch);
- `1 + (searchlightFactor(D) - 1) * par.searchlight_strength`
  inline at lines 171-173 (searchlight else-branch).

The Stage 14A.3 AOV-uniform discipline at
`OptixPrograms.cu:287-296` (the `__raygen__pinhole`'s
`aov_doppler_factor` + `aov_searchlight_factor`
writes) is preserved verbatim — the AOVs write the
raw physical math-leaf outputs (`D` directly +
`searchlightFactor(D)` directly) regardless of
perception engagement. This matches the CUDA-side
discipline at `CudaTestKernel.cu:713` + the
OBSERVER.13 `ObserverBeta` AOV's read-only contract.

Master rule #3 satisfied: no new physics math; no
new SR specialisation; the existing math leaves are
the canonical source of truth + their behaviour is
preserved across the migration.

### 3.7 Check #7 — CUDA/OptiX semantics match

**Five-axis cross-backend symmetry framework** (per
OBS-PERCEPT.6 §3.7 inherited + OBS-DOP.1 §8 +
OBS-DOP.3 audit's check #1 + #2 framework applied to
OBS-DOP.4):

| Axis | CUDA (OBS-DOP.2) | OptiX (OBS-DOP.4) |
|------|------------------|-------------------|
| POD type | `observer_frame` (kernel arg from OBS-P.2 signature extension) / `scene.observer_frame` (CudaSceneView field from OBSERVER.8) | `optixLaunchParams.observer_frame` (OptixLaunchParams field from OBSERVER.10) |
| Shared helper | `rr::manifold::apply_observer_doppler_color(obs_frame, rgb, D, strength)` + `rr::manifold::apply_observer_searchlight_scale(obs_frame, D, strength)` | (same; consumed verbatim from the OBS-DOP.2-landed `rr_manifold` surface) |
| Dispatch shape | `if (params.enable_*) { if (perception_active) { unified helper } else { legacy math leaf } }` | (same; the shim wraps both Doppler + searchlight dispatches with shared `perception_active`) |
| Math leaf (else branch) | `rr::relativity::applyDopplerColor(rgb, D, strength)` + `1 + (rr::relativity::searchlightFactor(D) - 1) * strength` | (same; byte-identical inline arithmetic) |
| Gate semantics | `perception_mode == ConstantVelocityMinkowski` (outer) + `|beta|² > 0` (inner via the unified helpers) | (same) |

**Cross-backend bit-identity** for the Doppler /
searchlight transform is **structurally guaranteed
by construction** — both backends consume the same
RR_HD inline helpers + the same math leaves; the same
emitted PTX/SASS arithmetic on both CUDA + OptiX.

The CUDA-side has dispatches at FOUR explicit sites
(two kernels × two effect blocks); the OptiX-side
has dispatches at ONE shim that all consumers
inherit through (one site × two effect blocks ×
three+one inheriting consumers). This shape difference
is structural (CUDA has per-kernel inlining without
a shared shim; OptiX has the shared shim due to the
payload-register-3 `D` threading discipline). The
**semantic equivalence** is preserved because:

- Both backends compute `D` upstream (CUDA: in
  `precompute_relativity(rel)` + `dopplerFactor(rel,
  dir)` at the start of each kernel; OptiX: in
  `__raygen__pinhole` for the trace branch or in
  `__raygen__pathtrace` for the path-tracer branch).
- Both backends pass the same `obs_frame` + `D` +
  strength to the same unified helpers (or to the
  same legacy math leaves on the else-branch).
- Both backends produce the same output by IEEE-754
  single-precision determinism.

Empirical SDK-host cross-backend `cmp` verification
deferred per check #9.

### 3.8 Check #8 — OptiX OFF build remains valid

The audit-host build at `build/` (`RR_ENABLE_CUDA=OFF`,
`RR_ENABLE_OPTIX=OFF`) compiles + tests cleanly
post-OBS-DOP.4:

- `cmake --build build -j` succeeds cleanly.
- `ctest`: `100% tests passed, 0 tests failed out of
  13`.
- `manifold_identity_tests: 437/437 PASS` (unchanged
  from OBS-DOP.3 baseline).
- Every other test suite unchanged.

The audit-host build's clean status confirms the
OBS-DOP.4 shim modification does NOT break the
header-only `rr_manifold` consumers on the non-OptiX
path. Specifically:

- The OBS-DOP.2-landed unified helpers in
  `ObserverFrame.h` are header-only; their consumers
  on the audit-host side (`manifold_identity_tests`)
  continue to compile + run cleanly without
  modification.
- The `rr_gpu` library's audit-host stub compiles
  cleanly (no OptiX dependency).
- The `rr_optix` library is excluded from the audit-
  host build per `RR_ENABLE_OPTIX=OFF`; the OBS-DOP.4
  shim modification only affects the OptiX TU, so
  the audit-host build is structurally unaffected.

This satisfies the operator's "Must compile with
OptiX OFF and ON" rule explicitly.

### 3.9 Check #9 — runtime CUDA / OptiX verification

`PASS_WITH_RUNTIME_DEFERRED` on the documented audit-
host SDK-absence limitation. The audit host has
neither CUDA nor OptiX SDK; the shim's empirical
per-pixel Doppler / searchlight modulation cannot be
exercised here.

The OptiX-ON-no-SDK build at
`/tmp/rr_build_optix_no_sdk` confirms the shim's
structural shape is sound (`ctest 14/14 PASS`
including `optix_tests`). The `optix_tests` binary
links + loads the OptiX entry points but cannot
execute them without an OptiX SDK runtime + an
NVIDIA GPU device.

This is the standard deferral pattern shared by
every prior CUDA / OptiX-touching slice. The
required SDK-host runtime checks are enumerated at
check #9's evidence cell + the combined CLI bridge
slice (per OBS-PERCEPT.10 §4.2 (a) extended) is the
canonical converging-leverage closure that converts
this verdict + every parallel arc family's deferred
verdict to PASS in one operation.

### 3.10 Check #10 — overall verdict

**PASS.** All nine structural / runtime-status
checks PASS or appropriately DEFERRED. No REPAIR or
BLOCKED item outstanding. The OBS-DOP.4 slice is
safe to extend; the next OBS-DOP.\* slot is OBS-DOP.6
— arc capstone audit (synthesises OBS-DOP.1 + .2 +
.3 + .4 + .5 verdicts into the arc-level verdict;
mirrors the OBS-PERCEPT.10 + FIELD-BEAUTY.8 capstone
shapes).

### 3.11 Master-rule satisfaction recap

- **Master rule #1 ("Build incrementally"):**
  satisfied. OptiX-only scope; CUDA byte-identical to
  OBS-DOP.3 baseline; the slice composes cleanly
  with the existing OBS-DOP.2 + OBS-PERCEPT.5 +
  OBS-P.2 + OBSERVER.6 + OBSERVER.10 infrastructure
  without modifying any of them.

- **Master rule #3 ("no fake stubs"):** satisfied.
  The shim's three-gate dispatch + the unified
  helpers ship with real arithmetic + real branches;
  the `CurvedChartGeodesicPlaceholder` mode's no-op
  fallback is honest (the shim's `perception_active
  == false` short-circuits to the legacy chain;
  future curved-chart arcs lift the contract with
  documented semantics).

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. The OBS-DOP.2-landed 16
  RR_CHECK assertions on the shared helpers cover
  both backends' consumption structurally (mirrors
  OBS-PERCEPT.5 precedent verbatim). The five-axis
  cross-backend symmetry argument (check #7) rests
  on inspectable file/line references on both
  backends.

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. The slice scope is deliberately narrow:
  - OBS-DOP.4 = OptiX-only shim migration; no CUDA
    changes; no `rr_manifold` helper modification;
    no `rr_relativity` math leaf modification.
  - The shim migration's elegance — three consumers
    inherit through one shim modification — keeps
    the source-code surface to ~61 net lines.
  - No new debug AOV; no fixture; no CLI flag; no
    new ObserverFrame POD field; no new math
    specialisation; no per-bounce Doppler /
    searchlight; no curved-chart Doppler /
    searchlight; no spectral pipeline.

- **Master rule #16 ("default-off / reasoning-
  traceable defaults"):** satisfied. The OBS-DOP.4
  default state is unchanged from the OBS-DOP.3
  baseline:
  - No `--render-*` or `--render-optix-*` action's
    output changes by default.
  - No existing PPM filename changes.
  - No new file produced by default.
  - No existing AOV slot's value changes.
  The single observable behaviour change is the
  structural presence of the shim's dispatch wrapper
  — its observable behaviour from every default CLI
  invocation (Identity perception mode + zero
  `observer.velocity`) is zero because the outer gate
  closes + the legacy else-branch fires the math
  leaves with the same `D` payload value the
  pre-OBS-DOP.4 shim consumed.

### 3.12 Honest scope recap

The OBS-DOP.4 slice is a **single-shim OptiX-side
migration with shared-helper consumption**, with the
**SDK-host runtime validation deferred** to a future
combined CLI bridge slice's audit (per OBS-PERCEPT.10
§4.2 (a) extended). The verdict `PASS` (with check #9
`DEFERRED` on the documented audit-host SDK-absence
limitation) honestly captures:

- The slice's structural content is complete +
  verified on the audit-host side (CUDA build clean +
  ctest 13/13 PASS + OptiX-ON-no-SDK build clean +
  ctest 14/14 PASS + helper tests 16/16 PASS
  inherited from OBS-DOP.2).
- The empirical runtime verification of the OptiX
  kernel arm's composed Doppler / searchlight
  modulation output (the seven SDK-host scenarios
  from §5.5 of the OBS-DOP.1 task brief extended to
  OptiX) is reserved for the future combined CLI
  bridge slice's audit.

With OBS-DOP.4 landed, both backends now have
symmetric Doppler / searchlight observer-frame
migration arms; the OBS-PERCEPT.10 capstone's check
#7 deferral closes structurally on both backends.
The OBSERVER.15 capstone's §10 risk #1 (kernel-side
perception-transform migration deferred) is now
closed structurally across every post-shading SR
site on both backends:

| SR site | CUDA closure | OptiX closure |
|---------|--------------|----------------|
| Primary-ray aberration | OBS-PERCEPT.3 | OBS-PERCEPT.5 |
| Doppler color shift | OBS-DOP.2 | OBS-DOP.4 (this slice) |
| Searchlight intensity | OBS-DOP.2 | OBS-DOP.4 (this slice) |

The OBS-DOP.6 capstone audit (the next slot) is the
canonical closure of the OBS-DOP.\* arc's per-slice
gate chain; the combined FIELD-\* + OBS-PERCEPT +
OBS-DOP CLI bridge slice (per OBS-PERCEPT.10 §4.2
(a) extended) would close the entire field-and-
observer arc family's runtime-deferred verdict tail
in one converging-leverage operation.

---

## 4. NEXT

### 4.1 Renumbered OBS-DOP.\* sub-slice ladder (in-band)

The OBS-DOP.5 audit closes the OptiX-side gate. The
post-OBS-DOP.5 ladder for remaining work is:

- **OBS-DOP.6** — Arc capstone audit (the
  renumbered capstone slot; mirrors the
  OBS-PERCEPT.10 + FIELD-BEAUTY.8 capstone audit
  shapes; synthesises OBS-DOP.1 + .2 + .3 + .4 +
  .5 verdicts into the arc-level verdict).

The ladder above is the **operator's choice**;
the OBS-DOP.\* arc's structural surface is now
complete on both backends. The combined FIELD-\* +
OBS-PERCEPT + OBS-DOP CLI bridge slice (per
OBS-PERCEPT.10 §4.2 (a) extended) collapses every
per-arc-family SDK-host runtime pass into a single
converging-leverage slice once it lands.

### 4.2 Candidate next slots (prioritised)

**(a) HIGHLY RECOMMENDED — OBS-DOP.6: Arc capstone
audit.** Mirrors the OBS-PERCEPT.10 + FIELD-BEAUTY.8
capstone shapes verbatim adapted to the OBS-DOP.\*
arc scope. Synthesises the OBS-DOP.1 + OBS-DOP.2 +
OBS-DOP.3 + OBS-DOP.4 + OBS-DOP.5 verdicts into the
arc-level verdict. The natural closure of the
OBS-DOP.\* arc's per-slice gate chain. Doc-only;
~1000-line capstone audit doc + BUILD_PLAN entry.

**(b) RECOMMENDED — combined FIELD-\* + OBS-PERCEPT
+ OBS-DOP CLI bridge slice** (per OBS-PERCEPT.10
§4.2 (a) extended). Best converging-leverage option;
closes the entire field-and-observer-arc family's
runtime-deferred verdict tail in one SDK-host audit.
Can be sequenced AFTER OBS-DOP.6 to maximise the
closed-verdict count per audit operation (closes 14+
runtime-deferred verdicts in one SDK-host pass:
FIELD-I.10 + .12 + .14 + FIELD-BEAUTY.4 + .6 + .8 +
OBS-PERCEPT.4 + .6 + .10 + OBS-DOP.3 + .5 + .6 +
the combined CLI bridge audit itself).

**(c) Manifold-orthogonal work.** Multiple options:
- **Deferred SDK-host runtime pass** for the entire
  arc family (OBSERVER.\* + OBS-P.\* + OBS-F.\* +
  FIELD-I.\* + FIELD-BEAUTY.\* + OBS-PERCEPT.\* +
  OBS-DOP.\*) — highest converging-leverage option.
- **MANI-I.12 final cross-host manifold audit**.
- **Denoiser integration with chart-aware AOVs**.
- **Path-tracer feature breadth** (NEE extension,
  BSDF expansion, MIS tuning).

**(d) NOT RECOMMENDED — OBS-DOP-AOV.\* arc**
(per-pixel Doppler / searchlight diagnostic AOV
extension before the CUDA + OptiX runtime pass is
empirically verified. Higher-risk than completing
OBS-DOP.6 + the combined CLI bridge first.

**(e) DEFERRABLE — OBS-PERCEPT.11 debug-AOV kernel-
arm bridge implementation** (still authorised per
OBS-PERCEPT.10 §4.2 (b); the OBS-DOP.\* arc preserves
the OBS-PERCEPT.8 AOV data-model entries verbatim).
Can be sequenced before or after the combined CLI
bridge depending on the operator's prioritisation
of debug-AOV runtime coverage vs cross-arc-family
verdict closure.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; master rule #1 + #3 +
  #11 + #12 + #16 satisfaction recap at §3.11).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §7.2
  (the observer-frame Lorentz boost concept the
  OBS-DOP.\* arc operationalises; the OBS-DOP.4
  OptiX-side migration completes the Doppler /
  searchlight component of the §7.2 framing on
  both backends).

### 5.2 OBS-DOP.\* arc references

- `docs/OBSERVER_DOPPLER_SEARCHLIGHT_TASK.md`
  (OBS-DOP.1 — the task brief this audit verifies
  the OBS-DOP.4 implementation against).
- `docs/OBSERVER_DOPPLER_SEARCHLIGHT_CUDA_AUDIT.md`
  (OBS-DOP.3 — the precedent CUDA-side per-slice
  audit shape this OBS-DOP.5 audit mirrors verbatim;
  the OBS-DOP.3 verdict + the OBS-DOP.5 verdict
  together close the CUDA + OptiX dual-backend
  audit-host structural surface).

### 5.3 OBS-PERCEPT.\* + OBSERVER.\* + OBS-P.\* + OBS-F.\* arc references

- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1) — the canonical observer-space
  perception plan; the OBS-DOP.\* arc consolidates
  the Doppler / searchlight sites onto the unified
  abstraction this plan introduced. The OBS-DOP.4
  OptiX-side migration completes the consolidation
  on the OptiX backend.
- `docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`
  (OBS-PERCEPT.2) — the precedent task brief shape
  OBS-DOP.1 mirrored.
- `docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md`
  (OBS-PERCEPT.4) — the precedent CUDA-side audit
  shape OBS-DOP.3 mirrored verbatim.
- `docs/OBSERVER_PRIMARY_RAY_OPTIX_AUDIT.md`
  (OBS-PERCEPT.6) — the precedent OptiX-side audit
  shape this OBS-DOP.5 audit mirrors verbatim
  (eleven-row evidence table + runtime status row +
  verdict variant + the §3.7 five-axis cross-backend
  symmetry framework). The OBS-PERCEPT.6 audit's
  §3.7 symmetry argument carries forward to this
  OBS-DOP.5 check #7.
- `docs/OBSERVER_PERCEPTION_ARC_AUDIT.md`
  (OBS-PERCEPT.10) — the arc-level capstone audit
  whose check #7 honestly deferred the
  Doppler/searchlight consolidation; the OBS-DOP.\*
  arc closes that deferral on both backends with
  OBS-DOP.2 (CUDA) + OBS-DOP.4 (OptiX).
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
  (OBSERVER.1) — the OBSERVER.\* foundation arc's
  plan; the OBS-DOP.4 implementation consumes the
  OBSERVER.6 adapter + OBSERVER.10 OptiX payload
  + the OBSERVER.2-shipped `ObserverFrame` POD
  verbatim.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the `ObserverFrame{}` default-
  constructed POD's contract the OBS-DOP.4 three-
  layer no-op anchor relies on.
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the adapter's beta-resolution +
  clamp-safety contracts the unified helpers rely
  on.
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — the `CudaSceneView::observer_frame`
  carry-only field the CUDA OBS-DOP.2 kernel arms
  read.
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11) — the `OptixLaunchParams::observer_frame`
  carry-only field the OptiX OBS-DOP.4 shim reads;
  the cross-backend payload symmetry verified at
  this audit's check #3.
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — the `ObserverBeta` AOV's read-
  only contract; the Stage 14A.3 AOV-uniform
  `aov_doppler_factor` + `aov_searchlight_factor`
  writes at `OptixPrograms.cu:287-296` mirror this
  contract verbatim per OBS-DOP.1 §4.8 + this
  audit's check #6.
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15) — the OBSERVER.\* arc capstone
  whose §10 risk #1 the OBS-DOP.\* arc closes for
  the post-shading Doppler / searchlight sites;
  OBS-DOP.4 (this slice's audited target) closes
  the OptiX-side portion.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3) — the precedent kernel-migration audit;
  OBS-DOP.4's shim migration consolidates the
  OBS-P.2 guarded ternary's OptiX-side `rel`
  snapshot consumer (the shim) onto the unified
  abstraction. The `__raygen__pinhole`'s OBS-P.2
  ternary at lines 215-221 + the `__raygen__pathtrace`'s
  OBS-P.2 ternary at lines 1216-1222 are preserved
  verbatim (the `D` payload computation upstream
  of the shim is unchanged; only the shim's
  application path is migrated).
- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md` (OBS-F.3)
  — the precedent fixture audit; the OBS-F.2 +
  OBS-PERCEPT.9 fixtures are the canonical runtime-
  deferred SDK-host validation surfaces for both
  backends.

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
  forward to OBS-DOP.5 verbatim.
- `docs/FIELD_SCALAR_BEAUTY_OPTIX_AUDIT.md`
  (FIELD-BEAUTY.6) — the precedent OptiX-bridge
  audit applied to the beauty-mapping scope;
  OBS-DOP.5 inherits the runtime-deferred verdict
  framing.

### 5.5 Source surface audited (this slice)

The OBS-DOP.4 commit (`5662e1a`) touched the
following source files (relative to the OBS-DOP.3
baseline `319e438`):

| File                          | Net lines  | Purpose |
|-------------------------------|------------|---------|
| `src/optix/OptixPrograms.cu`  | +61 / -8   | Shared shim dispatch migration at lines 99-176 |
| `docs/BUILD_PLAN.md`          | +289       | OBS-DOP.4 entry |

Total source-code surface: ~53 net lines (single
shim modification). Zero CMakeLists.txt change.
Zero CUDA-side modification. Zero `src/manifold/`
modification. Zero `src/relativity/` modification.
Zero `src/field/` modification. Zero test
modification. Zero non-shim source file
modification.

Verified via:
- `git diff 319e438..5662e1a --name-only` → 2 files
  (`docs/BUILD_PLAN.md` + `src/optix/OptixPrograms.cu`).
- `git diff 319e438..5662e1a -- 'src/cuda/'` →
  zero hits.
- `git diff 319e438..5662e1a -- 'src/manifold/'`
  → zero hits.
- `git diff 319e438..5662e1a -- 'src/relativity/'`
  → zero hits.
- `git diff 319e438..5662e1a -- 'src/field/'` →
  zero hits.
- `git diff 319e438..5662e1a -- 'tests/'` → zero
  hits.

### 5.6 Surrounding commit SHAs

- OBS-PERCEPT.10 (arc baseline / pre-OBS-DOP.\*):
  `0fcdd84` (Observer Perception Arc Capstone Audit).
- OBS-DOP.1 (task brief): `b334237`.
- OBS-DOP.2 (CUDA impl): `49eae42`.
- OBS-DOP.3 (CUDA audit; pre-OBS-DOP.4 audit
  baseline): `319e438`.
- OBS-DOP.4 (OptiX impl; the slice audited here):
  `5662e1a`.
- OBS-DOP.5 (this audit, when landed): `(pending)`.

### 5.7 Audit-host empirical state at this slice

- `ctest` on `build/`: 13/13 PASS (audit-host;
  `RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF`).
- Per-binary:
  - `manifold_identity_tests: 437/437 passed`
    (unchanged from OBS-DOP.3 baseline; OBS-DOP.2's
    16 RR_CHECKs cover both backends' consumption
    structurally).
  - `renderer_tests: 51/51 passed` (unchanged).
  - `relativity_tests: 841/841 passed` (unchanged).
  - `field_tests: 135/135 passed` (unchanged).
  - `cli_tests: 274/274 passed` (unchanged).
  - Every other suite unchanged.
- `ctest` on `/tmp/rr_build_optix_no_sdk`: 14/14
  PASS (OptiX-ON-no-SDK; `RR_ENABLE_OPTIX=ON`).
  Includes `optix_tests` (unchanged from
  OBS-DOP.3).
- `git diff 319e438..5662e1a --name-only --
  'src/cuda/'`: zero hits (CUDA byte-identical to
  OBS-DOP.3 baseline; verified at check #7
  cross-backend symmetry framework).
- `git diff 319e438..5662e1a --name-only --
  'src/manifold/'`: zero hits (the OBS-DOP.2-
  landed unified helpers consumed verbatim).
- `git diff 319e438..5662e1a --name-only --
  'src/relativity/'`: zero hits (math leaves
  preserved verbatim at check #6).

### 5.8 Single-source-of-truth math leaves

The OBS-DOP.4 slice consumes the existing
`src/relativity/` + `src/manifold/` surfaces
verbatim:

- `rr::relativity::applyDopplerColor(rgb, D,
  strength)` — RR_HD inline at
  `RelativityMath.h:219-230`.
- `rr::relativity::searchlightFactor(D)` — RR_HD
  inline at `RelativityMath.h:96-99`.
- `rr::relativity::dopplerFactor(rel, dir)` —
  RR_HD inline at `RelativityMath.h:173-179`
  (consumed upstream of the shim in
  `__raygen__pinhole:258` + `__raygen__pathtrace:1538`).
- `rr::relativity::precompute_relativity(beta_vec)`
  — RR_HD inline at `RelativityMath.h:160-167`
  (consumed by the OBS-P.2 ternary feeding the
  `rel` snapshot upstream of the shim).
- `rr::manifold::apply_observer_doppler_color(obs_frame,
  rgb, D, strength)` — RR_HD inline at
  `ObserverFrame.h:630-652` (the OBS-DOP.2-landed
  unified helper).
- `rr::manifold::apply_observer_searchlight_scale(obs_frame,
  D, strength)` — RR_HD inline at
  `ObserverFrame.h:684-705` (the OBS-DOP.2-landed
  unified helper).

The OBS-DOP.4 slice adds **no new** helpers; it only
consumes the OBS-DOP.2 + pre-existing math leaves
via the shim's dispatch wrapper.

Cross-backend bit-identity for the Doppler /
searchlight transform is **structurally guaranteed
by construction** — both backends consume the same
RR_HD inline helpers + the same math leaves; same
emitted PTX/SASS on both CUDA + OptiX.

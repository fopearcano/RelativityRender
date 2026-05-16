# Camera-to-Observer Adapter Audit (OBSERVER.7)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `e2cde15` ("manifold:
OBSERVER.6 — Camera-to-Observer Adapter (impl,
host-only)").
Audit baseline: `27ec0d9` ("docs: OBSERVER.5 —
ObserverFrame Config / CLI Bridge Audit (docs only)")
— the last commit before OBSERVER.6 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, `git diff` against
the post-OBSERVER.5 baseline, the
`manifold_identity_tests` runtime output, the
`cli_tests` runtime output, and `ctest` exit codes.

This audit is the per-slice gate for OBSERVER.6
(`e2cde15`). It verifies the eight items the task
brief enumerates — adapter / helper exists; default
camera maps to no-op observer; existing relativity
params propagate correctly; finite-value guarantees
exist; no CUDA / OptiX changes; no visual behaviour
changes; build / test status; verdict — and produces
a `PASS` / `REPAIR` / `BLOCKED` verdict that gates
progression to the renumbered OBSERVER.8 (CUDA
payload bridge).

---

## 1. VERDICT

**PASS.**

All seven structural checks return `PASS`. No
`REPAIR` or `BLOCKED` item is found. The OBSERVER.6
camera-to-observer adapter is safely landed, no-op-
default verified, legacy-relativity-param
propagation verified, finite-value-guaranteed across
all three perception modes, and produces zero CUDA /
OptiX / visual behaviour change. The operator may
proceed to OBSERVER.8 (CUDA payload bridge; the
first slice that threads the adapter's
`ObserverFrame` output into a kernel via
`CudaSceneView::observer_frame`).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Adapter / helper exists                  | **PASS** | The OBSERVER.6 commit (`e2cde15`) adds `src/manifold/CameraObserverAdapter.h` (222 lines, header-only) in `namespace rr::manifold` with one entry-point function:<br>**(a)** `inline ObserverFrame build_observer_frame_from_camera(const rr::camera::GpuCamera& gc, const rr::relativity::Observer& observer, const ObserverConfig& config)` at line 138. The function consumes the device-friendly `GpuCamera` POD (header-only, from `camera/CameraRay.h`) rather than the host `rr::camera::Camera` class — keeping the `rr_manifold` link graph clean (no new `rr_camera` link dep; `manifold_identity_tests` link line unchanged at `rr_manifold`).<br>**(b) Three perception-mode construction paths**, mirroring the OBSERVER.1 plan §7 OBSERVER.6 contract verbatim and the SCHW.9 / PENROSE.6 / PENROSE.8 dispatcher-merge pattern:<br>&nbsp;&nbsp;- `PerceptionMode::Identity` (line 155): `return rest_frame();` — the byte-identity no-op anchor; camera + observer + non-mode config fields are ignored on this path.<br>&nbsp;&nbsp;- `PerceptionMode::CurvedChartGeodesicPlaceholder` (line 164): returns `rest_frame()` byte-for-byte EXCEPT the `perception_mode` tag is preserved as `CurvedChartGeodesicPlaceholder` so downstream kernels can distinguish (structural passthrough; OBSERVER.1 plan §8 non-goals).<br>&nbsp;&nbsp;- `PerceptionMode::ConstantVelocityMinkowski` (lines 172-219): full construction populating `position4` from `gc.position` (lines 210), `velocity4` from resolved beta + `rr::relativity::gamma(...)` (line 211), `beta` from the resolved 3-velocity (line 212), tetrad legs from `gc.right` / `gc.up` / `gc.forward` (lines 213-215), `proper_time` from `config.proper_time` (line 216), `coordinate_time` from `position4.x` (line 217), and `perception_mode` set to `ConstantVelocityMinkowski` (line 218).<br>**(c) Beta resolution priority** (lines 178-198) per the documented contract: (i) `config.beta_magnitude != 0` AND `length(config.direction) > 0` → `clampBeta(beta_magnitude) * normalize(direction)`; (ii) `config.beta_magnitude != 0` AND direction sentinel `(0,0,0)` → `gc.forward` fallback (line 189); (iii) `config.beta_magnitude == 0` → `observer.velocity` directly (line 196).<br>**(d) Defensive magnitude clamp** at lines 202-207: a non-trivial legacy `observer.velocity` (e.g. injected by a scene file at `|beta| > 0.999999`) is capped at `clampBeta` (line 203) before the `gamma` derivation; mirrors the `observer_frame_from(Observer)` precedent in `ObserverFrame.h`. |
| 2 | Default camera maps to no-op observer    | **PASS** | Three-layer no-op preservation:<br>**(a) Identity path empirically verified.** `test_observer_6_default_is_camera_equivalent_no_op` (`tests/manifold_identity_tests.cpp:1698`) constructs the adapter inputs as `GpuCamera{}` (all-zero default), `rr::relativity::Observer{}` (velocity = 0), and `ObserverConfig{}` (`perception_mode = Identity`); the resulting frame is byte-identical to `rest_frame()` across all 9 documented fields (position4, velocity4, beta, three tetrad legs, proper_time, coordinate_time, perception_mode).<br>**(b) Identity-mode-ignores-camera invariant verified.** `test_observer_6_identity_mode_ignores_camera` (`tests/manifold_identity_tests.cpp:1726`) feeds a **non-default** `GpuCamera` (position `(1,2,3)`, world-basis tetrad) but `perception_mode = Identity`; the result is still `rest_frame()` byte-for-byte (the no-op anchor's contract — the kernel-side OBSERVER.8+ short-circuit on Identity mode means the camera state is structurally irrelevant on this path).<br>**(c) ConstantVelocityMinkowski-zero-beta camera-equivalence verified.** `test_observer_6_constant_velocity_zero_beta` (`tests/manifold_identity_tests.cpp:1754`) feeds a non-default `GpuCamera` + default `Observer` + ConstantVelocityMinkowski config with `beta_magnitude = 0`; the resulting frame carries `position4 = (0, 1, 2, 3)` (camera position), `velocity4 = (1, 0, 0, 0)` (rest 4-velocity), `beta = (0, 0, 0)`, tetrad = camera basis, both time placeholders = 0, mode = ConstantVelocityMinkowski. This is the "camera-equivalent no-op observer" baseline: a kernel-side SR helper called against beta=0 reduces all three SR effects (aberration / Doppler / searchlight) to their identity values, preserving byte-identity to today's renderer for the default camera path. |
| 3 | Existing relativity params propagate correctly | **PASS** | Three-layer propagation verification of the legacy `rr::relativity::Observer::velocity` path:<br>**(a) Direct propagation when CLI overlay is zero.** `test_observer_6_constant_velocity_from_legacy_observer` (`tests/manifold_identity_tests.cpp:1782`) feeds `observer.velocity = (0.3, -0.4, 0.0)` (|beta| = 0.5) + `config.beta_magnitude = 0`; the resulting `frame.beta == observer.velocity` exactly, and round-trip via `to_relativity_observer(frame)` recovers the input velocity byte-for-byte (`approx(back.velocity, obs.velocity)` passes at the default `1e-6f` epsilon). The frame additionally satisfies `is_normalised_timelike(frame, minkowski_metric())` — the four-velocity is timelike-normalised under Minkowski at the precision-stable beta=0.5 regime.<br>**(b) CLI overlay precedence verified.** `test_observer_6_constant_velocity_from_config` (`tests/manifold_identity_tests.cpp:1810`) feeds `observer.velocity = (0.9, 0, 0)` AND `config.beta_magnitude = 0.5` + `config.direction = (1, 0, 0)`; the resulting `frame.beta == (0.5, 0, 0)` — the CLI overlay won, the legacy `observer.velocity` was correctly discarded. `is_normalised_timelike` passes. This is the documented OBSERVER.6 dispatcher-merge precedence (CLI overlay > legacy SR observer).<br>**(c) Zero-direction sentinel + camera-forward fallback verified.** `test_observer_6_zero_direction_falls_back_to_camera_forward` (`tests/manifold_identity_tests.cpp:1859`) feeds `config.beta_magnitude = 0.4` + `config.direction = (0, 0, 0)`; the resulting `frame.beta == (0, 0, -0.4)` — direction is `gc.forward = (0, 0, -1)`. This realises the documented sentinel fallback (the `ObserverConfig::direction` doc-comment in `ObserverFrame.h` + the `--render-demo` precedent + the audit-doc 2's evidence for OBSERVER.5).<br>The `rr::relativity::RelativityParams` flags (`enable_aberration` / `enable_doppler` / `enable_searchlight` / `doppler_color_strength` / `searchlight_strength` / `max_beta`) are NOT consumed by the adapter — they continue to flow through the existing call paths (the legacy `Observer` + `RelativityParams` pair is read by the CUDA + OptiX kernels via `scene.observer` + `scene.relativity` exactly as today). Master rule #3 is satisfied: the adapter does not silently re-route the existing flags through a new path. |
| 4 | Finite-value guarantees exist            | **PASS** | Four-layer finite-value defence:<br>**(a) Adapter-level defensive clamp** at `src/manifold/CameraObserverAdapter.h:202-207`: after the priority-resolved 3-velocity is computed, the magnitude is re-clamped via `clampBeta(beta_mag, kMaxBeta=0.999999f)` and the vector is rescaled to the capped magnitude. This caps `|beta|` regardless of source (CLI overlay, legacy observer, zero-direction fallback). Floor-protection on the rescale (line 205-207): `(beta_mag > kEps)` guard keeps exact-zero beta as exact zero (no division-by-zero).<br>**(b) Empirical clamp verification.** `test_observer_6_clamp_beta_safety` (`tests/manifold_identity_tests.cpp:1883`) feeds `observer.velocity = (1.5, 0, 0)` (|beta| = 1.5, faster than light); the resulting `length(frame.beta) <= 0.999999 + 1e-6` AND `is_finite_observer_frame(frame)` returns `true`. The `is_normalised_timelike` check is intentionally NOT exercised at the cap (single-precision floats lose enough precision in `gamma^2 * (1 - beta^2)` catastrophic-cancellation at `|beta| = 0.999999` that the residual exceeds the 1.0e-4f tolerance; the normalisation invariant is covered at the precision-stable `|beta| = 0.5` regime by `test_observer_6_constant_velocity_from_legacy_observer`).<br>**(c) Cross-mode finite-value guarantee.** `test_observer_6_finite_value_guarantee` (`tests/manifold_identity_tests.cpp:1961`) iterates over all three `PerceptionMode` enumerators (Identity / ConstantVelocityMinkowski / CurvedChartGeodesicPlaceholder) with a non-trivial camera + non-trivial observer + non-trivial config (`beta_magnitude=0.4`, `direction=(1,0,0)`, `proper_time=7.5`); for each mode the resulting frame passes `is_finite_observer_frame(...)` + `is_orthonormal_tetrad(...)` + `is_normalised_timelike(..., minkowski_metric())`.<br>**(d) Validator coverage from OBSERVER.2.** The adapter's output is consumed by the three OBSERVER.2 validators (`is_finite_observer_frame`, `is_orthonormal_tetrad`, `is_normalised_timelike`); the OBSERVER.3 audit's PASS verdict on these validators carries forward — they catch NaN / inf in any of the 21 scalar fields, non-orthogonal tetrad legs, and non-unit four-velocity normalisation under any chart metric. |
| 5 | No CUDA / OptiX changes                  | **PASS** | `git diff 27ec0d9..e2cde15 --name-only` returns exactly three files: `docs/BUILD_PLAN.md`, `src/manifold/CameraObserverAdapter.h` (new), `tests/manifold_identity_tests.cpp`. Restricting to the broader source tree via `git diff 27ec0d9..e2cde15 --name-only -- 'src/*' ':(exclude)src/manifold/' 'tests/*' ':(exclude)tests/manifold_identity_tests.cpp' 'CMakeLists.txt'` returns **zero hits** — confirming zero touch on `src/cuda/`, `src/optix/`, `src/pathtracer/`, `src/renderer/`, `src/gpu/`, `src/scene/`, `src/io/`, `src/server/`, `src/camera/` (the host Camera class is unchanged), `src/core/`, `src/material/`, `src/lighting/`, `src/texture/`, `src/geometry/`, `src/image/`, `src/math/`, `src/relativity/`, `src/field/`, `src/main.cpp`, the `CMakeLists.txt` (no library / link-line / test-target change), or any non-manifold test binary.<br>The `CudaRenderer` / `OptixRenderer` `AOVTargets` / `OptixLaunchParams` shapes are unchanged from the post-OBSERVER.5 baseline. No kernel call site invokes the new adapter; the adapter is host-only by construction (`inline` function in a header-only file). The `manifold_identity_tests` link line is unchanged at `rr_manifold` (verified at `CMakeLists.txt:972` — the OBSERVER.6 commit modified zero CMake-side files). |
| 6 | No visual behaviour changes              | **PASS** | Four-layer no-visual-behaviour-change guarantee:<br>**(a) Default-default-default produces rest_frame() byte-for-byte** (check #2 evidence); the kernel-side OBSERVER.8+ paths will short-circuit on `PerceptionMode::Identity` to preserve byte-identity to today's renderer.<br>**(b) Zero kernel consumers today.** No call site in `src/cuda/`, `src/optix/`, `src/pathtracer/`, `src/renderer/`, `src/main.cpp`, or any other host-side dispatcher invokes `build_observer_frame_from_camera(...)`. Verified by `grep -rn "build_observer_frame_from_camera" src/` returning matches only in `src/manifold/CameraObserverAdapter.h` (the definition) — no host or device caller.<br>**(c) Zero `RelativityParams` flag propagation change.** The legacy `Observer` + `RelativityParams` types continue to flow through the existing CPU / CUDA / OptiX call paths verbatim. The adapter consumes `Observer::velocity` to populate the new `ObserverFrame::beta` but does NOT modify the source `Observer` or `RelativityParams` (the function is pure; takes by `const&` references).<br>**(d) Empirical ctest verification.** The non-`manifold_identity_tests` binaries remain at their post-OBSERVER.5 counts: `cli_tests: 254/254` (unchanged); `renderer_tests: 19/19` (unchanged); `pathtracer_tests`, `pathtracer_nee_tests`, `pathtracer_bsdf_tests`, `pathtracer_mis_tests`, `relativity_tests`, `math_tests`, `image_tests`, `gpu_tests`, `demo_tests` — all unchanged. No regression detected anywhere in the test surface. |
| 7 | Build / test status                      | **PASS** | Audit-host `cmake --build /home/user/RelativityRender/build` succeeds cleanly with no new warnings on the core / manifold modules. The new `src/manifold/CameraObserverAdapter.h` is consumed by the `manifold_identity_tests` binary cleanly via the shared `src/` include path (transitively from `rr_math`'s `target_include_directories(rr_math INTERFACE src)` at `CMakeLists.txt:135`); no new link dependency, no new CMake target.<br>Full `ctest` from the audit-host build directory: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `408 / 408 checks passed` (was `349 / 349` pre-OBSERVER.6 at the post-OBSERVER.5 baseline; **+59 new RR_CHECK assertions** from the 12 new test functions registered in `main()` at `tests/manifold_identity_tests.cpp:2077-2088` under the new `// OBSERVER.6: Camera-to-observer adapter` section). `cli_tests: 254/254 passed` (unchanged); `renderer_tests: 19/19 passed` (unchanged); all other test suites unchanged. |
| 8 | PASS / REPAIR / BLOCKED verdict          | **PASS** | All seven structural checks return `PASS`. No `REPAIR` or `BLOCKED` item is outstanding. The OBSERVER.6 adapter ships the documented three-mode construction surface (Identity / ConstantVelocityMinkowski / CurvedChartGeodesicPlaceholder), the documented beta-resolution priority (CLI overlay wins; zero-direction falls back to camera forward; default-config uses legacy `Observer::velocity`), and the documented defensive `clampBeta` second-clamp. Twelve new test functions cover every construction path + safety invariant. The adapter is structurally consumed by the test surface; OBSERVER.8 (CUDA payload bridge) will add the first kernel-side consumer. The slice is **safe to extend** under the renumbered OBSERVER.* ladder per §4 below. |

---

## 3. REASONING SUMMARY

The OBSERVER.6 commit (`e2cde15`) introduces:

- a new header-only file
  `src/manifold/CameraObserverAdapter.h` (222 lines)
  carrying:
    - the
      `build_observer_frame_from_camera(GpuCamera,
       Observer, ObserverConfig)` adapter function
      at line 138;
    - three perception-mode construction branches
      (Identity → `rest_frame()`;
      ConstantVelocityMinkowski → full construction;
      CurvedChartGeodesicPlaceholder → `rest_frame()`
      with `perception_mode` tag preserved);
    - the documented beta-resolution priority (CLI
      overlay wins; zero-direction sentinel falls
      back to `gc.forward`; default-config uses
      legacy `observer.velocity`);
    - a defensive `clampBeta` second-clamp on the
      resolved magnitude so a non-trivial legacy
      `observer.velocity` cannot push `gamma` to
      infinity.
- 12 new test functions appended to
  `tests/manifold_identity_tests.cpp` covering every
  construction path + safety invariant + the
  operator's four acceptance checks
  ("adapter produces valid ObserverFrame", "default
  camera → no-op observer", "finite-value
  guarantees", "existing relativity params
  propagate correctly").

The adapter-exists invariant (check #1) is **file-
level + signature-level + per-branch verified** at
documented file / line positions; all three
perception-mode branches resolve to construction
paths that the OBSERVER.1 plan §7 OBSERVER.6
contract specifies verbatim.

The default-camera-to-no-op invariant (check #2) is
**three-layer verified**: Identity path returns
`rest_frame()` byte-for-byte (verified
field-by-field); Identity mode ignores a
non-default camera (verified separately so the
contract is testable); ConstantVelocityMinkowski +
zero beta produces a camera-equivalent frame whose
kernel-side SR helpers collapse to identity.

The relativity-params-propagation invariant
(check #3) is **three-layer verified**: legacy
`Observer::velocity` propagates directly when the
CLI overlay is zero (verified by round-trip via
`to_relativity_observer`); the CLI overlay wins over
the legacy observer when both are present
(precedence verified); the zero-direction sentinel
falls back to `gc.forward` (sentinel verified). The
`RelativityParams` flags continue to flow through
the existing call paths verbatim; the adapter does
not silently re-route them (master rule #3
satisfied).

The finite-value-guarantees invariant (check #4) is
**four-layer verified**: adapter-level defensive
clamp; empirical clamp verification at `|beta| =
1.5`; cross-mode finite-value sweep over all three
perception modes; carry-forward of the OBSERVER.2
validator coverage. The `is_normalised_timelike`
exclusion at the `clampBeta` cap is documented as a
known floating-point catastrophic-cancellation
issue; the normalisation invariant is covered at
the precision-stable beta=0.5 regime.

The no-CUDA/OptiX-changes invariant (check #5) is
**directly verified** by `git diff 27ec0d9..e2cde15
--name-only` filtered against the renderer / kernel
subtrees returning zero hits. The OBSERVER.6 commit
is host-only by construction.

The no-visual-behaviour-changes invariant (check #6)
is **four-layer verified**: default produces
`rest_frame()`; zero kernel consumers
(`grep` verified); zero `RelativityParams` flag
propagation change; non-manifold test counts
unchanged.

The build/test status (check #7) is **directly
verified** by ctest 12/12 PASS + the
`manifold_identity_tests` +59 RR_CHECK delta with
no regression in any other test binary.

No `REPAIR` or `BLOCKED` action is outstanding. The
slice is safe to extend.

---

## 4. NEXT

The slice is **safe to extend**. The renumbered
`OBSERVER_FRAME_RENDERING_PLAN.md` §7 OBSERVER.*
sub-slice ladder needs a one-step shift to absorb
this audit slot, mirroring the OBSERVER.3 +
OBSERVER.5 audit-slot insertion precedent:

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
- **OBSERVER.7** — **THIS AUDIT**
  (Camera-to-Observer Adapter Audit, doc-only).
- **OBSERVER.8** — CUDA payload bridge (was
  OBSERVER.7 in the post-OBSERVER.5 plan;
  renumbered).
- **OBSERVER.9** — OptiX payload bridge (was
  OBSERVER.8).
- **OBSERVER.10** — Observer debug AOV (was
  OBSERVER.9).
- **OBSERVER.11** — Arc capstone audit (was
  OBSERVER.10); closes the observer-frame arc per
  the OBSERVER.1 plan §7.

The
`docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7
sub-slice ladder may be updated by a follow-on
docs slice if the operator prefers an in-plan
renumbering; this audit doc is the canonical
ladder-shift record for the OBSERVER.7 audit-slot
insertion.

No `REPAIR` action is required. No `BLOCKED` item
is outstanding. The next concrete commit the
operator may prompt for is **OBSERVER.8 — CUDA
payload bridge** per the renumbered OBSERVER.1
plan §7 OBSERVER.5 → OBSERVER.8 (adds an
`rr::manifold::ObserverFrame observer_frame{}`
field to `CudaSceneView` sibling of `manifold_mode`
+ `coordinate_chart`; the matching
`AOVTargets::observer_frame{}` slot on
`CudaRenderer.h`; dispatcher-side invocation of
`build_observer_frame_from_camera(...)` in
`main.cpp::run_render_aovs`; kernel-side read
under the existing
`PerceptionMode::ConstantVelocityMinkowski`
short-circuit guard so default-mode byte-identity
is preserved).

---

## 5. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") is the load-bearing invariant for the
  `CurvedChartGeodesicPlaceholder` path being a
  documented structural passthrough (not a fake
  GR solver) and for the adapter being
  structurally consumed by 12 tests + the planned
  OBSERVER.8 kernel-side consumer.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3
  Observer Frame — defines the seven-field
  ObserverFrame contract the adapter populates.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §6, §7
  OBSERVER.6 (renumbered from the original §7
  OBSERVER.4 after the OBSERVER.3 + OBSERVER.5
  audit-slot insertions) — the OBSERVER.1 plan
  brief that authorised the adapter's three-
  mode construction surface.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the prior per-slice audit on
  the underlying `ObserverFrame` POD +
  validators that the OBSERVER.6 adapter
  consumes; carry-forward of validator coverage.
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5) — the prior per-slice audit on
  the OBSERVER.4 CLI surface that the adapter
  consumes via the new `ObserverConfig`
  parameter.
- `docs/PENROSE_LIKE_CPU_INTEGRATION_AUDIT.md`
  (PENROSE.5) — the precedent host-only-impl
  audit doc this verdict mirrors in structure.
- `src/manifold/CameraObserverAdapter.h` (new
  at `e2cde15`) — the audited surface.
- `src/manifold/ObserverFrame.h` — the POD the
  adapter constructs; carries the
  `PerceptionMode` enum + the
  `is_finite_observer_frame` /
  `is_orthonormal_tetrad` /
  `is_normalised_timelike` validators the
  adapter's output passes.
- `src/camera/CameraRay.h` — defines the
  device-friendly `GpuCamera` POD that the
  adapter consumes (header-only; no
  `rr_camera` link dep needed for
  `rr_manifold` consumers).
- `src/camera/Camera.h` — the host-side
  Camera class that produces a `GpuCamera`
  via the existing `to_gpu()` method;
  unchanged by OBSERVER.6.
- `src/relativity/RelativityParams.h` — the
  legacy `Observer` type the adapter
  consumes; unchanged by OBSERVER.6.
- `src/relativity/RelativityMath.h` — provides
  `gamma` + `clampBeta` helpers the adapter
  uses; unchanged by OBSERVER.6.
- `tests/manifold_identity_tests.cpp`
  (modified at `e2cde15`) — 12 new test
  functions registered at lines 2077-2088;
  reports `408/408 checks passed` post-
  OBSERVER.6 (up from `349/349` at the
  post-OBSERVER.5 baseline).
- `tests/cli_tests.cpp` — unchanged by
  OBSERVER.6; reports `254/254 passed`
  (no regression from OBSERVER.4's surface).
- `docs/BUILD_PLAN.md` — OBSERVER.6 entry
  (lines 79644 onward as of `e2cde15`).
- Commit `e2cde15` — `manifold: OBSERVER.6 —
  Camera-to-Observer Adapter (impl,
  host-only)`.
- Commit `27ec0d9` — `docs: OBSERVER.5 —
  ObserverFrame Config / CLI Bridge Audit
  (docs only)`; the audit baseline.

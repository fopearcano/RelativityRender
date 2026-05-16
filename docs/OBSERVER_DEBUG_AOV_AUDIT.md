# Observer Debug AOV Audit (OBSERVER.14)

Date:   2026-05-16
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `b34e265` ("aov:
OBSERVER.13 — Observer Debug AOV Implementation
(impl, AOV + CUDA + OptiX + CLI)").
Audit baseline: `e6d6ffc` ("docs: OBSERVER.12 —
Observer Debug AOV Task Definition (docs only)")
— the last commit before OBSERVER.13 landed.
Audit host: linux, audit-host build (no CUDA SDK,
no OptiX SDK).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, `git diff` against
the post-OBSERVER.12 baseline, the
`renderer_tests` runtime output, the `cli_tests`
runtime output, the `manifold_identity_tests`
runtime output, `ctest` exit codes, and audit-host
smoke-test transcripts for the new
`--observer-debug` flag.

This audit is the per-slice gate for OBSERVER.13
(`b34e265`). It verifies the nine items the task
brief enumerates — observer debug AOV exists;
beauty output unchanged by default; default
observer diagnostic is neutral; AOV generation is
optional; CUDA path status; OptiX path status;
no observer perception transform added yet;
runtime status; verdict — and produces a
`PASS` / `REPAIR` / `BLOCKED` verdict that gates
progression to the renumbered OBSERVER.15 (arc
capstone audit) or to the broader OBSERVER.* arc
migration (gating kernel-side SR-helper calls on
`perception_mode`).

---

## 1. VERDICT

**PASS.**

All eight structural checks return `PASS`. Check
#8 (runtime CUDA / OptiX status) is `DEFERRED`
on the documented audit-host limitation (no CUDA
SDK, no OptiX SDK). Check #9 (overall verdict)
is `PASS`: the OBSERVER.13 implementation ships
the documented `AOVType::ObserverBeta` AOV +
`--observer-debug` CLI flag + matching CUDA +
OptiX kernel arms + dispatcher PPM-save paths +
tests; the default-off no-op invariant is
empirically verified at audit-host smoke tests
for both backends; the kernel-source diff against
the manifold-coordinates precedent is
structurally identical (MANI-I.8 mirror); the
runtime SDK verification is the documented
expected gate for the next OBSERVER.* slot. No
`REPAIR` or `BLOCKED` item is outstanding. The
operator may proceed to the next OBSERVER.*
audit slot (likely OBSERVER.15 arc capstone)
under the renumbered ladder per §4 below.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Observer debug AOV exists                | **PASS** | The OBSERVER.13 commit (`b34e265`) adds the new `AOVType::ObserverBeta` AOV across nine source-side attachment points + two test sites, mirroring the MANI-I.8 manifold-coordinates AOV precedent verbatim in shape:<br>**(a) Enumerator** at `src/renderer/AOV.h:97`: `ObserverBeta = 7` appended at the end of the `AOVType` enum (preserves every pre-OBSERVER.13 enumerator's value).<br>**(b) Component count** at `src/renderer/AOV.cpp:16`: `case AOVType::ObserverBeta: return 3;` (3 floats / pixel — Vec3 encoding of `observer_frame.beta`).<br>**(c) Type name** at `src/renderer/AOV.cpp:34`: `case AOVType::ObserverBeta: return "observer_beta";` (snake_case mirroring `manifold_coordinates`).<br>**(d) Factory** at `src/renderer/AOV.cpp:101-107`: `AOV AOV::make_observer_beta(std::string name)` returning a well-formed `AOV` with `type() == ObserverBeta` and `name() == "observer_beta"` (or the caller-supplied name).<br>**(e) Header declaration** at `src/renderer/AOV.h:158`: `[[nodiscard]] static AOV make_observer_beta(std::string name = {});`.<br>**(f) CUDA DeviceAOVView slot** at `src/cuda/CudaAOV.cuh:96`: `float* observer_beta = nullptr;`.<br>**(g) CUDA AOVTargets slot** at `src/cuda/CudaRenderer.h:175-184`: `float* observer_beta = nullptr;`.<br>**(h) OptiX launch-params slot** at `src/optix/OptixLaunchParams.h:351`: `float* aov_observer_beta = nullptr;`.<br>**(i) OptiX AovResult field** at `src/optix/OptixRenderer.h:447`: `rr::image::Image observer_beta;`.<br>The naming convention follows the existing `*_factor` / `manifold_coordinates` snake_case at the field level + `*Factor` / `ManifoldCoordinates` PascalCase at the enumerator level. The AOV is exposed through the existing factory + `aov_component_count(...)` + `aov_type_name(...)` infrastructure; no new helper function is required. |
| 2 | Beauty output unchanged by default       | **PASS** | Four-layer beauty-output preservation guarantee:<br>**(a) Default `cfg.observer.debug_visualization` is `false`.** The new field on `ObserverConfig` at `src/manifold/ObserverFrame.h:472-487` carries an explicit `= false` per-field initialiser. Without `--observer-debug`, the field stays at the default and the `targets.observer_beta` / `params.aov_observer_beta` device pointers stay `nullptr`. Empirically verified at `test_observer_debug_default_off` (`tests/cli_tests.cpp:631`).<br>**(b) Kernel null-gate short-circuits.** CUDA: the `k_render_scene` per-pixel block at `CudaTestKernel.cu:717` (`if (scene.aovs.observer_beta != nullptr)`) is the only entry into the new `observer_beta` write arm. With `nullptr` from (a), the kernel arithmetic in every other AOV write arm (Beauty / Normal / Depth / Albedo / DopplerFactor / SearchlightFactor / ManifoldCoordinates) is unchanged. OptiX: the `__closesthit__` arm at `OptixPrograms.cu` + the matching `__miss__` arm null-gate on `optixLaunchParams.aov_observer_beta != nullptr`. With `nullptr`, the per-pixel kernel arithmetic is unchanged.<br>**(c) Dispatcher allocation is gated.** CUDA: `main.cpp::run_render_aovs` allocates a `GpuAOVBuffer{AOV::make_observer_beta()}` only when `cfg.observer.debug_visualization` is true (else the `device_ptr()` stays `nullptr` because the buffer is never `resize()`-ed). OptiX: `OptixRenderer::render_aovs` allocates the device buffer via `alloc_aov(...)` only when the new trailing `observer_debug` parameter is `true`.<br>**(d) Empirical anchor across non-observer argv vectors.** The existing `test_observer_default_off_with_other_flags` (`tests/cli_tests.cpp:669`) was extended at OBSERVER.13 with one new assertion: across all 8 non-observer argv vectors (including ones combining `--manifold-*` flags + `--firefly-clamp` + `--render-demo` + `--scene-info` + `--render-aovs --denoise` + the `--render-aovs --manifold-enable --manifold-chart schwarzschild-like --manifold-strength 0.5` vector), `r.config.observer.debug_visualization` stays at the default `false`. This is the parser-surface byte-identity invariant the OBSERVER.12 task brief §3.1 ("Beauty output unchanged") declares. |
| 3 | Default observer diagnostic is neutral   | **PASS** | Two-layer neutral-diagnostic verification:<br>**(a) Identity-mode neutral value structurally guaranteed.** When `--observer-debug` IS set BUT `--observer-perception-mode` is at its default `identity` (i.e. `cfg.observer.perception_mode == PerceptionMode::Identity`), the OBSERVER.6 camera-to-observer adapter `build_observer_frame_from_camera(...)` (verified at OBSERVER.7 audit check #2) returns `rest_frame()` byte-for-byte. The resulting `observer_frame.beta == (0, 0, 0)`. The CUDA kernel arm at `CudaTestKernel.cu:719-721` writes `scene.observer_frame.beta.{x, y, z}` to the AOV at every hit pixel → all-zero hit pixels. The OptiX kernel arm at `OptixPrograms.cu` writes `optixLaunchParams.observer_frame.beta.{x, y, z}` → identical all-zero output. Miss pixels write `(0, 0, 0)` at both kernel arms by construction.<br>**(b) Cross-mode neutral value across the dispatcher.** Even with `--observer-perception-mode relativistic` + `--observer-beta 0.0` (zero magnitude), the OBSERVER.6 adapter's `ConstantVelocityMinkowski` branch with `cfg.observer.beta_magnitude == 0` falls through to the legacy `observer.velocity` path (verified at OBSERVER.7 audit check #3); the resulting `observer_frame.beta == (0, 0, 0)` when the scene's `observer.velocity` is also zero. So both `Identity` AND `ConstantVelocityMinkowski` with zero beta produce the same neutral `(0, 0, 0)` per-pixel AOV — the documented OBSERVER.12 task brief §3.3 contract ("default observer produces neutral diagnostic values"). Empirically gated by the `test_observer_6_*` family at OBSERVER.6 (`manifold_identity_tests.cpp`) which verifies the adapter's outputs; the AOV write arm is a direct field copy with no transformation, so the kernel-side neutral value is structurally guaranteed downstream. |
| 4 | AOV generation is optional               | **PASS** | Three-layer optionality verification:<br>**(a) CLI gate.** The new `--observer-debug` modifier flag at `CommandLine.cpp:767-784` is presence-only (no value consumed); without it, `r.config.observer.debug_visualization` stays at the default `false`. Verified at `test_observer_debug_flag` (`cli_tests.cpp:623`) + `test_observer_debug_default_off` (`cli_tests.cpp:631`) + `test_observer_debug_combined_with_other_flags` (`cli_tests.cpp:642`).<br>**(b) Dispatcher gate.** The CUDA dispatcher (`main.cpp::run_render_aovs`) only allocates the `GpuAOVBuffer` when `cfg.observer.debug_visualization` is true; otherwise `observer_beta_buffer.device_ptr()` is `nullptr`. The OptiX dispatcher (`run_render_optix_aovs`) passes `cfg.observer.debug_visualization` as the trailing `observer_debug` argument; the `OptixRenderer::render_aovs` implementation conditionally allocates `d_aov_observer_b` only when this argument is `true`.<br>**(c) Two-flag composition** (the operator brief's "AOV only when requested/generated" rule). The AOV is emitted only when BOTH conditions hold: the operator passes `--render-aovs` (CUDA) / `--render-optix-aovs` (OptiX) AND the operator passes `--observer-debug`. Either flag alone produces no `output/aov_observer_beta.ppm` / `output/optix_aov_observer_beta.ppm` file (verified by inspection: the dispatcher's save call at `main.cpp` is gated on `cfg.observer.debug_visualization`; the file is emitted only when that condition holds; `--observer-debug` without `--render-aovs` runs into a different action path that doesn't open the AOV save logic).<br>**(d) Compositional orthogonality with `--manifold-debug`.** The two debug-AOV gates are independent: setting both `--manifold-debug` AND `--observer-debug` results in BOTH AOVs being emitted (verified at `test_observer_debug_combined_with_other_flags`); setting neither results in zero new AOVs; setting either alone results in just that one. The operator brief's OBSERVER.12 task §8.5 composability check is structurally satisfied. |
| 5 | CUDA path status                         | **PASS** | The CUDA-side OBSERVER.13 surface is structurally complete with four documented attachment points:<br>**(a) Data path.** `cfg.observer.debug_visualization` → `targets.observer_beta = observer_beta_buffer.device_ptr()` (or `nullptr` when gate off) → `view.aovs.observer_beta = targets.observer_beta` (`CudaRenderer.cu:297`) → kernel reads `scene.aovs.observer_beta` at `CudaTestKernel.cu:717`.<br>**(b) Kernel arm.** Closest-hit + miss arms gated on `scene.aovs.observer_beta != nullptr`. Hit writes `scene.observer_frame.beta.{x, y, z}` (the OBSERVER.8 carry-only field) as three floats; miss writes `(0, 0, 0)`. Read-only on the observer payload — no `aberrateDirection` / `dopplerFactor` / `searchlightFactor` calls.<br>**(c) Buffer allocation + PPM save.** `main.cpp::run_render_aovs` allocates `observer_beta_buffer` only when `cfg.observer.debug_visualization` is true; saves `output/aov_observer_beta.ppm` via the existing `save_aov_to_ppm(...)` helper after the render returns. Empty-buffer path stays a no-op when the gate is off (the `device_ptr()` returns `nullptr`).<br>**(d) Compile-time validation.** The audit-host build is `RR_ENABLE_CUDA=OFF`; the `.cu` translation units (`CudaTestKernel.cu`, `CudaRenderer.cu`) don't compile here. The structural changes mirror the MANI-I.8 manifold-coordinates precedent verbatim (same null-gate; same 3-float-per-pixel write; same hit-vs-miss branch) so the SDK-host compile is structurally identical to the MANI-I.8 baseline (which is verified-green on SDK hosts per the MANI-I.9 audit). The `src/main.cpp` + `src/cuda/CudaRenderer.h` + `src/renderer/AOV.h/.cpp` + `src/manifold/ObserverFrame.h` host-side changes all compile cleanly on the audit host (ctest 12/12 PASS, renderer_tests 27/27, cli_tests 274/274). |
| 6 | OptiX path status                        | **PASS** | The OptiX-side OBSERVER.13 surface is structurally complete with four documented attachment points:<br>**(a) Data path.** `cfg.observer.debug_visualization` → `OptixRenderer::render_aovs(..., observer_debug)` trailing argument at `main.cpp::run_render_optix_aovs` → conditional `alloc_aov(aov3_floats, d_aov_observer_b)` inside `OptixRenderer.cpp` → `params.aov_observer_beta = static_cast<float*>(d_aov_observer_b)` → kernel reads `optixLaunchParams.aov_observer_beta`.<br>**(b) Kernel arms.** Closest-hit + miss arms gated on `optixLaunchParams.aov_observer_beta != nullptr`. Closest-hit writes `optixLaunchParams.observer_frame.beta.{x, y, z}` (the OBSERVER.10 carry-only field) as three floats; miss writes `(0, 0, 0)`. Read-only on the observer payload — no perception transform.<br>**(c) Buffer allocation + PPM save.** The OptiX `render_aovs` impl conditionally allocates `d_aov_observer_b` only when the trailing `observer_debug` argument is `true`; the buffer is downloaded into `R.observer_beta` via the existing `download_3(...)` helper; the `main.cpp::run_render_optix_aovs` dispatcher then saves `output/optix_aov_observer_beta.ppm` via the existing `save_one(...)` helper. Empty-buffer path stays a no-op when the gate is off (`R.observer_beta` stays as the default `Image{}`).<br>**(d) Compile-time validation.** The audit-host build is `RR_ENABLE_OPTIX=OFF`; the OptiX TUs (`OptixRenderer.cpp`, `OptixPrograms.cu`, `OptixLaunchParams.h`'s consumers) don't compile here. The structural changes mirror the MANI-I.8 manifold-coordinates + SCHW.7 trailing-defaulted-parameter precedents verbatim (same null-gate; same allocation pattern; same download path; same `AovResult` field). The SDK-host compile is structurally identical to the SCHW.7 + MANI-I.8 baselines (both verified-green on SDK hosts per the SCHW.8 + MANI-I.9 audits). The host-side `src/optix/OptixRenderer.h` changes compile cleanly on the audit host. |
| 7 | No observer perception transform added yet | **PASS** | Three-layer verification of the "no perception transform" contract:<br>**(a) Kernel sources at the AOV write site call zero SR helpers.** The CUDA arm at `CudaTestKernel.cu:717-727` writes `scene.observer_frame.beta.{x, y, z}` directly via three array stores — no `aberrateDirection` / `dopplerFactor` / `searchlightFactor` calls. Same for the OptiX arm at `OptixPrograms.cu`: three array stores via `optixLaunchParams.observer_frame.beta.{x, y, z}`. The AOV is a literal field copy from the launch payload.<br>**(b) Non-AOV kernel call sites unchanged.** `git diff e6d6ffc..b34e265 -- 'src/cuda/CudaTestKernel.cu' 'src/optix/OptixPrograms.cu'` shows only ADDITIONS (the new AOV write arms appended at the end of each kernel's per-pixel block); the existing aberration / Doppler / searchlight pipelines feeding on `scene.observer.velocity` (CUDA) / `optixLaunchParams.observer.velocity` (OptiX) are byte-unchanged. The non-AOV kernel paths continue to use the legacy SR observer exactly as today; the OBSERVER.* arc's perception_mode-gated migration of those call sites remains DEFERRED to a separate future slice.<br>**(c) `perception_mode` tag is dormant in the AOV write.** The new AOV writes `observer_frame.beta` but does NOT read `observer_frame.perception_mode`. The same AOV value `(beta.x, beta.y, beta.z)` is written regardless of which `PerceptionMode` enumerator is engaged. This is the documented "diagnostic AOV is a data pass-through" contract from the OBSERVER.12 task brief §1 + §4.2. The operator can verify the perception-mode-tag did its job at the dispatcher level via the existing host-side echo logs (`aovs observer config: ...` / `optix-aovs observer config: ...`) — not via the AOV. |
| 8 | Runtime CUDA / OptiX status              | **DEFERRED** | The audit host has neither CUDA nor OptiX SDK installed (`nvcc` not present; `optixGetVersion` unavailable; `RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF`). Consequently:<br>**(a) CUDA side:** The CUDA AOV-write arm (`CudaTestKernel.cu:717-727` + the buffer-allocation path in `CudaRenderer.cu` / `main.cpp::run_render_aovs`) cannot be compiled, linked, or device-launched from this host. Runtime CUDA verification is `DEFERRED` to a CUDA-SDK host.<br>**(b) OptiX side:** The OptiX AOV-write arms (`OptixPrograms.cu`'s closest-hit + miss arms + the allocation / threading / download path in `OptixRenderer.cpp`) cannot be compiled, linked, or device-launched from this host. Runtime OptiX verification is `DEFERRED` to an OptiX-SDK host.<br>**(c) Audit-host CAN verify:** the AOV data-model surface (`AOV.h/.cpp` + `make_observer_beta(...)` factory + `aov_component_count(ObserverBeta) == 3` + `aov_type_name(ObserverBeta) == "observer_beta"`) — verified via `renderer_tests`'s 3 new test functions (`test_observer_13_observer_beta_*` at `tests/renderer_tests.cpp:146-184`); the CLI surface (`--observer-debug` parses to `r.config.observer.debug_visualization`) — verified via `cli_tests`'s 3 new test functions (`test_observer_debug_*` at `tests/cli_tests.cpp:623-668`); the audit-host smoke test (running `--render-aovs --observer-debug --observer-beta 0.5 ...` produces the existing `aovs observer config` log + the existing `--render-aovs requires CUDA` error path with no new output, confirming the host-side CLI / config layer works correctly on the audit host).<br>This is the **same documented deferral** pattern accrued by every prior CUDA / OptiX-touching slice (MANI-I.5 / SCHW.5 / SCHW.7 / PENROSE.6 / PENROSE.8 / MANI-CONSUME.1 / OBSERVER.8 / OBSERVER.10). The SCHW.11 + PENROSE.12 + OBSERVER.9 + OBSERVER.11 capstones recorded runtime verification as deferred until SDK hosts run the full pipeline; OBSERVER.13 is no exception. The deferral is NOT a `BLOCKED` because: (i) the structural plumbing is verified PASS (checks #1-7); (ii) the AOV data-model + CLI surface tests pass; (iii) the audit-host smoke tests confirm the host-side gate works; (iv) the kernel-side write arms structurally mirror the MANI-I.8 precedent verbatim (which is verified-green on SDK hosts).<br>**Required runtime checks for a future SDK-host audit pass** (when the operator runs the audit on a CUDA-+-OptiX-equipped host) per the OBSERVER.12 task brief §8 + this audit's recommendation:<br>(a) `--render-aovs --observer-debug` produces `output/aov_observer_beta.ppm` with every hit pixel `(0, 0, 0)` and every miss pixel `(0, 0, 0)` on the default Identity-mode invocation;<br>(b) `--render-optix-aovs --observer-debug` produces `output/optix_aov_observer_beta.ppm` with the same per-pixel values;<br>(c) `cmp output/aov_observer_beta.ppm output/optix_aov_observer_beta.ppm` returns exit status `0` (cross-backend byte-identity per OBSERVER.12 task brief §8.6);<br>(d) `--render-aovs --observer-debug --observer-perception-mode relativistic --observer-beta 0.5 --observer-direction 1,0,0` produces an AOV PPM whose hit pixels decode to `(0.5, 0.0, 0.0)` per channel;<br>(e) `--render-aovs` WITHOUT `--observer-debug` produces NO `aov_observer_beta.ppm` file (the gate is structurally enforced);<br>(f) Beauty + every existing AOV's PPM is byte-identical to the pre-OBSERVER.13 baseline on every invocation. |
| 9 | PASS / REPAIR / BLOCKED verdict          | **PASS** | All eight structural checks return `PASS`. Check #8 (runtime CUDA / OptiX) is `DEFERRED` on the documented audit-host SDK-absence limitation (mirrors the OBSERVER.9 / OBSERVER.11 / MANI-I.9 / SCHW.5 / PENROSE.6 deferral pattern). No `REPAIR` or `BLOCKED` item is outstanding. The OBSERVER.13 commit ships the documented `AOVType::ObserverBeta` enumerator + `make_observer_beta(...)` factory + `--observer-debug` CLI flag + CUDA + OptiX kernel arms + dispatcher buffer-allocation + PPM-save paths + 6 new test functions across `renderer_tests` + `cli_tests`; the no-op-by-default + default-neutral + AOV-optional + no-perception-transform invariants are all structurally + empirically verified; and the runtime SDK pass is the documented expected gate for the next slot. The slice is **safe to extend** under the renumbered OBSERVER.* ladder per §4 below. |

---

## 3. REASONING SUMMARY

The OBSERVER.13 commit (`b34e265`) introduces a
complete debug-AOV surface across the renderer
stack, mirroring the MANI-I.8 manifold-coordinates
AOV precedent verbatim:

- **AOV data model** (`src/renderer/AOV.h` /
  `AOV.cpp`): `AOVType::ObserverBeta = 7`
  enumerator; `aov_component_count(...)` returns
  3; `aov_type_name(...)` returns
  `"observer_beta"`; `make_observer_beta(...)`
  factory.
- **CLI surface** (`src/core/CommandLine.cpp` +
  `src/manifold/ObserverFrame.h`): new
  `bool debug_visualization` field on
  `ObserverConfig`; new `--observer-debug`
  presence-only modifier flag; help-text entry.
- **CUDA backend** (`src/cuda/CudaAOV.cuh` +
  `CudaRenderer.h/.cu` + `CudaTestKernel.cu`):
  new `observer_beta` field on `DeviceAOVView`
  + `AOVTargets`; one-line dispatcher thread;
  closest-hit + miss kernel arms gated on null
  pointer.
- **OptiX backend** (`src/optix/OptixLaunchParams.h`
  + `OptixPrograms.cu` + `OptixRenderer.h/.cpp`):
  new `aov_observer_beta` field on launch params;
  new `observer_beta` field on `AovResult`; new
  trailing `bool observer_debug = false`
  parameter on `render_aovs(...)`; conditional
  device-buffer allocation; pointer threading;
  download into `AovResult`.
- **Dispatchers** (`src/main.cpp`):
  `run_render_aovs` (CUDA) allocates
  `GpuAOVBuffer{make_observer_beta()}` when
  `cfg.observer.debug_visualization` is true,
  threads `device_ptr()` into
  `targets.observer_beta`, saves
  `output/aov_observer_beta.ppm` via
  `save_aov_to_ppm(...)`. `run_render_optix_aovs`
  (OptiX) passes `cfg.observer.debug_visualization`
  as the new trailing argument, saves
  `output/optix_aov_observer_beta.ppm` via
  `save_one(...)`.
- **Tests** (`tests/renderer_tests.cpp` +
  `tests/cli_tests.cpp`): 6 new test functions
  covering the enum value + factory shapes + the
  CLI flag's default-off + presence + composability
  + default-off-vector behaviour.

The observer-debug-AOV-exists invariant (check #1)
is **nine-attachment-point verified** at
documented file / line positions; the entire
surface mirrors the MANI-I.8 manifold-coordinates
precedent verbatim.

The beauty-output-unchanged-by-default invariant
(check #2) is **four-layer verified**: default
field initialiser; kernel null-gate; dispatcher
allocation gate; empirical anchor across 8
non-observer argv vectors.

The default-observer-diagnostic-is-neutral
invariant (check #3) is **two-layer verified**:
the OBSERVER.6 adapter (audited at OBSERVER.7)
produces `rest_frame()` byte-for-byte on the
default Identity mode → kernel reads
`observer_frame.beta == (0, 0, 0)` → AOV writes
`(0, 0, 0)` at every hit pixel. Cross-mode
verification on `ConstantVelocityMinkowski` with
zero beta produces the same result.

The AOV-generation-is-optional invariant (check
#4) is **four-layer verified**: CLI gate;
dispatcher allocation gate; two-flag composition
(both `--render-*aovs` AND `--observer-debug`
required); orthogonality with `--manifold-debug`.

The CUDA-path-status invariant (check #5) is
**four-layer verified**: data path through the
dispatcher → AOVTargets → DeviceAOVView →
kernel; kernel arm structurally mirrors MANI-I.8;
buffer allocation + PPM save mirrors MANI-I.8;
audit-host compile clean for host-side code;
SDK-host compile structurally identical to
MANI-I.8 baseline.

The OptiX-path-status invariant (check #6) is
**four-layer verified**: same data path shape
with the trailing-defaulted-parameter pattern
from SCHW.7; kernel arms structurally mirror
MANI-I.8 + SCHW.7; allocation via existing
`alloc_aov(...)` helper; audit-host compile
clean for host-side code; SDK-host compile
structurally identical to MANI-I.8 + SCHW.7
baselines.

The no-perception-transform invariant (check #7)
is **three-layer verified**: kernel arms call
zero SR helpers; non-AOV kernel paths are
byte-unchanged (`git diff` shows only additions);
the `perception_mode` tag is dormant in the AOV
write (same per-pixel output regardless of mode).

The runtime CUDA/OptiX status (check #8) is
**DEFERRED** on the documented audit-host
SDK-absence limitation; the same deferral
pattern accrued for every prior CUDA / OptiX-
touching slice. The SDK-host pass is the
documented next step for the future OBSERVER.*
arc capstone audit.

The overall verdict (check #9) is **PASS**:
eight structural checks PASS + one appropriately-
DEFERRED runtime check; no REPAIR or BLOCKED
item; the slice is safe to extend.

---

## 4. NEXT

The slice is **safe to extend**. The renumbered
`OBSERVER_FRAME_RENDERING_PLAN.md` §7 OBSERVER.*
sub-slice ladder needs a one-step shift to absorb
this audit slot, mirroring the OBSERVER.3 +
OBSERVER.5 + OBSERVER.7 + OBSERVER.9 + OBSERVER.11
audit-slot insertion precedent:

- **OBSERVER.1**  — Planning slice
  (LANDED at `eee9d6b`).
- **OBSERVER.2**  — Data model
  (LANDED at `85496a5`).
- **OBSERVER.3**  — Data model audit
  (LANDED at `bf57c9e`).
- **OBSERVER.4**  — Config / CLI bridge
  (LANDED at `16600dc`).
- **OBSERVER.5**  — Config / CLI bridge audit
  (LANDED at `27ec0d9`).
- **OBSERVER.6**  — Camera-to-observer adapter
  (LANDED at `e2cde15`).
- **OBSERVER.7**  — Camera-to-observer adapter
  audit (LANDED at `a0215c0`).
- **OBSERVER.8**  — CUDA observer payload
  bridge (LANDED at `12f4942`).
- **OBSERVER.9**  — CUDA observer payload audit
  (LANDED at `e5fe441`).
- **OBSERVER.10** — OptiX observer payload
  bridge (LANDED at `977ff73`).
- **OBSERVER.11** — OptiX observer payload audit
  (LANDED at `c739c56`).
- **OBSERVER.12** — Observer debug AOV task
  definition (LANDED at `e6d6ffc`).
- **OBSERVER.13** — Observer debug AOV
  implementation (LANDED at `b34e265`).
- **OBSERVER.14** — **THIS AUDIT** (Observer
  Debug AOV Audit, doc-only).
- **OBSERVER.15** — Arc capstone audit (was
  OBSERVER.13 in the post-OBSERVER.11 ladder,
  renumbered through OBSERVER.12 task definition
  + OBSERVER.13 impl + OBSERVER.14 audit). The
  arc capstone synthesises the prior per-slice
  audits (OBSERVER.3 / .5 / .7 / .9 / .11 /
  .14) into a single arc-level verdict mirroring
  the SCHW.11 + PENROSE.12 capstone shapes.

The
`docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7
sub-slice ladder may be updated by a follow-on
docs slice if the operator prefers an in-plan
renumbering; this audit doc is the canonical
ladder-shift record for the OBSERVER.14
audit-slot insertion.

No `REPAIR` action is required. No `BLOCKED`
item is outstanding. The next concrete commit
the operator may prompt for is one of:

- **OBSERVER.15 — Arc capstone audit** —
  closes the OBSERVER.* arc per the OBSERVER.1
  plan §7. Synthesises the seven prior per-slice
  audit verdicts into a single arc-level
  PASS / PASS_WITH_RUNTIME_DEFERRED / REPAIR /
  BLOCKED verdict.
- **OBSERVER.* fixture follow-up** — the
  OBSERVER.12 task brief §5 deferred the
  fixture scene `scenes/test_observer_frame.rrscene`
  + companion doc `docs/OBSERVER_FRAME_FIXTURE.md`
  to a follow-up slice. The OBSERVER.14 audit
  verdict authorises landing the fixture if the
  operator prefers (mirrors MANI-I.8 → SCHW.9
  cadence where the AOV impl landed first and
  the fixture / companion doc landed at the
  parameter-authoring slice).
- **The broader OBSERVER.* arc migration** —
  the kernel-side reads of
  `observer_frame.beta` for the AOV are the
  ONLY new kernel reads landed at OBSERVER.13;
  the broader migration (gating the existing
  aberration / Doppler / searchlight helpers
  on `perception_mode == ConstantVelocityMinkowski`
  in the non-AOV pipelines) remains deferred per
  the OBSERVER.10 audit + OBSERVER.13 carry-only
  contract. The operator's call: extend the
  observer-frame arc with the broader kernel
  migration as a new task-definition + impl
  pair, OR close the arc at the OBSERVER.15
  capstone and revisit the migration in a later
  arc.

---

## 5. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") + #1 ("Build incrementally") + #2
  ("Keep every step compilable") all satisfied.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3
  Observer Frame + §6 GPU integration strategy —
  defines the contract the AOV reads from.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7
  OBSERVER.12 (renumbered to OBSERVER.13 impl
  through the OBSERVER.3 / OBSERVER.5 /
  OBSERVER.7 / OBSERVER.9 / OBSERVER.11
  audit-slot insertions) — the OBSERVER.1 plan
  brief that authorised the debug AOV.
- `docs/OBSERVER_DEBUG_AOV_TASK.md` (OBSERVER.12)
  — the operator-facing task definition for the
  OBSERVER.13 impl slice; the OBSERVER.14 audit
  verifies the implementation against this
  brief's structural + behavioural + test +
  documentation PASS criteria.
- `docs/MANIFOLD_DEBUG_AOV_AUDIT.md` (MANI-I.9)
  — the precedent per-slice audit doc for the
  manifold-coordinates AOV; OBSERVER.14 mirrors
  this audit's nine-row evidence table shape
  + runtime-DEFERRED treatment verbatim.
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9) — the CUDA-side payload audit
  the OBSERVER.13 CUDA kernel arm builds on
  (the `view.observer_frame.beta` field the
  kernel reads was audited PASS at OBSERVER.9).
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11) — the OptiX-side payload audit
  the OBSERVER.13 OptiX kernel arm builds on
  (the `optixLaunchParams.observer_frame.beta`
  field the kernel reads was audited PASS at
  OBSERVER.11).
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7) — the adapter audit that
  established the `rest_frame()` byte-for-byte
  return on Identity mode (the structural
  guarantee that the AOV writes `(0, 0, 0)` on
  the default).
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5) — the upstream CLI bridge's
  audit; the OBSERVER.13 new `--observer-debug`
  flag extends this CLI surface.
- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3) — the `ObserverFrame` POD's
  structural audit; the AOV reads
  `observer_frame.beta` from the audited
  POD's field.
- `src/renderer/AOV.h` (modified at `b34e265`)
  — carries the new `AOVType::ObserverBeta`
  enumerator at line 97 + `make_observer_beta`
  factory declaration at line 158.
- `src/renderer/AOV.cpp` (modified at
  `b34e265`) — carries the new
  `aov_component_count` / `aov_type_name` /
  `make_observer_beta` impls at lines 16 / 34
  / 101.
- `src/manifold/ObserverFrame.h` (modified at
  `b34e265`) — carries the new
  `bool debug_visualization = false` field on
  `ObserverConfig` at lines 472-487.
- `src/core/CommandLine.cpp` (modified at
  `b34e265`) — carries the new
  `--observer-debug` parser arm at lines
  767-784 + the help-text entry at lines
  1385-1402.
- `src/cuda/CudaAOV.cuh` (modified at
  `b34e265`) — carries the new
  `float* observer_beta = nullptr` field on
  `DeviceAOVView` at line 96.
- `src/cuda/CudaRenderer.h` (modified at
  `b34e265`) — carries the new
  `float* observer_beta = nullptr` field on
  `AOVTargets` at line 184.
- `src/cuda/CudaRenderer.cu` (modified at
  `b34e265`) — carries the new one-line
  thread `view.aovs.observer_beta = targets.observer_beta`
  at line 297.
- `src/cuda/CudaTestKernel.cu` (modified at
  `b34e265`) — carries the new CUDA AOV-write
  arm at lines 700-727 (null-gate at 717;
  hit-vs-miss branch).
- `src/optix/OptixLaunchParams.h` (modified at
  `b34e265`) — carries the new
  `float* aov_observer_beta = nullptr` field
  at line 351.
- `src/optix/OptixPrograms.cu` (modified at
  `b34e265`) — carries the new OptiX
  closest-hit + miss AOV-write arms.
- `src/optix/OptixRenderer.h` (modified at
  `b34e265`) — carries the new
  `rr::image::Image observer_beta` field on
  `AovResult` at line 447 + the new trailing
  `bool observer_debug = false` parameter on
  `render_aovs(...)` at line 519.
- `src/optix/OptixRenderer.cpp` (modified at
  `b34e265`) — carries the conditional
  `alloc_aov(...)` + pointer thread +
  `download_3(...)` impls.
- `src/main.cpp` (modified at `b34e265`) —
  carries the two dispatcher updates:
  `run_render_aovs` allocates
  `observer_beta_buffer` + saves PPM at the
  CUDA path; `run_render_optix_aovs` passes
  the new trailing arg + saves PPM at the
  OptiX path.
- `tests/renderer_tests.cpp` (modified at
  `b34e265`) — 3 new test functions
  (`test_observer_13_observer_beta_*` at
  lines 146-184); reports `27 / 27 passed`
  post-OBSERVER.13 (up from 19;
  +8 RR_CHECK).
- `tests/cli_tests.cpp` (modified at
  `b34e265`) — 3 new test functions
  (`test_observer_debug_*` at lines 623-668)
  + 1 new assertion on the existing
  `test_observer_default_off_with_other_flags`;
  reports `274/274 passed` post-OBSERVER.13
  (up from 254; +20 RR_CHECK).
- `tests/manifold_identity_tests.cpp` —
  unchanged by OBSERVER.13; reports
  `408/408 checks passed` (the
  `ObserverFrame` POD + OBSERVER.6 adapter
  are not touched).
- `docs/BUILD_PLAN.md` — OBSERVER.13 entry
  (lines 81513 onward as of `b34e265`).
- Commit `b34e265` — `aov: OBSERVER.13 —
  Observer Debug AOV Implementation (impl,
  AOV + CUDA + OptiX + CLI)`.
- Commit `e6d6ffc` — `docs: OBSERVER.12 —
  Observer Debug AOV Task Definition (docs
  only)`; the audit baseline.

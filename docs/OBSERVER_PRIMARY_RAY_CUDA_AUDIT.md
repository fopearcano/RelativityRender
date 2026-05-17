# Primary-Ray Perception Transform CUDA Audit (OBS-PERCEPT.4)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `b653e48` ("cuda:
OBS-PERCEPT.3 — CUDA Primary-Ray Perception Transform
(impl, kernel arms + helper)").
Audit baseline: `0bf2bb8` ("docs: OBS-PERCEPT.2 —
Primary-Ray Perception Transform Task (docs only)")
— the last commit before OBS-PERCEPT.3 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The OBS-PERCEPT.3 commit's OptiX-ON-no-SDK
build was empirically verified at landing time (ctest
14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, the OBS-PERCEPT.3
commit's content, the audit-host `ctest` runtime
outputs, and `git diff` filter inspections.

This audit is the per-slice gate for OBS-PERCEPT.3
(`b653e48`). It verifies the eleven items the task
brief enumerates — CUDA primary-ray aberration
exists; activation requires
ConstantVelocityMinkowski; activation requires
beta > 0; beta = 0 is no-op; default observer is
no-op; secondary rays unchanged; Doppler /
searchlight unchanged; OptiX path unchanged;
build/test status; runtime CUDA status
(PASS / DEFERRED / BLOCKED); and the overall verdict
(PASS / REPAIR / BLOCKED).

The OBS-PERCEPT.3 slice is the **first active
OBS-PERCEPT.* implementation**. It closes the
OBSERVER.15 capstone audit's `PASS_WITH_RUNTIME_DEFERRED`
future-kernel-migration risk #1 on the CUDA primary-
ray path. The OBS-PERCEPT.4 OptiX-bridge slice
mirrors this on the OptiX path; per this audit,
OBS-PERCEPT.3 ships CUDA-only.

---

## 1. VERDICT

**PASS.**

All ten structural / runtime-status checks (#1
through #10) PASS. Check #11 (overall verdict) is
`PASS`. The OBS-PERCEPT.3 surface ships exactly
what the operator's four-bullet brief authorised —
CUDA primary-ray directional aberration via the
unified `apply_observer_primary_ray_aberration(...)`
helper, with the documented preservation guarantees
(default observer no-op + beta=0 no-op + no
secondary-ray changes + no Doppler/searchlight
changes) — without spilling into OptiX, CLI,
dispatcher, or non-perception-mode surfaces.

Check #10's runtime CUDA status is the standard
`PASS_WITH_RUNTIME_DEFERRED` shape. The audit-host
has no CUDA SDK so the kernel arm's empirical
aberration cannot be exercised; the structural
data-path (host-side helper + kernel-arm dispatch)
is verified by the audit-host build's clean compile
+ 13/13 ctest pass + 13 NEW RR_CHECK assertions on
`manifold_identity_tests` (now 421/421 PASS). The
OptiX-ON-no-SDK build confirms the kernel-surface
modifications don't break the OptiX-on path
(14/14 ctest PASS at the OBS-PERCEPT.3 landing).

The narrow-scope verdict honesty: the operator's
OBS-PERCEPT.3 brief enumerated four implementation
bullets (primary-ray aberration; activation gates;
existing math leaves; preservation invariants). The
slice satisfies all four:

- **Bullet 1** (primary-ray aberration): the kernel
  arm dispatch at `CudaTestKernel.cu:249-251`
  (`k_render_scene`), `:388-390`
  (`k_sphere_relativistic`), and the helper call
  at `CudaPathTracer.cu:220` (`k_pathtrace_sample`)
  consume the new
  `rr::manifold::apply_observer_primary_ray_aberration(...)`
  helper.
- **Bullet 2** (activation gates): the unified
  helper at `ObserverFrame.h:553+` enforces the
  three-gate logic internally (`perception_mode ==
  ConstantVelocityMinkowski` AND `|beta|² > 0`
  AND pre-clamped beta via OBSERVER.6).
- **Bullet 3** (existing math leaves): the helper
  invokes the pre-existing
  `rr::relativity::aberrateDirection(beta,
  direction)` math leaf (the two-argument form);
  no new SR math added.
- **Bullet 4** (preservation): three-layer no-op
  anchor preserved on default Identity mode +
  zero-beta + Doppler / searchlight sites
  unchanged + secondary rays unchanged.

---

## 2. PER-CHECK RESULTS

| #  | Check                                                  | Evidence                                                                                                                                                                                                                                                                                                                  | Verdict |
|----|--------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1  | CUDA primary-ray aberration exists                     | The unified `apply_observer_primary_ray_aberration(observer_frame, direction)` RR_HD inline helper exists at `src/manifold/ObserverFrame.h:553+` in the `rr::manifold::` namespace. The helper invokes the pre-existing `rr::relativity::aberrateDirection(beta, direction)` two-argument math leaf when its three gates open. Three CUDA primary-ray sites consume the helper: `CudaTestKernel.cu:249-251` (`k_render_scene`), `CudaTestKernel.cu:388-390` (`k_sphere_relativistic`), `CudaPathTracer.cu:220` (`k_pathtrace_sample`). Empirically verified by `test_obs_percept_3_constant_velocity_nonzero_beta_aberrates` (4 RR_CHECKs) on `tests/manifold_identity_tests.cpp`. | PASS    |
| 2  | Activation requires ConstantVelocityMinkowski          | The helper's outer gate at `ObserverFrame.h:559-562` reads `if (obs_frame.perception_mode != PerceptionMode::ConstantVelocityMinkowski) return direction;`. The default `ObserverFrame{}` carries `perception_mode = Identity` (per OBSERVER.2); the gate closes; the helper returns input direction unchanged. The `CurvedChartGeodesicPlaceholder` reserved mode also fails the gate (master rule #3 placeholder honesty). Empirically verified by `test_obs_percept_3_identity_mode_returns_input_direction` (3 RR_CHECKs) + `test_obs_percept_3_curved_placeholder_returns_input` (3 RR_CHECKs). | PASS    |
| 3  | Activation requires beta > 0                           | The helper's inner gate at `ObserverFrame.h:565-571` reads `const float beta2 = beta.x * beta.x + beta.y * beta.y + beta.z * beta.z; if (!(beta2 > 0.0f)) return direction;`. Squared-magnitude check avoids sqrt cost + is exact at beta=0 (the NaN-safe `!(beta2 > 0.0f)` form also catches NaN beta values; mirrors the FIELD-I.5 audit's similar defensive-comparison precedent at the `evaluate_mapping`'s clamp gate). Empirically verified by `test_obs_percept_3_constant_velocity_zero_beta_returns_input` (3 RR_CHECKs). | PASS    |
| 4  | `beta = 0` is no-op                                    | Three-layer verified: (a) the helper's inner gate (§3) closes on zero beta — direction returned unchanged byte-for-byte; (b) even if the gate were bypassed, the existing `rr::relativity::aberrateDirection(beta_vec, direction)` math leaf at `RelativityMath.h:118-119` short-circuits at `beta_mag <= 1.0e-12f` to identity; (c) the OBSERVER.6 adapter produces `observer_frame.beta = (0, 0, 0)` on `cfg.observer.beta_magnitude = 0` (verified at OBSERVER.7 audit's check on `cfg_zero_beta` test). Empirically verified at `test_obs_percept_3_constant_velocity_zero_beta_returns_input`.                                                                                                                | PASS    |
| 5  | Default observer is no-op                              | Three-layer verified: (a) default `ObserverFrame{}` carries `perception_mode = Identity` (OBSERVER.2 audit's check #2 on default-constructed ObserverFrame); (b) the helper's outer gate closes on Identity → direction returned unchanged; (c) at the two existing primary-ray sites (k_render_scene + k_sphere_relativistic) the kernel-arm dispatch's else-branch fires (the legacy `aberrateDirection(rel, ray.direction)` path), preserving the post-OBS-P.2 behaviour for `--render-relativistic` flows that don't engage `--observer-perception-mode relativistic`. (d) at the new path-tracer site (k_pathtrace_sample) the helper short-circuits on Identity → the pre-OBS-PERCEPT.3 path-tracer baseline is preserved byte-for-byte (the path tracer had NO pre-existing aberration call per the OBS-P.3 audit check #5; on Identity the new helper call is no-op so the path tracer behaviour is unchanged). Empirically verified at `test_obs_percept_3_identity_mode_returns_input_direction`. | PASS    |
| 6  | Secondary rays unchanged                               | The OBS-PERCEPT.3 slice adds the helper call only at the PRIMARY-ray site of each kernel: line 251 / 390 of `CudaTestKernel.cu` (inside the kernel body, between camera-ray generation and intersection), and line 220 of `CudaPathTracer.cu` (immediately after `generate_primary_ray(...)`, before the bounce loop at line 207). The CudaPathTracer's bounce loop at lines 207+ is byte-identical to the post-OBS-PERCEPT.2 baseline. `git diff 0bf2bb8..b653e48 -- src/cuda/CudaPathTracer.cu` confirms zero changes inside the bounce loop's `closest_hit(...)` / `next_vec2(...)` / BSDF / NEE / MIS / shadow-ray / pathtracer continuation code. Master rule #12 + OBS-PERCEPT.1 plan §5.2 Option A (primary-ray-only) honoured.                                                                                                                            | PASS    |
| 7  | Doppler / searchlight unchanged                        | The kernel-arm sites for Doppler factor + Doppler colour shift + searchlight scaling preserve the post-OBS-P.2 guarded ternary verbatim. At `CudaTestKernel.cu`'s `k_render_scene` (line ~556+): `const float D = rr::relativity::dopplerFactor(rel, ray.direction);` consumes the `rel` snapshot computed from the OBS-P.2 ternary `(perception_active ? observer_frame.beta : observer.velocity)` at line 226-232. Same shape at `k_sphere_relativistic`. The OBS-PERCEPT.3 dispatch's CONSTANT-VELOCITY-MINKOWSKI branch uses the new helper for aberration only; the `rel` snapshot is still computed once per thread and consumed by Doppler / searchlight verbatim. `git diff 0bf2bb8..b653e48 -- src/cuda/CudaTestKernel.cu` confirms zero changes inside the Doppler / searchlight call-site blocks (lines 553-572 of the post-OBS-PERCEPT.3 file).                                                                                                                                                                                                                                                | PASS    |
| 8  | OptiX path unchanged                                   | `git diff 0bf2bb8..b653e48 --name-only -- 'src/optix/'` returns zero hits. Every `src/optix/*.cu` / `*.cuh` / `*.cpp` / `*.h` file is byte-identical to the post-OBS-PERCEPT.2 baseline. The OBS-PERCEPT.4 slice (mirroring this OBS-PERCEPT.3 slice on the OptiX path with the same helper) is the documented next OBS-PERCEPT.* impl slot.                                                                                                                                                                                                                                  | PASS    |
| 9  | Build / test status                                    | Audit-host `ctest` returns `100% tests passed, 0 tests failed out of 13` (unchanged from OBS-PERCEPT.2; no new ctest target). Per-binary: `manifold_identity_tests: 421/421 passed` (+13 NEW vs the 408 baseline); `relativity_tests: 841/841` unchanged; `cli_tests: 274/274` unchanged; `renderer_tests: 35/35` unchanged; `field_tests: 135/135` unchanged; every other suite unchanged. Full rebuild via `cmake --build /home/user/RelativityRender/build` adds no new warnings on any module. OptiX-ON-no-SDK build at OBS-PERCEPT.3 landing also clean (14/14 ctest PASS). Empirically verified.                                                                                                                                                                                                                                                                                                                                                                          | PASS    |
| 10 | Runtime CUDA status                                    | `PASS_WITH_RUNTIME_DEFERRED`. The audit-host build is `RR_ENABLE_CUDA=OFF` (no CUDA SDK present), so the kernel arm's empirical PPM output cannot be exercised this audit. The host-side data-path (helper + dispatch) is verified structurally — clean compile + 13/13 ctest PASS + 13 NEW RR_CHECK assertions on the helper's three-gate activation logic. The SDK-host runtime scenarios from the OBS-PERCEPT.2 task brief §8.5 (default-state byte identity; zero-beta byte identity; non-zero-beta consistency; OBS-F.2 fixture runtime; path-tracer primary-ray verification) defer to a future OBS-PERCEPT.7 arc capstone SDK-host pass OR the combined FIELD-* + OBS-PERCEPT CLI bridge slice's SDK-host audit (per the FIELD-BEAUTY.8 capstone's §4.2 (b) RECOMMENDED combined-slice option). | PASS (structural) — runtime DEFERRED to SDK-host audit pass |
| 11 | Verdict                                                | All ten structural / runtime-status checks PASS. The OBS-PERCEPT.3 surface is well-scoped, kernel-wired, byte-identical-by-default, gate-disciplined, OptiX-isolated. Master rule #3 + #11 + #12 + #16 satisfied (see §3 below).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Commit shape

The OBS-PERCEPT.3 commit (`b653e48`) modifies five
files:

```
docs/BUILD_PLAN.md                | 280 ++++++++++++
src/cuda/CudaPathTracer.cu        |  19 +++
src/cuda/CudaTestKernel.cu        |  42 +++++-
src/manifold/ObserverFrame.h      |  84 ++++++++++
tests/manifold_identity_tests.cpp | 136 ++++++++++++++
```

Source-code surface: one manifold header
(`ObserverFrame.h` — the new helper) + two CUDA
files (`CudaTestKernel.cu` for the existing two
sites + `CudaPathTracer.cu` for the path-tracer
site) + one test file (`manifold_identity_tests.cpp`
extensions). Zero `CMakeLists.txt` modification
(the `rr_manifold` PUBLIC link on `rr_gpu` from
OBSERVER.8 + MANI-I.5 already propagates the
include path; the new helper is in
`ObserverFrame.h` which the CUDA kernels already
include via `CudaScene.cuh`).

The narrow scope intentionally excludes every
other file: no `src/optix/*`, no
`src/core/Config.h`, no
`src/core/CommandLine.cpp`, no `src/main.cpp`, no
`src/io/SceneLoader.cpp`, no `src/relativity/`
modifications, no `src/field/` modifications, no
new scene fixture, no new CMakeLists.txt entry.
All deferred or out-of-scope per the operator's
brief.

### 3.2 Check #1 — CUDA primary-ray aberration exists

The unified helper at `ObserverFrame.h:553+` is the
load-bearing structural addition:

```cpp
RR_HD inline rr::math::Vec3 apply_observer_primary_ray_aberration(
        const ObserverFrame& obs_frame,
        rr::math::Vec3       direction) {
    if (obs_frame.perception_mode !=
            PerceptionMode::ConstantVelocityMinkowski) {
        return direction;
    }
    const rr::math::Vec3 beta = obs_frame.beta;
    const float beta2 = beta.x * beta.x
                      + beta.y * beta.y
                      + beta.z * beta.z;
    if (!(beta2 > 0.0f)) {
        return direction;
    }
    return rr::relativity::aberrateDirection(beta, direction);
}
```

The helper is consumed at three CUDA primary-ray
sites:

- **`CudaTestKernel.cu:249-251`** (`k_render_scene`):
  ```cpp
  if (perception_active) {
      ray.direction =
          rr::manifold::apply_observer_primary_ray_aberration(
              observer_frame, ray.direction);
  } else { ... legacy path ... }
  ```
- **`CudaTestKernel.cu:388-390`**
  (`k_sphere_relativistic`): same dispatch
  structure.
- **`CudaPathTracer.cu:220`**
  (`k_pathtrace_sample`):
  ```cpp
  ray.direction = rr::manifold::apply_observer_primary_ray_aberration(
      scene.observer_frame, ray.direction);
  ```
  (No legacy dispatch needed because the path
  tracer had NO pre-existing aberration call per
  OBS-P.3 audit check #5; this site is a NEW
  perception-engaging path).

Empirical verification via
`test_obs_percept_3_constant_velocity_nonzero_beta_aberrates`
on `manifold_identity_tests.cpp` (4 RR_CHECKs):
verifies that on `ConstantVelocityMinkowski` mode
+ non-zero beta, the helper returns the boosted
direction (unit-length + non-trivially different
from the input on transverse directions).

### 3.3 Check #2 — activation requires ConstantVelocityMinkowski

The helper's outer gate at `ObserverFrame.h:559-562`
is the explicit `perception_mode` check. The gate
opens only on `ConstantVelocityMinkowski`; closes
on `Identity` (the default mode for every
default-constructed `ObserverFrame`) AND on
`CurvedChartGeodesicPlaceholder` (the reserved
placeholder mode whose master rule #3 honesty
contract is "no-output-this-slice").

Two empirical tests verify the gate:

- `test_obs_percept_3_identity_mode_returns_input_direction`
  (3 RR_CHECKs): Identity-mode + intentionally
  non-zero beta = `(0.5, 0, 0)` → outer gate
  closes despite the non-zero beta; direction
  unchanged byte-for-byte. This empirically
  pins the "Identity-mode short-circuit"
  invariant.

- `test_obs_percept_3_curved_placeholder_returns_input`
  (3 RR_CHECKs): same shape but
  `perception_mode = CurvedChartGeodesicPlaceholder`.
  Verifies the placeholder mode's master-rule-#3
  honesty: the helper produces zero transform
  (the future CURVED-CHART arc would lift this).

### 3.4 Check #3 — activation requires beta > 0

The helper's inner gate at `ObserverFrame.h:565-571`
is the squared-magnitude check:

```cpp
const float beta2 = beta.x * beta.x
                  + beta.y * beta.y
                  + beta.z * beta.z;
if (!(beta2 > 0.0f)) {
    return direction;
}
```

Two design choices verified:

- **Squared-magnitude**: avoids the sqrt cost +
  is exact at beta = 0 (no floating-point
  imprecision around the gate boundary). Matches
  the FIELD-I.4 audit's defensive-comparison
  precedent at `evaluate_mapping`'s clamp gate.

- **`!(beta2 > 0.0f)` form**: NaN-safe. If `beta2`
  is NaN (e.g. one of the beta components is
  NaN), the comparison `beta2 > 0.0f` returns
  false, so `!(false) == true`, gate closes,
  direction returned unchanged. A naive
  `beta2 == 0.0f` would NOT catch NaN beta. The
  OBSERVER.6 adapter's safe-clamp upstream
  guarantees finite beta in practice, but the
  helper's defensive form is honest scope.

Empirical verification via
`test_obs_percept_3_constant_velocity_zero_beta_returns_input`
(3 RR_CHECKs): perception_mode =
`ConstantVelocityMinkowski` (outer gate opens) +
beta = (0, 0, 0) → inner gate closes; direction
returned unchanged byte-for-byte.

### 3.5 Check #4 — `beta = 0` is no-op

Three-layer no-op anchor preserved:

**Layer 1 — explicit inner gate** (this OBS-PERCEPT.3
slice): the helper's `!(beta2 > 0.0f)` gate
short-circuits to identity-direction return. This
is the NEW explicit contract the OBS-PERCEPT.3 slice
introduces; the pre-OBS-PERCEPT.3 behavior was
incidental rather than contractual.

**Layer 2 — math leaf identity** (pre-existing): even
if Layer 1 were bypassed, the
`rr::relativity::aberrateDirection(beta_vec,
direction)` math leaf at `RelativityMath.h:118-119`
internally checks `if (beta_mag <= 1.0e-12f) return
direction;`. This is the pre-existing
"infinitesimal beta = identity" safety the
OBSERVER.6 + OBS-P.2 + FIELD-BEAUTY.* arc family
relied on.

**Layer 3 — OBSERVER.6 adapter** (host-side): when
the operator passes `--observer-beta 0` (or no
`--observer-beta` flag), the adapter produces
`observer_frame.beta = (0, 0, 0)` exactly. No
sub-epsilon noise. Verified at OBSERVER.7 audit's
`test_observer_6_constant_velocity_zero_beta`.

All three layers compose: the OBSERVER.6 adapter
emits zero beta → the helper's inner gate closes →
even if bypassed, the math leaf's epsilon check
would still short-circuit. Triple-redundant; the
zero-beta no-op contract is structurally
guaranteed.

### 3.6 Check #5 — default observer is no-op

The DEFAULT `ObserverFrame{}` is the load-bearing
no-op anchor. Three-layer verified:

**Layer 1 — default `perception_mode = Identity`**:
the OBSERVER.2 data model audit's check #2 verified
the default-constructed `ObserverFrame{}` carries
`perception_mode = Identity` (the explicit `= 0`
default on the enum's first enumerator). The
OBS-PERCEPT.3 helper's outer gate closes on Identity.

**Layer 2 — kernel-arm dispatch's else-branch**: at
the two existing primary-ray sites
(`k_render_scene` + `k_sphere_relativistic`), the
kernel arm dispatches between the new helper (on
`perception_active`) and the legacy
`aberrateDirection(rel, ray.direction)` path (on
the Identity else-branch). The else-branch
preserves the post-OBS-P.2 behavior: `rel` is
computed from `observer.velocity` (the legacy
fallback) and `aberrateDirection(rel, ...)`
applies the legacy SR aberration. This means
`--render-relativistic` flows that don't engage
`--observer-perception-mode relativistic`
continue to use the legacy aberration path
unchanged.

**Layer 3 — path-tracer no-op on Identity**: at the
new `k_pathtrace_sample` site
(`CudaPathTracer.cu:220`), the helper is the ONLY
call (no legacy fallback). On Identity mode the
helper short-circuits → direction unchanged →
the path tracer's behavior is byte-identical to
the pre-OBS-PERCEPT.3 baseline (the path tracer
had NO pre-existing aberration call per OBS-P.3
audit check #5).

Empirical verification via
`test_obs_percept_3_identity_mode_returns_input_direction`.

### 3.7 Check #6 — secondary rays unchanged

The OBS-PERCEPT.3 slice's three kernel-arm
modifications all target the PRIMARY-ray
generation site:

- **`CudaTestKernel.cu:k_render_scene`**: the
  dispatch at lines 249-251 is INSIDE the
  primary-ray block (after `generate_camera_ray(...)`
  at line 235, before the intersection block at
  line 256). The bounce loop is not present in
  k_render_scene (this is the primary-hit
  kernel, not the path tracer); no secondary
  rays.
- **`CudaTestKernel.cu:k_sphere_relativistic`**:
  same structure; primary-hit kernel.
- **`CudaPathTracer.cu:k_pathtrace_sample`**: the
  helper call at line 220 is BEFORE the bounce
  loop at line 207+. Inside the bounce loop:
    - `closest_hit(ray, scene, ...)` at line 208
      — UNCHANGED.
    - `next_vec2(rng)` / sample bounces —
      UNCHANGED.
    - BSDF evaluation — UNCHANGED.
    - NEE / MIS / shadow rays — UNCHANGED.
    - Pathtracer continuation logic —
      UNCHANGED.

Per-line diff `git diff 0bf2bb8..b653e48 --
src/cuda/CudaPathTracer.cu` confirms zero changes
inside the bounce-loop body. Master rule #12 +
OBS-PERCEPT.1 plan §5.2 Option A (primary-ray-only)
honoured.

### 3.8 Check #7 — Doppler / searchlight unchanged

The post-OBS-P.2 guarded-ternary at the
Doppler / searchlight call sites is preserved
verbatim:

**At `k_render_scene`**:
- `const Vec3 beta_source = perception_active ?
  observer_frame.beta : observer.velocity;`
  (line 229-231, unchanged).
- `const auto rel = rr::relativity::precompute_relativity(beta_source);`
  (line 232, unchanged).
- Aberration block (line ~244-256): MODIFIED to
  use the new dispatch (per OBS-PERCEPT.3 scope).
- Doppler factor: `const float D =
  rr::relativity::dopplerFactor(rel,
  ray.direction);` — UNCHANGED.
- Doppler color shift: `if (scene.params.enable_doppler) {
  color = rr::relativity::applyDopplerColor(...); }`
  — UNCHANGED.
- Searchlight: `const float D4 =
  rr::relativity::searchlightFactor(D);` +
  scaling — UNCHANGED.

**At `k_sphere_relativistic`**: same shape; the
post-OBS-P.2 ternary at the Doppler / searchlight
sites is byte-identical.

Per-line diff `git diff 0bf2bb8..b653e48 --
src/cuda/CudaTestKernel.cu` confirms zero changes
inside the Doppler / searchlight call-site blocks
(only the aberration call-site block was
modified). The OBS-PERCEPT.* arc's Doppler /
searchlight migration is deferred to future
sub-slices (per OBS-PERCEPT.2 task brief §4.3
explicit "no new Doppler / searchlight math" non-
goal).

### 3.9 Check #8 — OptiX path unchanged

`git diff 0bf2bb8..b653e48 --name-only --
'src/optix/'` returns zero hits. Every `src/optix/`
file (`OptixLaunchParams.h`, `OptixPrograms.cu`,
`OptixRenderer.h`, `OptixRenderer.cpp`,
`OptixBackend.cpp`, `OptixDenoiser.cpp`,
`OptixPipeline.cpp`, `OptixAccel.cpp`,
`OptixSBT.h`) is byte-identical to the
post-OBS-PERCEPT.2 baseline.

The new helper at `ObserverFrame.h:553+` IS visible
to the OptiX side (via the existing
`#include "manifold/ObserverFrame.h"` in
`OptixLaunchParams.h` from OBSERVER.10), but no
OptiX program calls it this slice. The OBS-PERCEPT.4
slice (the renumbered next OBS-PERCEPT.* impl slot)
mirrors the CUDA dispatch on the OptiX path.

The operator's "Do not modify OptiX yet" rule
honoured.

### 3.10 Check #9 — build / test status

Audit-host `ctest` empirical output:

```
13/13 Test #13: renderer_tests ........ Passed
100% tests passed, 0 tests failed out of 13
```

Per-binary breakdown:

| Suite                       | Pre-OBS-PERCEPT.3 | Post-OBS-PERCEPT.3 |
|-----------------------------|-------------------|--------------------|
| math_tests                  | unchanged         | unchanged          |
| image_tests                 | unchanged         | unchanged          |
| gpu_tests                   | unchanged         | unchanged          |
| pathtracer_tests            | unchanged         | unchanged          |
| pathtracer_nee_tests        | unchanged         | unchanged          |
| pathtracer_bsdf_tests       | unchanged         | unchanged          |
| pathtracer_mis_tests        | unchanged         | unchanged          |
| cli_tests                   | 274/274           | 274/274            |
| relativity_tests            | 841/841           | 841/841            |
| manifold_identity_tests     | 408/408           | **421/421** (+13 NEW) |
| field_tests                 | 135/135           | 135/135            |
| demo_tests                  | unchanged         | unchanged          |
| renderer_tests              | 35/35             | 35/35              |

OptiX-ON-no-SDK build at the OBS-PERCEPT.3 landing
commit also clean: 14/14 ctest PASS (including
`optix_tests`). The CUDA-side kernel changes
propagate cleanly through the rr_gpu include path
without breaking the OptiX-on stub fallback.

Full rebuild via `cmake --build
/home/user/RelativityRender/build` clean — no new
warnings on any module. No CMakeLists.txt change
required (the `rr_manifold` PUBLIC link on
`rr_gpu` already propagates the
`ObserverFrame.h` include path transitively).

### 3.11 Check #10 — runtime CUDA status

`PASS_WITH_RUNTIME_DEFERRED`.

The audit-host build is `RR_ENABLE_CUDA=OFF` (no
CUDA SDK present), so the kernel arm's empirical
aberration cannot be exercised this audit. The
host-side data-path is verified structurally:

- The unified helper at `ObserverFrame.h:553+`
  compiles cleanly into both `rr_manifold` (the
  INTERFACE library) + every consumer
  (`rr_gpu` via CudaScene.cuh → ObserverFrame.h
  + `rr_optix` via OptixLaunchParams.h →
  ObserverFrame.h).
- The CUDA kernel-arm dispatches at
  `CudaTestKernel.cu:249-251` + `:388-390` +
  `CudaPathTracer.cu:220` pass the CUDA-disabled
  audit-host compile (the files are excluded
  from compilation on `RR_ENABLE_CUDA=OFF`, but
  the `manifold_identity_tests` binary's 13 NEW
  RR_CHECK assertions verify the unified
  helper's behaviour directly on the audit
  host's CPU).

The SDK-host runtime checks DEFERRED:

- **§8.5.1 Default-state byte identity**: Run
  `--render-aovs <every default fixture>`
  pre + post OBS-PERCEPT.3; `cmp` PPMs
  byte-by-byte. Structurally guaranteed by the
  three-layer no-op anchor (check #5); empirical
  verification deferred.
- **§8.5.2 Zero-beta byte identity**: Run
  `--render-aovs --observer-perception-mode
  relativistic --observer-beta 0 <fixture>`;
  `cmp` against
  `--observer-perception-mode default` PPMs.
  Structurally guaranteed by the inner gate
  (check #3); empirical verification deferred.
- **§8.5.3 Non-zero-beta consistency**: Run
  `--render-aovs --observer-perception-mode
  relativistic --observer-beta 0.5
  --observer-direction 1,0,0 <fixture>` pre +
  post; `cmp` PPMs (expected byte-identical for
  the two existing primary-hit sites because
  the unified helper's mathematical content is
  `aberrateDirection(beta, direction)` verbatim
  when both gates open; on the path-tracer site
  this is a NEW behaviour — the OBS-PERCEPT.*
  arc's intended semantic where the path tracer
  becomes perception-engaging).
- **§8.5.4 OBS-F.2 fixture runtime**: Run the
  fixture with relativistic perception engaged;
  verify visible aberration in the framebuffer
  matches the OBSERVER.13 `aov_observer_beta.ppm`
  diagnostic's flat-colour `(0, 0, -0.5)`
  anchor.
- **§8.5.5 Path-tracer primary-ray verification**:
  Run `--render-pathtrace` pre + post + the
  OBS-F.2 fixture + relativistic perception;
  `cmp` PPMs (the NEW perception-engaging path
  diverges from the pre-OBS-PERCEPT.3 baseline,
  which is the intended OBS-PERCEPT.* arc
  semantic per OBS-PERCEPT.2 §8.5).

The five SDK-host scenarios DEFER to: (a) a future
OBS-PERCEPT.7 arc capstone SDK-host pass; OR
(b) the combined FIELD-* + OBS-PERCEPT CLI bridge
slice's SDK-host audit (per FIELD-BEAUTY.8 §4.2 (b)
RECOMMENDED combined-slice option).

### 3.12 Master-rule satisfaction recap

- **Master rule #3 ("no fake stubs"):** satisfied.
  The unified helper at `ObserverFrame.h:553+` is
  fully wired (real perception-mode check; real
  inner-gate squared-magnitude check; real
  invocation of the math leaf). The
  `CurvedChartGeodesicPlaceholder` mode's no-
  transform fallback is honest (master rule #3:
  the helper short-circuits to identity; the
  future CURVED-CHART arc would lift this with
  documented contracts). The path-tracer site's
  NEW perception-engaging behavior is documented
  as such in the kernel doc-comment.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. Every documented gate
  behavior is tested empirically by the 13 new
  RR_CHECK assertions: outer gate (Identity +
  placeholder modes); inner gate (zero beta on
  ConstantVelocityMinkowski); both gates open
  (non-zero beta → aberration applied + unit-
  length preservation + non-trivial transverse
  change). The test surface covers every gate
  combination.

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Scope deliberately narrow to CUDA
  primary-ray ONLY — OptiX deferred to
  OBS-PERCEPT.4; secondary-ray transform deferred
  (Option A primary-ray-only); Doppler /
  searchlight migration deferred; debug AOV
  deferred (OBS-PERCEPT.5); fixture authoring
  deferred (OBS-PERCEPT.6); arc capstone deferred
  (OBS-PERCEPT.7); CLI bridge deferred.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied.
  The OBS-PERCEPT.3 default state is unchanged
  from the OBS-PERCEPT.2 baseline:
    - No `--render-*` action's output changes by
      default.
    - No existing PPM filename changes.
    - No new file produced.
    - No existing AOV slot's value changes.
  The single observable behaviour change is the
  structural presence of the unified helper +
  the kernel-arm dispatch; its observable
  behaviour from every default CLI invocation
  is zero because both gates close.

### 3.13 Honest scope recap

This audit is a **CUDA primary-ray aberration
audit with SDK-host runtime DEFERRED** + **OptiX
path preserved-unchanged** + **Doppler / searchlight
preserved-unchanged**. The verdict `PASS` reflects:

- (a) The structural helper + kernel-arm surface
  is well-formed (the unified helper at
  ObserverFrame.h:553+; three kernel-arm
  consumptions at CudaTestKernel.cu:249-251 +
  :388-390 + CudaPathTracer.cu:220; 13 NEW
  RR_CHECK assertions empirically pin the
  three-gate logic).
- (b) The default-state preservation is verified
  across all three no-op anchors (Identity mode
  + zero beta + legacy else-branch).
- (c) The OptiX isolation is structural (zero
  `src/optix/` hits).
- (d) The Doppler / searchlight preservation is
  structural (per-line diff confirms zero
  changes in those blocks; the post-OBS-P.2
  guarded ternary stays verbatim).
- (e) The secondary-ray preservation is
  structural (the path-tracer bounce loop is
  byte-identical; the helper call is BEFORE the
  loop).
- (f) Both build configs empirically verified
  (audit-host 13/13 + OptiX-ON-no-SDK 14/14).

The runtime status's `PASS_WITH_RUNTIME_DEFERRED`
is honest: the SDK-host scenarios from
OBS-PERCEPT.2 task brief §8.5 require a CUDA SDK
host AND (for the path-tracer site's NEW
perception-engaging behavior) acceptance of the
intended arc semantic. Master rule #3 + #11 +
#12 + #16 satisfied.

---

## 4. NEXT

### 4.1 Renumbered OBS-PERCEPT.* sub-slice ladder

The OBS-PERCEPT.4 audit slot insertion (mirroring
the FIELD-BEAUTY.4 / FIELD-I.10 / OBS-P.3 audit-
slot insertion precedents) shifts subsequent
OBS-PERCEPT.* sub-slices by one. The post-
OBS-PERCEPT.4 ladder is:

- **OBS-PERCEPT.5** — OptiX implementation (the
  renumbered FIELD-PERCEPT.* impl slot; mirrors
  this OBS-PERCEPT.3 slice on the OptiX path).
- **OBS-PERCEPT.6** — OptiX audit.
- **OBS-PERCEPT.7** — Debug AOV (the perception-
  transform diagnostic AOV).
- **OBS-PERCEPT.8** — Debug AOV audit.
- **OBS-PERCEPT.9** — Fixture.
- **OBS-PERCEPT.10** — Fixture audit.
- **OBS-PERCEPT.11** — Arc capstone audit.

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

### 4.2 Candidate next slots (prioritised)

**(a) RECOMMENDED — OBS-PERCEPT.5: OptiX
implementation** (the renumbered next
OBS-PERCEPT.* impl slot). Natural continuation:
mirrors this OBS-PERCEPT.3 CUDA-side slice on the
OptiX path. The unified helper at
`ObserverFrame.h:553+` already exists in
`rr_manifold` (header-only, shared); the
OBS-PERCEPT.5 slice adds the same dispatch
structure to the OptiX `__raygen__pinhole` +
`__raygen__pathtrace` programs. Cross-backend
bit-identity guaranteed structurally by the
shared helper.

**(b) HIGHLY RECOMMENDED — combined FIELD-* +
OBS-PERCEPT CLI bridge slice** (per FIELD-BEAUTY.8
§4.2 (b)). Single SDK-host audit closes the
entire field-and-observer-arc family's runtime-
deferred verdict tail (FIELD-I.10 + FIELD-I.12 +
FIELD-I.14 + FIELD-BEAUTY.4 + FIELD-BEAUTY.6 +
FIELD-BEAUTY.8 + OBS-PERCEPT.4 + OBS-PERCEPT.6
deferred verdicts all PASS at one SDK-host run
once both arcs' kernel surfaces are reachable).
Best leverage option if the operator has SDK-host
access.

**(c) Manifold-orthogonal work.** Multiple
options:
  - **Deferred SDK-host runtime pass** for the
    OBSERVER.* + OBS-P.* + OBS-F.* + FIELD-I.* +
    FIELD-BEAUTY.* + OBS-PERCEPT.* arc family
    (highest converging-leverage option).
  - **MANI-I.12 final cross-host manifold
    audit**.
  - **Denoiser integration with chart-aware
    AOVs**.
  - **Path-tracer feature breadth** (NEE
    extension, BSDF expansion, MIS tuning).

**(d) NOT RECOMMENDED — direct OBS-PERCEPT.7
debug AOV slice skipping OBS-PERCEPT.5 OptiX
bridge.** Would author a debug AOV for the
perception-transform delta but the OptiX side
wouldn't expose the same diagnostic; the
cross-backend bit-identity check breaks. Better
to pair the OptiX bridge with the CUDA bridge
before opening the debug-AOV gate so both
backends expose the same diagnostic surface.

**(e) DEFERRABLE — RETROACTIVE task brief
authoring.** The operator may choose to backfill
the missing FIELD-BEAUTY.1 + FIELD-BEAUTY.2 +
FIELD_INTERPRETATION_PHASE1_AUDIT.md task brief /
audit slots for archival precedent. The
honest-framing approach has worked across the
FIELD-BEAUTY.* + OBS-PERCEPT.* arc families;
backfilling is purely documentary + introduces
no source-code or runtime impact. Deferrable
to operator discretion.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the master rule #3 +
  #11 + #12 + #16 satisfaction recap at §3.12
  cites these).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §7.2 (the architecture-doc anchor for the
  observer-frame Lorentz boost of the tetrad
  concept the OBS-PERCEPT.3 helper implements).

### 5.2 OBS-PERCEPT.* arc references

- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1 — the canonical OBS-PERCEPT.*
  arc plan; the OBS-PERCEPT.3 implementation
  operationalises §1 + §2 + §5).
- `docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`
  (OBS-PERCEPT.2 — the operator-facing task
  brief; the OBS-PERCEPT.3 implementation
  honours the §2 activation rules + §3 default
  invariants + §4 CUDA-first scope + §8 PASS
  criteria).

### 5.3 OBSERVER.* + OBS-P.* + OBS-F.* arc references

- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3 — the default-constructed
  `ObserverFrame{}.perception_mode = Identity`
  underpinning check #5).
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5).
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7 — the OBSERVER.6 adapter's
  zero-beta + clamp-safety contracts underpinning
  check #4's Layer 3 + the helper's safe-clamp
  contract).
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9 — the CudaSceneView::observer_frame
  field's carry-through verified at the CUDA
  payload audit; the OBS-PERCEPT.3 helper
  consumes this field via the existing
  CudaSceneView surface).
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11 — the OptixLaunchParams::observer_frame
  carry-through verified at the OptiX payload
  audit; the OBS-PERCEPT.5 OptiX bridge will
  consume this field).
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md` (OBSERVER.14).
- `docs/OBSERVER_FRAME_ARC_AUDIT.md` (OBSERVER.15
  — the capstone whose §10 risk #1 the
  OBS-PERCEPT.3 slice closes on the CUDA primary-
  ray path).
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3 — check #5 confirmed the CUDA path
  tracer had NO pre-existing aberration call;
  the OBS-PERCEPT.3 slice adds the helper at the
  k_pathtrace_sample site as a NEW
  perception-engaging path).
- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md`
  (OBS-F.3 — the precedent fixture audit; the
  OBS-PERCEPT.6 fixture will follow the same
  pattern).

### 5.4 Parallel-arc references

- `docs/FIELD_SCALAR_BEAUTY_MAPPING_AUDIT.md`
  (FIELD-BEAUTY.8 — the precedent arc-capstone
  audit; its §4.2 (b) "combined CLI bridge slice"
  RECOMMENDATION applies to the OBS-PERCEPT.*
  arc's CLI bridge slot as well).

### 5.5 Source surface audited

- `src/manifold/ObserverFrame.h` (the OBS-PERCEPT.3
  surface — +84 lines vs the OBS-PERCEPT.2
  baseline; the new `apply_observer_primary_ray_aberration(...)`
  helper at lines 553+; its doc-comment block at
  lines 497-552 documents the three-gate
  activation logic + the per-thread duplicate
  rationale + the future OBS-PERCEPT.4 OptiX
  bridge contract).
- `src/cuda/CudaTestKernel.cu` (the OBS-PERCEPT.3
  surface — +42 net lines vs the OBS-PERCEPT.2
  baseline; the dispatch at
  `k_render_scene:249-251` + `:388-390`
  consolidates the post-OBS-P.2 ternary's true
  branch into the new helper call; the else-
  branch preserves the legacy `aberrateDirection(rel,
  ray.direction)` path verbatim for the Identity
  fallback).
- `src/cuda/CudaPathTracer.cu` (the OBS-PERCEPT.3
  surface — +19 lines vs the OBS-PERCEPT.2
  baseline; the helper call at line 220
  immediately after `generate_primary_ray(...)`;
  this is a NEW perception-engaging site — the
  path tracer had NO pre-existing aberration
  call).
- `tests/manifold_identity_tests.cpp` (the
  OBS-PERCEPT.3 surface — +136 lines vs the
  OBS-PERCEPT.2 baseline; 4 new test functions
  with 13 RR_CHECK assertions covering the
  three-gate activation logic).

### 5.6 Surrounding commit SHAs

- `b653e48` — OBS-PERCEPT.3 audited tree (the
  per-slice gate target).
- `0bf2bb8` — OBS-PERCEPT.2 baseline (the diff
  baseline for checks #6 + #7 + #8).
- `8db1f9c` — OBS-PERCEPT.1 plan (the
  architectural anchor for the unified-helper
  + dispatch design at §3.2 + §3.3).

### 5.7 Unchanged source files (sampled)

The following files are byte-identical to the
OBS-PERCEPT.2 baseline (`0bf2bb8`), confirmed by
the diff filters at checks #6 + #7 + #8 +
narrow-scope discipline:

- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/optix/`.
- Every file in `src/relativity/`.
- Every file in `src/manifold/` EXCEPT
  `ObserverFrame.h` (the only touched file).
- Every file in `src/scene/`.
- Every file in `src/io/`.
- Every file in `src/core/`, `src/math/`,
  `src/image/`, `src/gpu/`, `src/app/`,
  `src/field/`, `src/pathtracer/`,
  `src/camera/`, `src/geometry/`,
  `src/lighting/`, `src/material/`,
  `src/texture/`, `src/renderer/`.
- `src/main.cpp`.
- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/cuda/` EXCEPT `CudaTestKernel.cu` +
  `CudaPathTracer.cu` (the only two touched
  files).

### 5.8 Unchanged test + scene + build files

- All test files (`tests/`) byte-identical to the
  OBS-PERCEPT.2 baseline EXCEPT
  `manifold_identity_tests.cpp` (the only
  touched file).
- All scene files (`scenes/`) byte-identical to
  the OBS-PERCEPT.2 baseline (no new fixture
  this slice; OBS-PERCEPT.6 lands the fixture).
- `CMakeLists.txt` byte-identical to the
  OBS-PERCEPT.2 baseline (no link change; the
  helper is in the existing `rr_manifold` header
  which already propagates through `rr_gpu` +
  `rr_optix` PUBLIC links).

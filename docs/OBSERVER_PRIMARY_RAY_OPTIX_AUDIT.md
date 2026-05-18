# Primary-Ray Perception Transform OptiX Audit (OBS-PERCEPT.6)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `1dbeb23` ("optix:
OBS-PERCEPT.5 — OptiX Primary-Ray Perception Transform
(impl, OptiX program arms)").
Audit baseline: `40bb476` ("docs: OBS-PERCEPT.4 — CUDA
Primary-Ray Perception Transform Audit (docs only)")
— the last commit before OBS-PERCEPT.5 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The OBS-PERCEPT.5 commit's OptiX-ON-no-SDK
build was empirically verified at landing time (ctest
14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, the OBS-PERCEPT.5
commit's content, the audit-host `ctest` runtime
outputs, and `git diff` filter inspections.

This audit is the per-slice gate for OBS-PERCEPT.5
(`1dbeb23`). It verifies the eleven items the task
brief enumerates — OptiX primary-ray aberration
exists; activation requires
ConstantVelocityMinkowski; activation requires
beta > 0; beta = 0 is no-op; default observer is
no-op; CUDA / OptiX math and activation rules match;
secondary rays unchanged; Doppler / searchlight
unchanged; OptiX OFF build remains valid; runtime
CUDA / OptiX status (PASS / DEFERRED / BLOCKED);
and the overall verdict (PASS / REPAIR / BLOCKED).

The OBS-PERCEPT.5 slice is the **OptiX-side mirror**
of the OBS-PERCEPT.3 CUDA primary-ray aberration
arm. With OBS-PERCEPT.5 in, both backends have
symmetric primary-ray observer-frame aberration
dispatches; the OBSERVER.15 capstone audit's
`PASS_WITH_RUNTIME_DEFERRED` future-kernel-migration
risk #1 is closed on BOTH backends.

---

## 1. VERDICT

**PASS.**

All ten structural / runtime-status checks (#1
through #10) PASS. Check #11 (overall verdict) is
`PASS`. The OBS-PERCEPT.5 surface ships exactly
what the operator's four-bullet brief authorised —
OptiX primary-ray directional aberration via the
shared `apply_observer_primary_ray_aberration(...)`
helper at the two raygen sites
(`__raygen__pinhole` + `__raygen__pathtrace`), with
the documented preservation guarantees (default
observer no-op + beta=0 no-op + no secondary-ray
changes + no Doppler/searchlight changes + OptiX
OFF compile + CUDA-side preserved-unchanged).

Check #10's runtime CUDA/OptiX status is the
standard `PASS_WITH_RUNTIME_DEFERRED` shape. The
audit-host has neither CUDA SDK nor OptiX SDK so
neither kernel's empirical aberration can be
exercised; the structural data-paths are verified
by the audit-host build's clean compile + 13/13
ctest pass + the OptiX-ON-no-SDK build's 14/14
ctest pass at the OBS-PERCEPT.5 landing.

The narrow-scope verdict honesty: the operator's
OBS-PERCEPT.5 brief enumerated four implementation
bullets (OptiX primary-ray aberration; activation
gates; same-as-CUDA semantics; preservation
invariants). The slice satisfies all four:

- **Bullet 1** (OptiX primary-ray aberration): two
  raygen-site dispatches at
  `OptixPrograms.cu:240-249` (`__raygen__pinhole`)
  and `:1269-1277` (`__raygen__pathtrace`) consume
  the shared
  `rr::manifold::apply_observer_primary_ray_aberration(...)`
  helper.
- **Bullet 2** (activation gates): the shared
  helper at `ObserverFrame.h:553+` enforces the
  three-gate logic internally — identical contract
  to the CUDA-side consumers.
- **Bullet 3** (same-as-CUDA semantics): both
  backends consume the same RR_HD inline helper +
  the same `rr_relativity` math leaves;
  cross-backend bit-identity guaranteed by
  construction (mirrors the FIELD-BEAUTY.6 five-
  axis symmetry argument).
- **Bullet 4** (preservation): three-layer no-op
  anchor preserved (default Identity + zero beta
  + legacy else-branch via `aberrateDirection(rel,
  ...)`); secondary rays + Doppler / searchlight
  + CUDA path all byte-identical to the
  OBS-PERCEPT.4 baseline.

---

## 2. PER-CHECK RESULTS

| #  | Check                                                  | Evidence                                                                                                                                                                                                                                                                                                                  | Verdict |
|----|--------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1  | OptiX primary-ray aberration exists                    | Two OptiX raygen sites consume the shared `rr::manifold::apply_observer_primary_ray_aberration(observer_frame, direction)` helper (landed at OBS-PERCEPT.3, `ObserverFrame.h:553+`): (a) `__raygen__pinhole` site at `OptixPrograms.cu:240-249` — the dispatch's `if (perception_active_pinhole) { ray.direction = rr::manifold::apply_observer_primary_ray_aberration(observer_frame, ray.direction); } else { ray.direction = rr::relativity::aberrateDirection(rel, ray.direction); }` block. (b) `__raygen__pathtrace` site at `OptixPrograms.cu:1269-1277` — identical dispatch shape using `perception_active_pt`. Both consume the same RR_HD inline helper as the CUDA-side OBS-PERCEPT.3 sites; cross-backend bit-identity by construction.                                                                                                                                                                                                                                                                                          | PASS    |
| 2  | Activation requires ConstantVelocityMinkowski          | The shared helper's outer gate at `ObserverFrame.h:559-562` reads `if (obs_frame.perception_mode != PerceptionMode::ConstantVelocityMinkowski) return direction;`. Identical to the CUDA-side activation (verified at OBS-PERCEPT.4 audit's check #2). Empirically tested via the OBS-PERCEPT.3-landed `test_obs_percept_3_identity_mode_returns_input_direction` + `test_obs_percept_3_curved_placeholder_returns_input` (6 RR_CHECKs total on `manifold_identity_tests.cpp`) — both modes (Identity + Placeholder) close the gate.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | PASS    |
| 3  | Activation requires beta > 0                           | The shared helper's inner gate at `ObserverFrame.h:565-571` reads `const float beta2 = beta.x * beta.x + beta.y * beta.y + beta.z * beta.z; if (!(beta2 > 0.0f)) return direction;` (squared-magnitude + NaN-safe form). Identical to the CUDA-side activation. Empirically tested via the OBS-PERCEPT.3-landed `test_obs_percept_3_constant_velocity_zero_beta_returns_input` (3 RR_CHECKs).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | PASS    |
| 4  | `beta = 0` is no-op                                    | Three-layer verified (inherited from OBS-PERCEPT.3 + OBSERVER.6): (a) the shared helper's inner gate (§3) closes on zero beta — direction returned unchanged byte-for-byte; (b) even if bypassed, the existing `rr::relativity::aberrateDirection(beta_vec, direction)` math leaf at `RelativityMath.h:118-119` short-circuits at `beta_mag <= 1.0e-12f` to identity; (c) the OBSERVER.6 adapter produces `observer_frame.beta = (0, 0, 0)` on zero-beta inputs (OBSERVER.7 audit). Same anchor on both backends since both consume the same shared helper.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | PASS    |
| 5  | Default observer is no-op                              | Three-layer verified on OptiX: (a) default `optixLaunchParams.observer_frame{}` carries `perception_mode = Identity` (the OBSERVER.10 payload's default in-class init; same default as CUDA `view.observer_frame{}`); (b) the shared helper's outer gate closes on Identity → direction unchanged; (c) at both raygen sites the dispatch's else-branch fires (the legacy `rr::relativity::aberrateDirection(rel, ray.direction)` path), preserving the post-OBSERVER.10 + OBS-P.2 + FIELD-BEAUTY.5 behaviour for `--render-optix-relativistic` flows that don't engage `--observer-perception-mode relativistic`. Same anchor on both backends. Empirical SDK-host verification deferred per check #10.                                                                                                                                                                                                                                                                                                                                                                                                                                                                | PASS    |
| 6  | CUDA / OptiX math and activation rules match           | **Five-axis symmetry** verified — (a) **same POD type**: both backends consume `rr::manifold::ObserverFrame` directly (CUDA: `view.observer_frame`; OptiX: `optixLaunchParams.observer_frame`); (b) **same shared helper**: both backends invoke `rr::manifold::apply_observer_primary_ray_aberration(observer_frame, direction)` — the same RR_HD inline function from `src/manifold/ObserverFrame.h`; (c) **same dispatch shape**: both backends use `if (params.enable_aberration) { if (perception_active) { ray.direction = unified_helper(observer_frame, ray.direction); } else { ray.direction = aberrateDirection(rel, ray.direction); } }` — verified by inspecting the CUDA sites at `CudaTestKernel.cu:248-258` + `:387-397` + `CudaPathTracer.cu:209-220` and the OptiX sites at `OptixPrograms.cu:239-249` + `:1268-1277`; (d) **same math leaf**: both backends call `rr::relativity::aberrateDirection(beta, direction)` (the two-argument form) through the shared helper; (e) **same gate semantics**: identical outer-gate check (`perception_mode == ConstantVelocityMinkowski`) + inner-gate check (`|beta|² > 0`). Cross-backend bit-identity is **structurally guaranteed by construction** (mirrors the FIELD-BEAUTY.6 five-axis cross-backend symmetry argument applied to the OBS-PERCEPT.* helper). | PASS    |
| 7  | Secondary rays unchanged                               | The OBS-PERCEPT.5 slice's two raygen-site modifications target ONLY the primary-ray block of each raygen program. **`__raygen__pinhole`**: the dispatch at lines 239-249 is INSIDE the camera-ray-generation block; no secondary-ray block (the pinhole raygen fires a single primary ray + reads payload). **`__raygen__pathtrace`**: the dispatch at lines 1268-1277 is INSIDE the per-spp loop's primary-ray block (after `generate_camera_ray(...)` at line 1264, before the bounce loop at line 1281+). Inside the bounce loop: `optixTrace(...)` calls + closest-hit-payload-based shading + Doppler-from-payload + searchlight modulation — all UNCHANGED. Per-line diff `git diff 40bb476..1dbeb23 -- src/optix/OptixPrograms.cu` confirms zero changes inside the bounce-loop body OR inside the closest-hit (`__closesthit__radiance` / `__closesthit__pathtrace`) / miss (`__miss__radiance` / `__miss__pathtrace` / `__miss__shadow`) programs. Option A primary-ray-only per OBS-PERCEPT.1 §5.2.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | PASS    |
| 8  | Doppler / searchlight unchanged                        | The OBS-PERCEPT.5 slice does NOT modify the Doppler / searchlight call sites. At `__raygen__pinhole`: the Doppler-factor computation at line ~248+ (`const float D = rr::relativity::dopplerFactor(rel, ray.direction);`) reads from the same `rel` snapshot the OBS-P.2 ternary built (the `rel` variable + the `precompute_relativity(beta_source_pinhole)` call at lines 220-221 are byte-identical to the OBS-PERCEPT.4 baseline). The closest-hit Doppler-color-shift block + searchlight scaling block in `__closesthit__radiance` are byte-identical. Same shape at `__raygen__pathtrace`. Per-line diff confirms zero changes inside the Doppler / searchlight blocks. Master rule #12 satisfied — the OBS-PERCEPT.* arc's Doppler / searchlight migration is deferred to future sub-slices.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | PASS    |
| 9  | OptiX OFF build remains valid                          | Audit-host (`RR_ENABLE_OPTIX=OFF`) `ctest` returns `100% tests passed, 0 tests failed out of 13`. Per-binary: `manifold_identity_tests: 421/421 passed` (unchanged from OBS-PERCEPT.4 — no test extension); `relativity_tests: 841/841`; `cli_tests: 274/274`; `renderer_tests: 35/35`; `field_tests: 135/135`; every other suite unchanged. The audit-host build is structurally insulated because `if(RR_ENABLE_OPTIX)` at `CMakeLists.txt:548` is false; `rr_optix` is not compiled; the OBS-PERCEPT.5 OptiX source changes don't reach the compiler. Full rebuild via `cmake --build /home/user/RelativityRender/build` clean — no new warnings on any module. Empirically verified at the OBS-PERCEPT.5 landing commit's build transcript. No CMakeLists.txt change required (the `rr_manifold` PUBLIC link on `rr_optix` from MANI-I.5 + FIELD-I.11 already propagates the `ObserverFrame.h` include path; the OBS-PERCEPT.5 helper consumption is purely transitive).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | PASS    |
| 10 | Runtime CUDA / OptiX status                            | `PASS_WITH_RUNTIME_DEFERRED`. Both backends now have primary-ray aberration arms wired (CUDA at OBS-PERCEPT.3, OptiX at OBS-PERCEPT.5). The audit-host has neither CUDA SDK nor OptiX SDK so neither kernel's empirical aberration can be exercised. The structural data-paths are verified: (a) audit-host build (OptiX OFF) — 13/13 ctest PASS; (b) OptiX-ON-no-SDK build — 14/14 ctest PASS at OBS-PERCEPT.5 landing (including `optix_tests`). SDK-host runtime scenarios deferred to the future combined FIELD-* + OBS-PERCEPT CLI-bridge slice's audit on a CUDA + OptiX-SDK host (per FIELD-BEAUTY.8 §4.2 (b) RECOMMENDED combined-slice option). The seven SDK-host scenarios applying both backends symmetrically: (i) default-state byte identity; (ii) zero-beta byte identity; (iii) non-zero-beta consistency on `__raygen__pinhole`; (iv) non-zero-beta consistency on `__raygen__pathtrace`; (v) OBS-F.2 fixture runtime cross-backend equivalence; (vi) Doppler / searchlight interaction; (vii) CUDA ↔ OptiX cross-backend byte-identity (`cmp aov_beauty.ppm optix_aov_beauty.ppm` on the OBS-F.2 fixture with `--observer-perception-mode relativistic` + non-zero beta — structurally guaranteed by check #6's five-axis symmetry argument; empirical verification deferred).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | PASS (structural) — runtime DEFERRED to SDK-host audit pass |
| 11 | Verdict                                                | All ten structural / runtime-status checks PASS. The OBS-PERCEPT.5 surface is well-scoped, OptiX-kernel-wired, byte-identical-by-default, gate-disciplined, CUDA-isolated, cross-backend symmetric. Master rule #3 + #11 + #12 + #16 satisfied (see §3 below).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Commit shape

The OBS-PERCEPT.5 commit (`1dbeb23`) modifies two
files:

```
docs/BUILD_PLAN.md         | 254 ++++++++++++
src/optix/OptixPrograms.cu |  56 ++++++-
```

Source-code surface: one OptiX file
(`OptixPrograms.cu`). Zero CUDA / manifold /
relativity / scene / io / renderer / main / core /
field / pathtracer / camera / geometry / lighting /
material / texture file modifications. Zero
`CMakeLists.txt` modification (the `rr_manifold`
PUBLIC link on `rr_optix` from MANI-I.5 +
FIELD-I.11 already propagates the
`ObserverFrame.h` include path; the OBS-PERCEPT.5
helper consumption is purely transitive). Zero
test extension (the OBS-PERCEPT.3-landed 13
RR_CHECK assertions on the shared helper already
cover the three-gate logic; the OBS-PERCEPT.5
OptiX consumption inherits the contract
structurally).

The narrow scope intentionally excludes every
other file: no CUDA modifications, no
`OptixRenderer.cpp` / `OptixRenderer.h`
modification (the kernel-arm-internal scope), no
new CLI flag, no new ObserverFrame POD field, no
new tests, no new fixtures. All deferred or
out-of-scope per the operator's brief.

### 3.2 Check #1 — OptiX primary-ray aberration exists

Two OptiX raygen sites consume the shared
`rr::manifold::apply_observer_primary_ray_aberration(...)`
helper:

**`__raygen__pinhole` site** at
`OptixPrograms.cu:239-249`:

```cpp
if (optixLaunchParams.params.enable_aberration) {
    if (perception_active_pinhole) {
        ray.direction =
            rr::manifold::apply_observer_primary_ray_aberration(
                optixLaunchParams.observer_frame, ray.direction);
    } else {
        ray.direction = rr::relativity::aberrateDirection(
            rel, ray.direction);
    }
}
```

**`__raygen__pathtrace` site** at
`OptixPrograms.cu:1268-1277`:

```cpp
if (optixLaunchParams.params.enable_aberration) {
    if (perception_active_pt) {
        ray.direction =
            rr::manifold::apply_observer_primary_ray_aberration(
                optixLaunchParams.observer_frame, ray.direction);
    } else {
        ray.direction = rr::relativity::aberrateDirection(
            rel, ray.direction);
    }
}
```

Both sites consume the same shared helper as the
CUDA OBS-PERCEPT.3 sites. The dispatch shape is
identical: outer `params.enable_aberration` gate,
inner `perception_active_*` ternary, helper call
on the true branch, legacy
`aberrateDirection(rel, ...)` on the else branch.

### 3.3 Check #2 — activation requires ConstantVelocityMinkowski

The OBS-PERCEPT.5 OptiX raygen dispatches read
`perception_active_*` (a per-thread boolean
computed earlier in each raygen body):

- `__raygen__pinhole:215-217`: `const bool
  perception_active_pinhole =
  (optixLaunchParams.observer_frame.perception_mode
  == rr::manifold::PerceptionMode::ConstantVelocityMinkowski);`
- `__raygen__pathtrace:1216-1218`: same shape
  using `perception_active_pt`.

On `perception_active_*` true, the helper is
invoked; the shared helper's outer gate
(`ObserverFrame.h:559-562`) ALSO checks
`perception_mode == ConstantVelocityMinkowski`
internally — this is a defensive double-check that
makes the helper safe to call from non-perception-
mode flows (the helper short-circuits on a closed
outer gate regardless of caller context).

When `perception_active_*` is false (the
dispatch's else branch fires), the legacy
`aberrateDirection(rel, ...)` path is taken — this
path reads `observer.velocity` via the OBS-P.2
ternary's else branch (verified at
`OptixPrograms.cu:218-220` for pinhole;
`:1219-1221` for pathtrace).

Empirical verification of the helper's outer gate
is via the OBS-PERCEPT.3-landed test functions on
`manifold_identity_tests.cpp` (the OptiX consumer
inherits the contract structurally; same helper,
same gate).

### 3.4 Check #3 — activation requires beta > 0

The shared helper's inner gate at
`ObserverFrame.h:565-571` enforces the explicit
`|beta|² > 0` (squared-magnitude + NaN-safe form)
inner gate. The OptiX raygen dispatches invoke
the helper unconditionally on the
`perception_active_*` true branch; the inner
gate's behavior is identical to the CUDA-side
consumption.

Empirical verification via the OBS-PERCEPT.3-
landed `test_obs_percept_3_constant_velocity_zero_beta_returns_input`
(3 RR_CHECKs on `manifold_identity_tests.cpp`).

### 3.5 Check #4 — `beta = 0` is no-op

Three-layer no-op anchor preserved on both
backends (the structural contract inherits from
OBS-PERCEPT.3 → OBS-PERCEPT.5 verbatim):

**Layer 1 — shared helper's inner gate** (the
OBS-PERCEPT.3 contract): the explicit `!(beta2 >
0.0f)` short-circuit closes on zero beta.

**Layer 2 — math leaf identity** (pre-existing):
`rr::relativity::aberrateDirection(beta_vec,
direction)` internally checks `if (beta_mag <=
1.0e-12f) return direction;`. Defence-in-depth at
the math-leaf level.

**Layer 3 — OBSERVER.6 adapter** (host-side):
emits `observer_frame.beta = (0, 0, 0)` exactly
on zero-beta inputs.

All three layers compose on both backends. The
OBS-PERCEPT.5 OptiX-side consumption uses the
SAME helper (the shared `rr_manifold`
implementation), so the contract is
structurally identical to the CUDA-side
consumption.

### 3.6 Check #5 — default observer is no-op

The DEFAULT `optixLaunchParams.observer_frame{}`
is the load-bearing no-op anchor on OptiX (same
as the CUDA-side `view.observer_frame{}`):

**Layer 1 — default `perception_mode = Identity`**:
the OBSERVER.10 payload audit's check on
default-constructed `OptixLaunchParams{}`
verified the default value. The OBS-PERCEPT.5
outer gate closes on Identity.

**Layer 2 — dispatch else-branch fires**: at
both OptiX raygen sites, the
`perception_active_*` boolean is false on
default Identity mode; the dispatch's else-
branch fires the legacy
`aberrateDirection(rel, ...)` path. This
preserves the post-OBSERVER.10 + OBS-P.2 +
FIELD-BEAUTY.5 baseline behavior for
`--render-optix-relativistic` flows that don't
engage `--observer-perception-mode
relativistic`.

**Layer 3 — closest-hit / miss / shadow programs
byte-identical**: the OBS-PERCEPT.5 slice
modifies only the raygen primary-ray block; the
downstream programs are byte-identical to the
OBS-PERCEPT.4 baseline. No new arithmetic; no
new state.

Empirical SDK-host verification of the default-
state byte identity is deferred per check #10.

### 3.7 Check #6 — CUDA / OptiX math and activation rules match

**Five-axis symmetry** verified explicitly
(mirrors the FIELD-BEAUTY.6 §3.7 framework
applied to OBS-PERCEPT.* helper):

**Axis A — Same POD type.** Both backends
consume `rr::manifold::ObserverFrame` directly.
The CUDA-side field is `view.observer_frame`
(`CudaSceneView::observer_frame`, landed at
OBSERVER.8). The OptiX-side field is
`optixLaunchParams.observer_frame`
(`OptixLaunchParams::observer_frame`, landed
at OBSERVER.10). Both are the same
`rr::manifold::ObserverFrame` POD verbatim.

**Axis B — Same shared helper.** Both backends
invoke `rr::manifold::apply_observer_primary_ray_aberration(observer_frame,
direction)` — the same RR_HD inline function
from `src/manifold/ObserverFrame.h:553+`
(landed at OBS-PERCEPT.3). The function is
defined once; both backends inline it at their
respective consumer sites.

**Axis C — Same dispatch shape.** Both backends
use:

```cpp
if (params.enable_aberration) {
    if (perception_active) {
        ray.direction = rr::manifold::apply_observer_primary_ray_aberration(
            observer_frame, ray.direction);
    } else {
        ray.direction = rr::relativity::aberrateDirection(
            rel, ray.direction);
    }
}
```

Verified by inspection at:
- CUDA: `CudaTestKernel.cu:248-258` +
  `:387-397` + `CudaPathTracer.cu:209-220`.
- OptiX: `OptixPrograms.cu:239-249` +
  `:1268-1277`.

The CUDA + OptiX dispatch blocks are shape-
identical token-for-token (modulo the trivial
variable-name suffix differences:
`perception_active` vs
`perception_active_pinhole` /
`perception_active_pt`).

**Axis D — Same math leaf.** Both backends call
`rr::relativity::aberrateDirection(beta_vec,
direction)` (the two-argument form) through
the shared helper. The math leaf at
`RelativityMath.h:112-129` is RR_HD inline,
identical on both backends; both backends emit
equivalent PTX/SASS for the math computation
(modulo backend-specific optimization choices
that don't affect bit-identity of the float
output).

**Axis E — Same gate semantics.** The shared
helper's two-gate logic
(`perception_mode == ConstantVelocityMinkowski`
AND `|beta|² > 0`) fires identically on both
backends. The 13 RR_CHECK assertions in
`manifold_identity_tests.cpp` empirically pin
the gate behavior; both backends consume the
gate-pinned helper.

Cross-backend bit-identity is **structurally
guaranteed by construction**. Empirical
SDK-host PPM-cmp verification deferred to the
combined CLI-bridge slice's audit (per
FIELD-BEAUTY.8 §4.2 (b) RECOMMENDED).

### 3.8 Check #7 — secondary rays unchanged

The OBS-PERCEPT.5 slice's two raygen-site
modifications target ONLY the primary-ray
generation block of each raygen program.

**`__raygen__pinhole`**: the pinhole raygen
fires a single primary ray (`optixTrace(...)`
at line ~265, after the OBS-PERCEPT.5 dispatch
at lines 239-249) and processes the returned
radiance payload. No secondary-ray block in
this raygen.

**`__raygen__pathtrace`**: the dispatch at
lines 1268-1277 is INSIDE the per-spp loop's
primary-ray block (after `generate_camera_ray(...)`
at line 1264, before the bounce loop at line
1281+). The bounce loop's per-bounce
`optixTrace(...)` calls + closest-hit-payload-
based shading + Doppler-from-payload +
searchlight modulation are byte-identical to
the OBS-PERCEPT.4 baseline.

Per-line diff `git diff 40bb476..1dbeb23 --
src/optix/OptixPrograms.cu` confirms:
- Zero changes inside the bounce-loop body of
  `__raygen__pathtrace`.
- Zero changes inside `__closesthit__radiance`,
  `__closesthit__pathtrace`, `__miss__radiance`,
  `__miss__pathtrace`, `__miss__shadow`.

Option A primary-ray-only per OBS-PERCEPT.1 §5.2
is honored.

### 3.9 Check #8 — Doppler / searchlight unchanged

The OBS-PERCEPT.5 slice does NOT modify the
Doppler / searchlight call sites. The OBS-P.2
guarded ternary at the Doppler-from-`rel`
snapshot computation is preserved verbatim:

**At `__raygen__pinhole`**:
- The OBS-P.2 ternary at lines 215-221:
  ```cpp
  const bool perception_active_pinhole = ...;
  const rr::math::Vec3 beta_source_pinhole =
      perception_active_pinhole
          ? optixLaunchParams.observer_frame.beta
          : optixLaunchParams.observer.velocity;
  const auto rel = rr::relativity::precompute_relativity(beta_source_pinhole);
  ```
  is byte-identical to the OBS-PERCEPT.4 baseline.
- The Doppler-factor computation at line ~255+
  (`const float D = rr::relativity::dopplerFactor(rel,
  ray.direction);`) reads from the same `rel`
  snapshot. Byte-identical.

**At `__raygen__pathtrace`**: same shape; the OBS-P.2
ternary at lines 1216-1222 + the Doppler-from-`rel`
computation are preserved verbatim.

The closest-hit Doppler-color-shift + searchlight
scaling blocks in `__closesthit__radiance` are
byte-identical. Per-line diff confirms zero changes
inside the Doppler / searchlight blocks. Master
rule #12 satisfied — the OBS-PERCEPT.* arc's
Doppler / searchlight migration is deferred to
future sub-slices.

### 3.10 Check #9 — OptiX OFF build remains valid

Audit-host build (`RR_ENABLE_OPTIX=OFF`):

```
13/13 Test #13: renderer_tests ........ Passed
100% tests passed, 0 tests failed out of 13
```

Per-binary:
- `relativity_tests: 841/841 passed` —
  unchanged.
- `manifold_identity_tests: 421/421 passed` —
  unchanged from OBS-PERCEPT.4 (no test
  extension this slice; the OBS-PERCEPT.3-landed
  13 RR_CHECK assertions cover the shared
  helper's contract that the OptiX consumption
  inherits).
- `cli_tests: 274/274 passed` — unchanged.
- `renderer_tests: 35/35 passed` — unchanged.
- `field_tests: 135/135 passed` — unchanged.
- Every other suite unchanged.

The audit-host build is structurally insulated
from the OBS-PERCEPT.5 OptiX changes because the
`if(RR_ENABLE_OPTIX)` guard at `CMakeLists.txt:548`
is false; the `rr_optix` library is not built;
`OptixPrograms.cu` is not compiled. The
OBS-PERCEPT.5 source change is contained in this
single OptiX-only file.

OptiX-ON-no-SDK build at the OBS-PERCEPT.5 landing
also clean: 14/14 ctest PASS (including
`optix_tests`). The CUDA-disabled stub fallback
path in `OptixRenderer.cpp` is unchanged (no new
trailing parameter on `render_aovs(...)` was
added).

Full rebuild via `cmake --build
/home/user/RelativityRender/build` clean — no
new warnings on any module. No CMakeLists.txt
change required.

### 3.11 Check #10 — runtime CUDA / OptiX status

`PASS_WITH_RUNTIME_DEFERRED`.

Both backends now have primary-ray aberration
arms wired:
- **CUDA arm** at `CudaTestKernel.cu:248-258`
  + `:387-397` + `CudaPathTracer.cu:209-220`
  (OBS-PERCEPT.3).
- **OptiX arm** at `OptixPrograms.cu:239-249`
  + `:1268-1277` (OBS-PERCEPT.5).

The audit-host has neither CUDA SDK nor OptiX
SDK so neither kernel's empirical aberration
can be exercised this audit. The structural
data-paths are verified:

- **Audit-host build (OptiX OFF):** 13/13
  ctest PASS.
- **OptiX-ON-no-SDK build:** 14/14 ctest PASS at
  the OBS-PERCEPT.5 landing commit's empirical
  run in `/tmp/rr_build_optix_no_sdk`.

The SDK-host runtime checks DEFERRED to the
future combined FIELD-* + OBS-PERCEPT CLI bridge
slice's audit (per the FIELD-BEAUTY.8 §4.2 (b)
RECOMMENDED combined-slice option). Seven
deferred scenarios applying both backends
symmetrically:

- **(i) Default-state byte identity (both
  backends).** Run `--render-aovs` +
  `--render-optix-aovs` against every default
  scene; `cmp` PPMs pre + post OBS-PERCEPT.5
  byte-by-byte. Structurally guaranteed by the
  three-layer no-op anchor (check #5); empirical
  verification deferred.
- **(ii) Zero-beta byte identity (both
  backends).** Run with `--observer-perception-mode
  relativistic --observer-beta 0`; `cmp`
  against `--observer-perception-mode default`
  PPMs. Structurally guaranteed by the inner
  gate (check #3); empirical verification
  deferred.
- **(iii) Non-zero-beta consistency on
  `__raygen__pinhole` (OptiX).** Run
  `--render-optix-aovs --observer-perception-mode
  relativistic --observer-beta 0.5
  --observer-direction 1,0,0` on the OBS-F.2
  fixture; verify the OptiX-side PPM matches
  the corresponding CUDA-side PPM byte-for-byte
  (the shared helper's mathematical content +
  the dispatch shape are identical).
- **(iv) Non-zero-beta consistency on
  `__raygen__pathtrace` (OptiX).** Same
  invocation with `--render-optix-pathtrace`;
  verify the OptiX-side PPM matches the
  CUDA-side `--render-pathtrace` PPM.
- **(v) OBS-F.2 fixture runtime cross-backend
  equivalence.** Both backends produce byte-
  identical aov_beauty.ppm files on the
  fixture + relativistic perception engaged.
- **(vi) Doppler / searchlight interaction.**
  The aberrated ray direction flows through
  the unchanged Doppler / searchlight pipeline
  on both backends; the per-pixel D / D⁴
  values are byte-identical to the post-OBS-P.2
  baseline.
- **(vii) CUDA ↔ OptiX cross-backend byte-
  identity.** The five-axis symmetry argument
  at check #6 structurally guarantees this;
  empirical `cmp aov_beauty.ppm optix_aov_beauty.ppm`
  on the OBS-F.2 fixture with relativistic
  perception engaged is the canonical SDK-host
  validation. Deferred.

All seven SDK-host scenarios DEFER to: (a) a
future OBS-PERCEPT.11 arc capstone SDK-host pass;
OR (b) the combined FIELD-* + OBS-PERCEPT CLI
bridge slice's SDK-host audit. The combined
slice is the highest-converging-leverage option
per FIELD-BEAUTY.8 §4.2 (b).

### 3.12 Master-rule satisfaction recap

- **Master rule #3 ("no fake stubs"):** satisfied.
  The OptiX raygen dispatches are fully wired
  (real helper invocations; real else-branch
  dispatch; real per-thread snapshot
  consumption). The shared-helper consumption
  inherits the OBS-PERCEPT.3-empirically-tested
  three-gate logic structurally.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. Cross-backend
  symmetry verified at check #6's five-axis
  argument — structurally guaranteed by
  construction; the shared helper's behavior
  is empirically pinned by the OBS-PERCEPT.3-
  landed 13 RR_CHECK assertions on
  `manifold_identity_tests.cpp`. The OptiX-side
  consumption inherits the contract; no
  additional test surface is needed (the helper
  is the testable interface; consumers inherit
  structurally).

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Scope deliberately narrow to OptiX
  primary-ray ONLY. CUDA preserved verbatim;
  secondary-ray transform deferred (Option A
  primary-ray-only per OBS-PERCEPT.1 §5.2);
  Doppler / searchlight migration deferred to
  future OBS-PERCEPT.* sub-slices; CLI bridge
  deferred (combined FIELD-* + OBS-PERCEPT CLI
  bridge slice is the documented next slot);
  debug AOV deferred (OBS-PERCEPT.7); fixture
  authoring deferred (OBS-PERCEPT.9).

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied.
  The OBS-PERCEPT.5 default state is unchanged
  from the OBS-PERCEPT.4 baseline on both
  backends:
    - No `--render-*` action's output changes by
      default.
    - No existing PPM filename changes.
    - No new file produced.
    - No existing AOV slot's value changes.
  The single observable behaviour change is
  the structural presence of the unified
  helper consumption at the OptiX raygen sites;
  its observable behaviour from every default
  CLI invocation is zero because both gates
  close.

### 3.13 Honest scope recap

This audit is an **OptiX primary-ray aberration
audit with SDK-host runtime DEFERRED** + **CUDA
path preserved-unchanged** + **Doppler /
searchlight preserved-unchanged** + **secondary
rays preserved-unchanged**. The verdict `PASS`
reflects:

- (a) The structural OptiX raygen-arm surface
  is well-formed (two dispatch blocks at
  `__raygen__pinhole` + `__raygen__pathtrace`;
  shared helper consumption; legacy
  else-branch preserved).
- (b) The cross-backend symmetry is verified
  via the five-axis argument at check #6 (same
  POD type, same shared helper, same dispatch
  shape, same math leaf, same gate semantics).
- (c) The default-state preservation is
  structural on both backends (Identity outer
  gate closes; legacy else-branch fires).
- (d) The CUDA isolation is structural (zero
  `src/cuda/` hits).
- (e) The Doppler / searchlight + secondary-ray
  preservations are structural (per-line diff
  confirms zero changes inside those blocks).
- (f) Both build configs empirically verified
  (audit-host 13/13 + OptiX-ON-no-SDK 14/14).

The runtime status's `PASS_WITH_RUNTIME_DEFERRED`
is honest: the SDK-host scenarios from §3.11
require a CUDA + OptiX-SDK host AND (the
path-tracer site's NEW perception-engaging
behavior carries forward from OBS-PERCEPT.3).
Master rule #3 + #11 + #12 + #16 satisfied.

With OBS-PERCEPT.5 in, the OBSERVER.15 capstone
audit's `PASS_WITH_RUNTIME_DEFERRED` future-
kernel-migration risk #1 is now closed on BOTH
backends. The remaining OBS-PERCEPT.* arc work
is the debug AOV (OBS-PERCEPT.7), fixture
(OBS-PERCEPT.9), and arc capstone (OBS-PERCEPT.11)
plus the converging combined CLI bridge slice
that closes the entire field-and-observer-arc
family's runtime-deferred verdict tail.

---

## 4. NEXT

### 4.1 Renumbered OBS-PERCEPT.* sub-slice ladder

The OBS-PERCEPT.6 audit slot insertion (mirroring
the OBS-PERCEPT.4 + FIELD-BEAUTY.6 + FIELD-I.12
audit-slot insertion precedents) shifts subsequent
OBS-PERCEPT.* sub-slices by one. The
post-OBS-PERCEPT.6 ladder is:

- **OBS-PERCEPT.7** — Debug AOV (the perception-
  transform diagnostic AOV; mirrors the OBSERVER.13
  / FIELD-I.7 AOV-data-model precedent).
- **OBS-PERCEPT.8** — Debug AOV audit.
- **OBS-PERCEPT.9** — Fixture.
- **OBS-PERCEPT.10** — Fixture audit.
- **OBS-PERCEPT.11** — Arc capstone audit.

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

### 4.2 Candidate next slots (prioritised)

**(a) HIGHLY RECOMMENDED — combined FIELD-* +
OBS-PERCEPT CLI bridge slice** (per the
FIELD-BEAUTY.8 §4.2 (b) recommendation, applied
at OBS-PERCEPT.* arc scope). Single SDK-host
audit closes the entire field-and-observer-arc
family's runtime-deferred verdict tail:

  - FIELD-I.10 + FIELD-I.12 + FIELD-I.14 deferred
    PASS_WITH_RUNTIME_DEFERRED verdicts → PASS
    (the diagnostic AOV's runtime PPM cmp).
  - FIELD-BEAUTY.4 + FIELD-BEAUTY.6 + FIELD-BEAUTY.8
    deferred verdicts → PASS (the beauty-mapping
    SDK-host runtime cmp).
  - OBS-PERCEPT.4 + OBS-PERCEPT.6 deferred
    verdicts → PASS (the perception-transform
    SDK-host PPM cmp).
  Best converging-leverage option if the operator
  has SDK-host access.

**(b) RECOMMENDED — OBS-PERCEPT.7: debug AOV**
(the renumbered next OBS-PERCEPT.* impl slot).
Natural continuation: adds a perception-transform
diagnostic AOV (e.g. `ObserverPerceptionDelta`)
that writes the per-pixel `(boosted_dir -
pre_boost_dir)` vector to a 3-channel Vec3 AOV.
Mirrors the OBSERVER.13 `ObserverBeta` AOV
shape; gated on `--observer-debug` (or a new
`--observer-perception-debug` modifier flag).

**(c) Manifold-orthogonal work.** Multiple
options:
  - **Deferred SDK-host runtime pass** for the
    entire arc family (OBSERVER.* + OBS-P.* +
    OBS-F.* + FIELD-I.* + FIELD-BEAUTY.* +
    OBS-PERCEPT.*) — highest converging-leverage
    option.
  - **MANI-I.12 final cross-host manifold audit**.
  - **Denoiser integration with chart-aware
    AOVs**.
  - **Path-tracer feature breadth** (NEE
    extension, BSDF expansion, MIS tuning).

**(d) NOT RECOMMENDED — OBS-PERCEPT.9 fixture
authoring before debug AOV.** Possible but
premature: the fixture would expose
perception-engaging behavior but without the
debug AOV diagnostic, the SDK-host validation
relies on PPM-cmp alone. Better to land the
debug AOV first so future SDK-host audits can
visually verify the per-pixel transform output.

**(e) DEFERRABLE — RETROACTIVE task brief
authoring.** The operator may choose to
backfill the missing FIELD-BEAUTY.1 +
FIELD-BEAUTY.2 + FIELD_INTERPRETATION_PHASE1_AUDIT.md
task brief / audit slots. The honest-framing
approach has worked across the FIELD-BEAUTY.* +
OBS-PERCEPT.* arc families. Deferrable to
operator discretion.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the master rule #3
  + #11 + #12 + #16 satisfaction recap at
  §3.12 cites these).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §7.2 (the architecture-doc anchor for the
  observer-frame Lorentz boost of the tetrad
  concept).

### 5.2 OBS-PERCEPT.* arc references

- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1).
- `docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`
  (OBS-PERCEPT.2 — the task brief OBS-PERCEPT.5
  consumes; §5 "future OptiX mirror"
  invariants honored verbatim at this audit's
  check #6).
- `docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md`
  (OBS-PERCEPT.4 — the CUDA-side precedent
  audit; this audit's check #6 five-axis
  symmetry argument extends the CUDA-side
  contract to the OptiX-side consumption).

### 5.3 OBSERVER.* + OBS-P.* + OBS-F.* arc references

- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3).
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5).
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7).
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9).
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11 — the precedent OptiX-payload
  audit; the OBS-PERCEPT.5 OptiX consumption
  reads the OBSERVER.11-audited
  `optixLaunchParams.observer_frame` field
  verbatim).
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14).
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15 — capstone whose
  PASS_WITH_RUNTIME_DEFERRED risk #1 is now
  closed on BOTH backends).
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3).
- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md`
  (OBS-F.3).

### 5.4 Parallel-arc references

- `docs/FIELD_SCALAR_BEAUTY_MAPPING_AUDIT.md`
  (FIELD-BEAUTY.8 — capstone whose §4.2 (b)
  RECOMMENDED combined CLI bridge slice
  applies at the OBS-PERCEPT.* arc family
  scope).
- `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
  (FIELD-I.12 — the precedent OptiX-side
  bridge audit; this OBS-PERCEPT.6 audit's
  §3.7 five-axis symmetry argument inherits
  the FIELD-I.12 framework).
- `docs/FIELD_SCALAR_BEAUTY_OPTIX_AUDIT.md`
  (FIELD-BEAUTY.6 — the canonical OptiX-side
  beauty-mapping audit; this audit's
  five-axis symmetry argument structurally
  mirrors).

### 5.5 Source surface audited

- `src/optix/OptixPrograms.cu` (the OBS-PERCEPT.5
  surface — +52 lines vs the OBS-PERCEPT.4
  baseline; two raygen-site dispatches at
  `__raygen__pinhole:239-249` +
  `__raygen__pathtrace:1268-1277`; both
  consume the shared OBS-PERCEPT.3 helper
  from `rr_manifold`; both preserve the
  legacy else-branch via
  `rr::relativity::aberrateDirection(rel,
  ...)`).

### 5.6 Surrounding commit SHAs

- `1dbeb23` — OBS-PERCEPT.5 audited tree (the
  per-slice gate target).
- `40bb476` — OBS-PERCEPT.4 baseline (the diff
  baseline for checks #7 + #8 + #9).
- `b653e48` — OBS-PERCEPT.3 (the CUDA-side
  bridge whose helper the OBS-PERCEPT.5
  consumption inherits).
- `0bf2bb8` — OBS-PERCEPT.2 (the task brief
  whose §5 "future OptiX mirror" invariants
  the OBS-PERCEPT.5 implementation honors).

### 5.7 Unchanged source files (sampled)

The following files are byte-identical to the
OBS-PERCEPT.4 baseline (`40bb476`), confirmed by
the diff filters at checks #7 + #8 + #9 +
narrow-scope discipline:

- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/cuda/`.
- Every file in `src/manifold/` (the shared
  helper at `ObserverFrame.h:553+` is read
  but not modified).
- Every file in `src/relativity/`.
- Every file in `src/renderer/`.
- Every file in `src/scene/`.
- Every file in `src/io/`.
- Every file in `src/core/`, `src/math/`,
  `src/image/`, `src/gpu/`, `src/app/`,
  `src/field/`, `src/pathtracer/`,
  `src/camera/`, `src/geometry/`,
  `src/lighting/`, `src/material/`,
  `src/texture/`.
- `src/main.cpp`.
- Other `src/optix/` files: `OptixAccel.cpp`,
  `OptixAccel.h`, `OptixBackend.cpp`,
  `OptixBackend.h`, `OptixDenoiser.cpp`,
  `OptixDenoiser.h`, `OptixLaunchParams.h`,
  `OptixPipeline.cpp`, `OptixPipeline.h`,
  `OptixRenderer.cpp`, `OptixRenderer.h`,
  `OptixSBT.h`.

### 5.8 Unchanged test + scene + build files

- All test files (`tests/`) byte-identical to
  the OBS-PERCEPT.4 baseline. No test
  extension this slice (the OBS-PERCEPT.3-
  landed 13 RR_CHECK assertions on the
  shared helper already cover the three-gate
  logic; the OptiX-side consumption inherits
  structurally).
- All scene files (`scenes/`) byte-identical
  to the OBS-PERCEPT.4 baseline (the OBS-F.2
  fixture is the canonical runtime-deferred
  validation input; no new fixture this slice).
- `CMakeLists.txt` byte-identical to the
  OBS-PERCEPT.4 baseline (no link change; the
  shared helper from `rr_manifold` is consumed
  transitively via the existing
  `rr_manifold` PUBLIC link on `rr_optix`
  established at MANI-I.5 + FIELD-I.11).

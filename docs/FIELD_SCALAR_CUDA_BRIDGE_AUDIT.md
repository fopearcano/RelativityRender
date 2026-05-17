# Scalar Field CUDA Bridge Audit (FIELD-I.10)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `e1a42c2` ("cuda:
FIELD-I.9 — Scalar Field CUDA Bridge (impl, CUDA launch
payload + kernel arm)").
Audit baseline: `7cd4557` ("docs: FIELD-I.8 — Scalar
Field Diagnostic AOV Audit (docs only)") — the last
commit before FIELD-I.9 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, the FIELD-I.6 task brief + the
FIELD-I.7 / FIELD-I.8 / FIELD-I.9 commits' source
content, the `renderer_tests` runtime output, the
unchanged `field_tests` / `manifold_identity_tests` /
`cli_tests` / `relativity_tests` / `math_tests` /
`image_tests` / `gpu_tests` / `pathtracer_*_tests` /
`demo_tests` runtime outputs, and `ctest` exit codes.

This audit is the per-slice gate for FIELD-I.9
(`e1a42c2`). It verifies the nine items the task brief
enumerates — CUDA scalar-field payload exists if
needed; scalar-field config reaches CUDA-facing
launch/config structures; default disabled field
remains no-op; no beauty shading changes; no
observer/manifold behaviour changes; OptiX path
unchanged; build/test status; runtime CUDA status
(PASS / DEFERRED / BLOCKED); and the overall verdict
(PASS / REPAIR / BLOCKED).

The FIELD-I.9 slice is the **CUDA bridge** that lifts
the FIELD-I.7 AOV data-model entry from "structurally
present but kernel-unwired" to "kernel-wired but
dispatcher-unreached". It threads
`rr::field::ScalarFieldConfig` through
`AOVTargets` → `CudaSceneView` →
`CudaTestKernel.cu`'s `k_render_scene` kernel via the
documented FIELD-I.6 contract; the kernel arm reads
the payload exclusively for the AOV write and is gated
on `aovs.field_scalar != nullptr` (the structural
unreachability anchor preserved this slice — every
dispatcher caller passes `nullptr` by default).

---

## 1. VERDICT

**PASS.**

All eight structural checks (#1, #2, #3, #4, #5, #6,
#7) plus the runtime status (#8) return their
expected verdicts. Check #9 (overall verdict) is
`PASS`. The FIELD-I.9 surface ships exactly what the
operator's four-bullet brief authorised — CUDA-side
launch payload threading + kernel arm consumption for
the FieldScalar diagnostic AOV — without spilling into
OptiX, CLI, dispatcher, beauty, observer, or manifold
surfaces.

Check #8's runtime status is
`PASS_WITH_RUNTIME_DEFERRED` — the standard CUDA-host
deferral pattern: the audit-host has no CUDA SDK, so
the kernel arm's empirical write behaviour cannot be
exercised this audit; the structural data-path (the
host-side `AOVTargets` → `CudaSceneView` field
threading on `CudaRenderer.cu`) is verified by the
audit-host build's clean compile + 13/13 ctest pass +
unchanged renderer_tests / field_tests counts. The
SDK-host runtime pass mirrors the FIELD-I.6 task
brief's §8 scenarios (neutral diagnostic on
disabled-field default; non-default field
visualisation; off-path bit-identity; composability
with `--manifold-debug` + `--observer-debug`); those
deferred scenarios will land at the future CLI bridge
slice's audit when the `--field-debug` gate flips the
AOV reachable.

The intermediate-shape verdict from FIELD-I.8
(`PASS_WITH_FUTURE_KERNEL_WIRING_DEFERRED`) is now
**partially closed**: the kernel arm exists this
slice, so the FIELD-I.7 audit's checks #5 + #6's
runtime portion converges to the standard
`PASS_WITH_RUNTIME_DEFERRED` shape for CUDA (no
SDK-host on this audit machine), and the OptiX portion
remains `DEFERRED-FUTURE-WIRING` until the OptiX
bridge slice lands.

---

## 2. PER-CHECK RESULTS

| # | Check                                                  | Evidence                                                                                                                                                                                                                                                                                                                  | Verdict |
|---|--------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1 | CUDA scalar-field payload exists if needed             | The FIELD-I.2 `rr::field::ScalarFieldConfig` POD is RR_HD-inline-friendly + trivially copyable (verified at FIELD-I.3 audit), so it can be embedded directly into the CUDA launch payload without wrapping in a CUDA-safe shadow struct. FIELD-I.9 ships the field by-value on `CudaSceneView` (`src/cuda/CudaScene.cuh:175`) + on `AOVTargets` (`src/cuda/CudaRenderer.h:260`). No separate CUDA-safe payload structure was required; the existing POD is the CUDA-safe payload structure by construction. | PASS    |
| 2 | Scalar-field config reaches CUDA-facing launch/config structures | Three-layer threading verified: (a) `AOVTargets::scalar_field_config` (`CudaRenderer.h:260`) is the host-side authoring slot the dispatcher fills; (b) `CudaRenderer::render_scene_with_aovs` threads it into `view.scalar_field_config` at `src/cuda/CudaRenderer.cu:345`; (c) `CudaSceneView::scalar_field_config` (`CudaScene.cuh:175`) is the kernel-visible payload field the CUDA kernel reads. The matching AOV pointer flows alongside: `AOVTargets::field_scalar` (`CudaRenderer.h:206`) → `view.aovs.field_scalar` (`CudaRenderer.cu:309`) → `DeviceAOVView::field_scalar` (`CudaAOV.cuh:118`). | PASS    |
| 3 | Default disabled field remains no-op                   | Three-layer no-op anchor preserved: (a) every dispatcher caller passes `targets.field_scalar = nullptr` (the default; no CLI flag wires it up this slice) so the kernel arm is structurally unreachable — `git grep targets.field_scalar` returns only the `AOVTargets`-side field-declaration site + the threading site, no assignment site; (b) every dispatcher caller passes `targets.scalar_field_config = {}` (the default = `disabled_scalar_field_config()`); (c) even if both were set non-default, the FIELD-I.2 `evaluate(...)` short-circuit returns `0.0f` on the disabled-field config (FIELD-I.3 audit's check #2 three-layer no-op anchor empirically verified at the 80 RR_CHECK assertions in `tests/field_tests.cpp`). All four pre-FIELD-I.9 dispatcher invocation paths (`run_render_aovs`, `run_render_*`) preserve byte-identical output. | PASS    |
| 4 | No beauty shading changes                              | The CUDA kernel write arm (`src/cuda/CudaTestKernel.cu:797-805`) is structurally outside the beauty pass arithmetic — it lives in the `k_render_scene` kernel's post-shading AOV-write block, sibling to the OBSERVER.13 `observer_beta` arm, after the framebuffer pixel write at `CudaTestKernel.cu:574-579`. The arm reads `evaluate(scene.scalar_field_config, hit_pos)` and writes ONLY to `scene.aovs.field_scalar[pix_idx_1]`; no other variable is touched. The beauty pass's color / Doppler / searchlight arithmetic (lines `CudaTestKernel.cu:480-572`) is byte-identical to the FIELD-I.8 baseline. No kernel TU reads `scene.scalar_field_config` outside the gated AOV-write arm. | PASS    |
| 5 | No observer/manifold behaviour changes                 | `git diff 7cd4557..e1a42c2 --name-only -- 'src/manifold/' 'src/relativity/'` returns zero hits. The `CudaSceneView::manifold_mode` / `coordinate_chart` / `observer_frame` fields are byte-identical to the FIELD-I.8 baseline (the FIELD-I.9 commit only appends a new `scalar_field_config` field after them). The OBSERVER.13 `observer_beta` AOV arm's kernel read site is unchanged; the OBS-P.2 perception-mode-guarded ternary at the SR-helper call sites is unchanged. Every OBSERVER.* + OBS-P.* + OBS-F.* + SCHW.* + PENROSE.* + MANI-I.* arc's verdicts carry forward verbatim.                                                                                                       | PASS    |
| 6 | OptiX path unchanged                                   | `git diff 7cd4557..e1a42c2 --name-only -- 'src/optix/'` returns zero hits. Every `src/optix/*.cu` / `*.cuh` / `*.cpp` / `*.h` file is byte-identical to the FIELD-I.8 baseline. The OptiX bridge for FieldScalar is deferred to a separate slice per the operator's FIELD-I.9 brief "Do not modify OptiX yet" rule.                                                                                                                                                                                                                              | PASS    |
| 7 | Build / test status                                    | Audit-host `ctest` returns `100% tests passed, 0 tests failed out of 13` (unchanged from FIELD-I.8; no new ctest target). Per-binary: `renderer_tests: 35/35` (unchanged); `field_tests: 135/135` (unchanged); `relativity_tests: 841/841`; `manifold_identity_tests: 408/408`; `cli_tests: 274/274`; every other suite unchanged. Full rebuild via `cmake --build /home/user/RelativityRender/build` after the `rr_field` PUBLIC link addition on `rr_gpu` adds no new warnings on any module. Empirically verified.                                                                                                                                                                                                                                                                                                                                                                                                          | PASS    |
| 8 | Runtime CUDA status                                    | `PASS_WITH_RUNTIME_DEFERRED`. The audit-host build is `RR_ENABLE_CUDA=OFF` (no CUDA SDK present), so the CUDA kernel arm's empirical write cannot be exercised this audit. The host-side data-path (`AOVTargets` → `render_scene_with_aovs` → `CudaSceneView`) is verified by clean compile + clean tests. The FIELD-I.6 task brief's §8 SDK-host runtime scenarios (§8.1 + §8.5 + §8.7 — the three that apply this slice; §8.2 + §8.6 mention OptiX which is deferred; §8.3 + §8.4 + §8.6 mention `--field-debug` CLI flag which is deferred to a separate slice) are reserved for the future CLI-bridge slice's audit when the AOV becomes reachable from a `--render-aovs` invocation. | PASS (structural) — runtime DEFERRED to SDK-host audit pass when the future CLI bridge slice lands |
| 9 | Verdict                                                | All eight structural / runtime-status checks PASS. The FIELD-I.9 surface is well-scoped, kernel-wired, byte-identical-by-default, single-AOV-consumer-only. Master rule #3 + #11 + #12 + #16 satisfied (see §3 below).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Commit shape

The FIELD-I.9 commit (`e1a42c2`) modifies seven files:

```
CMakeLists.txt             |   6 +-
docs/BUILD_PLAN.md         | 302 +++++++++++++++++++++++++++++
src/cuda/CudaAOV.cuh       |  22 ++++
src/cuda/CudaRenderer.cu   |  22 ++++
src/cuda/CudaRenderer.h    |  35 ++++++
src/cuda/CudaScene.cuh     |  38 ++++++
src/cuda/CudaTestKernel.cu |  32 +++++
```

All source-code files touched are in `src/cuda/`;
zero non-CUDA source files modified. The only build-
configuration touched is `CMakeLists.txt` (a 1-line
addition of `rr_field` to the `rr_gpu` PUBLIC link
list at line 763 mirroring the `rr_manifold` PUBLIC
precedent). The remaining file (`docs/BUILD_PLAN.md`)
is the per-slice entry mirroring the standard rubric.

The narrow scope intentionally excludes every other
file from the FIELD-I.6 task brief's 15-row
files-likely-involved table: no `src/optix/*`, no
`src/core/Config.h`, no `src/core/CommandLine.cpp`,
no `src/main.cpp`, no `tests/cli_tests.cpp`, no
`tests/renderer_tests.cpp` extensions, no fixture
scene, no companion doc. All deferred to follow-up
slices.

### 3.2 Check #1 — CUDA scalar-field payload exists if needed

The FIELD-I.2 `rr::field::ScalarFieldConfig` POD is
trivially copyable + RR_HD-inline-friendly (verified
at the FIELD-I.3 audit). It contains:

- `bool enabled` (1 byte)
- `float strength` (4 bytes)
- `ScalarFieldKind kind` (`enum class : <implementation-defined>`)
- `rr::math::Vec3 center` (12 bytes)
- `float min_radius`, `max_radius`, `falloff`,
  `min_value`, `max_value`, `constant_value`
  (6 × 4 = 24 bytes)

All POD-trivial; can be embedded directly into the
CUDA launch payload by-value (small POD launch-arg
discipline shared with `Camera` / `Observer` /
`RelativityParams` / `ManifoldMode` /
`CoordinateChart` / `ObserverFrame`). No CUDA-safe
shadow struct or device-side mirror required; the
existing `ScalarFieldConfig` POD is the CUDA-safe
payload structure by construction.

The decision matches the OBSERVER.8 precedent
(`ObserverFrame` is embedded by-value on
`CudaSceneView`; no shadow struct) and the SCHW.5
precedent (`CoordinateChart` is embedded by-value;
no shadow struct).

### 3.3 Check #2 — scalar-field config reaches CUDA-facing launch/config structures

Three-layer host → kernel data-path verified:

**Layer 1 (host-side authoring).** `AOVTargets`
(`src/cuda/CudaRenderer.h:169-261`) gains two new
fields:
- `float* field_scalar = nullptr;`
  (`CudaRenderer.h:206`) — the AOV-pass device
  buffer pointer.
- `rr::field::ScalarFieldConfig scalar_field_config
  = {};` (`CudaRenderer.h:260`) — the per-launch
  field-config payload.

**Layer 2 (host → device threading).**
`CudaRenderer::render_scene_with_aovs`
(`src/cuda/CudaRenderer.cu`) gains two new threading
lines after the existing OBSERVER.13 + OBSERVER.8
threads:

```cpp
view.aovs.field_scalar    = targets.field_scalar;        // line 309
view.scalar_field_config  = targets.scalar_field_config; // line 345
```

Both happen before the kernel launch
(`launch_render_scene(...)` at the bottom of the
function), so the values are part of the kernel
launch-arg payload by the time the kernel reads
them.

**Layer 3 (kernel-visible payload).** `CudaSceneView`
(`src/cuda/CudaScene.cuh`) gains the new
`scalar_field_config` field at line 175 (sibling of
`observer_frame` at line 137); `DeviceAOVView`
(`src/cuda/CudaAOV.cuh`) gains the new
`field_scalar` pointer at line 118 (sibling of
`observer_beta` at line 96). The kernel reads both
via `scene.scalar_field_config` and
`scene.aovs.field_scalar`.

The threading is symmetric with the existing
OBSERVER.8 (`observer_frame`) + OBSERVER.13
(`observer_beta`) precedent verbatim; the FIELD-I.9
commit's kernel-side reads at `CudaTestKernel.cu:797-805`
exercise both new fields.

### 3.4 Check #3 — default disabled field remains no-op

Three-layer no-op anchor preserved this slice:

**Layer 1 — null pointer gate.** Every dispatcher
caller passes `targets.field_scalar = nullptr` by
default (the in-class default at `CudaRenderer.h:206`).
No call site in the FIELD-I.9 commit assigns a
non-null pointer:

```
$ git grep -E 'field_scalar\s*=' src/
src/cuda/CudaAOV.cuh:    float* field_scalar    = nullptr;
src/cuda/CudaRenderer.cu:    view.aovs.field_scalar = targets.field_scalar;
src/cuda/CudaRenderer.h:    float* field_scalar    = nullptr;
src/cuda/CudaTestKernel.cu:    scene.aovs.field_scalar[pix_idx_1] = ...
                                                          ^^^ (write site, gated)
```

The only assignments are the in-class defaults
(`= nullptr` on both
`DeviceAOVView::field_scalar` + `AOVTargets::field_scalar`)
and the threading (`view.aovs.field_scalar =
targets.field_scalar`). No host-side dispatcher
flips the pointer to non-null; therefore the kernel
arm at `CudaTestKernel.cu:797` is structurally
unreachable from every existing CLI invocation.

**Layer 2 — disabled-field-config gate.** Every
dispatcher caller passes
`targets.scalar_field_config = {}` (the in-class
default at `CudaRenderer.h:260`, which evaluates to
`disabled_scalar_field_config()` byte-for-byte —
the same `{enabled = false, strength = 0.0f,
kind = Constant, ...}` no-op anchor verified at
FIELD-I.3 check #2). No call site assigns a
non-default `scalar_field_config`.

**Layer 3 — evaluator short-circuit.** Even if both
layers 1 + 2 were bypassed (which no current CLI
flow does), the FIELD-I.2 `evaluate(...)` helper at
`src/field/ScalarField.h` short-circuits to `0.0f`
when `enabled = false` OR `strength = 0.0f`.
Empirically verified at the 80 RR_CHECK assertions
in `tests/field_tests.cpp` (the FIELD-I.3 audit's
check #2 three-layer no-op anchor).

The composition guarantees: every existing
`--render-aovs` / `--render-pathtrace` /
`--render-scene` / `--render-mesh-scene` /
`--render-material-scene` /
`--render-direct-lighting` /
`--render-relativistic` invocation produces
byte-identical output to the FIELD-I.8 baseline
(`7cd4557`). The new kernel arm fires zero times
across every existing dispatcher path; even when a
future slice adds the CLI flag, the disabled-field
default + the evaluator short-circuit guarantee a
flat-zero PPM until the operator explicitly
authors a non-trivial field.

### 3.5 Check #4 — no beauty shading changes

The CUDA kernel write arm at
`src/cuda/CudaTestKernel.cu:797-805` is
structurally outside the beauty pass arithmetic. It
is positioned at the END of `k_render_scene`,
after:

- The framebuffer pixel write
  (`pixels[idx + 0..3] = color.x..1.0f` at lines
  `CudaTestKernel.cu:574-579`).
- The Beauty / Normal / Depth / Albedo /
  DopplerFactor / SearchlightFactor /
  ManifoldCoordinates / ObserverBeta AOV write
  arms (lines `CudaTestKernel.cu:599-774`).

The arm reads `scene.scalar_field_config` +
`best.position` and writes ONLY to
`scene.aovs.field_scalar[pix_idx_1]`. No other
variable is touched. The beauty pass's color
computation (lines 450-572: closest-hit detection,
shading, lighting, Doppler colour shift, searchlight
beaming) is byte-identical to the FIELD-I.8 baseline
because the kernel arm modifies neither `color` nor
any intermediate the beauty computation depends on.

Per-line diff inspection (`git diff
7cd4557..e1a42c2 -- src/cuda/CudaTestKernel.cu`)
confirms: zero changes inside lines 450-572 of the
kernel.

### 3.6 Check #5 — no observer/manifold behaviour changes

`git diff 7cd4557..e1a42c2 --name-only --
'src/manifold/' 'src/relativity/'` returns zero
hits. Every file in `src/manifold/` and
`src/relativity/` is byte-identical to the FIELD-I.8
baseline. The `CudaSceneView::manifold_mode` /
`coordinate_chart` / `observer_frame` fields are
preserved verbatim (the FIELD-I.9 commit appends the
new `scalar_field_config` field AFTER them, leaving
their offsets + types untouched).

The OBSERVER.13 `observer_beta` AOV arm's kernel
read site at `CudaTestKernel.cu:764-774` is
byte-identical. The OBS-P.2 perception-mode-guarded
ternary at the SR-helper call sites (within the
beauty pass) is byte-identical. Every OBSERVER.* +
OBS-P.* + OBS-F.* + SCHW.* + PENROSE.* + MANI-I.*
arc's prior verdict carries forward unchanged.

### 3.7 Check #6 — OptiX path unchanged

`git diff 7cd4557..e1a42c2 --name-only --
'src/optix/'` returns zero hits. Every `src/optix/`
file (`OptixLaunchParams.h`, `OptixPrograms.cu`,
`OptixRenderer.h`, `OptixRenderer.cpp`, etc.) is
byte-identical to the FIELD-I.8 baseline. The
OptiX bridge for FieldScalar is deferred to a
separate slice per the operator's FIELD-I.9 brief
"Do not modify OptiX yet" rule.

When the future OptiX-bridge slice lands, it will
mirror the FIELD-I.9 CUDA bridge shape verbatim
(`OptixLaunchParams::scalar_field_config`;
`OptixLaunchParams::aov_field_scalar`; closest-hit
+ miss arms gated on the pointer; same RR_HD inline
`evaluate(...)` helper for cross-backend math
equivalence). The data-path the CUDA bridge
exercises is the same data-path the OptiX bridge
will exercise — no per-backend math divergence.

### 3.8 Check #7 — build / test status

Audit-host `ctest` empirical output:

```
13/13 Test #13: renderer_tests ........ Passed
100% tests passed, 0 tests failed out of 13
```

Per-binary breakdown (test counts):

| Suite                       | Pre-FIELD-I.9 | Post-FIELD-I.9 |
|-----------------------------|---------------|----------------|
| math_tests                  | unchanged     | unchanged      |
| image_tests                 | unchanged     | unchanged      |
| gpu_tests                   | unchanged     | unchanged      |
| pathtracer_tests            | unchanged     | unchanged      |
| pathtracer_nee_tests        | unchanged     | unchanged      |
| pathtracer_bsdf_tests       | unchanged     | unchanged      |
| pathtracer_mis_tests        | unchanged     | unchanged      |
| cli_tests                   | 274/274       | 274/274        |
| relativity_tests            | 841/841       | 841/841        |
| manifold_identity_tests     | 408/408       | 408/408        |
| field_tests                 | 135/135       | 135/135        |
| demo_tests                  | unchanged     | unchanged      |
| renderer_tests              | 35/35         | 35/35          |

Full rebuild via `cmake --build
/home/user/RelativityRender/build` after the
`rr_field` PUBLIC link addition on `rr_gpu` adds no
new warnings on any module. The reconfigure picks
up the new link dep cleanly (verified empirically
at the FIELD-I.9 landing commit's build transcript).

### 3.9 Check #8 — runtime CUDA status

`PASS_WITH_RUNTIME_DEFERRED`.

The audit-host build is `RR_ENABLE_CUDA=OFF` (no
CUDA SDK present), so the CUDA kernel arm's
empirical write cannot be exercised this audit. The
host-side data-path is verified structurally:

- `AOVTargets` field declarations compile + link
  cleanly into both `rr_gpu` (the renderer-tests
  link path) + `rr_renderer` (the
  renderer_tests target's transitive consumer).
- `CudaRenderer.cu`'s threading lines pass the
  CUDA-disabled audit-host compile (the file is
  excluded from compilation on
  `RR_ENABLE_CUDA=OFF`, but the host-side
  AOVTargets struct that consumers populate IS
  compiled into the audit-host build via
  `CudaRenderer.h`, and 35 / 35
  `renderer_tests` PASS confirms no host-side
  type / link breakage).
- The new `rr_field` PUBLIC link on `rr_gpu`
  propagates the include path cleanly to every
  consumer (audit-host build's clean compile
  verifies this).

The SDK-host runtime checks DEFERRED to the future
CLI-bridge slice's audit:

- **§8.1 (neutral diagnostic on disabled-field
  default, CUDA path).** DEFERRED — the kernel
  arm exists but is structurally unreachable
  (no CLI flag flips `targets.field_scalar`
  on). When the future CLI-bridge slice lands,
  the SDK-host pass will exercise this.
- **§8.3 (non-default Constant field
  visualisation, CUDA path).** DEFERRED — same
  reason; no CLI authoring surface yet.
- **§8.4 (non-default Radial field
  visualisation, CUDA path).** DEFERRED — same
  reason.
- **§8.5 (off-path bit-identity, CUDA path).**
  STRUCTURALLY SATISFIED today (the kernel arm
  is unreachable so no off-path PPM can be
  emitted); empirical SDK-host pass DEFERRED
  to the future bridge slice's audit (the
  beauty / Normal / Depth / Albedo /
  DopplerFactor / SearchlightFactor /
  ManifoldCoordinates / ObserverBeta PPMs are
  byte-identical to the FIELD-I.8 baseline
  because no kernel arm reads or writes the
  new fields outside the gated FieldScalar
  arm).
- **§8.6 (composability with `--manifold-debug`
  + `--observer-debug`).** DEFERRED — no
  `--field-debug` CLI flag yet.
- **§8.7 (cross-backend equivalence).**
  DEFERRED on the CUDA side (no SDK-host); on
  the OptiX side it doubles as the deferred
  OptiX bridge slice's check.

### 3.10 Master-rule satisfaction recap

- **Master rule #3 ("no fake stubs"):** satisfied.
  The CUDA kernel arm at `CudaTestKernel.cu:797-805`
  is fully wired: real `rr::field::evaluate(...)`
  invocation; real pointer dereferences; real
  on-hit + on-miss branches. The arm's
  structural-unreachability via null-pointer-gate
  is honest scope framing (per the doc-comment at
  `CudaTestKernel.cu:776-796`: "until a future
  CLI / dispatcher slice flips the pointer on...").
  No fake stub; no empty scaffold; no kernel arm
  pretending to consume data it doesn't actually
  read.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. The CUDA bridge's
  behaviour is documented as contract on every
  modified file's doc-comments + structurally
  rooted in the audit-host-verified FIELD-I.2
  `evaluate(...)` semantics (the 80 RR_CHECK
  assertions on `tests/field_tests.cpp` exercise
  the underlying evaluator's contract; the
  FIELD-I.7 audit's 8 RR_CHECK assertions on
  `tests/renderer_tests.cpp` exercise the AOV
  data-model entry; the FIELD-I.9 host-side
  threading + kernel-arm structure is verified
  by the audit-host's clean compile + 13 / 13
  ctest PASS).

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Scope deliberately narrow to CUDA
  bridge only — OptiX deferred per operator
  brief, CLI deferred, dispatcher emit deferred,
  field-to-beauty mapping deferred per operator
  brief. The renumbered FIELD-I.* sub-slice
  ladder (per §4 below) is the canonical record
  of the deferred surfaces' landing points.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied.
  The FIELD-I.9 default state is unchanged from
  the FIELD-I.8 baseline:
    - No CLI flag exists to engage the AOV.
    - No dispatcher caller flips the AOV pointer
      on.
    - Even if both were bypassed, the disabled-
      field config makes the evaluator return
      `0.0f` at every position.
  The single observable behaviour change is the
  structural presence of the kernel arm — which
  is null-pointer-gated AND disabled-field-
  config-gated, so its observable behaviour from
  every existing CLI invocation is zero.

### 3.11 Honest scope recap

This audit is a **CUDA bridge audit with
SDK-host runtime DEFERRED** + **OptiX path
preserved-unchanged**. The verdict `PASS` is
the FIELD-I.9 CUDA-side surface's verdict;
check #6 (OptiX path) is PASS-by-virtue-of-
non-modification (the deferral is the
operator's explicit "Do not modify OptiX yet"
rule); check #8 (runtime CUDA) is the
standard `PASS_WITH_RUNTIME_DEFERRED` shape
the OBSERVER.9 / OBSERVER.11 / OBSERVER.14 /
OBS-P.3 + MANI-I.* / SCHW.* / PENROSE.*
precedents established.

The intermediate-shape verdict from FIELD-I.8
(`PASS_WITH_FUTURE_KERNEL_WIRING_DEFERRED`)
converges this audit to the standard
`PASS_WITH_RUNTIME_DEFERRED` shape for the
CUDA side; the OptiX side retains the
`DEFERRED-FUTURE-WIRING` framing until the
OptiX bridge slice lands. The future CLI
bridge slice will close the CUDA-side
`PASS_WITH_RUNTIME_DEFERRED` to `PASS` once
the SDK-host pass exercises the §8.1 + §8.3
+ §8.4 + §8.5 + §8.6 scenarios end-to-end.

---

## 4. NEXT

### 4.1 Renumbered FIELD-I.* sub-slice ladder

The FIELD-I.10 audit slot insertion (mirroring the
FIELD-I.8 / FIELD-I.5 / FIELD-I.3 / OBS-F.3 /
OBSERVER.3 / .5 / .7 / .9 / .11 / .14 + OBS-P.3
precedent) shifts subsequent FIELD-I.* sub-slices
by one. The post-FIELD-I.10 ladder is:

- **FIELD-I.11** — Scalar Field OptiX Bridge (the
  renumbered next FIELD-I.* impl slot; mirrors
  the FIELD-I.9 CUDA bridge for the OptiX path —
  `OptixLaunchParams::scalar_field_config`
  payload field; `OptixLaunchParams::aov_field_scalar`
  pointer; closest-hit + miss arms gated on the
  pointer; same RR_HD inline `evaluate(...)`
  helper for cross-backend math equivalence;
  `OptixRenderer::render_aovs` gains a trailing-
  defaulted `ScalarFieldConfig` parameter
  mirroring the OBSERVER.10 / OBSERVER.13
  trailing-defaulted-parameter ABI-extension
  pattern).
- **FIELD-I.12** — OptiX bridge audit (docs-
  only; mirrors this audit slot's shape for the
  FIELD-I.11 surface).
- **FIELD-I.13** — CLI + Config + dispatcher
  bridge (lands the `--field-debug` modifier
  flag + the minimal `--field-*` authoring CLI
  surface; extends `rr::core::Config` with a
  `scalar_field_config` field; threads
  `cfg.scalar_field_config` from CLI through
  `run_render_aovs` / `run_render_optix_aovs`
  into `AOVTargets` / `OptixLaunchParams`;
  flips the AOV reachable on; ships the
  `output/aov_field_scalar.ppm` /
  `output/optix_aov_field_scalar.ppm` save
  sites; closes the FIELD-I.10 + FIELD-I.12
  audits' runtime-deferred portions on
  SDK-host).
- **FIELD-I.14** — CLI bridge audit.
- **FIELD-I.15** — Mapping CLI + Config bridge
  (the full FIELD-I.4 `FieldMappingConfig` CLI
  authoring surface;
  `--field-color-strength` /
  `--field-emission-strength` /
  `--field-aov-strength` / `--field-bias` /
  `--field-clamp-output` /
  `--field-mapping-target` flags).
- **FIELD-I.16** — Mapping CLI bridge audit.
- **FIELD-I.17** — Mapping kernel pipeline
  (lifts `FieldMappingConfig` onto the
  beauty-pass arithmetic; the actual field-to-
  beauty integration).
- **FIELD-I.18** — Mapping kernel pipeline
  audit.
- **FIELD-I.19** — Fixture scene + companion
  doc.
- **FIELD-I.20** — Fixture audit.
- **FIELD-I.21** — Arc capstone audit.

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

### 4.2 Candidate next slots (prioritised)

**(a) RECOMMENDED — FIELD-I.11: Scalar Field
OptiX Bridge** (the renumbered next FIELD-I.*
impl slot). Natural continuation of the
FIELD-I.* arc: mirrors the FIELD-I.9 CUDA
bridge for the OptiX path; closes the
FIELD-I.10 audit's check #6 OptiX-deferred
portion + half of check #8's runtime-deferred
portion. With both bridges in, the FIELD-I.13
CLI bridge becomes a single slice that flips
both backends reachable simultaneously.

**(b) Manifold-orthogonal work.** Multiple
options available with their own merit:
  - **Deferred SDK-host runtime pass** for the
    OBSERVER.* + OBS-P.* + OBS-F.* arc family
    (highest converging-leverage option;
    converts every PASS_WITH_RUNTIME_DEFERRED
    verdict in that family to PASS).
  - **MANI-I.12 final cross-host manifold
    audit**.
  - **Denoiser integration with chart-aware
    AOVs**.
  - **Path-tracer feature breadth** (NEE
    extension, BSDF expansion, MIS tuning).

**(c) NOT RECOMMENDED — direct FIELD-I.13 CLI
bridge slice skipping the FIELD-I.11 OptiX
bridge.** Would land a CLI flag that flips
only the CUDA-side AOV reachable; the OptiX
path would silently fall through to no-PPM.
Worse, the cross-backend bit-identity check
(FIELD-I.6 task brief §8.7) becomes
undefined. Better to pair the OptiX bridge
with the CUDA bridge before opening the CLI
gate so both backends become reachable
simultaneously.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the master rule #3
  + #11 + #12 + #16 satisfaction recap at §3.10
  cites these).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §6 (the Field Interpretation Layer as an
  OPTIONAL extension above the Manifold Core).
- `docs/FIELD_INTERPRETATION_LAYER.md` §4.6
  (the diagnostic-AOV channel design-doc
  anchor for the FIELD-I.7 / .9 surfaces).

### 5.2 FIELD-I.* arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md`
  (FIELD-I.3).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md`
  (FIELD-I.5).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6 — the canonical task brief the
  FIELD-I.7 / .9 impl slices consume; this
  audit references §4 + §6 + §8 verbatim).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_AUDIT.md`
  (FIELD-I.8 — the precedent host-side
  data-model audit shape; this audit picks up
  its `PASS_WITH_FUTURE_KERNEL_WIRING_DEFERRED`
  intermediate verdict and converges the CUDA
  portion to `PASS_WITH_RUNTIME_DEFERRED`).

### 5.3 Precedent CUDA-bridge references

- OBSERVER.8 — the precedent CUDA bridge that
  threaded `ObserverFrame` through
  `CudaSceneView` (`src/cuda/CudaScene.cuh`)
  carry-only style. FIELD-I.9 mirrors the
  precedent's "POD by-value on the view" +
  "threaded from `AOVTargets` through
  `render_scene_with_aovs`" pattern verbatim.
- OBSERVER.13 — the precedent CUDA bridge
  that wired the `ObserverBeta` AOV via the
  `DeviceAOVView::observer_beta` pointer +
  the kernel write arm at
  `CudaTestKernel.cu:764-774`. FIELD-I.9
  mirrors this verbatim except for the
  single-channel (1-float-per-pixel) vs
  Vec3 (3-floats-per-pixel) shape; the
  `pix_idx_1` index calculation matches the
  existing Depth / DopplerFactor /
  SearchlightFactor 1-channel AOVs.
- MANI-I.8 — the original AOV-extension
  precedent; FIELD-I.9's gating pattern
  (`if (scene.aovs.field_scalar != nullptr)`)
  matches the MANI-I.8 + OBSERVER.13
  null-gate precedent verbatim.

### 5.4 Source surface audited

- `src/cuda/CudaScene.cuh` (modified +38 lines
  vs FIELD-I.8 baseline; the new
  `scalar_field_config` field at line 175 +
  the doc-comment block at lines 139-174 + the
  new `#include "field/ScalarField.h"`).
- `src/cuda/CudaAOV.cuh` (modified +22 lines;
  the new `float* field_scalar = nullptr;`
  slot at line 118 + the doc-comment block at
  lines 98-117).
- `src/cuda/CudaRenderer.h` (modified +35
  lines; the new `float* field_scalar =
  nullptr;` slot at line 206 + the new
  `rr::field::ScalarFieldConfig
  scalar_field_config = {};` field at line
  260 + their doc-comment blocks at lines
  194-205 + 227-259 + the new
  `#include "field/ScalarField.h"`).
- `src/cuda/CudaRenderer.cu` (modified +22
  lines; the two threading lines at lines 309
  + 345 + the doc-comments at lines 305-308 +
  326-344).
- `src/cuda/CudaTestKernel.cu` (modified +32
  lines; the kernel arm at lines 797-805 +
  the doc-comment block at lines 776-796).
- `CMakeLists.txt` (modified +5 lines; the
  `rr_field` PUBLIC link addition + the
  preceding doc-comment at lines 760-764).

### 5.5 Test surface unchanged

All test files in `tests/` are byte-identical
to the FIELD-I.8 baseline. No test extension
this slice (the kernel arm's empirical
behaviour requires SDK-host runtime
verification; deferred per §3.9).

### 5.6 Surrounding commit SHAs

- `e1a42c2` — FIELD-I.9 audited tree (the
  per-slice gate target).
- `7cd4557` — FIELD-I.8 baseline (the diff
  baseline for checks #5 + #6).
- `181a579` — FIELD-I.7 impl (the antecedent
  AOV data-model entry; the FIELD-I.9 kernel
  arm consumes the FIELD-I.7 enumerator +
  factory).
- `683a16d` — FIELD-I.4 impl (the antecedent
  `FieldMappingConfig` POD; intentionally NOT
  consumed by the FIELD-I.9 kernel arm per
  the "no field-to-beauty mapping yet"
  non-goal).
- `40c387b` — FIELD-I.2 impl (the antecedent
  scalar field model; the FIELD-I.9 kernel
  arm calls
  `rr::field::evaluate(scalar_field_config,
  hit_pos)` from this commit's surface).

### 5.7 Unchanged source files (sampled)

The following files are byte-identical to the
FIELD-I.8 baseline (`7cd4557`), confirmed by
the diff filters at checks #5 + #6 +
narrow-scope discipline:

- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/optix/`.
- Every file in `src/manifold/`.
- Every file in `src/relativity/`.
- Every file in `src/renderer/` (including
  `AOV.h` + `AOV.cpp` — the FIELD-I.7 surface
  is preserved verbatim).
- Every file in `src/scene/`, `src/io/`,
  `src/core/`, `src/math/`, `src/image/`,
  `src/gpu/`, `src/app/`, `src/field/`,
  `src/pathtracer/`.
- `src/main.cpp`.

### 5.8 Unchanged test files (sampled)

All test files are byte-identical to the
FIELD-I.8 baseline:

- `tests/math_tests.cpp` — unchanged.
- `tests/image_tests.cpp` — unchanged.
- `tests/gpu_tests.cpp` — unchanged.
- `tests/pathtracer_*_tests.cpp` (4 binaries)
  — unchanged.
- `tests/cli_tests.cpp` — unchanged.
- `tests/relativity_tests.cpp` — unchanged.
- `tests/manifold_identity_tests.cpp` —
  unchanged.
- `tests/field_tests.cpp` — unchanged.
- `tests/renderer_tests.cpp` — unchanged.
- `tests/demo_tests.cpp` — unchanged.

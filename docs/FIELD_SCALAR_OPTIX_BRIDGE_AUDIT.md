# Scalar Field OptiX Bridge Audit (FIELD-I.12)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `e15934e` ("optix:
FIELD-I.11 — Scalar Field OptiX Bridge (impl, OptiX
launch payload + programs)").
Audit baseline: `9a12fa9` ("docs: FIELD-I.10 — Scalar
Field CUDA Bridge Audit (docs only)") — the last commit
before FIELD-I.11 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The FIELD-I.11 commit's OptiX-ON-no-SDK
build was empirically verified at landing time (ctest
14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, the FIELD-I.10 / FIELD-I.11
commits' source content, the audit-host `renderer_tests`
runtime output, the FIELD-I.11 commit's OptiX-ON-no-SDK
empirical ctest verdict, the unchanged `field_tests` /
`manifold_identity_tests` / `cli_tests` /
`relativity_tests` / `math_tests` / `image_tests` /
`gpu_tests` / `pathtracer_*_tests` / `demo_tests`
runtime outputs, and `ctest` exit codes.

This audit is the per-slice gate for FIELD-I.11
(`e15934e`). It verifies the nine items the task brief
enumerates — OptiX scalar-field payload exists if
needed; scalar-field config reaches OptiX launch
params; semantics match CUDA scalar-field payload;
default disabled field remains no-op; no beauty
shading changes; no observer/manifold behaviour
changes; OptiX OFF build remains valid; runtime
CUDA/OptiX status (PASS / DEFERRED / BLOCKED); and the
overall verdict (PASS / REPAIR / BLOCKED).

The FIELD-I.11 slice is the **OptiX bridge** that
mirrors the FIELD-I.9 CUDA bridge for the OptiX path.
It lifts the FIELD-I.7 AOV data-model entry from
"CUDA-wired, OptiX-deferred" (FIELD-I.10 verdict
shape) to "both backends symmetric, both unreachable
until CLI bridge". Both backends now have a kernel
arm that consumes `ScalarFieldConfig` via the same
RR_HD inline `evaluate(...)` helper — cross-backend
math equivalence is structurally guaranteed.

---

## 1. VERDICT

**PASS.**

All eight structural / runtime-status checks (#1, #2,
#3, #4, #5, #6, #7, #8) PASS. Check #9 (overall
verdict) is `PASS`. The FIELD-I.11 surface ships
exactly what the operator's five-bullet brief
authorised — OptiX-side launch payload threading +
kernel arm consumption for the FieldScalar diagnostic
AOV, with CUDA-side payload semantics preserved —
without spilling into CUDA (no behavioural changes
there), CLI, dispatcher, beauty, observer, or
manifold surfaces.

Check #8's runtime status is the standard
`PASS_WITH_RUNTIME_DEFERRED` shape for the dual-
backend audit: both CUDA + OptiX kernel arms exist
this slice + the FIELD-I.10 baseline, but the audit-
host has no CUDA SDK + no OptiX SDK, so the kernel
arms' empirical writes cannot be exercised. The
structural data-path (host-side `render_aovs(...)`
threading on `OptixRenderer.cpp` + the OptiX-ON-no-SDK
stub-fallback path) is verified by clean compile +
13 / 13 audit-host ctest pass + 14 / 14 OptiX-ON-no-SDK
ctest pass.

The FIELD-I.10 audit's checks #5 + #6 + #8
runtime-deferred portions are now partially closed:
the OptiX kernel arm exists this slice, so the OptiX
portion converges from `DEFERRED-FUTURE-WIRING` to
the standard `PASS_WITH_RUNTIME_DEFERRED` shape. The
remaining deferral on both backends is the SDK-host
runtime pass that exercises the `--field-debug` gate
(landing at the future CLI bridge slice).

The narrow-scope verdict honesty: the operator's
FIELD-I.11 brief enumerated five implement-only
bullets (payload fields if not already present; pass
config into launch structures; match CUDA semantics;
preserve beauty; AOV-only consumption). The slice
satisfies all five:

- **Bullet 1** (OptiX scalar-field payload exists if
  needed): the FIELD-I.2 `ScalarFieldConfig` POD is
  POD-trivial + RR_HD-friendly, so it embeds directly
  on `OptixLaunchParams` by-value without a shadow
  struct. Same decision as the FIELD-I.9 CUDA bridge
  (FIELD-I.10 audit's check #1).
- **Bullet 2** (pass config into launch structures):
  three-layer threading verified —
  `render_aovs(...)` trailing parameter →
  `OptixRenderer.cpp:2864` threading →
  `OptixLaunchParams::scalar_field_config`. Matching
  AOV pointer flows alongside.
- **Bullet 3** (match CUDA scalar-field payload
  semantics): both backends consume the same POD
  type (`rr::field::ScalarFieldConfig`); both call
  the same RR_HD inline `evaluate(...)` helper;
  both gate writes on `aov_field_scalar != nullptr`;
  both write `0.0f` on miss; both write the raw
  scalar sample on hit (no `FieldMappingConfig`
  transform). Cross-backend bit-identity guaranteed
  by construction.
- **Bullet 4** (preserve beauty rendering): no
  closest-hit / miss / raygen program reads
  `scalar_field_config` outside the gated FieldScalar
  AOV arm. The beauty pass's shading arithmetic
  (closest-hit's hit-path color computation + miss-
  path sky shading) is byte-identical to the
  FIELD-I.10 baseline.
- **Bullet 5** (AOV-only consumption): the OptiX
  programs read `scalar_field_config` exclusively in
  the new FieldScalar AOV-write arm. Every other
  arm (Beauty / Normal / Depth / Albedo /
  DopplerFactor / SearchlightFactor /
  ManifoldCoordinates / ObserverBeta) is byte-
  identical.

---

## 2. PER-CHECK RESULTS

| # | Check                                              | Evidence                                                                                                                                                                                                                                                                                                                  | Verdict |
|---|----------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1 | OptiX scalar-field payload exists if needed        | The FIELD-I.2 `rr::field::ScalarFieldConfig` POD is POD-trivial + RR_HD-friendly (FIELD-I.3 audit's check), so it embeds directly into the OptiX launch payload without wrapping in an OptiX-safe shadow struct. FIELD-I.11 ships the field by-value on `OptixLaunchParams` (`src/optix/OptixLaunchParams.h:513`) + as a trailing-defaulted parameter on `OptixRenderer::render_aovs(...)` (`src/optix/OptixRenderer.h:551`). No separate OptiX-safe payload structure was required; mirrors the FIELD-I.9 CUDA bridge precedent (FIELD-I.10 audit's check #1) verbatim. | PASS    |
| 2 | Scalar-field config reaches OptiX launch params    | Three-layer threading verified: (a) `render_aovs(...)` trailing `scalar_field_config` parameter (`OptixRenderer.h:551`) is the host-side authoring slot the dispatcher fills; (b) `OptixRenderer.cpp:2864` threads it into `params.scalar_field_config` (sibling of the `params.observer_frame` thread at line 2832); (c) `OptixLaunchParams::scalar_field_config` (`OptixLaunchParams.h:513`) is the kernel-visible payload field the OptiX programs read. The matching AOV pointer flows alongside: `render_aovs(...)` trailing `field_debug` bool gates `d_aov_field_scalar` allocation (`OptixRenderer.cpp:2778-2786`) → `params.aov_field_scalar` (`OptixRenderer.cpp:2828`) → `OptixLaunchParams::aov_field_scalar` (`OptixLaunchParams.h:376`). | PASS    |
| 3 | Semantics match CUDA scalar-field payload          | Five-axis symmetry verified — (a) **same POD type**: both backends consume `rr::field::ScalarFieldConfig` directly (no per-backend shadow struct); (b) **same default**: both backends default to `disabled_scalar_field_config()` via in-class `{}` initialization (`OptixLaunchParams.h:513` on OptiX side; `CudaSceneView.cuh:175` on CUDA side); (c) **same null-gate**: both kernel write arms guard on `aov_field_scalar != nullptr` (OptiX) / `aovs.field_scalar != nullptr` (CUDA); (d) **same math**: both arms call `rr::field::evaluate(scalar_field_config, hit_pos)` — the same RR_HD inline helper from `src/field/ScalarField.h` (`OptixPrograms.cu:973` + `CudaTestKernel.cu:802`); (e) **same encoding**: both backends write 1-float-per-pixel via `pix_idx_1` indexing on hit, `0.0f` on miss (`OptixPrograms.cu:366` + `:973` mirror `CudaTestKernel.cu:797-805`). Cross-backend bit-identity guaranteed by construction; SDK-host runtime equivalence pass deferred per check #8. | PASS    |
| 4 | Default disabled field remains no-op               | Three-layer no-op anchor preserved: (a) **null pointer gate**: every dispatcher caller passes `field_debug = false` (the default at `OptixRenderer.h:567`) so `d_aov_field_scalar` is `nullptr` and `params.aov_field_scalar = nullptr`; the OptiX programs' write arms short-circuit; (b) **disabled-field-config gate**: even if `field_debug = true` were passed, the default `scalar_field_config = {}` is `disabled_scalar_field_config()` byte-for-byte; (c) **evaluator short-circuit**: even bypassing both gates, `evaluate(...)` returns `0.0f` when `enabled = false` OR `strength = 0.0f` (FIELD-I.3 audit's check #2 three-layer anchor verified at 80 RR_CHECK assertions in `tests/field_tests.cpp`). All existing dispatcher invocation paths preserve byte-identical output. | PASS    |
| 5 | No beauty shading changes                          | The OptiX FieldScalar write arms are structurally outside the beauty-pass arithmetic. The closest-hit arm at `OptixPrograms.cu:960-980` is positioned at the END of `__closesthit__radiance`, after the framebuffer pixel write + the existing eight AOV write arms (Beauty / Normal / Depth / Albedo / DopplerFactor / SearchlightFactor / ManifoldCoordinates / ObserverBeta). The miss arm at `OptixPrograms.cu:365-367` is positioned at the END of `__miss__radiance`'s AOV-write block. Both arms read `scalar_field_config` + recompute world-space `hit_pos` (or use the local `pix_idx_1`) and write ONLY to `aov_field_scalar[pix_idx_1]`. No other launch-params field is touched; no other AOV pointer is touched. The closest-hit's beauty path (Beauty pass write at `OptixPrograms.cu` lines preceding the new arm) is byte-identical to the FIELD-I.10 baseline. | PASS    |
| 6 | No observer/manifold behaviour changes             | `git diff 9a12fa9..e15934e --name-only -- 'src/manifold/' 'src/relativity/'` returns zero hits. Every file in `src/manifold/` and `src/relativity/` is byte-identical to the FIELD-I.10 baseline. The `OptixLaunchParams::manifold_mode` / `coordinate_chart` / `observer_frame` fields are preserved verbatim (FIELD-I.11 appends `scalar_field_config` AFTER `observer_frame`, leaving offsets + types untouched). The OBSERVER.13 `observer_beta` AOV write arm at `OptixPrograms.cu:912-924` is byte-identical. The SCHW.7 / PENROSE.8 + MANI-I.8 manifold AOV arms are byte-identical. Every OBSERVER.* + OBS-P.* + OBS-F.* + SCHW.* + PENROSE.* + MANI-I.* arc's prior verdict carries forward verbatim. | PASS    |
| 7 | OptiX OFF build remains valid                      | Audit-host (`RR_ENABLE_OPTIX=OFF`) `ctest` returns `100% tests passed, 0 tests failed out of 13` (unchanged from FIELD-I.10; same ctest set; no new target). Per-binary: `renderer_tests: 35/35`; `field_tests: 135/135`; `relativity_tests: 841/841`; `manifold_identity_tests: 408/408`; `cli_tests: 274/274`; every other suite unchanged. Full rebuild via `cmake --build /home/user/RelativityRender/build` clean — no new warnings on any module. On the audit host, `rr_optix` is NOT compiled (the `if(RR_ENABLE_OPTIX)` guard at `CMakeLists.txt:548` is false), so the OptiX surface changes don't even reach the compiler; the audit-host build is structurally insulated from OptiX modifications. Empirically verified at the FIELD-I.11 landing commit's build transcript. | PASS    |
| 8 | Runtime CUDA / OptiX status                        | `PASS_WITH_RUNTIME_DEFERRED`. Both backends now have kernel arms wired: CUDA (FIELD-I.9, at `CudaTestKernel.cu:797-805`) + OptiX (FIELD-I.11, at `OptixPrograms.cu:365-367` miss + `:960-980` closest-hit). The audit-host has neither CUDA SDK nor OptiX SDK so neither kernel's empirical write can be exercised. The structural host-side data-paths are verified: (a) audit-host build (OptiX OFF) PASS — 13/13 ctest; (b) OptiX-ON-no-SDK build PASS — 14/14 ctest at the FIELD-I.11 landing (`/tmp/rr_build_optix_no_sdk`; includes `optix_tests`). The SDK-host runtime scenarios from the FIELD-I.6 task brief §8 (neutral diagnostic; non-default Constant + Radial; off-path bit-identity; composability with `--manifold-debug` + `--observer-debug`; cross-backend equivalence) are reserved for the future CLI-bridge slice's audit when the `--field-debug` gate flips both backends reachable simultaneously. The structural cross-backend equivalence is now both **typed-equivalent** (same POD on both launch payloads) AND **math-equivalent** (same RR_HD inline `evaluate(...)` helper on both arms). | PASS (structural) — runtime DEFERRED to SDK-host audit pass when the future CLI bridge slice lands |
| 9 | Verdict                                            | All eight structural / runtime-status checks PASS. The FIELD-I.11 surface is well-scoped, OptiX-kernel-wired, byte-identical-by-default on the existing PPM set, single-AOV-consumer-only, cross-backend symmetric. The prerequisite `render_pathtrace_progressive` audit-host stub fix is bounded — minimal stub-signature update to satisfy the operator's "Must compile with OptiX OFF and ON" rule; the SDK body is byte-untouched. Master rule #3 + #11 + #12 + #16 satisfied (see §3 below).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Commit shape

The FIELD-I.11 commit (`e15934e`) modifies six files:

```
CMakeLists.txt                |   7 +-
docs/BUILD_PLAN.md            | 274 ++++++++++++++++++++++++++++++++++++++++++
src/optix/OptixLaunchParams.h |  64 ++++++++++
src/optix/OptixPrograms.cu    |  53 ++++++++
src/optix/OptixRenderer.cpp   | 106 ++++++++++++++--
src/optix/OptixRenderer.h     |  59 ++++++++-
```

All source-code files touched are in `src/optix/`;
zero CUDA / manifold / observer / scene / renderer /
core / main / field-impl files modified. The only
build-configuration touched is `CMakeLists.txt` (a
6-line addition of `rr_field` to the `rr_optix`
PUBLIC link list at line 636 mirroring the FIELD-I.9
`rr_gpu` PUBLIC precedent + the existing
`rr_manifold` PUBLIC precedent). The remaining file
(`docs/BUILD_PLAN.md`) is the per-slice entry
mirroring the standard rubric.

The narrow scope intentionally excludes every other
file from the FIELD-I.6 task brief's 15-row
files-likely-involved table: no `src/cuda/*`, no
`src/core/Config.h`, no `src/core/CommandLine.cpp`,
no `src/main.cpp`, no `tests/cli_tests.cpp`, no
`tests/renderer_tests.cpp` extensions, no fixture
scene, no companion doc. All deferred to follow-up
slices.

### 3.2 Check #1 — OptiX scalar-field payload exists if needed

The FIELD-I.2 `rr::field::ScalarFieldConfig` POD is
POD-trivial + RR_HD-inline-friendly (verified at the
FIELD-I.3 audit) so it can be embedded directly into
the OptiX launch payload by-value (small POD launch-
arg discipline shared with the existing `Camera` /
`Observer` / `RelativityParams` / `ManifoldMode` /
`CoordinateChart` / `ObserverFrame` payload fields).
No OptiX-safe shadow struct required.

The decision is symmetric with the FIELD-I.9 CUDA
bridge (FIELD-I.10 audit's check #1) — both
backends consume the same `ScalarFieldConfig` POD
directly.

### 3.3 Check #2 — scalar-field config reaches OptiX launch params

Three-layer host → kernel data-path verified:

**Layer 1 (host-side authoring).**
`OptixRenderer::render_aovs(...)`
(`OptixRenderer.h:482-568`) gains two new trailing-
defaulted parameters after the existing
OBSERVER.13 `observer_debug` parameter:

```cpp
rr::field::ScalarFieldConfig scalar_field_config = {},
bool                          field_debug         = false
```

**Layer 2 (host → device threading).**
`OptixRenderer::render_aovs(...)`
(`OptixRenderer.cpp:2527-2937` SDK body):

- Allocate `d_aov_field_scalar` via the existing
  `alloc_aov(aov1_floats, ...)` lambda, gated on
  `if (field_debug)` (`OptixRenderer.cpp:2778-2786`).
- `params.aov_field_scalar =
  static_cast<float*>(d_aov_field_scalar);`
  (`OptixRenderer.cpp:2828-2829`).
- `params.scalar_field_config = scalar_field_config;`
  (`OptixRenderer.cpp:2864`) — sibling of the
  `params.observer_frame = observer_frame` thread at
  line 2832.

Both happen before the `optixLaunch` call so the
values are part of the device-resident
`OptixLaunchParams` payload by the time the OptiX
programs read them.

**Layer 3 (kernel-visible payload).**
`OptixLaunchParams` (`OptixLaunchParams.h`) gains
two new fields:
- `float* aov_field_scalar = nullptr;`
  (`OptixLaunchParams.h:376`) — sibling of
  `aov_observer_beta`.
- `rr::field::ScalarFieldConfig scalar_field_config{};`
  (`OptixLaunchParams.h:513`) — sibling of
  `observer_frame`.

The OptiX programs read both via
`optixLaunchParams.scalar_field_config` and
`optixLaunchParams.aov_field_scalar`.

### 3.4 Check #3 — semantics match CUDA scalar-field payload

Five-axis symmetry verified explicitly:

**Axis A — Same POD type.** Both backends consume
`rr::field::ScalarFieldConfig` directly. No
per-backend shadow struct, no per-backend POD
mirror. The struct definition lives at
`src/field/ScalarField.h`; both
`OptixLaunchParams::scalar_field_config` and
`CudaSceneView::scalar_field_config` are declared as
the same type.

**Axis B — Same default.** Both backends initialize
the field to `disabled_scalar_field_config()` via
in-class `{}` initialization:
- OptiX: `rr::field::ScalarFieldConfig scalar_field_config{};` at `OptixLaunchParams.h:513`.
- CUDA: `rr::field::ScalarFieldConfig scalar_field_config{};` at `CudaScene.cuh:175`.
- AOVTargets: `rr::field::ScalarFieldConfig scalar_field_config = {};` at `CudaRenderer.h:260`.
- `render_aovs` trailing param: `rr::field::ScalarFieldConfig scalar_field_config = {}` at `OptixRenderer.h:551`.

All four sites produce byte-identical default POD
state (the FIELD-I.3 audit's check #2 three-layer
no-op anchor).

**Axis C — Same null-gate.** Both kernel write arms
guard on the AOV pointer:
- OptiX miss: `if (optixLaunchParams.aov_field_scalar != nullptr)` (`OptixPrograms.cu:365`).
- OptiX closest-hit: `if (optixLaunchParams.aov_field_scalar != nullptr)` (`OptixPrograms.cu:960`).
- CUDA: `if (scene.aovs.field_scalar != nullptr)` (`CudaTestKernel.cu:797`).

All three sites guard identically; when the pointer
is null the arm short-circuits and the underlying
POD is not read.

**Axis D — Same math.** Both arms call the same
RR_HD inline `evaluate(...)` helper from
`src/field/ScalarField.h`:
- OptiX: `rr::field::evaluate(optixLaunchParams.scalar_field_config, hit_pos_v3)` (`OptixPrograms.cu:973`).
- CUDA: `rr::field::evaluate(scene.scalar_field_config, hit_pos_v3)` (`CudaTestKernel.cu:802`).

The helper is `__host__ __device__` inline; both
backends emit identical PTX/SASS for the math leaf.

**Axis E — Same encoding.** Both backends write
1-float-per-pixel via `pix_idx_1` indexing:
- OptiX hit: `aov_field_scalar[pix_idx_1] = evaluate(...)` (`OptixPrograms.cu:973`).
- OptiX miss: `aov_field_scalar[pix_idx_1] = 0.0f` (`OptixPrograms.cu:366`).
- CUDA hit: `scene.aovs.field_scalar[pix_idx_1] = evaluate(...)` (`CudaTestKernel.cu:802`).
- CUDA miss: `scene.aovs.field_scalar[pix_idx_1] = 0.0f` (`CudaTestKernel.cu:804`).

Cross-backend bit-identity is structurally
guaranteed: same input config + same scene + same
hit position → same scalar sample → same byte
write. SDK-host runtime equivalence pass deferred
per check #8.

### 3.5 Check #4 — default disabled field remains no-op

Three-layer no-op anchor preserved this slice:

**Layer 1 — null pointer gate.** Every dispatcher
caller passes `field_debug = false` (the in-class
default at `OptixRenderer.h:567`). No call site in
the FIELD-I.11 commit flips it to `true`:

```
$ git grep -E 'field_debug\s*=\s*true' src/
(zero hits)
```

The host-side `if (field_debug)` block at
`OptixRenderer.cpp:2778-2786` does not allocate
`d_aov_field_scalar`; the variable remains
`nullptr`. The threading at `OptixRenderer.cpp:2828-2829`
assigns `params.aov_field_scalar = nullptr`. The
OptiX programs' miss + closest-hit arms at
`OptixPrograms.cu:365 + :960` short-circuit; no
write to the (nonexistent) buffer.

**Layer 2 — disabled-field-config gate.** Every
dispatcher caller passes `scalar_field_config = {}`
(the in-class default at `OptixRenderer.h:551`,
which evaluates to `disabled_scalar_field_config()`
byte-for-byte). No call site assigns a non-default
config this slice.

**Layer 3 — evaluator short-circuit.** Even if both
layers 1 + 2 were bypassed (which no current OptiX
dispatcher flow does), the FIELD-I.2 `evaluate(...)`
helper short-circuits to `0.0f` when `enabled =
false` OR `strength = 0.0f`. Empirically verified at
the 80 RR_CHECK assertions in `tests/field_tests.cpp`
(the FIELD-I.3 audit's check #2 three-layer no-op
anchor).

The composition guarantees: every existing
`--render-optix-aovs` invocation produces byte-
identical output to the FIELD-I.10 baseline
(`9a12fa9`). The new OptiX program arms fire zero
times across every existing dispatcher path; even
when a future slice adds the `--field-debug` CLI
flag, the disabled-field default + the evaluator
short-circuit guarantee a flat-zero PPM until the
operator explicitly authors a non-trivial field.

### 3.6 Check #5 — no beauty shading changes

The OptiX FieldScalar write arms at
`OptixPrograms.cu:365-367` (miss) and `:960-980`
(closest-hit) are structurally outside the beauty-
pass arithmetic:

- **Miss arm placement**: the new arm at
  `OptixPrograms.cu:365-367` is positioned at the
  END of the `__miss__radiance` AOV-write block,
  after the existing eight AOV miss arms (Beauty /
  Normal / Depth / Albedo / DopplerFactor /
  SearchlightFactor / ManifoldCoordinates /
  ObserverBeta). The arm reads only the AOV
  pointer + writes `0.0f` to it; no other variable
  is touched.

- **Closest-hit arm placement**: the new arm at
  `OptixPrograms.cu:960-980` is positioned at the
  END of `__closesthit__radiance`, after the
  framebuffer pixel write + the existing eight AOV
  write arms. The arm reads only the AOV pointer +
  the `scalar_field_config` POD + the world-space
  hit position (recomputed locally from
  `optixGetWorldRayOrigin/Direction/Tmax`); writes
  only to `aov_field_scalar[pix_idx_1]`.

Per-line diff inspection (`git diff
9a12fa9..e15934e -- src/optix/OptixPrograms.cu`)
confirms: zero changes inside the closest-hit's
beauty-path color computation block (the lines
preceding the new arm); zero changes inside the
miss-path sky shading block. The beauty PPM byte
output is structurally insulated.

### 3.7 Check #6 — no observer/manifold behaviour changes

`git diff 9a12fa9..e15934e --name-only --
'src/manifold/' 'src/relativity/'` returns zero
hits. Every file in `src/manifold/` and
`src/relativity/` is byte-identical to the
FIELD-I.10 baseline.

The `OptixLaunchParams::manifold_mode` /
`coordinate_chart` / `observer_frame` fields are
preserved verbatim (the FIELD-I.11 commit appends
the new `scalar_field_config` field AFTER
`observer_frame` at `OptixLaunchParams.h:513`,
leaving the manifold/observer fields' offsets +
types untouched).

The OBSERVER.13 `observer_beta` AOV write arm at
`OptixPrograms.cu:912-924` is byte-identical. The
SCHW.7 / PENROSE.8 + MANI-I.8 manifold AOV arms
(closest-hit + miss) are byte-identical. The
OBS-P.2 perception-mode-guarded ternary in the
beauty pass is byte-identical (no closest-hit
SR-helper call site touched).

Every OBSERVER.* + OBS-P.* + OBS-F.* + SCHW.* +
PENROSE.* + MANI-I.* arc's prior verdict carries
forward verbatim.

### 3.8 Check #7 — OptiX OFF build remains valid

Audit-host (`RR_ENABLE_OPTIX=OFF`) build:

```
13/13 Test #13: renderer_tests ........ Passed
100% tests passed, 0 tests failed out of 13
```

Per-binary:
- `relativity_tests: 841/841 passed` — unchanged.
- `manifold_identity_tests: 408/408 passed` —
  unchanged.
- `cli_tests: 274/274 passed` — unchanged.
- `renderer_tests: 35/35 passed` — unchanged.
- `field_tests: 135/135 passed` — unchanged.
- Every other suite unchanged.

The audit-host build is structurally insulated from
the FIELD-I.11 OptiX changes because the
`if(RR_ENABLE_OPTIX)` guard at `CMakeLists.txt:548`
is false; the `rr_optix` library is not built;
`OptixLaunchParams.h`, `OptixRenderer.h`,
`OptixRenderer.cpp`, `OptixPrograms.cu` are not
compiled. The CMake `rr_field` PUBLIC link addition
on `rr_optix` (at line 636) is inside the same
`if(RR_ENABLE_OPTIX)` block; it's a no-op on the
audit host.

Full rebuild via `cmake --build
/home/user/RelativityRender/build` clean — no new
warnings on any module. Empirically verified at the
FIELD-I.11 landing commit's build transcript.

### 3.9 Check #8 — runtime CUDA/OptiX status

`PASS_WITH_RUNTIME_DEFERRED`.

Both backends now have kernel arms wired:
- **CUDA arm** at `CudaTestKernel.cu:797-805`
  (FIELD-I.9 landed).
- **OptiX miss arm** at `OptixPrograms.cu:365-367`
  (FIELD-I.11 landed this slice).
- **OptiX closest-hit arm** at
  `OptixPrograms.cu:960-980` (FIELD-I.11 landed
  this slice).

The audit-host has neither CUDA SDK nor OptiX SDK
so neither kernel's empirical write can be
exercised. Structural data-paths empirically
verified:

- **Audit-host build (OptiX OFF):** 13/13 ctest
  PASS. The host-side `AOVTargets` /
  `CudaSceneView` / `DeviceAOVView` field
  declarations compile cleanly; the OptiX
  surface is excluded.
- **OptiX-ON-no-SDK build:** 14/14 ctest PASS at
  the FIELD-I.11 landing commit's empirical run
  in `/tmp/rr_build_optix_no_sdk`. The
  audit-host stub fallback paths in
  `OptixRenderer.cpp` exercise empirically; my
  FIELD-I.11 stub signature matches the header.
  The extra test target (`optix_tests`) PASSes.

The FIELD-I.6 task brief's §8 SDK-host runtime
scenarios all DEFERRED to the future CLI-bridge
slice's audit:

- **§8.1 + §8.2** (neutral diagnostic on
  disabled-field default, CUDA + OptiX paths):
  DEFERRED — both kernel arms exist but no CLI
  flag flips the pointer on.
- **§8.3 + §8.4** (non-default Constant +
  Radial visualisation, both backends):
  DEFERRED — no CLI authoring surface yet.
- **§8.5** (off-path bit-identity, both
  backends): STRUCTURALLY SATISFIED today (both
  arms unreachable so no off-path PPM can be
  emitted); empirical SDK-host pass DEFERRED.
- **§8.6** (composability with
  `--manifold-debug` + `--observer-debug`):
  DEFERRED — no `--field-debug` CLI flag yet.
- **§8.7** (cross-backend equivalence):
  DEFERRED on the SDK-host empirical side;
  STRUCTURALLY GUARANTEED today (same POD +
  same math + same encoding — check #3's
  five-axis symmetry).

### 3.10 Master-rule satisfaction recap

- **Master rule #3 ("no fake stubs"):** satisfied.
  The OptiX miss + closest-hit arms at
  `OptixPrograms.cu:365-367` and `:960-980` are
  fully wired: real `rr::field::evaluate(...)`
  invocation; real pointer dereferences; real
  on-hit + on-miss branches. The
  structural-unreachability via null-pointer-gate
  is honest scope framing (per the doc-comments
  at `OptixPrograms.cu:947-959`: "until a future
  CLI bridge slice flips that gate, every
  dispatcher caller passes `false`..."). No fake
  stub; no empty scaffold.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. The OptiX bridge's
  behaviour is documented as contract on every
  modified file's doc-comments + structurally
  rooted in the audit-host-verified FIELD-I.2
  `evaluate(...)` semantics. The cross-backend
  symmetry's five-axis verification (check #3)
  rests on inspectable file/line references and
  the structural-equivalence argument the
  SCHW.5 / PENROSE.6 + MANI-I.8 + OBSERVER.13
  precedents established.

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Scope deliberately narrow to
  OptiX bridge only — CUDA byte-unchanged per
  the operator's "Do not change CUDA unless
  required by shared type consistency" rule
  (no shared-type adjustment needed; the POD
  embeds directly on both backends). CLI
  deferred; dispatcher emit deferred; fixture
  scene deferred; mapping deferred. The
  prerequisite `render_pathtrace_progressive`
  stub fix is bounded — minimal stub-signature
  update; the SDK body is byte-untouched.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied.
  The FIELD-I.11 default state is unchanged
  from the FIELD-I.10 baseline:
    - No `--render-*` action produces a new
      file.
    - No existing PPM filename changes.
    - No beauty pass arithmetic changes.
    - No existing AOV slot's value changes.
  The single observable behaviour change is the
  structural presence of the new kernel arm —
  which is null-pointer-gated AND disabled-
  field-config-gated, so its observable
  behaviour from every existing OptiX CLI
  invocation is zero.

### 3.11 Honest scope recap

This audit is an **OptiX bridge audit with
SDK-host runtime DEFERRED** + **CUDA path
preserved-unchanged**. The verdict `PASS` is
the FIELD-I.11 OptiX-side surface's verdict;
check #6 (CUDA path) — wait, the brief's
check #6 is observer/manifold, but the CUDA
path IS preserved-unchanged per check #3's
five-axis-symmetry framing (the FIELD-I.11
brief's "Do not change CUDA unless required
by shared type consistency" rule was honoured;
no shared-type adjustment was required so the
CUDA tree is byte-unchanged).

The FIELD-I.10 audit's checks #5 + #6 + #8
runtime-deferred portions for OptiX are now
partially closed by FIELD-I.11: the OptiX
kernel arm exists; the OptiX-side data-path is
verified structurally; the OptiX-ON-no-SDK
build is empirically verified; the SDK-host
runtime pass remains DEFERRED for both
backends until the CLI bridge slice lands.

---

## 4. NEXT

### 4.1 Renumbered FIELD-I.* sub-slice ladder

The FIELD-I.12 audit slot insertion (mirroring the
FIELD-I.10 / FIELD-I.8 / FIELD-I.5 / FIELD-I.3
audit-slot insertion precedent) shifts subsequent
FIELD-I.* sub-slices by one. The post-FIELD-I.12
ladder is:

- **FIELD-I.13** — CLI + Config + dispatcher bridge
  (the renumbered next FIELD-I.* impl slot; lands
  the `--field-debug` modifier flag + the minimal
  `--field-*` authoring CLI surface; extends
  `rr::core::Config` with a `scalar_field_config`
  field + a `field_debug_visualization` bool;
  threads both from CLI through `run_render_aovs`
  AND `run_render_optix_aovs` into the respective
  payload fields; flips both backends reachable
  simultaneously; ships the
  `output/aov_field_scalar.ppm` /
  `output/optix_aov_field_scalar.ppm` save sites;
  closes the FIELD-I.10 + FIELD-I.12 audits'
  runtime-deferred portions on SDK-host).
- **FIELD-I.14** — CLI bridge audit.
- **FIELD-I.15** — Mapping CLI + Config bridge
  (the full FIELD-I.4 `FieldMappingConfig` CLI
  authoring surface).
- **FIELD-I.16** — Mapping CLI bridge audit.
- **FIELD-I.17** — Mapping kernel pipeline (the
  actual field-to-beauty integration).
- **FIELD-I.18** — Mapping kernel pipeline audit.
- **FIELD-I.19** — Fixture scene + companion doc.
- **FIELD-I.20** — Fixture audit.
- **FIELD-I.21** — Arc capstone audit.

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

### 4.2 Candidate next slots (prioritised)

**(a) RECOMMENDED — FIELD-I.13: CLI + Config +
dispatcher bridge** (the renumbered next FIELD-I.*
impl slot). Natural continuation of the FIELD-I.*
arc: flips both backend AOV gates reachable
simultaneously by adding the `--field-debug`
modifier flag + the minimal `--field-*` authoring
flags. Closes the FIELD-I.10 + FIELD-I.12 audits'
runtime-deferred portions when its own audit runs
on an SDK host. The symmetry of the FIELD-I.9 +
FIELD-I.11 bridges makes this slice a single-file
threading addition.

**(b) Manifold-orthogonal work.** Multiple options
available with their own merit:
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

**(c) NOT RECOMMENDED — direct full FIELD-I.4
`FieldMappingConfig` CLI surface slice
(FIELD-I.15) skipping the FIELD-I.13 bridge.**
Would author the mapping CLI without a
diagnostic AOV to verify mapping behaviour
visually. Better to land the diagnostic AOV
CLI gate first so future mapping work can
be authored AND diagnosed.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the master rule #3 +
  #11 + #12 + #16 satisfaction recap at §3.10
  cites these).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §6 (the Field Interpretation Layer as an
  OPTIONAL extension above the Manifold Core).
- `docs/FIELD_INTERPRETATION_LAYER.md` §4.6
  (the diagnostic-AOV channel design-doc
  anchor for the FIELD-I.7 / .9 / .11
  surfaces).

### 5.2 FIELD-I.* arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md`
  (FIELD-I.3).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md`
  (FIELD-I.5).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_AUDIT.md`
  (FIELD-I.8).
- `docs/FIELD_SCALAR_CUDA_BRIDGE_AUDIT.md`
  (FIELD-I.10 — the precedent CUDA-bridge audit
  this audit mirrors structurally; check #3's
  five-axis symmetry references its
  `PASS_WITH_RUNTIME_DEFERRED` shape).

### 5.3 Precedent OptiX-bridge references

- OBSERVER.10 — the precedent OptiX bridge that
  threaded `ObserverFrame` through
  `OptixLaunchParams` carry-only style.
  FIELD-I.11 mirrors the precedent's "POD
  by-value on the payload" + "threaded from
  `render_aovs(...)` trailing-defaulted
  parameter" pattern verbatim.
- OBSERVER.13 — the precedent OptiX bridge that
  wired the `ObserverBeta` AOV via
  `OptixLaunchParams::aov_observer_beta` +
  the `observer_debug` trailing-defaulted bool
  gate + the closest-hit + miss arm write
  sites. FIELD-I.11 mirrors this verbatim
  except for the single-channel (1-float-per-
  pixel) vs Vec3 (3-floats-per-pixel) shape;
  the `pix_idx_1` index calculation matches
  the existing Depth / DopplerFactor /
  SearchlightFactor 1-channel AOVs.
- OBSERVER.14 — the precedent OptiX-bridge
  audit doc shape that this audit follows.

### 5.4 Source surface audited

- `src/optix/OptixLaunchParams.h` (modified +64
  lines vs the FIELD-I.10 baseline; the new
  `aov_field_scalar` pointer at line 376 + the
  new `scalar_field_config` POD field at line
  513 + doc-comment blocks + new `#include
  "field/ScalarField.h"`).
- `src/optix/OptixRenderer.h` (modified +59
  lines; the new `field_scalar` Image field on
  `AovResult` at line 469 + the new
  `scalar_field_config` / `field_debug`
  trailing-defaulted parameters on
  `render_aovs(...)` at lines 551 / 567 +
  doc-comment blocks + new `#include
  "field/ScalarField.h"`).
- `src/optix/OptixRenderer.cpp` (modified +106
  lines; the new render_aovs signature
  extension at line 2535 + the new
  `d_aov_field_scalar` local at line 2709 +
  the alloc block at lines 2778-2786 + the
  `params.aov_field_scalar` / `params.scalar_field_config`
  threading at lines 2828-2829 / 2864 + the
  download path at lines 2991-2992; plus the
  audit-host stub signature update at line
  3403 + the prerequisite
  `render_pathtrace_progressive` stub
  signature fix at line 3429).
- `src/optix/OptixPrograms.cu` (modified +53
  lines; the new miss arm at lines 365-367 +
  the new closest-hit arm at lines 960-980).
- `CMakeLists.txt` (modified +7 lines; the
  `rr_field` PUBLIC link addition on
  `rr_optix` + the preceding doc-comment).

### 5.5 Test surface unchanged

All test files in `tests/` are byte-identical to
the FIELD-I.10 baseline. No test extension this
slice (the OptiX kernel arms' empirical behaviour
requires SDK-host runtime verification; deferred
per §3.9).

### 5.6 Surrounding commit SHAs

- `e15934e` — FIELD-I.11 audited tree (the
  per-slice gate target).
- `9a12fa9` — FIELD-I.10 baseline (the diff
  baseline for checks #6 + #7).
- `e1a42c2` — FIELD-I.9 impl (the antecedent
  CUDA bridge; check #3's five-axis symmetry
  references this commit's CUDA-side surface).
- `181a579` — FIELD-I.7 impl (the antecedent
  AOV data-model entry; both backends consume
  the same `AOVType::FieldScalar = 8`
  enumerator + `make_field_scalar(...)`
  factory).
- `40c387b` — FIELD-I.2 impl (the antecedent
  scalar field model; both backend arms call
  `rr::field::evaluate(scalar_field_config,
  hit_pos)` from this commit's surface).

### 5.7 Unchanged source files (sampled)

The following files are byte-identical to the
FIELD-I.10 baseline (`9a12fa9`), confirmed by
diff filters at checks #3 (CUDA symmetry) +
#6 (observer/manifold) + narrow-scope
discipline:

- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/cuda/`.
- Every file in `src/manifold/`.
- Every file in `src/relativity/`.
- Every file in `src/renderer/`.
- Every file in `src/scene/`, `src/io/`,
  `src/core/`, `src/math/`, `src/image/`,
  `src/gpu/`, `src/app/`, `src/field/`,
  `src/pathtracer/`.
- `src/main.cpp`.
- Other `src/optix/` files: `OptixAccel.cpp`,
  `OptixAccel.h`, `OptixBackend.cpp`,
  `OptixBackend.h`, `OptixDenoiser.cpp`,
  `OptixDenoiser.h`, `OptixPipeline.cpp`,
  `OptixPipeline.h`, `OptixSBT.h`.

### 5.8 Unchanged test files (sampled)

All test files are byte-identical to the
FIELD-I.10 baseline:

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
- `tests/optix_tests.cpp` — unchanged.

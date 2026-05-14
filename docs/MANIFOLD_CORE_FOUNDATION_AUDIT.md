# Manifold Core Foundation Audit

Date:    2026-05-14
Branch:  `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `3a70de6` ("docs: Manifold
Integration Plan (docs only)").
Audit host: linux, audit-host build (no CUDA, no OptiX SDK).
Mode: documentation-only. No source code is touched by this
verdict; the result is synthesised purely from the tree's
current state and `git diff main..HEAD`.

This audit is the foundation gate for the Manifold Core
Pivot. It verifies that the eight foundation slices
(architecture doc → MANIFOLD.1-MANIFOLD.7 → FIELD design doc
→ FIELD.1-FIELD.3 → Manifold Integration Plan) are safe,
non-destructive, and leave the existing renderer's pixel
pipeline untouched.

---

## 1. VERDICT

**PASS.**

All eight structural checks return PASS. No REPAIR or
BLOCKED item is found. The Manifold Core Pivot foundation
is safe to extend into the MANI-I.* integration slices
(`docs/MANIFOLD_INTEGRATION_PLAN.md`).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Manifold architecture doc exists                | **PASS** | `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` present, 634 lines. Pinned by commit `671c57f`. |
| 2 | Manifold module exists                          | **PASS** | `src/manifold/` present with six headers (`CoordinateChart.h`, `MetricTensor.h`, `ObserverFrame.h`, `GeodesicState.h`, `ManifoldTransform.h`, `ManifoldMode.h`) + `README.md`, total ~1192 lines. `rr_manifold` INTERFACE library wired in `CMakeLists.txt`. |
| 3 | Default mode is disabled / no-op                | **PASS** | `ManifoldMode{}` has `enabled = false`, `chart = CoordinateChartType::Euclidean`, `strength = 0.0f`, `debug_visualization = false`, `preserve_light_speed_normally = true`, `transform_coordinates_instead_of_light = true` (the "no output change" anchor per MANIFOLD.6). `ManifoldTransform{}` aggregates the chart / metric / observer defaults, which are Euclidean / Minkowski / scene-rest respectively. |
| 4 | Identity tests pass                             | **PASS** | `tests/manifold_identity_tests.cpp` (376 lines, 112 hand-rolled `RR_CHECK` assertions, 8 test groups) ships and is wired into `ctest` as binary #10. Full audit-host `ctest` run: `100% tests passed, 0 tests failed out of 12`. |
| 5 | Field interpretation is optional                | **PASS** | `docs/FIELD_INTERPRETATION_LAYER.md` §1 / §6 / §7 / §8 explicitly declare the layer optional; `src/field/` ships header-only POD types (`FieldType.h`, `ScalarField.h`, `FieldMapping.h`, `FieldInterpreter.h`) that no other module includes. `rr_field` INTERFACE library exists but is not linked into any other target. All shipping defaults (`ManifoldMode::enabled = false`, `FieldInterpreter::enabled = false`, every per-target strength `0.0f`) make the layer a runtime no-op without any opt-out flag. |
| 6 | No renderer behaviour changed                   | **PASS** | `git diff main..HEAD` returns zero files in `src/cuda/`, `src/optix/`, `src/pathtracer/`, `src/renderer/`, `src/gpu/`, `src/scene/`, `src/io/`, `src/main.cpp`, `src/core/`, `src/camera/`, `src/material/`, `src/lighting/`, `src/texture/`, `src/geometry/`, `src/image/`, `src/math/`, or `src/relativity/`. The 12 existing ctest binaries (the pre-pivot 11 plus the new `manifold_identity_tests`) all pass byte-identically to the post-FIELD.3 baseline. |
| 7 | No CUDA / OptiX path changed                    | **PASS** | Sub-case of check 6: zero files modified under `src/cuda/` (eight CUDA kernels and supporting `.cu`/`.cpp` files) or `src/optix/` (OptiX backend + denoiser). The PTX-embed helper in `cmake/EmbedPtxAsHeader.cmake` is untouched. No `OptixLaunchParams` field added; no `__raygen__pathtrace` / `k_pathtrace_sample` signature change. CUDA + OptiX-SDK host runtime invariants are preserved by construction. |
| 8 | No C4D / server / UI / node-editor touched       | **PASS** | Zero files modified under `src/server/`. No `bridges/c4d_bridge/`, `bridges/c4d_native/`, `tools/node_editor/`, or `tools/preview_ui/` directories created (master-order modules #20-#23 / #25 remain unscaffolded per `docs/MASTER_ARCHITECTURE.md` §8). The pre-existing `tools/verify_cuda_host.py` is unchanged. No `.rrscene` schema change, no scene-format version bump. |

---

## 3. REASONING SUMMARY

The Manifold Core Pivot's first 14 commits (architecture
doc + skeleton + MANIFOLD.1-MANIFOLD.7 + FIELD design doc +
FIELD.1-FIELD.3 + integration plan) have collectively
introduced:

- two new architecture / design / planning docs in
  `docs/`: `MANIFOLD_RENDERING_ARCHITECTURE.md` (634
  lines), `FIELD_INTERPRETATION_LAYER.md` (737 lines),
  `MANIFOLD_INTEGRATION_PLAN.md` (907 lines);
- two new header-only INTERFACE libraries in `src/`:
  `rr_manifold` (six headers + README) and `rr_field`
  (four headers + README);
- one new ctest binary (`manifold_identity_tests`) with
  112 assertions across 8 test groups, wired into the
  audit-host `ctest` set as binary #10 of 12.

No source file outside `src/manifold/`, `src/field/`,
`tests/manifold_identity_tests.cpp`, `CMakeLists.txt`, or
`docs/` is modified. The 11 pre-pivot ctest binaries pass
byte-identically; the 12th (manifold_identity_tests)
verifies the default-no-op contract on every field of
every manifold POD plus the SR-bridge round-trip and the
Minkowski null-condition invariants.

The architectural-doc / design-doc / integration-plan
trilogy fixes the contract surface that the future MANI-I.*
slices will land against, with explicit
"never break the current renderer" invariants
(`MANIFOLD_INTEGRATION_PLAN.md` §2) and explicit
"no fake stubs pretending to be complete systems" honesty
(master rule #3 cited throughout). The
`docs/FIELD_INTERPRETATION_LAYER.md` §5 / §7 boundary
between Phase 1 (perceptual / optional / artistic) and
Phase 2 (manifold / required-when-on / physical) is
codified at the type-system level: `rr_field` cannot
depend on `rr_manifold`'s mutable state (only its
read-only surface), and `rr_manifold` cannot depend on
`rr_field` at all (verified by `CMakeLists.txt`'s link
graph).

The integration plan (`MANIFOLD_INTEGRATION_PLAN.md`) is
the **operational** complement to the **structural**
architecture doc: it names the MANI-I.* slices that will
move the renderer from "manifold POD exists but no
consumer" (today) to "Schwarzschild-like + Penrose-like
artistic charts available with bit-identical Euclidean
fallback" (post-MANI-I.8 under the current nine-slice
plan, after the MANI-I.2 / MANI-I.4 per-slice audits and
the MANI-I.5 / MANI-I.6 GPU-side slices land; see
`docs/MANIFOLD_CLI_CONFIG_AUDIT.md` §4,
`docs/MANIFOLD_RENDER_CONFIG_BRIDGE_AUDIT.md` §4, and
the integration plan §3 chain diagram). Each slice
carries its own acceptance gate that includes a
pixel-bit-identity check on at least seven enumerated
CLI actions.

---

## 4. NEXT

The foundation is **safe to extend**. The next concrete
slice the operator may prompt for is, per
`docs/MANIFOLD_INTEGRATION_PLAN.md` §4:

- **MANI-I.1 — CLI config only.** Adds
  `--manifold-mode` / `--manifold-strength` /
  `--manifold-debug-warp` flags populating a host-side
  `rr::manifold::ManifoldMode`; `cli_tests` gains parser
  assertions; no `RenderSettings` or GPU touch.
  Bit-identity invariant: every pre-pivot CLI action
  without any `--manifold-*` flag produces pixel-bit-
  identical output.

Alternative paths the operator may take, if MANI-I.1 is
deprioritised:

- Continue the Field Interpretation Layer's data-model
  slices (e.g. a `VectorField` / `TensorField` /
  `CurvatureField` data-model promotion mirroring FIELD.2
  / FIELD.3). These do not require any MANI-I.* slice.
- Run a CUDA + OptiX-SDK host pass through the full 12-
  binary ctest set to pin the post-foundation runtime
  baseline before MANI-I.* changes the GPU path. The
  audit-host build cannot do this; the pre-pivot
  `docs/STAGE_19_DENOISER_AUDIT.md` Q1 / Q2 deferred
  gates remain the canonical runtime-baseline pinning
  procedure.

No REPAIR action is required. No BLOCKED item is
outstanding.

# Manifold Euclidean GPU Identity Audit (MANI-I.6)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `a34e265` ("manifold:
MANI-I.5 — Euclidean Identity GPU Path (impl,
GPU-plumb-only)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the post-MANI-I.4
baseline, the `cli_tests` binary's runtime output, and
`ctest` exit codes.

This audit is the per-slice gate for the MANI-I.5
Euclidean Identity GPU Path (`a34e265`). It verifies the
eight items the task brief enumerates — GPU-side payload
landed; disabled mode is no-op; Euclidean chart is
identity; CUDA path visually unchanged by default; OptiX
path visually unchanged by default; build / test green;
runtime CUDA / OptiX status documented; verdict — and
produces the PASS / REPAIR / BLOCKED verdict that gates
progression to the debug coordinate-warp AOV (renumbered
MANI-I.7; see §4).

---

## 1. VERDICT

**PASS (structural). Runtime CUDA / OptiX verification
DEFERRED behind the audit-host gate.**

All eight checks return PASS structurally; the two
visual-output checks (#4 CUDA, #5 OptiX) report PASS
because the bit-identity invariant is **structurally
guaranteed** — no kernel arm reads the new
`manifold_mode` field, so the existing per-pixel
arithmetic cannot diverge from the pre-MANI-I.5
baseline. The CUDA + OptiX-SDK runtime re-verification
(byte-cmp of every pre-MANI-I.5 reference PPM) is
DEFERRED behind the audit host's existing
no-CUDA / no-OptiX-SDK fallback, same gate the
`firefly_clamp` / `enable_nee` byte-identity claims sit
behind.

No REPAIR or BLOCKED item is found. The operator may
proceed to the debug coordinate-warp AOV (MANI-I.7,
renumbered from the original MANI-I.6 per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | GPU-side manifold payload exists if needed   | **PASS** | OptiX side: `OptixLaunchParams::manifold_mode{}` appended at the end of the launch-params POD (`src/optix/OptixLaunchParams.h`); host populates `params.manifold_mode = manifold_mode` in `OptixRenderer::render_pathtrace_progressive`. CUDA side: `launch_pathtrace_sample(...)` in `src/cuda/CudaPathTracer.cuh` gains a trailing `rr::manifold::ManifoldMode manifold_mode = {}` parameter (default = disabled); the launcher's function-body signature accepts the parameter with `[[maybe_unused]]`. Both backends thus carry the mode to the device-facing surface; neither backend's kernel arm consumes it (the `is_active(...)` guard is reachable from device code but not called this slice). |
| 2 | Disabled mode is no-op                       | **PASS** | `ManifoldMode{}.enabled == false` (preserved unchanged from MANIFOLD.6). The new `rr::manifold::is_active(const ManifoldMode& m)` helper returns `m.enabled && m.chart != CoordinateChartType::Euclidean`; with `enabled = false` the first conjunct short-circuits and the helper returns `false`. No kernel arm calls the helper this slice, so even the structural guard is unreached — but its truth table is verified analytically and by the standalone runtime check the MANI-I.5 commit message documented. |
| 3 | Euclidean chart is identity                  | **PASS** | `ManifoldMode{}.chart == CoordinateChartType::Euclidean` (preserved unchanged from MANIFOLD.6). `is_active(m)` returns `false` for `m.chart == Euclidean` regardless of the `enabled` bit — the second conjunct `m.chart != Euclidean` short-circuits. So even with `--manifold-enable --manifold-chart euclidean`, the structural guard reports "not active" and the existing pre-pivot kernel code path runs unchanged. |
| 4 | CUDA path remains visually unchanged by default | **PASS (structural).** | `grep manifold src/cuda/CudaPathTracer.cu` returns 3 hits, all inside the `launch_pathtrace_sample` function signature (lines 421 / 422 / 427 — comment + parameter declaration). The `k_pathtrace_sample` __global__ kernel body (lines 178-405) contains **zero** `manifold` references. The CUDA kernel arithmetic is byte-identical to the pre-MANI-I.5 baseline. Runtime byte-cmp of `--render-pathtrace`'s output PPMs is DEFERRED behind the audit host's no-CUDA fallback (same gate as the existing `firefly_clamp` / `enable_nee` byte-identity claims). |
| 5 | OptiX path remains visually unchanged by default | **PASS (structural).** | `grep manifold src/optix/OptixPrograms.cu` returns **zero** hits. The OptiX device-side programs (`__raygen__pathtrace`, `__closesthit__pathtrace`, `__miss__pathtrace`, every other entry point) do not reference `optixLaunchParams.manifold_mode`. The host-side `OptixRenderer.cpp` populates the field; the device-side never reads it. OptiX per-pixel arithmetic is byte-identical to the pre-MANI-I.5 baseline. Runtime byte-cmp of `--render-optix-pathtrace`'s output PPMs is DEFERRED behind the audit host's no-OptiX-SDK fallback. |
| 6 | Build / test status                          | **PASS** | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. Full `ctest`: `100% tests passed, 0 tests failed out of 12` — the same twelve binaries that were green at the post-MANI-I.4 baseline. `cli_tests` reports `cli_tests: 123/123 passed` (unchanged — MANI-I.5 doesn't touch the parser). The `rr_optix → rr_manifold` PUBLIC INTERFACE link pulls the manifold header into the audit-host `rr_optix` build (it builds even when OptiX SDK is absent — header-only INTERFACE; no SDK symbol needed). |
| 7 | Runtime CUDA / OptiX status                  | **DEFERRED** | The bit-identity invariant for checks #4 and #5 is **structurally guaranteed** (the kernel arms do not read `manifold_mode`). Direct runtime verification requires a CUDA + OptiX-SDK host: re-run every pre-MANI-I.5 reference PPM under `--render-pathtrace` (CUDA), `--render-optix-pathtrace` (OptiX), `--render-scene`, `--render-mesh-scene`, `--render-material-scene`, `--render-direct-lighting`, `--render-aovs`, `--render-relativistic`, `--render-aovs --denoise`, both with and without the four `--manifold-*` flags, and `cmp`-byte-identical against the pre-MANI-I.5 PPMs. The audit-host build has neither backend at runtime; the deferral is documented in `docs/STAGE_19_DENOISER_AUDIT.md` Q1 / Q2's "runtime GPU validation is a documented deferred gate" rubric. **No new BLOCKED item; the deferral matches the existing `firefly_clamp` / `enable_nee` byte-identity gate's posture.** |
| 8 | PASS / REPAIR / BLOCKED verdict              | **PASS** | All six structural checks return PASS; the runtime CUDA / OptiX check is DEFERRED behind a documented audit-host gate, not BLOCKED. The slice's "what does NOT ship" list (in the integration plan §6 and the BUILD_PLAN MANI-I.5 entry) is exhaustive across the four "no" rules from the task brief (no Schwarzschild / Kruskal / Penrose / Kerr math, no coordinate warp, no debug AOV, no C4D / server / UI / node-editor touch). The bit-identity invariant is structurally guaranteed by the kernel arms' continued ignorance of `manifold_mode`. The slice is **safe to extend**. |

---

## 3. REASONING SUMMARY

The MANI-I.5 commit (`a34e265`) introduces:

- a `rr::manifold::ManifoldMode manifold_mode{}` field on
  `rr::optix::OptixLaunchParams` (appended at the end of
  the POD; default-constructed disabled / Euclidean /
  strength 0 / debug off);
- a trailing `rr::manifold::ManifoldMode manifold_mode =
  {}` parameter on `OptixRenderer::render_pathtrace_-
  progressive(...)`; the host populates the launch-params
  field;
- a trailing `rr::manifold::ManifoldMode manifold_mode =
  {}` parameter on `launch_pathtrace_sample(...)` in
  `src/cuda/CudaPathTracer.cuh`; the launcher's
  function-body signature accepts the parameter as
  `[[maybe_unused]]` — the CUDA kernel does not consume
  the field this slice;
- a `RR_HD inline bool is_active(const ManifoldMode&)`
  helper in `src/manifold/ManifoldMode.h` returning
  `m.enabled && m.chart != CoordinateChartType::Euclidean`
  — the single guard MANI-I.7+ slices flip when they
  wire per-chart logic;
- host-side wiring in `src/main.cpp` and
  `src/pathtracer/PathTracer.cpp` that threads
  `cfg.manifold` through to both backends' launchers;
- a `target_link_libraries(rr_optix PUBLIC rr_manifold)`
  CMake addition so consumers of `rr_optix` see the
  manifold header transitively.

No file outside `src/main.cpp`, `src/manifold/`,
`src/optix/`, `src/cuda/`, `src/pathtracer/`,
`CMakeLists.txt`, and `docs/` is touched. The CUDA
kernel `k_pathtrace_sample`'s body (lines 178-405 of
`CudaPathTracer.cu`) contains zero `manifold`
references. The OptiX device-side programs
(`OptixPrograms.cu`) contain zero `manifold` references.
Both backends' per-pixel arithmetic is byte-identical
to the pre-MANI-I.5 baseline structurally.

The bit-identity invariant the integration plan §2
requires (every existing CLI action without any
`--manifold-*` flag produces pixel-bit-identical output
to the pre-pivot baseline) is **structurally
guaranteed**: there is no device-side code path through
which `manifold_mode`'s value could affect the kernel's
output. The audit-host's runtime cannot directly verify
the bit-identity (no CUDA, no OptiX SDK); a CUDA +
OptiX-SDK host would re-verify by running every
pre-MANI-I.5 reference image and `cmp`-ing the output.
That runtime gate is the same one the existing
`firefly_clamp` / `enable_nee` log lines and byte-
identity claims sit behind, and is deferred to the
final cross-host audit (MANI-I.10 under the renumbered
integration plan §11).

The integration plan §6 "Implementation choice notes"
subsection (added at the MANI-I.5 commit) explicitly
documents the FP-byte-identity reason for NOT inserting
a kernel-side `transform_ray_like_direction(...)` call
on the Euclidean default this slice: even on a
unit-length input with `scale = 1.0f`, the IEEE-754
`sqrt + multiply` chain can drift by one ULP per
component, which would break the bit-identity invariant
on the existing reference images. MANI-I.7+ slices that
introduce curved-chart logic will place the call
*inside* an `is_active(mode)` guard so the Euclidean /
disabled fast path stays bit-exact.

---

## 4. NEXT

The slice is **safe to extend**. The integration plan's
slice numbering needs another one-step shift to absorb
this audit slot:

- **MANI-I.1** — CLI config only (LANDED).
- **MANI-I.2** — CLI Config Audit (LANDED).
- **MANI-I.3** — Render Config Bridge (LANDED).
- **MANI-I.4** — Render Config Bridge Audit (LANDED).
- **MANI-I.5** — Euclidean Identity GPU Path (LANDED).
- **MANI-I.6** — **THIS AUDIT** (Euclidean Identity GPU
  Path Audit, doc-only).
- **MANI-I.7** — debug coordinate-warp AOV (was
  MANI-I.6).
- **MANI-I.8** — Schwarzschild-like artistic coordinate
  remap (was MANI-I.7).
- **MANI-I.9** — Penrose-like compactification
  visualisation (was MANI-I.8).
- **MANI-I.10** — final cross-host audit (was
  MANI-I.9); merge gate for the whole MANI-I.*
  programme; absorbs the runtime CUDA / OptiX
  byte-identity gate this audit defers.

The integration plan §3 chain diagram and §7-§11 slice
sections are updated as part of this MANI-I.6 commit so
the per-slice numbering stays coherent. The
`MANIFOLD_INTEGRATION_PLAN.md` §11 non-goals and §12
references sections are unchanged. The two cross-
references in `MANIFOLD_CORE_FOUNDATION_AUDIT.md` and
`MANIFOLD_RENDERING_ARCHITECTURE.md` that point at the
final-audit slice number are updated to `MANI-I.10`.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator may
prompt for is **MANI-I.7 — debug coordinate-warp AOV**
per the renumbered integration plan §7 (introduces the
`ManifoldWarp` AOV slot that writes per-pixel chart-
space hit position; on the Euclidean chart the AOV is
identically the world-space hit position — visual
sanity check for the future curved-chart slices;
`--render-aovs` becomes capable of emitting
`manifold_warp.ppm` when `--manifold-debug-warp` (or
the renamed `--manifold-debug` from MANI-I.1) is set).

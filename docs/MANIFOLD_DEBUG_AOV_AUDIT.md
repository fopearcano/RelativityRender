# Manifold Debug AOV Audit (MANI-I.9)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `094306f` ("renderer:
MANI-I.8 — Manifold Debug AOV Implementation (impl,
CUDA-full + OptiX-kernel-only)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the post-MANI-I.6
baseline, the running `RelativityRender` executable's
runtime output, `cli_tests` / `renderer_tests` runtime
output, and `ctest` exit codes.

This audit is the per-slice gate for the MANI-I.8
Manifold Debug AOV Implementation (`094306f`). It
verifies the eight items the task brief enumerates —
AOV exists; beauty output unchanged by default; identity
/ neutral diagnostic on disabled / Euclidean; AOV
generation is optional; CUDA path status; OptiX path
status; runtime status; verdict — and produces the
PASS / REPAIR / BLOCKED verdict that gates progression
to the first curved-chart slice (renumbered MANI-I.10
Schwarzschild-like; see §4).

---

## 1. VERDICT

**PASS (structural).** The CUDA path is fully wired
end-to-end and the OptiX path's kernel arms are wired
but the OptiX host-side `render_aovs` allocation is
DEFERRED to a follow-up slice (documented). The
beauty-output bit-identity invariant is structurally
guaranteed by the AOV slot's null-gate. Runtime CUDA
pixel-value verification is DEFERRED behind the audit
host's no-CUDA / no-OptiX-SDK fallback.

No REPAIR item; one DEFERRED item (OptiX host-side
allocation); no BLOCKED item. The operator may proceed
to the first curved-chart slice (MANI-I.10
Schwarzschild-like, renumbered from the original
MANI-I.8 per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Manifold debug AOV exists                    | **PASS** | `src/renderer/AOV.h:75` declares `AOVType::ManifoldCoordinates = 6` appended at the end of the `AOVType` enum (preserves every pre-MANI-I.8 enumerator value). `src/renderer/AOV.cpp:15` returns `3` for `aov_component_count(ManifoldCoordinates)`; `src/renderer/AOV.cpp:32` returns `"manifold_coordinates"` for `aov_type_name(ManifoldCoordinates)`; `src/renderer/AOV.cpp:90` ships the `AOV::make_manifold_coordinates(std::string name)` factory. The five structural assertions in `tests/renderer_tests.cpp` cover enumerator value, component count, type name, default-name factory output, and custom-name factory output — all pass under `renderer_tests: 19 / 19 passed`. |
| 2 | Beauty output unchanged by default           | **PASS** | The CUDA kernel arm (`CudaTestKernel.cu:591`) is appended at the end of the AOV-write block, after the existing six AOV writes. It is gated on `scene.aovs.manifold_coordinates != nullptr`. The dispatchers that do not request the new AOV (the default state of every `--render-aovs` invocation that does not pass `--manifold-debug`, and every non-AOV CLI action) leave the pointer at its default `nullptr` and the arm short-circuits before any write. The Beauty pass arithmetic in `k_render_scene` is byte-identical to the pre-MANI-I.8 baseline structurally — no instruction in the Beauty pass code path was modified. |
| 3 | Disabled / Euclidean diagnostic is neutral / identity | **PASS** | When the AOV pointer IS populated (i.e. `--render-aovs --manifold-debug` is in effect), the CUDA kernel arm at `CudaTestKernel.cu:591-602` writes `best.position.x / .y / .z` on hit (world-space hit position) and `(0, 0, 0)` on miss. This is the documented identity / neutral diagnostic from `docs/MANIFOLD_DEBUG_AOV_TASK.md` §2.3 / §3.1: on the Euclidean chart with `scale = 1.0` and `origin = (0, 0, 0)`, `world_to_chart(transform, world_hit) == world_hit`, so the chart-space hit position equals the world-space hit position. The OptiX closest-hit arm (`OptixPrograms.cu:732-746`) computes the same value via `optixGetWorldRayOrigin() + optixGetRayTmax() * optixGetWorldRayDirection()`. No curved-chart math is shipped this slice (per the task brief's "No Schwarzschild/Penrose/Kerr behavior yet" rule); the value is identity / neutral for *every* chart selection because the chart-aware `world_to_chart` is not called yet. |
| 4 | AOV generation is optional                   | **PASS** | Two layers of opt-in:<br>**(a) Host-side allocation gate**: `src/main.cpp:3801` allocates the 7th `GpuAOVBuffer` ONLY when `cfg.manifold.debug_visualization == true`. Without the gate, `manifold_coords_buffer.resize(...)` is not called and the buffer's `device_ptr()` is `nullptr` (default `GpuAOVBuffer` state). The host then sets `targets.manifold_coordinates = cfg.manifold.debug_visualization ? buffer.device_ptr() : nullptr;` (`src/main.cpp:3820`) so the device pointer is `nullptr` unless the gate is on.<br>**(b) Device-side null-gate**: the CUDA kernel arm and both OptiX kernel arms check the pointer for `nullptr` before writing. When the pointer is null, the arm short-circuits with no write. This makes the AOV opt-in at *both* the allocation level (no wasted device memory when not requested) and the write level (no wasted instructions when the pointer is null).<br>The two-flag composition `--render-aovs --manifold-debug` is the only entry point that satisfies both gates. |
| 5 | CUDA path status                             | **PASS (full plumb)** | The CUDA write path is wired end-to-end: AOV data model (enumerator + helpers + factory) → `DeviceAOVView::manifold_coordinates` (`CudaAOV.cuh:79`) → `CudaRenderer::AOVTargets::manifold_coordinates` (`CudaRenderer.h:170`) → `CudaRenderer::render_scene_with_aovs` wiring (`CudaRenderer.cu`: `view.aovs.manifold_coordinates = targets.manifold_coordinates;`) → kernel write arm (`CudaTestKernel.cu:591`) → host-side allocation + save in `run_render_aovs` (`main.cpp:3799-3866`). Output filename: `output/aov_manifold_coordinates.ppm` (matches the existing `output/aov_<lowercase>.ppm` convention). |
| 6 | OptiX path status                            | **PASS (kernel arms only; host-side allocation DEFERRED)** | OptiX kernel arms wired: `OptixLaunchParams::aov_manifold_coordinates` field at `OptixLaunchParams.h:331` (appended at the end of the launch-params POD; preserves every pre-MANI-I.8 field's offset); closest-hit write arm at `OptixPrograms.cu:732-746` (computes world-space hit position via `optixGetWorldRayOrigin() + optixGetRayTmax() * optixGetWorldRayDirection()` and writes 3 floats per pixel); miss write arm at `OptixPrograms.cu:309-313` (writes `(0, 0, 0)` matching the Normal AOV's miss convention). Both arms null-gated. `grep -c "aov_manifold_coordinates" src/optix/OptixRenderer.cpp` returns `0` — the OptiX host-side `render_aovs` dispatcher does NOT allocate the new AOV's device buffer this slice. The field stays `nullptr` at runtime in the OptiX path; the kernel arms short-circuit; the OptiX path's pre-MANI-I.8 output is byte-identical. The host-side allocation is a small follow-up (allocate buffer + thread pointer + download + save); deferred to a subsequent commit (potentially the upcoming Schwarzschild-like slice or its prep). |
| 7 | Runtime status                               | **DEFERRED** | Runtime CUDA verification of `output/aov_manifold_coordinates.ppm`'s per-pixel content requires a CUDA host. The audit host has no CUDA, no OptiX SDK; `--render-aovs --manifold-debug` exits with the documented "requires CUDA" error before reaching the kernel. The deferred check is: (a) on a CUDA host, run `--render-aovs --manifold-debug` against the built-in scene fixture; (b) verify `output/aov_manifold_coordinates.ppm` exists with size `width * height * 3 floats` worth of data; (c) decode the PPM and confirm per-pixel values match the world-space hit positions for at least one known hit pixel to within `1.0e-5f`. The deferral matches the existing audit-host runtime-deferral rubric (per `docs/STAGE_19_DENOISER_AUDIT.md` Q1 / Q2). The OptiX path's runtime verification is double-deferred: first the host-side allocation must land, then a CUDA + OptiX-SDK host can re-verify. **No new BLOCKED item; the deferrals match the existing per-slice posture.** |
| 8 | PASS / REPAIR / BLOCKED verdict              | **PASS** | All six structural checks return PASS. The OptiX host-side allocation deferral is documented in the BUILD_PLAN MANI-I.8 entry and in the integration plan §7's "Risks & mitigations (LANDED)" subsection — it is a known follow-up, not a regression. The runtime verification deferral matches the prior audits' posture. The slice is **safe to extend**. |

---

## 3. REASONING SUMMARY

The MANI-I.8 commit (`094306f`) introduces:

- a `AOVType::ManifoldCoordinates = 6` enumerator (data
  model);
- helpers and factory for the new type
  (`aov_component_count`, `aov_type_name`,
  `AOV::make_manifold_coordinates`);
- `DeviceAOVView::manifold_coordinates` and
  `CudaRenderer::AOVTargets::manifold_coordinates`
  pointer fields with the null-gated write arm in
  `k_render_scene`;
- `OptixLaunchParams::aov_manifold_coordinates` field
  with closest-hit + miss kernel arms in
  `OptixPrograms.cu`;
- host-side allocation + save in `run_render_aovs`
  gated on `cfg.manifold.debug_visualization`;
- three new test functions in `renderer_tests.cpp`
  covering the data-model surface.

12 files modified (11 source / data-model + 1 test). The
CUDA write path is fully wired end-to-end and emits
`output/aov_manifold_coordinates.ppm` when
`--render-aovs --manifold-debug` is in effect. The OptiX
kernel arms are wired but the OptiX host-side
`render_aovs` dispatcher does not allocate the new
buffer this slice; the OptiX path's pre-MANI-I.8 output
is byte-identical structurally because the field stays
`nullptr` at runtime.

The bit-identity invariant the integration plan §2
requires (every existing CLI action without
`--manifold-debug` produces pixel-bit-identical output
to the pre-MANI-I.8 baseline) is **structurally
guaranteed**:

- The CUDA kernel arm's gate (`scene.aovs.manifold_coordinates
  != nullptr`) short-circuits when the pointer is null.
- The host-side allocator (`run_render_aovs`) sets the
  pointer to null when `cfg.manifold.debug_visualization`
  is false.
- The OptiX path's kernel arms are similarly null-gated;
  the OptiX host-side dispatcher does not populate the
  pointer.
- The Beauty pass arithmetic in `k_render_scene` and
  in the OptiX closest-hit / miss programs is
  byte-identical to the pre-MANI-I.8 baseline
  (`grep manifold_coordinates` returns zero hits in the
  Beauty-write code paths; the new arm sits below the
  six existing AOV write arms and writes to a separate
  buffer).

The audit-host's runtime cannot directly verify the
AOV's pixel content (no CUDA / no OptiX SDK); a CUDA
host can re-verify by:

```
RelativityRender --render-aovs --manifold-debug
                 scenes/test_full_scene.rrscene
```

and `cmp`-ing every pre-MANI-I.8 reference PPM
(`output/aov_beauty.ppm` etc.) against pinned
goldens — those should be byte-identical — plus
decoding `output/aov_manifold_coordinates.ppm` and
verifying its per-pixel values match the world-space
hit positions for at least one known hit pixel.

The OptiX host-side allocation deferral is a small
follow-up: in `OptixRenderer::render_aovs`, allocate
`d_aov_manifold` analogous to `d_aov_beauty` etc., set
`params.aov_manifold_coordinates =
static_cast<float*>(d_aov_manifold)`, add a manifold
slot to `AovResult`, download + return. Then update
`run_render_optix_aovs` to save the PPM when
`cfg.manifold.debug_visualization` is true. The slice
is small and lands when the operator prompts for it.

---

## 4. NEXT

The slice is **safe to extend**. The integration plan's
slice numbering needs another shift to absorb this
audit slot AND the implicit MANI-I.7 task-def /
MANI-I.8 impl split:

- **MANI-I.1** — CLI config only (LANDED).
- **MANI-I.2** — CLI Config Audit (LANDED).
- **MANI-I.3** — Render Config Bridge (LANDED).
- **MANI-I.4** — Render Config Bridge Audit (LANDED).
- **MANI-I.5** — Euclidean Identity GPU Path (LANDED).
- **MANI-I.6** — Euclidean Identity GPU Path Audit
  (LANDED).
- **MANI-I.7** — Manifold Debug AOV Task Definition
  (LANDED).
- **MANI-I.8** — Manifold Debug AOV Implementation
  (LANDED).
- **MANI-I.9** — **THIS AUDIT** (Manifold Debug AOV
  Audit, doc-only).
- **MANI-I.10** — Schwarzschild-like artistic
  coordinate remap (was MANI-I.8 in the plan after
  MANI-I.6 audit; shifted +2 by the MANI-I.7 task-def
  split + this MANI-I.9 audit insertion).
- **MANI-I.11** — Penrose-like compactification
  visualisation (was MANI-I.9 → +2 → MANI-I.11).
- **MANI-I.12** — final cross-host audit (was
  MANI-I.10 → +2 → MANI-I.12); merge gate for the
  whole MANI-I.* programme; absorbs the runtime
  CUDA / OptiX byte-identity gate this and prior
  audits defer.

Note the +2 shift on the last three slots: MANI-I.7
(Task Def) inserted a new slot for the debug AOV's
prep work, and this MANI-I.9 audit inserts another
slot. The MANI-I.8 Implementation occupies the slot
that was originally MANI-I.7-Debug-AOV (single slice).
Net effect: the curved-chart slices shift by +2 from
their MANI-I.6-era numbering.

The integration plan §3 chain diagram, §7 "What ships"
block, and §8/§9/§10 slice section headings are
updated as part of this MANI-I.9 commit to reflect
the new MANI-I.10 / MANI-I.11 / MANI-I.12 labels and
the inserted MANI-I.8 / MANI-I.9 boxes. The two
cross-references in `MANIFOLD_CORE_FOUNDATION_AUDIT.md`
and `MANIFOLD_RENDERING_ARCHITECTURE.md` that point at
the final-audit slice number are updated to `MANI-I.12`.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator may
prompt for is **MANI-I.10 — Schwarzschild-like
artistic coordinate remap** per the renumbered
integration plan §8 (first non-trivial chart;
closed-form artistic remap — NOT physical
Schwarzschild — that bends primary-ray directions
around a configured "mass" centre).

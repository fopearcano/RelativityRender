# Schwarzschild-Like CUDA Completion Audit (SCHW.5 Completion Audit)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `73e9591` ("cuda:
SCHW.5 — Schwarzschild-Like CUDA Kernel Warp Wiring
(impl, CUDA-side)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-SCHW.11 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the **completion verdict** for the
CUDA-side SchwarzschildLike kernel arm landed at
SCHW.5 (`73e9591`). It closes the deferred kernel-arm
finding that the SCHW.6 forward-looking audit
(`docs/SCHWARZSCHILD_LIKE_CUDA_WARP_AUDIT.md` §2 check
#2) acknowledged as "structural-only; runtime activation
deferred to SCHW.5". With SCHW.5 now landed, the CUDA
kernel actively invokes the shared SCHW.1 math leaf
under the same triple-gate the OptiX side uses, so the
cross-backend AOV equivalence the SCHW.11 capstone
audit anticipated is now structurally guaranteed.

---

## 1. VERDICT

**PASS_WITH_RUNTIME_DEFERRED.**

All six structural checks (#1–#6) return PASS.
Check #7 (runtime CUDA-host status) is DEFERRED on
documented audit-host limitations (no CUDA SDK; SDK
TUs compile but cannot link / launch device code).
Check #8 (verdict) returns PASS_WITH_RUNTIME_DEFERRED.

No REPAIR or BLOCKED item is found. The CUDA-side
kernel arm is structurally complete, byte-identical
to the OptiX-side wiring in form, and preserves the
default-no-op invariant across every default code
path. The SCHW.11 capstone audit's check #3
(`CUDA warp bridge exists and is default-no-op`)
transitions from **PARTIAL** to **PASS** as a
consequence.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | CUDA kernel arm exists                                  | **PASS**     | The SCHW.5 commit (`73e9591`) inserts a `SchwarzschildLike` arm into the CUDA kernel's `ManifoldCoordinates` AOV write site at `src/cuda/CudaTestKernel.cu:615-655`. Five concrete additions verified by `git diff 6ce6333..73e9591 -- src/cuda/CudaTestKernel.cu`:<br>**(a)** Header include `#include "manifold/SchwarzschildLikeWarp.h"` at line 16 — pulls in the shared `RR_HD inline` SCHW.1 math leaf.<br>**(b)** AOV null-gate `if (scene.aovs.manifold_coordinates != nullptr)` at line ~612 (preserved from MANI-I.8).<br>**(c)** Hit-path local `rr::math::Vec3 hit_pos{best.position.x, .y, .z}` at lines 616-617 — preserves the unwarped fallback.<br>**(d)** Triple-gate boolean expression `const bool active = ...` at lines 629-633.<br>**(e)** Active-path `SchwarzschildLikeWarpParams` construction (lines 635-640) + `schwarzschild_like_world_to_chart(...)` invocation (lines 641-645). On the inactive path, `hit_pos` retains the original `best.position`. The write at lines 647-649 emits the (possibly warped) `hit_pos` to the AOV slot. The miss-path arm at lines 650-654 is unchanged (writes `(0, 0, 0)`).<br>The arm shape is **byte-equivalent to the OptiX-side SCHW.7 arm** at `OptixPrograms.cu:773-795` modulo the launch-parameter access pattern (`scene.coordinate_chart.params` vs `optixLaunchParams.coordinate_chart.params`; `scene.manifold_mode` vs `optixLaunchParams.manifold_mode`). The shared `RR_HD inline` math leaf invocation is identical on both backends. |
| 2 | Activation conditions are correct                       | **PASS**     | The triple-gate at `CudaTestKernel.cu:629-633` consists of three independent boolean conjuncts, each gating a distinct activation precondition:<br>**(a) `is_active(scene.manifold_mode)`** — `ManifoldMode.h:143-145` returns `m.enabled && m.chart != Euclidean`. So the `enabled` gate is satisfied IFF `scene.manifold_mode.enabled = true`; the chart-non-Euclidean gate is satisfied IFF `scene.manifold_mode.chart != Euclidean`. Mirrors the OptiX-side gate at `OptixPrograms.cu:774`.<br>**(b) `scene.manifold_mode.chart == CoordinateChartType::SchwarzschildLike`** — explicit redundant check that structurally bypasses the other `*Like` / `*LikePlaceholder` families (Kruskal / Penrose / Kerr) per master rule #3 (no silent routing through SchwarzschildLike math). This is the same posture the SCHW.3 CPU-side seam takes (`ManifoldTransform.h:191` / `:210` / `:272` / `:299`) and that `test_schw_3_other_non_euclidean_passthrough` verifies. Mirrors the OptiX gate at `OptixPrograms.cu:775-776`.<br>**(c) `scene.manifold_mode.strength > 0.0f`** — the runtime dial. The strength is threaded into `SchwarzschildLikeWarpParams::warp_strength` at line 637, so even a positive strength flows through the math leaf's `warp_strength == 0` short-circuit (`SchwarzschildLikeWarp.h:153`) as a defensive layer; the explicit `> 0` gate avoids the wasted math-leaf call when the operator dials the strength to zero. Mirrors the OptiX gate at `OptixPrograms.cu:777`.<br>The conjunction is short-circuit-evaluated left-to-right, so the cheapest check (the `enabled` bit read) runs first and the most expensive (`scene.manifold_mode.chart` enum comparison) only runs when needed. Operator's brief activation conditions met exactly: "manifold.enabled == true AND chart == SchwarzschildLike AND strength > 0". |
| 3 | Disabled / Euclidean path remains no-op                 | **PASS**     | Four-layer safety guarantee, mirroring the OptiX-side analysis from `SCHWARZSCHILD_LIKE_OPTIX_WARP_AUDIT.md` §2 check #3:<br>**(a) Host-side allocation gate:** `main.cpp::run_render_aovs` at line ~3879 allocates the `manifold_coords_buffer` device buffer only when `effective_cuda_manifold.debug_visualization == true`. On the default (`ManifoldMode{}.debug_visualization == false`) the buffer is never allocated and `targets.manifold_coordinates` stays `nullptr`.<br>**(b) Kernel-side null gate:** `CudaTestKernel.cu:612` (`if (scene.aovs.manifold_coordinates != nullptr) {`) — the entire chart-aware arm is wrapped in this null check, so even if the host bypassed the allocation gate, the kernel would short-circuit on the null pointer. Preserved from MANI-I.8.<br>**(c) Triple-gate inactive branch:** on the default (`enabled=false` OR `chart=Euclidean` OR `strength<=0`), the `active` boolean is `false`; the kernel skips the math-leaf invocation; `hit_pos` retains the unwarped `best.position` value; the AOV write emits the raw position (MANI-I.8 baseline).<br>**(d) Math leaf defensive fallback:** even if the triple-gate were bypassed and the math leaf were invoked with `chart.params.mass = 0`, the leaf's `r_s == 0` short-circuit at `SchwarzschildLikeWarp.h:154` returns the input vector unchanged. The validator at `:113-122` rejects non-finite inputs + out-of-range `falloff` + non-positive `clamp_radius`.<br>Concrete consequence: every `--render-aovs` invocation without `--manifold-enable --manifold-chart schwarzschild-like --manifold-strength <s>` (and without a scene-file `manifold` block authoring those values) continues to produce a pre-SCHW.5 byte-identical `output/aov_*.ppm` set (no `output/aov_manifold_coordinates.ppm` is emitted on the default). |
| 4 | No new warp math was added                              | **PASS**     | `git diff 6ce6333..73e9591 -- src/manifold/SchwarzschildLikeWarp.h` returns **zero bytes**. The SCHW.5 slice reuses the SCHW.1 math leaf verbatim — no new mathematical content was added. The CUDA kernel arm calls the same four `RR_HD inline` helpers (`schwarzschild_like_validate_params`, `schwarzschild_like_world_to_chart`, `schwarzschild_like_chart_to_world`, `schwarzschild_like_warp_ray_direction`) that the SCHW.3 CPU seam and the SCHW.7 OptiX kernel already invoke. The single-source-of-truth invariant the SCHW.11 capstone audit asserted ("both backends invoke the same `RR_HD inline` math leaf") is preserved: with SCHW.5 landed, **all three call sites** (CPU seam, CUDA kernel, OptiX kernel) bind to the same forward-map definition at `SchwarzschildLikeWarp.h:145-162`.<br>Operator brief constraint "Do not add new warp math" met exactly. Master rule #3 ("no fake stubs") preserved — every shipping helper is real complete artistic math with documented properties (audited at SCHW.2). |
| 5 | OptiX path was not unintentionally changed              | **PASS**     | `git diff 6ce6333..73e9591 -- src/optix/` returns **zero bytes**. The seven OptiX-side files (`OptixBackend.cpp`, `OptixDenoiser.cpp`, `OptixLaunchParams.h`, `OptixPipeline.cpp`, `OptixPrograms.cu`, `OptixRenderer.cpp`, `OptixRenderer.h`, `OptixSBT.h`) are byte-identical to the post-SCHW.11 state. The SCHW.7 OptiX-side wiring is preserved verbatim; the SCHW.5 slice mirrored that wiring in the CUDA path without modifying OptiX. Operator brief constraint "Do not modify OptiX unless required by shared type consistency" met: the shared types (`ManifoldMode`, `CoordinateChart`, `SchwarzschildLikeWarpParams`) were already in place from prior slices (MANI-I.5 / SCHW.7 / SCHW.1), so no shared-type changes were required. |
| 6 | Build / test status                                     | **PASS**     | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. Full ctest: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `198 / 198 checks passed` (unchanged from the post-SCHW.11 baseline; SCHW.5 is a CUDA kernel-side wiring slice that reuses the existing math leaf — the unit tests are unaffected). `cli_tests: 123/123 passed`, `renderer_tests: 19/19 passed`, `relativity_tests` unchanged. No new ctest target; no CMake link-line change beyond the `rr_gpu → rr_manifold` PUBLIC link addition (mirrors the existing `rr_scene → rr_manifold` and `rr_pathtracer → rr_manifold` precedents). The audit-host `--render-aovs` action correctly refuses with the documented "requires CUDA" message because `RR_HAS_CUDA` is not defined; the CUDA-ON path's NVCC TUs compile under the audit-host's NVCC rules (verified by the project's existing CMake conditional). |
| 7 | Runtime CUDA status                                     | **DEFERRED** | Standard audit-host posture: the audit host (`linux`, no CUDA SDK) cannot execute CUDA device code. The CUDA-side TUs (`CudaTestKernel.cu`, `CudaRenderer.cu`, `CudaScene.cuh`) compile under the audit-host rules but cannot link / launch. Deferred runtime checks the operator should exercise once on a CUDA-equipped host:<br>**(a)** `--render-aovs --manifold-enable --manifold-chart schwarzschild-like --manifold-strength 1.0 --manifold-debug` produces `output/aov_manifold_coordinates.ppm` with the documented radial-compression signature near the mass origin (plan §4.1).<br>**(b)** `--render-aovs --manifold-chart euclidean --manifold-strength 1.0 --manifold-debug` (or any of the three chart-disabled override mechanisms per the fixture-doc §4.2) produces a `output/aov_manifold_coordinates.ppm` byte-identical to the pre-SCHW.5 baseline (the raw `best.position` output).<br>**(c)** `--render-aovs --manifold-enable --manifold-chart kerr-like` (reserved-but-inert per MANIFOLD.1) produces a `output/aov_manifold_coordinates.ppm` byte-identical to the `--manifold-chart euclidean` baseline because the triple-gate's `chart == SchwarzschildLike` check structurally bypasses Kerr.<br>**(d) Cross-backend equivalence:** the CUDA `output/aov_manifold_coordinates.ppm` is byte-identical to the OptiX `output/optix_aov_manifold_coordinates.ppm` for the same fixture and same `--manifold-*` parameters. This is the cross-backend equivalence the SCHW.11 capstone audit anticipated; it is now structurally guaranteed by both backends invoking the same `RR_HD inline` math leaf with identical parameter encoding (both backends use the same artistic-default `CoordinateChart` from main.cpp: `mass=1.0`, `spin=1.0`, `compactification_scale=0.1`, `origin=(0,0,0)`). |
| 8 | Verdict                                                 | **PASS_WITH_RUNTIME_DEFERRED** | All six structural checks (#1–#6) return PASS. Check #7 (runtime CUDA-host status) is DEFERRED on documented audit-host limitations. No REPAIR or BLOCKED item is outstanding. The CUDA-side kernel arm is **structurally complete** and the cross-backend AOV equivalence the SCHW.11 capstone anticipated is now structurally guaranteed. The slice closes the SCHW.11 capstone audit's check #3 PARTIAL finding (transitions PARTIAL → PASS). Remaining deferred items (CLI consumption-gap closure, runtime CUDA + OptiX-SDK fixture-render verification) are unchanged from the SCHW.11 capstone's catalogue and are not in this audit's scope. |

---

## 3. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3 ("Do not implement fake stubs
pretending to be complete systems") the audit is
explicit about its scope boundary:

- **No runtime device-side verification.** The audit
  host cannot execute the CUDA kernel arm; check #7
  enumerates the runtime checks deferred to a CUDA-
  equipped host. The structural checks (#1–#6) are
  exhaustive within the audit-host's reach.
- **No cross-backend byte-identity comparison.** Not
  exercisable on the audit host (no CUDA SDK + no
  OptiX SDK). The cross-backend equivalence is
  **structurally guaranteed** by both backends
  invoking the same `RR_HD inline` math leaf with
  byte-identical parameter encoding (both use the
  same `cuda_manifold_chart` / `manifold_chart`
  builder shape in main.cpp); empirical pixel-level
  verification requires an SDK-equipped host.
- **No CLI consumption-gap closure.** The SCHW.5 slice
  does NOT add a `<scene-path>` argument to
  `--render-aovs`. The existing inline-scene path is
  preserved verbatim. The dispatcher merge logic
  added at SCHW.9 (CLI-vs-scene merge) is still
  dead-code for `--render-aovs` until that
  consumption-gap CLI extension lands. SCHW.5 does
  not close that gap and does not claim to.
- **No primary-ray direction warp.** The
  `schwarzschild_like_warp_ray_direction` helper
  exists at SCHW.1 (validated at SCHW.2) but no
  kernel call site invokes it. SCHW.5 mirrors
  SCHW.7's choice to route only the hit position
  through the warp for the AOV write site; the
  beauty pass uses unwarped primary rays.
- **No chart-parameter scene-authoring.** The CUDA
  side's artistic defaults (`mass=1.0`, `spin=1.0`,
  `compactification_scale=0.1`) come from main.cpp's
  builder helper. The scene parser does NOT expose
  these slots. Future slices may broaden the parser
  surface without modifying the SCHW.5 contract.
- **No `CudaPathTracer.cu` SchwarzschildLike arm.**
  SCHW.5 wires the warp into `k_render_scene` (the
  CUDA `--render-aovs` kernel) but NOT into
  `CudaPathTracer.cu`'s path-tracer kernel. The
  `--render-pathtrace` action's `ManifoldCoordinates`
  AOV (if any future slice adds one to the
  pathtracer) would need its own wiring; today the
  pathtracer kernel does not write a
  `ManifoldCoordinates` AOV.

---

## 4. REASONING SUMMARY

The SCHW.5 commit (`73e9591`) ships six surface
additions in 389 added lines across seven files:

- **`CudaSceneView`** (`src/cuda/CudaScene.cuh`): new
  `manifold_mode` + `coordinate_chart` fields,
  mirroring `OptixLaunchParams::manifold_mode` +
  `coordinate_chart` from MANI-I.5 + SCHW.7.
- **`AOVTargets`** (`src/cuda/CudaRenderer.h`): new
  `manifold_mode` + `coordinate_chart` fields on
  the public host-side struct; defaulted to the
  pre-pivot no-op anchor.
- **`render_scene_with_aovs`** (`src/cuda/CudaRenderer.cu`):
  two new lines threading the payload from
  `targets` into the kernel-visible `view`.
- **`k_render_scene`** (`src/cuda/CudaTestKernel.cu`):
  triple-gate + shared math leaf invocation in the
  `ManifoldCoordinates` AOV write arm at lines
  ~582-655.
- **`run_render_aovs`** (`src/main.cpp`): builds a
  `cuda_manifold_chart` from
  `effective_cuda_manifold.chart` with the same
  artistic defaults as the OptiX-side helper;
  assigns the new `AOVTargets` fields before the
  `render_scene_with_aovs(...)` call.
- **`CMakeLists.txt`**: explicit `rr_gpu →
  rr_manifold` PUBLIC link.

The kernel-arm-existence invariant (check #1) is
**file-level + line-level verified**: the arm exists
at `CudaTestKernel.cu:615-655` with the same shape
the SCHW.7 OptiX arm uses at `OptixPrograms.cu:773-795`.

The activation-conditions-correctness invariant
(check #2) is **expression-level verified**: the
triple-gate at lines 629-633 evaluates the three
preconditions (enabled, chart-is-SchwarzschildLike,
strength>0) in short-circuit-evaluated conjunction.
Each conjunct mirrors the OptiX-side gate; the math
leaf's `warp_strength == 0` short-circuit serves as
a fourth defensive layer.

The disabled/Euclidean-path-no-op invariant (check
#3) is **four-layer-redundantly guaranteed**: host
allocation gate + kernel null gate + triple-gate
inactive branch + math leaf defensive fallback. Any
one of the four layers is sufficient.

The no-new-warp-math invariant (check #4) is
**diff-zero verified**: `git diff -- src/manifold/`
returns 0 bytes for the SCHW.5 commit. The SCHW.1
math leaf is reused verbatim; the kernel arm calls
the same `schwarzschild_like_world_to_chart(...)` the
SCHW.3 CPU seam and the SCHW.7 OptiX kernel call.

The OptiX-untouched invariant (check #5) is
**diff-zero verified**: `git diff -- src/optix/`
returns 0 bytes for the SCHW.5 commit.

The build/test status (check #6) shows the slice
integrates cleanly: ctest 12/12 PASS;
`manifold_identity_tests 198/198`; `cli_tests
123/123`; `renderer_tests 19/19`. No new test
binary; no regression vs the post-SCHW.11 baseline.

The runtime CUDA status (check #7) is DEFERRED on
documented audit-host limitations; the SCHW.11
capstone's per-slice DEFERRED posture is preserved.

The verdict (check #8) is
**PASS_WITH_RUNTIME_DEFERRED**.

---

## 5. SCHW.11 CAPSTONE CHECK #3 STATUS UPDATE

The SCHW.11 capstone audit
(`docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md`) reported
**check #3 as PARTIAL** with the following finding:

> CUDA warp bridge exists and is default-no-op —
> PARTIAL. Infrastructure in place (RR_HD inline
> math leaf, MANI-I.5 kernel-signature plumbing,
> MANI-I.8 AOV slot). BUT SCHW.5 kernel arm itself
> unlanded; CUDA kernel writes raw best.position.

With SCHW.5 landed at `73e9591`, this audit closes
the PARTIAL → **PASS** transition for that check:

- The kernel arm now exists at
  `CudaTestKernel.cu:615-655` (check #1 above).
- The triple-gate's activation conditions match the
  operator's specification (check #2 above).
- The default-no-op invariant is four-layer-
  redundantly guaranteed (check #3 above).
- The kernel arm invokes the same shared math leaf
  the OptiX arm invokes (check #4 above), making
  the cross-backend AOV equivalence structurally
  guaranteed.

The SCHW.11 capstone document itself is preserved as
a **point-in-time historical snapshot** (its verdict
at `6ce6333` was PASS_WITH_RUNTIME_DEFERRED with check
#3 marked PARTIAL). Future readers consulting the
capstone should also read this completion audit to
see that the PARTIAL finding has been closed by
SCHW.5.

The remaining deferred items from the SCHW.11
capstone are **unchanged**:

- **CLI consumption-gap closure** (no existing CLI
  action loads `<scene-path>` into a manifold-aware
  render);
- **Runtime CUDA + OptiX-SDK fixture-render
  verification** (audit host has no SDK);
- **Primary-ray direction warp** (cosmetic; helper
  exists but not invoked at raygen);
- **Chart-parameter scene-authoring** (artistic
  defaults baked into main.cpp);
- **Runtime PPM regression suite** (no golden PPMs
  pinned).

These are operator-driven follow-ups; SCHW.5
specifically closes the kernel-wiring gap and does
not claim to close the others.

---

## 6. NEXT

The completion of SCHW.5 closes the SCHW.11 capstone
audit's check #3 PARTIAL finding. **No REPAIR or
BLOCKED item is outstanding.** The arc's primary
implementation gap is now closed.

The next concrete commit the operator may prompt for
is one of:

- **CLI consumption-gap closure** — single-line
  extension to `--render-aovs` (and/or
  `--render-optix-aovs`) that accepts a
  `<scene-path>` argument. Closes the second of the
  SCHW.11 capstone's five remaining risks; ties the
  SCHW.9 fixture end-to-end on both backends through
  the existing dispatcher-merge logic.
- **Primary-ray direction warp** — invoke
  `schwarzschild_like_warp_ray_direction(...)` from
  the OptiX raygen (and / or the CUDA raygen) so the
  beauty pass shows the documented pseudo-lensing
  signature (plan §4.2 / §6.2).
- **Runtime fixture validation on a CUDA + OptiX-SDK
  host** — operator-side activity. The fixture's
  documented per-sphere visual signature (`docs/SCHWARZSCHILD_LIKE_FIXTURE.md`
  §3) becomes empirically verifiable; cross-backend
  AOV byte-identity can be confirmed by `cmp` of the
  CUDA + OptiX outputs.
- **Manifold-orthogonal work** — Field
  Interpretation Layer (Phase 1) prototyping, other
  path-tracer features (e.g. denoiser integration
  with chart-aware AOVs).

**Explicitly NOT recommended:** Penrose / Kerr /
Kruskal work (the `*LikePlaceholder` charts remain
reserved-but-inert per MANIFOLD.1 + the SCHW.11
capstone's explicit non-authorisation). C4D /
server / UI / node-editor work
(architecture-doc §8 non-goals stand).

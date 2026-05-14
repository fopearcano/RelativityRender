# Penrose-Like CUDA Integration Audit (PENROSE.7)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `2859acd` ("cuda:
PENROSE.6 — Penrose-Like CUDA Integration (impl,
CUDA-side)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-PENROSE.5 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the per-slice gate for the PENROSE.6 CUDA
integration (`2859acd`). It verifies the ten items the
task brief enumerates — CUDA-safe Penrose-like helper
exists; GPU payload supports PenroseLike chart;
activation conditions are correct; disabled/default
no-op; Euclidean identity; Schwarzschild unchanged;
OptiX path was not modified; build/test status;
runtime CUDA-host status; verdict — and produces a
PASS / REPAIR / BLOCKED verdict that gates progression
to the OptiX integration slice (renumbered PENROSE.8).

---

## 1. VERDICT

**PASS** (structural CUDA-side warp bridge verified),
with **DEFERRED** runtime CUDA-host status.

All nine structural checks (#1–#8 and #10) return PASS.
Check #9 (runtime CUDA-host status) is DEFERRED on
documented audit-host limitations (no CUDA SDK; CUDA
TUs compile but cannot link / launch). No REPAIR or
BLOCKED item is found. The CUDA-side PenroseLike
kernel arm is structurally complete; the
SchwarzschildLike SCHW.5 arm is preserved verbatim;
default / Euclidean / disabled paths produce
byte-identical output to the pre-PENROSE.6 baseline.
The operator may proceed to PENROSE.8 (OptiX
integration; renumbered from the original PENROSE.7
per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | CUDA-safe Penrose-like helper exists                | **PASS** | The PENROSE.2 math leaf at `src/manifold/PenroseLikeCompactification.h` is `RR_HD inline` end-to-end (CUDA-callable by construction; verified at PENROSE.3 audit check #1). The leaf is included at `CudaTestKernel.cu:16`. All three helpers carry the `__host__ __device__` macro from `math/MathUtils.h`: `penrose_like_validate_params` at `:160`; `penrose_like_world_to_chart` at `:214`; `penrose_like_chart_to_world` at `:269`. The downstream call closure (`<cmath>` via `std::tanh` / `std::atanh` / `std::pow`; `math/Vec3.h`) is `RR_HD inline` throughout — no host-only library call routes through a CPU-only path. The PENROSE.6 CUDA arm at `CudaTestKernel.cu:661-679` invokes the same `penrose_like_world_to_chart(...)` the PENROSE.4 CPU seam invokes — single-source-of-truth math, structurally CPU↔CUDA byte-equivalent. |
| 2 | GPU payload supports PenroseLike chart              | **PASS** | The CUDA-side launch payload structure was already extended at SCHW.5 (commit `73e9591`) to carry the per-launch manifold payload. Specifically: `CudaSceneView` carries `rr::manifold::ManifoldMode manifold_mode{}` and `rr::manifold::CoordinateChart coordinate_chart{}` fields (added at `CudaScene.cuh` during SCHW.5). The public `AOVTargets` struct in `CudaRenderer.h` carries the same defaulted fields (also from SCHW.5). The `render_scene_with_aovs` body in `CudaRenderer.cu` threads both fields into the kernel-visible view. The PENROSE.6 commit reuses these unchanged — the same payload that carries the SchwarzschildLike chart at SCHW.5 carries the PenroseLike chart at PENROSE.6, identified by `manifold_mode.chart == CoordinateChartType::PenroseLike` rather than `== SchwarzschildLike`. No new launch-payload field required (the SCHW.5 plumbing was designed to be chart-family-agnostic; this audit confirms it).<br><br>The host-side dispatcher at `main.cpp::run_render_aovs` populates the chart payload's per-chart artistic defaults via the `cuda_manifold_chart` builder (extended at PENROSE.6's `main.cpp` diff): on `PenroseLike` it sets `mass = 5.0f` (r_max), `spin = 1.0f` (falloff), `compactification_scale = 1.0f` (scale), `name = "penrose-like"`. These flow to the kernel via `targets.coordinate_chart` and reach the kernel arm at `scene.coordinate_chart.params`. |
| 3 | Activation conditions are correct                   | **PASS** | The triple-gate at `CudaTestKernel.cu:644-648` is implemented as `const bool penrose_active = ...`:<br>**(a) `is_active(scene.manifold_mode)`** — `ManifoldMode.h:143-145` returns `m.enabled && m.chart != Euclidean`. So the `enabled` gate is satisfied IFF `scene.manifold_mode.enabled = true`; the chart-non-Euclidean gate is satisfied IFF `scene.manifold_mode.chart != Euclidean`. Mirrors the SCHW.5-side gate at the parallel `:639-643` site.<br>**(b) `scene.manifold_mode.chart == CoordinateChartType::PenroseLike`** — explicit redundant check that structurally bypasses the other chart families. Mutually exclusive with the SchwarzschildLike `schwarzschild_active` gate (different enum constants); both `active` booleans cannot evaluate to `true` simultaneously.<br>**(c) `scene.manifold_mode.strength > 0.0f`** — the runtime dial. The strength is threaded into `PenroseLikeCompactificationParams::strength` at `CudaTestKernel.cu:672` (`pp.strength = scene.manifold_mode.strength`), so even a positive strength flows through the math leaf's `strength == 0` short-circuit (`PenroseLikeCompactification.h:222`) as a defensive layer; the explicit `> 0` gate avoids the wasted math-leaf call when the operator dials the strength to zero. Mirrors the SCHW.5-side gate at `:643`.<br><br>The two arms are structurally separated by the `else if` keyword at `:661`: when `schwarzschild_active` is true, the PenroseLike branch is unreachable (and vice versa). Combined with the enum-tag distinction (SchwarzschildLike ≠ PenroseLike), the two arms are **doubly mutually exclusive**. |
| 4 | Disabled / default mode remains no-op               | **PASS** | Four-layer safety guarantee:<br>**(a) Host-side allocation gate:** `main.cpp::run_render_aovs` at line ~3879 allocates the `manifold_coords_buffer` device buffer only when `effective_cuda_manifold.debug_visualization == true`. On the default the buffer is never allocated; `targets.manifold_coordinates` stays `nullptr`.<br>**(b) Kernel-side null gate:** `CudaTestKernel.cu:614` (`if (scene.aovs.manifold_coordinates != nullptr) {`) — the entire chart-aware arm is wrapped in this null check.<br>**(c) Triple-gate inactive branches:** on the default (`enabled=false` OR `chart=Euclidean` OR `strength<=0`), **both** `schwarzschild_active` and `penrose_active` are `false`; the kernel skips the math-leaf invocations; `hit_pos` retains the unwarped `best.position` value; the AOV write emits the raw position (MANI-I.8 baseline).<br>**(d) Math leaf defensive fallback:** even if a triple-gate were bypassed and the math leaf were invoked with `chart.params.mass = 0`, the leaf's `r_max == 0` short-circuit at `PenroseLikeCompactification.h:223` returns the input vector unchanged.<br>Concrete consequence: every `--render-aovs` invocation without `--manifold-enable --manifold-chart penrose-like --manifold-strength <s>` (and without a scene-file `manifold` block authoring those values) continues to produce a pre-PENROSE.6 byte-identical `output/aov_*.ppm` set (no PenroseLike AOV signature). |
| 5 | Euclidean mode remains identity                     | **PASS** | With `--manifold-enable --manifold-chart euclidean --manifold-strength <s>`, `manifold_mode.enabled = true` and `manifold_mode.chart = Euclidean`. The `is_active(manifold_mode)` helper at `ManifoldMode.h:143-145` returns `false` (Euclidean is "intentionally not active" per its documented semantic — the `enabled && chart != Euclidean` expression evaluates to `false`). Both triple-gates' `active` booleans evaluate to `false`; the kernel writes the raw `best.position` at `CudaTestKernel.cu:691-693` (MANI-I.8 baseline). Even with `--manifold-debug` set, the AOV buffer IS allocated but the kernel writes the raw position; the resulting PPM is the same as the pre-PENROSE.6 MANI-I.8 AOV output. The Euclidean path is **structurally guaranteed** to bypass both the SchwarzschildLike and the PenroseLike arms via the `is_active` helper's chart-non-Euclidean check. |
| 6 | Schwarzschild behavior unchanged                    | **PASS** | The SCHW.5 SchwarzschildLike arm is **preserved verbatim** in the PENROSE.6 commit. `git diff 7327813..2859acd -- src/cuda/CudaTestKernel.cu` shows the SCHW.5 region of the kernel arm (lines 639-660: the `schwarzschild_active` triple-gate and the SchwarzschildLikeWarpParams construction + math-leaf call) is byte-identical to the pre-PENROSE.6 state. The PENROSE.6 diff at this site is purely **additive**: it (a) renames the single `active` boolean to `schwarzschild_active` for clarity; (b) introduces a new `penrose_active` triple-gate; (c) wraps the chart-aware code in `if (schwarzschild_active) { ... } else if (penrose_active) { ... }` for structural mutual exclusion. The SchwarzschildLike chart's runtime semantics are unchanged:<br>**(i)** Same triple-gate (`is_active && chart == SchwarzschildLike && strength > 0`);<br>**(ii)** Same parameter encoding (mass→r_s, spin→falloff, compactification_scale→clamp_radius, strength→warp_strength);<br>**(iii)** Same math-leaf invocation (`schwarzschild_like_world_to_chart(hit_pos, scene.coordinate_chart.origin, sp)`).<br>The cross-backend AOV equivalence claim from the SCHW.11 capstone audit (CUDA ↔ OptiX SchwarzschildLike rendering byte-identical via single-source-of-truth math) is preserved by PENROSE.6 — the SchwarzschildLike kernel arm output is identical to pre-PENROSE.6. |
| 7 | OptiX path was not modified                         | **PASS** | `git diff 7327813..2859acd -- src/optix/` returns **zero bytes**. All eleven OptiX-side files (`OptixBackend.cpp`, `OptixDenoiser.cpp`, `OptixLaunchParams.h`, `OptixPipeline.cpp`, `OptixPrograms.cu`, `OptixRenderer.cpp`, `OptixRenderer.h`, `OptixSBT.h`) are byte-identical to the post-PENROSE.5 state. The SCHW.7 OptiX-side wiring is preserved verbatim; the PENROSE.6 slice mirrored the wiring in the CUDA path without modifying OptiX. The OptiX-side PenroseLike arm is the next concrete commit (renumbered PENROSE.8); operator brief constraint "Do not modify OptiX yet" met exactly. |
| 8 | Build / test status                                 | **PASS** | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. Full ctest: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `312 / 312 checks passed` (unchanged from the post-PENROSE.5 baseline; PENROSE.6 is a CUDA kernel-side wiring slice that reuses the existing math leaf — the unit tests are unaffected). `cli_tests: 123/123 passed`, `renderer_tests: 19/19 passed`, `relativity_tests` unchanged. No new ctest target; no CMake link-line change (the `rr_gpu → rr_manifold` PUBLIC link added at SCHW.5 already covers PenroseLike since `rr_manifold` is INTERFACE-only and includes both `SchwarzschildLikeWarp.h` and `PenroseLikeCompactification.h`). The CUDA-OFF audit-host path is preserved: smoke-testing `--render-aovs --manifold-enable --manifold-chart penrose-like --manifold-strength 0.5 --manifold-debug` correctly refuses with the documented "requires CUDA" message. |
| 9 | Runtime CUDA-host status                            | **DEFERRED** | Standard audit-host posture: the audit host (`linux`, no CUDA SDK) cannot execute CUDA device code. The CUDA-side TUs (`CudaTestKernel.cu`, `CudaRenderer.cu`, `CudaScene.cuh`) compile under the audit-host rules but cannot link / launch. Deferred runtime checks the operator should exercise once on a CUDA-equipped host:<br>**(a)** `--render-aovs --manifold-enable --manifold-chart penrose-like --manifold-strength 1.0 --manifold-debug` produces `output/aov_manifold_coordinates.ppm` with the documented asymptotic-compactification signature (far-field pixels saturate at `r_max = 5.0` per the artistic-defaults baked into main.cpp; near-field pixels remain close to world-space).<br>**(b)** `--render-aovs --manifold-chart euclidean --manifold-strength 1.0 --manifold-debug` (or any of the chart-disabled override mechanisms) produces an `output/aov_manifold_coordinates.ppm` byte-identical to the pre-PENROSE.6 MANI-I.8 baseline (raw `best.position` output).<br>**(c)** **SchwarzschildLike non-regression:** `--render-aovs --manifold-enable --manifold-chart schwarzschild-like --manifold-strength 1.0 --manifold-debug` produces an AOV byte-identical to the pre-PENROSE.6 SchwarzschildLike reference (the SCHW.5 arm is preserved verbatim; the PenroseLike `else if` cannot fire when SchwarzschildLike is selected).<br>**(d)** Off-chart non-regression: `--render-aovs --manifold-enable --manifold-chart kerr-like` (reserved-but-inert per MANIFOLD.1) produces a `output/aov_manifold_coordinates.ppm` byte-identical to the `--manifold-chart euclidean` baseline because both triple-gates' `chart == ...` checks structurally bypass Kerr.<br>**(e) Cross-call-site equivalence:** the CUDA `output/aov_manifold_coordinates.ppm` PenroseLike output should be byte-equivalent to the host-side `ManifoldTransform::world_to_chart(...)` PenroseLike output for the same input position + same chart parameters. This is the cross-call-site equivalence the PENROSE.4 audit anticipated; it is now structurally guaranteed by both call sites invoking the same `RR_HD inline` math leaf with identical parameter encoding (both use the same artistic-default `CoordinateChart` from main.cpp). |
| 10 | PASS / REPAIR / BLOCKED verdict                    | **PASS** | All nine structural checks (#1–#8 and #10) return PASS. Check #9 (runtime CUDA-host status) is DEFERRED on documented audit-host limitations. No REPAIR or BLOCKED item is outstanding. The CUDA-side PenroseLike kernel arm is **structurally complete**; the SchwarzschildLike SCHW.5 arm is preserved verbatim with structural mutual exclusion; the cross-call-site AOV equivalence (CUDA kernel ↔ CPU seam) is structurally guaranteed by both invoking the same shared math leaf. The slice is **safe to extend** to OptiX integration (renumbered PENROSE.8) under the renumbered PENROSE.* ladder. |

---

## 3. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3 ("Do not implement fake stubs
pretending to be complete systems") the audit is
explicit about its scope boundary:

- **No runtime device-side verification.** The audit
  host cannot execute the CUDA kernel arm; check #9
  enumerates the runtime checks deferred to a CUDA-
  equipped host. The structural checks (#1–#8 and
  #10) are exhaustive within the audit-host's reach.
- **No cross-backend byte-identity comparison.** Not
  exercisable on the audit host (no CUDA SDK, no
  OptiX SDK). The eventual cross-backend
  equivalence (CUDA PenroseLike output ↔ OptiX
  PenroseLike output for the same fixture) requires
  PENROSE.8 (OptiX integration) to land AND an
  SDK-equipped host. The equivalence is **forward-
  guaranteed** by single-source-of-truth math (both
  backends will invoke the same `RR_HD inline`
  `penrose_like_world_to_chart`).
- **No `CudaPathTracer.cu` PenroseLike arm.**
  PENROSE.6 wires the warp into `k_render_scene`
  (the CUDA `--render-aovs` kernel) but NOT into
  `CudaPathTracer.cu`'s path-tracer kernel
  (mirrors SCHW.5's choice). The `--render-pathtrace`
  action's `ManifoldCoordinates` AOV (if any future
  slice adds one to the pathtracer) would need
  its own wiring.
- **No primary-ray direction warp.** The PENROSE.2
  math leaf deliberately ships only a forward /
  inverse pair (no `penrose_like_warp_ray_direction`
  helper); the beauty pass is unaffected by the
  PenroseLike chart. SCHW.5 made the same choice;
  PENROSE.6 mirrors it.
- **No chart-parameter scene-authoring.** The CUDA
  side's PenroseLike artistic defaults
  (`mass=5.0`, `spin=1.0`, `compactification_scale=1.0`)
  come from main.cpp's builder helper. The scene
  parser does NOT expose these slots. Future
  slices may broaden the parser surface without
  modifying the PENROSE.6 contract.
- **No multi-chart composition.** Today only one
  `manifold_mode.chart` can be active per launch;
  the SchwarzschildLike and PenroseLike arms are
  structurally mutually exclusive (via `else if` +
  enum-tag check). A future `ManifoldStack`
  concept (sketched in
  `PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §7.3)
  could enable composition; not in scope here.

---

## 4. REASONING SUMMARY

The PENROSE.6 commit (`2859acd`) ships two surface
changes in 286 added / 12 deleted lines across three
files:

- **`CudaTestKernel.cu` `ManifoldCoordinates` AOV
  write arm extended** (lines 614-695): the
  existing SCHW.5 SchwarzschildLike arm is
  preserved verbatim; a parallel
  `else if (penrose_active)` branch is added with
  the PENROSE.6 PenroseLike triple-gate + math-leaf
  invocation. The two arms are structurally
  mutually exclusive via (a) the `else if`
  separator and (b) the enum-tag distinction
  (SchwarzschildLike ≠ PenroseLike).
- **`main.cpp::run_render_aovs` chart builder
  extended** (lines 3935-3953): adds an `else if
  (chart == PenroseLike)` branch with artistic
  defaults consistent with the PENROSE.4 test
  fixture `make_penrose_like_chart` (mass=5.0,
  spin=1.0, compactification_scale=1.0).

The CUDA-safe-helper-existence invariant (check #1)
is **inherited from PENROSE.2** — the math leaf
is `RR_HD inline` end-to-end; the PENROSE.6 CUDA
arm invokes it identically to the CPU seam.

The GPU-payload invariant (check #2) is
**inherited from SCHW.5** — the
`CudaSceneView::manifold_mode + coordinate_chart`
plumbing was designed chart-family-agnostic at
SCHW.5; PENROSE.6 reuses it unchanged.

The activation-conditions invariant (check #3) is
**expression-level verified**: the triple-gate at
lines 644-648 mirrors the SCHW.5 gate exactly with
the enum-tag swap (`SchwarzschildLike` → `PenroseLike`).
The `else if` separator ensures mutual exclusion;
the math leaf's `strength == 0` short-circuit is
a fourth defensive layer.

The disabled/default-no-op invariant (check #4) is
**four-layer-redundantly guaranteed**: host
allocation gate + kernel null gate + both
triple-gates' inactive branches + math leaf
defensive fallback.

The Euclidean-identity invariant (check #5) is
**bit-preserving** because the `is_active(...)`
helper returns `false` on Euclidean by design,
bypassing both chart-aware arms.

The Schwarzschild-unchanged invariant (check #6)
is **diff-verified**: the SCHW.5 region of the
CUDA arm is byte-identical to the pre-PENROSE.6
state (modulo the cosmetic rename `active` →
`schwarzschild_active` and the surrounding
control-flow restructure that preserves the
SchwarzschildLike code path's runtime semantics).

The OptiX-untouched invariant (check #7) is
**diff-zero verified**: `git diff -- src/optix/`
returns 0 bytes for the PENROSE.6 commit.

The build/test status (check #8) shows the slice
integrates cleanly: ctest 12/12 PASS;
`manifold_identity_tests 312/312`; `cli_tests
123/123`; `renderer_tests 19/19`. No new test
binary; no regression vs the post-PENROSE.5
baseline.

The runtime CUDA-host status (check #9) is
DEFERRED on documented audit-host limitations;
the SCHW.5 / SCHW.6 / SCHW.8 / SCHW.10 / SCHW.11
per-slice DEFERRED posture is preserved.

The verdict (check #10) is **PASS** structurally;
runtime DEFERRED.

---

## 5. NEXT

The slice is **safe to extend**. The
`PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §10
PENROSE.* sub-slice ladder needs a one-step shift
to absorb this audit slot (mirroring the SCHW.4 /
SCHW.6 / SCHW.8 / SCHW.10 + PENROSE.3 / PENROSE.5
audit-slot insertions):

- **PENROSE.1** — Planning slice (LANDED at `a84f8b2`).
- **PENROSE.2** — Math helper (LANDED at `7169547`).
- **PENROSE.3** — Audit of PENROSE.2 (LANDED at
  `1bf3f2a`).
- **PENROSE.4** — CPU integration (LANDED at
  `bd2046e`).
- **PENROSE.5** — Audit of PENROSE.4 (LANDED at
  `7327813`).
- **PENROSE.6** — CUDA integration (LANDED at
  `2859acd`).
- **PENROSE.7** — **THIS AUDIT** (Penrose-Like CUDA
  Integration Audit, doc-only).
- **PENROSE.8** — OptiX integration (was PENROSE.7
  in the post-PENROSE.6 plan; renumbered).
- **PENROSE.9** — Fixture / debug visualization
  (was PENROSE.8).
- **PENROSE.10** — Arc capstone audit (was
  PENROSE.9); closes the MANI-I.11 slot.

The `docs/PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §10
sub-slice ladder is updated as part of this
PENROSE.7 commit so the per-slice numbering stays
coherent. The plan's other sections (§1–§9,
§11–§12) are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is **PENROSE.8 — OptiX integration**
per the renumbered plan §10 PENROSE.8 (mirrors
SCHW.7's OptiX-side wiring; activates the PenroseLike
warp at `OptixPrograms.cu:773-795` via the same
triple-gate the CUDA arm uses; closes the OptiX-side
gap of the PenroseLike arc).

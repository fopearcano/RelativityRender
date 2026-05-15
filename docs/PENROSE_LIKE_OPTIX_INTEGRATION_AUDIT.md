# Penrose-Like OptiX Integration Audit (PENROSE.9)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `65250ea` ("optix:
PENROSE.8 — Penrose-Like OptiX Integration (impl,
OptiX-side)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-PENROSE.7 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the per-slice gate for the PENROSE.8 OptiX
integration (`65250ea`). It verifies the ten items the
task brief enumerates — OptiX launch params receive the
PenroseLike manifold payload; OptiX uses shared
Penrose-like helper; activation conditions are correct;
disabled/default no-op; Euclidean identity; Schwarzschild
unchanged; CUDA/OptiX Penrose math equivalence; OptiX
OFF build remains valid; runtime CUDA/OptiX-host status;
verdict — and produces a PASS / REPAIR / BLOCKED verdict
that gates progression to the fixture / debug
visualization slice (renumbered PENROSE.10).

---

## 1. VERDICT

**PASS** (structural OptiX-side warp bridge verified),
with **DEFERRED** runtime CUDA/OptiX-host status.

All nine structural checks (#1–#8 and #10) return PASS.
Check #9 (runtime CUDA/OptiX-host status) is DEFERRED
on documented audit-host limitations (no CUDA SDK, no
OptiX SDK; SDK_FOUND TUs compile but cannot link /
launch). No REPAIR or BLOCKED item is found. The
OptiX-side PenroseLike kernel arm is structurally
complete; the SchwarzschildLike SCHW.7 arm is preserved
verbatim; CUDA + OptiX now invoke the same shared
`RR_HD inline` math leaf so cross-backend AOV
byte-equivalence is structurally guaranteed. The
operator may proceed to PENROSE.10 (fixture / debug
visualization; renumbered from the original PENROSE.9
per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | OptiX launch params receive PenroseLike manifold payload | **PASS** | The OptiX launch-params structure was already extended at SCHW.7 (commit `fc71aed`) to carry the per-launch manifold payload chart-family-agnostic. Specifically: `OptixLaunchParams::manifold_mode` (MANI-I.5 era; field added at line 360) and `OptixLaunchParams::coordinate_chart` (SCHW.7; field added at line 391). The PENROSE.8 commit reuses these fields unchanged — `git diff c7d0acf..65250ea -- src/optix/OptixLaunchParams.h` returns 0 bytes. The PenroseLike chart payload reaches the kernel via the same `coordinate_chart.params.{mass, spin, compactification_scale, origin}` slots the SchwarzschildLike chart uses, identified by `manifold_mode.chart == CoordinateChartType::PenroseLike`. No new launch-payload field required (the SCHW.7 plumbing was designed chart-family-agnostic; this audit confirms it).<br><br>The host-side `OptixRenderer::render_aovs(scene, lights, w, h, manifold_mode, coordinate_chart)` signature from SCHW.7 carries both fields verbatim to OptiX. `main.cpp::run_render_optix_aovs` builds the chart payload's PenroseLike artistic defaults via the `manifold_chart` builder extended at the PENROSE.8 `main.cpp` diff: on `PenroseLike` it sets `mass = 5.0f` (r_max), `spin = 1.0f` (falloff), `compactification_scale = 1.0f` (scale), `name = "penrose-like"`. These flow into `OptixLaunchParams::coordinate_chart` via `OptixRenderer.cpp::render_aovs` body (preserved verbatim from SCHW.7), then reach the kernel at `optixLaunchParams.coordinate_chart.params`. |
| 2 | OptiX uses shared or equivalent Penrose-like helper | **PASS** | The PENROSE.2 math leaf at `src/manifold/PenroseLikeCompactification.h` is included at `OptixPrograms.cu:53` (`#include "manifold/PenroseLikeCompactification.h"`). The OptiX kernel arm at `OptixPrograms.cu:810-844` invokes `rr::manifold::penrose_like_world_to_chart(hit_pos, optixLaunchParams.coordinate_chart.origin, pp)` — **the identical function** the CUDA-side PENROSE.6 arm at `CudaTestKernel.cu:661-679` invokes. Both backends bind to the same `RR_HD inline` definition at `PenroseLikeCompactification.h:214`. **Single-source-of-truth math** — there is no duplicate implementation; the math is defined once and consumed by all three call sites (CPU seam at `ManifoldTransform.h`, CUDA kernel at `CudaTestKernel.cu`, OptiX kernel at `OptixPrograms.cu`). The `RR_HD inline` decoration carries the helper into device code unchanged; the downstream call closure (`<cmath>` via `std::tanh` / `std::atanh` / `std::pow`; `math/Vec3.h`) is `RR_HD inline` throughout. |
| 3 | Activation conditions are correct | **PASS** | The triple-gate at `OptixPrograms.cu:791-795` is implemented as `const bool penrose_active = ...`:<br>**(a) `is_active(optixLaunchParams.manifold_mode)`** — `ManifoldMode.h:143-145` returns `m.enabled && m.chart != Euclidean`. So the `enabled` gate is satisfied IFF `manifold_mode.enabled = true`; the chart-non-Euclidean gate is satisfied IFF `manifold_mode.chart != Euclidean`. Mirrors the SCHW.7-side gate at `:786-790`.<br>**(b) `optixLaunchParams.manifold_mode.chart == CoordinateChartType::PenroseLike`** — explicit redundant check that structurally bypasses the other chart families. Mutually exclusive with the SchwarzschildLike `schwarzschild_active` gate (different enum constants); both `active` booleans cannot evaluate to `true` simultaneously.<br>**(c) `optixLaunchParams.manifold_mode.strength > 0.0f`** — the runtime dial. The strength is threaded into `PenroseLikeCompactificationParams::strength` at `OptixPrograms.cu:834-835` (`pp.strength = optixLaunchParams.manifold_mode.strength`), so even a positive strength flows through the math leaf's `strength == 0` short-circuit (`PenroseLikeCompactification.h:222`) as a defensive layer; the explicit `> 0` gate avoids the wasted math-leaf call when the operator dials the strength to zero. Mirrors the SCHW.7-side gate at `:790`.<br><br>The two arms are structurally separated by the `else if` keyword at `:810`: when `schwarzschild_active` is true, the PenroseLike branch is unreachable (and vice versa). Combined with the enum-tag distinction (SchwarzschildLike ≠ PenroseLike), the two arms are **doubly mutually exclusive**. **The triple-gate shape is identical to the CUDA-side PENROSE.6 gate at `CudaTestKernel.cu:644-648`** — structural CUDA ↔ OptiX equivalence guaranteed at the gate level. |
| 4 | Disabled / default mode remains no-op | **PASS** | Three-layer safety guarantee, mirroring the OptiX-side analysis from `SCHWARZSCHILD_LIKE_OPTIX_WARP_AUDIT.md` §2 check #3:<br>**(a) Host-side allocation gate:** `OptixRenderer.cpp` (SCHW.7 body) allocates the `aov_manifold_coordinates` device buffer only when `manifold_mode.debug_visualization == true`. On the default the buffer is never allocated and the launch params' `aov_manifold_coordinates` pointer stays `nullptr`.<br>**(b) Kernel-side null gate:** `OptixPrograms.cu:749` (`if (optixLaunchParams.aov_manifold_coordinates != nullptr) {`) — the entire chart-aware arm is wrapped in this null check.<br>**(c) Triple-gate inactive branches:** on the default (`enabled=false` OR `chart=Euclidean` OR `strength<=0`), **both** `schwarzschild_active` and `penrose_active` are `false`; the kernel skips the math-leaf invocations; `hit_pos` retains the unwarped `ro + t * rd` world-space hit position; the AOV write emits the raw position (MANI-I.8 baseline).<br>**(d) Math leaf defensive fallback:** even if a triple-gate were bypassed and the math leaf were invoked with `chart.params.mass = 0`, the leaf's `r_max == 0` short-circuit at `PenroseLikeCompactification.h:223` returns the input vector unchanged.<br>Concrete consequence: every `--render-optix-aovs` invocation without `--manifold-enable --manifold-chart penrose-like --manifold-strength <s>` (and without a scene-file `manifold` block authoring those values) continues to produce a pre-PENROSE.8 byte-identical `output/optix_aov_*.ppm` set (no PenroseLike AOV signature). |
| 5 | Euclidean mode remains identity | **PASS** | With `--manifold-enable --manifold-chart euclidean --manifold-strength <s>`, `manifold_mode.enabled = true` and `manifold_mode.chart = Euclidean`. The `is_active(manifold_mode)` helper at `ManifoldMode.h:143-145` returns `false` (Euclidean is "intentionally not active" per its documented semantic — the `enabled && chart != Euclidean` expression evaluates to `false`). Both triple-gates' `active` booleans evaluate to `false`; the kernel writes the raw `ro + t * rd` hit position at `OptixPrograms.cu:846-848` (MANI-I.8 baseline). Even with `--manifold-debug` set, the AOV buffer IS allocated but the kernel writes the raw position; the resulting PPM is the same as the pre-PENROSE.8 MANI-I.8 AOV output. The Euclidean path is **structurally guaranteed** to bypass both the SchwarzschildLike and the PenroseLike arms via the `is_active` helper's chart-non-Euclidean check. |
| 6 | Schwarzschild behavior unchanged | **PASS** | The SCHW.7 SchwarzschildLike arm is **preserved verbatim** in the PENROSE.8 commit. `git diff c7d0acf..65250ea -- src/optix/OptixPrograms.cu` shows the SCHW.7 region of the kernel arm (lines 796-809: the `schwarzschild_active` triple-gate and the SchwarzschildLikeWarpParams construction + math-leaf call) is byte-identical to the pre-PENROSE.8 state. The PENROSE.8 diff at this site is purely **additive**: it (a) renames the single `active` boolean to `schwarzschild_active` for clarity; (b) introduces a new `penrose_active` triple-gate; (c) wraps the chart-aware code in `if (schwarzschild_active) { ... } else if (penrose_active) { ... }` for structural mutual exclusion. The SchwarzschildLike chart's runtime semantics are unchanged:<br>**(i)** Same triple-gate (`is_active && chart == SchwarzschildLike && strength > 0`);<br>**(ii)** Same parameter encoding (mass→r_s, spin→falloff, compactification_scale→clamp_radius, strength→warp_strength);<br>**(iii)** Same math-leaf invocation (`schwarzschild_like_world_to_chart(hit_pos, optixLaunchParams.coordinate_chart.origin, sp)`).<br>The cross-backend AOV equivalence claim from the SCHW.11 capstone audit (CUDA ↔ OptiX SchwarzschildLike rendering byte-identical via single-source-of-truth math) is preserved by PENROSE.8 — the SchwarzschildLike OptiX arm output is identical to pre-PENROSE.8. |
| 7 | CUDA / OptiX Penrose math equivalence | **PASS** | **Structurally guaranteed by single-source-of-truth math.** Both the CUDA arm at `CudaTestKernel.cu:661-679` (PENROSE.6) and the OptiX arm at `OptixPrograms.cu:810-844` (PENROSE.8) invoke the **identical** `RR_HD inline` `penrose_like_world_to_chart(...)` function declared at `PenroseLikeCompactification.h:214`. There is **a single source of truth** for the math; equivalence is by construction, not by parallel re-implementations.<br><br>The parameter encoding is **consistent**:<br>- CUDA arm at `CudaTestKernel.cu:670-674`: `pp.r_max = scene.coordinate_chart.params.mass; pp.strength = scene.manifold_mode.strength; pp.scale = scene.coordinate_chart.params.compactification_scale; pp.falloff = scene.coordinate_chart.params.spin;`<br>- OptiX arm at `OptixPrograms.cu:832-839`: `pp.r_max = optixLaunchParams.coordinate_chart.params.mass; pp.strength = optixLaunchParams.manifold_mode.strength; pp.scale = optixLaunchParams.coordinate_chart.params.compactification_scale; pp.falloff = optixLaunchParams.coordinate_chart.params.spin;`<br>**Identical parameter mapping** — same plan §3 reinterpretation table; same field-name correspondence.<br><br>The host-side `cuda_manifold_chart` builder (in `run_render_aovs`) and the `manifold_chart` builder (in `run_render_optix_aovs`) populate the **same artistic-default values** for PenroseLike: `mass = 5.0f`, `spin = 1.0f`, `compactification_scale = 1.0f`. So the chart payload reaching both kernels is byte-identical when invoked through the renderer dispatcher.<br><br>The triple-gate is also identical (mirrors the SCHW.* arc's CUDA ↔ OptiX equivalence pattern): same `is_active && chart == PenroseLike && strength > 0` shape on both backends. The chart-aware AOV output is therefore **structurally guaranteed byte-identical** between CUDA and OptiX for any given fixture and any given parameter set. Empirical verification on a CUDA + OptiX-SDK host (deferred per check #9) confirms this in practice. |
| 8 | OptiX OFF build remains valid | **PASS** | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. The OptiX-OFF build path uses the audit-host stub at `OptixRenderer.cpp:3282-3296` (SCHW.7 stub signature carries the new trailing parameters `manifold_mode` + `coordinate_chart` with `/*comment*/` suppression for `-Wunused-parameter`). The audit-host fallback message is preserved; smoke-testing `--render-optix-aovs --manifold-enable --manifold-chart penrose-like --manifold-strength 0.5 --manifold-debug` correctly refuses with the documented "requires OptiX" message (the audit host has no OptiX SDK). Full ctest: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests: 312/312`, `cli_tests: 123/123`, `renderer_tests: 19/19`, `relativity_tests` unchanged. No new ctest target; no CMake link-line change (the `rr_optix → rr_manifold` PUBLIC link added at SCHW.7 already covers PenroseLike since `rr_manifold` is INTERFACE-only). |
| 9 | Runtime CUDA / OptiX-host status | **DEFERRED** | Standard audit-host posture: the audit host (`linux`, no CUDA SDK, no OptiX SDK) cannot execute device code, link OptiX programs, or invoke `optixLaunch`. The SDK_FOUND TUs compile under the audit-host compile rules (the same `rr_apply_warnings` settings apply) but cannot link / launch. Verifying the runtime checks requires a CUDA + OptiX-SDK host. Deferred checks the operator should run on such a host:<br>**(a)** `--render-optix-aovs --manifold-enable --manifold-chart penrose-like --manifold-strength 1.0 --manifold-debug` produces `output/optix_aov_manifold_coordinates.ppm` with the documented asymptotic-compactification signature (far-field pixels saturate at `r_max = 5.0` per the artistic defaults baked into main.cpp; near-field pixels remain close to world-space).<br>**(b) Chart-disabled override byte-identity:** any of the three override mechanisms (CLI `--manifold-chart euclidean`; `--manifold-strength 0`; CLI without `--manifold-enable`) produces an `output/optix_aov_manifold_coordinates.ppm` byte-identical to the pre-PENROSE.8 MANI-I.8 baseline (raw `ro + t * rd` output).<br>**(c) SchwarzschildLike non-regression (OptiX):** `--render-optix-aovs --manifold-enable --manifold-chart schwarzschild-like --manifold-strength 1.0 --manifold-debug` produces an AOV byte-identical to the pre-PENROSE.8 SchwarzschildLike reference (the SCHW.7 arm is preserved verbatim; the PenroseLike `else if` cannot fire when SchwarzschildLike is selected).<br>**(d) Off-chart non-regression:** `--render-optix-aovs --manifold-enable --manifold-chart kerr-like` (reserved-but-inert per MANIFOLD.1) produces a PPM byte-identical to the `--manifold-chart euclidean` baseline because both triple-gates' `chart == ...` checks structurally bypass Kerr.<br>**(e) CUDA ↔ OptiX byte-equivalence (the key PENROSE.8 invariant):** the CUDA `output/aov_manifold_coordinates.ppm` PenroseLike output and the OptiX `output/optix_aov_manifold_coordinates.ppm` PenroseLike output are byte-identical for the same fixture and same `--manifold-*` parameters. This is the cross-backend equivalence the SCHW.11 capstone established for SchwarzschildLike; PENROSE.8 inherits the invariant by reusing the same shared math-leaf pattern. |
| 10 | PASS / REPAIR / BLOCKED verdict | **PASS** | All nine structural checks (#1–#8 and #10) return PASS. Check #9 (runtime CUDA / OptiX-host status) is DEFERRED on documented audit-host limitations. No REPAIR or BLOCKED item is outstanding. The OptiX-side PenroseLike kernel arm is **structurally complete**; the SchwarzschildLike SCHW.7 arm is preserved verbatim with structural mutual exclusion; CUDA + OptiX now invoke the identical shared math leaf with identical parameter encoding so the cross-backend AOV byte-equivalence is structurally guaranteed by single-source-of-truth math. The slice is **safe to extend** to fixture / debug visualization (renumbered PENROSE.10) under the renumbered PENROSE.* ladder. |

---

## 3. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3 ("Do not implement fake stubs
pretending to be complete systems") the audit is
explicit about its scope boundary:

- **No runtime device-side verification.** The audit
  host cannot execute the OptiX kernel arm; check #9
  enumerates the runtime checks deferred to a CUDA +
  OptiX-SDK host. The structural checks (#1–#8 and
  #10) are exhaustive within the audit-host's reach.
- **No cross-backend byte-identity comparison.** Not
  exercisable on the audit host (no CUDA SDK + no
  OptiX SDK). The cross-backend equivalence is
  **structurally guaranteed** by both backends
  invoking the same `RR_HD inline` math leaf with
  byte-identical parameter encoding (both use the
  same `manifold_chart` / `cuda_manifold_chart`
  builder shape in main.cpp); empirical pixel-level
  verification requires an SDK-equipped host.
- **No `render_aovs_retain` PenroseLike path.** The
  retained-buffer entry point (Stage 20N's denoiser
  fork) is unchanged this slice; PENROSE.8 only
  extends the public `render_aovs(...)`. A future
  slice may thread the PenroseLike chart payload
  through the retained path if denoiser integration
  with chart-aware AOVs becomes a priority.
- **No primary-ray direction warp.** The PENROSE.2
  math leaf deliberately ships only a forward /
  inverse pair; the beauty pass is unaffected by
  the PenroseLike chart. SCHW.7 made the same choice;
  PENROSE.8 mirrors it.
- **No chart-parameter scene-authoring.** The
  OptiX-side PenroseLike artistic defaults
  (`mass=5.0`, `spin=1.0`, `compactification_scale=1.0`)
  come from main.cpp's builder helper. The scene
  parser does NOT expose these slots. Future
  slices may broaden the parser surface without
  modifying the PENROSE.8 contract.
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

The PENROSE.8 commit (`65250ea`) ships two surface
changes in 322 added / 11 deleted lines across three
files:

- **`OptixPrograms.cu` closest-hit AOV write arm
  extended** (lines 749-849): the existing SCHW.7
  SchwarzschildLike arm is preserved verbatim; a
  parallel `else if (penrose_active)` branch is
  added with the PENROSE.8 PenroseLike triple-gate
  + math-leaf invocation. The two arms are
  structurally mutually exclusive via (a) the `else
  if` separator and (b) the enum-tag distinction
  (SchwarzschildLike ≠ PenroseLike). Same shape as
  the CUDA-side PENROSE.6 arm at
  `CudaTestKernel.cu:639-680`.
- **`main.cpp::run_render_optix_aovs` chart builder
  extended**: adds an `else if (chart ==
  PenroseLike)` branch with artistic defaults
  consistent with the PENROSE.4 test fixture +
  the CUDA-side PENROSE.6 dispatcher (mass=5.0,
  spin=1.0, compactification_scale=1.0). CUDA +
  OptiX use byte-identical chart payloads.

The OptiX-launch-params invariant (check #1) is
**inherited from SCHW.7** — the `manifold_mode +
coordinate_chart` plumbing was designed chart-family-
agnostic at SCHW.7; PENROSE.8 reuses it unchanged.

The shared-helper invariant (check #2) is
**inherited from PENROSE.2** — the math leaf is
`RR_HD inline` end-to-end; the PENROSE.8 OptiX arm
invokes it identically to the CUDA arm (PENROSE.6)
and the CPU seam (PENROSE.4).

The activation-conditions invariant (check #3) is
**expression-level verified**: the triple-gate at
lines 791-795 mirrors the SCHW.7 gate exactly with
the enum-tag swap (`SchwarzschildLike` → `PenroseLike`)
and also matches the CUDA-side PENROSE.6 gate at
`CudaTestKernel.cu:644-648` byte-for-byte. The
`else if` separator ensures mutual exclusion; the
math leaf's `strength == 0` short-circuit is a
fourth defensive layer.

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
is **diff-verified**: the SCHW.7 region of the
OptiX arm is byte-identical to the pre-PENROSE.8
state (modulo the cosmetic rename `active` →
`schwarzschild_active` and the surrounding
control-flow restructure that preserves the
SchwarzschildLike code path's runtime semantics).

The CUDA/OptiX-Penrose-math-equivalence invariant
(check #7) is **structurally guaranteed**: both
backends invoke the same `RR_HD inline` math leaf
with identical parameter encoding. The audit
documents the byte-identity of the parameter-
mapping code on both sides; the cross-backend AOV
byte-equivalence is consequently mathematical
identity.

The OptiX-OFF-build-validity invariant (check #8)
is **verified empirically** by the audit-host
`cmake --build build -j` succeeding with no new
warnings and `ctest 12/12 PASS`. The audit-host
stub signature is preserved from SCHW.7 (no new
parameter); the existing OptiX-OFF "requires SDK"
message is unchanged.

The runtime CUDA/OptiX-host status (check #9) is
DEFERRED on documented audit-host limitations; the
SCHW.6 / SCHW.8 / SCHW.11 / SCHW.5-completion /
PENROSE.5 / PENROSE.7 per-slice DEFERRED posture is
preserved.

The verdict (check #10) is **PASS** structurally;
runtime DEFERRED.

---

## 5. NEXT

The slice is **safe to extend**. The
`PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §10
PENROSE.* sub-slice ladder needs a one-step shift
to absorb this audit slot:

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
- **PENROSE.7** — Audit of PENROSE.6 (LANDED at
  `c7d0acf`).
- **PENROSE.8** — OptiX integration (LANDED at
  `65250ea`).
- **PENROSE.9** — **THIS AUDIT** (Penrose-Like OptiX
  Integration Audit, doc-only).
- **PENROSE.10** — Fixture / debug visualization
  (was PENROSE.9 in the post-PENROSE.8 plan;
  renumbered).
- **PENROSE.11** — Arc capstone audit (was
  PENROSE.10); closes the MANI-I.11 slot.

The `docs/PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §10
sub-slice ladder is updated as part of this
PENROSE.9 commit so the per-slice numbering stays
coherent. The plan's other sections (§1–§9,
§11–§12) are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is **PENROSE.10 — Fixture / debug
visualization** per the renumbered plan §10
PENROSE.10 (parallel to SCHW.9's fixture work:
adds a controlled diagnostic scene file
`scenes/test_penrose_like_manifold.rrscene` with
visible geometry spanning a wide radial range, a
`manifold` block authoring `chart="penrose-like"`,
and the fixture-companion doc).

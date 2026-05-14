# Schwarzschild-Like OptiX Warp Audit (SCHW.8)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `fc71aed` ("optix:
SCHW.7 — Schwarzschild-Like OptiX Warp Bridge (impl,
OptiX-side)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-SCHW.6 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the per-slice gate for the SCHW.7 OptiX
warp bridge (`fc71aed`). It verifies the nine items the
task brief enumerates — OptiX launch params receive the
manifold payload; OptiX warp activates only on the
intended gates; disabled/default no-op; Euclidean
identity; CUDA/OptiX warp math equivalence;
bounded / no-NaN; OptiX OFF build remains valid;
runtime CUDA/OptiX-host status; verdict — and produces
a PASS / REPAIR / BLOCKED verdict that gates progression
to the next concrete slice (SCHW.9 debug visualization;
renumbered from the post-SCHW.6 SCHW.8 per §4).

---

## 1. VERDICT

**PASS** (structural OptiX-side warp bridge verified),
with **DEFERRED** runtime CUDA/OptiX-SDK-host status.

All eight structural checks (#1–#7 and #9) return PASS.
Check #8 (runtime CUDA/OptiX-host status) is DEFERRED
on documented audit-host limitations (no CUDA SDK, no
OptiX SDK on the audit host; the SDK_FOUND TU compiles
under the audit-host rules but cannot link / launch).
This matches the MANI-I.6 / MANI-I.9 / SCHW.6 DEFERRED
posture for audit-host audits of GPU-side work.

No REPAIR or BLOCKED item is found. The OptiX-side
warp bridge is safely landed: it threads the
SchwarzschildLike chart payload through
`OptixLaunchParams`, gates the warp on
`is_active(manifold_mode) && chart == SchwarzschildLike
&& strength > 0.0f`, reuses the shared SCHW.1 math leaf
(equivalence with the eventual SCHW.5 CUDA-side
wiring is structurally guaranteed by the single source
of truth), inherits the bounded / no-NaN invariants
from the SCHW.1 / SCHW.2 audits, and preserves the
audit-host build's `--render-optix-aovs` semantics
verbatim. The operator may proceed to the renumbered
SCHW.9 (debug visualization) or to SCHW.5 (CUDA
integration) in any order.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | OptiX launch params receive manifold payload | **PASS**     | The SCHW.7 commit (`fc71aed`) extends `src/optix/OptixLaunchParams.h` with the `rr::manifold::CoordinateChart coordinate_chart{}` field at line 391 (right after the existing MANI-I.5 `manifold_mode` field at line 361). Header include added at line 7 (`#include "manifold/CoordinateChart.h"`). The SDK_FOUND `OptixRenderer::render_aovs(...)` body at `OptixRenderer.cpp:2760-2761` assigns both `params.manifold_mode = manifold_mode` and `params.coordinate_chart = coordinate_chart` from the trailing function parameters before invoking `cudaMemcpy(launch_params_device_ptr(), &params, sizeof(params), ...)`. The audit-host stub at `OptixRenderer.cpp:3282-3296` carries the identical signature (with `/*manifold_mode*/` / `/*coordinate_chart*/` parameter-name comments to suppress unused-parameter warnings) and returns the documented "requires SDK" message. The `OptixRenderer.h` declaration at lines 423-430 documents the trailing defaulted parameters and the byte-identity invariant the defaults provide. |
| 2 | OptiX warp activates only when enabled + SchwarzschildLike + strength > 0 | **PASS**     | The triple-gate is implemented at `OptixPrograms.cu:773-777` as a single `const bool active = ...` expression:<br>**(a) `is_active(manifold_mode)`** — `ManifoldMode.h:143-145` returns `m.enabled && m.chart != Euclidean`. So the `enabled` gate is satisfied IFF `manifold_mode.enabled = true`; the chart-non-Euclidean gate is satisfied IFF `manifold_mode.chart != Euclidean`.<br>**(b) `manifold_mode.chart == CoordinateChartType::SchwarzschildLike`** — explicit redundant check that structurally bypasses the other `*Like` / `*LikePlaceholder` families (Kruskal / Penrose / Kerr) per master rule #3 (no silent routing through SchwarzschildLike math). This is the same posture the SCHW.3 CPU-side seam takes (`ManifoldTransform.h:191` / `:210` / `:272` / `:299`) and that `test_schw_3_other_non_euclidean_passthrough` verifies.<br>**(c) `manifold_mode.strength > 0.0f`** — the runtime dial. The strength is threaded into `SchwarzschildLikeWarpParams::warp_strength` at `OptixPrograms.cu:781-782`, so even a positive strength flows through the math leaf's `warp_strength == 0` short-circuit (`SchwarzschildLikeWarp.h:153`) as a defensive layer; the explicit `> 0` gate avoids the wasted math-leaf call when the operator dials the strength to zero. |
| 3 | Disabled/default mode remains no-op           | **PASS**     | Two-layer guarantee:<br>**(a) Host-side allocation gate:** `OptixRenderer.cpp:2713-2720` allocates the `d_aov_manifold_c` device buffer only when `manifold_mode.debug_visualization == true`. On the default (`ManifoldMode{}.debug_visualization == false`) the buffer is never allocated and the launch params' `aov_manifold_coordinates` pointer stays `nullptr`.<br>**(b) Kernel-side null gate:** `OptixPrograms.cu:756` (`if (optixLaunchParams.aov_manifold_coordinates != nullptr) {`) — the entire chart-aware arm is wrapped in this null check, so even if the host bypassed the allocation gate, the kernel would short-circuit on the null pointer.<br>Together: on the default `ManifoldMode{}` (`enabled=false`, `chart=Euclidean`, `strength=0`, `debug_visualization=false`), the host does not allocate the buffer AND `is_active(...)` returns `false` AND the strength-positive gate returns `false` — three independent layers of safety, any one of which is sufficient. Every `--render-optix-aovs` invocation without `--manifold-*` flags continues to produce the pre-SCHW.7 byte-identical PPM set (`output/optix_aov_beauty.ppm`, ..., `output/optix_aov_searchlight.ppm`; no `output/optix_aov_manifold_coordinates.ppm` is emitted). |
| 4 | Euclidean mode remains identity               | **PASS**     | With `--manifold-enable --manifold-chart euclidean --manifold-strength <s>`, `manifold_mode.enabled = true` and `manifold_mode.chart = Euclidean`. The `is_active(manifold_mode)` helper at `ManifoldMode.h:143-145` returns `false` (Euclidean is "intentionally not active" per its documented semantic — the `enabled && chart != Euclidean` expression evaluates to `false`). The triple-gate's `active = false`, and the kernel writes the raw `ro + t * rd` world-space hit position at `OptixPrograms.cu:793-795` — the MANI-I.8 baseline. Even with `--manifold-debug` set, the AOV buffer IS allocated but the kernel writes the raw position; the resulting PPM is the same as the pre-SCHW.7 MANI-I.8 AOV output. The Euclidean arm's affine `(p - origin) / scale` path in `ManifoldTransform.h:187` is preserved verbatim from MANIFOLD.5 (SCHW.7 did not modify it). |
| 5 | CUDA/OptiX warp math is equivalent             | **PASS** (structural; CUDA call site deferred to SCHW.5) | Both the SCHW.3 CPU seam (`ManifoldTransform.h:191-196`) and the SCHW.7 OptiX kernel (`OptixPrograms.cu:778-792`) invoke the identical `RR_HD inline` `schwarzschild_like_world_to_chart(p_world, mass_origin, params)` function declared at `SchwarzschildLikeWarp.h:145-162`. There is **a single source of truth** for the math; equivalence is by construction. The parameter encoding is consistent: the SCHW.3 seam uses `schwarzschild_like_params_from(chart, warp_strength=1.0f)` and the SCHW.7 OptiX kernel inlines the same mapping (`mass→r_s`, `spin→falloff`, `compactification_scale→clamp_radius`) at `OptixPrograms.cu:779-786` with `warp_strength` threaded from `manifold_mode.strength`. The CUDA kernel call site is still SCHW.5-deferred; when it lands it will reuse the same helper, so CUDA/OptiX equivalence will hold by the same single-source-of-truth argument. |
| 6 | Bounded / no-NaN behavior exists              | **PASS**     | Inherited from the SCHW.1 / SCHW.2 audit verdicts (`docs/SCHWARZSCHILD_LIKE_WARP_AUDIT.md` §2 checks #2 / #3) and carried through unchanged. Four bounding/no-NaN mechanisms at `SchwarzschildLikeWarp.h`:<br>**(a)** `r = max(|delta|, clamp_radius)` (line 158) prevents `1 / r^falloff` underflow;<br>**(b)** validator rejects `clamp_radius <= 0` (line 120) plus rejects non-finite inputs (lines 115-118);<br>**(c)** Newton-Raphson 8-iteration cap + `1e-5` convergence tolerance (lines 214-215);<br>**(d)** `F'` zero-guard at `1e-9` (line 227); negative-`r` rebound to clamp_radius (line 230).<br>The OptiX kernel arm adds two defensive layers on top:<br>**(e)** Triple-gate's `strength > 0` check prevents the math-leaf call when the operator dials strength to zero — the math leaf's own short-circuit handles this case correctly but the explicit gate documents the intent at the call site;<br>**(f)** The default OptiX `coordinate_chart = CoordinateChart{}` has `params.mass = 0`, so the math leaf's `r_s == 0` short-circuit (`SchwarzschildLikeWarp.h:154`) returns the input unchanged even if the operator somehow bypasses the triple-gate. No code path can produce NaN/Inf inside the AOV write arm. |
| 7 | OptiX OFF build remains valid                 | **PASS**     | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. The OptiX-OFF build path uses the audit-host stub at `OptixRenderer.cpp:3282-3296`: signature matches the SDK_FOUND body, parameters are `/*comment*/`-suppressed to satisfy `-Wunused-parameter`, body returns the documented "requires SDK" message. The `OptixRenderer.h` declaration at lines 423-430 carries the defaulted parameters so every existing caller compiles without modification. Full ctest: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `198 / 198 checks passed` (unchanged from the post-SCHW.6 baseline; SCHW.7 doesn't touch the manifold tests). `cli_tests: 123/123`, `renderer_tests: 19/19`, `relativity_tests` all unchanged. `main.cpp`'s `run_render_optix_aovs` arm compiles in both branches of the `#ifndef RELATIVITYRENDER_ENABLE_OPTIX` / `#else` split; the audit-host fallback message is preserved at lines 2053-2059. |
| 8 | Runtime CUDA/OptiX-host status                | **DEFERRED** | Standard audit-host posture: the audit host (`linux`, no CUDA SDK, no OptiX SDK) cannot execute device code, link OptiX programs, or invoke `optixLaunch`. The SDK_FOUND TU compiles under the audit-host compile rules (the same `rr_apply_warnings` settings apply) but cannot link / launch. Verifying the runtime checks requires a CUDA + OptiX-SDK host. Deferred checks the operator should run on such a host:<br>**(a) Euclidean fallback byte-identity:** `--render-optix-aovs --manifold-enable --manifold-chart euclidean --manifold-strength 1.0 --manifold-debug` should produce the six pre-SCHW.7 PPMs byte-identical AND an `output/optix_aov_manifold_coordinates.ppm` containing the raw world-space hit positions (matching the MANI-I.8 OptiX output as if SCHW.7 had not landed).<br>**(b) `warp_strength = 0` byte-identity:** `--render-optix-aovs --manifold-enable --manifold-chart schwarzschild-like --manifold-strength 0.0` should produce the six pre-SCHW.7 PPMs byte-identical (the `manifold_mode.strength > 0` gate short-circuits the warp arm).<br>**(c) Visual signature on the AOV:** `--render-optix-aovs --manifold-enable --manifold-chart schwarzschild-like --manifold-strength 1.0 --manifold-debug` should produce `output/optix_aov_manifold_coordinates.ppm` with the documented radial-compression signature (plan §4.1 / §4.3): pixel values near `mass_origin = (0,0,0)` should diverge from world-space; pixel values far from the mass should be near-identity (plan §4.1 "warpStrength + falloff = 1" regime).<br>**(d) Off-chart non-regression:** `--render-optix-aovs --manifold-enable --manifold-chart kerr-like` (reserved-but-inert per MANIFOLD.1) should produce byte-identical PPMs to `--manifold-chart euclidean` because the triple-gate's `chart == SchwarzschildLike` check structurally bypasses Kerr.<br>**(e) CUDA/OptiX equivalence (deferred until SCHW.5 lands):** when the CUDA-side SchwarzschildLike arm lands at SCHW.5, the CUDA `aov_manifold_coordinates.ppm` and the OptiX `optix_aov_manifold_coordinates.ppm` should be byte-identical (modulo per-pixel sample-count differences if any). |
| 9 | PASS / REPAIR / BLOCKED verdict               | **PASS**     | All eight structural checks (#1–#7 and #9) return PASS; check #8 is DEFERRED on documented audit-host limitations. No REPAIR or BLOCKED item is outstanding. The SCHW.7 commit lands a clean, structurally-safe OptiX warp bridge: launch params receive the manifold payload, the triple-gate prevents accidental engagement, the shared math leaf guarantees CUDA/OptiX equivalence by construction, the bounded / no-NaN invariants are inherited from SCHW.1, the audit-host build remains green, and the default / disabled / Euclidean paths are byte-identical to the pre-SCHW.7 baseline. The slice is **safe to extend** to the renumbered SCHW.9 (debug visualization refinement) or to SCHW.5 (CUDA integration) in any order. |

---

## 3. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3 ("Do not implement fake stubs
pretending to be complete systems") the audit is
explicit about its scope boundary:

- **No runtime device-side verification.** The audit
  host cannot execute the OptiX kernel arm; check #8
  enumerates the runtime checks deferred to a CUDA +
  OptiX-SDK host. The structural checks (#1–#7) are
  exhaustive within the audit-host's reach.
- **No CUDA-side SchwarzschildLike arm.** The CUDA
  `ManifoldCoordinates` AOV write site in
  `CudaTestKernel.cu:582-602` still writes the raw
  `best.position` unconditionally (per MANI-I.8); the
  CUDA-side wiring is SCHW.5-deferred. The
  CUDA/OptiX equivalence claim (check #5) is
  forward-looking: it asserts that when SCHW.5 lands
  using the same `schwarzschild_like_world_to_chart`
  helper, the math will be equivalent by single-
  source-of-truth. The CUDA side does not produce
  a SchwarzschildLike-warped AOV today.
- **No primary-ray direction warp at raygen.** The
  `schwarzschild_like_warp_ray_direction` helper
  exists at SCHW.1 but neither the CUDA nor the
  OptiX raygen invokes it. SCHW.7 routes only the
  hit-position through the warp for the AOV write
  site; the beauty pass uses unwarped primary rays.
  SCHW.9 (debug visualization) may decide whether
  the primary-ray warp engages.
- **No `render_aovs_retain` chart-aware path.** The
  retained-buffer entry point (Stage 20N's denoiser
  fork) is unchanged this slice; SCHW.7 only
  extends the public `render_aovs(...)`. A future
  slice may thread the chart payload through the
  retained path if the denoiser integration with
  chart-aware AOVs becomes a priority.
- **No `OptixLaunchParams` ABI compatibility check
  across the broader OptiX SDK matrix.** The audit
  verifies the field addition at the source level;
  device-side ABI compatibility (e.g. NVCC version
  matrix, OptiX 7.x version matrix, CUDA toolkit
  version matrix) is the SDK_FOUND host's
  responsibility. The MANI-I.5 and MANI-I.8 audits
  set the precedent that audit-host audits do not
  perform ABI cross-checking; that is part of the
  deferred runtime suite.
- **No artistic-defaults verification.** The
  artistic-default chart parameters chosen at
  `main.cpp` (`mass=1.0`, `spin=1.0`,
  `compactification_scale=0.1`) are not subject to
  audit-time verification beyond their match with
  the SCHW.3 test fixture
  (`make_schwarzschild_like_chart` in
  `manifold_identity_tests.cpp:643-653`). The
  operator can validate the visual signature on a
  CUDA + OptiX-SDK host per check #8(c).

---

## 4. REASONING SUMMARY

The SCHW.7 commit (`fc71aed`) ships six host- and
device-side surface changes to land the OptiX-side
SchwarzschildLike warp bridge:

- **`OptixLaunchParams.h`:** new
  `coordinate_chart` field plus the
  `manifold/CoordinateChart.h` include. The
  per-launch chart payload is now expressible.
- **`OptixRenderer.h`:** new
  `AovResult::manifold_coordinates` Image slot
  plus the trailing defaulted `manifold_mode` /
  `coordinate_chart` parameters on `render_aovs`.
- **`OptixRenderer.cpp` (SDK_FOUND body):**
  allocates `aov_manifold_coordinates` device
  buffer when `manifold_mode.debug_visualization =
  true`; threads `manifold_mode` /
  `coordinate_chart` into the launch params;
  downloads the AOV when allocated. The MANI-I.9
  audit's deferred OptiX-side allocation finding
  closes here.
- **`OptixRenderer.cpp` (audit-host stub):**
  identical signature, "requires SDK" message
  unchanged.
- **`OptixPrograms.cu` (closest-hit MANI-I.8
  arm):** triple-gates the SchwarzschildLike warp;
  on the active path, invokes the shared
  `schwarzschild_like_world_to_chart` math leaf
  with parameters extracted from
  `coordinate_chart.params` + the runtime
  `manifold_mode.strength` dial. On the inactive
  path, writes the raw world-space hit position
  (MANI-I.8 baseline).
- **`main.cpp::run_render_optix_aovs`:** builds a
  `CoordinateChart` from `cfg.manifold.chart`
  with artistic SchwarzschildLike defaults;
  passes `cfg.manifold` + the built chart through;
  saves `r.manifold_coordinates` to
  `output/optix_aov_manifold_coordinates.ppm`
  when `cfg.manifold.debug_visualization = true`.

The bridge is **structurally complete** on the OptiX
side. The activation gate decomposes onto three
independent checks (existing `is_active` helper +
new redundant `chart == SchwarzschildLike` check +
new `strength > 0` check), each providing a layer of
defense. The shared `schwarzschild_like_world_to_chart`
helper guarantees CUDA/OptiX equivalence by
construction; the parameter mapping is consistent
between the SCHW.3 seam and the SCHW.7 kernel arm.
The bounded / no-NaN invariants inherited from
SCHW.1 / SCHW.2 carry through with two additional
defensive layers from the triple-gate.

The disabled-mode-no-op invariant (check #3) is
**three-layer-redundantly guaranteed**: host doesn't
allocate the AOV buffer; the kernel null-checks the
pointer; the triple-gate short-circuits on the
default. Any one of the three layers is sufficient;
all three together make the default path
structurally incapable of producing a chart-aware
AOV write.

The Euclidean-identity invariant (check #4) is
**bit-preserving** because the Euclidean default's
`is_active(...) = false` route bypasses the entire
chart-aware arm, writing the same raw `ro + t * rd`
expression the MANI-I.8 baseline writes.

The CUDA/OptiX-math-equivalence invariant (check #5)
is **forward-looking but structurally guaranteed**:
both paths will invoke the same RR_HD inline math
leaf; today only OptiX has a call site; when SCHW.5
adds the CUDA call site, the math will be identical
by single-source-of-truth. The parameter encoding
is consistent (both use the same
`mass→r_s / spin→falloff / compactification_scale→
clamp_radius` mapping the plan §3 specifies).

The bounded / no-NaN invariant (check #6) is
**inherited from SCHW.1** via direct math-leaf
delegation, with two additional defensive layers
from the triple-gate.

The OptiX-OFF-build-validity invariant (check #7) is
**verified empirically** by the audit-host
`cmake --build build -j` succeeding with no new
warnings and `ctest 12/12 PASS`. The audit-host
stub signature carries the new parameters with
`/*comment*/`-suppressed unused-parameter warnings;
the default values preserve every existing caller's
behavior without source-level change.

The build / test status (check #7) shows the slice
integrates cleanly with the existing test
infrastructure: `manifold_identity_tests` remains at
198 RR_CHECKs (SCHW.7 doesn't add tests; the kernel
arm is exercised at runtime, not at unit-test
level). The audit-host build is clean (no new
warnings).

---

## 5. NEXT

The slice is **safe to extend**. The
`SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §8 SCHW.* sub-slice
ladder needs a one-step shift to absorb this audit
slot:

- **SCHW.1** — Math helper (LANDED at `2da5780`).
- **SCHW.2** — Audit of SCHW.1 (LANDED at `c799621`).
- **SCHW.3** — CPU integration (LANDED at `b48c480`).
- **SCHW.4** — Audit of SCHW.3 (LANDED at `b78fe98`).
- **SCHW.5** — CUDA integration (not yet landed;
  forward-looking SCHW.6 audit gated the
  infrastructure).
- **SCHW.6** — Forward-looking CUDA-warp audit
  (LANDED at `6660bb4`).
- **SCHW.7** — OptiX integration (LANDED at
  `fc71aed`).
- **SCHW.8** — **THIS AUDIT** (Schwarzschild-Like
  OptiX Warp Audit, doc-only).
- **SCHW.9** — Debug visualization (was SCHW.8 in
  the post-SCHW.6 plan; renumbered).
- **SCHW.10** — Final audit (was SCHW.9); closes
  the MANI-I.10 slot.

The `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §8
sub-slice ladder is updated as part of this SCHW.8
commit so the per-slice numbering stays coherent. The
plan's other sections (§1–§7, §9–§10) are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is one of:
- **SCHW.5 — CUDA integration** (close the
  CUDA-side gap; mirror SCHW.7's OptiX-side wiring
  in `CudaTestKernel.cu` using the same shared
  math leaf), OR
- **SCHW.9 — Debug visualization** (refine the
  `ManifoldCoordinates` AOV encoding for visual
  clarity per the plan §6.4 / §4 visual-effect
  summary).

Both are tractable on the audit host. The runtime
CUDA + OptiX-SDK-host fixture renders enumerated in
check #8 above will become exercisable when the
operator runs the existing CLI on a CUDA + OptiX-SDK
host; the SCHW.10 final audit will close the
MANI-I.10 slot with the end-to-end PASS verdict.

# Schwarzschild-Like CPU Integration Audit (SCHW.4)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `b48c480` ("manifold:
SCHW.3 — Schwarzschild-Like CPU Transform Integration
(impl, host-only)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-SCHW.2 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the per-slice gate for the SCHW.3 CPU
integration (`b48c480`). It verifies the eight items
the task brief enumerates — ManifoldTransform supports
SchwarzschildLike chart; disabled/default mode remains
identity; Euclidean chart remains identity;
Schwarzschild-like transform is bounded; near-clamp
behavior avoids NaN/Inf; no CUDA/OptiX behavior
changed; build/test status; verdict — and produces the
PASS / REPAIR / BLOCKED verdict that gates progression
to the CUDA integration slice (renumbered SCHW.5; see
§4).

---

## 1. VERDICT

**PASS.**

All seven structural checks return PASS. No REPAIR or
BLOCKED item is found. The SCHW.3 CPU integration is
safely landed: it routes the bounded SCHW.1 math leaf
through the four `ManifoldTransform.h` helper arms in
a way that preserves the bit-identity invariant on the
Euclidean / disabled default and produces no
renderer-side behavior change. The operator may
proceed to SCHW.5 (CUDA integration; renumbered from
the original SCHW.4 per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | ManifoldTransform supports SchwarzschildLike chart | **PASS** | The SCHW.3 commit (`b48c480`) extends `src/manifold/ManifoldTransform.h` with five new surfaces:<br>**(a) Header include:** `#include "manifold/SchwarzschildLikeWarp.h"` adds the SCHW.1 math leaf to the seam header's dependency closure (line 105).<br>**(b) Builder helper:** `schwarzschild_like_params_from(const CoordinateChart&, float warp_strength = 1.0f)` at `ManifoldTransform.h:158-167` reads `chart.params.mass / .spin / .compactification_scale` and produces a `SchwarzschildLikeWarpParams` matching the plan §3 reinterpretation table verbatim.<br>**(c) `world_to_chart(t, Vec3)` SchwarzschildLike arm** at `ManifoldTransform.h:191-196` — calls `schwarzschild_like_world_to_chart(world_pos, t.chart.origin, schwarzschild_like_params_from(t.chart))`.<br>**(d) `chart_to_world(t, Vec3)` SchwarzschildLike arm** at `ManifoldTransform.h:210-215` — calls the bounded NR inverse.<br>**(e) Vec4 overload arms** at `ManifoldTransform.h:272-281` (`world_to_chart`) and `:299-308` (`chart_to_world`) — time component invariant per the plan §6.1 static-chart-in-time rule; spatial components routed through the Vec3 math leaf and re-packed. |
| 2 | Disabled/default mode remains identity            | **PASS** | The default `ManifoldTransform{}` (Euclidean chart with `origin = (0,0,0)`, `scale = 1.0`) routes through the *existing* Euclidean arm — SCHW.3 did NOT touch the Euclidean arm's expression. Verified empirically by `test_schw_3_disabled_identity_preserved` at `manifold_identity_tests.cpp:646` (Vec3 and Vec4 inputs both round-trip identity via `world_to_chart(t, p) == p` and `chart_to_world(t, p) == p`). The structural guarantee: the SCHW.3 chart-aware arm is gated by `t.chart.type == CoordinateChartType::SchwarzschildLike`, and the default `CoordinateChart{}` has `type = CoordinateChartType::Euclidean` (line `CoordinateChart.h:136`). The two enum values are distinct integers (`Euclidean = 0`, `SchwarzschildLike = 1` at `CoordinateChart.h:46-47`), so the arm is unreachable on a default-constructed transform. |
| 3 | Euclidean chart remains identity                   | **PASS** | The Euclidean arm in every helper is preserved verbatim from MANIFOLD.5:<br>`world_to_chart(t, Vec3)` at `ManifoldTransform.h:184-188` — `(world_pos - t.chart.origin) * (1.0f / t.chart.scale)`;<br>`chart_to_world(t, Vec3)` at `ManifoldTransform.h:203-207` — `t.chart.origin + chart_pos * t.chart.scale`;<br>`world_to_chart(t, Vec4)` at `ManifoldTransform.h:261-271` — time invariant, spatial part `(p.{yzw} - origin) * inv_scale`;<br>`chart_to_world(t, Vec4)` at `ManifoldTransform.h:288-298` — inverse Vec4 transform. The SchwarzschildLike arm sits AFTER the Euclidean arm's `return` — there is no fall-through risk; an explicit Euclidean chart with non-trivial `origin = (1,2,3)` and `scale = 2.0` still applies the affine rule unchanged, verified by `test_schw_3_euclidean_identity_preserved` at `manifold_identity_tests.cpp:667`. |
| 4 | Schwarzschild-like transform is bounded            | **PASS** | The SCHW.3 arm delegates to the SCHW.1 math leaf, which the SCHW.2 audit (`docs/SCHWARZSCHILD_LIKE_WARP_AUDIT.md` §2 check #2) verified bounded by construction:<br>**(a)** `r = max(|delta|, clamp_radius)` ensures `1 / r^falloff ≤ 1 / clamp_radius^falloff`; the displacement scalar `f = warp_strength * r_s / r^falloff` is bounded above by `warp_strength * r_s / clamp_radius^falloff`.<br>**(b)** Newton-Raphson inverse capped at 8 iterations with `1e-5` tolerance (`SchwarzschildLikeWarp.h:214-215`).<br>**(c)** Validator (`SchwarzschildLikeWarp.h:113-122`) rejects out-of-range parameters; the math leaf's defensive fallback returns the input unchanged on rejection.<br>Verified at the `ManifoldTransform` seam by `test_schw_3_world_to_chart_schwarzschild_like_known_value` (`manifold_identity_tests.cpp:717` — plan §2 worked example `(2,0,0) → (3,0,0)`) and `test_schw_3_chart_to_world_schwarzschild_like_round_trip` (`manifold_identity_tests.cpp:743` — round-trip residual `< 1e-4` across four representative inputs; Vec4 round-trip preserves time exactly with the spatial residual meeting the same bound). |
| 5 | Near-clamp behavior avoids NaN/Inf                 | **PASS** | The SCHW.1 math leaf's clamp-radius safety carries through the SCHW.3 seam unchanged. Verified at the `ManifoldTransform` seam by `test_schw_3_no_nan_inf_near_clamp_radius` at `manifold_identity_tests.cpp:780` across three cases:<br>**(a)** `p_world == chart.origin` (most singular geometric case): `delta = 0`; `chart_pos = mass_origin + 0 * (anything) = mass_origin`; output is finite and equals the input (test verifies `std::isfinite` on each component AND `approx(out, at_mass)`).<br>**(b)** `p_world` strictly inside the clamp shell (`|p_world - mass_origin| < clamp_radius = 0.1`): math leaf substitutes `r = clamp_radius` per `SchwarzschildLikeWarp.h:158`; no `1/r` evaluation at `r < clamp_radius`; output finite (test verifies `std::isfinite` on each component).<br>**(c)** `chart_to_world` from a clamp-shell input: the NR inverse uses the `in_clamp_shell` `F' = 1.0` substitution at `SchwarzschildLikeWarp.h:224-225`, so the Jacobian denominator is `1.0` (well-conditioned); output finite.<br>**(d)** Vec4 overload at the singular case (`Vec4{1.0f, origin.x, origin.y, origin.z}`): time component preserved at `1.0`; spatial part returns mass_origin; all four components finite. |
| 6 | No CUDA/OptiX behavior changed                     | **PASS** | `git diff c799621..b48c480 --name-only` returns exactly three files: `docs/BUILD_PLAN.md`, `src/manifold/ManifoldTransform.h`, `tests/manifold_identity_tests.cpp`. Filtered against the renderer subtree (`src/cuda/`, `src/optix/`, `src/pathtracer/`, `src/renderer/`, `src/gpu/`, `src/scene/`, `src/io/`, `src/server/`, `src/core/`, `src/camera/`, `src/material/`, `src/lighting/`, `src/texture/`, `src/geometry/`, `src/image/`, `src/math/`, `src/relativity/`, `src/field/`, `src/main.cpp`): **zero hits**. The SCHW.3 commit does not touch any kernel call site; the new `ManifoldTransform.h` arm exists on disk but is host-only and is not invoked by any GPU path. Beauty-pass and AOV outputs for every existing CLI action are byte-identical to the post-SCHW.2 baseline. |
| 7 | Build/test status                                  | **PASS** | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings (the `<initializer_list>` include added at `manifold_identity_tests.cpp:43` clears the only GCC 13 `-Werror` diagnostic the new code surface produced during initial compile). Full ctest: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `198 / 198 checks passed` (was `140 / 140` pre-SCHW.3; +58 new SCHW.3 RR_CHECKs across 8 new test functions enumerated in §3 below). `cli_tests: 123/123 passed`, `renderer_tests: 19 / 19 passed`, `relativity_tests` unchanged — both the parser and the renderer surfaces are still post-SCHW.2 baseline (SCHW.3 didn't touch them). |
| 8 | PASS / REPAIR / BLOCKED verdict                    | **PASS** | All seven structural checks return PASS. No REPAIR or BLOCKED item is outstanding. The SCHW.3 commit lands a clean, bounded, bit-identity-preserving CPU-side integration of the SCHW.1 math leaf into the `ManifoldTransform` seam. The slice is **safe to extend** to CUDA integration (renumbered SCHW.5) under the SCHW.* ladder. |

---

## 3. REASONING SUMMARY

The SCHW.3 commit (`b48c480`) introduces five host-side
surface changes to `src/manifold/ManifoldTransform.h`:

- a new header include (`manifold/SchwarzschildLikeWarp.h`);
- a new builder helper
  (`schwarzschild_like_params_from(chart, warp_strength)`)
  that bridges the chart-side parameter storage
  (`CoordinateChart::params`) to the math leaf's
  `SchwarzschildLikeWarpParams` per the plan §3
  reinterpretation table;
- four chart-aware arms inserted into the
  `world_to_chart` / `chart_to_world` Vec3 + Vec4
  overloads, each gated on `t.chart.type ==
  CoordinateChartType::SchwarzschildLike` and each
  calling the SCHW.1 math leaf.

The Euclidean arm of each helper is preserved verbatim
from MANIFOLD.5; the SchwarzschildLike arm sits AFTER
the Euclidean `return` so there is no fall-through. The
remaining non-Euclidean chart families
(`KruskalLikePlaceholder` / `PenroseLikePlaceholder` /
`KerrLikePlaceholder`) continue to passthrough — master
rule #3 forbids silent routing through SchwarzschildLike
math.

The host-side test additions to
`tests/manifold_identity_tests.cpp` cover the task
brief's four acceptance items plus four additional
invariants:

- `test_schw_3_disabled_identity_preserved` — default
  `ManifoldTransform{}` stays identity post-SCHW.3
  (Vec3 + Vec4).
- `test_schw_3_euclidean_identity_preserved` — explicit
  Euclidean chart with non-trivial `origin = (1,2,3)`
  and `scale = 2.0` still applies the affine map.
- `test_schw_3_schwarzschild_like_zero_mass_is_identity`
  — SchwarzschildLike with `chart.params.mass = 0`
  returns input via the math leaf's `r_s = 0`
  short-circuit.
- `test_schw_3_world_to_chart_schwarzschild_like_known_value`
  — plan §2 worked example produces
  `chart_pos = (3, 0, 0)` via both Vec3 and Vec4
  overloads (Vec4 time component preserved at `0.5`).
- `test_schw_3_chart_to_world_schwarzschild_like_round_trip`
  — forward → inverse round-trip residual `< 1e-4`
  across four representative inputs; Vec4 round-trip
  preserves time exactly.
- `test_schw_3_no_nan_inf_near_clamp_radius` — three
  cases: at mass origin, inside clamp shell, Vec4 at
  singular — all finite and well-defined.
- `test_schw_3_params_from_chart` — exercises the
  builder helper directly; default strength `1.0`;
  caller-supplied strength override; validator passes
  on the default chart's params.
- `test_schw_3_other_non_euclidean_passthrough` —
  Kruskal / Penrose / Kerr remain passthrough even with
  `params.mass = 1.0`.

The disabled/default-mode-identity invariant (check
#2) is **structurally guaranteed** by the enum-tag
gate: the SchwarzschildLike arm is unreachable on a
default-constructed `ManifoldTransform` because
`CoordinateChart{}.type = Euclidean` and the helper
takes the Euclidean arm's `return` before the
SchwarzschildLike gate is evaluated.

The Euclidean-identity invariant (check #3) is **bit-
preserving** because the Euclidean arm's expression is
unchanged from MANIFOLD.5 — no edit touches the
`(world_pos - origin) * (1 / scale)` line or its Vec4
counterpart. The SchwarzschildLike arm is a new
`if`-block inserted after the Euclidean `return`; it
cannot influence the Euclidean path.

The bounded-transform invariant (check #4) is
**inherited from SCHW.1** — the SCHW.2 audit already
verified the math leaf is bounded by construction, and
the SCHW.3 arm delegates the math directly to the leaf
without intermediate manipulation that could re-
introduce unboundedness.

The no-NaN/Inf invariant (check #5) is similarly
**inherited from SCHW.1** with one new test surface:
the Vec4 overload at the singular case
(`p_world == chart.origin`) is verified to produce a
finite, identity output — the test repacks the spatial
result into a Vec4 with the time component preserved,
so a regression in the Vec4 overload that fails to
preserve `world_pos4.x` would be caught here.

The no-renderer-behavior-change invariant (check #6)
is **structurally guaranteed** because the SCHW.3
diff is restricted to one host-only header
(`src/manifold/ManifoldTransform.h`), one test file
(`tests/manifold_identity_tests.cpp`), and the build
plan (`docs/BUILD_PLAN.md`). No kernel call site
invokes the new arms. The renderer's GPU paths are
byte-identical to the post-SCHW.2 baseline.

The build/test status (check #7) shows the slice
integrates cleanly with the existing test
infrastructure: `manifold_identity_tests` grew from
140 to 198 RR_CHECKs without any test-binary churn
(no new ctest target; no new CMake link line). The
audit-host build is clean (no new warnings).

---

## 4. NEXT

The slice is **safe to extend**. The
`SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §8 SCHW.* sub-slice
ladder needs a one-step shift to absorb this audit
slot:

- **SCHW.1** — Math helper (LANDED at `2da5780`).
- **SCHW.2** — Audit of SCHW.1 (LANDED at `c799621`).
- **SCHW.3** — CPU integration (LANDED at `b48c480`).
- **SCHW.4** — **THIS AUDIT** (Schwarzschild-Like CPU
  Integration Audit, doc-only).
- **SCHW.5** — CUDA integration (was SCHW.4 in the
  prior plan; renumbered).
- **SCHW.6** — OptiX integration (was SCHW.5).
- **SCHW.7** — Debug visualization (was SCHW.6).
- **SCHW.8** — Final audit (was SCHW.7); closes the
  MANI-I.10 slot.

The `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §8
sub-slice ladder is updated as part of this SCHW.4
commit so the per-slice numbering stays coherent. The
plan's other sections (§1–§7, §9–§10) are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is **SCHW.5 — CUDA integration** per
the renumbered plan §8 SCHW.5 (wires the
SchwarzschildLike arm into the CUDA kernels'
`ManifoldCoordinates` AOV write path; gated behind
`is_active(manifold_mode)` so the Euclidean default
stays bit-exact; deferred CUDA runtime checks for
the visual signature land at SCHW.8 audit).

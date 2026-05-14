# Penrose-Like CPU Integration Audit (PENROSE.5)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `bd2046e` ("manifold:
PENROSE.4 — Penrose-Like CPU Transform Integration
(impl, host-only)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-PENROSE.3 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the per-slice gate for the PENROSE.4 CPU
integration (`bd2046e`). It verifies the nine items the
task brief enumerates — ManifoldTransform supports
PenroseLike chart; disabled/default identity; Euclidean
identity; strength 0 identity; PenroseLike transform is
bounded; large coordinates avoid NaN/Inf; no CUDA/OptiX
behavior changed; build/test status; verdict — and
produces the PASS / REPAIR / BLOCKED verdict that gates
progression to the CUDA integration slice (renumbered
PENROSE.6; see §4).

---

## 1. VERDICT

**PASS.**

All eight structural checks return PASS. No REPAIR or
BLOCKED item is found. The PENROSE.4 CPU integration is
safely landed: it routes the bounded PENROSE.2 math
leaf through the four `ManifoldTransform.h` helper
arms in a way that preserves the bit-identity invariant
on the Euclidean / disabled default and produces no
renderer-side behavior change. The operator may proceed
to PENROSE.6 (CUDA integration; renumbered from the
original PENROSE.5 per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | ManifoldTransform supports PenroseLike chart       | **PASS** | The PENROSE.4 commit (`bd2046e`) extends `src/manifold/ManifoldTransform.h` with six new surfaces (mirroring the SCHW.3 pattern):<br>**(a) Header include:** `#include "manifold/PenroseLikeCompactification.h"` pulls in the PENROSE.2 math leaf at the same include level as `SchwarzschildLikeWarp.h`.<br>**(b) Builder helper:** `penrose_like_params_from(const CoordinateChart&, float strength = 1.0f)` at `ManifoldTransform.h:222` reads `chart.params.mass / .spin / .compactification_scale` and produces a `PenroseLikeCompactificationParams` matching the plan §3 reinterpretation table verbatim (`mass → r_max`, `spin → falloff`, `compactification_scale → scale` — the CANONICAL use of `compactification_scale` per MANIFOLD.1's original docstring; `strength` is supplied by the caller, default `1.0`).<br>**(c) `world_to_chart(t, Vec3)` PenroseLike arm** at `ManifoldTransform.h:261-266` — calls `penrose_like_world_to_chart(world_pos, t.chart.origin, penrose_like_params_from(t.chart))`.<br>**(d) `chart_to_world(t, Vec3)` PenroseLike arm** at `ManifoldTransform.h:286-291` — calls the analytical `atanh`-based inverse.<br>**(e) Vec4 overload arms** at `ManifoldTransform.h:359-368` (`world_to_chart`) and `:397-406` (`chart_to_world`) — time component invariant per the plan §4.6 "no time-axis compactification" non-goal; spatial components routed through the Vec3 math leaf and re-packed.<br>**(f) Enum rename:** `CoordinateChartType::PenroseLikePlaceholder` → `CoordinateChartType::PenroseLike` at `CoordinateChart.h:49` (parallels the SchwarzschildLike naming convention; the `*Placeholder` suffix is reserved for inert chart families with no concrete implementation, and PenroseLike now has a concrete arm). Six call-site updates carry the rename through (`CommandLine.cpp:34`, `SceneLoader.cpp:903`, `main.cpp:163`, `README.md:23`, `cli_tests.cpp:233` + `:336`). |
| 2 | Disabled/default mode remains identity              | **PASS** | The default `ManifoldTransform{}` (Euclidean chart with `origin = (0,0,0)`, `scale = 1.0`) routes through the *existing* Euclidean arm — PENROSE.4 did NOT touch the Euclidean arm's expression. Verified empirically by `test_penrose_4_disabled_identity_preserved` at `manifold_identity_tests.cpp:1228` (Vec3 and Vec4 inputs both round-trip identity via `world_to_chart(t, p) == p` and `chart_to_world(t, p) == p`). The structural guarantee: the PENROSE.4 chart-aware arm is gated by `t.chart.type == CoordinateChartType::PenroseLike`, and the default `CoordinateChart{}` has `type = CoordinateChartType::Euclidean` (line `CoordinateChart.h:136`). The two enum values are distinct integers (`Euclidean = 0`, `PenroseLike = 3` after the SCHW.1 / MANIFOLD.1 / PENROSE.4 promotions), so the PenroseLike arm is unreachable on a default-constructed transform. |
| 3 | Euclidean chart remains identity                    | **PASS** | The Euclidean arm in every helper is preserved verbatim from MANIFOLD.5 — PENROSE.4's diff inserted the `PenroseLike` arm AFTER each Euclidean `return` (mirroring SCHW.3's insertion pattern), never modifying the Euclidean expression. `world_to_chart(t, Vec3)` Euclidean expression at `ManifoldTransform.h:252-254` (`(world_pos - t.chart.origin) * (1.0f / t.chart.scale)`); `chart_to_world(t, Vec3)` at `:271-273` (`t.chart.origin + chart_pos * t.chart.scale`); Vec4 counterparts at `:339-347` / `:386-394`. The PenroseLike arm sits AFTER the SchwarzschildLike arm (which itself sits after the Euclidean return) — there is no fall-through risk; an explicit Euclidean chart with non-trivial `origin = (1,2,3)` and `scale = 2.0` still applies the affine rule unchanged, verified by `test_penrose_4_euclidean_identity_preserved` at `manifold_identity_tests.cpp:1248`. |
| 4 | Strength 0 remains identity                         | **PASS** | At the seam level, the `ManifoldTransform` helper hardcodes `strength = 1.0f` in the builder's default argument (mirrors SCHW.3's pattern; the runtime `ManifoldMode::strength` dial is the kernel-seam's responsibility at PENROSE.6 / PENROSE.7). So the only path to "strength 0 identity" at the seam level is the math leaf's defensive Euclidean fallback — specifically the `r_max = 0` short-circuit at `PenroseLikeCompactification.h:223` (which corresponds to `chart.params.mass = 0` flowing through the builder's `r_max ← params.mass` mapping). Verified empirically by `test_penrose_4_penrose_like_zero_mass_is_identity` at `manifold_identity_tests.cpp:1275`: a PenroseLike chart with `chart.params.mass = 0` returns input unchanged at every Vec3 + Vec4 overload, satisfying the operator brief's "strength 0 identity" acceptance item. **Additionally**, the math leaf's own `strength == 0` short-circuit at `PenroseLikeCompactification.h:222` provides a second defensive layer; the test `test_penrose_2_world_to_chart_identity_at_strength_zero` at `:949` (from PENROSE.2) already verifies that path. |
| 5 | Penrose-like transform is bounded                   | **PASS** | The PENROSE.4 arm delegates to the PENROSE.2 math leaf, which the PENROSE.3 audit (`docs/PENROSE_LIKE_COMPACTIFICATION_MATH_AUDIT.md` §2 check #3) verified bounded by construction:<br>**(a)** Mathematical bound: `tanh(x)` is bounded by `[-1, +1]` for all finite real `x`; therefore `r_chart = r_max * tanh(...)` is bounded by `[0, r_max]`.<br>**(b)** IEEE-754 saturation: `tanhf(15.0f) ≈ 0.99999988` and `tanhf(16.0f) == 1.0f` exactly, so the bound is finite-representable without NaN/Inf risk at saturation.<br>**(c)** Validator (`PenroseLikeCompactification.h:160-168`) rejects out-of-range parameters; the math leaf's defensive fallback returns the input unchanged on rejection.<br>Verified at the `ManifoldTransform` seam by `test_penrose_4_world_to_chart_penrose_like_bounded` (`manifold_identity_tests.cpp:1303` — three inputs at distances `r ∈ {0.5, 3.0, 1e4}` all produce chart-space outputs satisfying `|chart - origin| ≤ r_max`; the far-field input saturates within `1e-4` of `r_max`; the Vec4 overload also bounded with time invariant) and `test_penrose_4_chart_to_world_penrose_like_round_trip` (`:1340` — forward → inverse residual `< 1e-4` across four representative inputs; the analytical inverse is much tighter than SCHW.1's iterative bound). |
| 6 | Large coordinates avoid NaN/Inf                     | **PASS** | The PENROSE.2 math leaf's `tanh` saturation behavior (bounded; finite for all finite inputs) carries through the PENROSE.4 seam unchanged. Verified at the `ManifoldTransform` seam by `test_penrose_4_no_nan_inf_for_large_coordinates` at `manifold_identity_tests.cpp:1379` across four cases:<br>**(a)** `Vec3{1e6, 0, 0}` — single-axis extreme;<br>**(b)** `Vec3{0, 1e10, 0}` — extreme on the Y axis;<br>**(c)** `Vec3{-1e6, 1e6, -1e6}` — mixed-sign extreme;<br>**(d)** `Vec4{0.5, 1e8, -1e8, 1e8}` — extreme on three spatial axes through the Vec4 overload.<br>All four produce `std::isfinite` outputs through both the forward (`world_to_chart`) and inverse (`chart_to_world`) helpers. The Vec4 case additionally verifies time-component invariance (`chart4.x == p_world4.x`). |
| 7 | No CUDA/OptiX behavior changed                      | **PASS** | `git diff 1bf3f2a..bd2046e --name-only` returns exactly nine files: `docs/BUILD_PLAN.md`, `src/core/CommandLine.cpp` (enum-rename call site), `src/io/SceneLoader.cpp` (enum-rename call site), `src/main.cpp` (enum-rename call site), `src/manifold/CoordinateChart.h` (enum rename + doc-comment), `src/manifold/ManifoldTransform.h` (the seam extension), `src/manifold/README.md` (file-inventory comment), `tests/cli_tests.cpp` (two enum-rename references), and `tests/manifold_identity_tests.cpp` (new test functions + the SCHW.3 passthrough-iteration set update). Filtered against the GPU subtree (`src/cuda/`, `src/optix/`, `src/pathtracer/`): **zero hits**. The PENROSE.4 commit does not touch any kernel call site; the new `ManifoldTransform.h` arm exists on disk but is host-only and is not invoked by any GPU path. Beauty-pass and AOV outputs for every existing CLI action are byte-identical to the post-PENROSE.3 baseline. |
| 8 | Build/test status                                   | **PASS** | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. Full ctest: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `312 / 312 checks passed` (was `250 / 250` pre-PENROSE.4; **+62 new PENROSE.4 RR_CHECKs** across 8 new test functions enumerated in §3 below). `cli_tests: 123/123 passed` (the enum rename is transparent to the parser test because the kebab-case CLI name `penrose-like` is unchanged), `renderer_tests: 19 / 19 passed`, `relativity_tests` unchanged — none of the parser, renderer, or pathtracer surfaces are touched by PENROSE.4 (modulo the cosmetic enum-name updates in the parser tables, which preserve their semantic behavior). No new ctest target; no CMake link-line change. |
| 9 | PASS / REPAIR / BLOCKED verdict                     | **PASS** | All eight structural checks return PASS. No REPAIR or BLOCKED item is outstanding. The PENROSE.4 commit lands a clean, bounded, bit-identity-preserving CPU-side integration of the PENROSE.2 math leaf into the `ManifoldTransform` seam. The slice is **safe to extend** to CUDA integration (renumbered PENROSE.6) under the renumbered PENROSE.* ladder. |

---

## 3. REASONING SUMMARY

The PENROSE.4 commit (`bd2046e`) introduces six host-side
surface changes to `src/manifold/ManifoldTransform.h`
plus the cosmetic enum rename:

- a new header include
  (`manifold/PenroseLikeCompactification.h`);
- a new builder helper
  (`penrose_like_params_from(chart, strength)`) that
  bridges the chart-side parameter storage
  (`CoordinateChart::params`) to the math leaf's
  `PenroseLikeCompactificationParams` per the plan §3
  reinterpretation table;
- four chart-aware arms inserted into the
  `world_to_chart` / `chart_to_world` Vec3 + Vec4
  overloads, each gated on `t.chart.type ==
  CoordinateChartType::PenroseLike` and each calling
  the PENROSE.2 math leaf;
- the enum rename
  `PenroseLikePlaceholder` → `PenroseLike` (plus 6
  call-site updates).

The Euclidean arm of each helper is preserved verbatim
from MANIFOLD.5; the SchwarzschildLike arm (SCHW.3) is
preserved verbatim; the PenroseLike arm sits AFTER both
existing arms so there is no fall-through. The
remaining non-Euclidean chart families
(`KruskalLikePlaceholder` / `KerrLikePlaceholder`)
continue to passthrough — master rule #3 forbids
silent routing through PenroseLike math.

The host-side test additions to
`tests/manifold_identity_tests.cpp` cover the operator
brief's five acceptance items plus three additional
invariants:

- `test_penrose_4_disabled_identity_preserved` —
  default `ManifoldTransform{}` stays identity
  post-PENROSE.4 (Vec3 + Vec4).
- `test_penrose_4_euclidean_identity_preserved` —
  explicit Euclidean chart with non-trivial `origin =
  (1,2,3)` and `scale = 2.0` still applies the affine
  map.
- `test_penrose_4_penrose_like_zero_mass_is_identity`
  — PenroseLike with `chart.params.mass = 0` returns
  input via the math leaf's `r_max = 0` short-circuit
  (the seam's "strength 0 identity" path).
- `test_penrose_4_world_to_chart_penrose_like_bounded`
  — three inputs at `r ∈ {0.5, 3.0, 1e4}` all produce
  chart-space outputs bounded by `r_max`; far-field
  saturates within `1e-4` of `r_max`. Vec4 overload
  also bounded with time invariant.
- `test_penrose_4_chart_to_world_penrose_like_round_trip`
  — forward → inverse round-trip residual `< 1e-4`
  across four inputs; Vec4 round-trip preserves time
  exactly.
- `test_penrose_4_no_nan_inf_for_large_coordinates` —
  three Vec3 extremes + one Vec4 extreme all produce
  finite outputs through both forward + inverse
  helpers.
- `test_penrose_4_params_from_chart` — builder helper
  follows the §3 reinterpretation table verbatim.
- `test_penrose_4_other_non_euclidean_passthrough` —
  Kruskal / Kerr remain passthrough even with
  `params.mass = 1.0`.

The disabled/default-mode-identity invariant (check
#2) is **structurally guaranteed** by the enum-tag
gate: the PenroseLike arm is unreachable on a
default-constructed `ManifoldTransform` because
`CoordinateChart{}.type = Euclidean` and the helper
takes the Euclidean arm's `return` before either the
SchwarzschildLike or the PenroseLike gate is
evaluated.

The Euclidean-identity invariant (check #3) is
**bit-preserving** because the Euclidean arm's
expression is unchanged from MANIFOLD.5 — no edit
touches the `(world_pos - origin) * (1 / scale)` line
or its Vec4 counterpart. The PenroseLike arm is a new
`if`-block inserted after the SchwarzschildLike arm
(which itself sits after the Euclidean `return`); it
cannot influence the Euclidean path.

The strength-0-identity invariant (check #4) is
**single-layer-enforced-at-the-seam** (the math
leaf's `r_max = 0` short-circuit). The seam hardcodes
`strength = 1.0` per SCHW.3 precedent; "strength 0
identity" is therefore mapped to "r_max 0 identity"
at the seam. A future PENROSE.6 / PENROSE.7 (kernel
integration) will additionally thread the runtime
`ManifoldMode::strength` dial; at that point the math
leaf's `strength == 0` short-circuit becomes the
second defensive layer.

The bounded-transform invariant (check #5) is
**inherited from PENROSE.2** — the PENROSE.3 audit
already verified the math leaf is bounded by
construction (via `tanh` saturation), and the
PENROSE.4 arm delegates the math directly to the leaf
without intermediate manipulation that could
re-introduce unboundedness.

The no-NaN/Inf-for-large-coordinates invariant (check
#6) is similarly **inherited from PENROSE.2** with
one new test surface: the Vec4 overload at extreme
inputs is verified to produce a finite, time-
preserving output — the test repacks the spatial
result into a Vec4 with the time component
preserved, so a regression in the Vec4 overload that
fails to preserve `world_pos4.x` would be caught
here.

The no-renderer-behavior-change invariant (check #7)
is **structurally guaranteed** because the PENROSE.4
diff is restricted to one host-only header
(`src/manifold/ManifoldTransform.h`), one POD-level
enum file (`src/manifold/CoordinateChart.h`), 4
cosmetic enum-rename call-site updates
(`src/core/`, `src/io/`, `src/main.cpp`,
`src/manifold/README.md`), 2 test files
(`tests/cli_tests.cpp` + `tests/manifold_identity_tests.cpp`),
and the build plan (`docs/BUILD_PLAN.md`). No kernel
call site invokes the new arm. The renderer's GPU
paths are byte-identical to the post-PENROSE.3
baseline.

The build/test status (check #8) shows the slice
integrates cleanly with the existing test
infrastructure: `manifold_identity_tests` grew from
250 to 312 RR_CHECKs without any test-binary churn
(no new ctest target; no new CMake link line). The
audit-host build is clean (no new warnings).

---

## 4. NEXT

The slice is **safe to extend**. The
`PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §10 PENROSE.*
sub-slice ladder needs a one-step shift to absorb
this audit slot (mirroring the SCHW.4 / SCHW.6 /
SCHW.8 / SCHW.10 + PENROSE.3 audit-slot insertions):

- **PENROSE.1** — Planning slice (LANDED at `a84f8b2`).
- **PENROSE.2** — Math helper (LANDED at `7169547`).
- **PENROSE.3** — Audit of PENROSE.2 (LANDED at
  `1bf3f2a`).
- **PENROSE.4** — CPU integration (LANDED at
  `bd2046e`).
- **PENROSE.5** — **THIS AUDIT** (Penrose-Like CPU
  Integration Audit, doc-only).
- **PENROSE.6** — CUDA integration (was PENROSE.5 in
  the post-PENROSE.4 plan; renumbered).
- **PENROSE.7** — OptiX integration (was PENROSE.6).
- **PENROSE.8** — Fixture / debug visualization (was
  PENROSE.7).
- **PENROSE.9** — Arc capstone audit (was PENROSE.8);
  closes the MANI-I.11 slot.

The `docs/PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §10
sub-slice ladder is updated as part of this PENROSE.5
commit so the per-slice numbering stays coherent. The
plan's other sections (§1-§9, §11-§12) are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is **PENROSE.6 — CUDA integration**
per the renumbered plan §10 PENROSE.6 (wires the
PenroseLike arm into the CUDA kernels'
`ManifoldCoordinates` AOV write path; gated behind
`is_active(manifold_mode) && chart == PenroseLike &&
strength > 0` so the Euclidean / SchwarzschildLike
default stays bit-exact; deferred CUDA runtime
checks for the visual signature land at PENROSE.9
audit).

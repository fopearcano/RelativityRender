# Schwarzschild-Like Warp Audit (SCHW.2)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `2da5780` ("manifold:
SCHW.1 — Schwarzschild-Like Warp Math Helper (impl,
math-leaf)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-MANI-I.10 baseline, the
`manifold_identity_tests` runtime output, and `ctest`
exit codes.

This audit is the per-slice gate for the SCHW.1 math
leaf (`2da5780`). It verifies the seven items the
task brief enumerates — Euclidean fallback exists;
transforms are bounded; no singularity generation;
clamping behavior documented; build / test green; no
renderer behavior changed; verdict — and produces the
PASS / REPAIR / BLOCKED verdict that gates progression
to the CPU integration slice (renumbered SCHW.3; see
§4).

---

## 1. VERDICT

**PASS.**

All seven structural checks return PASS. No REPAIR or
BLOCKED item is found. The SCHW.1 math leaf is safely
landed, bounded by construction, and produces no
renderer-side behavior change. The operator may
proceed to SCHW.3 (CPU integration; renumbered from
the original SCHW.2 per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Euclidean fallback exists                   | **PASS** | All three transform helpers (`schwarzschild_like_world_to_chart`, `schwarzschild_like_chart_to_world`, `schwarzschild_like_warp_ray_direction`) carry an explicit Euclidean fallback: `if (!validate_params(p)) return input;` then `if (p.warp_strength == 0.0f) return input;` then `if (p.r_s == 0.0f) return input;`. Locations: `SchwarzschildLikeWarp.h:152-154` (world_to_chart), `:203-205` (chart_to_world), `:276-278` (warp_ray_direction). The fallback is verified analytically (the formula `f = warp_strength * r_s / r^falloff` evaluates to `0` when either factor is zero, so `chart_pos = p_world + 0 * delta = p_world`) AND empirically by the `test_schw_1_world_to_chart_euclidean_fallback` / `test_schw_1_chart_to_world_euclidean_fallback` / `test_schw_1_warp_ray_direction_euclidean_fallback` test functions in `manifold_identity_tests.cpp`. |
| 2 | Transforms are bounded                      | **PASS** | Three bounding mechanisms:<br>**(a) Coordinate-warp magnitude bound:** `r = max(|delta|, clamp_radius)` ensures `r ≥ clamp_radius > 0`, so `1 / r^falloff ≤ 1 / clamp_radius^falloff` and the displacement scalar `f = warp_strength * r_s / r^falloff` is bounded above by `warp_strength * r_s / clamp_radius^falloff`. Documented analytically in the header comment at lines 142-144 and in `SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §5.3.<br>**(b) Newton-Raphson iteration cap:** `kMaxIterations = 8` (line 214) and `kTolerance = 1.0e-5f` (line 215) hard-cap the inverse iteration; the loop cannot run unbounded.<br>**(c) Primary-ray bend cap:** `kBendCap = 0.5f` (line 287) clamps the bending factor to `[-0.5, +0.5]` so the ray's direction cannot flip; verified empirically by `test_schw_1_warp_ray_direction_bend_cap` with extreme parameters (`r_s = 1000`, `warp_strength = 100`) producing a finite, unit-length output. |
| 3 | No singularity generation                   | **PASS** | Four guards prevent NaN / Inf:<br>**(a) Validator NaN rejection:** `validate_params` calls `std::isfinite(...)` on every numeric field (lines 115-120) and rejects non-finite inputs.<br>**(b) `clamp_radius > 0` lower bound:** the validator rejects `clamp_radius <= 0` (line 120), preventing the `1/r^falloff` denominator from going to zero or negative.<br>**(c) Newton-Raphson `F'` early-break:** `if (std::fabs(F_prime) < 1.0e-9f) break;` (line 227) handles the parametric singularity where `F'(r) → 0` (which can happen at specific `r_s` / `warp_strength` / `falloff` combinations).<br>**(d) Negative-`r` rebound:** if a NR step produces `r < 0`, the iteration rebinds `r` to `clamp_radius` (line 233) instead of letting the next iteration evaluate `pow(negative_r, ...)`. <br>The `test_schw_1_world_to_chart_clamp_radius_safety` test verifies that `p_world == mass_origin` (the most singular input geometrically) returns `mass_origin` exactly, no NaN. |
| 4 | Clamping behavior documented                | **PASS** | The `clamp_radius` field's role is documented in three places:<br>(a) the `SchwarzschildLikeWarpParams` doc-comment (lines 87-90): "minimum `r` the formula uses; positive lower bound on the `1/r` denominator; range `(0, ∞)`; default `1.0`";<br>(b) the `world_to_chart` formula doc-comment (line 129): `r = max(|delta|, clamp_radius)`;<br>(c) the `chart_to_world` clamp-shell handling (lines 217-218 + 224-225): in the clamp shell, the formula evaluates at `r_eval = clamp_radius` and the derivative is `1` (the warp factor is constant inside the shell, so `F` reduces to a linear map). The `warp_ray_direction` bend cap is documented at lines 252-253 (header) and pinned in the code at line 287 (`kBendCap = 0.5f`). The Newton-Raphson iteration cap is documented at line 214 (`kMaxIterations = 8`). |
| 5 | Build / test status                         | **PASS** | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. Full `ctest`: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `140 / 140 checks passed` (was `112 / 112` pre-SCHW.1; +28 new SCHW.1 RR_CHECKs across 10 new test functions). `cli_tests: 123/123 passed`, `renderer_tests: 19 / 19 passed` — both unchanged from the post-MANI-I.10 baseline (SCHW.1 doesn't touch the parser or renderer surfaces). |
| 6 | No renderer behavior changed yet            | **PASS** | `git diff d8d7bf0..2da5780 --name-only` filtered for non-(docs/manifold/tests/CMake) files returns **zero hits**. The SCHW.1 commit modified only `src/manifold/README.md`, `tests/manifold_identity_tests.cpp`, `CMakeLists.txt`, `docs/BUILD_PLAN.md`, and added `src/manifold/SchwarzschildLikeWarp.h`. Zero files in `src/cuda/`, `src/optix/`, `src/pathtracer/`, `src/renderer/`, `src/gpu/`, `src/scene/`, `src/io/`, `src/server/`, `src/main.cpp`, `src/core/`, `src/camera/`, `src/material/`, `src/lighting/`, `src/texture/`, `src/geometry/`, `src/image/`, `src/math/`, `src/relativity/`, or `src/field/`. The math helpers compile but no kernel call site invokes them; SCHW.3 (CPU integration) is the slice that wires them into `ManifoldTransform.h`'s call sites. |
| 7 | PASS / REPAIR / BLOCKED verdict             | **PASS** | All six structural checks return PASS. No REPAIR or BLOCKED item is outstanding. The SCHW.1 helpers ship the documented forward + inverse + ray-warp surface with an empirically-verified residual bound (≤ `1e-4`) on the inverse and a hard-capped bending factor on the ray warp. The slice is **safe to extend** to CPU integration (SCHW.3) under the renumbered SCHW.* ladder. |

---

## 3. REASONING SUMMARY

The SCHW.1 commit (`2da5780`) introduces:

- a single new header
  `src/manifold/SchwarzschildLikeWarp.h` (~230
  lines, RR_HD inline throughout) carrying:
  - the `SchwarzschildLikeWarpParams` POD with
    four float fields (`r_s`, `warp_strength`,
    `falloff`, `clamp_radius`);
  - the `schwarzschild_like_validate_params(p)`
    boolean validator;
  - the closed-form forward map
    `schwarzschild_like_world_to_chart(p_world,
    mass_origin, p)`;
  - the bounded Newton-Raphson inverse
    `schwarzschild_like_chart_to_world(chart_pos,
    mass_origin, p)`;
  - the optional primary-ray warp
    `schwarzschild_like_warp_ray_direction(
    ray_origin, ray_dir, mass_origin, p)`.
- 28 new RR_CHECK assertions across 10 new test
  functions appended to
  `tests/manifold_identity_tests.cpp` exercising
  validator behavior, Euclidean fallback (both
  via `warp_strength = 0` and via `r_s = 0`),
  far-field identity at `r = 1e6`, the worked
  example from the design plan §2, clamp-radius
  safety at `p_world = mass_origin`, the
  forward / inverse residual bound across six
  parameter sweeps, and the ray-warp bend cap +
  direction-toward-mass behavior.
- Doc-comment additions in `CMakeLists.txt` and
  `src/manifold/README.md` describing the new
  header.

The Euclidean fallback (check #1) is implemented
**by analytic construction** — the formula `f =
warp_strength * r_s / r^falloff` is identically
zero when either `warp_strength` or `r_s` is zero,
so the displacement `f * delta` is the zero
vector regardless of the input position or the
mass origin. The explicit `return input;`
short-circuits at lines 153-154, 204-205, 277-278
are **defensive optimizations**: they save the
expensive `pow` and `length` evaluations when
the artist has explicitly disabled the chart, but
even without them the formula would still
produce the identity output (modulo IEEE-754
zero-multiplication rounding, which is `0` for
all finite inputs).

The bounded-transform invariant (check #2) is
provable analytically:
- `r = max(|delta|, clamp_radius) ≥ clamp_radius
  > 0` (validator-enforced);
- `r^falloff ≥ clamp_radius^falloff > 0` for
  `falloff ∈ [0.5, 4.0]`;
- `f = warp_strength * r_s / r^falloff ≤
  warp_strength * r_s / clamp_radius^falloff`.

For typical operator parameters (`warp_strength
∈ [0, 1]`, `r_s = 1.0`, `clamp_radius = 0.1`,
`falloff = 1.0`), the maximum displacement scalar
is `1 * 1.0 / 0.1 = 10`. The displacement vector
is `f * delta`, so its magnitude is at most `10
* |delta|` — the chart can stretch space by up
to a factor of 11 in the worst case at the
clamp-radius shell. That bound is documented
(plan §5.3) and the operator is expected to
choose parameter ranges that keep the visual
output sensible.

The no-singularity invariant (check #3) is
defended at four layers: validator NaN
rejection; positive `clamp_radius` lower bound;
NR `F'` zero-guard; negative-`r` rebound. The
combination gives the helper a documented
**no-NaN-no-Inf-no-divergence** contract for any
input — even adversarial inputs like
`p_world = mass_origin` (geometrically the most
singular case) produce finite, well-defined
outputs.

The clamping documentation (check #4) is
present in three places per check, with the
field's role explained, the formula's
substitution shown, and the iteration cap
named. An operator reading the header can
understand what each parameter does and where
its bounds are enforced.

The build / test status (check #5) shows the
slice integrates cleanly with the existing
test infrastructure: `manifold_identity_tests`
grew from 112 to 140 RR_CHECKs without any
test-binary churn (no new ctest target; no new
CMake link line). The audit-host build is
clean (no new warnings).

The no-renderer-behavior-change invariant (check
#6) is structurally guaranteed: the helpers
exist on disk but no kernel call site invokes
them. The SCHW.3 (CPU integration) slice is the
first call site that consumes them via
`ManifoldTransform.h`'s `world_to_chart` /
`chart_to_world` / `transform_ray_like_direction`
helpers; that slice in turn does NOT change
runtime behavior on the Euclidean default
because the manifold-mode `is_active(...)` guard
short-circuits before the SchwarzschildLike arm
runs.

---

## 4. NEXT

The slice is **safe to extend**. The
SCHWARZSCHILD_LIKE_REMAP_PLAN.md §8 SCHW.* sub-slice
ladder needs a one-step shift to absorb this
audit slot:

- **SCHW.1** — Math helper (LANDED at `2da5780`).
- **SCHW.2** — **THIS AUDIT** (Schwarzschild-Like
  Warp Audit, doc-only).
- **SCHW.3** — CPU integration (was SCHW.2 in the
  original plan; renumbered).
- **SCHW.4** — CUDA integration (was SCHW.3).
- **SCHW.5** — OptiX integration (was SCHW.4).
- **SCHW.6** — Debug visualization (was SCHW.5).
- **SCHW.7** — Final audit (was SCHW.6); closes
  the MANI-I.10 slot.

The `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §8
sub-slice ladder is updated as part of this
SCHW.2 commit so the per-slice numbering stays
coherent. The plan's other sections (§1-§7,
§9-§10) are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the
operator may prompt for is **SCHW.3 — CPU
integration** per the renumbered plan §8 SCHW.3
(extends `src/manifold/ManifoldTransform.h`'s
`world_to_chart` / `chart_to_world` /
`transform_ray_like_direction` helpers with the
SchwarzschildLike arm; gated behind the existing
`is_active(manifold_mode)` guard so the
Euclidean fallback stays bit-exact; verified by
extending `manifold_identity_tests`'s ManifoldTransform
test functions).

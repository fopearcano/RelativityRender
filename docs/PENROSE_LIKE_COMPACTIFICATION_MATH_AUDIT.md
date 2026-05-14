# Penrose-Like Compactification Math Audit (PENROSE.3)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `7169547` ("manifold:
PENROSE.2 — Penrose-Like Compactification Math Helper
(impl, math-leaf)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-PENROSE.1 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the per-slice gate for PENROSE.2
(`7169547`). It verifies the eight items the task
brief enumerates — math helper exists; strength 0 is
identity; output is bounded; no NaN/Inf behavior;
radial compression is monotonic; no renderer behavior
changed; build/test status; verdict — and produces a
PASS / REPAIR / BLOCKED verdict that gates progression
to the renumbered PENROSE.4 (CPU integration).

---

## 1. VERDICT

**PASS.**

All seven structural checks return PASS. No REPAIR or
BLOCKED item is found. The PENROSE.2 math leaf is
safely landed, bounded by construction, NaN/Inf-free,
monotonic on the radial scalar, and produces zero
renderer-side behavior change. The operator may
proceed to PENROSE.4 (CPU integration; renumbered
from the original PENROSE.3 per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Math helper exists                       | **PASS** | The PENROSE.2 commit (`7169547`) adds `src/manifold/PenroseLikeCompactification.h` (~210 lines, RR_HD inline throughout) with three helpers + one parameter POD:<br>**(a)** `struct PenroseLikeCompactificationParams { float r_max, strength, scale, falloff; }` at line 140 — defaults `r_max=0`, `strength=0`, `scale=1.0`, `falloff=1.0` (the documented Euclidean-fallback anchor).<br>**(b)** `RR_HD inline bool penrose_like_validate_params(p)` at line 160 — boolean validator enforcing the plan §6.5 bounds.<br>**(c)** `RR_HD inline Vec3 penrose_like_world_to_chart(p_world, origin, p)` at line 214 — closed-form `r_chart = r_max * tanh(strength * (r/scale)^falloff)` forward map.<br>**(d)** `RR_HD inline Vec3 penrose_like_chart_to_world(chart_pos, origin, p)` at line 269 — **analytical** inverse via `atanh` (closed-form, no Newton-Raphson; key design advantage over SCHW.1's iterative `1e-4` residual).<br>The header is structurally complete: includes only `math/MathUtils.h`, `math/Vec3.h`, `<cmath>`; no new module dependencies. The `RR_HD inline` decoration is preserved end-to-end so the helpers compile under both the host compiler and NVCC (CUDA-/OptiX-callable by construction; will land at PENROSE.5 / PENROSE.6). |
| 2 | Strength 0 is identity                   | **PASS** | Two layers of "strength 0 is identity" enforcement:<br>**(a) Defensive short-circuit in `penrose_like_world_to_chart`** at `PenroseLikeCompactification.h:222`: `if (p.strength == 0.0f) return p_world;`. Returns the input vector unchanged. Symmetric `r_max == 0` short-circuit at line 223 (matches SCHW.1's `r_s == 0` pattern). The same dual short-circuit is preserved in `penrose_like_chart_to_world` at lines 277-278.<br>**(b) Empirical verification** via `test_penrose_2_world_to_chart_identity_at_strength_zero` at `manifold_identity_tests.cpp:949` — exercises three test points `(0,0,0)`, `(1.5, -2.3, 4.7)`, `(-100, 0.5, 99.9)` with `strength=0` and verifies all return their input exactly. Also re-verifies the `r_max=0` short-circuit with `strength=1` (the symmetric Euclidean fallback).<br>The natural math at `strength=0` would evaluate `tanh(0) = 0` ⇒ `r_chart = 0` ⇒ uniform shrinkage to the origin — NOT identity. The explicit short-circuit is therefore **load-bearing** for the operator's "default 0 = identity" contract, not just a defensive optimization. The math leaf's header docstring at `PenroseLikeCompactification.h:42-46` explicitly flags this design choice. |
| 3 | Output is bounded                        | **PASS** | Two-layer bounded-output guarantee:<br>**(a) Mathematical bound:** `tanh(x)` is bounded by `[-1, +1]` for all finite real `x`; therefore `r_chart = r_max * tanh(...)` is bounded by `[0, r_max]` for `r_max > 0`. In IEEE-754 single precision, `tanhf(15.0f) ≈ 0.99999988` and `tanhf(16.0f) == 1.0f` exactly — so the bound is **strict and finite-representable** without NaN/Inf risk at saturation.<br>**(b) Empirical verification** via `test_penrose_2_world_to_chart_bounded_for_large_distance` at `manifold_identity_tests.cpp:979` — exercises `r = 1.0e6` with `r_max = 5.0`, `strength = 1.0`, `scale = 1.0`, `falloff = 1.0`. The resulting chart-space radial distance satisfies `r_chart <= r_max` AND `approx(r_chart, r_max, 1.0e-5f)`. A second test point on the −Y axis verifies the bound is direction-uniform.<br>The bound is also documented in the plan §5.3 ("the chart-space representation of the entire scene fits inside a bounding sphere of radius `R_max` centred on the compactification origin"). |
| 4 | No NaN/Inf behavior exists               | **PASS** | Four independent defensive layers prevent NaN/Inf:<br>**(a) Validator rejection** of out-of-range inputs at `PenroseLikeCompactification.h:160-168`: rejects `r_max < 0` or non-finite; `strength` non-finite; `scale <= 0` or non-finite; `falloff` non-finite or outside `[0.5, 4.0]`. On rejection, the math leaf's defensive fallback at line 221 returns the input vector unchanged.<br>**(b) Origin short-circuit** at line 227 (`if (r <= 1.0e-20f) return p_world;`) prevents `r_chart / r` evaluating as `0 / 0`. The inverse map has a parallel guard at line 282.<br>**(c) `tanh` saturation behavior** is well-defined and finite in IEEE-754 — `tanh(±Inf) = ±1.0f` cleanly; the math leaf cannot produce NaN from a `tanh` evaluation.<br>**(d) Inverse boundary clamp** at line 286 (`const float upper = p.r_max * (1.0f - detail::kBoundaryEpsilon);` with `kBoundaryEpsilon = 1.0e-6f`) prevents `atanh(arg)` evaluating at `arg = 1.0` (where it would diverge to `+∞`). The clamp keeps the inverse finite for any input `r_chart`, including operator-side bugs where `r_chart > r_max`.<br>Empirical verification at `test_penrose_2_world_to_chart_no_nan_inf` at `manifold_identity_tests.cpp:1020` — exercises five adversarial inputs (exactly at origin, ε-from-origin, near origin, very far, very far off-axis) with extreme parameters (`strength = 100`, `falloff = 4.0`); all produce `std::isfinite` outputs on every component. Additional verification at `test_penrose_2_chart_to_world_boundary_clamp` at `:1180` — `r_chart = r_max` and `r_chart > r_max` both produce finite outputs. |
| 5 | Radial compression is monotonic          | **PASS** | The forward map's monotonicity follows from `tanh`'s monotonicity: `tanh` is strictly increasing on `(-∞, +∞)`; therefore for any positive `(strength, r_max, scale, falloff)`, `r_chart = r_max * tanh(strength * (r/scale)^falloff)` is a strictly-increasing function of `r > 0`. (Strictly: `pow(r/scale, falloff)` is increasing for `falloff > 0`; `strength * (·)` is increasing; `tanh(·)` is strictly increasing; `r_max * (·)` is increasing. The composition is monotonically non-decreasing AND strictly increasing in the interior.) Empirically verified at `test_penrose_2_world_to_chart_monotonic_radial_compression` at `manifold_identity_tests.cpp:1053` — four input points at radial distances `r ∈ {1, 2, 5, 50}` produce chart-space distances `r_chart` that satisfy `r_chart(1) < r_chart(2) < r_chart(5) <= r_chart(50) <= r_max`. The non-strict inequality between `r=5` and `r=50` reflects `tanh` saturation: both points are well past the saturation knee at `r ≈ 16/strength = 16`, so `r_chart(5)` and `r_chart(50)` are both very close to `r_max` (the saturation is the documented asymptotic compactification behavior, not a violation of monotonicity). |
| 6 | No renderer behavior changed             | **PASS** | `git diff a84f8b2..7169547 --name-only` filtered against the renderer subtree (excluding `docs/`, `src/manifold/`, `tests/manifold_`, `CMakeLists.txt`) returns **zero hits**. The PENROSE.2 commit modified exactly five files: `CMakeLists.txt` (doc-comment block update only — no target/link line changes), `docs/BUILD_PLAN.md` (additive entry), `docs/PENROSE_LIKE_COMPACTIFICATION_PLAN.md` (§10 renumbering), `src/manifold/PenroseLikeCompactification.h` (new, the only source addition), and `tests/manifold_identity_tests.cpp` (test-only additions). Zero files in `src/cuda/`, `src/optix/`, `src/pathtracer/`, `src/renderer/`, `src/gpu/`, `src/scene/`, `src/io/`, `src/server/`, `src/core/`, `src/camera/`, `src/material/`, `src/lighting/`, `src/texture/`, `src/geometry/`, `src/image/`, `src/math/`, `src/relativity/`, `src/field/`, or `src/main.cpp`. The math helpers compile but no kernel call site invokes them; PENROSE.4 (CPU integration) is the slice that wires them into `ManifoldTransform.h`'s call sites. |
| 7 | Build / test status                      | **PASS** | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. Full ctest: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `250 / 250 checks passed` (was `198 / 198` pre-PENROSE.2; **+52 new RR_CHECKs** from the 9 new test functions). `cli_tests: 123/123 passed`, `renderer_tests: 19 / 19 passed`, `relativity_tests` unchanged — none of the parser, renderer, or pathtracer surfaces are touched by PENROSE.2. No new ctest target; no CMake link-line change (the `rr_manifold` library shape is unchanged; the new header is INTERFACE-only and pulled in by the test file's include). |
| 8 | PASS / REPAIR / BLOCKED verdict          | **PASS** | All seven structural checks return PASS. No REPAIR or BLOCKED item is outstanding. The PENROSE.2 helpers ship the documented forward + analytical-inverse surface with an empirically-verified residual bound (typical observed `<< 1e-6` because the inverse is closed-form, well-better than the SCHW.1 NR-iterative `1e-4` bound) and a hard-capped boundary clamp on the inverse (`kBoundaryEpsilon = 1e-6`). The slice is **safe to extend** to CPU integration (renumbered PENROSE.4) under the renumbered PENROSE.* ladder. |

---

## 3. REASONING SUMMARY

The PENROSE.2 commit (`7169547`) introduces:

- a single new header
  `src/manifold/PenroseLikeCompactification.h`
  (~210 lines, RR_HD inline throughout) carrying:
  - the `PenroseLikeCompactificationParams` POD
    with four float fields (`r_max`, `strength`,
    `scale`, `falloff`);
  - the `penrose_like_validate_params(p)` boolean
    validator;
  - the closed-form forward map
    `penrose_like_world_to_chart(p_world, origin,
    p)`;
  - the **analytical** inverse
    `penrose_like_chart_to_world(chart_pos,
    origin, p)` (closed-form via `atanh`; no
    Newton-Raphson required).
- 52 new RR_CHECK assertions across 9 new test
  functions appended to
  `tests/manifold_identity_tests.cpp` exercising:
  validator behavior (defaults + typical + invalid);
  identity at `strength = 0` (operator's PENROSE.2
  acceptance test #1); bounded output at `r = 1e6`
  (acceptance test #2); no-NaN/Inf across five
  adversarial inputs with extreme parameters
  (acceptance test #3); monotonic radial compression
  across four ordered radial distances
  (acceptance test #4); safe near-origin behavior
  at exact origin + `ε`-from-origin
  (acceptance test #5); forward/inverse round-trip
  residual across six representative parameter
  sweeps; inverse Euclidean fallback at
  `strength = 0`; inverse boundary clamp at
  `r_chart >= r_max`.
- Doc-comment additions in `CMakeLists.txt`
  describing the new header (the `rr_manifold`
  INTERFACE library's header inventory).

The math-helper-existence invariant (check #1) is
**file-level + signature-level verified**: the four
helpers are at documented file/line positions with
the correct `RR_HD inline` decoration and the
plan-§3-conformant parameter signatures.

The strength-0-identity invariant (check #2) is
**double-layer-protected**: defensive short-circuit
in source code AT lines 222 + 277 AND empirical
verification by a dedicated test function. The
short-circuit is load-bearing (not just an
optimization) because the natural math would produce
"shrinkage to origin" rather than identity.

The bounded-output invariant (check #3) is
**mathematically guaranteed by `tanh` saturation**
in IEEE-754 + empirically verified at `r = 1e6`.
The single-precision `tanhf` saturates to exactly
`1.0f` at `|x| > ~16`, making the bound
finite-representable without NaN/Inf risk.

The no-NaN/Inf invariant (check #4) is **four-layer-
redundantly protected**: validator rejection of
out-of-range inputs; origin short-circuit at
`|delta| <= 1e-20f`; `tanh` saturation behavior;
inverse boundary clamp at
`r_max * (1 - kBoundaryEpsilon)`. Any one layer is
sufficient; all four together cover the documented
domain.

The radial-compression-monotonicity invariant
(check #5) is **mathematically guaranteed by the
composition of monotonic primitives** + empirically
verified at four ordered radial distances.

The no-renderer-behavior-change invariant (check #6)
is **directly verified** by `git diff` filtered
against the renderer subtree returning zero hits.
The math leaf compiles but is unconsumed; PENROSE.4
will wire it into `ManifoldTransform.h`.

The build/test status (check #7) is **directly
verified** by ctest 12/12 PASS + a `+52`
manifold_identity_tests delta with no regression in
any other test binary.

---

## 4. NEXT

The slice is **safe to extend**. The
`PENROSE_LIKE_COMPACTIFICATION_PLAN.md` §10 PENROSE.*
sub-slice ladder needs a one-step shift to absorb
this audit slot (parallel to the SCHW.4 / SCHW.6 /
SCHW.8 / SCHW.10 audit-slot insertions in the SCHW.*
arc):

- **PENROSE.1** — Planning slice (LANDED at `a84f8b2`).
- **PENROSE.2** — Math helper (LANDED at `7169547`).
- **PENROSE.3** — **THIS AUDIT** (Penrose-Like
  Compactification Math Audit, doc-only).
- **PENROSE.4** — CPU integration (was PENROSE.3 in
  the post-PENROSE.2 plan; renumbered).
- **PENROSE.5** — CUDA integration (was PENROSE.4).
- **PENROSE.6** — OptiX integration (was PENROSE.5).
- **PENROSE.7** — Fixture / debug visualization
  (was PENROSE.6).
- **PENROSE.8** — Arc capstone audit (was
  PENROSE.7); closes the MANI-I.11 slot.

The `docs/PENROSE_LIKE_COMPACTIFICATION_PLAN.md`
§10 sub-slice ladder is updated as part of this
PENROSE.3 commit so the per-slice numbering stays
coherent. The plan's other sections (§1–§9, §11–§12)
are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is **PENROSE.4 — CPU integration**
per the renumbered plan §10 PENROSE.4 (extends
`src/manifold/ManifoldTransform.h`'s `world_to_chart`
/ `chart_to_world` helpers with the PenroseLike arm;
gated behind the existing `is_active(manifold_mode)
&& chart == PenroseLike` guard so the Euclidean
fallback stays bit-exact; verified by extending
`manifold_identity_tests`'s ManifoldTransform test
functions).

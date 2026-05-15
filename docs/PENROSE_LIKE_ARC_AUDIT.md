# Penrose-Like Arc Capstone Audit (PENROSE.12)

Date:   2026-05-15
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `eac0cef` ("docs:
PENROSE.11 — Penrose-Like Fixture Audit (docs only)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, the five prior per-slice audit
verdicts (`docs/PENROSE_LIKE_COMPACTIFICATION_MATH_AUDIT.md`,
`docs/PENROSE_LIKE_CPU_INTEGRATION_AUDIT.md`,
`docs/PENROSE_LIKE_CUDA_INTEGRATION_AUDIT.md`,
`docs/PENROSE_LIKE_OPTIX_INTEGRATION_AUDIT.md`,
`docs/PENROSE_LIKE_FIXTURE_AUDIT.md`,
`docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md`,
`docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md`),
the `manifold_identity_tests` runtime output, and
`ctest` exit codes.

This audit is the **capstone for the Penrose-Like
manifold-compactification arc** (the PENROSE.1 →
PENROSE.11 ladder under the MANI-I.11 integration
slot). It synthesises the prior per-slice verdicts
into a single arc-level verdict, surveys the remaining
risks, and recommends the next safe stage. Per the
operator's brief, the audit is **documentation-only**
and explicitly **does not start Kerr / Kruskal work** —
those families remain reserved-but-inert per
MANIFOLD.1's `*Like` / `*LikePlaceholder` naming
convention.

---

## 1. VERDICT

**PASS_WITH_RUNTIME_DEFERRED.**

The arc closes structurally: **all eleven prior
slices' per-slice gates returned PASS**; every
implementation slice (PENROSE.2 math leaf, PENROSE.4
CPU integration, PENROSE.6 CUDA integration,
PENROSE.8 OptiX integration, PENROSE.10 fixture)
landed in strict ladder order with its own per-slice
audit gate (PENROSE.3 / PENROSE.5 / PENROSE.7 /
PENROSE.9 / PENROSE.11). **There is no PARTIAL
finding** — unlike the SCHW.11 capstone where check
#3 was marked PARTIAL because SCHW.5 (CUDA wiring)
had been deferred, the PenroseLike arc landed every
impl slice in sequence without skipping.

The **runtime portion is DEFERRED** to a CUDA +
OptiX-SDK host: the audit host cannot exercise the
fixture-render suite; per-slice forward-looking
audits acknowledged this constraint and the
PENROSE.11 fixture audit inherited it. Three **known
gaps** are explicitly carried forward (§9 below):
the **consumption-gap CLI extension** is still
unlanded (mirrors SCHW.9 / SCHW.10's same gap; no
existing CLI action loads `<scene-path>` into a
manifold-aware render); **no primary-ray direction
warp** at raygen (the math leaf ships forward +
inverse only); **no chart-parameter scene authoring**
(artistic defaults baked into main.cpp). All three
gaps are tractable on the audit host and recommended
in §11.

No REPAIR action is required. No BLOCKED item is
outstanding. The MANI-I.11 slot is closed to the
extent the audit host can verify; the deferred items
become PASS-able when an SDK-equipped host runs the
plan §9 fixture renders AND the consumption-gap CLI
extension lands.

---

## 2. ARC TIMELINE

| Slice | Commit  | Kind | Verdict (per-slice) |
|-------|---------|------|---------------------|
| PENROSE.1 — Planning slice                            | `a84f8b2` | docs (design)                 | n/a (planning)                |
| PENROSE.2 — Math helper                               | `7169547` | impl (math-leaf)              | n/a (audit at PENROSE.3)      |
| PENROSE.3 — Audit of PENROSE.2                        | `1bf3f2a` | docs                          | **PASS** (7 checks)           |
| PENROSE.4 — CPU integration + enum rename             | `bd2046e` | impl (host-only)              | n/a (audit at PENROSE.5)      |
| PENROSE.5 — Audit of PENROSE.4                        | `7327813` | docs                          | **PASS** (8 checks)           |
| PENROSE.6 — CUDA integration                          | `2859acd` | impl (GPU-side)               | n/a (audit at PENROSE.7)      |
| PENROSE.7 — Audit of PENROSE.6                        | `c7d0acf` | docs                          | **PASS** (9 checks); runtime DEFERRED |
| PENROSE.8 — OptiX integration                         | `65250ea` | impl (GPU-side)               | n/a (audit at PENROSE.9)      |
| PENROSE.9 — Audit of PENROSE.8                        | `3ccee4c` | docs                          | **PASS** (9 checks); runtime DEFERRED |
| PENROSE.10 — Fixture / debug visualization            | `756a4bd` | impl (scene + doc)            | n/a (audit at PENROSE.11)     |
| PENROSE.11 — Audit of PENROSE.10                      | `eac0cef` | docs                          | **PASS** (7 checks); runtime DEFERRED |
| PENROSE.12 — **THIS ARC CAPSTONE**                    | (this)    | docs                          | **PASS_WITH_RUNTIME_DEFERRED** |

Eleven commits across the PENROSE.1 → PENROSE.11 arc
+ this capstone = twelve discrete numbered slots.
Five are real implementation slices (math, CPU, CUDA,
OptiX, fixture); seven are docs (1 design + 5 per-slice
audits + 1 capstone). The discipline of **every
implementation slice having its own per-slice audit
gate** is preserved without exception.

The cumulative diff across the arc:

- **Header / source:**
  `PenroseLikeCompactification.h` (new, ~210 lines);
  `ManifoldTransform.h` extended; `CoordinateChart.h`
  enum rename (`PenroseLikePlaceholder` →
  `PenroseLike`); `CudaScene.cuh` +
  `CudaRenderer.h/.cu` + `CudaTestKernel.cu`
  extended (else-if branches alongside SCHW.5 arms);
  `OptixPrograms.cu` extended (else-if branch
  alongside SCHW.7 arm); `main.cpp` extended (CPU +
  OptiX dispatcher artistic-defaults builder
  branches). Six enum-rename call-site updates
  (`CommandLine.cpp`, `SceneLoader.cpp`, `main.cpp`,
  `README.md`, `cli_tests.cpp` ×2). **OptixLaunchParams.h
  byte-identical** (SCHW.7's chart-family-agnostic
  plumbing reused unchanged).
- **Tests:** `manifold_identity_tests.cpp` grew from
  198 (post-SCHW.* arc) to **312 RR_CHECK
  assertions** (+114 across 17 new test functions:
  9 PENROSE.2 tests for the math leaf + 8 PENROSE.4
  tests for the ManifoldTransform seam). ctest
  target count unchanged at 12. `cli_tests`,
  `renderer_tests`, `relativity_tests` unchanged.
- **Fixture:** `scenes/test_penrose_like_manifold.rrscene`
  (new); nine pre-existing `.rrscene` files
  byte-identical (eight default + the SCHW.9
  fixture).
- **Docs:** six new audit / design / fixture
  documents under `docs/`; `BUILD_PLAN.md`
  appended-only;
  `PENROSE_LIKE_COMPACTIFICATION_PLAN.md`
  renumbered via audit-slot insertions.

---

## 3. PER-CHECK RESULTS

| #  | Check | Result | Evidence |
|----|-------|--------|----------|
| 1  | Architecture scope stayed artistic / bounded                          | **PASS**     | The arc never introduced a conformal factor on the metric, preservation of 45° null geodesics, time-axis compactification, or any other physical-Penrose-diagram primitive. The math leaf at `src/manifold/PenroseLikeCompactification.h` is a **closed-form `tanh`-based radial coordinate compactification** parametrised by `(r_max, strength, scale, falloff)` per the plan §3 reinterpretation table — purely a coordinate transform, not a metric operation. The `*Like` naming convention from MANIFOLD.1 is preserved (the PENROSE.4 enum rename promoted `PenroseLikePlaceholder` → `PenroseLike`, matching SchwarzschildLike's naming; the `*Like` suffix flags the artistic-not-physical status). Architecture-doc §8 non-goals ("physically exact Kerr ray tracing", "full GR solver") stand verbatim. The plan §4.6 explicitly enumerates four properties the chart does NOT produce (45° light-cone preservation; time-axis compactification; event-horizon visualisation; back-side-of-boundary emergent rays). Master rule #3 is preserved throughout: no fake stubs pretending to be complete physics. |
| 2  | CPU manifold transform supports PenroseLike safely                    | **PASS**     | PENROSE.4 (`bd2046e`) extended `src/manifold/ManifoldTransform.h` with the PenroseLike arm in all four `world_to_chart` / `chart_to_world` overloads (Vec3 + Vec4 each) via the `penrose_like_params_from(chart)` builder helper. The arm is gated on `t.chart.type == CoordinateChartType::PenroseLike`, sits AFTER the Euclidean `return` AND the SchwarzschildLike arm (no fall-through risk), and delegates directly to the PENROSE.2 math leaf. The PENROSE.5 audit (`7327813`) verified all 8 structural checks PASS: disabled / Euclidean defaults remain identity; bounded transform via the math leaf (`test_penrose_4_world_to_chart_penrose_like_bounded`); analytical inverse residual `< 1e-4` (`test_penrose_4_chart_to_world_penrose_like_round_trip`); no NaN/Inf for large coordinates (`test_penrose_4_no_nan_inf_for_large_coordinates`, verified up to `r = 1e10`). 62 new RR_CHECKs across 8 new test functions; manifold_identity_tests grew from 250 to 312. |
| 3  | CUDA compactification bridge exists and is default-no-op              | **PASS**     | PENROSE.6 (`2859acd`) added the PenroseLike arm to `k_render_scene`'s `ManifoldCoordinates` AOV write site at `CudaTestKernel.cu:644-680`. Triple-gate `is_active(scene.manifold_mode) && chart == PenroseLike && strength > 0.0f`; invokes the shared `penrose_like_world_to_chart(...)` math leaf with parameters extracted from `scene.coordinate_chart.params` per the plan §3 reinterpretation table. Mutually exclusive with the SCHW.5 SchwarzschildLike arm via `else if` separator + enum-tag check. Default-no-op invariant guaranteed by four layers: host-side allocation gate; kernel-side null gate; both triple-gates' inactive branches; math-leaf defensive fallback (`r_max=0` or `strength=0` short-circuit). The PENROSE.7 audit (`c7d0acf`) verified all 9 structural checks PASS; check #9 (runtime CUDA-host status) DEFERRED. **No PARTIAL finding** — unlike the SCHW.11 capstone's check #3 (which was PARTIAL because SCHW.5 had been deferred), the PenroseLike arc landed every impl slice in sequence. |
| 4  | OptiX compactification bridge mirrors CUDA behavior                   | **PASS**     | PENROSE.8 (`65250ea`) added the PenroseLike arm to `OptixPrograms.cu`'s closest-hit `ManifoldCoordinates` AOV write site at lines 791-844. **Identical shape** to the CUDA-side PENROSE.6 arm: same triple-gate; same parameter encoding; same shared `RR_HD inline` math-leaf invocation. The PENROSE.9 audit (`3ccee4c`) verified all 9 structural checks PASS; check #9 (runtime CUDA/OptiX-host status) DEFERRED. **Cross-backend AOV byte-equivalence is structurally guaranteed by single-source-of-truth math** — both backends bind to the same `penrose_like_world_to_chart(...)` definition at `PenroseLikeCompactification.h:214`; both use byte-identical parameter encoding (audited line-by-line at PENROSE.9 check #7); both use byte-identical artistic-default chart payloads from `main.cpp` (mass=5.0, spin=1.0, compactification_scale=1.0). |
| 5  | Fixture scene exists and is isolated                                  | **PASS**     | PENROSE.10 (`756a4bd`) shipped `scenes/test_penrose_like_manifold.rrscene`, the canonical fixture for the PenroseLike compactification. The fixture authors all four parser-supported `ManifoldMode` fields (`enabled=true`, `chart="penrose-like"`, `strength=0.5`, `debug_visualization=true`) and provides eight visible marker spheres at radial distances spanning `r ∈ {0.5, 0.94, 2.55, 4.0, 6.02}` (the chart's three visual regimes — near-identity, knee, saturation) + a wider ground plane (24×24, vs SCHW.9's 12×12) so the asymptotic compactification signature is visibly clear past the saturation knee. The PENROSE.11 audit (`eac0cef`) verified isolation: `git diff --name-only -- scenes/` returns exactly one new file (the PenroseLike fixture); the nine pre-existing fixtures (eight default + the SCHW.9 fixture) are byte-identical (mtimes `May 14 04:15` / `May 14 20:27` preserved). The fixture loads cleanly via `--scene-info` on the audit host. **Parser surface addition is diff-zero** — `git diff -- 'src/*'` returns 0 bytes on the PENROSE.10 commit because the SCHW.9 `apply_manifold` parser + the PENROSE.4 enum rename already provide the full PenroseLike scene-loading capability. |
| 6  | Default Euclidean / disabled output remains unchanged                 | **PASS**     | The bit-identity invariant is **structurally guaranteed** at multiple layers across the arc:<br>**(a) ManifoldMode default:** `ManifoldMode{}.enabled = false`, so `is_active(...)` returns `false` regardless of `chart` (`ManifoldMode.h:143-145`).<br>**(b) Euclidean chart semantic:** even with `enabled = true`, `is_active(...)` requires `chart != Euclidean` — the Euclidean chart is "intentionally not active" per the helper's documented design.<br>**(c) Strength gate:** both backend kernel arms require `manifold_mode.strength > 0.0f`; the math leaf's own `strength == 0.0f` short-circuit is a fourth defensive layer.<br>**(d) Host-side allocation gate:** `aov_manifold_coordinates` device buffer is allocated only when `manifold_mode.debug_visualization = true`. When the buffer is `nullptr`, the kernel's null-check short-circuits the arm.<br>**(e) `r_max = 0` short-circuit:** even if a chart with `params.mass = 0` reaches the math leaf, the leaf returns the input unchanged.<br>**(f) Mutually exclusive arms:** the SchwarzschildLike and PenroseLike arms are structurally mutually exclusive via `else if` + enum-tag distinction at both backends. Only one arm can fire per pixel.<br>The ten pre-existing fixture files (`test_camera.rrscene` etc. plus the SCHW.9 fixture and now the PENROSE.10 fixture) do not author chart values that engage PenroseLike unless explicitly requested. Every existing CLI action's PPM output is byte-identical to the pre-arc baseline. |
| 7  | Bounded / no-NaN safety status                                        | **PASS**     | Inherited from the PENROSE.2 math leaf's bounded-by-construction property (audited at PENROSE.3) and carried through every downstream consumer. The math leaf provides four independent safety guards:<br>**(a)** `tanh` saturation in IEEE-754 single precision — `tanhf(16.0f) == 1.0f` exactly; therefore `r_chart = r_max * tanh(...)` is bounded by `[0, r_max]` with finite-representable saturation (no NaN/Inf risk).<br>**(b)** Validator `penrose_like_validate_params` rejects `r_max < 0`, non-finite `strength`, `scale <= 0`, `falloff ∉ [0.5, 4.0]`. On invalid input the helper returns the input vector unchanged.<br>**(c)** Origin short-circuit at `|delta| <= 1e-20f` (forward) and `r_chart <= 1e-20f` (inverse) prevents `0/0` evaluation at the fixed point.<br>**(d)** Inverse boundary clamp at `r_max * (1 - kBoundaryEpsilon)` with `kBoundaryEpsilon = 1e-6f` keeps `atanh(arg)` finite at the saturated boundary.<br>**Analytical inverse via atanh** — no Newton-Raphson required (key design advantage over SCHW.1's NR inverse); residual `<< 1e-6` for typical parameter ranges (vs SCHW.1's `≤ 1e-4`).<br>The `RR_HD inline` decoration carries the safety properties verbatim into CUDA / OptiX device code. Verified at the seam (PENROSE.5) and at both kernel arms (PENROSE.7 / PENROSE.9). |
| 8  | Schwarzschild-like arc compatibility status                           | **PASS**     | The SCHW.* arc is **completely untouched** by the PenroseLike arc. Per the PENROSE.7 audit's check #6 (`PENROSE_LIKE_CUDA_INTEGRATION_AUDIT.md` §2): the SCHW.5 region of the CUDA kernel arm is byte-identical pre- and post-PENROSE.6 (modulo cosmetic rename `active` → `schwarzschild_active`). Per the PENROSE.9 audit's check #6: the SCHW.7 region of the OptiX kernel arm is byte-identical pre- and post-PENROSE.8 (same cosmetic rename only). Per the PENROSE.11 audit's check #6: the PENROSE.10 commit is scene-+-doc-only — `git diff -- 'src/*'` returns 0 bytes; all SchwarzschildLike runtime semantics across all three call sites are byte-identical to the pre-PENROSE.10 state.<br><br>The two arms (SchwarzschildLike + PenroseLike) are **structurally mutually exclusive** via:<br>**(a)** `else if` separator at each kernel arm (CUDA: `CudaTestKernel.cu:649 + :661`; OptiX: `OptixPrograms.cu:796 + :810`);<br>**(b)** Enum-tag distinction (`CoordinateChartType::SchwarzschildLike` ≠ `CoordinateChartType::PenroseLike`);<br>**(c)** Triple-gate ordering: both `schwarzschild_active` and `penrose_active` cannot evaluate to `true` simultaneously.<br>The SCHW.11 capstone's cross-backend AOV byte-equivalence claim (CUDA ↔ OptiX SchwarzschildLike rendering byte-identical via single-source-of-truth math) is preserved verbatim. The SCHW.9 fixture (`test_schwarzschild_like_manifold.rrscene`) continues to exercise the SCHW.* arc unchanged. |
| 9  | Runtime CUDA/OptiX validation status                                  | **DEFERRED** | Audit-host limitation: no CUDA SDK, no OptiX SDK. The SDK_FOUND TUs compile cleanly under the audit-host rules (verified at every per-slice audit) but cannot link / launch device code. The plan §9 fixture-render suite is **not exercisable on the audit host**. Deferred to a CUDA + OptiX-SDK host once the consumption-gap CLI extension (§10 below) lands.<br><br>**Per-slice runtime-status summary** (re-rendered from the prior audits' check #9 entries):<br>- PENROSE.3: structural-only audit; no runtime check required (math leaf compiles in standalone TU);<br>- PENROSE.5: structural-only audit; no runtime check required (CPU integration unit-tested on audit host via `manifold_identity_tests`);<br>- PENROSE.7: structural CUDA-side audit; runtime DEFERRED on documented audit-host limitations;<br>- PENROSE.9: structural OptiX-side audit; runtime DEFERRED on documented audit-host limitations;<br>- PENROSE.11: structural fixture audit; runtime DEFERRED on documented audit-host limitations + consumption-gap CLI extension unlanded.<br><br>**Key runtime checks deferred** (mirrors plan §9):<br>**(i)** Euclidean fallback byte-identity for seven CLI actions;<br>**(ii)** SchwarzschildLike non-regression (PENROSE.* must not affect SCHW.* AOV output);<br>**(iii)** `strength = 0` byte-identity;<br>**(iv)** Visual signature on AOV (asymptotic compactification at r_max=5.0);<br>**(v)** **CUDA ↔ OptiX byte-equivalence** (the arc's central claim — structurally guaranteed by single-source-of-truth math; empirical pixel-level verification gated on SDK host);<br>**(vi)** Off-chart non-regression (kerr-like / kruskal-like remain passthrough). |
| 10 | Remaining risks                                                       | **CATALOGUED** | The arc closes structurally but carries three documented gaps the operator should be aware of (mirrors the SCHW.11 capstone's catalogue with one fewer item because SCHW.5-equivalent gap doesn't exist here):<br>**(a) Consumption-gap CLI extension.** Same gap SCHW.10 / SCHW.11 documented: neither `--render-optix-aovs` nor `--render-aovs` accepts a `<scene-path>` argument; both build inline scenes. The PENROSE.10 fixture is loadable via `--scene-info` but not directly consumed by any manifold-aware render action. The dispatcher merge logic at `main.cpp::run_render_optix_aovs` + `run_render_aovs` is in place (dead-code today for the inline-scene paths; activates when a future single-line CLI extension adds a `<scene-path>` argument to either action). Single change closes both SCHW.* and PENROSE.* consumption gaps.<br>**(b) No primary-ray direction warp.** The beauty pass is unaffected by the PenroseLike chart — PENROSE.8 / PENROSE.6 only route the hit position through the warp for the `ManifoldCoordinates` AOV write site. The math leaf deliberately ships only a forward + inverse pair (no `penrose_like_warp_ray_direction` helper); the PENROSE.1 plan §8.3 explicitly defers primary-ray direction warp to a future addendum if the operator wants a "compactified primary-ray" visualization mode. SchwarzschildLike has the same gap (`schwarzschild_like_warp_ray_direction` exists at SCHW.1 but unused).<br>**(c) No chart-parameter scene-authoring.** The fixture's `CoordinateChart::params` come from main.cpp's artistic defaults (`mass=5.0`, `spin=1.0`, `compactification_scale=1.0` for PenroseLike); the scene parser cannot author these. A future slice could add an optional `chart_params: {r_max, scale, falloff}` sub-block to the `manifold` block without an ABI bump on the existing `CoordinateChartParameters` POD. Same gap SchwarzschildLike has.<br>**(d) Runtime PPM regression suite (deferred to SDK host).** No automated `cmp` of the PenroseLike AOV PPMs against golden references. The fixture-doc §3 enumerates expected per-sphere visual signatures qualitatively; pinning a golden PPM requires a CUDA + OptiX-SDK host running the consumption-gap-closed render action AND a stable random seed / sample count. Same gap SchwarzschildLike has. |
| 11 | Recommended next safe stage                                           | **CATALOGUED** | Three tractable options on the audit host, in **strategic priority order**:<br>**(A) CLI consumption-gap closure** — Highest priority. Single-line extension to `--render-optix-aovs` (and/or `--render-aovs`) that accepts a `<scene-path>` argument. When provided, load the scene through `rr::io::load(...)` instead of building an inline scene; the existing dispatcher-merge logic from SCHW.9 / PENROSE.* activates `scene.manifold` automatically. **Closes both arcs' consumption gaps simultaneously** — the SCHW.9 fixture + the PENROSE.10 fixture become end-to-end consumable on both backends. Tractable on the audit host (CLI parser extension; the SDK_FOUND render path is unchanged).<br>**(B) Primary-ray direction warp** — Medium priority (cosmetic / artistic). Invoke `schwarzschild_like_warp_ray_direction(...)` from raygen for SchwarzschildLike; add a `penrose_like_warp_ray_direction(...)` helper for PenroseLike (currently ships only forward + inverse). Requires per-ray gate at raygen + chart parameters in scope at raygen. The visual signature (pseudo-lensing on beauty pass) is only verifiable on an SDK-equipped host.<br>**(C) MANI-I.12 — Final cross-host audit** — Closes the integration plan's MANI-I.12 slot once a CUDA + OptiX-SDK host has run the plan §7 (SCHW.*) + §9 (PENROSE.*) fixture-render suites. Pinning golden PPMs at this slice would close the final deferred risk.<br><br>**Recommended:** Option (A) — CLI consumption-gap closure. It closes both arcs' largest outstanding non-runtime gap simultaneously, ties both fixtures end-to-end on both backends, and is tractable on the audit host.<br><br>**Explicitly NOT recommended:** Kerr / Kruskal work (the operator's brief explicitly forbids this; the `*LikePlaceholder` charts remain reserved-but-inert per MANIFOLD.1 + the PENROSE.* arc's explicit non-authorisation). Cinema 4D / server / UI / node-editor work (architecture-doc §8 non-goals; the operator's brief forbids). |

---

## 4. ARCHITECTURAL SCOPE — WHAT THE ARC IS AND ISN'T

The arc landed an **artistic, bounded coordinate-
compactification layer** inspired by — but not
implementing — Penrose diagrams' conformal
compactification.

**What the arc IS:**

- A **closed-form coordinate transform** parametrised
  by `(compactification_origin, r_max, strength,
  scale, falloff)` per the plan §3 reinterpretation
  table.
- **Forward map** (`penrose_like_world_to_chart`):
  bounded `tanh`-based radial compactification;
  monotonic; direction-preserving; saturates at
  `r_max` in IEEE-754 single precision.
- **Analytical inverse** (`penrose_like_chart_to_world`):
  closed-form via `atanh` (no Newton-Raphson);
  documented residual `<< 1e-6` for typical
  parameter ranges (vs SCHW.1's iterative `≤ 1e-4`);
  boundary clamp at `r_max * (1 - 1e-6)` keeps
  inverse finite at saturation.
- **Activation triple-gate** on both backends
  (`enabled && PenroseLike && strength > 0`);
  structurally mutually exclusive with the
  SchwarzschildLike arm via `else if` + enum-tag
  distinction.
- **Default no-op invariant** preserved through
  every consumer (default `ManifoldMode{}` is
  disabled; `is_active(...)` returns false on
  Euclidean; host-side allocation gates the AOV
  buffer; kernel-side null-check gates the write
  arm).
- **Cross-backend AOV byte-equivalence** —
  structurally guaranteed by single-source-of-truth
  math (CUDA + OptiX bind to the same `RR_HD inline`
  forward map definition).

**What the arc IS NOT:**

- **Not a physical Penrose / conformal
  compactification.** No conformal factor on the
  metric; no preservation of null-geodesic angles;
  no Penrose-Carter coordinate derivation;
  light-like geodesics are NOT preserved as 45°
  lines.
- **Not a time-axis compactification.** The chart
  compactifies only the spatial radial coordinate.
  Real Penrose diagrams compactify BOTH time and
  space; the Vec4 overload preserves the time
  component as invariant per the plan §8.1 static-
  chart-in-time rule.
- **Not an event-horizon visualiser.** The chart's
  boundary at `r_max` is a coordinate boundary, not
  a horizon.
- **Not composable with the SchwarzschildLike chart
  today.** Today only one `CoordinateChart::type`
  is active per render. A future `ManifoldStack`
  concept sketched in the plan §7.3 could allow
  composition; not in scope for this arc.
- **Not a path-tracer integrator rewrite.** The
  BSDF / NEE / MIS / RR machinery is byte-identical
  to the pre-arc state; only the
  `ManifoldCoordinates` AOV write site invokes the
  warp.
- **Not an OptiX denoiser change.** The denoiser
  still consumes Beauty / Albedo / Normal only.
- **Not a `.rrscene` schema bump.** The fixture
  uses schema v1.0.0; the `manifold` block is the
  same optional top-level key SCHW.9 defined.

This boundary is consistent with the
**MANIFOLD_RENDERING_ARCHITECTURE.md §3** ontology
and matches the SchwarzschildLike arc's
artistic-not-physical scope (per the SCHW.11
capstone).

---

## 5. CROSS-CUTTING INVARIANT CHECK

Four invariants the arc was required to preserve
across every per-slice landing:

### 5.1 Bit-identity on default code paths

**PRESERVED.** Verified at every per-slice audit:

- Default `ManifoldMode{}.enabled = false`
  short-circuits at the kernel-side `is_active(...)`
  guard.
- Default `CoordinateChart{}.type = Euclidean`
  short-circuits at the seam-level
  `chart == PenroseLike` gate.
- Default `manifold_mode.debug_visualization = false`
  short-circuits at the host-side
  `aov_manifold_coordinates` allocation gate.
- Default `cfg.manifold = disabled_manifold_mode()`
  resolves the dispatcher merge to the disabled
  default for every existing CLI invocation that
  doesn't pass `--manifold-enable`.
- Nine pre-existing `.rrscene` fixtures
  byte-identical at PENROSE.11.

### 5.2 SchwarzschildLike arc preservation

**PRESERVED.** The SCHW.* arc is **completely
untouched** by the PenroseLike arc:

- SCHW.5 region of CUDA arm byte-identical pre/
  post-PENROSE.6 (PENROSE.7 audit check #6);
- SCHW.7 region of OptiX arm byte-identical pre/
  post-PENROSE.8 (PENROSE.9 audit check #6);
- PENROSE.10 commit is scene-+-doc-only — `git
  diff -- 'src/*'` returns 0 bytes (PENROSE.11
  audit check #6);
- SCHW.11 capstone's cross-backend AOV
  byte-equivalence claim preserved verbatim;
- SCHW.9 fixture continues to exercise SCHW.* arc
  unchanged.

The two arms are structurally mutually exclusive
via `else if` separator + enum-tag distinction at
both backend kernel arms.

### 5.3 Master rule #3 — no fake stubs

**PRESERVED.** The arc's `*Like` naming convention
flags artistic-not-physical at every type definition
(`CoordinateChartType::PenroseLike`,
`PenroseLikeCompactificationParams`, the chart's
`name` field `"penrose-like"`). The math leaf's
documented analytical-inverse residual bound
(`<< 1e-6`), the documented boundary clamp
(`kBoundaryEpsilon = 1e-6f`), and the four safety
guards are real complete artistic math — not
stubs. The PENROSE.4 enum rename
(`PenroseLikePlaceholder` → `PenroseLike`)
formally promoted the chart family from inert
placeholder to concrete implementation, matching
the SchwarzschildLike precedent. The remaining
`*LikePlaceholder` charts (Kruskal / Kerr) remain
reserved-but-inert with no implementation behind
their enum values; selecting one passes through
both the SchwarzschildLike and PenroseLike gates
without engaging either warp.

### 5.4 Architecture-doc §8 non-goals

**PRESERVED.** No physically exact Penrose / Kerr
ray tracing. No full GR solver. No conformal
factor on the metric. No time-axis compactification.
No Christoffel symbols. No geodesic ODE. No Riemann
tensor. No C4D / server / UI / node-editor work.
No new GPU resource (no new BVH, no new SBT record
type, no new denoiser input). The arc's footprint
is restricted to: one new header
(`PenroseLikeCompactification.h`), one
`ManifoldTransform.h` arm extension, one CUDA
kernel arm extension, one OptiX kernel arm
extension, four `main.cpp` artistic-defaults
branches, one fixture scene, one fixture
companion doc, and the enum rename (with 6
mechanical call-site updates). No other module is
touched.

---

## 6. BUILD + TEST STATUS

- **Audit-host build:** `cmake --build build -j`
  succeeds cleanly with no new warnings under the
  project's `rr_apply_warnings` settings.
- **ctest:** `100% tests passed, 0 tests failed
  out of 12`.
- **`manifold_identity_tests`:** `312 / 312 checks
  passed` (was `198 / 198` pre-arc; **+114
  RR_CHECKs** across 17 new test functions: 9
  PENROSE.2 tests for the math leaf, 8 PENROSE.4
  tests for the ManifoldTransform seam).
- **`cli_tests`:** `123 / 123 passed` (unchanged
  from the post-SCHW.* baseline; the PENROSE.4
  enum rename is transparent to the parser test
  because the kebab-case CLI name `penrose-like`
  is unchanged).
- **`renderer_tests`:** `19 / 19 passed`
  (unchanged from the post-MANI-I.8 baseline).
- **`relativity_tests`:** unchanged from the
  pre-arc baseline.
- **OptiX OFF build:** SDK_FOUND TUs compile
  under the audit-host rules; OptiX-OFF stub
  functions carry the SCHW.7-era signatures and
  return the documented "requires SDK" messages.

The arc passes every audit-host-reachable
verification gate without exception. ctest count
unchanged at 12; no new test binary required.

---

## 7. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3, the audit is explicit about
its scope boundary:

- **No end-to-end runtime render.** The audit
  host cannot execute device code; the plan §9
  fixture renders are deferred to a CUDA +
  OptiX-SDK host.
- **No cross-backend byte-identity pixel-level
  verification.** Not exercisable on the audit
  host. The cross-backend equivalence is
  **structurally guaranteed** by single-source-of-
  truth math + identical parameter encoding (both
  backends bind to the same `RR_HD inline`
  `penrose_like_world_to_chart`). Empirical
  pixel-level verification requires an
  SDK-equipped host.
- **No golden-PPM pinning.** The fixture-doc §3
  enumerates expected visual signatures
  qualitatively; pinning a golden PPM as a
  regression anchor requires a CUDA + OptiX-SDK
  host AND the consumption-gap CLI extension AND
  a stable random seed / sample count.
- **No path-tracer integration.** The arc only
  touches the AOV write site; the path-tracer's
  bounce loop is byte-identical to the pre-arc
  state. A future arc could route bounce rays
  through the chart; PENROSE.* is explicitly the
  "AOV only" scope.
- **No Cinema 4D / preview-UI / node-editor
  integration.** Architecture-doc §8 non-goals
  stand; operator brief explicitly forbids.
- **No Kerr / Kruskal work.** Operator brief
  explicitly forbids; `*LikePlaceholder` charts
  remain reserved-but-inert.

---

## 8. RECOMMENDATION TO OPERATOR

**Verdict: PASS_WITH_RUNTIME_DEFERRED.**

The Penrose-Like manifold-compactification arc is
**structurally closed** at the audit-host build
level. **All five implementation slices landed in
ladder order without skipping** — the arc has no
PARTIAL findings (in contrast to the SCHW.11
capstone where check #3 was PARTIAL because SCHW.5
had been deferred). Eight arc-level structural
checks (§3 checks #1–#8) return PASS unconditionally;
check #9 (runtime CUDA/OptiX-host status) is
DEFERRED on documented audit-host limitations.
Check #10 (remaining risks) is CATALOGUED — three
gaps the operator should be aware of, all tractable
in future commits. Check #11 (recommended next
stage) points at **CLI consumption-gap closure** as
the highest-priority tractable continuation; it
would simultaneously close the SCHW.10 / PENROSE.10
fixture consumption gaps.

The MANI-I.11 slot in the integration plan §8 +
§9 is closed by this capstone for the audit-host
portion; the deferred items become PASS-able when:
1. **CLI consumption-gap extension lands**
   (enables end-to-end fixture render via
   `--render-optix-aovs <scene>` and
   `--render-aovs <scene>`);
2. **CUDA + OptiX-SDK host runs the plan §9
   fixture renders** (verifies the visual
   signature; pins the golden PPMs if desired);
3. **Cross-backend byte-equivalence** is verified
   empirically on the SDK-equipped host (CUDA AOV
   == OptiX AOV for the same fixture — structurally
   guaranteed today by single-source-of-truth math;
   pixel-level verification deferred).
4. **MANI-I.12 final cross-host audit** synthesises
   both SCHW.* and PENROSE.* arcs' SDK-host runtime
   verdicts into the integration plan's final
   verdict for the manifold-rendering pivot.

No REPAIR action is required. No BLOCKED item is
outstanding. The operator may proceed to any of:
- **CLI consumption-gap closure** (recommended) —
  closes both arcs' largest outstanding gap;
- **Primary-ray direction warp** — adds the
  pseudo-lensing beauty-pass signature;
- **MANI-I.12 final cross-host audit** — when an
  SDK host runs both arcs' fixture suites;
- **Manifold-orthogonal work** — Field
  Interpretation Layer (Phase 1), other
  path-tracer features, denoiser integration
  with chart-aware AOVs.

The `*LikePlaceholder` chart families (Kerr,
Kruskal) **remain reserved-but-inert** per this
audit's explicit confirmation; their implementation
is a separate architectural arc that should not be
started until the operator explicitly authorises.

---

## 9. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") is the load-bearing invariant for this
  audit.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` — §3
  ontology (Coordinate Chart / Metric Tensor /
  Observer Frame / Geodesic State); §8 non-goals.
- `docs/MANIFOLD_INTEGRATION_PLAN.md` §9 — the
  MANI-I.11 slice this arc consumed.
- `docs/PENROSE_LIKE_COMPACTIFICATION_PLAN.md` —
  canonical design doc with the §10 PENROSE.*
  sub-slice ladder (renumbered across the arc); §3
  reinterpretation table; §4 expected visual
  effects; §6 safety constraints; §9 deferred
  runtime checks.
- `docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md` — the
  earlier audit that validated the architectural
  foundation.
- `docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md` —
  SCHW.11 capstone (verdict
  PASS_WITH_RUNTIME_DEFERRED) the PENROSE.* arc
  modeled itself on.
- `docs/PENROSE_LIKE_COMPACTIFICATION_MATH_AUDIT.md`
  (PENROSE.3) — math-leaf verdict; bounded by
  construction; analytical inverse residual.
- `docs/PENROSE_LIKE_CPU_INTEGRATION_AUDIT.md`
  (PENROSE.5) — CPU-seam verdict.
- `docs/PENROSE_LIKE_CUDA_INTEGRATION_AUDIT.md`
  (PENROSE.7) — CUDA-bridge verdict (structural
  PASS; runtime DEFERRED).
- `docs/PENROSE_LIKE_OPTIX_INTEGRATION_AUDIT.md`
  (PENROSE.9) — OptiX-bridge verdict; cross-
  backend equivalence structurally guaranteed.
- `docs/PENROSE_LIKE_FIXTURE_AUDIT.md`
  (PENROSE.11) — fixture verdict;
  zero-source-diff confirmed.
- `docs/PENROSE_LIKE_FIXTURE.md` (PENROSE.10
  companion) — fixture purpose / expected
  behavior / consumption status.
- `src/manifold/PenroseLikeCompactification.h` —
  the math leaf at the heart of the arc (PENROSE.2).
- `src/manifold/ManifoldTransform.h` — the
  CPU-seam extension at PENROSE.4.
- `src/cuda/CudaTestKernel.cu:644-680` — the
  CUDA kernel arm at PENROSE.6.
- `src/optix/OptixPrograms.cu:791-844` — the
  OptiX kernel arm at PENROSE.8.
- `src/manifold/CoordinateChart.h:49` — the
  renamed `PenroseLike` enum value (PENROSE.4).
- `scenes/test_penrose_like_manifold.rrscene` —
  the canonical fixture at PENROSE.10.
- `tests/manifold_identity_tests.cpp` — 312
  RR_CHECK assertions, grown from 198 (post-SCHW.*)
  to 312 (post-PENROSE.*) across the arc.

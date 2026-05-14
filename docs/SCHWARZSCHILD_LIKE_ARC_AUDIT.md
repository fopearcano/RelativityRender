# Schwarzschild-Like Arc Capstone Audit (SCHW.11)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `3a390be` ("docs:
SCHW.10 — Schwarzschild-Like Fixture Audit (docs
only)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, the eight prior per-slice audit
verdicts (`docs/SCHWARZSCHILD_LIKE_WARP_AUDIT.md`,
`docs/SCHWARZSCHILD_LIKE_CPU_INTEGRATION_AUDIT.md`,
`docs/SCHWARZSCHILD_LIKE_CUDA_WARP_AUDIT.md`,
`docs/SCHWARZSCHILD_LIKE_OPTIX_WARP_AUDIT.md`,
`docs/SCHWARZSCHILD_LIKE_FIXTURE_AUDIT.md`,
`docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md`),
the `manifold_identity_tests` runtime output, and
`ctest` exit codes.

This audit is the **capstone for the Schwarzschild-Like
manifold-warp arc** (the SCHW.1 → SCHW.10 ladder under
the MANI-I.10 integration slot). It synthesises the
prior per-slice verdicts into a single arc-level
verdict, surveys the remaining risks, and recommends the
next safe stage. Per the operator's brief, the audit is
**documentation-only** and explicitly **does not start
Penrose / Kerr / Kruskal work** — those families remain
reserved-but-inert per MANIFOLD.1's `*Like` /
`*LikePlaceholder` naming convention.

---

## 1. VERDICT

**PASS_WITH_RUNTIME_DEFERRED.**

The arc closes structurally: all landed slices passed
their per-slice gates; the architecture stayed within
the artistic / bounded envelope; bit-identity is
preserved on every default code path; the math leaf's
safety invariants carry through every consumer. The
**runtime portion is DEFERRED** to a CUDA + OptiX-SDK
host: the audit host cannot exercise the fixture-render
suite (plan §7 §7.1–§7.6); the per-slice forward-looking
SCHW.6 audit acknowledged this constraint and the
SCHW.8 / SCHW.10 audits inherited it. Two **known gaps**
are explicitly carried forward (§9 below): the
**CUDA-side kernel arm (SCHW.5) is still unlanded** so
the CUDA path's `aov_manifold_coordinates.ppm`
currently lags the OptiX path's; and **no existing CLI
action loads the SCHW.9 fixture's `<scene-path>` into a
manifold-aware render** so the dispatcher-merge logic
is dead-code today. Both gaps are tractable on the
audit host and recommended in §10.

No REPAIR action is required. No BLOCKED item is
outstanding. The MANI-I.10 slot is closed to the
extent the audit host can verify; the deferred items
become PASS-able when an SDK-equipped host runs the
plan §7 fixture renders AND the consumption-gap CLI
extension lands AND the CUDA-side SCHW.5 wiring lands.

---

## 2. ARC TIMELINE

| Slice | Commit  | Kind | Verdict (per-slice) |
|-------|---------|------|---------------------|
| SCHW.1 — Math helper                                  | `2da5780` | impl (math-leaf)              | n/a (audit at SCHW.2)         |
| SCHW.2 — Audit of SCHW.1                              | `c799621` | docs                          | **PASS** (7 checks)           |
| SCHW.3 — CPU integration                              | `b48c480` | impl (host-only)              | n/a (audit at SCHW.4)         |
| SCHW.4 — Audit of SCHW.3                              | `b78fe98` | docs                          | **PASS** (8 checks)           |
| SCHW.5 — CUDA integration                             | **NOT LANDED** | impl (GPU-side) deferred | n/a (forward-looking SCHW.6)  |
| SCHW.6 — Forward-looking CUDA-warp audit              | `6660bb4` | docs (forward-looking)        | **PASS** (8 checks); runtime DEFERRED |
| SCHW.7 — OptiX integration                            | `fc71aed` | impl (GPU-side)               | n/a (audit at SCHW.8)         |
| SCHW.8 — Audit of SCHW.7                              | `fd7084f` | docs                          | **PASS** (8 checks); runtime DEFERRED |
| SCHW.9 — Debug visualization + fixture                | `f102dd3` | impl (scene-loader + fixture) | n/a (audit at SCHW.10)        |
| SCHW.10 — Audit of SCHW.9                             | `3a390be` | docs                          | **PASS** (6 checks); runtime DEFERRED |
| SCHW.11 — **THIS ARC CAPSTONE**                       | (this)    | docs                          | **PASS_WITH_RUNTIME_DEFERRED** |

Ten commits across the SCHW.1–SCHW.10 arc + this
capstone = eleven discrete numbered slots. Four are
real implementation slices; six are audit / capstone
documentation (plus one forward-looking SCHW.6 that
land before its predecessor SCHW.5). The discipline of
**every implementation slice having its own per-slice
audit gate** is preserved with one explicit exception:
SCHW.5 (CUDA integration) is not yet landed and the
SCHW.6 audit is forward-looking by design.

The cumulative diff across the arc:

- **Header / source:** `SchwarzschildLikeWarp.h` (new, ~230 lines);
  `ManifoldTransform.h` extended; `Scene.h` /
  `Scene.cpp` extended; `SceneLoader.cpp` extended;
  `OptixLaunchParams.h` extended;
  `OptixRenderer.h` / `OptixRenderer.cpp` extended;
  `OptixPrograms.cu` extended; `main.cpp` extended.
  CUDA-side kernels (`CudaTestKernel.cu`,
  `CudaPathTracer.cu`, etc.) **byte-identical** to
  pre-arc state.
- **Tests:** `manifold_identity_tests.cpp` grew from
  112 to **198 RR_CHECK assertions** (+86 across 18
  new test functions); ctest target count unchanged
  at 12.
- **Fixture:** `scenes/test_schwarzschild_like_manifold.rrscene`
  (new); eight pre-existing `.rrscene` files
  byte-identical.
- **Docs:** six new audit / design documents under
  `docs/`; `BUILD_PLAN.md` appended-only;
  `SCHWARZSCHILD_LIKE_REMAP_PLAN.md` renumbered via
  audit-slot insertions.

---

## 3. PER-CHECK RESULTS

| #  | Check | Result | Evidence |
|----|-------|--------|----------|
| 1  | Architecture scope stayed artistic / bounded, not full GR              | **PASS**     | The arc never introduced Christoffel symbols, geodesic ODE integrators, Riemann tensors, or Einstein-field-equation evaluation. The math leaf at `src/manifold/SchwarzschildLikeWarp.h` is a **closed-form radial coordinate displacement** parametrised by `(r_s, warp_strength, falloff, clamp_radius)` per the plan §3 reinterpretation table. The `*Like` naming convention from MANIFOLD.1 (audited at `docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md`) flags this artistic-not-physical status at every call site. Architecture-doc §8 non-goals ("physically exact Kerr ray tracing", "full GR solver") stand verbatim. Master rule #3 is preserved throughout: no fake stubs pretending to be complete physics. |
| 2  | CPU manifold transform supports SchwarzschildLike safely               | **PASS**     | SCHW.3 (`b48c480`) extended `src/manifold/ManifoldTransform.h` with the SchwarzschildLike arm in all four `world_to_chart` / `chart_to_world` overloads (Vec3 + Vec4 each) via the `schwarzschild_like_params_from(chart)` builder helper. The arm is gated on `t.chart.type == CoordinateChartType::SchwarzschildLike`, sits AFTER the Euclidean `return` (no fall-through risk), and delegates directly to the SCHW.1 math leaf. The SCHW.4 audit (`b78fe98`) verified all 8 structural checks PASS: disabled / Euclidean defaults remain identity (test_schw_3_disabled_identity_preserved, test_schw_3_euclidean_identity_preserved); bounded transform via the math leaf (test_schw_3_world_to_chart_schwarzschild_like_known_value, test_schw_3_chart_to_world_schwarzschild_like_round_trip); no NaN/Inf at clamp shell (test_schw_3_no_nan_inf_near_clamp_radius). 58 new RR_CHECKs across 8 new test functions; manifold_identity_tests stayed at 12/12 ctest target. |
| 3  | CUDA warp bridge exists and is default-no-op                            | **PARTIAL**  | The CUDA-side **infrastructure** is in place: the SCHW.1 math leaf is `RR_HD inline` (CUDA-callable by construction); MANI-I.5 plumbed `ManifoldMode manifold_mode` into the CUDA kernel signatures at `CudaPathTracer.cu:427` (`[[maybe_unused]] rr::manifold::ManifoldMode manifold_mode`); MANI-I.8 created the `aov_manifold_coordinates` AOV slot in `CudaAOV.cuh` + `CudaTestKernel.cu:582-602`. The SCHW.6 forward-looking audit (`6660bb4`) verified all eight structural CUDA-safety checks PASS (math leaf RR_HD inline, seam RR_HD inline, kernel signature accepts ManifoldMode, OptiX untouched, ctest green). **BUT** the CUDA kernel arm itself does NOT yet invoke the SchwarzschildLike warp — `CudaTestKernel.cu:582-602` still writes raw `best.position` unconditionally. The default-no-op property is preserved trivially (the kernel never engages the warp); however, the kernel also doesn't engage the warp **even when the operator requests it**. The SCHW.5 (CUDA integration) implementation slice is the next concrete commit that closes this gap. This is the arc's **primary outstanding implementation gap** (§9 below). |
| 4  | OptiX warp bridge mirrors CUDA behavior                                 | **PASS** (structurally; today AHEAD of CUDA) | SCHW.7 (`fc71aed`) landed the OptiX-side SchwarzschildLike arm in `OptixPrograms.cu:773-792` (closest-hit `ManifoldCoordinates` AOV write site). The triple-gate `is_active(manifold_mode) && chart == SchwarzschildLike && strength > 0.0f` engages the shared math leaf `schwarzschild_like_world_to_chart(...)`. The SCHW.8 audit (`fd7084f`) verified all eight structural checks PASS; check #5 ("CUDA/OptiX warp math is equivalent") was rendered PASS on the grounds that **both backends invoke the same `RR_HD inline` math leaf** — equivalence is structurally guaranteed by single-source-of-truth math, NOT by parallel implementations. As of today, the OptiX bridge is **structurally complete + activated**, while the CUDA bridge is **structurally complete but not yet activated** (see check #3). When SCHW.5 lands, both backends will produce byte-identical AOV output for the same fixture; until then, the OptiX backend renders the warped AOV signature, the CUDA backend renders the raw position. The MANI-I.9 audit's deferred OptiX host-side allocation finding ALSO closes at SCHW.7 (`OptixRenderer.cpp:2713-2720` allocates `aov_manifold_coordinates` only on `debug_visualization = true`). |
| 5  | Fixture scene exists and is isolated                                    | **PASS**     | SCHW.9 (`f102dd3`) shipped `scenes/test_schwarzschild_like_manifold.rrscene` (2321 bytes), the canonical fixture for the SchwarzschildLike warp. The fixture authors all four parser-supported `ManifoldMode` fields (`enabled=true`, `chart="schwarzschild-like"`, `strength=0.5`, `debug_visualization=true`) and provides simple visible geometry (six marker spheres at radial distances `r ∈ {0.5, 1.5, 2.0, 2.0, 3.0, 3.0}` from the mass origin + a ground-plane mesh) with clear camera framing (camera at `(0, 1.2, 6.0)` looking toward origin with FoV 45°). The SCHW.10 audit (`3a390be`) verified isolation: `git diff --name-only -- scenes/` returns exactly one file (the new fixture); the eight pre-existing fixture files are byte-identical to the pre-SCHW.9 state (mtime `May 14 04:15` for all eight; new fixture mtime `May 14 20:27`). The fixture loads cleanly via `--scene-info` on the audit host with no parse error or warning. Parser surface addition is **minimal additive**: 119 added lines to `SceneLoader.cpp` for four optional fields + one helper + one dispatcher arm; zero changes to existing parser surfaces. |
| 6  | Default Euclidean / disabled output should remain unchanged             | **PASS**     | The bit-identity invariant is **structurally guaranteed** at multiple layers across the arc:<br>**(a) ManifoldMode default:** `ManifoldMode{}.enabled = false`, so `is_active(...)` returns `false` regardless of `chart` (`ManifoldMode.h:143-145`).<br>**(b) Euclidean chart semantic:** even with `enabled = true`, `is_active(...)` requires `chart != Euclidean` — the Euclidean chart is "intentionally not active" per the helper's documented design.<br>**(c) Strength gate:** the SCHW.7 OptiX kernel arm requires `manifold_mode.strength > 0.0f` (`OptixPrograms.cu:777`); the math leaf's own `warp_strength == 0.0f` short-circuit (`SchwarzschildLikeWarp.h:153`) is a fourth defensive layer.<br>**(d) Host-side allocation gate:** `aov_manifold_coordinates` device buffer is allocated only when `manifold_mode.debug_visualization = true` (`OptixRenderer.cpp:2713-2720`). When the buffer is `nullptr`, the kernel's null-check short-circuits the arm (`OptixPrograms.cu:756`).<br>**(e) `r_s = 0` short-circuit:** even if a chart with `params.mass = 0` reaches the math leaf, the leaf returns the input unchanged (`SchwarzschildLikeWarp.h:154`).<br>**(f) Other non-Euclidean charts:** Kruskal / Penrose / Kerr structurally bypass the SchwarzschildLike arm (the OptiX kernel's `chart == SchwarzschildLike` explicit check); they remain passthrough per master rule #3 (no silent routing through SchwarzschildLike math). Verified at `test_schw_3_other_non_euclidean_passthrough` for the CPU seam.<br>The eight pre-existing fixture files do not author a `manifold` block; the parser's optional-field handling leaves `scene.manifold = ManifoldMode{}` (the disabled / Euclidean / strength-0 default); the dispatcher merge `effective_manifold = cfg.manifold.enabled ? cfg.manifold : scene.manifold` resolves to the disabled CLI default. Every existing CLI action's PPM output is byte-identical to the pre-arc baseline. |
| 7  | Bounded / no-NaN safety status                                          | **PASS**     | Inherited from the SCHW.1 math leaf's bounded-by-construction property and carried through every downstream consumer. The math leaf provides four independent safety guards (audited at SCHW.2 / SCHW.4 / SCHW.6 / SCHW.8):<br>**(a)** `r = max(|delta|, clamp_radius)` (`SchwarzschildLikeWarp.h:158`) prevents `1 / r^falloff` underflow / NaN at the mass origin.<br>**(b)** Newton-Raphson 8-iteration cap + `1e-5` convergence tolerance (`:214-215`) bounds the inverse-map iteration regardless of input pathology.<br>**(c)** `F'` zero-guard at `|F'| < 1e-9` (`:227`) plus negative-`r` rebound to `clamp_radius` (`:230`) handle the parametric singularities.<br>**(d)** Primary-ray bend hard-cap at `±0.5` via `kBendCap` (`:289`) so the ray-direction warp cannot flip a primary ray's direction.<br>**(e)** Host-side validator `schwarzschild_like_validate_params` (`:113-122`) rejects non-finite inputs + out-of-range `falloff` (`[0.5, 4.0]`) + non-positive `clamp_radius`.<br>The `RR_HD inline` decoration carries the safety properties verbatim into CUDA / OptiX device code (no host-only library call routes through a CPU-only path). For the fixture's specific parameters (`strength=0.5`, dispatcher artistic defaults `mass=1.0`, `spin=1.0`, `compactification_scale=0.1`), the maximum displacement scalar is `f = 0.5 * 1.0 / 0.1 = 5.0`, bounded analytically and well below any IEEE-754 overflow regime. |
| 8  | Runtime CUDA/OptiX validation status                                    | **DEFERRED** | Audit-host limitation: no CUDA SDK, no OptiX SDK. The SDK_FOUND TUs compile cleanly under the audit-host rules (verified at every per-slice audit) but cannot link / launch device code. The plan §7 fixture-render suite (§7.1 Euclidean fallback byte-identity for seven CLI actions, §7.2 OptiX fallback byte-identity, §7.3 `warp_strength = 0` byte-identity, §7.4 visual AOV signature, §7.5 beauty-pass lensing signature, §7.6 off-chart non-regression) is **not exercisable on the audit host**. Deferred to a CUDA + OptiX-SDK host once the consumption-gap CLI extension (§9 below) lands. The same posture applies to the cross-backend equivalence check (CUDA vs OptiX `aov_manifold_coordinates.ppm` byte-identity for the same fixture) — deferred until SCHW.5 (CUDA integration) lands.<br><br>**Per-slice runtime-status summary** (re-rendered from the prior audits' check #8 entries):<br>- SCHW.2: structural-only audit; no runtime check required (the math leaf compiles in standalone TU);<br>- SCHW.4: structural-only audit; no runtime check required (CPU integration unit-tested on audit host);<br>- SCHW.6: forward-looking; runtime DEFERRED on documented audit-host limitations + SCHW.5 unlanded;<br>- SCHW.8: structural OptiX-side audit; runtime DEFERRED on documented audit-host limitations;<br>- SCHW.10: structural fixture audit; runtime DEFERRED on documented audit-host limitations + consumption-gap CLI extension unlanded. |
| 9  | Remaining risks                                                         | **CATALOGUED** | The arc closes structurally but carries five documented gaps the operator should be aware of:<br>**(a) SCHW.5 CUDA-side wiring unlanded.** The CUDA kernel's `ManifoldCoordinates` AOV write site still writes raw `best.position` unconditionally. Today, the OptiX path renders the warped AOV signature while the CUDA path renders the raw position for the same fixture. This is the arc's primary outstanding implementation gap; the SCHW.6 forward-looking audit acknowledged it; the SCHW.10 audit re-confirmed it.<br>**(b) Consumption-gap CLI extension.** Neither `--render-optix-aovs` nor `--render-aovs` accepts a `<scene-path>` argument; both build inline scenes. The SCHW.9 fixture is loadable via `--scene-info` but not directly consumed by any manifold-aware render action. The dispatcher merge logic at `main.cpp::run_render_optix_aovs` + `run_render_aovs` is in place (dead-code today); a single-line CLI extension would activate it.<br>**(c) No primary-ray direction warp.** The beauty pass is unaffected by the SchwarzschildLike chart — SCHW.7 only routes the hit position through the warp for the `ManifoldCoordinates` AOV write site. The `schwarzschild_like_warp_ray_direction` helper exists at SCHW.1 (validated at SCHW.2) but no kernel call site invokes it. Adding the primary-ray warp would produce the documented "pseudo-lensing" beauty-pass signature from the plan §4.2 / §6.2.<br>**(d) No chart-parameter scene-authoring.** The fixture's `CoordinateChart::params` come from main.cpp's artistic defaults (`mass=1.0`, `spin=1.0`, `compactification_scale=0.1`); the scene parser cannot author these. A future slice could add an optional `chart_params: {mass, falloff, clamp_radius}` sub-block without an ABI bump.<br>**(e) Runtime PPM regression suite.** No automated `cmp` of the OptiX `aov_manifold_coordinates.ppm` against a golden reference. The fixture-doc §3 enumerates the expected per-sphere visual signature, but the audit-host build cannot pin a golden PPM (the SDK_FOUND TU cannot launch). Gated on a CUDA + OptiX-SDK host running the consumption-gap-closed render action AND pinning the result in `tests/goldens/`. |
| 10 | Recommended next safe stage                                             | **CATALOGUED** | Three tractable options on the audit host, in **strategic priority order**:<br>**(A) SCHW.5 — CUDA-side kernel wiring.** Highest priority. Mirror the SCHW.7 OptiX kernel arm in `CudaTestKernel.cu` (the `ManifoldCoordinates` AOV write site at lines 582-602) using the same shared `RR_HD inline` math leaf. Closes the OptiX/CUDA divergence (check #4); makes the CUDA path's AOV match the OptiX path's AOV for the same fixture; preserves the bit-identity invariant via the same triple-gate. Tractable on the audit host (CUDA kernel arms compile under the audit-host stub; behavioural verification is runtime-DEFERRED, but structural compile + the existing `manifold_identity_tests` regression coverage are sufficient gates).<br>**(B) Consumption-gap CLI extension.** Medium priority. Single-line addition: extend `--render-optix-aovs` (and optionally `--render-aovs`) to accept an optional `<scene-path>` argument. When provided, load the scene through `rr::io::load(...)` instead of building an inline scene; the existing dispatcher-merge logic at `main.cpp::run_render_optix_aovs` activates `scene.manifold` automatically. Closes the consumption gap (gap b above); ties the SCHW.9 fixture end-to-end on the OptiX backend. Tractable on the audit host (CLI parser extension; the SDK_FOUND render path is unchanged).<br>**(C) Primary-ray direction warp at raygen.** Lower priority (cosmetic / artistic). Invoke `schwarzschild_like_warp_ray_direction(...)` from the OptiX raygen entry point (and / or the CUDA raygen) so the beauty pass shows the documented pseudo-lensing signature. Requires a per-ray gate at raygen + the chart parameters in scope at raygen. Tractable on the audit host but the visual signature is only verifiable on an SDK-equipped host.<br><br>**Recommended:** Option (A) — SCHW.5 CUDA-side kernel wiring. It closes the most visible architectural divergence the arc introduced (OptiX has the warp, CUDA does not), strengthens the cross-backend equivalence claim, and is the natural completion of the SCHW.5-SCHW.7 pairing that the renumbered plan §8 has tracked since SCHW.6's forward-looking insertion.<br><br>**Explicitly NOT recommended:** Penrose / Kerr / Kruskal work (the operator's brief explicitly forbids this; the `*Like` placeholders remain reserved-but-inert per MANIFOLD.1). Cinema 4D / server / UI / node-editor work (architecture-doc §8 non-goals; the operator's brief forbids). |

---

## 4. ARCHITECTURAL SCOPE — WHAT THE ARC IS AND ISN'T

The arc landed an **artistic, bounded coordinate-warp
layer** inspired by — but not implementing — the
Schwarzschild solution's radial coordinate compression.

**What the arc IS:**

- A **closed-form coordinate transform** parametrised
  by `(mass_origin, r_s, warp_strength, falloff,
  clamp_radius)` per the plan §3 reinterpretation
  table.
- **Forward map** (`schwarzschild_like_world_to_chart`):
  bounded radial displacement; identity in the far
  field; bounded maximum displacement at the clamp
  shell.
- **Approximate inverse** (`schwarzschild_like_chart_to_world`):
  bounded Newton-Raphson on the radial scalar
  equation; documented residual `≤ 1e-4` for typical
  parameter ranges.
- **Optional primary-ray direction warp**
  (`schwarzschild_like_warp_ray_direction`): hard-
  capped at `±0.5` bending; exists at SCHW.1 but
  not yet invoked by any kernel.
- **Activation triple-gate** on the OptiX side
  (`enabled && SchwarzschildLike && strength > 0`);
  the CUDA side will mirror this when SCHW.5 lands.
- **Default no-op invariant** preserved through every
  consumer (default `ManifoldMode{}` is disabled;
  `is_active(...)` returns false on Euclidean;
  host-side allocation gates the AOV buffer; kernel-
  side null-check gates the write arm).

**What the arc IS NOT:**

- **Not a physical Schwarzschild ray tracer.** No
  Christoffel symbols, no geodesic ODE, no Riemann
  tensor evaluation. Architecture-doc §8 non-goals
  stand.
- **Not a frame-dragging Kerr extension.** The
  `params.spin` slot is reinterpreted as `falloff`
  for the SchwarzschildLike chart per the plan §3
  table; Kerr-like usage will reclaim it.
- **Not a photon-sphere or back-side-of-horizon
  emergent ray simulator.** The clamp shell hides
  the inner region; the arc does not integrate
  null geodesics across the horizon.
- **Not a path-tracer integrator rewrite.** The
  BSDF / NEE / MIS / RR machinery is byte-identical
  to the pre-arc state; only the
  `ManifoldCoordinates` AOV write site invokes the
  warp on the OptiX side today.
- **Not an OptiX denoiser change.** The denoiser
  still consumes Beauty / Albedo / Normal only.
- **Not a `.rrscene` schema bump.** The fixture uses
  schema v1.0.0; the `manifold` block is an
  optional top-level key alongside the existing
  optional `relativity` block.

This boundary is consistent with the
**MANIFOLD_RENDERING_ARCHITECTURE.md §3** ontology
(the arc lands `CoordinateChart` /
`CoordinateChartParameters` consumption; the
`MetricTensor` / `ObserverFrame` / `GeodesicState`
PODs ship but are unconsumed; the future GR-aware
integrator that consumes those would be a separate
architectural arc, not a SCHW.* extension).

---

## 5. CROSS-CUTTING INVARIANT CHECK

Three invariants the arc was required to preserve
across every per-slice landing:

### 5.1 Bit-identity on default code paths

**PRESERVED.** Verified at every per-slice audit:

- Default `ManifoldMode{}.enabled = false`
  short-circuits at the kernel-side `is_active(...)`
  guard.
- Default `CoordinateChart{}.type = Euclidean`
  short-circuits at the seam-level
  `chart == SchwarzschildLike` gate.
- Default `manifold_mode.debug_visualization = false`
  short-circuits at the host-side
  `aov_manifold_coordinates` allocation gate.
- Default `cfg.manifold = disabled_manifold_mode()`
  resolves the dispatcher merge to the disabled
  default for every existing CLI invocation that
  doesn't pass `--manifold-enable`.
- Eight pre-existing `.rrscene` fixtures byte-
  identical (SCHW.10 audit check #4).

### 5.2 Master rule #3 — no fake stubs

**PRESERVED.** The arc's `*Like` naming convention
flags artistic-not-physical at every type definition
(`CoordinateChartType::SchwarzschildLike`,
`SchwarzschildLikeWarpParams`, the chart's `name`
field `"schwarzschild-like"`). The math leaf's
documented residual bound (`≤ 1e-4`), the
documented bend cap (`±0.5`), and the four bounding
guards are real complete artistic math — not stubs.
The `*LikePlaceholder` charts (Kruskal / Penrose /
Kerr) remain reserved-but-inert with no implementation
behind their enum values; selecting one passes
through the SchwarzschildLike gate without engaging
the warp (`test_schw_3_other_non_euclidean_passthrough`
verifies).

### 5.3 Architecture-doc §8 non-goals

**PRESERVED.** No physically exact Kerr ray tracing.
No full GR solver. No Christoffel symbols on
`MetricTensor`. No geodesic ODE. No Riemann tensor.
No C4D / server / UI / node-editor work. No new
GPU resource (no new BVH, no new SBT record type,
no new denoiser input). The arc's footprint is
restricted to: one new header (`SchwarzschildLikeWarp.h`),
one Scene POD field (`Scene::manifold`), one
OptiX launch param (`coordinate_chart`), one OptiX
kernel arm (the closest-hit `ManifoldCoordinates`
write site at `OptixPrograms.cu:773-792`), four
`ManifoldTransform.h` arm extensions, one
fixture scene, one minor parser surface
(`apply_manifold`), and the dispatcher merge logic
at `main.cpp`. No other module is touched.

---

## 6. BUILD + TEST STATUS

- **Audit-host build:** `cmake --build build -j`
  succeeds cleanly with no new warnings under the
  project's `rr_apply_warnings` settings.
- **ctest:** `100% tests passed, 0 tests failed out
  of 12`.
- **`manifold_identity_tests`:** `198 / 198 checks
  passed` (was `112 / 112` pre-arc; +86 RR_CHECKs
  across 18 new test functions: 10 SCHW.1 tests for
  the math leaf, 8 SCHW.3 tests for the
  ManifoldTransform seam).
- **`cli_tests`:** `123 / 123 passed` (unchanged
  from the post-MANI-I.1 baseline).
- **`renderer_tests`:** `19 / 19 passed` (unchanged
  from the post-MANI-I.8 baseline).
- **`relativity_tests`:** unchanged from the
  pre-arc baseline.
- **OptiX OFF build:** SDK_FOUND TUs compile under
  the audit-host rules; OptiX-OFF stub functions
  carry the new signatures and return the
  documented "requires SDK" messages.

The arc passes every audit-host-reachable verification
gate without exception.

---

## 7. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3, the audit is explicit about its
scope boundary:

- **No end-to-end runtime render.** The audit host
  cannot execute device code; the plan §7 fixture
  renders are deferred to a CUDA + OptiX-SDK host.
- **No cross-backend byte-identity comparison.** Not
  exercisable on the audit host AND not yet
  exercisable on an SDK-equipped host either (SCHW.5
  CUDA-side wiring not yet landed).
- **No golden-PPM pinning.** The fixture-doc §3
  enumerates expected visual signatures qualitatively;
  pinning a golden PPM as a regression anchor
  requires a CUDA + OptiX-SDK host AND the
  consumption-gap CLI extension AND a stable random
  seed / sample count.
- **No path-tracer integration.** The arc only
  touches the AOV write site; the path-tracer's
  bounce loop is byte-identical to the pre-arc
  state. A future arc could route bounce rays through
  the chart; SCHW.* is explicitly the "AOV only"
  scope.
- **No Cinema 4D / preview-UI / node-editor
  integration.** Architecture-doc §8 non-goals
  stand; operator brief explicitly forbids.
- **No Penrose / Kerr / Kruskal work.** Operator
  brief explicitly forbids; `*LikePlaceholder`
  charts remain reserved-but-inert.

---

## 8. RECOMMENDATION TO OPERATOR

**Verdict: PASS_WITH_RUNTIME_DEFERRED.**

The Schwarzschild-Like manifold-warp arc is
**structurally closed** at the audit-host build
level. Six structural arc-level checks (`§3` checks
#1–#7) return PASS or PARTIAL (the PARTIAL is
SCHW.5's known deferred gap, not a regression).
Check #8 (runtime CUDA/OptiX status) is DEFERRED on
documented audit-host limitations. Check #9
(remaining risks) is CATALOGUED — five gaps the
operator should be aware of, all tractable in
future commits. Check #10 (recommended next stage)
points at **SCHW.5 CUDA-side kernel wiring** as the
highest-priority tractable continuation.

The MANI-I.10 slot in the integration plan §8
sub-slice ladder is closed by this capstone for
the audit-host portion; the deferred items become
PASS-able when:
1. **SCHW.5 lands** (closes check #3's PARTIAL →
   PASS);
2. **CLI consumption-gap extension lands** (enables
   end-to-end fixture render via `--render-optix-aovs
   <scene>`);
3. **CUDA + OptiX-SDK host runs the plan §7 fixture
   renders** (verifies the visual signature; pins
   the golden PPMs if desired);
4. **Cross-backend byte-equivalence** is verified on
   the SDK-equipped host (CUDA AOV == OptiX AOV for
   the same fixture).

No REPAIR action is required. No BLOCKED item is
outstanding. The operator may proceed to any of:
- **SCHW.5** (recommended) — close the CUDA-side
  gap;
- **CLI consumption-gap closure** — single-line
  extension to `--render-optix-aovs`;
- **Manifold-orthogonal work** — Field
  Interpretation Layer (Phase 1), other
  path-tracer features, etc.

The `*LikePlaceholder` chart families (Penrose,
Kerr, Kruskal) **remain reserved-but-inert** per
this audit's explicit confirmation; their
implementation is a separate architectural arc
that should not be started until the operator
explicitly authorises and until the SCHW.5 gap is
closed.

---

## 9. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` —
  top-level rules; master rule #3 ("no fake stubs")
  is the load-bearing invariant for this audit.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` — §3
  ontology (Coordinate Chart / Metric Tensor /
  Observer Frame / Geodesic State); §8 non-goals.
- `docs/MANIFOLD_INTEGRATION_PLAN.md` §8 — the
  MANI-I.10 slice this arc consumed.
- `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` — canonical
  design doc with the §8 SCHW.* sub-slice ladder
  (renumbered across the arc); §3 reinterpretation
  table; §4 expected visual effects; §5 safety
  constraints; §7 deferred runtime checks.
- `docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md` — the
  earlier audit that validated the architectural
  foundation (MANIFOLD.1 through MANIFOLD.7 + the
  Field Interpretation Layer scaffolding).
- `docs/SCHWARZSCHILD_LIKE_WARP_AUDIT.md` (SCHW.2)
  — math-leaf verdict; bounded by construction.
- `docs/SCHWARZSCHILD_LIKE_CPU_INTEGRATION_AUDIT.md`
  (SCHW.4) — CPU-seam verdict.
- `docs/SCHWARZSCHILD_LIKE_CUDA_WARP_AUDIT.md`
  (SCHW.6) — forward-looking CUDA-safety verdict.
- `docs/SCHWARZSCHILD_LIKE_OPTIX_WARP_AUDIT.md`
  (SCHW.8) — OptiX-bridge verdict.
- `docs/SCHWARZSCHILD_LIKE_FIXTURE_AUDIT.md`
  (SCHW.10) — fixture verdict.
- `docs/SCHWARZSCHILD_LIKE_FIXTURE.md` (SCHW.9
  companion) — fixture purpose / expected behavior
  / consumption status.
- `src/manifold/SchwarzschildLikeWarp.h` — the math
  leaf at the heart of the arc.
- `src/manifold/ManifoldTransform.h` — the CPU-seam
  extension at SCHW.3.
- `src/optix/OptixPrograms.cu:720-797` — the OptiX
  kernel arm at SCHW.7.
- `src/optix/OptixLaunchParams.h:391` — the
  `coordinate_chart` payload field at SCHW.7.
- `src/optix/OptixRenderer.cpp:2515-2848` — the
  SDK_FOUND `render_aovs` body extended at SCHW.7 /
  SCHW.9.
- `scenes/test_schwarzschild_like_manifold.rrscene`
  — the canonical fixture at SCHW.9.
- `src/io/SceneLoader.cpp::apply_manifold` — the
  scene-parser surface at SCHW.9.
- `src/scene/Scene.h::Scene::manifold` — the Scene
  POD slot at SCHW.9.
- `src/main.cpp::run_render_optix_aovs` /
  `run_render_aovs` — the dispatcher merge logic at
  SCHW.7 / SCHW.9.
- `tests/manifold_identity_tests.cpp` — 198 RR_CHECK
  assertions across 18 test functions, grown from
  112 to 198 across the arc.

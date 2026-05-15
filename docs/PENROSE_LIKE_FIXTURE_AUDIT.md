# Penrose-Like Fixture Audit (PENROSE.11)

Date:   2026-05-15
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `756a4bd` ("scene:
PENROSE.10 — Penrose-Like Fixture / Debug
Visualization (impl, scene + companion doc)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-PENROSE.9 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the per-slice gate for PENROSE.10
(`756a4bd`). It verifies the eight items the task brief
enumerates — fixture scene exists; fixture uses
PenroseLike manifold mode; values are bounded/safe;
default scenes remain unchanged; parser changes are
minimal; Schwarzschild behavior unchanged; CUDA/OptiX
runtime status; verdict — and produces the PASS /
REPAIR / BLOCKED verdict that gates progression to
the renumbered PENROSE.12 (arc capstone audit).

---

## 1. VERDICT

**PASS.**

All seven structural checks return PASS. No REPAIR or
BLOCKED item is found. Check #7 (CUDA/OptiX runtime
status) is DEFERRED on documented audit-host
limitations, matching the SCHW.10 / PENROSE.* per-slice
audit posture. The PENROSE.10 fixture is safe and
isolated: the scene file exists; it authors the
PenroseLike manifold mode through all four parser-
supported fields; every value is within the math leaf's
documented safe range; no default scene is altered; the
parser surface is zero-touched (reuses SCHW.9 verbatim);
the Schwarzschild behavior is unchanged. The operator
may proceed to PENROSE.12 (arc capstone audit;
renumbered from the original PENROSE.11 per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Fixture scene exists                            | **PASS**     | `scenes/test_penrose_like_manifold.rrscene` is present on the post-PENROSE.10 tree (`ls -la scenes/` confirms; the SCHW.9 fixture `test_schwarzschild_like_manifold.rrscene` retains its `May 14 20:27` mtime; the new PenroseLike fixture has a fresh mtime from the PENROSE.10 commit). The file is the **only** new entry in the `scenes/` subtree per `git diff 3ccee4c..756a4bd --name-only -- scenes/`, which returns exactly that filename. The fixture loads cleanly through `RelativityRender --scene-info scenes/test_penrose_like_manifold.rrscene` on the audit host with no parse error or warning (verified). The scene's JSON shape mirrors the SCHW.9 baseline (version `"1.0.0"`, optional `render_settings` / `camera` / `materials` / `spheres` / `meshes` / `lights` blocks) plus the SCHW.9-era `manifold` top-level block. |
| 2 | Fixture uses PenroseLike manifold mode          | **PASS**     | The fixture's `manifold` block authors all four parser-supported `ManifoldMode` fields:<br>**(a)** `"enabled": true` at fixture line 20 — engages the manifold gate at the renderer-side dispatcher (`is_active(...)` returns `true` when paired with a non-Euclidean chart).<br>**(b)** `"chart": "penrose-like"` at fixture line 21 — selects `CoordinateChartType::PenroseLike`. The `apply_manifold` parser at `src/io/SceneLoader.cpp` invokes the local `parse_chart_type` helper which maps this kebab-case string to the PenroseLike enumerator. The kebab-case name was unchanged by PENROSE.4's enum rename (`PenroseLikePlaceholder` → `PenroseLike`); only the C++ enumerator changed.<br>**(c)** `"strength": 0.5` at fixture line 22 — moderate compactification dial, well within the math leaf's nominal `[0, 1]` range and validator-safe bounds. Engages the PENROSE.6 (CUDA) / PENROSE.8 (OptiX) triple-gate's `strength > 0.0f` check.<br>**(d)** `"debug_visualization": true` at fixture line 23 — engages the `aov_manifold_coordinates` device-buffer allocation at the renderer dispatchers (OptiX side at `OptixRenderer::render_aovs`; CUDA side at the SCHW.9 host-side allocator). |
| 3 | Values are bounded / safe                       | **PASS**     | Three-layer safety analysis:<br>**(a) ManifoldMode-side (fixture):** `strength = 0.5` is bounded in the `[0, 1]` nominal range. `enabled` and `debug_visualization` are booleans; `chart` is parsed as an enum (`apply_manifold` rejects unknown values).<br>**(b) CoordinateChart-side (dispatcher-supplied artistic defaults):** PENROSE.6 (CUDA) and PENROSE.8 (OptiX) main.cpp helpers supply `chart.params.mass = 5.0f` (r_max), `chart.params.spin = 1.0f` (falloff), `chart.params.compactification_scale = 1.0f` (scale), `chart.origin = (0,0,0)`. All four are within the math leaf's validator-enforced ranges (`PenroseLikeCompactification.h:160-168`: `r_max > 0`, `scale > 0`, `falloff ∈ [0.5, 4.0]`, `strength` finite).<br>**(c) Math-leaf-side guarantees (inherited):** the PENROSE.3 audit verified the math leaf is bounded by construction (tanh saturation), NaN/Inf-free at the validator-enforced parameter ranges, and analytically reversible via atanh.<br>For the fixture's specific parameters: the formula `r_chart = r_max * tanh(strength * (r/scale)^falloff)` evaluates to:<br>- At `r = 0.5`: `r_chart ≈ 5.0 * tanh(0.25) ≈ 1.22` (near-identity regime);<br>- At `r = 6.0`: `r_chart ≈ 5.0 * tanh(3.0) ≈ 4.99` (saturated near r_max);<br>- At `r → ∞`: `r_chart → r_max = 5.0` (asymptotic boundary).<br>The fixture's eight marker spheres span radial distances `r ∈ {0.5, 0.94, 0.94, 2.55, 2.55, 6.02, 6.02, 4.0}` — all comfortably inside the math leaf's documented safe domain. No NaN/Inf paths reached for any fixture-authored pixel. |
| 4 | Default scenes remain unchanged                 | **PASS**     | `git diff 3ccee4c..756a4bd --name-only -- scenes/` returns **exactly one file**: `scenes/test_penrose_like_manifold.rrscene` (the new fixture). The nine pre-existing fixture files — `test_camera.rrscene`, `test_full_scene.rrscene`, `test_lights.rrscene`, `test_materials.rrscene`, `test_mesh.rrscene`, `test_relativity.rrscene`, `test_render_settings.rrscene`, `test_schwarzschild_like_manifold.rrscene`, `test_spheres.rrscene`, `test_textured_material.rrscene` — are byte-identical to the pre-PENROSE.10 state. `ls -la scenes/` confirms the seven oldest fixtures retain their `May 14 04:15` mtime, the SCHW.9 fixture retains its `May 14 20:27` mtime, and only the new PenroseLike fixture carries the PENROSE.10 commit mtime. The dispatcher's merge logic at `main.cpp::run_render_optix_aovs` / `run_render_aovs` is structurally a no-op for scenes that do not author a `manifold` block: the parser's optional-field handling leaves `scene.manifold = ManifoldMode{}` (the disabled / Euclidean / strength-0 default), so the merge `effective_manifold = cfg.manifold.enabled ? cfg.manifold : scene.manifold` resolves to whichever default the CLI is using — preserving the byte-identity invariant for every existing render action. |
| 5 | Parser changes, if any, are minimal             | **PASS**     | `git diff 3ccee4c..756a4bd -- 'src/*'` returns **zero bytes**. The PENROSE.10 commit is documentation-only on the source tree side; nothing in `src/io/SceneLoader.cpp`, `src/scene/Scene.h/.cpp`, `src/core/CommandLine.cpp`, or any other source file is touched. The SCHW.9 `apply_manifold` parser and the SCHW.9 / PENROSE.4 `parse_chart_type` helper already handle the fixture's `manifold` block + `chart: "penrose-like"` value verbatim:<br>**(a)** SCHW.9's `apply_manifold` parses the four optional ManifoldMode fields (`enabled`, `chart`, `strength`, `debug_visualization` with `debugVisualization` camelCase alias).<br>**(b)** SCHW.9's `parse_chart_type` was updated at PENROSE.4 to map the kebab-case `penrose-like` chart name to the renamed `CoordinateChartType::PenroseLike` enum value.<br>**(c)** Scene POD slot `Scene::manifold` was added at SCHW.9.<br>**(d)** Dispatcher merge logic (`effective_manifold = cfg.manifold.enabled ? cfg.manifold : scene.manifold`) was added at SCHW.9 for both `run_render_optix_aovs` and `run_render_aovs`.<br>PENROSE.10 is **purely additive** at the documentation + fixture layer; the source surface added at SCHW.9 was already designed to accept the PenroseLike fixture's manifold block by construction (chart-family-agnostic). Master rule #3 honesty: the parser surface is **exactly zero new code lines**; the fixture exercises the existing SCHW.9 parser. |
| 6 | Schwarzschild behavior unchanged                | **PASS**     | The PENROSE.10 commit is **scene + doc only** — `git diff 3ccee4c..756a4bd -- 'src/*'` returns 0 bytes. The SchwarzschildLike chart's runtime semantics across all three call sites (PENROSE.4 CPU seam in `ManifoldTransform.h`; PENROSE.6 CUDA arm in `CudaTestKernel.cu`; PENROSE.8 OptiX arm in `OptixPrograms.cu`) are byte-identical to the pre-PENROSE.10 state. The cross-backend AOV byte-equivalence claim from the SCHW.11 capstone audit is preserved verbatim. The new PenroseLike fixture exercises a different chart family (`penrose-like` ≠ `schwarzschild-like`); the two charts are mutually exclusive via the `else if` separator + enum-tag check at each kernel arm (audited at PENROSE.7 + PENROSE.9). The SCHW.9 fixture (`test_schwarzschild_like_manifold.rrscene`) is also unchanged — it continues to load cleanly and exercise the SCHW.5 / SCHW.7 SchwarzschildLike kernel arms exactly as before. |
| 7 | CUDA/OptiX runtime status                       | **DEFERRED** | Two grounds, matching the standard audit-host posture:<br>**(a)** No CUDA SDK, no OptiX SDK on the audit host. The SDK_FOUND TUs compile cleanly but cannot link / launch device code. The audit-host's `cmake --build build -j` succeeds cleanly with no new warnings; ctest 12/12 PASS at the post-PENROSE.10 baseline (manifold_identity_tests 312/312 unchanged; cli_tests 123/123; renderer_tests 19/19; relativity_tests unchanged).<br>**(b)** Even on a CUDA + OptiX-SDK host, the fixture is not yet end-to-end consumable by an existing CLI action. Both `--render-optix-aovs` and `--render-aovs` build their scenes inline (do not load a `<scene-path>` argument); SCHW.9's documented consumption gap applies verbatim to PENROSE.10. The fixture's `manifold` block is loadable (the parser surface is exercised by `--scene-info`); the dispatcher merge logic in main.cpp is in place; but no existing CLI action loads the fixture AND calls a manifold-aware render. This consumption gap is honestly documented in the PENROSE.10 fixture doc §5 ("Current consumption status"). A future single-line CLI extension (`--render-optix-aovs [<scene-path>]`) would close the gap; the work is gated on operator approval and is not part of PENROSE.10 or PENROSE.11's scope.<br>Deferred checks the operator should run on a CUDA + OptiX-SDK host once the consumption-gap CLI extension lands:<br>**(i)** `output/optix_aov_manifold_coordinates.ppm` shows the documented asymptotic-compactification signature (centre sphere near-identity; near-* + knee-* spheres compactified; far-* spheres saturated at chart-radius boundary);<br>**(ii)** The chart-disabled overrides (CLI `--manifold-chart euclidean`; fixture edit to `"euclidean"`; fixture edit to `"strength": 0.0`) all produce byte-identical PPMs to the pre-PENROSE.8 OptiX AOV baseline (fixture doc §4.2);<br>**(iii)** The beauty / normal / depth / albedo / doppler / searchlight AOVs are byte-identical between the chart-engaged and chart-disabled runs (the chart affects only the `manifold_coordinates` AOV per PENROSE.8);<br>**(iv)** SchwarzschildLike non-regression: rendering the SCHW.9 fixture on either backend produces byte-identical output to the pre-PENROSE.* state;<br>**(v) CUDA ↔ OptiX byte-equivalence:** the CUDA `output/aov_manifold_coordinates.ppm` PenroseLike output and the OptiX `output/optix_aov_manifold_coordinates.ppm` PenroseLike output are byte-identical for the same fixture and same `--manifold-*` parameters. This is the **key PENROSE.* arc cross-backend equivalence claim** — structurally guaranteed by single-source-of-truth math at PENROSE.7 / PENROSE.9 audits, with empirical pixel-level verification deferred to an SDK-equipped host. |
| 8 | PASS / REPAIR / BLOCKED verdict                 | **PASS**     | All seven structural checks (#1–#6 and #8) return PASS; check #7 is DEFERRED on documented audit-host limitations. No REPAIR or BLOCKED item is outstanding. The PENROSE.10 commit lands a minimal, safe, isolated fixture: the scene file exists; it authors the PenroseLike manifold mode through all four parser-supported fields; every value is within the math leaf's documented safe range; no default scene is altered; the parser surface addition is **exactly zero new code lines** (reuses SCHW.9 verbatim); the dispatcher merge logic is in place for the future CLI extension that ties the fixture end-to-end. The SchwarzschildLike behavior is preserved verbatim — the SCHW.* arc is untouched. The slice is **safe to extend** to PENROSE.12 (arc capstone audit) under the renumbered PENROSE.* ladder. |

---

## 3. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3 ("Do not implement fake stubs
pretending to be complete systems") the audit is
explicit about its scope boundary:

- **No end-to-end runtime render.** The audit host
  cannot execute device code; the PENROSE.10 fixture
  doc §6 runtime checks are deferred to a CUDA +
  OptiX-SDK host. The structural checks (#1–#6 and
  #8) are exhaustive within the audit-host's reach.
- **No consumption-gap closure.** Today, neither
  `--render-optix-aovs` nor `--render-aovs` loads a
  scene-file argument; both build inline scenes. The
  fixture's `manifold` block is therefore not
  end-to-end consumed by any existing render action.
  The audit verifies the parser-side surface and
  the dispatcher-merge logic are in place; closing
  the consumption gap requires a future CLI
  extension out of scope for PENROSE.10 / PENROSE.11.
- **No cross-backend pixel-level verification.** The
  CUDA ↔ OptiX byte-equivalence claim from the
  PENROSE.7 + PENROSE.9 audits is structurally
  guaranteed by single-source-of-truth math, but
  empirical pixel-level verification requires an
  SDK-equipped host running both backends on the
  same fixture.
- **No chart-parameter authoring surface.** The
  fixture deliberately does not author the
  `CoordinateChart::params` slots; the dispatcher
  supplies artistic defaults. The audit does not
  verify the operator's ability to override these
  via a scene-file mechanism (no such mechanism
  exists today).
- **No `MODULE_MAP.md` cross-host promotion.** The
  per-slice audit is doc-only; module-map promotion
  still waits for MANI-I.12 (final cross-host
  audit) per the integration plan §11.

---

## 4. REASONING SUMMARY

The PENROSE.10 commit (`756a4bd`) ships three files,
all documentation or fixture data — no source code
touched:

- **Fixture scene**
  (`scenes/test_penrose_like_manifold.rrscene`):
  eight visible marker spheres at radial distances
  spanning `r ∈ {0.5, 0.94, 2.55, 4.0, 6.02}` (the
  chart's three visual regimes — near-identity,
  knee, saturation); one ground-plane mesh
  (24 × 24, wider than SCHW.9's 12 × 12); two
  lights; a `manifold` block authoring
  `enabled=true / chart="penrose-like" /
  strength=0.5 / debug_visualization=true`. No
  relativity block; no chart-parameter authoring;
  no extreme/singular values.
- **Fixture companion doc**
  (`docs/PENROSE_LIKE_FIXTURE.md`): seven sections
  covering purpose, composition (with per-sphere
  `r_chart` predictions), expected visual behavior
  across the three regimes, default/no-op
  comparison (three override mechanisms + SCHW.*
  non-regression + no-fixture baseline), current
  consumption status (parser-loadable today;
  renderer end-to-end gated on the same future CLI
  extension SCHW.9 documented), runtime DEFERRED
  rationale (with the key cross-backend
  equivalence claim), and references.
- **BUILD_PLAN.md entry**: appended.

The fixture-scene-existence invariant (check #1) is
**file-level verified**: the file is on disk, its
content matches the documented shape, and the
`--scene-info` loader accepts it without error on
the audit host.

The PenroseLike-engagement invariant (check #2) is
**content-level verified**: all four parser-
supported fields are authored with values that
engage the renderer dispatchers' triple-gate
(`enabled=true`, `chart=penrose-like`, `strength>0`,
`debug_visualization=true`).

The bounded/safe-values invariant (check #3) is
**three-layer-redundantly verified**: ManifoldMode
fields are within their documented ranges; the
dispatcher-supplied CoordinateChart parameters are
within the math leaf's documented safe ranges; the
math leaf's tanh saturation property (audited at
PENROSE.3) preserves no-NaN/no-Inf for any
fixture-authored pixel.

The default-scenes-unchanged invariant (check #4) is
**directly verified** by `git diff --name-only --
scenes/` returning exactly one file (the new
fixture). The nine pre-existing fixture files are
byte-identical to the pre-PENROSE.10 state.

The minimal-parser-changes invariant (check #5) is
**diff-zero verified**: `git diff -- 'src/*'`
returns 0 bytes. The SCHW.9 parser surface +
PENROSE.4 enum rename together provide the full
PenroseLike scene-loading capability without any
PENROSE.10 source-code change.

The Schwarzschild-unchanged invariant (check #6) is
**structurally guaranteed** by the scene-+-doc-only
PENROSE.10 commit. The SCHW.* arc's source code is
unmodified.

The CUDA/OptiX runtime status (check #7) is
DEFERRED on documented audit-host limitations —
the standard posture for PENROSE.* per-slice
audits.

The verdict (check #8) is PASS structurally;
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
- **PENROSE.9** — Audit of PENROSE.8 (LANDED at
  `3ccee4c`).
- **PENROSE.10** — Fixture / debug visualization
  (LANDED at `756a4bd`).
- **PENROSE.11** — **THIS AUDIT** (Penrose-Like
  Fixture Audit, doc-only).
- **PENROSE.12** — Arc capstone audit (was
  PENROSE.11 in the post-PENROSE.10 plan;
  renumbered); closes the MANI-I.11 slot.

The `docs/PENROSE_LIKE_COMPACTIFICATION_PLAN.md`
§10 sub-slice ladder is updated as part of this
PENROSE.11 commit so the per-slice numbering stays
coherent. The plan's other sections (§1-§9,
§11-§12) are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is **PENROSE.12 — Arc capstone
audit** per the renumbered plan §10 PENROSE.12 —
the per-arc capstone verdict synthesizing the prior
per-slice audits (PENROSE.3 / PENROSE.5 / PENROSE.7
/ PENROSE.9 / PENROSE.11) into a single arc-level
PASS / PASS_WITH_RUNTIME_DEFERRED / REPAIR /
BLOCKED verdict for the MANI-I.11 PenroseLike slot.

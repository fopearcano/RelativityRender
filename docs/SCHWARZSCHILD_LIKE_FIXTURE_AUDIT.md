# Schwarzschild-Like Fixture Audit (SCHW.10)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `f102dd3` ("scene:
SCHW.9 — Schwarzschild-Like Debug Visualization +
Fixture (impl, scene-loader + fixture)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-SCHW.8 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is the per-slice gate for SCHW.9 (`f102dd3`).
It verifies the seven items the task brief enumerates —
fixture scene exists; fixture uses SchwarzschildLike
manifold mode; values are bounded/safe; default scenes
remain unchanged; parser changes are minimal;
CUDA/OptiX runtime status; verdict — and produces the
PASS / REPAIR / BLOCKED verdict that gates progression
to the renumbered SCHW.11 (final audit; closes the
MANI-I.10 slot).

---

## 1. VERDICT

**PASS** (the fixture is safe and isolated; the parser
surface is minimal and additive; default scenes are
byte-identical to the pre-SCHW.9 baseline), with
**DEFERRED** runtime CUDA/OptiX-host status (the
fixture's visual signature cannot be exercised on the
audit host; the SDK_FOUND TUs compile but cannot
link / launch).

All six structural checks (#1–#5 and #7) return PASS.
Check #6 (CUDA/OptiX runtime status) is DEFERRED on
documented audit-host limitations, matching the
MANI-I.6 / MANI-I.9 / SCHW.2 / SCHW.4 / SCHW.6 /
SCHW.8 DEFERRED posture. No REPAIR or BLOCKED item is
found.

The operator may proceed to the renumbered SCHW.11
(final audit) per §4 below, or to SCHW.5 (CUDA-side
kernel wiring; still deferred from the SCHW.6
forward-looking audit) in any order.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Fixture scene exists                                  | **PASS**     | `scenes/test_schwarzschild_like_manifold.rrscene` is present on the post-SCHW.9 tree (`ls -la scenes/` lists it with size 2321 bytes, mtime `May 14 20:27`). The file is the **only** new entry in the `scenes/` subtree per `git diff f102dd3^..f102dd3 --name-only -- scenes/`, which returns exactly that filename. The fixture loads cleanly through `RelativityRender --scene-info scenes/test_schwarzschild_like_manifold.rrscene` on the audit host with no parse error or warning. The scene's JSON shape mirrors the established `test_full_scene.rrscene` baseline (version `"1.0.0"`, optional `render_settings` / `camera` / `materials` / `spheres` / `meshes` / `lights` blocks) plus the new top-level `manifold` block. |
| 2 | Fixture uses SchwarzschildLike manifold mode          | **PASS**     | The fixture's `manifold` block authors all four parser-supported `ManifoldMode` fields:<br>**(a)** `"enabled": true` at fixture line 20 — engages the manifold gate at the renderer-side dispatcher (`is_active(...)` returns `true` when paired with a non-Euclidean chart).<br>**(b)** `"chart": "schwarzschild-like"` at fixture line 21 — selects `CoordinateChartType::SchwarzschildLike`. The `apply_manifold` parser at `src/io/SceneLoader.cpp:933-985` invokes the local `parse_chart_type` helper which maps this kebab-case string to the SchwarzschildLike enumerator per the MANIFOLD.1 + SCHW.* design.<br>**(c)** `"strength": 0.5` at fixture line 22 — moderate warp dial, well below the math leaf's `±0.5` bend cap (`SchwarzschildLikeWarp.h:289`) and the validator's nominal `[0, 1]` range. Engages the SCHW.7 triple-gate's `strength > 0.0f` check at `OptixPrograms.cu:777`.<br>**(d)** `"debug_visualization": true` at fixture line 23 — engages the `aov_manifold_coordinates` device-buffer allocation at `OptixRenderer.cpp:2713-2720` (SCHW.7) AND the CUDA `--render-aovs` host-side allocator at `main.cpp:3865+` (extended at SCHW.9 to consult the merged manifold mode). |
| 3 | Values are bounded / safe                             | **PASS**     | Three-layer safety analysis:<br>**(a) ManifoldMode-side (fixture):** `strength = 0.5` is bounded in the `[0, 1]` nominal range; well below the `±0.5` bend cap. `enabled` and `debug_visualization` are booleans (no numeric range); `chart` is an enum (parsed by name; `apply_manifold` rejects unknown values with a clear error per `SceneLoader.cpp:944-953`).<br>**(b) CoordinateChart-side (dispatcher-supplied artistic defaults):** The SCHW.7 main.cpp helper at `run_render_optix_aovs` supplies `chart.params.mass = 1.0f`, `chart.params.spin = 1.0f`, `chart.params.compactification_scale = 0.1f`, `chart.origin = (0,0,0)`. All four are within the math leaf's documented safe ranges (`SchwarzschildLikeWarp.h:113-122` validator). `clamp_radius > 0` is enforced; `falloff` is in `[0.5, 4.0]`; `r_s` is finite and positive.<br>**(c) Math-leaf-side guarantees (inherited):** The SCHW.1 / SCHW.2 audit verified four bounding/no-NaN mechanisms (`r = max(|delta|, clamp_radius)` lower bound; NR 8-iter cap; `F'` zero-guard at `1e-9`; primary-ray bend cap at `±0.5`). For the fixture's specific parameters: maximum displacement `f = strength * r_s / clamp_radius^falloff = 0.5 * 1.0 / 0.1^1.0 = 5.0`, bounded analytically; far-field displacement `f → 0` as `r → ∞`. No code path produces NaN/Inf for any fixture-authored pixel.<br>The fixture's six marker spheres span radial distances `r ∈ {0.5, 1.5, 2.0, 2.0, 3.0, 3.0}` from the mass origin — all outside the clamp shell except the `centre` sphere (whose surface lies partially inside the clamp shell, where the math leaf's clamp substitution produces a finite "uniform-warp shell" output per plan §4.4). No fixture geometry pixel evaluates the math at `r = 0` (the math leaf's `delta = 0` short-circuit handles the pathological `p_world == mass_origin` case correctly per `SchwarzschildLikeWarp.h:154` + the SCHW.4 audit's check #5). |
| 4 | Default scenes remain unchanged                       | **PASS**     | `git diff f102dd3^..f102dd3 --name-only -- scenes/` returns **exactly one file**: `scenes/test_schwarzschild_like_manifold.rrscene` (the new fixture). The eight pre-existing fixture files — `test_camera.rrscene`, `test_full_scene.rrscene`, `test_lights.rrscene`, `test_materials.rrscene`, `test_mesh.rrscene`, `test_relativity.rrscene`, `test_render_settings.rrscene`, `test_spheres.rrscene`, `test_textured_material.rrscene` — are byte-identical to the pre-SCHW.9 state (`ls -la` shows mtime `May 14 04:15` for all eight; the new fixture has mtime `May 14 20:27`). The dispatcher's merge logic at `main.cpp::run_render_optix_aovs` / `run_render_aovs` is structurally a no-op for scenes that do not author a `manifold` block: the parser's optional-field handling leaves `scene.manifold = ManifoldMode{}` (the disabled / Euclidean / strength-0 default), so the merge `effective_manifold = cfg.manifold.enabled ? cfg.manifold : scene.manifold` resolves to whichever default the CLI is using — preserving the byte-identity invariant for every existing render action. |
| 5 | Parser changes, if any, are minimal                   | **PASS**     | `git diff f102dd3^..f102dd3 -- src/io/SceneLoader.cpp` shows **119 added lines** total (per `wc -l` filtered to additions). The diff is composed of three minimal surface additions:<br>**(a)** Two `#include` lines (`manifold/CoordinateChart.h`, `manifold/ManifoldMode.h`) appended to the existing include block.<br>**(b)** Local `parse_chart_type(string, CoordinateChartType&)` helper at the anonymous-namespace scope — 22 lines including doc-comment, a 5-branch switch on the five kebab-case chart names. Deliberately duplicates the CLI's `src/core/CommandLine.cpp::parse_chart_type` (also 5 branches) to avoid widening the `rr_core` → `rr_io` dependency edge; the duplication is a known cost and acknowledged in the local helper's doc-comment.<br>**(c)** `apply_manifold(JsonValue&, ManifoldMode&, string& err)` function — 60 lines including doc-comment, parsing four optional `ManifoldMode` fields (`enabled`, `chart`, `strength`, `debug_visualization` with `debugVisualization` camelCase alias matching the existing `relativity` block's canonical/shorthand precedent). Chart-side `CoordinateChart::params` fields are explicitly NOT exposed (per the operator brief's "do not broaden scene format beyond this fixture's needs" rule).<br>**(d)** Top-level dispatcher wires the `manifold` block into `parse_scene` right after the `relativity` block — 13 lines including doc-comment.<br>Zero changes to existing parser arms (`apply_render_settings`, `apply_camera`, `apply_relativity`, `apply_materials`, `apply_sphere`, `apply_meshes`, `apply_lights`); zero changes to the JSON-tokenizer or value-coercer surfaces. The Scene POD addition is a single field (`rr::manifold::ManifoldMode manifold`) at `src/scene/Scene.h` plus a one-line reset in `Scene::clear()`. The dispatcher-merge addition at `src/main.cpp` is two parallel code paths in `run_render_optix_aovs` and `run_render_aovs`. Master rule #3 honesty: the parser surface is the **minimum sufficient** to load the fixture; future slices may broaden the surface (e.g. an optional `chart_params: {mass, falloff, clamp_radius}` sub-block) without modifying the SCHW.9 contract. |
| 6 | CUDA/OptiX runtime status                             | **DEFERRED** | Two grounds, matching the standard audit-host posture:<br>**(a) No CUDA SDK, no OptiX SDK on the audit host.** The SDK_FOUND TUs compile (the fixture-doc §6 explicitly notes this) but cannot link / launch device code. The audit-host's `cmake --build build -j` succeeds cleanly with no new warnings; ctest 12/12 PASS at the post-SCHW.9 baseline.<br>**(b) Even on a CUDA + OptiX-SDK host, the fixture is not yet end-to-end consumable by an existing CLI action.** Both `--render-optix-aovs` and `--render-aovs` build their scenes inline (do not load a `<scene-path>` argument). The fixture's `manifold` block is loadable (the parser surface is exercised by `--scene-info`); the dispatcher merge logic in main.cpp is in place; but no existing CLI action loads the fixture AND calls a manifold-aware render. This consumption gap is honestly documented in the fixture doc §5 ("Current consumption status"). A future single-line CLI extension (`--render-optix-aovs [<scene-path>]`) would close the gap; the work is gated on operator approval and is not part of SCHW.9 or SCHW.10's scope.<br>Deferred checks the operator should run on a CUDA + OptiX-SDK host once the consumption-gap CLI extension lands:<br>**(i)** `output/optix_aov_manifold_coordinates.ppm` shows the documented radial-compression signature near the `centre` sphere, near-identity at the `far-*` spheres, and the clamp-shell uniform-warp around the mass origin (fixture doc §3 enumerates the per-sphere predictions);<br>**(ii)** the chart-disabled overrides (CLI `--manifold-chart euclidean`; fixture edit to `"euclidean"`; fixture edit to `"strength": 0.0`) all produce byte-identical PPMs to the pre-SCHW.7 OptiX AOV baseline (fixture doc §4);<br>**(iii)** the beauty / normal / depth / albedo / doppler / searchlight AOVs are byte-identical between the chart-engaged and chart-disabled runs (the chart affects only the `manifold_coordinates` AOV per SCHW.7);<br>**(iv)** when SCHW.5 lands and the CUDA-side warp engages, the CUDA `output/aov_manifold_coordinates.ppm` is byte-identical to the OptiX `output/optix_aov_manifold_coordinates.ppm` for the same fixture (single-source-of-truth math leaf guarantees this by construction). |
| 7 | PASS / REPAIR / BLOCKED verdict                       | **PASS**     | All six structural checks (#1–#5 and #7) return PASS; check #6 is DEFERRED on documented audit-host limitations. No REPAIR or BLOCKED item is outstanding. The SCHW.9 commit lands a minimal, safe, isolated fixture: the scene file exists; it authors the SchwarzschildLike manifold mode through all four parser-supported fields; every value is within the math leaf's documented safe range; no default scene is altered; the parser surface addition is exactly four optional `ManifoldMode` fields (no `CoordinateChart` parameter broadening); the dispatcher merge logic is in place for the future CLI extension that ties the fixture end-to-end. The slice is **safe to extend** to SCHW.11 (final audit) or to SCHW.5 (CUDA-side kernel wiring) in any order. |

---

## 3. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3 ("Do not implement fake stubs
pretending to be complete systems") the audit is
explicit about its scope boundary:

- **No end-to-end runtime render.** The audit host
  cannot execute device code; the fixture-doc §6
  runtime checks are deferred to a CUDA + OptiX-SDK
  host. The structural checks (#1–#5 and #7) are
  exhaustive within the audit-host's reach.
- **No consumption-gap closure.** Today, neither
  `--render-optix-aovs` nor `--render-aovs` loads a
  scene-file argument; both build inline scenes. The
  fixture's `manifold` block is therefore not
  end-to-end consumed by any existing render action.
  The audit verifies that the parser-side surface and
  the dispatcher-merge logic are in place; closing
  the consumption gap requires a future CLI extension
  that is out of scope for SCHW.9 / SCHW.10.
- **No CUDA-side SchwarzschildLike arm.** The CUDA
  kernel still writes raw `best.position` to the
  `aov_manifold_coordinates` AOV (per MANI-I.8). The
  SCHW.5 (CUDA integration) slice is still deferred
  from the SCHW.6 forward-looking audit. When SCHW.5
  lands, the CUDA-side fixture rendering will produce
  the same SchwarzschildLike signature; until then,
  the CUDA path's `output/aov_manifold_coordinates.ppm`
  is the raw position regardless of the fixture's
  manifold mode.
- **No chart-parameter authoring surface.** The
  fixture deliberately does not author the
  `CoordinateChart::params` slots (`mass`, `spin`,
  `compactification_scale`, `origin`); the
  dispatcher supplies artistic defaults. The audit
  does not verify the operator's ability to override
  these via a scene-file mechanism (no such mechanism
  exists today).
- **No `.rrscene` schema version bump verification.**
  The fixture authors `"version": "1.0.0"` matching
  every existing fixture. The audit does not verify
  whether the `manifold` block addition warrants a
  version bump (the SceneLoader's version-check at
  lines `1633-1646` accepts major version 1; the
  `manifold` block is an optional top-level key, so
  v1 compatibility is preserved by the standard
  optional-key contract).
- **No `MODULE_MAP.md` cross-host promotion.** The
  per-slice audit is doc-only; module-map promotion
  still waits for MANI-I.12 (final cross-host audit)
  per the integration plan §11.

---

## 4. REASONING SUMMARY

The SCHW.9 commit (`f102dd3`) introduces a minimal,
isolated fixture + parser surface:

- **Fixture scene** (`scenes/test_schwarzschild_like_manifold.rrscene`):
  six visible marker spheres at known radial distances
  from the mass origin, one ground-plane mesh, two
  lights, and a `manifold` block authoring
  `enabled=true / chart="schwarzschild-like" /
  strength=0.5 / debug_visualization=true`. No
  relativity block; no chart-parameter authoring; no
  extreme/singular values. The fixture's geometry
  exposes the radial-warp signature across the
  documented `r ∈ [~0.5, 3.0]` distance range.

- **Scene POD slot** (`src/scene/Scene.h::Scene::manifold`):
  one new field, defaulted to `ManifoldMode{}` (the
  pre-pivot disabled / Euclidean / strength-0 no-op
  anchor). `Scene::clear()` resets the field to the
  same default. The eight existing `.rrscene` files
  do not author a `manifold` block; the parser's
  optional-field handling leaves the field at its
  default for those scenes — preserving the
  byte-identity invariant.

- **Parser surface** (`src/io/SceneLoader.cpp::apply_manifold`):
  parses four optional fields (`enabled`, `chart`,
  `strength`, `debug_visualization` with
  `debugVisualization` camelCase alias). Local
  `parse_chart_type` helper duplicates the CLI's
  identical helper to avoid widening the `rr_core`
  → `rr_io` dependency edge. No broadening of the
  scene format beyond the fixture's needs (per the
  operator brief).

- **Dispatcher merge** (`src/main.cpp::run_render_optix_aovs`
  + `run_render_aovs`): both dispatchers resolve
  `effective_manifold = cfg.manifold.enabled ?
  cfg.manifold : scene.manifold`. CLI wins on
  explicit `--manifold-enable`; scene fills in
  otherwise. The merge is dead-code today for the
  existing actions (which build inline scenes) but
  activates when a future single-line CLI extension
  adds a `<scene-path>` argument to either action.

- **CMakeLists.txt**: explicit `rr_scene → rr_manifold`
  link (mirrors MANI-I.3's `rr_pathtracer →
  rr_manifold` precedent). `rr_manifold` is
  INTERFACE-only so no artifact change.

- **Fixture-companion doc**
  (`docs/SCHWARZSCHILD_LIKE_FIXTURE.md`): seven
  sections covering purpose, composition, expected
  visual behavior, default/no-op comparison, current
  consumption status, runtime DEFERRED rationale,
  and references.

The fixture-scene-existence invariant (check #1) is
**file-level verified**: the file is on disk, its
content matches the documented shape, and the
`--scene-info` loader accepts it without error on the
audit host.

The SchwarzschildLike-engagement invariant (check #2)
is **content-level verified**: all four parser-
supported fields are authored with values that engage
the SCHW.7 triple-gate (`enabled=true`,
`chart=schwarzschild-like`, `strength>0`).

The bounded/safe-values invariant (check #3) is
**three-layer-redundantly verified**: ManifoldMode
fields are within their documented ranges; the
dispatcher-supplied CoordinateChart parameters are
within the math leaf's documented safe ranges (the
validator at `SchwarzschildLikeWarp.h:113-122` would
reject otherwise); the math leaf's four bounding
guards (audited at SCHW.2 / SCHW.4 / SCHW.6 /
SCHW.8) preserve no-NaN/no-Inf for any fixture-
authored pixel.

The default-scenes-unchanged invariant (check #4) is
**directly verified** by `git diff --name-only --
scenes/` returning exactly one file (the new
fixture). The eight pre-existing fixture files are
byte-identical to the pre-SCHW.9 state.

The minimal-parser-changes invariant (check #5) is
**diff-size verified**: 119 added lines total to
`SceneLoader.cpp`, composed of three small surface
additions (two includes, one helper, one parser
function plus 13-line dispatcher wire-up). Zero
changes to existing parser arms or to the JSON
tokenizer / value-coercer surfaces.

The CUDA/OptiX runtime status (check #6) is DEFERRED
on documented audit-host limitations — the standard
posture for SCHW.* per-slice audits.

The verdict (check #7) is PASS structurally; runtime
DEFERRED.

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
- **SCHW.8** — Audit of SCHW.7 (LANDED at `fd7084f`).
- **SCHW.9** — Debug visualization + fixture
  (LANDED at `f102dd3`).
- **SCHW.10** — **THIS AUDIT** (Schwarzschild-Like
  Fixture Audit, doc-only).
- **SCHW.11** — Final audit (was SCHW.10 in the
  post-SCHW.8 plan; renumbered); closes the
  MANI-I.10 slot.

The `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §8
sub-slice ladder is updated as part of this SCHW.10
commit so the per-slice numbering stays coherent. The
plan's other sections (§1–§7, §9–§10) are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is one of:
- **SCHW.5 — CUDA integration** (close the
  CUDA-side gap; mirror SCHW.7's OptiX-side wiring
  in `CudaTestKernel.cu` using the same shared
  math leaf), OR
- **SCHW.11 — Final audit** (close the MANI-I.10
  slot with the end-to-end PASS verdict; the audit
  may still report runtime DEFERRED items on the
  audit host until a CUDA + OptiX-SDK host is
  available), OR
- **CLI consumption-gap closure** — a future
  single-line extension to `--render-optix-aovs`
  (and/or `--render-aovs`) that accepts a scene-
  file argument so the SCHW.9 fixture's `manifold`
  block end-to-end activates the SchwarzschildLike
  warp through the existing dispatcher-merge
  logic.

All three are tractable on the audit host. The
runtime CUDA + OptiX-SDK-host fixture renders
enumerated in check #6 above will become exercisable
when the operator runs the existing CLI on a CUDA +
OptiX-SDK host AND the consumption-gap CLI extension
lands; the SCHW.11 final audit will close the
MANI-I.10 slot with the end-to-end PASS verdict.

# ObserverFrame Data Model Audit (OBSERVER.3)

Date:   2026-05-15
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `85496a5` ("manifold:
OBSERVER.2 — ObserverFrame Data Model (impl, POD-leaf)").
Audit baseline: `eee9d6b` ("docs: OBSERVER.1 —
Observer-Frame Rendering Plan (docs only)") — the
last commit before OBSERVER.2 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, `git diff` against
the post-OBSERVER.1 baseline, the
`manifold_identity_tests` runtime output, and
`ctest` exit codes.

This audit is the per-slice gate for OBSERVER.2
(`85496a5`). It verifies the nine items the task
brief enumerates — `ObserverFrame` exists;
`position4` exists; beta/velocity representation
exists; local-basis / tetrad placeholder exists;
time placeholders exist; perception-mode placeholder
exists; defaults are camera-equivalent / no-op;
build / test status; verdict — and produces a
`PASS` / `REPAIR` / `BLOCKED` verdict that gates
progression to the renumbered OBSERVER.4 (Config /
CLI bridge).

---

## 1. VERDICT

**PASS.**

All eight structural checks return `PASS`. No
`REPAIR` or `BLOCKED` item is found. The OBSERVER.2
data-model surface is safely landed, default-no-op
verified, finite + orthonormal validators in place,
and produces zero renderer-side behaviour change.
The operator may proceed to OBSERVER.4 (Config /
CLI bridge; renumbered from the original
OBSERVER.3 per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | `ObserverFrame` exists                   | **PASS** | The OBSERVER.2 commit (`85496a5`) preserves the MANIFOLD.3 `struct ObserverFrame` and minimally extends it. The POD is declared at `src/manifold/ObserverFrame.h:138` (`struct ObserverFrame {`) inside `namespace rr::manifold` (line 94). Header is structurally complete: includes only `manifold/MetricTensor.h`, `math/MathUtils.h`, `math/Vec3.h`, `math/Vec4.h`, `relativity/RelativityMath.h`, `relativity/RelativityParams.h`, and `<cmath>` (the OBSERVER.2 addition for `std::isfinite`; line 92). No new module dependencies. The accompanying factory `rest_frame()` at line 210 returns a default-constructed `ObserverFrame{}`; the bridge helpers `observer_frame_from(...)` (line 232) and `to_relativity_observer(...)` (line 260) round-trip beta exactly. |
| 2 | `position4` exists                       | **PASS** | `rr::math::Vec4 position4 = {0.0f, 0.0f, 0.0f, 0.0f};` at `src/manifold/ObserverFrame.h:144`. Default is the chart-space origin (scene-rest observer at scene origin per the architecture-doc §3.3 convention `(x⁰, x¹, x², x³)`; on the Euclidean chart this reads `(t, x, y, z)` in scene-natural units `c = 1`). Field is preserved verbatim from the MANIFOLD.3 surface — no `position4` rename, no signature change. Validator coverage: `is_finite_observer_frame(...)` at line 368 reads all four components of `position4` into its 21-scalar finite-check array (positions 0-3 of `scalars[]` at line 370). |
| 3 | beta / velocity representation exists    | **PASS** | Two-field beta/velocity representation:<br>**(a)** `rr::math::Vec4 velocity4 = {1.0f, 0.0f, 0.0f, 0.0f};` at `src/manifold/ObserverFrame.h:153` — the 4-velocity `u^μ` in chart coordinates; default `(1, 0, 0, 0)` is the rest 4-velocity (one unit coordinate-time per proper-time, no spatial motion). For the scene-rest observer on the Euclidean chart with mostly-plus signature this satisfies `g_{μν} u^μ u^ν = -1` analytically.<br>**(b)** `rr::math::Vec3 beta = {0.0f, 0.0f, 0.0f};` at line 165 — the sibling 3-velocity `β = v/c` that mirrors the legacy `rr::relativity::Observer::velocity` so the existing SR helpers (`aberrateDirection`, `dopplerFactor`, `searchlightFactor`, `applyDopplerColor`) can be fed without re-derivation.<br>The two fields are populated consistently by `observer_frame_from(...)` at line 232 via `f.beta = beta3; f.velocity4 = Vec4{g, g*beta3.x, g*beta3.y, g*beta3.z};` (lines 249-250) with `g = gamma(clampBeta(|v|, 0.999999f))` from `rr::relativity::gamma`. The four-velocity normalisation gate is `is_normalised_timelike(frame, metric, tol)` at line 280; default tolerance `1.0e-4f`. Empirically verified at `test_observer_frame_defaults` (lines 308-345 of `tests/manifold_identity_tests.cpp`) including a round-trip at `beta = (0.3, -0.4, 0)` that preserves the input velocity exactly through `to_relativity_observer(observer_frame_from(obs))`. |
| 4 | local basis / tetrad placeholder exists  | **PASS** | Three-field spatial tetrad:<br>**(a)** `rr::math::Vec3 right   = {1.0f, 0.0f, 0.0f};` at `src/manifold/ObserverFrame.h:174`<br>**(b)** `rr::math::Vec3 up      = {0.0f, 1.0f, 0.0f};` at line 175<br>**(c)** `rr::math::Vec3 forward = {0.0f, 0.0f, 1.0f};` at line 176<br>Defaults form the right-handed world basis the existing pinhole `rr::camera::Camera` produces (the no-op camera-equivalent state). Per architecture-doc §3.3 the timelike leg `e_0` is the four-velocity `velocity4` above; the three spatial legs `e_1 / e_2 / e_3` are stored as `Vec3` (Euclidean spatial part only this arc; a future curved-chart slice will promote them to `Vec4` and add parallel-transport machinery — documented at the "What does NOT live here this slice" block, lines 67-70). New OBSERVER.2 validator: `is_orthonormal_tetrad(frame, tolerance)` at line 326 — `RR_HD inline bool` returning `true` iff the pairwise dot products `right·up`, `up·forward`, `forward·right` are within `tolerance` of zero AND all three leg lengths are within `tolerance` of unity. Default tolerance `1.0e-4f` matches the `is_normalised_timelike` precedent. Empirically verified at `test_observer_2_orthonormal_tetrad_default` (the helper returns `true` on `rest_frame()` and on `observer_frame_from(...)` at moderate beta; fails on three documented degenerate cases — non-unit leg, non-orthogonal legs `up == right`, collinear legs `forward == right`). |
| 5 | time placeholders exist                  | **PASS** | Two-field worldline-time placeholder set:<br>**(a)** `float proper_time = 0.0f;` at `src/manifold/ObserverFrame.h:184` — cumulative proper time `τ` along the observer's worldline since a reference epoch (e.g. the camera-start event).<br>**(b)** `float coordinate_time = 0.0f;` at line 192 — chart-space `t`-coordinate at the observer's current worldline parameter.<br>Both fields are zero-initialised; no code path advances them this slice (header doc-comment lines 64-66: "No geodesic integrator. `proper_time` and `coordinate_time` are zero-initialised placeholders; no code path advances them — architecture-doc §3.4 / §10 step 2"). Both fields are reserved for the future geodesic-integrator arc per the architecture-doc §3.4. Validator coverage: `is_finite_observer_frame(...)` at line 368 reads both fields into its 21-scalar finite-check array (positions 19-20 of `scalars[]` at lines 376-377). Empirically verified at `test_observer_2_finite_observer_frame` — NaN in `proper_time` and NaN in `coordinate_time` are both detected as non-finite. |
| 6 | perception mode placeholder exists       | **PASS** | Two-part addition by OBSERVER.2:<br>**(a) `PerceptionMode` enum** at `src/manifold/ObserverFrame.h:113-136` — `enum class PerceptionMode { Identity = 0, ConstantVelocityMinkowski, CurvedChartGeodesicPlaceholder };`. Three enumerators per the OBSERVER.1 plan §3.6. `Identity = 0` is explicit so the default-constructed value is unambiguous; `enum class` scope prevents the enumerators leaking into the enclosing namespace. The enum is declared parallel to `CoordinateChartType` (the two enums together identify "where space maps" + "how the observer perceives it" per the architecture-doc §3 ontology).<br>**(b) `ObserverFrame::perception_mode` field** at line 202: `PerceptionMode perception_mode = PerceptionMode::Identity;` — appended at the end of the POD after `coordinate_time`, default `PerceptionMode::Identity`. Reserved-but-declared this slice; no kernel call site reads the field (master rule #3 "no fake stubs" satisfied: the field is structurally consumed by `default_perception_mode()` at line 308 + tests + the planned OBSERVER.4-OBSERVER.6 slices — not a fake stub). The doc comment at line 194-201 explicitly enumerates which subsequent slices (OBSERVER.3 CLI; OBSERVER.4 camera adapter; OBSERVER.5 / OBSERVER.6 GPU payload bridges) will populate + consume it.<br>**(c) Factory** `RR_HD inline PerceptionMode default_perception_mode()` at line 308 returns `PerceptionMode::Identity`. Empirically verified at `test_observer_2_perception_mode_default` — `default_perception_mode() == Identity`; a default-constructed `ObserverFrame` carries `Identity`; the three enumerators are pairwise distinct (defence-in-depth against an accidental enumerator collision in a future edit). |
| 7 | defaults are camera-equivalent / no-op   | **PASS** | Three layers of camera-equivalent / no-op enforcement:<br>**(a) Field-by-field default audit.** Every `ObserverFrame` field's default value resolves to the scene-rest observer at the chart origin with the world-basis tetrad: `position4 = (0, 0, 0, 0)`; `velocity4 = (1, 0, 0, 0)`; `beta = (0, 0, 0)`; `right = (1, 0, 0)`; `up = (0, 1, 0)`; `forward = (0, 0, 1)`; `proper_time = 0`; `coordinate_time = 0`; `perception_mode = Identity`. The `Identity` perception mode is the no-op anchor (per the enum's doc comment at line 114-117: "Scene-rest observer; no aberration, no Doppler, no searchlight. Matches the pre-pivot Euclidean camera bit-for-bit. The renderer's default.").<br>**(b) Bridge round-trip.** `to_relativity_observer(rest_frame())` returns `rr::relativity::Observer{(0, 0, 0)}` — zero velocity, which means even if the existing SR helpers were called against this observer the aberration / Doppler / searchlight factors all reduce to their identity values (verified at `test_observer_2_default_no_deformation`).<br>**(c) Empirical no-deformation gate.** `test_observer_2_default_no_deformation` is the operator's "no-op observer does not imply coordinate deformation" gate: it pins every field of a default-constructed `ObserverFrame` to its expected anchor value, verifies the bridge to the legacy `Observer` carries zero velocity, verifies the round-trip via `observer_frame_from(rest Observer)` preserves the default-frame's beta and velocity4 exactly, and verifies all three validator gates (`is_finite_observer_frame`, `is_orthonormal_tetrad`, `is_normalised_timelike` against `minkowski_metric()`) hold on the default.<br>**(d) Zero-renderer-touch.** `git diff eee9d6b..85496a5 --name-only` returns only `docs/BUILD_PLAN.md`, `src/manifold/ObserverFrame.h`, `tests/manifold_identity_tests.cpp` — zero files in `src/cuda/`, `src/optix/`, `src/pathtracer/`, `src/renderer/`, `src/gpu/`, `src/scene/`, `src/io/`, `src/server/`, `src/core/`, `src/camera/`, `src/material/`, `src/lighting/`, `src/texture/`, `src/geometry/`, `src/image/`, `src/math/`, `src/relativity/`, `src/field/`, or `src/main.cpp`. No kernel call site reads the new `perception_mode` field; no scene-aware action's output is altered. |
| 8 | Build / test status                      | **PASS** | Audit-host `cmake --build /home/user/RelativityRender/build` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings on the manifold module. Full `ctest` from the audit-host build directory: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `349 / 349 checks passed` (was `312 / 312` pre-OBSERVER.2 at the post-MANI-CONSUME.2 baseline; **+37 RR_CHECK assertions** from the four new test functions plus the two assertions added to the existing `test_observer_frame_defaults`). `cli_tests: 123/123 passed`; `renderer_tests: 19/19 passed`. No regression in any other test binary. No new ctest target; no CMake link-line change (the `rr_manifold` library shape is unchanged; the modified header is INTERFACE-only and pulled in by the test file's include). |
| 9 | PASS / REPAIR / BLOCKED verdict          | **PASS** | All eight structural checks return `PASS`. No `REPAIR` or `BLOCKED` item is outstanding. The OBSERVER.2 data-model surface ships the documented seven-field POD (preserved verbatim from MANIFOLD.3) plus the new `PerceptionMode` enum + `perception_mode` field + three validator helpers (`default_perception_mode`, `is_orthonormal_tetrad`, `is_finite_observer_frame`), with empirically-verified default-no-op invariants and a zero-renderer-touch diff. The slice is **safe to extend** to Config / CLI bridge (renumbered OBSERVER.4) under the renumbered OBSERVER.* ladder per §4 below. |

---

## 3. REASONING SUMMARY

The OBSERVER.2 commit (`85496a5`) introduces three
additions to the MANIFOLD.3 `ObserverFrame` surface:

- the `PerceptionMode` enum (three enumerators:
  `Identity = 0` / `ConstantVelocityMinkowski` /
  `CurvedChartGeodesicPlaceholder`), declared in
  `namespace rr::manifold` parallel to
  `CoordinateChartType`;
- the `ObserverFrame::perception_mode` field
  (default `PerceptionMode::Identity`), appended
  to the POD so existing aggregate-initialisations
  by-name are unaffected;
- three new `RR_HD inline` validator helpers:
  `default_perception_mode()` → factory;
  `is_orthonormal_tetrad(frame, tol)` →
  pairwise-tetrad-leg orthogonality + unit-length
  gate; `is_finite_observer_frame(frame)` →
  NaN/inf gate over all 21 scalar fields.

The seven pre-existing MANIFOLD.3 fields (`position4`,
`velocity4`, `beta`, three tetrad legs `right` /
`up` / `forward`, `proper_time`, `coordinate_time`)
are preserved verbatim — no rename, no type change,
no default-value change. The MANIFOLD.3 factories
(`rest_frame()`, `observer_frame_from(...)`,
`to_relativity_observer(...)`) and the validator
helper (`is_normalised_timelike(...)`) are
preserved verbatim.

`tests/manifold_identity_tests.cpp` grows by four
new test functions plus two new assertions on
`test_observer_frame_defaults`:

- `test_observer_2_perception_mode_default`
  verifies the factory + the POD default + the
  pairwise enumerator distinctness;
- `test_observer_2_orthonormal_tetrad_default`
  verifies the helper on `rest_frame()` and on
  `observer_frame_from(...)` at non-trivial beta,
  plus failure on three degenerate cases
  (non-unit leg, non-orthogonal legs, collinear
  legs);
- `test_observer_2_finite_observer_frame`
  verifies the helper on the default + the
  moving-observer frame, plus failure on a NaN in
  six independent scalar slots and positive /
  negative infinity in two of them;
- `test_observer_2_default_no_deformation` is the
  operator's "no-op observer does not imply
  coordinate deformation" gate: it pins every
  default field, verifies the bridge to the legacy
  `Observer` carries zero velocity, and asserts
  all three validator gates hold on the default.

The ObserverFrame-exists invariant (check #1) is
**file-level + signature-level verified**: the POD
is at `src/manifold/ObserverFrame.h:138` with the
correct `struct ObserverFrame {` declaration inside
`namespace rr::manifold`.

The position4-exists invariant (check #2) is
**field-level verified** at line 144 with the
documented `Vec4` default `(0, 0, 0, 0)` matching
the chart-space-origin convention from the
architecture-doc §3.3.

The beta/velocity-representation invariant
(check #3) is **two-field verified**: `velocity4`
at line 153 (the canonical 4-velocity) and `beta`
at line 165 (the sibling 3-velocity mirroring the
legacy `rr::relativity::Observer::velocity`). The
two fields are populated consistently by
`observer_frame_from(...)` and the four-velocity
normalisation gate `is_normalised_timelike(...)`
verifies the timelike condition under the chart
metric.

The local-basis / tetrad-placeholder invariant
(check #4) is **three-leg verified** with the new
`is_orthonormal_tetrad(...)` validator gate:
empirically verified on the default world-basis
tetrad + the `observer_frame_from(...)` output
(both pass), plus three documented degenerate
cases (all fail).

The time-placeholders invariant (check #5) is
**two-field verified** at lines 184 + 192 with the
documented zero-initialisation; the
"reserved-but-not-advanced" semantics are
documented at the header's "What does NOT live
here this slice" block (lines 64-66).

The perception-mode-placeholder invariant
(check #6) is **enum + field + factory + test
verified**: the `PerceptionMode` enum at lines
113-136 with the documented three enumerators
(`Identity = 0` explicitly, the other two named
without `*Placeholder` qualifier for `Identity` and
`ConstantVelocityMinkowski` and with the suffix
for `CurvedChartGeodesicPlaceholder` per the
MANIFOLD.1 precedent set by `PenroseLikePlaceholder`
before its PENROSE.4 promotion); the
`perception_mode` field at line 202 with default
`Identity`; the `default_perception_mode()`
factory at line 308; the
`test_observer_2_perception_mode_default` test
verifying all three. Master rule #3 ("no fake
stubs") is satisfied: the field is structurally
consumed by the validator + the planned
OBSERVER.4-OBSERVER.6 slices — not a fake stub.
The header's "What does NOT live here this slice"
block enumerates which subsequent slices populate
+ consume the field.

The defaults-are-camera-equivalent / no-op
invariant (check #7) is **four-layer verified**:
field-by-field default audit (every field
resolves to the scene-rest anchor); bridge
round-trip (`to_relativity_observer(rest_frame())`
returns zero velocity); empirical
no-deformation gate
(`test_observer_2_default_no_deformation` pins
every field + asserts all three validator gates
hold on the default); and the
zero-renderer-touch invariant verified by
`git diff` returning zero non-manifold hits.

The build/test status (check #8) is **directly
verified** by `ctest 12/12 PASS` and the
`manifold_identity_tests` +37 RR_CHECK delta
with no regression in any other test binary.

No `REPAIR` or `BLOCKED` action is outstanding.
The slice is safe to extend.

---

## 4. NEXT

The slice is **safe to extend**. The
`OBSERVER_FRAME_RENDERING_PLAN.md` §7 OBSERVER.*
sub-slice ladder needs a one-step shift to absorb
this audit slot (parallel to the SCHW.4 / SCHW.6 /
SCHW.8 / SCHW.10 audit-slot insertions in the
SCHW.* arc and the PENROSE.3 / PENROSE.5 /
PENROSE.7 / PENROSE.9 / PENROSE.11 audit-slot
insertions in the PENROSE.* arc):

- **OBSERVER.1** — Planning slice
  (LANDED at `eee9d6b`).
- **OBSERVER.2** — Data model
  (LANDED at `85496a5`).
- **OBSERVER.3** — **THIS AUDIT** (ObserverFrame
  Data Model Audit, doc-only).
- **OBSERVER.4** — Config / CLI bridge (was
  OBSERVER.3 in the post-OBSERVER.2 plan;
  renumbered).
- **OBSERVER.5** — Camera-to-observer adapter
  (was OBSERVER.4).
- **OBSERVER.6** — CUDA payload bridge (was
  OBSERVER.5).
- **OBSERVER.7** — OptiX payload bridge (was
  OBSERVER.6).
- **OBSERVER.8** — Observer debug AOV (was
  OBSERVER.7).
- **OBSERVER.9** — Arc capstone audit (was
  OBSERVER.8); closes the observer-frame arc
  per the OBSERVER.1 plan §7.

The
`docs/OBSERVER_FRAME_RENDERING_PLAN.md` §7
sub-slice ladder may be updated by a follow-on
docs slice if the operator prefers an in-plan
renumbering; this audit doc is the canonical
ladder-shift record for the OBSERVER.3 audit-slot
insertion.

No `REPAIR` action is required. No `BLOCKED` item
is outstanding. The next concrete commit the
operator may prompt for is **OBSERVER.4 — Config
/ CLI bridge** per the renumbered OBSERVER.1 plan
§7 OBSERVER.3 → OBSERVER.4 (adds the
`--perception-mode <name>` CLI flag parsed by
`src/core/CommandLine.cpp`, the
`rr::manifold::PerceptionMode perception` field on
`rr::core::Config`, and the scene-loader
extension `apply_observer_frame` that parses an
optional `perception` block on the `.rrscene`
schema; verified by extending `cli_tests` with
new assertions on the flag parse + the kebab-case
→ enumerator mapping).

---

## 5. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") is the load-bearing invariant for the
  perception-mode placeholder.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.3
  Observer Frame — defines the seven-field
  contract OBSERVER.2 preserves.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md` §3, §7
  OBSERVER.2 — the OBSERVER.1 plan brief that
  authorised OBSERVER.2's surface.
- `docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md` —
  MANIFOLD.3 audit that landed the
  pre-OBSERVER.2 `ObserverFrame` POD.
- `docs/PENROSE_LIKE_COMPACTIFICATION_MATH_AUDIT.md`
  (PENROSE.3) — the precedent per-slice POD-leaf
  audit doc this verdict mirrors in structure.
- `docs/MANIFOLD_CONSUMPTION_GAP_AUDIT.md`
  (MANI-CONSUME.2) — the prior audit that
  established the post-OBSERVER.1 baseline at
  `eee9d6b` (`docs/BUILD_PLAN.md` OBSERVER.1
  entry).
- `src/manifold/ObserverFrame.h` (modified at
  `85496a5`) — the audited surface.
- `src/manifold/CoordinateChart.h` — sibling
  POD whose `CoordinateChartType` enum the
  `PerceptionMode` enum is declared parallel to.
- `src/manifold/MetricTensor.h` — sibling header
  whose `is_finite(MetricTensor)` precedent the
  new `is_finite_observer_frame(...)` validator
  mirrors.
- `src/relativity/RelativityMath.h` — the
  `gamma` + `clampBeta` helpers
  `observer_frame_from(...)` consumes.
- `src/relativity/RelativityParams.h` — the
  legacy `Observer` type the
  `to_relativity_observer(...)` bridge maps
  back to.
- `tests/manifold_identity_tests.cpp` (modified
  at `85496a5`) — the test surface that gates
  the OBSERVER.2 invariants; reports `349/349
  checks passed` post-OBSERVER.2 (up from
  `312/312` at the post-MANI-CONSUME.2
  baseline).
- `docs/BUILD_PLAN.md` — OBSERVER.2 entry
  (lines 78641 onward as of `85496a5`).
- Commit `85496a5` — `manifold: OBSERVER.2 —
  ObserverFrame Data Model (impl, POD-leaf)`.
- Commit `eee9d6b` — `docs: OBSERVER.1 —
  Observer-Frame Rendering Plan (docs only)`;
  the audit baseline.

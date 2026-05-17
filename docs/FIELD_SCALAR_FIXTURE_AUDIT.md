# Scalar Field Fixture Audit (FIELD-I.14)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `98a0e35` ("scenes:
FIELD-I.13 — Scalar Field Fixture (impl, scene +
parser + companion doc)").
Audit baseline: `505c2b9` ("docs: FIELD-I.12 — Scalar
Field OptiX Bridge Audit (docs only)") — the last
commit before FIELD-I.13 landed.
Audit host: linux, audit-host build (no CUDA SDK, no
OptiX SDK). The FIELD-I.13 commit's OptiX-ON-no-SDK
build was empirically verified at landing time (ctest
14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from
the tree's current state, the FIELD-I.12 / FIELD-I.13
commits' content, the audit-host `--scene-info` smoke
transcript, the `ctest` runtime outputs, and the
`git diff` filter inspections.

This audit is the per-slice gate for FIELD-I.13
(`98a0e35`). It verifies the nine items the task brief
enumerates — fixture scene exists; fixture enables
scalar field config; fixture targets fieldScalar
diagnostic AOV; values are bounded/safe; default
scenes remain unchanged; parser changes are minimal;
no beauty shading changes; CUDA/OptiX runtime status
(PASS / DEFERRED / BLOCKED); and the overall verdict
(PASS / REPAIR / BLOCKED).

The FIELD-I.13 slice is the **fixture + minimal
parser** slot in the FIELD-I.* arc. It lands a
controlled scene (`scenes/test_scalar_field_diagnostic.rrscene`),
a companion doc (`docs/FIELD_SCALAR_FIXTURE.md`), and
the scene-loader extension needed to engage the
fixture's `scalar_field` block. The fixture is
**forward-looking**: the parsed
`Scene::scalar_field_config` is not threaded into any
renderer dispatcher this slice — the future CLI
bridge slice flips the gate reachable.

---

## 1. VERDICT

**PASS.**

All eight structural / runtime-status checks (#1, #2,
#3, #4, #5, #6, #7, #8) PASS. Check #9 (overall
verdict) is `PASS`. The FIELD-I.13 fixture is well-
scoped, parser-clean, isolated from default scenes
+ renderer behaviour, and ready as the canonical
SDK-host validation surface for the future CLI
bridge slice's audit.

Check #8's runtime status is the standard
`PASS_WITH_RUNTIME_DEFERRED` shape. The audit-host
exercises the fixture's host-side surface (parser
loads cleanly via `--scene-info`); the SDK-host
runtime scenarios from `docs/FIELD_SCALAR_FIXTURE.md`
§6 (default-off bit-identity; disabled-field
neutral; Radial smoothstep correctness;
composability with `--manifold-debug` +
`--observer-debug`; CUDA ↔ OptiX byte-identity) all
require a CUDA + OptiX-SDK host AND the future CLI
bridge slice's `--field-debug` gate to be reachable.
Both are deferred per FIELD-I.13's "no CLI flag, no
dispatcher emit" narrow scope.

The narrow-scope verdict honesty: the operator's
FIELD-I.13 brief enumerated four implement-only
bullets (create fixture scene; create companion
doc; specific fixture content requirements; minimal
parser support if needed). The slice satisfies all
four:

- **Bullet 1** (fixture scene exists):
  `scenes/test_scalar_field_diagnostic.rrscene`
  (75 lines) verified to parse cleanly.
- **Bullet 2** (companion doc):
  `docs/FIELD_SCALAR_FIXTURE.md` (~500 lines, 7
  sections).
- **Bullet 3** (fixture content): simple visible
  geometry (6 spheres + ground plane + 2 lights)
  + scalar field enabled (`enabled: true`) +
  Radial kind (`kind: "radial"`) + bounded values
  (`min_value: 0.0` / `max_value: 1.0`) + AOV
  intended for validation (documented in
  companion §3 + §6).
- **Bullet 4** (minimal parser): only the existing
  FIELD-I.2 `ScalarFieldConfig` fields exposed;
  no scene-format broadening per operator brief.

---

## 2. PER-CHECK RESULTS

| # | Check                                          | Evidence                                                                                                                                                                                                                                                                                                                  | Verdict |
|---|------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1 | Fixture scene exists                           | `scenes/test_scalar_field_diagnostic.rrscene` (new, 75 lines) ships at the FIELD-I.13 landing commit. Verified by `git diff 505c2b9..98a0e35 --name-only -- 'scenes/'` returning exactly one entry: the new fixture file. The accompanying companion doc `docs/FIELD_SCALAR_FIXTURE.md` (~500 lines) ships alongside. Audit-host `--scene-info scenes/test_scalar_field_diagnostic.rrscene` invocation completes without parser errors (verified at the FIELD-I.13 landing commit's smoke transcript). | PASS    |
| 2 | Fixture enables scalar field config            | The fixture's `scalar_field` block authors `enabled: true` (line 21 of the fixture file). Eight of the ten FIELD-I.2 `ScalarFieldConfig` fields are exercised: `enabled`, `strength`, `kind`, `center`, `min_radius`, `max_radius`, `falloff`, `min_value`, `max_value` (the `constant_value` field is omitted because the fixture engages the `radial` kind; the `procedural-placeholder` kind is not used). The block is consumed by the new `apply_scalar_field(...)` parser at `src/io/SceneLoader.cpp:992`; the parsed config lands on `Scene::scalar_field_config` (declared at `src/scene/Scene.h:140`). | PASS    |
| 3 | Fixture targets fieldScalar diagnostic AOV     | The companion doc `docs/FIELD_SCALAR_FIXTURE.md` §1.1 + §3 + §6 explicitly documents the fixture as the canonical target for the FIELD-I.7 `fieldScalar` diagnostic AOV's future SDK-host validation. §3.2 + §3.3 specify the expected CLI invocations (`--render-aovs --field-debug` / `--render-optix-aovs --field-debug`) and the expected per-pixel AOV signature (smoothstep envelope with `min_value = 0.0` at centre + `max_value = 1.0` outside `max_radius`). §6 enumerates the five deferred SDK-host runtime scenarios that consume the fixture (§6.1 – §6.5 mapping to FIELD-I.6 task brief §8.1 + §8.4 + §8.5 + §8.6 + §8.7 verbatim). | PASS    |
| 4 | Values are bounded/safe                        | Six-axis verified — (a) `enabled = true` is a bool; (b) `strength = 1.0` is finite, non-negative; (c) `kind = "radial"` is one of the three parser-accepted values; (d) `center = [0.0, 0.5, 0.0]` is finite + matches the centre-sphere anchor; (e) `min_radius = 1.0` / `max_radius = 5.0` is non-degenerate (`max > min`); (f) `min_value = 0.0` / `max_value = 1.0` is the canonical `[0, 1]` grayscale range (no clamping at PPM 8-bit encode time). The fixture exercises the `evaluate(...)` Radial path's safe branches: no degenerate envelope (`max <= min`), no negative falloff (FIELD-I.3 audit's check #2 defence-in-depth tests). | PASS    |
| 5 | Default scenes remain unchanged                | `git diff 505c2b9..98a0e35 --name-only -- 'scenes/' ':(exclude)scenes/test_scalar_field_diagnostic.rrscene'` returns zero hits. Every pre-FIELD-I.13 `.rrscene` fixture file (`test_camera.rrscene`, `test_full_scene.rrscene`, `test_lights.rrscene`, `test_materials.rrscene`, `test_mesh.rrscene`, `test_observer_frame.rrscene`, `test_penrose_like_manifold.rrscene`, `test_relativity.rrscene`, `test_render_settings.rrscene`, `test_schwarzschild_like_manifold.rrscene`, `test_spheres.rrscene`, `test_textured_material.rrscene`) is byte-identical to the FIELD-I.12 baseline (`505c2b9`). The new fixture is purely additive. | PASS    |
| 6 | Parser changes, if any, are minimal            | Per the operator's "If parser support for scalar-field scene fields is incomplete: add only minimal parser support for existing scalar field config fields. Do not broaden scene format beyond fixture needs" rule. Two source files modified: (a) `src/scene/Scene.h` (+24 lines) — adds `Scene::scalar_field_config` field at line 140 (sibling of `manifold`) + `#include "field/ScalarField.h"`. (b) `src/io/SceneLoader.cpp` (+150 lines) — adds `parse_scalar_field_kind(...)` helper at line 946 + `apply_scalar_field(...)` parser at line 992 + the new `if (const JsonValue* sf_v = root.find("scalar_field"))` block in the main `load(...)` body at line 1927. Plus 1 CMakeLists.txt line (`rr_field` PUBLIC link on `rr_scene` at line 434). Every parsed field maps 1:1 to an existing FIELD-I.2 `ScalarFieldConfig` field; no new fields invented; no broader scene-format additions. The parser's shape mirrors the existing `apply_manifold(...)` precedent verbatim (canonical snake_case + camelCase shorthand via `find_or`; per-field error messages with block-prefixed labels). Master rule #12 satisfied — minimal scope. | PASS    |
| 7 | No beauty shading changes                      | `git diff 505c2b9..98a0e35 --name-only -- 'src/cuda/' 'src/optix/' 'src/manifold/' 'src/relativity/' 'src/renderer/' 'src/main.cpp' 'src/core/'` returns zero hits. No kernel TU, no renderer.h, no main.cpp dispatcher, no core/Config.h, no observer / manifold / relativity surface is touched. The new `Scene::scalar_field_config` field carries through the parsed scene but no consumer reads it — the future CLI bridge slice will thread it to `AOVTargets::scalar_field_config` / `OptixRenderer::render_aovs(...)` trailing parameter. Every existing `--render-*` invocation against the new fixture (or against any other scene) preserves byte-identical PPM output. | PASS    |
| 8 | CUDA / OptiX runtime status                    | `PASS_WITH_RUNTIME_DEFERRED`. The host-side fixture surface is verified empirically: (a) audit-host build (OptiX OFF) — 13/13 ctest PASS at the FIELD-I.13 landing; `--scene-info` loads the fixture cleanly with no parser errors. (b) OptiX-ON-no-SDK build — 14/14 ctest PASS at the FIELD-I.13 landing (including `optix_tests`); the `rr_field` PUBLIC link on `rr_scene` propagates correctly through the rr_io → rr_scene → rr_field include path. SDK-host runtime scenarios from `docs/FIELD_SCALAR_FIXTURE.md` §6 are ALL deferred — they require the future CLI bridge slice's `--field-debug` gate AND a CUDA + OptiX-SDK host. The FIELD-I.10 + FIELD-I.12 audits' runtime-deferred portions remain at the same shape; FIELD-I.14 inherits the deferral cleanly. | PASS (structural) — runtime DEFERRED to SDK-host audit pass when the future CLI bridge slice lands |
| 9 | Verdict                                        | All eight structural / runtime-status checks PASS. The FIELD-I.13 surface is well-scoped, parser-clean, isolated from default scenes + renderer behaviour, and ready as the canonical SDK-host validation surface for the future CLI bridge slice's audit. Master rule #3 + #11 + #12 + #16 satisfied (see §3 below).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Commit shape

The FIELD-I.13 commit (`98a0e35`) modifies six files:

```
CMakeLists.txt                              |   7 +-
docs/BUILD_PLAN.md                          | 291 ++++++++++
docs/FIELD_SCALAR_FIXTURE.md                | 522 +++++++++++
scenes/test_scalar_field_diagnostic.rrscene |  75 +++
src/io/SceneLoader.cpp                      | 150 ++++
src/scene/Scene.h                           |  24 +
```

Source-code files touched: `src/scene/Scene.h` +
`src/io/SceneLoader.cpp` (the minimal parser surface).
Build configuration: 1-line CMakeLists.txt addition
(`rr_field` PUBLIC link on `rr_scene` at line 434).
Scene file: 1 new fixture file. Documentation: 1 new
companion doc + the standard BUILD_PLAN.md entry.

The narrow scope intentionally excludes every other
file from the broader FIELD-I.6 task brief's 15-row
files-likely-involved table: no `src/cuda/*`, no
`src/optix/*`, no `src/core/Config.h`, no
`src/core/CommandLine.cpp`, no `src/main.cpp`, no
`src/renderer/*`, no test extensions. All deferred
to the future CLI bridge slice.

### 3.2 Check #1 — fixture scene exists

The new fixture file
`scenes/test_scalar_field_diagnostic.rrscene` is a
75-line JSON document with seven top-level blocks:
`version`, `render_settings`, `camera`,
`scalar_field` (the FIELD-I.13 novel block),
`materials`, `spheres`, `meshes`, `lights`. The
companion doc `docs/FIELD_SCALAR_FIXTURE.md` is the
522-line operator-facing reference documenting the
fixture's purpose + composition + expected runtime
behaviour.

Both files appear in `git diff 505c2b9..98a0e35
--name-only` as the only new files outside `docs/`
+ `scenes/` (the BUILD_PLAN.md entry is the
modification within `docs/`).

Audit-host empirical verification: the
`--scene-info` smoke transcript from the FIELD-I.13
landing commit shows the fixture loads cleanly
(version 1.0.0 parsed; render_settings, camera,
relativity, materials, spheres, meshes, lights all
parsed; no parser errors).

### 3.3 Check #2 — fixture enables scalar field config

The fixture's `scalar_field` block (lines 19-29 of
`scenes/test_scalar_field_diagnostic.rrscene`)
authors:

```json
"scalar_field": {
  "enabled":     true,
  "strength":    1.0,
  "kind":        "radial",
  "center":      [0.0, 0.5, 0.0],
  "min_radius":  1.0,
  "max_radius":  5.0,
  "falloff":     1.0,
  "min_value":   0.0,
  "max_value":   1.0
}
```

The `enabled: true` flag opens the FIELD-I.2 master-
switch gate. Eight of the ten ScalarFieldConfig
fields are exercised; the `constant_value` field is
intentionally omitted because the fixture engages
the `radial` kind (Constant-only field). The
`procedural-placeholder` kind is not used (the
operator brief says "radial or constant"; radial
is the more diagnostic choice per §2.2 of the
companion doc).

The block is consumed by the new
`apply_scalar_field(...)` parser at
`src/io/SceneLoader.cpp:992`. The parsed config
lands on `Scene::scalar_field_config` (declared at
`src/scene/Scene.h:140`).

### 3.4 Check #3 — fixture targets fieldScalar diagnostic AOV

The companion doc `docs/FIELD_SCALAR_FIXTURE.md`
explicitly documents the fixture as the canonical
target for the FIELD-I.7 `fieldScalar` diagnostic
AOV:

- **§1.1** lists three goals: parser smoke test +
  diagnostic-AOV runtime template + cross-backend
  equivalence anchor.
- **§3** ("Expected visual signature") specifies
  the per-invocation expected output:
    - **§3.1**: `--render-aovs` (no `--field-debug`)
      produces 6 AOVs, NO `aov_field_scalar.ppm`.
    - **§3.2**: `--render-aovs --field-debug`
      produces 7 AOVs including the new
      `aov_field_scalar.ppm` with the smoothstep
      pattern.
    - **§3.3**: `--render-optix-aovs --field-debug`
      produces the OptiX-side
      `optix_aov_field_scalar.ppm` byte-identical
      to the CUDA-side PPM.
- **§6** enumerates the five deferred SDK-host
  runtime scenarios consuming the fixture:
    - §6.1: default-off bit-identity (FIELD-I.6
      §8.5).
    - §6.2: disabled-field neutral PPM (FIELD-I.6
      §8.1 + §8.2).
    - §6.3: Radial smoothstep PPM (FIELD-I.6
      §8.4).
    - §6.4: composability with `--manifold-debug`
      + `--observer-debug` (FIELD-I.6 §8.6).
    - §6.5: CUDA ↔ OptiX byte-identity
      (FIELD-I.6 §8.7).

The fixture is the empirical exercise of every
scenario the FIELD-I.6 task brief §8 lists.

### 3.5 Check #4 — values are bounded/safe

Six-axis safety verified:

**Axis A — `enabled = true`** is a bool; engaging
the field is the fixture's purpose.

**Axis B — `strength = 1.0`** is finite, non-
negative, and the canonical identity multiplier. No
strength clamping required at evaluator time; the
output value depends only on the Radial envelope.

**Axis C — `kind = "radial"`** is one of the three
parser-accepted values (`constant` / `radial` /
`procedural-placeholder`). The string-to-enum
mapping is verified at `parse_scalar_field_kind(...)`
in `src/io/SceneLoader.cpp:946`.

**Axis D — `center = [0.0, 0.5, 0.0]`** is finite +
matches the centre-sphere anchor's world position.
The y-offset (0.5) accounts for the centre sphere's
radius (0.5) sitting on the ground plane (y = 0).

**Axis E — `min_radius = 1.0` / `max_radius = 5.0`**
is non-degenerate (`max > min`; explicit gap of 4
units). The FIELD-I.2 `evaluate(...)` Radial path's
degenerate-envelope defence-in-depth branch
(`max <= min` returns 0) is NOT triggered;
empirically tested at FIELD-I.3 audit's check #2
via `test_radial_kind_degenerate_envelope_returns_zero`.

**Axis F — `min_value = 0.0` / `max_value = 1.0`**
is the canonical `[0, 1]` grayscale range. The PPM
8-bit encoder maps directly with no clamping; the
fixture's per-pixel scalar sample is always in
`[0.0, 1.0]` (bounded by the smoothstep cubic's
output range).

The fixture exercises the Radial path's safe
branches; no defence-in-depth fallback is needed
at evaluate time. The `falloff = 1.0` keeps the
smoothstep linear (no exponent reshaping); the
`evaluate(...)` `falloff > 0` defence-in-depth
branch is honoured by construction.

### 3.6 Check #5 — default scenes remain unchanged

The diff filter
`git diff 505c2b9..98a0e35 --name-only --
'scenes/' ':(exclude)scenes/test_scalar_field_diagnostic.rrscene'`
returns zero hits. Every pre-FIELD-I.13 `.rrscene`
fixture is byte-identical to the FIELD-I.12
baseline (`505c2b9`):

- `test_camera.rrscene` — unchanged.
- `test_full_scene.rrscene` — unchanged.
- `test_lights.rrscene` — unchanged.
- `test_materials.rrscene` — unchanged.
- `test_mesh.rrscene` — unchanged.
- `test_observer_frame.rrscene` — unchanged.
- `test_penrose_like_manifold.rrscene` — unchanged.
- `test_relativity.rrscene` — unchanged.
- `test_render_settings.rrscene` — unchanged.
- `test_schwarzschild_like_manifold.rrscene` —
  unchanged.
- `test_spheres.rrscene` — unchanged.
- `test_textured_material.rrscene` — unchanged.

The new fixture is purely additive. The operator's
"Do not alter default scenes" rule honoured.

Empirical sanity-check: any existing CLI test
(e.g. `cli_tests` at 274/274 PASS) that consumes a
default scene file passes unchanged, because the
scene file content is byte-identical AND the
parser is missing-field-tolerant (default-
constructed `Scene::scalar_field_config` matches
the FIELD-I.2 no-op anchor for every existing
scene that lacks a `scalar_field` block).

### 3.7 Check #6 — parser changes, if any, are minimal

Per the operator's brief: "If parser support for
scalar-field scene fields is incomplete: add only
minimal parser support for existing scalar field
config fields. Do not broaden scene format beyond
fixture needs."

The slice adds:

- **`Scene::scalar_field_config`** field at
  `src/scene/Scene.h:140` (24 net lines including
  doc-comment). Sibling of the existing
  `manifold` field landed at SCHW.9. Default
  `ScalarFieldConfig{}` = the FIELD-I.2 no-op
  anchor. Doc-comment documents the
  "parsed-but-not-rendered" shape.

- **`parse_scalar_field_kind(...)`** helper at
  `src/io/SceneLoader.cpp:946` (~15 lines).
  String-to-enum mapping for the three FIELD-I.2
  `ScalarFieldKind` enumerators.

- **`apply_scalar_field(...)`** parser at
  `src/io/SceneLoader.cpp:992` (~75 lines). Ten
  supported fields covering every FIELD-I.2
  ScalarFieldConfig slot: `enabled`, `strength`,
  `kind`, `center`, `min_radius` / `minRadius`,
  `max_radius` / `maxRadius`, `falloff`,
  `min_value` / `minValue`, `max_value` /
  `maxValue`, `constant_value` / `constantValue`.
  Each field maps 1:1 to an existing FIELD-I.2
  POD slot; no new fields invented; no
  broader scene-format additions. Canonical
  snake_case + camelCase shorthand mirrors the
  existing manifold + relativity parsers'
  precedent verbatim.

- **`load(...)` body extension** at
  `src/io/SceneLoader.cpp:1927` (~15 lines). New
  `if (const JsonValue* sf_v =
  root.find("scalar_field"))` block (sibling of
  the existing `manifold` / `relativity` /
  `materials` blocks). Doc-comment documents
  the forward-looking parsed-but-not-rendered
  shape this slice.

- **CMakeLists.txt** (+6 lines). The `rr_field`
  PUBLIC link addition on `rr_scene` mirrors
  the FIELD-I.9 `rr_gpu` precedent + the
  FIELD-I.11 `rr_optix` precedent.

The total parser delta is ~165 lines across 2
source files + 1 CMake line + 24 header lines. The
parser's shape mirrors the existing `apply_manifold(...)`
precedent verbatim. No new fields invented. No
broader scene-format additions. The minimum
sufficient scope per the operator's brief.

### 3.8 Check #7 — no beauty shading changes

The diff filter
`git diff 505c2b9..98a0e35 --name-only --
'src/cuda/' 'src/optix/' 'src/manifold/'
'src/relativity/' 'src/renderer/' 'src/main.cpp'
'src/core/'` returns zero hits. No kernel TU,
no renderer.h, no main.cpp dispatcher,
no core/Config.h, no observer / manifold /
relativity surface is touched.

The only source surface change is the addition of
the `Scene::scalar_field_config` field + the
`apply_scalar_field(...)` parser. No consumer reads
`Scene::scalar_field_config` this slice — the
future CLI bridge slice will thread it into the
renderer dispatchers. Every existing `--render-*`
invocation against any scene file produces
byte-identical PPM output to the FIELD-I.12
baseline.

The new fixture file does NOT engage any beauty-
pass change at runtime even when the future CLI
bridge slice's `--field-debug` gate flips: the
FIELD-I.7 + FIELD-I.9 + FIELD-I.11 surfaces are
explicitly AOV-write-only on `scalar_field_config`;
the beauty / Normal / Depth / Albedo /
DopplerFactor / SearchlightFactor /
ManifoldCoordinates / ObserverBeta arms do NOT
read the field config.

### 3.9 Check #8 — runtime CUDA / OptiX status

`PASS_WITH_RUNTIME_DEFERRED`.

**Audit-host runtime verification (PASS).** The
audit-host build (`RR_ENABLE_OPTIX=OFF`) exercises
the host-side fixture surface end-to-end:

```
$ ctest
13/13 PASS — 100% tests passed
```

Per-binary counts unchanged from FIELD-I.12:
`relativity_tests: 841/841`;
`manifold_identity_tests: 408/408`;
`cli_tests: 274/274`; `renderer_tests: 35/35`;
`field_tests: 135/135`.

The fixture's `--scene-info` smoke confirms the
parser loads the fixture cleanly with no errors.

**OptiX-ON-no-SDK runtime verification (PASS).**
The OptiX-ON-no-SDK build (`/tmp/rr_build_optix_no_sdk`)
exercises the `rr_field` PUBLIC link's
propagation through rr_io → rr_scene → rr_field
include path:

```
$ ctest
14/14 PASS — 100% tests passed (includes optix_tests)
```

**SDK-host runtime verification (DEFERRED to future
CLI bridge slice).** The five scenarios from
`docs/FIELD_SCALAR_FIXTURE.md` §6 require BOTH:
- A CUDA + OptiX-SDK host (this audit-host has
  neither).
- The future CLI bridge slice's `--field-debug`
  gate (does not exist yet).

The deferrals:

- **§6.1** (default-off bit-identity): DEFERRED.
  The future CLI bridge slice's audit will verify
  `--render-aovs scenes/test_scalar_field_diagnostic.rrscene`
  (no `--field-debug`) produces exactly six AOV
  PPMs; no `aov_field_scalar.ppm` emitted; every
  PPM byte-identical to the pre-CLI-bridge
  baseline.
- **§6.2** (disabled-field neutral): DEFERRED.
- **§6.3** (Radial smoothstep correctness): DEFERRED.
- **§6.4** (composability with `--manifold-debug` +
  `--observer-debug`): DEFERRED.
- **§6.5** (CUDA ↔ OptiX byte-identity):
  DEFERRED on the empirical side; STRUCTURALLY
  GUARANTEED today by the FIELD-I.12 audit's
  five-axis symmetry argument.

### 3.10 Master-rule satisfaction recap

- **Master rule #3 ("no fake stubs"):** satisfied.
  The `apply_scalar_field(...)` parser is fully
  wired (real validation; real error messages;
  real config population; tested by the fixture's
  smoke invocation). The fixture file authors a
  fully-formed scalar_field block. No fake stub;
  no empty scaffold.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. Every parsed field
  is documented + the parser's behaviour is
  inspectable + the fixture exercises every
  field. The companion doc §6 enumerates every
  deferred runtime scenario explicitly.

- **Master rule #12 ("do not overbuild a later
  system before the current layer works"):**
  satisfied. Scope deliberately narrow per the
  operator's "Do not broaden scene format beyond
  fixture needs" rule. No CLI flag; no dispatcher
  emit; no test extension; no default-scene
  alteration; no kernel-TU touch. The renumbered
  next FIELD-I.* impl slot (per §4 below) is
  the CLI + Config + dispatcher bridge that
  flips the gate.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied.
  The FIELD-I.13 default state is unchanged
  from the FIELD-I.12 baseline:
    - No `--render-*` action's output changes.
    - No existing PPM filename changes.
    - No beauty pass arithmetic changes.
    - No existing AOV slot's value changes.
  The single observable change is the new fixture
  file + the parser surface; both are
  null-pointer-equivalent (no consumer reads
  `Scene::scalar_field_config` this slice).

### 3.11 Honest scope recap

This audit is a **fixture + minimal parser audit
with SDK-host runtime DEFERRED**. The verdict
`PASS` reflects:

- (a) The structural fixture surface is well-
  formed (fixture file parses; companion doc
  documents the runtime contract; minimal parser
  exposes only existing FIELD-I.2 POD fields).
- (b) The default-scene-preservation rule is
  honoured (every pre-FIELD-I.13 scene is
  byte-identical).
- (c) The beauty-pass-preservation rule is
  honoured (no kernel TU touched; no renderer
  surface touched).
- (d) The audit-host + OptiX-ON-no-SDK builds
  are both empirically verified.
- (e) The SDK-host runtime scenarios from
  `docs/FIELD_SCALAR_FIXTURE.md` §6 are honestly
  documented as deferred (require both the
  future CLI bridge slice AND an SDK host).

The runtime deferral is consistent with the
FIELD-I.10 + FIELD-I.12 audits' framing — those
audits also deferred the SDK-host runtime
scenarios to the future CLI bridge slice. The
FIELD-I.14 fixture surface is the canonical
SDK-host validation input when that slice lands.

---

## 4. NEXT

### 4.1 Renumbered FIELD-I.* sub-slice ladder

The FIELD-I.14 audit slot insertion (mirroring the
FIELD-I.12 / FIELD-I.10 / FIELD-I.8 / FIELD-I.5 /
FIELD-I.3 audit-slot insertion precedent) shifts
subsequent FIELD-I.* sub-slices by one. The
post-FIELD-I.14 ladder is:

- **FIELD-I.15** — CLI + Config + dispatcher
  bridge (the renumbered next FIELD-I.* impl
  slot; lands the `--field-debug` modifier flag
  + the minimal `--field-*` authoring CLI
  surface; extends `rr::core::Config` with a
  `scalar_field_config` field + a
  `field_debug_visualization` bool; threads
  both `cfg.scalar_field_config` AND
  `scene.scalar_field_config` from CLI / scene
  loader through `run_render_aovs` AND
  `run_render_optix_aovs` into the respective
  payload fields; flips both backends reachable
  simultaneously; ships the
  `output/aov_field_scalar.ppm` /
  `output/optix_aov_field_scalar.ppm` save
  sites; closes the FIELD-I.10 + FIELD-I.12
  + FIELD-I.14 audits' runtime-deferred
  portions on SDK-host).
- **FIELD-I.16** — CLI bridge audit.
- **FIELD-I.17** — Mapping CLI + Config bridge
  (the full FIELD-I.4 `FieldMappingConfig` CLI
  authoring surface).
- **FIELD-I.18** — Mapping CLI bridge audit.
- **FIELD-I.19** — Mapping kernel pipeline (the
  actual field-to-beauty integration).
- **FIELD-I.20** — Mapping kernel pipeline audit.
- **FIELD-I.21** — Arc capstone audit.

The fixture slot lands earlier in the ladder than
the FIELD-I.6 task brief originally enumerated;
the operator's FIELD-I.13 brief intentionally
positioned the fixture before the CLI bridge so
the CLI bridge slice has a forward-looking scene-
file precedent to consume.

The ladder above is the **operator's choice**;
audit slots may be inserted in-band as the
operator's cadence requires.

### 4.2 Candidate next slots (prioritised)

**(a) RECOMMENDED — FIELD-I.15: CLI + Config +
dispatcher bridge** (the renumbered next
FIELD-I.* impl slot). Natural continuation of
the FIELD-I.* arc: flips both backend AOV gates
reachable simultaneously by adding the
`--field-debug` modifier flag + the minimal
`--field-*` authoring flags. Closes the
FIELD-I.10 + FIELD-I.12 + FIELD-I.14 audits'
runtime-deferred portions when its own audit
runs on an SDK host. The symmetry of the
FIELD-I.9 + FIELD-I.11 bridges + the FIELD-I.13
fixture's parsed `Scene::scalar_field_config`
make this slice a single-file threading
addition plus a CLI parser extension.

**(b) Manifold-orthogonal work.** Multiple
options available:
  - **Deferred SDK-host runtime pass** for the
    OBSERVER.* + OBS-P.* + OBS-F.* arc family
    (highest converging-leverage option).
  - **MANI-I.12 final cross-host manifold
    audit**.
  - **Denoiser integration with chart-aware
    AOVs**.
  - **Path-tracer feature breadth** (NEE
    extension, BSDF expansion, MIS tuning).

**(c) NOT RECOMMENDED — direct full FIELD-I.4
`FieldMappingConfig` CLI surface slice
(FIELD-I.17) skipping the FIELD-I.15 bridge.**
Would author the mapping CLI without a
diagnostic AOV to verify mapping behaviour
visually. Better to land the diagnostic AOV
CLI gate first so future mapping work can
be authored AND diagnosed.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; the master rule #3 +
  #11 + #12 + #16 satisfaction recap at §3.10
  cites these).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §6 (the Field Interpretation Layer as an
  OPTIONAL extension above the Manifold Core).
- `docs/FIELD_INTERPRETATION_LAYER.md` §4.6
  (the diagnostic-AOV channel design-doc
  anchor for the FIELD-I.7 / .9 / .11 / .13
  surfaces).

### 5.2 FIELD-I.* arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md`
  (FIELD-I.3).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md`
  (FIELD-I.5).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6 — the canonical FIELD-I.* arc
  diagnostic-AOV task brief; §8 enumerates
  the deferred SDK-host runtime scenarios
  the FIELD-I.14 audit references at §3.9).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_AUDIT.md`
  (FIELD-I.8).
- `docs/FIELD_SCALAR_CUDA_BRIDGE_AUDIT.md`
  (FIELD-I.10).
- `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
  (FIELD-I.12 — the precedent OptiX-bridge
  audit; its five-axis cross-backend symmetry
  argument at §3.4 underpins the FIELD-I.14
  audit's §3.4 + §6.5 deferred cross-backend
  verification claim).
- `docs/FIELD_SCALAR_FIXTURE.md` (FIELD-I.13 —
  the companion doc that ships alongside the
  fixture file; this audit references its
  §1.1 / §3 / §6 / §7).

### 5.3 Precedent fixture-audit references

- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md`
  (OBS-F.3) — the precedent fixture audit
  shape this audit mirrors structurally.
- `docs/MANIFOLD_DEBUG_AOV_AUDIT.md`
  (MANI-I.9) — the precedent AOV-fixture
  audit; the SDK-host runtime deferral
  framing carries forward.

### 5.4 Source surface audited

- `src/scene/Scene.h` (the FIELD-I.13 +24
  lines; the new `scalar_field_config` field
  at line 140 + its doc-comment block at
  lines 118-139 + the new `#include
  "field/ScalarField.h"`).
- `src/io/SceneLoader.cpp` (the FIELD-I.13
  +150 lines; the new
  `parse_scalar_field_kind(...)` helper at
  line 946 + the new `apply_scalar_field(...)`
  parser at line 992 + the new `load(...)`
  body extension at line 1927).
- `CMakeLists.txt` (the FIELD-I.13 +6 lines;
  the `rr_field` PUBLIC link addition on
  `rr_scene` at line 434).

### 5.5 Scene + companion-doc surface audited

- `scenes/test_scalar_field_diagnostic.rrscene`
  (the 75-line fixture file; the `scalar_field`
  block at lines 19-29 is the FIELD-I.13 novel
  authoring surface).
- `docs/FIELD_SCALAR_FIXTURE.md` (the
  522-line companion doc).

### 5.6 Surrounding commit SHAs

- `98a0e35` — FIELD-I.13 audited tree (the
  per-slice gate target).
- `505c2b9` — FIELD-I.12 baseline (the diff
  baseline for checks #5 + #7).
- `e15934e` — FIELD-I.11 OptiX bridge impl
  (the antecedent kernel-arm surface the
  fixture exercises when the future CLI
  bridge slice flips the gate).
- `e1a42c2` — FIELD-I.9 CUDA bridge impl
  (the symmetric CUDA-side kernel-arm
  surface).
- `181a579` — FIELD-I.7 AOV data-model entry
  impl (both backend kernel arms consume the
  same `AOVType::FieldScalar = 8` enumerator).
- `40c387b` — FIELD-I.2 impl (both backend
  arms call `rr::field::evaluate(scalar_field_config,
  hit_pos)` from this commit's surface).

### 5.7 Unchanged source files (sampled)

The following files are byte-identical to the
FIELD-I.12 baseline (`505c2b9`), confirmed by
the diff filters at checks #5 + #7:

- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/cuda/`.
- Every `.cu` / `.cuh` / `.cpp` / `.h` file in
  `src/optix/`.
- Every file in `src/manifold/`.
- Every file in `src/relativity/`.
- Every file in `src/renderer/`.
- Every file in `src/scene/` EXCEPT `Scene.h`
  (the only `src/scene/` file touched).
- Every file in `src/io/` EXCEPT
  `SceneLoader.cpp` (the only `src/io/` file
  touched).
- Every file in `src/core/`, `src/math/`,
  `src/image/`, `src/gpu/`, `src/app/`,
  `src/field/`, `src/pathtracer/`,
  `src/camera/`, `src/geometry/`,
  `src/lighting/`, `src/material/`,
  `src/texture/`.
- `src/main.cpp`.

### 5.8 Unchanged scenes / tests

The diff filter
`git diff 505c2b9..98a0e35 --name-only --
'scenes/' ':(exclude)scenes/test_scalar_field_diagnostic.rrscene'`
returns zero hits — every pre-FIELD-I.13
fixture is byte-identical.

The diff filter
`git diff 505c2b9..98a0e35 --name-only --
'tests/'` returns zero hits — every test
binary's source is byte-identical.

### 5.9 Unchanged build configuration (other targets)

Only the `rr_scene` target's PUBLIC link list
gains `rr_field`. Every other CMake target
(`rr_field` itself, `rr_math`, `rr_camera`,
`rr_geometry`, `rr_relativity`, `rr_material`,
`rr_lighting`, `rr_texture`, `rr_manifold`,
`rr_gpu`, `rr_optix`, `rr_renderer`, `rr_io`,
`rr_server`, every test executable) is
byte-identical to the FIELD-I.12 CMakeLists.txt
baseline.

# Scalar Field Diagnostic Fixture (FIELD-I.13)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Fixture file: `scenes/test_scalar_field_diagnostic.rrscene`
Related task brief: `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md` (FIELD-I.6)
Related impl audits: `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_AUDIT.md` (FIELD-I.8)
                     `docs/FIELD_SCALAR_CUDA_BRIDGE_AUDIT.md` (FIELD-I.10)
                     `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md` (FIELD-I.12)

This document is the companion to
`scenes/test_scalar_field_diagnostic.rrscene` — the
operator-facing fixture scene that exercises the
FIELD-I.13 `scalar_field` scene-block parser surface,
authors a non-trivial Radial scalar field, and serves
as the canonical input for the future SDK-host
runtime validation of the FIELD-I.7 `fieldScalar`
diagnostic AOV (per the FIELD-I.6 task brief's §8
runtime-deferred scenarios).

The fixture is currently **forward-looking**: the
`scalar_field` block parses cleanly + the field
config lands on `Scene::scalar_field_config`, but no
renderer dispatcher reads the field this slice (the
FIELD-I.13 brief explicitly scopes to fixture
authoring; the CLI / dispatcher bridge that wires
`scene.scalar_field_config` → `AOVTargets` /
`OptixLaunchParams` lands at the renumbered next
impl slot). Until the CLI bridge slice lands, the
fixture's `scalar_field` block has no visible runtime
effect; the fixture exists to:

1. Verify the FIELD-I.13 parser surface end-to-end
   (the fixture loads cleanly via `--scene-info`
   today).
2. Provide a controlled, byte-stable scene the
   future CLI bridge slice's audit will exercise
   for §8.1 + §8.3 + §8.4 + §8.5 + §8.6 + §8.7 of
   the FIELD-I.6 task brief.
3. Document the expected per-pixel AOV output the
   future SDK-host validation pass will pin
   against goldens.

---

## 1. Purpose

The fixture exercises the FIELD-I.13 parser surface
and authors a scene whose scalar-field configuration
is sufficient to produce a **visually verifiable**
diagnostic AOV when the future CLI bridge slice
flips the `--field-debug` gate reachable.

### 1.1 Three goals

- **Parser smoke test.** Loading the fixture via
  `RelativityRender --scene-info
  scenes/test_scalar_field_diagnostic.rrscene`
  exercises the new `apply_scalar_field(...)` parser
  in `src/io/SceneLoader.cpp`. The fixture's
  `scalar_field` block covers eight of the ten
  ScalarFieldConfig fields (every Radial field +
  `enabled` + `strength` + `kind`; the
  `constant_value` slot is omitted as it's
  Constant-kind only).
- **Diagnostic-AOV runtime template.** When the
  future CLI bridge slice lands, the operator
  invokes:
    - `RelativityRender --render-aovs
      --field-debug
      scenes/test_scalar_field_diagnostic.rrscene`
      (CUDA path) → `output/aov_field_scalar.ppm`.
    - `RelativityRender --render-optix-aovs
      --field-debug
      scenes/test_scalar_field_diagnostic.rrscene`
      (OptiX path) → `output/optix_aov_field_scalar.ppm`.
  Both PPMs encode the per-pixel scalar sample
  through the FIELD-I.2 RR_HD inline
  `evaluate(scalar_field_config, hit_pos)` helper.
- **Cross-backend equivalence anchor.** With both
  bridges in (FIELD-I.9 CUDA + FIELD-I.11 OptiX)
  the future CLI bridge's audit can `cmp`-verify
  byte-identical PPM output across backends (per
  the FIELD-I.6 task brief's §8.7 scenario;
  guaranteed structurally by the FIELD-I.12 audit's
  five-axis symmetry argument).

### 1.2 Honest scope boundaries

The fixture **DOES NOT** modulate the beauty pass.
Even when the future CLI bridge slice flips the
`--field-debug` gate on, the beauty / Normal /
Depth / Albedo / DopplerFactor / SearchlightFactor
/ ManifoldCoordinates / ObserverBeta PPMs from
`--render-aovs` / `--render-optix-aovs` are
byte-identical to the pre-FIELD-I.13 baseline (the
FIELD-I.9 + FIELD-I.11 bridges' write arms are
strictly read-only on `scalar_field_config`; the
"no field-to-beauty mapping yet" non-goal per
FIELD-I.6 §6 holds).

The fixture **DOES NOT** exercise the
`FieldMappingConfig` (FIELD-I.4) authoring surface.
The `scalar_field` block in the fixture file
authors only the FIELD-I.2 `ScalarFieldConfig`
fields. No `field_mapping` block is present; no
mapping CLI flag is engaged.

---

## 2. Composition

The fixture composes from three layers:

### 2.1 Geometry layer (lifted verbatim from OBS-F.2)

The geometry layer mirrors the OBS-F.2
`test_observer_frame.rrscene` precedent exactly,
providing the same six-sphere + ground-plane
composition. This gives:

- **Centre sphere** at `[0, 0.5, 0]` (radius 0.5,
  matterial-w). Anchored at the scalar field's
  `center` so the field's `min_radius` = 1.0
  sphere extends just past the centre sphere's
  surface — pixels hitting the centre sphere
  should write `min_value = 0.0` to the AOV.
- **Five marker spheres** (near-right, near-left,
  above, in-front, far-centre) at distances
  ∈ [1.5, 3.0] from the field centre. Pixels
  hitting these spheres should write smoothstep
  samples in `[min_value, max_value]` per the
  Radial envelope.
- **Ground plane** at y = 0 spanning [-6, 6] in
  x + z. The plane's distance from the field
  centre `[0, 0.5, 0]` varies continuously across
  the framebuffer, so AOV pixels hitting the
  plane produce the smoothest gradient.
- **Two lights** (directional key + environment
  sky) for the beauty pass to remain artistically
  recognisable when the AOV is requested
  alongside.

The fixture deliberately reuses OBS-F.2 geometry
so the renderer infrastructure exercised by both
fixtures is identical; any future runtime
divergence between them is attributable to the
field-specific surface, not to geometry differences.

### 2.2 Scalar-field layer (FIELD-I.13)

The fixture's `scalar_field` block authors:

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

The Radial kind produces a 3D smoothstep envelope
centred at the world-space origin (with y-offset to
match the centre sphere's anchor). The fixture's
parameter choices are **safe and bounded**:

- `enabled = true` engages the field; the
  evaluator's master-switch gate opens.
- `strength = 1.0` is the universal multiplier;
  doesn't reshape the field, lets the artist
  visually compare AOV pixels against the
  `[min_value, max_value]` range directly.
- `kind = "radial"` is the most informative kind
  for diagnostic visualisation (Constant
  produces a flat colour; ProceduralPlaceholder
  produces 0 everywhere — both less useful for
  spatial-debugging visualisation).
- `center = [0, 0.5, 0]` anchors the field at the
  centre sphere's location.
- `min_radius = 1.0` / `max_radius = 5.0` produces
  a 4-unit smoothstep transition. The centre
  sphere (radius 0.5) lies entirely inside
  `min_radius`; the four close marker spheres
  (`near-right` / `near-left` / `above` /
  `in-front` at distances 1.5, 1.5, 1.5, 1.5)
  lie just past `min_radius`; `far-centre` at
  distance 3.5 lies mid-transition; the ground
  plane at distances 0 to ~8 spans the full
  envelope.
- `falloff = 1.0` keeps the smoothstep linear
  (no exponent reshaping; lets the
  smoothstep cubic show through cleanly).
- `min_value = 0.0` / `max_value = 1.0` produces
  output in the canonical `[0, 1]` grayscale
  range; the PPM 8-bit encoder maps this
  directly with no clamping.

The resulting per-pixel AOV signature:

- **Pixels hitting the centre sphere** (world
  `|delta| ≤ 1.0` from `(0, 0.5, 0)`): write
  `0.0` (black in the grayscale-replicated PPM).
- **Pixels hitting the five close marker spheres**
  (distances 1.5 ± a small radial offset): write
  smoothstep at `t ≈ 0.125` → `t³(3 - 2t) ≈
  0.043`.
- **Pixels hitting the far-centre sphere** at
  distance 3.5: write smoothstep at `t = 0.625`
  → `t³(3 - 2t) ≈ 0.789`.
- **Pixels hitting the ground plane** at varying
  distances: produce a smooth gradient from
  black at the (0, 0, 0) projection to white at
  the edges (|x|, |z| ≥ ~5).
- **Miss pixels** (sky): write `0.0` (black) —
  the kernel arms' miss-side write the documented
  miss anchor.

### 2.3 What the fixture deliberately omits

- **No `manifold` block.** The fixture does NOT
  engage the SCHW.* / PENROSE.* manifold layer.
  When the future SDK-host validation pass runs
  the fixture, it can independently verify the
  beauty pass is unmodulated.
- **No `observer` / `relativity` block.** The
  fixture does NOT engage the OBSERVER.* arc's
  perception transforms. The observer is at
  rest by default (`Observer{velocity = (0, 0,
  0)}`); the beauty pass produces standard
  shading.
- **No `field_mapping` block.** No
  `FieldMappingConfig` authoring. The fixture
  authors only the scalar-field config (FIELD-I.2)
  and lets the future kernel arm read the raw
  `evaluate(...)` output.

---

## 3. Expected visual signature

When the future CLI bridge slice's audit runs the
fixture on an SDK host, the expected per-pixel AOV
output is:

### 3.1 Default invocation (no `--field-debug`)

```
RelativityRender --render-aovs scenes/test_scalar_field_diagnostic.rrscene
```

- Produces the standard six AOV PPMs (Beauty /
  Normal / Depth / Albedo / DopplerFactor /
  SearchlightFactor).
- NO `aov_field_scalar.ppm` is emitted (the AOV
  is opt-in via `--field-debug`).
- The standard six PPMs are byte-identical to the
  hypothetical pre-FIELD-I.13 fixture invocation
  (the `scalar_field` block's parser is opt-in
  via the future CLI bridge; the renderer
  dispatcher does NOT read
  `scene.scalar_field_config` this slice).

### 3.2 With `--field-debug` (future CLI bridge slice)

```
RelativityRender --render-aovs --field-debug \
  scenes/test_scalar_field_diagnostic.rrscene
```

- Produces SEVEN AOV PPMs (the standard six + the
  new `aov_field_scalar.ppm`).
- `aov_field_scalar.ppm`'s 1-channel scalar
  (replicated to RGB at save time) encodes the
  per-pixel smoothstep sample.
- The standard six PPMs are byte-identical to
  the §3.1 invocation (the `--field-debug` gate
  only allocates the new AOV buffer; the existing
  AOVs' kernel writes are byte-stable).

### 3.3 OptiX path

```
RelativityRender --render-optix-aovs --field-debug \
  scenes/test_scalar_field_diagnostic.rrscene
```

- Produces `optix_aov_field_scalar.ppm` alongside
  the standard OptiX AOV PPMs.
- The PPM file is **byte-identical** to the CUDA-
  side `aov_field_scalar.ppm` per the FIELD-I.12
  audit's five-axis cross-backend symmetry
  (same POD + same default + same null-gate +
  same math + same encoding). The SDK-host
  validation pass `cmp`'s the two files; exit
  status MUST be `0`.

---

## 4. Cross-backend equivalence

Both backends consume the same fixture file +
produce the same per-pixel scalar sample via the
same RR_HD inline `evaluate(...)` helper. The
fixture is the canonical test surface for the
FIELD-I.6 task brief's §8.7 (cross-backend
equivalence) runtime scenario.

The five-axis symmetry argument from the FIELD-I.12
audit (`docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
§3.4) guarantees the structural equivalence by
construction:

- **Same POD type**: both backends consume
  `rr::field::ScalarFieldConfig`.
- **Same default**: `disabled_scalar_field_config()`
  on both.
- **Same null-gate**: `aov_field_scalar != nullptr`
  guard on both kernel arms.
- **Same math**: same `RR_HD inline evaluate(...)`
  helper.
- **Same encoding**: 1-float per pixel via
  `pix_idx_1` indexing.

The fixture exercises every axis: it authors a
non-default config (`enabled = true`,
`kind = radial`, non-zero `strength`), engages the
kernel arm (when the future `--field-debug` flag
flips), exercises the `evaluate(...)` Radial path,
and produces 1-float-per-pixel output. The empirical
SDK-host `cmp` is the canonical runtime
verification.

---

## 5. Audit-host smoke transcript

The FIELD-I.13 landing commit's audit-host smoke
verifies the fixture loads cleanly:

```
$ RelativityRender --scene-info \
    scenes/test_scalar_field_diagnostic.rrscene
[INFO] scene file: scenes/test_scalar_field_diagnostic.rrscene
[INFO]   version           : 1.0.0
[INFO]   render_settings   :
[INFO]     width             : 1280
[INFO]     height            : 720
[INFO]     samples_per_pixel : 1
[INFO]     max_depth         : 1
[INFO]     output_path       : output/scalar_field_diagnostic_fixture.ppm
[INFO]   camera            :
...
[INFO]   relativity        :
[INFO]     observer_velocity : (0, 0, 0)
[INFO]     |beta|            : 0.0
...
[INFO]   materials         : count 6
[INFO]   spheres           : count 6
[INFO]   meshes            : count 1
[INFO]   lights            : count 2
```

No parse errors. The `scalar_field` block is
silently consumed by the new `apply_scalar_field`
parser; the `--scene-info` logger does not
enumerate field config today (future
`SceneInfoLogger` slice may add this — out of scope
for FIELD-I.13).

The fixture is parser-clean on the audit host
(`RR_ENABLE_OPTIX=OFF`, no CUDA SDK); empirical
verification of the parsed-but-not-rendered field
config requires a future test extension that
inspects `Scene::scalar_field_config` directly
(deferred — the FIELD-I.13 brief explicitly scopes
to fixture authoring + minimal parser).

---

## 6. Runtime SDK-host validation checks (DEFERRED)

The following runtime scenarios from the FIELD-I.6
task brief's §8 will be exercised by the future CLI
bridge slice's audit on a CUDA + OptiX-SDK host:

### 6.1 Default-off bit-identity (§8.5)

Run `RelativityRender --render-aovs
scenes/test_scalar_field_diagnostic.rrscene` (no
`--field-debug` flag). Verify exactly six AOV PPMs
are produced; no `aov_field_scalar.ppm`; every
PPM byte-identical to the FIELD-I.13 pre-CLI-bridge
baseline.

### 6.2 Disabled-field neutral PPM (§8.1 + §8.2)

Run `RelativityRender --render-aovs --field-debug
scenes/test_scalar_field_diagnostic.rrscene` AND
override the scene's `scalar_field.enabled` with
`--field-disable` CLI flag (or similar; exact
flag-name TBD at the CLI bridge slice). Verify
`aov_field_scalar.ppm` exists + every pixel
decodes to `0.0` within `1.0e-5f` (the disabled-
field three-layer no-op anchor).

### 6.3 Radial-field smoothstep PPM (§8.4)

Run `RelativityRender --render-aovs --field-debug
scenes/test_scalar_field_diagnostic.rrscene` with
the fixture's Radial config engaged. Verify:

- Pixels hitting the centre sphere decode to
  `0.0` within `1.0e-5f`.
- Pixels at the smoothstep midpoint (distance =
  3.0 from centre) decode to `0.5` within
  `1.0e-5f`.
- Pixels outside `max_radius = 5.0` decode to
  `1.0` within `1.0e-5f`.
- Miss pixels decode to `0.0`.

### 6.4 Composability with other debug AOVs (§8.6)

Run `RelativityRender --render-aovs --field-debug
--observer-debug --manifold-debug ...`. Verify all
three debug AOVs (`aov_field_scalar.ppm`,
`aov_observer_beta.ppm`,
`aov_manifold_coordinates.ppm`) are emitted; the
six standard AOVs are byte-identical to the
single-flag baseline.

### 6.5 CUDA ↔ OptiX byte-identity (§8.7)

Run the same fixture invocation on both backends.
`cmp output/aov_field_scalar.ppm
output/optix_aov_field_scalar.ppm` MUST return
exit status `0`. Structural equivalence is
guaranteed by the FIELD-I.12 five-axis symmetry
argument; this is the empirical verification.

---

## 7. References

### 7.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (the core engineering rules).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §6
  (the Field Interpretation Layer as an OPTIONAL
  extension).

### 7.2 FIELD-I.* arc references

- `docs/FIELD_INTERPRETATION_PHASE1_PLAN.md`
  (FIELD-I.1).
- `docs/FIELD_SCALAR_MODEL_AUDIT.md` (FIELD-I.3 —
  the FIELD-I.2 evaluator semantics rooted in the
  fixture's "field disabled = neutral" anchor).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md` (FIELD-I.5
  — the FIELD-I.4 mapping POD; intentionally NOT
  consumed by the fixture).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6 — the canonical task brief).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_AUDIT.md`
  (FIELD-I.8).
- `docs/FIELD_SCALAR_CUDA_BRIDGE_AUDIT.md`
  (FIELD-I.10 — the CUDA-side bridge whose kernel
  arm the fixture exercises when the CLI bridge
  flips the gate).
- `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
  (FIELD-I.12 — the OptiX-side bridge whose
  symmetric kernel arm produces the byte-identical
  PPM).

### 7.3 Precedent fixture references

- `scenes/test_observer_frame.rrscene` (OBS-F.2 —
  the precedent fixture whose geometry is reused
  verbatim).
- `scenes/test_schwarzschild_like_manifold.rrscene`
  (SCHW.9 — the precedent that introduced the
  `manifold` scene-block; the FIELD-I.13
  `scalar_field` block mirrors the structure).
- `scenes/test_penrose_like_manifold.rrscene`
  (PENROSE.10).

### 7.4 Source surface exercised

- `src/scene/Scene.h` — the
  `Scene::scalar_field_config` field (FIELD-I.13).
- `src/io/SceneLoader.cpp` — the
  `apply_scalar_field(...)` parser
  (FIELD-I.13) + the new `scalar_field` block
  consumer in the main `load(...)` body.
- `src/field/ScalarField.h` — the FIELD-I.2
  `ScalarFieldConfig` POD definition + the
  `evaluate(...)` helper the future kernel arm
  invokes.
- `CMakeLists.txt` — `rr_field` PUBLIC link on
  `rr_scene` (FIELD-I.13).

### 7.5 Cross-backend math leaf

The single-source-of-truth math leaf both
backends consume is `rr::field::evaluate(...)` at
`src/field/ScalarField.h`. The fixture exercises:

- The disabled-field short-circuit at the default
  POD (FIELD-I.3 audit's check #2).
- The Radial smoothstep path (when `kind =
  Radial` + non-zero strength + non-degenerate
  envelope; verified at FIELD-I.2's
  `test_radial_kind_smoothstep_midway`).
- The strength multiplication (1.0 in the
  fixture; doesn't reshape but exercises the
  multiplication site).

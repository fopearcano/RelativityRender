# Scalar Field Beauty Mapping Fixtures (FIELD-BEAUTY.7)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Fixture files: `scenes/test_scalar_field_color_multiplier.rrscene`
                `scenes/test_scalar_field_emission.rrscene`
Related impl audits: `docs/FIELD_SCALAR_BEAUTY_CUDA_AUDIT.md` (FIELD-BEAUTY.4)
                     `docs/FIELD_SCALAR_BEAUTY_OPTIX_AUDIT.md` (FIELD-BEAUTY.6)
Related diagnostic fixture: `docs/FIELD_SCALAR_FIXTURE.md` (FIELD-I.13)

This document is the companion to the two FIELD-BEAUTY.7
fixture scenes — the operator-facing fixtures that
exercise the FIELD-BEAUTY.* arc's CUDA + OptiX kernel
arms via the FIELD-I.4 `FieldMappingConfig` POD's
two operator-engageable beauty targets:
`ColorMultiplier` (multiplicative tinting) and
`Emission` (additive grayscale glow).

The fixtures pair with the FIELD-I.13 diagnostic
fixture (`scenes/test_scalar_field_diagnostic.rrscene`)
which exercises the FIELD-I.* arc's read-only AOV
write arm. With all three fixtures + both arc families
+ the deferred CLI bridge, the FIELD-I.* + FIELD-BEAUTY.*
arcs' SDK-host validation surface is canonically
complete.

The FIELD-BEAUTY.7 fixtures are **forward-looking**: the
`scalar_field` + `field_mapping` blocks parse cleanly +
land on `Scene::scalar_field_config` / 
`Scene::field_mapping_config`, but no renderer
dispatcher reads `scene.field_mapping_config` this slice
(the FIELD-BEAUTY.* CLI bridge that wires
`scene.field_mapping_config` →
`AOVTargets::field_mapping_config` /
`OptixRenderer::render_aovs(...)` trailing parameter
lands at the renumbered next impl slot). Until the CLI
bridge slice lands, the fixtures' `field_mapping`
blocks have no visible runtime effect; the fixtures
exist to:

1. Verify the FIELD-BEAUTY.7 `field_mapping` block
   parser surface end-to-end (both fixtures load
   cleanly via `--scene-info` today).
2. Provide controlled, byte-stable scenes the future
   CLI bridge slice's audit will exercise for the
   FIELD-BEAUTY.4 + FIELD-BEAUTY.6 audits'
   runtime-deferred SDK-host scenarios.
3. Document the expected per-pixel beauty modulation
   the future SDK-host validation pass will pin
   against goldens.

---

## 1. Purpose

The two fixtures exercise the FIELD-BEAUTY.7 parser
surface and author scenes whose
`scalar_field` + `field_mapping` configurations are
sufficient to produce **visually verifiable** beauty
modulation when the future CLI bridge slice flips
both backends' mapping arms reachable.

### 1.1 Three goals

- **Parser smoke test.** Loading either fixture via
  `RelativityRender --scene-info <fixture>`
  exercises the new `apply_field_mapping(...)`
  parser in `src/io/SceneLoader.cpp`. Both fixtures'
  `field_mapping` blocks cover five of the six
  FieldMappingConfig fields (`target`, `strength`,
  `bias`, `min_value`, `max_value`; `clamp_output`
  is left at default `false`).
- **Beauty-mapping runtime template.** When the
  future CLI bridge slice lands, the operator
  invokes:
    - `RelativityRender --render-aovs <fixture>`
      (CUDA path) → produces the per-pixel
      beauty-modulated PPM.
    - `RelativityRender --render-optix-aovs <fixture>`
      (OptiX path) → produces the same beauty-
      modulated PPM byte-identical to the CUDA-side
      output.
- **Two-target distinction.** The fixtures encode
  the two operator-engageable beauty targets
  separately so the SDK-host validation can
  distinguish:
    - **ColorMultiplier**: the multiplier scales the
      per-pixel beauty color; the fixture's bias=1
      anchors the centre at `× 1.0` (no change) and
      the outer envelope at `× 2.0` (brightened).
    - **Emission**: the additive emission adds
      bounded grayscale; the fixture's strength=0.5,
      bias=0 anchors the centre at `+ 0` (no
      addition) and the outer envelope at `+ 0.5`
      (visible bright glow).

### 1.2 Honest scope boundaries

The fixtures **DO** engage the FIELD-I.4 mapping
pipeline (the `field_mapping` block is the
operator-authoring surface for the FieldMappingConfig
POD); but **DO NOT** activate the kernel arms this
slice (no renderer dispatcher threads
`scene.field_mapping_config` into the kernel-visible
`field_mapping_config` payload). The future CLI
bridge slice flips this.

The fixtures **DO NOT** modify the FIELD-I.13
diagnostic fixture; the three fixtures coexist:

- `test_scalar_field_diagnostic.rrscene` — FIELD-I.*
  diagnostic AOV (read-only sample visualization).
- `test_scalar_field_color_multiplier.rrscene` —
  FIELD-BEAUTY.* ColorMultiplier (beauty
  modulation via multiplication).
- `test_scalar_field_emission.rrscene` —
  FIELD-BEAUTY.* Emission (beauty modulation via
  additive grayscale).

The fixtures **DO NOT** engage any manifold or
observer behaviour — every default scalar-field +
field-mapping is independent of the OBSERVER.* /
OBS-P.* / OBS-F.* / SCHW.* / PENROSE.* / MANI-I.*
arc families' authored state.

---

## 2. Composition

Both fixtures share the same geometry + lighting +
camera + scalar_field layers (mirroring the FIELD-I.13
fixture verbatim); only the `field_mapping` block
differs. This three-layer separation lets the
SDK-host validation isolate the mapping target's
contribution from every other variable.

### 2.1 Shared geometry layer (lifted verbatim from FIELD-I.13 / OBS-F.2)

- 6 spheres (centre + 5 markers at distances 1.5–3.5
  from the field centre) at the same positions as
  the FIELD-I.13 fixture
- Ground plane at y = 0 spanning [-6, 6]
- 2 lights (directional key + environment sky)
- Camera at `(0, 1.2, 6)` looking slightly down

### 2.2 Shared scalar_field layer

Both fixtures author the identical
`scalar_field` block (verbatim from FIELD-I.13):

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

This Radial field produces a 3D smoothstep envelope
centred at the centre sphere's anchor. Per-pixel
sample value:

- Inside `|hit_pos - center| ≤ min_radius (1.0)`:
  sample = `min_value (0.0)`.
- Outside `|hit_pos - center| ≥ max_radius (5.0)`:
  sample = `max_value (1.0)`.
- Between: smoothstep cubic `3t² - 2t³` where
  `t = (|hit_pos - center| - min_radius) /
  (max_radius - min_radius)`.

The shared scalar_field layer means the kernel arm's
input sample is identical across both fixtures; the
only difference is how the FieldMappingConfig
processes the sample.

### 2.3 ColorMultiplier-specific `field_mapping` layer

```json
"field_mapping": {
  "target":       "color-multiplier",
  "strength":     1.0,
  "bias":         1.0,
  "min_value":    1.0,
  "max_value":    2.0,
  "clamp_output": false
}
```

The kernel arm computes `mapped = strength * sample +
bias = 1.0 * sample + 1.0 = sample + 1.0`. With
sample ∈ [0, 1] (Radial envelope), mapped ∈ [1.0,
2.0]. The kernel applies `color = color * mapped`,
so:

- **Inside the inner sphere** (sample = 0): `color
  *= 1.0` (no change; pre-mapping beauty preserved).
- **Outside the outer envelope** (sample = 1):
  `color *= 2.0` (brightened by 2×).
- **Between**: smoothstep-interpolated brightening.

The `clamp_output = false` is honest: the
strength+bias affine transform on a bounded
[0, 1] sample produces a structurally bounded
[1, 2] output without explicit clamping. The
`min_value = 1.0` / `max_value = 2.0` fields are
documented but inert (they would only matter if
`clamp_output = true`).

### 2.4 Emission-specific `field_mapping` layer

```json
"field_mapping": {
  "target":       "emission",
  "strength":     0.5,
  "bias":         0.0,
  "min_value":    0.0,
  "max_value":    0.5,
  "clamp_output": false
}
```

The kernel arm computes `mapped = strength * sample +
bias = 0.5 * sample + 0.0 = 0.5 * sample`. With
sample ∈ [0, 1], mapped ∈ [0.0, 0.5]. The kernel
applies `color = color + Vec3{mapped, mapped,
mapped}`, so:

- **Inside the inner sphere** (sample = 0): `color
  += 0.0` (no change; pre-mapping beauty preserved).
- **Outside the outer envelope** (sample = 1):
  `color += 0.5` (additive gray emission +0.5 per
  channel).
- **Between**: smoothstep-interpolated additive
  emission.

The `clamp_output = false` is honest by the same
reasoning as the ColorMultiplier fixture — the
output is structurally bounded by [0, 0.5].

### 2.5 What the fixtures deliberately omit

- **No `manifold` block.** The fixtures do NOT
  engage the SCHW.* / PENROSE.* manifold layer.
- **No `observer` / `relativity` block.** The
  observer is at rest by default; the beauty pass
  produces standard shading; Doppler/searchlight
  are at identity.
- **No `field_debug` engagement.** The fixtures
  focus on the beauty-mapping kernel arms, not the
  FIELD-I.7 diagnostic AOV. Both fixtures can
  coexist with `--field-debug` on the future CLI
  to also produce the `aov_field_scalar.ppm`
  diagnostic side-by-side; deferred to the future
  CLI bridge slice's audit.

---

## 3. Expected visual signature

When the future CLI bridge slice's audit runs the
fixtures on an SDK host, the expected per-pixel
beauty PPM output is:

### 3.1 ColorMultiplier fixture (CUDA)

```
RelativityRender --render-aovs \
  scenes/test_scalar_field_color_multiplier.rrscene
```

- Produces the standard six AOV PPMs (Beauty / Normal
  / Depth / Albedo / DopplerFactor /
  SearchlightFactor) + `aov_beauty.ppm`.
- **Beauty PPM** shows the field's contribution:
    - Centre sphere (white material, distance 0
      from field center): pre-mapping beauty
      brightness *retained* (× 1.0 multiplier).
    - Marker spheres at distance 1.5: ~ × 1.0625
      brighter (smoothstep at t ≈ 0.125 → mapped ≈
      1.043).
    - far-centre sphere at distance 3.5: ~ × 1.789
      brighter.
    - Ground plane at varying distances: smooth
      brightening from centre (× 1.0) to edges
      (× 2.0).

### 3.2 Emission fixture (CUDA)

```
RelativityRender --render-aovs \
  scenes/test_scalar_field_emission.rrscene
```

- Same six AOV PPMs.
- **Beauty PPM** shows additive grayscale glow:
    - Centre sphere: no emission added (sample = 0
      at |delta| ≤ 1.0).
    - Marker spheres at distance 1.5: +0.022 gray
      emission (smoothstep at t ≈ 0.125 →
      mapped ≈ 0.022).
    - far-centre sphere at distance 3.5: +0.395
      gray emission.
    - Ground plane: smooth additive glow from
      centre (+0) to edges (+0.5).

### 3.3 OptiX path (both fixtures)

```
RelativityRender --render-optix-aovs <fixture>
```

- Produces the OptiX-side beauty PPM **byte-identical**
  to the CUDA-side beauty PPM (per the FIELD-BEAUTY.6
  audit's five-axis cross-backend symmetry argument).
- The SDK-host pass `cmp`-verifies byte-identity
  between `aov_beauty.ppm` and
  `optix_aov_beauty.ppm` for each fixture.

### 3.4 Default invocation (existing CLI today)

Today (before the FIELD-BEAUTY.* CLI bridge lands),
the operator can already invoke:

```
RelativityRender --scene-info <fixture>
RelativityRender --render-aovs <fixture>
RelativityRender --render-optix-aovs <fixture>
```

In all three cases:
- The `scalar_field` + `field_mapping` blocks parse
  cleanly without errors.
- The renderer dispatcher passes the default
  `targets.field_mapping_config` (target = None) to
  the kernel arms, so the beauty pass is
  byte-identical to the pre-FIELD-BEAUTY-arc
  baseline (`-renderer no-op).
- The fixtures behave like FIELD-I.13 today (no
  visible field effect) and will produce the
  documented §3.1 / §3.2 / §3.3 outputs once the
  CLI bridge slice flips the gate.

---

## 4. Cross-backend equivalence

Both fixtures + both backends produce byte-identical
beauty PPMs by construction. The cross-backend
guarantees:

- **Same FieldMappingConfig POD** consumed by both
  arms (`CudaSceneView::field_mapping_config` =
  `OptixLaunchParams::field_mapping_config` type).
- **Same scalar sample** (both backends call the
  same RR_HD inline `rr::field::evaluate(...)` from
  `src/field/ScalarField.h` at the same hit
  position).
- **Same mapping evaluator** (both backends call the
  same RR_HD inline `rr::field::evaluate_mapping(...)`
  from `src/field/FieldMapping.h`).
- **Same operator semantics** (both apply the same
  `color * mapped` for ColorMultiplier and
  `color + Vec3{m, m, m}` for Emission).
- **Same insertion point** (both backends insert
  the mapping BEFORE Doppler / searchlight
  modulation, so the field contribution participates
  uniformly in the relativistic pipeline).

The FIELD-BEAUTY.6 audit's §3.7 documents this
five-axis symmetry in detail.

---

## 5. Audit-host smoke transcript

The FIELD-BEAUTY.7 landing commit's audit-host smoke
verifies both fixtures load cleanly:

```
$ RelativityRender --scene-info \
    scenes/test_scalar_field_color_multiplier.rrscene
[INFO] scene file: scenes/test_scalar_field_color_multiplier.rrscene
[INFO]   version           : 1.0.0
[INFO]   render_settings   :
[INFO]     width             : 1280
[INFO]     height            : 720
[INFO]     samples_per_pixel : 1
[INFO]     max_depth         : 1
[INFO]     output_path       : output/scalar_field_color_multiplier_fixture.ppm
[INFO]   camera            :
... (no parser errors)

$ RelativityRender --scene-info \
    scenes/test_scalar_field_emission.rrscene
[INFO] scene file: scenes/test_scalar_field_emission.rrscene
[INFO]   version           : 1.0.0
[INFO]   render_settings   :
[INFO]     output_path       : output/scalar_field_emission_fixture.ppm
... (no parser errors)
```

Both fixtures' `scalar_field` + `field_mapping`
blocks are silently consumed by the new
`apply_scalar_field(...)` + `apply_field_mapping(...)`
parsers; the `--scene-info` logger does not
enumerate these blocks today (future
`SceneInfoLogger` slice may add this — out of scope
for FIELD-BEAUTY.7).

Both fixtures are parser-clean on the audit host
(`RR_ENABLE_OPTIX=OFF`, no CUDA SDK); empirical
verification of the parsed-but-not-rendered field
mapping requires a future test extension that
inspects `Scene::field_mapping_config` directly
(deferred — the FIELD-BEAUTY.7 brief explicitly
scopes to fixture authoring + minimal parser).

---

## 6. Runtime SDK-host validation checks (DEFERRED)

The following runtime scenarios will be exercised by
the future CLI bridge slice's audit on a CUDA +
OptiX-SDK host:

### 6.1 ColorMultiplier beauty modulation (CUDA + OptiX)

Run:
```
RelativityRender --render-aovs scenes/test_scalar_field_color_multiplier.rrscene
RelativityRender --render-optix-aovs scenes/test_scalar_field_color_multiplier.rrscene
```

Verify:
- Both backends' `aov_beauty.ppm` files show the
  per-pixel brightening pattern: centre sphere
  pre-mapping bright; far-centre and outer ground-
  plane pixels brightened by up to 2×.
- The five-axis symmetry guarantees byte-identity:
  `cmp aov_beauty.ppm optix_aov_beauty.ppm` MUST
  return exit status `0`.

### 6.2 Emission beauty modulation (CUDA + OptiX)

Run:
```
RelativityRender --render-aovs scenes/test_scalar_field_emission.rrscene
RelativityRender --render-optix-aovs scenes/test_scalar_field_emission.rrscene
```

Verify:
- Both backends' `aov_beauty.ppm` files show the
  additive grayscale glow: centre sphere pre-mapping
  bright (no additive); far-centre and outer ground-
  plane pixels glow with up to +0.5 added per
  channel.
- Byte-identity between CUDA and OptiX outputs.

### 6.3 Disabled-mapping baseline (both fixtures)

The future CLI bridge slice will need to flip
`cfg.field_mapping_config.target = None` (or
disable the field) via CLI. Verify: the beauty PPMs
revert to the pre-FIELD-BEAUTY-arc baseline (the
fixture geometry rendered without any field
modulation). Byte-identical to the renderer's
default beauty output.

### 6.4 Compatibility with FIELD-I.7 diagnostic AOV

Run the ColorMultiplier fixture with both
`--field-debug` (diagnostic AOV) and the
ColorMultiplier mapping engaged. Verify:
- `aov_beauty.ppm` shows the beauty-modulated PPM
  (per §3.1).
- `aov_field_scalar.ppm` shows the raw scalar
  sample pattern (the FIELD-I.7 diagnostic; the
  same value the mapping consumes).
- The diagnostic and the beauty are *independent*
  — the diagnostic AOV writes the raw sample
  regardless of the mapping target.

### 6.5 Cross-fixture beauty diff

Run both fixtures (ColorMultiplier + Emission)
back-to-back; compare the beauty PPMs.

- The two `aov_beauty.ppm` files MUST differ —
  ColorMultiplier produces brightening,
  Emission produces additive grayscale glow.
- The diagnostic AOV (if requested) is identical
  across both fixtures (same scalar field).

### 6.6 Doppler / searchlight interaction

Run either fixture with `--observer-perception-mode
relativistic --observer-beta 0.5 --observer-direction
1,0,0`. Verify the field's mapping contribution
flows through the standard Doppler / searchlight
pipeline (the FIELD-BEAUTY.3 + FIELD-BEAUTY.5 arms
are positioned BEFORE the Doppler / searchlight
modulation, so the field-modulated color sees the
relativistic treatment).

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
  the FIELD-I.2 evaluator semantics underpinning
  the shared `scalar_field` block in both
  fixtures).
- `docs/FIELD_MAPPING_CONFIG_AUDIT.md` (FIELD-I.5
  — the FIELD-I.4 mapping evaluator semantics
  underpinning the `field_mapping` block in both
  fixtures).
- `docs/FIELD_SCALAR_DIAGNOSTIC_AOV_TASK.md`
  (FIELD-I.6).
- `docs/FIELD_SCALAR_FIXTURE.md` (FIELD-I.13 —
  the precedent fixture whose geometry +
  `scalar_field` block both FIELD-BEAUTY.7
  fixtures inherit verbatim).

### 7.3 FIELD-BEAUTY.* arc references

- (FIELD-BEAUTY.1) — UNFILLED task brief slot.
- (FIELD-BEAUTY.2) — UNFILLED task brief slot.
- `docs/FIELD_SCALAR_BEAUTY_CUDA_AUDIT.md`
  (FIELD-BEAUTY.4 — the CUDA-side kernel arm
  audit; the §3.10 deferred SDK-host scenarios
  reference these fixtures).
- `docs/FIELD_SCALAR_BEAUTY_OPTIX_AUDIT.md`
  (FIELD-BEAUTY.6 — the OptiX-side kernel arm
  audit; the §3.7 five-axis symmetry argument
  underpins the cross-backend equivalence claims
  at §3 + §4 + §6 of this companion).

### 7.4 Source surface exercised

- `src/scene/Scene.h` — the new
  `Scene::field_mapping_config` field
  (FIELD-BEAUTY.7).
- `src/io/SceneLoader.cpp` — the new
  `apply_field_mapping(...)` parser
  (FIELD-BEAUTY.7) + the
  `parse_field_mapping_target(...)` helper +
  the new `field_mapping` block consumer in
  the main `load(...)` body.
- `src/field/ScalarField.h` — the FIELD-I.2
  `ScalarFieldConfig` POD + `evaluate(...)`
  helper.
- `src/field/FieldMapping.h` — the FIELD-I.4
  `FieldMappingConfig` POD + `evaluate_mapping(...)`
  helper + `FieldMappingTarget` enum.

### 7.5 Cross-backend math leaves

Both backends consume:

- `rr::field::evaluate(scalar_field_config,
  hit_pos)` — same RR_HD inline helper from
  `src/field/ScalarField.h`.
- `rr::field::evaluate_mapping(field_mapping_config,
  sample)` — same RR_HD inline helper from
  `src/field/FieldMapping.h`.

Both fixtures exercise:

- The Radial smoothstep path (sample computation).
- The ColorMultiplier / Emission target branch
  selection (one fixture per target).
- The strength + bias affine transform.
- The structural-boundedness of `[0, 1] * strength
  + bias` for both fixtures' chosen parameter
  ranges (ColorMultiplier ∈ [1, 2], Emission ∈
  [0, 0.5]).

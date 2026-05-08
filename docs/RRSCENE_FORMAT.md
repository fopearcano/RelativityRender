# `.rrscene` File Format — v1.0

Date: 2026-04-29
Branch: `relativity-core-v1`
Status: **specification only** — no parser is implemented in this
slice. This doc is the contract the master-module-15 parser slice
will implement against.

---

## 1. Overview

`.rrscene` is the on-disk format for a complete RelativityRender
scene. One file holds the full set of inputs the renderer needs to
produce a frame:

- render settings (resolution, sample / bounce counts)
- camera
- relativistic observer state
- materials
- sphere primitives
- triangle meshes
- lights

The file is a single UTF-8-encoded JSON object. The on-disk
extension is `.rrscene`; the MIME type is
`application/x-rrscene+json`. Editors / tools that want
JSON-compatible tooling should also accept the `.rrjson` extension
(same content).

Field naming is **snake_case throughout**. The C++ types the parser
populates have a few camelCase exceptions (`baseColor`,
`emissionColor`, `emissionStrength`); the parser converts JSON
`base_color` → C++ `baseColor` etc. so the file format stays
consistent for the user.

Vectors are JSON arrays:

- `Vec3`        → `[x, y, z]` (length 3)
- `Vec2`        → `[u, v]` (length 2)
- triangle      → `[v0, v1, v2]` (length 3 of `uint32_t`)

Colours are **linear-space floats**, conventionally in `[0, 1]` but
HDR values `> 1` are valid (emission, environment intensity).

Coordinate convention: right-handed, +Y up, +X right, looking down
−Z by default. Distances are scene units (no implicit metres).

## 2. Versioning

Every file MUST carry a top-level `version` field. The current
schema is `"1.0.0"`.

```json
{ "version": "1.0.0", ... }
```

Versioning policy (compatible with semantic versioning):

- The parser MUST require a matching **major** version. v1 parsers
  reject v2 files; v2 files MUST therefore not load in v1 parsers.
- Minor and patch increments are backwards-compatible additions
  (new optional fields, new light types). A v1.0 parser MUST
  ignore unknown optional fields and load a v1.x file silently.
- The parser MAY warn on unknown unrecognised top-level objects.

## 3. Top-level structure

```json
{
  "version": "1.0.0",
  "render_settings": { ... },
  "camera":          { ... },
  "relativity":      { ... },
  "materials":       [ ... ],
  "spheres":         [ ... ],
  "meshes":          [ ... ],
  "lights":          [ ... ]
}
```

| Key               | Type    | Required? | Default                            |
|-------------------|---------|:---------:|------------------------------------|
| `version`         | string  | **yes**   | —                                  |
| `render_settings` | object  | no        | `RenderSettings{}` defaults        |
| `camera`          | object  | no        | default `Camera{}`                 |
| `relativity`      | object  | no        | β=0, all `enable_*=true`           |
| `materials`       | array   | no        | `[]`                               |
| `spheres`         | array   | no        | `[]`                               |
| `meshes`          | array   | no        | `[]`                               |
| `lights`          | array   | no        | `[]`                               |

The minimal valid v1 file is therefore:

```json
{ "version": "1.0.0" }
```

which loads as a default scene with the default camera, no
geometry, no materials, no lights. The renderer outputs the sky
gradient.

## 4. `render_settings`

Maps to `rr::scene::RenderSettings`.

```json
"render_settings": {
  "width":             1280,
  "height":            720,
  "samples_per_pixel": 1,
  "max_depth":         1,
  "output_path":       ""
}
```

| Key                | Type   | Required? | Default | Validation                    |
|--------------------|--------|:---------:|--------:|-------------------------------|
| `width`            | int    | no        |    1280 | `> 0`                         |
| `height`           | int    | no        |     720 | `> 0`                         |
| `samples_per_pixel`| int    | no        |       1 | `>= 1` (path tracer reads it) |
| `max_depth`        | int    | no        |       1 | `>= 1` (path tracer reads it) |
| `output_path`      | string | no        |    `""` | UTF-8; empty == no default    |

`samples_per_pixel` and `max_depth` are stored faithfully but are
not consumed by the Stage 9B kernel; the path tracer (master
module 16) reads them.

`output_path` is informational metadata authored into the file: an
optional default destination for the rendered image. The CLI's
`--output` flag (or each render command's hard-coded default)
takes precedence; tools that want to honour the authored path
should consult `RenderSettings::output_path` and apply it when no
explicit `--output` is passed.

### 4.1 Authoring shorthands (Stage 10B.2)

The parser accepts these shorthand keys for convenience and
treats them as exact synonyms for the canonical names above:

| Canonical name      | Accepted shorthand | Notes                               |
|---------------------|--------------------|-------------------------------------|
| `render_settings`   | `render`           | Top-level section name              |
| `samples_per_pixel` | `samples`          | Inside the section object           |
| `output_path`       | `output`           | Inside the section object           |

Files SHOULD use the canonical names; tools that emit `.rrscene`
files (the writer in `SceneWriter.cpp`) MUST emit the canonical
forms only. Shorthands exist to make hand-authored files less
verbose; they are not part of the wire format guarantees.

## 5. `camera`

Maps to `rr::camera::Camera`. Specified by an eye / target / up
triple plus optical knobs; the parser feeds these into
`Camera::look_at` + `set_vertical_fov_degrees` +
`set_clip_range` + `set_aspect`.

```json
"camera": {
  "position":    [0.0, 0.0, 0.0],
  "target":      [0.0, 0.0, -1.0],
  "up":          [0.0, 1.0, 0.0],
  "fov_degrees": 45.0,
  "near":        0.1,
  "far":         1000.0
}
```

| Key           | Type        | Required? | Default                    | Validation                                            |
|---------------|-------------|:---------:|----------------------------|-------------------------------------------------------|
| `position`    | Vec3        | no        | `[0, 0, 0]`                | finite                                                |
| `target`      | Vec3        | no        | `[0, 0, -1]`               | finite; `target != position` (else basis untouched)   |
| `up`          | Vec3        | no        | `[0, 1, 0]`                | finite; ideally non-parallel to `target − position`   |
| `fov_degrees` | float       | no        | `45.0`                     | `0.01 ≤ fov < 180`                                    |
| `near`        | float       | no        | `0.1`                      | `near > 0`                                            |
| `far`         | float       | no        | `1000.0`                   | `far > near`                                          |

`aspect` is **not** in the file because it is a function of the
output framebuffer (`width / height`) — the parser sets
`Camera::set_aspect(width / height)` from `render_settings`. Files
that override aspect would do so by changing resolution.

### 5.1 Authoring shorthands (Stage 10B.3)

Like `render_settings`, the `camera` block accepts a few
authoring shorthands as exact synonyms for the canonical names:

| Canonical name | Accepted shorthand | Notes                                        |
|----------------|--------------------|----------------------------------------------|
| `target`       | `forward`          | Direction vector instead of look-at point    |
| `fov_degrees`  | `fovDegrees`       | camelCase variant for hand-authoring         |

`forward` and `target` express the camera's orientation in two
different idioms: `target` (canonical) is a world-space point the
camera looks at; `forward` is a direction vector relative to
`position`. When both are present `forward` wins. When `forward`
is given, the parser computes `target = position + forward` and
hands the pair to `Camera::look_at`; the magnitude of `forward`
does not matter because `look_at` normalises internally. A
zero-length `forward` is rejected.

Tools that emit `.rrscene` files MUST emit the canonical names
only; shorthands exist to make hand-authored files less verbose
and are not part of the wire format guarantees.

## 6. `relativity`

Maps to `rr::relativity::Observer` (kinematic state) plus
`rr::relativity::RelativityParams` (artist toggles). The two PODs
are merged into a single JSON object for authoring convenience.

```json
"relativity": {
  "observer_velocity":      [0.0, 0.0, 0.0],
  "enable_aberration":      true,
  "enable_doppler":         true,
  "enable_searchlight":     true,
  "doppler_color_strength": 1.0,
  "searchlight_strength":   1.0,
  "max_beta":               0.999999
}
```

| Key                       | Type  | Required? | Default      | Validation                                  |
|---------------------------|-------|:---------:|-------------:|---------------------------------------------|
| `observer_velocity`       | Vec3  | no        | `[0, 0, 0]`  | `length(v) < max_beta` (and `< 1`)          |
| `enable_aberration`       | bool  | no        | `true`       |                                             |
| `enable_doppler`          | bool  | no        | `true`       |                                             |
| `enable_searchlight`      | bool  | no        | `true`       |                                             |
| `doppler_color_strength`  | float | no        | `1.0`        | `>= 0`                                      |
| `searchlight_strength`    | float | no        | `1.0`        | `>= 0`                                      |
| `max_beta`                | float | no        | `0.999999`   | `0 < max_beta < 1`                          |

The kernel's `clampBeta` helper enforces the last invariant at
runtime, but the parser MUST reject files where
`length(observer_velocity) >= 1`. That value is not physical and
indicates an authoring or unit error.

### 6.1 Authoring shorthands (Stage 10B.4)

Like §4.1 / §5.1, the `relativity` block accepts a few authoring
shorthands as exact synonyms (or composable equivalents) for the
canonical fields:

| Canonical                     | Accepted shorthand                        |
|-------------------------------|-------------------------------------------|
| `observer_velocity` (Vec3)    | `betaVelocity` (float, scalar speed) **+** `velocityDirection` (Vec3, direction) |
| `enable_aberration` (bool)    | `aberrationStrength` (float, `0` ⇒ off, `> 0` ⇒ on) |
| `doppler_color_strength`      | `dopplerStrength`                         |
| `searchlight_strength`        | `searchlightStrength`                     |
| (no canonical equivalent)     | `enabled` (master gate; `false` zeroes all three `enable_*` flags) |

Shorthands win when both forms are present, matching the §5.1
precedence policy. Notes:

- `betaVelocity` + `velocityDirection` are **paired**: authoring
  one without the other is an error. The parser computes
  `observer_velocity = normalize(velocityDirection) *
  betaVelocity`. A zero-length `velocityDirection` is rejected.
- `aberrationStrength` collapses onto the `enable_aberration`
  boolean today because the host `RelativityParams` has no float
  aberration-strength field; the kernel only reads the bool.
  Files that need a fractional aberration response should depend
  on a future schema version.
- `dopplerStrength` and `searchlightStrength` are exact synonyms
  for the canonical `*_strength` fields; no information is lost.
- `enabled` is a one-way master gate: `false` forces all three
  per-effect flags off; `true` (the default) leaves them at
  whatever the canonical / shorthand inputs set. There is no
  canonical equivalent because the per-effect flags already
  encode three independent gates.

The cross-section validation rule from §12 #2
(`length(observer_velocity) < max_beta < 1`) is enforced after
all shorthands have been resolved into canonical fields.

Tools that emit `.rrscene` files MUST emit canonical names only.

## 7. `materials`

Each entry maps to `rr::scene::SceneMaterial { id, name, params }`,
where `params` is an `rr::material::MaterialParams`.

```json
"materials": [
  {
    "id":                 0,
    "name":               "red diffuse",
    "base_color":         [0.85, 0.20, 0.20],
    "emission_color":     [0.0, 0.0, 0.0],
    "emission_strength":  0.0,
    "roughness":          0.5,
    "metallic":           0.0,
    "specular":           0.5,
    "transmission":       0.0
  }
]
```

| Key                       | Type   | Required? | Default                  | Validation                  |
|---------------------------|--------|:---------:|--------------------------|-----------------------------|
| `id`                      | int    | **yes**   | —                        | `>= 0`; unique within array |
| `name`                    | string | no        | `""`                     | UTF-8                       |
| `base_color`              | Vec3   | no        | `[0.8, 0.8, 0.8]`        | each component `>= 0`       |
| `emission_color`          | Vec3   | no        | `[0, 0, 0]`              | each component `>= 0`       |
| `emission_strength`       | float  | no        | `0.0`                    | `>= 0`                      |
| `roughness`               | float  | no        | `0.5`                    | in `[0, 1]`                 |
| `metallic`                | float  | no        | `0.0`                    | in `[0, 1]`                 |
| `specular`                | float  | no        | `0.5`                    | in `[0, 1]`                 |
| `transmission`            | float  | no        | `0.0`                    | in `[0, 1]` (PLACEHOLDER)   |
| `use_base_color_texture`  | bool   | no        | `false`                  | TEX-P.6                     |
| `base_color_texture_id`   | int    | no        | `-1`                     | any int; `-1` = no texture; |
|                           |        |           |                          | range vs `textures` array   |
|                           |        |           |                          | is checked at scene-build   |
|                           |        |           |                          | time by                     |
|                           |        |           |                          | `validate_material_texture_ids` |
|                           |        |           |                          | (TEX-P.5)                   |

**Index convention**: `id` is the lookup key. The parser MAY also
treat the array's positional index as the material's stable id when
all `id` fields are absent; the canonical form is to set both and
use the array position. Sphere `material_index` and Mesh
`material_id` reference the array position (`materials[i]`), not
the `id`.

`transmission` is reserved for the future glass / refraction BSDF;
the kernel does not read it today.

### 7.1 Authoring shorthands (Stage 10B.5)

The three compound names accept the C++-style camelCase form as
exact synonyms for the canonical snake_case (per §1's general
rule that "the parser converts JSON `base_color` → C++
`baseColor`"). When both are present in the same entry, the
result is the same; the parser still does not allow conflicting
duplicates of the same logical field within one material.

| Canonical (snake_case)    | Accepted shorthand (camelCase) |
|---------------------------|--------------------------------|
| `base_color`              | `baseColor`                    |
| `emission_color`          | `emissionColor`                |
| `emission_strength`       | `emissionStrength`             |
| `use_base_color_texture`  | `useBaseColorTexture`          |
| `base_color_texture_id`   | `baseColorTextureId`           |

The single-word fields (`id`, `name`, `roughness`, `metallic`,
`specular`, `transmission`) have no shorthand; their canonical
spelling is the only form.

Stage 10B.5 status notes:

- The Stage 10B.5 parser implements every field listed in §7
  except `transmission`. That placeholder stays at its
  `MaterialParams` default (`0.0`) until the BSDF stage that
  consumes it ships. Until then a `transmission` value
  authored in the file is parsed by the JSON layer but never
  consulted by the schema mapper - this is a v1.0 parser
  rounding out incrementally, not §14 forward compatibility.

TEX-P.6 status notes:

- The `use_base_color_texture` / `base_color_texture_id` pair
  ships in TEX-P.6 to give scene fixtures a stable handle on
  the texture-binding contract documented in
  `docs/TEXTURE_SYSTEM.md` §2 (the three flag/id cases). The
  loader does NOT yet load texture pixel data — the v1.0.0
  `textures` top-level key is reserved for a future slice. As
  a consequence, every scene loaded today has an empty
  `textures` array; the host-side validator
  `validate_material_texture_ids` therefore treats every
  authored `use_base_color_texture = true` material as a
  Case 3 fixup (warning + flag cleared) and every
  authored `base_color_texture_id` with `use_base_color_texture
  = false` as a Case 1 audit (info log; state preserved).
  The `--scene-info` dispatcher runs the validator after
  loading so the cases fire visibly.
- A future slice will load inline texture pixel data, after
  which Case 2 (flag ON, id in range) will start passing the
  validator silently.

Tools that emit `.rrscene` files MUST emit canonical snake_case.

## 8. `spheres`

Each entry maps to `rr::scene::SceneSphere { object, geometry }`.

```json
"spheres": [
  {
    "name":           "left",
    "visible":        true,
    "transform":      { "position": [0,0,0], "rotation_radians": [0,0,0], "scale": [1,1,1] },
    "center":         [-1.5, 0.2, -4.0],
    "radius":         0.7,
    "material_index": 0
  }
]
```

| Key              | Type       | Required? | Default                        | Validation                                                        |
|------------------|------------|:---------:|--------------------------------|-------------------------------------------------------------------|
| `name`           | string     | no        | `""`                           | UTF-8                                                             |
| `visible`        | bool       | no        | `true`                         |                                                                   |
| `transform`      | Transform  | no        | identity                       | see §11                                                           |
| `center`         | Vec3       | **yes**   | —                              | finite                                                            |
| `radius`         | float      | **yes**   | —                              | `> 0`                                                             |
| `material_index` | int        | no        | `-1`                           | `-1` or in `[0, materials.size())`                                |

Stage 9B note: the `transform` on a sphere is currently not applied
to `center`; the kernel reads `center` as world-space. The
authoring `transform` is preserved for forward compatibility with
the upcoming relative-transform scene editor.

### 8.1 Authoring shorthands (Stage 10B.6)

| Canonical (snake_case) | Accepted shorthand (camelCase) |
|------------------------|--------------------------------|
| `material_index`       | `materialId`                   |

`materialId` is an exact synonym; the parser stores the value into
`Sphere::material_index` either way. Spheres parsed from a file
that omits the reference get the default `-1` (renderer falls back
to neutral defaults).

Stage 10B.6 status notes:

- The Stage 10B.6 parser implements `name`, `center`, `radius`,
  and the material reference. `visible` and `transform` (also §8
  v1.0 fields) are parsed by the JSON layer but not yet
  consulted by the schema mapper; both stay at their
  `SceneObject` defaults (`visible = true`, identity transform).
  This is partial v1.0 implementation, rounded out in a
  follow-up sub-stage.
- Cross-section validation is enforced where the in-scope
  fields supply the inputs: §12 #9 (`radius > 0`) and §12 #4
  (`material_index` is `-1` or in `[0, materials.size())`); the
  v1 parser implementation chooses "reject file" rather than
  "reject sphere, load rest, warn" so authoring mistakes
  surface immediately at the CLI rather than silently swapping
  in the neutral default.

Tools that emit `.rrscene` files MUST emit canonical snake_case.

## 9. `meshes`

Each entry maps to `rr::scene::SceneMesh` (currently a placeholder
shell carrying `SceneObject + source_path`; the parser also
populates an `rr::geometry::Mesh` on the side that is uploaded
through `GpuScene::upload_mesh`).

The format supports two ways to populate vertex data:

- Inline `vertices` + `triangles` arrays (canonical for v1).
- A `source_path` reference to an external asset (reserved; v1
  parsers MUST treat `source_path` as informational metadata and
  leave the mesh empty unless inline arrays are also present).

```json
"meshes": [
  {
    "name":        "quad",
    "visible":     true,
    "transform":   { "position": [0,0,0], "rotation_radians": [0,0,0], "scale": [1,1,1] },
    "source_path": "",
    "material_id": 4,
    "vertices": [
      { "position": [-3.0, -3.0, -6.0], "normal": [0, 0, 1], "uv": [0, 0] },
      { "position": [ 3.0, -3.0, -6.0], "normal": [0, 0, 1], "uv": [1, 0] },
      { "position": [ 3.0,  3.0, -6.0], "normal": [0, 0, 1], "uv": [1, 1] },
      { "position": [-3.0,  3.0, -6.0], "normal": [0, 0, 1], "uv": [0, 1] }
    ],
    "triangles": [
      [0, 1, 2],
      [0, 2, 3]
    ]
  }
]
```

### 9.1 Mesh entry

| Key            | Type      | Required? | Default                | Validation                                       |
|----------------|-----------|:---------:|------------------------|--------------------------------------------------|
| `name`         | string    | no        | `""`                   | UTF-8                                            |
| `visible`      | bool      | no        | `true`                 |                                                  |
| `transform`    | Transform | no        | identity               | see §11                                          |
| `source_path`  | string    | no        | `""`                   | informational only in v1                         |
| `material_id`  | int       | no        | `-1`                   | `-1` or in `[0, materials.size())`               |
| `vertices`     | array     | no        | `[]`                   | every entry passes §9.2                          |
| `triangles`    | array     | no        | `[]`                   | every entry passes §9.3                          |

A mesh with `vertices.length() == 0` OR `triangles.length() == 0`
is rendered as empty (Mesh::empty() returns true and the kernel
loop runs zero iterations). This is permitted; the parser MUST
still accept it.

### 9.2 Vertex entry

| Key       | Type | Required? | Default       | Validation         |
|-----------|------|:---------:|--------------:|--------------------|
| `position`| Vec3 | **yes**   | —             | finite             |
| `normal`  | Vec3 | no        | `[0, 0, 0]`   | finite (NOT auto-normalised; renderer expects unit length)|
| `uv`      | Vec2 | no        | `[0, 0]`      | finite             |

### 9.3 Triangle entry

A triangle is a JSON array of exactly three `uint32_t` vertex
indices: `[v0, v1, v2]`. Counter-clockwise winding when viewed from
the front face.

| Constraint | Rule |
|---|---|
| Element type | non-negative integer fitting in `uint32_t` |
| Length       | exactly `3` |
| Index range  | each `vN` in `[0, vertices.length())` |
| Distinct?    | indices SHOULD be distinct; degenerate triangles (`v0 == v1` etc.) are accepted but produce zero contribution at intersection time |

### 9.4 Stage 9B mesh-renderer note

The kernel currently reads vertex positions in **world space** —
the per-mesh `transform` is uploaded but **not** applied to
positions during intersection. Authors who need a transformed
mesh today must pre-bake the transform into the vertex positions.
This restriction lifts when per-vertex transform support lands
(planned alongside instancing).

### 9.5 Stage 10B.8 status notes

Stage 10B.8 promotes `rr::scene::SceneMesh` from a placeholder
shell (`{object, source_path}`) to a real authoring entry that
composes `rr::geometry::Mesh` alongside the metadata, mirroring
the `SceneMaterial` / `SceneLight` pattern. The parser writes
into `SceneMesh::geometry`; the kernel-side mesh upload
(`GpuScene::upload_mesh`) still takes `rr::geometry::Mesh`
directly. Threading the loaded mesh through the upload path is
the final 10B sub-stage's job.

Implemented in this stage:

- `name` (string, optional).
- Inline `vertices` array per §9.2: required `position`
  (Vec3), optional `normal` (Vec3, **not** auto-normalised; the
  renderer expects unit-length normals), optional `uv`
  (Vec2). Empty vertex arrays are accepted - the resulting
  mesh is empty per §9.
- Inline `triangles` array per §9.3: each entry is a length-3
  array of non-negative integers fitting in `uint32_t`.
  Indices are validated against the already-populated vertex
  count per §12 #6 in strict reject-file mode.
- `material_id` (canonical) or `materialId` (camelCase
  shorthand consistent with the §8.1 sphere shorthand).
  Validated `-1` or in `[0, materials.size())` per §12 #5,
  reject-file mode.
- `transform` per §11: `position`, `rotation_radians` (note:
  the C++ field is `euler_rotation_radians`; the spec name
  wins on the wire), `scale` with all-positive validation.

Out of scope for Stage 10B.8 (deferred to the final 10B
sub-stage):

- `source_path`: the spec already labels this as
  informational; the parser does not consume it today. Kept
  on the `SceneMesh` shell for forward compatibility with the
  external-asset loader.
- `visible` on the `SceneObject` wrapper: same partial-
  implementation posture as 10B.6 / 10B.7.

Tools that emit `.rrscene` files MUST emit canonical
snake_case.

## 10. `lights`

Each entry maps to `rr::scene::SceneLight { object, data }`, where
`data` is an `rr::lighting::Light`. The Light is a flat
type-discriminated POD; the JSON object names the type explicitly.

```json
"lights": [
  {
    "name":       "key",
    "visible":    true,
    "type":       "directional",
    "direction":  [-0.4, -0.7, -0.6],
    "color":      [1.0, 0.95, 0.85],
    "intensity":  0.9
  },
  {
    "name":       "fill",
    "type":       "point",
    "position":   [2.0, 1.5, -2.5],
    "color":      [1.0, 0.85, 0.6],
    "intensity":  30.0
  },
  {
    "name":       "sky",
    "type":       "environment",
    "color":      [0.30, 0.40, 0.55],
    "intensity":  0.4
  }
]
```

| Key           | Type    | Required?     | Default                | Validation                                          |
|---------------|---------|:-------------:|------------------------|-----------------------------------------------------|
| `name`        | string  | no            | `""`                   |                                                     |
| `visible`     | bool    | no            | `true`                 |                                                     |
| `transform`   | Transform | no          | identity               | see §11; uploaded but unused in v1                  |
| `type`        | string  | **yes**       | —                      | one of `"point" / "directional" / "area" / "environment"` |
| `color`       | Vec3    | no            | `[1, 1, 1]`            | each `>= 0`                                         |
| `intensity`   | float   | no            | `1.0`                  | `>= 0`                                              |
| `position`    | Vec3    | required for `point`, `area`; ignored otherwise | `[0, 0, 0]` | finite |
| `direction`   | Vec3    | required for `directional`, `area`; ignored otherwise | `[0, -1, 0]` | finite, non-zero |
| `area_width`  | float   | required for `area` (PLACEHOLDER) | `0.0` | `> 0`                                  |
| `area_height` | float   | required for `area` (PLACEHOLDER) | `0.0` | `> 0`                                  |

**Direction normalisation**: the parser MUST normalise `direction`
for `directional` and `area` lights before populating
`Light::direction` (matching the behaviour of
`make_directional_light` / `make_area_light`). Zero-length input
falls back to `(0, -1, 0)`.

**Type semantics** (matches Stage 9B kernel behaviour):

| `type`        | Real renderer support | Notes                                                                                  |
|---------------|:---------------------:|----------------------------------------------------------------------------------------|
| `point`       | yes                   | `light_color * max(0, N · L) / d²` with epsilon falloff floor                          |
| `directional` | yes                   | `light_color * max(0, N · -direction)`; no falloff                                     |
| `area`        | **PLACEHOLDER**       | Uploaded; kernel currently skips. Real area-light sampling lands with the path tracer. |
| `environment` | yes (basic)           | Treated as ambient `light_color` (no directional dependence); HDR env-maps are future. |

### 10.1 Stage 10B.7 status notes

The Stage 10B.7 parser implements `type` (required, validated
against the four §10 enumerators), `name`, `color` (each
component `>= 0`), `intensity` (`>= 0`), and the type-specific
position / direction fields per §12 #8:

- `point` and `area` require `position`. Missing `position` for
  these types rejects the file.
- `directional` requires `direction`. The parser normalises the
  input vector before storing it (matching the
  `make_directional_light` factory). A zero-length input
  collapses to `(0, -1, 0)` rather than producing NaNs.
- `environment` requires neither `position` nor `direction`.

Out of scope for Stage 10B.7 (deferred until area-light sampling
ships with the path tracer):

- `area_width` / `area_height` for area lights. Both stay at
  the `Light` POD defaults (`0.0`); a v1.0 file that authors
  them is parsed by the JSON layer but the schema mapper does
  not consult them. Per §12 #8, area lights without these
  fields are degenerate today; the area-sampling stage will
  enforce the rule once the kernel actually consumes them.
- `visible` / `transform` on the `SceneObject` wrapper. Both
  stay at their defaults (`visible = true`, identity
  transform). Same partial-implementation posture as 10B.6
  spheres.

Tools that emit `.rrscene` files MUST emit canonical names only.

## 11. `transform` (used by spheres, meshes, lights)

Maps to `rr::math::Transform`. Plain data; conversion to a 4×4
matrix is deferred to consumers.

```json
"transform": {
  "position":         [0.0, 0.0, 0.0],
  "rotation_radians": [0.0, 0.0, 0.0],
  "scale":            [1.0, 1.0, 1.0]
}
```

| Key                | Type | Required? | Default      | Validation     |
|--------------------|------|:---------:|--------------|----------------|
| `position`         | Vec3 | no        | `[0, 0, 0]`  | finite         |
| `rotation_radians` | Vec3 | no        | `[0, 0, 0]`  | finite (Euler) |
| `scale`            | Vec3 | no        | `[1, 1, 1]`  | finite, all components > 0 |

**Note on Euler order**: the v1 parser MUST treat
`rotation_radians` as XYZ-order intrinsic Euler angles (the
prototype convention). The current renderer does not consume the
transform, so the order is informational; once the kernel applies
transforms (mesh-instancing / animation slices), this field's
order is locked.

## 12. Cross-section validation rules

Beyond per-field validation, the parser MUST enforce these
relationships before handing the scene to the renderer:

| # | Rule | Failure mode |
|---|------|-------------|
| 1 | `version` major must match parser major version. | reject file |
| 2 | `length(relativity.observer_velocity) < relativity.max_beta < 1`. | reject file |
| 3 | Every `materials[i].id` must be unique. | reject file |
| 4 | Every sphere `material_index` is `-1` or in `[0, materials.length())`. | reject sphere (load rest, warn) OR reject file (parser's choice; consistent across the file) |
| 5 | Every mesh `material_id` is `-1` or in `[0, materials.length())`. | same as #4 |
| 6 | Every triangle `[v0, v1, v2]` has indices in `[0, vertices.length())`. | reject mesh (load rest, warn) OR reject file |
| 7 | Every light `type` is one of `point / directional / area / environment`. | reject file |
| 8 | Light type-specific required fields are present (`position` for point/area, `direction` for directional/area, `area_*` for area). | reject light (load rest, warn) OR reject file |
| 9 | Sphere `radius > 0`. | reject sphere (load rest, warn) OR reject file |
| 10 | Resolution `width > 0` and `height > 0`. | reject file |

The choice between "reject file" vs "reject entry, load rest, warn"
is a parser-implementation choice that v1 declares **per-rule**
above. Tools that need strict file validity (e.g. a CLI converter)
MAY treat all "reject entry" cases as "reject file"; rendering
tools SHOULD load what they can and surface warnings.

## 13. Complete example

A small lit scene with one diffuse-red sphere and a quad ground
plane, plus directional + environment lighting:

```json
{
  "version": "1.0.0",

  "render_settings": {
    "width":             512,
    "height":            384,
    "samples_per_pixel": 1,
    "max_depth":         1
  },

  "camera": {
    "position":    [0.0, 0.5, 0.0],
    "target":      [0.0, 0.0, -3.0],
    "up":          [0.0, 1.0, 0.0],
    "fov_degrees": 50.0,
    "near":        0.1,
    "far":         100.0
  },

  "relativity": {
    "observer_velocity":      [0.0, 0.0, 0.0],
    "enable_aberration":      true,
    "enable_doppler":         true,
    "enable_searchlight":     true,
    "doppler_color_strength": 1.0,
    "searchlight_strength":   1.0,
    "max_beta":               0.999999
  },

  "materials": [
    {
      "id":         0,
      "name":       "red",
      "base_color": [0.85, 0.20, 0.20]
    },
    {
      "id":         1,
      "name":       "neutral",
      "base_color": [0.65, 0.65, 0.65]
    }
  ],

  "spheres": [
    {
      "name":           "ball",
      "center":         [0.0, 0.0, -3.0],
      "radius":         0.6,
      "material_index": 0
    }
  ],

  "meshes": [
    {
      "name":        "ground",
      "material_id": 1,
      "vertices": [
        { "position": [-2.0, -0.6, -2.0], "normal": [0, 1, 0], "uv": [0, 0] },
        { "position": [ 2.0, -0.6, -2.0], "normal": [0, 1, 0], "uv": [1, 0] },
        { "position": [ 2.0, -0.6, -5.0], "normal": [0, 1, 0], "uv": [1, 1] },
        { "position": [-2.0, -0.6, -5.0], "normal": [0, 1, 0], "uv": [0, 1] }
      ],
      "triangles": [
        [0, 1, 2],
        [0, 2, 3]
      ]
    }
  ],

  "lights": [
    {
      "name":      "sun",
      "type":      "directional",
      "direction": [-0.4, -0.7, -0.6],
      "color":     [1.0, 0.95, 0.85],
      "intensity": 1.0
    },
    {
      "name":      "sky",
      "type":      "environment",
      "color":     [0.30, 0.40, 0.55],
      "intensity": 0.4
    }
  ]
}
```

## 14. Format-evolution policy

When v1.x adds backwards-compatible features, this doc gets a new
section per addition; the version string bumps the minor / patch
component. The complete list of v1 reserved-but-unused features
the parser MUST accept silently:

| Future feature              | Reserved field(s)                             | Lands in module |
|-----------------------------|-----------------------------------------------|-----------------|
| Texture binding             | `materials[].base_color_texture_id` (int)     | 18              |
| Texture array               | top-level `textures` (array)                  | 18              |
| Material node graphs        | `materials[].graph` (object)                  | 23              |
| Per-mesh transform applied at intersection | (current `transform` finally consumed by kernel) | mesh-instancing slice |
| Area-light sampling         | (current `area_*` finally consumed by kernel) | 16 (path tracer)|
| HDR environment maps        | `lights[].env_map_path` (string)              | 18              |
| Spectral colour             | `materials[].base_color_spectrum` (array)     | post-18         |
| Animation                   | top-level `animation` (object)                | post-22         |

A v1.0 parser MUST silently ignore any field name it does not
recognise from the v1.0 schema. This guarantees that a v1.0 file
authored against a v1.x parser still loads in a v1.0 parser, with
the v1.x-specific features absent at runtime.

## 15. Parser non-goals (v1)

To set expectations for the parser-implementation slice:

- **No comments in JSON.** Strict JSON only. JSON5 / JSONC are
  rejected. (Tools that want comments can pre-process to standard
  JSON.)
- **No file references** beyond the placeholder `source_path` on
  meshes (v2 will define a binary mesh format and texture references).
- **No coordinate-system options.** The format is right-handed,
  +Y-up, units-agnostic. Files for other conventions must convert
  before saving.
- **No exposure / tone-map metadata.** Writeback is linear
  Rgba32F → 8-bit PPM today; HDR output is a future deliverable.
- **No camera animation.** A single static camera per file. Time
  / motion-blur land in a future schema version.

---

This is the complete `.rrscene` v1.0 specification. The parser
implementation lives in master module 15 and lands in the
`src/io/` layer (`SceneLoader.{h,cpp}` + `SceneWriter.{h,cpp}`)
in the next stage; this document is the contract that work
implements against.

# RRSCENE — RelativityRender Scene Format (v1)

Status: **specification only**. No parser exists yet. The eventual
parser is M13 work and lives in `src/io/`; this document is the
contract it implements.

This is the v1 minimum. Only the sections listed below are defined.
Advanced features (meshes, lights, materials, textures, environment
maps, motion blur, AOVs, ...) are explicit future-version concerns and
are listed in the [Out of scope for v1](#out-of-scope-for-v1) section
at the end.

## 1. File extensions

| Extension   | Meaning                                                     |
|-------------|-------------------------------------------------------------|
| `.rrscene`  | Canonical extension. Currently JSON-encoded; reserved name. |
| `.rrjson`   | Explicit "this is JSON" alias. Identical content.           |

A loader recognising one MUST recognise the other; they only differ
in the file-name convention authoring tools may prefer.

## 2. Top-level shape

A v1 file is a single JSON object with these optional sections plus
a mandatory `version` field:

```jsonc
{
    "version": 1,
    "render_settings": { ... },
    "camera":          { ... },
    "relativity":      { ... },
    "spheres":         [ ... ],
    "materials":       [ ... ]
}
```

- `version` is mandatory and MUST be `1` for this spec.
- All other sections are optional. Missing sections take their
  defaults (documented per section).
- Unknown top-level keys MUST be ignored by the parser, but the
  validator SHOULD warn about them so typos surface.

JSON has no comment syntax. The `.rrscene` parser MUST NOT accept
`//` or `/* ... */` comments. Authoring metadata that needs to
survive in the file goes in the (future-version) `meta` section.

## 3. Versioning

- The schema version is independent of the project version
  (`rr::core::kVersionString`).
- Backwards-incompatible changes bump the integer (`2`, `3`, ...).
- A v1 file MUST always parse on a v1+ parser. A v2 file MAY fail
  on a v1-only parser; the parser should report the version
  mismatch instead of guessing.
- A v1 file is allowed to omit any section. The defaults are
  authoritative for every field.

## 4. Coordinate system & units

- Right-handed, **+X right, +Y up, -Z forward**.
- Lengths are in scene units (the renderer is unit-agnostic;
  treat 1 unit as 1 metre by convention).
- Angles in degrees only when explicitly named `*_degrees`; all
  other angle-like quantities are in radians.
- Colours (when they appear in v2+) are linear RGB.
- All `Vec3` values are JSON arrays of three floats: `[x, y, z]`.

## 5. Section: `render_settings`

Per-frame output settings.

```json
{
    "width":  1280,
    "height": 720
}
```

| Field    | Type | Required | Default | Notes                              |
|----------|------|----------|---------|------------------------------------|
| `width`  | int  | no       | 1280    | Output image width in pixels. > 0. |
| `height` | int  | no       | 720     | Output image height in pixels. > 0.|

A v1 parser MUST reject non-positive `width` / `height` with a
descriptive error. Sample count, max bounce depth, and AOV
selection are deferred to v2+.

## 6. Section: `camera`

Pinhole camera placement and field of view.

```json
{
    "position": [0.0, 0.0, 0.0],
    "forward":  [0.0, 0.0, -1.0],
    "up":       [0.0, 1.0,  0.0],
    "fov":      45.0
}
```

| Field      | Type   | Required | Default          | Notes                                    |
|------------|--------|----------|------------------|------------------------------------------|
| `position` | Vec3   | no       | `[0, 0, 0]`      | Camera world position.                   |
| `forward`  | Vec3   | no       | `[0, 0, -1]`     | View direction; need not be unit length. |
| `up`       | Vec3   | no       | `[0, 1, 0]`      | Up hint; basis is re-orthogonalised.     |
| `fov`      | float  | no       | `45.0`           | Vertical field of view, in **degrees**. Open interval `(0, 180)`.|

Conventions:

- `forward` is the direction the camera looks toward. The renderer
  uses the existing `Camera::look_at(eye, eye + forward, up_hint)`
  pipeline; the loaded basis is re-orthogonalised, so non-unit or
  slightly non-orthogonal hints are tolerated.
- If `forward` is zero-length, the parser MUST reject the file.
- If `up` is parallel to `forward`, the parser falls back to the
  same convention as `Camera::look_at`: it picks an axis-aligned
  alternative so the basis stays well-defined.
- Aspect ratio is **derived from `render_settings`** (`width /
  height`); it is not stored here.
- `near` / `far` clip planes are deferred to v2+; the renderer
  treats every primary ray with the implicit `[0, +inf)` range.

## 7. Section: `relativity`

Observer kinematic state plus the artistic strength knobs that
control how the relativistic perception model participates in
rendering.

```json
{
    "beta_velocity":        0.0,
    "velocity_direction":   [0.0, 0.0, -1.0],
    "aberration_strength":  1.0,
    "doppler_strength":     1.0,
    "searchlight_strength": 1.0
}
```

| Field                  | Type  | Required | Default          | Notes                                                   |
|------------------------|-------|----------|------------------|---------------------------------------------------------|
| `beta_velocity`        | float | no       | `0.0`            | Magnitude of `\|β\| = \|v\|/c`. Clamped to `[0, 0.999999]`.|
| `velocity_direction`   | Vec3  | no       | `[0, 0, -1]`     | Unit vector. Auto-normalised; zero-length falls back to default.|
| `aberration_strength`  | float | no       | `1.0`            | Mix of full Lorentz aberration: `0` = identity, `1` = full physics. Outside `[0, 1]` is clamped. |
| `doppler_strength`     | float | no       | `1.0`            | Mix of the artistic Doppler colour shift. Same range / clamp.|
| `searchlight_strength` | float | no       | `1.0`            | Mix of the bolometric `D^4` beaming factor. Same range / clamp.|

Notes:

- The observer 3-velocity in c-units is reconstructed by the
  parser as `velocity = beta_velocity * normalize(velocity_direction)`.
  Splitting magnitude and direction in the file is purely an
  authoring affordance; the renderer's host model
  (`rr::relativity::Observer { velocity }`) carries them combined.
- `beta_velocity = 0` collapses every relativistic effect to
  identity regardless of the strength values; the renderer
  short-circuits in that case.
- The three `*_strength` fields each map to one continuous knob in
  `rr::relativity::RelativityParams`. The boolean toggles
  (`enable_aberration` / `enable_doppler` / `enable_searchlight`)
  in that struct are derived: a strength of exactly `0.0` disables
  the corresponding effect; non-zero enables it.
- The renderer never extrapolates beyond `|β| = 0.999999`; the
  parser silently clamps. This guards `gamma` from blowing up
  at the lightspeed singularity.

## 8. Section: `spheres`

Array of analytic sphere primitives. Each entry is a JSON object.

```json
[
    {
        "position":    [0.0, 0.0, -3.0],
        "radius":      1.0,
        "material_id": 0
    }
]
```

| Field         | Type  | Required | Default     | Notes                                           |
|---------------|-------|----------|-------------|-------------------------------------------------|
| `position`    | Vec3  | yes      | -           | World-space centre.                             |
| `radius`      | float | yes      | -           | Sphere radius. Must be > 0.                     |
| `material_id` | int   | no       | `-1`        | Forward-compatible reference; see below.        |

Notes:

- An empty `spheres` array is legal; the renderer produces a
  light-only / empty scene.
- `material_id` is the integer `id` of an entry in the
  [`materials`](#9-section-materials) array. `-1` means "no
  material assigned"; the renderer falls back to its neutral
  default. Ids that do not match any material in the array also
  fall back to the default (the parser MAY warn).

## 9. Section: `materials`

Array of PBR-style material parameter packs. Each entry is a JSON
object referenced by integer `id` from `spheres[i].material_id`.
The schema is intentionally a small, GPU-uploadable subset of the
host `rr::material::MaterialParams` POD; remaining fields
(`metallic`, `specular`, `transmission`) take their host defaults
in v1.

```json
[
    {
        "id":                0,
        "name":              "matte_red",
        "base_color":        [0.9, 0.15, 0.15],
        "emission_color":    [0.0, 0.0, 0.0],
        "emission_strength": 0.0,
        "roughness":         0.5
    }
]
```

| Field               | Type   | Required | Default          | Notes                                                            |
|---------------------|--------|----------|------------------|------------------------------------------------------------------|
| `id`                | int    | yes      | -                | Non-negative. Must be unique within the array. Referenced by `spheres[i].material_id`. |
| `name`              | string | no       | `""`             | Authoring / debug label. Not used by the renderer.               |
| `base_color`        | Vec3   | no       | `[0.8, 0.8, 0.8]`| Linear RGB albedo / diffuse colour. Components clamped to `[0, ∞)`. |
| `emission_color`    | Vec3   | no       | `[0, 0, 0]`      | Linear RGB emissive tint. Components clamped to `[0, ∞)`.        |
| `emission_strength` | float  | no       | `0.0`            | Multiplier on `emission_color`. `>= 0`. `0` disables emission.   |
| `roughness`         | float  | no       | `0.5`            | Microfacet roughness. Clamped to `[0, 1]`: `0` = mirror, `1` = fully rough. |

Notes:

- An empty `materials` array is legal. Every sphere whose
  `material_id` is unmatched falls back to the renderer's neutral
  default material (`base_color = [0.8, 0.8, 0.8]`, no emission,
  `roughness = 0.5`).
- `id` is the **lookup key**, not the array index. A v1 file is
  free to list materials in any order or sparsely (e.g. ids
  `0`, `2`, `5`); the parser builds a host array indexed by `id`
  internally. Duplicate `id` values are an error.
- v1 only exposes `base_color`, `emission_color`,
  `emission_strength`, and `roughness`. The host
  `MaterialParams` carries `metallic`, `specular`, and
  `transmission` slots; in v1 these take their host defaults
  (`metallic = 0`, `specular = 0.5`, `transmission = 0`). Future
  schema versions surface them as additional optional fields
  with the same names; absent fields keep host defaults.
- Texture-driven parameters (image maps for `base_color`,
  `roughness`, etc.) are explicitly not in v1; they land with
  the texture system (M16). v1 files that want a uniform colour
  per material are fully expressive.

## 10. Common types

### Vec3

A JSON array of exactly three numbers (int or float — JSON does not
distinguish; the parser converts to `float`).

```json
[1.0, 2.0, 3.0]
```

Anything else (`[1, 2]`, `[1, 2, 3, 4]`, an object, a string) is a
parse error.

## 11. Defaults policy

Every field has a documented default. A minimal valid v1 file is:

```json
{ "version": 1 }
```

The renderer treats this as: 1280x720 image, default camera at the
origin looking down -Z, no relativistic motion, no spheres. The
output is the existing default sky gradient.

A parser MUST apply defaults only to **missing** fields. It MUST
NOT silently substitute defaults for malformed inputs (e.g., a Vec3
with two entries) — those are errors.

## 12. Validation rules

A v1 parser MUST enforce, in addition to the per-section rules
above:

1. The top-level value is a JSON object; arrays / strings / scalars
   at the top level are errors.
2. `version` is present and equals `1`.
3. All Vec3 values are arrays of exactly 3 numbers.
4. `render_settings.width` and `render_settings.height` are > 0.
5. `camera.fov` is strictly within `(0, 180)`.
6. `camera.forward` is non-zero-length.
7. Each sphere has `position` and `radius`; `radius` > 0.
8. Each material has `id`; `id` is a non-negative integer and
   unique within the `materials` array. Out-of-range numerical
   colour or roughness values are clamped per the
   [materials](#9-section-materials) table; missing fields take
   their documented defaults.
9. Unknown top-level keys SHOULD warn but MUST NOT fail.

Files that violate any MUST clause are rejected with a descriptive
diagnostic that names the offending field and (when the underlying
parser supports it) line / column.

## 13. Complete example

A single sphere sitting 3 units in front of a camera that is
boosted to 50% of light speed along its forward direction, with all
relativistic effects at full strength:

```json
{
    "version": 1,

    "render_settings": {
        "width":  800,
        "height": 600
    },

    "camera": {
        "position": [0.0, 0.0,  0.0],
        "forward":  [0.0, 0.0, -1.0],
        "up":       [0.0, 1.0,  0.0],
        "fov":      50.0
    },

    "relativity": {
        "beta_velocity":        0.5,
        "velocity_direction":   [0.0, 0.0, -1.0],
        "aberration_strength":  1.0,
        "doppler_strength":     1.0,
        "searchlight_strength": 1.0
    },

    "spheres": [
        {
            "position":    [0.0, 0.0, -3.0],
            "radius":      1.0,
            "material_id": 0
        }
    ],

    "materials": [
        {
            "id":                0,
            "name":              "matte_red",
            "base_color":        [0.9, 0.15, 0.15],
            "emission_color":    [0.0, 0.0, 0.0],
            "emission_strength": 0.0,
            "roughness":         0.5
        }
    ]
}
```

This file is parser-equivalent to the M9 relativistic-sphere case
at `β = 0.5`, with the sphere now resolved to a matte-red material
instead of the renderer's neutral default.

## 14. Out of scope for v1

These features are deferred to later schema versions and MUST NOT
appear in v1 files. The eventual parser MAY warn-and-ignore them
when present, to ease forward migration:

- `meshes` array (triangle meshes, vertex buffers, index buffers,
  external `.obj` references).
- `lights` array (point / directional / area / environment).
- Material fields beyond `base_color` / `emission_color` /
  `emission_strength` / `roughness` (i.e. `metallic`, `specular`,
  `transmission`).
- `textures`, environment maps, IBL.
- Per-frame motion (animation curves, per-object velocities,
  retarded-time controls).
- AOV / render-pass selection.
- `meta` section for authoring metadata (tool name / version,
  comments, asset URIs).
- Camera depth-of-field, motion blur shutter.
- Near / far clip planes.
- Per-sphere transform / parent links / object names.

The host data model already supports several of these
(meshes and lights have host structs and GPU upload paths);
they're left out of v1 only because this slice is intentionally
small. Future schema versions add them by mapping the existing
C++ structs to JSON sections of the same names.

## 15. References

- `src/scene/Scene.h` — host data model the parser populates.
- `src/scene/Transform.h` — `rr::math::Transform` carried by every
  `SceneObject`.
- `src/camera/Camera.h` — `look_at` semantics the parser invokes.
- `src/relativity/RelativityParams.h` —
  `Observer { velocity }` and `RelativityParams { ... }` the
  parser populates from the v1 `relativity` section.
- `src/geometry/Sphere.h` — POD `Sphere` consumed by `GpuScene`.
- `src/material/MaterialTypes.h` — `MaterialParams` POD the parser
  populates from each `materials[i]` entry. Fields not exposed in
  v1 (`metallic`, `specular`, `transmission`) keep their host
  defaults.
- `docs/MASTER_ARCHITECTURE.md` — `Scene File Format` module
  contract (no GPU / UI / DCC dependencies).
- `docs/MODULE_MAP.md` — module 18 (Scene File Format).
- `docs/BUILD_PLAN.md` — milestone state and parser implementation
  plan.

# RelativityRenderBridge — Cinema 4D Python plugin

**Milestone:** M19 (extension slice 1). **Status:** in progress.

Cinema 4D Python plugin that connects a live document to the
RelativityRender renderer over the project's `.rrscene` file
format. Currently ships two commands; geometry / materials /
lights translation, server-protocol traffic, and preview frame
display in the C4D viewport land in subsequent slices.

## Commands

### Plugins → RelativityRender: Export Scene

Writes a v1 `.rrscene` containing:

- The active rendering camera's transform (position + forward +
  up), converted from Cinema 4D's left-handed `+Z forward`
  convention to the renderer's right-handed `-Z forward`.
- The camera's vertical FOV in degrees (`CAMERAOBJECT_FOV_VERTICAL`,
  with a horizontal-FOV + 4:3 fallback if the vertical id is
  unavailable).
- The active render data's resolution (`RDATA_XRES` /
  `RDATA_YRES`), with a 640×480 fallback when no render data is
  reachable.
- The relativity section, populated from the relativity
  controller's user data when one is present in the document
  (see *Create Controller* below). Without a controller the
  command writes the v1 defaults.
- Every native polygon object in the document. For each:
  - **Vertices** baked to world space (the polygon's global
    matrix `mg` is applied to each local point) and
    Z-flipped into the renderer's coordinate system.
  - **Triangle indices** - quads are split along the `a-c`
    diagonal into two triangles; degenerate triangles are
    pruned. Native triangles (Cinema 4D's `c == d` quad
    encoding) emit a single triangle.
  - **Material id** - an integer lookup key into the scene's
    `materials[]`. The bridge allocates a fresh id the first
    time it sees a Cinema 4D material name and reuses the
    same id for every later mesh that references it. Meshes
    without a Texture tag get `material_id = -1` (renderer's
    neutral default). Real material parameter translation
    (RGB albedo, roughness, emission) is a follow-up slice;
    today the bridge only writes a stub `{ id, name }` per
    unique material so the mesh ↔ material relationship is
    visible.

#### Unsupported objects

The bridge skips and warns about any object kind it cannot
faithfully translate yet:

| Object kind | Detected via                      | Note                              |
|-------------|-----------------------------------|-----------------------------------|
| Generators  | `GetInfo() & OBJECT_GENERATOR`    | Cloner / Subdivision / Boole / ...|
| Deformers   | `GetInfo() & OBJECT_MODIFIER`     | as standalone objects             |
| Volumes     | `Ovolume`/`Ovolumebuilder`/etc.   | when those constants exist        |
| Hair        | `Ohair`                           | when that constant exists         |

The "make editable" / `GetCache()` workflow needed to bake
generators into polygons is a deliberate follow-up slice.

A polygon mesh whose subtree contains a deformer is still
exported, but the deformation is **not** applied: the bridge
reads the raw `GetAllPoints()`, not `GetDeformCache()`. The
dialog calls out which polygons that affects so the user
knows their on-disk geometry is the pre-deform mesh.

#### Output path

- If the document has been saved, the file lands next to it
  with the same stem and a `.rrscene` extension.
- Otherwise it lands at
  `<C4D_startup_write>/RelativityRender/untitled.rrscene`.

The confirmation dialog summarises the export: saved path,
resolution, camera FOV, controller status, exported mesh /
triangle / material counts, and a list of skipped objects
with reasons.

### Plugins → RelativityRender: Create Controller

Creates a Null object named `RelativityRender Controller` in the
active document and selects it. The Null carries five user-data
fields the **Export Scene** command later picks up by name:

| User-data field         | Type   | Range            |
|-------------------------|--------|------------------|
| `beta_velocity`         | Real   | `0` … `0.999999` |
| `velocity_direction`    | Vector | any              |
| `aberration_strength`   | Real   | `0` … `1`        |
| `doppler_strength`      | Real   | `0` … `1`        |
| `searchlight_strength`  | Real   | `0` … `1`        |

Defaults are: `beta_velocity = 0`, `velocity_direction = (0, 0, -1)`,
all strengths at `1`. The values mirror the v1 `.rrscene`
`relativity` section + the host parser's clamps in
`src/io/SceneLoader.cpp`. The create operation is wrapped in
`StartUndo`/`EndUndo` so the standard Edit → Undo reverses it.

Re-running the command creates an additional controller; the
Export Scene command picks the first one it finds (depth-first).

## Layout

```
RelativityRenderBridge/
    RelativityRenderBridge.pyp    # Cinema 4D plugin entry point
    rrscene_writer.py             # plain-Python .rrscene writer (testable)
    tests/
        test_rrscene_writer.py    # standalone test (runs without C4D)
    README.md
```

## Install

Copy the `RelativityRenderBridge/` directory into your Cinema 4D
**plugins** folder. The exact location depends on your platform
and Cinema 4D version. On a typical install:

- **Windows:** `%APPDATA%\MAXON\Cinema 4D <version>\plugins\`
- **macOS:** `~/Library/Preferences/MAXON/Cinema 4D <version>/plugins/`
- **Linux:** `~/.MAXON/Cinema 4D <version>/plugins/`

Restart Cinema 4D. The new menu entries appear under **Plugins →
RelativityRender: Export Scene** and **Plugins → RelativityRender:
Create Controller**.

## Plugin IDs

Two development plugin ids are used as placeholders:

- `RelativityRender: Export Scene` — `1058600`
- `RelativityRender: Create Controller` — `1058601`

**Before any public release**, request real plugin ids from
PluginCafe (https://plugincafe.maxon.net/) and replace
`PLUGIN_ID_EXPORT_SCENE` / `PLUGIN_ID_CREATE_CONTROLLER` in
`RelativityRenderBridge.pyp`.

## Coordinate convention

Cinema 4D uses a **left-handed** Y-up coordinate system: `+X`
right, `+Y` up, `+Z` forward into the scene; the camera looks
down its local `+Z`. The renderer uses a **right-handed** Y-up
coordinate system with `+Z` toward the viewer; the camera looks
down its local `-Z` (rrscene default `forward = [0, 0, -1]`).

The bridge converts by negating the `Z` component of every world-
space position and direction vector. This conversion is centralised
in `rrscene_writer.convert_c4d_position` /
`convert_c4d_direction` / `convert_c4d_camera_basis` so the .pyp
plugin never has to know about handedness directly.

## Dependency rules

Per `docs/MODULE_MAP.md` and `integrations/c4d/README.md`:

- This plugin depends only on the **Cinema 4D SDK** (Python
  `c4d` module) and the project's **`.rrscene` file format**.
- The plugin must NOT import anything under `src/` or any
  internal renderer headers. The renderer is reached, in
  later slices, through the file format and the renderer
  server protocol (`src/server/RenderServer.{h,cpp}` from the
  M18 foundation, port `127.0.0.1:7777`).

## Running the standalone tests

`rrscene_writer.py` is plain Python with no Cinema 4D imports,
so the test harness runs under stock `python3`:

```
$ python3 integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py
test_rrscene_writer: 88/88 passed
```

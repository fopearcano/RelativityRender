# RelativityRenderBridge — Cinema 4D Python plugin

**Milestone:** M19 (extension slice 1). **Status:** in progress.

Cinema 4D Python plugin that connects a live document to the
RelativityRender renderer over the project's `.rrscene` file
format. Currently ships two commands; geometry / materials /
lights translation, server-protocol traffic, and preview frame
display in the C4D viewport land in subsequent slices.

## Commands

The bridge ships six command plugins. Two operate on the
Cinema 4D document only; three talk to the RelativityRender
renderer server over TCP; one opens a floating dialog that
groups the server-talking actions plus the four relativity
sliders into a single panel.

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
    `materials[]`. Resolved in priority order:
    1. First Texture tag's bound material - pulls
       `MATERIAL_COLOR_COLOR` × `MATERIAL_COLOR_BRIGHTNESS`
       into `base_color`, and (when `MATERIAL_USE_LUMINANCE`
       is on) `MATERIAL_LUMINANCE_COLOR` plus
       `MATERIAL_LUMINANCE_BRIGHTNESS` as
       `emission_color` + `emission_strength`. The bridge
       supports Cinema 4D's Standard `Mmaterial`; other
       material types (Physical, Redshift, Octane, ...)
       export only the material name and surface a warning
       in the dialog.
    2. **Viewport "Display Color"** when no Texture tag is
       present and `ID_BASEOBJECT_USECOLOR` is set to
       `Always` or `Layer`. The bridge emits a deduped
       `Viewport: r, g, b` material entry; multiple
       polygons with the same viewport colour share one
       `materials[]` slot.
    3. `material_id = -1` (renderer's neutral default)
       when neither resolves.
- Cinema 4D **lights** the v1 protocol can carry:
  - `LIGHT_TYPE_OMNI` -> `"point"` (position from `mg.off`).
  - `LIGHT_TYPE_DISTANT` / `LIGHT_TYPE_PARALLEL` ->
    `"directional"` (direction from `mg.v3`).
  - `LIGHT_TYPE_AREA` / `LIGHT_TYPE_TUBE` -> degraded to
    `"point"` at the area's origin; the dialog flags the
    lossy conversion (rrscene v1 has no area metadata).
  - `LIGHT_TYPE_SPOT` / `LIGHT_TYPE_PARSPOT` -> skipped with
    a warning (no spot cone in v1).
  Light `color` comes from `LIGHT_COLOR`; `intensity` from
  `LIGHT_BRIGHTNESS`. Both are non-negative-clamped on the
  writer side.

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
triangle / material counts, point + directional light counts,
and warning blocks for skipped objects, deformer-affected
polygons, light-type caveats, and unsupported material types.

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

### Plugins → RelativityRender: Ping Server

Opens a TCP socket to the RelativityRender renderer server
(default `127.0.0.1:7777`), sends `ping`, displays the reply
in a dialog. Used as a connectivity check before issuing the
heavier Send Scene / Render Scene commands. Fails with a
clear "could not reach the renderer server" dialog when no
server is listening on the port.

### Plugins → RelativityRender: Send Scene

Exports the active document to a `.rrscene` file via the
same `_export_to_disk` path the Export Scene command uses,
then sends `load_scene <abs_path>` over the protocol so the
server caches the parsed scene ready to render. The dialog
shows both the export summary and the server's reply
(`OK loaded ... materials, ... spheres, ... lights, ...
meshes` on success).

If the export succeeds but the server cannot be reached, the
dialog says so explicitly and reminds the user that the
on-disk `.rrscene` is still available for later submission.

### Plugins → RelativityRender: Render Scene

Sends the `render` command and displays the server's reply.
On a CUDA-enabled server build, that's
`OK rendered <W>x<H> to <abs_path>` carrying the absolute
path of the saved PPM. On host-only builds it reports
`ERR render: no CUDA backend compiled in` so the user knows
the server needs to be rebuilt with `-DRR_ENABLE_CUDA=ON`.

The bridge does NOT pull pixels back over the protocol
itself - that's a follow-up slice. For now the user opens
the saved file from the path the dialog reports.

### Plugins → RelativityRender: Preview Dialog

Opens a floating `c4d.gui.GeDialog` panel that groups every
server-talking action and the four relativity controls into
one window:

| Group        | Controls                                          |
|--------------|---------------------------------------------------|
| Server       | host text field, port spinner, **Ping** button    |
| Actions      | **Export Scene**, **Send Scene**, **Render**      |
| Relativity   | beta, aberration, doppler, searchlight sliders    |
| Response     | multi-line read-only text area                    |

The dialog opens asynchronously (`DLG_TYPE_ASYNC`) so it
stays available while the user keeps working in Cinema 4D.
A single instance is reused across re-opens, so the
response-text history persists for the life of the C4D
session.

The four relativity sliders are dialog-local state. Send
Scene from the dialog passes its slider values directly into
`_export_to_disk(doc, relativity_override=...)`, bypassing
any controller in the document. The menu commands continue
to read the controller as before. That keeps the two paths
independent so a user comparing slider tweaks against a
saved controller can do so without the dialog clobbering the
document.

The dialog is **text-only** at this slice. The response area
shows the most recent server reply (and any input-validation
error) one line at a time, newest at the bottom. Image
preview - bitmap area, framebuffer streaming, progressive
update - is a follow-up slice and is intentionally not in
this slice's scope.

#### Server connection details

| Parameter      | Default          |
|----------------|------------------|
| Host           | `127.0.0.1`      |
| Port           | `7777`           |
| Ping timeout   | `2 s`            |
| Load timeout   | `10 s`           |
| Render timeout | `60 s`           |

Each command opens a fresh TCP connection, sends one line,
drains the response, and closes - matching the v1 server's
"one client at a time" accept loop. A C4D plugin-prefs panel
that lets the user override host / port / timeouts will land
alongside the multi-client server work.

## Layout

```
RelativityRenderBridge/
    RelativityRenderBridge.pyp    # Cinema 4D plugin entry point
    rrscene_writer.py             # plain-Python .rrscene writer (testable)
    server_client.py              # plain-Python protocol client (testable)
    preview_state.py              # plain-Python dialog helpers (testable)
    tests/
        test_rrscene_writer.py    # standalone test (runs without C4D)
        test_server_client.py     # standalone test (runs without C4D)
        test_preview_state.py     # standalone test (runs without C4D)
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

Six development plugin ids are used as placeholders:

| Command                                | Placeholder id |
|----------------------------------------|----------------|
| `RelativityRender: Export Scene`       | `1058600`      |
| `RelativityRender: Create Controller`  | `1058601`      |
| `RelativityRender: Ping Server`        | `1058602`      |
| `RelativityRender: Send Scene`         | `1058603`      |
| `RelativityRender: Render Scene`       | `1058604`      |
| `RelativityRender: Preview Dialog`     | `1058605`      |

**Before any public release**, request real plugin ids from
PluginCafe (https://plugincafe.maxon.net/) and replace each
`PLUGIN_ID_*` constant in `RelativityRenderBridge.pyp`.

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

`rrscene_writer.py`, `server_client.py`, and `preview_state.py`
are plain Python with no Cinema 4D imports, so the test
harnesses run under stock `python3`:

```
$ python3 integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py
test_rrscene_writer: 118/118 passed

$ python3 integrations/c4d/RelativityRenderBridge/tests/test_server_client.py
test_server_client: 33/33 passed

$ python3 integrations/c4d/RelativityRenderBridge/tests/test_preview_state.py
test_preview_state: 89/89 passed
```

The TCP socket layer of `RenderServerClient.send_command` is
exercised manually via the `RelativityRender --serve` smoke
test documented in `docs/BUILD_PLAN.md`. The C4D-only parts
of the bridge (`PreviewDialog`'s `CreateLayout` /
`InitValues` / `Command` overrides) are validated by AST
parse only - their behaviour reduces to calls into the
already-tested helpers.

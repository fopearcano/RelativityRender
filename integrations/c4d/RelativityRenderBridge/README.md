# RelativityRenderBridge — Cinema 4D Python plugin

**Milestone:** M19 (foundation slice). **Status:** in progress.

Cinema 4D Python command plugin that exports the current document
as a RelativityRender `.rrscene` file. This is the bridge's
foundation slice — it ships exactly one command and the file it
writes is intentionally minimal:

- Registers the menu entry **Plugins → RelativityRender: Export Scene**.
- On execute: writes a minimal but-valid v1 `.rrscene` to disk and
  shows a confirmation dialog with the saved path.
- Uses the active document's render-settings resolution if
  available; otherwise falls back to 640×480.
- No geometry, materials, lights, or live document translation
  yet. No preview UI. No server connection. Those land in
  subsequent slices.

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

Restart Cinema 4D. The new menu entry appears under **Plugins →
RelativityRender: Export Scene**.

## Plugin ID

The .pyp uses the development plugin id `1058600` as a
placeholder. **Before any public release**, request a real
plugin id from PluginCafe (https://plugincafe.maxon.net/) and
replace `PLUGIN_ID` in `RelativityRenderBridge.pyp`.

## Where the file lands

- If the active Cinema 4D document has been saved, the bridge
  writes `<doc_dir>/<doc_stem>.rrscene` next to the original
  file.
- Otherwise it writes to
  `<C4D startup-write path>/RelativityRender/untitled.rrscene`,
  which is the user's per-platform Cinema 4D prefs folder.

The dialog that appears after a successful export shows the
absolute path that was written.

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
test_rrscene_writer: <N>/<N> passed
```

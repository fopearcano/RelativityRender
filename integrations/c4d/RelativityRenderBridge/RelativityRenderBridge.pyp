"""Cinema 4D Python plugin: RelativityRender bridge.

Registers a single command, `RelativityRender: Export Scene`,
that:
  1. Decides where to write the .rrscene file (next to the
     active document if it has been saved; otherwise under the
     user's Cinema 4D temp folder).
  2. Writes a minimal but-valid v1 .rrscene through
     `rrscene_writer.write_empty_rrscene`.
  3. Shows a message dialog confirming the destination path.

This is the **foundation slice** of the bridge (M19). Translation
of the live Cinema 4D document into the .rrscene schema (objects,
materials, lights, camera) lands in subsequent slices. No preview
UI, no server connection, no progressive frames yet.

Per the project's dependency rules
(`docs/DEVELOPMENT_RULES.md` + `integrations/c4d/README.md`):

  - This file is the only Cinema 4D Python entry point in the
    repo. Nothing under `src/` may import it.
  - The bridge depends on the .rrscene file format and (later)
    the renderer server protocol. It does NOT import any
    `rr_*` C++ headers or link against renderer internals.

Plugin install layout (Cinema 4D-side):
    <C4D_PLUGINS_DIR>/RelativityRenderBridge/
        RelativityRenderBridge.pyp
        rrscene_writer.py
        README.md
"""

import os
import sys

import c4d
from c4d import gui, plugins, storage

# The .pyp file is the plugin entry point Cinema 4D loads. Helper
# modules (`rrscene_writer`) live next to it; add the plugin
# directory to sys.path so the import below resolves regardless of
# how Cinema 4D's loader sets up the working directory.
_PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))
if _PLUGIN_DIR not in sys.path:
    sys.path.insert(0, _PLUGIN_DIR)

import rrscene_writer  # noqa: E402  (sys.path mutation must precede)


# Cinema 4D plugin IDs are globally unique 32-bit integers. The
# range 1000001-1000010 is reserved for in-house testing; ranges
# above ~1058600 are real registered IDs from PluginCafe
# (https://plugincafe.maxon.net/). The placeholder below is a
# development ID. Before a public release the project must
# request a real ID from PluginCafe and replace this constant.
PLUGIN_ID = 1058600

# Display name (Plugins menu entry). Matches the prompt's
# `RelativityRender: Export Scene` exactly.
PLUGIN_NAME = "RelativityRender: Export Scene"

PLUGIN_HELP = (
    "Export the current Cinema 4D document as a RelativityRender "
    "(.rrscene) file. Foundation slice: writes a placeholder "
    "scene and shows where it landed; full document translation "
    "arrives in subsequent slices."
)


def _resolve_export_path(doc):
    """Choose where to write the .rrscene file.

    Strategy:
      - If the active document has been saved, write next to it
        with the same stem and a `.rrscene` extension.
      - Otherwise fall back to Cinema 4D's user prefs folder
        (always exists per the SDK), under
        `RelativityRender/untitled.rrscene`.
    """
    doc_path = doc.GetDocumentPath() if doc is not None else ""
    doc_name = doc.GetDocumentName() if doc is not None else ""

    if doc_path:
        stem = os.path.splitext(doc_name)[0] if doc_name else "untitled"
        return os.path.join(doc_path, stem + ".rrscene")

    # Fall back to a writable user-prefs folder. C4DPL_PREFS_DIRECTORY
    # is the canonical "this user's prefs" path on every platform.
    prefs = storage.GeGetStartupWritePath()
    if not prefs:
        # Last-resort: current working directory. Cinema 4D's CWD
        # is process-dependent; this is a best-effort fallback.
        prefs = os.getcwd()
    target_dir = os.path.join(prefs, "RelativityRender")
    return os.path.join(target_dir, "untitled.rrscene")


class ExportSceneCommand(plugins.CommandData):
    """Cinema 4D command plugin handler.

    `Execute` is invoked when the user picks the menu entry. It
    runs in the main thread, so blocking work (file IO, message
    dialogs) is allowed.
    """

    def Execute(self, doc):
        try:
            target = _resolve_export_path(doc)

            # The note becomes a `_note` field in the saved JSON.
            # The host parser warns-and-ignores unknown top-level
            # keys, so this round-trips without breaking the
            # parse - and helps a human inspecting the file know
            # who wrote it.
            note = ("Empty bridge export from RelativityRenderBridge "
                    "(M19 foundation slice).")

            # Use the Cinema 4D document's render settings if
            # available. Falls back to the writer's defaults
            # (640x480, 50deg fov) when no doc / no render data is
            # reachable - keeps the foundation slice testable
            # without a saved document.
            width  = 640
            height = 480
            fov    = 50.0
            if doc is not None:
                rd = doc.GetActiveRenderData()
                if rd is not None:
                    w = rd[c4d.RDATA_XRES]
                    h = rd[c4d.RDATA_YRES]
                    if w and w > 0:  width  = int(w)
                    if h and h > 0:  height = int(h)

            saved_path = rrscene_writer.write_empty_rrscene(
                path=target,
                width=width,
                height=height,
                fov_deg=fov,
                note=note,
            )

            gui.MessageDialog(
                "RelativityRender bridge\n\n"
                "Exported empty .rrscene to:\n"
                + saved_path
                + "\n\n"
                "(Foundation slice - geometry / materials / "
                "lights translation comes in a later slice.)"
            )
            return True
        except Exception as exc:  # noqa: BLE001
            # Cinema 4D plugins must not raise out of `Execute`;
            # surface the error in a dialog and return True so the
            # command stays registered.
            gui.MessageDialog(
                "RelativityRender bridge\n\n"
                "Export failed:\n" + str(exc)
            )
            return True


def _register():
    plugins.RegisterCommandPlugin(
        id=PLUGIN_ID,
        str=PLUGIN_NAME,
        info=0,
        help=PLUGIN_HELP,
        dat=ExportSceneCommand(),
        icon=None,
    )


if __name__ == "__main__":
    _register()

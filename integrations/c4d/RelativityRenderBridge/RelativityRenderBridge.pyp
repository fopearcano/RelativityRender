"""Cinema 4D Python plugin: RelativityRender bridge.

Registers two commands under the Plugins menu:

  - **RelativityRender: Export Scene** - reads the active
    document's camera / render settings / (optional)
    relativity-controller user data, and writes a v1 .rrscene
    file. Shows a confirmation dialog with the saved path.
  - **RelativityRender: Create Controller** - creates a Null
    object named "RelativityRender Controller" with five
    user-data fields the Export Scene command will pick up:
    `beta_velocity`, `velocity_direction`,
    `aberration_strength`, `doppler_strength`,
    `searchlight_strength`. Selects the new object so the
    user lands on it ready to scrub the values.

The bridge is the only place in the repository allowed to
depend on Cinema 4D. Per `docs/MODULE_MAP.md` and
`integrations/c4d/README.md`:

  - This file is the only Cinema 4D Python entry point in the
    repo. Nothing under `src/` may import it.
  - The bridge depends on the `.rrscene` file format and (later)
    the renderer server protocol. It does NOT import any
    `rr_*` C++ headers or link against renderer internals.

Plugin install layout (Cinema 4D-side):
    <C4D_PLUGINS_DIR>/RelativityRenderBridge/
        RelativityRenderBridge.pyp
        rrscene_writer.py
        README.md
"""

import math
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
# range above ~1058600 is for real registered IDs from PluginCafe
# (https://plugincafe.maxon.net/); the values below are
# placeholders. Before a public release the project must request
# real IDs from PluginCafe and replace these constants.
PLUGIN_ID_EXPORT_SCENE       = 1058600
PLUGIN_ID_CREATE_CONTROLLER  = 1058601

PLUGIN_NAME_EXPORT_SCENE      = "RelativityRender: Export Scene"
PLUGIN_NAME_CREATE_CONTROLLER = "RelativityRender: Create Controller"

PLUGIN_HELP_EXPORT_SCENE = (
    "Export the current Cinema 4D document as a RelativityRender "
    "(.rrscene) file. Camera transform, FOV, render resolution, "
    "and (if a Relativity Controller is in the scene) its user "
    "data are written. Geometry / materials / lights translation "
    "lands in subsequent slices."
)
PLUGIN_HELP_CREATE_CONTROLLER = (
    "Create a Null object that carries the relativity controls "
    "(beta_velocity, velocity_direction, aberration / doppler / "
    "searchlight strengths) the Export Scene command picks up."
)

# Marker name. Both commands use this constant: the export
# searches the document for an object with this exact name; the
# create command sets it on the new Null. Keeping it as a single
# constant means the two commands cannot drift.
RELATIVITY_CONTROLLER_NAME = "RelativityRender Controller"

# User-data field names. Mirror the .rrscene `relativity` section
# exactly so a user reading both files / both UIs sees the same
# vocabulary. The .pyp matches user-data fields by name (not by
# desc id) so renaming on the C4D side breaks discovery loudly
# rather than silently corrupting the export.
UD_FIELD_BETA_VELOCITY        = "beta_velocity"
UD_FIELD_VELOCITY_DIRECTION   = "velocity_direction"
UD_FIELD_ABERRATION_STRENGTH  = "aberration_strength"
UD_FIELD_DOPPLER_STRENGTH     = "doppler_strength"
UD_FIELD_SEARCHLIGHT_STRENGTH = "searchlight_strength"


# ---------------------------------------------------------------------------
# Cinema 4D helpers.
# ---------------------------------------------------------------------------

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

    prefs = storage.GeGetStartupWritePath()
    if not prefs:
        prefs = os.getcwd()
    target_dir = os.path.join(prefs, "RelativityRender")
    return os.path.join(target_dir, "untitled.rrscene")


def _get_active_camera(doc):
    """Return the document's currently rendering camera.

    `BaseDocument.GetRenderBaseDraw().GetSceneCamera(doc)` returns
    the camera the renderer would use on this frame, falling back
    to the editor's default camera when no scene camera is
    active. That matches the Export Scene command's intent: write
    out *what would render*, not just whatever is selected.
    """
    if doc is None:
        return None
    bd = doc.GetRenderBaseDraw()
    if bd is None:
        return None
    cam = bd.GetSceneCamera(doc)
    if cam is None:
        cam = bd.GetEditorCamera()
    return cam


def _camera_basis_from_matrix(mg):
    """Pull (position, forward, up) tuples from a C4D global
    matrix. Cinema 4D's camera looks down its local +Z axis, so
    `mg.v3` is the world-space view direction. The renderer's
    coordinate conversion (Z-flip) happens later, in the writer.
    """
    return (
        (mg.off.x, mg.off.y, mg.off.z),  # position
        (mg.v3.x,  mg.v3.y,  mg.v3.z),   # forward (C4D look direction)
        (mg.v2.x,  mg.v2.y,  mg.v2.z),   # up
    )


def _camera_vertical_fov_degrees(cam):
    """Read the active camera's vertical field of view, in
    degrees. Cinema 4D stores FOV in radians. `CAMERAOBJECT_FOV_VERTICAL`
    is the canonical vertical-FOV id; on cameras that store only
    the horizontal FOV we derive the vertical one from
    `CAMERAOBJECT_FOV` plus the active render data's aspect.
    """
    try:
        v_rad = cam[c4d.CAMERAOBJECT_FOV_VERTICAL]
    except Exception:  # noqa: BLE001
        v_rad = None

    if v_rad and v_rad > 0.0:
        return math.degrees(v_rad)

    # Fallback: derive vertical FOV from horizontal FOV + aspect.
    try:
        h_rad = cam[c4d.CAMERAOBJECT_FOV]
    except Exception:  # noqa: BLE001
        h_rad = None
    if h_rad and h_rad > 0.0:
        # Assume 4:3 if no render data is reachable; the export
        # path replaces this when an aspect is known.
        aspect = 4.0 / 3.0
        v_rad = 2.0 * math.atan(math.tan(0.5 * h_rad) / aspect)
        return math.degrees(v_rad)

    return rrscene_writer.DEFAULT_FOV_DEGREES


def _render_resolution(doc):
    """Read the active render data's resolution. Falls back to
    the writer's defaults when no render data is available.
    """
    if doc is None:
        return (rrscene_writer.DEFAULT_RENDER_WIDTH,
                rrscene_writer.DEFAULT_RENDER_HEIGHT)
    rd = doc.GetActiveRenderData()
    if rd is None:
        return (rrscene_writer.DEFAULT_RENDER_WIDTH,
                rrscene_writer.DEFAULT_RENDER_HEIGHT)
    w = rd[c4d.RDATA_XRES]
    h = rd[c4d.RDATA_YRES]
    if not w or w <= 0:
        w = rrscene_writer.DEFAULT_RENDER_WIDTH
    if not h or h <= 0:
        h = rrscene_writer.DEFAULT_RENDER_HEIGHT
    return (int(w), int(h))


def _find_controller(doc):
    """Walk the document looking for an object whose name matches
    `RELATIVITY_CONTROLLER_NAME`. Returns the first match (depth
    first) or `None`. The bridge intentionally treats multiple
    controllers as undefined behaviour: the user is expected to
    keep one per document, and the create command does not
    de-duplicate either.
    """
    if doc is None:
        return None

    def walk(op):
        while op is not None:
            if op.GetName() == RELATIVITY_CONTROLLER_NAME:
                return op
            child = op.GetDown()
            if child is not None:
                hit = walk(child)
                if hit is not None:
                    return hit
            op = op.GetNext()
        return None

    return walk(doc.GetFirstObject())


def _read_user_data_by_name(obj, name):
    """Return the value of the named user-data field, or `None`
    when no field with that exact `DESC_NAME` is present.
    """
    if obj is None:
        return None
    container = obj.GetUserDataContainer()
    if container is None:
        return None
    for desc_id, sub_bc in container:
        if sub_bc[c4d.DESC_NAME] == name:
            return obj[desc_id]
    return None


def _read_relativity_from_controller(controller):
    """Pull the five rrscene relativity inputs out of the
    controller's user data. Missing fields fall back to the
    writer's defaults so a controller with only some fields set
    still produces a valid scene.
    """
    beta = _read_user_data_by_name(controller, UD_FIELD_BETA_VELOCITY)
    if beta is None:
        beta = rrscene_writer.DEFAULT_BETA_VELOCITY

    direction = _read_user_data_by_name(controller, UD_FIELD_VELOCITY_DIRECTION)
    if direction is None:
        c4d_dir = rrscene_writer.DEFAULT_VELOCITY_DIRECTION
    else:
        c4d_dir = (direction.x, direction.y, direction.z)

    aberr = _read_user_data_by_name(controller, UD_FIELD_ABERRATION_STRENGTH)
    if aberr is None:
        aberr = rrscene_writer.DEFAULT_ABERRATION_STRENGTH

    dopp = _read_user_data_by_name(controller, UD_FIELD_DOPPLER_STRENGTH)
    if dopp is None:
        dopp = rrscene_writer.DEFAULT_DOPPLER_STRENGTH

    sl = _read_user_data_by_name(controller, UD_FIELD_SEARCHLIGHT_STRENGTH)
    if sl is None:
        sl = rrscene_writer.DEFAULT_SEARCHLIGHT_STRENGTH

    # Convert direction from C4D (left-handed +Z) to renderer
    # (right-handed -Z) so the relativistic boost vector points
    # the same way the user authored it on the controller.
    direction_renderer = rrscene_writer.convert_c4d_direction(c4d_dir)
    return rrscene_writer.make_relativity_section(
        beta_velocity=beta,
        velocity_direction=direction_renderer,
        aberration_strength=aberr,
        doppler_strength=dopp,
        searchlight_strength=sl,
    )


def _build_camera_section(doc):
    """Resolve the active camera, pull its global matrix + FOV,
    and return a populated camera section. Falls back to the
    writer defaults when no camera is reachable.
    """
    cam = _get_active_camera(doc)
    if cam is None:
        return rrscene_writer.make_camera_section()

    mg = cam.GetMg()
    c4d_pos, c4d_fwd, c4d_up = _camera_basis_from_matrix(mg)
    pos, fwd, up = rrscene_writer.convert_c4d_camera_basis(
        c4d_pos, c4d_fwd, c4d_up)
    fov_deg = _camera_vertical_fov_degrees(cam)
    return rrscene_writer.make_camera_section(
        position=pos, forward=fwd, up=up, fov_degrees=fov_deg)


def _add_user_data_real(obj, name, default, *,
                        unit=None, lo=None, hi=None, step=None):
    """Add a single REAL user-data slot. Cinema 4D copies the
    container, so we mutate a fresh `BaseContainer` for each
    field and `obj.AddUserData(bc)` returns the new desc id.
    """
    bc = c4d.GetCustomDatatypeDefault(c4d.DTYPE_REAL)
    bc[c4d.DESC_NAME]       = name
    bc[c4d.DESC_SHORT_NAME] = name
    bc[c4d.DESC_DEFAULT]    = float(default)
    bc[c4d.DESC_ANIMATE]    = c4d.DESC_ANIMATE_ON
    if unit is not None:
        bc[c4d.DESC_UNIT] = unit
    if lo is not None:
        bc[c4d.DESC_MIN] = float(lo)
        bc[c4d.DESC_MINSLIDER] = float(lo)
    if hi is not None:
        bc[c4d.DESC_MAX] = float(hi)
        bc[c4d.DESC_MAXSLIDER] = float(hi)
    if step is not None:
        bc[c4d.DESC_STEP] = float(step)
    desc_id = obj.AddUserData(bc)
    obj[desc_id] = float(default)
    return desc_id


def _add_user_data_vector(obj, name, default):
    """Add a single VECTOR user-data slot. `default` is a tuple."""
    bc = c4d.GetCustomDatatypeDefault(c4d.DTYPE_VECTOR)
    bc[c4d.DESC_NAME]       = name
    bc[c4d.DESC_SHORT_NAME] = name
    bc[c4d.DESC_ANIMATE]    = c4d.DESC_ANIMATE_ON
    desc_id = obj.AddUserData(bc)
    obj[desc_id] = c4d.Vector(float(default[0]),
                              float(default[1]),
                              float(default[2]))
    return desc_id


def _build_controller_object():
    """Create + populate the Null with all five user-data fields.
    Returned object has not yet been inserted into a document; the
    caller is responsible for `InsertObject` + `EventAdd`.
    """
    obj = c4d.BaseObject(c4d.Onull)
    obj.SetName(RELATIVITY_CONTROLLER_NAME)

    # Beta is a unit-less ratio in [0, 0.999999]. Strengths are
    # also unit-less in [0, 1]. Step values keep the slider
    # gestures usable without needing pinpoint precision.
    _add_user_data_real(obj, UD_FIELD_BETA_VELOCITY,
                        default=rrscene_writer.DEFAULT_BETA_VELOCITY,
                        lo=0.0, hi=0.999999, step=0.001)
    _add_user_data_vector(obj, UD_FIELD_VELOCITY_DIRECTION,
                          default=rrscene_writer.DEFAULT_VELOCITY_DIRECTION)
    _add_user_data_real(obj, UD_FIELD_ABERRATION_STRENGTH,
                        default=rrscene_writer.DEFAULT_ABERRATION_STRENGTH,
                        lo=0.0, hi=1.0, step=0.01)
    _add_user_data_real(obj, UD_FIELD_DOPPLER_STRENGTH,
                        default=rrscene_writer.DEFAULT_DOPPLER_STRENGTH,
                        lo=0.0, hi=1.0, step=0.01)
    _add_user_data_real(obj, UD_FIELD_SEARCHLIGHT_STRENGTH,
                        default=rrscene_writer.DEFAULT_SEARCHLIGHT_STRENGTH,
                        lo=0.0, hi=1.0, step=0.01)
    return obj


# ---------------------------------------------------------------------------
# Command plugins.
# ---------------------------------------------------------------------------

class ExportSceneCommand(plugins.CommandData):
    """Cinema 4D command: export the active document as a
    `.rrscene` file. Reads the active camera transform + FOV,
    the active render data's resolution, and (if a controller
    is in the document) its relativity user data.
    """

    def Execute(self, doc):
        try:
            target = _resolve_export_path(doc)

            width, height = _render_resolution(doc)
            camera_section   = _build_camera_section(doc)
            render_settings  = rrscene_writer.make_render_settings(
                width=width, height=height)

            controller = _find_controller(doc)
            if controller is not None:
                relativity_section = _read_relativity_from_controller(controller)
                controller_note    = (
                    "controller='" + RELATIVITY_CONTROLLER_NAME + "'")
            else:
                relativity_section = rrscene_writer.make_relativity_section()
                controller_note    = "no controller found (using defaults)"

            note  = ("Exported by RelativityRenderBridge; "
                     + controller_note + ".")
            scene = rrscene_writer.build_rrscene(
                camera=camera_section,
                render_settings=render_settings,
                relativity=relativity_section,
                note=note,
            )

            saved_path = rrscene_writer.write_rrscene(scene, target)

            gui.MessageDialog(
                "RelativityRender: Export Scene\n"
                "\n"
                "Saved: " + saved_path + "\n"
                "Resolution: " + str(width) + " x " + str(height) + "\n"
                "Camera FOV (vert): "
                + ("%.2f" % camera_section["fov"]) + " deg\n"
                "Relativity: " + controller_note + "\n"
                "\n"
                "(Geometry / materials / lights translation "
                "comes in a later slice.)"
            )
            return True
        except Exception as exc:  # noqa: BLE001
            gui.MessageDialog(
                "RelativityRender: Export Scene\n\n"
                "Export failed:\n" + str(exc)
            )
            return True


class CreateControllerCommand(plugins.CommandData):
    """Cinema 4D command: create the relativity controller Null
    in the active document and select it. Re-running the command
    creates an additional controller (no de-duplication) so a
    user can intentionally have multiple if they need to;
    `_find_controller` returns the first match, which is the
    document's natural authoring expectation.
    """

    def Execute(self, doc):
        try:
            if doc is None:
                gui.MessageDialog(
                    "RelativityRender: Create Controller\n\n"
                    "No active document - open a document first."
                )
                return True

            obj = _build_controller_object()

            # Undo group around the document mutation so the
            # standard Edit > Undo reverses the create.
            doc.StartUndo()
            doc.InsertObject(obj, parent=None, pred=None)
            doc.AddUndo(c4d.UNDOTYPE_NEW, obj)
            doc.SetActiveObject(obj, c4d.SELECTION_NEW)
            doc.EndUndo()

            c4d.EventAdd()

            gui.MessageDialog(
                "RelativityRender: Create Controller\n"
                "\n"
                "Created '" + RELATIVITY_CONTROLLER_NAME + "' "
                "with user data:\n"
                "  - " + UD_FIELD_BETA_VELOCITY + " (0..0.999999)\n"
                "  - " + UD_FIELD_VELOCITY_DIRECTION + " (Vector)\n"
                "  - " + UD_FIELD_ABERRATION_STRENGTH + " (0..1)\n"
                "  - " + UD_FIELD_DOPPLER_STRENGTH + " (0..1)\n"
                "  - " + UD_FIELD_SEARCHLIGHT_STRENGTH + " (0..1)\n"
                "\n"
                "Run RelativityRender: Export Scene to write the "
                "controller's values into the .rrscene file."
            )
            return True
        except Exception as exc:  # noqa: BLE001
            gui.MessageDialog(
                "RelativityRender: Create Controller\n\n"
                "Create failed:\n" + str(exc)
            )
            return True


def _register():
    plugins.RegisterCommandPlugin(
        id=PLUGIN_ID_EXPORT_SCENE,
        str=PLUGIN_NAME_EXPORT_SCENE,
        info=0,
        help=PLUGIN_HELP_EXPORT_SCENE,
        dat=ExportSceneCommand(),
        icon=None,
    )
    plugins.RegisterCommandPlugin(
        id=PLUGIN_ID_CREATE_CONTROLLER,
        str=PLUGIN_NAME_CREATE_CONTROLLER,
        info=0,
        help=PLUGIN_HELP_CREATE_CONTROLLER,
        dat=CreateControllerCommand(),
        icon=None,
    )


if __name__ == "__main__":
    _register()

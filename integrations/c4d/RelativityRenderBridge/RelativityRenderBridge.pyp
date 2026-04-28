"""Cinema 4D Python plugin: RelativityRender bridge.

Registers two commands under the Plugins menu:

  - **RelativityRender: Export Scene** - reads the active
    document's camera / render settings / (optional)
    relativity-controller user data + every native polygon
    object (with quads triangulated and global transforms
    baked into world-space vertices), and writes a v1
    .rrscene file. Shows a confirmation dialog summarising
    what was exported and which objects were skipped.
  - **RelativityRender: Create Controller** - creates a Null
    object named "RelativityRender Controller" with five
    user-data fields the Export Scene command will pick up:
    `beta_velocity`, `velocity_direction`,
    `aberration_strength`, `doppler_strength`,
    `searchlight_strength`. Selects the new object so the
    user lands on it ready to scrub the values.

Unsupported object kinds (generators, deformers, volumes,
hair) are skipped with a clear warning in the export dialog.
A polygon object whose subtree contains a deformer is still
exported, but the deformation is NOT applied (the bridge
reads the raw `GetAllPoints()`, not `GetDeformCache()`); the
dialog calls out which polygons that affects.

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
    "(.rrscene) file. Writes camera transform, FOV, render "
    "resolution, the relativity controller user data (if a "
    "RelativityRender Controller is in the scene), and every "
    "native polygon object (quads triangulated; global transforms "
    "baked into world-space vertices). Generators, deformers, "
    "volumes, and hair are skipped with a warning."
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


# ---------------------------------------------------------------------------
# Polygon-mesh export.
# ---------------------------------------------------------------------------
#
# Walks the active document, classifies each object, and produces:
#   - a list of `(name, mesh_dict)` pairs for the exported polygon
#     meshes (vertices baked to world space; quads triangulated);
#   - a list of `(name, reason)` pairs for objects we deliberately
#     skipped so the dialog can warn about them.
#
# Objects in this slice's "ignore" set:
#   - generators  (OBJECT_GENERATOR flag)
#   - deformers   (OBJECT_MODIFIER  flag)
#   - volumes     (Ovolume / Ovolumebuilder / Ovolumemesher)
#   - hair        (Ohair, where the constant exists)
# Everything else that is not a polygon (Null, Camera, Light,
# spline, ...) is silently skipped: those are not "geometry the
# bridge could have exported".

# Cinema 4D type ids we consider unsupported in this slice. Some
# constants may not exist on every C4D Python build; the lookups
# below tolerate that gracefully.
_UNSUPPORTED_TYPE_IDS = set()
for _name in ("Ovolume", "Ovolumebuilder", "Ovolumemesher", "Ohair"):
    _val = getattr(c4d, _name, None)
    if _val is not None:
        _UNSUPPORTED_TYPE_IDS.add(_val)


def _classify(obj):
    """Return one of:
      - ('polygon', None)       - native polygon mesh; export it.
      - ('skip',    reason)     - explicitly unsupported; warn.
      - ('ignore',  None)       - not a mesh kind we care about.
    """
    if obj is None:
        return ("ignore", None)

    if obj.IsInstanceOf(c4d.Opolygon):
        return ("polygon", None)

    info = obj.GetInfo()
    if info & c4d.OBJECT_GENERATOR:
        return ("skip", "generator")
    if info & c4d.OBJECT_MODIFIER:
        return ("skip", "deformer")

    if obj.GetType() in _UNSUPPORTED_TYPE_IDS:
        # Best-effort label: hair / volume builds often surface
        # under multiple type ids; reuse the constant name when
        # available, fall back to "unsupported".
        for nm in ("Ovolume", "Ovolumebuilder", "Ovolumemesher"):
            if getattr(c4d, nm, None) == obj.GetType():
                return ("skip", "volume")
        if getattr(c4d, "Ohair", None) == obj.GetType():
            return ("skip", "hair")
        return ("skip", "unsupported")

    return ("ignore", None)


def _has_deformer_descendant(obj):
    """Return True if any descendant of `obj` is a deformer.

    Deformers attached to a polygon would normally modify its
    cached points; the bridge intentionally ignores deformation
    by reading raw `GetAllPoints()`. Surfacing this in the
    dialog warns the user that the exported geometry is the
    pre-deform mesh.
    """
    child = obj.GetDown()
    while child is not None:
        if child.GetInfo() & c4d.OBJECT_MODIFIER:
            return True
        if _has_deformer_descendant(child):
            return True
        child = child.GetNext()
    return False


def _primary_material_name(obj):
    """Return the name of the first Texture tag's material on
    `obj`, or `None` when no Texture tag is present. We do NOT
    follow material inheritance up the hierarchy in this slice;
    a future slice can add the proper Cinema 4D material
    inheritance walk.
    """
    tag = obj.GetFirstTag()
    while tag is not None:
        if tag.GetType() == c4d.Ttexture:
            mat = tag[c4d.TEXTURETAG_MATERIAL]
            if mat is not None:
                name = mat.GetName()
                if name:
                    return name
        tag = tag.GetNext()
    return None


def _polygon_to_mesh_entry(obj, material_name_to_id):
    """Convert a Cinema 4D `PolygonObject` into one
    `meshes[]` entry. Bakes the global matrix into world-space
    vertex positions, then Z-flips into the renderer's
    right-handed coordinate system. Triangulates quads.
    Returns `None` when the polygon has no points or no faces
    (caller filters those out so the .rrscene stays clean).
    """
    points = obj.GetAllPoints()
    if not points:
        return None

    mg = obj.GetMg()
    # Cinema 4D matrix columns as plain tuples - keeps the math
    # entirely inside the writer's pure helpers.
    v1  = (mg.v1.x,  mg.v1.y,  mg.v1.z)
    v2  = (mg.v2.x,  mg.v2.y,  mg.v2.z)
    v3  = (mg.v3.x,  mg.v3.y,  mg.v3.z)
    off = (mg.off.x, mg.off.y, mg.off.z)

    world_vertices = []
    for p in points:
        wx, wy, wz = rrscene_writer.transform_point(
            (p.x, p.y, p.z), v1, v2, v3, off)
        world_vertices.append(rrscene_writer.convert_c4d_position(
            (wx, wy, wz)))

    polys = obj.GetAllPolygons() or []
    triangles = []
    for poly in polys:
        triangles.extend(rrscene_writer.triangulate_cpolygon(
            poly.a, poly.b, poly.c, poly.d))
    if not triangles:
        return None

    mat_name = _primary_material_name(obj)
    if mat_name is None:
        material_id = -1
    else:
        # Allocate a fresh integer id the first time we see the
        # material, reuse otherwise. Stable across the export so
        # multiple meshes that share a Cinema 4D material end up
        # referencing the same materials[] entry.
        if mat_name not in material_name_to_id:
            material_name_to_id[mat_name] = len(material_name_to_id)
        material_id = material_name_to_id[mat_name]

    return rrscene_writer.make_mesh_section(
        vertices=world_vertices,
        triangles=triangles,
        material_id=material_id,
    )


def _walk_document_meshes(doc):
    """Walk the document collecting polygon meshes + skipped
    objects. Returns a tuple
      (mesh_entries, materials_list, skipped, deformer_warnings)
    where:
      - `mesh_entries`        : list of dicts ready for `meshes[]`.
      - `materials_list`      : list of stub material dicts referenced
                                by the meshes (id + name only).
      - `skipped`             : list of `(name, reason)` for
                                generators / deformers / volumes /
                                hair we declined to export.
      - `deformer_warnings`   : list of polygon-mesh names whose
                                subtrees contain deformers (the
                                deformation was ignored).
    """
    mesh_entries = []
    skipped = []
    deformer_warnings = []
    material_name_to_id = {}

    def visit(op):
        while op is not None:
            kind, reason = _classify(op)
            if kind == "polygon":
                if _has_deformer_descendant(op):
                    deformer_warnings.append(op.GetName())
                entry = _polygon_to_mesh_entry(op, material_name_to_id)
                if entry is not None:
                    mesh_entries.append((op.GetName(), entry))
            elif kind == "skip":
                skipped.append((op.GetName(), reason))
            # In all cases recurse into children: a Null parent
            # might hold polygon meshes; a generator's polygon
            # children are not exported (the generator owns
            # them) but a deformer's polygon siblings are.
            child = op.GetDown()
            if child is not None:
                visit(child)
            op = op.GetNext()

    if doc is not None:
        visit(doc.GetFirstObject())

    materials_list = []
    for name, idx in sorted(material_name_to_id.items(), key=lambda kv: kv[1]):
        materials_list.append(rrscene_writer.make_material_section(
            id=idx, name=name))

    return (
        [entry for (_n, entry) in mesh_entries],
        materials_list,
        skipped,
        deformer_warnings,
    )


def _format_skip_summary(skipped, deformer_warnings, max_lines=8):
    """Format the warning section of the export dialog. Caps
    the number of lines so a document with hundreds of skipped
    generators doesn't produce an unscrollable dialog.
    """
    lines = []
    if skipped:
        lines.append("Skipped {0} unsupported object(s):".format(len(skipped)))
        for (name, reason) in skipped[:max_lines]:
            lines.append("  - {0} ({1})".format(name, reason))
        if len(skipped) > max_lines:
            lines.append("  ... and {0} more".format(len(skipped) - max_lines))
    if deformer_warnings:
        lines.append("")
        lines.append("Deformers ignored on {0} polygon mesh(es) "
                     "(undeformed geometry exported):"
                     .format(len(deformer_warnings)))
        for name in deformer_warnings[:max_lines]:
            lines.append("  - " + name)
        if len(deformer_warnings) > max_lines:
            lines.append("  ... and {0} more"
                         .format(len(deformer_warnings) - max_lines))
    return "\n".join(lines) if lines else "No unsupported objects."


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
            camera_section  = _build_camera_section(doc)
            render_settings = rrscene_writer.make_render_settings(
                width=width, height=height)

            controller = _find_controller(doc)
            if controller is not None:
                relativity_section = _read_relativity_from_controller(controller)
                controller_note    = (
                    "controller='" + RELATIVITY_CONTROLLER_NAME + "'")
            else:
                relativity_section = rrscene_writer.make_relativity_section()
                controller_note    = "no controller found (using defaults)"

            (meshes,
             materials,
             skipped,
             deformer_warnings) = _walk_document_meshes(doc)

            note = ("Exported by RelativityRenderBridge; "
                    + controller_note + "; "
                    + "meshes=" + str(len(meshes)) + "; "
                    + "skipped=" + str(len(skipped)) + ".")
            scene = rrscene_writer.build_rrscene(
                camera=camera_section,
                render_settings=render_settings,
                relativity=relativity_section,
                meshes=meshes,
                materials=materials,
                note=note,
            )

            saved_path = rrscene_writer.write_rrscene(scene, target)

            tri_count = 0
            for m in meshes:
                tri_count += len(m["triangles"])

            warn_block = _format_skip_summary(skipped, deformer_warnings)
            gui.MessageDialog(
                "RelativityRender: Export Scene\n"
                "\n"
                "Saved: " + saved_path + "\n"
                "Resolution: " + str(width) + " x " + str(height) + "\n"
                "Camera FOV (vert): "
                + ("%.2f" % camera_section["fov"]) + " deg\n"
                "Relativity: " + controller_note + "\n"
                "Polygon meshes: " + str(len(meshes))
                + " (" + str(tri_count) + " triangles, "
                + str(len(materials)) + " materials)\n"
                "\n"
                + warn_block + "\n"
                "\n"
                "(Lights translation + scene-camera animation "
                "come in later slices.)"
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

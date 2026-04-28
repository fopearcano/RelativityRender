"""Cinema 4D Python plugin: RelativityRender bridge.

Registers six commands under the Plugins menu:

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
  - **RelativityRender: Ping Server** - opens a TCP socket
    to the M18 renderer server (default `127.0.0.1:7777`),
    sends the `ping` command, displays the server reply.
  - **RelativityRender: Send Scene** - exports the document
    via the same `_export_to_disk` path the Export Scene
    command uses, then sends `load_scene <abs_path>` over
    the protocol so the server caches the parsed scene
    ready to render. Shows the server reply alongside the
    export summary.
  - **RelativityRender: Render Scene** - sends the `render`
    command and displays the server reply (which carries
    the absolute path of the saved PPM on success or a
    clear error otherwise). Does NOT pull pixels back over
    the wire - that's a future slice; for now the user
    opens the saved file from disk.
  - **RelativityRender: Preview Dialog** - opens a floating
    `c4d.gui.GeDialog` panel grouping the four protocol
    actions (Ping / Export / Send / Render), the host +
    port fields, the four relativity sliders (beta /
    aberration / doppler / searchlight), and a multi-line
    text area that shows the most recent server response.
    Image preview is intentionally NOT in this slice; the
    dialog displays text only.

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
import server_client    # noqa: E402  (same)
import preview_state    # noqa: E402  (same)
import image_io         # noqa: E402  (same)


# Cinema 4D plugin IDs are globally unique 32-bit integers. The
# range above ~1058600 is for real registered IDs from PluginCafe
# (https://plugincafe.maxon.net/); the values below are
# placeholders. Before a public release the project must request
# real IDs from PluginCafe and replace these constants.
PLUGIN_ID_EXPORT_SCENE       = 1058600
PLUGIN_ID_CREATE_CONTROLLER  = 1058601
PLUGIN_ID_PING_SERVER        = 1058602
PLUGIN_ID_SEND_SCENE         = 1058603
PLUGIN_ID_RENDER_SCENE       = 1058604
PLUGIN_ID_PREVIEW_DIALOG     = 1058605

PLUGIN_NAME_EXPORT_SCENE      = "RelativityRender: Export Scene"
PLUGIN_NAME_CREATE_CONTROLLER = "RelativityRender: Create Controller"
PLUGIN_NAME_PING_SERVER       = "RelativityRender: Ping Server"
PLUGIN_NAME_SEND_SCENE        = "RelativityRender: Send Scene"
PLUGIN_NAME_RENDER_SCENE      = "RelativityRender: Render Scene"
PLUGIN_NAME_PREVIEW_DIALOG    = "RelativityRender: Preview Dialog"

# Per-command timeouts. Ping is trivial; load_scene parses a
# JSON file so 10s is generous; render kicks off the GPU
# pipeline so the timeout has to be longer. All in seconds.
TIMEOUT_PING        = 2.0
TIMEOUT_LOAD_SCENE  = 10.0
TIMEOUT_RENDER      = 60.0

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
PLUGIN_HELP_PING_SERVER = (
    "Send a ping to the RelativityRender renderer server on "
    "127.0.0.1:7777 and display the reply. Confirms the server "
    "is running and reachable from Cinema 4D."
)
PLUGIN_HELP_SEND_SCENE = (
    "Export the active document to a .rrscene file and ask the "
    "renderer server to load it. The bridge uses the same export "
    "path as RelativityRender: Export Scene; the server caches "
    "the parsed scene ready for the Render Scene command."
)
PLUGIN_HELP_RENDER_SCENE = (
    "Ask the renderer server to render the most recently loaded "
    "scene. The server saves the result to disk and replies with "
    "the absolute file path; the bridge displays the path. Pixel "
    "delivery over the protocol is a follow-up slice."
)
PLUGIN_HELP_PREVIEW_DIALOG = (
    "Open the floating Preview Dialog. Groups the four protocol "
    "actions (Ping / Export / Send / Render), the host + port "
    "fields, and the four relativity sliders into a single panel "
    "that shows the most recent server response in a text area. "
    "No image preview yet."
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


def _primary_texture_material(obj):
    """Return the first Texture tag's bound `BaseMaterial`, or
    `None`. The bridge does not follow material inheritance up
    the hierarchy in this slice; a follow-up can add the proper
    Cinema 4D inheritance walk.
    """
    tag = obj.GetFirstTag()
    while tag is not None:
        if tag.GetType() == c4d.Ttexture:
            mat = tag[c4d.TEXTURETAG_MATERIAL]
            if mat is not None:
                return mat
        tag = tag.GetNext()
    return None


def _extract_standard_material_params(mat):
    """Read base colour + emission off a Cinema 4D Standard
    material (`Mmaterial`). Returns a dict with `base_color` and
    optional `emission_color` / `emission_strength` populated.

    Non-Standard material types (Physical, Redshift, Octane, ...)
    fall through with an empty dict + a `kind` of "unsupported"
    so the caller can warn the user that only the material name
    made it across.
    """
    out = {}
    if mat is None or mat.GetType() != c4d.Mmaterial:
        out["kind"] = "unsupported"
        return out
    out["kind"] = "standard"

    if mat[c4d.MATERIAL_USE_COLOR]:
        col = mat[c4d.MATERIAL_COLOR_COLOR]
        # MATERIAL_COLOR_BRIGHTNESS is a multiplier (default 1.0).
        try:
            mult = float(mat[c4d.MATERIAL_COLOR_BRIGHTNESS])
        except Exception:  # noqa: BLE001
            mult = 1.0
        out["base_color"] = (col.x * mult, col.y * mult, col.z * mult)

    # Emission: only emit when the channel is actually enabled.
    # `MATERIAL_LUMINANCE_BRIGHTNESS` is a multiplier; we use it
    # as the strength so a 0% slider produces no emission.
    if mat[c4d.MATERIAL_USE_LUMINANCE]:
        ec = mat[c4d.MATERIAL_LUMINANCE_COLOR]
        try:
            ebr = float(mat[c4d.MATERIAL_LUMINANCE_BRIGHTNESS])
        except Exception:  # noqa: BLE001
            ebr = 1.0
        if ebr > 0.0:
            out["emission_color"]    = (ec.x, ec.y, ec.z)
            out["emission_strength"] = ebr

    return out


def _viewport_fallback_color(obj):
    """If `obj` has its viewport "Display Color" enabled, return
    the (r, g, b) triple. Otherwise return `None`.

    `ID_BASEOBJECT_USECOLOR` modes:
      0 -> "Off"     (no override)
      1 -> "Auto"    (parent / inherit)
      2 -> "Always"  (use the per-object color)
      3 -> "Layer"   (use the layer's color)
    Modes 2 and 3 surface a real per-object viewport colour;
    modes 0 and 1 do not. We treat 2 / 3 as "use it" and fall
    back otherwise.
    """
    if obj is None:
        return None
    try:
        mode = int(obj[c4d.ID_BASEOBJECT_USECOLOR])
    except Exception:  # noqa: BLE001
        return None
    if mode not in (2, 3):
        return None
    try:
        col = obj[c4d.ID_BASEOBJECT_COLOR]
    except Exception:  # noqa: BLE001
        return None
    if col is None:
        return None
    return (col.x, col.y, col.z)


def _format_color_slug(rgb, ndigits=3):
    """Stable string slug for an RGB triple, used as the dedupe
    key (and the human-readable name) for viewport-fallback
    materials. Keeps the precision low so two near-identical
    colours collapse into one entry.
    """
    fmt = "%." + str(ndigits) + "f"
    return ", ".join(fmt % float(c) for c in rgb)


def _polygon_to_mesh_entry(obj, material_registry, unsupported_mat_names):
    """Convert a Cinema 4D `PolygonObject` into one
    `meshes[]` entry. Bakes the global matrix into world-space
    vertex positions, then Z-flips into the renderer's
    right-handed coordinate system. Triangulates quads.

    Resolves the polygon's material id in this priority order:
      1. First Texture tag's bound `Mmaterial` -> a deduped
         `materials[]` entry built from the standard material's
         colour + luminance channels.
      2. The polygon's viewport "Display Color" when enabled
         (`ID_BASEOBJECT_USECOLOR` mode 2/3) -> a deduped
         `materials[]` entry named `Viewport: r, g, b`.
      3. `material_id = -1` (renderer's neutral default).

    Returns `None` when the polygon has no points or no faces
    (caller filters those out so the .rrscene stays clean).

    `material_registry` is the shared `MaterialRegistry`
    instance owned by the document walker; this function calls
    `register(...)` on it but does not own the storage.

    `unsupported_mat_names` accumulates the names of Cinema 4D
    materials that were of an unsupported type (Physical,
    Redshift, Octane, ...) so the caller can warn the user
    that only the material name made it across.
    """
    points = obj.GetAllPoints()
    if not points:
        return None

    mg = obj.GetMg()
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

    material_id = -1
    mat = _primary_texture_material(obj)
    if mat is not None:
        params = _extract_standard_material_params(mat)
        if params.get("kind") == "unsupported":
            unsupported_mat_names.append(mat.GetName())
        material_id = material_registry.register_c4d_material(
            mat.GetName(), params)
    else:
        rgb = _viewport_fallback_color(obj)
        if rgb is not None:
            material_id = material_registry.register_viewport_color(rgb)

    return rrscene_writer.make_mesh_section(
        vertices=world_vertices,
        triangles=triangles,
        material_id=material_id,
    )


class MaterialRegistry(object):
    """Allocates and stores `materials[]` entries during an
    export. Two key spaces share a single integer id sequence:

      - `register_c4d_material(name, params)` keys by Cinema 4D
        material name; same name always returns the same id and
        contributes one entry built from `_extract_standard_material_params`.
      - `register_viewport_color(rgb)` keys by an RGB slug and
        emits a `Viewport: r, g, b` entry, deduping near-equal
        colours.

    The registry stores the entries in insertion order so the
    final `materials[]` array enumerates 0, 1, 2, ... contiguously.
    """

    def __init__(self):
        self._key_to_id = {}      # ("c4d", name) | ("vp", slug) -> int
        self._entries   = []      # list of dicts (parallel to the ids)

    def __len__(self):
        return len(self._entries)

    def entries(self):
        return list(self._entries)

    def register_c4d_material(self, name, params):
        key = ("c4d", name)
        if key in self._key_to_id:
            return self._key_to_id[key]
        idx = len(self._entries)
        self._key_to_id[key] = idx
        entry = rrscene_writer.make_material_section(
            id=idx,
            name=name,
            base_color=params.get("base_color"),
            emission_color=params.get("emission_color"),
            emission_strength=params.get("emission_strength"),
        )
        self._entries.append(entry)
        return idx

    def register_viewport_color(self, rgb):
        slug = _format_color_slug(rgb)
        key = ("vp", slug)
        if key in self._key_to_id:
            return self._key_to_id[key]
        idx = len(self._entries)
        self._key_to_id[key] = idx
        entry = rrscene_writer.make_material_section(
            id=idx,
            name="Viewport: " + slug,
            base_color=rgb,
        )
        self._entries.append(entry)
        return idx


def _walk_document_meshes(doc):
    """Walk the document collecting polygon meshes + skipped
    objects. Returns a tuple
      (mesh_entries, materials_list, skipped, deformer_warnings,
       unsupported_material_names)
    where:
      - `mesh_entries`               : list of dicts ready for `meshes[]`.
      - `materials_list`             : list of materials referenced
                                       by the meshes (id + name +
                                       base_color + optional emission).
      - `skipped`                    : list of `(name, reason)` for
                                       generators / deformers /
                                       volumes / hair we declined
                                       to export.
      - `deformer_warnings`          : list of polygon-mesh names
                                       whose subtrees contain
                                       deformers (the deformation
                                       was ignored).
      - `unsupported_material_names` : list of Cinema 4D material
                                       names whose type is not
                                       `Mmaterial`. Only the name
                                       made it into the export;
                                       the user is warned in the
                                       dialog.
    """
    registry = MaterialRegistry()
    mesh_entries = []
    skipped = []
    deformer_warnings = []
    unsupported_mat_names = []

    def visit(op):
        while op is not None:
            kind, reason = _classify(op)
            if kind == "polygon":
                if _has_deformer_descendant(op):
                    deformer_warnings.append(op.GetName())
                entry = _polygon_to_mesh_entry(
                    op, registry, unsupported_mat_names)
                if entry is not None:
                    mesh_entries.append((op.GetName(), entry))
            elif kind == "skip":
                skipped.append((op.GetName(), reason))
            child = op.GetDown()
            if child is not None:
                visit(child)
            op = op.GetNext()

    if doc is not None:
        visit(doc.GetFirstObject())

    return (
        [entry for (_n, entry) in mesh_entries],
        registry.entries(),
        skipped,
        deformer_warnings,
        # Dedupe while preserving order.
        list(dict.fromkeys(unsupported_mat_names)),
    )


# ---------------------------------------------------------------------------
# Light export.
# ---------------------------------------------------------------------------
#
# rrscene v1 supports two light types: "point" and "directional".
# Cinema 4D's omni / distant / parallel map cleanly:
#   - LIGHT_TYPE_OMNI       -> "point"
#   - LIGHT_TYPE_DISTANT    -> "directional"
#   - LIGHT_TYPE_PARALLEL   -> "directional"
# Area lights are degraded to a point at the area's origin, with
# the dialog flagging the lossy conversion. Spot / parallel-spot
# lights are skipped (no v1 cone metadata).

# Map from C4D light-type ids to a (rrscene_type, lossy_label).
# `lossy_label` is None when the conversion is faithful and a
# string when the dialog should flag it.
def _light_type_mapping():
    """Build the C4D-light-type -> rrscene-type mapping at
    runtime so missing constants on older C4D builds don't
    break the import. Not a module-level constant because
    `c4d.LIGHT_TYPE_*` is only available after `import c4d`.
    """
    mapping = {}
    for (attr, rr_type, lossy) in (
        ("LIGHT_TYPE_OMNI",     rrscene_writer.LIGHT_TYPE_POINT,       None),
        ("LIGHT_TYPE_DISTANT",  rrscene_writer.LIGHT_TYPE_DIRECTIONAL, None),
        ("LIGHT_TYPE_PARALLEL", rrscene_writer.LIGHT_TYPE_DIRECTIONAL, None),
        ("LIGHT_TYPE_AREA",     rrscene_writer.LIGHT_TYPE_POINT,
            "area light degraded to point (no area metadata in rrscene v1)"),
        ("LIGHT_TYPE_TUBE",     rrscene_writer.LIGHT_TYPE_POINT,
            "tube area light degraded to point"),
        ("LIGHT_TYPE_SPOT",     None,
            "spot light skipped (no spot cone in rrscene v1)"),
        ("LIGHT_TYPE_PARSPOT",  None,
            "parallel-spot light skipped (no spot cone in rrscene v1)"),
    ):
        val = getattr(c4d, attr, None)
        if val is not None:
            mapping[val] = (rr_type, lossy)
    return mapping


def _read_light_color_and_intensity(light):
    """Return (color_rgb, intensity) for a Cinema 4D light. The
    brightness slider value drives `intensity`; the colour
    drives `color`. Both are non-negative-clamped on the writer
    side, so out-of-range C4D inputs surface as zero rather than
    a parse error.
    """
    try:
        col = light[c4d.LIGHT_COLOR]
    except Exception:  # noqa: BLE001
        col = c4d.Vector(1.0, 1.0, 1.0)
    try:
        intensity = float(light[c4d.LIGHT_BRIGHTNESS])
    except Exception:  # noqa: BLE001
        intensity = 1.0
    return ((col.x, col.y, col.z), intensity)


def _build_light_entry(light, c4d_to_rr):
    """Convert one Cinema 4D `LightObject` into one
    `lights[]` entry. Returns `(entry, caveat)` where `entry` is
    either a dict or `None` (when the light is unsupported), and
    `caveat` is either `None` or a string describing a lossy
    conversion the user should know about.
    """
    try:
        type_id = int(light[c4d.LIGHT_TYPE])
    except Exception:  # noqa: BLE001
        type_id = -1

    mapping = c4d_to_rr.get(type_id)
    if mapping is None:
        return (None, "unsupported light type")
    rr_type, lossy = mapping
    if rr_type is None:
        return (None, lossy)

    color, intensity = _read_light_color_and_intensity(light)
    mg = light.GetMg()

    if rr_type == rrscene_writer.LIGHT_TYPE_POINT:
        c4d_pos = (mg.off.x, mg.off.y, mg.off.z)
        position = rrscene_writer.convert_c4d_position(c4d_pos)
        return (rrscene_writer.make_point_light(
            position=position, color=color, intensity=intensity),
            lossy)

    # Directional: photons travel along the light's local +Z in
    # Cinema 4D. World-space propagation direction = mg.v3.
    c4d_dir = (mg.v3.x, mg.v3.y, mg.v3.z)
    direction = rrscene_writer.convert_c4d_direction(c4d_dir)
    return (rrscene_writer.make_directional_light(
        direction=direction, color=color, intensity=intensity),
        lossy)


def _walk_document_lights(doc):
    """Walk the document collecting Cinema 4D LightObjects.
    Returns (light_entries, light_caveats, light_skips) where:
      - `light_entries` : list of dicts ready for `lights[]`.
      - `light_caveats` : list of `(name, message)` for lights
                          that were exported but lossily.
      - `light_skips`   : list of `(name, message)` for lights
                          that were dropped entirely.
    """
    light_entries = []
    light_caveats = []
    light_skips   = []
    if doc is None:
        return (light_entries, light_caveats, light_skips)

    olight = getattr(c4d, "Olight", None)
    if olight is None:
        return (light_entries, light_caveats, light_skips)

    c4d_to_rr = _light_type_mapping()

    def visit(op):
        while op is not None:
            if op.GetType() == olight:
                entry, caveat = _build_light_entry(op, c4d_to_rr)
                if entry is None:
                    light_skips.append((op.GetName(), caveat or "unsupported"))
                else:
                    light_entries.append(entry)
                    if caveat:
                        light_caveats.append((op.GetName(), caveat))
            child = op.GetDown()
            if child is not None:
                visit(child)
            op = op.GetNext()

    visit(doc.GetFirstObject())
    return (light_entries, light_caveats, light_skips)


def _format_skip_summary(skipped,
                         deformer_warnings,
                         light_caveats,
                         light_skips,
                         unsupported_material_names,
                         max_lines=8):
    """Format the warning section of the export dialog. Caps
    each list at `max_lines` so a document with hundreds of
    items doesn't produce an unscrollable dialog.
    """
    def _bullet_block(header, items, format_item):
        out = [header]
        for it in items[:max_lines]:
            out.append("  - " + format_item(it))
        if len(items) > max_lines:
            out.append("  ... and {0} more".format(len(items) - max_lines))
        return out

    blocks = []
    if skipped:
        blocks.append(_bullet_block(
            "Skipped {0} unsupported object(s):".format(len(skipped)),
            skipped, lambda it: "{0} ({1})".format(it[0], it[1])))
    if deformer_warnings:
        blocks.append(_bullet_block(
            "Deformers ignored on {0} polygon mesh(es) "
            "(undeformed geometry exported):".format(len(deformer_warnings)),
            deformer_warnings, lambda it: it))
    if light_skips:
        blocks.append(_bullet_block(
            "Skipped {0} light(s):".format(len(light_skips)),
            light_skips, lambda it: "{0} ({1})".format(it[0], it[1])))
    if light_caveats:
        blocks.append(_bullet_block(
            "Light caveats ({0}):".format(len(light_caveats)),
            light_caveats, lambda it: "{0}: {1}".format(it[0], it[1])))
    if unsupported_material_names:
        blocks.append(_bullet_block(
            "Material types not fully supported ({0}); "
            "only the name was exported:".format(
                len(unsupported_material_names)),
            unsupported_material_names, lambda it: it))

    if not blocks:
        return "No unsupported objects."

    lines = []
    for i, blk in enumerate(blocks):
        if i > 0:
            lines.append("")
        lines.extend(blk)
    return "\n".join(lines)


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

class _ExportResult(object):
    """Bag of values `_export_to_disk` returns. The
    ExportSceneCommand renders this into a confirmation dialog;
    the SendSceneCommand re-uses the saved path to build the
    `load_scene <path>` request and renders a shorter dialog
    with both the export summary and the server's reply.
    """

    def __init__(self,
                 saved_path,
                 width,
                 height,
                 camera_section,
                 controller_note,
                 meshes,
                 materials,
                 lights,
                 skipped,
                 deformer_warnings,
                 unsupported_material_names,
                 light_caveats,
                 light_skips):
        self.saved_path                 = saved_path
        self.width                      = width
        self.height                     = height
        self.camera_section             = camera_section
        self.controller_note            = controller_note
        self.meshes                     = meshes
        self.materials                  = materials
        self.lights                     = lights
        self.skipped                    = skipped
        self.deformer_warnings          = deformer_warnings
        self.unsupported_material_names = unsupported_material_names
        self.light_caveats              = light_caveats
        self.light_skips                = light_skips


def _export_to_disk(doc, relativity_override=None):
    """Build the .rrscene from the active document and write
    it to the resolved export path. Shared by the ExportScene
    and SendScene commands so the wire format the server sees
    is bit-for-bit what the standalone export wrote.

    `relativity_override` lets the preview dialog hand in a
    `relativity` section assembled from its own sliders -
    bypassing the controller (and the controller-not-found
    fallback). When `None` (the default) the function reads
    the controller as before, preserving the existing menu
    command behaviour.

    Returns an `_ExportResult` carrying every value the
    confirmation dialog (and, for SendScene, the load_scene
    request) needs.
    """
    target = _resolve_export_path(doc)

    width, height = _render_resolution(doc)
    camera_section  = _build_camera_section(doc)
    render_settings = rrscene_writer.make_render_settings(
        width=width, height=height)

    if relativity_override is not None:
        relativity_section = relativity_override
        controller_note    = "from preview dialog"
    else:
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
     deformer_warnings,
     unsupported_material_names) = _walk_document_meshes(doc)

    (lights,
     light_caveats,
     light_skips) = _walk_document_lights(doc)

    note = ("Exported by RelativityRenderBridge; "
            + controller_note + "; "
            + "meshes=" + str(len(meshes)) + "; "
            + "lights=" + str(len(lights)) + "; "
            + "skipped=" + str(len(skipped) + len(light_skips)) + ".")
    scene = rrscene_writer.build_rrscene(
        camera=camera_section,
        render_settings=render_settings,
        relativity=relativity_section,
        meshes=meshes,
        materials=materials,
        lights=lights,
        note=note,
    )
    saved_path = rrscene_writer.write_rrscene(scene, target)

    return _ExportResult(
        saved_path=saved_path,
        width=width, height=height,
        camera_section=camera_section,
        controller_note=controller_note,
        meshes=meshes, materials=materials, lights=lights,
        skipped=skipped, deformer_warnings=deformer_warnings,
        unsupported_material_names=unsupported_material_names,
        light_caveats=light_caveats, light_skips=light_skips)


def _format_export_summary(result):
    """Build the human-readable summary lines that follow a
    successful export. Shared between the ExportScene dialog
    and the SendScene dialog so both produce identical wording.
    """
    tri_count = 0
    for m in result.meshes:
        tri_count += len(m["triangles"])
    n_point = sum(1 for L in result.lights
                  if L.get("type") == rrscene_writer.LIGHT_TYPE_POINT)
    n_dir   = sum(1 for L in result.lights
                  if L.get("type")
                  == rrscene_writer.LIGHT_TYPE_DIRECTIONAL)
    warn_block = _format_skip_summary(
        result.skipped, result.deformer_warnings,
        result.light_caveats, result.light_skips,
        result.unsupported_material_names)
    body = (
        "Saved: " + result.saved_path + "\n"
        "Resolution: " + str(result.width) + " x "
                       + str(result.height) + "\n"
        "Camera FOV (vert): "
        + ("%.2f" % result.camera_section["fov"]) + " deg\n"
        "Relativity: " + result.controller_note + "\n"
        "Polygon meshes: " + str(len(result.meshes))
        + " (" + str(tri_count) + " triangles, "
        + str(len(result.materials)) + " materials)\n"
        "Lights: " + str(n_point) + " point, "
        + str(n_dir) + " directional\n"
        "\n"
        + warn_block
    )
    return body


class ExportSceneCommand(plugins.CommandData):
    """Cinema 4D command: export the active document as a
    `.rrscene` file. Reads the active camera transform + FOV,
    the active render data's resolution, and (if a controller
    is in the document) its relativity user data.
    """

    def Execute(self, doc):
        try:
            result = _export_to_disk(doc)
            gui.MessageDialog(
                "RelativityRender: Export Scene\n"
                "\n"
                + _format_export_summary(result)
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


# ---------------------------------------------------------------------------
# Server-talking commands.
# ---------------------------------------------------------------------------
#
# Each command opens a fresh `server_client.RenderServerClient`,
# sends one line, drains one response, and closes. That mirrors
# the v1 server's "one client at a time" accept loop and keeps
# the bridge's interaction with the renderer trivially
# auditable from a wire trace.

def _format_server_reply(response, command_label):
    """Standard wording for the dialog that follows a server
    round-trip. Always shows the status line first; tacks on
    the body when the body has more than just that line so
    multi-line replies do not lose information.
    """
    lines = ["RelativityRender: " + command_label, ""]
    if response.ok:
        lines.append("Server: " + response.status_line)
    else:
        lines.append("Server (error): " + response.status_line)

    body = response.body.strip("\n")
    extra = ""
    if body and body != response.status_line:
        # Strip the status line off the body so it's not shown
        # twice; what's left is any extra content from a
        # multi-line reply.
        rest = []
        seen_status = False
        for line in body.split("\n"):
            if not seen_status and line == response.status_line:
                seen_status = True
                continue
            rest.append(line)
        extra = "\n".join(rest).strip("\n")
    if extra:
        lines.append("")
        lines.append(extra)
    return "\n".join(lines)


def _format_server_error(exc, command_label):
    return ("RelativityRender: " + command_label + "\n"
            "\n"
            "Could not reach the renderer server.\n"
            "\n"
            + str(exc) + "\n"
            "\n"
            "Make sure RelativityRender is running with "
            "`--serve` on " + server_client.DEFAULT_HOST + ":"
            + str(server_client.DEFAULT_PORT) + ".")


class PingServerCommand(plugins.CommandData):
    """Cinema 4D command: send `ping` to the renderer server
    and display the reply (`OK pong`). Used as a connectivity
    check before issuing the heavier Send Scene / Render Scene
    commands.
    """

    def Execute(self, doc):
        try:
            client = server_client.RenderServerClient(
                timeout=TIMEOUT_PING)
            response = client.send_command("ping",
                                           timeout=TIMEOUT_PING)
            gui.MessageDialog(
                _format_server_reply(response, "Ping Server"))
            return True
        except server_client.ServerClientError as exc:
            gui.MessageDialog(_format_server_error(exc, "Ping Server"))
            return True
        except Exception as exc:  # noqa: BLE001
            gui.MessageDialog(
                "RelativityRender: Ping Server\n\n"
                "Unexpected error:\n" + str(exc))
            return True


class SendSceneCommand(plugins.CommandData):
    """Cinema 4D command: export the active document and ask
    the server to load it. The dialog shows the export summary
    + the server's `OK loaded ...` (or `ERR ...`) reply.
    """

    def Execute(self, doc):
        # Step 1: build + write the .rrscene through the same
        # path the standalone Export Scene command uses, so
        # what the server reads is bit-for-bit what the user
        # would have seen on disk.
        try:
            result = _export_to_disk(doc)
        except Exception as exc:  # noqa: BLE001
            gui.MessageDialog(
                "RelativityRender: Send Scene\n\n"
                "Export failed:\n" + str(exc))
            return True

        # Step 2: tell the server to load_scene <abs_path>.
        cmd = "load_scene " + result.saved_path
        try:
            client = server_client.RenderServerClient(
                timeout=TIMEOUT_LOAD_SCENE)
            response = client.send_command(cmd,
                                           timeout=TIMEOUT_LOAD_SCENE)
        except server_client.ServerClientError as exc:
            # The export already succeeded - tell the user
            # explicitly so they know the on-disk file is fine
            # and they can retry the server connection.
            gui.MessageDialog(
                _format_server_error(exc, "Send Scene")
                + "\n\nThe .rrscene file was still written to:\n"
                + result.saved_path)
            return True
        except Exception as exc:  # noqa: BLE001
            gui.MessageDialog(
                "RelativityRender: Send Scene\n\n"
                "Unexpected error contacting server:\n" + str(exc))
            return True

        gui.MessageDialog(
            "RelativityRender: Send Scene\n"
            "\n"
            + _format_export_summary(result) + "\n"
            "\n"
            "Server reply (load_scene):\n"
            + ("OK " if response.ok else "ERR ")
            + response.status_line)
        return True


class RenderSceneCommand(plugins.CommandData):
    """Cinema 4D command: ask the server to render the most
    recently loaded scene. The server replies with the saved
    image's absolute path on success.
    """

    def Execute(self, doc):
        try:
            client = server_client.RenderServerClient(
                timeout=TIMEOUT_RENDER)
            response = client.send_command("render",
                                           timeout=TIMEOUT_RENDER)
        except server_client.ServerClientError as exc:
            gui.MessageDialog(
                _format_server_error(exc, "Render Scene"))
            return True
        except Exception as exc:  # noqa: BLE001
            gui.MessageDialog(
                "RelativityRender: Render Scene\n\n"
                "Unexpected error:\n" + str(exc))
            return True

        gui.MessageDialog(
            _format_server_reply(response, "Render Scene"))
        return True


# ---------------------------------------------------------------------------
# Preview dialog.
# ---------------------------------------------------------------------------
#
# Floating GeDialog grouping the four server-talking actions
# (Ping / Export / Send / Render), a host + port editor, and
# the four relativity sliders. The display is text-only at
# this slice: every server reply lands in a multi-line read-
# only text area; no bitmap output yet.
#
# The dialog is its own state container - slider values do
# NOT round-trip into the C4D scene's relativity controller.
# Send Scene from the dialog uses the dialog's slider values
# (via `_export_to_disk(doc, relativity_override=...)`); the
# menu commands continue to read the controller as before.
# Keeping the two paths independent means a user comparing
# slider tweaks against a saved controller can do so without
# the dialog clobbering the document.

# Element IDs. Any positive int unique within the dialog. Do
# NOT use the global plugin id range here - GeDialog element
# ids share a separate namespace.
_EID_GROUP_SERVER       = 1000
_EID_HOST               = 1001
_EID_PORT               = 1002
_EID_PING               = 1003

_EID_GROUP_ACTIONS      = 1010
_EID_EXPORT             = 1011
_EID_SEND               = 1012
_EID_RENDER             = 1013

_EID_GROUP_RELATIVITY   = 1020
_EID_BETA               = 1021
_EID_ABERRATION         = 1022
_EID_DOPPLER            = 1023
_EID_SEARCHLIGHT        = 1024

_EID_GROUP_RESPONSE     = 1030
_EID_RESPONSE           = 1031

_EID_GROUP_PREVIEW      = 1040
_EID_PREVIEW            = 1041

# Names of the scene objects the fallback preview path
# creates / updates. Both are kept stable across runs so a
# user re-rendering sees the same Plane / Material instead of
# a forest of duplicates.
PREVIEW_PLANE_NAME    = "RelativityRender Preview"
PREVIEW_MATERIAL_NAME = "RelativityRender Preview Material"


def _find_or_create_preview_plane(doc):
    """Find a top-level Plane object named
    `PREVIEW_PLANE_NAME` in the document; create one if
    missing. Returns the BaseObject. Plane is sized 200x150
    on creation so it's visible in a default scene; the user
    can rescale freely afterwards.
    """
    plane = doc.SearchObject(PREVIEW_PLANE_NAME)
    if plane is not None and plane.GetType() == c4d.Oplane:
        return plane

    plane = c4d.BaseObject(c4d.Oplane)
    plane.SetName(PREVIEW_PLANE_NAME)
    try:
        plane[c4d.PRIM_PLANE_WIDTH]   = 200.0
        plane[c4d.PRIM_PLANE_HEIGHT]  = 150.0
        plane[c4d.PRIM_PLANE_SUBW]    = 1
        plane[c4d.PRIM_PLANE_SUBH]    = 1
    except Exception:  # noqa: BLE001
        # Older C4D versions occasionally rename plane
        # parameters. Defaults are fine; skip the resize.
        pass
    # The default Plane lies in the XZ plane; rotate it so it
    # faces +Z (toward a viewer at the origin looking forward)
    # which makes the texture visible without further setup.
    try:
        plane.SetRelRot(c4d.Vector(0.0, 0.0, math.radians(-90.0)))
    except Exception:  # noqa: BLE001
        pass
    doc.InsertObject(plane)
    return plane


def _find_or_create_preview_material(doc, image_path):
    """Find a Standard material named
    `PREVIEW_MATERIAL_NAME`; create one if missing. Either
    way set its color-channel bitmap shader to `image_path`
    and update the material so a re-render of the preview
    refreshes in the viewport.
    """
    mat = None
    cur = doc.GetFirstMaterial()
    while cur is not None:
        if cur.GetName() == PREVIEW_MATERIAL_NAME:
            mat = cur
            break
        cur = cur.GetNext()
    if mat is None:
        mat = c4d.BaseMaterial(c4d.Mmaterial)
        mat.SetName(PREVIEW_MATERIAL_NAME)
        doc.InsertMaterial(mat)

    mat[c4d.MATERIAL_USE_COLOR] = True

    # Reuse an existing bitmap shader when possible so we
    # don't leak shaders on repeated renders. Cinema 4D
    # `BaseShader` has no public delete-from-material API in
    # all versions, so re-using the existing one is the
    # robust path.
    shader = mat[c4d.MATERIAL_COLOR_SHADER]
    if shader is None or shader.GetType() != c4d.Xbitmap:
        shader = c4d.BaseList2D(c4d.Xbitmap)
        mat.InsertShader(shader)
        mat[c4d.MATERIAL_COLOR_SHADER] = shader

    shader[c4d.BITMAPSHADER_FILENAME] = image_path
    mat.Message(c4d.MSG_UPDATE)
    mat.Update(True, True)
    return mat


def _ensure_texture_tag(plane, mat):
    """Make sure `plane` has exactly one Texture tag pointing
    at `mat`. Reuses an existing Texture tag for `mat` when
    found; otherwise adds a new one.
    """
    tag = plane.GetFirstTag()
    while tag is not None:
        if tag.GetType() == c4d.Ttexture:
            if tag[c4d.TEXTURETAG_MATERIAL] == mat:
                return tag
        tag = tag.GetNext()
    new_tag = plane.MakeTag(c4d.Ttexture)
    new_tag[c4d.TEXTURETAG_MATERIAL] = mat
    return new_tag


class _PreviewArea(c4d.gui.GeUserArea):
    """Bitmap blitter for the dialog's preview area.

    Cinema 4D draws a `GeUserArea` by calling `DrawMsg` with
    the area's pixel rect. We blit the most recently loaded
    bitmap into the rect, fitted while preserving aspect.
    Background is filled with the C4D background colour so an
    empty (no-bitmap) area looks like the rest of the dialog.
    """

    def __init__(self):
        super(_PreviewArea, self).__init__()
        self._bitmap = None

    def set_bitmap(self, bmp):
        self._bitmap = bmp
        try:
            self.Redraw()
        except Exception:  # noqa: BLE001
            # GeUserArea.Redraw() can raise if the area is
            # not yet attached to a layout; ignore so the
            # dialog still opens cleanly on first use.
            pass

    def DrawMsg(self, x1, y1, x2, y2, msg_ref):
        try:
            self.OffScreenOn()
            self.DrawSetPen(c4d.COLOR_BG)
            self.DrawRectangle(x1, y1, x2, y2)
            bmp = self._bitmap
            if bmp is None:
                return

            area_w = x2 - x1
            area_h = y2 - y1
            if area_w <= 0 or area_h <= 0:
                return

            img_w = bmp.GetBw()
            img_h = bmp.GetBh()
            if img_w <= 0 or img_h <= 0:
                return

            # Fit while preserving aspect; centre.
            scale = min(float(area_w) / img_w,
                        float(area_h) / img_h)
            draw_w = max(1, int(img_w * scale))
            draw_h = max(1, int(img_h * scale))
            ox = x1 + (area_w - draw_w) // 2
            oy = y1 + (area_h - draw_h) // 2

            self.DrawBitmap(bmp,
                            ox, oy, draw_w, draw_h,
                            0, 0, img_w, img_h,
                            c4d.BMP_NORMAL)
        except Exception:  # noqa: BLE001
            # A draw failure must not abort the dialog event
            # loop; swallow and let the next paint try again.
            pass


class PreviewDialog(c4d.gui.GeDialog):
    """Floating text-only preview dialog.

    Layout (top to bottom):

      Server   :  [ host ] [ port ] [ Ping ]
      Actions  :  [ Export ] [ Send ] [ Render ]
      Relativity:
        beta        :  | slider |
        aberration  :  | slider |
        doppler     :  | slider |
        searchlight :  | slider |
      Response :  multi-line text (read-only)

    Each button performs its action SYNCHRONOUSLY on the
    main C4D thread. That blocks the dialog until the server
    replies; for ping (2s) / load_scene (10s) the wait is
    fine, for render (60s) the user sees the C4D UI freeze.
    Threaded async lives behind the eventual progress / cancel
    work; it is NOT in this slice's scope.
    """

    def __init__(self):
        super(PreviewDialog, self).__init__()
        # Cached so the dialog can recover the most recent
        # response without forcing the user to scroll back.
        self._last_response_text = ""
        # GeUserArea instance owned by the dialog; populated
        # at CreateLayout time and re-used across rebuilds.
        self._preview_area = _PreviewArea()
        # Last successfully loaded preview bitmap. Held by the
        # dialog (not just the GeUserArea) so the bitmap stays
        # alive across re-layouts and the next render can
        # release it deterministically.
        self._preview_bitmap = None

    # --- Lifecycle hooks ----------------------------------------------------

    def CreateLayout(self):
        self.SetTitle("RelativityRender Preview")

        # Server row.
        if self.GroupBegin(id=_EID_GROUP_SERVER,
                           flags=c4d.BFH_SCALEFIT,
                           cols=5, rows=1, title="Server"):
            self.GroupBorderSpace(4, 4, 4, 4)
            self.AddStaticText(id=0, flags=0, name="Host:")
            self.AddEditText(id=_EID_HOST,
                             flags=c4d.BFH_SCALEFIT, initw=140)
            self.AddStaticText(id=0, flags=0, name="Port:")
            self.AddEditNumberArrows(id=_EID_PORT,
                                     flags=c4d.BFH_LEFT, initw=80)
            self.AddButton(id=_EID_PING,
                           flags=c4d.BFH_RIGHT, initw=80, name="Ping")
        self.GroupEnd()

        # Actions row.
        if self.GroupBegin(id=_EID_GROUP_ACTIONS,
                           flags=c4d.BFH_SCALEFIT,
                           cols=3, rows=1, title="Actions"):
            self.GroupBorderSpace(4, 4, 4, 4)
            self.AddButton(id=_EID_EXPORT,
                           flags=c4d.BFH_SCALEFIT, name="Export Scene")
            self.AddButton(id=_EID_SEND,
                           flags=c4d.BFH_SCALEFIT, name="Send Scene")
            self.AddButton(id=_EID_RENDER,
                           flags=c4d.BFH_SCALEFIT, name="Render")
        self.GroupEnd()

        # Sliders.
        if self.GroupBegin(id=_EID_GROUP_RELATIVITY,
                           flags=c4d.BFH_SCALEFIT,
                           cols=2, rows=4, title="Relativity"):
            self.GroupBorderSpace(4, 4, 4, 4)
            self.AddStaticText(id=0, flags=0, name="beta")
            self.AddEditSlider(id=_EID_BETA,
                               flags=c4d.BFH_SCALEFIT)
            self.AddStaticText(id=0, flags=0, name="aberration")
            self.AddEditSlider(id=_EID_ABERRATION,
                               flags=c4d.BFH_SCALEFIT)
            self.AddStaticText(id=0, flags=0, name="doppler")
            self.AddEditSlider(id=_EID_DOPPLER,
                               flags=c4d.BFH_SCALEFIT)
            self.AddStaticText(id=0, flags=0, name="searchlight")
            self.AddEditSlider(id=_EID_SEARCHLIGHT,
                               flags=c4d.BFH_SCALEFIT)
        self.GroupEnd()

        # Response area.
        if self.GroupBegin(id=_EID_GROUP_RESPONSE,
                           flags=c4d.BFH_SCALEFIT | c4d.BFV_SCALEFIT,
                           cols=1, rows=1, title="Server Response"):
            self.GroupBorderSpace(4, 4, 4, 4)
            self.AddMultiLineEditText(
                id=_EID_RESPONSE,
                flags=c4d.BFH_SCALEFIT | c4d.BFV_SCALEFIT,
                inith=100,
                style=c4d.DR_MULTILINE_READONLY)
        self.GroupEnd()

        # Preview area. A `GeUserArea` we own + attach; the
        # `_PreviewArea.DrawMsg` callback paints the most
        # recent rendered bitmap fitted into the rect. Empty
        # until the first successful render.
        if self.GroupBegin(id=_EID_GROUP_PREVIEW,
                           flags=c4d.BFH_SCALEFIT | c4d.BFV_SCALEFIT,
                           cols=1, rows=1, title="Preview"):
            self.GroupBorderSpace(4, 4, 4, 4)
            self.AddUserArea(
                id=_EID_PREVIEW,
                flags=c4d.BFH_SCALEFIT | c4d.BFV_SCALEFIT,
                initw=400, inith=240)
            self.AttachUserArea(self._preview_area, _EID_PREVIEW)
        self.GroupEnd()

        return True

    def InitValues(self):
        self.SetString(_EID_HOST, preview_state.DEFAULT_HOST)
        self.SetInt32(_EID_PORT,  preview_state.DEFAULT_PORT,
                      preview_state.MIN_PORT, preview_state.MAX_PORT)

        # Slider ranges. step=0.001 keeps the spinner usable
        # without adding a noticeable lag on the multi-decimal
        # values.
        self.SetReal(_EID_BETA,        preview_state.DEFAULT_BETA,
                     0.0, 0.999, 0.001, c4d.FORMAT_FLOAT)
        self.SetReal(_EID_ABERRATION,  preview_state.DEFAULT_ABERRATION,
                     0.0, 1.0, 0.001, c4d.FORMAT_FLOAT)
        self.SetReal(_EID_DOPPLER,     preview_state.DEFAULT_DOPPLER,
                     0.0, 1.0, 0.001, c4d.FORMAT_FLOAT)
        self.SetReal(_EID_SEARCHLIGHT, preview_state.DEFAULT_SEARCHLIGHT,
                     0.0, 1.0, 0.001, c4d.FORMAT_FLOAT)

        self.SetMultiLineEditText(_EID_RESPONSE, "")
        return True

    def Command(self, id, msg):
        if id == _EID_PING:
            self._on_ping()
        elif id == _EID_EXPORT:
            self._on_export()
        elif id == _EID_SEND:
            self._on_send()
        elif id == _EID_RENDER:
            self._on_render()
        return True

    # --- Helpers ------------------------------------------------------------

    def _read_host_port(self):
        host = self.GetString(_EID_HOST)
        port = self.GetInt32(_EID_PORT)
        return (host, port)

    def _read_relativity(self):
        return preview_state.make_relativity_from_dialog(
            beta        = self.GetReal(_EID_BETA),
            aberration  = self.GetReal(_EID_ABERRATION),
            doppler     = self.GetReal(_EID_DOPPLER),
            searchlight = self.GetReal(_EID_SEARCHLIGHT),
        )

    def _append_response_line(self, line):
        """Append a line to the response text area, keeping
        the whole history visible. The newest line is at the
        bottom (read order) so a user can re-issue commands
        and see them stack up.
        """
        text = self._last_response_text
        if text:
            text += "\n"
        text += str(line)
        self._last_response_text = text
        self.SetMultiLineEditText(_EID_RESPONSE, text)

    def _validate_server_target(self):
        host, port = self._read_host_port()
        ok, msg = preview_state.validate_host(host)
        if not ok:
            self._append_response_line("[error] " + msg)
            return None
        ok, msg = preview_state.validate_port(port)
        if not ok:
            self._append_response_line("[error] " + msg)
            return None
        return (host, port)

    # --- Action handlers ---------------------------------------------------

    def _on_ping(self):
        target = self._validate_server_target()
        if target is None:
            return
        host, port = target
        try:
            client = server_client.RenderServerClient(
                host=host, port=port, timeout=TIMEOUT_PING)
            response = client.send_command("ping",
                                           timeout=TIMEOUT_PING)
            self._append_response_line(
                preview_state.format_server_reply(response, "ping"))
        except server_client.ServerClientError as exc:
            self._append_response_line(
                preview_state.format_connection_error(
                    exc, "ping", host, port))
        except Exception as exc:  # noqa: BLE001
            self._append_response_line(
                "[ping] unexpected error: " + str(exc))

    def _on_export(self):
        doc = c4d.documents.GetActiveDocument()
        try:
            result = _export_to_disk(
                doc, relativity_override=self._read_relativity())
            self._append_response_line(
                "[export] OK saved " + result.saved_path)
        except Exception as exc:  # noqa: BLE001
            self._append_response_line(
                "[export] failed: " + str(exc))

    def _on_send(self):
        target = self._validate_server_target()
        if target is None:
            return
        host, port = target

        doc = c4d.documents.GetActiveDocument()
        try:
            result = _export_to_disk(
                doc, relativity_override=self._read_relativity())
        except Exception as exc:  # noqa: BLE001
            self._append_response_line(
                "[send] export failed: " + str(exc))
            return

        try:
            client = server_client.RenderServerClient(
                host=host, port=port, timeout=TIMEOUT_LOAD_SCENE)
            response = client.send_command(
                "load_scene " + result.saved_path,
                timeout=TIMEOUT_LOAD_SCENE)
            self._append_response_line(
                preview_state.format_server_reply(response, "send"))
        except server_client.ServerClientError as exc:
            self._append_response_line(
                preview_state.format_connection_error(
                    exc, "send", host, port)
                + " (file still on disk: " + result.saved_path + ")")
        except Exception as exc:  # noqa: BLE001
            self._append_response_line(
                "[send] unexpected error: " + str(exc))

    def _on_render(self):
        target = self._validate_server_target()
        if target is None:
            return
        host, port = target

        response = None
        try:
            client = server_client.RenderServerClient(
                host=host, port=port, timeout=TIMEOUT_RENDER)
            response = client.send_command("render",
                                           timeout=TIMEOUT_RENDER)
            self._append_response_line(
                preview_state.format_server_reply(response, "render"))
        except server_client.ServerClientError as exc:
            self._append_response_line(
                preview_state.format_connection_error(
                    exc, "render", host, port))
            return
        except Exception as exc:  # noqa: BLE001
            self._append_response_line(
                "[render] unexpected error: " + str(exc))
            return

        # Only attempt the image display path on a clean OK.
        # Every step below is best-effort: failures land in
        # the response area, never as Python exceptions out
        # of the dialog event loop.
        if response is None or not response.ok:
            return
        w, h, ppm_path = preview_state.parse_render_response(
            response.status_line)
        if not ppm_path:
            self._append_response_line(
                "[render] could not parse path from server reply")
            return

        self._show_rendered_image(ppm_path)

    # --- Rendered-image display (post-render) -----------------------------

    def _show_rendered_image(self, ppm_path):
        """Two-stage image display.

        Stage 1 (primary): convert the PPM to BMP, load via
        `c4d.bitmaps.BaseBitmap.InitWith`, hand to the
        `_PreviewArea` so the dialog paints it.

        Stage 2 (fallback): if any of {file missing, PPM
        decode, BMP write, bitmap init} fail, create-or-update
        a "RelativityRender Preview" plane in the active
        document with the BMP applied as a texture so the
        user still sees the image somewhere.

        Each step is wrapped: a failure in stage 1 falls
        through to stage 2; a failure in stage 2 logs a line
        in the response area but doesn't otherwise raise.
        """
        if not os.path.isfile(ppm_path):
            self._append_response_line(
                "[render] saved image not found on disk: " + ppm_path
                + " (server saved to a path the bridge cannot read)")
            return

        # Step A: convert PPM -> BMP.
        bmp_path = ""
        try:
            bmp_path = image_io.convert_ppm_to_bmp(ppm_path)
            self._append_response_line(
                "[render] converted to BMP: " + bmp_path)
        except image_io.PpmDecodeError as exc:
            self._append_response_line(
                "[render] could not decode PPM: " + str(exc))
            return
        except Exception as exc:  # noqa: BLE001
            self._append_response_line(
                "[render] PPM->BMP conversion failed: " + str(exc))
            return

        # Step B: try the in-dialog preview (primary path).
        if self._try_load_into_dialog(bmp_path):
            return

        # Step C: fallback - scene plane.
        self._fallback_to_scene_plane(bmp_path)

    def _try_load_into_dialog(self, bmp_path):
        """Returns True iff the bitmap loaded successfully and
        the dialog will display it. False signals the caller
        to try the fallback path.
        """
        try:
            bmp = c4d.bitmaps.BaseBitmap()
            init = bmp.InitWith(bmp_path)
            # `InitWith` returns either an int or a tuple
            # depending on the C4D Python build; tolerate both.
            if isinstance(init, tuple):
                rc = init[0]
            else:
                rc = init
            if rc != c4d.IMAGERESULT_OK:
                self._append_response_line(
                    "[render] BaseBitmap.InitWith failed (rc="
                    + str(rc) + "); falling back to scene plane")
                return False
            self._preview_bitmap = bmp
            self._preview_area.set_bitmap(bmp)
            self._append_response_line(
                "[render] preview displayed in dialog "
                "(" + str(bmp.GetBw()) + "x" + str(bmp.GetBh()) + ")")
            return True
        except Exception as exc:  # noqa: BLE001
            self._append_response_line(
                "[render] in-dialog preview failed: " + str(exc)
                + "; falling back to scene plane")
            return False

    def _fallback_to_scene_plane(self, image_path):
        """Create or update a "RelativityRender Preview" plane
        in the active document with `image_path` applied as a
        Color-channel bitmap shader. The plane is parked at
        the world origin in front of the camera; the user can
        move it freely afterwards (the bridge updates the
        bitmap, not the transform, on subsequent renders).
        """
        try:
            doc = c4d.documents.GetActiveDocument()
            if doc is None:
                self._append_response_line(
                    "[render] no active document for fallback plane")
                return

            mat = _find_or_create_preview_material(doc, image_path)
            plane = _find_or_create_preview_plane(doc)
            _ensure_texture_tag(plane, mat)

            c4d.EventAdd()
            self._append_response_line(
                "[render] preview applied to scene plane '"
                + PREVIEW_PLANE_NAME + "'")
        except Exception as exc:  # noqa: BLE001
            self._append_response_line(
                "[render] scene-plane fallback failed: " + str(exc))


# Keep a single dialog instance around for the life of the
# Cinema 4D session. Re-opening the menu entry surfaces the
# same window (with its accumulated response history) instead
# of creating a fresh one each time.
_preview_dialog_singleton = None


class OpenPreviewDialogCommand(plugins.CommandData):
    """Cinema 4D command: open the preview dialog. Async
    (`DLG_TYPE_ASYNC`) so the dialog stays open while the
    user keeps working in C4D; closing the window does not
    end the C4D session.
    """

    def Execute(self, doc):
        global _preview_dialog_singleton
        try:
            if _preview_dialog_singleton is None:
                _preview_dialog_singleton = PreviewDialog()
            _preview_dialog_singleton.Open(
                dlgtype=c4d.DLG_TYPE_ASYNC,
                pluginid=PLUGIN_ID_PREVIEW_DIALOG,
                defaultw=420, defaulth=380)
            return True
        except Exception as exc:  # noqa: BLE001
            gui.MessageDialog(
                "RelativityRender: Preview Dialog\n\n"
                "Could not open the dialog:\n" + str(exc))
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
    plugins.RegisterCommandPlugin(
        id=PLUGIN_ID_PING_SERVER,
        str=PLUGIN_NAME_PING_SERVER,
        info=0,
        help=PLUGIN_HELP_PING_SERVER,
        dat=PingServerCommand(),
        icon=None,
    )
    plugins.RegisterCommandPlugin(
        id=PLUGIN_ID_SEND_SCENE,
        str=PLUGIN_NAME_SEND_SCENE,
        info=0,
        help=PLUGIN_HELP_SEND_SCENE,
        dat=SendSceneCommand(),
        icon=None,
    )
    plugins.RegisterCommandPlugin(
        id=PLUGIN_ID_RENDER_SCENE,
        str=PLUGIN_NAME_RENDER_SCENE,
        info=0,
        help=PLUGIN_HELP_RENDER_SCENE,
        dat=RenderSceneCommand(),
        icon=None,
    )
    plugins.RegisterCommandPlugin(
        id=PLUGIN_ID_PREVIEW_DIALOG,
        str=PLUGIN_NAME_PREVIEW_DIALOG,
        info=0,
        help=PLUGIN_HELP_PREVIEW_DIALOG,
        dat=OpenPreviewDialogCommand(),
        icon=None,
    )


if __name__ == "__main__":
    _register()

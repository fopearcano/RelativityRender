#!/usr/bin/env python3
"""Hand-rolled assertion runner for `rrscene_writer.py`.

Runs under stock `python3` - no Cinema 4D required. The .pyp
plugin imports the same `rrscene_writer` module the tests
exercise, so everything below validates the production code
path that lands inside Cinema 4D.

Verifies, end to end:

  - Per-section builders (`make_render_settings`,
    `make_camera_section`, `make_relativity_section`) produce
    dicts shaped exactly like the host parser expects.
  - C4D <-> renderer coordinate conversion (Z-flip on positions
    and direction vectors).
  - Foundation-slice helpers (`build_empty_rrscene`,
    `write_empty_rrscene`) still round-trip through JSON.
  - Top-level scene assembly (`build_rrscene`,
    `serialize_rrscene`, `write_rrscene`) builds a valid v1
    scene from explicit per-section inputs.
  - The module does not import `c4d`.
"""

from __future__ import annotations

import json
import math
import os
import sys
import tempfile

# Add the parent directory so we can import the writer module
# without installing it.
_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGIN_DIR = os.path.dirname(_HERE)
if _PLUGIN_DIR not in sys.path:
    sys.path.insert(0, _PLUGIN_DIR)

import rrscene_writer  # noqa: E402

g_total  = 0
g_failed = 0


def check(cond, message):
    global g_total, g_failed
    g_total += 1
    if not cond:
        g_failed += 1
        sys.stderr.write("FAIL: " + message + "\n")


def almost_equal(a, b, eps=1.0e-9):
    return math.isclose(a, b, rel_tol=eps, abs_tol=eps)


# ---------------------------------------------------------------------------
# Section builders.
# ---------------------------------------------------------------------------

def test_render_settings_defaults_and_clamps():
    rs = rrscene_writer.make_render_settings()
    check(rs == {"width": 640, "height": 480}, "render defaults")

    rs = rrscene_writer.make_render_settings(width=1920, height=1080)
    check(rs == {"width": 1920, "height": 1080}, "explicit resolution")

    # Non-positive / zero / negative values fall back to v1
    # defaults so the saved file is never invalid even when the
    # C4D side hands us a degenerate render configuration.
    rs = rrscene_writer.make_render_settings(width=0, height=-5)
    check(rs == {"width": 640, "height": 480}, "non-positive falls back")

    # Float inputs are coerced to int.
    rs = rrscene_writer.make_render_settings(width=1280.4, height=720.9)
    check(rs == {"width": 1280, "height": 720}, "float coercion")


def test_camera_section_defaults_and_clamps():
    cs = rrscene_writer.make_camera_section()
    check(cs["position"] == [0.0, 0.0, 0.0], "default position")
    check(cs["forward"]  == [0.0, 0.0, -1.0], "default forward")
    check(cs["up"]       == [0.0, 1.0, 0.0],  "default up")
    check(cs["fov"]      == 50.0,             "default fov")

    cs = rrscene_writer.make_camera_section(
        position=(1.5, 2.0, 3.0),
        forward=(0.0, 0.0, 1.0),
        up=(0.0, 1.0, 0.0),
        fov_degrees=24.0,
    )
    check(cs["position"] == [1.5, 2.0, 3.0], "explicit position")
    check(cs["forward"]  == [0.0, 0.0, 1.0], "explicit forward")
    check(cs["fov"]      == 24.0,            "explicit fov")

    # FOVs at or beyond the parser's open interval (0, 180) fall
    # back to the writer default so the host doesn't reject the
    # file.
    for bad_fov in (-5.0, 0.0, 180.0, 200.0):
        cs = rrscene_writer.make_camera_section(fov_degrees=bad_fov)
        check(cs["fov"] == 50.0,
              "fov %r should fall back to default" % bad_fov)


def test_relativity_section_defaults_and_clamps():
    rel = rrscene_writer.make_relativity_section()
    check(rel["beta_velocity"]        == 0.0,             "default beta")
    check(rel["velocity_direction"]   == [0.0, 0.0, -1.0], "default dir")
    check(rel["aberration_strength"]  == 1.0,              "default aberration")
    check(rel["doppler_strength"]     == 1.0,              "default doppler")
    check(rel["searchlight_strength"] == 1.0,              "default searchlight")

    rel = rrscene_writer.make_relativity_section(
        beta_velocity=0.7,
        velocity_direction=(1.0, 0.0, 0.0),
        aberration_strength=0.5,
        doppler_strength=0.25,
        searchlight_strength=0.0,
    )
    check(rel["beta_velocity"]        == 0.7,             "explicit beta")
    check(rel["velocity_direction"]   == [1.0, 0.0, 0.0], "explicit dir")
    check(rel["aberration_strength"]  == 0.5,             "explicit aberr")
    check(rel["doppler_strength"]     == 0.25,            "explicit doppler")
    check(rel["searchlight_strength"] == 0.0,             "explicit sl")

    # Beta is clamped to [0, 0.999999].
    rel = rrscene_writer.make_relativity_section(beta_velocity=1.5)
    check(rel["beta_velocity"] == 0.999999, "beta high clamp")
    rel = rrscene_writer.make_relativity_section(beta_velocity=-0.2)
    check(rel["beta_velocity"] == 0.0,      "beta low clamp")

    # Strengths are clamped to [0, 1].
    rel = rrscene_writer.make_relativity_section(
        aberration_strength=3.0, doppler_strength=-1.0,
        searchlight_strength=2.0,
    )
    check(rel["aberration_strength"]  == 1.0, "aberration high clamp")
    check(rel["doppler_strength"]     == 0.0, "doppler low clamp")
    check(rel["searchlight_strength"] == 1.0, "searchlight high clamp")


# ---------------------------------------------------------------------------
# Coordinate conversion: C4D (left-handed +Z forward) <-> renderer
# (right-handed -Z forward).
# ---------------------------------------------------------------------------

def test_c4d_position_z_flip():
    # Identity origin maps to identity origin.
    check(rrscene_writer.convert_c4d_position((0.0, 0.0, 0.0))
          == (0.0, 0.0, 0.0),
          "origin position")
    # C4D camera 10 units 'forward' (+Z) lands at -Z 10 in the
    # renderer's right-handed space.
    check(rrscene_writer.convert_c4d_position((0.0, 0.0, 10.0))
          == (0.0, 0.0, -10.0),
          "+Z 10 -> -Z 10")
    # X / Y are preserved.
    check(rrscene_writer.convert_c4d_position((1.0, 2.0, -3.0))
          == (1.0, 2.0, 3.0),
          "x/y preserved, z flipped")


def test_c4d_direction_z_flip():
    check(rrscene_writer.convert_c4d_direction((0.0, 0.0, 1.0))
          == (0.0, 0.0, -1.0),
          "C4D forward -> renderer -Z")
    check(rrscene_writer.convert_c4d_direction((0.0, 1.0, 0.0))
          == (0.0, 1.0, 0.0),
          "+Y up unchanged")


def test_c4d_camera_basis_default_matches_renderer_default():
    # An identity-pose camera in C4D (position 0, forward +Z, up
    # +Y) must map to the renderer's identity camera (position 0,
    # forward -Z, up +Y).
    pos, fwd, up = rrscene_writer.convert_c4d_camera_basis(
        position=(0.0, 0.0, 0.0),
        forward=(0.0, 0.0, 1.0),
        up=(0.0, 1.0, 0.0),
    )
    check(pos == (0.0, 0.0, 0.0), "identity position")
    check(fwd == (0.0, 0.0, -1.0), "identity forward maps to -Z")
    check(up  == (0.0, 1.0, 0.0),  "identity up unchanged")


# ---------------------------------------------------------------------------
# Top-level scene builders.
# ---------------------------------------------------------------------------

def test_build_rrscene_assembles_full_v1_dict():
    cam = rrscene_writer.make_camera_section(
        position=(1.0, 2.0, 3.0), forward=(0.0, 0.0, -1.0),
        up=(0.0, 1.0, 0.0), fov_degrees=35.0,
    )
    rs  = rrscene_writer.make_render_settings(1920, 1080)
    rel = rrscene_writer.make_relativity_section(
        beta_velocity=0.5, velocity_direction=(0.0, 0.0, -1.0),
    )

    scene = rrscene_writer.build_rrscene(
        camera=cam, render_settings=rs, relativity=rel,
        note="test note",
    )
    check(scene["version"] == 1, "version")
    check(scene["render_settings"] == rs, "render_settings round-trip")
    check(scene["camera"] == cam, "camera round-trip")
    check(scene["relativity"] == rel, "relativity round-trip")
    check(scene["_note"] == "test note", "_note preserved")


def test_build_empty_rrscene_still_works():
    # The foundation slice's helper is preserved as a fallback.
    scene = rrscene_writer.build_empty_rrscene()
    check(scene["version"] == 1, "version")
    check(scene["render_settings"]["width"]  == 640, "default width")
    check(scene["render_settings"]["height"] == 480, "default height")
    check(scene["camera"]["fov"] == 50.0, "default fov")
    check(scene["relativity"]["beta_velocity"] == 0.0, "default beta")


def test_serialize_round_trips_through_json():
    scene = rrscene_writer.build_empty_rrscene(note="round-trip")
    text  = rrscene_writer.serialize_rrscene(scene)
    check(text.endswith("\n"), "missing trailing newline")
    parsed = json.loads(text)
    check(parsed == scene, "round-trip mismatch")


def test_write_rrscene_creates_parents_and_returns_abs_path():
    with tempfile.TemporaryDirectory() as tmp:
        nested = os.path.join(tmp, "a", "b", "out.rrscene")
        check(not os.path.exists(os.path.dirname(nested)),
              "test invariant: parents must not exist yet")

        cam = rrscene_writer.make_camera_section(
            position=(0.0, 1.0, 0.0), forward=(0.0, 0.0, -1.0),
            up=(0.0, 1.0, 0.0), fov_degrees=42.0,
        )
        scene = rrscene_writer.build_rrscene(
            camera=cam,
            render_settings=rrscene_writer.make_render_settings(320, 240),
            relativity=rrscene_writer.make_relativity_section(
                beta_velocity=0.42),
            note="custom",
        )
        returned = rrscene_writer.write_rrscene(scene, nested)
        check(os.path.isabs(returned), "returned path is not absolute")
        check(os.path.isfile(returned), "file was not created")

        with open(returned, "r", encoding="utf-8") as f:
            parsed = json.loads(f.read())
        check(parsed["version"] == 1, "written version")
        check(parsed["camera"]["fov"] == 42.0, "fov on disk")
        check(parsed["render_settings"]["width"] == 320, "width on disk")
        check(parsed["relativity"]["beta_velocity"] == 0.42,
              "beta on disk")
        check(parsed.get("_note") == "custom", "note on disk")


def test_write_overwrites_existing_file():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "out.rrscene")
        rrscene_writer.write_empty_rrscene(path=path, width=100, height=100)
        rrscene_writer.write_empty_rrscene(path=path, width=200, height=200)
        with open(path, "r", encoding="utf-8") as f:
            parsed = json.loads(f.read())
        check(parsed["render_settings"]["width"]  == 200, "overwrite width")
        check(parsed["render_settings"]["height"] == 200, "overwrite height")


# ---------------------------------------------------------------------------
# Mesh helpers: triangulation + matrix-times-point.
# ---------------------------------------------------------------------------

def test_triangulate_cpolygon_triangle():
    # Cinema 4D encodes triangles as quads with c == d.
    tris = rrscene_writer.triangulate_cpolygon(0, 1, 2, 2)
    check(tris == [(0, 1, 2)], "triangle -> single triangle")


def test_triangulate_cpolygon_quad():
    # Quads split along the a-c diagonal, fan from a.
    tris = rrscene_writer.triangulate_cpolygon(0, 1, 2, 3)
    check(tris == [(0, 1, 2), (0, 2, 3)], "quad -> two triangles")


def test_triangulate_cpolygon_quad_high_indices():
    # Indices in the thousands round-trip without coercion bugs.
    tris = rrscene_writer.triangulate_cpolygon(1000, 1001, 1002, 1003)
    check(tris == [(1000, 1001, 1002), (1000, 1002, 1003)],
          "quad with large indices")


def test_triangulate_cpolygon_degenerate_triangle():
    # Triangle (a, b, a) is degenerate; output is empty.
    tris = rrscene_writer.triangulate_cpolygon(7, 9, 7, 7)
    check(tris == [], "degenerate triangle pruned")
    # Triangle with a == b is also degenerate.
    tris = rrscene_writer.triangulate_cpolygon(5, 5, 6, 6)
    check(tris == [], "a == b degenerate triangle pruned")


def test_triangulate_cpolygon_degenerate_quad():
    # Quad with a == d collapses one half-triangle.
    tris = rrscene_writer.triangulate_cpolygon(1, 2, 3, 1)
    # First half: (1, 2, 3) is fine. Second half: (1, 3, 1) has
    # a == d, so it's pruned. Final: just the first triangle.
    check(tris == [(1, 2, 3)], "quad with a == d -> one triangle")


def test_transform_point_identity():
    # Identity matrix: v1=+X, v2=+Y, v3=+Z, off=0.
    out = rrscene_writer.transform_point(
        point=(1.0, 2.0, 3.0),
        v1=(1.0, 0.0, 0.0), v2=(0.0, 1.0, 0.0), v3=(0.0, 0.0, 1.0),
        off=(0.0, 0.0, 0.0))
    check(out == (1.0, 2.0, 3.0), "identity matrix returns the input")


def test_transform_point_translation():
    out = rrscene_writer.transform_point(
        point=(1.0, 0.0, 0.0),
        v1=(1.0, 0.0, 0.0), v2=(0.0, 1.0, 0.0), v3=(0.0, 0.0, 1.0),
        off=(10.0, 20.0, 30.0))
    check(out == (11.0, 20.0, 30.0), "translation lands at off + p")


def test_transform_point_uniform_scale():
    out = rrscene_writer.transform_point(
        point=(1.0, 1.0, 1.0),
        v1=(2.0, 0.0, 0.0), v2=(0.0, 2.0, 0.0), v3=(0.0, 0.0, 2.0),
        off=(0.0, 0.0, 0.0))
    check(out == (2.0, 2.0, 2.0), "2x scale doubles every component")


def test_transform_point_y_axis_rotation():
    # 90deg yaw around Y: local +X maps to world -Z (in C4D's
    # left-handed system; we don't care about handedness here,
    # only that the basis vectors are applied correctly).
    out = rrscene_writer.transform_point(
        point=(1.0, 0.0, 0.0),
        v1=(0.0, 0.0, -1.0), v2=(0.0, 1.0, 0.0), v3=(1.0, 0.0, 0.0),
        off=(0.0, 0.0, 0.0))
    check(almost_equal(out[0], 0.0), "rotated x")
    check(almost_equal(out[1], 0.0), "rotated y")
    check(almost_equal(out[2], -1.0), "rotated z")


# ---------------------------------------------------------------------------
# Mesh + material section builders.
# ---------------------------------------------------------------------------

def test_make_mesh_section_shape():
    m = rrscene_writer.make_mesh_section(
        vertices=[(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)],
        triangles=[(0, 1, 2)],
        material_id=3,
    )
    check(m["vertices"] == [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0],
                            [0.0, 1.0, 0.0]],
          "vertices serialised as list-of-lists")
    check(m["triangles"]   == [[0, 1, 2]], "triangles serialised")
    check(m["material_id"] == 3,            "material_id round-trip")


def test_make_mesh_section_default_material_id():
    m = rrscene_writer.make_mesh_section(
        vertices=[(0.0, 0.0, 0.0)], triangles=[])
    check(m["material_id"] == -1, "default material_id is -1")


def test_make_material_section_minimal():
    mat = rrscene_writer.make_material_section(id=5)
    check(mat == {"id": 5}, "minimal material entry has id only")


def test_make_material_section_with_name_and_color():
    mat = rrscene_writer.make_material_section(
        id=2, name="MyRed", base_color=(0.9, 0.1, 0.1))
    check(mat["id"]         == 2,                      "id")
    check(mat["name"]       == "MyRed",                "name")
    check(mat["base_color"] == [0.9, 0.1, 0.1],        "base_color")


def test_build_rrscene_with_meshes_and_materials():
    cam = rrscene_writer.make_camera_section()
    rs  = rrscene_writer.make_render_settings()
    rel = rrscene_writer.make_relativity_section()
    meshes = [
        rrscene_writer.make_mesh_section(
            vertices=[(0, 0, 0), (1, 0, 0), (0, 1, 0)],
            triangles=[(0, 1, 2)],
            material_id=0),
    ]
    materials = [rrscene_writer.make_material_section(id=0, name="Default")]
    scene = rrscene_writer.build_rrscene(
        camera=cam, render_settings=rs, relativity=rel,
        meshes=meshes, materials=materials)
    check(scene["meshes"]    == meshes,    "meshes preserved")
    check(scene["materials"] == materials, "materials preserved")


def test_build_rrscene_omits_empty_meshes_and_materials():
    # Older callers that pass no meshes/materials, or empty
    # iterables, must produce identical output to the
    # camera+render+relativity-only slice.
    scene_a = rrscene_writer.build_rrscene(
        camera=rrscene_writer.make_camera_section(),
        render_settings=rrscene_writer.make_render_settings(),
        relativity=rrscene_writer.make_relativity_section(),
    )
    scene_b = rrscene_writer.build_rrscene(
        camera=rrscene_writer.make_camera_section(),
        render_settings=rrscene_writer.make_render_settings(),
        relativity=rrscene_writer.make_relativity_section(),
        meshes=[], materials=[],
    )
    check("meshes"    not in scene_a, "no meshes key when omitted")
    check("materials" not in scene_a, "no materials key when omitted")
    check(scene_a == scene_b,
          "empty iterables match the omit-by-default shape")


def test_full_mesh_scene_round_trips_through_json():
    # End-to-end: build a small scene with a quad-mesh
    # equivalent (two triangles) + a single material, write to
    # a tempfile, parse it back, confirm the round trip is
    # byte-stable through json.
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "mesh.rrscene")
        m = rrscene_writer.make_mesh_section(
            vertices=[(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)],
            triangles=rrscene_writer.triangulate_cpolygon(0, 1, 2, 3),
            material_id=0)
        scene = rrscene_writer.build_rrscene(
            camera=rrscene_writer.make_camera_section(),
            render_settings=rrscene_writer.make_render_settings(),
            relativity=rrscene_writer.make_relativity_section(),
            meshes=[m],
            materials=[rrscene_writer.make_material_section(
                id=0, name="quad-mat")],
            note="quad",
        )
        rrscene_writer.write_rrscene(scene, path)
        with open(path, "r", encoding="utf-8") as f:
            parsed = json.loads(f.read())
        check(parsed == scene, "round-trip with meshes+materials")
        check(len(parsed["meshes"][0]["triangles"]) == 2,
              "quad expanded into two triangles on disk")


def test_module_does_not_import_c4d():
    check("c4d" not in sys.modules,
          "rrscene_writer pulled the c4d module into sys.modules")


def main():
    test_render_settings_defaults_and_clamps()
    test_camera_section_defaults_and_clamps()
    test_relativity_section_defaults_and_clamps()
    test_c4d_position_z_flip()
    test_c4d_direction_z_flip()
    test_c4d_camera_basis_default_matches_renderer_default()
    test_build_rrscene_assembles_full_v1_dict()
    test_build_empty_rrscene_still_works()
    test_serialize_round_trips_through_json()
    test_write_rrscene_creates_parents_and_returns_abs_path()
    test_write_overwrites_existing_file()
    test_triangulate_cpolygon_triangle()
    test_triangulate_cpolygon_quad()
    test_triangulate_cpolygon_quad_high_indices()
    test_triangulate_cpolygon_degenerate_triangle()
    test_triangulate_cpolygon_degenerate_quad()
    test_transform_point_identity()
    test_transform_point_translation()
    test_transform_point_uniform_scale()
    test_transform_point_y_axis_rotation()
    test_make_mesh_section_shape()
    test_make_mesh_section_default_material_id()
    test_make_material_section_minimal()
    test_make_material_section_with_name_and_color()
    test_build_rrscene_with_meshes_and_materials()
    test_build_rrscene_omits_empty_meshes_and_materials()
    test_full_mesh_scene_round_trips_through_json()
    test_module_does_not_import_c4d()
    print("test_rrscene_writer: %d/%d passed" % (g_total - g_failed, g_total))
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Hand-rolled assertion runner for `rrscene_writer.py`.

Runs under stock `python3` - no Cinema 4D required. The .pyp
plugin imports the same `rrscene_writer` module the tests
exercise, so everything below validates the production code
path that lands inside Cinema 4D.

Verifies:

  - The constructed dict has the v1 schema shape the host
    SceneLoader expects (`version: 1`, render_settings, camera,
    relativity).
  - Optional `_note` is round-tripped under that exact key.
  - `serialize_rrscene` produces parseable JSON; the parsed
    result equals the original dict.
  - `write_empty_rrscene` creates parent dirs, writes the file,
    and returns an absolute path; the file contents parse back
    to the same scene.
  - Custom width / height / fov values are preserved on round
    trip.
"""

from __future__ import annotations

import json
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


def test_default_dict_has_v1_shape():
    scene = rrscene_writer.build_empty_rrscene()
    check(scene.get("version") == 1,
          "version is not 1")
    check("render_settings" in scene,
          "missing render_settings")
    check("camera" in scene,
          "missing camera")
    check("relativity" in scene,
          "missing relativity")
    check(scene["render_settings"]["width"]  == 640, "default width")
    check(scene["render_settings"]["height"] == 480, "default height")
    check(scene["camera"]["fov"]             == 50.0, "default fov")
    check(scene["camera"]["forward"]         == [0.0, 0.0, -1.0],
          "default forward")
    check(scene["camera"]["up"]              == [0.0, 1.0, 0.0],
          "default up")
    check(scene["relativity"]["beta_velocity"] == 0.0,
          "default beta_velocity")


def test_custom_resolution_preserved():
    scene = rrscene_writer.build_empty_rrscene(
        width=1920, height=1080, fov_deg=24.0,
    )
    check(scene["render_settings"]["width"]  == 1920, "width preserved")
    check(scene["render_settings"]["height"] == 1080, "height preserved")
    check(scene["camera"]["fov"]             == 24.0, "fov preserved")


def test_note_round_trips():
    scene = rrscene_writer.build_empty_rrscene(note="hello")
    check(scene.get("_note") == "hello",
          "_note key was not written")
    # No `_note` key when the caller passes None.
    scene_no_note = rrscene_writer.build_empty_rrscene()
    check("_note" not in scene_no_note,
          "_note key present without caller request")


def test_serialize_round_trips_through_json():
    scene = rrscene_writer.build_empty_rrscene(note="round-trip")
    text  = rrscene_writer.serialize_rrscene(scene)
    check(text.endswith("\n"), "missing trailing newline")
    parsed = json.loads(text)
    check(parsed == scene, "round-trip mismatch")


def test_write_creates_parent_dirs_and_returns_abs_path():
    with tempfile.TemporaryDirectory() as tmp:
        nested = os.path.join(tmp, "a", "b", "c", "out.rrscene")
        check(not os.path.exists(os.path.dirname(nested)),
              "test invariant: parents must not exist yet")

        returned = rrscene_writer.write_empty_rrscene(
            path=nested, width=320, height=240, note="test",
        )
        check(os.path.isabs(returned), "returned path is not absolute")
        check(os.path.isfile(returned), "file was not created")

        with open(returned, "r", encoding="utf-8") as f:
            parsed = json.loads(f.read())
        check(parsed["version"] == 1, "written version != 1")
        check(parsed["render_settings"]["width"]  == 320, "width on disk")
        check(parsed["render_settings"]["height"] == 240, "height on disk")
        check(parsed.get("_note") == "test", "note on disk")


def test_write_overwrites_existing_file():
    # Successive calls must replace the old contents (not append),
    # so a Cinema 4D user re-running the export gets a clean file.
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "out.rrscene")
        rrscene_writer.write_empty_rrscene(path=path, width=100, height=100)
        rrscene_writer.write_empty_rrscene(path=path, width=200, height=200)
        with open(path, "r", encoding="utf-8") as f:
            parsed = json.loads(f.read())
        check(parsed["render_settings"]["width"]  == 200, "overwrite width")
        check(parsed["render_settings"]["height"] == 200, "overwrite height")


def test_module_does_not_import_c4d():
    # Per the dependency rules, the helper must NOT pull in the
    # Cinema 4D SDK; that constraint is what lets us test it
    # under stock python3 here. Verify the module did not
    # smuggle a c4d import in.
    check("c4d" not in sys.modules,
          "rrscene_writer pulled the c4d module into sys.modules")


def main():
    test_default_dict_has_v1_shape()
    test_custom_resolution_preserved()
    test_note_round_trips()
    test_serialize_round_trips_through_json()
    test_write_creates_parent_dirs_and_returns_abs_path()
    test_write_overwrites_existing_file()
    test_module_does_not_import_c4d()
    print("test_rrscene_writer: %d/%d passed" % (g_total - g_failed, g_total))
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

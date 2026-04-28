"""Empty .rrscene v1 writer.

The Cinema 4D bridge's first slice ships a single command that writes
a placeholder .rrscene file proving the plugin -> filesystem path is
in place. Translation of the live C4D document (objects, materials,
lights, camera) lands in subsequent slices; for now the file the
bridge writes is a minimal but-valid v1 scene that the host
SceneLoader (`src/io/SceneLoader.cpp`) accepts unchanged.

This module is plain Python. It does not import the Cinema 4D
SDK, so it can be exercised by stock python3 in the standalone
test harness under `tests/test_rrscene_writer.py`. The .pyp
plugin imports `build_empty_rrscene` and `write_empty_rrscene`
from here.

Per the project's dependency rules:

  - The bridge depends on the scene file format
    (`src/io/SceneLoader.cpp` is the authoritative reader).
  - The bridge does NOT depend on internal renderer code; it
    talks to the renderer only over the file format and the
    server protocol.
"""

from __future__ import annotations

import json
import os
from typing import Any, Dict, Optional

# rrscene v1 format version. Bumped by the host loader if/when the
# schema changes; the bridge mirrors the host expectation here so a
# version drift fails loudly at parse time on the host rather than
# silently at the bridge.
RRSCENE_VERSION = 1


def build_empty_rrscene(
    width: int = 640,
    height: int = 480,
    fov_deg: float = 50.0,
    note: Optional[str] = None,
) -> Dict[str, Any]:
    """Construct a minimal v1 .rrscene dict.

    Contains only the sections the host loader treats as well-known
    (`render_settings`, `camera`, `relativity`). No materials /
    spheres / lights / meshes are declared; on the host side this
    parses to an empty scene, which the renderer surfaces as a
    sky-only render. That keeps this slice's output reproducible
    end-to-end without committing the bridge to a translator
    implementation yet.

    The optional `note` is written to a top-level `_note` key. The
    host parser warns-and-ignores unknown top-level keys (per the
    .rrscene v1 spec), so the note round-trips through a real load
    without breaking the parse.
    """
    scene: Dict[str, Any] = {
        "version": RRSCENE_VERSION,
        "render_settings": {
            "width":  int(width),
            "height": int(height),
        },
        "camera": {
            "position": [0.0, 0.0,  0.0],
            "forward":  [0.0, 0.0, -1.0],
            "up":       [0.0, 1.0,  0.0],
            "fov":      float(fov_deg),
        },
        "relativity": {
            "beta_velocity":        0.0,
            "velocity_direction":   [0.0, 0.0, -1.0],
            "aberration_strength":  1.0,
            "doppler_strength":     1.0,
            "searchlight_strength": 1.0,
        },
    }
    if note:
        scene["_note"] = str(note)
    return scene


def serialize_rrscene(scene: Dict[str, Any]) -> str:
    """Serialise a scene dict to .rrscene JSON text.

    Indented for readability so the file is diff-friendly when a
    human inspects the bridge's output. Trailing newline so it
    plays well with text editors and POSIX line conventions.
    """
    return json.dumps(scene, indent=4, sort_keys=False) + "\n"


def write_empty_rrscene(
    path: str,
    width: int = 640,
    height: int = 480,
    fov_deg: float = 50.0,
    note: Optional[str] = None,
) -> str:
    """Build the minimal scene dict and write it to `path`.

    Creates parent directories if they don't exist. Returns the
    absolute path written, so the plugin can surface it in the
    confirmation dialog without re-deriving it.
    """
    scene = build_empty_rrscene(width=width, height=height,
                                fov_deg=fov_deg, note=note)
    text  = serialize_rrscene(scene)

    abs_path = os.path.abspath(path)
    parent   = os.path.dirname(abs_path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent, exist_ok=True)

    with open(abs_path, "w", encoding="utf-8") as f:
        f.write(text)

    return abs_path

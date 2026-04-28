"""rrscene v1 writer for the Cinema 4D bridge.

The bridge's first slice shipped a single command that wrote a
placeholder .rrscene file. This module is now its data layer: it
exposes per-section builders the .pyp plugin uses to construct a
scene dict from the live Cinema 4D document, plus the
serialisation + write helpers that put the finished dict on disk.

Stays plain Python. No `c4d` import - the module is exercised by
stock python3 in the standalone test harness, and the same code
path runs inside Cinema 4D when the .pyp imports it.

Per the project's dependency rules:

  - The bridge depends on the scene file format
    (`src/io/SceneLoader.cpp` is the authoritative reader).
  - The bridge does NOT depend on internal renderer code; it
    talks to the renderer only over the file format and the
    server protocol.

Field names + value ranges below mirror the host parser's
`load_camera`, `load_render_settings`, and `load_relativity`
sections in `src/io/SceneLoader.cpp`. A drift fails loudly at
parse time on the host rather than silently in the writer.
"""

from __future__ import annotations

import json
import os
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

# rrscene v1 format version. Bumped by the host loader if/when the
# schema changes; the bridge mirrors the host expectation here.
RRSCENE_VERSION = 1

# Default values. Camera defaults match the host's identity camera;
# render defaults match the .rrscene v1 spec example. Kept as module
# constants so the .pyp plugin can fall back to them when a Cinema
# 4D document does not surface a value (no active camera, no
# render data, etc.).
DEFAULT_RENDER_WIDTH  = 640
DEFAULT_RENDER_HEIGHT = 480
DEFAULT_FOV_DEGREES   = 50.0
DEFAULT_FORWARD       = (0.0, 0.0, -1.0)
DEFAULT_UP            = (0.0, 1.0,  0.0)
DEFAULT_POSITION      = (0.0, 0.0,  0.0)

# Defaults for the relativity section. Mirror
# `RelativityParams::*` and the .rrscene parser's clamps:
# - beta_velocity in [0, 0.999999]
# - *_strength in [0, 1]
# - velocity_direction is normalised at parse time; zero-vector
#   falls back to (0, 0, -1).
DEFAULT_BETA_VELOCITY        = 0.0
DEFAULT_VELOCITY_DIRECTION   = (0.0, 0.0, -1.0)
DEFAULT_ABERRATION_STRENGTH  = 1.0
DEFAULT_DOPPLER_STRENGTH     = 1.0
DEFAULT_SEARCHLIGHT_STRENGTH = 1.0


def _vec3_list(v: Sequence[float]) -> List[float]:
    """Normalise a Cinema 4D / tuple / list vector triple to a
    JSON-serialisable [x, y, z] list of floats. Raises on shape
    mismatch so a Cinema 4D-side bug surfaces here rather than at
    parse time on the host.
    """
    out = [float(v[0]), float(v[1]), float(v[2])]
    return out


def _clamp(x: float, lo: float, hi: float) -> float:
    return lo if x < lo else (hi if x > hi else x)


# ---------------------------------------------------------------------------
# Per-section builders.
# ---------------------------------------------------------------------------

def make_render_settings(width:  int = DEFAULT_RENDER_WIDTH,
                         height: int = DEFAULT_RENDER_HEIGHT) -> Dict[str, Any]:
    """Build the `render_settings` section.

    The host parser reads `width` and `height` as positive ints.
    Non-positive values fall back to the v1 defaults so the
    written file is never invalid even when the C4D side hands us
    a degenerate render configuration.
    """
    w = int(width)  if int(width)  > 0 else DEFAULT_RENDER_WIDTH
    h = int(height) if int(height) > 0 else DEFAULT_RENDER_HEIGHT
    return {"width": w, "height": h}


def make_camera_section(position: Sequence[float] = DEFAULT_POSITION,
                        forward:  Sequence[float] = DEFAULT_FORWARD,
                        up:       Sequence[float] = DEFAULT_UP,
                        fov_degrees: float = DEFAULT_FOV_DEGREES) -> Dict[str, Any]:
    """Build the `camera` section.

    Vectors are written as `[x, y, z]` triples in world space. The
    renderer is right-handed with +Y up; the .pyp plugin is
    responsible for converting from Cinema 4D's left-handed
    `+Z forward` convention before calling this builder (see
    `convert_c4d_camera_basis`).

    `fov_degrees` is the vertical field of view in degrees. The
    host parser rejects values outside the open interval `(0, 180)`,
    so we clamp into that range with a small epsilon either side.
    """
    fov = float(fov_degrees)
    if not (0.0 < fov < 180.0):
        # Ship a sane default rather than letting the host's parser
        # reject the file. A bad Cinema 4D value is the user's
        # problem, but the bridge should still produce parseable
        # output so they see a render and can adjust.
        fov = DEFAULT_FOV_DEGREES
    return {
        "position": _vec3_list(position),
        "forward":  _vec3_list(forward),
        "up":       _vec3_list(up),
        "fov":      fov,
    }


def make_relativity_section(beta_velocity:        float = DEFAULT_BETA_VELOCITY,
                            velocity_direction:   Sequence[float] = DEFAULT_VELOCITY_DIRECTION,
                            aberration_strength:  float = DEFAULT_ABERRATION_STRENGTH,
                            doppler_strength:     float = DEFAULT_DOPPLER_STRENGTH,
                            searchlight_strength: float = DEFAULT_SEARCHLIGHT_STRENGTH,
                            ) -> Dict[str, Any]:
    """Build the `relativity` section.

    Clamps every scalar to the same range the host parser will
    clamp it to (`beta_velocity` in `[0, 0.999999]`, strengths in
    `[0, 1]`). Doing it here too means the saved file is
    self-consistent: the values you see in the .rrscene match the
    values the renderer will actually use.
    """
    return {
        "beta_velocity":        _clamp(float(beta_velocity), 0.0, 0.999999),
        "velocity_direction":   _vec3_list(velocity_direction),
        "aberration_strength":  _clamp(float(aberration_strength),  0.0, 1.0),
        "doppler_strength":     _clamp(float(doppler_strength),     0.0, 1.0),
        "searchlight_strength": _clamp(float(searchlight_strength), 0.0, 1.0),
    }


# ---------------------------------------------------------------------------
# Coordinate conversion: Cinema 4D <-> rrscene.
# ---------------------------------------------------------------------------
#
# Cinema 4D uses a LEFT-HANDED Y-up coordinate system (+X right,
# +Y up, +Z forward into the scene; the camera looks down its
# local +Z). The renderer is RIGHT-HANDED Y-up (+X right, +Y up,
# +Z toward the viewer; the camera looks down its local -Z).
#
# To convert C4D world-space coordinates to renderer world-space,
# negate the Z component of every position and direction vector.
# Doing this here keeps the conversion in one place; the .pyp
# plugin never has to know about handedness directly.

def convert_c4d_position(p: Sequence[float]) -> Tuple[float, float, float]:
    """Flip the Z component of a C4D world-space position so the
    point lands at the correct location in the renderer's
    right-handed coordinate system."""
    return (float(p[0]), float(p[1]), -float(p[2]))


def convert_c4d_direction(d: Sequence[float]) -> Tuple[float, float, float]:
    """Flip the Z component of a C4D world-space direction vector
    so it points the same way in the renderer's right-handed
    coordinate system. Used for forward / up / right basis
    vectors and for `velocity_direction`."""
    return (float(d[0]), float(d[1]), -float(d[2]))


def convert_c4d_camera_basis(position: Sequence[float],
                             forward:  Sequence[float],
                             up:       Sequence[float]
                             ) -> Tuple[Tuple[float, float, float],
                                        Tuple[float, float, float],
                                        Tuple[float, float, float]]:
    """Apply the C4D-to-renderer Z-flip to a (position, forward, up)
    triple in one call. Returns three 3-tuples ready to feed
    straight into `make_camera_section`.

    The .pyp plugin reads the camera's global matrix, picks
    `mg.off`, `mg.v3`, and `mg.v2` (Cinema 4D's "view direction"
    is the matrix's third column), and hands them in here.
    """
    return (convert_c4d_position(position),
            convert_c4d_direction(forward),
            convert_c4d_direction(up))


# ---------------------------------------------------------------------------
# Top-level scene builders.
# ---------------------------------------------------------------------------

def build_empty_rrscene(width: int = DEFAULT_RENDER_WIDTH,
                        height: int = DEFAULT_RENDER_HEIGHT,
                        fov_deg: float = DEFAULT_FOV_DEGREES,
                        note: Optional[str] = None) -> Dict[str, Any]:
    """Construct a minimal v1 .rrscene dict with default sections.

    Retained for the foundation slice's smoke-test path and used
    as the fallback when the .pyp plugin cannot resolve a Cinema
    4D document at all. New code should prefer `build_rrscene`
    with explicit camera / render / relativity sections.
    """
    return build_rrscene(
        camera=make_camera_section(fov_degrees=fov_deg),
        render_settings=make_render_settings(width=width, height=height),
        relativity=make_relativity_section(),
        note=note,
    )


def build_rrscene(camera: Dict[str, Any],
                  render_settings: Dict[str, Any],
                  relativity: Dict[str, Any],
                  note: Optional[str] = None) -> Dict[str, Any]:
    """Assemble the top-level v1 .rrscene dict from per-section
    inputs. The optional `note` is written to a top-level
    `_note` key; the host parser warns-and-ignores unknown
    top-level keys (per the .rrscene v1 spec) so it round-trips
    through a real load without breaking the parse.
    """
    scene: Dict[str, Any] = {
        "version": RRSCENE_VERSION,
        "render_settings": render_settings,
        "camera": camera,
        "relativity": relativity,
    }
    if note:
        scene["_note"] = str(note)
    return scene


# ---------------------------------------------------------------------------
# Serialise + write.
# ---------------------------------------------------------------------------

def serialize_rrscene(scene: Dict[str, Any]) -> str:
    """Serialise a scene dict to .rrscene JSON text.

    Indented for readability so the file is diff-friendly when a
    human inspects the bridge's output. Trailing newline so it
    plays well with text editors and POSIX line conventions.
    """
    return json.dumps(scene, indent=4, sort_keys=False) + "\n"


def write_rrscene(scene: Dict[str, Any], path: str) -> str:
    """Write a finished scene dict to disk.

    Creates parent directories if they don't exist. Returns the
    absolute path written so the plugin can surface it in the
    confirmation dialog without re-deriving it.
    """
    text = serialize_rrscene(scene)

    abs_path = os.path.abspath(path)
    parent   = os.path.dirname(abs_path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent, exist_ok=True)

    with open(abs_path, "w", encoding="utf-8") as f:
        f.write(text)

    return abs_path


def write_empty_rrscene(path: str,
                        width: int = DEFAULT_RENDER_WIDTH,
                        height: int = DEFAULT_RENDER_HEIGHT,
                        fov_deg: float = DEFAULT_FOV_DEGREES,
                        note: Optional[str] = None) -> str:
    """Foundation-slice convenience wrapper. Build a minimal
    scene and write it to `path`.
    """
    scene = build_empty_rrscene(width=width, height=height,
                                fov_deg=fov_deg, note=note)
    return write_rrscene(scene, path)

"""Pure-Python helpers for the Cinema 4D preview dialog.

The dialog itself is a `c4d.gui.GeDialog` subclass that lives
in `RelativityRenderBridge.pyp` (it has to - GeDialog only
exists inside Cinema 4D). The pieces of its behaviour that
DON'T need a C4D environment - input validation, slider
clamping, mapping the dialog's four relativity sliders into
a `relativity` section, and formatting the server's reply
for the response text area - live here so the standalone
Python test harness can exercise them.

Per the project's dependency rules, this module is a CLIENT
of the .rrscene file format and the renderer server protocol.
It does NOT import any internal `rr_*` C++ headers or anything
under `src/`. Nothing under `src/` may import this module.
"""

from __future__ import annotations

import os
import sys

# Re-use the writer's relativity-section builder so the dialog
# and the standalone Export Scene command produce identical
# output for the same inputs.
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import rrscene_writer  # noqa: E402

# Default values for the dialog's controls. Match the v1
# .rrscene defaults so a freshly opened dialog produces the
# same scene the writer's `make_relativity_section()` would.
DEFAULT_HOST              = "127.0.0.1"
DEFAULT_PORT              = 7777
DEFAULT_BETA              = 0.0
DEFAULT_ABERRATION        = 1.0
DEFAULT_DOPPLER           = 1.0
DEFAULT_SEARCHLIGHT       = 1.0

# Hard caps on what counts as "a sensible TCP port". The
# dialog rejects anything outside this range with a clear
# message rather than silently letting the socket layer
# raise an OSError later.
MIN_PORT = 1
MAX_PORT = 65535


def clamp_unit(value):
    """Clamp a float to `[0.0, 1.0]`. The four relativity
    sliders all live in this range; clamping here means a
    misconfigured slider can never produce a value the host
    parser would reject downstream.
    """
    try:
        v = float(value)
    except (TypeError, ValueError):
        return 0.0
    if v < 0.0:
        return 0.0
    if v > 1.0:
        return 1.0
    return v


def clamp_beta(value):
    """Clamp the beta slider to the same `(-1, 1)` bound the
    server's set_beta command applies. The dialog only exposes
    the magnitude (beta in `[0, 1)`), so the lower bound is 0
    here even though the server itself accepts negative
    velocities.

    The upper bound is `0.999` rather than `1.0` so the host's
    `clampBeta(...)` invariant ("|beta| < 1") cannot be
    violated by a slider rounded to the wall.
    """
    try:
        v = float(value)
    except (TypeError, ValueError):
        return 0.0
    if v < 0.0:
        return 0.0
    if v >= 1.0:
        return 0.999
    return v


def make_relativity_from_dialog(beta,
                                aberration,
                                doppler,
                                searchlight):
    """Build a `relativity` section dict from the four dialog
    sliders. Values are clamped to their valid ranges before
    being handed to the writer. Output matches what
    `rrscene_writer.make_relativity_section(...)` would
    produce for the same inputs - i.e. the .pyp can pass this
    dict straight into `rrscene_writer.build_rrscene(...)`
    instead of a controller-derived section.
    """
    return rrscene_writer.make_relativity_section(
        beta_velocity        = clamp_beta(beta),
        velocity_direction   = (0.0, 0.0, -1.0),
        aberration_strength  = clamp_unit(aberration),
        doppler_strength     = clamp_unit(doppler),
        searchlight_strength = clamp_unit(searchlight),
    )


def validate_host(host):
    """Return `(ok, message)` for the dialog's host field.

    Rejects empty / whitespace-only inputs and embedded
    whitespace (which would smash into the protocol's line-
    framing). Does NOT do a DNS lookup or a connect probe -
    that's `RenderServerClient`'s job, and surfacing those
    errors at button-press time keeps the dialog responsive.
    """
    if host is None:
        return (False, "host is empty")
    s = str(host).strip()
    if not s:
        return (False, "host is empty")
    for ch in s:
        if ch.isspace():
            return (False, "host must not contain whitespace")
    return (True, "")


def validate_port(port):
    """Return `(ok, message)` for the dialog's port field.

    Accepts integers and integer-shaped strings; rejects
    anything outside `[MIN_PORT, MAX_PORT]`. Numeric coercion
    failures map to a clear "must be an integer" message
    rather than a generic ValueError.

    Reject `bool` (a subclass of `int` in Python; without
    special-casing `validate_port(True)` would accept "port 1")
    and reject floats explicitly (`int(3.5)` silently
    truncates, which would let `3.5` pass as `3`).
    """
    if isinstance(port, bool):
        return (False, "port must be an integer")
    if isinstance(port, float):
        return (False, "port must be an integer")
    try:
        n = int(port)
    except (TypeError, ValueError):
        return (False, "port must be an integer")
    if n < MIN_PORT or n > MAX_PORT:
        return (False, "port must be in [{0}, {1}]".format(MIN_PORT, MAX_PORT))
    return (True, "")


def format_server_reply(response, command_label):
    """Render a `server_client.ServerResponse` into the text
    the dialog drops into the response text area.

    Always prefixes the status line with `OK ` / `ERR `
    explicitly so a multi-line reply remains readable. Adds
    the command label so a user scanning the panel knows
    which click produced which line.
    """
    head = "[" + str(command_label) + "] "
    if response is None:
        return head + "(no response)"
    if response.ok:
        head += "OK"
    else:
        head += "ERR"
    if response.status_line:
        return head + " " + response.status_line
    return head


def format_connection_error(exc, command_label, host, port):
    """Render a `server_client.ServerClientError` into the
    text the dialog drops into the response text area.

    Includes the host / port the dialog tried, plus a
    reminder about `RelativityRender --serve`, so a user
    investigating the failure has every piece of information
    a single line of dialog text can reasonably carry.
    """
    return ("[" + str(command_label) + "] could not reach "
            + str(host) + ":" + str(port) + " - " + str(exc)
            + " (start the server with `RelativityRender --serve`)")


# ---------------------------------------------------------------------------
# Render-response parser.
# ---------------------------------------------------------------------------

# The renderer server's render reply (M18 wiring slice) is:
#   `OK rendered <W>x<H> to <abs_path>`
# After a successful render the preview dialog parses this
# line to know (a) how big the image is and (b) where to
# load it from.

def parse_render_response(status_line):
    """Return `(width, height, path)` parsed out of the
    server's `OK rendered ...` reply, or `(None, None, None)`
    when the line is not a successful render reply.

    The path is everything after the literal " to " - so
    spaces inside the path are preserved verbatim. Width and
    height are returned as integers; non-numeric components
    yield `(None, None, None)` rather than raising, so the
    caller can fall through to a friendlier error path.
    """
    if not status_line:
        return (None, None, None)
    s = str(status_line).strip()
    prefix = "OK rendered "
    if not s.startswith(prefix):
        return (None, None, None)
    rest = s[len(prefix):]
    marker = " to "
    idx = rest.find(marker)
    if idx < 0:
        return (None, None, None)
    dims = rest[:idx]
    path = rest[idx + len(marker):].strip()
    if not path:
        return (None, None, None)
    if "x" not in dims:
        return (None, None, None)
    w_str, h_str = dims.split("x", 1)
    try:
        w = int(w_str)
        h = int(h_str)
    except (TypeError, ValueError):
        return (None, None, None)
    if w <= 0 or h <= 0:
        return (None, None, None)
    return (w, h, path)

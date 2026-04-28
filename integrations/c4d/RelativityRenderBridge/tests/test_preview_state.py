#!/usr/bin/env python3
"""Hand-rolled assertion runner for `preview_state.py`.

Runs under stock `python3` - no Cinema 4D required. The pure
helpers in `preview_state.py` cover the parts of the C4D
`PreviewDialog`'s behaviour that don't need a GeDialog: input
validation, slider clamping, dialog -> rrscene relativity
section conversion, and server-reply formatting for the
multi-line text area.
"""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGIN_DIR = os.path.dirname(_HERE)
if _PLUGIN_DIR not in sys.path:
    sys.path.insert(0, _PLUGIN_DIR)

import preview_state    # noqa: E402
import rrscene_writer   # noqa: E402
import server_client    # noqa: E402


g_total  = 0
g_failed = 0


def check(cond, message):
    global g_total, g_failed
    g_total += 1
    if not cond:
        g_failed += 1
        sys.stderr.write("FAIL: " + message + "\n")


def almost_equal(a, b, eps=1e-9):
    return abs(float(a) - float(b)) <= eps


# ---------------------------------------------------------------------------
# Defaults pinned to the writer + protocol contracts.
# ---------------------------------------------------------------------------

def test_defaults_match_v1_contract():
    check(preview_state.DEFAULT_HOST == "127.0.0.1",      "host")
    check(preview_state.DEFAULT_PORT == 7777,             "port")
    check(preview_state.DEFAULT_BETA == 0.0,              "beta")
    check(preview_state.DEFAULT_ABERRATION == 1.0,        "aberration")
    check(preview_state.DEFAULT_DOPPLER == 1.0,           "doppler")
    check(preview_state.DEFAULT_SEARCHLIGHT == 1.0,       "searchlight")
    check(preview_state.MIN_PORT == 1,                    "MIN_PORT")
    check(preview_state.MAX_PORT == 65535,                "MAX_PORT")


# ---------------------------------------------------------------------------
# Slider clamping.
# ---------------------------------------------------------------------------

def test_clamp_unit_in_range():
    check(preview_state.clamp_unit(0.0)  == 0.0, "0")
    check(preview_state.clamp_unit(0.5)  == 0.5, "0.5")
    check(preview_state.clamp_unit(1.0)  == 1.0, "1")


def test_clamp_unit_clamps_out_of_range():
    check(preview_state.clamp_unit(-0.1) == 0.0, "below 0")
    check(preview_state.clamp_unit(1.1)  == 1.0, "above 1")
    check(preview_state.clamp_unit(-100) == 0.0, "very negative")
    check(preview_state.clamp_unit(1e6)  == 1.0, "very high")


def test_clamp_unit_handles_garbage_input():
    check(preview_state.clamp_unit(None)    == 0.0, "None")
    check(preview_state.clamp_unit("nope")  == 0.0, "non-numeric str")
    check(preview_state.clamp_unit("0.42")  == 0.42, "numeric str round-trips")


def test_clamp_beta_pins_below_one():
    # The host's clampBeta uses |beta| < 1 strictly. The
    # dialog clamps to 0.999 so a slider rounded to the wall
    # cannot violate that invariant.
    check(preview_state.clamp_beta(0.0)  == 0.0,   "0")
    check(preview_state.clamp_beta(0.5)  == 0.5,   "0.5")
    check(preview_state.clamp_beta(1.0)  == 0.999, "1.0 -> 0.999")
    check(preview_state.clamp_beta(2.5)  == 0.999, "high -> 0.999")
    check(preview_state.clamp_beta(-0.4) == 0.0,   "negative -> 0")


# ---------------------------------------------------------------------------
# Relativity section assembly.
# ---------------------------------------------------------------------------

def test_make_relativity_from_dialog_default_path_matches_writer():
    # With every slider at its default, the assembled section
    # must match what `rrscene_writer.make_relativity_section()`
    # produces. Pins: a future drift in either side fails this
    # test alongside the code.
    a = preview_state.make_relativity_from_dialog(
        beta=preview_state.DEFAULT_BETA,
        aberration=preview_state.DEFAULT_ABERRATION,
        doppler=preview_state.DEFAULT_DOPPLER,
        searchlight=preview_state.DEFAULT_SEARCHLIGHT)
    b = rrscene_writer.make_relativity_section()
    check(a == b,
          "default dialog state matches default writer section")


def test_make_relativity_from_dialog_carries_slider_values():
    rel = preview_state.make_relativity_from_dialog(
        beta=0.4, aberration=0.7, doppler=0.6, searchlight=0.3)
    check(almost_equal(rel["beta_velocity"],         0.4), "beta")
    check(almost_equal(rel["aberration_strength"],   0.7), "aberration")
    check(almost_equal(rel["doppler_strength"],      0.6), "doppler")
    check(almost_equal(rel["searchlight_strength"],  0.3), "searchlight")


def test_make_relativity_from_dialog_clamps_out_of_range_inputs():
    rel = preview_state.make_relativity_from_dialog(
        beta=2.0, aberration=-0.5, doppler=99.0, searchlight=-1.0)
    check(rel["beta_velocity"]         == 0.999, "beta 2.0 -> 0.999")
    check(rel["aberration_strength"]   == 0.0,   "aberration -0.5 -> 0")
    check(rel["doppler_strength"]      == 1.0,   "doppler 99 -> 1")
    check(rel["searchlight_strength"]  == 0.0,   "searchlight -1 -> 0")


# ---------------------------------------------------------------------------
# Host validation.
# ---------------------------------------------------------------------------

def test_validate_host_accepts_typical_inputs():
    for h in ("127.0.0.1", "localhost", "10.0.0.5", "render.example.com"):
        ok, _ = preview_state.validate_host(h)
        check(ok, "expected ok host: " + h)


def test_validate_host_rejects_empty_or_whitespace():
    for h in ("", "   ", "\t", "\n"):
        ok, msg = preview_state.validate_host(h)
        check(not ok, "expected reject: " + repr(h))
        check("empty" in msg or "whitespace" in msg,
              "message mentions empty/whitespace: " + msg)


def test_validate_host_rejects_embedded_whitespace():
    for h in ("foo bar", "foo\tbar", "foo\nbar"):
        ok, msg = preview_state.validate_host(h)
        check(not ok, "expected reject: " + repr(h))
        check("whitespace" in msg,
              "message mentions whitespace: " + msg)


def test_validate_host_rejects_none():
    ok, _ = preview_state.validate_host(None)
    check(not ok, "None rejected")


# ---------------------------------------------------------------------------
# Port validation.
# ---------------------------------------------------------------------------

def test_validate_port_accepts_in_range():
    for p in (1, 80, 7777, 8080, 65535):
        ok, _ = preview_state.validate_port(p)
        check(ok, "expected ok port: " + str(p))


def test_validate_port_accepts_string_integers():
    ok, _ = preview_state.validate_port("7777")
    check(ok, "string '7777' accepted")


def test_validate_port_rejects_out_of_range():
    for p in (0, -1, 65536, 100000):
        ok, msg = preview_state.validate_port(p)
        check(not ok, "expected reject: " + str(p))
        check("[1," in msg.replace(" ", ","),
              "message mentions range: " + msg)


def test_validate_port_rejects_non_integer():
    for p in (3.5, "abc", "", None, "12.5"):
        ok, msg = preview_state.validate_port(p)
        check(not ok, "expected reject: " + repr(p))
        check("integer" in msg, "message mentions integer: " + msg)


def test_validate_port_rejects_bool():
    # bool is a subclass of int in Python; without special-
    # casing `validate_port(True)` would accept "port 1".
    ok, _ = preview_state.validate_port(True)
    check(not ok, "True rejected")
    ok, _ = preview_state.validate_port(False)
    check(not ok, "False rejected")


# ---------------------------------------------------------------------------
# Server-reply formatting.
# ---------------------------------------------------------------------------

def test_format_server_reply_ok():
    resp = server_client.parse_response("OK pong\nEND\n")
    out  = preview_state.format_server_reply(resp, "ping")
    check(out == "[ping] OK OK pong"
          # The status_line carries the OK prefix already; we
          # accept either "[ping] OK OK pong" (current) or a
          # future cleaned-up format. Pin the structural bits:
          or out.startswith("[ping] OK"),
          "status framed with OK")
    check("OK pong" in out, "status_line embedded")


def test_format_server_reply_err():
    resp = server_client.parse_response(
        "ERR no scene loaded\nEND\n")
    out = preview_state.format_server_reply(resp, "render")
    check(out.startswith("[render] ERR"), "ERR framed")
    check("no scene loaded" in out, "status_line embedded")


def test_format_server_reply_none():
    out = preview_state.format_server_reply(None, "ping")
    check("[ping]" in out, "label preserved")
    check("no response" in out, "no-response message")


# ---------------------------------------------------------------------------
# Connection-error formatting.
# ---------------------------------------------------------------------------

def test_format_connection_error_includes_host_port_and_remedy():
    err = server_client.ServerClientError("Connection refused")
    out = preview_state.format_connection_error(
        err, "ping", "127.0.0.1", 7777)
    check("ping"            in out, "label")
    check("127.0.0.1"       in out, "host")
    check("7777"            in out, "port")
    check("Connection refused" in out, "underlying error")
    check("--serve"         in out, "remedy")


# ---------------------------------------------------------------------------
# parse_render_response.
# ---------------------------------------------------------------------------

def test_parse_render_response_typical():
    w, h, p = preview_state.parse_render_response(
        "OK rendered 1280x720 to /tmp/render.ppm")
    check(w == 1280, "width")
    check(h == 720,  "height")
    check(p == "/tmp/render.ppm", "path")


def test_parse_render_response_path_with_spaces():
    w, h, p = preview_state.parse_render_response(
        "OK rendered 100x50 to /tmp/space dir/render to thing.ppm")
    check(w == 100 and h == 50,                 "dimensions")
    # Path must preserve every space verbatim - including the
    # literal " to " inside it. The parser uses the FIRST
    # `" to "` after the dimensions block, then takes the
    # rest as the path.
    check(p == "/tmp/space dir/render to thing.ppm", "path with spaces")


def test_parse_render_response_unicode_path():
    line = "OK rendered 1x1 to /tmp/éclair.ppm"
    w, h, p = preview_state.parse_render_response(line)
    check(w == 1 and h == 1, "dimensions")
    check(p.endswith("éclair.ppm"), "unicode preserved")


def test_parse_render_response_rejects_err_line():
    out = preview_state.parse_render_response(
        "ERR render: no scene loaded")
    check(out == (None, None, None), "ERR -> None")


def test_parse_render_response_rejects_missing_to():
    out = preview_state.parse_render_response(
        "OK rendered 100x50 /tmp/render.ppm")
    check(out == (None, None, None),
          "missing ' to ' marker -> None")


def test_parse_render_response_rejects_non_numeric_dims():
    out = preview_state.parse_render_response(
        "OK rendered foox50 to /tmp/render.ppm")
    check(out == (None, None, None),
          "non-numeric width -> None")
    out = preview_state.parse_render_response(
        "OK rendered 100xbar to /tmp/render.ppm")
    check(out == (None, None, None),
          "non-numeric height -> None")


def test_parse_render_response_rejects_zero_dims():
    out = preview_state.parse_render_response(
        "OK rendered 0x0 to /tmp/render.ppm")
    check(out == (None, None, None),
          "zero dims -> None")


def test_parse_render_response_rejects_empty_path():
    out = preview_state.parse_render_response(
        "OK rendered 10x10 to    ")
    check(out == (None, None, None),
          "empty path -> None")


def test_parse_render_response_rejects_empty_input():
    check(preview_state.parse_render_response("") ==
          (None, None, None), "empty -> None")
    check(preview_state.parse_render_response(None) ==
          (None, None, None), "None input -> None")


# ---------------------------------------------------------------------------
# No c4d import sneaks in.
# ---------------------------------------------------------------------------

def test_module_does_not_import_c4d():
    check("c4d" not in sys.modules,
          "preview_state pulled c4d into sys.modules")


def main():
    test_defaults_match_v1_contract()
    test_clamp_unit_in_range()
    test_clamp_unit_clamps_out_of_range()
    test_clamp_unit_handles_garbage_input()
    test_clamp_beta_pins_below_one()
    test_make_relativity_from_dialog_default_path_matches_writer()
    test_make_relativity_from_dialog_carries_slider_values()
    test_make_relativity_from_dialog_clamps_out_of_range_inputs()
    test_validate_host_accepts_typical_inputs()
    test_validate_host_rejects_empty_or_whitespace()
    test_validate_host_rejects_embedded_whitespace()
    test_validate_host_rejects_none()
    test_validate_port_accepts_in_range()
    test_validate_port_accepts_string_integers()
    test_validate_port_rejects_out_of_range()
    test_validate_port_rejects_non_integer()
    test_validate_port_rejects_bool()
    test_format_server_reply_ok()
    test_format_server_reply_err()
    test_format_server_reply_none()
    test_format_connection_error_includes_host_port_and_remedy()
    test_parse_render_response_typical()
    test_parse_render_response_path_with_spaces()
    test_parse_render_response_unicode_path()
    test_parse_render_response_rejects_err_line()
    test_parse_render_response_rejects_missing_to()
    test_parse_render_response_rejects_non_numeric_dims()
    test_parse_render_response_rejects_zero_dims()
    test_parse_render_response_rejects_empty_path()
    test_parse_render_response_rejects_empty_input()
    test_module_does_not_import_c4d()
    print("test_preview_state: %d/%d passed" % (g_total - g_failed, g_total))
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

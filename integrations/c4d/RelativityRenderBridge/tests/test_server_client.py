#!/usr/bin/env python3
"""Hand-rolled assertion runner for `server_client.py`.

Runs under stock `python3` - no Cinema 4D required, and no
real TCP socket binds happen here. The protocol parser layer
(`parse_response`, `read_until_terminator`,
`RenderServerClient._normalise_command_line`) is exercised
against in-memory byte streams; the `RenderServerClient`'s
socket-using methods are validated separately by the live
`--serve` smoke test in BUILD_PLAN.md.
"""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGIN_DIR = os.path.dirname(_HERE)
if _PLUGIN_DIR not in sys.path:
    sys.path.insert(0, _PLUGIN_DIR)

import server_client  # noqa: E402

g_total  = 0
g_failed = 0


def check(cond, message):
    global g_total, g_failed
    g_total += 1
    if not cond:
        g_failed += 1
        sys.stderr.write("FAIL: " + message + "\n")


# ---------------------------------------------------------------------------
# parse_response
# ---------------------------------------------------------------------------

def test_parse_response_ok_single_line():
    r = server_client.parse_response("OK pong\nEND\n")
    check(r.ok,                          "ok flag")
    check(r.status_line == "OK pong",    "status_line")
    check(r.body == "OK pong",           "body without END")


def test_parse_response_err_single_line():
    r = server_client.parse_response("ERR no scene loaded\nEND\n")
    check(not r.ok,                                  "ok flag")
    check(r.status_line == "ERR no scene loaded",    "status_line")
    check(r.body == "ERR no scene loaded",           "body")


def test_parse_response_multi_line_body():
    text = "OK rendered 1280x720 to /tmp/out.ppm\nextra info\nEND\n"
    r = server_client.parse_response(text)
    check(r.ok, "ok flag")
    check(r.status_line == "OK rendered 1280x720 to /tmp/out.ppm",
          "first line is the status line")
    check("extra info" in r.body, "body retains extra lines")
    check(not r.body.endswith("END"), "END stripped")


def test_parse_response_tolerates_crlf():
    r = server_client.parse_response("OK pong\r\nEND\r\n")
    check(r.ok, "ok flag")
    check(r.status_line == "OK pong",
          "CRLF normalised to status_line")


def test_parse_response_handles_missing_terminator():
    # The dispatcher uses a soft fallback when the terminator
    # is not present (the socket-side caller is supposed to
    # validate termination first, but the parser must not
    # crash on a degenerate input).
    r = server_client.parse_response("OK pong")
    check(r.ok, "ok flag")
    check(r.status_line == "OK pong",
          "status_line still extracted")


def test_parse_response_empty_body():
    r = server_client.parse_response("END\n")
    check(not r.ok, "empty -> not OK")
    check(r.status_line == "",
          "no status_line on terminator-only reply")
    check(r.body == "", "body empty")


def test_parse_response_just_ok_token():
    # `OK` (no trailing space) is also a valid OK status.
    r = server_client.parse_response("OK\nEND\n")
    check(r.ok, "OK alone counts")
    check(r.status_line == "OK", "status_line")


# ---------------------------------------------------------------------------
# read_until_terminator
# ---------------------------------------------------------------------------

class _FakeReader(object):
    """Stand-in for a socket's `recv` method. Returns successive
    chunks from a pre-baked list; returns `b""` to signal EOF
    when the list is exhausted.
    """
    def __init__(self, chunks):
        self.chunks = list(chunks)

    def __call__(self, n):
        if not self.chunks:
            return b""
        chunk = self.chunks.pop(0)
        if len(chunk) <= n:
            return chunk
        # Split: return up to `n` bytes; push the rest back.
        head, tail = chunk[:n], chunk[n:]
        self.chunks.insert(0, tail)
        return head


def test_read_until_terminator_single_chunk():
    r = _FakeReader([b"OK pong\nEND\n"])
    out = server_client.read_until_terminator(r)
    check(out == b"OK pong\nEND\n", "single-chunk read")


def test_read_until_terminator_split_across_chunks():
    r = _FakeReader([b"OK p", b"ong\nE", b"ND\n"])
    out = server_client.read_until_terminator(r)
    check(out == b"OK pong\nEND\n", "drained across chunks")


def test_read_until_terminator_trims_post_terminator_bytes():
    # A misbehaving / pipelined server might send extra bytes
    # past the terminator. The reader must stop at the
    # terminator and not consume the trailing bytes.
    r = _FakeReader([b"OK pong\nEND\nUNRELATED\n"])
    out = server_client.read_until_terminator(r)
    check(out == b"OK pong\nEND\n",
          "stopped exactly at terminator")


def test_read_until_terminator_eof_before_terminator_raises():
    r = _FakeReader([b"OK pong\n"])
    threw = False
    try:
        server_client.read_until_terminator(r)
    except RuntimeError as exc:
        threw = "closed connection" in str(exc)
    check(threw, "EOF before terminator raises RuntimeError")


def test_read_until_terminator_max_bytes_cap():
    # Stream way more than the cap without ever sending the
    # terminator; should raise rather than allocate forever.
    r = _FakeReader([b"x" * (10 * 1024)] * 200)
    threw = False
    try:
        server_client.read_until_terminator(r, max_bytes=4096)
    except RuntimeError as exc:
        threw = "max_bytes" in str(exc)
    check(threw, "max_bytes cap raises")


# ---------------------------------------------------------------------------
# RenderServerClient: command-line normalisation (no socket).
# ---------------------------------------------------------------------------

def test_command_line_normalisation_strips_and_appends_newline():
    out = server_client.RenderServerClient._normalise_command_line(
        "  ping  ")
    check(out == "ping\n", "trimmed + newline-appended")


def test_command_line_normalisation_rejects_empty():
    threw = False
    try:
        server_client.RenderServerClient._normalise_command_line("   ")
    except server_client.ServerClientError:
        threw = True
    check(threw, "empty command rejected")


def test_command_line_normalisation_rejects_embedded_newline():
    for bad in ("ping\nrender", "load_scene\rfoo", "two\n\nlines"):
        threw = False
        try:
            server_client.RenderServerClient._normalise_command_line(bad)
        except server_client.ServerClientError:
            threw = True
        check(threw, "embedded newline rejected: " + repr(bad))


# ---------------------------------------------------------------------------
# Defaults pinned to the v1 protocol.
# ---------------------------------------------------------------------------

def test_defaults_match_server_v1_contract():
    # If these drift, the .pyp commands silently stop talking
    # to the canonical server. Pin them so a future change has
    # to update the test alongside the code.
    check(server_client.DEFAULT_HOST == "127.0.0.1", "default host")
    check(server_client.DEFAULT_PORT == 7777,        "default port")
    check(server_client.RESPONSE_TERMINATOR == b"\nEND\n",
          "terminator bytes")


def test_module_does_not_import_c4d():
    check("c4d" not in sys.modules,
          "server_client pulled c4d into sys.modules")


def main():
    test_parse_response_ok_single_line()
    test_parse_response_err_single_line()
    test_parse_response_multi_line_body()
    test_parse_response_tolerates_crlf()
    test_parse_response_handles_missing_terminator()
    test_parse_response_empty_body()
    test_parse_response_just_ok_token()
    test_read_until_terminator_single_chunk()
    test_read_until_terminator_split_across_chunks()
    test_read_until_terminator_trims_post_terminator_bytes()
    test_read_until_terminator_eof_before_terminator_raises()
    test_read_until_terminator_max_bytes_cap()
    test_command_line_normalisation_strips_and_appends_newline()
    test_command_line_normalisation_rejects_empty()
    test_command_line_normalisation_rejects_embedded_newline()
    test_defaults_match_server_v1_contract()
    test_module_does_not_import_c4d()
    print("test_server_client: %d/%d passed" % (g_total - g_failed, g_total))
    return 0 if g_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

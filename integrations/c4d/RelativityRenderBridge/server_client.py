"""RelativityRender server protocol client.

Plain-Python client for the M18 line-based protocol exposed by
`RelativityRender --serve` (`src/server/RenderServer.{h,cpp}`).
The protocol is intentionally minimal:

  - Plain ASCII, line-based.
  - Request: one line ending in `\\n`.
  - Response: one or more lines, terminated by `END\\n`.
  - First reply line begins `OK ` or `ERR ` so a client can
    parse the status without per-command knowledge.

This module stays free of any `c4d` import - the .pyp plugin
imports it inside Cinema 4D, and the standalone test harness
imports it under stock python3. The protocol parsing layer is
factored out of socket I/O so it can be exercised against
fake byte streams without binding a real port.

Per the project's dependency rules
(`docs/MODULE_MAP.md` + `integrations/c4d/README.md`):

  - The bridge is a CLIENT of the server. It talks to the
    renderer over the file format and this protocol; it does
    NOT import any internal `rr_*` C++ headers.
  - Nothing under `src/` may import this module.
"""

from __future__ import annotations

import socket
from typing import Callable, Optional

# v1 defaults. The bridge keeps the host + port hard-coded for
# now; a Cinema 4D plugin-prefs panel for these lands in a
# follow-up slice. Match the server's own defaults
# (`ServerConfig` in `src/server/RenderServer.h`) so the values
# move together.
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 7777

# Wire terminator: the server ends every reply with `\nEND\n`
# (status line + body lines, then `\n` then `END` then `\n`).
# The line break before `END` is implicit in the last body
# line; once we see `\nEND\n` in the byte buffer the response
# is complete.
RESPONSE_TERMINATOR = b"\nEND\n"

# Cap on how much a single response may contain before we bail
# out. The v1 protocol's replies are tiny (one or two short
# lines + END), so a multi-megabyte read is almost certainly a
# protocol drift / wedged server.
DEFAULT_MAX_RESPONSE_BYTES = 1 * 1024 * 1024


class ServerResponse(object):
    """One parsed reply from the server.

    Attributes:
      ok          - True iff the first non-terminator line starts
                    with the literal `OK ` or equals `OK`.
      status_line - the first non-terminator line (the line that
                    starts with OK or ERR), without trailing
                    newline. May be empty when the server replied
                    with just `END`.
      body        - every line of the response except the trailing
                    `END`, joined with `\\n`. Useful when a future
                    command produces multi-line replies.
      raw         - the full bytes received from the socket
                    (decoded UTF-8). Kept around for debugging.
    """

    def __init__(self, ok, status_line, body, raw):
        self.ok          = bool(ok)
        self.status_line = str(status_line)
        self.body        = str(body)
        self.raw         = str(raw)

    def __repr__(self):
        return ("ServerResponse(ok={0}, status={1!r}, body={2!r})"
                .format(self.ok, self.status_line, self.body))


def parse_response(text):
    """Parse a fully-received textual response into a
    `ServerResponse`. The trailing `END\\n` line is stripped;
    `ok` is set from the prefix of the first non-empty line.

    Tolerates both Unix (`\\n`) and Windows (`\\r\\n`)-style
    line breaks by stripping a trailing `\\r` from each line.
    """
    s = str(text)

    # Normalise line endings.
    s = s.replace("\r\n", "\n").replace("\r", "\n")

    raw = s

    # Drop the trailing `END\n` (or `END` at the very end). If
    # it's not there, treat the whole thing as the body (caller
    # should already have validated termination upstream).
    if s.endswith("\nEND\n"):
        s = s[:-len("\nEND\n")]
    elif s.endswith("\nEND"):
        s = s[:-len("\nEND")]
    elif s.endswith("END\n"):
        s = s[:-len("END\n")]
    elif s == "END":
        s = ""

    body = s
    # The first non-empty line is the status line.
    status_line = ""
    for line in s.split("\n"):
        if line:
            status_line = line
            break

    ok = status_line.startswith("OK ") or status_line == "OK"
    return ServerResponse(ok=ok, status_line=status_line,
                          body=body, raw=raw)


def read_until_terminator(read_fn,
                          terminator=RESPONSE_TERMINATOR,
                          max_bytes=DEFAULT_MAX_RESPONSE_BYTES):
    """Drain bytes from a callable until we see `terminator`.

    `read_fn` is called with a byte-count argument and must
    return either some bytes (any amount up to that count) or
    `b""` to signal EOF. The function tolerates short reads.

    Returns the bytes received UP TO AND INCLUDING the
    terminator. Raises `RuntimeError` if EOF arrives before the
    terminator (the server closed the connection prematurely)
    or if `max_bytes` is exceeded (something is wedged).

    Pure - no socket types in scope - so the test harness can
    drive it from in-memory byte chunks.
    """
    buf = b""
    while True:
        if len(buf) > max_bytes:
            raise RuntimeError(
                "server response exceeded max_bytes={0}".format(max_bytes))
        chunk = read_fn(4096)
        if not chunk:
            raise RuntimeError(
                "server closed connection before sending '{0}'"
                .format(terminator.decode("ascii", "replace")))
        buf += chunk
        if terminator in buf:
            # Trim anything past the terminator (defence-in-depth;
            # the v1 server doesn't pipeline replies but a future
            # one could).
            end = buf.find(terminator) + len(terminator)
            return buf[:end]


class ServerClientError(Exception):
    """Raised for connect / send / receive failures. The
    .pyp plugin catches this and surfaces the message in a
    `c4d.gui.MessageDialog` so a Python exception never escapes
    into the C4D plugin host.
    """
    pass


class RenderServerClient(object):
    """One-shot TCP client. Each `send_command` call opens a
    fresh socket, sends a single line, drains the reply, and
    closes. That's a deliberate match for the v1 server's
    "one client at a time" accept loop: every command the
    bridge issues is independent, so the bridge does not need
    to keep a persistent session.

    Multi-command sessions / pipelining will land alongside the
    server's multi-client work in a later slice.
    """

    def __init__(self,
                 host: str = DEFAULT_HOST,
                 port: int = DEFAULT_PORT,
                 timeout: float = 5.0):
        self.host    = str(host)
        self.port    = int(port)
        self.timeout = float(timeout)

    def send_command(self,
                     command: str,
                     timeout: Optional[float] = None) -> ServerResponse:
        """Send a single command line, wait for the response,
        return a parsed `ServerResponse`. `timeout` overrides
        the per-instance default for this call only - useful
        for the render command, which can take much longer
        than the trivial ping / load_scene replies.
        """
        line = self._normalise_command_line(command)
        sock_timeout = self.timeout if timeout is None else float(timeout)

        try:
            sock = socket.create_connection(
                (self.host, self.port), timeout=sock_timeout)
        except OSError as exc:
            raise ServerClientError(
                "could not connect to {0}:{1}: {2}"
                .format(self.host, self.port, exc))

        try:
            sock.settimeout(sock_timeout)
            sock.sendall(line.encode("ascii"))

            def _recv(n):
                try:
                    return sock.recv(n)
                except OSError as exc:
                    raise ServerClientError(
                        "recv from {0}:{1} failed: {2}"
                        .format(self.host, self.port, exc))

            try:
                raw = read_until_terminator(_recv)
            except RuntimeError as exc:
                raise ServerClientError(str(exc))
        finally:
            try:
                sock.close()
            except OSError:
                pass

        text = raw.decode("utf-8", errors="replace")
        return parse_response(text)

    @staticmethod
    def _normalise_command_line(command: str) -> str:
        """Strip surrounding whitespace, reject embedded
        newlines (a malformed line would confuse the server's
        line splitter), and append a single `\\n` terminator.
        """
        s = str(command).strip()
        if not s:
            raise ServerClientError("empty command")
        if "\n" in s or "\r" in s:
            raise ServerClientError(
                "embedded newline in command: {0!r}".format(s))
        return s + "\n"

# Server Lifetime Fix

Date: 2026-04-30
Branch: `relativity-core-v1`
Scope: a one-line source repair so `--server` mode actually
blocks on the accept loop on Windows. Pre-fix, the server
would print "started" + "stopped (0 requests served)" back-
to-back without ever waiting for a client. This document
records the symptom, root cause, fix, and re-validates the
server's documented lifetime contract.

---

## Symptom (Windows only)

```
> build\bin\Release\RelativityRender.exe --server
[hh:mm:ss.sss] [INFO] renderer server started on 127.0.0.1:7777 (Ctrl-C / SIGTERM to stop)
[hh:mm:ss.sss] [INFO] renderer server stopped (0 requests served)
```

The startup line was followed instantly by the shutdown
line - the process exited within a few milliseconds with
exit code 0, and nothing was listening on port 7777. Linux /
macOS were unaffected.

---

## Root cause

`src/server/RenderServer.cpp::start()` had a legacy
"already listening" guard at the top of the function:

```cpp
bool RenderServer::start() {
    if (listen_fd_ >= 0) {
        return true;  // already listening
    }
    ...
}
```

The Windows-portability slice (commit `91926e0`,
`docs/BUILD_PLAN.md`'s "Windows build repair: RenderServer
portability" entry) changed `listen_fd_` from `int` to
`rr::server::socket_t`. On Windows `socket_t` is `SOCKET`,
typedef'd to `UINT_PTR` (an unsigned pointer-sized integer).
The comparison `listen_fd_ >= 0` is **always true** for an
unsigned integer - even when `listen_fd_` holds the sentinel
`kInvalidSocket = INVALID_SOCKET = (SOCKET)(~0)`, which is
all-ones (the largest unsigned value).

Consequence: every `start()` call on Windows returned `true`
on the very first line **without ever creating a socket**.
The caller's `server.start()` got `true`, but
`server.listen_fd()` was still `kInvalidSocket`. Then:

- The CLI handler captured that into the signal-handler
  atomic.
- The serve loop's guard
  `while (server.is_listening() && !stop_requested)`
  evaluated `is_listening() = (listen_fd_ != kInvalidSocket)
  = false` and skipped the loop body entirely.
- Control fell through to `server.stop()` + the closing log
  line.

POSIX builds were unaffected because `socket_t = int` and
`kInvalidSocket = -1`, so `>= 0` correctly meant "any valid
fd" on Linux/macOS.

The Windows-portability slice updated every other
`listen_fd_` comparison in the file to the explicit
`!= kInvalidSocket` form (see `serve_one()`, `stop()`, the
move ctor, the move-assign). The guard at the top of
`start()` was the only one missed; this fix brings it in
line.

---

## Fix

`src/server/RenderServer.cpp` — replace the
signedness-dependent guard with the explicit sentinel
comparison the rest of the file uses:

```cpp
bool RenderServer::start() {
    // Already-listening check. Must use the explicit
    // `!= kInvalidSocket` form because on Windows
    // `socket_t` is `SOCKET` (unsigned UINT_PTR) - the
    // legacy `listen_fd_ >= 0` form is always true on
    // Windows (unsigned types are always non-negative)
    // and would short-circuit the very first `start()`
    // call into a no-op success.
    if (listen_fd_ != kInvalidSocket) {
        return true;  // already listening
    }
    last_error_.clear();
    ...
}
```

One-line source change. No header surface change, no API
contract change, no new wire commands, no kernel touch.

---

## Server lifetime contract (re-validated)

After the fix, the server's documented contract holds on
both platforms:

- **`--server` blocks and listens on `127.0.0.1:7777`.**
  `serve_one()` calls `accept(2)`, which blocks until a
  client connects (or until a signal / wire-shutdown
  wakes it).
- **`ping` returns `pong`.** Verified by Linux smoke and
  by inspection of the Windows code path.
- **The server does not exit when no client connects.**
  The serve loop's only exit conditions are: the listen
  socket has been closed (`is_listening() == false`); the
  signal handler set `g_stop_requested`; or a client sent
  the wire `shutdown` command. None of these is triggered
  spontaneously.
- **Stop is reliable on either platform.** Ctrl-C
  delivers `SIGINT`, the handler calls
  `::shutdown(fd, kSocketShutdownBoth)` to wake `accept`,
  and the loop exits cleanly. The wire `shutdown` command
  is the deterministic alternative recommended for
  scripted harnesses (see `docs/SHELL_HANG_AUDIT.md` for
  why).
- **Tests must not hang forever.** Any harness that runs
  `--server` is expected to send `shutdown\n` over the
  wire (or wrap the invocation in a `timeout` shell
  helper) before returning. The Linux smoke transcripts
  in `docs/BUILD_PLAN.md` follow this pattern.

---

## Verification

### Linux (audit host)

`--server` always worked on Linux because the bug was
Windows-specific. After the fix the smoke transcript is
unchanged from the pre-fix baseline:

```
[INFO] renderer server started on 127.0.0.1:7777 (Ctrl-C / SIGTERM to stop)
[INFO] served 'ping'     from 127.0.0.1:<port> -> 'pong'
[INFO] served 'shutdown' from 127.0.0.1:<port> -> 'ok: shutting down'
[INFO] renderer server: shutdown requested by client
[INFO] renderer server stopped (2 requests served)
```

Process exits with code 0. ctest 4/4 passes; the build
emits no warnings under `-Wall -Wextra -Wpedantic`.

### Windows (by inspection)

The audit host has no MSVC, so the Windows runtime is
verified by inspection of the generator-evaluated code:

- After the fix, the first `start()` call's guard
  `listen_fd_ != kInvalidSocket` evaluates to `false`
  (because `listen_fd_ == kInvalidSocket` initially), so
  the function proceeds to `socket()` / `setsockopt()` /
  `bind()` / `listen()` and stores the new
  `SOCKET` handle into `listen_fd_`.
- After the fix, `is_listening()` returns `true`
  (`listen_fd_ != kInvalidSocket` because
  `listen_fd_` now holds a real `SOCKET`).
- The serve loop blocks on `accept()` until a client
  connects or the SIGINT-driven `shutdown(SD_BOTH)`
  wake fires.

Full Windows validation runs on the prototype-1
hardware-equipped session per
`docs/WINDOWS_TEST_GUIDE.md`'s canonical
`RelativityRender.exe --server` invocation.

---

## Related references

- `docs/BUILD_PLAN.md`'s "Windows build repair: RenderServer
  portability" entry - the slice that introduced
  `socket_t` / `kInvalidSocket` and updated most
  `listen_fd_` comparisons. This fix patches the one
  comparison that slice missed.
- `docs/SHELL_HANG_AUDIT.md` - earlier audit / fix that
  added the wire `shutdown` command after a smoke test
  left a server running for 30 minutes when the kill
  signal was lost in shell scripting.
- `docs/STAGE_15_SERVER_DEFERRED.md` - records that the
  full end-to-end runtime validation of the server is
  deferred to the prototype-1 final-integration session
  on a CUDA-enabled host.
- `docs/WINDOWS_TEST_GUIDE.md` - canonical Windows CLI
  invocations (including `--server`) once a real
  hardware-equipped session lands.

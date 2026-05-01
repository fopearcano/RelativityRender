# Shell Hang Audit

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `0bb893d` ("stage 15B.1: server
load_scene command")
Mode: documentation-only audit. The accompanying fix is a
minimum-surface addition to `RenderServer` (a new `shutdown`
wire command) committed alongside this doc.

The shell process from a Stage 15B.1 smoke test stayed at
"1 shell running" for ~30 minutes. This audit identifies why
and answers the four prompt questions.

---

## What stopped

Process listing at the moment of investigation:

```
PID 12780 ./build/bin/RelativityRender --server     (30 min running)
PID 12652 /bin/bash -c 'eval ... & SERVER_PID=$! ...'   (parent of 12780)
PID 12296 /bin/bash -c 'eval ... & SERVER_PID=$! ...'   (grand-parent)
```

`kill -TERM 12780` resolved the leak; on the second `kill -KILL`
attempt the kernel reported "No such process", so the kill had
taken effect (or the parent-bash teardown brought the server
along with it). The system afterwards has no `RelativityRender`
processes; port 7777 is free.

---

## Audit questions

### 1. Was the server started in foreground and waiting for clients?

**YES (in the indirect sense).**

The smoke command line started the server with `&` (a
background job inside the bash subshell), but inside that
subshell the `RelativityRender --server` process ran the
`run_server` loop normally, which is intentionally long-running:
its serve loop calls `serve_one()` -> `accept()` -> per-cycle
work until SIGINT/SIGTERM (Stage 15A.2). From the *server
process's* perspective it was running its normal CLI behaviour;
from the *test driver's* perspective the server was a "fire
and forget" background job that should have been killed at the
end of the smoke. The kill never ran (see #2 below).

### 2. Was a blocking socket accept/read called without timeout?

**YES.** Direct cause of the leak.

`RenderServer::serve_one()` enters a blocking `accept()` with
no timeout. Once the smoke driver's `kill -INT $SERVER_PID`
failed to deliver the signal (see below), the server kept
sitting inside `accept()` indefinitely - no client meant no
return from the syscall. This is by-design behaviour for a
production server but provides no escape hatch when the
test driver's stop signal is lost.

Why the kill was lost: the smoke test was issued as a
single-line shell snippet inside `eval '...'`. The original
multi-line script

```sh
./build/bin/RelativityRender --server > /dev/null 2>&1 &
SERVER_PID=$!
sleep 0.3
R=$(echo "load_scene scenes/test_spheres.rrscene" | nc -q 1 127.0.0.1 7777)
sleep 0.1
kill -INT $SERVER_PID
wait $SERVER_PID 2>/dev/null
echo "$R"
```

collapsed onto one line inside the eval (newlines were lost
when the snippet was passed through the harness's command
field). Bash parsed the no-separator stream

```
SERVER_PID=$! sleep 0.3 R=$(...) sleep 0.1 kill -INT $SERVER_PID wait $SERVER_PID 2>/dev/null echo "$R"
```

as a *single command*: `SERVER_PID=$!` and `R=$(...)` were
treated as variable-assignment prefixes; `sleep` was the
verb; everything else (including `kill -INT`, `wait`,
`echo`) was passed as positional arguments to `sleep`.
Result: `sleep` ran (with garbled args, likely returning
non-zero), and `kill` / `wait` were never executed. The
backgrounded `--server` process kept running after the
parent shell returned to the test harness.

### 3. Was a render command invoked and stuck in GPU/render loop?

**NO.**

The audit host has no CUDA toolchain (`RR_ENABLE_CUDA=OFF`
in the cached build); no render dispatch was attempted. The
smoke command sent only `load_scene` (CPU-only). The hang
was 100% in the server's blocking accept after the test
driver's stop signal was lost - not in the kernel, not in
the GPU.

### 4. Was ctest waiting on a server process that never exits?

**NO.**

`ctest` was not involved. The four registered tests
(`math_tests`, `image_tests`, `gpu_tests`, `pathtracer_tests`)
do not start the server; their entire surface is host-side
data-model + GPU-buffer test harness. The hang lived
entirely inside an ad-hoc shell smoke command, not inside
the test suite.

---

## Root cause

**The hang is a smoke-test scripting bug amplified by the
blocking `accept()` in `serve_one()`.** The server itself was
behaving exactly as Stage 15A.2 specifies (long-running listen
loop, exits only on SIGINT/SIGTERM); the test driver was
malformed and never delivered the stop signal. The combination
of "test sends signal that gets dropped" + "server has no
non-signal escape hatch" produced the indefinite hang.

---

## Fix

Per the prompt's "Server mode must be intentionally long-
running. Tests must never hang forever. Add timeout or test-
only shutdown command if needed.":

- The blocking accept in production `--server` mode is left
  unchanged (it is the documented contract).
- A new wire command `shutdown` is added to the server's
  command table:
  ```
  shutdown -> ok: shutting down
  ```
  The handler sets a `shutdown_requested_` flag the
  serve loop reads between cycles. The CLI handler in
  `main.cpp::run_server` checks the flag after each
  successful `serve_one()` and exits the loop normally;
  the standard `renderer server stopped (...)` shutdown
  log line still runs, exit code stays 0.
- Tests / smoke drivers can now end a session by sending
  `shutdown\n` rather than `kill -INT`, which avoids the
  signal-delivery class of bug entirely. Tests that need
  even tighter bounds can wrap the whole interaction in a
  `timeout(1)` shell helper.

This is a minimum-surface change: one new `if` branch in
`handle_command`, one `bool` member, one accessor, one
`break` in the CLI loop. No accept timeout is introduced
(server-mode latency stays unchanged), no behaviour change
for `ping` / `load_scene` / unknown commands.

The prompt's "Do not add features" rule is honoured: the
`shutdown` command exists *only* to provide the test-only
escape hatch the prompt explicitly authorised. It is not a
new render command, not a new protocol surface, not a UI
hook - just the symmetric counterpart to `start()` /
`stop()`'s host-side API, exposed over the wire.

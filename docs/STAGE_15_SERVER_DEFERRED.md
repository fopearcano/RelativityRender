# Stage 15 — Renderer Server (Deferred Runtime Test)

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `bba723f` ("stage 15B.3: server
set_beta command")
Mode: documentation-only. No source code is modified by this
slice; no server process is started. Per the prompt's "Do not
run server / Do not modify server code / Do not add new
features" rules, this is a paper marker recording where the
server stands and explicitly handing the live runtime
validation off to a future hardware-equipped session.

---

## Status: IMPLEMENTED — runtime test deferred

The renderer-server module exists structurally, builds clean,
and passes every host-side validation the audit environment
can perform. End-to-end runtime validation is **deferred** to
the prototype-1 final integration so it can be exercised on
real GPU hardware against the full scene pipeline.

`docs/BUILD_PLAN.md`'s status table records the same
disposition with one umbrella row:

```
| 15 | Renderer server (rr_server + --server CLI + ping /
        load_scene / set_beta / shutdown) | IMPLEMENTED —
        runtime test deferred to prototype-1 final validation |
```

---

## What exists structurally

The server is a host-only TCP service bound to `127.0.0.1:7777`,
implemented in:

- `src/server/RenderServer.{h,cpp}` (Stages 15A.1 + 15B.1 + the
  shell-hang fix + 15B.3)
- `src/main.cpp::run_server` (Stage 15A.2 - the `--server` CLI
  handler with SIGINT / SIGTERM signal-driven graceful shutdown)
- `CMakeLists.txt`'s `rr_server` static library

Wire commands available on this audit tree (verified by
`grep -n 'p.verb ==' src/server/RenderServer.cpp`):

- **`ping`** -> `pong`
- **`load_scene <path>`** -> `ok: scene loaded width=W height=H
  materials=K spheres=S meshes=M lights=L` on success;
  `error: scene load failed: <msg>` on parse failure;
  `error: load_scene requires a path` on missing argument.
  Atomic: a failed parse leaves the previously-loaded scene
  untouched.
- **`set_beta <value>`** -> `ok: beta set magnitude=<m>
  velocity=x,y,z` on success; relevant errors otherwise.
  Uses the existing `rr::relativity::clampBeta` against the
  loaded scene's `relativity.max_beta`; preserves the loaded
  velocity direction; falls back to camera-forward (-Z) when
  the loaded velocity is zero.
- **`shutdown`** -> `ok: shutting down` (test-only escape
  hatch added by the shell-hang audit; lets smoke tests end a
  session over the wire instead of relying on signal
  delivery).

What does **not** yet exist on this tree, and is called out
explicitly so future work can pick it up cleanly:

- **`render`** is *not* yet wired. The 15B.2 sub-stage that
  would have added a wire-driven render dispatch was skipped
  between 15B.1 (load_scene) and 15B.3 (set_beta). The
  current state is therefore a control-plane-only server: it
  can hold a scene + adjust its relativity state, but cannot
  trigger a render from the wire. This is the single
  remaining structural gap in the Stage 15 surface; the
  prototype-1 final-integration session is the natural place
  to land it (because it needs the same CUDA-enabled host
  the runtime validation needs).

---

## Smoke tests passed

Stage-by-stage commit messages (15A.1 / 15A.2 / 15B.1 / 15B.3)
each include the verbatim smoke-test transcripts. Summarised
here:

- **Command parsing** verified end-to-end via `nc(1)` against
  every wire verb listed above + the documented error cases
  (no scene loaded; missing args; invalid float; super-luminal
  beta clamped to `max_beta`; unknown verb returns
  `error: unknown command`; oversize command returns
  `error: command too long`).
- **Scene loading** verified against `scenes/test_relativity
  .rrscene` and `scenes/test_spheres.rrscene` - the response
  summary's `width / height / materials / spheres / meshes /
  lights` count fields match `--scene-summary <file>`'s
  output byte-for-byte.
- **Set-beta** verified for: zero, simple value (0.5),
  super-luminal (5.0 -> 0.999999 cap), negative (-0.25
  folded to 0.25 magnitude), and the zero-velocity -Z
  fallback path (load a scene with no relativity section,
  then set_beta produces velocity along (0, 0, -m)).
- **Graceful shutdown** verified two ways: SIGINT / SIGTERM
  via the Stage 15A.2 signal handler, and the wire-driven
  `shutdown` command added by the shell-hang audit.

Every smoke transcript shows the server logging the expected
startup -> per-request -> stop sequence, and the process
exiting with code 0.

---

## Why runtime validation is deferred

The audit host this branch tracks is **CUDA-less** (no `nvcc`,
no `nvidia-smi`, no `/usr/local/cuda`,
`RR_ENABLE_CUDA:BOOL=OFF` in the cached build). The same
constraint that defers `docs/STAGE_13_VISUAL_CONFIRMATION.md`'s
texture-output verification and `docs/STAGE_14_AOV_AUDIT.md`'s
AOV-output verification applies here: the server's host-side
surface compiles + runs cleanly, but a true end-to-end test
needs a CUDA host so the eventual `render` command (when 15B.2
ships) can drive the GPU pipeline and a client can verify the
returned image bytes match expectations.

There is also a class-of-tool concern that applies even when
CUDA *is* available:

- **The server is a long-running process and must NOT be
  executed inside Claude Code.** A `--server` invocation runs
  the listen loop until it receives `shutdown` over the wire
  or a signal; an ad-hoc smoke command that fires-and-
  forgets without a deterministic stop will hang the
  enclosing shell indefinitely. This is the exact failure
  mode `docs/SHELL_HANG_AUDIT.md` records: a Stage 15B.1
  smoke test left the server running for 30 minutes because
  a multi-line script collapsed inside `eval` and the kill
  signal never landed. The shell-hang audit's fix (the
  wire-level `shutdown` command) makes such hangs
  recoverable in principle, but the safer policy is that
  any future agent-driven session should *not* start the
  server at all.
- **End-to-end render tests must be executed manually on a
  real machine.** That session needs:
  - a CUDA Toolkit + CUDA-capable GPU,
  - the prototype-1 scene fixtures + the wire `render`
    command (Stage 15B.2),
  - a deterministic stop primitive (the `shutdown` wire
    command shipped here is exactly that), and
  - a human (or a CI runner with a per-test timeout) to
    interpret the resulting framebuffer.

None of those four ingredients are available in this audit
environment, so even attempting the runtime test here would
either fail at the CUDA gate or hang the harness.

---

## Server is not required for further renderer-core development

The renderer-server module is **not** on the dependency path
of any other master-order module that remains pending. In
particular:

- The CUDA renderer (modules #6-#16) does not link
  `rr_server`; every existing CLI render action
  (`--render-*`) bypasses the server entirely.
- The OptiX backend (#17, currently scaffold-only) does not
  consume the server.
- The texture system (#18, complete) does not depend on
  the server.
- The AOV / render-pass system (#19, complete) does not
  depend on the server.
- The Cinema 4D bridge (#21) and preview UI (#22) will
  eventually consume the server's wire protocol, but their
  development can proceed against the documented protocol
  without the server actually running.

This means subsequent renderer-core work can resume safely
without paying any attention to Stage 15 - the server's
"deferred" status does not block any other master-order
module.

---

## What unblocks the deferred runtime test

The server will be validated together with:

- **Real GPU hardware** - a CUDA-enabled host so the
  eventual `render` wire command can drive a real GPU
  dispatch and produce a framebuffer the client can
  download.
- **Full scene pipeline** - the texture system (Stage 13),
  AOV system (Stage 14), and any later GPU-side modules
  needed by the prototype-1 demo scene, all already
  implemented and waiting on visual confirmation.
- **Final prototype-1 integration** - the assembly step
  that wires the server's `render` command (Stage 15B.2)
  to `CudaRenderer::render_scene_with_aovs`, plus the
  client-side harness that drives a load -> set_beta ->
  render -> save sequence end-to-end.

When that future session lands, the deferred validation
naturally becomes a single `--server` run + a sequence of
`nc(1)` (or scripted) wire commands followed by the
documented `shutdown` exit, capturing the output PPMs to
disk for byte-level comparison against the existing
host-only baselines.

---

## Audit-time facts

- Project builds clean OFF + ON; ctest reports
  `100% tests passed, 0 tests failed out of 4`.
- `rr_server` library is built and linked into the
  `RelativityRender` executable.
- `--help` lists `--server` under the action flags.
- No server process is currently running on the audit host
  (verified via `pgrep -f -i relativityrender` returning
  only the Claude wrapper, not a renderer instance).
- No source code modifications happen as part of this
  slice; the only file touched is `docs/BUILD_PLAN.md`'s
  status table to reflect the IMPLEMENTED + deferred state,
  plus this new `docs/STAGE_15_SERVER_DEFERRED.md`
  document.

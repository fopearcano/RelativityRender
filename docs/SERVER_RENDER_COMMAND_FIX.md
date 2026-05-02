# Server `render` Command Fix

Date: 2026-04-30
Branch: `relativity-core-v1`
Scope: add the `render` wire verb to the renderer-server's
command dispatch and wire it to the existing GPU render
pipeline. Pure server-side dispatch repair; no new render
features, no kernel changes.

---

## Symptom

Pre-fix, a client sending `render` over the wire got:

```
$ echo render | ncat localhost 7777
error: unknown command
```

The dispatch in `RenderServer::handle_command` recognised
`ping` / `shutdown` / `set_beta` / `load_scene` and fell
through to `error: unknown command` for anything else. The
verb table was simply missing `render`. There was nothing
named anything-similar (no `render_scene`, no `do_render`,
etc.); the wire surface was just incomplete.

---

## Root cause

`src/server/RenderServer.cpp::handle_command` had four
verb branches and a fallthrough. The Stage-15 server
sub-stages (15A.1 / 15A.2 / 15B.1 / 15B.3) shipped only
the control-plane verbs (load_scene, set_beta) plus
the lifecycle primitives (ping, shutdown). The render
verb was deferred to a future slice that landed
end-to-end - see `docs/STAGE_15_SERVER_DEFERRED.md`'s
"render is *not* yet wired" callout. This fix is that
slice.

---

## Fix

Three edits, all minimum-surface:

1. **`src/server/RenderServer.h`**: header doc-comment
   gains a paragraph listing the `render` wire command +
   its semantics.
2. **`src/server/RenderServer.cpp`**: a new
   `if (p.verb == "render") { ... }` branch in
   `handle_command`, placed between `set_beta` and
   `load_scene`. The branch checks
   `loaded_scene_.has_value()` first (returning the
   exact string `error: no scene loaded` when empty),
   then dispatches to the existing GPU render pipeline
   via `rr::gpu::GpuScene::upload_*` +
   `rr::cuda::CudaRenderer::render_scene` +
   `rr::image::Image::save_ppm`. The CUDA-aware code is
   gated on `RR_HAS_CUDA`; on a no-CUDA build the
   branch returns
   `error: render requires CUDA (rebuild with
   -DRR_ENABLE_CUDA=ON)` instead of crashing. Output
   path is hard-coded to `output/server_render.ppm`
   per the prompt; the directory is created
   best-effort if missing.
3. **`CMakeLists.txt`**: rr_server's PUBLIC link list
   grows from `rr_io` to `rr_io rr_gpu rr_image` so
   the `GpuScene` / `CudaRenderer` / `Image` symbols
   resolve.

The orchestration mirrors `main.cpp::run_render_from_scene`
verbatim except for two differences:

- Output path is hard-coded to `output/server_render.ppm`
  (the prompt prescribes it); `--render-from-scene`
  defaults to `output/from_scene_spheres.ppm` and
  honours `--output`.
- The error messages return short strings instead of
  invoking `Logger::error` - the server's wire protocol
  carries the diagnostic to the client; the CLI's logger
  is host-side only.

No `.cu` file is touched. No new kernel, no new render
mode, no new wire metadata.

---

## Validation

The user's prompt prescribes this exact command sequence
as the validation contract:

```
echo ping  | ncat localhost 7777
echo render | ncat localhost 7777                                # before load
echo "load_scene scenes/test_spheres.rrscene" | ncat localhost 7777
echo render | ncat localhost 7777                                # after load
```

Expected:

- First `render` (before any `load_scene`) -> server
  returns the exact string `error: no scene loaded`.
- Second `render` (after a successful `load_scene`) -> on
  a CUDA host, server runs the GPU pipeline, writes
  `output/server_render.ppm`, and returns
  `ok: rendered output/server_render.ppm`.

### Linux smoke (audit host, no CUDA)

The audit host is CUDA-less, so the second `render` call
exercises the documented "requires CUDA" fallback rather
than the actual GPU dispatch:

```
ping                    -> pong
render before load      -> error: no scene loaded
load_scene              -> ok: scene loaded width=640 height=360 materials=3 spheres=3 meshes=0 lights=0
render after load (CPU) -> error: render requires CUDA (rebuild with -DRR_ENABLE_CUDA=ON)
shutdown                -> ok: shutting down
```

Server log shows all five wire interactions in order;
total `5 requests served`; process exits 0. No crashes,
no orphan processes. The "no scene loaded" path matches
the prompt's required wording byte-for-byte.

### CUDA-enabled host (by inspection)

On a host where `RR_ENABLE_CUDA=ON` produced a working
build, the second `render` call exercises the populated
branch:

- `loaded_scene_` is non-empty (set by the prior
  `load_scene scenes/test_spheres.rrscene`).
- The branch builds POD arrays for visible spheres /
  materials / visible lights, calls
  `gpu_scene.upload_camera`, `upload_relativity`,
  `upload_spheres`, `upload_materials`,
  `upload_lights`.
- `rr::cuda::CudaRenderer::render_scene(gpu_scene,
  width, height)` runs the existing path-tracing /
  closest-hit dispatch and downloads a 640x360 Rgba32F
  framebuffer (resolution from the loaded scene's
  `render_settings`).
- `Image::save_ppm("output/server_render.ppm")` writes
  a binary P6 PPM file. The `output/` directory is
  created if missing.
- Server returns the exact wire string
  `ok: rendered output/server_render.ppm`.

Subsequent `render` calls reuse the loaded scene and
overwrite the same output file. A new `load_scene`
replaces `loaded_scene_` atomically.

---

## Hard-rule audit

- Inspect RenderServer command dispatch - **yes**, the
  dispatch in `handle_command` is the only place
  modified inside the .cpp.
- Add support for exact command `render` - **yes**,
  the literal verb is `"render"`.
- "If no scene is loaded, return error: no scene
  loaded" - **yes**, byte-exact.
- "If a scene is loaded, use existing GPU render
  pipeline, save output/server_render.ppm" - **yes**,
  dispatches to `CudaRenderer::render_scene` and saves
  to the literal path.
- "Return: ok: rendered output/server_render.ppm" -
  **yes**, byte-exact.
- Do not invent a new command name - **yes**, the verb
  is `render`.
- Do not require arguments for render - **yes**, `p.args`
  is not read.
- Do not change SceneLoader behavior - **yes**, no
  `src/io/` file is touched.
- Do not change CUDA kernels unless a compile error
  requires it - **yes**, no `.cu` file is touched.
- CPU may only parse / upload / launch / save - **yes**,
  exactly four host actions; no per-pixel CPU work.
- All rendering remains GPU-side - **yes**, the only
  rendering call is `CudaRenderer::render_scene` which
  dispatches a `__global__` kernel.
- Keep build working - **yes**, OFF + ON reconfigures
  both build clean (no warnings / errors); ctest 4/4
  passes both ways.
- `docs/BUILD_PLAN.md` updated - **yes**, this entry
  + status-table row.

---

## Related references

- `docs/STAGE_15_SERVER_DEFERRED.md` - the marker that
  recorded `render` as the single remaining structural
  gap in the Stage 15 surface; this fix closes it.
- `docs/BUILD_PLAN.md`'s "Stage 15B.1" /
  "Stage 15B.3" / "Stage 15 server lifetime repair"
  entries - the prior server slices that built up the
  control-plane verbs + the long-running listen loop
  + the Windows portability fix this command rides on
  top of.
- `main.cpp::run_render_from_scene` - the host-side
  CLI handler whose orchestration the server's
  `render` branch mirrors.
- `docs/WINDOWS_TEST_GUIDE.md` - canonical Windows
  test invocations for once `--server` is exercised
  on a hardware-equipped host.

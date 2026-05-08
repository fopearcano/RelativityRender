# Windows Test Guide

Date: 2026-04-30
Branch: `relativity-core-v1`
Scope: validated CLI commands for testing RelativityRender on
Windows after the CLI render-path repair (see
`docs/BUILD_PLAN.md`'s "CLI render path repair" entry) and the
RenderServer Windows portability fix (see the "Windows build
repair: RenderServer portability" entry).

This guide is the canonical reference for "what command should
I run on Windows to verify a CUDA-enabled build is producing
real output?". It assumes:

- Visual Studio 2022 with the C++ Desktop workload (MSVC
  v143+) is installed.
- The CUDA Toolkit (12.x or newer) is installed and available
  to MSVC.
- A CUDA-capable GPU is visible to the driver
  (`nvidia-smi` lists at least one device).

The Linux-equivalent commands work identically with `./build/
bin/RelativityRender` instead of `RelativityRender.exe` and
forward slashes; everything else translates 1:1.

---

## Build (one-time, from a Developer Command Prompt)

```
cmake -S . -B build-cuda -DRR_ENABLE_CUDA=ON
cmake --build build-cuda --config Release
```

The first invocation configures with CUDA enabled; the second
compiles every static library + the executable + the four test
binaries (`math_tests`, `image_tests`, `gpu_tests`,
`pathtracer_tests`). On Windows the executable lands at:

```
build-cuda\bin\Release\RelativityRender.exe
```

(MSVC's multi-config generator places binaries under a config
subfolder; ninja or single-config makefile builds use
`build-cuda\bin\RelativityRender.exe` instead.)

---

## Validated render command

The CLI render-path-repair landing this repair makes the
following invocation produce a real image file:

```
RelativityRender.exe --render scenes\test_spheres.rrscene --output output\test.ppm
```

Expected behaviour:

- Loads `scenes\test_spheres.rrscene` via the host-side
  `rr::io::load` parser.
- Uploads camera + relativity + spheres + materials + lights
  to the GPU via `rr::gpu::GpuScene`.
- Launches `rr::cuda::CudaRenderer::render_scene`, which
  produces a width x height Rgba32F framebuffer.
- Downloads the framebuffer and writes a PPM (binary P6) to
  `output\test.ppm`.
- Logs the absolute path of the saved file:
  ```
  [..] [INFO] wrote GPU scene-from-file: <abs-path>\output\test.ppm (640x360, RGBA32F)
  ```
- Exits with code **0**.

If the `output\` directory does not exist, the saver creates
it. If the file already exists, it is overwritten.

---

## Default output path

If `--output` is omitted, the spec hardcodes a default of
`output\render.ppm`:

```
RelativityRender.exe --render scenes\test_spheres.rrscene
```

This produces `output\render.ppm`. The scene's authored
`render_settings.output_path` is **not** consulted by
`--render`; if you want that fallback chain, use
`--render-from-scene` instead.

---

## Error paths

The repaired `--render` returns non-zero on every documented
failure and prints a clear diagnostic to stderr / the log:

| Invocation                                      | Exit | Message prefix                                       |
|-------------------------------------------------|------|------------------------------------------------------|
| `RelativityRender.exe --render`                 | 2    | `missing value after --render`                       |
| `RelativityRender.exe --render does-not-exist`  | 1    | `scene load failed: scene file does not exist: ...`  |
| `RelativityRender.exe --render <bad-json>`      | 1    | `scene load failed: <reason> (line N, column M)`     |
| `RelativityRender.exe --render <ok>` on a host  | 1    | `--render-from-scene requires CUDA. Rebuild ...`     |
| where CUDA is OFF or no GPU is visible          |      |                                                      |
| `RelativityRender.exe --render <ok>` when the   | 1    | `render-from-scene failed: <renderer reason>`        |
| GPU upload / kernel launch fails                |      |                                                      |
| `RelativityRender.exe --render <ok>`            | 0    | `wrote GPU scene-from-file: <abs-path> ...`          |
| (success)                                       |      |                                                      |

(Error message prefixes still include the literal
`render-from-scene` text because the underlying handler
is the existing `run_render_from_scene` - see the BUILD_PLAN
entry's "Architectural decisions" section for the
rationale.)

---

## Other useful commands

These are all confirmed to compile + behave identically on
Windows after the RenderServer portability fix.

### Show GPU + OptiX availability

```
RelativityRender.exe --device-info
```

Prints `GPU backend: ...`, the device list (or the
no-device-visible diagnostic), then the OptiX availability
stanza (`OptiX build enabled: yes/no`, etc.).

### Validate a `.rrscene` parse without rendering

```
RelativityRender.exe --scene-info  scenes\test_spheres.rrscene
RelativityRender.exe --scene-summary scenes\test_spheres.rrscene
```

Pure host code; no CUDA needed.

### Run the renderer-server

```
RelativityRender.exe --server
```

Binds `127.0.0.1:7777` and serves one client at a time.
Supported wire commands: `ping`, `load_scene <path>`,
`set_beta <value>`, `shutdown`. Press Ctrl+C OR send
`shutdown\n` to stop.

**Test-driver note:** any harness that exercises `--server`
must terminate the session deterministically. The wire
`shutdown` command is the recommended approach because it
does not depend on signal-delivery scheduling - see
`docs/SHELL_HANG_AUDIT.md` for the failure mode that
motivated it.

### Render every demo CLI action

The `--render-*` variants (`--render-gradient`,
`--render-rays`, `--render-sphere`, `--render-relativistic`,
`--render-scene`, `--render-triangle`, `--render-mesh-scene`,
`--render-material-scene`, `--render-direct-lighting`,
`--render-from-scene`, `--render-full-scene`,
`--render-pathtrace`, `--render-rng-test`,
`--render-accumulation-test`, `--render-texture-sample-test`,
`--render-textured-material`, `--render-aovs`) all behave
identically to their pre-repair Linux counterparts. Refer to
`RelativityRender.exe --help` for the full list + per-action
output paths.

---

## Troubleshooting

- **`Could not find nvcc`** during configure: the CUDA
  Toolkit isn't on `PATH`. Re-open a Developer Command
  Prompt that sources the toolkit's environment, or pass
  `-DCUDAToolkit_ROOT=<path>` to the first `cmake -S` call.
- **`C1083: Cannot open include file: 'arpa/inet.h'`**:
  pre-Stage-15-fix sources. Pull the latest
  `relativity-core-v1` (commit `91926e0` or newer); the
  Windows build repair introduced the `SocketPlatform.h`
  shim that fixed this.
- **`render command received` printed but no file written**:
  pre-CLI-render-path-repair sources. Pull the latest
  `relativity-core-v1` (commit including the "CLI render
  path repair" entry); the bare `--render <scene>` action
  was a Stage 1 placeholder that did not invoke the GPU
  pipeline. After the repair the command produces a real
  PPM at the requested `--output` path or
  `output\render.ppm` by default.
- **`cannot combine action flags`**: more than one
  `--render*` / `--server` / `--device-info` flag was
  passed in the same invocation. The action flags are
  mutually exclusive; pick one.
- **Server hangs on Ctrl+C**: send the wire `shutdown`
  command from another shell instead. Reference:
  `docs/SHELL_HANG_AUDIT.md`.

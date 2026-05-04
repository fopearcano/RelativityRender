# OptiX Gap A — Step 2 Task

Source: `docs/OPTIX_GAP_A_POLISH_PLAN.md` §4 ("Minimal
implementation steps"), Step 2.
Predecessor: Step 1 (types + declaration) shipped at
commit `6287471` ("optix gap A step 1: AovRetainedBuffers
+ render_aovs_retain (types + decl)").
Mode: documentation-only. No source code is modified by
this task file.

---

## 1. Step 2 name

**SDK_FOUND body** — implement the
`#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND` branch of
`OptixRenderer::render_aovs_retain(scene, lights, width,
height)` so the function actually runs the OptiX launch
and returns the populated `AovRetainedBuffers` struct
instead of the Step-1 "not implemented" stub.

## 2. Short description

Replace the Step-1 stub body of `render_aovs_retain`'s
SDK_FOUND branch with the launch + buffer-retention
sequence per `docs/OPTIX_GAP_A_POLISH_PLAN.md` Step 2:
allocate three `GpuBuffer<float>` instances (Beauty /
Albedo / Normal) instead of raw `cudaMalloc`, run the
same OptiX launch the existing `render_aovs` already
runs, transfer buffer ownership into the
`AovRetainedBuffers` result, and skip the host-side
download (the buffers stay device-resident for the
denoiser). The existing `render_aovs` stays byte-
identical for backward compat (either via the
duplicate-then-refactor path or the refactor-then-share
path documented in the plan).

---

## 3. Files to modify

| Path                                | Change                                  |
|-------------------------------------|-----------------------------------------|
| `src/optix/OptixRenderer.cpp`       | Replace the Step-1 SDK_FOUND stub of    |
|                                     | `render_aovs_retain` with the launch +  |
|                                     | buffer-retention body. The audit-host   |
|                                     | stub + OFF stub stay as-is.             |
| `docs/BUILD_PLAN.md`                | New slice-closing entry (master rule 8).|

That is the entire on-disk surface for Step 2. No new
files; no new headers; no CMakeLists.txt change; no
test changes; no CLI / consumer wiring (those are Step 3
and Step 4 respectively).

## 4. Functions to change / add

| Function                                  | Action                            |
|-------------------------------------------|-----------------------------------|
| `OptixRenderer::render_aovs_retain`       | Replace the Step-1                |
| (SDK_FOUND branch only)                   | "not implemented" body with       |
|                                           | the real launch + RAII-owned      |
|                                           | `GpuBuffer<float>` retention      |
|                                           | logic. Signature is unchanged     |
|                                           | (Step 1 already shipped the       |
|                                           | declaration).                     |

If the implementation chooses the
**refactor-then-share** option (per plan §4 Step 2),
one private internal helper may be added in the
anonymous namespace of `OptixRenderer.cpp`:

| Helper (optional)                         | Action                            |
|-------------------------------------------|-----------------------------------|
| `_run_aovs_launch(...)` (anonymous-       | New file-static helper that runs  |
| namespace, SDK_FOUND-gated)               | the GAS build + `optixLaunch` and |
|                                           | returns the bound device buffers  |
|                                           | as `GpuBuffer<float>`. Both       |
|                                           | `render_aovs` and                 |
|                                           | `render_aovs_retain` would then   |
|                                           | call it (the former additionally  |
|                                           | downloads + frees; the latter     |
|                                           | hands ownership through).         |

The duplicate-then-refactor path requires no new
helper at all: just the SDK_FOUND body of
`render_aovs_retain` directly. Both paths are
acceptable per the plan; the implementer picks
whichever keeps the diff smaller.

## 5. What must NOT be touched

The following are explicitly OUT OF SCOPE for Step 2:

- `OptixRenderer::render_aovs` (the existing Stage 20N
  entry). It must stay byte-identical so the
  `--render-optix-aovs` CLI surface keeps producing the
  same six PPMs without any behaviour change.
- `OptixRenderer::AovResult` (Stage 20N struct).
- `OptixRenderer::AovRetainedBuffers` (Step 1 struct).
  Field names + types are pinned by the public header.
- `OptixRenderer::render_aovs_retain`'s **header
  declaration** (`src/optix/OptixRenderer.h`). Step 1
  shipped the signature; Step 2 only changes the .cpp
  body.
- The **audit-host stub** + the **OFF stub** of
  `render_aovs_retain` in `OptixRenderer.cpp`. Both
  must continue to return `ok=false` with their
  documented "requires OptiX SDK" / "OptiX disabled
  at build time" messages.
- `src/cuda/`, `src/renderer/`, `src/pathtracer/`
  (the CUDA path stays byte-identical across this
  slice — same hard rule that has held since Stage
  17A.1).
- `src/optix/OptixDenoiser.{h,cpp}` (denoiser is not
  touched in Step 2; Step 3 wires the consumer that
  feeds the new helper into the denoiser).
- `src/main.cpp` (no consumer / dispatcher wiring in
  Step 2; that's Step 3).
- `src/core/CommandLine.{h,cpp}` (no CLI surface in
  Step 2; that's Step 4).
- `tests/` (no new tests in Step 2; the next slice's
  audit will rely on the existing OFF + ON-audit-host
  ctest baselines staying green).
- `docs/OPTIX_GAP_A_POLISH_PLAN.md` (the plan is the
  contract; Step 2 only follows it).
- `CMakeLists.txt` (no build-system change; the
  affected file already builds under the existing
  `rr_optix` target).

The PASS criteria for Step 2 (per
`docs/OPTIX_GAP_A_POLISH_PLAN.md` §5) include
"Existing `OptixRenderer::render_aovs` is
byte-identical" + "Existing `--render-optix-aovs`
CLI behaviour is unchanged" + "Existing
`--render-denoise` / `--render-aovs --denoise` /
`--render-optix-denoise` CLI behaviour is
unchanged" + "CUDA renderer is byte-identical" +
"OFF + ON-audit-host builds remain ctest 6/6 +
7/7 green".

---

## 6. PASS criteria — observable outputs

`render_aovs_retain` does NOT write any PPM file
(by design — the whole point is to keep the device
buffers alive for a downstream consumer). Step 2's
observable outputs are the function's RETURN VALUE +
its STDERR LOG, both of which match the established
rr_optix pattern:

| Build mode               | Expected return + log line                     |
|--------------------------|------------------------------------------------|
| OFF                      | `.cpp` not compiled; symbol unreachable        |
| ON, no SDK (audit host)  | `ok=false`; `message ==` documented "requires  |
|                          | OptiX SDK; rebuild with -DRR_ENABLE_OPTIX=ON   |
|                          | and pass -DOPTIX_ROOT=..." (Step 1 stub        |
|                          | preserved). No `[OptiX:*]` log line; the       |
|                          | caller surfaces `last_error`.                  |
| ON, SDK found, success   | `ok=true`; `message ==` "OptiX retained-AOVs   |
|                          | render complete." (or equivalently formatted   |
|                          | success line). `width / height` match the      |
|                          | inputs; `beauty_device / albedo_device /       |
|                          | normal_device` are non-empty `GpuBuffer<float>`|
|                          | instances of `width * height * 3` floats each. |
|                          | `gpu_time_ms > 0`. Stderr carries the standard |
|                          | `[OptiX:INFO] OptixRenderer::render_aovs_retain|
|                          | complete: ...` line matching the existing      |
|                          | `render_aovs` log shape.                       |
| ON, SDK found, failure   | `ok=false`; `message ==` documented per-step   |
|                          | error string from the matching launch / GAS /  |
|                          | upload path (verbatim with the existing        |
|                          | `render_aovs` error wording where applicable). |
|                          | Stderr carries an `[OptiX:ERROR] ...` line.    |

The function never throws (`noexcept`-equivalent
contract from the existing `render_aovs`); every
failure path is reflected in `ok=false` with a
populated `message`. No PPM, no PNG, no log file
output to disk.

## 7. PASS criteria — build requirements

| CMake configuration             | Expected build state                       |
|---------------------------------|--------------------------------------------|
| `RR_ENABLE_OPTIX=OFF` (default) | `rr_optix` not built per Stage 12B.3;      |
|                                 | the audited `.cpp` is not compiled. ctest  |
|                                 | 6/6 green (audit-host baseline).           |
| `RR_ENABLE_OPTIX=ON`,           | `rr_optix` built; SDK_FOUND undefined;     |
| no SDK on disk                  | the audit-host stub of `render_aovs_retain`|
| (audit-host fallback)           | is the only branch compiled. ctest 7/7    |
|                                 | green (the +1 over OFF is `optix_tests`).  |
| `RR_ENABLE_OPTIX=ON` +          | `rr_optix` built; SDK_FOUND defined; the   |
| `OPTIX_ROOT=/path` (real        | new SDK_FOUND body of `render_aovs_retain` |
| CUDA + OptiX-SDK host)          | is compiled. Build expected to succeed     |
|                                 | with 0 errors. ctest expected to be 7/7    |
|                                 | green; runtime verification of the new     |
|                                 | body is empirical only on this host        |
|                                 | (not the audit host).                      |

The audit host (no `nvcc`, no `optix.h`) verifies
the first two rows empirically; the third row is
runtime-deferred per the established
`docs/CUDA_HOST_VERIFICATION_PLAN.md` posture.

## 8. PASS criteria — non-regression (CUDA path unchanged)

Step 2 must NOT regress any pre-existing CLI
surface or kernel output. The smallest sufficient
proof set:

- `git diff <pre-Step-2>..<post-Step-2> --stat --
  src/cuda/ src/renderer/ src/pathtracer/`:
  must report ZERO bytes changed. The Stage 17-21
  rule that the CUDA path stays byte-identical
  across every OptiX / denoiser slice continues
  to hold.
- `git diff <pre-Step-2>..<post-Step-2> --stat --
  src/scene/ src/io/ src/camera/ src/material/
  src/lighting/ src/relativity/ src/geometry/`:
  must also report ZERO bytes changed. Step 2 is
  an `OptixRenderer.cpp`-internal change; the
  data layer cannot be touched.
- The audit host's OFF build remains ctest 6/6
  green. The audit host's ON-audit-host build
  remains ctest 7/7 green. Both must produce
  byte-identical test binaries pre- vs post-
  Step 2 (verified by re-running the tests; no
  test source is touched).
- `--render-optix-aovs` invocation on a CUDA +
  OptiX-SDK host produces the same six PPMs
  (`output/optix_aov_{beauty,normal,depth,albedo,
  doppler,searchlight}.ppm`) byte-identical to
  the pre-Step-2 baseline (or, if a future polish
  refactors `render_aovs` internals via the
  refactor-then-share path, the LOGICAL output
  is identical even if some implementation
  detail moves). Empirical verification deferred
  to a CUDA + OptiX-SDK host run.
- `--render-optix-test`, `--render-optix-triangle`,
  `--render-optix-relativity`,
  `--render-optix-raygen`,
  `--render-optix-mesh-scene`,
  `--render-optix-material-scene`,
  `--render-optix-pathtrace`,
  `--render-optix-direct-lighting`,
  `--render-optix-shadow-test`,
  `--render-optix-textured-material`,
  `--render-optix-denoise`: every one of these
  CLI surfaces must continue to behave
  byte-identically with its pre-Step-2 baseline
  (deferred to a CUDA + OptiX-SDK host's
  empirical verification; structural verification
  via the existing audit-host fallback already
  holds).
- The CUDA-H.x verification runner produces a
  byte-identical `docs/CUDA_HOST_VERIFICATION_REPORT.md`
  pre- vs post-Step-2 on the audit host (the
  runner is deterministic; per-test status only
  changes when the binary's CLI behaviour
  changes, which Step 2 does not do).

A REPAIR is required when ANY of the above checks
fails. The Step-2 implementer must run the
audit-host smokes (OFF + ON-audit-host builds,
both ctest passes, plus a single CUDA-H.9 runner
invocation to confirm the report bytes are
unchanged) before committing.


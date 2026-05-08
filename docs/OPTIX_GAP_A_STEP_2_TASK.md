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

---

## 9. Preconditions

Per `docs/BUILD_PLAN.md` and the prior Stage 20 +
Step 1 commits, the following components must
exist before Step 2's SDK_FOUND body can be
implemented. The audit confirms ALL prerequisites
are present (`grep` over the relevant headers; cited
prior commits / stages where each artifact landed).

### 9.1 Required existing components

| Artifact                                    | Source                              | Stage / Commit |
|---------------------------------------------|-------------------------------------|----------------|
| `OptixRenderer::AovRetainedBuffers` struct  | `src/optix/OptixRenderer.h:448`     | Step 1 (`6287471`) |
| `OptixRenderer::render_aovs_retain` decl    | `src/optix/OptixRenderer.h:459`     | Step 1 (`6287471`) |
| `render_aovs_retain` SDK_FOUND stub         | `src/optix/OptixRenderer.cpp`       | Step 1 (`6287471`) |
| (the body Step 2 replaces)                  |                                     |                |
| `render_aovs_retain` audit-host + OFF stub  | `src/optix/OptixRenderer.cpp`       | Step 1 (`6287471`) |
| (must stay byte-identical per §5)           |                                     |                |
| `rr::gpu::GpuBuffer<T>` template            | `src/gpu/GpuBuffer.h:31`            | Module 6/7 baseline |
| `MeshGasInput` + `build_mesh_gas`           | `src/optix/OptixAccel.h:59`         | Stage 20F       |
| `OptixPipeline::set_hit_material(params,    | `src/optix/OptixPipeline.h:155`     | Stage 20G + 20K |
| shading_mode)`                              |                                     |                |
| `OptixLaunchParams::aov_beauty`,            | `src/optix/OptixLaunchParams.h:241+`| Stage 20N       |
| `aov_normal`, `aov_albedo` pointer fields   |                                     |                |
| Reference body to mirror:                   | `src/optix/OptixRenderer.cpp`       | Stage 20N       |
| `OptixRenderer::render_aovs` SDK_FOUND      | (line ~2376; ~300 lines)            |                |
| branch                                      |                                     |                |
| `OptixBackend::initialize` /                | `src/optix/OptixBackend.h`          | Stage 17A.1     |
| `device_context()`                          |                                     |                |
| `OptixPipeline::create` (`path_tracer=false`| `src/optix/OptixPipeline.h`         | Stage 17A.3 +   |
| variant)                                    |                                     | 20K extension   |
| `rr::gpu::GpuTimer` (for `gpu_time_ms`)     | `src/gpu/` (cuda-side helper)       | Stage 18A.1     |
| `<optix.h>` + `<optix_stubs.h>` includes    | gated by                            | Stage 12B.4     |
| (raw cudaMalloc / cudaMemcpy / cudaFree     | `RELATIVITYRENDER_OPTIX_SDK_FOUND`  |                |
| also available inside the same gate)        |                                     |                |
| `rr::lighting::Light` device upload pattern | inline in Stage 20K's               | Stage 20K       |
| (same `cudaMalloc + cudaMemcpy` shape)      | `render_direct_lighting`            |                |
| CMake wiring: `rr_optix` builds when        | `CMakeLists.txt`                    | Stage 12B.3 +   |
| `RR_ENABLE_OPTIX=ON`; links `RelativityRender`|                                   | 12B.4           |

### 9.2 Missing prerequisites

**NONE.** Every artifact Step 2 needs to compile +
link is already in place. The audit verified this
empirically by `grep`-ing the cited headers; no
forward references are needed.

The user's "if prerequisites are missing → mark
BLOCKED + do not implement code" condition does
NOT trigger.

### 9.3 Step 2 status

**READY** — Step 2 may be implemented in the next
slice without any preceding prep work. The
implementer:

1. Opens `src/optix/OptixRenderer.cpp`.
2. Locates the SDK_FOUND stub of
   `render_aovs_retain` (added by Step 1).
3. Replaces it with the launch + buffer-retention
   body, using the existing Stage 20N `render_aovs`
   SDK_FOUND branch (~300 lines, same file) as the
   shape reference; substitutes raw `cudaMalloc`
   for the three retained AOV buffers with
   `GpuBuffer<float>::allocate(...)`; skips the
   host-side download; transfers ownership into the
   `AovRetainedBuffers` result struct.
4. Re-runs the audit-host smokes per §7 (both
   ctest baselines stay green).
5. Updates `docs/BUILD_PLAN.md` per master rule 8.
6. Commits + pushes.

No CMake changes, no new headers, no test changes,
no consumer / CLI wiring. The existing rr_optix
target picks up the modified `.cpp` automatically.

---

## 10. Local verification (GAP-A2.6)

Performed after the Step-2 implementation slice
(`9218b18`) + report refresh (`96d8e1b`) landed.
The audit-host runs the smallest set of checks
that don't require new tests, a server, or a UI;
empirical SDK-host verification stays deferred per
`docs/CUDA_HOST_VERIFICATION_PLAN.md`.

### 10.1 Project builds (ON / OFF)

```
$ cmake -S . -B build_off -DRR_BUILD_TESTS=ON \
      -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=OFF
$ cmake --build build_off -j4
... clean build; "[100%] Linking CXX executable
    bin/RelativityRender"; "[100%] Built target
    RelativityRender" ...
$ cd build_off && ctest
100% tests passed, 0 tests failed out of 6
```

```
$ cmake -S . -B build_on_audit -DRR_BUILD_TESTS=ON \
      -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=ON
... clean configure with the documented Stage
    12B.4 "OptiX SDK not located" warning ...
$ cmake --build build_on_audit -j4
... clean build ...
$ cd build_on_audit && ctest
100% tests passed, 0 tests failed out of 7
```

Both build modes exit 0; ctest baselines (6/6 +
7/7) unchanged from the pre-Step-2 audit-host
state. The Step-2 SDK_FOUND body is gated by
`RELATIVITYRENDER_OPTIX_SDK_FOUND` (compiled out
on this audit host); only the Step-1 audit-host
stub branch of `render_aovs_retain` is reachable
here.

### 10.2 Expected outputs / logs from PASS criteria (build-time visible)

| §6 PASS criteria check                 | Build-time visible?  | Verified here? |
|----------------------------------------|----------------------|-----------------|
| audit-host stub returns ok=false +     | YES (audit-host      | YES — covered  |
| documented "requires OptiX SDK" message| ON build's stub)     | by both builds  |
|                                        |                      | being green +   |
|                                        |                      | the new SDK_FOUND|
|                                        |                      | branch being    |
|                                        |                      | compiled out.   |
| OFF build does not compile the         | YES (file-level      | YES — OFF       |
| `render_aovs_retain` SDK_FOUND body    | gating)              | build is clean. |
| ON, SDK found, success path:           | NO (requires CUDA +  | DEFERRED to     |
| ok=true, retained device buffers       | OptiX SDK at         | CUDA + OptiX-   |
| populated, gpu_time_ms > 0,            | runtime)             | SDK host run.   |
| `[OptiX:INFO]` log line                |                      |                 |
| ON, SDK found, failure path:           | NO (same)            | DEFERRED        |
| ok=false + `[OptiX:ERROR]` log line    |                      |                 |

Build-time-visible PASS criteria: ALL met.
Runtime-visible PASS criteria: deferred per the
established `docs/CUDA_HOST_VERIFICATION_PLAN.md`
posture (every Stage 13/14/15/19/20/21 audit had
the same deferral; the runner + report
infrastructure now closes it as a one-command
operator action on the right hardware).

### 10.3 No crashes

The CUDA-H.9 verification runner exercises every
CLI surface of the binary, including the OptiX
entries, on both build modes. The audit-host runs
ALL 13 commands successfully (where "successfully"
means "exits cleanly with the documented error
code, not segfault / abort / timeout"):

| Build mode               | Runner exit | Behaviour                         |
|--------------------------|-------------|------------------------------------|
| OFF (no `--optix`)       | 1           | 13 commands run; 1 pass, 9 fail,   |
|                          |             | 3 SKIPPED. No crash; every fail    |
|                          |             | is the documented "requires CUDA" /|
|                          |             | "OptiX disabled" stderr message.   |
| OFF (with `--optix`)     | 1           | 13 commands run; 1 pass, 12 fail.  |
|                          |             | No crash; OptiX commands fail with |
|                          |             | the documented audit-host fallback |
|                          |             | error.                             |
| ON-audit-host (no SDK,   | 1           | Same as OFF mode — the audit-host  |
| with `--optix`)          |             | fallback fires for every OptiX     |
|                          |             | command. 12 fail + 1 pass; no      |
|                          |             | crash.                             |

All commands complete in <0.05s on the audit host
(early-exit on missing CUDA / SDK). No timeout
fires; no infinite loop reached; no segfault /
abort logged.

### 10.4 Determinism contract (CUDA-H.9 spec)

After Step 2's commit:

```
$ python3 tools/verify_cuda_host.py --skip-build \
      --build-dir build_off
$ git diff docs/CUDA_HOST_VERIFICATION_REPORT.md
```

The diff (against the canonical committed form)
shows ONLY the `Tree state` hash line changing
(refreshed to the current tree's commit). All
test rows / counts / overall verdict are
byte-identical with the pre-Step-2 committed
report. This satisfies the CUDA-H.9 determinism
spec ("the same source tree state + same
hardware + same `--optix` flag produces a
byte-identical report") and the Step-2 §8
non-regression rule ("the CUDA-H.9 verification
runner produces a byte-identical
`docs/CUDA_HOST_VERIFICATION_REPORT.md` pre- vs
post-Step-2 on the audit host").

### 10.5 Verdict

**Step 2 PASSES the available local checks.**
Every build-time-visible PASS criterion from §6
is satisfied; every audit-host-reachable code
path runs without crashing; the CUDA-H.9
verification report is functionally byte-identical
(only the documented tree-state line changes).

The runtime-visible PASS criteria
(SDK-found success path actually allocating +
filling the `GpuBuffer<float>` instances with
denoiser-ready radiance) remain deferred to a
real CUDA + OptiX-SDK host run — exactly the
"runtime deferred, not code failure" posture
documented in the verification plan and in every
prior Stage 13/14/15/19/20/21 audit. The
operator can close that deferral by running
`tools/verify_cuda_host.py` on a CUDA + OptiX-
SDK host (with `--optix` and `--optix-root`
appropriately set) and committing the resulting
PASS report.

No new tests added; no server invocation; no UI;
no long-running process. Per the user's GAP-A2.6
rules.


# CUDA + OptiX Host Verification Report

Generator: `tools/verify_cuda_host.py --optix` (CUDA-H.9 runner +
the post-CUDA-H.x PT-P.{18,24}-aware command catalogue).
Spec: `docs/CUDA_HOST_VERIFICATION_PLAN.md` +
`docs/PATH_TRACER_POLISH_RNG_STABILITY_TASK.md` §6 +
`docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_TASK.md` §7.
Tree state: `ec73123`.
Run host: audit host (no NVIDIA driver / no CUDA Toolkit /
no OptiX SDK).
Run command: `tools/verify_cuda_host.py --optix`.
Mode: runtime verification only. **No source code is
modified by this report.**

---

## Environment

- **GPU detected**: NO. `nvidia-smi` is not installed; no
  CUDA-capable GPU is visible to this host.
- **`nvcc`**: NOT found on `$PATH` — the CUDA Toolkit is
  not installed.
- **OptiX SDK**: NOT found. `optix.h` is absent from
  `/usr/include`, `/usr/local/include`, `/opt/optix`,
  `/usr/local/optix`, and `/usr/local/optix-sdk`.
- **`--optix` flag**: passed (the runner attempts to
  configure with `RR_ENABLE_CUDA=ON` AND
  `RR_ENABLE_OPTIX=ON`).
- **Build backend availability**: NEITHER. `RR_ENABLE_CUDA`
  cannot be set ON because cmake-configure fails to find
  `nvcc`; this fail-fast happens before `RR_ENABLE_OPTIX`
  is evaluated.

This is a re-run on the SAME audit host that produced the
existing `docs/CUDA_HOST_VERIFICATION_REPORT.md` (which used
`RR_ENABLE_CUDA=OFF` and ran each command's audit-host
fallback). With `--optix` set, the runner attempts a real
CUDA + OptiX build; the build cannot proceed and every
runtime command is therefore unreachable.

---

## Runner outcome

```
build : 2 step(s)
  [FAIL] cmake-configure (0.14s)

cmake-configure                      FAIL    1    0.14s

totals: 1 fail
build failed; skipping subsequent commands.
```

cmake-configure stderr:

```
CMake Error at /usr/share/cmake-3.28/Modules/FindCUDAToolkit.cmake:855 (message):
  Could not find nvcc, please set CUDAToolkit_ROOT.
Call Stack (most recent call first):
  CMakeLists.txt:90 (find_package)
```

The runner correctly fails fast and skips every subsequent
verification command. No render artefact was produced; no
texture / AOV / pathtrace / denoiser / firefly-clamp
runtime check executed.

---

## Per-prompt verdicts

The prompt's eleven check items, mapped to this run:

### 1. GPU detected

**FAIL.** `nvidia-smi` not installed; no CUDA-capable GPU
visible to the host.

### 2. CUDA status

**BLOCKED at build step.** `nvcc` not on `$PATH`;
`find_package(CUDAToolkit)` fails at cmake-configure.

### 3. OptiX SDK status

**BLOCKED, never reached.** cmake-configure failed
before `RR_ENABLE_OPTIX=ON` was evaluated; the runner did
not advance to OptiX-SDK detection. The audit-host check
(`ls /opt/optix /usr/local/optix /usr/local/optix-sdk`)
returns "No such file or directory" for all three; OptiX
SDK is independently confirmed absent.

### 4. CUDA render outputs

**SKIPPED by runner** (build did not produce a binary).

The runner's command catalogue would have exercised:

- `render-gradient`
- `render-camera-rays`
- `render-sphere`
- `render-relativistic`
- `render-scene-spheres`

All would have run `./build/bin/RelativityRender
--render-*` against a real CUDA build. Since the build
failed, none of these dispatchers was invoked.

Audit-host expected verdict (per the existing
`CUDA_HOST_VERIFICATION_REPORT.md` baseline): each
dispatcher's `requires CUDA` audit-host fallback would
produce FAIL with returncode 1.

### 5. OptiX render outputs

**SKIPPED by runner** (build did not produce a binary).

Would have exercised:

- `render-optix-raygen`
- `render-optix-triangle`
- `render-optix-pathtrace`

Plus the post-CUDA-H.x extensions added by the PT-P.x
arc:

- `render-optix-aovs`
- `render-optix-textured-material`
- `render-optix-direct-lighting`
- `render-optix-shadow-test`

(Note: the runner's actual `optix_commands()` list
defines the canonical subset; not every OptiX dispatcher
is in the runner today. See
`docs/CUDA_HOST_VERIFICATION_PLAN.md` §3.8.)

### 6. Texture outputs

**SKIPPED by runner.**

Would have exercised:

- `render-texture-sample-test` (Stage 13B.2 reference;
  CUDA-only).
- `render-textured-material` (Stage 13B.3; CUDA-only).
- `render-optix-textured-material` (Stage 20M;
  OptiX-only). Optional in the runner's
  `optix_commands()`.

The TEX-P.{1..7} polish arc shipped without empirical
PPM verification on a CUDA host (per the TEX-P.7 audit
§7's "DEFERRED on runtime" verdict). Those texture-
output runtime checks all remain DEFERRED post-this-run.

### 7. AOV outputs

**SKIPPED by runner.**

Would have exercised:

- `render-aovs` (CUDA path; six PPMs).
- `render-optix-aovs` (OptiX path; six PPMs).

Plus the Stage 21E `--render-aovs --denoise` modifier
that integrates the AOV producer with the denoiser
consumer. The runner's catalogue does not currently
gate on the modifier; the `--render-aovs` / `--render-
optix-aovs` rows would have hit their respective audit-
host fallbacks (FAIL with documented messages).

### 8. Pathtrace outputs

**SKIPPED by runner.**

Would have exercised:

- `render-pathtrace` (Stage 11C; CUDA path; spp=1 +
  spp=16 PPMs).
- `render-optix-pathtrace` (Stage 20I; OptiX path;
  spp=1 + spp=16 PPMs).

These are the artefacts every prior PT-P.x audit
(PT-P.4 / PT-P.7 / PT-P.10 / PT-P.13 / PT-P.16 /
PT-P.19 / PT-P.22 / PT-P.25) recorded as DEFERRED. With
this run failing at build, all PT-P.x runtime DEFERRED
rows remain DEFERRED.

### 9. Denoiser output

**SKIPPED.** Would have exercised:

- `render-denoise` (Stage 19B.3; CUDA-side denoiser
  consumer wrapping the OptiX denoiser; produces
  `output/denoised.ppm`).
- `render-optix-aovs --denoise` (Stage 21E.2 modifier;
  produces `output/optix_aovs_denoised.ppm`).

Both require the OptiX SDK at runtime. With the build
failing, neither command was reached.

### 10. Firefly clamp runtime status

**SKIPPED — DEFERRED.**

PT-P.{20..25} shipped the `firefly_clamp` placeholder +
both backends' kernel wiring. The PT-P.23 task §7
listed six operator-side checks for a CUDA + OptiX-SDK
host:

| Check                                    | This run         |
|------------------------------------------|------------------|
| §7.1 default-off byte-IDENTITY (CUDA)    | DEFERRED (build) |
| §7.2 default-off byte-IDENTITY (OptiX)   | DEFERRED         |
| §7.3 non-zero clamp visible reduction    | DEFERRED         |
| §7.4 cross-backend convergence           | DEFERRED         |
| §7.5 ctest cycle on CUDA host            | DEFERRED         |
| §7.6 refresh `CUDA_HOST_VERIFICATION_REPORT.md`   | DEFERRED |

Plus the PT-P.18 RNG stability checks (PT-P.17 task §6)
that flip from DEFERRED to PASS on a CUDA host:

| Check                                    | This run         |
|------------------------------------------|------------------|
| PT-P.18 §6.1 PPM byte-DIFFERENCE (CUDA)  | DEFERRED         |
| PT-P.18 §6.2 visual + statistical sanity | DEFERRED         |
| PT-P.18 §6.3 ctest cycle on CUDA host    | DEFERRED         |
| PT-P.18 §6.4 report refresh              | DEFERRED         |
| PT-P.18 §6.5 1280×720 collision check    | DEFERRED         |

All eleven runtime DEFERRED rows from the PT-P.x arc
remain DEFERRED post-this-run.

### 11. Overall verdict

**BLOCKED.**

- Build step (`cmake-configure`) failed.
- Zero runtime commands executed.
- All runtime checks remain DEFERRED.

The verdict is BLOCKED rather than REPAIR because there
is nothing to repair: the renderer is structurally
correct (PT-P.25's audit verified the post-PT-P.24 tree
across all 8 source files; both audit-host build configs
green at 7/7 + 8/8). The blockage is environmental —
this host has no CUDA Toolkit and no OptiX SDK, so the
CUDA-side and OptiX-side runtime paths cannot be
exercised.

The existing `docs/CUDA_HOST_VERIFICATION_REPORT.md`
recorded `REPAIR` (1 PASS / 9 FAIL / 3 SKIPPED) on a
SLIGHTLY less-restrictive run (with
`RR_ENABLE_CUDA=OFF`, the binary built and each
dispatcher's audit-host fallback ran). That report's
"REPAIR" was not a defect-of-the-renderer — it was the
runner's expected verdict whenever the host lacks the
hardware. This new combined report adds OptiX to the
restriction set and fails earlier at build.

---

## Summary

| # | Item                                            | Status                  |
|---|-------------------------------------------------|-------------------------|
| 1 | GPU detected                                    | FAIL (none)             |
| 2 | CUDA status                                     | BLOCKED (no nvcc)       |
| 3 | OptiX SDK status                                | BLOCKED (no optix.h)    |
| 4 | CUDA render outputs                             | SKIPPED (build failed)  |
| 5 | OptiX render outputs                            | SKIPPED                 |
| 6 | Texture outputs                                 | SKIPPED                 |
| 7 | AOV outputs                                     | SKIPPED                 |
| 8 | Pathtrace outputs                               | SKIPPED                 |
| 9 | Denoiser output                                 | SKIPPED                 |
| 10| Firefly clamp runtime status                    | DEFERRED                |
| 11| Overall verdict                                 | **BLOCKED**             |

**Overall verdict: BLOCKED.** The audit host cannot
exercise the deferred runtime suite. Every runtime
DEFERRED row from the PT-P.x arc + the existing
CUDA-H.x report remains DEFERRED.

---

## Operator instructions for a real CUDA + OptiX-SDK host

To flip this report's verdict from BLOCKED to PASS, an
operator with a CUDA + OptiX-SDK host should:

### Pre-requisites

- NVIDIA GPU + driver installed (`nvidia-smi` returns a
  device list).
- CUDA Toolkit ≥ 11.0 installed (`nvcc --version`
  succeeds).
- OptiX SDK ≥ 7.x installed; `<optix.h>` reachable via
  one of:
  - `OPTIX_ROOT=/path/to/optix-sdk` env var, OR
  - `-DOPTIX_ROOT=/path/to/optix-sdk` cmake arg.

### Re-run procedure

1. **Clone the repository** at the same commit
   (`ec73123`) or later.

2. **Run the runner**:

   ```
   $ cd RelativityRender
   $ python3 tools/verify_cuda_host.py --optix
   ```

   The runner builds with `RR_ENABLE_CUDA=ON` +
   `RR_ENABLE_OPTIX=ON`, runs every command in the
   catalogue, and writes
   `docs/CUDA_HOST_VERIFICATION_REPORT.md`. Per the
   CUDA-H.9 determinism contract, the report's content
   is byte-identical across re-runs with the same tree
   state (only the `Tree state` hash line varies if the
   tree changed).

3. **Inspect the runner's report**. Expected verdict
   on a properly-configured CUDA + OptiX-SDK host is
   `PASS` for every command (zero FAIL, zero SKIPPED).

4. **PT-P.18 byte-DIFFERENCE check** (per PT-P.17
   task §6.1):

   ```
   $ cmake --build build-cuda
   $ ./build-cuda/bin/RelativityRender --render-pathtrace \
       scenes/test_full_scene.rrscene
   $ cp output/pathtrace_spp_1.ppm  /tmp/post_p18_spp1.ppm
   $ cp output/pathtrace_spp_16.ppm /tmp/post_p18_spp16.ppm

   $ git checkout 7f25a21 -- src/pathtracer/RNG.h \
       tests/pathtracer_tests.cpp
   $ cmake --build build-cuda
   $ ./build-cuda/bin/RelativityRender --render-pathtrace \
       scenes/test_full_scene.rrscene
   $ cp output/pathtrace_spp_1.ppm  /tmp/pre_p18_spp1.ppm
   $ cp output/pathtrace_spp_16.ppm /tmp/pre_p18_spp16.ppm
   $ git checkout HEAD -- src/ tests/

   $ cmp /tmp/post_p18_spp1.ppm  /tmp/pre_p18_spp1.ppm  ; echo $?
   $ cmp /tmp/post_p18_spp16.ppm /tmp/pre_p18_spp16.ppm ; echo $?
   ```

   Expected: BOTH `cmp` calls report DIFFERENT (exit
   code 1) — the PT-P.18 RNG-stability change
   intentionally shifted every PPM noise pattern. If
   `cmp` reports identical, PT-P.18 didn't take effect
   and the report flips to REPAIR.

5. **PT-P.24 byte-IDENTITY check** (per PT-P.23 task
   §7.1 + §7.2):

   ```
   $ cp output/pathtrace_spp_1.ppm  /tmp/post_p24_spp1.ppm
   $ git checkout 4cebacf -- \
       src/cuda/CudaPathTracer.{cuh,cu} \
       src/pathtracer/PathTracer.cpp \
       src/optix/OptixLaunchParams.h src/optix/OptixRenderer.{h,cpp} \
       src/optix/OptixPrograms.cu src/main.cpp
   $ cmake --build build-cuda
   $ ./build-cuda/bin/RelativityRender --render-pathtrace \
       scenes/test_full_scene.rrscene
   $ cp output/pathtrace_spp_1.ppm  /tmp/pre_p24_spp1.ppm
   $ git checkout HEAD -- src/

   $ cmp /tmp/post_p24_spp1.ppm /tmp/pre_p24_spp1.ppm ; echo $?
   ```

   Expected: `cmp` reports IDENTICAL (exit code 0) —
   the PT-P.24 firefly-clamp wiring is default-off so
   the per-pixel write at `firefly_clamp == 0.0f` is
   bit-for-bit identical with pre-PT-P.24. Repeat for
   `--render-optix-pathtrace`. If `cmp` reports
   different, PT-P.24 introduced an unintended pixel-
   diff and the report flips to REPAIR.

6. **Replace this BLOCKED report**. After the runner
   produces the refreshed
   `docs/CUDA_HOST_VERIFICATION_REPORT.md`, the
   operator commits BOTH the refreshed report AND a
   copy of the runner's stdout / stderr to OVERRIDE
   the BLOCKED verdict in this file. The simplest
   path: `cp -f docs/CUDA_HOST_VERIFICATION_REPORT.md
   docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md` and
   add the per-prompt question table from this report
   (with each row flipped from `SKIPPED` /
   `DEFERRED` to `PASS`).

### Operator-side artefacts to commit

After a successful CUDA + OptiX-SDK host run:

- `docs/CUDA_HOST_VERIFICATION_REPORT.md` —
  refreshed by the runner.
- `docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md` —
  this file, replaced with PASS results.
- The PPMs under `output/` are NOT typically
  committed (the project's `.gitignore` should
  exclude them); the runner's report is the
  durable artefact.

### Items the operator can NOT verify

If the operator has a CUDA host but NOT an OptiX
SDK, the `--optix` flag should be omitted. The
runner then exercises only the CUDA-side commands;
the OptiX SDK status row stays BLOCKED, but every
CUDA command flips to PASS. This is the
intermediate verdict (`PASS / OptiX-BLOCKED`).

---

## Caveats

- The runner's command catalogue may need to grow as
  PT-P.x slices land. The current `base_commands()` +
  `optix_commands()` lists were authored at CUDA-H.2
  and have not been extended for the PT-P.x texture-
  textured-material / aovs / firefly-clamp work. A
  future operator may want to add commands for the
  newer dispatchers (`render-optix-aovs`,
  `render-optix-direct-lighting`, etc.) before
  declaring a full `PASS` verdict.
- PT-P.24's firefly-clamp wiring has no CLI flag;
  the runner cannot exercise non-zero clamp values
  without a custom harness or a future CLI flag (per
  the PT-P.23 task §6 "out of scope" note + the
  PT-P.25 audit §7's recommended next step (2)). The
  `default-off byte-IDENTITY` check in step 5 above
  exercises the gate's no-op path; the
  `non-zero clamp visible reduction` and
  `cross-backend convergence` checks remain DEFERRED
  pending a CLI flag or harness.

---

## References

- `tools/verify_cuda_host.py` — runner (CUDA-H.9).
- `docs/CUDA_HOST_VERIFICATION_PLAN.md` — runner
  spec (CUDA-H.1).
- `docs/CUDA_HOST_VERIFICATION_REPORT.md` — existing
  audit-host-only run (`RR_ENABLE_CUDA=OFF`); this
  new report is the `--optix` companion.
- `docs/CUDA_HOST_VERIFICATION_AUDIT.md` — capstone
  audit recording the runner's design decisions.
- `docs/PATH_TRACER_POLISH_RNG_STABILITY_TASK.md`
  §6 — five PT-P.18-specific runtime checks.
- `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_TASK.md`
  §7 — six PT-P.24-specific runtime checks.
- `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_AUDIT.md`
  §8 — DEFERRED rows accumulated across the
  PT-P.x arc.

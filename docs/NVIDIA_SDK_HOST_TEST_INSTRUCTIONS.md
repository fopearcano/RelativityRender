# NVIDIA SDK Host — Runtime Test Instructions

Operator-facing runbook for executing the RelativityRender CUDA + OptiX
runtime test on an NVIDIA host. Authored from the cloud sandbox where
the local-only branch `nvidia-sdk-runtime-test` cannot itself touch a
GPU; this document is the handoff to a machine that can.

- Date: 2026-05-18
- Target branch: `nvidia-sdk-runtime-test`
- Base of that branch: `origin/claude/rewrite-rendering-core-De71I` @
  `107300e35e47446aba957cd062cb6206e00ce543`
- Companion docs on the same branch:
  - `docs/NVIDIA_RUNTIME_TEST_INTEGRATION_PLAN.md`
  - `docs/NVIDIA_RUNTIME_TEST_BRANCH_STATUS.md`
  - `docs/NVIDIA_RUNTIME_TEST_BUILD_AUDIT.md`
  - `docs/BRANCH_INVENTORY_FOR_NVIDIA_TEST.md`

This document does not modify source. Run all commands from a shell
on the NVIDIA host, not from the authoring sandbox.

---

## 1. Required Host

| Requirement                | Minimum                                          | Notes                                                                |
|----------------------------|--------------------------------------------------|----------------------------------------------------------------------|
| NVIDIA GPU                 | Any CUDA-capable card (compute capability 5.0 +) | `nvidia-smi` must list the device.                                   |
| NVIDIA driver              | Compatible with the chosen CUDA Toolkit          | Newer driver than toolkit is fine; older driver is not.              |
| CUDA Toolkit               | 11.x or 12.x with `nvcc` on `PATH`               | CMake's `find_package(CUDAToolkit REQUIRED)` must succeed.           |
| OptiX SDK                  | 7.x or 8.x                                       | Install root will be passed to CMake as `OPTIX_ROOT` (see §3.3).     |
| CMake                      | ≥ 3.20 (3.28+ recommended)                       | The repository pins `cmake_minimum_required(VERSION 3.20)`.          |
| C++ compiler               | C++20-capable host compiler                      | g++ ≥ 11, clang ≥ 14, or MSVC 19.30+. CUDA host compiler must match. |
| Python                     | 3.10+                                            | Required by `tools/verify_cuda_host.py` (PEP-604 typing).            |
| Git                        | Any modern version                               | For clone / fetch / checkout.                                        |
| Disk                       | ≥ 5 GB free                                      | Build trees + test outputs.                                          |

Pre-flight sanity checks:

```bash
nvidia-smi
nvcc --version
cmake --version
g++ --version           # or clang++ --version / cl.exe
python3 --version
ls "$OPTIX_INSTALL_DIR" # or wherever the SDK lives
```

If any of `nvidia-smi`, `nvcc`, or the OptiX SDK directory is missing,
stop here — the corresponding part of the matrix below cannot run, and
the report (§5) should mark those configurations BLOCKED, not REPAIR.

---

## 2. Branch to Test

`nvidia-sdk-runtime-test` carries three docs-only commits on top of the
canonical base:

```
33c17a2  docs: NVIDIA runtime test build audit (Config #1 PASS; #2/#3 BLOCKED/DEFERRED)
187aaa6  docs: NVIDIA runtime test branch status snapshot (post-integration)
9b87e25  docs: AREA arc planning document
107300e  docs: OBS-DOP.6 — Observer Doppler/Searchlight Capstone Audit (docs only)  ← base
```

The branch is local-only on the authoring sandbox at the time of this
write; it has no upstream and has not been pushed. Two ways to obtain
it on the NVIDIA host:

### 2.A If the branch has been pushed to origin

```bash
git clone https://github.com/fopearcano/relativityrender.git
cd relativityrender
git fetch origin nvidia-sdk-runtime-test
git checkout nvidia-sdk-runtime-test
git log --oneline -5
```

Confirm the top commit matches `33c17a2 …` (or the latest tip the
operator was told to use).

### 2.B Reconstruct from public refs (no push required)

Use this path when `nvidia-sdk-runtime-test` has not been published.
The source tree is exactly the base branch plus one cherry-picked docs
commit; the additional status / audit docs on top are not required for
the runtime test.

```bash
git clone https://github.com/fopearcano/relativityrender.git
cd relativityrender
git fetch origin claude/rewrite-rendering-core-De71I area-light-arc
git checkout -b nvidia-sdk-runtime-test origin/claude/rewrite-rendering-core-De71I
git cherry-pick 447b909          # docs: AREA arc planning document
git log --oneline -3
```

Confirm:

- HEAD subject is `docs: AREA arc planning document`.
- Second commit is `107300e docs: OBS-DOP.6 — …`.
- `git diff --stat 107300e..HEAD` shows exactly one file
  (`docs/PATH_TRACER_AREA_LIGHT_PLAN.md`, +1763).

Either 2.A or 2.B leaves the source tree byte-identical to what the
test was prepared against. Do not push the reconstructed branch from
the NVIDIA host — it is throwaway.

---

## 3. Exact Commands

Run all commands from the repository root. Use clean `build/<n>`
directories so a stale CMake cache cannot mask a real failure.

### 3.1 Pre-flight gate

```bash
git status                  # must be clean
git rev-parse HEAD          # record for the report
git log --oneline 107300e..HEAD
```

### 3.2 Configure — CUDA ON, OptiX OFF (Configuration #2)

```bash
rm -rf build/2
cmake -S . -B build/2 \
    -DRR_ENABLE_CUDA=ON \
    -DRR_ENABLE_OPTIX=OFF
```

Expected configure banner contains:

```
CUDA backend : ON
```

### 3.3 Configure — CUDA ON, OptiX ON (Configuration #4)

```bash
rm -rf build/4
cmake -S . -B build/4 \
    -DRR_ENABLE_CUDA=ON \
    -DRR_ENABLE_OPTIX=ON \
    -DOPTIX_ROOT="$OPTIX_INSTALL_DIR"
```

If the operator's OptiX SDK lives elsewhere (e.g. `/opt/optix`,
`C:\ProgramData\NVIDIA Corporation\OptiX SDK 8.0.0`), substitute that
path for `$OPTIX_INSTALL_DIR`.

Expected configure banner contains both `CUDA backend : ON` and an
OptiX status message acknowledging `-DRR_ENABLE_OPTIX=ON`.

### 3.4 Build

```bash
cmake --build build/2 -j
cmake --build build/4 -j
```

Each build must reach `100 %`. Capture the full build log per
configuration; do not commit the logs.

### 3.5 Run ctest

```bash
ctest --test-dir build/2 --output-on-failure
ctest --test-dir build/4 --output-on-failure
```

Expected on build/2: all host tests pass; `gpu_tests` exercises the
CUDA path now that `RR_HAS_CUDA` is defined; `optix_tests` is **not**
built.

Expected on build/4: full matrix — `math_tests`, `image_tests`,
`gpu_tests`, `pathtracer_tests`, `pathtracer_nee_tests`,
`pathtracer_bsdf_tests`, `pathtracer_mis_tests`, `cli_tests`,
`relativity_tests`, `manifold_identity_tests`, `field_tests`,
`demo_tests`, `renderer_tests`, **plus** `optix_tests`. All must pass.

### 3.6 Run `tools/verify_cuda_host.py --optix`

`verify_cuda_host.py` is a Python harness that (re-)builds the
renderer with the requested toolkit, discovers the binary, runs a
device-info smoke and any catalogued render commands, and writes a
structured report.

Standard invocation against the OptiX-enabled build:

```bash
python3 tools/verify_cuda_host.py \
    --optix \
    --optix-root "$OPTIX_INSTALL_DIR" \
    --build-dir build/4 \
    --skip-build \
    --timeout 120 \
    --report-out docs/NVIDIA_RUNTIME_TEST_EXECUTION_REPORT.md \
    --show-stdout
```

Flag notes (from `tools/verify_cuda_host.py` argparse):

- `--optix` enables the OptiX command set (skipped when off).
- `--optix-root` is passed as `-DOPTIX_ROOT=<path>` if the runner
  rebuilds.
- `--skip-build` is set here because §3.3 / §3.4 already produced
  `build/4`. Drop it to let the harness reconfigure + rebuild itself.
- `--build-dir` points the auto-discovery at `build/4/bin/RelativityRender`.
- `--timeout` (default 60 s) raises the per-command timeout to 120 s
  to accommodate slower render commands.
- `--report-out` overrides the harness default
  (`docs/CUDA_HOST_VERIFICATION_REPORT.md`) so the result lands in the
  file §5 expects.
- `--show-stdout` keeps full subprocess output visible during the run.

Exit code 0 means every catalogued command passed. Non-zero means at
least one command failed or timed out; consult the report and the
per-command captured output to classify.

For the CUDA-only build (no OptiX), drop `--optix` and
`--optix-root`:

```bash
python3 tools/verify_cuda_host.py \
    --build-dir build/2 \
    --skip-build \
    --timeout 120 \
    --report-out docs/NVIDIA_RUNTIME_TEST_EXECUTION_REPORT.md.cuda-only \
    --show-stdout
```

---

## 4. Expected Outputs

### 4.1 CUDA render outputs

From `./build/2/bin/RelativityRender` and `./build/4/bin/RelativityRender`.
Use the `--scene <path>` form (or the project's standard scene flag
visible via `RelativityRender --help`) and direct output to an
`output/` directory that is **not** committed.

For each of the scenes in §4.4–§4.7 the operator expects:

- A PPM (or configured-format) file written for the beauty pass.
- Exit code 0.
- No CUDA driver faults (`cudaErrorXxx`) in stderr.
- No NaN / INF banner lines.

### 4.2 OptiX render outputs

From `./build/4/bin/RelativityRender` with the OptiX backend
explicitly selected (CLI flag visible via `--help`). Expected:

- Same beauty file as the CUDA backend for identity / default-OFF
  arms (cross-backend byte-identity is the standing invariant).
- AOV files for every AOV enabled at the CLI.

### 4.3 AOV outputs

The renderer ships observer, field, manifold, and standard
beauty/normal/depth AOVs. For each enabled AOV, expect a separate
output file (per the AOV naming convention in `src/renderer/AOV.cpp`).
Verify:

- File present.
- Non-zero size.
- No NaN / INF.
- Cross-backend identity on identity arms.

### 4.4 Manifold fixtures

| Scene                                                | Exercises                                     |
|------------------------------------------------------|-----------------------------------------------|
| `scenes/test_penrose_like_manifold.rrscene`          | Penrose-like compactification warp.           |
| `scenes/test_schwarzschild_like_manifold.rrscene`    | Schwarzschild-like warp.                      |

Identity-mode default arms must match the host baseline
(`manifold_identity_tests` already pins host-side correctness).

### 4.5 Observer fixtures

| Scene                                                | Exercises                                     |
|------------------------------------------------------|-----------------------------------------------|
| `scenes/test_observer_frame.rrscene`                 | Observer frame fixture.                       |
| `scenes/test_observer_primary_ray_perception.rrscene`| Observer perception transform (primary rays). |

Doppler / searchlight AOVs (per the OBS-DOP arc on the base branch)
should populate without NaNs.

### 4.6 Scalar field fixtures

| Scene                                                | Exercises                                       |
|------------------------------------------------------|-------------------------------------------------|
| `scenes/test_scalar_field_diagnostic.rrscene`        | Field interpreter diagnostic AOV.               |
| `scenes/test_scalar_field_color_multiplier.rrscene`  | Scalar-field beauty mapping (color path).       |
| `scenes/test_scalar_field_emission.rrscene`          | Scalar-field beauty mapping (emission path).    |

### 4.7 Area-light tests (if available)

The `area-light-arc` work that landed on `nvidia-sdk-runtime-test` is
**plan-only** (`docs/PATH_TRACER_AREA_LIGHT_PLAN.md`). There is no
area-light source on this branch yet, so:

- There is no AREA scene fixture to render.
- There is no AREA test target in `ctest`.
- Mark this row **BLOCKED — not implemented yet on this branch** in
  the execution report. Do NOT attempt to construct an ad-hoc area
  light scene; the AREA arc is not in scope here.

### 4.8 Catch-all smoke scenes

Always also run, on both `build/2` and `build/4` where applicable:

- `scenes/test_full_scene.rrscene`
- `scenes/test_relativity.rrscene`
- `scenes/test_camera.rrscene`
- `scenes/test_spheres.rrscene`
- `scenes/test_mesh.rrscene`
- `scenes/test_lights.rrscene`
- `scenes/test_materials.rrscene`
- `scenes/test_textured_material.rrscene`
- `scenes/test_render_settings.rrscene`

---

## 5. How to Report

The execution report lands at
`docs/NVIDIA_RUNTIME_TEST_EXECUTION_REPORT.md`.

Two ways to produce it:

1. **Automatic.** Let `tools/verify_cuda_host.py` (§3.6) write it via
   `--report-out docs/NVIDIA_RUNTIME_TEST_EXECUTION_REPORT.md`. The
   harness emits a structured per-command table.
2. **Manual.** If the harness cannot run (Python missing, etc.), copy
   the template below into the file and fill it in by hand.

### 5.1 Report template

```markdown
# NVIDIA Runtime Test — Execution Report

- Date: <YYYY-MM-DD>
- Operator: <handle>
- Host: <OS, distro, kernel, GPU model from nvidia-smi>
- Driver: <from nvidia-smi>
- CUDA Toolkit: <nvcc --version>
- OptiX SDK: <version + install path>
- CMake: <cmake --version>
- Compiler: <g++ / clang++ / cl version>
- Repository tip: <git rev-parse HEAD>
- Branch: nvidia-sdk-runtime-test
- Base: claude/rewrite-rendering-core-De71I @ 107300e

## Pre-flight

| Check               | Result   |
|---------------------|----------|
| nvidia-smi          | <output> |
| nvcc --version      | <output> |
| OptiX SDK path      | <output> |
| Repository clean    | yes/no   |

## Build matrix

| Configuration              | Configure | Build (100 %) | ctest pass | Verdict       |
|----------------------------|-----------|---------------|------------|----------------|
| #1 CUDA OFF / OptiX OFF    | <yes/no>  | <yes/no>      | N/M        | PASS/REPAIR/BLOCKED |
| #2 CUDA ON  / OptiX OFF    | <yes/no>  | <yes/no>      | N/M        | PASS/REPAIR/BLOCKED |
| #3 CUDA OFF / OptiX ON     | <yes/no>  | <yes/no>      | N/M        | PASS/REPAIR/BLOCKED |
| #4 CUDA ON  / OptiX ON     | <yes/no>  | <yes/no>      | N/M        | PASS/REPAIR/BLOCKED |

## verify_cuda_host.py --optix

| Command                  | Backend | Result | Notes |
|--------------------------|---------|--------|-------|
| device-info              | CUDA    | <pass/fail> | <stderr summary> |
| device-info              | OptiX   | <pass/fail> | <stderr summary> |
| …catalogued commands…    | …       | …      | …     |

Exit code: <0/1>
Report path: docs/NVIDIA_RUNTIME_TEST_EXECUTION_REPORT.md (overridden via --report-out)

## Scene renders

| Scene                                                | Backend | Output written | NaN/INF | Verdict |
|------------------------------------------------------|---------|----------------|---------|---------|
| scenes/test_full_scene.rrscene                       | CUDA    | <path>         | no      | PASS    |
| scenes/test_full_scene.rrscene                       | OptiX   | <path>         | no      | PASS    |
| scenes/test_penrose_like_manifold.rrscene            | CUDA    | …              | …       | …       |
| scenes/test_penrose_like_manifold.rrscene            | OptiX   | …              | …       | …       |
| scenes/test_schwarzschild_like_manifold.rrscene      | CUDA    | …              | …       | …       |
| scenes/test_schwarzschild_like_manifold.rrscene      | OptiX   | …              | …       | …       |
| scenes/test_observer_frame.rrscene                   | CUDA    | …              | …       | …       |
| scenes/test_observer_frame.rrscene                   | OptiX   | …              | …       | …       |
| scenes/test_observer_primary_ray_perception.rrscene  | CUDA    | …              | …       | …       |
| scenes/test_observer_primary_ray_perception.rrscene  | OptiX   | …              | …       | …       |
| scenes/test_scalar_field_diagnostic.rrscene          | CUDA    | …              | …       | …       |
| scenes/test_scalar_field_diagnostic.rrscene          | OptiX   | …              | …       | …       |
| scenes/test_scalar_field_color_multiplier.rrscene    | CUDA    | …              | …       | …       |
| scenes/test_scalar_field_color_multiplier.rrscene    | OptiX   | …              | …       | …       |
| scenes/test_scalar_field_emission.rrscene            | CUDA    | …              | …       | …       |
| scenes/test_scalar_field_emission.rrscene            | OptiX   | …              | …       | …       |
| AREA fixture                                         | —       | n/a            | n/a     | BLOCKED — AREA arc is plan-only on this branch |

## Cross-backend identity

For identity / default-OFF arms, compare CUDA vs OptiX outputs
byte-for-byte (or per the project's pixel-diff tooling) and note any
divergence. Default expectation: byte-identical.

## Overall verdict

PASS — every matrix point configured / built / tested / rendered without
findings.
REPAIR — at least one runtime defect surfaced that must be fixed before
the branch is merged back. Open issues / commits should target
claude/rewrite-rendering-core-De71I via a fresh feature branch, NOT this
throwaway.
BLOCKED — at least one matrix point could not run for environment
reasons (missing SDK, driver mismatch, etc.). Document exactly what is
missing.

Use a single overall verdict at the top of the file (PASS / REPAIR /
BLOCKED), and have the matrix tables back it up.

## Disposition

- If PASS: report it, then run rollback per the integration plan §7.
- If REPAIR: open a fresh feature branch off
  claude/rewrite-rendering-core-De71I, cherry-pick any fix-bearing
  commits, push that — do not push nvidia-sdk-runtime-test itself.
- If BLOCKED: document the gap (missing toolkit version, OS issue,
  etc.) and stop. No source change is appropriate.
```

### 5.2 What counts as PASS / REPAIR / BLOCKED

- **PASS** — every attempted configuration configured, built to 100 %,
  passed `ctest`, and every requested scene rendered cleanly with the
  expected cross-backend identity on identity arms.
- **REPAIR** — at least one configure, build, or test failed on the
  branch in a way that points to a defect on
  `claude/rewrite-rendering-core-De71I`. Capture the exact log lines.
  Do NOT push fixes from this branch; cherry-pick onto a new feature
  branch off the base.
- **BLOCKED** — environment / SDK problem only (driver too old,
  toolkit missing, OptiX root not set, etc.). The branch itself is
  not defective. Per the standing rule, missing CUDA / OptiX is
  always BLOCKED or DEFERRED, never REPAIR.

---

## 6. Standing Rules for the Operator

1. **No push of `nvidia-sdk-runtime-test`.** It is throwaway.
2. **No source modifications during the runtime test.** Findings go
   into the report; fixes land later via a new branch off
   `claude/rewrite-rendering-core-De71I`.
3. **No merges or destructive resets.** If `git status` ever shows a
   dirty tree, run `git reset --merge` (only) and re-check.
4. **No feature implementation.** This is verification, not
   development.
5. **Treat build/4 (CUDA + OptiX) as the production target.** The
   other configurations are diagnostic.
6. **Do not edit `tools/verify_cuda_host.py`.** Use its CLI surface
   only; pass `--bin` / `--build-dir` / `--optix-root` / `--timeout`
   etc. as needed.
7. **Capture, do not interpret.** Raw outputs in the report; analysis
   in a separate follow-up.

---

## 7. Out of Scope

- Pinning specific CUDA / OptiX / driver minor versions (left to the
  operator's host).
- Performance benchmarking.
- Pushing fix commits from the NVIDIA host.
- Anything that touches `origin/main`,
  `origin/claude/rewrite-rendering-core-De71I`, or
  `origin/area-light-arc`.
- AREA arc implementation work (this branch carries only the plan
  doc).

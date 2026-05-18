# NVIDIA Runtime Test — Build Audit

Host-side build verification of the temporary integration branch
`nvidia-sdk-runtime-test` prior to any CUDA / OptiX runtime test on
real NVIDIA hardware. Captured per
`docs/NVIDIA_RUNTIME_TEST_INTEGRATION_PLAN.md` §3 Step 1 and §6.

- Date: 2026-05-18
- Working directory: `/home/user/RelativityRender`
- Branch: `nvidia-sdk-runtime-test`
- HEAD: `187aaa6` — `docs: NVIDIA runtime test branch status snapshot
  (post-integration)`
- Base: `origin/claude/rewrite-rendering-core-De71I` @ `107300e`
- Integrated docs commits beyond base: `9b87e25` (AREA arc plan),
  `187aaa6` (status snapshot). No source / build-system changes.

## Host Environment Snapshot

| Component               | Value                          |
|-------------------------|--------------------------------|
| OS                      | Linux 6.18.5 x86_64            |
| Host CPU cores          | 4                              |
| CMake                   | 3.28.3                         |
| C++ compiler            | g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) |
| `nvcc` on PATH          | **absent**                     |
| `nvidia-smi` on PATH    | **absent**                     |
| `CUDA_PATH`             | unset                          |
| `CUDA_HOME`             | unset                          |
| `OptiX_INSTALL_DIR`     | unset                          |
| `/usr/local/cuda*`      | not present                    |
| `/opt/nvidia*`          | not present                    |
| `/opt/optix*`           | not present                    |

This is the managed cloud sandbox the integration work is being
authored in, not a CUDA-capable NVIDIA host.

## CUDA Availability

**Not available.** No CUDA Toolkit on this machine. `cmake
-DRR_ENABLE_CUDA=ON` reaches `find_package(CUDAToolkit REQUIRED)` at
`CMakeLists.txt:90` and fails immediately with:

```
CMake Error at /usr/share/cmake-3.28/Modules/FindCUDAToolkit.cmake:855 (message):
  Could not find nvcc, please set CUDAToolkit_ROOT.
Call Stack (most recent call first):
  CMakeLists.txt:90 (find_package)
```

Per the standing rule ("If CUDA/OptiX SDK is missing, mark runtime as
BLOCKED/DEFERRED, not REPAIR") this is **not** a code defect. It is an
environment gap that must be closed by running the same matrix on an
NVIDIA host.

## OptiX SDK Availability

**Not available.** `OptiX_INSTALL_DIR` is unset and no `/opt/optix*`
directory exists. Configuration #4 (CUDA ON / OptiX ON) is not
attempted because the CUDA gate (#2) already blocks. Configuration #3
(CUDA OFF / OptiX ON) was not in this task's matrix and was not run.

## Build Configurations Attempted

### Configuration #1 — `RR_ENABLE_CUDA=OFF`, `RR_ENABLE_OPTIX=OFF`

- Command: `cmake -S . -B build/1 -DRR_ENABLE_CUDA=OFF
  -DRR_ENABLE_OPTIX=OFF`
- Configure: **success** (`Configuring done (1.3s)` / `Generating
  done`).
- Banner output:

  ```
  -- RelativityRender 0.1.0 (Stage 20N: OptiX AOVs)
  --   Build type   : RelWithDebInfo
  --   C++ standard : 20
  --   Build tests  : ON
  --   CUDA backend : OFF
  ```

- Build: `cmake --build build/1 -j4` reached **100 %**. All static
  libraries and the `RelativityRender` executable plus 13 test
  binaries linked cleanly.
- Built test binaries in `build/1/bin/`:
  `RelativityRender`, `cli_tests`, `demo_tests`, `field_tests`,
  `gpu_tests`, `image_tests`, `manifold_identity_tests`,
  `math_tests`, `pathtracer_bsdf_tests`, `pathtracer_mis_tests`,
  `pathtracer_nee_tests`, `pathtracer_tests`, `relativity_tests`,
  `renderer_tests`. (`optix_tests` correctly absent — gated behind
  the OptiX backend.)

### Configuration #2 — `RR_ENABLE_CUDA=ON`, `RR_ENABLE_OPTIX=OFF`

- Command: `cmake -S . -B build/2 -DRR_ENABLE_CUDA=ON
  -DRR_ENABLE_OPTIX=OFF`
- Configure: **BLOCKED** at `find_package(CUDAToolkit REQUIRED)` —
  see the CUDA Availability section above. No build attempted.
- This is environment-only; no source / CMake change is required.

### Configuration #3 — `RR_ENABLE_CUDA=ON`, `RR_ENABLE_OPTIX=ON`

- **DEFERRED.** The CUDA gate in Configuration #2 already excludes
  this matrix point. Re-attempt once a host with both CUDA Toolkit
  and OptiX SDK is available.

## ctest Result

`ctest --test-dir build/1 --output-on-failure` ran the full host-side
suite for Configuration #1:

```
      Start  1: math_tests
 1/13 Test  #1: math_tests .......................   Passed    0.00 sec
      Start  2: image_tests
 2/13 Test  #2: image_tests ......................   Passed    0.00 sec
      Start  3: gpu_tests
 3/13 Test  #3: gpu_tests ........................   Passed    0.00 sec
      Start  4: pathtracer_tests
 4/13 Test  #4: pathtracer_tests .................   Passed    0.01 sec
      Start  5: pathtracer_nee_tests
 5/13 Test  #5: pathtracer_nee_tests .............   Passed    0.00 sec
      Start  6: pathtracer_bsdf_tests
 6/13 Test  #6: pathtracer_bsdf_tests ............   Passed    0.01 sec
      Start  7: pathtracer_mis_tests
 7/13 Test  #7: pathtracer_mis_tests .............   Passed    0.00 sec
      Start  8: cli_tests
 8/13 Test  #8: cli_tests ........................   Passed    0.00 sec
      Start  9: relativity_tests
 9/13 Test  #9: relativity_tests .................   Passed    0.00 sec
      Start 10: manifold_identity_tests
10/13 Test #10: manifold_identity_tests ..........   Passed    0.00 sec
      Start 11: field_tests
11/13 Test #11: field_tests ......................   Passed    0.00 sec
      Start 12: demo_tests
12/13 Test #12: demo_tests .......................   Passed    0.00 sec
      Start 13: renderer_tests
13/13 Test #13: renderer_tests ...................   Passed    0.00 sec

100% tests passed, 0 tests failed out of 13
Total Test time (real) =   0.04 sec
```

`gpu_tests` ran in the no-CUDA branch (host-side `GpuBuffer<T>`
semantics) because `RR_HAS_CUDA` is undefined in this configuration —
exactly the expected behaviour per `CMakeLists.txt`.

## Per-Configuration Verdict

| Configuration                          | Verdict     | Reason                                                                                       |
|----------------------------------------|-------------|-----------------------------------------------------------------------------------------------|
| #1 CUDA OFF / OptiX OFF                | **PASS**    | Configure + 100 % build + 13/13 ctest pass.                                                  |
| #2 CUDA ON / OptiX OFF                 | **BLOCKED** | No CUDA Toolkit on this host. Run on the NVIDIA target; not a code defect on this branch.    |
| #3 CUDA ON / OptiX ON                  | **DEFERRED**| Excluded by the #2 gate. Re-attempt once both CUDA Toolkit and OptiX SDK are installed.       |

## Overall Verdict

**PASS for the host-only configuration. BLOCKED / DEFERRED for the
GPU configurations.**

Nothing on `nvidia-sdk-runtime-test` requires repair. The build is
sound for the configurations this host can exercise; the remaining
matrix entries require an NVIDIA target with the CUDA Toolkit (and
OptiX SDK for #3). Re-run the integration plan §6 matrix on that
target to complete the runtime verification.

## Invariants Preserved

- `origin/claude/rewrite-rendering-core-De71I` not touched.
- `origin/main` not touched.
- `origin/area-light-arc` not touched.
- No renderer source files modified.
- No build-system files modified (CMakeLists.txt unchanged).
- No new features added.
- No push performed.

## Next Steps

1. On an NVIDIA host with CUDA Toolkit installed, re-run
   `docs/NVIDIA_RUNTIME_TEST_INTEGRATION_PLAN.md` §6 Configuration
   #2 to clear the CUDA gate.
2. With OptiX SDK additionally available, run Configurations #3 and
   #4 from the same matrix.
3. Run the scene-render smokes listed in
   `docs/NVIDIA_RUNTIME_TEST_BRANCH_STATUS.md` §6 against the
   CUDA + OptiX builds.
4. Capture results in a follow-up audit doc; surface any findings
   back onto `claude/rewrite-rendering-core-De71I` via a fresh
   feature branch (not this throwaway).

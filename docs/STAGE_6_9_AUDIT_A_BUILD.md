# Stage 6–9 Audit A — Build

Date: 2026-04-29
Branch: `relativity-core-v1`
Scope: build status + file-tree existence only. Architecture / kernel
correctness / runtime semantics are **out of scope** for Audit A and
will be covered by later audit slices (Audit B, etc.).

This doc is documentation. No source files are modified.

---

## 1. Current file tree

Per-module listing for the six modules the prompt named.
File sizes are in bytes; line counts via `wc -l`.

### `src/scene/`

| File              | Lines | Bytes |
|-------------------|------:|------:|
| `RenderSettings.h`|    22 |   776 |
| `Scene.cpp`       |    17 |   353 |
| `Scene.h`         |   113 |  4152 |
| `SceneObject.h`   |    23 |   755 |
| `Transform.h`     |    22 |   749 |

5 files, 197 lines.

### `src/geometry/`

| File          | Lines | Bytes |
|---------------|------:|------:|
| `Mesh.cpp`    |    44 |  1003 |
| `Mesh.h`      |    75 |  2840 |
| `Sphere.h`    |    31 |  1128 |
| `Triangle.h`  |    32 |  1025 |

4 files, 182 lines.

### `src/gpu/`

| File           | Lines | Bytes |
|----------------|------:|------:|
| `GpuBuffer.cpp`|    44 |   923 |
| `GpuBuffer.h`  |   113 |  3968 |
| `GpuDevice.cpp`|    45 |   825 |
| `GpuDevice.h`  |    37 |  1348 |
| `GpuMesh.cpp`  |    56 |  1483 |
| `GpuMesh.h`    |    84 |  3570 |
| `GpuScene.cpp` |   115 |  2978 |
| `GpuScene.h`   |   154 |  6737 |

8 files, 648 lines.

### `src/cuda/`

| File                    | Lines | Bytes |
|-------------------------|------:|------:|
| `CudaBuffer.cpp`        |    54 |  1332 |
| `CudaBuffer.h`          |    23 |   917 |
| `CudaContext.cpp`       |    42 |  1144 |
| `CudaContext.h`         |    21 |   700 |
| `CudaIntersection.cuh`  |   148 |  5809 |
| `CudaKernels.cuh`       |    87 |  4497 |
| `CudaLight.cuh`         |    14 |   557 |
| `CudaMaterial.cuh`      |    16 |   645 |
| `CudaMesh.cuh`          |    36 |  1405 |
| `CudaRenderer.cu`       |   174 |  6548 |
| `CudaRenderer.h`        |    95 |  4347 |
| `CudaScene.cuh`         |    58 |  2372 |
| `CudaTestKernel.cu`     |   480 | 19634 |

13 files, 1,248 lines.

### `src/material/`

| File              | Lines | Bytes |
|-------------------|------:|------:|
| `Material.cpp`    |    49 |  1253 |
| `Material.h`      |    49 |  1879 |
| `MaterialTypes.h` |    45 |  1929 |

3 files, 143 lines.

### `src/lighting/`

| File       | Lines | Bytes |
|------------|------:|------:|
| `Light.cpp`|    70 |  2027 |
| `Light.h`  |    92 |  3737 |

2 files, 162 lines.

### Module totals

| Module      | Files | Lines |
|-------------|------:|------:|
| `scene/`    |    5  |   197 |
| `geometry/` |    4  |   182 |
| `gpu/`      |    8  |   648 |
| `cuda/`     |   13  | 1,248 |
| `material/` |    3  |   143 |
| `lighting/` |    2  |   162 |
| **total**   | **35**| **2,580** |

---

## 2. Build commands used

The two supported configurations (per `docs/BUILD_PLAN.md`):

```sh
# Host-only (verified in this audit; no CUDA Toolkit needed)
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure

# CUDA-enabled (NOT runnable in this audit's environment)
cmake -S . -B build-cuda -DRR_ENABLE_CUDA=ON
cmake --build build-cuda -j
```

In this audit the host-only build was exercised:

```
$ cmake --build build -j
[100%] Built target rr_scene
[100%] Built target image_tests
[100%] Built target rr_gpu
[100%] Built target gpu_tests
[100%] Built target RelativityRender
```

## 3. Does clean build pass?

**Yes**, host-only. Run from the current tree:

- `cmake --build build -j` finishes with `[100%] Built target …` for
  every target. No warnings under `-Wall -Wextra -Wpedantic`. No
  errors.
- All eight CMake targets link cleanly:

```
build/librr_camera.a
build/librr_geometry.a
build/librr_gpu.a
build/librr_image.a
build/librr_lighting.a
build/librr_material.a
build/librr_scene.a
build/bin/RelativityRender
build/bin/math_tests
build/bin/image_tests
build/bin/gpu_tests
```

(The two header-only INTERFACE libraries `rr_math` and `rr_relativity`
do not produce `.a` files — that is expected.)

- `ctest --test-dir build --output-on-failure` reports `3/3 passed`
  (`math_tests`, `image_tests`, `gpu_tests`).

The CUDA-enabled build was **not runnable** in this audit's
environment (no CUDA Toolkit, no GPU). The CMakeLists fails fast
on `find_package(CUDAToolkit)` when nvcc is absent, which is the
correct behaviour and is verified in Audit-step 1's prior run
(`docs/STAGE_1_5_AUDIT.md` §1).

## 4. Does the executable run?

**Yes**, the host-only binary runs and dispatches every CLI mode.
Result of running each CLI invocation in the host-only build:

| Invocation | Exit | Notes |
|---|---|---|
| (no args) | 0 | startup banner |
| `--help` | 0 | usage |
| `--version` | 0 | `RelativityRender 0.1.0` |
| `--device-info` | 0 | reports `GPU backend: (none)` + actionable rebuild hint |
| `--render-gradient` | 1 | actionable "requires CUDA" error |
| `--render-rays` | 1 | actionable "requires CUDA" error |
| `--render-sphere` | 1 | actionable "requires CUDA" error |
| `--render-relativistic` | 1 | actionable "requires CUDA" error |
| `--render-scene` | 1 | actionable "requires CUDA" error |
| `--render-triangle` | 1 | actionable "requires CUDA" error |
| `--render-mesh-scene` | 1 | actionable "requires CUDA" error |
| `--render-material-scene` | 1 | actionable "requires CUDA" error |
| `--render-direct-lighting` | 1 | actionable "requires CUDA" error |

Every render action exits cleanly (with the documented "requires
CUDA" error and exit code 1) when the host-only build is used.
Nothing crashes; nothing produces garbage output.

## 5. Which output files are generated?

In this audit's environment (host-only, no GPU) **only one file is
produced**, by the test binary, not the renderer:

| Path                                  | Size      | Producer                          | Stage |
|---------------------------------------|----------:|-----------------------------------|------:|
| `build/output/image_test.ppm`         | 12,301 B  | `tests/image_tests` (CPU IO test) |     3 |

The eleven GPU-output paths declared by the renderer
(`output/gpu_gradient.ppm`, `gpu_camera_rays.ppm`, `gpu_sphere.ppm`,
the four `sphere_beta_{000,025,075,095}.ppm`, `gpu_scene_spheres.ppm`,
`gpu_triangle.ppm`, `gpu_mesh_scene.ppm`, `gpu_material_scene.ppm`,
`gpu_direct_lighting.ppm`) are **not produced** because they all
require CUDA. They are **not** missing files; they are CUDA-gated
outputs documented in `docs/BUILD_PLAN.md` §"Run modes" and will be
produced on a CUDA host. Audit B is the appropriate place to confirm
their content.

## 6. Which Stage 6–9 files exist?

Mapping the user's stage numbering onto the file-tree state:

### Stage 6A — Host scene structures

| Expected file               | Present? | Notes |
|-----------------------------|:--------:|-------|
| `src/math/Transform.h`      | ✓        | recovered alongside Stage 6A; needed by `scene/Transform.h` |
| `src/scene/Transform.h`     | ✓        | alias header |
| `src/scene/RenderSettings.h`| ✓        | |
| `src/scene/SceneObject.h`   | ✓        | |
| `src/scene/Scene.h`         | ✓        | |
| `src/scene/Scene.cpp`       | ✓        | |

### Stage 6B — GPU scene upload

| Expected file               | Present? |
|-----------------------------|:--------:|
| `src/cuda/CudaScene.cuh`    | ✓        |
| `src/gpu/GpuScene.h`        | ✓        |
| `src/gpu/GpuScene.cpp`      | ✓        |

Plus kernel additions (`k_render_scene` + `launch_render_scene`) in
`src/cuda/CudaTestKernel.cu` and a `--render-scene` CLI action.

### Stage 7A — Mesh structures

| Expected file              | Present? |
|----------------------------|:--------:|
| `src/geometry/Triangle.h`  | ✓        |
| `src/geometry/Mesh.h`      | ✓        |
| `src/geometry/Mesh.cpp`    | ✓        |

### Stage 7B — GPU mesh upload

| Expected file           | Present? |
|-------------------------|:--------:|
| `src/cuda/CudaMesh.cuh` | ✓        |
| `src/gpu/GpuMesh.h`     | ✓        |
| `src/gpu/GpuMesh.cpp`   | ✓        |

### Stage 7C — CUDA triangle intersection

| Expected change                                            | Present? |
|------------------------------------------------------------|:--------:|
| `intersect_triangle` in `src/cuda/CudaIntersection.cuh`    | ✓        |
| Triangle closest-hit loop in `k_render_scene`              | ✓        |
| Mesh slot in `CudaSceneView`                               | ✓        |
| `--render-triangle` + `--render-mesh-scene` CLI actions    | ✓        |

### Stage 8A — Material data model

| Expected file                | Present? |
|------------------------------|:--------:|
| `src/material/MaterialTypes.h` | ✓      |
| `src/material/Material.h`    | ✓        |
| `src/material/Material.cpp`  | ✓        |
| `src/cuda/CudaMaterial.cuh`  | ✓        |

### Stage 8B — GPU material shading

| Expected change                                | Present? |
|------------------------------------------------|:--------:|
| `GpuScene::upload_materials` + accessors       | ✓        |
| `materials` slot in `CudaSceneView`            | ✓        |
| Material lookup in `k_render_scene` step 4     | ✓        |
| `--render-material-scene` CLI action           | ✓        |

### Stage 9A — Light data model

| Expected file              | Present? |
|----------------------------|:--------:|
| `src/lighting/Light.h`     | ✓        |
| `src/lighting/Light.cpp`   | ✓        |
| `src/cuda/CudaLight.cuh`   | ✓        |

### Stage 9B — Direct lighting on GPU

| Expected change                                 | Present? |
|-------------------------------------------------|:--------:|
| `GpuScene::upload_lights` + accessors           | ✓        |
| `lights` slot in `CudaSceneView`                | ✓        |
| Direct-lighting evaluation in `k_render_scene`  | ✓        |
| `--render-direct-lighting` CLI action           | ✓        |

**Every Stage 6A–9B source file the BUILD_PLAN promises is present
on disk.**

## 7. Which expected files are missing?

**No source files** that the BUILD_PLAN entries for Stages 6–9 promise
are missing. Three observations belong here for completeness, but
none of them is a Stage 6–9 deliverable:

1. **`tests/scene_tests.cpp`, `tests/mesh_tests.cpp`,
   `tests/material_tests.cpp`, `tests/lighting_tests.cpp`** are
   absent. None of the Stage 6–9 prompts asked for these; they will
   land alongside their respective master-order modules' "B" slices
   when host coverage of those data models becomes a requirement.
   **Out of Stage 6–9 scope; not "missing" in the audit sense.**

2. **GPU-side runtime outputs**
   (`output/gpu_scene_spheres.ppm` ... `output/gpu_direct_lighting.ppm`)
   are absent because no CUDA host has been used. Same situation as
   the equivalent paths from Stages 6 / 7 / 8 / 10 of `STAGE_1_5_AUDIT.md`
   item H1. **Documented; not "missing files" — runtime artifacts.**

3. **Scene parser** (`src/io/SceneLoader.{h,cpp}`,
   `src/io/SceneWriter.{h,cpp}`, vendored JSON library, `.rrscene`
   fixtures) are deliberately absent. Master module 15 is the next
   stage; that is **the explicit subject** of the upcoming work and
   the reason for this checkpoint audit. **Expected absence.**

---

## Summary

- **35 files** across the six modules the prompt named, totalling
  **2,580 lines**.
- **Clean build passes** under `-Wall -Wextra -Wpedantic`, no
  warnings; ctest 3/3.
- **Executable runs** end-to-end for every CLI action; render
  actions error honestly with an actionable hint when CUDA is off.
- **Every Stage 6A–9B source file is present.**
- **No source files are missing** for the audited stages. The three
  absences flagged in §7 are either out-of-scope (host tests, GPU
  runtime PPMs) or deliberately deferred (scene parser).

Audit B will cover the deeper questions (architecture, kernel
correctness, lighting math, relativity integration, CPU per-ray
violations, remaining issues). Audit A confirms the **shape** of
the tree is intact and the **build pipeline** is healthy — green
gate before any deeper review.

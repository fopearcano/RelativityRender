# Prototype File Index

Status: **inventory only**. No analysis, no classification,
no judgement. Audit step 1 of N.

This document is a flat index of every tracked file in the
prototype, with line counts and a single category label per
file. Subsequent audit steps will pull from this index when
classifying files for the rewrite.

## 1. How this index is built

- Tracked files only. `.git/`, `build/`, and `__pycache__/`
  are excluded.
- Line counts are `wc -l` (no tokenising; blank / comment
  lines counted).
- Categories are the nine labels the prompt requested:
  `core`, `math`, `image`, `gpu/cuda`, `renderer`,
  `scene`, `c4d/plugin`, `server`, `other`. Each file
  belongs to exactly one category.
- README files inside a module are listed under the same
  category as the module they document.

## 2. Category mapping

| Category    | Source directories                                                                                          |
|-------------|-------------------------------------------------------------------------------------------------------------|
| core        | `src/core/`, `src/main.cpp`                                                                                 |
| math        | `src/math/`, `src/relativity/`                                                                              |
| image       | `src/image/`, `src/renderer/AOV.{h,cpp}`                                                                    |
| gpu/cuda    | `src/gpu/`, `src/cuda/`                                                                                     |
| renderer    | `src/renderer/Hit.h`, `src/renderer/README.md`, `src/optix/`, `src/pathtracer/`                             |
| scene       | `src/scene/`, `src/io/`, `src/geometry/`, `src/camera/`, `src/lighting/`, `src/material/`, `src/texture/`   |
| c4d/plugin  | `integrations/c4d/`                                                                                         |
| server      | `src/server/`                                                                                               |
| other       | `tests/`, `scenes/`, `docs/`, `tools/`, `third_party/`, root build / docs files                             |

## 3. core

| File                       | Lines |
|----------------------------|------:|
| `src/main.cpp`             |   325 |
| `src/core/CommandLine.cpp` |   111 |
| `src/core/CommandLine.h`   |    35 |
| `src/core/Config.cpp`      |     5 |
| `src/core/Config.h`        |    40 |
| `src/core/Logger.cpp`      |    56 |
| `src/core/Logger.h`        |    14 |
| `src/core/Version.h`       |    11 |
| `src/core/README.md`       |    11 |

Subtotal: **9 files, 608 lines**.

## 4. math

| File                                   | Lines |
|----------------------------------------|------:|
| `src/math/Vec2.h`                      |    37 |
| `src/math/Vec3.h`                      |    78 |
| `src/math/Vec4.h`                      |    40 |
| `src/math/Mat4.h`                      |    81 |
| `src/math/Transform.h`                 |    26 |
| `src/math/MathUtils.h`                 |    54 |
| `src/math/README.md`                   |    11 |
| `src/relativity/RelativityMath.h`      |   167 |
| `src/relativity/RelativityMath.cuh`    |    13 |
| `src/relativity/RelativityParams.h`    |    43 |
| `src/relativity/README.md`             |    15 |

Subtotal: **11 files, 565 lines**.

## 5. image

| File                            | Lines |
|---------------------------------|------:|
| `src/image/Image.h`             |    67 |
| `src/image/Image.cpp`           |   104 |
| `src/image/Color.h`             |    40 |
| `src/image/Framebuffer.h`       |    37 |
| `src/image/Framebuffer.cpp`     |    20 |
| `src/image/README.md`           |    12 |
| `src/renderer/AOV.h`            |    81 |
| `src/renderer/AOV.cpp`          |    73 |

Subtotal: **8 files, 434 lines**.

## 6. gpu/cuda

| File                                  | Lines |
|---------------------------------------|------:|
| `src/gpu/GpuBuffer.h`                 |   113 |
| `src/gpu/GpuBuffer.cpp`               |    44 |
| `src/gpu/GpuDevice.h`                 |    37 |
| `src/gpu/GpuDevice.cpp`               |    45 |
| `src/gpu/GpuMesh.h`                   |    84 |
| `src/gpu/GpuMesh.cpp`                 |    56 |
| `src/gpu/GpuScene.h`                  |   218 |
| `src/gpu/GpuScene.cpp`                |   344 |
| `src/gpu/README.md`                   |    11 |
| `src/cuda/CudaBuffer.h`               |    23 |
| `src/cuda/CudaBuffer.cpp`             |    54 |
| `src/cuda/CudaContext.h`              |    21 |
| `src/cuda/CudaContext.cpp`            |    42 |
| `src/cuda/CudaRenderer.h`             |   104 |
| `src/cuda/CudaRenderer.cu`            |   348 |
| `src/cuda/CudaTestKernel.cu`          |   917 |
| `src/cuda/CudaScene.cuh`              |   127 |
| `src/cuda/CudaKernels.cuh`            |    65 |
| `src/cuda/CudaIntersection.cuh`       |   139 |
| `src/cuda/CudaTexture.cuh`            |    93 |
| `src/cuda/CudaMesh.cuh`               |    36 |
| `src/cuda/CudaMaterial.cuh`           |    16 |
| `src/cuda/CudaMaterialGraph.cuh`      |   213 |
| `src/cuda/CudaLight.cuh`              |    14 |
| `src/cuda/CudaAOV.cuh`                |    62 |
| `src/cuda/README.md`                  |    11 |

Subtotal: **26 files, 3 237 lines**.

## 7. renderer

| File                                | Lines |
|-------------------------------------|------:|
| `src/renderer/Hit.h`                |    43 |
| `src/renderer/README.md`            |    17 |
| `src/optix/OptixBackend.h`          |    76 |
| `src/optix/OptixBackend.cpp`        |   124 |
| `src/optix/OptixRenderer.h`         |    36 |
| `src/optix/OptixRenderer.cpp`       |    34 |
| `src/optix/README.md`               |    12 |
| `src/pathtracer/RNG.h`              |    77 |
| `src/pathtracer/RNG.cuh`            |    12 |
| `src/pathtracer/Sampling.h`         |   108 |
| `src/pathtracer/Sampling.cuh`       |    12 |
| `src/pathtracer/README.md`          |    14 |

Subtotal: **12 files, 565 lines**.

## 8. scene

| File                                              | Lines |
|---------------------------------------------------|------:|
| `src/scene/Scene.h`                               |   125 |
| `src/scene/Scene.cpp`                             |    18 |
| `src/scene/SceneObject.h`                         |    24 |
| `src/scene/Transform.h`                           |    14 |
| `src/scene/README.md`                             |    13 |
| `src/io/SceneLoader.h`                            |    51 |
| `src/io/SceneLoader.cpp`                          |   831 |
| `src/io/SceneWriter.h`                            |    40 |
| `src/io/SceneWriter.cpp`                          |   315 |
| `src/io/README.md`                                |    16 |
| `src/geometry/Sphere.h`                           |    31 |
| `src/geometry/Triangle.h`                         |    32 |
| `src/geometry/Mesh.h`                             |    54 |
| `src/geometry/Mesh.cpp`                           |    21 |
| `src/geometry/README.md`                          |    13 |
| `src/camera/Camera.h`                             |    68 |
| `src/camera/Camera.cpp`                           |    92 |
| `src/camera/CameraRay.h`                          |    58 |
| `src/camera/README.md`                            |    12 |
| `src/lighting/Light.h`                            |    92 |
| `src/lighting/Light.cpp`                          |    70 |
| `src/lighting/README.md`                          |    11 |
| `src/material/Material.h`                         |    48 |
| `src/material/Material.cpp`                       |    49 |
| `src/material/MaterialTypes.h`                    |    52 |
| `src/material/MaterialGraph.h`                    |   168 |
| `src/material/MaterialGraph.cpp`                  |   442 |
| `src/material/GpuMaterial.h`                      |   221 |
| `src/material/GpuMaterial.cpp`                    |   395 |
| `src/material/graph/Graph.h`                      |   165 |
| `src/material/graph/Graph.cpp`                    |   522 |
| `src/material/graph/Node.h`                       |   127 |
| `src/material/graph/Socket.h`                     |    89 |
| `src/material/graph/GraphEvaluator.h`             |    91 |
| `src/material/graph/GraphEvaluator.cpp`           |   116 |
| `src/material/README.md`                          |    12 |
| `src/texture/Texture.h`                           |    58 |
| `src/texture/Texture.cpp`                         |    17 |
| `src/texture/ImageTexture.h`                      |    85 |
| `src/texture/ImageTexture.cpp`                    |    45 |
| `src/texture/README.md`                           |    12 |

Subtotal: **41 files, 4 715 lines**.

## 9. c4d/plugin

| File                                                                                    | Lines |
|-----------------------------------------------------------------------------------------|------:|
| `integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp`                    |  2 075 |
| `integrations/c4d/RelativityRenderBridge/rrscene_writer.py`                             |   496 |
| `integrations/c4d/RelativityRenderBridge/server_client.py`                              |   245 |
| `integrations/c4d/RelativityRenderBridge/preview_state.py`                              |   237 |
| `integrations/c4d/RelativityRenderBridge/image_io.py`                                   |   263 |
| `integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py`                  |   623 |
| `integrations/c4d/RelativityRenderBridge/tests/test_image_io.py`                        |   287 |
| `integrations/c4d/RelativityRenderBridge/tests/test_server_client.py`                   |   237 |
| `integrations/c4d/RelativityRenderBridge/tests/test_preview_state.py`                   |   365 |
| `integrations/c4d/RelativityRenderBridge/README.md`                                     |   357 |
| `integrations/c4d/README.md`                                                            |    29 |
| `integrations/README.md`                                                                |    18 |

Subtotal: **12 files, 5 232 lines**.

## 10. server

| File                          | Lines |
|-------------------------------|------:|
| `src/server/RenderServer.h`   |   128 |
| `src/server/RenderServer.cpp` |   393 |
| `src/server/README.md`        |    14 |

Subtotal: **3 files, 535 lines**.

## 11. other

### 11.1 Tests

| File                                     | Lines |
|------------------------------------------|------:|
| `tests/material_graph_core_tests.cpp`    |  1 479 |
| `tests/material_graph_tests.cpp`         |   513 |
| `tests/gpu_tests.cpp`                    |   455 |
| `tests/geometry_tests.cpp`               |   374 |
| `tests/io_tests.cpp`                     |   342 |
| `tests/server_tests.cpp`                 |   323 |
| `tests/aov_tests.cpp`                    |   265 |
| `tests/sampling_tests.cpp`               |   246 |
| `tests/relativity_tests.cpp`             |   234 |
| `tests/texture_tests.cpp`                |   216 |
| `tests/math_tests.cpp`                   |   195 |
| `tests/camera_tests.cpp`                 |   190 |
| `tests/image_tests.cpp`                  |   172 |
| `tests/scene_tests.cpp`                  |   167 |
| `tests/mesh_tests.cpp`                   |   165 |
| `tests/material_tests.cpp`               |   148 |
| `tests/lighting_tests.cpp`               |   142 |
| `tests/README.md`                        |    19 |

Subtotal: **18 files, 5 645 lines**.

### 11.2 Scenes

| File                              | Lines |
|-----------------------------------|------:|
| `scenes/test_geometry.rrscene`    |    72 |
| `scenes/test.rrscene`             |    65 |
| `scenes/test_minimal.rrscene`     |    23 |

Subtotal: **3 files, 160 lines**.

### 11.3 Docs

| File                                  | Lines |
|---------------------------------------|------:|
| `docs/BUILD_PLAN.md`                  |  7 116 |
| `docs/C4D_NATIVE_RENDERER_PLAN.md`    |  1 743 |
| `docs/MATERIAL_GRAPH_SPEC.md`         |  1 501 |
| `docs/OPTIX_BACKEND_PLAN.md`          |   865 |
| `docs/RRSCENE_FORMAT.md`              |   634 |
| `docs/DENOISING_PLAN.md`              |   464 |
| `docs/MODULE_MAP.md`                  |   369 |
| `docs/MASTER_ARCHITECTURE.md`         |   349 |
| `docs/MILESTONE_ROADMAP.md`           |   324 |
| `docs/DEVELOPMENT_RULES.md`           |   147 |

Subtotal: **10 files, 13 512 lines**.

### 11.4 Build / repo top-level

| File                                          | Lines |
|-----------------------------------------------|------:|
| `CMakeLists.txt`                              |   565 |
| `README.md`                                   |    83 |
| `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` | 135 |
| `src/README.md`                               |    29 |

Subtotal: **4 files, 812 lines**.

### 11.5 Empty placeholder dirs

| File                          | Lines |
|-------------------------------|------:|
| `tools/README.md`             |    16 |
| `third_party/README.md`       |    24 |

Subtotal: **2 files, 40 lines**.

## 12. Totals

| Category    | Files | Lines  |
|-------------|------:|-------:|
| core        |     9 |    608 |
| math        |    11 |    565 |
| image       |     8 |    434 |
| gpu/cuda    |    26 |  3 237 |
| renderer    |    12 |    565 |
| scene       |    41 |  4 715 |
| c4d/plugin  |    12 |  5 232 |
| server      |     3 |    535 |
| other       |    37 | 20 169 |
| **TOTAL**   | **159** | **36 060** |

The "other" total is dominated by docs (13 512 lines) and
tests (5 645 lines); production code outside `integrations/`
is `core + math + image + gpu/cuda + renderer + scene + server`
= 110 files, 10 659 lines.

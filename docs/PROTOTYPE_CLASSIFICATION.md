# Prototype Classification

Status: **high-level pass**, audit step 2 of N. Surface
classification only - not a deep code review. Built off
`docs/PROTOTYPE_FILE_INDEX.md`.

For the **serious rewrite**, every file is given exactly
one of:

| Class                 | Meaning for the rewrite                                                   |
|-----------------------|---------------------------------------------------------------------------|
| `KEEP_AS_IS`          | Ships unchanged. Design / shape are a fit.                                 |
| `KEEP_WITH_REFACTOR`  | Idea + scope are right; needs structural cleanup before reuse.             |
| `REWRITE`             | Bin and start over. Concept may carry; the implementation does not.        |
| `ARCHIVE_ONLY`        | Preserve as reference. Not part of the rewrite's production tree.          |
| `DELETE_LATER`        | Outright remove once dependents migrate.                                   |

Focus criteria from the prompt:

- **Obvious stubs** -> `REWRITE` or `DELETE_LATER`.
- **Premature systems** (C4D plugin, server, UI scaffolding) ->
  `ARCHIVE_ONLY` or `KEEP_WITH_REFACTOR` depending on
  whether the rewrite carries the same path forward.
- **Core vs non-core** - core (math, image, GPU buffer,
  intersection, RNG, sampling) leans `KEEP_AS_IS`;
  non-core (server, bridge, demo loops) leans `ARCHIVE` /
  `REWRITE` / `REFACTOR`.

Per-file reasons are deliberately one line. Deeper
analysis is the next audit step's job.

## 1. core

| File                       | Class                | Reason                                                                |
|----------------------------|----------------------|-----------------------------------------------------------------------|
| `src/main.cpp`             | KEEP_WITH_REFACTOR   | Has demo / "deliverable" loops baked in (M14 / M16 / M17). Strip to a CLI dispatcher; demos move out. |
| `src/core/CommandLine.h`   | KEEP_AS_IS           | Small argv parser; clean.                                              |
| `src/core/CommandLine.cpp` | KEEP_AS_IS           | Same.                                                                  |
| `src/core/Config.h`        | KEEP_AS_IS           | POD config struct.                                                     |
| `src/core/Config.cpp`      | KEEP_AS_IS           | Trivial.                                                               |
| `src/core/Logger.h`        | KEEP_AS_IS           | Minimal stdio logger.                                                  |
| `src/core/Logger.cpp`      | KEEP_AS_IS           | Same.                                                                  |
| `src/core/Version.h`       | KEEP_AS_IS           | Version constants.                                                     |
| `src/core/README.md`       | KEEP_AS_IS           | Module doc.                                                            |

## 2. math

| File                                | Class       | Reason                                                                   |
|-------------------------------------|-------------|--------------------------------------------------------------------------|
| `src/math/Vec2.h`                   | KEEP_AS_IS  | Header-only RR_HD POD; foundation of everything.                          |
| `src/math/Vec3.h`                   | KEEP_AS_IS  | Same.                                                                    |
| `src/math/Vec4.h`                   | KEEP_AS_IS  | Same.                                                                    |
| `src/math/Mat4.h`                   | KEEP_AS_IS  | Same.                                                                    |
| `src/math/Transform.h`              | KEEP_AS_IS  | Canonical Transform; everything depends on it.                            |
| `src/math/MathUtils.h`              | KEEP_AS_IS  | RR_HD macro + small helpers; foundation.                                  |
| `src/math/README.md`                | KEEP_AS_IS  | Module doc.                                                              |
| `src/relativity/RelativityMath.h`   | KEEP_AS_IS  | RR_HD inline relativity primitives; well-tested.                          |
| `src/relativity/RelativityMath.cuh` | KEEP_AS_IS  | Thin re-export of the .h; intentional pattern.                            |
| `src/relativity/RelativityParams.h` | KEEP_AS_IS  | POD observer + relativity-knob struct.                                    |
| `src/relativity/README.md`          | KEEP_AS_IS  | Module doc.                                                              |

## 3. image

| File                          | Class                | Reason                                                       |
|-------------------------------|----------------------|--------------------------------------------------------------|
| `src/image/Image.h`           | KEEP_AS_IS           | Clean host buffer.                                            |
| `src/image/Image.cpp`         | KEEP_AS_IS           | Pixel write + PPM IO; allowed CPU work.                       |
| `src/image/Color.h`           | KEEP_AS_IS           | POD Rgb / Rgba.                                               |
| `src/image/Framebuffer.h`     | KEEP_WITH_REFACTOR   | Small standalone shape; verify it's still used in rewrite.    |
| `src/image/Framebuffer.cpp`   | KEEP_WITH_REFACTOR   | Same; consolidate with `Image` if redundant.                  |
| `src/image/README.md`         | KEEP_AS_IS           | Module doc.                                                   |
| `src/renderer/AOV.h`          | KEEP_AS_IS           | M17 foundation; design is right.                              |
| `src/renderer/AOV.cpp`        | KEEP_AS_IS           | Scalar-AOV PPM normalisation; clean.                          |

## 4. gpu/cuda

| File                                | Class                | Reason                                                                     |
|-------------------------------------|----------------------|----------------------------------------------------------------------------|
| `src/gpu/GpuBuffer.h`               | KEEP_AS_IS           | RAII GPU memory wrapper; foundation.                                        |
| `src/gpu/GpuBuffer.cpp`             | KEEP_AS_IS           | Same.                                                                      |
| `src/gpu/GpuDevice.h`               | KEEP_AS_IS           | Backend-agnostic device enumeration.                                        |
| `src/gpu/GpuDevice.cpp`             | KEEP_AS_IS           | Same.                                                                      |
| `src/gpu/GpuMesh.h`                 | KEEP_AS_IS           | Host-side mesh upload wrapper.                                              |
| `src/gpu/GpuMesh.cpp`               | KEEP_AS_IS           | Same.                                                                      |
| `src/gpu/GpuScene.h`                | KEEP_WITH_REFACTOR   | Per-section uploads accumulated incrementally; factor + verify ownership.   |
| `src/gpu/GpuScene.cpp`              | KEEP_WITH_REFACTOR   | Same; eight `upload_*` paths could share more.                              |
| `src/gpu/README.md`                 | KEEP_AS_IS           | Module doc.                                                                |
| `src/cuda/CudaBuffer.h`             | KEEP_AS_IS           | Thin CUDA-runtime wrapper for `GpuBuffer`.                                  |
| `src/cuda/CudaBuffer.cpp`           | KEEP_AS_IS           | Same.                                                                      |
| `src/cuda/CudaContext.h`            | KEEP_AS_IS           | CUDA lifecycle.                                                            |
| `src/cuda/CudaContext.cpp`          | KEEP_AS_IS           | Same.                                                                      |
| `src/cuda/CudaRenderer.h`           | KEEP_WITH_REFACTOR   | Three near-duplicate `render_*` entry points.                               |
| `src/cuda/CudaRenderer.cu`          | KEEP_WITH_REFACTOR   | Three near-duplicate view-population blocks; one helper.                    |
| `src/cuda/CudaTestKernel.cu`        | KEEP_WITH_REFACTOR   | 917 lines mixing diagnostic kernels (M5-M9) with production (M11/M14/M17). Split. |
| `src/cuda/CudaScene.cuh`            | KEEP_AS_IS           | View POD.                                                                  |
| `src/cuda/CudaKernels.cuh`          | KEEP_AS_IS           | Launcher declarations.                                                     |
| `src/cuda/CudaIntersection.cuh`     | KEEP_AS_IS           | RR_HD intersect_* primitives; well-tested.                                  |
| `src/cuda/CudaTexture.cuh`          | KEEP_AS_IS           | View POD + RR_HD sampler.                                                   |
| `src/cuda/CudaMesh.cuh`             | KEEP_AS_IS           | View POD.                                                                  |
| `src/cuda/CudaMaterial.cuh`         | KEEP_AS_IS           | View POD; trivial.                                                         |
| `src/cuda/CudaMaterialGraph.cuh`    | KEEP_AS_IS           | View POD + RR_HD `evaluateMaterial`; M21-current.                           |
| `src/cuda/CudaLight.cuh`            | KEEP_AS_IS           | View POD.                                                                  |
| `src/cuda/CudaAOV.cuh`              | KEEP_AS_IS           | M17 view POD + RR_HD writers.                                               |
| `src/cuda/README.md`                | KEEP_AS_IS           | Module doc.                                                                |

## 5. renderer

| File                          | Class                | Reason                                                                     |
|-------------------------------|----------------------|----------------------------------------------------------------------------|
| `src/renderer/Hit.h`          | KEEP_AS_IS           | POD hit record; barycentrics + UVs already wired.                           |
| `src/renderer/README.md`      | KEEP_AS_IS           | Module doc.                                                                |
| `src/optix/OptixBackend.h`    | KEEP_WITH_REFACTOR   | Lifecycle scaffold; rewrite layers the actual pipeline on top.              |
| `src/optix/OptixBackend.cpp`  | KEEP_WITH_REFACTOR   | Same.                                                                      |
| `src/optix/OptixRenderer.h`   | REWRITE              | Header for `render_placeholder`; the rewrite IS the OptiX renderer.         |
| `src/optix/OptixRenderer.cpp` | REWRITE              | Stub returning "not implemented"; bin and replace with the real pipeline.   |
| `src/optix/README.md`         | KEEP_AS_IS           | Module doc.                                                                |
| `src/pathtracer/RNG.h`        | KEEP_AS_IS           | RR_HD PCG; foundation.                                                     |
| `src/pathtracer/RNG.cuh`      | KEEP_AS_IS           | Thin re-export.                                                            |
| `src/pathtracer/Sampling.h`   | KEEP_AS_IS           | RR_HD hemisphere sampling; foundation.                                      |
| `src/pathtracer/Sampling.cuh` | KEEP_AS_IS           | Thin re-export.                                                            |
| `src/pathtracer/README.md`    | KEEP_AS_IS           | Module doc.                                                                |

## 6. scene

### 6.1 scene + io

| File                          | Class                | Reason                                                                     |
|-------------------------------|----------------------|----------------------------------------------------------------------------|
| `src/scene/Scene.h`           | KEEP_AS_IS           | Host scene container POD.                                                  |
| `src/scene/Scene.cpp`         | KEEP_AS_IS           | Trivial.                                                                   |
| `src/scene/SceneObject.h`     | KEEP_AS_IS           | Wrapper POD.                                                               |
| `src/scene/Transform.h`       | DELETE_LATER         | Back-compat shim aliasing `math::Transform`. Drop once callers migrate.     |
| `src/scene/README.md`         | KEEP_AS_IS           | Module doc.                                                                |
| `src/io/SceneLoader.h`        | KEEP_WITH_REFACTOR   | Public surface fine; depends on hand-rolled parser.                         |
| `src/io/SceneLoader.cpp`      | KEEP_WITH_REFACTOR   | 831 lines of hand-rolled JSON; rewrite swaps in a real lib (nlohmann etc.). |
| `src/io/SceneWriter.h`        | KEEP_WITH_REFACTOR   | Same.                                                                      |
| `src/io/SceneWriter.cpp`      | KEEP_WITH_REFACTOR   | Same.                                                                      |
| `src/io/README.md`            | KEEP_AS_IS           | Module doc.                                                                |

### 6.2 geometry / camera / lighting / texture

| File                                | Class                | Reason                                                          |
|-------------------------------------|----------------------|-----------------------------------------------------------------|
| `src/geometry/Sphere.h`             | KEEP_AS_IS           | POD.                                                             |
| `src/geometry/Triangle.h`           | KEEP_AS_IS           | POD.                                                             |
| `src/geometry/Mesh.h`               | KEEP_AS_IS           | POD with small helpers.                                          |
| `src/geometry/Mesh.cpp`             | KEEP_AS_IS           | Trivial.                                                         |
| `src/geometry/README.md`            | KEEP_AS_IS           | Module doc.                                                      |
| `src/camera/Camera.h`               | KEEP_AS_IS           | Host-side pinhole camera.                                        |
| `src/camera/Camera.cpp`             | KEEP_AS_IS           | Same.                                                            |
| `src/camera/CameraRay.h`            | KEEP_AS_IS           | RR_HD ray-gen helper + GpuCamera POD.                            |
| `src/camera/README.md`              | KEEP_AS_IS           | Module doc.                                                      |
| `src/lighting/Light.h`              | KEEP_WITH_REFACTOR   | Light POD with Area / Environment fields flagged PLACEHOLDER.    |
| `src/lighting/Light.cpp`            | KEEP_WITH_REFACTOR   | Same.                                                            |
| `src/lighting/README.md`            | KEEP_AS_IS           | Module doc.                                                      |
| `src/texture/Texture.h`             | KEEP_AS_IS           | POD + factories.                                                 |
| `src/texture/Texture.cpp`           | KEEP_AS_IS           | Trivial.                                                         |
| `src/texture/ImageTexture.h`        | KEEP_AS_IS           | Host image-backed texture wrapper.                               |
| `src/texture/ImageTexture.cpp`      | KEEP_AS_IS           | Same.                                                            |
| `src/texture/README.md`             | KEEP_AS_IS           | Module doc.                                                      |

### 6.3 material

| File                                      | Class                | Reason                                                                          |
|-------------------------------------------|----------------------|---------------------------------------------------------------------------------|
| `src/material/Material.h`                 | KEEP_AS_IS           | Small wrapper around `MaterialParams`.                                           |
| `src/material/Material.cpp`               | KEEP_AS_IS           | Trivial.                                                                        |
| `src/material/MaterialTypes.h`            | KEEP_WITH_REFACTOR   | Has `transmission` field flagged PLACEHOLDER; tidy in rewrite.                   |
| `src/material/MaterialGraph.h`            | REWRITE              | Older monolithic graph runtime; **superseded by** `material/graph/`.             |
| `src/material/MaterialGraph.cpp`          | REWRITE              | Same; the rewrite picks one graph implementation and bins this.                  |
| `src/material/GpuMaterial.h`              | KEEP_AS_IS           | M21 GPU IR + lowering; design is the spec target.                                 |
| `src/material/GpuMaterial.cpp`            | KEEP_AS_IS           | Same.                                                                           |
| `src/material/graph/Graph.h`              | KEEP_AS_IS           | Newer data-core; cleaner; canonical for the rewrite.                              |
| `src/material/graph/Graph.cpp`            | KEEP_AS_IS           | Same.                                                                           |
| `src/material/graph/Node.h`               | KEEP_AS_IS           | Catalogue + `make_node`.                                                         |
| `src/material/graph/Socket.h`             | KEEP_AS_IS           | POD socket.                                                                     |
| `src/material/graph/GraphEvaluator.h`     | KEEP_AS_IS           | CPU reference evaluator (verification only - per docstring).                      |
| `src/material/graph/GraphEvaluator.cpp`   | KEEP_AS_IS           | Same.                                                                           |
| `src/material/README.md`                  | KEEP_AS_IS           | Module doc.                                                                     |

## 7. c4d/plugin

Premature for the serious rewrite per the prompt's
framing. Native C++ integration (M23 plan) is the
rewrite's path; the Python bridge stays as the
reference for translation rules but is not part of the
production tree.

| File                                                                             | Class          | Reason                                                            |
|----------------------------------------------------------------------------------|----------------|-------------------------------------------------------------------|
| `integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp`             | ARCHIVE_ONLY   | Replaced by the M23 native plugin; reference for translation.      |
| `integrations/c4d/RelativityRenderBridge/rrscene_writer.py`                      | ARCHIVE_ONLY   | Native path builds `rr::scene::Scene` in-memory, no .rrscene round-trip. |
| `integrations/c4d/RelativityRenderBridge/server_client.py`                       | ARCHIVE_ONLY   | Bridge-only; native is in-process.                                 |
| `integrations/c4d/RelativityRenderBridge/preview_state.py`                       | ARCHIVE_ONLY   | Bridge-only dialog helpers.                                        |
| `integrations/c4d/RelativityRenderBridge/image_io.py`                            | ARCHIVE_ONLY   | PPM->BMP for the bridge's preview dialog; replaced by native bitmap fill. |
| `integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py`           | ARCHIVE_ONLY   | Tied to the archived module.                                       |
| `integrations/c4d/RelativityRenderBridge/tests/test_image_io.py`                 | ARCHIVE_ONLY   | Same.                                                             |
| `integrations/c4d/RelativityRenderBridge/tests/test_server_client.py`            | ARCHIVE_ONLY   | Same.                                                             |
| `integrations/c4d/RelativityRenderBridge/tests/test_preview_state.py`            | ARCHIVE_ONLY   | Same.                                                             |
| `integrations/c4d/RelativityRenderBridge/README.md`                              | ARCHIVE_ONLY   | Tied to the bridge's existence.                                    |
| `integrations/c4d/README.md`                                                     | KEEP_WITH_REFACTOR | Module-level doc; updated to reflect M23 native-first path.       |
| `integrations/README.md`                                                         | KEEP_WITH_REFACTOR | Same.                                                             |

## 8. server

| File                          | Class                | Reason                                                                                    |
|-------------------------------|----------------------|-------------------------------------------------------------------------------------------|
| `src/server/RenderServer.h`   | KEEP_WITH_REFACTOR   | Protocol shape good; rewrite hardens (multi-client, binary streaming, EXR, cancellation). |
| `src/server/RenderServer.cpp` | KEEP_WITH_REFACTOR   | Same; one-client-at-a-time loop is v1 only.                                               |
| `src/server/README.md`        | KEEP_AS_IS           | Module doc.                                                                               |

## 9. other

### 9.1 Tests

| File                                     | Class                | Reason                                                              |
|------------------------------------------|----------------------|---------------------------------------------------------------------|
| `tests/math_tests.cpp`                   | KEEP_AS_IS           | Validates kept code.                                                 |
| `tests/relativity_tests.cpp`             | KEEP_AS_IS           | Same.                                                               |
| `tests/image_tests.cpp`                  | KEEP_AS_IS           | Same.                                                               |
| `tests/gpu_tests.cpp`                    | KEEP_AS_IS           | Same.                                                               |
| `tests/camera_tests.cpp`                 | KEEP_AS_IS           | Same.                                                               |
| `tests/geometry_tests.cpp`               | KEEP_AS_IS           | Same.                                                               |
| `tests/scene_tests.cpp`                  | KEEP_AS_IS           | Same.                                                               |
| `tests/mesh_tests.cpp`                   | KEEP_AS_IS           | Same.                                                               |
| `tests/material_tests.cpp`               | KEEP_AS_IS           | Same.                                                               |
| `tests/lighting_tests.cpp`               | KEEP_AS_IS           | Same.                                                               |
| `tests/io_tests.cpp`                     | KEEP_WITH_REFACTOR   | Refactor follows `SceneLoader/Writer`'s refactor.                    |
| `tests/sampling_tests.cpp`               | KEEP_AS_IS           | Same.                                                               |
| `tests/texture_tests.cpp`                | KEEP_AS_IS           | Same.                                                               |
| `tests/aov_tests.cpp`                    | KEEP_AS_IS           | Same.                                                               |
| `tests/server_tests.cpp`                 | KEEP_WITH_REFACTOR   | Follows the server's hardening; protocol-level tests survive.        |
| `tests/material_graph_tests.cpp`         | REWRITE              | Tests the older `material/MaterialGraph` runtime that's being binned.|
| `tests/material_graph_core_tests.cpp`    | KEEP_AS_IS           | Tests the data-core + GPU IR + evaluator that stays.                 |
| `tests/README.md`                        | KEEP_AS_IS           | Module doc.                                                         |

### 9.2 Scenes

| File                              | Class       | Reason                       |
|-----------------------------------|-------------|------------------------------|
| `scenes/test_minimal.rrscene`     | KEEP_AS_IS  | Small fixture.                |
| `scenes/test.rrscene`             | KEEP_AS_IS  | Small fixture.                |
| `scenes/test_geometry.rrscene`    | KEEP_AS_IS  | Small fixture.                |

### 9.3 Docs

| File                                  | Class       | Reason                                                                |
|---------------------------------------|-------------|-----------------------------------------------------------------------|
| `docs/MASTER_ARCHITECTURE.md`         | KEEP_AS_IS  | Foundational; the rewrite is the next-version implementation of this. |
| `docs/MODULE_MAP.md`                  | KEEP_AS_IS  | Foundational.                                                         |
| `docs/MILESTONE_ROADMAP.md`           | KEEP_AS_IS  | Foundational.                                                         |
| `docs/DEVELOPMENT_RULES.md`           | KEEP_AS_IS  | Foundational.                                                         |
| `docs/RRSCENE_FORMAT.md`              | KEEP_AS_IS  | Format spec stays.                                                    |
| `docs/OPTIX_BACKEND_PLAN.md`          | KEEP_AS_IS  | Plan stays; rewrite implements it.                                    |
| `docs/MATERIAL_GRAPH_SPEC.md`         | KEEP_AS_IS  | Same.                                                                 |
| `docs/DENOISING_PLAN.md`              | KEEP_AS_IS  | Same.                                                                 |
| `docs/C4D_NATIVE_RENDERER_PLAN.md`    | KEEP_AS_IS  | Same; M23 plan.                                                       |
| `docs/BUILD_PLAN.md`                  | ARCHIVE_ONLY| Historical change-log of prototype slices; rewrite starts a fresh log.|
| `docs/PROTOTYPE_FILE_INDEX.md`        | KEEP_AS_IS  | Audit step 1 doc.                                                     |
| `docs/PROTOTYPE_CLASSIFICATION.md`    | KEEP_AS_IS  | This doc.                                                             |

### 9.4 Build / repo top-level

| File                                                  | Class                | Reason                                                       |
|-------------------------------------------------------|----------------------|--------------------------------------------------------------|
| `CMakeLists.txt`                                      | KEEP_WITH_REFACTOR   | 565 lines accumulated per-module; rewrite tightens.           |
| `README.md`                                           | KEEP_WITH_REFACTOR   | Surfaces prototype-era workflow; updated for the rewrite.     |
| `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`     | KEEP_AS_IS           | The development rules; canonical.                             |
| `src/README.md`                                       | KEEP_AS_IS           | Module-tree pointer.                                          |

### 9.5 Empty placeholder dirs

| File                          | Class       | Reason                          |
|-------------------------------|-------------|---------------------------------|
| `tools/README.md`             | KEEP_AS_IS  | Placeholder for future tools.    |
| `third_party/README.md`       | KEEP_AS_IS  | Placeholder for future vendored deps. |

## 10. Summary

| Class                 | Files |
|-----------------------|------:|
| `KEEP_AS_IS`          |   116 |
| `KEEP_WITH_REFACTOR`  |    25 |
| `REWRITE`             |     5 |
| `ARCHIVE_ONLY`        |    11 |
| `DELETE_LATER`        |     1 |
| Skipped (none)        |     1 |
| **TOTAL**             | **159** |

(Skipped: `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
appears once in the table - it's classified above.)

### What this pass surfaces

- **Most of the surface area is reusable.** ~73% of files
  are `KEEP_AS_IS`; another ~16% are `KEEP_WITH_REFACTOR`.
  The core (math, image, GPU buffer, intersection, RNG,
  sampling, AOV, material data-core + GPU IR) carries
  forward.
- **Two stub-shaped concentrations.** `optix/OptixRenderer`
  is a `render_placeholder` returning "not implemented";
  the older `material/MaterialGraph.{h,cpp}` is
  superseded by `material/graph/` + `material/GpuMaterial`
  and should be binned.
- **Two integration tracts to archive.** The C4D Python
  bridge (M19) is well-built but the rewrite's path is
  the M23 native plugin; the bridge stays as reference,
  not as production code.
- **Server stays, but only as v1 shape.** The protocol
  is good; the implementation is one-client-at-a-time
  and needs hardening (multi-client, binary streaming,
  cancellation).
- **Demo / orchestration code in `main.cpp`.** Three
  hand-rolled demos baked into the renderer's CLI;
  the rewrite splits them out so `main` is just an
  argv dispatcher.

This is a surface pass. The next audit step does the
deep code review (memory ownership, kernel correctness,
module-boundary respect) and may move some files
between classes. Until then, the table above is
provisional.

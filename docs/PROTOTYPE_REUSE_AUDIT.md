# Prototype Reuse Audit — Final

Date: 2026-04-29
Branch: `claude/create-docs-architecture-T2Dp5`
Status: **Final.** Consolidates audit steps 1–8.
Sources:
- `docs/PROTOTYPE_FILE_INDEX.md` (step 1)
- `docs/PROTOTYPE_CLASSIFICATION.md` (step 2)
- `docs/GPU_RENDER_AUDIT.md` (step 3)
- `docs/GPU_MEMORY_AUDIT.md` (step 4)
- `docs/ARCHITECTURE_AUDIT.md` (step 5)
- `docs/BUILD_SYSTEM_AUDIT.md` (step 6)
- `docs/PREMATURE_SYSTEMS_AUDIT.md` (step 7)
- `docs/API_AUDIT.md` (step 8)

This document is the single per-file decision table for the rewrite.
`docs/REUSE_PLAN.md` (companion) carries the migration strategy and the
minimum-safe starting point.

The five categories are unchanged from step 2:

- **KEEP_AS_IS** — ships unchanged.
- **KEEP_WITH_REFACTOR** — idea right, structure needs cleanup.
- **REWRITE** — bin and start over.
- **ARCHIVE_ONLY** — preserve as reference, not in the rewrite tree.
- **DELETE_LATER** — remove once dependents migrate.

Step 2's surface pass had 116 / 25 / 5 / 11 / 1. The deeper passes (steps
3–8) confirmed every classification. **No file moved between categories.**
The table below is the surface-pass table, finalised.

---

## 1. Final classification table

### 1.1 core

| File | Decision |
|---|---|
| `src/main.cpp` | KEEP_WITH_REFACTOR |
| `src/core/CommandLine.{h,cpp}` | KEEP_AS_IS |
| `src/core/Config.{h,cpp}` | KEEP_AS_IS |
| `src/core/Logger.{h,cpp}` | KEEP_AS_IS |
| `src/core/Version.h` | KEEP_AS_IS |
| `src/core/README.md` | KEEP_AS_IS |

### 1.2 math + relativity

| File | Decision |
|---|---|
| `src/math/Vec2.h`, `Vec3.h`, `Vec4.h`, `Mat4.h` | KEEP_AS_IS |
| `src/math/Transform.h` | KEEP_AS_IS |
| `src/math/MathUtils.h` | KEEP_AS_IS |
| `src/math/README.md` | KEEP_AS_IS |
| `src/relativity/RelativityMath.h` | KEEP_AS_IS |
| `src/relativity/RelativityMath.cuh` | KEEP_AS_IS |
| `src/relativity/RelativityParams.h` | KEEP_AS_IS |
| `src/relativity/README.md` | KEEP_AS_IS |

### 1.3 image + AOV

| File | Decision |
|---|---|
| `src/image/Image.{h,cpp}` | KEEP_AS_IS |
| `src/image/Color.h` | KEEP_AS_IS |
| `src/image/Framebuffer.{h,cpp}` | KEEP_WITH_REFACTOR |
| `src/image/README.md` | KEEP_AS_IS |
| `src/renderer/AOV.{h,cpp}` | KEEP_AS_IS |
| `src/renderer/Hit.h` | KEEP_AS_IS |
| `src/renderer/README.md` | KEEP_AS_IS |

### 1.4 gpu + cuda

| File | Decision |
|---|---|
| `src/gpu/GpuBuffer.{h,cpp}` | KEEP_AS_IS |
| `src/gpu/GpuDevice.{h,cpp}` | KEEP_AS_IS |
| `src/gpu/GpuMesh.{h,cpp}` | KEEP_AS_IS |
| `src/gpu/GpuScene.{h,cpp}` | KEEP_WITH_REFACTOR |
| `src/gpu/README.md` | KEEP_AS_IS |
| `src/cuda/CudaBuffer.{h,cpp}` | KEEP_AS_IS |
| `src/cuda/CudaContext.{h,cpp}` | KEEP_AS_IS |
| `src/cuda/CudaRenderer.{h,cu}` | KEEP_WITH_REFACTOR |
| `src/cuda/CudaTestKernel.cu` | KEEP_WITH_REFACTOR |
| `src/cuda/CudaScene.cuh` | KEEP_AS_IS |
| `src/cuda/CudaKernels.cuh` | KEEP_AS_IS |
| `src/cuda/CudaIntersection.cuh` | KEEP_AS_IS |
| `src/cuda/CudaTexture.cuh` | KEEP_AS_IS |
| `src/cuda/CudaMesh.cuh` | KEEP_AS_IS |
| `src/cuda/CudaMaterial.cuh` | KEEP_AS_IS |
| `src/cuda/CudaMaterialGraph.cuh` | KEEP_AS_IS |
| `src/cuda/CudaLight.cuh` | KEEP_AS_IS |
| `src/cuda/CudaAOV.cuh` | KEEP_AS_IS |
| `src/cuda/README.md` | KEEP_AS_IS |

### 1.5 optix + pathtracer

| File | Decision |
|---|---|
| `src/optix/OptixBackend.{h,cpp}` | KEEP_WITH_REFACTOR |
| `src/optix/OptixRenderer.{h,cpp}` | REWRITE |
| `src/optix/README.md` | KEEP_AS_IS |
| `src/pathtracer/RNG.{h,cuh}` | KEEP_AS_IS |
| `src/pathtracer/Sampling.{h,cuh}` | KEEP_AS_IS |
| `src/pathtracer/README.md` | KEEP_AS_IS |

### 1.6 scene + io + geometry + camera + lighting + texture

| File | Decision |
|---|---|
| `src/scene/Scene.{h,cpp}` | KEEP_AS_IS |
| `src/scene/SceneObject.h` | KEEP_AS_IS |
| `src/scene/Transform.h` | DELETE_LATER |
| `src/scene/README.md` | KEEP_AS_IS |
| `src/io/SceneLoader.{h,cpp}` | KEEP_WITH_REFACTOR |
| `src/io/SceneWriter.{h,cpp}` | KEEP_WITH_REFACTOR |
| `src/io/README.md` | KEEP_AS_IS |
| `src/geometry/Sphere.h`, `Triangle.h` | KEEP_AS_IS |
| `src/geometry/Mesh.{h,cpp}` | KEEP_AS_IS |
| `src/geometry/README.md` | KEEP_AS_IS |
| `src/camera/Camera.{h,cpp}` | KEEP_AS_IS |
| `src/camera/CameraRay.h` | KEEP_AS_IS |
| `src/camera/README.md` | KEEP_AS_IS |
| `src/lighting/Light.{h,cpp}` | KEEP_WITH_REFACTOR |
| `src/lighting/README.md` | KEEP_AS_IS |
| `src/texture/Texture.{h,cpp}` | KEEP_AS_IS |
| `src/texture/ImageTexture.{h,cpp}` | KEEP_AS_IS |
| `src/texture/README.md` | KEEP_AS_IS |

### 1.7 material

| File | Decision |
|---|---|
| `src/material/Material.{h,cpp}` | KEEP_AS_IS |
| `src/material/MaterialTypes.h` | KEEP_WITH_REFACTOR |
| `src/material/MaterialGraph.{h,cpp}` | REWRITE |
| `src/material/GpuMaterial.{h,cpp}` | KEEP_AS_IS |
| `src/material/graph/Graph.{h,cpp}` | KEEP_AS_IS |
| `src/material/graph/Node.h` | KEEP_AS_IS |
| `src/material/graph/Socket.h` | KEEP_AS_IS |
| `src/material/graph/GraphEvaluator.{h,cpp}` | KEEP_AS_IS |
| `src/material/README.md` | KEEP_AS_IS |

### 1.8 c4d / integrations

| File | Decision |
|---|---|
| `integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp` | ARCHIVE_ONLY |
| `integrations/c4d/RelativityRenderBridge/rrscene_writer.py` | ARCHIVE_ONLY |
| `integrations/c4d/RelativityRenderBridge/server_client.py` | ARCHIVE_ONLY |
| `integrations/c4d/RelativityRenderBridge/preview_state.py` | ARCHIVE_ONLY |
| `integrations/c4d/RelativityRenderBridge/image_io.py` | ARCHIVE_ONLY |
| `integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py` | ARCHIVE_ONLY |
| `integrations/c4d/RelativityRenderBridge/tests/test_image_io.py` | ARCHIVE_ONLY |
| `integrations/c4d/RelativityRenderBridge/tests/test_server_client.py` | ARCHIVE_ONLY |
| `integrations/c4d/RelativityRenderBridge/tests/test_preview_state.py` | ARCHIVE_ONLY |
| `integrations/c4d/RelativityRenderBridge/README.md` | ARCHIVE_ONLY |
| `integrations/c4d/README.md` | KEEP_WITH_REFACTOR |
| `integrations/README.md` | KEEP_WITH_REFACTOR |

### 1.9 server

| File | Decision |
|---|---|
| `src/server/RenderServer.{h,cpp}` | KEEP_WITH_REFACTOR |
| `src/server/README.md` | KEEP_AS_IS |

### 1.10 tests

| File | Decision |
|---|---|
| `tests/math_tests.cpp` | KEEP_AS_IS |
| `tests/relativity_tests.cpp` | KEEP_AS_IS |
| `tests/image_tests.cpp` | KEEP_AS_IS |
| `tests/gpu_tests.cpp` | KEEP_AS_IS |
| `tests/camera_tests.cpp` | KEEP_AS_IS |
| `tests/geometry_tests.cpp` | KEEP_AS_IS |
| `tests/scene_tests.cpp` | KEEP_AS_IS |
| `tests/mesh_tests.cpp` | KEEP_AS_IS |
| `tests/material_tests.cpp` | KEEP_AS_IS |
| `tests/lighting_tests.cpp` | KEEP_AS_IS |
| `tests/io_tests.cpp` | KEEP_WITH_REFACTOR |
| `tests/sampling_tests.cpp` | KEEP_AS_IS |
| `tests/texture_tests.cpp` | KEEP_AS_IS |
| `tests/aov_tests.cpp` | KEEP_AS_IS |
| `tests/server_tests.cpp` | KEEP_WITH_REFACTOR |
| `tests/material_graph_tests.cpp` | REWRITE |
| `tests/material_graph_core_tests.cpp` | KEEP_AS_IS |
| `tests/README.md` | KEEP_AS_IS |

### 1.11 scenes / docs / build / repo

| File | Decision |
|---|---|
| `scenes/test_minimal.rrscene`, `test.rrscene`, `test_geometry.rrscene` | KEEP_AS_IS |
| `docs/MASTER_ARCHITECTURE.md` | KEEP_AS_IS |
| `docs/MODULE_MAP.md` | KEEP_AS_IS |
| `docs/MILESTONE_ROADMAP.md` | KEEP_AS_IS |
| `docs/DEVELOPMENT_RULES.md` | KEEP_AS_IS |
| `docs/RRSCENE_FORMAT.md` | KEEP_AS_IS |
| `docs/OPTIX_BACKEND_PLAN.md` | KEEP_AS_IS |
| `docs/MATERIAL_GRAPH_SPEC.md` | KEEP_AS_IS |
| `docs/DENOISING_PLAN.md` | KEEP_AS_IS |
| `docs/C4D_NATIVE_RENDERER_PLAN.md` | KEEP_AS_IS |
| `docs/BUILD_PLAN.md` | ARCHIVE_ONLY |
| `docs/PROTOTYPE_FILE_INDEX.md`, `PROTOTYPE_CLASSIFICATION.md`, `GPU_RENDER_AUDIT.md`, `GPU_MEMORY_AUDIT.md`, `ARCHITECTURE_AUDIT.md`, `BUILD_SYSTEM_AUDIT.md`, `PREMATURE_SYSTEMS_AUDIT.md`, `API_AUDIT.md` | KEEP_AS_IS |
| `CMakeLists.txt` | KEEP_WITH_REFACTOR |
| `README.md` | KEEP_WITH_REFACTOR |
| `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` | KEEP_AS_IS |
| `src/README.md` | KEEP_AS_IS |
| `tools/README.md`, `third_party/README.md` | KEEP_AS_IS |

### 1.12 totals

| Class | Files |
|---|---:|
| `KEEP_AS_IS` | 116 |
| `KEEP_WITH_REFACTOR` | 25 |
| `REWRITE` | 5 |
| `ARCHIVE_ONLY` | 11 |
| `DELETE_LATER` | 1 |
| **TOTAL** | **159** (+ this doc and `REUSE_PLAN.md`) |

---

## 2. KEEP_AS_IS — what survives unchanged

These come across into the rewrite verbatim. They are the renderer's spine.

### 2.1 Math + relativity (foundation)

`Vec2/3/4`, `Mat4`, `Transform`, `MathUtils.h` (which carries `RR_HD`),
`RelativityMath.{h,cuh}`, `RelativityParams.h`. Header-only RR_HD PODs;
`MathUtils.h` defines the macro that lets host tests verify device behaviour by
construction (step 8 §4 calls this out explicitly).

### 2.2 GPU primitives

`GpuBuffer<T>` and its `gpu_alloc / gpu_free / gpu_copy_*` byte-level backend,
`GpuDevice`, `GpuMesh`, `CudaBuffer`, `CudaContext`. Step 4 found zero memory
risks; step 8 confirmed `[[nodiscard]]`, `noexcept`, move-only, RAII discipline
is at the level the rewrite needs.

### 2.3 CUDA view PODs + RR_HD primitives

`CudaScene.cuh`, `CudaKernels.cuh`, `CudaIntersection.cuh`, `CudaTexture.cuh`,
`CudaMesh.cuh`, `CudaMaterial.cuh`, `CudaMaterialGraph.cuh`, `CudaLight.cuh`,
`CudaAOV.cuh`. The view-POD pattern is the right shape and ports straight into
OptiX SBT records.

### 2.4 Image + AOV foundation

`Image.{h,cpp}`, `Color.h`, `renderer/AOV.{h,cpp}`, `renderer/Hit.h`. M17 AOV
foundation is the right shape (six-AOV pack including DopplerFactor and
SearchlightFactor); the denoising plan (M22) builds on it.

### 2.5 Pathtracer primitives

`RNG.{h,cuh}`, `Sampling.{h,cuh}`. RR_HD PCG + cosine-weighted hemisphere
sampling. Foundation.

### 2.6 Geometry + camera + texture

`Sphere.h`, `Triangle.h`, `Mesh.{h,cpp}`, `Camera.{h,cpp}`, `CameraRay.h`,
`Texture.{h,cpp}`, `ImageTexture.{h,cpp}`. Small, clean, host-side.

### 2.7 Material data core + GPU IR

`Material.{h,cpp}`, `GpuMaterial.{h,cpp}`, `material/graph/Graph.{h,cpp}`,
`Node.h`, `Socket.h`, `GraphEvaluator.{h,cpp}`. Step 7 confirmed this is the
keeper graph.

### 2.8 Scene container

`Scene.{h,cpp}`, `SceneObject.h`. Plain host container.

### 2.9 Tests for kept code

All `*_tests.cpp` whose targets stay (`math`, `relativity`, `image`, `gpu`,
`camera`, `geometry`, `scene`, `mesh`, `material`, `lighting`, `sampling`,
`texture`, `aov`, `material_graph_core`).

### 2.10 Foundational docs + scene fixtures + repo-root

`MASTER_ARCHITECTURE.md`, `MODULE_MAP.md`, `MILESTONE_ROADMAP.md`,
`DEVELOPMENT_RULES.md`, `RRSCENE_FORMAT.md`, `OPTIX_BACKEND_PLAN.md`,
`MATERIAL_GRAPH_SPEC.md`, `DENOISING_PLAN.md`, `C4D_NATIVE_RENDERER_PLAN.md`,
all step-1..8 audit docs, three `.rrscene` fixtures,
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`.

---

## 3. KEEP_WITH_REFACTOR — what carries forward with cleanup

25 files. The idea is right, the structure needs work.

### 3.1 `main.cpp` (325 lines)

Strip M14/M16/M17 demo blocks; reduce to a CLI dispatcher. Demos relocate to
`tools/` or to per-milestone test binaries (step 5 §1.4).

### 3.2 `image/Framebuffer.{h,cpp}`

Verify it is still needed alongside `Image`; consolidate or delete.

### 3.3 `gpu/GpuScene.{h,cpp}`

Eight `upload_*` paths grew incrementally. Factor shared upload logic.

### 3.4 `cuda/CudaRenderer.{h,cu}`

Static-only class (step 8 §3) with three near-duplicate `render_*` entry
points. Pick: free functions in `rr::cuda::renderer::` *or* an instance with
device + stream + persistent buffers.

### 3.5 `cuda/CudaTestKernel.cu` (917 lines)

Step 5 §1.2 + step 3: real production kernels (`k_render_scene`,
`k_path_trace`, `k_render_aovs`) live alongside diagnostic kernels (M6–M9).
**Split:** production kernels into `cuda/kernels/` (or move to
`renderer/`/`pathtracer/` to fix the empty-module problem); diagnostics into
`cuda/diagnostics/` or a separate test target.

### 3.6 `optix/OptixBackend.{h,cpp}`

Lifecycle scaffold is clean (step 3). The rewrite layers raygen / miss /
closest-hit / SBT on top per `OPTIX_BACKEND_PLAN.md`.

### 3.7 `io/SceneLoader.{h,cpp}` + `io/SceneWriter.{h,cpp}` (831-line loader)

Public surface fine. Replace hand-rolled JSON with a real library (nlohmann or
similar) — step 5 §1.3.

### 3.8 `lighting/Light.{h,cpp}`

Has `Area` / `Environment` fields flagged PLACEHOLDER. Tidy in rewrite once
sampler support lands.

### 3.9 `material/MaterialTypes.h`

`transmission` field flagged PLACEHOLDER. Tidy alongside material rewrite.

### 3.10 `server/RenderServer.{h,cpp}`

Step 7: protocol stays, implementation needs a v2 pass (multi-client, binary
streaming, EXR, cancellation, progress).

### 3.11 Tests that follow the above refactors

`io_tests.cpp` (follows SceneLoader/Writer), `server_tests.cpp` (follows server
v2). Protocol-level expectations survive.

### 3.12 Build / repo-root

`CMakeLists.txt` (565 lines, single file — step 6 says split per-module).
`README.md` (still surfaces prototype-era workflow).
`integrations/README.md` and `integrations/c4d/README.md` (update for M23
native-first path).

---

## 4. REWRITE — bin and start over

5 files. The concept may carry; the implementation does not.

| File | Why |
|---|---|
| `src/optix/OptixRenderer.h` | Header for `render_placeholder`; rewrite IS the OptiX renderer (per `OPTIX_BACKEND_PLAN.md` M15.4). |
| `src/optix/OptixRenderer.cpp` | Stub returning "not implemented"; replaced by real raygen / miss / closest-hit / SBT pipeline. |
| `src/material/MaterialGraph.h` | Older monolithic graph runtime, superseded by `material/graph/` + `material/GpuMaterial`. |
| `src/material/MaterialGraph.cpp` | Same. |
| `tests/material_graph_tests.cpp` | Tests the legacy graph that's being binned. New tests cover `material/graph/` (already KEEP_AS_IS). |

Rationale (from steps 5 + 7 + 8):

- The legacy `MaterialGraph` and the new `material::graph::` form two parallel
  implementations. The new one is the keeper; the legacy one (`GraphNode`,
  `compile_*`, `TextureSamplerFn`) is the project's biggest single piece of
  structural debt.
- `OptixRenderer::render_placeholder` is honestly named — it explicitly says
  "scaffold only" and points at M15.4. There is nothing to keep; the rewrite
  writes the real thing.

---

## 5. ARCHIVE_ONLY — preserve as reference, not in production tree

11 files, all under `integrations/c4d/RelativityRenderBridge/` plus
`docs/BUILD_PLAN.md`.

### 5.1 C4D Python bridge (M19)

Step 7 confirmed: well-built six-slice plugin, but every churn in the renderer
pulls churn through the bridge. The native C++ `VideoPostData` plugin in
`docs/C4D_NATIVE_RENDERER_PLAN.md` (M23) replaces it at renderer-replacement
priority.

| File | Reason |
|---|---|
| `RelativityRenderBridge.pyp` | Replaced by M23 native plugin; reference for translation rules. |
| `rrscene_writer.py` | Native path builds `rr::scene::Scene` in-process; no `.rrscene` round-trip. |
| `server_client.py` | Bridge-only; native is in-process. |
| `preview_state.py` | Bridge-only dialog helpers. |
| `image_io.py` | PPM→BMP for the bridge dialog; replaced by native bitmap fill. |
| `tests/test_*.py` (4 files) | Tied to archived modules. |
| `RelativityRenderBridge/README.md` | Tied to bridge's existence. |

### 5.2 Historical change-log

| File | Reason |
|---|---|
| `docs/BUILD_PLAN.md` | 7,116-line historical change-log of prototype slices. The rewrite starts a fresh log. |

The bridge tree should move to `archive/c4d-python-bridge-m19/` (or similar)
**outside** the rewrite source tree — referenced by `C4D_NATIVE_RENDERER_PLAN.md`,
not built.

---

## 6. DELETE_LATER — remove once dependents migrate

1 file.

| File | Reason |
|---|---|
| `src/scene/Transform.h` | Back-compat shim aliasing `math::Transform`. Drop once the last caller switches to `math::Transform` directly. |

This is a one-line `using` shim. Trivial to remove, but waits until the rewrite
tree compiles without it.

---

## Cross-audit consistency

Each step's findings map to the same per-file categories without conflict:

| Step | Confirms |
|---|---|
| 3 — GPU rendering | KEEP_AS_IS for the production kernels' file-of-residence (`CudaScene.cuh`, `CudaIntersection.cuh`, `CudaAOV.cuh`); KEEP_WITH_REFACTOR for `CudaTestKernel.cu` (split production from diagnostic); REWRITE for `OptixRenderer.{h,cpp}`. |
| 4 — GPU memory | KEEP_AS_IS for `GpuBuffer<T>`, `gpu_alloc/free`, RAII surface — zero risks. |
| 5 — architecture | KEEP_WITH_REFACTOR for `main.cpp`, `CudaTestKernel.cu`, `SceneLoader.cpp`, `GpuScene.cpp`; flags empty `renderer/` + `pathtracer/` modules as content-vs-name gap, not a per-file reclassification. |
| 6 — build | KEEP_WITH_REFACTOR for `CMakeLists.txt`; KEEP_AS_IS for the 17 module subdirs. |
| 7 — premature systems | ARCHIVE_ONLY for the C4D bridge; KEEP_WITH_REFACTOR for the server v1; the new material graph is the keeper. |
| 8 — naming + API | KEEP_AS_IS-supporting: naming/module/`[[nodiscard]]`/move-only/RAII discipline is consistent; `CudaRenderer` static-only shape and the empty `renderer/`+`pathtracer/` content match KEEP_WITH_REFACTOR slots. |

No file changed category between step 2 and step 9. The audit phase produced
**confirmations**, not reversals.

---

## What the rewrite inherits

- **~73% of the prototype's files KEEP_AS_IS** (116/159).
- **~16% KEEP_WITH_REFACTOR** (25/159) — concentrated in five well-known
  hotspots (main.cpp, GpuScene, CudaRenderer, CudaTestKernel, SceneLoader,
  CMakeLists), plus the v2 server pass.
- **Five files REWRITE.** Two of them (`OptixRenderer.{h,cpp}`) are placeholders
  by their own admission; three (`MaterialGraph.{h,cpp}`,
  `material_graph_tests.cpp`) are the legacy half of the duplicated graph.
- **Eleven files ARCHIVE_ONLY** (all C4D bridge, plus `BUILD_PLAN.md`).
- **One file DELETE_LATER.**

The rewrite is therefore *not* a from-scratch project. It is the prototype's
spine — math + relativity + GPU primitives + CUDA view PODs + AOV pack +
material data core + GPU IR — moved into a tighter shell that owns the
integrator, the OptiX pipeline, the scene loader, and a hardened server. The
companion `docs/REUSE_PLAN.md` carries the migration sequence.

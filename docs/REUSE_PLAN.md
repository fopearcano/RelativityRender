# Reuse Plan — relativity-core-v1

Date: 2026-04-29
Branch: `claude/create-docs-architecture-T2Dp5`
Status: **Final.** Companion to `docs/PROTOTYPE_REUSE_AUDIT.md`.

This doc says **how** the rewrite is started: which files are copied across,
which are left behind, in what order, and what the smallest tree is that can
still build, render a relativistic image, and run its tests on day 1.

The rewrite branch / repo (working name **`relativity-core-v1`**) is created
fresh from the prototype's classification table, not by branching off
`main`. The prototype repo stays as a frozen reference.

---

## 1. Migration strategy

### 1.1 What gets copied into `relativity-core-v1`

In one pass, copy verbatim:

**Math + relativity foundation** (no work needed):

```
src/math/                                  # 7 files
src/relativity/                            # 4 files
```

**GPU + CUDA primitives** (no work needed):

```
src/gpu/GpuBuffer.{h,cpp}
src/gpu/GpuDevice.{h,cpp}
src/gpu/GpuMesh.{h,cpp}
src/gpu/README.md
src/cuda/CudaBuffer.{h,cpp}
src/cuda/CudaContext.{h,cpp}
src/cuda/CudaScene.cuh
src/cuda/CudaKernels.cuh
src/cuda/CudaIntersection.cuh
src/cuda/CudaTexture.cuh
src/cuda/CudaMesh.cuh
src/cuda/CudaMaterial.cuh
src/cuda/CudaMaterialGraph.cuh
src/cuda/CudaLight.cuh
src/cuda/CudaAOV.cuh
src/cuda/README.md
```

**Image + AOV foundation** (no work needed):

```
src/image/Image.{h,cpp}
src/image/Color.h
src/image/README.md
src/renderer/AOV.{h,cpp}
src/renderer/Hit.h
src/renderer/README.md
```

**Pathtracer primitives** (no work needed):

```
src/pathtracer/RNG.{h,cuh}
src/pathtracer/Sampling.{h,cuh}
src/pathtracer/README.md
```

**Geometry + camera + texture + scene container** (no work needed):

```
src/geometry/                              # 5 files (Sphere/Triangle/Mesh + README)
src/camera/                                # 4 files
src/texture/                               # 5 files
src/scene/Scene.{h,cpp}
src/scene/SceneObject.h
src/scene/README.md
```

**Material — keep new, drop legacy** (no work needed for kept files):

```
src/material/Material.{h,cpp}
src/material/GpuMaterial.{h,cpp}
src/material/graph/                        # 6 files
src/material/README.md
```
**Do not** copy `src/material/MaterialGraph.{h,cpp}` (REWRITE) or
`tests/material_graph_tests.cpp` (REWRITE).

**Tests for kept code** (no work needed):

```
tests/math_tests.cpp
tests/relativity_tests.cpp
tests/image_tests.cpp
tests/gpu_tests.cpp
tests/camera_tests.cpp
tests/geometry_tests.cpp
tests/scene_tests.cpp
tests/mesh_tests.cpp
tests/material_tests.cpp
tests/lighting_tests.cpp
tests/sampling_tests.cpp
tests/texture_tests.cpp
tests/aov_tests.cpp
tests/material_graph_core_tests.cpp
tests/README.md
```

**Foundational docs + scene fixtures + repo-root** (no work needed):

```
docs/MASTER_ARCHITECTURE.md
docs/MODULE_MAP.md
docs/MILESTONE_ROADMAP.md
docs/DEVELOPMENT_RULES.md
docs/RRSCENE_FORMAT.md
docs/OPTIX_BACKEND_PLAN.md
docs/MATERIAL_GRAPH_SPEC.md
docs/DENOISING_PLAN.md
docs/C4D_NATIVE_RENDERER_PLAN.md
docs/PROTOTYPE_FILE_INDEX.md
docs/PROTOTYPE_CLASSIFICATION.md
docs/GPU_RENDER_AUDIT.md
docs/GPU_MEMORY_AUDIT.md
docs/ARCHITECTURE_AUDIT.md
docs/BUILD_SYSTEM_AUDIT.md
docs/PREMATURE_SYSTEMS_AUDIT.md
docs/API_AUDIT.md
docs/PROTOTYPE_REUSE_AUDIT.md
docs/REUSE_PLAN.md
scenes/test_minimal.rrscene
scenes/test.rrscene
scenes/test_geometry.rrscene
RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt
src/README.md
tools/README.md
third_party/README.md
```

Files copied **with refactor** (in their own dedicated rewrite slices, not
verbatim):

- `src/main.cpp` — strip M14/M16/M17 demos; reduce to argv dispatcher.
- `src/image/Framebuffer.{h,cpp}` — verify need; consolidate or drop.
- `src/gpu/GpuScene.{h,cpp}` — factor shared upload logic.
- `src/cuda/CudaRenderer.{h,cu}` — pick free-function or stateful instance shape.
- `src/cuda/CudaTestKernel.cu` — split production kernels (move into
  `renderer/`/`pathtracer/`) from diagnostic kernels.
- `src/optix/OptixBackend.{h,cpp}` — keep lifecycle; layer the real pipeline
  on top per `OPTIX_BACKEND_PLAN.md`.
- `src/io/SceneLoader.{h,cpp}` + `src/io/SceneWriter.{h,cpp}` — replace
  hand-rolled JSON with a real lib (nlohmann etc.).
- `src/lighting/Light.{h,cpp}` — clean up `Area`/`Environment` placeholders
  when sampler support lands.
- `src/material/MaterialTypes.h` — clean up `transmission` placeholder.
- `src/server/RenderServer.{h,cpp}` — server v2 (multi-client, binary AOV,
  EXR, cancellation, progress).
- `tests/io_tests.cpp` — follows SceneLoader/Writer.
- `tests/server_tests.cpp` — follows server v2.
- `CMakeLists.txt` — split into per-module CMakes, helper functions for
  warning/test boilerplate, promote `core/` to a library, add Hopper +
  Blackwell archs.
- `README.md`, `integrations/README.md`, `integrations/c4d/README.md` —
  reflect the rewrite's path.

`src/scene/Transform.h` (DELETE_LATER) is **not** copied; the rewrite uses
`math::Transform` from day one.

### 1.2 What stays behind in the prototype repo

- The C4D Python bridge (`integrations/c4d/RelativityRenderBridge/`, 11 files)
  — referenced by `docs/C4D_NATIVE_RENDERER_PLAN.md` for translation rules.
- `docs/BUILD_PLAN.md` (7,116-line slice-by-slice change log).
- Demo blocks of `main.cpp` (M14/M16/M17 deliverable runs).
- The legacy `src/material/MaterialGraph.{h,cpp}` and
  `tests/material_graph_tests.cpp`.
- The `OptixRenderer::render_placeholder` stub.
- `src/scene/Transform.h` shim.
- `src/cuda/CudaTestKernel.cu`'s diagnostic kernels (M6–M9 visualisations).
  Production kernels move in their own rewrite slice; diagnostics either
  port to a separate test-only target or stay behind.

### 1.3 Prototype repo state after migration

- Frozen branch: `prototype/v0-frozen-2026-04-29` (or similar) tagged at the
  current head of `claude/create-docs-architecture-T2Dp5` after step 9 lands.
- No further development on the prototype repo. All audit docs survive in both
  repos.

### 1.4 Order of operations

1. Land step 9 (this commit) on `claude/create-docs-architecture-T2Dp5`.
2. Tag the prototype: `git tag prototype/v0-frozen-2026-04-29`.
3. Create `relativity-core-v1` repo / branch fresh.
4. Copy `KEEP_AS_IS` files in one commit, preserving `src/` layout. This is
   the **minimum safe starting point** in §2.
5. Add a fresh `CMakeLists.txt` (per `BUILD_SYSTEM_AUDIT.md` §recommendations)
   that compiles only the kept libs and runs the kept tests.
6. Verify the kept tests still pass on the new tree before any further work.
7. Begin rewrite slices, one `KEEP_WITH_REFACTOR` / `REWRITE` item per slice,
   in the dependency order in §3.

---

## 2. Minimum safe starting point for `relativity-core-v1`

The smallest tree that compiles, links, and produces a useful artefact (a
relativistic-shaded sphere PNG/PPM via the existing CUDA path) on day 1 is:

### 2.1 Source

```
src/
  math/                # Vec*, Mat4, Transform, MathUtils, README
  relativity/          # RelativityMath.{h,cuh}, RelativityParams, README
  image/               # Image.{h,cpp}, Color.h, README
  geometry/            # Sphere, Triangle, Mesh, README
  camera/              # Camera, CameraRay, README
  texture/             # Texture, ImageTexture, README
  pathtracer/          # RNG, Sampling, README
  renderer/            # AOV.{h,cpp}, Hit.h, README
  scene/               # Scene.{h,cpp}, SceneObject.h, README
  material/
    Material.{h,cpp}
    GpuMaterial.{h,cpp}
    graph/             # Graph, Node, Socket, GraphEvaluator
    README.md
  gpu/
    GpuBuffer.{h,cpp}
    GpuDevice.{h,cpp}
    GpuMesh.{h,cpp}
    README.md
  cuda/                # CudaBuffer, CudaContext, all CudaXxx.cuh views, README
  README.md
```

That is the entire `KEEP_AS_IS` set, 116 files.

**Not in the day-1 tree:**

- `src/main.cpp` — written fresh as a tiny argv dispatcher in slice 1 of the
  rewrite (or the day-1 PR).
- `src/cuda/CudaRenderer.{h,cu}`, `src/cuda/CudaTestKernel.cu` — first
  KEEP_WITH_REFACTOR slice; until then there is no executable that produces
  a frame.
- `src/io/SceneLoader.{h,cpp}`, `src/io/SceneWriter.{h,cpp}` — second
  KEEP_WITH_REFACTOR slice; until then scenes are constructed in-process from
  the kept fixtures.
- `src/optix/` — REWRITE/REFACTOR slice; nothing OptiX on day 1.
- `src/server/`, `src/lighting/Light.{h,cpp}`, `src/gpu/GpuScene.{h,cpp}`,
  `src/material/MaterialTypes.h` — later slices.

The day-1 tree therefore **does not yet render**. To render on day 1 you also
need the first slice of the integrator extraction:

### 2.2 Day-1 "render slice" (first KEEP_WITH_REFACTOR work, optional)

If the rewrite wants a renderable artefact on the very first day, do this
slice immediately after the day-1 copy:

1. Bring across `cuda/CudaRenderer.{h,cu}` as-is.
2. Bring across `cuda/CudaTestKernel.cu` minus the diagnostic kernels (M6–M9).
   Production kernels (`k_render_scene`, `k_path_trace`, `k_render_aovs`)
   stay; diagnostic kernels do not move.
3. Bring across `gpu/GpuScene.{h,cpp}` as-is (refactor of the eight
   `upload_*` paths is a later slice).
4. Write a 30-line `src/main.cpp` that constructs a hardcoded scene
   in-process (one sphere, default camera, default Observer at β=0.5),
   uploads it via `GpuScene`, calls `CudaRenderer::render_scene(...)`, saves
   the PPM.

That is the smallest renderable rewrite. After it lands, the kept tests still
pass and there is one end-to-end command (`relativityrender --render`) that
produces a relativistic-shaded image.

### 2.3 Build

Fresh `CMakeLists.txt` per `BUILD_SYSTEM_AUDIT.md` §recommendations:

- Per-module subdirectories with their own `CMakeLists.txt` (avoid the
  prototype's 565-line single file).
- Helper function `rr_add_library(name SRCS …)` that applies the standard
  warning flags + C++ standard centrally.
- Helper function `rr_add_test(name SRCS …)` that wires GoogleTest +
  `add_test()` in one line.
- Promote `core/` to a real library (the prototype had `core/` as
  header-only by accident).
- `CMAKE_BUILD_TYPE` defaulted to `RelWithDebInfo` if unset (kernel work
  needs `-O2` + debug symbols).
- CUDA archs include Hopper (90) and Blackwell (100/120) alongside Ada/Ampere.
- `RR_HAS_CUDA` / `RR_HAS_OPTIX` capability macros propagated as today.

### 2.4 Tests

The day-1 ctest suite:

- `math_tests`, `relativity_tests`, `image_tests`, `gpu_tests`,
  `camera_tests`, `geometry_tests`, `scene_tests`, `mesh_tests`,
  `material_tests`, `lighting_tests`, `sampling_tests`, `texture_tests`,
  `aov_tests`, `material_graph_core_tests`.

All host tests, all RR_HD-verified-on-host. Kernel-launching tests (`gpu_*`)
gate on `RR_HAS_CUDA`. The 354/354 `material_graph_core_tests` from the
prototype's M21 slice continue to pass.

What is **not** in the day-1 ctest suite:

- `io_tests` (waits on SceneLoader/Writer rewrite).
- `server_tests` (waits on server v2).
- `material_graph_tests` (legacy; deleted).
- Any future OptiX tests.

---

## 3. Rewrite slice order

Once the day-1 tree is in place and the kept tests pass, work the
KEEP_WITH_REFACTOR / REWRITE items in dependency order:

| # | Slice | Touches | Unblocks |
|--:|---|---|---|
| 1 | Production-kernel extraction | split `CudaTestKernel.cu`; move `k_render_scene` / `k_path_trace` / `k_render_aovs` into `renderer/`+`pathtracer/`; `CudaRenderer` reshape | end-to-end render via clean module boundaries |
| 2 | `main.cpp` slim-down | strip demos, become argv dispatcher | clean CLI |
| 3 | Scene loader rewrite | `io/SceneLoader.{h,cpp}` + `io/SceneWriter.{h,cpp}` on a real JSON lib | richer scenes, `io_tests` re-enabled |
| 4 | `GpuScene` factoring | shared upload helper | smaller, more maintainable upload surface |
| 5 | OptiX renderer (M15.4) | `optix/OptixRenderer.{h,cpp}` rewritten on top of kept `OptixBackend` per `OPTIX_BACKEND_PLAN.md` | OptiX path; basis for denoiser slice |
| 6 | Lighting + material cleanup | `lighting/Light.{h,cpp}` and `material/MaterialTypes.h` placeholders | proper area + environment lights, transmission semantics |
| 7 | Server v2 | `server/RenderServer.{h,cpp}` hardening | streaming AOVs, cancellation, progress |
| 8 | Build system | per-module CMakes, helper functions | smaller, faster, easier to evolve build |
| 9 | OptiX denoiser (M22) | uses M22 plan + kept AOV pack + OptiX renderer slice | shippable noise-free output |
| 10 | C4D native plugin (M23) | uses kept scene + material + server | replaces the archived Python bridge |
| 11 | DELETE_LATER cleanup | `scene/Transform.h` removed once nothing references it | smaller surface |

Each slice is its own PR. None of them require revisiting the day-1 KEEP_AS_IS
files.

---

## 4. What this plan deliberately does *not* do

- **No archive moves inside the rewrite repo.** The C4D bridge stays in the
  prototype repo; it is *not* copied into `relativity-core-v1` even under an
  `archive/` directory. Linking to it from the rewrite docs is enough.
- **No "future-proofing" abstractions.** The day-1 tree mirrors the prototype's
  module names exactly; new abstractions only appear when a slice needs them.
- **No UI work.** Per step 7, no `src/ui/` is created until denoising and
  stable AOVs land (after slice 9).
- **No new file-naming or coding conventions.** Step 8 confirmed the existing
  conventions are clean across 159 files; the rewrite adopts them verbatim.
- **No BUILD_PLAN successor.** The rewrite tracks slices in
  `MILESTONE_ROADMAP.md` and PR descriptions, not in a single 7,000-line log.

---

## 5. Acceptance criteria

The rewrite has successfully started when:

1. The 116 KEEP_AS_IS files exist in `relativity-core-v1` byte-identical to
   the prototype (modulo include-path adjustments if the layout shifts).
2. The 14 day-1 ctest binaries build and pass on the new tree.
3. A fresh `CMakeLists.txt` follows the per-module-CMake pattern from
   `BUILD_SYSTEM_AUDIT.md`.
4. No file from `ARCHIVE_ONLY` or `DELETE_LATER` is present.
5. `docs/PROTOTYPE_REUSE_AUDIT.md` and `docs/REUSE_PLAN.md` are copied across
   so the rewrite repo carries its own provenance.
6. The prototype repo is tagged frozen and no further commits land on it.

After (1)–(6), slice 1 (production-kernel extraction) starts.

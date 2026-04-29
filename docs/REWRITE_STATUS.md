# Rewrite Status — relativity-core-v1

Date: 2026-04-29
Branch: `relativity-core-v1` (current)
Frozen reference: `prototype_v0` tag (was branch `claude/create-docs-architecture-T2Dp5`).

This branch is a clean restart. It is seeded **selectively** from the
prototype, not by branching off it. The selection rules and the
classification table are in `docs/PROTOTYPE_REUSE_AUDIT.md` and
`docs/REUSE_PLAN.md`. Day-1 scope: **GPU core foundation only.**

This document is the live status of the rewrite — what has been
reused, what has been rewritten, what has been discarded.

---

## Day-1 acceptance

The branch builds and runs with:

- **CUDA detection** — `RelativityRender --detect` enumerates visible
  CUDA devices when CUDA is compiled in, or honestly reports
  "no backend" otherwise.
- **GPU gradient render** — `RelativityRender --render-gradient WxH OUT.ppm`
  runs a single GPU kernel and writes a UV-gradient PPM. Every pixel
  is computed on the device; the host only allocates, launches, and
  downloads.

That is the entire intended capability of day-1. Anything else is
deferred to a dedicated slice below.

### Build modes

| Mode          | CMake invocation                                           | What works |
|---------------|------------------------------------------------------------|-----------|
| Host-only     | `cmake -S . -B build`                                       | Builds, runs `--detect` and reports "(none)". `--render-gradient` reports "rebuild with -DRR_ENABLE_CUDA=ON". |
| CUDA-enabled  | `cmake -S . -B build -DRR_ENABLE_CUDA=ON` (needs CUDA Toolkit + GPU host) | Builds, runs `--detect` and lists devices, runs `--render-gradient` end-to-end. |

---

## Reused components (copied from `prototype_v0`)

These came across **verbatim** because the prototype audit (steps 3,
4, 8) found them clean and the rewrite needs them now. Nothing
copied here was modified.

| File / path                          | Purpose                                                       |
|--------------------------------------|---------------------------------------------------------------|
| `src/math/Vec2.h`                    | RR_HD POD, foundation for all vector math                     |
| `src/math/Vec3.h`                    | Same                                                          |
| `src/math/Vec4.h`                    | Same                                                          |
| `src/math/Mat4.h`                    | RR_HD POD, foundation for transforms                          |
| `src/math/Transform.h`               | Canonical transform struct                                    |
| `src/math/MathUtils.h`               | The `RR_HD` macro and small helpers                           |
| `src/core/Logger.{h,cpp}`            | Minimal stdio logger                                          |
| `src/core/Version.h`                 | Project version constants                                     |
| `src/image/Image.{h,cpp}`            | Host-side float framebuffer + PPM writer                      |
| `src/image/Color.h`                  | Rgb / Rgba PODs                                               |
| `src/gpu/GpuBuffer.{h,cpp}`          | Move-only RAII GPU memory wrapper                             |
| `src/gpu/GpuDevice.{h,cpp}`          | Backend-agnostic device description + enumeration             |
| `src/cuda/CudaContext.{h,cpp}`       | CUDA device enumeration via cudaGetDeviceProperties           |
| `src/cuda/CudaBuffer.{h,cpp}`        | Byte-level CUDA alloc / free / copy backing `GpuBuffer<T>`    |
| `src/math/README.md`, `src/core/README.md`, `src/image/README.md`, `src/gpu/README.md`, `src/cuda/README.md` | Module-level docs |

---

## Rewritten components (trimmed or replaced versions)

These existed in the prototype but the rewrite needed a smaller or
cleaner version. The rewrite carries the **new** version; the
prototype version stays on the `prototype_v0` tag.

| File / path                          | What changed                                                  |
|--------------------------------------|---------------------------------------------------------------|
| `src/cuda/CudaRenderer.{h,cu}`       | Was 348 lines covering 7 render entry points. Trimmed to a single `render_gradient` static method. |
| `src/cuda/CudaGradientKernel.cu`     | Replaces `cuda/CudaTestKernel.cu` (917 lines, 7 kernels). Contains only `k_gradient_rgba32f` + `launch_gradient_rgba32f`. |
| `src/main.cpp`                       | Was 325 lines of M14/M16/M17 demo blocks. Replaced with a 130-line CLI dispatcher: `--detect` and `--render-gradient`. |
| `CMakeLists.txt`                     | Was 565 lines, single file accumulated per-module. Replaced with ~120 lines: `rr_apply_warnings()` and `rr_add_test()` helpers, three module libs, one executable, three test binaries. |
| `tests/image_tests.cpp`              | Trimmed to drop `Framebuffer` references (Framebuffer not ported day-1). |
| `tests/gpu_tests.cpp`                | Trimmed from a multi-module test (camera + geometry + scene + lighting + material + GpuMesh + GpuScene) to test only `GpuBuffer<T>` + `GpuDevice`. |

---

## Discarded components (not in the rewrite tree)

Everything below is preserved on `prototype_v0` and is not in the
day-1 rewrite. Some will be reintroduced — see "Reintroduction
roadmap" below.

### Discarded permanently

| Path                                          | Reason                                                       |
|-----------------------------------------------|--------------------------------------------------------------|
| `integrations/c4d/RelativityRenderBridge/`    | C4D Python bridge (M19). Replaced by a future native plugin slice. Reference-only on `prototype_v0`. |
| `src/material/MaterialGraph.{h,cpp}`          | Legacy duplicate of `material/graph/`. Already classified REWRITE in step 2. |
| `tests/material_graph_tests.cpp`              | Tests the legacy graph being binned.                          |
| `src/optix/OptixRenderer.{h,cpp}`             | `render_placeholder` stub returning "not implemented".        |
| `src/scene/Transform.h`                       | Back-compat shim aliasing `math::Transform`. Rewrite uses `math::Transform` directly. |
| `docs/BUILD_PLAN.md`                          | 7,116-line slice-by-slice change log of the prototype.        |
| `src/cuda/CudaTestKernel.cu`                  | 917 lines mixing 7 kernels (M5–M17). Production kernels come back per-slice in their own files. |

### Discarded for now (will return in a later slice)

| Path                              | Returns in slice                                       |
|-----------------------------------|--------------------------------------------------------|
| `src/camera/`                     | Ray-gen slice (after gradient render)                   |
| `src/geometry/`                   | Intersection / first scene render slice                |
| `src/scene/Scene.{h,cpp}`         | Same                                                   |
| `src/material/graph/` + `GpuMaterial`, `Material`, `MaterialTypes.h` | Shading slice |
| `src/lighting/Light.{h,cpp}`      | Direct-lighting slice                                  |
| `src/relativity/RelativityMath.{h,cuh}`, `RelativityParams.h` | Relativistic kernel slice |
| `src/texture/`                    | Texture-sampling slice                                 |
| `src/pathtracer/RNG.{h,cuh}`, `Sampling.{h,cuh}` | Path tracer slice                       |
| `src/renderer/AOV.{h,cpp}`, `Hit.h` | AOV / integrator extraction slice                     |
| `src/io/SceneLoader.{h,cpp}`, `SceneWriter.{h,cpp}` | Scene-loader-on-real-JSON-lib slice |
| `src/server/RenderServer.{h,cpp}` | Server v2 slice (multi-client, binary AOV, EXR, cancellation, progress) |
| `src/optix/OptixBackend.{h,cpp}`  | OptiX-renderer slice (the lifecycle scaffold gets a real pipeline on top) |
| `src/gpu/GpuMesh.{h,cpp}`, `GpuScene.{h,cpp}` | Scene-upload slice (with the factoring step 5 §1.5 calls for) |
| `src/image/Framebuffer.{h,cpp}`   | Only if a real consumer appears; otherwise dropped     |
| All other prototype tests         | Per-slice, alongside the code they test                |
| Feature plan docs (`OPTIX_BACKEND_PLAN.md`, `MATERIAL_GRAPH_SPEC.md`, `DENOISING_PLAN.md`, `C4D_NATIVE_RENDERER_PLAN.md`, `RRSCENE_FORMAT.md`) | When their slices start; preserved on `prototype_v0` until then. |
| Audit step 1–8 docs                | Not copied; superseded by `PROTOTYPE_REUSE_AUDIT.md` and `REUSE_PLAN.md` which carry the consolidated decisions. |

---

## Reintroduction roadmap

Slices land one at a time. Each slice is its own PR; nothing in the
day-1 KEEP_AS_IS set is touched by these slices.

| #  | Slice                                | Brings back                                       |
|----|--------------------------------------|---------------------------------------------------|
| 1  | Camera + ray-gen kernel              | `src/camera/`, ray-gen kernel, `--render-rays`     |
| 2  | First sphere scene                   | `src/geometry/Sphere.h`, scene render kernel       |
| 3  | Scene container + multi-sphere       | `src/scene/`, `src/gpu/GpuScene.{h,cpp}`           |
| 4  | Texture sampling                     | `src/texture/`                                    |
| 5  | Material data core + GPU IR          | `src/material/graph/`, `GpuMaterial`              |
| 6  | Lighting + sampling primitives       | `src/lighting/`, `src/pathtracer/`                |
| 7  | Path tracer + AOV pack               | `src/renderer/AOV.{h,cpp}`, integrator in `pathtracer/` |
| 8  | Relativistic kernel                  | `src/relativity/`, relativistic perception in path tracer |
| 9  | Scene loader (real JSON lib)         | `src/io/SceneLoader.{h,cpp}`                      |
| 10 | OptiX renderer                       | `src/optix/`, real raygen / miss / closest-hit     |
| 11 | Denoiser                             | OptiX denoiser                                    |
| 12 | Server v2                            | `src/server/`, multi-client, binary AOV, EXR, cancellation, progress |
| 13 | Native C4D plugin                    | `integrations/c4d/`                               |

This list expands `docs/REUSE_PLAN.md` §3 with the slice numbering
the rewrite branch will actually use.

---

## Constraints carried forward

- **GPU-only rendering.** No CPU per-pixel or per-ray work. Verified
  by audit step 3.
- **RR_HD shared host+device pattern.** Host tests verify device math
  by construction. Verified by audit step 8.
- **Module = library = namespace one-to-one.** `foo/` ↔ `rr_foo` ↔
  `rr::foo`. Verified by audit step 8.
- **Capability macros, not feature flags.** `RR_HAS_CUDA` propagated
  PUBLIC from `rr_gpu` so call sites gate on the same macro.

---

## What is NOT in this rewrite (and shouldn't be added without a slice)

- C4D plugin (any form)
- Render server
- Scene parser
- Path tracer
- Material graph
- OptiX renderer
- Denoiser
- Anything UI-shaped

Adding any of the above is a dedicated slice and gets its own PR.

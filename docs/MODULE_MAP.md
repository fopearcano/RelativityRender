# Module Map

Per-module ownership, source location, and **honest** implementation
status for the 22 architectural modules defined in
`docs/MASTER_ARCHITECTURE.md` §4.

This file is referenced as authoritative by `MASTER_ARCHITECTURE.md`
§4 / §5 / §10, `MILESTONE_ROADMAP.md` M0, `DEVELOPMENT_RULES.md`
§8 / §B.1 / §C.5, and the README's documentation index. Per-slice
implementation history remains in `docs/BUILD_PLAN.md`; this file
records the per-module rollup at the most recent audit point.

Last verified: 2026-05-02 (post-Stage 19D + roadmap-consistency
audit + RR_ENABLE_OPTIX flag rename).

## Status legend

The status of each module is one of the following seven tiers,
listed weakest → strongest. This legend is shared with
`docs/MILESTONE_ROADMAP.md` "Maturity semantics" so the same
vocabulary scores both architectural modules and milestones.
The line between **foundation landed** and **production
ready** is the most important — it prevents the docs from
claiming a system works when only its data PODs compile.

- **not started** — no source files in the module's directory,
  no design doc.
- **spec only** — design doc(s) exist (e.g. under `docs/`) but
  no source code yet.
- **foundation landed** — host-side data PODs / scaffold types
  / enums compile and have unit-test coverage where applicable,
  but the system has **no real runtime function** at the module's
  intended scope. Examples: a `Light` POD union exists but no
  shadow rays / NEE; a `Texture` POD exists but no GPU sampling;
  a `Material` POD exists but the only shading on the device is
  a facing-ratio fallback. These modules ship working *code*,
  but not the *behaviour* the architectural module name implies.
- **partial implementation** — at least one production-style
  runtime path is in place, but key features are missing, OR
  the GPU / SDK path that the module depends on is unverified
  on real hardware. Examples: the path tracer integrates one
  diffuse bounce per sample but has no NEE / MIS / Russian
  roulette; the OptiX backend's pipeline + GAS + closest-hit
  programs link cleanly but no frame has been rendered on a
  real OptiX-SDK host.
- **in progress** — most of the module's planned features are
  coded, but cross-cutting work (validation, error handling,
  integration smoke tests) is not done. The promotion line
  out of "partial implementation" is that no major *feature*
  is missing — what remains is hardening / coverage.
- **landed** — the module ships its declared scope end-to-end
  on the supported test matrix; user-facing behaviour works.
  The system has not yet been hardened with regression
  baselines, edge-case coverage, or stress tests.
- **production ready** — same as landed, plus regression
  baselines pinned, edge cases covered, and **no documented
  "deferred" gate exists for the module's core runtime
  behaviour.**

A module sitting at "foundation landed" is the most easily
overstated status: the data types compile, the unit tests pass,
and the `#include` path looks complete — but the architectural
*system* the module is named after does not yet do its job.
The promotion criterion to "partial implementation" is that at
least one real runtime feature is wired end-to-end (even if
many features are still missing).

## Project-wide gate (applied to every GPU / OptiX / denoiser status below)

Per `README.md` ("This project is not production-ready. No
frame has been end-to-end visually validated on a real CUDA +
OptiX-SDK host in this branch") and
`docs/STAGE_19_DENOISER_AUDIT.md` (Q1 PARTIAL, Q2 DEFERRED),
no GPU-side rendering subsystem can sit at "production ready"
in this branch. The audit-host build (no CUDA, no OptiX SDK)
verifies code structure + fallback semantics; runtime GPU
output is unexercised. Cap for any module whose user-facing
behaviour is GPU-side: **partial implementation**, until a
CUDA + OptiX-SDK host run pins regression baselines.

## Module status

Module numbers match `docs/MASTER_ARCHITECTURE.md` §4. Source
location is the *actual* directory in the tree (the
architectural-name → directory-name mapping is documented in
`docs/MASTER_ARCHITECTURE.md` §8 naming notes).

| #  | Module                              | Source                                   | Status                  | Notes                                                                                                |
|----|-------------------------------------|------------------------------------------|-------------------------|------------------------------------------------------------------------------------------------------|
| 1  | Core Engine                         | `src/core/`                              | production ready        | Logger / Config / CommandLine / Version. 100% host-side; ctest covers the parser + validator.        |
| 2  | Math Library                        | `src/math/`                              | production ready        | Vec2/3/4 + Mat4 + Transform + MathUtils + RR_HD; 60 ctest assertions in `tests/math_tests.cpp`.      |
| 3  | Image / Framebuffer System          | `src/image/`                             | foundation landed       | Rgb32F / Rgba32F + 8-bit PPM IO + Framebuffer wrapper; **no EXR / OpenImageIO**, no DPX, no MIP.     |
| 4  | GPU Device Layer                    | `src/gpu/`                               | partial implementation  | GpuDevice + GpuBuffer + GpuMesh + GpuScene + GpuTexture + GpuTiming + GpuAOVBuffer + AccumulationBuffer compile; runtime GPU validation gated. |
| 5  | CUDA Backend                        | `src/cuda/`                              | partial implementation  | 6 `.cu` TUs (kernels for sphere / triangle / multi-scene / RNG / accumulation / path tracer); no end-to-end frame validated on real CUDA. |
| 6  | OptiX Backend                       | `src/optix/`                             | partial implementation  | OptixBackend + Renderer + Pipeline + Accel + Programs.cu + raygen / miss / closest-hit; **never executed on a real OptiX-SDK host** in this branch. |
| 7  | Scene Graph                         | `src/scene/`                             | foundation landed       | Scene aggregate + SceneObject + RenderSettings PODs; flat lists, no hierarchy traversal, no instancing, no transformation stack. |
| 8  | Geometry System                     | `src/geometry/`                          | foundation landed       | Sphere / Triangle / Mesh / Vertex PODs + `Mesh::local_bounds`; **no BVH**, no acceleration structure, no instancing. |
| 9  | Material / Shading System           | `src/material/`                          | foundation landed       | `MaterialParams` POD + presets (diffuse / emissive / metal); **no BSDF eval / sample / pdf** — device path uses a facing-ratio fallback (Stage 9B). |
| 10 | Texture System                      | `src/texture/`                           | foundation landed       | `ImageTexture` POD + `ImageTextureFormat` enum; nearest-neighbour sampler only; **no MIP, no UDIM, no HDR decode, no wrap modes**. |
| 11 | Lighting System                     | `src/lighting/`                          | foundation landed       | `Light` POD union; Point + Directional are real, Area + Environment are flagged PLACEHOLDER in source; **no shadow rays, no NEE**. |
| 12 | Camera System                       | `src/camera/`                            | foundation landed       | `Camera` + `GpuCamera` POD + `generate_camera_ray`; **pinhole only** — no DOF, no motion blur. |
| 13 | Relativistic Camera Model           | `src/relativity/`                        | production ready        | `aberrateDirection` / `dopplerFactor` / `searchlightFactor` + `RelativityParams` + `Observer`; RR_HD host + device callable; **800 analytic-formula assertions** in `tests/relativity_tests.cpp` (Stage 19E.1) cover identity at \|beta\|=0, longitudinal blue/red shift against `sqrt((1+b)/(1-b))`, aberration angle against `(cos(theta)-beta)/(1-beta cos(theta))`, finite + positive D for \|beta\|<1, `clampBeta` semantics, numerical stability at \|beta\|=0.99. Integrated into both CUDA + OptiX raygen / closest-hit. The project's core differentiator. |
| 14 | Path Tracer                         | `src/pathtracer/` + `src/cuda/CudaPathTracer.cu` | partial implementation  | RNG (4 ctest assertions) + cosine-hemisphere sampler + diffuse Lambert kernel + multi-bounce-via-spp host loop; **no NEE, no MIS, no Russian roulette, no specular / metallic / glass BSDF branches**. |
| 15 | Progressive Renderer                | `src/renderer/AccumulationBuffer.{h,cpp}` + `src/cuda/CudaAccumulation.cu` | partial implementation  | float4 fast-path + spp weighting + `cudaMemcpy(D2D)` first-sample optimisation; tied to the GPU gate above. |
| 16 | Denoiser Integration                | `src/optix/OptixDenoiser.{h,cpp}`         | partial implementation  | OptiX HDR + 3-guide-layer wrapper + `--denoise` CLI flag + Stage 19C.3 noisy-Beauty fallback; **success path never executed** (no OptiX SDK on audit host); STAGE_19_DENOISER_AUDIT.md Q1 PARTIAL / Q2 DEFERRED. |
| 17 | Render Passes / AOVs                | `src/renderer/AOV.{h,cpp}` + `src/renderer/GpuAOVBuffer.{h,cpp}` + AOV writes in `src/cuda/CudaRenderer.cu` | partial implementation  | 6 AOV types (Beauty / Normal / Depth / Albedo / DopplerFactor / SearchlightFactor) + per-pass GpuBuffer owner + kernel writes + `--render-aovs` CLI; pixel writes unverified on real GPU. |
| 18 | Scene File Format                   | `src/io/SceneLoader.{h,cpp}` + `src/io/SceneWriter.{h,cpp}` | partial implementation  | `.rrscene` v1 **loader** is feature-complete (camera / settings / observer / relativity / spheres / meshes / materials / lights / textures); **writer is a stub** that returns "not yet implemented"; no `tests/io_tests.cpp`. |
| 19 | Renderer Server                     | `src/server/`                            | partial implementation  | `RenderServer` (TCP localhost:7777) + verbs `ping` / `load_scene` / `set_beta` / `render` / `shutdown`; per `STAGE_15_SERVER_DEFERRED.md` runtime test deferred to a CUDA host. |
| 20 | Cinema 4D Bridge                    | (planned `bridges/c4d_bridge/`)           | not started             | Directory does not exist. README "Spec / planned systems" lists this as not started (master order #21). |
| 21 | Future Native Cinema 4D Renderer    | (planned `bridges/c4d_native/`)           | not started             | Directory does not exist (master order #25). |
| 22 | Node Editor / Material Graph        | (planned `tools/node_editor/`)            | not started             | Directory does not exist. README lists this as not started (master order #23). |

### Cross-cutting items the master order also tracks

The 22-module list above is the *architectural* module set. The
project's 25-step master order in
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` adds a few
items that aren't standalone architectural modules:

| Master order # | Item                                | Source / planned location                | Status                  |
|:--------------:|-------------------------------------|------------------------------------------|-------------------------|
| #22            | Preview UI                          | (planned `tools/preview_ui/`)             | not started             |
| #24            | Denoising (= module 16 above)       | `src/optix/OptixDenoiser.{h,cpp}`         | partial implementation  |

## Status rollup

- **production ready**: 3 modules (#1 Core Engine, #2 Math
  Library, #13 Relativistic Camera Model — the project's
  differentiator).
- **partial implementation**: 9 modules / items (#4 GPU Device
  Layer, #5 CUDA Backend, #6 OptiX Backend, #14 Path Tracer,
  #15 Progressive Renderer, #16 Denoiser, #17 AOVs, #18 Scene
  Format, #19 Renderer Server). The dominant cap on every GPU-
  side module is the project-wide visual-validation gate.
- **foundation landed**: 6 modules (#3 Image, #7 Scene Graph,
  #8 Geometry, #9 Material, #10 Texture, #11 Lighting, #12
  Camera) — data PODs + factories compile; the rendering-time
  behaviour each architectural module is named for is not yet
  on the device path.
- **not started**: 4 items (#20 C4D Bridge, #21 Native C4D
  Renderer, #22 Node Editor, master-order #22 Preview UI).
- **spec only**: 0 (every spec'd subsystem either has source
  code or is honestly "not started").

The honest summary is that the **renderer skeleton is in
place** (every architectural module exists in source form
where the master order has reached its slice; the backbone
data PODs + the GPU data path + the relativistic math leaf
+ the OptiX SDK linkage all compile clean), but **end-to-end
production readiness is gated on a CUDA + OptiX-SDK host
run** that has not happened in this branch. Modules at
"foundation landed" are not failures — they are intentionally
shipped as PODs first, with the GPU shading / sampling /
acceleration features deferred to their own slices later in
the master order. The point of this file is to make sure the
distinction is honest.

## How to update

Add or update rows here whenever a slice promotes a module's
status. The change should be additive in the same style as
`docs/BUILD_PLAN.md`'s slice-closing pattern: bump the row +
add a one-line note to the slice's BUILD_PLAN entry citing
this file. Do not promote a module to "production ready"
while any documented "deferred" gate for its core runtime
behaviour is still open.

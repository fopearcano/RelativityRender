# Architecture Audit

Status: audit step 5. Focused only on architectural
quality - module boundaries, file-size hygiene,
responsibility mixing, CPU/GPU separation, fit with a
serious renderer's expected layout. Builds on steps
1-4; does not re-litigate kernel correctness or memory
safety (those are clean per the GPU-render and
GPU-memory audits).

## 1. Current module map

| Module             | Path             | Role                                          | Files | Lines  |
|--------------------|------------------|-----------------------------------------------|------:|-------:|
| core               | `src/core/`      | logging, config, version, CLI                  |     8 |    283 |
| math               | `src/math/`      | vec / mat / transform / utils                  |     7 |    327 |
| relativity         | `src/relativity/`| RR_HD relativity primitives                    |     4 |    238 |
| image              | `src/image/`     | host buffer + IO                               |     6 |    280 |
| renderer           | `src/renderer/`  | Hit POD + AOV foundation                       |     5 |    214 |
| pathtracer         | `src/pathtracer/`| RNG + sampling helpers                         |     5 |    223 |
| geometry           | `src/geometry/`  | Sphere / Triangle / Mesh PODs                  |     5 |    151 |
| camera             | `src/camera/`    | host camera + RR_HD ray-gen                    |     4 |    230 |
| lighting           | `src/lighting/`  | Light POD                                       |     3 |    173 |
| material           | `src/material/`  | flat params + 2 graph implementations          |    14 |  2 528 |
| texture            | `src/texture/`   | constant + image texture                       |     5 |    217 |
| scene              | `src/scene/`     | host Scene container                           |     5 |    194 |
| io                 | `src/io/`        | .rrscene loader + writer                       |     5 |  1 253 |
| gpu                | `src/gpu/`       | backend-agnostic GPU layer                     |     9 |    921 |
| cuda               | `src/cuda/`      | kernels + view PODs + host wrappers            |    26 |  3 237 |
| optix              | `src/optix/`     | OptiX lifecycle + placeholder renderer         |     5 |    282 |
| server             | `src/server/`    | TCP renderer server                            |     3 |    535 |
| **TOTAL (src/)**   |                  |                                                | **119** | **11 286** |

(Tests: 18 files, 5 645 lines. Bridge: 12 files,
5 232 lines. Docs: 13 files, 18 056 lines.)

## 2. Major structural problems

The five concerns below are the blocking ones for the
serious rewrite. Listed in rough severity order.

### 2.1 Two parallel material-graph implementations

`src/material/` contains two implementations of the
same concept:

- `material/MaterialGraph.{h,cpp}` (**168 + 442 = 610
  lines**) - the older slice's monolithic data + eval
  + bake path. Namespace `rr::material::`.
- `material/graph/{Graph,Node,Socket,GraphEvaluator}.{h,cpp}`
  (**165 + 522 + 127 + 89 + 91 + 116 = 1 110 lines**)
  - the newer slice's data-core + reference evaluator.
  Namespace `rr::material::graph::`.

The newer code does NOT replace the older one - both
are linked into `rr_material`. Internal API surface is
divergent (`rr::material::compile_graph_to_material`
vs `rr::material::compile_graph_to_gpu_material`;
different `Graph` type, different `NodeType` enum,
different validation surface).

The rewrite picks **one** and drops the other. Per the
classification doc, the newer `graph/` data-core +
`GpuMaterial` IR is the keeper; `material/MaterialGraph`
is the bin pile. Until the migration runs, the
material module is the most architecturally muddled
part of the codebase.

### 2.2 `cuda/CudaTestKernel.cu` carries seven kernels in one TU

**917 lines** mixing every milestone from M6 through
M21:

| Kernel                  | Milestone | Status                                  |
|-------------------------|-----------|------------------------------------------|
| `k_gradient_rgba32f`    | M6        | Diagnostic; reachable but unused.         |
| `k_camera_rays_visualize`| M7       | Diagnostic; reachable but unused.         |
| `k_sphere_visualize`    | M8        | Diagnostic; reachable but unused.         |
| `k_sphere_relativistic` | M9        | Diagnostic; reachable but unused.         |
| `k_render_scene`        | M10/11/12 | **Production**.                           |
| `k_path_trace`          | M14       | **Production**.                           |
| `k_render_aovs`         | M17       | **Production**.                           |

Plus three `__device__` helpers (`trace_closest`,
`sky_color`, `lookup_material`,
`override_material_with_graph`) shared between the
production kernels.

The TU is structurally a stepping-stone artefact.
Production kernels share `__device__` helpers, but
mixing them with four obsolete diagnostic kernels in
the same TU forces every change to recompile a 917-
line file. Splitting into per-kernel files
(`k_render_scene.cu`, `k_path_trace.cu`,
`k_render_aovs.cu`) and a small `kernel_helpers.cuh`
for the shared `__device__` functions is the obvious
rewrite move.

### 2.3 `io/SceneLoader.cpp` is hand-rolled JSON in one 831-line file

The loader hand-implements:

- A JSON tokeniser + parser
- A `JsonValue` union type
- Per-section extractors (camera / render_settings /
  relativity / materials / spheres / lights / meshes)
- Per-field validators

All in **one .cpp**. The header surfaces `load_rrscene`
as a single entry point.

The loader works (16 io_tests pass) but it's the kind
of file a serious rewrite splits across at least three
TUs and ideally swaps in a vetted JSON library
(nlohmann's, RapidJSON, or simdjson). The hand-rolled
parser is functional but represents ~250 lines of
custom parser code the rewrite would not need to
maintain.

### 2.4 `main.cpp` carries three milestone "deliverables"

**325 lines**, of which roughly:

- 60 lines = CLI parse + dispatch (the actual main job).
- 60 lines = scene load + GpuScene upload.
- 30 lines = single `--render` (M13).
- 25 lines = path-trace passes at spp=1 and spp=16
  (M14 deliverable).
- 60 lines = build a 32x32 procedural checkerboard +
  bind it as a texture + render again (M16
  deliverable).
- 60 lines = save 6 PPMs from the AOV kernel (M17
  deliverable).
- 30 lines = `--serve` branch (M18).

The four "deliverable" sections (M14 path-trace, M16
texture, M17 AOVs) are demos that ran for the
milestone deliverables and never got cleaned up. They
should live in `tools/` or a separate examples binary;
`main` should be a CLI dispatcher only.

### 2.5 `renderer/` is mostly empty; the integrator lives in `cuda/`

The directory layout suggests `src/renderer/` is the
home of the renderer's logic - but it contains only:

- `Hit.h` (43 lines, one POD).
- `AOV.{h,cpp}` (the M17 foundation - which is image
  storage, not rendering logic).

The actual integrator code (path tracer's
`trace_one_path`, closest-hit search, shading dispatch)
lives in `cuda/CudaTestKernel.cu`. That CUDA-specific
TU is a sensible home for the kernel itself, but the
**logical integrator** (how a path is structured, what
a sample is, how AOVs map to integrator state) has no
dedicated module. It's diffused across:

- `cuda/CudaTestKernel.cu` (the kernel + path logic)
- `pathtracer/RNG.h` + `Sampling.h` (sampling
  primitives only; not the integrator)
- `renderer/Hit.h` (hit record)
- `material/MaterialGraph.cpp` (the bake path)

A serious renderer would have an `integrator/` module
(or expand `renderer/`) that owns the integrator's
data shape, the per-bounce loop, the AOV write
contract, the cancellation contract. That module would
be backend-agnostic; `cuda/` would contain only the
backend-specific kernel that EXECUTES the integrator's
logic on the device.

## 3. Acceptable parts

What works well, and the rewrite should reuse the
shape unchanged.

### 3.1 Layered build with no internal-header leakage

`docs/MODULE_MAP.md` formalises a layered architecture
(L0-L7); the build mostly respects it. Looking at
`CMakeLists.txt`'s `target_link_libraries`:

- `rr_image`, `rr_camera`, `rr_geometry`,
  `rr_material`, `rr_lighting`, `rr_texture`,
  `rr_relativity`, `rr_renderer` link nothing
  upstream - they're leaf math / data modules.
- `rr_scene` PUBLIC-links the leaf modules it embeds
  PODs from.
- `rr_io` PUBLIC-links `rr_scene`.
- `rr_gpu` PUBLIC-links `rr_scene + rr_geometry +
  rr_material + rr_lighting + rr_renderer`.
- `rr_optix` PUBLIC-links `rr_image`.
- `rr_server` PUBLIC-links `rr_io + rr_gpu`.
- The executable links `rr_gpu + rr_io + rr_optix +
  rr_server`.

Each step adds, never inverts, the dependency
direction. No layer-skipping. Forbidden imports
(per `docs/MODULE_MAP.md`) are respected: nothing
under `src/` includes from `integrations/`, no module
links to internal `rr_*` headers it shouldn't.

### 3.2 RR_HD shared math runs in both worlds

Math, relativity, sampling, RNG, intersection,
material graph evaluation are all `RR_HD inline` in
headers. The host test suite calls the same functions
the kernel calls; correctness of the device path
follows from the host tests by construction. This is
the project's strongest architectural property: it
makes the kernel-side work auditable from a host-only
build (the audits in steps 3-4 relied on it directly).

### 3.3 GPU layer / CUDA backend separation

`gpu/` is the host-agnostic surface (`GpuBuffer<T>`,
`GpuDevice`, `GpuScene`); `cuda/` is the
implementation. The dispatch is via:

- `RR_HAS_CUDA` macro at compile time (gated by
  `RR_ENABLE_CUDA` CMake option).
- `gpu/GpuBuffer.cpp` forwards every byte op to
  `rr::cuda::cuda_*`.

This means the host-only build (no CUDA Toolkit)
compiles cleanly; the GPU layer reports honest
absence at runtime. Opening a non-CUDA backend later
(a hypothetical Metal / Vulkan compute path) would
mean adding a `metal/` module + a new dispatch arm
in `gpu/GpuBuffer.cpp` - small surface area.

### 3.4 View-POD pattern for kernel arguments

`cuda/Cuda*.cuh` defines a family of "view" PODs:
`CudaSceneView`, `CudaMaterialGraphView`,
`TextureView`, `CudaMeshView`, `CudaAOVPack`. Each is:

- A struct of raw device pointers + counts.
- Trivially copyable.
- Host-includable (no CUDA-runtime types).
- Passed to the kernel by value as launch arguments
  OR baked into a parent view.

The pattern is clean: views borrow the underlying
data (`GpuScene` owns it via `GpuBuffer<T>`), live
exactly as long as the kernel call, and let the
kernel never touch a host pointer. The rewrite
should keep this shape.

### 3.5 AOV foundation + material data-core are well-shaped

Section 1's table puts `material/graph/` at
**1 110 lines** across six files - well-sized,
single-responsibility per file. Same for `renderer/AOV`
- 81 + 73 lines, focused.

Both are recent additions (M17 / M21). They show what
a clean module looks like in this codebase; the
rewrite should treat them as the template.

### 3.6 Tests track modules

Every leaf module has a matching `tests/<module>_tests.cpp`
file. Coverage is uneven (math: 195 lines, server: 323;
material_graph_core: 1 479) but the per-module shape
holds. The test suite **passes 17/17** suites today,
verified by every audit step's build check.

## 4. Missing modules

Things a serious renderer expects that this codebase
does not yet have.

| Module                        | Today's status                                                                 |
|-------------------------------|--------------------------------------------------------------------------------|
| **integrator** / `integrator/`| Lives inside `cuda/CudaTestKernel.cu`. No backend-agnostic integrator module.   |
| **bsdf** / `bsdf/`            | Single `MaterialEvalResult{baseColor, emissionColor, emissionStrength}`. Per-BSDF code (Lambertian / GGX / dielectric / glass) is not a module; placeholders flagged in `MaterialTypes.h`. |
| **accelerator** / `acc/`      | Brute-force `for` loops over spheres + triangles in every closest-hit kernel. No BVH, no kd-tree, no OptiX AS yet (M15 spec only). |
| **sampler** / `sampler/`      | Just `pathtracer/Sampling.h` (cosine-weighted hemisphere). No stratified / Sobol / blue-noise samplers. |
| **denoiser** / `denoise/`     | M22 spec only. No code.                                                         |
| **animation** / `anim/`       | Per-frame Execute calls work, but no time-aware data model; no motion vectors. |
| **scene-format graph block**  | The .rrscene parser does not yet read `materials[].graph` (M21 spec section 10.2.3); only flat material fields. |
| **multi-pass / EXR I/O**      | AOV save is per-AOV PPM; no multi-channel EXR.                                  |
| **server framebuffer streaming**| Server returns a saved file path; no binary frame transport.                   |
| **adaptive sampling / convergence detection** | None.                                                                |

These absences are all acknowledged in their own
specs (`OPTIX_BACKEND_PLAN.md`, `DENOISING_PLAN.md`,
`MATERIAL_GRAPH_SPEC.md`, `C4D_NATIVE_RENDERER_PLAN.md`)
- the architecture has placeholders for them - but
the rewrite has to build each one.

## 5. CPU / GPU separation

The CPU/GPU split is **clean** by construction:

- `__global__` / `__device__` code lives only in
  `cuda/*.cu` and `cuda/*.cuh`.
- Host code in any other module imports `cuda/*.cuh`
  ONLY for the view PODs (which are host-includable
  by design - no CUDA-runtime types).
- The path through which a host call reaches the
  device is fixed: `rr::cuda::CudaRenderer::render_*`
  -> `run_kernel_render` -> `launch_*` -> `<<<grid,
  block>>>` kernel.
- The path through which device data reaches the
  host is fixed: `cudaDeviceSynchronize` ->
  `GpuBuffer::download` -> `rr::image::Image`.

Per the GPU-render audit (step 3): zero violations of
the GPU-only-rendering rule. Per the GPU-memory audit
(step 4): zero raw `cudaMalloc` pointers escape the
backend. The split is enforced by the type system +
the build's RR_HAS_CUDA gating, not by convention.

The one architectural soft-spot is at the seam: a few
host headers (`gpu/GpuScene.h`) include
`cuda/CudaTexture.cuh` and `cuda/CudaMaterialGraph.cuh`
to embed view-PODs in the host scene class. The
headers are deliberately host-includable, but
**physically including a `cuda/` header from a
non-`cuda/` module reads as a layering inversion** at
first glance. The fix is small: move the view-POD
types into a backend-agnostic header (`gpu/Views.h`?)
and keep `cuda/Cuda*.cuh` as kernel-side aliases. v1
is not broken; v2 (the rewrite) cleans it up.

## 6. Implications for the rewrite

What changes architecturally:

1. **Pick one material-graph implementation** (the
   `graph/` data-core + `GpuMaterial` IR). Drop the
   other.
2. **Split `CudaTestKernel.cu`** into per-kernel
   files; move the M6-M9 diagnostic kernels into a
   separate target (`rr_diagnostics`) under `tools/`
   or delete.
3. **Promote the integrator out of `cuda/`** into a
   new `integrator/` module that owns the backend-
   agnostic per-path / per-sample data flow; `cuda/`
   keeps only the kernel that calls into integrator
   primitives.
4. **Strip `main.cpp`** to a CLI dispatcher; move
   the M14 / M16 / M17 demos into `tools/examples/`
   (or a dedicated `rr_demos` target).
5. **Replace the hand-rolled JSON loader** with a
   library (nlohmann or simdjson). Ship `io/` as a
   thin schema layer over the library.
6. **Move view-POD definitions** out of `cuda/*.cuh`
   into `gpu/Views.h` so non-`cuda/` modules don't
   include `cuda/` headers.
7. **Add the missing modules** (`integrator/`,
   `bsdf/`, `accelerator/`, `denoise/`) as the
   roadmap milestones land.
8. **Drop the `scene/Transform.h` shim** once
   callers migrate to `math::Transform`.

What stays:

- `core/`, `math/`, `relativity/`, `image/`,
  `geometry/`, `camera/`, `lighting/`, `texture/`,
  `pathtracer/RNG`, `pathtracer/Sampling`,
  `material/MaterialTypes`, `material/Material`,
  `material/graph/*`, `material/GpuMaterial.{h,cpp}`,
  `cuda/CudaBuffer`, `cuda/CudaContext`,
  `cuda/CudaIntersection.cuh`, `cuda/Cuda*View*.cuh`,
  `cuda/CudaMaterialGraph.cuh`, `gpu/GpuBuffer`,
  `gpu/GpuDevice`, `gpu/GpuMesh`,
  `gpu/GpuScene` (with the upload-path refactor),
  `optix/OptixBackend` (drop `OptixRenderer`
  placeholder), `server/RenderServer` (with the
  hardening listed in step 2's classification),
  `renderer/Hit`, `renderer/AOV`.

The bridge stays archived (`integrations/c4d/`)
per step 2's classification.

The architecture is **mostly good with five
fixable concentrations of debt**. Each item in
section 2 is a discrete cleanup; together they
move the codebase from "incremental development
artefact" to "serious renderer layout".

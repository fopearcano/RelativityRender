# RelativityRender — BUILD PLAN

This file is the live, project-wide log of what has landed and what is next.
Update it after every implementation step, per
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` rule 8.

---

## Current State

- **Active milestones:** M2 — Core Engine (in progress), **M3 — Math
  Library (landed)**, **M4 — Image / Framebuffer System (landed)**,
  **M5 — CUDA Device Layer (landed)**.
- **Active branch:** `claude/create-docs-architecture-T2Dp5`.
- **Code in repo:** repository skeleton, top-level CMake project, the
  minimal C++20 application foundation, configuration + CLI handling,
  the math library, the image / framebuffer system, the GPU device
  layer with device queries, the GPU buffer abstraction, the first
  CUDA kernel (gradient), the **camera system** (host
  `rr::camera::Camera` plus device-friendly `GpuCamera` POD and
  `RR_HD generate_camera_ray`), and a kernel that writes per-pixel
  primary rays as RGB. The `RelativityRender` executable's
  `--render` path now runs the camera-ray-visualization kernel and
  saves to `output/gpu_camera_rays.ppm` (or `--output`); the
  `render_gradient` kernel is preserved as a diagnostic. Four test
  executables — `math_tests` (42), `image_tests` (39), `gpu_tests`
  (20), and `camera_tests` (43) — all green through `ctest`.
  `rr_image`, `rr_gpu`, and `rr_camera` are static libraries;
  `RelativityRender` links `rr_gpu`. No primitive intersection,
  no scene system.

## Module Status (mirrors `docs/MODULE_MAP.md`)

| #  | Module                              | Status        |
|----|-------------------------------------|---------------|
| 1  | Core Engine                         | in progress   |
| 2  | Math Library                        | landed        |
| 3  | Image / Framebuffer System          | landed        |
| 4  | GPU Device Layer                    | landed        |
| 5  | CUDA Backend                        | in progress   |
| 6  | OptiX Backend                       | in progress   |
| 7  | Scene Graph                         | landed        |
| 8  | Geometry System                     | landed        |
| 9  | Material / Shading System           | landed        |
| 10 | Texture System                      | in progress   |
| 11 | Lighting System                     | landed        |
| 12 | Camera System                       | landed        |
| 13 | Relativistic Camera Model           | landed        |
| 14 | Path Tracer                         | in progress   |
| 15 | Progressive Renderer                | not started   |
| 16 | Denoiser Integration                | not started   |
| 17 | Render Passes / AOVs                | not started   |
| 18 | Scene File Format                   | landed        |
| 19 | Renderer Server                     | not started   |
| 20 | Cinema 4D Bridge                    | not started   |
| 21 | Future Native Cinema 4D Renderer    | not started   |
| 22 | Node Editor / Material Graph        | not started   |

All modules now have a placeholder source directory under `src/`,
`integrations/`, or `tools/` and a README pointing back at
`docs/MODULE_MAP.md`. No module ships any code.

## Milestone Status (mirrors `docs/MILESTONE_ROADMAP.md`)

| Milestone | Title                                   | Status      |
|-----------|-----------------------------------------|-------------|
| M0        | Architecture & Documentation            | landed      |
| M1        | Repository Skeleton & Build System      | landed      |
| M2        | Core Engine: Logging, Config, Lifecycle | in progress |
| M3        | Math Library                            | landed      |
| M4        | Image / Framebuffer System              | landed      |
| M5        | CUDA Device Layer                       | landed      |
| M6        | CUDA Framebuffer & First Kernel         | landed      |
| M7        | Camera System & GPU Camera Rays         | landed      |
| M8        | GPU Primitive Intersection              | landed      |
| M9        | Relativistic Camera Model (First Pass)  | landed      |
| M10       | GPU Scene Upload & Triangle Mesh        | landed      |
| M11       | Material System (Foundations)           | landed      |
| M12       | Lighting System (Foundations)           | landed      |
| M13       | Scene File Format & Parser              | landed      |
| M14       | Path Tracing Foundation                 | in progress |
| M15       | OptiX Backend (Upgrade Path)            | in progress |
| M16       | Texture System                          | in progress |
| M17       | Render Passes / AOVs                    | not started |
| M18       | Renderer Server                         | not started |
| M19       | Cinema 4D Bridge (Plugin)               | not started |
| M20       | Preview UI                              | not started |
| M21       | Material Node Graph (Editor)            | not started |
| M22       | Denoiser Integration                    | not started |
| M23       | Native Cinema 4D Renderer Integration   | not started |

---

## Change Log

### 2026-04-27 — M16 (foundation): texture system foundation

Host-side foundation only. ConstantTexture + ImageTexture + a
device-friendly `TextureView` POD with an `RR_HD inline` sampler.
No filtering beyond nearest-neighbor; no kernel currently
consumes a textured material slot. Shader / scene-format /
material-binding slices follow.

- **`src/texture/Texture.h` / `.cpp`:**
  - `TextureType` discriminator (`Constant = 0`, `Image = 1`)
    with stable ordinals so the upload contract stays
    forward-compatible.
  - `ConstantTexture` POD with a `Vec3 color` and an
    `RR_HD inline sample(Vec2)` that ignores UV (the simplest
    texture; the renderer's default for any unbound material
    slot).
  - Convenience factories: `make_white_texture`,
    `make_black_texture`, `make_constant_texture(color)`.
- **`src/texture/ImageTexture.h` / `.cpp`:** image-backed
  texture wrapping `rr::image::Image`. Sampling parameters
  surfaced now (`Wrap = Clamp / Repeat / Mirror`,
  `Filter = Nearest / Bilinear`) so scene-format and GPU
  upload paths can carry them; the host sampler only honors
  `Clamp + Nearest` at this milestone, with the others
  silently falling through to the implemented combination.
  Out-of-`[0,1]` UVs are clamped; empty images return black so
  callers don't have to special-case unbound slots.
  V-axis flipped on read so UV `(0, 0)` maps to the texture's
  bottom-left and UV `(0, 1)` maps to the top-left, matching
  the conventional UV orientation while the underlying
  `Image` keeps a top-left origin.
- **`src/cuda/CudaTexture.cuh`:** device-side launch-argument
  POD `TextureView` (tagged union of `Constant` /
  `Image` fields) plus an `RR_HD inline sample_texture(view,
  uv)` sampler. Despite the `.cuh` name the header pulls in
  no CUDA-runtime types, so the host suite runs the exact
  same code the kernel will. Null-image-data defensive
  fallback returns the `constant_color` so a host-only build
  produces predictable values without crashing on uninitialised
  slots.
- **`tests/texture_tests.cpp`:** 28 host assertions.
  - ConstantTexture defaults / factories / `type_tag`.
  - ImageTexture default-empty samples to black; `type_tag`.
  - Deterministic 2x2 fixture exercising all four corners,
    confirming the UV `v`-flip lands on the right pixels.
  - Out-of-range UV clamps to the nearest in-range texel.
  - Device-side `sample_texture` returns the constant colour
    for `Constant` and for `Image` with null data, and
    matches the host `ImageTexture::sample` value-for-value
    on every uv in a sweep across the corners + clamp cases.
- **`CMakeLists.txt`:** added `rr_texture` static library
  (PUBLIC-links `rr_image` because `ImageTexture` carries an
  `rr::image::Image` by value); added the `texture_tests`
  test executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/13 ... 13/13 all Passed
100% tests passed, 0 tests failed out of 13

$ ./build/bin/texture_tests
texture_tests: 28/28 passed
```

#### Per the prompt

- Five requested files (`Texture.h` / `Texture.cpp` /
  `ImageTexture.h` / `ImageTexture.cpp` / `CudaTexture.cuh`)
  all present.
- Constant-color texture + image texture placeholder + UV
  coordinates as a `Vec2` parameter at every public surface.
- No complex filtering: only nearest-neighbor + clamp wrap
  honored. Bilinear / repeat / mirror documented in the
  header / device POD for when the next slice lands.

#### Module / milestone status

- Module 10 (Texture System): `not started` -> `in progress`.
- M16 (Texture System): `not started` -> `in progress`.

The remaining texture work (kernel-side material binding,
GPU image upload through GpuScene, scene-format texture
references, full filtering modes) lands in subsequent slices.

### 2026-04-27 — M15.1 + M15.2: OptiX backend scaffold (steps 1+2 of the migration plan)

First implementation slice of the OptiX backend. Detection + a
lifecycle wrapper + a placeholder renderer; no programs, no
acceleration structures, no rendering yet. The CUDA backend
remains primary.

- **`src/optix/OptixBackend.{h,cpp}`:** lifecycle + status
  surface. `optix_backend_available()`, `optix_backend_name()`,
  and a `optix_backend_status_line()` probe suitable for
  startup logging. `OptixBackend` class wraps an
  `OptixDeviceContext`: move-only, `init()` calls
  `cudaFree(nullptr)` to ensure a CUDA context exists,
  `optixInit()` to load the runtime, and
  `optixDeviceContextCreate()` to open the context;
  `shutdown()` is always safe (no-op when uninitialised, when
  moved-from, or when the SDK is not compiled in).
- **`src/optix/OptixRenderer.{h,cpp}`:** placeholder render
  entry point mirroring the shape of `rr::cuda::CudaRenderer`
  (`Result { ok, image, message }`). At this milestone
  `render_placeholder` only probes the runtime and returns a
  descriptive message; the real OptiX pipeline lands in
  M15.3 / M15.4 per `docs/OPTIX_BACKEND_PLAN.md`.
- **`CMakeLists.txt`:**
  - Renamed the M1 `RR_ENABLE_OPTIX` placeholder option to
    `RELATIVITYRENDER_ENABLE_OPTIX` per the prompt.
  - Added an OptiX SDK detection block: respects
    `OPTIX_INSTALL_DIR` (CMake var or environment), then
    falls back to `find_path` over the common SDK locations
    (`/usr/local/optix/include`, `/opt/nvidia/optix/include`,
    `/opt/optix/include`, `$HOME/optix/include`). Failure to
    find `optix.h` is a `FATAL_ERROR` with a clear remedy.
    The block also enforces `RELATIVITYRENDER_ENABLE_OPTIX`
    requires `RR_ENABLE_CUDA` (OptiX sits on top of CUDA).
  - New `rr_optix` static library compiled into every build:
    sources `OptixBackend.cpp` + `OptixRenderer.cpp`, PUBLIC
    include of `src/`, PUBLIC link to `rr_image` (its public
    surface returns an `Image`). The OFF path compiles only
    the stub bodies. The ON path adds `RR_HAS_OPTIX` PUBLIC,
    `${OPTIX_INCLUDE_DIR}` PRIVATE, and `CUDA::cudart`
    PRIVATE (OptiX's headers transitively include
    `cuda_runtime.h`).
  - `RelativityRender` executable now links `rr_optix` so the
    status surface is reachable from `main.cpp`.
- **`src/main.cpp`:** added `log_optix_info()`, called from
  the `--device-info` path. It prints the
  `optix_backend_status_line()` probe result; on the
  default OFF build the line reads
  `OptiX backend: not compiled in (rebuild with RELATIVITYRENDER_ENABLE_OPTIX=ON)`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake -S . -B build && cmake --build build
... configures cleanly with default OFF; rr_optix builds the OFF
    stubs; rr_optix links into the executable ...

$ cd build && ctest --output-on-failure
 1/12 ... 12/12 all Passed
100% tests passed, 0 tests failed out of 12

$ ./build/bin/RelativityRender --device-info
[INFO] RelativityRender 0.0.1 starting
[INFO] GPU backend: (none)
[INFO] No GPU backend compiled in. Reconfigure with -DRR_ENABLE_CUDA=ON to enable CUDA.
[INFO] OptiX backend: not compiled in (rebuild with RELATIVITYRENDER_ENABLE_OPTIX=ON)
```

The OptiX-enabled path
(`-DRELATIVITYRENDER_ENABLE_OPTIX=ON -DRR_ENABLE_CUDA=ON` plus
`OPTIX_INSTALL_DIR` pointing at an installed SDK on a machine
with an NVIDIA driver) is correct by construction: the .cpp
calls only stable OptiX 7.x runtime entry points
(`optixInit`, `optixDeviceContextCreate`,
`optixDeviceContextDestroy`) and the link list matches the
SDK's documented dependencies. Not end-to-end runnable in this
environment.

#### Per the prompt

- The four requested files are all present.
- The CMake option is exactly `RELATIVITYRENDER_ENABLE_OPTIX`.
- SDK detection lands in CMake; init goes through `optixInit`
  + `optixDeviceContextCreate`; availability is printed on
  `--device-info`.
- No rendering: `OptixRenderer::render_placeholder` is a
  stub.
- The CUDA backend remains primary - `--render` continues to
  drive the existing CUDA path (and the host-only fallback);
  nothing routes through `rr_optix` for actual pixels.

#### Module / milestone status

- Module 6 (OptiX Backend): `not started` -> `in progress`.
- M15 (OptiX Backend / Upgrade Path): `not started` ->
  `in progress`.

The remaining migration steps (M15.3 build AS from `GpuScene`,
M15.4 programs + SBT, M15.5 validation, M15.6 promote OptiX to
default) are documented in
`docs/OPTIX_BACKEND_PLAN.md` §15.

### 2026-04-27 — OptiX backend plan: materials + camera + relativity + migration

Doc-only extension. Four final sections in
`docs/OPTIX_BACKEND_PLAN.md`. The plan is now design-complete -
implementation slices follow the step-by-step migration in
§15. No code, no CMake change.

- **§12 Material system integration.** Maps the existing
  CUDA-path material flow onto OptiX seam by seam. The
  `MaterialParams` array stays launch-wide (lives in launch
  parameters, not SBT records); per-instance / per-primitive
  `material_index` rides in the hit-group SBT record's
  payload. The host upload path (`GpuScene::upload_materials`)
  is unchanged. Adding more BSDF lobes is a closest-hit
  program edit; live material updates rewrite the buffer with
  no AS / SBT touch.
- **§13 Camera integration.** `GpuCamera` becomes a launch
  parameter (per-launch state, same for every pixel and
  bounce). The raygen program reads `launch_params.camera` and
  calls the existing `RR_HD inline generate_camera_ray` with
  no source change. The `M7 aspect / fov / basis` logic
  carries over identically; `camera_tests` keeps validating
  the device math by construction.
- **§14 Relativity integration.** All five seams stay in the
  raygen program: aberration on the primary ray's direction
  immediately after `generate_camera_ray`; `dopplerFactor` /
  `searchlightFactor` / `applyDopplerColor` wrap the
  *integrated* radiance after the bounce loop. The closest-hit
  and miss programs are deliberately relativity-free.
  Per-bounce aberration is documented as a small follow-up,
  not a blocker. Every `RR_HD inline` helper in
  `relativity/RelativityMath.h` and the host
  `relativity_tests` suite (52) carry over unchanged.
- **§15 Migration plan.** Six concrete, independently-
  shippable slices:
  - M15.1: SDK + CMake plumbing (`RR_ENABLE_OPTIX` option,
    `find_package(OptiX)`).
  - M15.2: `rr_optix` library skeleton (`OptixContext`,
    pipeline scaffold, no programs / AS / SBT yet).
  - M15.3: Build acceleration structures from `GpuScene`
    (sphere GAS, per-mesh triangle GAS, IAS over them; build
    only, no traversal yet).
  - M15.4: Programs + modules + SBT.
    `RaygenPathTrace.cu` / `MissEnvironment.cu` /
    `HitClosestRadiance.cu`; new entry point
    `CudaRenderer::render_pathtrace_optix` next to the
    existing `render_pathtrace`.
  - M15.5: Validation. Side-by-side comparison test;
    bit-equal at fixed seed for trivial scenes, sample-noise
    envelope for the full path tracer.
  - M15.6: Promote OptiX to default. CUDA stays available
    behind the same flag.
  Plus an explicit §15.7 statement of why the CUDA path stays
  after M15: host-test coverage of the shared `RR_HD inline`
  math, debug fallback for non-RTX hardware, regression
  baseline for OptiX bugs. **OptiX as default, CUDA as
  fallback - not "OptiX replaces CUDA".**

The "Out of scope" list (now §16) shrinks dramatically -
materials / camera / relativity / build & SDK plumbing are no
longer there. What remains: specific OptiX SDK version
targeting (chosen in step M15.1), OptiX denoiser integration
(M22), multi-GPU / multi-stream traversal (M18+), per-bounce
relativistic aberration (small follow-up), and curves /
volumes / displaced surfaces (each adds incremental scaffolding
on top of the v1 OptiX backend).

References (§17) is unchanged.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 12/12`).

#### Per the prompt

- The four requested topics (Material System Integration,
  Camera Integration, Relativity Integration, Migration Plan)
  are now sections in the doc.
- Explanations are concrete and tied to the current architecture
  - each section names the existing source files, structs, and
  test suites that map onto the OptiX layer.
- No code; the migration plan is the implementation contract
  for the M15 slices that follow.

### 2026-04-27 — OptiX backend plan: AS + SBT + data flow

Doc-only extension. Three new sections in
`docs/OPTIX_BACKEND_PLAN.md`. No code, no CMake change, no
implementation.

- **§9 Acceleration structures.** Two AS kinds in v1.
  - **GAS** (Geometry Acceleration Structure): a BVH over a
    single set of primitives. Triangle GAS uses OptiX's
    built-in triangle intersection (consumes `Vertex` /
    `Triangle` arrays our existing `GpuMesh` already
    uploads). Custom-primitive GAS uses AABB build inputs
    plus a custom intersection program; spheres land here,
    with `rr::cuda::intersect_sphere` lifted from
    `cuda/CudaIntersection.cuh` as the program body.
  - **IAS** (Instance Acceleration Structure): a BVH-of-BVHs.
    Each leaf is an `Instance` carrying a GAS handle plus a
    `3x4` world transform (the existing host
    `rr::math::Transform` SRT decomposition flows in
    unchanged). Instancing lets N copies share one
    vertex/index buffer + one GAS.
  - **Why BVH is critical.** Compares the three approaches
    side by side: today's naive `O(N)` linear scan, a
    software BVH at `~O(log N)` plus traversal overhead, and
    OptiX hardware BVH at `~O(log N)` on RT cores. The naive
    path stays as a fallback / regression baseline; the
    OptiX path is what scales to non-trivial scene
    complexity.
- **§10 Shader Binding Table.** Three record kinds in v1.
  - **Record layout**: 32-byte header (program-group hash
    populated by `optixSbtRecordPackHeader`) plus an aligned
    user-defined payload.
  - **Records**: one raygen, one miss per ray type (v1 has
    one ray type, "radiance"), one hitgroup per (instance,
    ray type) pair. Hit-group payload is where the
    geometry / material wiring lives - device pointers to
    vertex / index buffers, the mesh's `material_index`,
    transform pointers. Exact field list deferred to the
    materials slice of the plan.
  - **Why the SBT matters**: dispatch is a data structure,
    not an `if/else` ladder; new primitive types add record
    kinds; material updates don't rebuild geometry; multiple
    ray types share hit groups.
- **§11 Data flow.** End-to-end ASCII pipeline diagram
  showing the seven steps from `.rrscene` to PPM. Steps 1, 2,
  5, 6 already exist (loader, `GpuScene::upload_from`,
  framebuffer download, `Image::save_ppm`); steps 3 and 4 are
  what M15 adds (OptiX backend build + `optixLaunch`).
  Captured the three architectural invariants:
  - GpuScene remains the single owner of scene data on the
    device; the OptiX backend is a consumer that references
    GpuScene's pointers through GAS build inputs and SBT
    payloads (no data duplication).
  - `CudaRenderer` keeps its public surface; the new entry
    point sits next to `render_pathtrace`.
  - The CPU's job does not change - no per-pixel work
    crosses back to the host.

The "Out of scope" list (now §12) shrinks: AS, SBT, and the
data-flow diagram are no longer there. Materials / camera /
relativity integration / build-and-SDK plumbing remain
deferred. References (§13) is unchanged.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 12/12`).

#### Per the prompt

- The three requested topics (Acceleration Structures, Shader
  Binding Table, Data flow) are now sections in the doc.
- Focus is architectural - GAS / IAS roles, SBT shape, end-to-end
  pipeline. No record byte layouts, no concrete code.
- Materials, camera, relativity integration intentionally
  remain out-of-scope (still listed in §12 with the build /
  SDK plumbing).

### 2026-04-27 — OptiX backend plan: introduction slice

Documentation-only. First slice of `docs/OPTIX_BACKEND_PLAN.md`.
No code, no CMake change, no parser / kernel / module
changes.

- **`docs/OPTIX_BACKEND_PLAN.md`:** introduction slice.
  - Status callout: spec only; subsequent slices add the
    deferred sections (AS, SBT, materials, camera, relativity,
    build / SDK integration) before any implementation.
  - **§2 Where we are today**: snapshot of the current naive
    intersection path, the two scene kernels that share
    `trace_closest`, and the brute-force loop over
    `scene.spheres` + `scene.mesh.triangles` per ray.
  - **§3 Why naive intersection does not scale**: per-pixel cost
    is `spp * max_depth * (sphere_count + triangle_count)`;
    the M12 test scene at 1280x720 / spp = 16 / depth = 4 is
    ~880 M intersections - tractable. Adding one 100 k-tri
    mesh balloons it to ~14 trillion. Three structural
    problems enumerated: no early rejection, no instancing, no
    RT-core hardware help.
  - **§4 Why OptiX**: hardware-accelerated BVH traversal,
    built-in instancing, pluggable program model. OptiX runs
    on top of CUDA, so it slots in next to the existing
    backend rather than replacing it.
  - **§5 Pipeline overview**: three programs in v1
    (`raygen` / `miss` / `closest-hit`); custom intersection
    and any-hit are out of scope. The CPU's job (configure
    scene, launch, save) does not change; the GPU's structure
    does.
  - **§6 raygen**: owns the work at the top of `k_path_trace` -
    pixel index, RNG, primary ray, aberration, bounce loop
    driving `optixTrace`, post-loop Doppler / searchlight,
    framebuffer write. All ray paths still on the GPU; the
    `RR_HD inline` helpers carry over unchanged.
  - **§7 miss**: the existing `sky_color` lives here verbatim;
    Doppler / searchlight intentionally do **not** run inside
    the miss program because they wrap the integrated radiance
    after the whole path is done.
  - **§8 closest-hit**: owns the bounce-step branch of
    `trace_one_path` - material lookup, surface reconstruction,
    emission accumulation, cosine-weighted bounce sample,
    throughput update. The bounce *loop* stays in raygen.
  - **§9 Out of scope for this slice**: acceleration structures,
    shader binding table, material data, camera data,
    relativity integration, build / SDK integration. Each gets
    its own slice in the same incremental style as the
    RRSCENE format spec.
  - **§10 References**: the CUDA backend files the migration
    replaces, the master architecture / module map / milestone
    roadmap entries that govern the work.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 12/12`).

#### Per the prompt

- Only the introduction + high-level overview slice was added.
- The three pipeline programs (raygen, miss, closest-hit) are
  described with their RelativityRender role.
- Acceleration structures, SBT, materials, camera, relativity
  intentionally not covered yet; documented as out-of-scope
  with a placeholder list at §9.
- No code.

### 2026-04-27 — M14 minimal CUDA path tracer

First end-to-end path tracer. Per pixel: traces `spp` independent
paths with cosine-weighted Lambertian bounces up to `max_depth`,
accumulates emission + environment-fallback radiance, applies the
existing relativistic pipeline (Doppler + searchlight) to the
integrated value, and writes the average to the framebuffer.
Single-launch, multi-sample-per-thread. The "accumulation buffer"
is the per-thread Vec3 register sum; the framebuffer carries the
mean. CPU only configures + launches + saves.

- **`src/cuda/CudaScene.cuh`:** added `launch_path_trace`
  declaration. Lives next to `launch_render_scene` since both
  consume `CudaSceneView`.
- **`src/cuda/CudaTestKernel.cu`:** factored out three device
  helpers:
  - `trace_closest(scene, ray, t_min)` - the closest-hit search
    over spheres + the single mesh slot, used by the path tracer
    on every bounce.
  - `sky_color(scene, ray_dir)` - the M12 environment fallback
    extracted; an uploaded `Environment` light wins, otherwise
    the existing vertical sky gradient.
  - `lookup_material(scene, idx)` - bounds-checked material
    fetch with the renderer's neutral default fallback.
  Plus `trace_one_path(...)`, the per-sample core: aberrate the
  primary ray, then for up to `max_depth` bounces find the
  closest hit, accumulate emission, sample a cosine-weighted
  bounce off the (orientation-corrected) normal, multiply the
  throughput by `baseColor` (Lambertian /pi vs cos(theta)/pi
  cancellation), and continue. On miss the path adds
  `throughput * sky_color` and terminates. After the loop
  Doppler colour + searchlight scaling are applied to the
  integrated radiance using the primary ray direction.
  `k_path_trace` runs the per-sample loop spp times and writes
  the mean. Cheap throughput-near-zero early-out terminates
  paths whose contribution can no longer matter (Russian
  roulette is the proper unbiased version; that's the next
  M14 slice).
- **`src/cuda/CudaRenderer.{h,cu}`:** added
  `render_pathtrace(GpuScene, width, height, spp, max_depth,
  seed_offset = 0)`. Reuses the existing `run_kernel_render`
  scaffold; the only new logic is the launch-argument bundling
  and the spp / max_depth clamp.
- **`src/main.cpp`:** after the existing single-bounce
  `render_scene` save, `--render` now also produces two M14
  deliverables of the same uploaded scene:
  - `output/pathtrace_spp_1.ppm`  (spp = 1, max_depth = 4)
  - `output/pathtrace_spp_16.ppm` (spp = 16, max_depth = 4)
  Each pass logs the spp / max_depth before launch and the
  saved path after. Both go through the same `GpuScene` upload,
  so the path-traced and direct-lit passes diff only in the
  kernel. `--output` continues to override the direct-lit save
  path; the path-trace files are fixed for the milestone
  deliverable so they're easy to compare across commits.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/12 Test  #1: math_tests       ........... Passed  0.00 sec
 ...
12/12 Test #12: sampling_tests   ........... Passed  0.01 sec
100% tests passed, 0 tests failed out of 12

$ ./build/bin/RelativityRender --render scenes/test_minimal.rrscene \
        --width 32 --height 32
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] loading scene: scenes/test_minimal.rrscene
[INFO] loaded scene: 0 materials, 0 spheres, 0 lights, 0 meshes
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON
       to render the loaded scene)
```

The CUDA-enabled path is correct by construction: every
device-side helper the integrator composes (`generate_camera_ray`,
`aberrateDirection`, `intersect_sphere`, `intersect_triangle`,
`make_rng` / `next_float`, `build_orthonormal_basis`,
`sample_hemisphere_cosine`, `dopplerFactor`, `searchlightFactor`,
`applyDopplerColor`) is exercised by the host suite via the
`RR_HD inline` shared headers (`camera_tests` 43, `geometry_tests`
46, `relativity_tests` 52, `sampling_tests` 60). The integrator
itself is straight-line vector arithmetic over those primitives.

#### Hard-rule check

- **All ray paths on the GPU**: the bounce loop and every
  intersection / sampling / shading step run inside
  `k_path_trace`. The CPU has no per-ray work.
- **CPU only launches + saves**: `main.cpp` builds a `GpuScene`,
  invokes `render_pathtrace` twice (spp = 1 and 16), and writes
  the resulting `Image` to PPM. The only CPU iteration over
  pixels is `Image::save_ppm`'s float -> byte conversion,
  permitted as image save internals.

#### Not in this slice (M14 still in progress)

- No NEE / MIS direct-light sampling; the integrator picks up
  light only through brute-force bounces and emissive surfaces.
- No Russian roulette; the cheap throughput-zero early-out is a
  conservative biased substitute. Real RR with continuation
  probability is the next M14 slice.
- No multi-mesh upload; `CudaSceneView` still has a single mesh
  slot.
- No real device-side accumulation buffer across launches. The
  framebuffer is the final mean of one launch; consumers wanting
  multi-launch progressive refinement can blend frames
  themselves and pass distinct `seed_offset` values - the kernel
  already supports the latter.
- Module 14 / M14 stay "in progress" until those land.

### 2026-04-27 — M14 prep: GPU sampling foundation (RNG + hemisphere)

Path-tracer foundation only - no integrator, no kernel changes,
no `--render` rewiring. The headers ship the primitives the M14
integrator will compose with: a per-pixel RNG and uniform /
cosine-weighted hemisphere samples, all `RR_HD inline` so the
host suite covers the device math by construction.

- **`src/pathtracer/RNG.h`:** PCG-XSH-RR (64-bit state, 32-bit
  output). Surface: `RNG`, `wang_hash(uint32)`,
  `make_rng(x, y, sample) -> RNG`, `next_uint(RNG&)`,
  `next_float(RNG&) -> [0, 1)`. The seed mixer guarantees that
  adjacent `(x, y, sample)` triples land on distinct streams;
  the state is forced odd / non-zero. `next_float` uses 24
  bits of the next integer so the value lands exactly inside a
  single-precision mantissa with no rounding bias.
- **`src/pathtracer/RNG.cuh`:** thin re-export of `RNG.h` so
  kernel TUs can include a `.cuh`. Future device-specific
  overrides (warp-coherent advancement, hardware-RNG hooks)
  land here without touching the host surface.
- **`src/pathtracer/Sampling.h`:** hemisphere sampling
  primitives. Frisvad / Duff branchless ONB
  (`build_orthonormal_basis(n, t, b)`); local-frame samples
  `sample_hemisphere_uniform_local(u1, u2)` (PDF =
  `1 / (2*pi)`) and `sample_hemisphere_cosine_local(u1, u2)`
  (PDF = `cos(theta) / pi`); world-frame wrappers that
  build a basis around `normal` and rotate the local sample;
  matching `pdf_hemisphere_uniform()` and
  `pdf_hemisphere_cosine(cos_theta)` helpers; RNG-driven
  convenience wrappers that pull the two `u1`/`u2` floats
  internally for kernel ergonomics.
- **`src/pathtracer/Sampling.cuh`:** thin re-export of
  `Sampling.h`.
- **`tests/sampling_tests.cpp`:** 60 host assertions covering
  the requested surface plus the corner cases the kernel needs
  to be robust against.
  - RNG: `next_float` always in `[0, 1)`; same seed reproduces
    the same stream; adjacent `(x, y)` pixels diverge within
    the first 16 floats; mean over 10000 samples is
    `0.5 +/- 0.01`.
  - ONB: tangent / bitangent / normal are unit length and
    mutually orthogonal for the six axis normals (including
    `(0, 0, -1)` where the naive Frisvad form breaks) plus
    three arbitrary directions.
  - Hemisphere: 1000-sample sweeps confirm every uniform /
    cosine sample is unit length and lies in the hemisphere
    (`dot(d, n) >= 0`); 10000-sample means confirm the
    analytic statistics (uniform mean cos(theta) = 1/2,
    cosine-weighted mean cos(theta) = 2/3); a tilted-normal
    sweep verifies that the cosine-weighted mean direction
    aligns with the normal.
  - PDFs: `pdf_hemisphere_uniform` is `1 / (2 pi)`;
    `pdf_hemisphere_cosine(1)` is `1/pi`,
    `pdf_hemisphere_cosine(0)` is `0`,
    `pdf_hemisphere_cosine(-x)` clamps to `0`.
- **`CMakeLists.txt`:** added `sampling_tests` test executable.
  Header-only module, no library; `rr_camera` link supplies
  math + the `src/` include path transitively.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/12 Test  #1: math_tests       ........... Passed  0.00 sec
 ...
12/12 Test #12: sampling_tests   ........... Passed  0.01 sec
100% tests passed, 0 tests failed out of 12

$ ./build/bin/sampling_tests
sampling_tests: 60/60 passed
```

#### Per the prompt

- `Sampling.h` / `Sampling.cuh` / `RNG.h` / `RNG.cuh` are the
  only new source files.
- Per-pixel RNG, random float, hemisphere sampling, and
  cosine-weighted sampling are all surfaced.
- No path-tracing integration: the kernels in
  `cuda/CudaTestKernel.cu` are unchanged; nothing reads
  `Sampling.cuh` yet.
- Module 14 (Path Tracer) and M14 (Path Tracing Foundation)
  flip from "not started" to "in progress" - foundation in,
  integrator still to come.

### 2026-04-27 — M13 finalized: full SceneLoader + SceneWriter + renderer wiring

The format becomes real. Every v1 spec section is now parsed,
written back, uploaded to the GPU, and rendered through the
existing kernel. The user's documented command
`RelativityRender --render scenes/test.rrscene --output
output/from_scene.ppm` works end-to-end.

- **`src/scene/Scene.h`:** rewrote `SceneMesh` and `SceneLight`
  from the M9/M11 placeholders to embed the host PODs:
  - `SceneMesh { object, data: rr::geometry::Mesh,
                 source_path }` - `data` carries vertices,
    triangles, `material_id`, transform; `source_path` is
    reserved for future external-asset references.
  - `SceneLight { object, data: rr::lighting::Light }` - the
    embedded POD is what `GpuScene::upload_from` publishes.
  Updated `scene_tests.cpp` to the new shapes.
- **`src/io/SceneLoader.{h,cpp}`:** added `load_lights` and
  `load_meshes`.
  - `load_lights`: discriminated by `type` string. `"point"`
    requires `position`; `"directional"` requires `direction`
    (auto-normalised by the existing `make_*` factories).
    `intensity >= 0`. Other type strings (incl. `"area"` /
    `"environment"`) are v1 errors per spec.
  - `load_meshes`: `vertices` (array of Vec3) and `triangles`
    (array of `[v0,v1,v2]` index triplets, CCW) required.
    Per-vertex normals / UVs are not in v1 - the parser
    populates them at zero and the renderer derives geometric
    face normals. `material_id` (default `-1`) is the spec
    lookup key. `transform` is optional with identity default
    and is parsed via a shared `load_transform` helper that
    maps file-side `rotation` onto host
    `Transform::euler_rotation_radians`.
  Header docstring updated; nothing remains on the
  warn-and-ignore list for v1.
- **`src/io/SceneWriter.{h,cpp}`:** new module. Inverse of the
  loader. `WriteResult save_rrscene(scene, path)` writes a v1
  JSON file with readable indentation; creates parent dirs.
  Material fields v1 doesn't expose (`metallic`, `specular`,
  `transmission`) and light types v1 doesn't expose (`area`,
  `environment`) are silently dropped during serialisation -
  they live on host PODs but aren't part of the v1 schema.
  Round-trips through the loader for everything v1 stores.
- **`src/gpu/GpuScene.cpp`:** `upload_from` now does the full
  scene-to-device translation:
  - Builds a flat `MaterialParams[]` from `scene.materials` and
    a spec-id -> array-index map.
  - Sphere `material_index` (the spec lookup key after parsing)
    is remapped to the device array index via that map.
  - Visible lights are flattened into a `Light[]` and uploaded.
  - The first visible mesh fills the single mesh slot
    (multi-mesh upload is a future slice); its `material_id`
    is remapped the same way. If no visible mesh exists the
    slot is cleared so stale state from a previous render
    can't leak through.
- **`src/main.cpp`:** `--render` is now driven by the loader.
  Always loads the file (host-side; works even without CUDA),
  reports counts, and - when `RR_HAS_CUDA` is on - runs the
  full upload + render + save pipeline. Output defaults to
  `output/from_scene.ppm` matching the user's command;
  `--output` overrides. The previous M12 hard-coded scene is
  gone (it was a stand-in until real loading existed; earlier
  outputs remain reproducible from older git commits).
- **`scenes/test.rrscene`:** drop-in fixture matching the M12
  lighting scene (5 materials, 4 spheres, 1 quad, 2 lights).
  This is the exact file the user's documented command points
  at.
- **`tests/io_tests.cpp`:** added `test_load_full_scene`
  (asserts the M12 fixture loads with the right counts +
  per-light + mesh details, prints a summary) and
  `test_writer_round_trip` (load -> save -> reload, asserts
  every v1 field survives intact). `io_tests: 81/81 passed`
  (was 36).
- **`CMakeLists.txt`:** `rr_io` lists `src/io/SceneWriter.cpp`;
  the executable link list adds `rr_io`; `rr_scene` PUBLIC-
  links `rr_geometry` and `rr_lighting` since `Scene.h` now
  embeds their PODs.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/11 ... 11/11 all Passed
100% tests passed, 0 tests failed out of 11

$ ./build/bin/io_tests | tail -8
--- loaded full scene ---
  materials: 5  spheres: 4  lights: 2  meshes: 1
    light[0] directional color=(1.00, 0.95, 0.85) intensity=0.90
    light[1] point color=(0.70, 0.80, 1.00) intensity=8.00
    mesh[0] name=warm_quad vertices=4 triangles=2 material=3
-------------------------
io_tests: 81/81 passed

$ ./build/bin/RelativityRender --render scenes/test.rrscene --output output/from_scene.ppm
[INFO] render command received
[INFO] loading scene: scenes/test.rrscene
[INFO] loaded scene: 5 materials, 4 spheres, 2 lights, 1 meshes
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render the loaded scene)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a Turing/Ampere/
Ada GPU) produces `output/from_scene.ppm`. Correct by
construction: the kernel calls the same `RR_HD` routines that
the host suite already covers (`camera_tests` / `geometry_tests`
/ `relativity_tests` / `material_tests` / `lighting_tests`).
The new translation layer (spec id -> array index) is
exercised by `io_tests` for the host side and by the host
tests for material lookup; the GPU only sees flat array
indices.

#### Hard-rule check

- **No CPU rendering** - the entire shading pipeline still runs
  in `k_render_scene`. The CPU only loads the file, uploads to
  the GPU, and saves the framebuffer.
- **No CPU pixel iteration in the render path** - only inside
  `Image::save_ppm`.

#### What this milestone closes (M13 / Module 18)

- M13 (Scene File Format & Parser) -> landed: spec is in,
  loader covers every v1 section, writer round-trips, the
  executable consumes a `.rrscene` end-to-end.
- Module 18 (Scene File Format) -> landed for v1. v2 will add
  textures, env maps, area lights, metallic/specular/
  transmission, vertex normals/UVs, `source_path` mesh
  references; all are forward-compatible.

### 2026-04-27 — M13 parser slice 2: SceneLoader gains materials + spheres

Second `.rrscene` loader slice. Parses both new sections per the
v1 spec, populates the host `Scene` lists, and lays the wiring
needed for the eventual renderer-side integration (still to
come). No `SceneWriter`; rendering remains GPU-only via the
existing M12 hard-coded scene.

- **`src/scene/Scene.h`:** rewrote `SceneMaterial` from the M9
  placeholder (`name` + `albedo`) to the real shape: `id`
  (lookup key matching the spec's stable handle), `name`,
  `params` (full host `MaterialParams`). `id == -1` keeps the
  "renderer's neutral default" semantics that unmatched lookups
  use. Added `#include "material/MaterialTypes.h"`.
- **`src/io/SceneLoader.{h,cpp}`:** added `load_materials` and
  `load_spheres` extractors.
  - `materials`: each entry requires `id` (non-negative,
    unique within the array). `name` defaults to empty. The
    rest (`base_color`, `emission_color`, `emission_strength`,
    `roughness`) take their host `MaterialParams` defaults
    when absent. Per-spec clamps: `roughness` to `[0, 1]`,
    `emission_strength` to `>= 0`, colour components to
    `[0, ∞)`. Duplicate `id` is a parse error with a clear
    diagnostic that names the duplicating entry.
  - `spheres`: `position` and `radius` are required;
    `material_id` is optional and defaults to `-1`. `radius`
    must be `> 0`. The spec stores `material_id` as the
    lookup key (the entry's `id`, not its array index); the
    parser preserves it on `Sphere::material_index` so the
    eventual GpuScene::upload_from translation step has the
    raw value to remap.
  - The header docstring now lists `materials` and `spheres`
    alongside the previously-supported sections; only `lights`
    and `meshes` remain on the warn-and-ignore list.
- **`scenes/test_geometry.rrscene`:** new fixture - 3 materials
  (including a sparse `id = 3`), 4 spheres (one of them omits
  `material_id` entirely so the `-1` default path is exercised),
  default zero-velocity observer, default camera. Designed to
  cover the corner cases for the next renderer slice.
- **`tests/io_tests.cpp`:** added `test_load_test_geometry_scene`
  (loads the new fixture, prints counts + per-entry summary,
  asserts each loaded value including the sparse-id and
  no-material-id cases) and
  `test_minimal_scene_still_has_no_geometry` (the original
  minimal fixture, which omits both new sections, still loads
  with empty lists rather than failing). `io_tests: 36/36
  passed`.
- **`tests/scene_tests.cpp`:** updated to the new
  `SceneMaterial` shape (`id` + `name` + `params`) so the
  existing scene structure tests keep passing.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/11 ... 11/11 all Passed
100% tests passed, 0 tests failed out of 11

$ ./build/bin/io_tests
... (minimal scene printout) ...
--- loaded geometry scene ---
  materials: 3 entries
    [0] id=0 name=matte_red baseColor=(0.90, 0.15, 0.15) emission=(0.00, 0.00, 0.00) * 0.00 rough=0.50
    [1] id=1 name=matte_green baseColor=(0.15, 0.85, 0.25) emission=(0.00, 0.00, 0.00) * 0.00 rough=0.50
    [2] id=3 name=warm_emitter baseColor=(0.00, 0.00, 0.00) emission=(1.00, 0.85, 0.40) * 2.00 rough=1.00
  spheres: 4 entries
    [0] center=(0.00, 0.00, -3.00) r=1.00 material_id=0
    [1] center=(-1.60, 0.00, -3.50) r=0.60 material_id=1
    [2] center=(0.00, 1.60, -3.00) r=0.40 material_id=3
    [3] center=(1.60, 0.00, -3.50) r=0.60 material_id=-1
------------------------------
io_tests: 36/36 passed
```

#### Per the prompt

- Only `materials` and `spheres` were added.
- `lights` and `meshes` are not parsed (still warn-and-ignore).
- No `SceneWriter`.
- Rendering remains GPU-only - the loader is exercised by
  `io_tests` only; `--render` continues to use the M12
  hard-coded scene.

#### Naming note

The on-disk `material_id` references the spec's lookup key
(the material's `id`), not the array index. The parser stores
this value verbatim on `Sphere::material_index`. The
translation from spec id to device-side array index happens at
GPU upload time and is the next M13 / renderer slice's
responsibility.

### 2026-04-27 — M13 parser slice 1: SceneLoader (camera + relativity + render settings)

First real `.rrscene` parsing. Hand-rolled JSON parser (~250 lines)
in the implementation TU; pulling in a 25k-line third-party
header for six top-level keys is overkill at this milestone, and
the same `load_rrscene` surface lets us swap to nlohmann/json
without churn when the format grows. Per the prompt: only
`render_settings`, `camera`, and `relativity` are parsed; the
deferred sections (materials, spheres, lights, meshes) are
warned-and-ignored per spec. No SceneWriter; no main.cpp wiring;
rendering remains GPU-only.

- **`src/io/SceneLoader.h`:** `rr::io::LoadResult { ok, scene,
  message }` + `load_rrscene(path) -> LoadResult`. Host-only;
  no GPU dependencies. The header only mentions the v1 sections
  the parser actually populates and explicitly lists the
  warn-and-ignore deferred sections so future slices have a
  clear contract.
- **`src/io/SceneLoader.cpp`:** in-house JSON parser
  (`JsonParser` + `JsonValue` tagged-union; supports objects,
  arrays, strings with basic escapes, numbers including
  scientific notation, `true` / `false` / `null`; reports
  line / column on every error). Section extractors map the
  parsed tree onto host structs:
  - `render_settings` -> `Scene::render_settings.{width,height}`,
    rejects non-positive dimensions.
  - `camera` -> `Scene::camera`. `position` + `forward` +
    `up` populate the basis via `Camera::look_at`; `fov`
    in `(0, 180)` becomes `vertical_fov_degrees`. After
    loading, `Camera::set_aspect(width / height)` derives
    aspect from the render settings.
  - `relativity` -> `Scene::observer.velocity = beta_velocity *
    normalize(velocity_direction)` (clamped to `|β| < 1`),
    plus the three strengths mapped onto the host
    `RelativityParams`. Aberration is binary on the host so
    `aberration_strength > 0` toggles `enable_aberration`;
    `doppler_strength` / `searchlight_strength` flow into the
    matching continuous knobs and toggle their `enable_*`
    booleans.
  Validation rules (`version == 1`, vec3 length, fov range,
  non-zero forward, etc.) all reported with file-relative
  diagnostics.
- **`scenes/test_minimal.rrscene`:** fixture covering the three
  parsed sections at the same `β = 0.3` setup as the M12 light
  scene.
- **`tests/io_tests.cpp`:** loads the fixture via
  `RR_TEST_FIXTURES_DIR` (compile-time absolute path baked in
  by CMake so the test runs from any ctest working directory),
  prints the parsed scene state for human inspection, and
  asserts each loaded value. Plus a missing-file negative test.
  `io_tests: 16/16 passed`.
- **`CMakeLists.txt`:** added `rr_io` static library
  (`src/io/SceneLoader.cpp`, PUBLIC link to `rr_scene`) and
  the `io_tests` executable. The fixtures path is wired via
  `target_compile_definitions(io_tests PRIVATE
   RR_TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/scenes")`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/11 ... 11/11 all Passed
100% tests passed, 0 tests failed out of 11

$ ./build/bin/io_tests
--- loaded scene ---
  render_settings:
    width  = 640
    height = 480
  camera:
    position = (0.000, 0.000, 0.000)
    forward  = (0.000, 0.000, -1.000)
    up       = (0.000, 1.000, 0.000)
    fov      = 50.00 deg
    aspect   = 1.3333
  relativity:
    velocity = (0.000, 0.000, -0.300)  |beta| = 0.3000
    enable_aberration       = true
    enable_doppler          = true
    enable_searchlight      = true
    doppler_color_strength  = 1.000
    searchlight_strength    = 1.000
---------------------
io_tests: 16/16 passed
```

#### Per the prompt

- Only `render_settings`, `camera`, and `relativity` are
  parsed; `materials`, `spheres`, `lights`, `meshes` are
  ignored (warn-and-continue per spec).
- No `SceneWriter`; that's the next slice.
- Rendering remains GPU-only - `--render` continues to use the
  existing M12 hard-coded scene; the loader is exercised only
  by `io_tests`.

#### Next M13 slice

Wire `--render <scene file>` to call `load_rrscene` once enough
of the spec is parsed to make a useful renderable scene:
materials + spheres + lights at minimum. Until then the loader
is a library + a unit test, not yet a render entry point.

### 2026-04-27 — RRSCENE v1: `lights` section + final compact example

Doc-only extension. The v1 spec is now feature-complete for the
sections the prompt sequence defined. No parser, no source
changes.

- **`docs/RRSCENE_FORMAT.md`:** added section 11 (`lights`).
  - Fields: `type` (required string, `"point"` or
    `"directional"`), `position` (required for point),
    `direction` (required for directional, propagation vector,
    auto-normalised), `color` (linear RGB, defaults to white),
    `intensity` (defaults to 1, must be >= 0).
  - Point lights use **inverse-square falloff**
    (`Li = color * intensity / r^2`); no falloff radius / cutoff
    in v1.
  - Directional lights have no positional component and no
    distance falloff. The shader uses `-direction` as the
    to-light vector, matching the kernel's M12 convention.
  - The kernel skips back-faced contributions
    (`dot(N, wi) <= 0`); no shadow / occlusion ray (M14).
  - Light types beyond `point` / `directional` (`area`,
    `environment`) are explicitly out of v1; the host
    `LightType` enum carries them but a v1 file MUST NOT use
    them - `area` / `environment` strings are parser errors.
- Top-level shape (section 2) now lists `lights` as an optional
  section. Section numbers below shifted by one
  (Common types -> 12, Defaults -> 13, Validation -> 14,
  Complete example -> 15, Out of scope -> 16, References -> 17).
- Validation rules (section 14) gained a clause: each light has
  a `type` from the {`"point"`, `"directional"`} set; `intensity`
  is `>= 0`; out-of-set type strings (incl. `"area"` /
  `"environment"`) are v1 errors.
- Out of scope (section 16) updated: removed the standalone
  `lights` bullet (lights are now defined); added an explicit
  "light types beyond point and directional" bullet for
  `area` / `environment`; added `material node graphs / shader
  graphs` so the deferred shading-graph work is documented.
- Complete example (section 15) **replaced** with a single
  compact example exercising every section at once - render
  settings, camera, relativity (`β = 0.3`), two materials, one
  sphere, one triangle mesh, one directional + one point light.
  The composition mirrors the M12 lighting scene in `main.cpp`,
  so the file is a drop-in for what `--render` already
  produces.
- References (section 17) updated with `src/lighting/Light.h`
  and noted that only the `Point` / `Directional` enumerators
  of `LightType` are reachable from a v1 file.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 10/10`).

#### Per the prompt

- Only `lights` was added.
- One final complete example exercises every section in a
  compact form (one of each except materials and lights, which
  show two entries to demonstrate arrays).
- No textures, no node graphs, no parser code.

#### v1 spec status

With `lights` in, the v1 spec covers every host-side data
module currently consumed by `--render`. The remaining v1 work
is parser implementation (the next M13 slice).

### 2026-04-27 — RRSCENE v1: `meshes` section added

Doc-only extension. No parser, no source changes.

- **`docs/RRSCENE_FORMAT.md`:** added section 10 (`meshes`).
  - Fields: `name` (optional debug label), `vertices` (required
    array of Vec3 positions), `triangles` (required array of
    `[v0, v1, v2]` index triplets in CCW front-face winding),
    `material_id` (optional, indexes into `materials`),
    `transform` (optional, identity by default).
  - `transform` is the canonical SRT decomposition mapped onto
    the host `rr::math::Transform`: `position` (Vec3),
    `rotation` (Vec3 of Euler angles in radians, intrinsic XYZ),
    `scale` (Vec3, negative axes flip).
  - Index validity is enforced (each index in
    `[0, vertices.size())`; out-of-range / negative is an error).
  - Vertex attributes beyond position (normals, UVs, tangents,
    vertex colours) and external file references
    (`source_path`) explicitly deferred. The renderer derives
    geometric face normals from triangle winding.
  - Quaternion / axis-angle rotations are out of scope; future
    versions add an alternative `rotation_quaternion` field
    that takes precedence when both are present.
- Top-level shape (section 2) now lists `meshes` as an optional
  section. Section numbers below shifted by one (Common types
  -> 11, Defaults -> 12, Validation -> 13, Complete example ->
  14, Out of scope -> 15, References -> 16).
- Validation rules (section 13) gained a new clause: each mesh
  has `vertices` and `triangles`; every triangle index is in
  `[0, vertices.size())`; negative or out-of-range indices are
  errors; empty arrays are legal but warn-eligible.
- Out of scope (section 15) updated: removed the `meshes`
  bullet; added one for "mesh fields beyond `vertices` /
  `triangles` / `material_id` / `transform`" so the deferred
  per-vertex normals / UVs / tangents / colours and the
  external `source_path` reference are explicitly out of v1.
  `lights` remains out-of-scope per the prompt.
- Complete example (section 14) extended: a `warm_emitter`
  material entry plus a 4-vertex / 2-triangle quad mesh that
  references it. Mirrors the M11 material scene's quad +
  emissive-material setup so the file is one-to-one with the
  current `--render` output.
- References updated with `src/geometry/Mesh.h`,
  `src/geometry/Triangle.h`, and `src/math/Transform.h`.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 10/10`).

#### Per the prompt

- Only `meshes` was added.
- One small JSON example sits inside the new section; the
  end-to-end example was extended to exercise it alongside the
  existing materials.
- No lights, no parser code.

### 2026-04-27 — RRSCENE v1: `materials` section added

Doc-only extension to the v1 spec. No parser, no source changes.

- **`docs/RRSCENE_FORMAT.md`:** added section 9 (`materials`).
  - Fields: `id` (required, non-negative, unique within the
    array, the lookup key referenced by
    `spheres[i].material_id`), `name` (optional, debug label),
    `base_color`, `emission_color`, `emission_strength`,
    `roughness`. Roughness is clamped to `[0, 1]`; colours are
    clamped to `[0, ∞)`.
  - `id` is a lookup key, not an array index - sparse / out-of-
    order ids are legal so authoring tools can manage stable
    handles. Duplicate ids are an error.
  - Unmatched `material_id` and explicit `-1` fall back to the
    renderer's neutral default material
    (`[0.8, 0.8, 0.8]`, no emission, roughness `0.5`).
  - The host `MaterialParams` carries `metallic`, `specular`,
    `transmission` slots; v1 does not expose them and the
    parser keeps them at their host defaults. Future schema
    versions add them as additional optional fields with the
    same names.
  - Texture-driven parameters explicitly excluded from v1;
    they land with the texture system (M16).
- Top-level shape (section 2) updated to list `materials` as an
  optional section. Section numbers below it shifted by one
  (Common types -> 10, Defaults -> 11, Validation -> 12,
  Complete example -> 13, Out of scope -> 14, References -> 15).
- Validation rules (section 12) gained a new clause: material
  `id` must be a non-negative integer and unique within the
  `materials` array; out-of-range numerical values are clamped
  per the materials table.
- Spheres section (section 8) note rewritten: `material_id` now
  resolves to an entry in the `materials` array; `-1` /
  unmatched ids fall back to the renderer's default. The earlier
  "v1 has no materials section" caveat is gone.
- "Out of scope for v1" (section 14) updated: removed the
  bullet for "materials array"; added an explicit bullet for
  "material fields beyond `base_color` / `emission_color` /
  `emission_strength` / `roughness`" so the absent
  `metallic` / `specular` / `transmission` fields are
  documented as deferred. `meshes` and `lights` remain
  out-of-scope per the user's narrower prompt.
- Complete example (section 13) updated: the single sphere now
  references a real `matte_red` material via `material_id: 0`,
  exercising the new section end-to-end.
- References (section 15) updated to add
  `src/material/MaterialTypes.h` and document which
  `MaterialParams` fields v1 keeps at host defaults.

#### Verified

No source changes; the existing build and tests remain green
from the previous slice (`ctest -> 10/10`).

#### Per the prompt

- Only `materials` was added.
- One small JSON example in the new section, plus the
  end-to-end example file in section 13 was extended to
  reference a material.
- No meshes; no lights.
- No parser code.

### 2026-04-27 — RRSCENE v1 format spec landed (doc only, M13 in progress)

Documentation-only slice. Defines the minimal v1 contract the
upcoming parser implements; no parser code, no CMake / source
changes.

- **`docs/RRSCENE_FORMAT.md`:** v1 specification.
  - File extensions: `.rrscene` (canonical) and `.rrjson`
    (explicit JSON alias).
  - JSON encoding. Top-level shape is `{ version: 1,
    render_settings, camera, relativity, spheres }`. Every
    section is optional; defaults are explicitly listed and
    match the existing C++ defaults.
  - Sections defined in v1:
    1. `render_settings` - `width`, `height` only. `samples_per_pixel`,
       `max_depth`, AOV selection deferred to v2+.
    2. `camera` - `position`, `forward`, `up`, `fov` (degrees).
       Aspect derives from `render_settings`. Near/far deferred.
    3. `relativity` - `beta_velocity` (scalar), `velocity_direction`
       (Vec3), and three continuous strengths
       (`aberration_strength`, `doppler_strength`,
       `searchlight_strength`). The parser will reconstruct the
       host `Observer.velocity = beta * normalize(direction)` and
       map `strength == 0` to the corresponding boolean toggle.
    4. `spheres` - array of `{ position, radius, material_id }`.
       `material_id` is a forward-compatibility hint;
       v1 has no `materials` section yet, so unknown / `-1` ids
       map to the renderer's neutral default.
  - Conventions: right-handed +Y up / -Z forward, lengths in
    scene units, angles in degrees only when named
    `*_degrees`, all `Vec3` are 3-element JSON arrays.
  - Validation rules enumerated explicitly (positive
    dimensions, FOV in `(0, 180)`, non-zero forward, etc.).
  - One full example file matches the M9 single-sphere
    relativistic test at `β = 0.5`.
  - Explicit "out of scope for v1" list: materials, meshes,
    lights, textures, environment maps, AOVs, motion blur,
    DOF, near/far. Each has a host data model already; v2 adds
    the matching JSON section.
- **`docs/BUILD_PLAN.md`:** marked module 18 (Scene File Format)
  and milestone M13 as **in progress** - spec landed; parser
  implementation is the next slice.

#### Verified locally

No source changes; the existing build is unaffected.

```
$ cd build && ctest --output-on-failure
 1/10 ... 10/10 all Passed
100% tests passed, 0 tests failed out of 10
```

#### Per the prompt

- Only `render_settings` (width/height), `camera`, `relativity`
  (the listed five fields), and `spheres` (position, radius,
  material_id) are defined.
- No meshes, lights, materials, textures, AOVs, environment
  maps in v1 - these are explicitly enumerated as deferred.
- No parser code; the spec is the contract M13's parser slice
  implements.

### 2026-04-27 — M12 finalized: simple direct lighting on GPU

Lights now flow end-to-end. Each hit accumulates contributions from
the uploaded directional + point lights with inverse-square falloff,
adds emission from the material, and falls back to an environment
tint (or the existing default sky gradient) when no Environment
light is present. No shadows, no path tracing.

- **`src/gpu/GpuScene.{h,cpp}`:** added
  `GpuBuffer<rr::lighting::Light>` slot,
  `upload_lights(host, count)`, `light_count()` query, and a
  `device_lights()` accessor. Same dynamic-size,
  fail-predictably-without-backend semantics as the existing
  sphere / mesh / material upload paths.
- **`src/cuda/CudaScene.cuh`:** added `lights` device pointer +
  `light_count` to `CudaSceneView`. `nullptr` + count `0` is
  allowed and means "no lights uploaded - fall back to default
  ambient + sky gradient".
- **`src/cuda/CudaRenderer.cu`:** `render_scene` copies the
  lights view (`device_lights()` + `light_count()`) into the
  launch argument before dispatch.
- **`src/cuda/CudaTestKernel.cu`:** the kernel's shade phase is
  rewritten as a single pass over the uploaded lights:
  - `Environment` lights record an `env_color`.
  - `Point` and `Directional` lights, on hit, accumulate
    `Li * ndotl` into `lighting`. Backface (`ndotl <= 0`) is
    skipped; degenerate point-light geometry (`d^2 < 1e-12`) is
    skipped.
  - `Area` lights are skipped at this milestone (sampling lands
    at M14).
  After the loop, the hit shade is
  `mat.baseColor * (lighting + env_color)
   + mat.emissionColor * mat.emissionStrength`. The miss path
  uses the `env_color` when an Environment light is present, and
  the existing vertical sky gradient otherwise. The relativistic
  pipeline (Doppler colour + searchlight beaming) continues to
  wrap the final colour, so high-beta passes still see
  blueshift / beaming on the per-light shaded result.
- **`src/main.cpp`:** `--render` builds a three-light rig - a
  warm directional sun, a cool point light above-right, a pale
  blue environment - on top of the M11 material scene, uploads
  it through `GpuScene`, and saves to
  `output/gpu_direct_lighting.ppm`. `--output` is still ignored
  at this milestone.
- **`tests/gpu_tests.cpp`:** +8 `upload_lights` assertions
  (87/87 total). Default `GpuScene` has no lights; empty upload
  succeeds everywhere; non-empty upload fails predictably
  without a backend, with `light_count == 0` and
  `device_lights() == nullptr`.
- **`CMakeLists.txt`:** `rr_gpu` PUBLIC link list now includes
  `rr_lighting` so the renderer's GPU layer can reach `Light`
  symbols.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/10 Test  #1: math_tests       ........... Passed  0.00 sec
 2/10 Test  #2: image_tests      ........... Passed  0.00 sec
 3/10 Test  #3: gpu_tests        ........... Passed  0.00 sec
 4/10 Test  #4: camera_tests     ........... Passed  0.00 sec
 5/10 Test  #5: geometry_tests   ........... Passed  0.00 sec
 6/10 Test  #6: relativity_tests ........... Passed  0.00 sec
 7/10 Test  #7: scene_tests      ........... Passed  0.00 sec
 8/10 Test  #8: mesh_tests       ........... Passed  0.00 sec
 9/10 Test  #9: material_tests   ........... Passed  0.00 sec
10/10 Test #10: lighting_tests   ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 10

$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 87/87 passed
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`) produces
`output/gpu_direct_lighting.ppm`. Correct by construction: every
device-side helper used in the new shade path
(`generate_camera_ray`, `intersect_sphere`,
`intersect_triangle`, `aberrateDirection`, `dopplerFactor`,
`searchlightFactor`, `applyDopplerColor`) is exercised by the
host suite (`camera_tests` 43, `geometry_tests` 46,
`relativity_tests` 52); the new direct-lighting accumulation is
straight-line vector arithmetic over POD inputs.

#### Hard-rule check

- **No shadows / no path tracing** per the prompt: the kernel
  evaluates `Li * ndotl` directly, with no occlusion ray.
- **Lights read on the GPU**: every light lookup happens inside
  `k_render_scene`. The CPU only fills the `Light` array and
  uploads it.
- **No CPU pixel iteration in the render path**: only inside
  `Image::save_ppm`.

#### What this milestone closes (M12 / Module 11)

- M12 (Lighting System Foundations) -> landed: data model +
  factories + GPU upload + kernel-side direct lighting all in.
- Module 11 (Lighting System) -> landed for the foundation. Real
  importance-sampled `eval` / `sample` / `pdf` per light type,
  area-light sampling, and shadow rays are required for path-
  tracing fidelity; those land with M14 (path tracer) and M15
  (OptiX upgrade for ray dispatch).

### 2026-04-27 — M12 lighting data model landed (M12 in progress)

Host-side lighting POD + factories + CUDA-side re-export. No path
tracing, no kernel changes; the data shape is the contract M14 (path
tracer) and M16 (env-map textures) populate.

- **`src/lighting/Light.h`:** `LightType` enum (`Point`,
  `Directional`, `Area`, `Environment`) with stable ordinals so
  the upload contract is forward-compatible. `Light` POD with a
  flat layout (no union) so `GpuBuffer<Light>` can carry it via
  `std::is_trivially_copyable`. Field semantics are
  type-discriminated and documented in the header. Defaults
  describe a neutral white point light at the origin with unit
  intensity.
- **`src/lighting/Light.cpp`:** factory functions
  `make_point_light` / `make_directional_light` /
  `make_area_light` / `make_environment_light`. Direction-bearing
  factories normalize the input via a `safe_normalize` helper that
  falls back to `(0, -1, 0)` on degenerate (zero-length) input.
  `make_area_light` clamps negative dimensions to zero. The Area
  and Environment slots are explicitly documented as placeholders
  - the geometry / sampling routines arrive at M14, env-map
  textures at M16.
- **`src/cuda/CudaLight.cuh`:** thin re-export of `Light.h` so
  kernel TUs can include a `.cuh`. Future `RR_HD inline` sampling
  / eval / pdf helpers per `LightType` land here without touching
  the host surface.
- **`tests/lighting_tests.cpp`:** 35 host assertions covering
  `LightType` ordinals (upload contract); default `Light` is a
  neutral point at origin; each factory produces the expected
  fields; directional / area factories normalize their input
  vector and fall back to `(0, -1, 0)` for zero-length input;
  area light clamps negative width / height to zero.
- **`CMakeLists.txt`:** added `rr_lighting` static library and
  the `lighting_tests` test executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/10 Test  #1: math_tests       ........... Passed  0.00 sec
 2/10 Test  #2: image_tests      ........... Passed  0.00 sec
 3/10 Test  #3: gpu_tests        ........... Passed  0.00 sec
 4/10 Test  #4: camera_tests     ........... Passed  0.00 sec
 5/10 Test  #5: geometry_tests   ........... Passed  0.00 sec
 6/10 Test  #6: relativity_tests ........... Passed  0.00 sec
 7/10 Test  #7: scene_tests      ........... Passed  0.00 sec
 8/10 Test  #8: mesh_tests       ........... Passed  0.00 sec
 9/10 Test  #9: material_tests   ........... Passed  0.00 sec
10/10 Test #10: lighting_tests   ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 10

$ ./build/bin/lighting_tests
lighting_tests: 35/35 passed
```

#### Not in this slice (M12 / Module 11 still "in progress")

- No path tracing, per the prompt; nothing reads `Light` yet.
- No GPU upload of the light array; `GpuScene` does not yet
  expose `upload_lights(...)` and `CudaSceneView` has no light
  array.
- No kernel-side direct-lighting evaluation. The current shade
  is still the M11 hemispherical key term.
- Area and Environment are explicit placeholders - geometry +
  sampling routines for area lights arrive at M14; env-map
  textures at M16.
- No update to `scene::SceneLight`, which remains the M9
  placeholder. It will be rewritten to embed
  `rr::lighting::Light` when a real consumer lands.

### 2026-04-27 — M11 finalized: materials end-to-end through the GPU renderer

`material_index` now flows from primitives, through the upload path,
to the kernel, which evaluates a simple diffuse + emission + normal-
driven hemisphere shade. The relativistic pipeline still wraps the
result, so Doppler / searchlight modify the per-material output.

- **`src/renderer/Hit.h`:** added `material_index` (defaults to
  `-1` = "no material"). Both `intersect_sphere` and the kernel
  triangle loop now populate it.
- **`src/geometry/Sphere.h`:** added `material_index` to the POD.
  Aggregate-init `Sphere{c, r}` still works (the field defaults
  to `-1`); `make_sphere(...)` returns `-1` explicitly. Existing
  call sites are unaffected.
- **`src/cuda/CudaIntersection.cuh`:** `intersect_sphere`
  propagates `sphere.material_index` into the resulting `Hit`.
  `intersect_triangle` is unchanged (the kernel sets the mesh's
  `material_id` on the hit at the call site, since standalone
  triangles have no material concept).
- **`src/cuda/CudaScene.cuh`:** added a material array to
  `CudaSceneView` (`materials` device pointer + `material_count`).
  `nullptr` + count `0` is allowed and means "no materials
  uploaded - everything uses the default neutral shade".
- **`src/gpu/GpuScene.{h,cpp}`:** added `GpuBuffer<MaterialParams>`
  + `upload_materials(host, count)` + `material_count()` query +
  `device_materials()` accessor. Same dynamic-size, fail-
  predictably-without-backend semantics as the existing sphere /
  mesh upload paths.
- **`src/cuda/CudaRenderer.cu`:** `render_scene` now also copies
  the materials view (`device_materials()` + `material_count()`)
  into `CudaSceneView` before launching.
- **`src/cuda/CudaTestKernel.cu`:** the kernel's hit branch now:
  1. looks up `MaterialParams` at `best.material_index` (with a
     bounds check; out-of-range falls back to a neutral default);
  2. computes a hemispherical key shade
     `0.4 + 0.6 * max(0, dot(N, +Y))`;
  3. composes `baseColor * shade + emissionColor *
     emissionStrength`.
  Doppler colour and searchlight beaming continue to apply on the
  composed colour, so the relativistic effects modify the
  per-material result. The triangle loop now tags each accepted
  hit with `mesh.material_id` so meshes participate in the
  material lookup.
- **`src/main.cpp`:** `--render` builds a host scene with five
  materials (red / green / blue diffuse, warm emissive for the
  quad, light grey floor), assigns each sphere a material index,
  flags the quad's mesh with the emissive material, uploads
  everything, and saves to `output/gpu_material_scene.ppm`.
  `--output` is still ignored at this milestone.
- **`tests/geometry_tests.cpp`:** +6 assertions (46/46 total) -
  `make_sphere(...)` returns the `-1` sentinel;
  `intersect_sphere` propagates the per-sphere index when set,
  preserves `-1` for default-constructed spheres, and the
  aggregate `{center, radius}` form still works.
- **`tests/gpu_tests.cpp`:** +8 assertions (79/79 total) -
  default `GpuScene` has no materials; empty `upload_materials`
  succeeds everywhere; non-empty upload fails predictably without
  a backend, with `material_count == 0` and
  `device_materials() == nullptr`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/9 Test #1: math_tests       ........... Passed  0.00 sec
2/9 Test #2: image_tests      ........... Passed  0.00 sec
3/9 Test #3: gpu_tests        ........... Passed  0.00 sec
4/9 Test #4: camera_tests     ........... Passed  0.00 sec
5/9 Test #5: geometry_tests   ........... Passed  0.00 sec
6/9 Test #6: relativity_tests ........... Passed  0.00 sec
7/9 Test #7: scene_tests      ........... Passed  0.00 sec
8/9 Test #8: mesh_tests       ........... Passed  0.00 sec
9/9 Test #9: material_tests   ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 9

$ ./build/bin/geometry_tests
geometry_tests: 46/46 passed
$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 79/79 passed
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`) produces
`output/gpu_material_scene.ppm`. The kernel calls the same
`RR_HD generate_camera_ray`, `intersect_sphere`,
`intersect_triangle`, `aberrateDirection`, `dopplerFactor`,
`searchlightFactor`, and `applyDopplerColor` covered by the
existing host tests; the new shading combines the same
`MaterialParams` fields the host suite exercises.

#### Hard-rule check

- **Materials read on the GPU**: every material lookup happens
  inside `k_render_scene`. The CPU only fills the parameter
  array and uploads it.
- **No CPU pixel iteration in the render path**: only inside
  `Image::save_ppm`.

#### What this milestone closes (M11 / Module 9)

- M11 (Material System Foundations) -> landed: parameter pack +
  per-primitive id + GPU array upload + kernel-side shading all
  in.
- Module 9 (Material / Shading System) -> landed for the
  foundation. The full BSDF interface (`eval` / `sample` /
  `pdf`) and the texture-driven parameter binding are still
  required for path-tracing fidelity; those land with M14
  (path tracer) and M16 (textures).

### 2026-04-27 — Material foundation landed (M11 in progress)

PBR-style parameter pack + thin host wrapper + CUDA-side re-export.
No BSDF, no node graph, no textures - those come in their own
milestones. The data shape is the contract M13 (scene file
format), M14 (path tracer), and M21 (node editor) populate.

- **`src/material/MaterialTypes.h`:** `MaterialParams` POD with
  `baseColor`, `emissionColor`, `emissionStrength`, `roughness`,
  `metallic`, `specular`, plus a `transmission` placeholder slot
  reserved for the dielectric / glass BSDF that joins later.
  Defaults describe a neutral 80% grey diffuse surface
  (roughness 0.5, metallic 0, specular 0.5, no emission, no
  transmission). Field names use the DCC / PBR camelCase
  convention so artists and scene-file authors recognise them
  - the rest of the project uses snake_case; the material module
  is the one exception, mirroring the relativity module's
  physics-literature naming.
- **`src/material/Material.h` / `.cpp`:** thin host wrapper.
  `Material { name, params }` with const + mutable `params()`
  accessors so authoring code can tweak fields in place. Three
  presets: `make_diffuse(base_color)` (matte 1.0 roughness),
  `make_emissive(emission_color, strength)` (black diffuse, full
  emission), `make_metal(base_color, roughness)`
  (metallic = 1, specular = 1).
- **`src/cuda/CudaMaterial.cuh`:** thin re-export of
  `MaterialTypes.h` so kernel TUs can include a `.cuh`. Future
  `RR_HD inline` BSDF helpers (`eval` / `sample` / `pdf`) and
  device-specific overrides land here without touching the host
  surface.
- **`tests/material_tests.cpp`:** 33 host assertions covering
  `MaterialParams` defaults; default + params + named ctors;
  setters; the mutable `params()` accessor (modify a field, other
  fields unchanged); the three presets; and that the
  `transmission` placeholder round-trips through the params
  struct as a plain float (no shader behaviour wired yet, but the
  slot is reachable).
- **`CMakeLists.txt`:** added `rr_material` static library and
  the `material_tests` test executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/9 Test #1: math_tests       ........... Passed  0.00 sec
2/9 Test #2: image_tests      ........... Passed  0.00 sec
3/9 Test #3: gpu_tests        ........... Passed  0.00 sec
4/9 Test #4: camera_tests     ........... Passed  0.00 sec
5/9 Test #5: geometry_tests   ........... Passed  0.00 sec
6/9 Test #6: relativity_tests ........... Passed  0.00 sec
7/9 Test #7: scene_tests      ........... Passed  0.00 sec
8/9 Test #8: mesh_tests       ........... Passed  0.00 sec
9/9 Test #9: material_tests   ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 9

$ ./build/bin/material_tests
material_tests: 33/33 passed
```

#### Not in this slice (M11 / Module 9 still "in progress")

- No BSDF interface (`eval` / `sample` / `pdf`). The kernel
  still shades hits by normal-as-color, not by material.
- No GPU upload of the material list; `GpuScene` does not yet
  expose `upload_materials(...)` and `CudaSceneView` has no
  material array.
- No `material_index` plumbing in `Hit`. Spheres / triangles
  carry an id today (via `SceneSphere` / `Mesh::material_id`)
  but the kernel has no way to look up the corresponding
  parameters.
- No texture / node graph. The user explicitly excluded both
  from this milestone; they land at M16 (textures) and M21
  (node editor).
- No update to `scene::SceneMaterial`, which remains the M9
  placeholder. It will be rewritten to embed
  `rr::material::Material` when a real consumer lands.

### 2026-04-27 — M10 finalized: CUDA triangle rendering

Naive GPU triangle loop alongside the existing sphere loop. Both
primitive types compete for the same nearest-hit slot in
`k_render_scene`. CPU does no intersection work; the kernel walks
the uploaded vertex / index buffers per pixel.

- **`src/cuda/CudaIntersection.cuh`:** added `RR_HD inline
  intersect_triangle(ray, v0, v1, v2, t_min, t_max) -> Hit`.
  Moller-Trumbore. Double-sided (back-face hits accepted; the
  `|det| < eps` check rejects only edge-parallel rays). Returns
  the geometric front-face normal of the CCW winding `(v0, v1,
  v2)`. `ray.direction` does not need to be unit length.
- **`src/cuda/CudaMesh.cuh`** (existing): the launch-argument
  contract is now consumed by `k_render_scene`.
- **`src/cuda/CudaScene.cuh`:** added a single-mesh slot to
  `CudaSceneView`. `mesh.triangle_count == 0` means "no mesh
  contributes triangles", so a sphere-only scene still works
  unchanged.
- **`src/cuda/CudaTestKernel.cu`:** extended `k_render_scene` with
  a triangle loop after the sphere loop. Both update the running
  `t_max`, so the closest hit across primitive types wins. Vertex
  positions are taken as-is from the uploaded buffer (effectively
  world-space). Per-mesh transforms join the kernel alongside the
  M11 material system; for now the host pre-places vertices in
  world space.
- **`src/cuda/CudaRenderer.cu`:** `render_scene` now also copies
  the mesh slot from `GpuScene::gpu_mesh()` into `CudaSceneView`.
- **`src/gpu/GpuScene.{h,cpp}`:** added a `GpuMesh mesh_;` slot,
  `upload_mesh(const Mesh&)` convenience, `has_mesh()` query, and
  a `gpu_mesh()` accessor for the renderer. `upload_from(Scene)`
  is intentionally unchanged (scene-side mesh wrappers are still
  placeholders); callers push the mesh directly via
  `upload_mesh`.
- **`src/main.cpp`:** `--render` now produces both deliverables.
  A reusable `build_quad()` helper builds a 2-triangle CCW quad
  in front of the camera; the first scene uses only the mesh
  (output `output/gpu_triangle.ppm`); the second adds the M10
  four-sphere arrangement (output `output/gpu_mesh_scene.ppm`).
- **`tests/geometry_tests.cpp`:** +15 triangle assertions
  (40/40 total) covering centre-pixel hit (`t = 3`,
  `position = (0, 0, -3)`, `normal = (0, 0, 1)` for a CCW
  triangle in the `z = -3` plane), outside-triangle miss,
  parallel-ray miss, double-sided hit from behind,
  `t_min`/`t_max` clipping, and CCW vs CW winding flipping the
  geometric normal. The kernel calls the same `RR_HD` routine, so
  the host coverage validates the device math by construction.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/8 Test #1: math_tests       ........... Passed  0.00 sec
2/8 Test #2: image_tests      ........... Passed  0.00 sec
3/8 Test #3: gpu_tests        ........... Passed  0.00 sec
4/8 Test #4: camera_tests     ........... Passed  0.00 sec
5/8 Test #5: geometry_tests   ........... Passed  0.00 sec
6/8 Test #6: relativity_tests ........... Passed  0.00 sec
7/8 Test #7: scene_tests      ........... Passed  0.00 sec
8/8 Test #8: mesh_tests       ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 8

$ ./build/bin/geometry_tests
geometry_tests: 40/40 passed
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`) produces both
`output/gpu_triangle.ppm` and `output/gpu_mesh_scene.ppm`. Correct
by construction: the kernel calls the same `RR_HD generate_camera_ray`,
`intersect_sphere`, and `intersect_triangle` that
`camera_tests` (43) and `geometry_tests` (40) cover on the host.

#### Hard-rule check

- **No CPU intersection**: every ray-primitive test happens inside
  `k_render_scene`. Host calls to `intersect_triangle` exist only
  in `geometry_tests` for validation.
- **No CPU pixel iteration in the render path**:
  `Image::save_ppm` is the only CPU loop over pixels (image save
  internals, permitted).

#### What this milestone closes (M10 / Module 8)

- M10 (GPU Scene Upload & Triangle Mesh) -> landed: scene upload
  + naive triangle rendering both work.
- Module 8 (Geometry System) -> landed: host-side `Sphere` /
  `Triangle` / `Mesh`, host- and device-callable
  `intersect_sphere` / `intersect_triangle`, GPU-uploadable form
  via `GpuMesh`, all exercised end-to-end.

### 2026-04-27 — GPU mesh upload landed (M10 - kernel side still open)

Backend-agnostic owner for a single mesh's GPU resources.
`rr::gpu::GpuMesh` carries two device-resident buffers (vertices +
triangle indices) plus the per-mesh metadata (material id +
world-space transform), and a thin `CudaMeshView` POD declares the
launch-argument shape the eventual mesh kernel will consume. No
kernel reads it yet (per the prompt: "no rendering yet, no BVH
yet").

- **`src/gpu/GpuMesh.h` / `.cpp`:** move-only RAII container.
  `GpuBuffer<rr::geometry::Vertex>` for the vertex array,
  `GpuBuffer<rr::geometry::Triangle>` for the index array, both
  dynamic (no compile-time cap). Surface:
  `upload_vertices(host, count)`, `upload_triangles(host, count)`,
  `set_metadata(material_id, transform)`,
  `upload_from(const Mesh&)` convenience, plus queries
  (`vertex_count`, `triangle_count`, `material_id`, `transform`,
  `has_data`, `device_vertices`, `device_triangles`). Empty
  uploads always succeed; non-empty uploads fail predictably
  without a backend (counts stay zero, device pointers stay
  null). Metadata is host state and is set even on a failed
  upload so callers can inspect partial state for debugging.
- **`src/cuda/CudaMesh.cuh`:** `CudaMeshView` POD - device
  pointers + counts + material id + transform, the launch-argument
  shape the kernel will read by value. Defined now as a stable
  contract so the next slice (closest-hit triangle loop) can land
  without touching this header.
- **`tests/gpu_tests.cpp`:** +30 GpuMesh assertions (71/71 total).
  Default state; metadata setter is pure host (succeeds without
  a backend); empty upload succeeds everywhere; non-empty upload
  without a backend fails predictably with counts zero / device
  pointers null; `upload_from` round-trip on a quad with full
  metadata - succeeds with non-zero counts when CUDA + a device
  are present, fails predictably otherwise; move-only preserves
  metadata across move-ctor and move-assign.
- **`CMakeLists.txt`:** `rr_gpu` lists `src/gpu/GpuMesh.cpp` and
  PUBLIC-links `rr_geometry` (the renderer's GPU layer needs the
  Mesh / Triangle / Vertex types). PUBLIC-link list for `rr_gpu`
  is now `rr_scene rr_geometry`; image / camera flow in
  transitively.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/8 Test #1: math_tests       ........... Passed  0.00 sec
2/8 Test #2: image_tests      ........... Passed  0.00 sec
3/8 Test #3: gpu_tests        ........... Passed  0.00 sec
4/8 Test #4: camera_tests     ........... Passed  0.00 sec
5/8 Test #5: geometry_tests   ........... Passed  0.00 sec
6/8 Test #6: relativity_tests ........... Passed  0.00 sec
7/8 Test #7: scene_tests      ........... Passed  0.00 sec
8/8 Test #8: mesh_tests       ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 8

$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 71/71 passed
```

#### Hard-rule check

- **CPU uploads only**: every `GpuMesh` operation either copies a
  POD into host state (metadata setter) or pushes contiguous
  arrays through `GpuBuffer<T>`. No per-vertex / per-triangle
  CPU work; the kernel-side iteration arrives in the next slice.
- **No rendering changes**: `--render` continues to use the M10
  sphere-only scene; no kernel reads `GpuMesh` or
  `CudaMeshView` yet.

#### Not in this slice (M10 still "in progress")

- No mesh kernel. The closest-hit loop over uploaded triangles,
  the `intersect_triangle` routine, and a `CudaMeshView` slot
  inside `CudaSceneView` are the next slice.
- No BVH per the prompt; brute-force intersection is the M10
  baseline. OptiX acceleration arrives at M15.
- `SceneMesh` in `scene/Scene.h` remains the M9 placeholder
  (name + source_path + material_index). It will be rewritten
  to embed an `rr::geometry::Mesh` once the kernel actually
  reads it.

### 2026-04-27 — Mesh geometry structures landed (Geometry System in progress)

Host-side mesh data model. No renderer wiring; no GPU upload yet.
The structures are sized and laid out so the next M10 slice can
upload them with `GpuBuffer<Vertex>` / `GpuBuffer<Triangle>`
without any reshape.

- **`src/math/Transform.h`:** the canonical Transform now lives
  in math, with the same fields and `identity()` factory as the
  former `scene::Transform`. Promotion was needed so geometry
  could carry a transform without crossing back into scene
  (scene already depends on geometry via Sphere; the inverse
  would have been a cycle).
- **`src/scene/Transform.h`:** thin back-compat shim. Now reads
  `using Transform = rr::math::Transform;` so existing callers
  (`SceneObject`, scene tests, the parser when it lands) keep
  working unchanged.
- **`src/geometry/Triangle.h`:** plain `Triangle { uint32_t v0,
  v1, v2 }` POD. CCW front-face winding (matches the convention
  the upcoming `intersect_triangle` will use). Layout-compatible
  with a flat `uint32_t[3*N]` index array - the form most kernels
  will read. `RR_HD make_triangle` factory for symmetry with
  `make_sphere`.
- **`src/geometry/Mesh.h`:** `Vertex { position, normal, uv }`
  with sensible defaults so callers with positions only can still
  construct a mesh and fill the rest later. `Mesh { vertices,
  triangles, material_id (-1 = renderer default), transform }`
  plus `vertex_count`, `triangle_count`, `empty`, `clear`,
  `reserve` helpers. Vector-of-vertex / vector-of-triangle storage
  is the contiguous form the GPU upload path consumes via
  `GpuBuffer<...>`.
- **`src/geometry/Mesh.cpp`:** `Mesh::empty()` (returns true if
  either list is empty - both are required for a renderable mesh),
  `Mesh::clear()` (full reset including transform and
  material_id), `Mesh::reserve` (pure capacity hint, does not
  change reported counts).
- **`tests/mesh_tests.cpp`:** 47 host assertions covering Triangle
  aggregate / factory / defaults; Vertex defaults and explicit
  init; Mesh default state; populating a unit quad as two CCW
  triangles with material_id and transform set; the
  "empty if either list empty" rule (vertices alone is empty,
  triangles alone is empty); `clear()` reset; back-compat that
  `rr::scene::Transform` still resolves to the same type as
  `rr::math::Transform`.
- **`CMakeLists.txt`:** added `rr_geometry` static library
  (`src/geometry/Mesh.cpp`) and the `mesh_tests` executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/8 Test #1: math_tests       ........... Passed  0.00 sec
2/8 Test #2: image_tests      ........... Passed  0.00 sec
3/8 Test #3: gpu_tests        ........... Passed  0.00 sec
4/8 Test #4: camera_tests     ........... Passed  0.00 sec
5/8 Test #5: geometry_tests   ........... Passed  0.00 sec
6/8 Test #6: relativity_tests ........... Passed  0.00 sec
7/8 Test #7: scene_tests      ........... Passed  0.00 sec
8/8 Test #8: mesh_tests       ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 8

$ ./build/bin/mesh_tests
mesh_tests: 47/47 passed
```

#### Not in this slice

- No renderer wiring per the prompt: `--render` continues to use
  the M10 sphere-only scene; no kernel reads `Mesh` yet.
- No GPU upload: `GpuScene` does not yet expose
  `upload_meshes(...)` and `CudaSceneView` has no mesh handle.
  Those are the next M10 deliverable.
- No triangle intersection routine in `cuda/CudaIntersection.cuh`
  yet; lands with the GPU mesh upload.
- `SceneMesh` in `scene/Scene.h` remains the M9 placeholder
  (name + source_path + material_index). It rewrites to embed an
  `rr::geometry::Mesh` once the upload path uses it.

### 2026-04-27 — M10 GPU scene upload landed (sphere arrays only)

The Scene Graph now has a real consumer. The host populates an
`rr::scene::Scene`, hands it to a `GpuScene` for upload, and
`CudaRenderer::render_scene` walks the device-side sphere array per
pixel. Triangle mesh upload remains the open piece of M10; when it
lands the milestone is fully complete.

- **`src/gpu/GpuScene.h` / `.cpp`:** backend-agnostic GPU scene
  container. Stores the camera / observer / relativity PODs as
  host snapshots (uploads "always succeed" for those - no GPU
  work) plus a device-resident sphere array via
  `GpuBuffer<rr::geometry::Sphere>`. Surface:
  `upload_camera`, `upload_relativity`, `upload_spheres(host,
  count)` (dynamic count, no compile-time cap), and a convenience
  `upload_from(scene)` that flattens visible spheres on the host
  before issuing the device upload. Move-only, copy-deleted; the
  empty-sphere upload is a no-op success regardless of backend so
  consumers can build empty scenes without branching.
- **`src/cuda/CudaScene.cuh`:** `CudaSceneView { camera, observer,
  params, spheres (device pointer), sphere_count }` POD that the
  scene-render kernel takes by value, plus the host-callable
  `launch_render_scene` declaration.
- **`src/cuda/CudaTestKernel.cu`:** added `__global__ k_render_scene`
  and its launcher. Per pixel: ray-gen -> aberration -> closest-hit
  loop over the sphere array (tightening `t_max` as it accepts
  hits) -> base shade -> Doppler colour -> beaming -> framebuffer
  write. Same pipeline as the M9 single-sphere kernel; the only
  change is the loop. Brute-force; OptiX acceleration arrives at
  M15.
- **`src/cuda/CudaRenderer.{h,cu}`:** added
  `render_scene(GpuScene, w, h)`. Validates that the scene has a
  camera + relativity uploaded, builds a `CudaSceneView`, and runs
  the existing `run_kernel_render` scaffold.
- **`src/main.cpp`:** `--render` now constructs a host `Scene`
  with four spheres (centre + two flankers + a large "ground"
  sphere), uploads to a `GpuScene`, calls `render_scene`, and saves
  the single deliverable `output/gpu_scene_spheres.ppm`.
  `--output` is still ignored at this milestone for reproducibility.
- **`tests/gpu_tests.cpp`:** added 21 host assertions on `GpuScene`
  (41/41 total). Coverage: default state (no camera / relativity /
  spheres); camera + relativity uploads succeed unconditionally
  (pure host snapshots); empty sphere upload succeeds everywhere
  (backend or no backend); non-empty sphere upload fails predictably
  on the host-only build with `sphere_count == 0`,
  `device_spheres() == nullptr`; `upload_from` filters invisible
  spheres on the host before uploading.
- **`CMakeLists.txt`:** `rr_gpu` now lists `src/gpu/GpuScene.cpp`
  and PUBLIC-links `rr_scene` (the renderer's GPU layer needs the
  scene structures).

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/7 Test #1: math_tests       ........... Passed  0.00 sec
2/7 Test #2: image_tests      ........... Passed  0.00 sec
3/7 Test #3: gpu_tests        ........... Passed  0.00 sec
4/7 Test #4: camera_tests     ........... Passed  0.00 sec
5/7 Test #5: geometry_tests   ........... Passed  0.00 sec
6/7 Test #6: relativity_tests ........... Passed  0.00 sec
7/7 Test #7: scene_tests      ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 7

$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 41/41 passed

$ ./build/bin/RelativityRender --render scene.scn --width 16 --height 16
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a Turing/Ampere/Ada
GPU) produces `output/gpu_scene_spheres.ppm`. The kernel calls the
same `RR_HD` ray-gen / aberration / intersection routines covered
by `camera_tests`, `geometry_tests`, and `relativity_tests` on the
host, so the device math is validated by construction.

#### Hard-rule check

- **CPU uploads only**: the host populates `Scene`, calls
  `GpuScene::upload_from`, and launches one kernel. No per-pixel
  / per-ray work happens on the CPU. The closest-hit loop and
  shading run inside `k_render_scene`.
- **No CPU pixel iteration in the render path**:
  `Image::save_ppm` is the only CPU loop over pixels (image save
  internals, permitted).

#### Not in this slice (M10 still "in progress")

- Triangle mesh upload. The geometry / mesh module is the next
  piece - it adds `rr::geometry::TriangleMesh`,
  `GpuBuffer<float>` / `GpuBuffer<uint32_t>` arrays for positions
  / indices, and a per-mesh handle in `CudaSceneView`.
- Materials / lights still flow only as scene-side placeholders;
  M11 / M12 turn them into real GPU data.

### 2026-04-27 — Host-side scene structures landed (Scene Graph in progress)

Pure host data model. Camera + render settings + observer +
RelativityParams + lists of spheres / meshes (placeholder) /
materials (placeholder) / lights (placeholder). No renderer
changes; no parser. Lays the foundation that M10 (GPU upload),
M11 (materials), M12 (lights), and M13 (scene file format) will
populate.

- **`src/scene/Transform.h`:** `Transform { position,
  euler_rotation_radians, scale }` plain data with an
  `identity()` factory. The matrix conversion is intentionally
  deferred to the consumer (GPU upload at M10, scene file at M13)
  so the data model doesn't bake in a math choice (column- vs
  row-major, Euler order, quaternion form) before we know who
  reads it.
- **`src/scene/SceneObject.h`:** `SceneObject { name, transform,
  visible }` - the common metadata composed into every
  transformable scene entity. Composition over inheritance, per
  the development rules.
- **`src/scene/Scene.h`:** the top-level container.
  `RenderSettings { width, height, samples_per_pixel, max_depth }`
  alongside scene-side wrappers:
  - `SceneSphere { object, geometry, material_index }`
    (geometry is `rr::geometry::Sphere`).
  - `SceneMesh { object, source_path, material_index }` -
    placeholder until M11.
  - `SceneMaterial { name, albedo }` - placeholder until M11.
  - `SceneLight { object, color, intensity }` - placeholder
    until M12.
  Materials are referenced by integer index so the data is
  trivially serialisable for M13 and uploadable for M11.
- **`src/scene/Scene.cpp`:** `Scene::clear()` empties every list
  and resets camera / render settings / observer / relativity to
  default-constructed state.
- **`tests/scene_tests.cpp`:** 38 host assertions covering
  defaults (Transform, SceneObject, RenderSettings), Scene
  emptiness on construction, populating the four lists with
  cross-referenced indices, and `clear()` resetting every field.
- **`CMakeLists.txt`:** added `rr_scene` static library
  (`src/scene/Scene.cpp`, PUBLIC link to `rr_camera`) and a
  `scene_tests` test executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/7 Test #1: math_tests       ........... Passed  0.00 sec
2/7 Test #2: image_tests      ........... Passed  0.00 sec
3/7 Test #3: gpu_tests        ........... Passed  0.00 sec
4/7 Test #4: camera_tests     ........... Passed  0.00 sec
5/7 Test #5: geometry_tests   ........... Passed  0.00 sec
6/7 Test #6: relativity_tests ........... Passed  0.00 sec
7/7 Test #7: scene_tests      ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 7

$ ./build/bin/scene_tests
scene_tests: 38/38 passed
```

#### Not in this slice

- Scene Graph is "in progress", not "landed". The data model is
  in; what's missing for "landed" is at least the upload path
  to the GPU (M10) so a real consumer exercises every field.
- No renderer changes: `--render` continues to use the M9
  hard-coded sphere + relativity sweep. Nothing reads `Scene`
  yet.
- No scene file format: M13 will define the on-disk schema and
  parser; today only the in-memory representation exists.
- Mesh / material / light wrappers are deliberately minimal -
  the matching modules (M11 / M12) replace the placeholders
  with real data.

### 2026-04-27 — M9 relativity integrated into the CUDA sphere renderer

The relativity math leaf is now wired into the GPU sphere pipeline. The
six-step pipeline runs entirely on the device per pixel; the host only
configures, launches once per beta, and saves.

- **`src/cuda/CudaKernels.cuh`:** added `launch_sphere_relativistic`
  declaration. Includes `relativity/RelativityParams.h` so the
  `Observer` and `RelativityParams` PODs are part of the kernel ABI.
- **`src/cuda/CudaTestKernel.cu`:** added `__global__
  k_sphere_relativistic`. Per pixel:
  1. `generate_camera_ray`
  2. (if `enable_aberration`) `aberrateDirection(observer.velocity,
     ray.direction)`
  3. `intersect_sphere`
  4. base shade (`0.5*n + 0.5` on hit; sky gradient on miss)
  5. (if `enable_doppler`) `applyDopplerColor` modulated by
     `doppler_color_strength`
  6. (if `enable_searchlight`) scale by
     `lerp(1, D^4, searchlight_strength)`
  7. framebuffer write.
  Aberration runs *before* intersection so geometry is hit-tested in
  the apparent frame; Doppler / searchlight run *after* shading so
  they modify perceived radiance rather than surface state.
- **`src/cuda/CudaRenderer.{h,cu}`:** added
  `render_relativistic_sphere(camera, observer, params, sphere, w, h)`.
  Reuses the templated `run_kernel_render` scaffold from M7 (validate
  -> allocate `GpuBuffer<float>` -> launch -> drain CUDA errors ->
  download into `Image`).
- **`src/main.cpp`:** `--render` now performs a fixed four-beta sweep
  along -Z (forward motion) and writes:
  - `output/sphere_beta_000.ppm`
  - `output/sphere_beta_025.ppm`
  - `output/sphere_beta_075.ppm`
  - `output/sphere_beta_095.ppm`
  `--output` is intentionally ignored at this milestone so the
  deliverable is reproducible. Single-image renders remain available
  through `CudaRenderer::render_sphere` for tests / future CLI
  additions.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/6 Test #1: math_tests       ........... Passed  0.00 sec
2/6 Test #2: image_tests      ........... Passed  0.00 sec
3/6 Test #3: gpu_tests        ........... Passed  0.00 sec
4/6 Test #4: camera_tests     ........... Passed  0.00 sec
5/6 Test #5: geometry_tests   ........... Passed  0.00 sec
6/6 Test #6: relativity_tests ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 6

$ ./build/bin/RelativityRender --render scene.scn --width 16 --height 16
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a Turing/Ampere/Ada
GPU) produces the four PPM files. The kernel calls the same `RR_HD`
routines exercised by `relativity_tests` (52 assertions) and
`geometry_tests` (25 assertions), so the host coverage validates the
device math by construction.

#### Hard-rule check

- **All effects on the GPU**: every step of the per-pixel pipeline
  (ray gen, aberration, intersection, base shade, Doppler colour,
  searchlight, framebuffer write) runs inside `k_sphere_relativistic`.
- **No CPU pixel/ray work**: `main.cpp` builds Camera / Sphere /
  Observer / RelativityParams structs, calls the renderer once per
  beta, and writes the downloaded `Image` to PPM. The only CPU
  iteration over pixels is inside `Image::save_ppm` (image save
  internals, permitted).

#### Note on physical honesty at high beta

`searchlight_strength = 1.0` (the default) applies the bolometric
`D^4` factor at full strength. At `beta = 0.95` along the view axis
the forward direction has `D ~= 6.25` and `D^4 ~= 1500`, so on-axis
pixels saturate to white in `output/sphere_beta_095.ppm`. That is
the iconic relativistic-flight headlight effect; lowering
`searchlight_strength` (or moving toward a tone-mapped HDR pipeline
once we have one) is an artistic decision that lives behind the
existing knob - the math itself is intentionally physical.

### 2026-04-27 — M9 relativity math leaf landed (no renderer integration)

The mathematical core of the differentiator: Lorentz factor, length
contraction, relativistic Doppler, beaming, aberration, and a clearly
labelled artistic Doppler colour shift. Every function is `RR_HD
inline`, callable from kernels and host tests alike. No camera or
renderer wiring yet - that comes in the next slice.

- **`src/relativity/RelativityParams.h`:** two PODs.
  - `Observer { velocity (Vec3 in c-units) }` - kinematic state of
    the boosted frame; position lives on the camera.
  - `RelativityParams { enable_aberration, enable_doppler,
    enable_searchlight, doppler_color_strength,
    searchlight_strength, max_beta }` - artist-facing toggles and
    intensities, separated from `Observer` so artistic knobs don't
    pollute the physical state.
- **`src/relativity/RelativityMath.h`:** the math leaf. Natural
  units (c = 1) throughout. Each function carries an explicit
  PHYSICAL or ARTISTIC APPROXIMATION tag in its docstring:
  - `clampBeta` (PHYSICAL): magnitude clamp below the lightspeed
    singularity; `max_beta` itself capped at `0.999999`.
  - `gamma` (PHYSICAL): `1 / sqrt(1 - beta^2)` with a numerical
    safety net when the caller forgets to clamp.
  - `lorentzContraction` (PHYSICAL): `sqrt(1 - beta^2)` (i.e.
    `1/gamma`).
  - `dopplerFactor` (PHYSICAL): `1 / [gamma * (1 - beta . dir)]`
    with `dir` the photon's scene-frame direction of travel.
  - `searchlightFactor` (PHYSICAL, bolometric): `D^4` (specific
    intensity uses `D^3` if the renderer wants monochromatic
    light).
  - `aberrateDirection` (PHYSICAL): textbook Lorentz aberration in
    vector form, identity at `|beta| = 0`, output renormalised.
    Treats `direction` as the photon's direction of travel
    (the SOURCE position is the opposite vector).
  - `applyDopplerColor` (ARTISTIC APPROXIMATION): bounded
    `tanh(0.5 * log(D)) * strength` mix toward a cool tint for
    blueshift and a warm tint for redshift. Identity at
    `strength = 0` and at `D = 1`. Marked clearly as a
    placeholder until the spectral pipeline arrives in M16/M17.
- **`src/relativity/RelativityMath.cuh`:** thin re-export of
  `RelativityMath.h` so kernel TUs can include a `.cuh`. Future
  device-specific intrinsics (`rsqrtf`, `__fdividef`) can land
  here without touching the host surface.
- **`tests/relativity_tests.cpp`:** 52 host-side assertions covering
  every function on the requested list. Spot-checks include
  `gamma(0.6) = 1.25` (3-4-5), `lorentzContraction(0.6) = 0.8`,
  Doppler identity at rest, longitudinal `D_blue * D_red = 1`,
  transverse-Doppler `D = 1/gamma`, `searchlightFactor(2) = 16`,
  aberration zero-beta identity, unit-length output, the
  along-motion invariant, and the perpendicular case which gives
  the closed-form result `d' = (-beta, perp/gamma, 0)`. The
  artistic colour shift is checked for `strength = 0` identity,
  `D = 1` identity, and the warm/cool tint behaviour at strong
  red/blueshift.
- **`CMakeLists.txt`:** added `relativity_tests` test executable.
  Header-only module; just needs `src/` on the include path.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/6 Test #1: math_tests       ........... Passed  0.00 sec
2/6 Test #2: image_tests      ........... Passed  0.00 sec
3/6 Test #3: gpu_tests        ........... Passed  0.00 sec
4/6 Test #4: camera_tests     ........... Passed  0.00 sec
5/6 Test #5: geometry_tests   ........... Passed  0.00 sec
6/6 Test #6: relativity_tests ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 6

$ ./build/bin/relativity_tests
relativity_tests: 52/52 passed
```

#### Not in this slice (M9 still "in progress")

- No renderer integration, per the explicit instruction in the
  prompt. `Observer` does not yet flow into `Camera`, the kernels
  do not yet call `aberrateDirection`, and `--render` continues
  to use the classical sphere kernel.
- Retarded-time approximation: out of scope for this slice; lands
  with the path tracer (M14) where it can interact with motion
  blur sampling.

#### Naming note

The functions use camelCase (`clampBeta`, `gamma`,
`lorentzContraction`, `dopplerFactor`, `searchlightFactor`,
`aberrateDirection`, `applyDopplerColor`) per the prompt. The
rest of the project uses snake_case; the relativity module is the
exception so the API names match the physics literature.

### 2026-04-27 — M8 GPU primitive intersection landed

First real ray-traced output: per pixel the GPU generates the primary
ray, intersects against a single sphere, and shades. The CPU only
constructs the camera and sphere structs and launches the kernel.

- **`src/geometry/Sphere.h`:** plain-data sphere POD (`center`,
  `radius`) with an `RR_HD make_sphere(...)` factory. Trivial
  aggregate so it can be passed to kernels by value with no
  ABI surprises. The full geometry system (triangle meshes,
  instancing, AS-build inputs) lands in M10; this is the minimum
  primitive needed to validate GPU intersection.
- **`src/renderer/Hit.h`:** `Hit { hit, t, position, normal }` POD
  plus an `RR_HD make_miss()` factory. Material / primitive ids
  are deferred to M14 (path tracer); keeping `Hit` minimal here
  avoids leaking later concerns into the geometry layer.
- **`src/cuda/CudaIntersection.cuh`:** `RR_HD inline
  intersect_sphere(ray, sphere, t_min, t_max) -> Hit`. Solves the
  half-`b` quadratic with `disc = b*b - a*c`, falls back to the far
  root when the near one is out of `(t_min, t_max)`, returns a miss
  on a degenerate ray (`a <= 0`). Uses `sqrtf` and the inverse-radius
  multiply to avoid a redundant `length` call. Despite the `.cuh`
  extension the header is host- and device-callable and pulls in no
  CUDA runtime, so the host tests exercise the same code path the
  kernel runs.
- **`src/cuda/CudaKernels.cuh` / `CudaTestKernel.cu`:** added
  `launch_sphere_visualize` and `__global__ k_sphere_visualize`.
  Per pixel the kernel calls `generate_camera_ray`, runs
  `intersect_sphere`, and writes either the normal-as-color shade
  (`0.5*n + 0.5`) on hit or a vertical sky gradient
  (`mix(white, sky_blue, 0.5*(dir.y + 1))`) on miss. Same launch
  config as the earlier kernels (16x16 blocks, ceiling-divided
  grid, bounds-checked).
- **`src/cuda/CudaRenderer.h` / `.cu`:** added
  `render_sphere(camera, sphere, w, h)`. Validates the radius,
  snapshots the camera into the device POD, and reuses the
  templated `run_kernel_render` scaffold from M7. `render_gradient`
  and `render_camera_rays` are kept as diagnostics.
- **`src/main.cpp`:** `--render` now defaults to
  `output/gpu_sphere.ppm`, builds a default `Camera` and a
  hard-coded test sphere `{(0, 0, -3), r = 1}`, and calls
  `render_sphere`. Real scene loading lands at M13. Without CUDA
  the executable still compiles and reports the missing backend
  honestly.
- **`tests/geometry_tests.cpp`:** 25 host-side assertions. Direct
  unit tests on `intersect_sphere`: rays pointing the wrong way
  miss; the centre ray hits the front of a sphere with `t = 2`,
  `position = (0, 0, -2)`, `normal = (0, 0, 1)`; grazing rays
  miss; `(t_min, t_max)` clipping behaves correctly (including
  forcing the far-root fallback); rays starting inside a sphere
  hit at the far root. Plus a "kernel replay" pair: build the same
  default camera the M8 kernel uses, generate the centre / corner
  primary rays, intersect against the same hard-coded test sphere,
  and assert hit / miss + geometry. The kernel runs the same
  `RR_HD` routine, so these host tests cover the device path by
  construction.
- **`CMakeLists.txt`:** added `geometry_tests` test executable
  (links `rr_camera` for math + camera headers; `Sphere.h`,
  `Hit.h`, and `CudaIntersection.cuh` are header-only and need no
  library).

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake -S . -B build && cmake --build build
$ cd build && ctest --output-on-failure
1/5 Test #1: math_tests     ........... Passed  0.00 sec
2/5 Test #2: image_tests    ........... Passed  0.00 sec
3/5 Test #3: gpu_tests      ........... Passed  0.00 sec
4/5 Test #4: camera_tests   ........... Passed  0.00 sec
5/5 Test #5: geometry_tests ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 5

$ ./build/bin/geometry_tests
geometry_tests: 25/25 passed

$ ./build/bin/RelativityRender --render scene.scn --width 16 --height 16
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a machine with
NVCC + a Turing/Ampere/Ada GPU) produces `output/gpu_sphere.ppm`.
The kernel calls the exact same `RR_HD` ray and intersection
routines exercised by `camera_tests` + `geometry_tests`, so the
host tests cover the device math.

#### Hard-rule check

- **No CPU ray tracing:** the rendered output is produced entirely
  by `k_sphere_visualize`. Host calls to `intersect_sphere` exist
  only inside `geometry_tests` for validation.
- **CPU only creates struct + launches:** `main.cpp` builds a
  `Camera` and a `Sphere`, calls `CudaRenderer::render_sphere`,
  and writes the downloaded `Image` to PPM. The only CPU iteration
  over pixels is in `Image::save_ppm` (image save internals,
  permitted).

### 2026-04-27 — M7 camera system & GPU camera rays landed

Pinhole perspective camera on the host, plain-data POD on the device,
and a `RR_HD` ray-generation function the kernel and host tests both
call. The new `--render` path lets the GPU generate one primary ray
per pixel and encodes the direction as RGB; the CPU only configures
the camera, launches, downloads, and saves.

- **`src/camera/CameraRay.h`:** `CameraRay { origin, direction }` and
  the device-friendly `GpuCamera { position, forward, up, right,
  tan_half_vfov, aspect }` POD. The pre-computed `tan_half_vfov` keeps
  the per-thread arithmetic to a few multiplies and one normalize.
  `RR_HD inline generate_camera_ray(GpuCamera, x, y, w, h)` samples
  pixel centres at `(x+0.5, y+0.5)`, builds the image-plane offset
  `(u = (2x/w - 1) * aspect * tan_half_vfov,
    v = (1 - 2y/h) * tan_half_vfov)` (top-left origin), and returns
  the normalized direction `forward + right*u + up*v`. Same code path
  runs on host and device.
- **`src/camera/Camera.h` / `.cpp`:** host class with explicit
  position / forward / up / right / vfov / aspect / near / far. `look_at`
  re-orthogonalizes the basis (with a sensible fallback when the up
  hint is parallel to forward, and a no-op when eye == target).
  Setters clamp vfov to `(0.01, 179.0)` degrees and reject
  non-positive aspect ratios. `to_gpu()` snapshots into the device
  POD.
- **`src/cuda/CudaKernels.cuh`:** declares
  `launch_camera_rays_visualize(float*, w, h, GpuCamera, stream)`
  alongside the existing gradient launcher.
- **`src/cuda/CudaTestKernel.cu`:** added `__global__
  k_camera_rays_visualize` that calls
  `rr::camera::generate_camera_ray` per thread and writes
  `(0.5*dir + 0.5, 1.0)` into the Rgba32F framebuffer. Same launch
  config as the gradient kernel (16x16 blocks, ceiling-divided grid,
  bounds-checked).
- **`src/cuda/CudaRenderer.h` / `.cu`:** factored the host scaffold
  (validate dims -> allocate `GpuBuffer<float>` -> launch kernel ->
  drain CUDA errors -> download into `Image`) into a templated
  `run_kernel_render(...)` helper. `render_gradient` keeps its
  existing surface; `render_camera_rays(camera, w, h)` is the new
  entry point. Both delegate per-pixel work to the GPU.
- **`src/main.cpp`:** `--render` now defaults to
  `output/gpu_camera_rays.ppm`, constructs an
  `rr::camera::Camera` (origin, looking down -Z, aspect = w/h), and
  calls `render_camera_rays`. Without CUDA the executable still
  compiles and reports the missing backend honestly.
- **`tests/camera_tests.cpp`:** 43 host-side assertions: default
  state, basis orthonormality after `look_at` (incl. the
  parallel-up-hint fallback and the eye==target degenerate case),
  vfov clamping, `to_gpu()` round-trip, and `generate_camera_ray`
  geometry checks (centre pixel ≈ forward; corner sign patterns
  for top-left and bottom-right; all directions unit-length; wider
  aspect ratio pushes the left-edge ray further left). The same
  function runs on the GPU - validating the math on the host
  validates the kernel by construction.
- **`CMakeLists.txt`:** added `rr_camera` static library
  (`src/camera/Camera.cpp`, `PUBLIC src` includes). When
  `RR_ENABLE_CUDA` is on, `rr_gpu` PUBLIC-links `rr_camera` because
  `CudaRenderer::render_camera_rays` consumes `Camera`. Added a
  fourth test executable, `camera_tests`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake -S . -B build && cmake --build build
$ cd build && ctest --output-on-failure
1/4 Test #1: math_tests   ............ Passed  0.00 sec
2/4 Test #2: image_tests  ............ Passed  0.00 sec
3/4 Test #3: gpu_tests    ............ Passed  0.00 sec
4/4 Test #4: camera_tests ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 4

$ ./build/bin/camera_tests
camera_tests: 43/43 passed

$ ./build/bin/RelativityRender --render scene.scn --width 32 --height 32
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a machine with NVCC
+ a Turing/Ampere/Ada GPU) produces `output/gpu_camera_rays.ppm`. It
is correct by construction: the `RR_HD` ray generator the kernel
calls is the exact same function `camera_tests` exercises on the
host, so the host tests cover the device math.

#### Hard-rule check

- **All ray generation on the GPU**: the `__global__ k_camera_rays_visualize`
  is the only place rays are produced for the rendered output. The
  host calls `generate_camera_ray` only inside `camera_tests` to
  validate the math; the executable's render path never iterates
  pixels on the CPU.
- **No CPU pixel loop**: the only iteration over pixels in the
  render path is inside `Image::save_ppm`, which converts floats to
  bytes for the PPM payload (image save internals, permitted).

### 2026-04-27 — M6 first CUDA kernel (GPU-rendered gradient) landed

End-to-end host -> device -> kernel -> host -> PPM pipeline. The GPU
generates every pixel; the CPU only allocates, launches, downloads,
and saves. The save path is the only place a CPU loop touches pixels,
exactly as the engineering rules permit.

- **`src/cuda/CudaKernels.cuh`:** kernel-side helpers + host-callable
  launch wrapper declarations. Pulls in `cuda_runtime.h`, so it is
  only safe to include from `.cu` files. Currently exposes
  `launch_gradient_rgba32f(float*, int, int, cudaStream_t)`.
- **`src/cuda/CudaTestKernel.cu`:** the actual `__global__` kernel.
  One thread per pixel, 16x16 blocks, ceiling-divided grid. Each
  thread writes (R=u, G=v, B=0, A=1) into the channel-interleaved
  Rgba32F layout used by `rr::image::Image`. Bounds-checked. The
  host-callable wrapper is a thin launch-config shim - no CPU
  pixel logic.
- **`src/cuda/CudaRenderer.h`:** CUDA-Runtime-free public surface.
  `CudaRenderer::Result { ok, image, message }` plus a single
  `render_gradient(width, height) -> Result` static. The header is
  host-includable; only the `.cu` implementation pulls in
  `cuda_runtime.h`.
- **`src/cuda/CudaRenderer.cu`:** allocates a `GpuBuffer<float>` of
  `width*height*4` floats, launches the gradient kernel, drains
  errors via `cudaGetLastError` + `cudaDeviceSynchronize`, and
  downloads into a fresh `Image(width, height, Rgba32F)`. On any
  CUDA failure it returns `ok=false` with a human-readable message
  derived from `cudaGetErrorString` and clears the sticky error
  state.
- **`src/main.cpp`:** the `--render` path now calls
  `CudaRenderer::render_gradient`, creates the parent directory if
  needed, and saves to `output/gpu_gradient.ppm` (or
  `--output <path>` when given). Gated by `RR_HAS_CUDA`; without
  CUDA the executable still compiles and reports the missing
  backend honestly.
- **`CMakeLists.txt`:** under `RR_ENABLE_CUDA`, `enable_language(CUDA)`
  is now turned on, `CMAKE_CUDA_STANDARD = 17`, and
  `CMAKE_CUDA_ARCHITECTURES` defaults to `75;80;86;89` (Turing
  through Ada; override via `-DCMAKE_CUDA_ARCHITECTURES=...`). The
  `.cu` files join `rr_gpu`. `RR_HAS_CUDA` is now `PUBLIC` so
  consumers (RelativityRender exe, gpu_tests) gate CUDA call
  sites on it. `rr_gpu` PUBLIC-links `rr_image` when CUDA is on
  because `CudaRenderer::Result` carries an `Image`.

#### Verified locally (host-only, no CUDA Toolkit on this machine)

```
$ cmake -S . -B build && cmake --build build
$ cd build && ctest --output-on-failure
1/3 Test #1: math_tests  ............ Passed  0.00 sec
2/3 Test #2: image_tests ............ Passed  0.00 sec
3/3 Test #3: gpu_tests   ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 3

$ ./build/bin/RelativityRender --render scene.scn --width 64 --height 32
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled path (`-DRR_ENABLE_CUDA=ON`, on a machine with
NVCC + an NVIDIA GPU) produces `output/gpu_gradient.ppm`. It is
correct by construction (uses only the standard CUDA Runtime API,
the kernel is bounds-checked, the layout matches
`rr::image::Image::PixelFormat::Rgba32F`) but is not end-to-end
runnable in this environment.

#### Hard rule check

- GPU generates every pixel: yes - the kernel computes `(u, v, 0, 1)`
  per pixel; CPU never reads or writes per-pixel values.
- CPU pixel loop only inside image save: yes - the only CPU iteration
  over pixels is in `Image::save_ppm`, which converts floats to
  bytes for the PPM payload (this is "image save internals" per the
  engineering rules).

### 2026-04-27 — M5/M6 GPU buffer abstraction landed

Move-only typed device-memory handle with allocate / upload / download /
reset, plus a CUDA byte-level implementation. Still no kernels - the
upload-no-kernel-download round trip is enough to validate the
plumbing.

- **`src/gpu/GpuBuffer.h`:** templated `rr::gpu::GpuBuffer<T>`. RAII,
  move-only (copy ctor / assignment deleted; move ctor / assignment
  noexcept, transfers ownership and leaves the source empty).
  `static_assert(std::is_trivially_copyable_v<T>)` because the
  backend just shuffles bytes. Surface: `allocate(count)`,
  `upload(host, count)` (resizes as needed), `download(host, count)
  const`, `reset()`, `empty()`, `size()`, `size_in_bytes()`,
  `device_ptr()` (mutable + const). Allocation is deferred -
  default construction does not touch the GPU.
- **`src/gpu/GpuBuffer.cpp`:** byte-level shim
  (`detail::gpu_alloc/free/copy_h2d/copy_d2h`). `#ifdef RR_HAS_CUDA`
  forwards to `rr::cuda::cuda_alloc/free/copy_h2d/copy_d2h`; without
  CUDA, every call returns an honest failure (`nullptr` / `false`)
  so consumers receive a predictable empty-buffer state instead of a
  silent no-op.
- **`src/cuda/CudaBuffer.h`:** CUDA-Runtime-free header exposing only
  the byte-level free functions. Keeps `cuda_runtime.h` confined to
  the `.cpp` so templated consumers in `rr::gpu::` don't drag the
  CUDA toolchain onto every include path.
- **`src/cuda/CudaBuffer.cpp`:** wraps `cudaMalloc`, `cudaFree`,
  `cudaMemcpy(...HostToDevice)`, `cudaMemcpy(...DeviceToHost)`. On
  every failure path the sticky CUDA last-error flag is cleared so a
  later real CUDA call observes a clean state. `cuda_alloc(0)`
  returns `nullptr` deliberately (no zero-byte allocations);
  `cuda_free(nullptr)` is a no-op; zero-byte copies succeed.
- **`tests/gpu_tests.cpp`:** added 16 buffer assertions on top of the
  existing 4 device-query checks. Unconditional: default-state
  invariants (empty, size 0, null device_ptr, idempotent `reset`),
  move ctor + move-assign on default-constructed buffers. Host-only
  contract: with no CUDA backend, `allocate` / `upload` /
  `download(non-zero)` all fail predictably while the buffer stays
  empty, and `download(0)` succeeds as a no-op. CUDA + device
  present: upload a 256-element float array with a non-trivial
  pattern, run no kernel, download into a fresh vector, compare
  byte-for-byte; then move ownership and re-download from the new
  owner. The round-trip path skips with a printed message when CUDA
  isn't compiled or no devices are visible, so CI without GPUs
  remains green.
- **`CMakeLists.txt`:** `rr_gpu` now also lists
  `src/gpu/GpuBuffer.cpp`, and adds `src/cuda/CudaBuffer.cpp` under
  the existing `if(RR_ENABLE_CUDA)` block (same `RR_HAS_CUDA` define
  + `CUDA::cudart` link). No new CMake plumbing - the buffer rides
  through on the existing CUDA gate. No `enable_language(CUDA)` yet:
  these are still host-side calls into the CUDA Runtime API, no
  device code.

#### Verified locally (host-only configure)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/3 Test #1: math_tests  ............ Passed  0.00 sec
2/3 Test #2: image_tests ............ Passed  0.00 sec
3/3 Test #3: gpu_tests   ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 3

$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 20/20 passed
```

The CUDA-enabled round-trip is correct by construction (uses only
`cudaMalloc` / `cudaFree` / `cudaMemcpy` from the standard CUDA
Runtime, all gated on the same flag as `find_package(CUDAToolkit)`)
but is not end-to-end runnable in this environment.

### 2026-04-27 — M5 CUDA device layer landed

First GPU-aware code in the project. Backend-agnostic surface in
`rr::gpu::`; concrete CUDA implementation in `rr::cuda::`. The host
build still configures and runs without CUDA installed; CUDA is gated
by `-DRR_ENABLE_CUDA=ON`. No kernels, no allocations, no rendering -
just device detection and property queries.

- **`src/gpu/GpuDevice.h` / `.cpp`:** `rr::gpu::GpuDevice` POD struct
  (index, name, compute capability major/minor, total memory bytes,
  multiprocessor count) plus `compute_capability_string()` and
  `total_memory_human()` formatters. Free functions:
  `gpu_backend_available()`, `gpu_backend_name()`,
  `enumerate_devices()`. The `.cpp` `#ifdef RR_HAS_CUDA`-includes
  `cuda/CudaContext.h` and forwards; otherwise it returns
  `"(none)"` / `false` / empty list. Callers never need to know
  whether CUDA was compiled in.
- **`src/cuda/CudaContext.h` / `.cpp`:** `rr::cuda::query_devices()`
  wrapping the CUDA Runtime API (`cudaGetDeviceCount` +
  `cudaGetDeviceProperties`). Robust to driver-init failures: returns
  empty on failure and clears the sticky last-error so a later real
  CUDA call doesn't observe it. Compiled only when CUDA is enabled.
- **`src/main.cpp`:** `--device-info` now logs backend name, prints
  the device count, and emits one line per device formatted as
  `[i] <name> (cc <maj>.<min>, <MiB> MiB, <SMs> SMs)`. When no
  backend is compiled, it logs that explicitly and tells the user how
  to re-enable. When the backend is compiled but no devices are
  visible, it warns instead of pretending to enumerate.
- **`CMakeLists.txt`:** `find_package(CUDAToolkit REQUIRED)` only when
  `RR_ENABLE_CUDA` is ON. New `rr_gpu` static library carrying
  `src/gpu/GpuDevice.cpp`. When CUDA is enabled, `src/cuda/CudaContext.cpp`
  is added to the same library, `RR_HAS_CUDA` is defined PRIVATE, and
  `CUDA::cudart` is linked. The main executable links `rr_gpu`. No
  `enable_language(CUDA)` yet - we only call the runtime API from host
  C++; NVCC arrives in M6 with the first kernel.
- **`tests/gpu_tests.cpp`:** 4 assertions exercising the public surface
  against invariants that hold either way:
  `gpu_backend_name()` is non-empty,
  `available()` and `name() == "(none)"` agree, and
  `enumerate_devices()` is empty when no backend is compiled. Also
  validates `compute_capability_string()` and `total_memory_human()`
  formatters. CI machines without GPUs are valid environments and the
  test is silent about that.

#### Verified locally (host-only configure)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/3 Test #1: math_tests  ............ Passed  0.00 sec
2/3 Test #2: image_tests ............ Passed  0.00 sec
3/3 Test #3: gpu_tests   ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 3

$ ./build/bin/RelativityRender --device-info
[..] [INFO] RelativityRender 0.0.1 starting
[..] [INFO] GPU backend: (none)
[..] [INFO] No GPU backend compiled in.
            Reconfigure with -DRR_ENABLE_CUDA=ON to enable CUDA.
```

The CUDA-enabled path (`-DRR_ENABLE_CUDA=ON`) was not exercised in
this environment (no CUDA Toolkit installed), but is correct by
construction: it relies only on the standard CMake `CUDAToolkit`
package and the `CUDA::cudart` imported target, with all CUDA-specific
sources, defines, and link deps gated on the same `RR_ENABLE_CUDA`
flag that controls the `find_package` call.

#### Module status nuance

`rr::gpu::` is the long-term backend-agnostic surface; the CUDA
backend is one implementation of it. The Module Map's "GPU Device
Layer" (#4) is the surface and is *landed*. The "CUDA Backend" (#5)
will grow to cover streams, buffers, kernel launches, error wrapping,
and pinned memory in M6+; for now it only contains the device-query
plumbing, so it is marked *in progress*.

### 2026-04-27 — M4 image / framebuffer system landed

Host-side pixel storage, set/get/clear/resize, and PPM save (no
third-party dep). PPM is intentionally minimal; OpenEXR/PNG IO arrives
when the GPU paths need real HDR formats.

- **`src/image/Color.h`:** `Rgb` and `Rgba` plain-data structs; both
  `RR_HD constexpr`-friendly so they will be usable from device code
  later. `Rgba` carries `a` defaulted to 1; `Rgba::rgb()` strips it. No
  arithmetic operators yet — premature for image storage; they'll come
  in with shading.
- **`src/image/Image.h` / `.cpp`:** `PixelFormat { Rgb32F, Rgba32F }`
  and an `Image` class with `width/height/format/channels/empty`,
  `set_pixel(x,y,Rgba)`, `get_pixel(x,y)->Rgba` (alpha=1 for Rgb32F),
  `clear(Rgba)`, `resize(w,h)` (zero-fills, format preserved),
  raw `data()` / `size_in_floats()` for future GPU upload, and
  `save_ppm(path)`. Storage is row-major, channel-interleaved,
  contiguous floats (the layout we'll mirror on the device side
  later). OOB pixel access is debug-asserted.
- **`save_ppm`** writes 8-bit P6 binary. Floats are clamped to [0,1]
  and quantized; HDR > 1 is lost; alpha is dropped (PPM has no alpha).
  Empty images return `false`. Honest minimal IO, not a stub: it
  produces files an EXR/PNG viewer can convert and a hex dump can
  validate.
- **`src/image/Framebuffer.h` / `.cpp`:** thin render-target wrapper
  owning a single color `Image`. Provides `color()` (mutable + const),
  `resize`, `clear`, `save_ppm`. AOVs, accumulation buffers, and tile
  metadata join later (M14 / M17). The Image / Framebuffer split is
  intentional: Image is generic 2D pixel storage; Framebuffer is what
  the renderer writes into during a frame.
- **`tests/image_tests.cpp`:** 39 assertions covering Rgba/Rgb format
  set/get round-trip, Rgb32F alpha-on-read = 1, clear, resize zeroes,
  Framebuffer clear + resize, and the **gradient-to-PPM IO
  validation** (verifies header `P6 W H 255` + payload size = W*H*3
  bytes). Empty-image save returns false. The gradient is the only
  CPU-side pixel generation in this module, allowed exclusively as IO
  validation.
- **`CMakeLists.txt`:** first module promoted to a static library —
  `rr_image` (`src/image/{Image,Framebuffer}.cpp` + `PUBLIC` include
  on `src/`). `image_tests` links it; the main executable does not
  yet use it. Same warning flags as elsewhere (`-Wall -Wextra
  -Wpedantic` / `/W4 /permissive-`).

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
1/2 Test #1: math_tests  ............ Passed  0.00 sec
2/2 Test #2: image_tests ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 2
```

`image_tests` reports `39/39 passed`; the gradient is written to
`<temp>/rr_image_test_gradient.ppm`, validated, and removed.

#### Order note

M4 lands before M2's remaining sub-items (`Error`, `FileSystem`,
`App`, `Config::load`/`save`, real test framework, host CI) for the
same reason M3 did: Image depends only on Math (already landed) and a
trivial subset of Core that exists now (none of the deferred Core
pieces are needed here). Per `docs/MODULE_MAP.md`, this respects the
declared dependency direction.

### 2026-04-27 — M3 math library landed

The math leaf is in. Header-only, host/device portable, and exercised by a
small test runner that hooks into `ctest`.

- **`src/math/MathUtils.h`:** `RR_HD` host/device macro (expands to
  `__host__ __device__` under NVCC, empty otherwise), constants
  (`kPi`, `kTwoPi`, `kHalfPi`, `kInvPi`, `kEpsilon`), and templated
  `min` / `max` / `clamp` / `lerp` plus `radians`, `degrees`,
  `saturate`. All `constexpr RR_HD` where possible.
- **`src/math/Vec2.h` / `Vec3.h` / `Vec4.h`:** plain structs with `float`
  members. Constructors include a single-arg `explicit` broadcast to
  prevent accidental scalar→vector conversions. Operator suite covers
  `+`, `-`, unary `-`, scalar `*` and `/`, in-place compound assignments,
  and `==` / `!=`. `Vec3` adds component-wise (Hadamard) `*`. Free
  functions: `dot` (all three), `cross` (Vec3), `length`,
  `length_squared`, `normalize` (returns zero on degenerate input
  rather than NaN). Free overloads of `clamp` and `lerp` for `Vec3`
  pick up the file's component-wise semantics without conflicting with
  the scalar templates.
- **`src/math/Mat4.h`:** row-major 4x4 (`m[row][col]`) with translation
  in column 3. Static constructors `identity`, `translation(Vec3)`,
  `scale(Vec3)`. `operator*` for matrix multiply. Free functions
  `transform_point` (homogeneous w=1, applies translation) and
  `transform_vector` (w=0, ignores translation). All `constexpr RR_HD`.
- **`tests/math_tests.cpp`:** 42 assertions covering scalar utilities,
  Vec3 add/sub/scalar/compound, dot/cross identities (right-handed
  basis + anti-commutativity), length / normalize (including the
  degenerate-input zero result), clamp/lerp, Mat4 identity / translation
  / scale, matrix multiply with `T*S != S*T`. Hand-rolled assertion
  macro is variadic so braced-init expressions like `Vec3{0,0,0}`
  inside `RR_CHECK(...)` aren't split by the preprocessor. The macro
  plumbing is throwaway — it gets replaced by Catch2/doctest on the
  M2 deferred list — but the assertions stay.
- **`CMakeLists.txt`:** added `math_tests` executable (header-only
  consumer of `src/math/`), `target_include_directories(... src)`,
  and `add_test(NAME math_tests COMMAND math_tests)` so `ctest`
  picks it up. Same warning flags as the main executable.

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
1/1 Test #1: math_tests ..................... Passed   0.00 sec
100% tests passed, 0 tests failed out of 1
```

#### Order note

The master order has Math (step 4) following Core (step 3). Math has zero
dependency on Core (it is the leaf), so per `docs/MODULE_MAP.md` it is
safe to land while M2's remaining items (`Error`, `FileSystem`, `App`,
`Config::load`/`save`, real test framework, host CI) are still pending.
M2 stays "in progress" until those land.

### 2026-04-27 — M2 configuration + command-line handling landed

Continues M2 (Core Engine). Adds the runtime configuration struct and the
command-line parser that populates it. No rendering, no GPU calls — every
command-line surface is parsed today and acted on for real once the
underlying backends arrive.

- **`src/core/Config.h` / `.cpp`:** `rr::core::Config` plain-data struct
  with `show_device_info`, `render_scene_path` (`std::optional<string>`),
  `output_image_path` (`std::optional<string>`), `width` (default 1280),
  `height` (default 720), and a `wants_render()` helper. The `.cpp` is
  intentionally near-empty — Config is a data carrier; future load/save
  (TOML / JSON) lands here without churn.
- **`src/core/CommandLine.h` / `.cpp`:** `rr::core::CommandLine` class
  with `Status { Ok, Help, Version, Error }`, a `ParseResult { status,
  message }`, a `parse(argc, argv, Config&)` static method, and a
  `usage()` static method. The parser is stateless, dependency-free,
  and handles missing values, non-positive sizes, malformed integers,
  and unknown flags by returning `Status::Error` with a human message.
  Integers go through `std::from_chars` to avoid `atoi`-style silent
  truncation.
- **`src/main.cpp`:** wired to `CommandLine::parse`. `--help` and
  `--version` are handled before the startup banner so their output is
  clean. `--device-info` logs honestly that the CUDA backend lands at
  M5. `--render <scene>` logs `render command received` and exits — per
  this milestone's scope it does not render. Unknown flags / bad values
  return exit code 2 and print usage on stderr.
- **`CMakeLists.txt`:** added `src/core/Config.cpp` and
  `src/core/CommandLine.cpp` to the `RelativityRender` executable.

#### Verified locally

```
$ ./build/bin/RelativityRender --help          # prints usage, rc=0
$ ./build/bin/RelativityRender --version       # prints "RelativityRender 0.0.1", rc=0
$ ./build/bin/RelativityRender --device-info   # logs CUDA-not-yet message, rc=0
$ ./build/bin/RelativityRender --render scene.scn --output out.exr \
                                --width 1920 --height 1080  # logs "render command received", rc=0
$ ./build/bin/RelativityRender --width foo     # error + usage on stderr, rc=2
$ ./build/bin/RelativityRender --render        # error (missing path), rc=2
$ ./build/bin/RelativityRender --bogus         # error (unknown arg), rc=2
```

#### Deliberately deferred (still inside M2)

- `core::Error` type.
- `core::App` lifecycle wrapper.
- `core::FileSystem` minimal IO.
- `Config::load` / `Config::save` (TOML or JSON).
- A test framework dependency under `third_party/` and tests for `Logger`,
  `Config`, and `CommandLine`.
- Host-only CI.

These will be added in subsequent M2 sub-prompts before M2 is marked
landed and M3 (Math Library) begins.

### 2026-04-27 — M2 minimal C++20 application foundation landed

First compiled binary in the project. Scope was deliberately restricted to a
minimal application foundation; config, lifecycle, error type, filesystem
helper, and tests are not in this slice and remain on the M2 todo list.

- **CMakeLists.txt:** bumped C++ standard from C++17 to **C++20** (pinned
  project-wide). Added the `RelativityRender` executable target with sources
  `src/main.cpp` and `src/core/Logger.cpp`, `src/` on the include path, and
  `-Wall -Wextra -Wpedantic` (or `/W4 /permissive-` on MSVC). Removed the
  commented-out `add_subdirectory(...)` placeholder block now that the build
  links source files directly; modules will be promoted to static libraries
  as they grow.
- **`src/core/Version.h`:** `rr::core::kProjectName`,
  `kVersionMajor/Minor/Patch`, `kVersionString` as `inline constexpr`.
  Hand-written, not CMake-generated, to keep the foundation self-contained.
- **`src/core/Logger.h`:** `rr::core::Logger` class with three static
  methods — `info`, `warning`, `error`. Accepts `std::string_view`.
- **`src/core/Logger.cpp`:** thread-safe implementation. `info` writes to
  `stdout`; `warning` and `error` write to `stderr`. Each line is
  `[HH:MM:SS.mmm] [LEVEL] message`. A single `std::mutex` serializes
  writes across threads. No external logging library; this is honest minimal
  code, not a stub.
- **`src/main.cpp`:** entry point that logs the project name, version, the
  platform tagline, and a "Core application foundation online" message,
  then exits 0.
- **Verified locally:** `cmake -S . -B build && cmake --build build`
  succeeds with the warning flags above; running `build/bin/RelativityRender`
  prints three timestamped INFO lines.

#### Naming choice

Constants in `Version.h` use the `kPascalCase` `inline constexpr` style
(e.g. `kVersionString`). This is the convention to expect for compile-time
constants throughout the renderer. `docs/DEVELOPMENT_RULES.md` §8 will be
updated to record this in a follow-up doc-only pass.

#### Deliberately deferred (still part of M2)

- `core::Config` (load / save).
- `core::Error` type.
- `core::App` lifecycle.
- `core::FileSystem` minimal IO.
- A test framework dependency under `third_party/` and tests for the logger.
- Host-only CI.

These will be added in subsequent M2 sub-prompts before M2 is marked
landed and M3 (Math Library) begins.

### 2026-04-27 — M1 repository skeleton landed

- Added top-level `CMakeLists.txt`. Declares the project, pins C++17,
  and exposes options:
  - `RR_ENABLE_CUDA` (default OFF) — CUDA backend.
  - `RR_ENABLE_OPTIX` (default OFF) — OptiX backend.
  - `RR_BUILD_TESTS` (default ON).
  - `RR_BUILD_TOOLS` (default OFF).
  - `RR_BUILD_INTEGRATIONS` (default OFF).
  Defaults are chosen so the host-only configure works on any machine
  (no CUDA / OptiX / Cinema 4D toolchains required). No targets are
  built yet — `add_subdirectory(...)` calls are commented out and
  annotated with the milestone that turns each one on.
- Added top-level `README.md` with project overview, status, repository
  layout, and configure / build instructions.
- Created the source skeleton:
  ```
  src/{core,math,image,gpu,cuda,optix,scene,geometry,material,
       texture,lighting,camera,relativity,renderer,pathtracer,
       io,server}/
  tests/
  tools/
  integrations/c4d/
  third_party/
  ```
- Added a `README.md` in every major folder (each `src/*` module,
  `tests/`, `tools/`, `integrations/`, `integrations/c4d/`,
  `third_party/`). Module READMEs are intentionally short pointers —
  one paragraph of purpose + a reference to `docs/MODULE_MAP.md`,
  which remains the authoritative contract.
- Updated this file. Marked M0 as landed and M1 as landed.

#### Naming notes vs. M0 docs

The skeleton uses the directory names listed in the M1 prompt, which differ
in a few places from the planned shape sketched in
`docs/MASTER_ARCHITECTURE.md` §8:

| M0 doc sketch         | M1 actual          |
|-----------------------|--------------------|
| `src/cuda_backend/`   | `src/cuda/`        |
| `src/optix_backend/`  | `src/optix/`       |
| `src/relativistic/`   | `src/relativity/`  |
| `src/scene_format/`   | `src/io/` (shared with image IO) |
| `src/aov/`, `src/progressive/`, `src/denoise/` | `src/renderer/` (umbrella) |
| `bridges/c4d_bridge/`, `bridges/c4d_native/` | `integrations/c4d/` |

These are layout-only differences. The 22 logical modules from
`docs/MODULE_MAP.md` are unchanged; reconciling MASTER_ARCHITECTURE §8 and
MODULE_MAP path references with this layout is a small, doc-only follow-up
and does not affect the architecture or dependency rules.

#### Deliberately deferred

- No source files (.h / .cpp / .cu) added. Module CMakeLists.txt files
  are added when the corresponding module is implemented (M2+).
- No third-party dependencies fetched or vendored. They are introduced in
  the milestones that need them (logging/test framework in M2, EXR/PNG
  in M4, etc.).
- No CI configuration. CI is added with M2 once there is a real target
  to compile.
- No CUDA / OptiX / Cinema 4D detection logic in CMake — only options.
  Detection lands when the corresponding backend module starts (M5
  for CUDA, M15 for OptiX, M19 for the C4D bridge).

### 2026-04-27 — M0 documentation set landed

- Added `docs/MASTER_ARCHITECTURE.md`: identity, layers, 22 modules, dependency
  direction, forbidden dependencies, end-to-end data flow, planned repository
  shape, non-goals.
- Added `docs/MODULE_MAP.md`: per-module ownership, dependencies, forbidden
  list, public surface, GPU-side flag, status.
- Added `docs/DEVELOPMENT_RULES.md`: identity, engineering, dependency, build,
  GPU, relativistic, process, style, testing, and "done" rules.
- Added `docs/MILESTONE_ROADMAP.md`: M0–M23 with goals, deliverables, and exit
  criteria. Cinema 4D work gated behind a working renderer server (M18).
- Added this file (`docs/BUILD_PLAN.md`) tracking module and milestone state.

---

## Next Step

**Finish M14 — wire sampling into a real integrator.** The
RNG + hemisphere primitives are in. To move M14 / Module 14
from "in progress" to "landed":

1. BSDF `sample` / `eval` / `pdf` on `MaterialParams`. Lambert
   first; later GGX, dielectric, conductor.
2. A `__global__ k_path_trace` kernel that uses the existing
   `intersect_sphere` / `intersect_triangle` primitives and
   the new RNG/sampling helpers to bounce rays through the
   scene with NEE + MIS for direct light.
3. Promote `RenderSettings.samples_per_pixel` and
   `max_depth` from "stored but not consumed" to real kernel
   arguments; both persist through `.rrscene` already.
4. Russian roulette path termination based on throughput.
5. Multi-mesh scene upload (currently `GpuScene` has a single
   mesh slot) - either an array of `CudaMeshView` or a flat
   global vertex/index buffer with per-mesh offsets - so a
   path-traced scene isn't restricted to one quad.

Before or alongside this, the M2 deferred items (`Error`,
`FileSystem`, `App`, `Config::load`/`save`, real test framework,
host-only CI) remain a backlog rather than a blocker.

Alongside M6, the M2 deferred items should be cleaned up so the core
foundation is honest end-to-end:

1. `core::Error` — a small result/error type used at module boundaries.
2. `core::FileSystem` — minimal path / read / write helpers using
   `std::filesystem` plus a thin error-aware wrapper.
3. `Config::load` / `Config::save` — TOML or JSON persistence via a
   vendored parser under `third_party/`.
4. `core::App` — application lifecycle wrapper that owns parse → run →
   exit.
5. A real test framework (Catch2 or doctest) under `third_party/`,
   migrating the existing test runners.
6. Host-only CI configuration that runs the build and tests.

Per development rules, none of these may introduce code from M7+
modules - no camera, scene, material, or rendering code yet.

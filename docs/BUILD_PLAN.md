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
| 6  | OptiX Backend                       | not started   |
| 7  | Scene Graph                         | landed        |
| 8  | Geometry System                     | not started   |
| 9  | Material / Shading System           | not started   |
| 10 | Texture System                      | not started   |
| 11 | Lighting System                     | not started   |
| 12 | Camera System                       | landed        |
| 13 | Relativistic Camera Model           | landed        |
| 14 | Path Tracer                         | not started   |
| 15 | Progressive Renderer                | not started   |
| 16 | Denoiser Integration                | not started   |
| 17 | Render Passes / AOVs                | not started   |
| 18 | Scene File Format                   | not started   |
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
| M10       | GPU Scene Upload & Triangle Mesh        | in progress |
| M11       | Material System (Foundations)           | not started |
| M12       | Lighting System (Foundations)           | not started |
| M13       | Scene File Format & Parser              | not started |
| M14       | Path Tracing Foundation                 | not started |
| M15       | OptiX Backend (Upgrade Path)            | not started |
| M16       | Texture System                          | not started |
| M17       | Render Passes / AOVs                    | not started |
| M18       | Renderer Server                         | not started |
| M19       | Cinema 4D Bridge (Plugin)               | not started |
| M20       | Preview UI                              | not started |
| M21       | Material Node Graph (Editor)            | not started |
| M22       | Denoiser Integration                    | not started |
| M23       | Native Cinema 4D Renderer Integration   | not started |

---

## Change Log

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

**Finish M10 — triangle mesh upload.** The sphere upload path is
in. To complete M10 and move on to M11:

1. `rr::geometry::TriangleMesh` host representation (positions,
   indices, attribute layouts) plus a GPU-uploadable form (SoA
   buffers via `GpuBuffer<float>` / `GpuBuffer<uint32_t>`).
2. Extend `GpuScene` with `upload_meshes(...)` and add a
   per-mesh handle to `CudaSceneView`.
3. Add a brute-force triangle intersection routine
   (`intersect_triangle` next to `intersect_sphere` in
   `cuda/CudaIntersection.cuh`) and a closest-hit loop step in
   `k_render_scene` that walks each mesh's index buffer.
4. Host regressions on the upload round-trip and on intersection
   geometry (centre-pixel hit / corner miss for a known
   triangle).

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

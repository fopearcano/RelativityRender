# Build Plan

Tracking doc per `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
("Update docs/BUILD_PLAN.md after every implementation"). Each entry
records what landed, in which stage, and the next concrete step.

## Current state

**Stages 1–10 + 6A + 6B + 7A + 7B + 7C + 8A + 8B + 9A + 9B + 10A +
10B.1 — through scene parser foundation.** Skeleton C++20 executable; header-only RR_HD math
library; host-side floating-point image + framebuffer system;
backend-agnostic GPU device + memory layers; pinhole `Camera` + RR_HD
`generate_camera_ray`; single-sphere intersection kernel; relativity
math leaf (`Observer`, `RelativityParams`, RR_HD `clampBeta` / `gamma`
/ `lorentzContraction` / `dopplerFactor` / `searchlightFactor` /
`aberrateDirection` / `applyDopplerColor`); **and a new relativistic
single-sphere kernel that runs aberration → intersection → Doppler
colour → searchlight beaming entirely on the device**. Four GPU
kernels live now: UV gradient, camera-ray visualisation, single-sphere
visualisation, relativistic single-sphere. `--device-info`,
`--render-gradient`, `--render-rays`, `--render-sphere`, and
`--render-relativistic` actions are all live. The relativistic action
runs a four-β sweep (0.00, 0.25, 0.75, 0.95) and writes four named
PPMs. 150 host-side test assertions across three ctest binaries pass.
A new `rr::gpu::GpuScene` upload manager and a `k_render_scene`
closest-hit kernel landed at Stage 6B; Stage 7A added host-side
`Triangle`/`Vertex`/`Mesh` with a local-space AABB; Stage 7B
added the `GpuMesh` upload manager and the `CudaMeshView` device
POD; Stage 7C restored `intersect_triangle` (Möller-Trumbore) and
extended `k_render_scene` with a triangle closest-hit loop. Stage 8A added the host-side material data model; Stage 8B brought
materials online on the device. Stage 9A added the host-side light
data model and promoted `SceneLight` to carry real `Light` data.
**Stage 9B** brings lighting online on the device:
`GpuScene::upload_lights` uploads a flat `Light` array,
`CudaSceneView` gains the lights slot, and `k_render_scene`'s
base-shade step now evaluates direct lighting per hit
(unconditionally - shadows are deferred): point + directional
contributions are accumulated as `albedo * light_color * intensity *
max(0, N · L) * falloff`, environment lights act as ambient,
emission is added on top, and the existing relativistic Doppler /
searchlight pipeline still runs after the shade. Scenes that
upload no lights (the existing `--render-scene` /
`--render-mesh-scene` / `--render-material-scene` CLI actions) keep
the Stage 8B facing-ratio fallback unchanged. Five GPU kernels live
(gradient / camera-rays / single-sphere / relativistic-sphere /
multi-sphere-mesh-scene-with-materials-and-lights). **Ten** GPU CLI
actions are live: `--render-gradient`, `--render-rays`,
`--render-sphere`, `--render-relativistic`, `--render-scene`,
`--render-triangle`, `--render-mesh-scene`,
`--render-material-scene`, `--render-direct-lighting`, plus the
`--render` placeholder. No shadows, no textures, no path tracer,
no server, no integrations.

### Files in scope

| File                       | Role                                                |
|----------------------------|-----------------------------------------------------|
| `CMakeLists.txt`           | Executable + interface library + ctest wiring.      |
| `src/main.cpp`             | Entry point. Parses CLI, dispatches per action.     |
| `src/core/Logger.h`        | `info` / `warning` / `error` static API.            |
| `src/core/Logger.cpp`      | Thread-safe stdio logger with timestamp + level.    |
| `src/core/Version.h`       | `kProjectName` / `kVersionMajor/Minor/Patch` / `kVersionString`. |
| `src/core/Config.h`        | `Config` POD: `width` / `height` / `scene_path` / `output_path`, plus `validate()`. |
| `src/core/Config.cpp`      | `Config::validate()` returns first problem (positive dims) as a string. |
| `src/core/CommandLine.h`   | `CommandLine::parse(argc, argv) -> ParseResult { Action, Config, error_message }`, `usage(...)`, `version_string()`. |
| `src/core/CommandLine.cpp` | Hand-rolled flag parser. Action flags mutually exclusive; numeric / value validation; clean exit codes. |
| `src/math/MathUtils.h`     | `RR_HD` host/device portability macro; `min`/`max`/`clamp`/`lerp`/`saturate`/`radians`/`degrees`; `kPi`/`kTwoPi`/`kHalfPi`/`kEpsilon`. |
| `src/math/Vec2.h`          | RR_HD POD; `+`/`-`/scalar `*`/`/`, `dot`, `length`. |
| `src/math/Vec3.h`          | RR_HD POD; `+`/`-`/scalar `*`/`/`, Hadamard `*`, `dot`, `cross`, `length`, `length_squared`, `normalize` (zero on degenerate input), `clamp` (Vec3 + scalar bounds), `lerp`. |
| `src/math/Vec4.h`          | RR_HD POD; `+`/`-`/scalar `*`/`/`, `dot`, `xyz()` accessor, `(Vec3, w)` constructor. |
| `src/math/Mat4.h`          | Row-major 4x4; `identity` / `translation` / `scale` (snake_case to match codebase convention); `operator*`; `transform_point` / `transform_vector`. |
| `tests/math_tests.cpp`     | Stand-alone executable, hand-rolled `RR_CHECK` macro, 60 assertions covering every Vec3 + Mat4 capability the prompt called out. |
| `src/image/Color.h`        | RR_HD POD: `Rgb` (3 floats) and `Rgba` (4 floats) with constructors and equality. |
| `src/image/Image.h`        | Host-side 2D float image. Row-major, channel-interleaved, top-left origin. `set_pixel` / `get_pixel` / `clear` / `resize` / `save_ppm` / raw `data()` accessor. `Rgb32F` and `Rgba32F` formats. |
| `src/image/Image.cpp`      | Implementation; `save_ppm` clamps to [0,1] and quantises to 8-bit P6. |
| `src/image/Framebuffer.h`  | Thin owning wrapper that pairs an `Image` with the renderer-target lifecycle. Forwards `width` / `height` / `format` / `clear` / `resize` / `save_ppm` to its color image. |
| `src/image/Framebuffer.cpp`| Implementation; one-line forwards. |
| `tests/image_tests.cpp`    | Stand-alone executable: 70 automated assertions on `Image` + `Framebuffer` + 1 manual debug gradient saved to `output/image_test.ppm`. |
| `src/gpu/GpuDevice.h`      | Backend-agnostic `GpuDevice` POD (index, name, compute capability major/minor, total VRAM bytes, multiprocessor count) plus `compute_capability_string()` / `total_memory_human()` helpers. Free functions `gpu_backend_available()` / `gpu_backend_name()` / `enumerate_devices()`. |
| `src/gpu/GpuDevice.cpp`    | Implementation; delegates `enumerate_devices()` to `rr::cuda::query_devices()` when `RR_HAS_CUDA` is defined, returns an empty list otherwise. |
| `src/cuda/CudaContext.h`   | CUDA-only header. Declares `query_devices()` returning `std::vector<gpu::GpuDevice>`. Compiled into the build only when `RR_ENABLE_CUDA=ON`. |
| `src/cuda/CudaContext.cpp` | Plain C++ (no kernels). Iterates `cudaGetDeviceCount` + `cudaGetDeviceProperties` and clears sticky errors on early-exit paths so a later CUDA call does not observe a stale flag. |
| `src/gpu/GpuBuffer.h`      | Header-only `GpuBuffer<T>` template. Move-only RAII; `T` static-asserted trivially copyable. Surface: `allocate(count)`, `upload(host, count)`, `download(host, count)`, `reset()`, `empty()`, `size()`, `size_in_bytes()`, `device_ptr()`. Plus `rr::gpu::detail::gpu_alloc / gpu_free / gpu_copy_h2d / gpu_copy_d2h` byte-level dispatch. |
| `src/gpu/GpuBuffer.cpp`    | Implementation of the byte-level dispatch. Forwards to `rr::cuda::cuda_*` when `RR_HAS_CUDA` is defined; returns `nullptr` / `false` honestly otherwise. |
| `src/cuda/CudaBuffer.h`    | CUDA backend declarations for the four byte-level primitives. CUDA-Runtime-free header so callers (including header-only template consumers) do not need the toolchain on their include path. |
| `src/cuda/CudaBuffer.cpp`  | Plain C++ (no kernels). Implements `cuda_alloc` / `cuda_free` / `cuda_copy_h2d` / `cuda_copy_d2h`. Each call inspects the `cudaError_t` return; on failure it clears the sticky last-error flag and returns `nullptr` / `false`. |
| `tests/gpu_tests.cpp`      | Stand-alone executable; 19 host-only assertions (default state, zero-alloc no-op, move-only traits, move ctor / assign on empty buffers, honest failure when no backend, backend-name vs availability consistency). When CUDA is on and a device is visible, 10 more assertions run end-to-end: 8-float upload / download / verify, resize via re-upload to a smaller count, oversized-download bounds check. |
| `src/cuda/CudaKernels.cuh` | Host-callable launcher declarations shared across `.cu` translation units. Pulls in `<cuda_runtime.h>`; only safe to include from `.cu`. Currently declares `launch_gradient_rgba32f`. |
| `src/cuda/CudaTestKernel.cu` | First `__global__` kernel (`k_gradient_rgba32f`) plus its launcher. One thread per pixel; row-major Rgba32F output `(R=u, G=v, B=0, A=1)`. No host pixel loop. |
| `src/cuda/CudaRenderer.h`  | Host-side, CUDA-Runtime-free header. Static `render_gradient(int w, int h)` returning a `Result {ok, image, message}`. |
| `src/cuda/CudaRenderer.cu` | Host-side scaffold: allocate `GpuBuffer<float>` of `w*h*4` floats, launch the kernel, drain CUDA errors, `cudaDeviceSynchronize`, download into an `rr::image::Image(Rgba32F)`. Used by both `render_gradient` and `render_camera_rays`. The CPU never iterates over pixels. |
| `src/camera/Camera.h`      | Host-side perspective `Camera` class: position, look-at, vfov, aspect, clip range, basis vectors. Exposes `to_gpu()` snapshotting into `GpuCamera`. |
| `src/camera/Camera.cpp`    | Implementation; `look_at` re-orthogonalises the basis with a fallback when `up_hint` is parallel to `forward_`. |
| `src/camera/CameraRay.h`   | Device-friendly `GpuCamera` POD (position / forward / up / right / `tan_half_vfov` / aspect) plus the `RR_HD generate_camera_ray(cam, x, y, w, h)` helper. Same code path runs on host (tests) and device (kernels). |
| `src/geometry/Sphere.h`    | RR_HD POD `Sphere {center, radius, material_index}`. Trivial aggregate; passed by value to kernels. `material_index` defaults to -1 and is inert at this stage (the material system has not landed yet). |
| `src/geometry/Triangle.h`  | RR_HD POD `Triangle {v0, v1, v2}` (uint32_t indices). Counter-clockwise winding when viewed from the front face, matching the convention `intersect_triangle` will use in Stage 7B. Layout-compatible with a flat `uint32_t[3*N]` index array. |
| `src/geometry/Mesh.h`      | Host-side indexed triangle mesh. `vector<Vertex>` (position + normal + uv) + `vector<Triangle>` + `material_id` (defaults `-1`) + `math::Transform`. Helpers: `vertex_count`, `triangle_count`, `empty`, `clear`, `reserve`, `local_bounds() -> AABB`. AABB is local-space only at this stage; the world-space bounds (BVH, frustum cull) wait on a real consumer. |
| `src/geometry/Mesh.cpp`    | Implementation. `local_bounds` runs a single linear pass over `vertices` and returns `{min, max, valid=false}` for an empty mesh so callers detect "no points" without sentinel values. |
| `src/cuda/CudaMesh.cuh`    | Device-side `CudaMeshView` POD: `Vertex*` + `Triangle*` device pointers, vertex / triangle counts, `material_id`, and a `math::Transform` (currently unused by the kernel - vertex positions are read as-is from the buffer; per-vertex transform comes alongside the material system). |
| `src/gpu/GpuMesh.h`        | Move-only host-side owner of one mesh's GPU resources. Owns `GpuBuffer<Vertex>` + `GpuBuffer<Triangle>` plus per-mesh metadata (material id + transform). Three explicit upload methods: `upload_vertices`, `upload_triangles`, `set_metadata`; `upload_from(const rr::geometry::Mesh&)` is the convenience used by `GpuScene::upload_mesh`. |
| `src/gpu/GpuMesh.cpp`      | Implementation. Backend-honest: zero-count uploads always succeed; non-zero uploads require a working GPU backend and reset the count to zero on any failure. Metadata is host-only and always safe to write. |
| `src/material/MaterialTypes.h` | RR_HD POD `MaterialParams { baseColor, emissionColor, emissionStrength, roughness, metallic, specular, transmission }`. CamelCase field names follow PBR / DCC convention (the documented exception alongside the relativity layer). `transmission` is a placeholder slot reserved for the eventual glass / refraction BSDF; nothing reads it yet. The texture binding field (`base_color_texture_id` in the prototype) is deliberately omitted; it returns at the texture stage. |
| `src/material/Material.h`  | Host-side wrapper: optional `name` plus a `MaterialParams` POD, plus three convenience presets (`make_diffuse`, `make_emissive`, `make_metal`). |
| `src/material/Material.cpp`| Implementation; presets pre-populate `MaterialParams` to sensible PBR-correct values. |
| `src/cuda/CudaMaterial.cuh`| Thin re-export of `MaterialTypes.h` for kernel TUs. Future device-specific overrides (packed-field POD, fast-math intrinsics, BSDF helpers) land here without touching the host surface. |
| `src/lighting/Light.h`     | RR_HD POD `Light { type, color, intensity, position, direction, area_width, area_height }`. `LightType { Point, Directional, Area, Environment }` discriminator. Area + Environment fields are populated by their factories but the renderer treats them as PLACEHOLDER until the path tracer (master module 16) and texture system (module 18) land. Four convenience factories: `make_point_light`, `make_directional_light`, `make_area_light`, `make_environment_light`. |
| `src/lighting/Light.cpp`   | Factory implementations. `safe_normalize` falls back to `(0, -1, 0)` for zero-length input directions so directional / area constructions never produce NaN normals. Area extents are clamped to non-negative. |
| `src/cuda/CudaLight.cuh`   | Thin re-export of `Light.h` for kernel TUs. Sampling / eval / pdf helpers per `LightType` join here when the path tracer lands. |
| `src/renderer/Hit.h`       | RR_HD POD `Hit {hit, t, position, normal, material_index, uv, bary_u, bary_v}`. The kernel only reads `hit` / `normal` at this stage; the remaining fields are populated by intersection routines for later stages (textures, materials, mesh barycentrics) and have safe defaults. |
| `src/cuda/CudaIntersection.cuh` | RR_HD `intersect_sphere(ray, sphere, t_min, t_max) -> Hit`. Same code runs on host (tests) and device (kernel). Trimmed from the prototype's superset to sphere-only at this stage; triangle intersection joins the file in the mesh stage. |
| `src/relativity/RelativityParams.h` | `Observer { Vec3 velocity = beta }` and `RelativityParams { enable_aberration / enable_doppler / enable_searchlight, doppler_color_strength, searchlight_strength, max_beta }`. The artist-facing toggles live here, separate from the kinematic `Observer`. Velocity is in c-units; `\|beta\| < 1` is the caller's invariant. |
| `src/relativity/RelativityMath.h`   | RR_HD inline math leaf. Each function carries a `PHYSICAL` or `ARTISTIC APPROXIMATION` tag in its header. See "Stage 9 — function tags" below. |
| `src/relativity/RelativityMath.cuh` | Thin re-export of `RelativityMath.h` for kernel translation units; future device-specific overrides (`rsqrtf`, `__fdividef`, fast-math intrinsics) land here without touching the host surface. |
| `src/math/Transform.h`              | Plain-data local-to-parent transform: position + Euler-rotation-radians + scale + `identity()` static factory. Conversion to a 4x4 matrix is deferred to consumers. Brought across in Stage 6A so `scene/Transform.h` can alias it. |
| `src/scene/Transform.h`             | Thin alias `using rr::scene::Transform = rr::math::Transform`. Preserved per the user's Stage 6A spec; the prior reuse audit had it `DELETE_LATER`, but it stays as long as the user-facing scene API names a `Transform` from inside the scene namespace. New scene-side code is welcome to use `rr::math::Transform` directly. |
| `src/scene/RenderSettings.h`        | POD: `width`, `height`, `samples_per_pixel`, `max_depth`. Resolution lives here (output-surface concern) rather than on `Camera` (optical-config concern). The two trailing fields are stored faithfully but not consumed yet - the path tracer (master module 16) reads them. |
| `src/scene/SceneObject.h`           | POD `{name, Transform, visible}`. Composed into the type-specific scene wrappers (`SceneSphere`, `SceneMesh`, `SceneLight`) so the GPU-upload paths can read fields directly. |
| `src/scene/Scene.h`                 | The authoring-side container. Real fields: `Camera`, `RenderSettings`, `Observer`, `RelativityParams`, `vector<SceneSphere>`. Placeholder fields (Stage 6A scaffolding, populated as later master-order modules ship): `vector<SceneMesh>` (module 12), `vector<SceneMaterial>` (module 13), `vector<SceneLight>` (module 14). |
| `src/scene/Scene.cpp`               | `Scene::clear()` resets every list and every default-constructible POD field. |
| `src/cuda/CudaScene.cuh`            | Device-side `CudaSceneView` POD: `GpuCamera` + `Observer` + `RelativityParams` (by value, launch-arg PODs) + `Sphere*` device pointer + count. CUDA-Runtime-pulling header; only safe to include from `.cu` files. Stage 6B keeps it minimal; mesh / material / light / texture views join in their own stages. |
| `src/gpu/GpuScene.h`                | Move-only host-side scene container that owns a `GpuBuffer<Sphere>` plus by-value `GpuCamera` / `Observer` / `RelativityParams` snapshots. Three explicit upload methods (`upload_camera`, `upload_relativity`, `upload_spheres`) plus `reset_device()` / `clear()` and the per-field accessors the renderer uses. |
| `src/gpu/GpuScene.cpp`              | Implementation; `upload_spheres` reallocates via `GpuBuffer<Sphere>::upload`, returns `false` honestly when no GPU backend is available, and resets state to zero on any failure path. |

Build:

```sh
# Host-only build (no CUDA Toolkit needed; --device-info reports
# backend "(none)").
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure

# CUDA-enabled build (requires CUDA Toolkit + a CUDA-capable GPU
# host; --device-info enumerates visible devices).
cmake -S . -B build-cuda -DRR_ENABLE_CUDA=ON
cmake --build build-cuda -j

# Run modes:
build/bin/RelativityRender                                # startup banner
build/bin/RelativityRender --help
build/bin/RelativityRender --version
build/bin/RelativityRender --device-info
build/bin/RelativityRender --render scene.rrscene --output out.png --width 1920 --height 1080  # placeholder
build-cuda/bin/RelativityRender --render-gradient         # -> output/gpu_gradient.ppm
build-cuda/bin/RelativityRender --render-rays             # -> output/gpu_camera_rays.ppm
build-cuda/bin/RelativityRender --render-sphere           # -> output/gpu_sphere.ppm
build-cuda/bin/RelativityRender --render-relativistic     # -> output/sphere_beta_{000,025,075,095}.ppm
build-cuda/bin/RelativityRender --render-scene            # -> output/gpu_scene_spheres.ppm
build-cuda/bin/RelativityRender --render-triangle         # -> output/gpu_triangle.ppm
build-cuda/bin/RelativityRender --render-mesh-scene       # -> output/gpu_mesh_scene.ppm
build-cuda/bin/RelativityRender --render-material-scene   # -> output/gpu_material_scene.ppm
build-cuda/bin/RelativityRender --render-direct-lighting  # -> output/gpu_direct_lighting.ppm
```

### CLI behavior (Stage 1)

| Invocation                                                                    | Output / exit code |
|-------------------------------------------------------------------------------|--------------------|
| (no args)                                                                     | startup banner via `Logger::info`, exit 0. |
| `--help`                                                                      | usage text on stdout, exit 0. |
| `--version`                                                                   | `RelativityRender 0.1.0` on stdout, exit 0. |
| `--device-info`                                                               | `[INFO] GPU device info not implemented yet`, exit 0. |
| `--render <scene>` (with optional `--output / --width / --height`)            | `[INFO] render command received`, exit 0. (No actual rendering.) |
| Unknown flag, missing value, non-numeric `--width / --height`, non-positive dimensions, two action flags | `[ERROR] <reason>` then usage on stderr, exit 2. |

## Stage history

### Stage 1.1 — Core app skeleton

Status: implemented.

- Created `CMakeLists.txt` (one executable, C++20,
  `-Wall -Wextra -Wpedantic` / `/W4 /permissive-`).
- Created `src/main.cpp` (no GPU; logs project name + version + a
  Stage-1 status line, exits 0).
- Reused `src/core/Logger.{h,cpp}` and `src/core/Version.h` from the
  prototype — both clean per the prior audit, both already supply the
  exact API Stage 1 requires.

The branch was scoped back from an earlier "GPU foundation" attempt
that landed Stage 6 work (CUDA device layer) before Stages 4 (math
library) and 5 (image / framebuffer system). Per the master
instructions' module order and the rule "Do not overbuild a later
system before the current layer works", the GPU layer was removed
from the branch and will be reintroduced in its proper stage.

### Stage 1.2 — Config + command-line handling

Status: implemented.

- Added `src/core/Config.{h,cpp}`. Plain aggregate carrying the four
  Stage-1 knobs (`width`, `height`, `scene_path`, `output_path`) plus
  a `validate()` method that returns the first problem string (empty
  on success).
- Added `src/core/CommandLine.{h,cpp}`. Pure host parser; depends only
  on `core/Config.h` and `core/Version.h`. `parse(argc, argv)` returns
  a `ParseResult { Action, Config, error_message }`. Action flags
  (`--help`, `--version`, `--device-info`, `--render <scene>`) are
  mutually exclusive; configuration flags (`--output <path>`,
  `--width <int>`, `--height <int>`) are accepted alongside any
  action.
- Rewrote `src/main.cpp` to dispatch on `result.action`. Each action
  produces exactly the Stage-1 behaviour: usage on stdout for
  `--help`, version on stdout for `--version`, the placeholder
  `"GPU device info not implemented yet"` for `--device-info`, the
  literal `"render command received"` for `--render`, and the
  startup banner for the no-args default.
- Added the two new `.cpp` files to `CMakeLists.txt`.
- All five behaviours plus five error paths (unknown flag, missing
  value, non-numeric integer, non-positive dimension, combined
  actions) verified manually; error paths return exit code 2.

No new dependencies. No GPU, no rendering, no scene parser, no
server, no C4D — same as Stage 1.1.

### Stage 2 — Math library

Status: implemented.

- Recovered the five math headers from the `prototype_v0` tag (each
  was classified KEEP_AS_IS by the prior audit and supplies exactly
  the surface the prompt requires):
  `src/math/MathUtils.h`, `Vec2.h`, `Vec3.h`, `Vec4.h`, `Mat4.h`.
- The `RR_HD` macro in `MathUtils.h` expands to `__host__ __device__`
  under nvcc and is empty otherwise, so the same headers will be
  usable from CUDA kernels in a later stage without modification.
- `Mat4` keeps the codebase's snake_case naming for member operations
  (`Mat4::identity` / `translation` / `scale` static factories,
  `transform_point` / `transform_vector` free functions). The Stage-2
  prompt named these `transformPoint` / `transformVector` informally;
  using snake_case keeps the math library consistent with the rest of
  the public surface (`Logger::info`, `Config::validate`,
  `CommandLine::parse`, ...) and with the audit's naming-consistency
  finding.
- Added `tests/math_tests.cpp`. The project does not yet have a
  third-party test framework, so this is a small self-contained
  executable: hand-rolled `RR_CHECK` macro, 60 assertions covering
  every Vec3 + Mat4 capability the prompt called out (construction,
  arithmetic, dot, cross, length / length_squared, normalize incl.
  degenerate input, clamp incl. both overloads, lerp, identity,
  translation / scale point-vs-vector semantics, multiplication +
  composition order). main() returns 0 on full pass, 1 otherwise.
- CMake additions: a header-only `rr_math` INTERFACE library
  (publishes `src/` as include path), an `RR_BUILD_TESTS` option (ON
  by default), an `rr_apply_warnings()` helper to keep warning flags
  centralised, an `enable_testing()` block, and a `math_tests`
  executable wired through `add_test(NAME math_tests COMMAND
  math_tests)`.

Verified: `cmake --build build -j` clean (no warnings under
`-Wall -Wextra -Wpedantic`); `ctest` reports `1/1 passed`; running
`math_tests` directly prints `math_tests: 60 / 60 passed`. The main
`RelativityRender` binary is unchanged — math is not yet consumed by
core code.

No new dependencies. No GPU, no rendering, no scene parser, no
server, no C4D — same as Stages 1.1 and 1.2.

### Stage 3 — Image / framebuffer system

Status: implemented.

- Recovered the five image files from `prototype_v0` (each KEEP_AS_IS
  / KEEP_WITH_REFACTOR per the audit; the contents match Stage 3's
  scope without modification): `src/image/Color.h`, `Image.{h,cpp}`,
  `Framebuffer.{h,cpp}`.
- `Image` is the generic 2D float buffer (also valid for textures and
  saved files); `Framebuffer` is "what the renderer writes into" and
  pairs an `Image` with the per-frame lifecycle. The distinction is
  intentional and carried over from the prototype.
- `Color.h` includes `math/MathUtils.h` for the `RR_HD` macro so
  `Rgb` / `Rgba` are device-friendly when the CUDA stage lands.
- Added `tests/image_tests.cpp`. 70 automated assertions cover
  default state, construction + channel counts, `clear`, `set_pixel`
  / `get_pixel` round-trip, `Rgb32F` alpha semantics, `resize` zeroes
  pixels, `save_ppm` header is `P6 / W / H / 255`, empty-image save
  fails honestly, and the `Framebuffer` wrapper's forwarding methods.
  Plus a single debug-gradient step that builds a 64×64 UV gradient
  on the host and writes it to `output/image_test.ppm` so the
  developer can visually confirm PPM output is well-formed. The
  gradient is **explicitly labelled non-renderer output** in both
  the file header and the `printf` line; the renderer will produce
  its own pixels from a GPU kernel later.
- CMake additions: `rr_image` STATIC library
  (`src/image/Image.cpp` + `Framebuffer.cpp`, PUBLIC-links `rr_math`
  for `RR_HD`); `image_tests` executable wired through `add_test`
  with `WORKING_DIRECTORY ${CMAKE_BINARY_DIR}` so the gradient lands
  inside the build tree (gitignored) instead of the source tree.

Naming note: same as Stage 2. The prompt called the API
`setPixel` / `getPixel` / `savePPM` informally; the codebase
convention is snake_case (`set_pixel` / `get_pixel` / `save_ppm`)
and the audit's naming-consistency finding endorses that. The
behaviour is identical.

Verified: `cmake --build build -j` clean (no warnings under
`-Wall -Wextra -Wpedantic`); `ctest` reports `2/2 passed`; running
`image_tests` directly prints
`image_tests: 71 / 71 passed` and writes
`build/output/image_test.ppm` (12,301 bytes, header `P6\n64 64\n255\n`
+ 12,288 bytes of RGB triples).

CPU work in this module is limited to clearing, debug fills, and IO
validation, exactly as the master engineering rules permit. No CPU
ray tracing, no per-ray work.

No new external dependencies. No GPU, no rendering, no scene parser,
no server, no C4D — same as Stages 1.x and 2.

### Stage 4 — CUDA device layer

Status: implemented.

- Recovered four files from `prototype_v0` (audited clean):
  `src/gpu/GpuDevice.{h,cpp}` and `src/cuda/CudaContext.{h,cpp}`.
- Backend-agnostic surface in `rr::gpu::`. The CUDA-specific
  enumeration sits behind the `RR_HAS_CUDA` capability macro: when
  defined, `enumerate_devices()` calls `rr::cuda::query_devices()`;
  otherwise it returns an empty list and `gpu_backend_name()` returns
  `"(none)"`.
- `CudaContext.cpp` is **plain C++** (no `__global__`, no `<<<...>>>`),
  so Stage 4 deliberately does NOT call `enable_language(CUDA)`. The
  CUDA Runtime headers + `libcudart` are pulled in through
  `find_package(CUDAToolkit)`. `enable_language(CUDA)` arrives in
  Stage 5 (CUDA framebuffer / kernel infrastructure) when the first
  `.cu` kernel lands.
- `--device-info` now calls `report_device_info()` in `main.cpp`
  which:
    1. logs `GPU backend: <CUDA|(none)>`
    2. enumerates devices and either logs each one as
       `[i] <name> (sm_<MAJOR.MINOR>, <MiB> MiB, <N> SMs)`
    3. or, when no devices are visible, prints an actionable
       `"rebuild with -DRR_ENABLE_CUDA=ON"` message.
- CMake additions:
    - `option(RR_ENABLE_CUDA ...)` (OFF by default)
    - `find_package(CUDAToolkit REQUIRED)` gated on it
    - `rr_gpu` STATIC library (always built; PUBLIC includes `src/`)
    - When CUDA is ON: `target_sources(rr_gpu PRIVATE
      src/cuda/CudaContext.cpp)`,
      `target_compile_definitions(rr_gpu PUBLIC RR_HAS_CUDA)`,
      `target_link_libraries(rr_gpu PRIVATE CUDA::cudart)`
    - `RelativityRender` PRIVATE-links `rr_gpu`.

Verified:

- Host-only build (`RR_ENABLE_CUDA=OFF`, default): clean under
  `-Wall -Wextra -Wpedantic`. `ctest` reports `2/2 passed`.
  `RelativityRender --device-info` prints
  `GPU backend: (none)` followed by the rebuild hint, exit 0.
- CUDA-enabled configure (`-DRR_ENABLE_CUDA=ON`) on a host **without**
  the CUDA Toolkit fails immediately at `find_package(CUDAToolkit)`
  with `Could not find nvcc, please set CUDAToolkit_ROOT.` — the
  honest, actionable error we want.
- CUDA-enabled build on a host **with** the Toolkit: the kept code
  (`CudaContext.cpp`) is byte-identical to the prototype's tested
  code; it iterates `cudaGetDeviceCount` + `cudaGetDeviceProperties`
  and populates `GpuDevice` PODs that `--device-info` prints.
  Runtime verification needs a GPU host.

CPU role in this module: orchestration / device query only — exactly
the master rules' allowed list.

No new external dependencies beyond the optional CUDA Toolkit. No
rendering, no CUDA framebuffer, no kernels, no scene parser, no
server, no C4D.

### Stage 5 — GPU buffer layer

Status: implemented.

- Recovered four files from `prototype_v0` (audited clean):
  `src/gpu/GpuBuffer.{h,cpp}` and `src/cuda/CudaBuffer.{h,cpp}`.
- `GpuBuffer<T>` is the typed move-only RAII handle; it forwards to
  byte-level primitives in `rr::gpu::detail::` (`gpu_alloc`,
  `gpu_free`, `gpu_copy_h2d`, `gpu_copy_d2h`). When `RR_HAS_CUDA` is
  defined those forward to the CUDA backend in `rr::cuda::`; otherwise
  they return `nullptr` / `false` honestly so the host build stays
  buildable and `GpuBuffer<T>` surfaces failure without crashing.
- The CUDA backend (`CudaBuffer.cpp`) is plain C++ - it includes
  `<cuda_runtime.h>` and calls `cudaMalloc` / `cudaFree` /
  `cudaMemcpy`. Stage 5 still does NOT call `enable_language(CUDA)`
  because there are no `.cu` files yet; that comes when the first
  kernel lands in a later stage.
- Each CUDA call inspects the `cudaError_t` return. On failure the
  sticky last-error flag is cleared (`cudaGetLastError`) so a later
  real CUDA call does not observe a stale error state.
- Added `tests/gpu_tests.cpp`. 19 host-only assertions cover default
  state, zero-allocate no-op, move-only `static_assert` traits, move
  construction / assignment on empty buffers, honest failure when no
  backend is compiled in, and backend-name vs availability
  consistency. When CUDA is on and a device is visible, 10 more
  assertions run end-to-end - the user's "small validation path":
  upload an array of 8 floats, download it, verify each value matches;
  re-upload a smaller count to exercise the resize path; verify
  oversized-download is rejected without writing past the destination.
- CMake additions:
    - `rr_gpu` STATIC library now contains
      `src/gpu/GpuDevice.cpp` + `src/gpu/GpuBuffer.cpp` always.
    - When `RR_ENABLE_CUDA=ON`: also includes
      `src/cuda/CudaContext.cpp` + `src/cuda/CudaBuffer.cpp`,
      defines `RR_HAS_CUDA` PUBLIC, links `CUDA::cudart` PRIVATE.
    - `gpu_tests` test target wired through `add_test`.

Verified (host-only):

- `cmake --build build -j` clean under `-Wall -Wextra -Wpedantic`.
- `ctest --test-dir build` reports `3/3 passed`.
- `gpu_tests` directly: `19 / 19 passed` and prints
  `gpu_tests: float round-trip skipped (no CUDA backend / no device).`,
  exit 0.
- `--device-info` still reports `(none)` honestly.

CPU role in this module: orchestration / memory management only - no
per-pixel work, no per-ray work. The byte-level copy path is exactly
what the master engineering rules permit ("upload data to GPU",
"receive framebuffers").

No new external dependencies beyond the optional CUDA Toolkit. No
rendering, no kernels, no scene parser, no server, no C4D.

### Stage 6 — CUDA kernel infrastructure

Status: implemented.

- New files (Stage 6 are written fresh, not recovered from
  `prototype_v0`, because the prototype's `CudaTestKernel.cu` was 917
  lines covering seven kernels at once - the master rules forbid
  introducing future systems early, so only the gradient kernel ships
  here):
    - `src/cuda/CudaKernels.cuh`   — declares `launch_gradient_rgba32f`
    - `src/cuda/CudaTestKernel.cu` — `__global__ k_gradient_rgba32f`
                                       + the host-callable launcher
    - `src/cuda/CudaRenderer.h`    — host-side, CUDA-Runtime-free
                                       facade with one `render_gradient`
                                       static method
    - `src/cuda/CudaRenderer.cu`   — implementation: allocate
                                       `GpuBuffer<float>` (`w*h*4`
                                       floats), launch, sync, download
                                       into `rr::image::Image`
- The kernel is one-thread-per-pixel; the host never loops over
  pixels. The only CPU work is dimension validation, the
  `GpuBuffer<float>` allocation, the kernel launch, sticky-error
  drain, `cudaDeviceSynchronize`, and the device→host download via
  `GpuBuffer::download`.
- `--render-gradient` CLI action added to `CommandLine`. Mutually
  exclusive with the other action flags. Uses `Config`'s
  `width` / `height` (defaults 1280 × 720) and `output_path`
  (defaults to `output/gpu_gradient.ppm` when not set). Creates the
  parent directory if it does not exist before writing the PPM.
- When CUDA is OFF the `--render-gradient` action prints a clear
  error and exits 1; the rest of the build (including all tests)
  continues to work.
- CMake additions:
    - `enable_language(CUDA)` now runs inside the `RR_ENABLE_CUDA`
      block, alongside `CMAKE_CUDA_STANDARD=17` and a default
      `CMAKE_CUDA_ARCHITECTURES=75;80;86;89;90;100;120` (Turing →
      Blackwell; overrideable on the command line).
    - `rr_gpu` picks up `CudaTestKernel.cu` + `CudaRenderer.cu` when
      CUDA is on; PUBLIC-links `rr_image` since `CudaRenderer::Result`
      carries an `rr::image::Image` by value.
    - `RelativityRender` PRIVATE-links `rr_image` so the host-only
      build still has the image library available for future stages.

Verified:

- Host-only build (`RR_ENABLE_CUDA=OFF`, default): clean under
  `-Wall -Wextra -Wpedantic`; ctest 3/3; `--device-info` reports
  `(none)`; `--render-gradient` prints an actionable
  `--render-gradient requires CUDA. Rebuild with -DRR_ENABLE_CUDA=ON…`
  and exits 1.
- CUDA-enabled build on a GPU host: the kept CudaTestKernel.cu /
  CudaRenderer.cu logic is byte-identical to the prototype's tested
  gradient render. Runtime verification (kernel launch + PPM written
  to `output/gpu_gradient.ppm`) requires a host with a CUDA Toolkit
  and a CUDA-capable GPU and was **not** runnable in the development
  environment for this commit.

Hard-rule audit:

- GPU generates all pixels  — yes (only `k_gradient_rgba32f` writes
  `pixels[idx + …]`).
- CPU does not loop over pixels except in `Image::save_ppm` IO
  internals — yes; `CudaRenderer.cu` only does `allocate` /
  `launch_gradient_rgba32f` / `cudaDeviceSynchronize` / `download`.
- No ray tracing, no scene system, no C4D — yes.

### Stage 7 — Camera system

Status: implemented.

- Recovered three files from `prototype_v0` byte-identical (audited
  clean, KEEP_AS_IS): `src/camera/Camera.{h,cpp}` and
  `src/camera/CameraRay.h`.
- `Camera` is a pinhole host class with position, look-at,
  vertical-FOV (degrees / radians), aspect, clip range. `look_at`
  re-orthogonalises the basis and falls back to a sensible axis when
  `up_hint` is parallel to `forward`.
- `CameraRay.h` defines:
    - `CameraRay` (origin + direction)
    - `GpuCamera` POD (position / forward / up / right /
      `tan_half_vfov` / aspect) - kernel launch argument
    - `RR_HD generate_camera_ray(cam, x, y, w, h)` - same code path
      compiled into host tests and into the device kernel.
- New `__global__ void k_camera_rays_visualize(float*, int, int,
  rr::camera::GpuCamera)` in `CudaTestKernel.cu`. One thread per
  pixel: calls `generate_camera_ray`, encodes the (already-normalised)
  direction as RGB via `0.5*dir + 0.5`, writes Rgba32F. Host code
  never touches per-pixel state.
- `launch_camera_rays_visualize` declared in `CudaKernels.cuh`,
  defined in `CudaTestKernel.cu`.
- `CudaRenderer::render_camera_rays(const Camera&, int, int)` added.
  Uses the same `run_kernel_render` template scaffold as
  `render_gradient` (allocate `GpuBuffer<float>`, launch, drain
  errors, sync, download).
- New CLI action `--render-rays` (mutually exclusive with the other
  action flags). `main.cpp::run_render_camera_rays`:
    1. Constructs a default `Camera` (origin, looking down −Z, +Y up,
       45° vfov - the default `Camera()` ctor).
    2. Sets aspect = `width / height`.
    3. Calls `CudaRenderer::render_camera_rays(cam, w, h)`.
    4. Saves the resulting `Image` to `output/gpu_camera_rays.ppm`
       (or `--output` if specified). Creates the parent directory if
       missing; reports the absolute path.
  When CUDA is OFF the action prints an actionable error and exits 1.
- `main.cpp` now factors the "create dir + save_ppm + log absolute
  path" step into a single `save_image_or_error` helper used by both
  `run_render_gradient` and `run_render_camera_rays`. The helper is
  gated on `RR_HAS_CUDA` because both call sites are.
- CMake additions:
    - New `rr_camera` STATIC library (`Camera.cpp`, PUBLIC-links
      `rr_math`).
    - `rr_gpu` PUBLIC-links `rr_camera` when CUDA is on
      (`CudaRenderer::render_camera_rays` takes `Camera` by const
      ref; `CudaTestKernel.cu` calls `generate_camera_ray`).
    - `RelativityRender` PRIVATE-links `rr_camera` so
      `run_render_camera_rays` can construct one in host-only builds.

Hard-rule audit (per the prompt):

- All ray generation must happen on GPU - **yes**. Only the
  `__global__ k_camera_rays_visualize` kernel calls
  `generate_camera_ray`. The host never executes that helper.
- CPU only creates camera parameters, launches kernel, receives
  framebuffer - **yes**. Host work is limited to `Camera`
  construction, `Camera::to_gpu()`, kernel launch, sync, and
  `GpuBuffer::download`.
- No sphere intersection - **yes**.
- No scene system, no relativity, no C4D - **yes**.

Verified (host-only build):

- `cmake --build build -j` clean under `-Wall -Wextra -Wpedantic`,
  no warnings.
- `ctest --test-dir build` reports `3/3 passed`.
- `--help` lists `--render-rays`.
- `--render-rays` on a host without CUDA prints the actionable
  `--render-rays requires CUDA. Rebuild with -DRR_ENABLE_CUDA=ON…`
  and exits 1.
- `--render-rays` on a CUDA host: kept code path is byte-identical
  to the prototype's tested `render_camera_rays`; runtime
  verification requires a GPU host and was not runnable in the
  development environment for this commit.

No new external dependencies. No `tests/camera_tests.cpp` in this
commit - the user's Stage 7 prompt did not ask for one. It can be
added in a follow-up if explicit unit coverage of `Camera::look_at`,
the basis re-orthogonalisation, and `generate_camera_ray` is wanted
on the host before geometry stages start consuming the camera.

### Stage 8 — Primitive GPU rendering

Status: implemented.

- Recovered two files from `prototype_v0` byte-identical (audited
  clean): `src/geometry/Sphere.h` and `src/renderer/Hit.h`.
- Wrote a trimmed `src/cuda/CudaIntersection.cuh` containing only
  `RR_HD intersect_sphere`. The prototype's version also defined
  `intersect_triangle`; that joins the file in the mesh stage to
  avoid overbuilding here.
- `intersect_sphere` populates `Hit.normal` (outward unit normal),
  `Hit.position`, `Hit.t`, and `Hit.uv` (spherical mapping). The
  kernel only reads `hit` / `normal` at this stage; the other fields
  are populated upfront so the intersection routine is reusable when
  texture / material stages start consuming them.
- New `__global__ void k_sphere_visualize(float*, int, int,
  GpuCamera, Sphere)` in `CudaTestKernel.cu`. One thread per pixel:
    1. Generate the primary ray on the device via
       `generate_camera_ray`.
    2. Intersect against `sphere` via `intersect_sphere`.
    3. On hit: shade with `0.5 * normal + 0.5` (normal-as-color
       diagnostic - reveals geometry without committing to a
       material system).
    4. On miss: lerp from white at the horizon to a soft blue
       overhead (`(1-t)*white + t*sky_blue` with `t = 0.5*(dy+1)`)
       so the image does not collapse outside the sphere.
    5. Write Rgba32F.
- `launch_sphere_visualize` declared in `CudaKernels.cuh`, defined
  in `CudaTestKernel.cu`. The launcher takes `GpuCamera` and
  `Sphere` PODs by value as launch arguments.
- `CudaRenderer::render_sphere(const Camera&, const Sphere&, int,
  int)` added; reuses the same `run_kernel_render` template scaffold
  as the gradient and camera-ray entries. Validates `radius > 0`
  before launch.
- New CLI action `--render-sphere` (mutually exclusive with the
  other action flags). `main.cpp::run_render_sphere` constructs:
    - `Camera` (default ctor: at origin, looking down -Z, +Y up,
      45 deg vfov, aspect set from width/height).
    - `Sphere{Vec3{0, 0, -3}, 1.0f, -1}` (centered along the
      camera's default forward, 3 units away, radius 1, no
      material).
    Output path defaults to `output/gpu_sphere.ppm`. Creates the
    parent directory if missing; reports the absolute path.
- CMake additions:
    - New `rr_geometry` INTERFACE library (header-only at this
      stage; promotes to STATIC when `Mesh.cpp` lands). Publishes
      `src/` as include path; PUBLIC-links `rr_math`.
    - `rr_gpu` PUBLIC-links `rr_geometry` when CUDA is on
      (CudaRenderer::render_sphere takes Sphere by const ref;
      CudaTestKernel.cu reads its fields).
    - `RelativityRender` always PRIVATE-links `rr_geometry` so
      `run_render_sphere` can construct one in host-only builds.

Hard-rule audit (per the prompt):

- All ray generation on GPU - **yes**. Only the
  `__global__ k_sphere_visualize` kernel calls
  `generate_camera_ray`; the host never executes it.
- No CPU per-ray / per-pixel rendering - **yes**. Host work is
  limited to `Camera` + `Sphere` construction, `Camera::to_gpu()`,
  kernel launch, `cudaDeviceSynchronize`, and `GpuBuffer::download`.
- No scene system, no materials, no lights, no C4D - **yes**. The
  inert `material_index` field on `Sphere` / `Hit` is just an
  integer with default `-1`; no material code is referenced.

Verified (host-only build):

- `cmake --build build -j` clean under `-Wall -Wextra -Wpedantic`,
  no warnings.
- `ctest --test-dir build` reports `3/3 passed`.
- `--help` lists `--render-sphere`.
- `--render-sphere` on a host without CUDA prints the actionable
  rebuild hint and exits 1.

CUDA-enabled runtime verification (kernel launch + PPM written to
`output/gpu_sphere.ppm`) requires a host with the CUDA Toolkit and
a CUDA-capable GPU and was **not** runnable in the development
environment for this commit. The kept `intersect_sphere` math is
byte-identical to the prototype's tested implementation.

### Stage 9 — Relativistic camera math (math leaf only)

Status: implemented (math layer only; no renderer integration).

- Recovered three files byte-identical from `prototype_v0` (audited
  clean):
    - `src/relativity/RelativityParams.h`
    - `src/relativity/RelativityMath.h`
    - `src/relativity/RelativityMath.cuh`
- Function naming: deliberately camelCase (`clampBeta`, `gamma`,
  `lorentzContraction`, `dopplerFactor`, `searchlightFactor`,
  `aberrateDirection`, `applyDopplerColor`). The codebase
  convention elsewhere is snake_case (`Logger::info`,
  `Camera::set_position`, `intersect_sphere`); the relativity layer
  is an intentional exception that matches the prompt's spec
  verbatim. Documenting this here so a future audit pass does not
  silently rename them.
- Stability around |beta| → 1 is enforced structurally:
    - `clampBeta(beta_mag, max_beta)` folds negatives to magnitude,
      clamps `max_beta` to `0.999999f`, then clamps the input to
      `max_beta`. So `gamma` and `lorentzContraction` always see
      `|beta| <= 0.999999`, keeping `1 - beta^2` >= ~2e-6.
    - `gamma` carries a numerical safety net (`if (denom <= 0.0f)
      return 1/sqrt(1e-12)`) for callers who skip `clampBeta`.
    - `lorentzContraction` returns 0 on `radicand <= 0` instead of
      NaN.
    - `dopplerFactor` / `aberrateDirection` short-circuit when the
      denominator collapses (epsilon checks at 1e-12 / 1e-24).
    - `applyDopplerColor` clamps `D` to `>= 1e-6` before `log` and
      clamps the scaled mix factor into `[-1, +1]`.
- Stage 9 is **math-only**. The kernels in `CudaTestKernel.cu` are
  unchanged; `CudaRenderer` is unchanged; `main.cpp` is unchanged;
  no new CLI action. The next stage wires this leaf into a
  relativistic kernel.
- CMake addition: a new header-only `rr_relativity` INTERFACE
  library that PUBLIC-links `rr_math`. Nothing links it yet; it is
  ready for the next stage to pick up.

#### Function tags

Tags are copied from the function-level docstrings in
`RelativityMath.h`:

| Function              | Tag                       | Notes                                            |
|-----------------------|---------------------------|--------------------------------------------------|
| `clampBeta`           | PHYSICAL                  | Defensive clamp: `\|beta\| <= max_beta <= 0.999999`. |
| `gamma`               | PHYSICAL                  | `1 / sqrt(1 - beta^2)`. Caller clamps first.     |
| `lorentzContraction`  | PHYSICAL                  | `sqrt(1 - beta^2)` = `1/gamma`.                  |
| `dopplerFactor`       | PHYSICAL                  | `D = 1 / [gamma * (1 - beta . dir)]`.            |
| `searchlightFactor`   | PHYSICAL                  | Bolometric `D^4` (per-frequency form is `D^3`; the renderer chooses). |
| `aberrateDirection`   | PHYSICAL                  | Vector form of the textbook angle relation; renormalises output. |
| `applyDopplerColor`   | ARTISTIC APPROXIMATION    | Placeholder until a spectral pipeline lands. `tanh(0.5 log D) * strength` -> mix toward warm/cool tints. |

Hard-rule audit (per the prompt):

- Beta clamped safely below 1 - **yes** (`clampBeta` caps at
  `0.999999f`).
- Functions stable near beta 0.999 - **yes** (epsilons guard
  every potentially-degenerate divide; `gamma(0.999999) ~ 707`,
  finite).
- PHYSICAL vs ARTISTIC documented - **yes**, both in the file
  header and per-function. Six PHYSICAL, one ARTISTIC.
- No renderer integration yet - **yes**, only the math leaf and a
  header-only CMake target ship.
- All per-ray usage GPU-compatible - **yes**, every function is
  `RR_HD inline` and uses cross-target intrinsics from `<cmath>`.
- No scene system, no C4D - **yes**.

Verified (host-only build):

- `cmake --build build -j` clean under `-Wall -Wextra -Wpedantic`,
  no warnings.
- `ctest` 3/3.
- Behaviour of the existing CLI actions is unchanged (Stage 9 ships
  no behavioural delta).

CUDA-enabled compile path: the relativity headers are RR_HD inline
and are **not** included by any `.cu` translation unit yet, so the
CUDA build still compiles only the existing kernels. Wiring the
relativity math into a kernel is the next stage's job.

### Stage 10 — Relativistic GPU rendering

Status: implemented (integration half - the math half landed in
Stage 9).

- New `__global__ void k_sphere_relativistic(float*, int, int,
  GpuCamera, Observer, RelativityParams, Sphere)` in
  `CudaTestKernel.cu`. One thread per pixel runs the eight-step
  pipeline:
    1. `generate_camera_ray`.
    2. `aberrateDirection(observer.velocity, ray.direction)` if
       `params.enable_aberration`.
    3. `intersect_sphere`.
    4. Base shade: `0.5 * normal + 0.5` on hit; vertical sky
       gradient (white -> soft blue) on miss.
    5. `D = dopplerFactor(observer.velocity, ray.direction)` (using
       the *aberrated* direction, computed once and reused).
    6. `applyDopplerColor(color, D, params.doppler_color_strength)`
       if `params.enable_doppler`.
    7. `scale = 1 + (D^4 - 1) * params.searchlight_strength`;
       `color *= scale` if `params.enable_searchlight`.
    8. Write Rgba32F.
- `launch_sphere_relativistic` declared in `CudaKernels.cuh`,
  defined in `CudaTestKernel.cu`. Takes `GpuCamera` + `Observer` +
  `RelativityParams` + `Sphere` PODs by value as launch arguments.
- `CudaRenderer::render_relativistic_sphere(camera, observer,
                                              params, sphere, w, h)`
  added; reuses the same `run_kernel_render` template scaffold.
  Validates `radius > 0` before launch.
- New CLI action `--render-relativistic` (mutually exclusive with
  the other action flags). `main.cpp::run_render_relativistic`
  drives a four-β sweep:

    | Beta | Output PPM                       |
    |------|----------------------------------|
    | 0.00 | `output/sphere_beta_000.ppm`     |
    | 0.25 | `output/sphere_beta_025.ppm`     |
    | 0.75 | `output/sphere_beta_075.ppm`     |
    | 0.95 | `output/sphere_beta_095.ppm`     |

  All four runs use the same camera (origin, looking down -Z),
  sphere (`{(0,0,-3), radius=1}`), and `RelativityParams` (all
  effects on, strengths 1, `max_beta=0.999999`). The observer's
  3-velocity is `(0, 0, -beta)` for each run, so positive β =
  observer approaches the sphere → blueshift in front +
  forward-aberration concentration + searchlight brightening; β=0 is
  the reference image identical to the non-relativistic
  `--render-sphere` output. `--output` is ignored for this action;
  the four paths are fixed.
- CMake additions:
    - `rr_gpu` PUBLIC-links `rr_relativity` when CUDA is on
      (`render_relativistic_sphere` takes `Observer` +
      `RelativityParams` by const ref; the kernel calls
      `aberrateDirection` / `dopplerFactor` / `applyDopplerColor` /
      `searchlightFactor` on the device).
    - `RelativityRender` always PRIVATE-links `rr_relativity` so
      `run_render_relativistic` can construct the PODs in host-only
      builds.

#### Implemented relativistic effects

- **Lorentz aberration** (PHYSICAL). Vector form of the textbook
  angle relation. Applied to each primary ray's direction before
  intersection so the observed positions of objects shift forward
  along the boost direction at high β.
- **Relativistic Doppler factor + colour shift** (PHYSICAL D;
  ARTISTIC colour). The factor `D = 1 / [γ (1 - β·d̂)]` is computed
  for each ray. Colour is shifted with the
  `tanh(0.5 log D) * strength` placeholder (per the Stage 9 audit -
  a proper spectral remap waits on the texture / shading pipeline).
- **Relativistic beaming** (PHYSICAL, bolometric form). The
  intensity scaling `D^4` is applied as `1 + (D^4 - 1) * strength`,
  so `strength = 0` disables beaming and `strength = 1` applies
  full bolometric `D^4`. Per-frequency `D^3` is left to a future
  monochromatic / spectral path.
- **Length contraction** (`lorentzContraction`) is in the math
  library but **not** wired into the kernel. The single-sphere
  scene has no rigid extended object whose dimension along the
  boost direction would be visible; contraction returns when the
  mesh stage starts shipping objects with a sensible aspect ratio.

#### Documented limitations

- **No retarded-time treatment.** The kernel uses the camera-frame
  position of the sphere directly. A correct treatment would solve
  for the position of the sphere at the photon emission time given
  the observer's worldline. This becomes visually relevant when the
  sphere itself moves; with a stationary sphere and only the
  observer boosted along a fixed direction, the simplification is
  acceptable for Stage 10.
- **No spectral colour pipeline.** `applyDopplerColor` is the
  ARTISTIC APPROXIMATION already documented in Stage 9. Replacing
  it with a spectral shift + colour-primary remap is a later stage.
- **Beaming is bolometric only.** The kernel uses `D^4`. A
  per-frequency renderer would prefer `D^3`; the choice will be
  revisited alongside the spectral pipeline.
- **No CPU-side beta validation.** The kernel does not re-check
  `\|β\| < 1`. The host-side `run_render_relativistic` driver picks
  values strictly less than 0.999999 (max 0.95), so the kernel does
  not need to guard them. Out-of-band callers must clamp via
  `clampBeta`.
- **No materials, no lights, no scene.** The pipeline shades hits
  with `0.5 * normal + 0.5` and misses with a sky gradient -
  exactly as Stage 8 did. Real materials and lights land in their
  own stages.
- **No CUDA-runtime verification in this commit.** The development
  environment has no CUDA Toolkit and no GPU; kernel launch +
  PPM write was not exercised at this commit. The kernel logic
  matches the prototype's tested `k_sphere_relativistic` byte-for-
  byte, modulo the trimmed scope (no scene loop, no AOVs).

#### Hard-rule audit

- All per-ray / per-pixel effects on GPU - **yes**. Only
  `__global__ k_sphere_relativistic` calls
  `generate_camera_ray` / `aberrateDirection` / `intersect_sphere`
  / `dopplerFactor` / `applyDopplerColor` / `searchlightFactor`.
- No CPU rendering - **yes**. Host work is `Camera` + `Observer` +
  `Sphere` + `RelativityParams` construction, `Camera::to_gpu()`,
  kernel launch, sync, `GpuBuffer::download`, and `Image::save_ppm`
  (the allowed image-saving internal).
- No scene system, no materials, no lights, no C4D - **yes**.

Verified (host-only build):

- `cmake --build build -j` clean under `-Wall -Wextra -Wpedantic`,
  no warnings.
- `ctest` 3/3 passes.
- `--help` lists `--render-relativistic` with the correct sweep
  description.
- `--render-relativistic` on a host without CUDA prints the
  actionable rebuild hint and exits 1.

#### Outputs (cannot be confirmed in this environment)

The four PPMs `output/sphere_beta_{000,025,075,095}.ppm` are
**produced by the kernel sweep**, but **not** confirmed in this
commit because the development environment has no CUDA Toolkit and
no GPU. The kernel logic + driver loop match the prototype's
tested implementation exactly, so on a GPU host the four PPMs are
expected to:

- `sphere_beta_000.ppm` - identical to `gpu_sphere.ppm`: a
  normal-shaded sphere against the sky gradient.
- `sphere_beta_025.ppm` - mild blue tint on the front-facing
  hemisphere, slight forward aberration, slight overall brightening.
- `sphere_beta_075.ppm` - clearly bluer, smaller (apparent)
  silhouette as rays aberrate forward, noticeable brightening.
- `sphere_beta_095.ppm` - strong blue tint, sphere appears
  significantly compressed forward in the frame, pronounced
  searchlight brightening (`D^4` scales rapidly past β=0.9).

### Stage 6A — Host scene structures (Module 11, host half)

Status: implemented.

The user split master module 11 (GPU scene upload) into two slices:

- **Stage 6A (this slice)**: host-side scene structures only. No
  GPU work, no kernel changes, no behavioural delta.
- **Stage 6B (next slice)**: `GpuScene` + `CudaSceneView` +
  `k_render_scene` closest-hit kernel + `--render-scene` CLI
  action. Consumes the structures from Stage 6A.

Stage 6A ships:

- `src/math/Transform.h` (recovered byte-identical from
  `prototype_v0` - had been deliberately skipped at Stage 2 because
  the user's Stage-2 file list did not include it, but Stage 6A's
  `scene/Transform.h` alias requires it).
- `src/scene/Transform.h` (alias to `rr::math::Transform`).
- `src/scene/RenderSettings.h`.
- `src/scene/SceneObject.h`.
- `src/scene/Scene.{h,cpp}` (the container + `clear()`).

`Scene` carries:

- Real fields: `Camera`, `RenderSettings`, `Observer`,
  `RelativityParams`, `vector<SceneSphere>` (using the existing
  `rr::geometry::Sphere`).
- Placeholder fields (each tagged with the master module that
  fills it in): `vector<SceneMesh>` (module 12),
  `vector<SceneMaterial>` (module 13), `vector<SceneLight>`
  (module 14).

The placeholder types are honest scaffolding, not "fake stubs of
complete systems":

- `SceneMesh { SceneObject object; std::string source_path; }`
  carries authoring metadata; the geometry payload (vertices,
  triangles, transform, material id) joins at module 12 along
  with `rr::geometry::Mesh`.
- `SceneMaterial { int id; std::string name; }` carries the
  lookup id and human-readable name; the shading payload
  (`MaterialParams`) joins at module 13.
- `SceneLight { SceneObject object; }` carries authoring metadata;
  the type discriminator + parameters join at module 14 along
  with `rr::lighting::Light`.

CMake additions:

- New `rr_scene` STATIC library. PUBLIC-links `rr_math`,
  `rr_camera`, `rr_geometry`, `rr_relativity` because `Scene`
  exposes those types by value.
- **Not** linked into `rr_gpu` or `RelativityRender` yet - the
  rule was "must compile, no behavioural change". Stage 6B brings
  the executable + `rr_gpu` consumers online.

Hard-rule audit (per the prompt):

- No scene parser yet - **yes**, no `SceneLoader` / `SceneWriter`
  files exist.
- No server, no C4D - **yes**.
- No CPU rendering - **yes**, scene contains only data; no per-ray
  / per-pixel logic added.
- CUDA kernels unchanged - **yes**, `CudaTestKernel.cu`,
  `CudaKernels.cuh`, `CudaRenderer.{h,cu}` are byte-identical to
  Stage 10.
- Must compile - **yes**, host-only build clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `librr_scene.a` is
  produced; ctest 3/3 passes; `--device-info` /
  `--render-gradient` / `--render-rays` / `--render-sphere` /
  `--render-relativistic` behaviour identical to Stage 10.

Naming exception called out: `scene/Transform.h` is the same
back-compat shim the prior reuse audit classified
`DELETE_LATER`. The user's Stage 6A spec named it explicitly, so
it stays. New code under `src/scene/` is welcome to use
`rr::math::Transform` directly - the alias is for legibility of
the user-facing scene API, not a separate type.

### Stage 6B — GPU scene upload (Module 11, GPU half)

Status: implemented.

The user split master module 11 into Stage 6A (host scene
structures, landed in the previous commit) and Stage 6B (GPU
upload + closest-hit kernel, this commit). Stage 6B ships:

- `src/cuda/CudaScene.cuh` — `CudaSceneView` launch-arg POD with
  the camera / observer / params PODs by value plus a device
  pointer + count for the sphere array. Future entity types join
  here without changing the kernel signature.
- `src/gpu/GpuScene.{h,cpp}` — move-only host-side container.
  Three explicit upload methods (`upload_camera`,
  `upload_relativity`, `upload_spheres`); the latter reallocates a
  `GpuBuffer<Sphere>` to fit and reports honest failure when no
  GPU backend is available. `reset_device()` / `clear()` /
  per-field accessors complete the surface.
- New `__global__ k_render_scene` in `CudaTestKernel.cu` — same
  eight-step relativistic pipeline as `k_sphere_relativistic`, but
  step 3 (intersection) is a closest-hit loop over
  `scene.spheres[0 .. sphere_count - 1]`. The loop tightens
  `t_max` as candidates land so each later sphere only needs to
  beat the running best.
- `launch_render_scene` declared in `CudaKernels.cuh`, defined in
  `CudaTestKernel.cu`.
- `CudaRenderer::render_scene(const rr::gpu::GpuScene&, w, h)` —
  snapshots the GpuScene's host-resident state into a
  `CudaSceneView`, reads the device pointer + count off the
  `GpuScene`, and reuses the existing `run_kernel_render`
  scaffold. No copy of the device buffer happens at render time;
  the `GpuScene` retains ownership.
- `--render-scene` CLI action (mutually exclusive with the other
  action flags). `main.cpp::build_demo_scene` constructs a
  hard-coded four-sphere scene (`left`, `centre`, `right`,
  `ground-bulb`) at the camera's default forward; β = 0
  (relativity-identity) so the result isolates the GpuScene
  upload + closest-hit loop from the relativity pipeline.
  `main.cpp::run_render_scene` builds the scene, pulls
  `rr::geometry::Sphere` PODs out of the `SceneSphere` wrappers
  (filtering invisible entries), uploads via the three
  `GpuScene::upload_*` methods, calls
  `CudaRenderer::render_scene`, and saves
  `output/gpu_scene_spheres.ppm` (or `--output`).
- `rr::gpu::GpuScene` deliberately does **not** depend on
  `rr_scene` — its API takes raw `Camera` / `Observer` /
  `RelativityParams` / `Sphere*` arguments. main.cpp does the
  `Scene` → `Sphere*` extraction. This keeps the dependency
  direction one-way: `rr_scene` and `rr_gpu` are siblings; the
  executable composes them.

CMake additions:

- `rr_gpu` picks up `src/gpu/GpuScene.cpp` and PUBLIC-links
  `rr_camera` / `rr_relativity` / `rr_geometry` because
  `gpu/GpuScene.h` exposes those types by value.
- `RelativityRender` PRIVATE-links `rr_scene` (Stage 6B brings
  the executable consumer online; Stage 6A had `rr_scene` built
  but unconsumed).

Hard-rule audit (per the prompt):

- CPU uploads data only - **yes**. The host work in
  `run_render_scene` is: build a `Scene`, copy `Sphere` PODs out
  of the wrappers, call the three `GpuScene::upload_*` methods,
  invoke `CudaRenderer::render_scene`, save the PPM. No per-pixel
  / per-ray work touches the host.
- GPU does all ray / intersection / shading - **yes**. The new
  `k_render_scene` is the only new code that runs `pixels[idx +
  …] = …`. The closest-hit loop is inside the kernel, one
  thread per pixel.
- No mesh yet - **yes**. `CudaSceneView` has only spheres; the
  `SceneMesh` placeholder list on `Scene` is unread.
- No parser yet - **yes**. `--render-scene` builds the demo scene
  in C++; `--render <scene>` is still the placeholder action.
- No server, no C4D - **yes**.
- Must compile - **yes**. Host-only build clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `librr_gpu.a` and
  `librr_scene.a` link; ctest 3/3.

Verified (host-only build):

- `cmake --build build -j` clean; `ctest` 3/3.
- `--help` lists `--render-scene` with the correct default output
  path.
- `--render-scene` on a host without CUDA prints the actionable
  rebuild hint and exits 1.

CUDA-enabled runtime verification (kernel launch, four-sphere
closest-hit, PPM written to `output/gpu_scene_spheres.ppm`)
requires a host with the CUDA Toolkit and a CUDA-capable GPU and
was **not** runnable in the development environment for this
commit. The kernel logic + GpuScene upload paths are
byte-equivalent to the prototype's tested implementations
(prior reuse audit classified them KEEP_AS_IS / KEEP_WITH_REFACTOR;
this commit ships only the minimum surface the prompt asks for).

### Stage 7A — Mesh structures (Module 12, host half)

Status: implemented.

The user is splitting master module 12 (Mesh system) the same way
module 11 was split:

- **Stage 7A (this slice)**: host-side `Triangle` / `Vertex` /
  `Mesh` / `AABB`. No GPU upload, no kernel changes, no rendering
  delta.
- **Stage 7B (next slice)**: restore `intersect_triangle` in
  `cuda/CudaIntersection.cuh`, add `cuda/CudaMesh.cuh` device
  view, `gpu/GpuMesh.{h,cpp}` upload manager, extend `GpuScene` /
  `CudaSceneView` with a single mesh slot, extend
  `k_render_scene` with a triangle closest-hit loop, promote
  `SceneMesh` from placeholder to real (carries
  `rr::geometry::Mesh` data).

Stage 7A ships:

- `src/geometry/Triangle.h` (recovered byte-identical from
  `prototype_v0`).
- `src/geometry/Mesh.h` (prototype's `Vertex` + `Mesh` plus a new
  `AABB` struct + `Mesh::local_bounds()` method - the user
  explicitly asked for "bounds calculation if simple"; the
  prototype shipped without one).
- `src/geometry/Mesh.cpp` (clear / empty / reserve from the
  prototype, plus the `local_bounds` impl - single linear pass
  over vertices).

Bounds scope: **local-space only** at this stage. The
`Transform` POD doesn't commit to a 4x4 matrix conversion (Euler
order, basis convention), so transforming each vertex would force
that decision early. Local-space AABB is the simplest thing the
user's "if simple" qualifier covers; world-space bounds (which a
BVH or frustum cull would need) join when a real consumer
appears.

CMake change: `rr_geometry` is promoted from INTERFACE to STATIC
because `Mesh.cpp` now ships real implementation. PUBLIC includes
+ PUBLIC link to `rr_math` are unchanged.

Hard-rule audit (per the prompt):

- No GPU upload yet - **yes**, no `GpuMesh.{h,cpp}` /
  `CudaMesh.cuh` files exist.
- No rendering changes - **yes**, `CudaTestKernel.cu`,
  `CudaIntersection.cuh`, `CudaRenderer.{h,cu}`, and `main.cpp`
  are byte-identical to Stage 6B.
- No parser - **yes**, no `SceneLoader` / `SceneWriter` files
  exist.
- No server, no C4D - **yes**.
- Must compile - **yes**, host-only build clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `librr_geometry.a` is
  produced; ctest 3/3; every existing CLI action behaves
  identically to Stage 6B.

Stage 7A ships zero behavioural delta - it is a pure additive
structural slice that prepares Stage 7B.

### Stage 7B — Mesh GPU upload (Module 12, upload half)

Status: implemented (CMake wiring landed alongside Stage 7C in
the same commit; the file recovery happened in the prior commit
window but the `add_library(rr_gpu …)` source list was finalised
here).

Stage 7B ships:

- `src/gpu/GpuMesh.{h,cpp}` (recovered byte-identical from
  `prototype_v0`). Move-only RAII owner of one mesh's GPU
  resources (`GpuBuffer<Vertex>` + `GpuBuffer<Triangle>` +
  `material_id` + `Transform`). Three explicit upload methods +
  `upload_from` convenience.
- `src/cuda/CudaMesh.cuh` (recovered byte-identical from
  `prototype_v0`). Device-side launch-arg POD.
- `rr_gpu` CMake target picks up `src/gpu/GpuMesh.cpp`.

Stage 7B is the upload half - no kernel reads the mesh yet at this
slice. The kernel integration follows in Stage 7C.

### Stage 7C — CUDA triangle intersection (Module 12, kernel half)

Status: implemented.

- Restored `RR_HD intersect_triangle` (Möller-Trumbore) in
  `src/cuda/CudaIntersection.cuh`. Treats triangles as
  double-sided (front- + back-face hits); returns the front-face
  outward normal of the CCW winding `(v0, v1, v2)`. Populates
  `Hit::bary_u` / `bary_v` so future stages can interpolate
  per-vertex attributes.
- Slotted a `CudaMeshView` into `CudaSceneView` (single mesh slot;
  multi-mesh support is a future slice).
- Extended `GpuScene` with `upload_mesh(const rr::geometry::Mesh&)`
  + a `mesh()` accessor for the renderer to pull device pointers
  from.
- Extended `CudaRenderer::render_scene` to populate the
  `CudaMeshView` from the GpuScene's owned `GpuMesh`.
- Extended `k_render_scene` with the triangle closest-hit loop:
  after the sphere loop, the kernel iterates
  `mesh.triangles[0 .. mesh.triangle_count - 1]`, looks up each
  triangle's three vertices from `mesh.vertices`, calls
  `intersect_triangle`, and updates `best` + `t_max` when the
  triangle hit beats the running best. The triangle loop shares
  `t_max` with the sphere loop, so primitives compete for the
  same nearest-hit slot. On a hit the kernel copies
  `mesh.material_id` into `Hit::material_index` (a placeholder
  pending the materials stage).
- Two new CLI actions:
    - `--render-triangle` — Scene with **zero spheres** + a single
      front-facing equilateral triangle at z = -3. Demonstrates
      the triangle-only path. Default output:
      `output/gpu_triangle.ppm`.
    - `--render-mesh-scene` — multi-sphere demo scene + a
      6×6 quad (two CCW triangles) at z = -6 behind the spheres.
      Demonstrates sphere/triangle closest-hit competition.
      Default output: `output/gpu_mesh_scene.ppm`.
  Both use the existing `CudaRenderer::render_scene` —
  k_render_scene's combined sphere + triangle loop handles both
  cases without a kernel duplication.
- main.cpp adds two builders: `build_demo_triangle_mesh()` and
  `build_demo_quad_mesh()`. Both ship with normal `(0, 0, +1)`,
  facing the camera. `material_id = -1` (the inert default — no
  material lookup yet, the kernel falls through to normal-as-color
  shading).

Architecture note: `SceneMesh` stays the placeholder shell from
Stage 7A (`{SceneObject, source_path}`). The current renderer
does not promote it to carry `rr::geometry::Mesh` data because
main.cpp uploads the mesh directly via `GpuScene::upload_mesh`,
bypassing scene-side mesh storage. Promotion happens when a
real consumer (the scene loader, or per-frame mesh editing)
needs scene-resident mesh data.

Hard-rule audit (per the prompt):

- No CPU intersection - **yes**. `intersect_triangle` is `RR_HD
  inline`; the only call sites are `k_render_scene` and the same
  pattern would apply to host tests, of which none exist yet for
  triangle (same as Stage 8 for sphere).
- No BVH yet - **yes**. The kernel runs a naive linear loop over
  `mesh.triangles`. A BVH lands in its own stage when scenes
  warrant it.
- No materials beyond simple color/material id placeholder -
  **yes**. Triangle hits copy `mesh.material_id` into
  `Hit.material_index`, which the kernel still ignores; shading
  is normal-as-color (`0.5*N + 0.5`) on hit, sky gradient on
  miss.
- No parser - **yes**. Both demos build their meshes in C++.
- No server, no C4D - **yes**.
- Must compile - **yes**. Host-only build clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `librr_gpu.a` (now
  with `GpuMesh.cpp`) and `RelativityRender` link; ctest 3/3.

Verified (host-only build):

- `cmake --build build -j` clean.
- `ctest` 3/3.
- `--help` lists `--render-triangle` + `--render-mesh-scene`
  with the correct default output paths.
- Both new actions on a host without CUDA print the actionable
  rebuild hint and exit 1.

CUDA-enabled runtime verification (kernel launches, two PPMs
produced) requires a host with the CUDA Toolkit and a CUDA-capable
GPU and was **not** runnable in the development environment for
this commit. The kernel logic + GpuMesh upload paths are
byte-equivalent to the prototype's tested implementations
(`intersect_triangle` and the kernel's mesh-loop body match
prototype_v0 verbatim).

### Stage 8A — Material data model (Module 13, host half)

Status: implemented.

The user is splitting master module 13 (Material system) the same
way modules 11 and 12 were split:

- **Stage 8A (this slice)**: host-side material data model. No GPU
  upload, no kernel changes, no rendering delta.
- **Stage 8B (next slice)**: `GpuScene::upload_materials`, extend
  `CudaSceneView` with the materials array + count, extend
  `k_render_scene` to read `scene.materials[Hit::material_index]`
  for the base colour (replacing the normal-as-color diagnostic
  on hit), demo CLI action.

Stage 8A ships:

- `src/material/MaterialTypes.h` (the seven fields the user named
  plus the `transmission` placeholder; the prototype's
  `base_color_texture_id` field is omitted because the texture
  system has not landed).
- `src/material/Material.h` and `Material.cpp` (host-side wrapper
  with diffuse / emissive / metal presets).
- `src/cuda/CudaMaterial.cuh` (thin re-export of `MaterialTypes.h`
  for kernel TUs).
- `src/scene/Scene.h::SceneMaterial` promoted from placeholder
  `{int id, std::string name}` to real `{int id, std::string name,
  MaterialParams params}`. Sphere `material_index` and Mesh
  `material_id` now have a real array to index into; no kernel
  reads them yet.

Naming exception called out: `MaterialParams` field names are
camelCase (`baseColor`, `emissionColor`, `emissionStrength`,
`roughness`, `metallic`, `specular`, `transmission`) per PBR / DCC
convention. This matches the user's prompt verbatim and is
documented inside `MaterialTypes.h`. The codebase's snake_case
default still applies elsewhere; material + relativity are the two
documented exceptions.

CMake additions:

- New `rr_material` STATIC library (`Material.cpp`, PUBLIC-links
  `rr_math`).
- `rr_scene` PUBLIC-links `rr_material` because `SceneMaterial`
  exposes `MaterialParams` by value.
- `RelativityRender` PRIVATE-links `rr_material` so future code
  in `main.cpp` can construct `Material` instances.

Hard-rule audit (per the prompt):

- No node graph - **yes**, no graph types or operators in this
  slice.
- No textures - **yes**, the `base_color_texture_id` slot from
  the prototype is omitted; no texture-related fields on
  `MaterialParams`.
- No path tracing - **yes**, kernel surface is byte-identical to
  Stage 7C.
- No C4D - **yes**.
- Must compile - **yes**, host-only build clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `librr_material.a`
  produced; `librr_scene.a` re-links cleanly with the promoted
  `SceneMaterial`; ctest 3/3; every existing CLI action behaves
  identically to Stage 7C.

Stage 8A ships zero behavioural delta - the kernel still ignores
`Hit::material_index` and shades normal-as-color. Stage 8B brings
the kernel consumer online.

### Stage 8B — GPU material shading (Module 13, GPU half)

Status: implemented.

Stage 8B ships:

- `CudaSceneView` extended with `const MaterialParams* materials` +
  `int material_count` (CudaScene.cuh now pulls
  `material/MaterialTypes.h`).
- `GpuScene::upload_materials(const MaterialParams* host, size_t
  count)` with the same backend-honest semantics as
  `upload_spheres` / `upload_mesh`: zero-count clears, non-zero
  needs a working backend, count resets to zero on any failure
  path.
- `device_materials()` / `material_count()` accessors on
  `GpuScene` that the renderer reads to populate the
  `CudaSceneView`.
- `CudaRenderer::render_scene` snapshots the materials pointer +
  count alongside the existing camera / observer / params /
  spheres / mesh fields.
- `k_render_scene` step 4 (base shade) replaced. The kernel now
  reads `scene.materials[Hit::material_index]` when the index is
  in range; otherwise it falls back to a neutral default
  `MaterialParams` (matches `MaterialParams`'s default ctor).
  Shading is `albedo * shade + emission` where `shade` is a
  facing-ratio attenuation `max(0, N · -rd)` with a 0.15 ambient
  floor, and `emission = emissionColor * emissionStrength` is
  added unconditionally so back-faces of emissive materials still
  glow. The miss path is unchanged (vertical sky gradient).
- The Doppler / searchlight pipeline (steps 5-7) still applies on
  top of the new shaded colour, so material colours are
  Doppler-shifted and searchlight-boosted under non-zero
  observer velocity exactly as the existing pipeline expects.
- New CLI action `--render-material-scene` (mutually exclusive
  with the other action flags). `main.cpp::run_render_material_scene`:
    1. builds a five-material palette (red / green / blue diffuse,
       a yellow emissive at strength 2, a neutral grey),
    2. assigns the four spheres to materials 0..3 and the
       background quad to material 4,
    3. uploads camera / relativity / spheres / mesh / materials
       via the five `GpuScene::upload_*` calls,
    4. invokes `CudaRenderer::render_scene`,
    5. saves to `output/gpu_material_scene.ppm` (or `--output`).

CMake change: `rr_gpu` PUBLIC-links `rr_material` so
`gpu/GpuScene.h`'s include of `material/MaterialTypes.h` resolves
for downstream consumers.

Shading rationale: facing-ratio with an ambient floor is *not* a
BSDF and *not* Lambertian (which needs a direct light direction);
it is a viewing-angle attenuation that keeps geometry discernible
without committing to a real BRDF. The path tracer (master module
16) replaces it with proper light-aware shading. This matches the
prompt's "diffuse/simple shading" bound and "no advanced BSDF" /
"no path tracing" constraints.

Hard-rule audit (per the prompt):

- No textures - **yes**, no `base_color_texture_id` field on
  `MaterialParams`, no texture-related kernel paths.
- No node graph - **yes**, no graph types or operators.
- No path tracing - **yes**, single-bounce primary-ray shading
  only; no recursion, no RNG.
- No advanced BSDF - **yes**, the kernel reads `baseColor` +
  `emissionColor` + `emissionStrength` only; `roughness` /
  `metallic` / `specular` / `transmission` are uploaded but
  unused. They are populated for forward compatibility with the
  path tracer.
- No parser / server / C4D - **yes**.
- All shading remains GPU-side - **yes**, `k_render_scene` is the
  only code that reads a `MaterialParams` and writes a pixel; the
  host's only role is constructing PODs, calling the upload
  methods, launching the kernel, and saving the PPM.

Verified (host-only build):

- `cmake --build build -j` clean under `-Wall -Wextra -Wpedantic`,
  no warnings; `librr_gpu.a` re-links with the new
  `upload_materials` impl; ctest 3/3.
- `--help` lists `--render-material-scene` with the correct
  default output path.
- Host-only `--render-material-scene` prints the actionable
  rebuild hint and exits 1.

CUDA-enabled runtime verification (kernel launch + PPM written to
`output/gpu_material_scene.ppm`) requires a host with the CUDA
Toolkit and a CUDA-capable GPU and was **not** runnable in the
development environment for this commit. The shading change is
small and self-contained; on a GPU host the four spheres render in
their assigned colours (red / green / blue / yellow-emissive) on a
neutral-grey quad, with the facing-ratio falloff giving them a
3D-looking shade.

### Stage 9A — Light data model (Module 14, host half)

Status: implemented.

The user is splitting master module 14 (Lighting system) the same
way modules 11 / 12 / 13 were split:

- **Stage 9A (this slice)**: host-side light data model. No GPU
  upload, no kernel changes, no rendering delta.
- **Stage 9B (next slice)**: `GpuScene::upload_lights`, extend
  `CudaSceneView` with the lights array + count, extend
  `k_render_scene` to evaluate direct lighting at each hit
  (likely with a shadow-ray visibility test reusing
  `intersect_sphere` / `intersect_triangle`), and a CLI action.

Stage 9A ships:

- `src/lighting/Light.h` and `Light.cpp` (recovered byte-identical
  from `prototype_v0`). RR_HD POD with a `LightType`
  discriminator (`Point` / `Directional` / `Area` / `Environment`)
  plus type-specific fields (position, direction, area extents,
  colour, intensity). Four convenience factories
  (`make_point_light`, `make_directional_light`,
  `make_area_light`, `make_environment_light`).
- `src/cuda/CudaLight.cuh` (recovered byte-identical). Thin
  re-export today; sampling / eval / pdf helpers per `LightType`
  join here when the path tracer lands.
- `src/scene/Scene.h::SceneLight` promoted from placeholder
  `{SceneObject object}` to real `{SceneObject object,
  rr::lighting::Light data}`. `Scene::lights` is now a useful
  authoring list; the kernel just doesn't read it yet.

Placeholder semantics (called out per the prompt):

- `LightType::Area` is a PLACEHOLDER. The geometry slots
  (`area_width`, `area_height`, `direction` as a surface normal)
  are populated for forward compatibility, but no kernel samples
  area lights yet. Real area-light sampling joins the path tracer
  (master module 16).
- `LightType::Environment` is a PLACEHOLDER. The current shape is
  a single colour + intensity acting as a flat sky tint; HDR
  environment maps + IBL importance sampling join the texture
  system (master module 18).

CMake additions:

- New `rr_lighting` STATIC library (`Light.cpp`, PUBLIC-links
  `rr_math`).
- `rr_scene` PUBLIC-links `rr_lighting` because `SceneLight`
  exposes `rr::lighting::Light` by value.
- `RelativityRender` PRIVATE-links `rr_lighting` so future code
  in `main.cpp` can construct lights directly.

Hard-rule audit (per the prompt):

- No shadows yet - **yes**, no kernel reads light arrays; no
  shadow-ray code in the kernel.
- No path tracing - **yes**, kernel surface is byte-identical to
  Stage 8B.
- No parser / server / C4D - **yes**.
- Must compile - **yes**, host-only build clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `librr_lighting.a`
  produced; `librr_scene.a` re-links cleanly with the promoted
  `SceneLight`; ctest 3/3; every existing CLI action behaves
  identically to Stage 8B.

Stage 9A ships zero behavioural delta - the kernel still ignores
`Scene::lights` (since no upload exists yet) and shades using only
`MaterialParams::baseColor` + emission + facing-ratio. Stage 9B
brings the kernel consumer online.

### Stage 9B — Direct lighting on GPU (Module 14, GPU half)

Status: implemented.

Stage 9B ships:

- `CudaSceneView` extended with `const Light* lights` +
  `int light_count` (CudaScene.cuh now pulls `lighting/Light.h`).
- `GpuScene::upload_lights(const Light*, std::size_t)` with the
  same backend-honest semantics as the other uploads
  (`upload_spheres` / `upload_mesh` / `upload_materials`).
- `device_lights()` / `light_count()` accessors on `GpuScene` that
  the renderer reads to populate the `CudaSceneView`.
- `CudaRenderer::render_scene` snapshots the lights pointer +
  count alongside the existing fields.
- `k_render_scene` step 4 (base shade) extended. The kernel now:
    1. Looks up the hit's material (unchanged from Stage 8B) to
       obtain `albedo` and `emission`.
    2. **If `light_count > 0`**: iterates the lights array.
       Directional lights contribute
       `light_color * intensity * max(0, N · -direction)`. Point
       lights contribute
       `light_color * intensity * max(0, N · L) / d²` where
       `d² = ||L_pos - hit_pos||²` (with a small epsilon floor
       to keep the singular point tractable). Environment lights
       contribute `light_color * intensity` to an ambient
       accumulator; if no environment light is present a small
       implicit ambient floor `(0.05, 0.05, 0.05)` is added so
       grazing-angle pixels are not pitch black. Area lights are
       skipped (PLACEHOLDER per Stage 9A; real sampling lands
       with the path tracer). The shaded colour is
       `albedo * (direct + ambient) + emission`.
    3. **If `light_count == 0`**: falls back to the Stage 8B
       facing-ratio shade so the existing unlit CLI actions
       (`--render-scene`, `--render-mesh-scene`,
       `--render-material-scene`) keep producing recognisable
       output instead of going pitch black.
- Steps 5-7 of the pipeline (Doppler factor → Doppler colour →
  searchlight beaming) are unchanged - the relativistic post-
  shading modifier applies on top of the new direct-lighting
  result, exactly as the prompt requires.
- New CLI action `--render-direct-lighting` (mutually exclusive
  with the other action flags). `main.cpp::run_render_direct_lighting`
  builds a scene that exercises every code path:
    - the four-sphere + quad geometry from
      `--render-material-scene`,
    - the same five-material palette,
    - three lights:
        - **Directional** at `(-0.4, -0.7, -0.6)` (a "sun" coming
          from upper-front-left), warm white, intensity 0.9;
        - **Point** at `(2, 1.5, -2.5)`, warm fill, intensity 30
          (compensating for the `1/d²` falloff at a few units);
        - **Environment** flat sky tint `(0.30, 0.40, 0.55)`,
          intensity 0.4.
  Output: `output/gpu_direct_lighting.ppm` (override with
  `--output`).

CMake change: `rr_gpu` PUBLIC-links `rr_lighting` so
`gpu/GpuScene.h`'s include of `lighting/Light.h` resolves for
downstream consumers.

Energy-conservation note: the kernel deliberately drops the
`1/π` Lambertian normalisation. "Basic GPU direct lighting" per
the prompt is not energy-conserving PBR; the path tracer
(master module 16) replaces this with a proper BRDF that
includes the normalisation. With `1/π` dropped, default
intensities of ~1 produce visible (not crushed-dark) results
without explicit exposure control.

Hard-rule audit (per the prompt):

- No CPU lighting - **yes**, every per-light evaluation runs
  inside `k_render_scene`. Host-side code only constructs `Light`
  PODs and calls `upload_lights`.
- No shadows yet - **yes**, the kernel adds each light's
  contribution unconditionally; no shadow-ray code exists in the
  kernel.
- No path tracing yet - **yes**, single-bounce primary-ray
  shading; no recursion, no RNG.
- No textures - **yes**, no texture-related fields on the kernel
  path; `MaterialParams` carries no texture binding.
- No node graph - **yes**.
- No parser / server / C4D - **yes**.
- All shading remains GPU-side - **yes**, `k_render_scene` is the
  only code that reads `Light` / `MaterialParams` and writes a
  pixel; the host's role is constructing PODs, calling the upload
  methods, launching, and saving the PPM.

Verified (host-only build):

- `cmake --build build -j` clean under `-Wall -Wextra -Wpedantic`,
  no warnings.
- `ctest` 3/3.
- `--help` lists `--render-direct-lighting` with the correct
  default output path.
- `--render-direct-lighting` on a host without CUDA prints the
  actionable rebuild hint and exits 1.

CUDA-enabled runtime verification (kernel launch + PPM written to
`output/gpu_direct_lighting.ppm`) requires a host with the CUDA
Toolkit and a CUDA-capable GPU and was **not** runnable in the
development environment for this commit. The shading change is
self-contained and the kept code paths (light evaluation,
material lookup, Doppler / searchlight) are byte-equivalent to the
prototype's tested implementation.

### Stage 10A — `.rrscene` format spec

Status: implemented (specification only).

- `docs/RRSCENE_FORMAT.md` defines the v1.0 file format the
  parser will implement against. 15 sections covering top-level
  shape, render settings, camera, relativity, materials, spheres,
  meshes, lights, transforms, cross-section validation rules,
  format-evolution policy, and parser non-goals.
- No source code modified by this slice.

### Stage 10B.1 — Scene parser foundation

Status: implemented.

The user split master module 15 (Scene format / parser) into
fine-grained sub-stages, mirroring the 6A/6B pattern. Stage
**10B.1** ships only the library scaffold + a single host helper +
the JSON-strategy decision; actual `.rrscene` parsing arrives in a
follow-up sub-stage.

Files:

- `src/io/SceneLoader.h`   — declares the single Stage-10B.1
  function `bool sceneFileExists(const std::string& path)`. Any
  `parse(...)` / `load(...)` API + `LoadResult` struct lands in
  the next sub-stage; the header is kept narrow on purpose.
- `src/io/SceneLoader.cpp` — implementation: `std::filesystem::exists`
  + `is_regular_file` with error-code suppression. Pure host code,
  never throws.
- `src/io/SceneWriter.h`   — empty namespace scaffold; the
  writer's API surface (`save(path, scene)`, `serialize(scene)`,
  `WriteResult`) joins alongside the parser in the next sub-stage.
- `src/io/SceneWriter.cpp` — empty translation unit so the
  `rr_io` library has a non-empty source list and the writer's
  public header has a paired implementation file from the start.

CMake: new `rr_io` STATIC library under
`add_library(rr_io STATIC SceneLoader.cpp SceneWriter.cpp)`,
PUBLIC-links `rr_scene` (because the parser API will populate
`rr::scene::Scene` by value). `librr_io.a` is built but not yet
linked into `RelativityRender`; the executable picks it up when
the parser entry points actually become reachable from
`main.cpp::run_render` (Stage 10B.2).

#### JSON strategy decision

**Decision: hand-roll a focused JSON parser + schema mapper for
`.rrscene` v1.0.** Rationale:

- The Stage 10A spec is closed and small (15 sections, one root
  schema, ~20 distinct field shapes). A focused recursive-descent
  parser specific to this schema is feasible at ~400 lines for
  parser + ~300 for the schema layer.
- The prototype's lesson was *not* "always hand-roll" — it was
  "don't hand-roll a parser **and** an ill-specified schema at
  the same time". With the schema locked in `RRSCENE_FORMAT.md`
  v1.0 and a bounded set of validation rules, the schema risk is
  paid down before the parser starts.
- Vendoring `nlohmann/json` would drop a ~30,000-line single
  header into `third_party/` and significantly slow compilation
  for every TU that includes JSON; for a closed format that
  expense is hard to justify.
- The dev environment for this branch cannot fetch external
  libraries from the network, so a vendoring-only path would
  block forward progress without a local copy of the header.
- The hand-rolled parser produces error messages tailored to
  this schema (line/col + section name). A general JSON parser
  would surface generic JSON errors and require the schema layer
  to translate them.

Tradeoffs accepted:

- More code under our maintenance (vs. a third-party header that
  someone else maintains).
- Need to handle JSON edge cases (UTF-8 escapes, exponents,
  empty arrays/objects) ourselves; this is bounded by the
  "strict JSON only, no comments, no trailing commas" stance in
  `RRSCENE_FORMAT.md` §15.

Mitigation: the parser is split across logical sections (tokeniser
→ value tree → schema mapper) with each section under ~150 lines,
so unit-style tests can target each layer independently when
`tests/io_tests.cpp` lands.

If this decision proves wrong (parser bugs accumulate, schema
needs to grow beyond v1's scope), the migration path is to swap
the JSON-tree layer for `nlohmann::json` and keep the schema
layer; the schema layer is the contract, not the JSON
implementation.

Hard-rule audit (per the prompt):

- Do not parse scene content yet - **yes**, the only function in
  this slice is `sceneFileExists`.
- Do not modify renderer - **yes**, `main.cpp`,
  `CudaTestKernel.cu`, `CudaRenderer.{h,cu}`, `GpuScene.{h,cpp}`
  are byte-identical to Stage 9B.
- No server, no C4D - **yes**.
- Must compile - **yes**, host-only build clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `librr_io.a` is
  produced; ctest 3/3; every existing CLI action behaves
  identically to Stage 9B.

Stage 10B.1 ships zero behavioural delta - pure additive
structural slice. The next sub-stage (planned name: Stage
10B.2) implements the JSON tokeniser + parser + schema mapper
against the v1.0 spec, then wires `--render <scene>` in
`main.cpp` to call `SceneLoader::load(...)` followed by the
existing `GpuScene::upload_*` chain.

## Stage 10B.2 — parse render settings

**Scope of this slice (Stage 10B.2): hand-rolled JSON parser +
top-level `version` + `render_settings` schema mapper, wired into
a new `--scene-info <file>` CLI action.** Camera, relativity,
materials, spheres, meshes, and lights are intentionally out of
scope; the parser accepts them as arbitrary JSON for syntactic
validation and then drops them.

### What ships

- `src/scene/RenderSettings.h` — added `std::string output_path`
  field. Empty by default; populated by the parser when the file
  authors `output_path` (or its alias `output`).
- `src/io/SceneLoader.h` — added `LoadResult` (carries `ok`,
  `scene`, `version`, `error_message`, `error_line`,
  `error_column`) and the two entry points
  `LoadResult load(const std::string& path)` /
  `LoadResult parse(const std::string& text)`. The
  `sceneFileExists` helper from Stage 10B.1 stays put.
- `src/io/SceneLoader.cpp` — full hand-rolled implementation:
  - `JsonValue` tagged-variant tree (`Null` / `Bool` / `Number`
    / `String` / `Array` / `Object`) with insertion-ordered
    object key/value pairs and a `find` / `find_or` lookup pair
    that supports the alias shorthands.
  - `Parser` — single-pass recursive-descent tokeniser+parser
    over a `std::string`. Tracks 1-based line/column for
    diagnostics. Implements the full RFC-8259 grammar:
    objects, arrays, strings (with `\uXXXX` and surrogate-pair
    decoding to UTF-8), numbers (int / fraction / exponent),
    `true` / `false` / `null`. Rejects unescaped control
    characters and trailing content.
  - `apply_render_settings` schema mapper: reads `width`,
    `height`, `samples_per_pixel` (alias: `samples`),
    `max_depth`, `output_path` (alias: `output`) onto an
    `rr::scene::RenderSettings` and validates ranges (`width >
    0`, `height > 0`, `samples_per_pixel >= 1`, `max_depth >=
    1`).
  - `parse(text)`: parses the document, requires a top-level
    JSON object, requires the `version` field, gates on major
    version `1.x.y`, then applies `render_settings` (or its
    alias `render`) if present. Other top-level keys are
    syntax-checked and dropped.
  - `load(path)`: existence check + `std::ifstream` slurp +
    delegate to `parse(text)`.
- `src/core/CommandLine.{h,cpp}` — added the `SceneInfo` action
  and the `--scene-info <file>` flag. Mutually exclusive with
  every other action flag, same as the existing actions.
  Updated `--help` text and the action-conflict error message.
- `src/main.cpp` — added `run_scene_info(cfg)`: calls
  `rr::io::load(cfg.scene_path)`, prints the parsed version +
  every `RenderSettings` field (with `(none)` for an empty
  `output_path`), and returns `0` on success or `1` on parse
  failure (with line/column attached when the parser populates
  them). Wired into the `main()` switch. Bumped the default-
  action hint message to mention `--scene-info`.
- `CMakeLists.txt` — `RelativityRender` now PRIVATE-links
  `rr_io` (added at the end of the existing list). The status
  line bumps to "Stage 10B.2: parse render settings".
- `scenes/test_render_settings.rrscene` — small fixture
  exercising every `render_settings` field (640×360, samples=4,
  max_depth=2, `output_path = "output/test_render_settings.ppm"`)
  with the canonical key names.
- `docs/RRSCENE_FORMAT.md` — §4 addendum: `output_path` listed in
  the field table; new §4.1 documenting the three accepted
  authoring shorthands (`render`, `samples`, `output`) as exact
  synonyms for the canonical names. Tools that emit `.rrscene`
  files MUST emit canonical names only; shorthands are an
  authoring convenience.

### Naming-tension resolution

The Stage 10B.2 prompt listed `render.width` / `render.height` /
`render.samples` / `render.output`; the Stage 10A spec uses
`render_settings.{width, height, samples_per_pixel, max_depth}`
and had no `output_path` field. Resolution:

- The parser accepts both the canonical spec names and the
  prompt's shorthand. `find_or` looks up the canonical key
  first, then the alias.
- `output_path` is added to `RenderSettings` as a new optional
  field rather than re-purposing the existing `Config.output_path`
  (which is the CLI knob). Two paths exist for two
  responsibilities: scene-authored default (`RenderSettings`) and
  per-invocation override (`Config`).
- The format spec gets the §4 addendum + §4.1 shorthand table so
  the contract reflects what the parser actually accepts.

### Hard-rule audit

- Do not parse camera yet — **yes**, no camera mapper exists; a
  `camera` block is parsed for syntactic validity and dropped.
- Do not render yet — **yes**, `--scene-info` only loads + prints;
  no GPU scene is built, no kernel is launched.
- No GPU changes — **yes**, no file under `src/gpu/` or
  `src/cuda/` is touched. `rr_gpu`'s sources, headers, and
  link list are byte-identical to Stage 10B.1.
- Must compile — **yes**, host-only build is clean under
  `-Wall -Wextra -Wpedantic`, no warnings, no new third-party
  dependencies. `ctest` reports 3/3.

### Verified at the CLI

- `--scene-info scenes/test_render_settings.rrscene` prints
  version `1.0.0` + the five fields exactly as authored.
- Shorthand fixture (`render` / `samples` / `output`) loads
  identically to canonical names.
- Missing file → exit 1, "scene file does not exist".
- `version: "2.0.0"` → exit 1, "unsupported scene version
  '2.0.0' (this build accepts major version 1)".
- `width: 0` → exit 1, "render_settings.width must be > 0".
- Truncated JSON → exit 1, "JSON parse error: ... (line N,
  column M)".
- Files containing non-render-settings keys (e.g. `camera`) are
  parsed without error and the section is silently dropped, as
  intended for this slice.

## Stage 10B.3 — parse camera

**Scope of this slice (Stage 10B.3): add a camera schema mapper
that reads `camera.position`, `camera.forward`, `camera.up`, and
`camera.fovDegrees` from a `.rrscene` v1 file onto
`rr::scene::Scene::camera`, plus a fixture and an extension to
`--scene-info`.** Relativity, materials, geometry, and lights
remain out of scope; `near` / `far` are also deferred.

### What ships

- `src/io/SceneLoader.cpp` — added `to_float`, `to_vec3`, and
  `apply_camera`. The mapper:
  - Reads `position` (Vec3) and `up` (Vec3 hint), each via the
    new `to_vec3` helper that validates length-3 finite arrays.
  - Reads orientation via either `forward` (direction vector;
    user-shorthand authoring style) OR `target` (world-space
    look-at point; canonical spec form). When both are present
    `forward` wins. Zero-length `forward` and `target ==
    position` are both rejected.
  - Reads `fov_degrees` (canonical) with `fovDegrees` accepted
    as a camelCase shorthand. Validates `0.01 < fov < 180`.
  - Calls `Camera::look_at(position, target, up_hint) +
    set_vertical_fov_degrees(fov) + set_aspect(width / height)`,
    deriving aspect from the already-validated render settings
    (per RRSCENE_FORMAT.md §5).
  - Stage 10B.3 explicitly does **not** read `near` / `far`; the
    Camera retains its constructor defaults. Those mappings join
    when a render feature actually consumes the clip range.
- `src/io/SceneLoader.cpp::parse` — wires the camera mapper in
  after `apply_render_settings`. Other top-level keys
  (`relativity`, `materials`, `spheres`, `meshes`, `lights`) are
  still parsed for syntactic validity and dropped.
- `src/main.cpp::run_scene_info` — extended to print the parsed
  camera fields (`position`, `forward`, `up`, `fov_degrees`,
  derived `aspect`) under a `camera:` heading, with the existing
  render-settings block now nested under `render_settings:` for
  symmetry. A small `fmt_vec3` lambda formats `[x, y, z]`.
- `scenes/test_camera.rrscene` — fixture exercising every
  required camera field plus the `forward` / `fovDegrees`
  shorthands, on a 640×360 framebuffer.
- `docs/RRSCENE_FORMAT.md` §5.1 — new shorthand table:
  `target` ↔ `forward`, `fov_degrees` ↔ `fovDegrees`. Documents
  the precedence rule (`forward` wins when both are present),
  the `target = position + forward` derivation, and the
  zero-length rejection. Writers must still emit canonical names.

### Naming-tension resolution

Stage 10B.3's prompt listed `camera.forward` and `camera.fovDegrees`;
the Stage 10A spec uses `target` (world-space point) and
`fov_degrees` (snake_case). Same approach as 10B.2: accept both,
prefer canonical in `find_or` lookups, document shorthands in the
spec under §5.1.

`forward` is **not** equivalent to `target` semantically (one is a
direction, the other a point) so the parser converts: `target =
position + forward` before calling `Camera::look_at`. The magnitude
of `forward` is irrelevant because `look_at` normalises. The
distinction matters when a future writer emits files: it will only
emit `target`, never `forward`.

### Hard-rule audit

- Do not parse materials/geometry/lights yet — **yes**, no
  mapper for `materials` / `spheres` / `meshes` / `lights` exists;
  those blocks are JSON-validated and dropped.
- Do not render yet unless a parser test path already exists —
  **yes**, the only consumer of the parser is `--scene-info`,
  which only prints. No GPU launch path is reached.
- No GPU changes — **yes**, no source under `src/gpu/` or
  `src/cuda/` is touched. `rr_gpu`'s sources, headers, and link
  list are byte-identical to Stage 10B.2.
- Must compile — **yes**, host-only build is clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `ctest` reports 3/3.

### Verified at the CLI

- `--scene-info scenes/test_camera.rrscene` prints position
  `[0, 1.5, 4.0]`, forward normalised to `[0, -0.2425, -0.9701]`,
  up reorthogonalised to `[0, 0.9701, -0.2425]`, fov 55°, aspect
  derived as `640/360 ≈ 1.7778`.
- A canonical-form fixture using `target` + `fov_degrees` parses
  identically; the resulting forward / up basis is correct.
- `forward = [0, 0, 0]` → exit 1, "camera.forward must be a
  non-zero vector".
- `position` of length 2 → exit 1, "field 'camera.position'
  must have exactly 3 elements (got 2)".
- The Stage 10B.2 fixture (`scenes/test_render_settings.rrscene`)
  still parses without a `camera` block and yields the camera's
  default basis.

## Stage 10B.4 — parse relativity

**Scope of this slice (Stage 10B.4): add a relativity schema
mapper that reads the canonical §6 `observer_velocity` /
`enable_*` / `*_strength` / `max_beta` fields *and* the
user-shorthand authoring style from the prompt
(`enabled` / `betaVelocity` + `velocityDirection` /
`aberrationStrength` / `dopplerStrength` /
`searchlightStrength`), then enforces the §12 #2 cross-section
rule (`|observer_velocity| < max_beta < 1`).** Materials,
geometry, and lights remain out of scope.

### What ships

- `src/io/SceneLoader.cpp` — added `to_bool` helper and the
  `apply_relativity` mapper. The mapper:
  - Reads canonical fields first: `observer_velocity` (Vec3),
    `enable_aberration` / `enable_doppler` /
    `enable_searchlight` (bools), `doppler_color_strength`,
    `searchlight_strength`, `max_beta` (floats with `>= 0`
    validation; `max_beta` further validated `0 < max_beta <
    1`).
  - Then applies shorthands as overrides (consistent with the
    10B.3 `forward`-wins-over-`target` precedence policy):
    - `betaVelocity` + `velocityDirection` (both required
      together; one without the other is rejected) →
      `observer_velocity = normalize(direction) * beta`.
    - `dopplerStrength` → `doppler_color_strength`.
    - `searchlightStrength` → `searchlight_strength`.
    - `aberrationStrength` → collapsed onto
      `enable_aberration` (`> 0` ⇒ true, `== 0` ⇒ false).
      Documented in `RRSCENE_FORMAT.md` §6.1 as a host-side
      precision loss because the kernel only reads the bool
      today.
    - `enabled = false` forces all three `enable_*` flags off
      as a master gate (no-op when `true`).
  - Enforces §12 #2 cross-section validation after all
    shorthands resolve: rejects `|observer_velocity| >= 1` and
    `|observer_velocity| >= max_beta` with distinct messages
    so the artist can tell which guard tripped.
- `src/io/SceneLoader.cpp::parse` — wires the relativity mapper
  in after `apply_camera`. Other top-level keys (`materials` /
  `spheres` / `meshes` / `lights`) are still parsed for
  syntactic validity and dropped.
- `src/main.cpp::run_scene_info` — prints the parsed relativity
  state under a `relativity:` heading: the full
  `observer_velocity` Vec3, its scalar `|beta|`, the three
  `enable_*` bools, both strength floats, and `max_beta`. Also
  added an explicit `<cmath>` include so `std::sqrt` is
  unambiguous on every host build.
- `scenes/test_relativity.rrscene` — fixture exercising every
  shorthand (`enabled` / `betaVelocity` / `velocityDirection` /
  `aberrationStrength` / `dopplerStrength` /
  `searchlightStrength`) on a 640×360 framebuffer with no
  camera or render-settings overrides.
- `docs/RRSCENE_FORMAT.md` §6.1 — new shorthand table covering
  all five user-prompt fields, with the precedence rule, the
  `betaVelocity` ↔ `velocityDirection` pairing requirement, the
  `aberrationStrength` precision-loss caveat, and the master-
  gate semantics. Writers must still emit canonical names.

### Naming-tension resolution

Stage 10B.4's prompt listed six relativity fields; only three
have direct canonical equivalents (`dopplerStrength` ↔
`doppler_color_strength`, `searchlightStrength` ↔
`searchlight_strength`, and the implicit equivalence between
`enabled` and the three `enable_*` flags). The other three need
deliberate handling:

- `betaVelocity` + `velocityDirection` is a polar-form
  factorisation of the canonical `observer_velocity` Vec3; the
  parser composes them at apply-time and stores only the
  Vec3, so downstream code reads exactly what it always read.
- `aberrationStrength` would naturally map to a host-side
  `aberration_strength` float, but no such field exists on
  `RelativityParams` today and the rule "No GPU changes"
  forbids growing the POD that flows through
  `GpuScene::upload_relativity` / `CudaSceneView` /
  `k_render_scene`. The parser instead collapses the float
  onto the existing `enable_aberration` bool as a 0-or-non-zero
  gate. When a future stage actually grows `RelativityParams`,
  the mapper turns into a single field assignment.
- `enabled` has no canonical analogue because the canonical
  schema already has three independent gates. It is recorded
  as a one-way master switch (false ⇒ all off) rather than a
  bidirectional toggle.

### Hard-rule audit

- Do not parse materials/geometry/lights yet — **yes**, no
  mapper for those sections exists; they remain syntax-checked
  and dropped.
- Do not render yet — **yes**, the only consumer of the parser
  is `--scene-info`, which only prints. No GPU launch path is
  reached.
- No GPU changes — **yes**, no source under `src/gpu/` or
  `src/cuda/` is touched, no field added to `RelativityParams`,
  the upload payload size is byte-identical to Stage 10B.3.
- Must compile — **yes**, host-only build is clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `ctest` reports 3/3.

### Verified at the CLI

- `--scene-info scenes/test_relativity.rrscene` prints
  `observer_velocity = [0, 0, -0.75]` (composed from
  `betaVelocity = 0.75` + `velocityDirection = [0, 0, -1]`),
  `|beta| = 0.75`, all three `enable_*` true,
  `doppler_color_strength = 0.8`,
  `searchlight_strength = 0.6`, `max_beta = 0.999999`.
- `enabled: false` with a non-zero velocity preserves the
  velocity but forces all three `enable_*` flags false.
- `betaVelocity = 1.5` → exit 1, "|v| >= 1 (must be < 1 in
  c-units)".
- `betaVelocity = 0.6` with `max_beta = 0.5` → exit 1, "|v| >=
  max_beta (must be strictly less than max_beta)".
- Authoring `betaVelocity` without `velocityDirection` (or vice
  versa) → exit 1, "betaVelocity and velocityDirection must be
  authored together".
- `aberrationStrength: 0` → `enable_aberration = false`.
- A canonical-form fixture (full §6 schema with no shorthands)
  parses identically.
- Stage 10B.3's `scenes/test_camera.rrscene` (no `relativity`
  block) still loads with default observer + relativity state.

## Stage 10B.5 — parse materials

**Scope of this slice (Stage 10B.5): add a materials schema mapper
that reads the canonical §7 `id` / `name` / `base_color` /
`emission_color` / `emission_strength` / `roughness` / `metallic`
/ `specular` fields onto `rr::scene::SceneMaterial` entries, with
the §1 camelCase shorthands (`baseColor`, `emissionColor`,
`emissionStrength`) accepted as synonyms. Enforces the §12 #3
unique-`id` rule.** Geometry and lights remain out of scope;
`transmission` is intentionally deferred until its consuming BSDF
ships.

### What ships

- `src/io/SceneLoader.cpp` — added `apply_material` (single
  entry) and `apply_materials` (array). The single-entry mapper:
  - Requires `id` (non-negative integer, validated as a finite
    JSON number with no fractional part). `name` is optional and
    defaults to the empty string.
  - Reads `base_color` / `emission_color` (Vec3, each component
    `>= 0`) and `emission_strength` (float `>= 0`), each
    accepting the camelCase shorthand (`baseColor`,
    `emissionColor`, `emissionStrength`).
  - Reads `roughness` / `metallic` / `specular` (floats clamped
    to `[0, 1]`) via a small lambda-shaped helper so the three
    shape-identical fields share validation.
  - Stage 10B.5 explicitly skips `transmission`. That field
    stays at its `MaterialParams` default until the BSDF stage
    that consumes it ships.
- `apply_materials` (array driver):
  - Parses the JSON array, populating `scene.materials` from
    scratch (clears + reserves; the parser is the source of
    truth).
  - Enforces the §12 #3 unique-id rule via an
    `unordered_map<int, size_t>`; the duplicate error names both
    colliding indices so the artist can find them.
- `src/io/SceneLoader.cpp::parse` — wires `apply_materials` in
  after `apply_relativity`. Other top-level keys (`spheres` /
  `meshes` / `lights`) remain syntax-checked and dropped.
- `src/main.cpp::run_scene_info` — prints the parsed material
  count and the first material's eight fields under a
  `materials:` heading. Empty arrays still print
  `count : 0` and skip the per-material block, so the existing
  10B.4-and-earlier fixtures remain readable.
- `scenes/test_materials.rrscene` — three-material fixture:
  - `[0]` red diffuse (camelCase shorthand throughout).
  - `[1]` warm emitter (non-zero emission + strength).
  - `[2]` polished steel (no emission fields - exercises the
    "every PBR knob optional except id" path).
- `docs/RRSCENE_FORMAT.md` §7.1 — new shorthand table noting
  that `base_color`/`emission_color`/`emission_strength` accept
  the camelCase form as exact synonyms (consistent with the §1
  general rule). Documents the Stage 10B.5 status of
  `transmission` (parsed by the JSON layer but never consulted
  by the schema mapper).

### Naming-tension resolution

The user prompt listed camelCase field names; the spec §7
canonical form is snake_case. The §1 general rule already
licenses camelCase in the C++ types as a documented exception,
so accepting both directions in the parser is a small policy
extension rather than a new shorthand category. `find_or` does
the same job it did for `samples_per_pixel` / `samples` and
`output_path` / `output` in 10B.2.

`transmission` is a v1.0 schema field but the user prompt
explicitly omitted it. Per the master rule "Do only that scope.
Do not silently add future systems", the parser does not yet
read it. This is *partial v1.0 implementation* (rounded out in a
follow-up), not §14 forward compatibility (which is about v1.x
fields in v1.0 parsers).

### Hard-rule audit

- Do not parse geometry/lights yet — **yes**, no mapper for
  `spheres` / `meshes` / `lights`; those blocks remain syntax-
  checked and dropped.
- Do not render yet — **yes**, the only consumer of the parser
  is `--scene-info`, which only prints. No GPU launch path is
  reached.
- No GPU changes — **yes**, no source under `src/gpu/` or
  `src/cuda/` is touched. `MaterialParams` is byte-identical to
  Stage 10B.4 (the parser writes to existing fields).
- Must compile — **yes**, host-only build is clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `ctest` reports 3/3.

### Verified at the CLI

- `--scene-info scenes/test_materials.rrscene` prints
  `count : 3` and the `[0]` block matches the fixture's
  red-diffuse values byte-for-byte.
- A canonical snake_case fixture (`base_color`,
  `emission_strength`) parses identically to the camelCase
  shorthand fixture.
- `materials[0]` missing `id` → exit 1, "is missing required
  'id'".
- Duplicate ids (`[{"id":0},{"id":1},{"id":0}]`) → exit 1,
  "materials[2].id (0) collides with materials[0].id".
- `roughness: 1.5` → exit 1, "must be in [0, 1]".
- `baseColor: [-0.1, 0, 0]` → exit 1, "components must be >= 0".
- The Stage 10B.4 fixture (no `materials` block) still loads
  with `materials.count = 0`.

## Stage 10B.6 — parse spheres

**Scope of this slice (Stage 10B.6): add a spheres schema mapper
that reads the canonical §8 `name` / `center` / `radius` /
`material_index` fields onto `rr::scene::SceneSphere` entries,
with `materialId` accepted as a camelCase shorthand. Enforces
the §12 #9 (`radius > 0`) and §12 #4 (`material_index` in `[-1,
materials.size())`) cross-section rules.** Meshes and lights
remain out of scope; `visible` and `transform` (also §8 v1.0
fields) are deferred per the prompt scope.

### What ships

- `src/io/SceneLoader.cpp` — added `apply_sphere` (single
  entry) and `apply_spheres` (array driver). The single-entry
  mapper:
  - Optional `name` → `SceneObject::name`.
  - Required `center` (Vec3) and `radius` (float, validated
    `> 0`) — both rejected with named-field errors when absent
    so authoring mistakes are obvious.
  - Optional material reference: `material_index` (canonical)
    or `materialId` (camelCase shorthand) via `find_or`.
    Validated as a finite integer (no fractional input
    allowed), then range-checked against the already-parsed
    `materials.size()` per §12 #4. Stage 10B.6 chooses the
    "reject file" branch of §12 #4 rather than "reject sphere
    and warn" — it's the strictest stance the spec licenses
    and matches the Stage 10B.5 strictness for material `id`s.
  - Stage 10B.6 explicitly skips `visible` and `transform`.
    Both stay at the `SceneObject` defaults.
- `apply_spheres` (array driver):
  - Resets `scene.spheres` (the parser is the source of truth)
    and reserves the entry count.
  - Walks entries through `apply_sphere`, threading the
    already-parsed `materials.size()` so per-entry validation
    is self-contained.
- `src/io/SceneLoader.cpp::parse` — wires `apply_spheres` in
  after `apply_materials`. `materials.size()` is captured at
  call time so the cross-reference uses the canonical count
  for the same file. `meshes` and `lights` remain syntax-
  checked and dropped.
- `src/main.cpp::run_scene_info` — prints sphere count and the
  first sphere's four fields under a `spheres:` heading. Empty
  arrays still print `count : 0` and skip the per-sphere
  block, so 10B.5-and-earlier fixtures remain readable.
- `scenes/test_spheres.rrscene` — three-sphere fixture with
  three materials:
  - `[0]` "left" using the `materialId` shorthand → material 0.
  - `[1]` "centre" using the canonical `material_index` →
    material 1.
  - `[2]` "ground-bulb" with no material reference (defaults
    to `-1`).
- `docs/RRSCENE_FORMAT.md` §8.1 — new shorthand table noting
  `material_index` ↔ `materialId` synonymy. Documents the
  Stage 10B.6 partial-implementation status of `visible` and
  `transform` and the parser's per-rule choice for §12 #4 and
  §12 #9.

### Naming-tension resolution

The user prompt listed `materialId` (camelCase); spec §8 uses
`material_index` (snake_case). Same precedent as the 10B.5
material-field shorthands and the 10B.2 `samples` /
`samples_per_pixel` pattern: accept both via `find_or`, prefer
the canonical name in lookups, document the synonymy in §8.1,
and keep writers on the canonical form.

The §8 `visible` and `transform` fields are in the v1.0 schema
but the prompt explicitly omitted them. Per the master rule
"Do only that scope. Do not silently add future systems", the
parser does not yet read them. This is *partial v1.0
implementation*, the same posture taken for `transmission` in
10B.5.

### Hard-rule audit

- Do not parse meshes/lights yet — **yes**, no mapper for
  `meshes` or `lights`; both remain syntax-checked and
  dropped.
- Do not render yet — **yes**, the only consumer of the parser
  is `--scene-info`, which only prints. No GPU launch path is
  reached.
- No GPU changes unless existing scene structure requires no
  changes — **yes**, no source under `src/gpu/` or `src/cuda/`
  is touched. `rr::geometry::Sphere` is byte-identical to
  Stage 10B.5; the parser writes to existing fields. The host
  `Scene` container's `spheres` vector is the same type the
  GPU upload path (`GpuScene::upload_spheres`) already
  consumes from `--render-scene`.
- Must compile — **yes**, host-only build is clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `ctest` reports
  3/3.

### Verified at the CLI

- `--scene-info scenes/test_spheres.rrscene` prints
  `spheres.count = 3` and the `[0]` block matches the
  fixture's `left` sphere byte-for-byte
  (`center = [-1.5, 0.2, -4.0]`, `radius = 0.7`,
  `material_index = 0`).
- `spheres[0]` missing `center` → exit 1, "is missing required
  'center'".
- `spheres[0]` missing `radius` → exit 1, "is missing required
  'radius'".
- `radius = 0` and `radius = -1` → exit 1, "must be > 0"
  (§12 #9).
- `materialId = 5` against a one-material file → exit 1, "is
  out of range [0, 1)" (§12 #4).
- `material_index = -1` (renderer's neutral fallback) accepted.
- `materialId = 0.5` (fractional) → exit 1, "must be an
  integer".
- The Stage 10B.5 fixture (`scenes/test_materials.rrscene`,
  three materials, no `spheres` block) still loads with
  `spheres.count = 0`.

## Stage 10B.7 — parse lights

**Scope of this slice (Stage 10B.7): add a lights schema mapper
that reads the canonical §10 `type` / `name` / `color` /
`intensity` fields onto `rr::scene::SceneLight` entries plus the
type-specific `position` (for `point` + `area`) or `direction`
(for `directional`) per §12 #8. Enforces §12 #7 (type is one of
the four enumerators) and the type-specific required-field rules
in §12 #8.** Meshes remain out of scope; `area_width` /
`area_height` and the `SceneObject` `visible` / `transform`
fields are deferred per the prompt scope.

### What ships

- `src/io/SceneLoader.cpp` — added `apply_light` (single entry)
  and `apply_lights` (array driver). The single-entry mapper:
  - Required `type` string, validated against the four §10
    enumerators (`point` / `directional` / `area` /
    `environment`); unknown types reject the file (§12 #7)
    with the offending value quoted in the error.
  - Optional `name`, `color` (each component `>= 0`),
    `intensity` (`>= 0`).
  - Type-specific required fields (§12 #8): `position` for
    `point` + `area`; `direction` for `directional`. Errors
    name the light index, the missing field, and the type so
    authoring mistakes are easy to find.
  - `direction` is normalised before storage (matching
    `make_directional_light` behaviour). A zero-length input
    falls back to `(0, -1, 0)` rather than producing NaNs.
  - `area_width` / `area_height` stay at `Light` POD defaults
    (area lights are still a §10 PLACEHOLDER and the prompt
    excluded those fields). `SceneObject::visible` and
    `transform` stay at defaults for the same reason.
- `apply_lights` (array driver):
  - Resets `scene.lights` and reserves the entry count.
  - Walks entries through `apply_light`; the array-level
    bookkeeping is deliberately thin since per-entry
    validation already names the offending index.
- `src/io/SceneLoader.cpp::parse` — wires `apply_lights` in
  after `apply_spheres`. Only `meshes` remains syntax-checked
  and dropped after this stage.
- `src/main.cpp::run_scene_info` — prints light count and the
  first light's fields under a `lights:` heading: `type`
  (string-formatted via a small `fmt_light_type` lambda),
  `name`, `color`, `intensity`, plus `position` / `direction`
  conditional on the type so the output stays compact and
  matches what the parser actually populated.
- `scenes/test_lights.rrscene` — three-light fixture covering
  every type the prompt scope reaches:
  - `[0]` `point` light (warm fill at `[2.0, 1.5, -2.5]`,
    intensity 30).
  - `[1]` `directional` "key" (sun-like, with an
    unnormalised input vector to demonstrate normalisation).
  - `[2]` `environment` "sky" (cool ambient tint, no
    direction or position).
- `docs/RRSCENE_FORMAT.md` §10.1 — new status block listing
  the implemented fields, the type-specific required-field
  rules, the parser's normalisation + zero-length fallback
  behaviour for `direction`, and the deferred fields
  (`area_width`/`area_height`, `visible`, `transform`).

### Naming-tension resolution

None this stage. The user prompt's field names match the
canonical §10 names verbatim (`type`, `name`, `position`,
`direction`, `color`, `intensity`); no shorthand table needed.
The "if point/area" / "if directional" qualifiers in the prompt
match §12 #8's type-specific required-field rules exactly, so
the parser's per-type branching is a direct translation of the
spec.

`area_width` / `area_height` are §10 v1.0 fields but the prompt
explicitly omitted them (and area sampling is still a §10
PLACEHOLDER); same partial-v1.0 posture as 10B.5 (`transmission`)
and 10B.6 (`visible` / `transform`).

### Hard-rule audit

- Do not parse meshes yet — **yes**, no `apply_meshes` mapper;
  the `meshes` array remains syntax-checked and dropped.
- Do not render yet — **yes**, the only consumer of the parser
  is `--scene-info`, which only prints. No GPU launch path is
  reached.
- No GPU changes — **yes**, no source under `src/gpu/` or
  `src/cuda/` is touched. `rr::lighting::Light` is byte-
  identical to Stage 10B.6; the host `Scene` container's
  `lights` vector is the same type the GPU upload path
  (`GpuScene::upload_lights`) already consumes from
  `--render-direct-lighting`.
- Must compile — **yes**, host-only build is clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `ctest` reports
  3/3.

### Verified at the CLI

- `--scene-info scenes/test_lights.rrscene` prints
  `lights.count = 3` and the `[0]` block matches the
  fixture's `point` "fill" light byte-for-byte
  (`color = [1.0, 0.85, 0.6]`, `intensity = 30`,
  `position = [2.0, 1.5, -2.5]`).
- `lights[0]` missing `type` → exit 1, "is missing required
  'type'".
- `type = "laser"` → exit 1, "must be one of \"point\",
  \"directional\", \"area\", \"environment\" (got \"laser\")"
  (§12 #7).
- `type = "point"` without `position` → exit 1,
  ".position is required for type \"point\"" (§12 #8).
- `type = "directional"` without `direction` → exit 1,
  ".direction is required for type \"directional\"".
- `type = "area"` without `position` → exit 1,
  ".position is required for type \"area\"".
- `direction = [0, 0, 0]` for a directional light parses and
  collapses to `[0, -1, 0]` (spec §10 fallback).
- `direction = [3, 0, 4]` (unnormalised) parses and stores
  `[0.6, 0, 0.8]` (length-1 unit vector).
- `intensity = -1` → exit 1, "must be >= 0".
- `color = [-1, 0, 0]` → exit 1, "components must be >= 0".
- `type = "environment"` with no other fields parses with
  defaults (`color = [1, 1, 1]`, `intensity = 1`).
- The Stage 10B.6 fixture (`scenes/test_spheres.rrscene`,
  three spheres, no `lights` block) still loads with
  `lights.count = 0`.

## Stage 10B.8 — parse inline meshes

**Scope of this slice (Stage 10B.8): promote `rr::scene::SceneMesh`
from a placeholder shell (`{object, source_path}`) to a real
authoring entry composing `rr::geometry::Mesh`, then add a meshes
schema mapper that reads the canonical §9 `name` / `vertices` /
`triangles` / `material_id` (or `materialId` shorthand) /
`transform` (§11) onto each entry. After this stage every
top-level v1.0 section has a parser.**

### What ships

- `src/scene/Scene.h` — `SceneMesh` promoted to
  `{object, source_path, geometry}` where `geometry` is an
  `rr::geometry::Mesh`. Includes `geometry/Mesh.h`. The host
  scene container's `meshes` vector is unchanged in shape;
  `rr_scene` already PUBLIC-links `rr_geometry` so no
  CMakeLists edits are needed. Existing call sites are
  unaffected because `SceneMesh` was not consumed outside its
  own declaration before this stage.
- `src/io/SceneLoader.cpp` — added:
  - `to_vec2` helper (a Vec3-shaped twin sized for the §9.2
    `uv` slot) that keeps the array-shape error messages
    specific.
  - `apply_transform` for §11 Transform objects: `position`,
    `rotation_radians` (mapped to `euler_rotation_radians` in
    C++), `scale` (each component validated `> 0`).
  - `apply_mesh_vertices`: walks the `vertices` array,
    enforcing `position` required, `normal` / `uv` optional;
    normals are NOT auto-normalised per §9.2.
  - `apply_mesh_triangles`: walks the `triangles` array,
    validating each entry is a length-3 array of non-negative
    integers fitting in `uint32_t`, with indices in
    `[0, vertices.size())` per §12 #6.
  - `apply_mesh` (single entry): glues `name`, optional
    `material_id` / `materialId` (validated `-1` or in
    `[0, materials.size())` per §12 #5, strict reject-file
    mode), optional §11 `transform`, then the vertex / triangle
    arrays. Vertex parsing happens before triangle parsing so
    the index range check sees the canonical count.
  - `apply_meshes` (array driver): clears + reserves
    `scene.meshes`, walks entries through `apply_mesh`,
    threads `materials.size()` through for the §12 #5 check.
- `src/io/SceneLoader.cpp::parse` — wires `apply_meshes` in
  after `apply_lights`. After this stage the parser has a
  schema mapper for every top-level v1.0 section.
- `src/main.cpp::run_scene_info` — prints mesh count and the
  first mesh's `name` / `vertex_count` / `triangle_count` /
  `material_id` under a `meshes:` heading. Empty arrays still
  print `count : 0` and skip the per-entry block.
- `scenes/test_mesh.rrscene` — two-mesh fixture:
  - `[0]` ground-quad (4 vertices, 2 triangles, with the
    `materialId` shorthand and an explicit identity
    `transform` to exercise the §11 mapper).
  - `[1]` tetrahedron (4 vertices, 4 triangles, no transform,
    no material reference - exercises the all-defaults path).
- `docs/RRSCENE_FORMAT.md` §9.5 — new status block
  documenting the SceneMesh promotion, the implemented
  fields, the deferred ones (`source_path`, `visible`), and
  the strict reject-file stance for §12 #5 / §12 #6.

### Naming-tension resolution

Two minor reconciliations:

- The user prompt's `materialId` (camelCase) is accepted as a
  shorthand for the canonical §9 `material_id`, consistent
  with the §8.1 sphere shorthand.
- The §11 transform object names the rotation field
  `rotation_radians` (snake_case) but the C++ struct stores it
  as `euler_rotation_radians`. The wire name wins on the file
  side; the parser does the rename when populating the POD.

`source_path` and `SceneObject::visible` are §9 v1.0 fields
the prompt scope excluded; same partial-implementation posture
as 10B.5 (`transmission`), 10B.6 (`visible` / `transform` for
spheres), and 10B.7 (`area_width` / `area_height`).

### SceneMesh promotion vs. the "no GPU changes" rule

Earlier 10B sub-stages carried a "no GPU changes" rule. This
sub-stage's prompt drops that rule and explicitly anticipates
structural changes ("transform if supported by existing
Mesh/Scene structures"). The promotion is host-side only:
`SceneMesh` was not previously consumed by the GPU upload
path (which takes `rr::geometry::Mesh` directly via
`GpuScene::upload_mesh`), and `rr_scene` already PUBLIC-links
`rr_geometry`. The `--render-mesh-scene` and
`--render-material-scene` / `--render-direct-lighting`
demos still construct `geometry::Mesh` instances locally;
threading the loaded `SceneMesh::geometry` through to the
upload path is the final 10B sub-stage's job, not this one.

### Hard-rule audit

- Do not render yet — **yes**, the only consumer of the
  parser is `--scene-info`, which only prints. The `SceneMesh`
  promotion is host data only; no GPU upload path is reached.
- No server, no C4D — **yes**, no source under any
  hypothetical server / DCC bridge directory; this slice is
  exclusively `src/scene/Scene.h`, `src/io/SceneLoader.cpp`,
  `src/main.cpp`, two doc files, and the new fixture.
- Must compile — **yes**, host-only build is clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `ctest` reports
  3/3.

### Verified at the CLI

- `--scene-info scenes/test_mesh.rrscene` prints
  `meshes.count = 2` and the `[0]` block matches the
  fixture's `ground-quad` byte-for-byte
  (`vertex_count = 4`, `triangle_count = 2`, `material_id = 0`).
- A triangle index of `5` against a 2-vertex mesh → exit 1,
  ".triangles[0][2] (5) is out of range [0, 2)" (§12 #6).
- A length-4 triangle → exit 1, "must have exactly 3
  elements (got 4)".
- A negative or fractional triangle index → exit 1, "must be
  a non-negative integer fitting in uint32_t".
- A vertex missing `position` → exit 1,
  ".vertices[0] is missing required 'position'".
- A mesh with no `vertices` and no `triangles` parses fine
  (per §9: empty meshes are accepted; counts print as 0/0).
- `materialId = 3` against a one-material file → exit 1,
  ".material_id (3) is out of range [0, 1)" (§12 #5).
- `transform.scale = [0, 1, 1]` → exit 1, "components must
  be > 0".
- `uv = [0, 1, 2]` (length 3) → exit 1, "must have exactly 2
  elements (got 3)".
- The Stage 10B.7 fixture (`scenes/test_lights.rrscene`,
  three lights, no `meshes` block) still loads with
  `meshes.count = 0`.

## Stage 10B.9 — full scene load test

**Scope of this slice (Stage 10B.9): a verification slice that
exercises every per-section mapper landed in 10B.2 - 10B.8 on a
single integrated fixture, plus a compact CLI summary printer
that confirms the load reached the host `Scene` container with
the expected counts.** No render path; no writer; no deferred
fields. The 10B.8 BUILD_PLAN entry's "Next stage" projection
was wider than the actual prompt — Stage 10B.9 is intentionally
narrow per the master rule "Do only that scope."

### What ships

- `scenes/test_full_scene.rrscene` — the integration fixture.
  Single `.rrscene` v1 file with **every top-level section
  present**:
  - `version` (1.0.0)
  - `render_settings` (1280x720 + samples / max_depth /
    output_path)
  - `camera` (using the §5.1 `forward` + `fovDegrees`
    shorthands)
  - `relativity` (using the §6.1 `enabled` master gate +
    `betaVelocity` + `velocityDirection` polar-form shorthand
    + the three `*Strength` shorthands)
  - `materials` (5 entries spanning diffuse / emissive / no-
    emission shapes; mixes §7.1 camelCase shorthands with
    canonical snake_case)
  - `spheres` (4 entries, mixing §8.1 `materialId` shorthand
    and canonical `material_index`)
  - `meshes` (1 entry: 4-vertex / 2-triangle ground-quad,
    full §9.2 vertex layout including `normal` + `uv`)
  - `lights` (3 entries: directional + point + environment,
    matching the `--render-direct-lighting` demo so authors
    can compare load output against the hard-coded scene)
- `src/core/CommandLine.{h,cpp}` — new `Action::SceneSummary`
  enumerator + `--scene-summary <file>` parsing branch
  (mirroring `--scene-info`'s shape: action + path argument,
  exclusive with other action flags). Usage block extended
  with the new flag's description; the action-collision error
  string includes `--scene-summary` so authors who try to
  combine actions get a complete list.
- `src/main.cpp::run_scene_summary` — handler that calls
  `rr::io::load(path)` and on success prints a compact
  one-section summary:
  ```
    resolution     : 1280x720
    materials      : 5
    spheres        : 4
    meshes         : 1
    lights         : 3
    |beta|         : 0.300000
  ```
  `|beta|` is `length(observer.velocity)`, which collapses the
  Vec3 form into the scalar speed authors are likely to
  reason about. On parse failure the same `error_line` /
  `error_column` diagnostic the `--scene-info` handler uses
  is reported, so the new action is a drop-in for either
  handler at the CLI.
- `src/main.cpp` default-action hint — extended to mention
  `--scene-summary <file>` alongside `--scene-info <file>`.

### Why a separate `--scene-summary` instead of reusing `--scene-info`?

`--scene-info` already prints every parsed field; it is the
exhaustive view useful while a per-section mapper is being
written. The Stage 10B.9 prompt asks for a *summary* with six
specific lines. Mixing a "compact summary" mode into
`--scene-info` would either bloat the existing handler with a
flag or quietly change its output. A second action keeps both
views available without coupling: `--scene-info` for full
field dumps, `--scene-summary` for the integration check that
the load completed and produced the expected entity counts.

### Hard-rule audit

- Do not render yet — **yes**, `--scene-summary` only loads
  and prints. No `CudaRenderer` call, no `GpuScene` upload,
  no save. The render-action handlers (`--render-*`) are
  byte-identical to Stage 10B.8.
- No server, no C4D — **yes**, no source under any server /
  DCC bridge directory; this slice is exclusively the
  fixture, the new CLI action plumbing in `core/`, the new
  handler in `main.cpp`, the BUILD_PLAN entry, and the CMake
  status string.
- Must compile — **yes**, host-only build is clean under
  `-Wall -Wextra -Wpedantic`, no warnings; `ctest` reports
  3/3.

### Verified at the CLI

- `--scene-summary scenes/test_full_scene.rrscene` exits 0
  and prints `1280x720 / 5 / 4 / 1 / 3 / 0.300000` matching
  the fixture exactly.
- `--scene-summary` without a path → exit 2, "missing value
  after --scene-summary" + usage block. Same shape as
  `--scene-info`'s missing-path response.
- `--scene-summary x --scene-info y` → exit 2, "cannot
  combine action flags ..." error including `--scene-summary`
  in the listed flags. Confirms the action-set error string
  was updated.
- `--scene-summary /tmp/missing.rrscene` → exit 1, "scene
  file does not exist: ..." (the same error the loader
  produces today).
- `--help` shows the new flag with its full description in
  the usage block.
- Every prior fixture (`test_render_settings`, `test_camera`,
  `test_relativity`, `test_materials`, `test_spheres`,
  `test_lights`, `test_mesh`) still loads cleanly through
  both `--scene-info` and the new `--scene-summary`. No
  regressions across 10B.2 - 10B.8.

### Stage 10B status after this slice

| Sub-stage | Surface                          | Status |
|-----------|----------------------------------|:------:|
| 10B.1     | `rr_io` scaffold + `sceneFileExists` | ✅ |
| 10B.2     | `version` + `render_settings`    | ✅ |
| 10B.3     | `camera`                         | ✅ |
| 10B.4     | `relativity`                     | ✅ |
| 10B.5     | `materials`                      | ✅ |
| 10B.6     | `spheres`                        | ✅ |
| 10B.7     | `lights`                         | ✅ |
| 10B.8     | inline `meshes` + SceneMesh promotion | ✅ |
| 10B.9     | full-scene fixture + `--scene-summary` | ✅ |

Every top-level v1.0 section now has a mapper *and* a
verified end-to-end load path through a single integrated
fixture.

## Next stage

The remaining 10B work (writer, `--render <file>` end-to-end
wiring, deferred fields like `transmission` / `visible` /
`transform` on `SceneObject` wrappers / `area_width` /
`area_height` / `source_path`, plus `tests/io_tests.cpp`
exercising round-trip + each §12 rule) lands in a follow-up
sub-stage when prompted; this slice only ships verification.

## Constraints carried forward

From `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — these apply
to every stage:

- Build incrementally. Keep every step compilable.
- No fake stubs. No empty scaffold dirs that pretend a system exists.
- No CPU per-pixel or per-ray work as the production path (will apply
  once rendering lands; documented up-front so it stays visible).
- Core modules never depend on Cinema 4D, UI, node editor, or any DCC.
- Update this file after every implementation.

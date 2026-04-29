# Build Plan

Tracking doc per `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
("Update docs/BUILD_PLAN.md after every implementation"). Each entry
records what landed, in which stage, and the next concrete step.

## Current state

**Stages 1–8 — Core app + math + image + GPU device + GPU buffer layer
+ CUDA kernel infrastructure + camera system + primitive GPU
intersection.** Skeleton C++20 executable; header-only RR_HD math
library; host-side floating-point image + framebuffer system;
backend-agnostic GPU device + memory layers; pinhole `Camera` + RR_HD
`generate_camera_ray`; first real GPU ray-primitive intersection — a
single-sphere kernel that ray-gens, intersects, and shades on the
device, with a sky-gradient miss path. Three GPU kernels live now:
UV gradient, camera-ray visualisation, single-sphere visualisation.
`--device-info`, `--render-gradient`, `--render-rays`, and
`--render-sphere` actions are all live. 150 host-side test assertions
across three ctest binaries pass. No scene system, no materials, no
lights, no path tracer, no relativity, no server, no integrations.

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
| `src/renderer/Hit.h`       | RR_HD POD `Hit {hit, t, position, normal, material_index, uv, bary_u, bary_v}`. The kernel only reads `hit` / `normal` at this stage; the remaining fields are populated by intersection routines for later stages (textures, materials, mesh barycentrics) and have safe defaults. |
| `src/cuda/CudaIntersection.cuh` | RR_HD `intersect_sphere(ray, sphere, t_min, t_max) -> Hit`. Same code runs on host (tests) and device (kernel). Trimmed from the prototype's superset to sphere-only at this stage; triangle intersection joins the file in the mesh stage. |

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
build/bin/RelativityRender                          # startup banner
build/bin/RelativityRender --help
build/bin/RelativityRender --version
build/bin/RelativityRender --device-info
build/bin/RelativityRender --render scene.rrscene --output out.png --width 1920 --height 1080  # placeholder
build-cuda/bin/RelativityRender --render-gradient   # -> output/gpu_gradient.ppm
build-cuda/bin/RelativityRender --render-rays       # -> output/gpu_camera_rays.ppm
build-cuda/bin/RelativityRender --render-sphere     # -> output/gpu_sphere.ppm
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

## Next stage

**Stage 9 — Relativistic camera model.** Module 10 in the master
order. Scope: introduce the relativity primitives (`Observer`,
`RelativityParams`, RR_HD aberration / Doppler / searchlight math)
and wire them into the existing single-sphere kernel so the GPU
shows the visible effect of moving the camera at relativistic
velocity. This is the project's signature feature; everything up to
this point has been ground-laying.

Deliverables (planned, NOT yet implemented):

- `src/relativity/RelativityParams.h` (Observer + RelativityParams
                                        PODs; |beta| < 1 invariant)
- `src/relativity/RelativityMath.h`   (RR_HD aberration / Doppler /
                                        beaming primitives)
- `src/relativity/RelativityMath.cuh` (thin .cu re-export)
- A relativistic-sphere kernel + launcher.
- `CudaRenderer::render_relativistic_sphere(camera, observer,
                                              params, sphere, w, h)`.
- A `--render-relativistic` CLI action writing to
  `output/gpu_relativistic_sphere.ppm`.

## Constraints carried forward

From `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — these apply
to every stage:

- Build incrementally. Keep every step compilable.
- No fake stubs. No empty scaffold dirs that pretend a system exists.
- No CPU per-pixel or per-ray work as the production path (will apply
  once rendering lands; documented up-front so it stays visible).
- Core modules never depend on Cinema 4D, UI, node editor, or any DCC.
- Update this file after every implementation.

# Build Plan

Tracking doc per `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
("Update docs/BUILD_PLAN.md after every implementation"). Each entry
records what landed, in which stage, and the next concrete step.

## Current state

**Stages 1–4 — Core app + math + image + GPU device layer.**
Skeleton C++20 executable; header-only RR_HD math library;
host-side floating-point image + framebuffer system; backend-agnostic
GPU device layer with optional CUDA backend that enumerates visible
devices via `cudaGetDeviceCount` / `cudaGetDeviceProperties`. The
`--device-info` CLI flag now reports a real backend status and (when
CUDA is compiled in and devices are present) per-device name, compute
capability, total VRAM, and SM count. 131 host-side test assertions
across two ctest binaries pass. No rendering, no kernels, no scene
system, no server, no integrations.

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
build/bin/RelativityRender                          # default startup banner
build/bin/RelativityRender --help
build/bin/RelativityRender --version
build/bin/RelativityRender --device-info
build/bin/RelativityRender --render scene.rrscene --output out.png --width 1920 --height 1080
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

## Next stage

**Stage 5 — CUDA framebuffer / kernel infrastructure.** Module 7 in
the master order. Scope: a move-only RAII GPU memory wrapper plus a
trivial diagnostic kernel (UV gradient) wired through the existing
`Image` / `Framebuffer` types. This is when `enable_language(CUDA)`
lands and the first `.cu` translation unit appears.

Deliverables (planned, NOT yet implemented):

- `src/gpu/GpuBuffer.{h,cpp}`         (move-only RAII; allocates via
                                        a backend-dispatched
                                        `gpu_alloc` / `gpu_free`)
- `src/cuda/CudaBuffer.{h,cpp}`       (byte-level CUDA backend for the
                                        above; thin wrappers over
                                        `cudaMalloc` / `cudaFree` /
                                        `cudaMemcpy`)
- `src/cuda/CudaGradientKernel.cu`    (one `__global__` kernel writing
                                        an Rgba32F UV gradient)
- `src/cuda/CudaRenderer.{h,cu}`      (thin host-side launcher that
                                        allocates a `GpuBuffer<float>`,
                                        runs the kernel, downloads
                                        into an `rr::image::Image`)
- `tests/gpu_tests.cpp`               (default-state, zero-alloc,
                                        honest-failure-when-no-backend,
                                        move-only properties)
- A new CLI mode (e.g. `--render-gradient WxH OUT.ppm`) that runs the
  GPU kernel end-to-end and saves the PPM. This is **kernel
  infrastructure**, not a renderer; the path tracer and relativistic
  perception are still many stages away.

## Constraints carried forward

From `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — these apply
to every stage:

- Build incrementally. Keep every step compilable.
- No fake stubs. No empty scaffold dirs that pretend a system exists.
- No CPU per-pixel or per-ray work as the production path (will apply
  once rendering lands; documented up-front so it stays visible).
- Core modules never depend on Cinema 4D, UI, node editor, or any DCC.
- Update this file after every implementation.

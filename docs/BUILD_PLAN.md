# RelativityRender — BUILD PLAN

This file is the live, project-wide log of what has landed and what is next.
Update it after every implementation step, per
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` rule 8.

---

## Current State

- **Active milestone:** M2 — Core Engine (in progress).
- **Active branch:** `claude/create-docs-architecture-T2Dp5`.
- **Code in repo:** repository skeleton, top-level CMake project, the
  minimal C++20 application foundation (`src/main.cpp`,
  `src/core/Version.h`, `src/core/Logger.{h,cpp}`), and now basic
  configuration + command-line handling (`src/core/Config.{h,cpp}`,
  `src/core/CommandLine.{h,cpp}`). The `RelativityRender` executable
  parses `--help`, `--version`, `--device-info`, `--render`, `--output`,
  `--width`, `--height`, and reports honest "not implemented yet"
  responses for the GPU- and rendering-dependent actions. No GPU, no
  rendering, no scene system.

## Module Status (mirrors `docs/MODULE_MAP.md`)

| #  | Module                              | Status        |
|----|-------------------------------------|---------------|
| 1  | Core Engine                         | in progress   |
| 2  | Math Library                        | not started   |
| 3  | Image / Framebuffer System          | not started   |
| 4  | GPU Device Layer                    | not started   |
| 5  | CUDA Backend                        | not started   |
| 6  | OptiX Backend                       | not started   |
| 7  | Scene Graph                         | not started   |
| 8  | Geometry System                     | not started   |
| 9  | Material / Shading System           | not started   |
| 10 | Texture System                      | not started   |
| 11 | Lighting System                     | not started   |
| 12 | Camera System                       | not started   |
| 13 | Relativistic Camera Model           | not started   |
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
| M3        | Math Library                            | not started |
| M4        | Image / Framebuffer System              | not started |
| M5        | CUDA Device Layer                       | not started |
| M6        | CUDA Framebuffer & First Kernel         | not started |
| M7        | Camera System & GPU Camera Rays         | not started |
| M8        | GPU Primitive Intersection              | not started |
| M9        | Relativistic Camera Model (First Pass)  | not started |
| M10       | GPU Scene Upload & Triangle Mesh        | not started |
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

**Finish M2 — Core Engine.**

Logger, Config, and CommandLine are in place. To complete M2 and move on
to M3 (Math Library), the next implementation prompts should add, in
roughly this order:

1. `core::Error` — a small result/error type used at module boundaries.
2. `core::FileSystem` — minimal path / read / write helpers using
   `std::filesystem` plus a thin error-aware wrapper.
3. `Config::load` / `Config::save` — TOML or JSON persistence via a
   vendored parser under `third_party/`. Keep the on-disk schema small;
   it grows as later milestones add their own settings.
4. `core::App` — application lifecycle wrapper that owns parse → run →
   exit.
5. A test framework (Catch2 or doctest) under `third_party/`, plus tests
   for `Logger`, `CommandLine` (every flag + every error path), `Config`
   round-trip, and `FileSystem` read / write.
6. Host-only CI configuration that runs the build and tests.

Only after these land is M2 considered finished. Per development rules,
M2 must not introduce code from M3+ modules — no math, image, or GPU
code. Core Engine is host-only and renderer-free.

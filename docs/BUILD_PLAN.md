# RelativityRender — BUILD PLAN

This file is the live, project-wide log of what has landed and what is next.
Update it after every implementation step, per
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` rule 8.

---

## Current State

- **Active milestone:** M1 — Repository Skeleton & Build System (landed).
- **Next milestone:** M2 — Core Engine (Logging, Config, Lifecycle).
- **Active branch:** `claude/create-docs-architecture-T2Dp5`.
- **Code in repo:** repository skeleton + top-level CMake project. No
  renderer code yet.

## Module Status (mirrors `docs/MODULE_MAP.md`)

| #  | Module                              | Status        |
|----|-------------------------------------|---------------|
| 1  | Core Engine                         | not started   |
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
| M2        | Core Engine: Logging, Config, Lifecycle | next        |
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

**M2 — Core Engine: Logging, Config, Lifecycle.**

Concretely, the next implementation prompt should:

1. Add `src/core/CMakeLists.txt` and uncomment the matching
   `add_subdirectory(src/core)` call in the top-level `CMakeLists.txt`.
2. Implement `core::Logger` (severity levels + sinks), `core::Config`
   (load / save), `core::Error`, minimal `core::FileSystem`, and a
   `core::App` lifecycle wrapper.
3. Add a small CLI entry point under `src/core/` (or a new `src/cli/`)
   that uses the engine to print a structured banner. This is the first
   compiled binary in the project.
4. Add a test framework dependency under `third_party/` and the first
   tests for logging and config round-trips.
5. Wire up host-only CI (build + run tests).
6. Update this file:
   - Flip M2 to `in progress`, then `landed` once shipped.
   - Flip Module 1 (Core Engine) to `landed`.
   - Add a Change Log entry describing what landed and any deferred work.

Per development rules, M2 must not introduce code from M3+ modules. No
math, image, or GPU code yet — Core Engine is host-only and renderer-free.

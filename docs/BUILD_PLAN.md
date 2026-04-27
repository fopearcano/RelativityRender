# RelativityRender — BUILD PLAN

This file is the live, project-wide log of what has landed and what is next.
Update it after every implementation step, per
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` rule 8.

---

## Current State

- **Active milestone:** M0 — Architecture & Documentation
- **Active branch:** `claude/create-docs-architecture-T2Dp5`
- **Code in repo:** none. Documentation only.

## Module Status (mirrors `docs/MODULE_MAP.md`)

| #  | Module                              | Status      |
|----|-------------------------------------|-------------|
| 1  | Core Engine                         | not started |
| 2  | Math Library                        | not started |
| 3  | Image / Framebuffer System          | not started |
| 4  | GPU Device Layer                    | not started |
| 5  | CUDA Backend                        | not started |
| 6  | OptiX Backend                       | not started |
| 7  | Scene Graph                         | not started |
| 8  | Geometry System                     | not started |
| 9  | Material / Shading System           | not started |
| 10 | Texture System                      | not started |
| 11 | Lighting System                     | not started |
| 12 | Camera System                       | not started |
| 13 | Relativistic Camera Model           | not started |
| 14 | Path Tracer                         | not started |
| 15 | Progressive Renderer                | not started |
| 16 | Denoiser Integration                | not started |
| 17 | Render Passes / AOVs                | not started |
| 18 | Scene File Format                   | not started |
| 19 | Renderer Server                     | not started |
| 20 | Cinema 4D Bridge                    | not started |
| 21 | Future Native Cinema 4D Renderer    | not started |
| 22 | Node Editor / Material Graph        | not started |

## Milestone Status (mirrors `docs/MILESTONE_ROADMAP.md`)

| Milestone | Title                                   | Status      |
|-----------|-----------------------------------------|-------------|
| M0        | Architecture & Documentation            | in progress |
| M1        | Repository Skeleton & Build System      | not started |
| M2        | Core Engine: Logging, Config, Lifecycle | not started |
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
- No source code added. M0 is in progress and will be marked complete once
  this documentation set is reviewed and merged.

---

## Next Step

**M1 — Repository Skeleton & Build System.**

Concretely, the next implementation prompt should:

1. Create the source tree per the planned repository shape in
   `docs/MASTER_ARCHITECTURE.md` §8 (empty module subdirectories).
2. Introduce a top-level CMake project producing a host-only no-op CLI binary
   that links against an empty Core Engine.
3. Detect (but do not require) the CUDA toolchain. CUDA targets must be
   optional at this milestone so the host build always works.
4. Add a CI-friendly host-only build target.
5. Update this file:
   - Move M1 to `in progress`, then `landed` once shipped.
   - Add a Change Log entry describing what landed and what was deliberately
     deferred.

Per development rules, M1 must not introduce any code from M2+ modules
(no math, no image, no GPU kernels). It is pure scaffolding.

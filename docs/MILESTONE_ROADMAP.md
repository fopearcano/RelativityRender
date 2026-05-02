# RelativityRender — MILESTONE ROADMAP

This roadmap is the canonical order of work. It maps the 25-step development
order in `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` onto numbered
milestones with explicit goals, deliverables, and exit criteria.

A milestone is **complete** only when its exit criteria are met *and*
`docs/BUILD_PLAN.md` reflects the landed state.

Cinema 4D integration begins only after standalone milestones (M0 through
M16) are complete, per the master instructions.

The reverse direction is also a hard rule: **Cinema 4D / preview-UI /
node-editor work must never block the standalone renderer milestone**
(see `docs/DEVELOPMENT_RULES.md` §3.8). The dependency arrow points one
way: standalone milestones gate C4D / UI work, not the other way around.
A blocker, schedule slip, or API change on the C4D / UI side must not
pause the renderer core's progression. The four relevant items
(M19 / M20 / M21 / M23 above and master-order #22 Preview UI) are tracked
at status `not started` in `docs/MODULE_MAP.md`; their absence is not a
gate on any standalone slice.

---

## Maturity semantics

Per-milestone status uses the following five tiers (weakest →
strongest). They are stricter than `docs/MODULE_MAP.md`'s
six-tier module legend because milestones are scored against
their stated **exit criteria**, not against whether some code
under the relevant `src/` directory compiles. A milestone is
not "landed" unless its exit criteria are *truly* satisfied.

- **spec only** — design or planning documents exist, but no
  code yet (or only an inert placeholder file).
- **foundation landed** — host-side data PODs / scaffold
  types / enums compile and have unit-test coverage where
  applicable, but the milestone's **exit criteria are not
  satisfied** because the system has no real runtime
  function at the milestone's intended scope. Example: a
  `Light` POD union exists but no shadow rays / NEE — so a
  milestone whose exit criterion is "lit shaded scene with
  multiple light types" is not landed even though the
  lighting *data model* compiles.
- **partial implementation** — at least one production-style
  runtime path is in place, but key features are missing OR
  the GPU / SDK path that the milestone depends on is
  unverified on real hardware. The exit criteria's *visual*
  or *runtime* clauses are not pinned.
- **landed** — the milestone's exit criteria are satisfied
  end-to-end on the supported test matrix; the scope the
  milestone defined ships and works. The system has not yet
  been hardened with cross-cutting validation, regression
  baselines, or stress coverage.
- **production ready** — same as landed, plus regression
  baselines pinned, edge cases covered, and no documented
  "deferred" gate exists for the milestone's core runtime
  behaviour.

The line between **foundation landed** and **partial
implementation** is whether *any* runtime feature works; the
line between **partial implementation** and **landed** is
whether the *exit criteria* are satisfied. Milestones whose
exit criteria phrase a *visual* result ("output image clearly
shows...", "scene rendered at relativistic speeds shows...")
cannot graduate past "partial implementation" until a CUDA +
OptiX-SDK host run pins the visual baseline.

### Project-wide validation gate

A single project-wide gate caps every milestone whose exit
criteria is GPU-side at "partial implementation" until a
CUDA + OptiX-SDK host run pins regression baselines (per
`README.md` and `docs/STAGE_19_DENOISER_AUDIT.md` Q1 / Q2).
The audit-host build (no CUDA, no OptiX SDK) verifies code
structure + fallback semantics; runtime GPU output is
unexercised in this branch. The next step that lifts every
GPU-side milestone in lockstep is a single CUDA + OptiX-SDK
host run (Stage 19E or equivalent).

---

## Milestone status snapshot

Last verified: 2026-05-02 (post-Stage 19D + roadmap-consistency
audit + RR_ENABLE_OPTIX flag rename + module-status pass).
The fuller per-module status (which architectural module
backs each milestone) lives in `docs/MODULE_MAP.md`.

| #   | Milestone                              | Status                  | Validation needed? |
|-----|----------------------------------------|-------------------------|:------------------:|
| M0  | Architecture & Documentation           | landed                  | —                  |
| M1  | Repository Skeleton & Build System     | landed                  | —                  |
| M2  | Core Engine: Logging, Config, Lifecycle | partial implementation | host-only          |
| M3  | Math Library                           | landed                  | —                  |
| M4  | Image / Framebuffer System             | partial implementation  | host-only          |
| M5  | CUDA Device Layer                      | landed                  | —                  |
| M6  | CUDA Framebuffer & First Kernel        | partial implementation  | **GPU host**       |
| M7  | Camera System & GPU Camera Rays        | partial implementation  | **GPU host**       |
| M8  | GPU Primitive Intersection             | partial implementation  | **GPU host**       |
| M9  | Relativistic Camera Model (First Pass) | partial implementation  | **GPU host**       |
| M10 | GPU Scene Upload & Triangle Mesh       | partial implementation  | **GPU host**       |
| M11 | Material System (Foundations)          | foundation landed       | **GPU host** (after BSDFs land) |
| M12 | Lighting System (Foundations)          | foundation landed       | **GPU host** (after NEE / shadows land) |
| M13 | Scene File Format & Parser             | partial implementation  | host-only          |
| M14 | Path Tracing Foundation                | partial implementation  | **GPU host**       |
| M15 | OptiX Backend (Upgrade Path)           | partial implementation  | **OptiX-SDK host** |
| M16 | Texture System                         | foundation landed       | **GPU host** (after sampling lands) |
| M17 | Render Passes / AOVs                   | partial implementation  | **GPU host**       |
| M18 | Renderer Server                        | partial implementation  | **GPU host**       |
| M19 | Cinema 4D Bridge (Plugin)              | not started             | (pending M18)      |
| M20 | Preview UI                             | not started             | (pending M18)      |
| M21 | Material Node Graph (Editor)           | not started             | (pending M11)      |
| M22 | Denoiser Integration                   | partial implementation  | **OptiX-SDK host** |
| M23 | Native Cinema 4D Renderer Integration  | not started             | (pending M19)      |

Rollup: 4 landed (M0 / M1 / M3 / M5 — host-only milestones
whose exit criteria are met today), 13 partial-implementation
(every GPU-side milestone whose visual exit criteria is
gated on a real-hardware run, plus M2 / M4 / M13 whose host-
only deliverables are incomplete), 3 foundation-landed (M11
/ M12 / M16 — data PODs compile but the runtime feature the
milestone is named for is not on the device), 4 not-started
(M19 / M20 / M21 / M23), 0 spec-only.

### Milestones flagged for validation before landing

The following milestones require a one-time CUDA + OptiX-SDK
host run to graduate from "partial implementation" to
"landed". They are listed in dependency order; a single host
run can lift every entry in the GPU-host group in lockstep,
followed by the OptiX-SDK runs for M15 / M22. (Status of
each milestone is unchanged by the eventual run; the run
*pins* the exit criteria.)

- **GPU-host validation** (one CUDA-host run lifts all of
  these): M6, M7, M8, M9, M10, M13 (loader integration
  smoke test only), M14, M17, M18.
- **OptiX-SDK-host validation** (one OptiX-SDK-host run
  lifts both): M15, M22.
- **Per-module follow-ups before validation can lift the
  status** (each requires a slice of additional source code
  before the GPU-host run can pin its exit criteria):
    - M11: BSDF eval / sample / pdf must land first.
      Today's facing-ratio fallback does not satisfy "Same
      scene renders with real BSDFs."
    - M12: shadow rays + NEE must land first. Today's
      Point + Directional are real but Area + Environment
      are flagged PLACEHOLDER in source.
    - M16: GPU sampling beyond nearest-neighbour + path-
      tracer integration must land first. Today's POD +
      smoke-test sampler does not satisfy "Textured
      materials render correctly under the path tracer".
- **M2 follow-ups before landing**: `core::App` lifecycle,
  `core::Error` type, and `core::FileSystem` from the
  deliverables list. Today's Logger / Config / CommandLine
  satisfy the *exit criteria* literally ("tests for logging
  and config; CLI uses the engine to produce structured
  logs") but the deliverables list contains items not yet
  implemented; flagged here so the gap is visible.
- **M4 follow-ups before landing**: EXR + PNG load/save
  from the deliverables list. Today only PPM is
  implemented; M17's "Multi-channel EXR" exit criterion is
  blocked on this.

---

## M0 — Architecture & Documentation (CURRENT)

- **Goal:** Establish the platform's identity, layering, module map, rules,
  roadmap, and build plan.
- **Deliverables:**
  - `docs/MASTER_ARCHITECTURE.md`
  - `docs/MODULE_MAP.md`
  - `docs/DEVELOPMENT_RULES.md`
  - `docs/MILESTONE_ROADMAP.md`
  - `docs/BUILD_PLAN.md`
- **Exit criteria:** All five documents exist, are consistent, and define the
  22 long-term modules with dependency direction and forbidden dependencies.
- **No code.**

---

## M1 — Repository Skeleton & Build System

- **Goal:** Create the empty source tree and a working build (host-only) that
  produces a no-op CLI binary linking against an empty Core Engine.
- **Deliverables:**
  - Top-level CMake project (or chosen build system).
  - `src/`, `tests/`, `cmake/` directories present from the
    start. `bridges/`, `tools/`, and `third_party/` are added
    by the milestones that introduce their first contents
    (M19 / M20 / vendored dep) rather than pre-allocated as
    empty scaffolding — see `docs/MASTER_ARCHITECTURE.md` §8.
  - Module subdirectories under `src/` per
    `docs/MASTER_ARCHITECTURE.md` §8 (the architectural modules
    map to the actual directory names listed there:
    `src/cuda/`, `src/optix/`, `src/relativity/`, `src/io/`,
    etc.).
  - CI-friendly host-only build target.
- **Exit criteria:** Clean build on Linux. CLI runs and prints a banner. No
  GPU code yet.

---

## M2 — Core Engine: Logging, Config, Lifecycle

- **Goal:** Provide the foundational services every other module relies on.
- **Deliverables:**
  - `core::Logger` (severity levels, sinks).
  - `core::Config` (load/save).
  - `core::App` lifecycle entry points.
  - `core::Error` type.
  - `core::FileSystem` minimal IO.
- **Exit criteria:** Tests for logging and config. CLI uses the engine to
  produce structured logs.

---

## M3 — Math Library

- **Goal:** Land the pure math leaf used by every layer above.
- **Deliverables:**
  - Vec/Mat/Quat, Ray, AABB, transforms.
  - Sampling primitives (sphere/disk/hemisphere).
  - Color spaces (linear/sRGB).
  - `__host__ __device__`-friendly inline implementations (header-only where
    appropriate).
- **Exit criteria:** Unit tests for all primitives. No external dependencies.
  Buildable on host even before CUDA toolchain is present.

---

## M4 — Image / Framebuffer System

- **Goal:** Host-side image data structures and IO.
- **Deliverables:**
  - `Image`, `Framebuffer`, `AccumBuffer`.
  - EXR + PNG load/save.
  - Tile descriptors.
- **Exit criteria:** Round-trip tests. CLI can save a deterministic test image.

---

## M5 — CUDA Device Layer

- **Goal:** GPU device enumeration and a thin CUDA-backed device abstraction.
- **Deliverables:**
  - `gpu::Device` interface.
  - `cuda::Device`, `cuda::Stream`, `cuda::DeviceBuffer<T>`.
  - Error mapping.
  - Toolchain integration in the build (CUDA detected; if missing, CUDA tests
    are skipped, host build still works).
- **Exit criteria:** A "list devices" CLI command prints discovered GPUs.

---

## M6 — CUDA Framebuffer & First Kernel

- **Goal:** End-to-end host → device → host pipeline producing a real image.
- **Deliverables:**
  - Device-side framebuffer mirror.
  - A first kernel that writes a procedural pattern to the framebuffer
    (e.g., UV gradient).
  - Host download + EXR save.
- **Exit criteria:** CLI writes a deterministic GPU-generated image.

---

## M7 — Camera System & GPU Camera Rays

- **Goal:** Generate primary rays on GPU using the classical camera.
- **Deliverables:**
  - `camera::Camera` interface, perspective camera.
  - Device-side `generateRay`.
  - Visualize ray directions to a framebuffer (debug AOV-like view).
- **Exit criteria:** Output image clearly shows correct primary-ray directions.

---

## M8 — GPU Primitive Intersection

- **Goal:** Hit-test a single GPU primitive (sphere or implicit) so the
  framebuffer reflects intersection results.
- **Deliverables:**
  - Minimal device-side intersection.
  - Shaded output (normal-as-color, etc.).
- **Exit criteria:** Output image shows a shaded primitive rendered fully on
  GPU.

---

## M9 — Relativistic Camera Model (First Pass)

- **Goal:** Land the relativistic ray transformation as a wrapper over the
  classical camera, integrated at primary-ray generation.
- **Deliverables:**
  - `rel::Observer` (4-velocity).
  - `rel::transformRay`, `rel::aberrate`, `rel::dopplerFactor`.
  - Path through camera → relativistic transform → kernel.
  - Visual sanity tests against analytic expectations.
- **Exit criteria:** A scene rendered at relativistic speeds shows expected
  aberration/Doppler behavior on the simple GPU primitive from M8.

---

## M10 — GPU Scene Upload & Triangle Mesh

- **Goal:** Move from a single primitive to triangle meshes on GPU.
- **Deliverables:**
  - `geom::TriangleMesh` host representation.
  - Upload to device buffers.
  - Brute-force or simple BVH-driven intersection (still pre-OptiX).
- **Exit criteria:** A simple OBJ-like mesh renders on GPU under the
  relativistic camera.

---

## M11 — Material System (Foundations)

- **Goal:** Replace fixed normal-shading with a real BSDF interface.
- **Deliverables:**
  - `material::BSDF` interface.
  - Lambertian and a basic GGX material.
  - Material parameter binding (constants for now).
- **Exit criteria:** Same scene renders with real BSDFs. Energy conservation
  spot-checked.

---

## M12 — Lighting System (Foundations)

- **Goal:** First-class lights with importance sampling.
- **Deliverables:**
  - Point, directional, area lights.
  - NEE-friendly sampling routines.
- **Exit criteria:** A lit shaded scene renders with multiple light types.

---

## M13 — Scene File Format & Parser

- **Goal:** Define the canonical scene format and load it.
- **Deliverables:**
  - Versioned schema.
  - Loader producing a `scene::Scene`.
  - Saver from `scene::Scene`.
- **Exit criteria:** A scene authored as a file renders identically through
  the CLI.

---

## M14 — Path Tracing Foundation

- **Goal:** First real integrator: unidirectional path tracing with NEE + MIS,
  Russian roulette.
- **Deliverables:**
  - `pt::Renderer`.
  - Multi-bounce path tracing on GPU.
  - Reference comparisons against simple analytic / Cornell-style scenes.
- **Exit criteria:** Path-traced output converges to expected reference
  within tolerance.

---

## M15 — OptiX Backend (Upgrade Path)

- **Goal:** Introduce OptiX for acceleration structures and ray dispatch.
- **Deliverables:**
  - OptiX context, modules, programs, SBT.
  - GAS/IAS builds for the geometry module.
  - Path tracer dispatcher path that uses OptiX.
- **Exit criteria:** Path tracer renders the same scene through both the CUDA
  and OptiX paths with matching results (within tolerance).

---

## M16 — Texture System

- **Goal:** Real textures driving BSDF parameters.
- **Deliverables:**
  - 2D textures + samplers on GPU.
  - Image-driven albedo / roughness inputs to BSDFs.
- **Exit criteria:** Textured materials render correctly under the path
  tracer.

---

## M17 — Render Passes / AOVs

- **Goal:** Standard AOVs plus the relativistic AOVs.
- **Deliverables:**
  - Beauty, albedo, normal, depth, motion, ID.
  - Doppler factor, observed direction, retarded time, frame velocity.
  - EXR multi-channel output.
- **Exit criteria:** Multi-channel EXR contains expected, correct passes.

---

## M18 — Renderer Server

- **Goal:** Standalone long-running render service over IPC/network.
- **Deliverables:**
  - Wire protocol.
  - Service binary.
  - CLI client to submit scene + receive frames/AOVs.
- **Exit criteria:** External process can submit a scene file and receive a
  rendered EXR back.

---

## M19 — Cinema 4D Bridge (Plugin)

- **Goal:** First C4D integration — strictly as a client of the server.
- **Deliverables:**
  - C4D plugin under `bridges/c4d_bridge/`.
  - C4D scene → Scene File Format translator.
  - Server-protocol client.
  - Preview frame display in C4D.
- **Exit criteria:** A C4D scene renders through the server and the result is
  shown in the C4D viewport. The bridge does not link renderer internals.

---

## M20 — Preview UI

- **Goal:** Standalone preview viewer outside C4D for development.
- **Deliverables:**
  - Minimal UI (whatever framework is chosen).
  - Server protocol client.
- **Exit criteria:** A tester can load a scene file and watch progressive
  frames stream from the server.

---

## M21 — Material Node Graph (Editor)

- **Goal:** Visual material authoring; emits Scene File Format material
  blocks. Pure UI.
- **Deliverables:**
  - Node editor under `tools/node_editor/`.
  - Compile-to-format pipeline.
- **Exit criteria:** A graph authored in the editor renders identically to
  hand-written equivalents.

---

## M22 — Denoiser Integration

- **Goal:** Production-grade denoising.
- **Deliverables:**
  - OptiX denoiser and/or OIDN wrappers.
  - AOV preparation (albedo/normal).
- **Exit criteria:** Denoised output is selectable via config and visibly
  improves low-sample renders.

---

## M23 — Native Cinema 4D Renderer Integration

- **Goal:** Register RelativityRender as a native C4D renderer / video post.
- **Deliverables:**
  - Plugin under `bridges/c4d_native/`.
  - Direct mapping from C4D state into Scene File Format.
- **Exit criteria:** A C4D project can pick "RelativityRender" as the active
  renderer and render frames through C4D's standard render pipeline.

---

## Milestone Dependency Quick Reference

```
M0 -> M1 -> M2 -> M3 -> M4 -> M5 -> M6 -> M7 -> M8 -> M9
                                                       |
                                                       v
                                          M10 -> M11 -> M12 -> M13 -> M14
                                                                       |
                                                                       v
                                                                M15 -> M16 -> M17 -> M18
                                                                                          |
                                                                                          v
                                                                              M19 -> M20 -> M21 -> M22 -> M23
```

Cinema 4D work (M19+) does not begin until M18 ships a working server.

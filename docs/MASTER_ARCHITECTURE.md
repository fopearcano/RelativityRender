# RelativityRender — MASTER ARCHITECTURE

Status: **Architecture document only. No renderer code exists yet.**

This document defines the long-term architecture of RelativityRender. It is the
authoritative description of what the platform IS, how it is layered, and how
the modules relate to each other. It is read alongside:

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` (root)
- `docs/MODULE_MAP.md`
- `docs/DEVELOPMENT_RULES.md`
- `docs/MILESTONE_ROADMAP.md`
- `docs/BUILD_PLAN.md`

---

## 1. What RelativityRender Is

RelativityRender is a **serious GPU renderer platform**.

It is:

- CUDA-first, OptiX-first.
- Designed around GPU path tracing.
- Built around a unique **relativistic camera / perception model** (relativistic
  aberration, Doppler color shift, searchlight/headlight effect, Lorentz-style
  perception, retarded-time approximation).
- A standalone renderer first, with a renderer server protocol.
- Eventually integrated with Cinema 4D via a bridge plugin.
- Eventually shippable as a native Cinema 4D renderer.

It is NOT:

- A toy renderer.
- A demo.
- "Just a Cinema 4D plugin."
- An Octane / Redshift / Arnold clone.
- A CPU ray tracer.

The relativistic perception model is the differentiator. Every other subsystem
(scene graph, materials, lighting, denoising, AOVs, server, bridge) exists to
make that model usable in production.

---

## 2. Architectural Pillars

1. **GPU is the renderer.** All per-pixel and per-ray work happens on GPU
   (CUDA / OptiX). The CPU never produces final pixels.
2. **CPU is the orchestrator.** CPU parses scenes, manages IO, uploads data,
   launches kernels, receives framebuffers, runs the server. CPU never ray-traces
   for production.
3. **Layered, one-way dependencies.** Lower layers know nothing about higher
   layers. The renderer core knows nothing about UI or Cinema 4D.
4. **The Cinema 4D bridge is a client of the renderer, not part of it.** It
   communicates over the scene format and server protocol — never by linking
   against renderer internals.
5. **Incremental, always-compilable growth.** Every module lands as a working,
   testable layer before the next one starts. No fake stubs pretending to be
   complete systems.
6. **Relativistic camera is a first-class system**, not a post effect bolted on
   the side. It is integrated into the camera/path tracer pipeline.

---

## 3. Layered Architecture (Top-Level View)

Layers are listed from bottom (foundational) to top (integration / UX). A higher
layer may depend on lower layers; a lower layer may NEVER depend on a higher
one.

```
+-------------------------------------------------------------+
| L7  Integrations & Authoring                                |
|     - Cinema 4D Bridge                                      |
|     - Future Native Cinema 4D Renderer                      |
|     - Node Editor / Material Graph (UI)                     |
|     - Preview UI                                            |
+-------------------------------------------------------------+
| L6  Renderer Service Layer                                  |
|     - Renderer Server (IPC / network protocol)              |
|     - Scene File Format (parser / serializer)               |
+-------------------------------------------------------------+
| L5  Rendering Algorithms                                    |
|     - Path Tracer                                           |
|     - Progressive Renderer                                  |
|     - Render Passes / AOVs                                  |
|     - Denoiser Integration                                  |
+-------------------------------------------------------------+
| L4  Rendering Domain                                        |
|     - Scene Graph                                           |
|     - Geometry System                                       |
|     - Material / Shading System                             |
|     - Texture System                                        |
|     - Lighting System                                       |
|     - Camera System                                         |
|     - Relativistic Camera Model                             |
+-------------------------------------------------------------+
| L3  GPU Backends                                            |
|     - CUDA Backend                                          |
|     - OptiX Backend                                         |
+-------------------------------------------------------------+
| L2  GPU Abstraction                                         |
|     - GPU Device Layer (device, stream, buffer, kernel API) |
+-------------------------------------------------------------+
| L1  Foundations                                             |
|     - Math Library                                          |
|     - Image / Framebuffer System                            |
+-------------------------------------------------------------+
| L0  Core Engine                                             |
|     - Application lifecycle                                 |
|     - Logging                                               |
|     - Config                                                |
|     - Error handling                                        |
|     - IO primitives                                         |
+-------------------------------------------------------------+
```

The Core Engine (L0) sits "below" everything in terms of services it provides
(logging/config/IO), but it is itself UI-agnostic and DCC-agnostic. It does not
depend on anything in L4–L7.

---

## 4. The 22 Long-Term Modules

Brief one-line role for each. Full ownership and dependency rules are in
`docs/MODULE_MAP.md`.

1. **Core Engine** — App lifecycle, logging, config, error handling, IO.
2. **Math Library** — Vectors, matrices, quaternions, ray, AABB, transforms,
   sampling primitives, color spaces. Pure, header-first, GPU-friendly.
3. **Image / Framebuffer System** — Tiled image buffers, pixel formats,
   accumulation buffers, image IO (EXR/PNG).
4. **GPU Device Layer** — Backend-agnostic GPU abstraction: device enumeration,
   streams, buffers, kernel launch interface.
5. **CUDA Backend** — Concrete CUDA implementation of the GPU Device Layer plus
   CUDA-native kernels and memory management.
6. **OptiX Backend** — OptiX traversable/SBT/pipeline management, ray scheduling,
   built on top of CUDA Backend.
7. **Scene Graph** — Transform hierarchy, node references to geometry, materials,
   lights, cameras. Pure data; no GPU types leak in.
8. **Geometry System** — Triangle meshes, instancing, BVH/AS build inputs,
   curves (later), volumes (later). GPU-uploadable form.
9. **Material / Shading System** — BSDF interface, layered materials, parameter
   binding, shader evaluation on GPU.
10. **Texture System** — 2D/3D textures, samplers, MIP, UDIM, baked textures,
    procedural inputs.
11. **Lighting System** — Light types (area, point, directional, environment),
    emissive geometry, importance sampling.
12. **Camera System** — Perspective, orthographic, thin-lens DOF, motion blur
    sampling. Generates primary rays.
13. **Relativistic Camera Model** — Ray-level relativistic transformation:
    aberration, Doppler shift, searchlight beaming, Lorentz boost, retarded-time.
    Sits on top of Camera System.
14. **Path Tracer** — Integrator: BSDF sampling, NEE, MIS, Russian roulette.
    GPU-side. Uses CUDA and/or OptiX.
15. **Progressive Renderer** — Sample budgeting, tile/sample scheduling,
    convergence, interactive refresh.
16. **Denoiser Integration** — OptiX denoiser / OIDN wrapper. Operates on
    framebuffer + AOV inputs.
17. **Render Passes / AOVs** — Beauty, albedo, normal, depth, motion vectors,
    relativistic AOVs (per-ray velocity, Doppler factor), arbitrary user passes.
18. **Scene File Format** — On-disk and over-the-wire representation of scenes.
    The contract between authoring tools and the renderer.
19. **Renderer Server** — Long-running render process exposing a protocol over
    IPC/network. Accepts scenes, returns framebuffers/AOVs, supports progressive
    updates.
20. **Cinema 4D Bridge** — Cinema 4D plugin. Translates C4D scene state into the
    Scene File Format and talks to the Renderer Server. **Does not** link
    against renderer internals.
21. **Future Native Cinema 4D Renderer** — Deeper C4D integration as a registered
    renderer/video post. Built on the same renderer core via the same scene
    format.
22. **Node Editor / Material Graph** — UI tooling for authoring materials and,
    eventually, render graphs. Pure UI; renderer never depends on it.

---

## 5. Dependency Direction (Summary)

The full matrix lives in `docs/MODULE_MAP.md`. The hard rules:

- **Math Library** depends on nothing. It must remain pure.
- **Image System** depends only on Math.
- **GPU Device Layer** depends on Math.
- **CUDA Backend** depends on GPU Device Layer, Math, Image.
- **OptiX Backend** depends on CUDA Backend, GPU Device Layer, Math, Image.
- **Scene Graph** depends on Math. It does NOT know about GPU backends.
- **Geometry, Material, Texture, Lighting, Camera, Relativistic Camera** depend
  on Math, Image, Scene Graph (as needed). They do NOT depend on the path
  tracer or higher.
- **Path Tracer** depends on the Rendering Domain (L4) and GPU Backends (L3).
- **Progressive Renderer / AOVs / Denoiser** depend on the Path Tracer and
  Image System.
- **Scene File Format** depends only on Scene Graph + Math.
- **Renderer Server** depends on Core Engine + Scene File Format + Renderer
  Algorithms. It does NOT depend on UI or Cinema 4D.
- **Cinema 4D Bridge** depends on Scene File Format and the Renderer Server
  protocol. It MUST NOT depend on internal renderer code.
- **Future Native Cinema 4D Renderer** is the only module other than the bridge
  allowed to depend on the Cinema 4D SDK.
- **Node Editor / Material Graph / Preview UI** are top-level and may depend on
  lower layers. NOTHING below them depends on them.

---

## 6. Forbidden Dependencies

These rules are enforced by code review and module structure.

| Module                               | MUST NOT depend on                                       |
|--------------------------------------|----------------------------------------------------------|
| Core Engine                          | UI, Cinema 4D, Node Editor, any renderer algorithm       |
| Math Library                         | Anything (must stay pure)                                |
| Image / Framebuffer                  | UI, Cinema 4D, GPU backends, renderer algorithms         |
| GPU Device Layer                     | UI, Cinema 4D, renderer algorithms                       |
| CUDA Backend                         | UI, Cinema 4D, OptiX Backend (CUDA is below OptiX)       |
| OptiX Backend                        | UI, Cinema 4D                                            |
| Scene Graph                          | UI, Cinema 4D, GPU backends                              |
| Geometry System                      | UI, Cinema 4D, Path Tracer, Server                       |
| Material / Shading                   | UI, Cinema 4D, Path Tracer (uses interfaces only)        |
| Texture System                       | UI, Cinema 4D, Path Tracer                               |
| Lighting System                      | UI, Cinema 4D, Path Tracer                               |
| Camera System                        | UI, Cinema 4D, Path Tracer                               |
| Relativistic Camera Model            | UI, Cinema 4D, Path Tracer                               |
| Path Tracer                          | UI, Cinema 4D, Renderer Server, Bridge                   |
| Progressive Renderer                 | UI, Cinema 4D                                            |
| Denoiser Integration                 | UI, Cinema 4D                                            |
| Render Passes / AOVs                 | UI, Cinema 4D                                            |
| Scene File Format                    | UI, Cinema 4D, GPU backends, Path Tracer internals       |
| Renderer Server                      | UI, Cinema 4D                                            |
| Cinema 4D Bridge                     | Renderer internals (anything other than format/protocol) |
| Future Native C4D Renderer           | UI / Node Editor (uses scene format + core only)         |
| Node Editor / Material Graph (UI)    | (everything below it may be used; nothing depends on it) |

The single most important invariant of the project: **the renderer core does
not know that Cinema 4D, a node editor, or any UI exists.**

---

## 7. Data Flow (End-to-End, Long-Term)

```
[Authoring Tool]            (Cinema 4D, future Node Editor, CLI)
       |
       v
[Scene File Format]         (canonical scene description)
       |
       v
[Renderer Server]           (long-running process, accepts jobs)
       |
       v
[Core Engine]               (logging, config, lifecycle)
       |
       v
[Scene Graph + Domain]      (geometry, materials, textures, lights, cameras,
       |                     relativistic camera)
       v
[GPU Upload]                (CUDA Backend / OptiX Backend buffers + AS)
       |
       v
[Path Tracer Kernel]        (GPU-only per-ray work; uses relativistic camera)
       |
       v
[Progressive Renderer]      (sample scheduling, convergence)
       |
       v
[Render Passes / AOVs]      (beauty + albedo/normal/depth/Doppler/...)
       |
       v
[Denoiser Integration]      (optional)
       |
       v
[Image / Framebuffer]       (final buffers, EXR/PNG output, server stream)
       |
       v
[Client]                    (Cinema 4D Bridge, CLI viewer, Preview UI)
```

The relativistic camera model is **inside** the per-ray pipeline, not a
post-process. AOVs may export relativistic quantities (Doppler factor, observed
direction, retarded time) for analysis and shading.

---

## 8. Build / Repository Shape (Planned)

The repo will eventually contain (kept minimal until each layer lands):

```
RelativityRender/
  docs/                       # this directory
  cmake/                      # CMake helpers (when build system is introduced)
  third_party/                # vendored or fetched deps
  src/
    core/                     # Core Engine
    math/                     # Math Library
    image/                    # Image / Framebuffer
    gpu/                      # GPU Device Layer
    cuda_backend/             # CUDA Backend
    optix_backend/            # OptiX Backend
    scene/                    # Scene Graph
    geometry/                 # Geometry System
    material/                 # Material / Shading
    texture/                  # Texture System
    lighting/                 # Lighting System
    camera/                   # Camera System
    relativistic/             # Relativistic Camera Model
    pathtracer/               # Path Tracer
    progressive/              # Progressive Renderer
    denoise/                  # Denoiser Integration
    aov/                      # Render Passes / AOVs
    scene_format/             # Scene File Format
    server/                   # Renderer Server
    cli/                      # Standalone CLI renderer
  bridges/
    c4d_bridge/               # Cinema 4D Bridge plugin
    c4d_native/               # Future Native Cinema 4D Renderer
  tools/
    node_editor/              # UI: Node Editor / Material Graph
    preview_ui/               # UI: Preview client
  tests/                      # Unit + integration tests
```

This layout is enforced by the dependency rules above. Top-level directories
exist primarily to make forbidden dependencies easy to spot in `#include` paths
and CMake link lists.

---

## 9. Non-Goals

- CPU ray tracing as a production renderer path.
- A renderer that depends on Cinema 4D to function.
- A monolithic single-binary plugin.
- "Quick wins" that hardcode UI assumptions into the core.
- Hand-tuning a single demo scene at the cost of the platform.

---

## 10. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — top-level rules (read
  every session).
- `docs/MODULE_MAP.md` — per-module ownership + dependency rules.
- `docs/DEVELOPMENT_RULES.md` — coding/process rules.
- `docs/MILESTONE_ROADMAP.md` — milestones and exit criteria.
- `docs/BUILD_PLAN.md` — current build state and next step.

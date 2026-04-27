# RelativityRender — MODULE MAP

This document is the per-module contract. For every long-term module it defines:

- **Owns** — what data and behavior live in this module.
- **Depends on** — modules it is allowed to use.
- **Forbidden** — modules it must NOT use.
- **Public surface** — informal description of what other modules may call.
- **GPU-side?** — whether the module ships kernels / device code.
- **Status** — `not started`, `in progress`, or `landed`.

All modules are currently `not started`. No code exists yet.

The forbidden lists are normative. A change that violates them must be rejected
in code review.

Layer abbreviations match `docs/MASTER_ARCHITECTURE.md`:
L0 Core, L1 Foundations, L2 GPU Abstraction, L3 GPU Backends, L4 Domain,
L5 Algorithms, L6 Service, L7 Integrations/UI.

---

## 1. Core Engine — `src/core/` (L0)

- **Owns:** application lifecycle, logging, config (TOML/JSON-ish), error
  types, file IO primitives, time/clock, threading helpers, command-line entry
  points used by CLI/server.
- **Depends on:** standard library, third-party logging/config only.
- **Forbidden:** Math, GPU, Scene Graph, Path Tracer, UI, Cinema 4D. Core is
  beneath the renderer; it provides services, it does not pull renderer code in.
- **Public surface:** `core::Logger`, `core::Config`, `core::Error`,
  `core::FileSystem`, `core::App` lifecycle.
- **GPU-side?** No.
- **Status:** not started.

---

## 2. Math Library — `src/math/` (L1)

- **Owns:** `Vec2/3/4`, `Mat3/4`, `Quat`, `Ray`, `AABB`, transforms, intersection
  helpers, sampling primitives (sphere/disk/hemisphere), color spaces (linear /
  sRGB / spectral approximations later), constants, GPU-friendly inline impls.
- **Depends on:** standard library only.
- **Forbidden:** **Everything.** Math must remain a pure leaf.
- **Public surface:** Header-first types and inline functions usable on host
  and device (CUDA `__host__ __device__`).
- **GPU-side?** Yes (header-only / device-callable).
- **Status:** not started.

---

## 3. Image / Framebuffer System — `src/image/` (L1)

- **Owns:** pixel formats (RGB32F, RGBA32F, half), framebuffer/accumulation
  buffer abstractions, tile descriptors, image IO (EXR via OpenEXR, PNG via
  stb-image), tone mapping primitives.
- **Depends on:** Math, Core Engine (for IO + logging), third-party EXR/PNG.
- **Forbidden:** GPU backends (an image is just data; GPU mirrors live in CUDA
  Backend), Scene Graph, Renderer Algorithms, UI, Cinema 4D.
- **Public surface:** `Image`, `Framebuffer`, `AccumBuffer`, `ImageIO::loadEXR`,
  `ImageIO::saveEXR`.
- **GPU-side?** No (host-side only; GPU companions live in CUDA Backend).
- **Status:** not started.

---

## 4. GPU Device Layer — `src/gpu/` (L2)

- **Owns:** backend-agnostic interface for GPU resources: device enumeration,
  device handles, streams, events, generic buffer types (typed and raw),
  kernel launch descriptor, error wrapping.
- **Depends on:** Math, Core Engine.
- **Forbidden:** Scene Graph, Renderer Algorithms, UI, Cinema 4D, OptiX directly.
  Implementations live in CUDA Backend / OptiX Backend.
- **Public surface:** `gpu::Device`, `gpu::Stream`, `gpu::Buffer<T>`,
  `gpu::KernelLaunch`, error helpers.
- **GPU-side?** Interface only; no kernels.
- **Status:** not started.

---

## 5. CUDA Backend — `src/cuda_backend/` (L3)

- **Owns:** concrete CUDA implementation of GPU Device Layer; CUDA streams,
  pinned host memory, device memory allocation, async copies, kernel
  registration, error string mapping; reusable kernel utilities (reduction,
  atomic accumulation into framebuffer); CUDA build glue.
- **Depends on:** GPU Device Layer, Math, Image, Core Engine, CUDA toolkit.
- **Forbidden:** Scene Graph internals, UI, Cinema 4D, OptiX. CUDA Backend is
  *below* OptiX Backend.
- **Public surface:** `cuda::Device`, `cuda::DeviceBuffer<T>`,
  `cuda::launchKernel(...)`, framebuffer mirrors of Image types.
- **GPU-side?** Yes.
- **Status:** not started.

---

## 6. OptiX Backend — `src/optix_backend/` (L3)

- **Owns:** OptiX context creation, module/program group/pipeline/SBT
  management, acceleration structure builds (GAS/IAS), traversable handles,
  ray scheduling helpers.
- **Depends on:** CUDA Backend, GPU Device Layer, Math, Image, Core Engine,
  OptiX SDK.
- **Forbidden:** Scene Graph internals (it consumes geometry already prepared by
  Geometry System), UI, Cinema 4D.
- **Public surface:** `optix::Context`, `optix::Pipeline`, `optix::SBT`,
  `optix::Traversable`, helpers to build AS from geometry buffers.
- **GPU-side?** Yes.
- **Status:** not started.

---

## 7. Scene Graph — `src/scene/` (L4)

- **Owns:** scene node hierarchy, transforms (local + world), node references
  to geometry/material/light/camera resources, traversal helpers, scene-level
  bounds, scene IDs.
- **Depends on:** Math, Core Engine.
- **Forbidden:** GPU backends, Path Tracer, UI, Cinema 4D, Scene File Format
  (the format depends on scene graph, not the other way around).
- **Public surface:** `scene::Node`, `scene::Scene`, `scene::Transform`,
  resource handle types.
- **GPU-side?** No (pure host data).
- **Status:** not started.

---

## 8. Geometry System — `src/geometry/` (L4)

- **Owns:** triangle mesh representation, instancing data, attribute layouts
  (position/normal/uv/tangent), curve and volume placeholders for the future,
  GPU-uploadable forms (interleaved or SoA), AS build inputs for OptiX.
- **Depends on:** Math, Scene Graph, Image (for displacement maps later),
  GPU Device Layer (for upload abstractions only).
- **Forbidden:** UI, Cinema 4D, Path Tracer, Renderer Server.
- **Public surface:** `geom::TriangleMesh`, `geom::Instance`,
  `geom::buildUploadDescriptor(...)`.
- **GPU-side?** Provides device-friendly layouts; kernels live elsewhere.
- **Status:** not started.

---

## 9. Material / Shading System — `src/material/` (L4)

- **Owns:** BSDF interface, parameterized BSDFs (Lambert, GGX, dielectric,
  layered), material parameter binding (constants + textures), shader evaluation
  callable on device, PDF/sample/eval triple per BSDF.
- **Depends on:** Math, Texture System, Scene Graph.
- **Forbidden:** Path Tracer, UI, Cinema 4D, Node Editor. The path tracer calls
  `material::eval/sample/pdf` — not the other way around.
- **Public surface:** `material::BSDF` interface, concrete BSDFs,
  `material::Material` (parameter pack).
- **GPU-side?** Yes (shader code is device-callable).
- **Status:** not started.

---

## 10. Texture System — `src/texture/` (L4)

- **Owns:** 2D/3D texture storage, sampler descriptors (wrap, filter, MIP),
  UDIM, texture cache, baked textures, procedural texture inputs that fan into
  the BSDF parameter graph.
- **Depends on:** Math, Image, GPU Device Layer.
- **Forbidden:** Path Tracer, UI, Cinema 4D, Node Editor.
- **Public surface:** `texture::Texture2D`, `texture::Texture3D`,
  `texture::Sampler`, device-side sample helpers.
- **GPU-side?** Yes.
- **Status:** not started.

---

## 11. Lighting System — `src/lighting/` (L4)

- **Owns:** light types (point, directional, area, environment), emissive mesh
  link-up, light importance sampling, light tree / power-based selection.
- **Depends on:** Math, Scene Graph, Texture System (for env maps), Geometry
  (for emissive triangles).
- **Forbidden:** Path Tracer, UI, Cinema 4D.
- **Public surface:** `light::Light` interface, concrete lights,
  `light::sampleLight(...)`, `light::evalEnv(...)`.
- **GPU-side?** Yes.
- **Status:** not started.

---

## 12. Camera System — `src/camera/` (L4)

- **Owns:** camera types (perspective, orthographic), thin-lens DOF, motion
  blur sampling, ray generation in classical (non-relativistic) form, screen-space
  and lens-space sampling.
- **Depends on:** Math, Scene Graph.
- **Forbidden:** Path Tracer, UI, Cinema 4D, Relativistic Camera Model (the
  Relativistic Camera Model wraps / extends Camera, not the other way around).
- **Public surface:** `camera::Camera` interface, `camera::generateRay(...)`.
- **GPU-side?** Yes (ray generation is device-callable).
- **Status:** not started.

---

## 13. Relativistic Camera Model — `src/relativistic/` (L4)

- **Owns:** the differentiator. Lorentz boost on ray directions, relativistic
  aberration, Doppler color shift, searchlight/headlight beaming, retarded-time
  approximation, observer 4-velocity, relativistic ray transformation pipeline.
- **Depends on:** Math, Camera System.
- **Forbidden:** Path Tracer, UI, Cinema 4D. The path tracer *uses* this module;
  this module does not know the path tracer exists.
- **Public surface:** `rel::Observer`, `rel::transformRay(...)`,
  `rel::dopplerFactor(...)`, `rel::aberrate(...)`,
  `rel::generateRelativisticRay(...)` that wraps `camera::generateRay`.
- **GPU-side?** Yes (device-callable transformations).
- **Status:** not started.

---

## 14. Path Tracer — `src/pathtracer/` (L5)

- **Owns:** integrator(s): unidirectional path tracer with NEE + MIS, Russian
  roulette, tile/ray scheduling, hit handling, BSDF/light interaction loop.
  Supports both CUDA-only and OptiX-driven dispatch.
- **Depends on:** CUDA Backend, OptiX Backend, Scene Graph, Geometry, Material,
  Texture, Lighting, Camera, Relativistic Camera, Image.
- **Forbidden:** UI, Cinema 4D, Renderer Server, Cinema 4D Bridge, Node Editor.
  The path tracer does not know who is asking it to render.
- **Public surface:** `pt::Renderer`, `pt::renderFrame(scene, camera, fb, cfg)`.
- **GPU-side?** Yes.
- **Status:** not started.

---

## 15. Progressive Renderer — `src/progressive/` (L5)

- **Owns:** sample budgeting per pixel/tile, progressive accumulation control,
  convergence metrics, interactive refresh hooks, optional adaptive sampling.
- **Depends on:** Path Tracer, Image, Core Engine.
- **Forbidden:** UI, Cinema 4D. It reports progress through callbacks/buffers,
  not by calling UI.
- **Public surface:** `progressive::Session`, callbacks for "tile updated".
- **GPU-side?** Mixed (host scheduling + device kernels in path tracer).
- **Status:** not started.

---

## 16. Denoiser Integration — `src/denoise/` (L5)

- **Owns:** wrappers for OptiX denoiser and/or OIDN, AOV preparation for the
  denoiser (albedo/normal), temporal denoising hooks (later).
- **Depends on:** Image, AOV, GPU Device Layer, OptiX Backend (for OptiX
  denoiser), Core Engine.
- **Forbidden:** UI, Cinema 4D, Path Tracer internals (uses output buffers and
  AOVs, not internal kernel state).
- **Public surface:** `denoise::Denoiser`, `denoise::run(fb, aovs)`.
- **GPU-side?** Yes (delegates to denoiser library kernels).
- **Status:** not started.

---

## 17. Render Passes / AOVs — `src/aov/` (L5)

- **Owns:** AOV channel registry: beauty, albedo, normal, depth, motion vector,
  object/material ID, and the relativistic AOVs (per-ray Doppler factor,
  observed direction, retarded time, frame-velocity-magnitude).
- **Depends on:** Image, Math, Path Tracer interfaces (writes during shading).
- **Forbidden:** UI, Cinema 4D.
- **Public surface:** `aov::Registry`, `aov::Pass` definitions, device-side
  write helpers used by path tracer kernels.
- **GPU-side?** Yes (write-only helpers in kernels).
- **Status:** not started.

---

## 18. Scene File Format — `src/scene_format/` (L6)

- **Owns:** the canonical on-disk and over-the-wire scene description.
  Versioned. Parser and serializer. The contract between authoring tools and
  the renderer.
- **Depends on:** Scene Graph, Math, Core Engine, third-party JSON / USD-like
  serialization library.
- **Forbidden:** GPU backends, Path Tracer internals, UI, Cinema 4D. Scene File
  Format is data-only.
- **Public surface:** `scene_format::load(path) -> scene::Scene`,
  `scene_format::save(scene, path)`, schema version constants.
- **GPU-side?** No.
- **Status:** not started.

---

## 19. Renderer Server — `src/server/` (L6)

- **Owns:** long-running render service. Listens on IPC/network, accepts scene
  payloads (Scene File Format), launches Path Tracer / Progressive Renderer,
  streams framebuffers and AOVs back, manages cancellation, multi-job queuing.
- **Depends on:** Core Engine, Scene File Format, Scene Graph, Path Tracer,
  Progressive Renderer, AOV, Image.
- **Forbidden:** UI, Cinema 4D. The server has no idea what its clients are.
- **Public surface:** wire protocol (documented separately when this module
  starts), CLI to start/stop the server, message types.
- **GPU-side?** No (it drives GPU code).
- **Status:** not started.

---

## 20. Cinema 4D Bridge — `bridges/c4d_bridge/` (L7)

- **Owns:** Cinema 4D plugin that observes the C4D scene, translates it into
  Scene File Format, and talks to the Renderer Server over its protocol.
  Handles preview frames, parameter sync, and progressive updates on the C4D
  side only.
- **Depends on:** Cinema 4D SDK, Scene File Format, Renderer Server protocol
  (client side), small shared utility code.
- **Forbidden:** Renderer internals — Path Tracer, GPU backends, Scene Graph
  internal types, kernels, AOV write helpers. The bridge is a *client* of the
  renderer.
- **Public surface:** Cinema 4D plugin entry points only.
- **GPU-side?** No.
- **Status:** not started.

---

## 21. Future Native Cinema 4D Renderer — `bridges/c4d_native/` (L7)

- **Owns:** registration of RelativityRender as a native Cinema 4D renderer /
  video post. Maps C4D camera/lights/materials directly into Scene File Format
  and drives the renderer in-process via the public renderer API (not internal
  headers).
- **Depends on:** Cinema 4D SDK, Scene File Format, Renderer Server (in-process
  mode) or Path Tracer public façade.
- **Forbidden:** UI, Node Editor, internal Path Tracer state, internal GPU
  kernel headers.
- **Public surface:** Cinema 4D registration entry points.
- **GPU-side?** No (drives GPU through renderer API).
- **Status:** not started.

---

## 22. Node Editor / Material Graph — `tools/node_editor/` (L7)

- **Owns:** authoring UI for materials and (later) render graphs. Compiles a
  graph into a Scene File Format material/graph block. Pure UI; uses no
  renderer internals.
- **Depends on:** UI framework, Scene File Format, Math (for displays), maybe
  Image (for thumbnails).
- **Forbidden:** Path Tracer, GPU backends, Renderer Server internals,
  Cinema 4D SDK. NOTHING below L7 may depend on this module.
- **Public surface:** standalone application (or embeddable widget) that emits
  Scene File Format documents.
- **GPU-side?** No.
- **Status:** not started.

---

## Cross-Cutting Rules

1. **Renderer Core never depends on UI or Cinema 4D.** Specifically, modules
   1–19 must never `#include` or link anything from L7. Build files (CMake) and
   `#include` paths are organized so this is visible at a glance.
2. **The renderer is reachable two ways:** in-process via a public façade, or
   out-of-process via the Renderer Server protocol. The Cinema 4D Bridge uses
   the protocol. This is the contract that keeps the bridge decoupled.
3. **GPU backends are interchangeable behind the GPU Device Layer.** Higher
   layers should prefer the abstraction; OptiX-specific code is allowed only in
   the OptiX Backend and inside the Path Tracer's OptiX-specific dispatcher.
4. **No module short-circuits the layering for convenience.** If you find
   yourself wanting to call up a layer, the design is wrong; introduce a
   callback or an interface owned by the lower layer.
5. **Relativistic Camera is not a post-process.** It is integrated at ray
   generation and during shading where Doppler/aberration affect light
   transport. AOVs may export relativistic quantities.

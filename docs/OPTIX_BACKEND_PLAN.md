# OptiX Backend — Migration Plan

Date: 2026-04-30
Branch: `relativity-core-v1`
Status: **planning only**. This document is being built up
incrementally across the Stage 12A sub-stages; no OptiX code is
implemented in any of them. The CUDA path tracer landed in
Stage 11C remains the project's canonical render path until at
least Stage 12B.

This sub-stage (12A.1) covers only the four motivation
sections below. Subsequent sub-stages append the rest of the
plan (raygen / miss / closest-hit programs, acceleration
structures, shader binding table, material + camera data flow,
relativity integration, path-tracing integration, file layout,
migration risks).

---

## 1. Purpose

This document is the design reference for the OptiX backend
slice of RelativityRender. It captures the *why*, *what*, and
*how* of replacing the project's current naive CUDA closest-hit
loops with OptiX-accelerated traversal, before any OptiX code
is written. The intent is twofold:

- **Honesty about the gap.** The Stage 11C path tracer is
  architecturally complete but uses an O(spheres + triangles)
  linear closest-hit walk. That's correct for the test scenes
  shipped today (handfuls of primitives) and prohibitively
  slow for any scene a real artist would author. Pretending
  otherwise would be the kind of "fake stub pretending to be a
  complete system" the master rules forbid.
- **Reduce migration surprise.** OptiX is a non-trivial
  dependency: it has its own program model (raygen / miss /
  hit groups), its own build flow (PTX compilation, embedded
  programs, pipeline + SBT setup), and its own toolchain
  requirements (NVIDIA-only, separate SDK, driver minimums).
  Writing the design down before the implementation slice
  starts means we don't discover any of these on the wrong
  side of a half-finished commit.

This is master order #17 ("OptiX upgrade path") in
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`. Stage
12A is the planning slice; Stage 12B is the minimum-viable
OptiX backend (parity with the current CUDA path tracer on a
single scene); Stage 12C+ is feature parity (multi-mesh,
instancing, multiple ray types, etc.).

**What this document deliberately does not contain (yet).**
The HOW of OptiX programming, accel structures, SBT, and the
data-flow integrations land in later sub-stages of 12A. This
sub-stage (12A.1) is the motivation paper: why the project
needs OptiX at all, why the current CUDA-only path is not
enough at scale, what stays unchanged when OptiX lands, and
what does not. The rest follows.

---

## 2. Why naive CUDA triangle loops are not enough

The project's current closest-hit logic is a textbook linear
scan. In `src/cuda/CudaPathTracer.cu::closest_hit` (Stage 11C)
and the older `src/cuda/CudaTestKernel.cu::k_render_scene`
(Stage 6B), every ray walks:

```
for sphere in scene.spheres:           // O(S)
    intersect_sphere(ray, sphere); update t_max if closer
for triangle in scene.mesh.triangles:  // O(T)
    intersect_triangle(ray, v0, v1, v2); update t_max if closer
```

This is fine for the tutorial-style fixtures shipped through
Stage 11 (`scenes/test_full_scene.rrscene` has 4 spheres +
2 triangles). It does not survive contact with a real scene.

### 2.1 Asymptotic cost

Per ray, the scan is `O(S + T)` intersection tests. The path
tracer launches one ray per pixel per sample, plus up to
`max_bounces - 1` continuation rays per pixel per sample for
those that hit. The per-frame total is bounded above by:

```
rays_per_frame  =  width × height × samples_per_pixel × max_bounces
tests_per_frame =  rays_per_frame × (S + T)
```

For the project's defaults (1280 × 720, 16 spp, 4 bounces) and
a moderately authored scene of 100k triangles (a single
character + a small room), the upper bound is approximately:

```
rays_per_frame  ≈ 1280 × 720 × 16 × 4   = 58.9 M rays
tests_per_frame ≈ 58.9 M × 100k         = 5.9 T intersection tests
```

Even at modern device intersection throughput (call it 10-100
G tests/sec, depending on the formulation and GPU), that's
**~60 to 600 seconds per frame** for a scene size that should
finish in tens of milliseconds with proper acceleration. At
1 M triangles (a single high-poly hero character), the same
math gives 60 T tests/frame — minutes to hours, not seconds.

### 2.2 Single-mesh slot

The geometry surface compounds the asymptotic problem.
`GpuScene::upload_mesh` currently holds **exactly one mesh**;
multi-mesh upload was deferred at Stage 10B.11. Authoring
fixtures with several meshes today renders only the first
visible non-empty one (the handler logs a warning when this
happens). Lifting that restriction by concatenating every
authored mesh's triangle list into a single buffer is an
antipattern: the closest-hit loop becomes one giant linear
scan with no spatial culling, no per-mesh material isolation
beyond an indirection into a flat material array, and no
opportunity for instancing.

In other words: even before we hit the asymptotic wall, the
data shape is already wrong for the next thing artists will
ask for ("can I have two characters in the same scene?").

### 2.3 Spatial coherence is wasted

The linear scan also ignores ray-coherence properties that
modern GPU traversal exploits:

- **Frame-coherence.** Adjacent pixels mostly hit the same
  primitives. A BVH lets adjacent threads converge to the
  same node early; the linear scan re-walks the entire array
  per thread.
- **Bounce-coherence.** A diffuse bounce at most reorients
  the ray; the new ray usually still hits primitives "near"
  the originating one. BVH locality preserves this; linear
  scan does not.
- **RT-core hardware.** Turing+ GPUs expose dedicated
  ray-triangle and ray-AABB intersection units that the
  linear-scan code path can't reach. The hardware sits idle
  while the kernel does fixed-function math the units were
  designed to accelerate.

### 2.4 What this means for the project

Stage 11C ships a *correct* path tracer that *does not scale*.
Every architectural choice in Stages 11A/B/C was made so that
the upgrade to a scalable backend changes the smallest
possible surface: `pathtracer::Rng`, `pathtracer::Sampling`,
`renderer::AccumulationBuffer` are all backend-agnostic; the
host orchestration in `PathTracer::render` only changes
*which launcher* it calls per sample. The piece that *must*
change to scale is the closest-hit walk + the intersection
primitives, and that piece is exactly what OptiX replaces.

---

## 3. Why OptiX matters for serious scenes

OptiX is NVIDIA's GPU ray-tracing programming model. From the
project's point of view it provides three things the naive
CUDA path can't:

### 3.1 BVH-accelerated traversal

OptiX's `optixTrace` takes a ray and an acceleration handle
and traverses a Bounding Volume Hierarchy in `O(log N)` per
ray, where `N` is the primitive count. The same 100k-triangle
scene from §2.1, redone:

```
tests_per_frame_optix ≈ rays_per_frame × log2(100k) × constant
                      ≈ 58.9 M × ~17 × ~few          = ~1-3 G
```

A roughly 1000x reduction in raw test count, and the per-test
work is also smaller because the BVH leaves are tight. In
production engines, well-built BVHs render the 100k-triangle
case in low double-digit milliseconds; the 1M case in
high-double-digit milliseconds. RelativityRender's
relativistic-perception aesthetic depends on being able to
move the camera through a populated scene in real-ish time;
the linear scan rules that out, the BVH path enables it.

### 3.2 RT-core hardware traversal

On Turing (RTX 20-series) and newer NVIDIA GPUs,
ray-triangle intersection is a dedicated hardware unit; OptiX
dispatches to it implicitly when the geometry AS is built
from triangles. The performance ceiling for triangle-heavy
scenes is set by a fixed-function pipeline rather than
by-shader math. This is the gap a CUDA-only renderer cannot
close — software cannot beat silicon at the operation the
silicon was designed for.

### 3.3 Programmable program model

OptiX exposes the ray-tracing loop as a small set of program
types (raygen, miss, closest-hit, any-hit, intersection,
exception) that the runtime calls back into. The model maps
cleanly onto the project's existing concerns:

- **Raygen** owns per-pixel state (camera ray gen, bounce
  loop, accumulation read-modify-write) — the place the
  project already wants its host orchestration to push down
  toward the device.
- **Miss** is exactly the "environment fallback" arm the
  Stage 11C path tracer already has.
- **Closest-hit** is exactly the "shade this surface" arm
  the path tracer already has, parameterised by the hit
  primitive.
- **Intersection** is needed only for primitive types OptiX
  doesn't intersect built-in (custom procedurals,
  pre-7.5 spheres). Triangles get the built-in path.
- **Any-hit** is where shadow-ray opacity lives — not needed
  for the minimum port (no NEE yet) but the slot is there
  for free.

This decomposition matches the path tracer's existing
hit-shade-bounce structure almost exactly. The migration is
substantively a rewrite of `closest_hit()` as a hit-group
program, plus surrounding plumbing.

### 3.4 Multi-ray-type SBT

OptiX's Shader Binding Table allows multiple "ray types" per
launch (typically: radiance, shadow, AO). When direct-light
sampling lands as a future stage, a shadow ray gets its own
miss + any-hit programs without touching the radiance path.
The CUDA-only renderer would need explicit conditional logic
in the closest-hit loop to switch behaviours; OptiX does it
by table dispatch.

### 3.5 Instancing without bespoke infrastructure

Instance Acceleration Structures (IAS) reference Geometry
Acceleration Structures (GAS) with per-instance transforms.
A scene with 100 trees can render from 1 GAS + 100 instance
records, total memory 1× tree geometry + 100× transform.
This is exactly the multi-mesh case the project deferred at
10B.11 — OptiX gives it for free as a consequence of having
an IAS at all. The CUDA path would need its own per-mesh
upload, per-mesh transform application during intersection,
and a top-level "which mesh?" loop; OptiX collapses all of
that into the BVH walk.

### 3.6 Production-quality ecosystem

OptiX is the dispatch layer for NVIDIA's denoiser (NRD), for
PBRT-aligned reference renderers (PBRTv4-OptiX, Falcor's RT
backend), and for most production-tier real-time RT research
prototypes. Targeting OptiX puts RelativityRender on the same
plumbing those tools assume; future work like denoising,
reference-image regression against PBRT, or DLSS-RR
integration becomes integration work, not infrastructure
work.

---

## 4. What remains CUDA-only for now

The OptiX migration is **not a rewrite of the project**. The
backend swap replaces a small, well-defined piece (the
closest-hit walk + intersection primitives + the per-frame
launch path) and leaves everything around it intact. This is
a deliberate design choice: the cost of the migration scales
with the migration's surface, and keeping the surface small
is the cheapest way to land OptiX without breaking what
already works.

The pieces below stay CUDA / host-side / unchanged across
Stages 12A, 12B, and 12C. They are listed here so future
sub-stage planning can refer back to a single canonical "what
moves" / "what stays" boundary.

### 4.1 Stays unchanged

- **Host scene parser + Scene data model** (`src/io/`,
  `src/scene/`). The `.rrscene` v1 parser, the `Scene`
  container, every per-section mapper from Stages 10B.2 -
  10B.8 — all of it is backend-agnostic. OptiX consumes the
  same `Scene` the CUDA path consumes.
- **`GpuScene` data uploads** (`src/gpu/GpuScene.{h,cpp}`).
  The host accessors (`device_spheres`, `device_materials`,
  `device_lights`, `mesh()`) are reused as the source of
  truth for OptiX's accel-structure builds and SBT
  population. We add an OptiX-specific consumer; we do not
  duplicate the upload.
- **Image / Framebuffer / PPM IO** (`src/image/`,
  `Image::save_ppm`). OptiX writes Rgba32F to the same host
  image format the CUDA path writes today. Save is identical.
- **GPU-safe RNG and sampling** (`src/pathtracer/RNG.{h,cuh}`,
  `src/pathtracer/Sampling.{h,cuh}`). PCG-XSH-RR-64-32 +
  cosine-weighted hemisphere sampling are RR_HD inline
  templates; they compile clean inside OptiX programs (which
  are `__global__`-shaped CUDA programs from the toolchain's
  point of view) without modification.
- **`AccumulationBuffer`** (`src/renderer/`). The Stage 11B
  buffer's contract is "an Rgba32F sum buffer the host
  accumulates samples into." OptiX raygen produces one
  sample's worth of radiance per pixel per launch, the same
  shape the CUDA `k_pathtrace_sample` does today; the
  accumulator takes either one without knowing which.
- **Diagnostic CUDA kernels.** The Stage 6-9 single-shot
  diagnostics (`--render-gradient` / `--render-rays` /
  `--render-sphere` / `--render-relativistic` / etc.) and the
  Stage 11A/B validation kernels (`--render-rng-test`,
  `--render-accumulation-test`) stay CUDA-native. These are
  small, scene-free, math-validation kernels; OptiX would
  add complexity without gain.
- **`--render-scene` / `--render-mesh-scene` /
  `--render-material-scene` / `--render-direct-lighting`
  reference paths.** These run the Stage 6B-9B closest-hit
  shader (single-bounce, no progressive accumulation) and
  serve as the project's regression baseline. They stay
  CUDA-native — useful both as a working-set control while
  OptiX matures and as the canonical "what should the image
  look like" reference. The OptiX backend's spp=∞ output
  must converge to these (with tone-mapping and emission
  accounted for).
- **Build system structure.** `CMakeLists.txt`'s library
  graph (`rr_pathtracer` INTERFACE, `rr_renderer` STATIC,
  `rr_gpu` STATIC, `rr_io`, `rr_scene`, etc.) is preserved.
  OptiX adds a new `rr_optix` STATIC library alongside
  `rr_gpu`; nothing in the existing graph moves.

### 4.2 Migration boundary

The OptiX swap replaces precisely:

- **`src/cuda/CudaPathTracer.cu`'s `closest_hit` walk +
  intersection primitives.** Replaced by an OptiX hit-group
  + intersection program set with a BVH-backed traversal.
- **`src/cuda/CudaPathTracer.cu`'s `k_pathtrace_sample`
  kernel structure.** The bounce loop moves into an OptiX
  raygen program; per-bounce trace calls become
  `optixTrace` invocations. The math (RNG step, hemisphere
  sample, throughput update, emission / environment
  contribution) is identical.
- **The launcher.** `launch_pathtrace_sample` is replaced by
  an OptiX `optixLaunch` of the path-tracer pipeline, with
  per-frame launch parameters carrying camera / observer /
  AS handle / output buffer / sample index / RNG seed.

Everything else around those three items stays. The
PathTracer host class signature does not change (the CUDA
launcher is replaced behind it); the AccumulationBuffer is
called identically; the CLI handler and its `--render-pathtrace`
surface stay intact (with possibly an additional flag to
select backend during the migration period — this is a 12B
decision).

### 4.3 Why keep the CUDA path tracer at all

Once OptiX is the production renderer for non-trivial scenes,
the CUDA path tracer remains valuable as:

- A **regression baseline**: any image produced by OptiX must
  match (within Monte-Carlo noise) the same scene rendered
  by the CUDA path tracer at high spp. If the two diverge,
  one of them has a bug.
- A **fallback** for environments without OptiX (older GPUs,
  driver issues, or non-NVIDIA hardware in a future
  cross-vendor port).
- A **testbed** for new sampling / shading code: the CUDA
  kernel is faster to iterate on than the OptiX pipeline (no
  PTX compile, no pipeline rebuild). New BSDFs can be
  prototyped in the CUDA path before being copied over to an
  OptiX hit-group program.

The codebase's CUDA-only kernel set becomes the project's
*correctness reference*. OptiX becomes the project's
*performance backend*. Both have a place.

---

## Sections to come

Future Stage 12A sub-stages will append (one per slice or
small group of slices):

- Raygen / Miss / Closest-hit / Intersection program design
- Acceleration structures (GAS, IAS, build flags, refit vs
  rebuild)
- Shader Binding Table layout (records per ray type, per
  geometry type, per material)
- Material data flow (per-record vs constant-memory vs
  launch-param)
- Camera data flow
- Relativity integration (where aberration / Doppler /
  searchlight live across the program model)
- Path-tracing integration (iterative bounce loop in raygen,
  payload layout, RNG state threading)
- Planned module / file layout under `src/optix/` + CMake
  changes
- Migration risks (toolchain, debug story, build-host
  requirements, code duplication during transition)

Stage 12B is the first slice that ships OptiX **code**. Until
then the project's renderer is exactly what it is today — a
correct CUDA path tracer that wants a faster traversal layer.

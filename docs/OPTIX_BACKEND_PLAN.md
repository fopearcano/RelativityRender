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

## 5. Raygen program

The OptiX **raygen** program is the per-pixel entry point of an
`optixLaunch`. One thread is dispatched per launch index; for
RelativityRender's 2D framebuffer that maps to one thread per
pixel, exactly mirroring the Stage 11C CUDA path-tracer kernel
(`k_pathtrace_sample`) — the OptiX migration replaces the
`<<<grid, block>>>` launch with `optixLaunch`, but the unit of
parallelism does not change.

### 5.1 Role

The raygen program owns three responsibilities on the GPU:

1. **Generate the primary ray** for its launch index `(x, y)`,
   with sub-pixel jitter sampled from the per-pixel
   `pathtracer::Rng`. This is the same maths as
   `generate_primary_ray` in `CudaPathTracer.cu` (mirrors
   `rr::camera::generate_camera_ray` with the +0.5 centre
   offset replaced by the random jitter so the spp loop
   produces stratified-by-default anti-aliasing).

2. **Drive the iterative bounce loop**: for each bounce in
   `[0, max_bounces)`, call `optixTrace` against the scene
   acceleration structure, read the populated payload, fold
   the hit's contribution into the running radiance + update
   the throughput, sample the next bounce direction with
   `pathtracer::sample_cosine_hemisphere`, and continue. On
   miss, fold in the environment fallback and break. This
   logic is host-of-shading: the closest-hit program (§7,
   future sub-stage) only writes hit data into the payload,
   leaving every shading decision to the raygen.

3. **Write per-sample radiance** to the per-launch output
   buffer at the pixel's offset. One sample per launch — the
   spp loop stays on the host (one `optixLaunch` per sample,
   accumulated through the existing Stage 11B
   `AccumulationBuffer`). This is identical to the Stage 11C
   CUDA orchestration shape; the migration changes the launch
   primitive, not the spp partitioning.

The bounce loop is **iterative**. OptiX 7+ supports recursive
trace calls via continuation-call stacks, but production path
tracers iterate to bound stack growth on wide divergence. The
raygen carries `radiance` and `throughput` as thread-local
floats across the loop iterations; only the per-bounce *hit
data* travels through OptiX's payload registers.

### 5.2 Inputs

The raygen program reads two distinct input surfaces:

#### 5.2.1 Constant-memory `optixLaunchParams`

A single, statically-sized POD bound to a fixed device symbol
at pipeline link time, populated by the host before each
`optixLaunch`. RelativityRender's planned struct (subject to
refinement when Stage 12B ships):

```cpp
struct OptixLaunchParams {
    // Output (write-only from the raygen)
    float*                              sample_pixels;    // device pointer, w*h*4 floats
    int                                 width;
    int                                 height;

    // Per-launch sample / RNG state
    unsigned int                        seed;
    unsigned int                        sample_index;     // changes between launches
    int                                 max_bounces;

    // Scene
    OptixTraversableHandle              scene_handle;     // GAS / IAS root for optixTrace
    const rr::material::MaterialParams* materials;        // device pointer
    int                                 material_count;

    // Camera (host POD; identical to GpuCamera)
    rr::camera::GpuCamera               camera;

    // Relativity (host POD; consumed by raygen for primary-ray aberration)
    rr::relativity::Observer            observer;
    rr::relativity::RelativityParams    params;

    // Environment fallback for misses (raygen consumes; the miss
    // program just signals "no hit" via the payload)
    rr::math::Vec3                      env_color;
    float                               env_intensity;
};
```

The struct is 1:1 with the data the Stage 11C CUDA kernel
already takes by value as launch arguments — the migration is
moving fields from kernel arguments to a constant-memory POD,
not adding new state.

#### 5.2.2 SBT raygen record

Every OptiX program is reached through a Shader Binding Table
record. The raygen record's slot is a single SBT entry, of the
form `[OptixProgramGroup header (32 B)] + [user-data
payload]`. For Stage 12B the user-data payload is **empty**:
the raygen reads everything it needs from
`optixLaunchParams`, so the record carries only the program-
group identifier. This keeps the raygen launch dirt-cheap (no
per-launch SBT rebuilds for the most common state changes).
Future expansion can put per-launch overrides in the user-data
slot — denoiser blend factors, debug-overlay flags, etc. —
without touching the program code.

The SBT layout in full lands in §9 (a future sub-stage). For
this section: the raygen record exists, has zero user data,
and is built once per pipeline.

### 5.3 Outputs

The raygen program produces two output streams across two
distinct lifetimes:

#### 5.3.1 Per-bounce ray payload (transient)

The OptiX **ray payload** is the per-`optixTrace`-call slot of
typed registers (up to 32 32-bit registers in OptiX 7.6+) that
the raygen, miss, and closest-hit programs use as their shared
per-ray scratchpad. The raygen sets initial payload values
*before* each `optixTrace` (typically zero / sentinel; the
miss / CH programs overwrite the relevant slots) and reads the
payload *after* the call returns to decide what to do next.

The payload is **not** the radiance/throughput accumulators —
those are thread-local floats living in the raygen's stack
frame across the bounce loop iterations. The payload's job is
exclusively to carry hit data (closest-hit `t`, world position
+ normal, material index) or the miss flag back to the raygen.
This separation keeps the radiance accumulation logic in one
place (raygen) and the hit-data marshalling local to its
producer (CH / miss).

The exact register assignment is a §7 / §6 concern (closest-
hit and miss define the payload contract); the raygen consumes
whatever they produce.

#### 5.3.2 Per-sample output buffer (persistent)

After the bounce loop exits, the raygen writes the per-sample
radiance estimate to the output sample buffer:

```cpp
const int idx = (y * params.width + x) * 4;
params.sample_pixels[idx + 0] = radiance.x;
params.sample_pixels[idx + 1] = radiance.y;
params.sample_pixels[idx + 2] = radiance.z;
params.sample_pixels[idx + 3] = 1.0f;
```

The host then folds this buffer into the Stage 11B
`AccumulationBuffer` via `accumulate_sample(...)`, exactly as
Stage 11C does for the CUDA path tracer. After
`samples_per_pixel` such launches the host calls
`resolve_to_image` and saves the PPM. **Nothing about the
accumulation chain changes for the OptiX migration** — the
raygen produces the same Rgba32F sample-buffer format the CUDA
kernel already produces.

### 5.4 Read / write summary

| Surface                              | Direction      | Lifetime                      |
|--------------------------------------|----------------|-------------------------------|
| `optixLaunchParams`                  | read           | constant per launch           |
| SBT raygen record header             | read (implicit)| constant per pipeline         |
| SBT raygen record user-data          | (none in 12B)  | n/a                           |
| OptiX ray payload (per `optixTrace`) | write before / read after each trace | per-bounce |
| `pathtracer::Rng` thread-local state | read/write     | per-pixel, per-launch         |
| `radiance`, `throughput` floats      | read/write     | per-pixel, per-launch         |
| `params.sample_pixels` (output)      | write (once at end) | persistent across launches |

The raygen does not write to `optixLaunchParams` (the host
owns that). It does not write to the SBT (the host builds the
SBT once per pipeline). The only persistent device-memory
write is to the sample buffer, and it happens once per
thread, at the end of the bounce loop.

### 5.5 All per-pixel work stays on the GPU

The raygen program enforces RelativityRender's project-wide
rule that no per-pixel / per-ray work runs on the CPU. Concretely:

- The host orchestration (`PathTracer::render` will grow an
  OptiX path; see §11 file-layout, future sub-stage) only:
  allocates buffers (`AccumulationBuffer`, sample
  `GpuBuffer<float>`), populates `optixLaunchParams`, calls
  `optixLaunch` once per sample, and drives `accumulate_sample`
  + `resolve_to_image`. No per-pixel host loop, no host
  intersection code, no host shading.
- The raygen program owns every step from "I am pixel `(x, y)`,
  sample `s`" through "here is my radiance estimate" — primary
  ray gen, RNG seed, jitter, the entire bounce loop, the
  cosine-hemisphere sampling, the throughput math, the
  emission accumulation, the environment-fallback evaluation,
  and the output write.
- The closest-hit and miss programs (§6, §7 — future sub-stages)
  fill in *their* slices of the per-ray work also on the GPU.

This keeps the migration honest against the Stage 11 audit's
items 6 and 8 (ray paths fully GPU-side; no CPU ray tracing or
sample accumulation): once OptiX ships, the same audit
questions still answer PASS, just against a different
device-side implementation. The CPU's role does not change
between the CUDA and OptiX backends — both backends fit
within the master rule that the CPU may only "orchestrate
execution / parse-load scenes / manage IO / upload to GPU /
launch CUDA/OptiX kernels / receive framebuffers / save image
files".

---

## 6. Miss program

The OptiX **miss** program is the per-ray sink for `optixTrace`
calls that find no intersection in `[t_min, t_max]`. Where the
raygen owns the primary-ray and bounce-loop logic and the
closest-hit owns the surface-data unpack, the miss program owns
the **environment evaluation** — including the relativistic
modulation that makes RelativityRender different from a textbook
diffuse path tracer.

### 6.1 Role

A miss program is invoked once per `optixTrace` call that
terminates without a closest-hit. Its output is the radiance
arriving from infinity along the ray's direction — what the path
tracer treats as the "sky / environment" contribution.

For RelativityRender's Stage 12B target, one miss program is
bound to the radiance ray type. It runs once per missed ray and
writes a single RGB radiance value into the OptiX payload. The
raygen, on returning from `optixTrace`, reads that radiance and
folds it into the running radiance accumulator (multiplied by
the current throughput, then bounce-loop break).

This refines the coarse §5.1 sketch where the raygen carried
the entire environment evaluation. Moving the env evaluation
into the miss program is preferred for three reasons:

1. **Locality.** The miss program is the canonical site for
   "what happened at the end of this ray". Computing the env
   contribution here keeps the closest-hit / miss split
   symmetric: each writes its own radiance answer; raygen
   integrates.
2. **Relativistic modulation.** The Doppler factor depends on
   the ray's direction relative to the observer's velocity
   (§6.4). The ray direction is already in the miss program's
   scope via `optixGetWorldRayDirection()`; folding Doppler in
   here avoids re-deriving it raygen-side and avoids a payload
   slot for "the ray direction at miss".
3. **Future ray types.** Stage 12C+ adds shadow rays (NEE
   visibility queries) which need a *different* miss handler —
   a "no occluder" radiance value rather than a sky term. SBT
   already supports per-ray-type miss programs; designing the
   radiance miss as the env evaluator makes the second miss
   program a natural sibling rather than a refactor target.

### 6.2 Inputs

The miss program reads three input surfaces:

#### 6.2.1 Built-in OptiX state

Available via OptiX intrinsics inside any program:

- `optixGetWorldRayDirection()` — the world-space ray
  direction the trace was launched with. Already aberrated
  for the primary ray (raygen runs `aberrateDirection` against
  the observer's frame before the first `optixTrace`); already
  *un*-aberrated for bounce rays (those are world-frame). The
  miss program cannot tell primary from bounce, which is fine —
  see §6.4 for the design choice this implies.
- `optixGetWorldRayOrigin()` — not used by Stage 12B's miss
  (env radiance is direction-only), but available for future
  parametric env maps.

#### 6.2.2 Constant-memory `optixLaunchParams`

Same single global struct §5.2.1 sketches. The miss program
reads:

- `env_color` (Vec3) — base environment radiance.
- `env_intensity` (float) — scalar multiplier on `env_color`.
- `observer` (`rr::relativity::Observer`) — its `velocity`
  field is the 3-velocity in c-units used by Doppler /
  searchlight.
- `params` (`rr::relativity::RelativityParams`) — the
  artist-facing toggles and strengths (`enable_doppler`,
  `enable_searchlight`, `doppler_color_strength`,
  `searchlight_strength`, `max_beta`).

The miss program does **not** read the AS handle, the materials
array, or the framebuffer pointer. It is a pure direction →
radiance evaluator.

#### 6.2.3 SBT miss record

For Stage 12B the radiance-miss SBT record carries the program
identifier and **no user data**. Future expansion can put
per-launch env overrides (HDR env-map textures, multi-band sky
parameters) in the user-data slot without touching the program;
for the bounded sky-tint Stage 12B targets, launch params are
sufficient and SBT updates are saved for genuine per-record
state.

### 6.3 Outputs

The miss program's output is a single 3-channel radiance value
written to the OptiX payload. Per §5.3.1, the payload's job is
to carry per-trace data back to the raygen; the miss program
overwrites the slots the raygen reserves for radiance:

```cpp
// Miss program (sketch). Register layout to be finalised in
// 12A.2.3 (closest-hit) so radiance / hit-data slots don't
// collide; what matters here is that the miss writes RGB
// radiance + clears the hit flag.
optixSetPayload_<HIT>(0u);             // hit flag = false
optixSetPayload_<R>(__float_as_uint(env.x));
optixSetPayload_<G>(__float_as_uint(env.y));
optixSetPayload_<B>(__float_as_uint(env.z));
```

The miss program does **not** write to the framebuffer, the
accumulation buffer, or any other persistent device-memory
surface. Its scope ends at the payload boundary; raygen owns
every per-pixel persistent write (§5.3.2).

### 6.4 Doppler / searchlight interaction

The miss program is the natural site for relativistic
modulation of the environment. Two RR_HD inline helpers from
`relativity/RelativityMath.h` apply directly without
modification — the headers were authored to be device-callable,
which §3.1 / §11A audited:

- `dopplerFactor(observer.velocity, ray_direction)` returns the
  scalar Doppler shift `D` along the ray. `D > 1` for
  approaching rays (blueshift), `D < 1` for receding rays
  (redshift).
- `applyDopplerColor(color, D, doppler_color_strength)` shifts
  the input radiance's hue using the perceptual approximation
  the Stage 9 kernels already validated.
- `searchlightFactor(D, searchlight_strength)` returns the
  brightness multiplier from relativistic beaming (the
  intensity boost on approaching rays / dim on receding ones).

The miss program composes them in the canonical order — colour
shift first, then intensity scale, both gated on the relevant
`params.enable_*` toggle:

```cpp
Vec3 env = launch_params.env_color * launch_params.env_intensity;

const Vec3& v   = launch_params.observer.velocity;
const Vec3  dir = optixGetWorldRayDirection();

if (launch_params.params.enable_doppler ||
    launch_params.params.enable_searchlight) {
    const float D =
        rr::relativity::dopplerFactor(v, dir);

    if (launch_params.params.enable_doppler) {
        env = rr::relativity::applyDopplerColor(
            env, D, launch_params.params.doppler_color_strength);
    }
    if (launch_params.params.enable_searchlight) {
        env = env * rr::relativity::searchlightFactor(
            D, launch_params.params.searchlight_strength);
    }
}
```

### 6.4.1 Primary vs bounce rays — a deliberate choice

A subtle physics point surfaces here. Doppler / searchlight is
the transformation that takes the world-frame intensity field
into the observer's-frame received radiance. It applies
strictly to *the rays the observer sees* — i.e., the **primary
ray** in a path tracer's terms. Subsequent bounce rays are
geometric photon-walks inside the world frame; they don't
re-enter the observer's frame, so applying Doppler again on
their misses has no physical justification.

But OptiX miss programs don't have the bounce index — the miss
is invoked the same way for primary and bounce rays. Three
options:

1. **Apply Doppler / searchlight on every miss** (Stage 12B's
   choice). Simplest implementation, matches the "all rays
   are observer-frame" aesthetic the Stage 6-9 single-shot
   kernels already use, accepts the small physical
   inaccuracy that bounce-ray misses also get modulated.
2. **Pass an `is_primary` payload bit** the raygen sets to 1
   on the first trace and 0 on subsequent bounces. The miss
   program reads it and gates the Doppler block. Costs one
   payload register; trivial bookkeeping in raygen.
3. **Two miss programs**, one per ray type — a "primary-
   radiance" miss that applies Doppler, and a "bounce-
   radiance" miss that doesn't. Costs an extra ray-type slot
   in the SBT layout (§9, future sub-stage); also costs a
   raygen change to use different ray types per bounce
   index, which is unusual and complicates the bounce loop.

Stage 12B picks option 1 for the same reason `k_render_scene`
already applies Doppler to its primary-only ray:
**RelativityRender is an artistic / perceptual renderer first,
a physics simulator second**. The relativistic perception
*model* is consistent across the existing kernels, and bounce-
ray contributions are dim relative to the primary in the kinds
of scenes the project targets (the Stage 11C path tracer's
Lambert throughput attenuates by `albedo` per bounce, so by
bounce 3 the throughput is typically `albedo^3 ≈ 0.1`).

If a future scene reveals visible artifacts from the bounce-
miss Doppler, option 2 (`is_primary` payload bit) is the
upgrade path: one register, one branch, no SBT changes.
Option 3 stays available if a more thorough "world-frame
bounces" model is ever needed; it lands when (or if) the path
tracer grows a "physically accurate" mode alongside the
artistic default.

### 6.5 Read / write summary

| Surface                              | Direction   | Lifetime               |
|--------------------------------------|-------------|------------------------|
| `optixGetWorldRayDirection()`        | read        | per `optixTrace`       |
| `optixLaunchParams.env_*`            | read        | constant per launch    |
| `optixLaunchParams.observer`         | read        | constant per launch    |
| `optixLaunchParams.params`           | read        | constant per launch    |
| SBT miss-record header               | read (implicit) | constant per pipeline |
| SBT miss-record user-data            | (none in 12B) | n/a                  |
| OptiX ray payload (radiance slots)   | write       | per `optixTrace`       |
| OptiX ray payload (hit-flag slot)    | write (= 0) | per `optixTrace`       |

The miss program writes nothing to launch params, the SBT,
the framebuffer, the accumulation buffer, or the materials /
spheres / mesh arrays. It is a pure function `(direction,
launch_params) -> radiance`, encoded into payload registers
the raygen reads back.

### 6.6 Scope: what's NOT in §6

Two adjacent concerns deserve forward-pointers:

- **Shadow-ray miss program** — for NEE in Stage 12C+. A
  separate miss program writes "no occluder" into a
  visibility-flag payload register, the raygen reads it to
  decide whether the sampled light contributes. Miss-program
  layout supports it via the per-ray-type SBT records covered
  in §9 (future sub-stage); §6 is radiance-only.
- **HDR env-maps / parametric skies** — not in 12B. The
  current bounded `env_color * env_intensity` model is enough
  to demonstrate the path tracer; HDR env-maps land alongside
  the texture system (master order #18) and need an extra
  device pointer in launch params + a directional sample
  decoded against the env map. The miss program's shape — pure
  direction → radiance — extends to that case without
  refactor.

---

## 7. Closest-hit program

The OptiX **closest-hit** program runs once per `optixTrace`
call that resolves to a finalised intersection in
`[t_min, t_max]`. It is the per-bounce shading site:
material evaluation, emission Doppler modulation, hit-normal
extraction, and the data hand-off the raygen needs to
construct the next bounce ray.

§7 supersedes the earlier "thin CH, fat raygen" sketch from
§5 and §3.x. The earlier design had the CH writing only hit
geometry into the payload (t, position, normal,
material_index) and the raygen reading those back to do the
shading. The current design moves *per-hit shading* into the
CH while keeping *trace-loop control + RNG advance + ray
construction* in the raygen — a "fat CH for shading, fat
raygen for integration" hybrid that is closer to canonical
OptiX path-tracer designs and that lets the relativistic
modulation of emission live next to the analogous miss-time
modulation in §6.4.

### 7.1 Role

For each `optixTrace` call that ends in a hit, the CH
program owns five pieces of work:

1. **Hit-data extraction.** Build the world-space hit
   position from `optixGetWorldRayOrigin()` +
   `optixGetWorldRayDirection() * optixGetRayTmax()`. Build
   the world-space hit normal from the per-primitive recipe
   (sphere: `(P - C) / r`; triangle: barycentric blend of
   per-vertex normals — see §7.6).
2. **Material lookup.** Read the
   `rr::material::MaterialParams` record by index. The index
   comes from the primitive's metadata (sphere
   `material_index` for sphere hits; mesh `material_id` for
   triangle hits — see §7.6).
3. **Emission evaluation.** Compute `emission =
   m.emissionColor * m.emissionStrength`. Stage 12B is
   diffuse-only; that single line is the entire emission
   model.
4. **Relativistic modulation.** Apply Doppler colour shift
   and searchlight beaming to `emission` using the same
   RR_HD helpers §6.4 calls — `dopplerFactor`,
   `applyDopplerColor`, `searchlightFactor`. Same canonical
   ordering, same `params.enable_*` gating, same toggle
   strengths. The hit-time modulation mirrors the miss-time
   modulation so the relativistic-perception aesthetic is
   consistent across radiance sources.
5. **Payload write-back.** Write the per-hit shading result
   to the OptiX payload: post-modulation emission (3
   floats), surface albedo (3 floats; the diffuse base
   colour the raygen uses to update throughput), hit normal
   (3 floats; the raygen orients the cos-hemisphere sample
   against this), hit position (3 floats; the raygen uses
   it as the next ray's origin), and the hit flag (1 word;
   set to 1 to distinguish from miss).

The CH program does **not**:

- advance the RNG (raygen owns RNG state across bounces);
- sample the next bounce direction (raygen samples
  `pathtracer::sample_cosine_hemisphere`, which advances
  the RNG, after the trace returns);
- construct the next ray (raygen builds it from the hit
  data the CH wrote);
- multiply throughput by albedo (raygen does the throughput
  update — keeps the throughput accumulator in one place);
- accumulate radiance (raygen does `radiance += throughput
  * emission_from_payload` after the trace returns).

This split puts shading-state at the CH (per-hit, no
cross-bounce dependencies) and integration-state at the
raygen (per-thread, persists across the bounce loop). The
RNG state never leaves the raygen's stack, which avoids
having to carry it through OptiX payload registers.

### 7.2 Inputs

The CH program reads three input surfaces: built-in OptiX
state, launch params, and (for primitive-specific recipes)
the SBT CH record.

#### 7.2.1 Built-in OptiX state

Available via OptiX intrinsics inside any CH program:

- `optixGetWorldRayOrigin()` — ray origin at trace time.
- `optixGetWorldRayDirection()` — ray direction at trace
  time. Used for hit-position reconstruction *and* for the
  Doppler factor (§7.5).
- `optixGetRayTmax()` — the finalised closest-hit `t`. The
  CH treats this as the hit's `t` because OptiX guarantees
  it; manually re-deriving via `dot(P - origin, direction)`
  costs maths and accumulates floating-point error.
- `optixGetPrimitiveIndex()` — index of the hit primitive
  within its GAS. Sphere CH uses it to index the sphere
  array; triangle CH uses it to index the mesh's triangle
  array.
- `optixGetInstanceId()` — populated when an IAS sits above
  a GAS (Stage 12C+ multi-mesh). For Stage 12B's single-GAS
  layout this returns 0 and is not consulted.
- `optixGetTriangleBarycentrics()` — the barycentric
  `(u, v)` of the hit relative to the triangle's
  `(v0, v1, v2)` vertex order. Triangle CH only.
- `optixGetSphereData()` — the sphere centre + radius
  (OptiX 7.5+ built-in sphere primitive only). Available
  to sphere CH when the GAS uses
  `OPTIX_PRIMITIVE_TYPE_SPHERE`.

#### 7.2.2 Constant-memory `optixLaunchParams`

The CH reads:

- `materials` (device pointer to `MaterialParams[]`) +
  `material_count` — for the material index → params
  lookup.
- `observer` (`Observer`) and `params`
  (`RelativityParams`) — for the Doppler / searchlight
  evaluation in §7.5. The same fields the miss program
  consumes in §6.2.2.
- `mesh.{vertices, triangles, ...}` (or sphere array
  pointer) — for the per-primitive geometry recipe in
  §7.6. Stage 12B routes per-primitive metadata through
  launch params rather than per-record SBT data; §9
  (future sub-stage) covers the SBT record layout that
  will let this move out of launch params for production
  scenes.

The CH does **not** read:

- `env_color` / `env_intensity` — that is the miss
  program's domain.
- `sample_pixels` — only the raygen writes the per-sample
  output buffer; the CH never touches the framebuffer.
- `seed` / `sample_index` — the RNG state and its
  per-pixel seeding are raygen-local.

#### 7.2.3 SBT CH record

Stage 12B's CH record carries the program identifier and
**no user-data payload**. All per-record metadata — sphere
arrays, mesh vertex/triangle pointers, materials —
currently lives in launch params (see §7.2.2 rationale).

The SBT user-data slot is reserved for §9's future
treatment, when per-mesh records (one CH record per mesh in
a multi-mesh scene) become the natural way to thread
per-mesh transforms + material IDs through the SBT without
a launch-params resize on every scene edit.

### 7.3 Outputs — the OptiX payload register layout

§5.3.1 deferred this to §7. Stage 12B's payload uses 13 of
OptiX 7.6+'s 32 32-bit registers:

| Slot       | Reg | Type   | Filled by | Read by |
|------------|----:|--------|-----------|---------|
| `hit_flag` |   0 | u32    | CH (=1)/miss (=0) | raygen  |
| `pos.x`    |   1 | f32    | CH        | raygen  |
| `pos.y`    |   2 | f32    | CH        | raygen  |
| `pos.z`    |   3 | f32    | CH        | raygen  |
| `nrm.x`    |   4 | f32    | CH        | raygen  |
| `nrm.y`    |   5 | f32    | CH        | raygen  |
| `nrm.z`    |   6 | f32    | CH        | raygen  |
| `emit.r`   |   7 | f32    | CH / miss | raygen  |
| `emit.g`   |   8 | f32    | CH / miss | raygen  |
| `emit.b`   |   9 | f32    | CH / miss | raygen  |
| `albedo.r` |  10 | f32    | CH        | raygen  |
| `albedo.g` |  11 | f32    | CH        | raygen  |
| `albedo.b` |  12 | f32    | CH        | raygen  |

The miss program writes the `emit.*` slots (with the
Doppler-modulated env radiance) and clears `hit_flag`; it
leaves `pos.*` / `nrm.*` / `albedo.*` undefined (the raygen
breaks the bounce loop on miss without consulting them).
The CH program writes every slot.

The `hit_flag` register is intentionally first so the
raygen can decide miss vs hit with a single load before
fetching the rest. With 32 registers available and 13
used, future expansion (UVs for textures, transmission
coefficients, BRDF type discriminator, MIS PDFs) can grow
the payload without a layout overhaul.

Notes on encoding:

- All floats are written via `__float_as_uint` into the
  payload's u32 slots, read back via `__uint_as_float`.
  This is the standard OptiX pattern; the bit cast is free.
- The payload is **per-trace**, not per-pixel. Each
  `optixTrace` call has its own payload lifetime; the
  raygen pulls values out and uses them, then the next
  trace overwrites everything.

### 7.4 Material evaluation

Stage 12B reads only two `MaterialParams` fields, matching
the Stage 11C CUDA path tracer's "diffuse only" posture:

- `baseColor` (Vec3) — the diffuse albedo. Written
  unmodified to the payload's `albedo.*` slots; raygen
  multiplies throughput by it on the next bounce.
- `emissionColor` (Vec3) and `emissionStrength` (float) —
  combined as `emission = emissionColor * emissionStrength`.
  Modulated for relativity in §7.5, then written to the
  payload's `emit.*` slots.

Out of scope for §7:

- `roughness`, `metallic`, `specular`, `transmission` are
  uploaded by the parser (Stage 10B.5) and read by the host
  side, but the CH program ignores them. A real BSDF
  dispatch is a follow-up master-order module (#13 material
  expansion); the CH grows a small switch then.
- Texture sampling (`base_color_texture_id` etc., reserved
  in `RRSCENE_FORMAT.md` §14) needs the texture system
  (master order #18) to land first.

The default material for an out-of-range index is the same
neutral grey diffuse `MaterialParams{}` returns — Stage 11C
established this convention; the CH preserves it.

### 7.5 Relativistic modifiers

The hit-site Doppler / searchlight modulation mirrors §6.4
exactly. Same RR_HD helpers from
`relativity/RelativityMath.h`, same canonical order
(colour shift first, then intensity scale), same
`params.enable_*` gating:

```cpp
const Vec3& v   = launch_params.observer.velocity;
const Vec3  dir = optixGetWorldRayDirection();

Vec3 emission = m.emissionColor * m.emissionStrength;

if (launch_params.params.enable_doppler ||
    launch_params.params.enable_searchlight) {
    const float D = rr::relativity::dopplerFactor(v, dir);
    if (launch_params.params.enable_doppler) {
        emission = rr::relativity::applyDopplerColor(
            emission, D, launch_params.params.doppler_color_strength);
    }
    if (launch_params.params.enable_searchlight) {
        emission = emission * rr::relativity::searchlightFactor(
            D, launch_params.params.searchlight_strength);
    }
}
```

Two design notes that distinguish hit-time from miss-time
modulation:

1. **Direction reuse.** Both §6.4 and §7.5 evaluate
   `dopplerFactor(v, ray_dir)` against the same launch
   params. The factor depends on `dot(v, dir)` so it is
   identical at all hits along a single primary ray's
   bounce chain — same `v` (per launch), and `dir` only
   changes between bounces. There is no opportunity to
   share the factor across bounces because the ray
   direction changes; each bounce starts a fresh trace
   with a new `dir`.

2. **Albedo is *not* modulated.** Doppler shifts only the
   *emitted* radiance, not the surface's reflective
   response. `albedo` is a property of the material in the
   world frame, not a radiance carrier. The raygen's
   `throughput *= albedo` step on the next bounce
   propagates the unmodified albedo; the cumulative effect
   on the bounce-chain colour is right because every
   subsequent emission / env evaluation goes through its
   own per-bounce Doppler modulation.

This matches the Stage 6-9 single-shot kernel posture
where Doppler is applied to direct lighting + emission,
not to the base colour. The path-tracer integration
inherits the same physics interpretation.

### 7.6 Per-primitive-type closest-hit

Stage 12B's HitGroup table has one record per primitive
type — a sphere CH and a triangle CH — both bound to the
radiance ray type. The two share §7.1-§7.5 verbatim and
differ only in §7.1 step 1 (hit-data extraction):

#### 7.6.1 Sphere CH

```cpp
const float t = optixGetRayTmax();
const Vec3 ray_o = optixGetWorldRayOrigin();
const Vec3 ray_d = optixGetWorldRayDirection();
const Vec3 P = ray_o + ray_d * t;

// Geometry: sphere index + per-sphere POD.
const unsigned int idx = optixGetPrimitiveIndex();
// Stage 12B: launch_params.spheres is the device pointer
// uploaded by GpuScene::upload_spheres. Future SBT-data
// layouts (§9) move this pointer into per-record data.
const Sphere s = launch_params.spheres[idx];

const Vec3 N = (P - s.center) * (1.0f / s.radius);
const int  material_index = s.material_index;
```

When OptiX 7.5+'s built-in sphere primitive
(`OPTIX_PRIMITIVE_TYPE_SPHERE`) is used, the same data is
available via `optixGetSphereData()` without the
launch-params load. The choice is a §10 (acceleration
structures, future sub-stage) concern; the CH's read site
is otherwise unchanged.

#### 7.6.2 Triangle CH

```cpp
const float t = optixGetRayTmax();
const Vec3  P = optixGetWorldRayOrigin()
              + optixGetWorldRayDirection() * t;

const unsigned int prim = optixGetPrimitiveIndex();
const float2 bc = optixGetTriangleBarycentrics();
// 1 - u - v is implicit; u maps to v1, v maps to v2.
const float w0 = 1.0f - bc.x - bc.y;
const float w1 = bc.x;
const float w2 = bc.y;

// Mesh metadata via launch params for Stage 12B.
const auto& mesh = launch_params.mesh;
const auto& tri  = mesh.triangles[prim];
const Vec3 n0 = mesh.vertices[tri.v0].normal;
const Vec3 n1 = mesh.vertices[tri.v1].normal;
const Vec3 n2 = mesh.vertices[tri.v2].normal;
const Vec3 N  = normalize(n0 * w0 + n1 * w1 + n2 * w2);
const int  material_index = mesh.material_id;
```

The mesh's `material_id` overrides any per-vertex
material index; this matches Stage 9B's
`k_render_scene` behaviour where mesh hits rewrite
`Hit::material_index` to `mesh.material_id` on the
closest-hit candidate. UV / per-vertex colour / tangent
interpolation joins the same recipe when the texture
stage lands; the per-vertex `Vertex` POD already carries
`uv` (Stage 7A).

### 7.7 Read / write summary

| Surface                              | Direction       | Lifetime               |
|--------------------------------------|-----------------|------------------------|
| `optixGetWorldRayOrigin()`           | read            | per `optixTrace`       |
| `optixGetWorldRayDirection()`        | read            | per `optixTrace`       |
| `optixGetRayTmax()`                  | read            | per `optixTrace`       |
| `optixGetPrimitiveIndex()`           | read            | per `optixTrace`       |
| `optixGetTriangleBarycentrics()`     | read (tri only) | per `optixTrace`       |
| `optixLaunchParams.materials`        | read            | constant per launch    |
| `optixLaunchParams.spheres` /        | read            | constant per launch    |
| `optixLaunchParams.mesh.{vertices,   |                 |                        |
|  triangles, material_id}`            |                 |                        |
| `optixLaunchParams.observer`         | read            | constant per launch    |
| `optixLaunchParams.params`           | read            | constant per launch    |
| SBT CH-record header                 | read (implicit) | constant per pipeline  |
| SBT CH-record user-data              | (none in 12B)   | n/a                    |
| OptiX ray payload (all slots)        | write           | per `optixTrace`       |

The CH writes nothing to launch params, the SBT, the
framebuffer, the accumulation buffer, or any of the
materials / spheres / mesh arrays. It is a pure function
`(hit context, launch_params) -> payload`.

### 7.8 Scope: what's NOT in §7

Three adjacent concerns that the CH program will eventually
host but that Stage 12B explicitly defers:

- **Direct light sampling (NEE).** A future ray type would
  spawn shadow rays from the hit point toward sampled
  scene lights, with an AH program for visibility. The CH
  program would compute the direct-lighting contribution
  via `BRDF * cos / pdf` and add it to the payload's
  emission slots. This needs the shadow-ray miss program
  §6.6 forward-points to, the AH program covered in §8
  (future sub-stage), and the per-ray-type SBT layout
  covered in §9 (future sub-stage).
- **Non-diffuse BSDFs.** Specular / metallic / transmission
  evaluation grows the CH past the diffuse-only Lambert
  path. The MaterialParams fields are already plumbed
  through the parser; the CH grows a small switch +
  per-BSDF sample / eval / pdf calls. Adding a payload
  slot for the chosen sampling PDF (for MIS) is a natural
  follow-on once the BSDF dispatch lands.
- **Surface textures.** UV-driven base-colour /
  normal-map / emission-map evaluations need the texture
  system (master order #18). The vertex `uv` data is
  already interpolated in §7.6.2's recipe; the CH grows a
  texture sample call when the texture system arrives.

Each of these reaches the CH program; none of them
restructure the §7.1-§7.7 contract. The Stage 12B CH is a
narrow but extensible foundation.

---

## 8. Any-hit program

The OptiX **any-hit** program is the per-intersection
filter: it runs once for *every* candidate intersection
along a ray *before* OptiX commits the closest-hit, and
decides whether the candidate counts. By calling
`optixIgnoreIntersection()` it tells OptiX to drop the
candidate (so the ray continues searching for a closer
hit); by calling `optixTerminateRay()` it tells OptiX to
abandon the rest of the search and treat the current
candidate as final. Without either call the candidate
proceeds toward closest-hit selection unchanged.

This is fundamentally different from the **closest-hit**
program, which runs *once* per ray, only on the finalised
nearest hit. AH is the per-intersection visitor; CH is the
per-ray winner.

For Stage 12B's diffuse-only opaque path tracer the AH slot
adds nothing useful; the slot exists in every HitGroup
record whether or not we attach a program, and Stage 12B
attaches none. The AH slot becomes essential in later
stages — for shadow rays and alpha-tested geometry — and
the §9 SBT layout reserves it explicitly.

### 8.1 Role

OptiX invokes the AH program in two scenarios, both
gated by HitGroup-record content and per-trace ray flags:

- **Per-intersection filter for opaque geometry.** Each
  primitive intersection along the ray is offered to AH
  before OptiX considers it for the closest-hit slot. AH
  can `optixIgnoreIntersection()` to discard it (the ray
  keeps searching past `t_current`); the canonical use
  case is alpha-tested cutout geometry, where AH samples
  an opacity texture at the hit's UV and discards the
  intersection if the alpha is below threshold.
- **Early termination for binary-visibility queries.**
  When the caller doesn't care *which* primitive
  occluded a ray, only *whether* one did, AH can
  `optixTerminateRay()` on the first valid intersection.
  The canonical use case is shadow rays in NEE: the
  caller wants "is this light visible?", not "what is
  the closest occluder?". Terminating early skips the
  remaining traversal cost.

The AH program does NOT compute shading, write radiance
into the payload, or update throughput. Those stay in CH
and miss. AH's only outputs are its control-flow
intrinsics (`optixIgnoreIntersection` / `optixTerminateRay`)
and, optionally, payload registers set ahead of an early
terminate so the raygen can read post-trace state (for
shadow rays: a "occluded?" bit).

### 8.2 When the AH slot is used vs skipped

The AH slot is **used** when at least one of the
following is true for the ray + scene combination:

| Condition                                           | Stage     |
|-----------------------------------------------------|-----------|
| Ray type is "shadow" (NEE visibility query)         | 12C+      |
| Hit primitive's material has alpha-test cutout      | post-#18  |
| Hit primitive's material is partially transparent for shadow accumulation | far-future (after a transparency model lands) |
| Per-instance opacity overrides via SBT user-data    | far-future |

The AH slot is **skipped** in every other case. Stage 12B's
radiance ray type, against fully opaque diffuse Lambert
materials, has no AH need — every intersection is final,
visibility is binary by construction, and there is no
texture system yet to drive alpha tests.

OptiX exposes two complementary mechanisms for skipping
AH:

- **Per-trace ray flag `OPTIX_RAY_FLAG_DISABLE_ANYHIT`**.
  Set on every `optixTrace` call from raygen; OptiX skips
  AH invocation regardless of what the HitGroup record
  contains. Stage 12B's raygen sets this flag on every
  bounce ray.
- **HitGroup record carries `nullptr` for AH**. With no
  program attached, AH is a no-op even when the ray flag
  doesn't disable it. Stage 12B's HitGroup records (sphere
  + triangle, for the radiance ray type) carry
  `entry_function_name_AH = nullptr` at pipeline build time.

Belt-and-braces: Stage 12B uses both. The ray-flag is the
authoritative skip; the null AH record is the safety
net. Either alone suffices, but having both makes "did we
mean to invoke AH here?" answerable from either side of
the OptiX boundary.

### 8.3 Minimal plan

Stage 12B's AH plan is **no AH program**. Concretely:

- The pipeline configuration enumerates exactly two program
  groups for hit handling — `sphere_hitgroup` (CH only)
  and `triangle_hitgroup` (CH only). Neither names an AH
  entry function.
- The raygen's `optixTrace` calls all set
  `OPTIX_RAY_FLAG_DISABLE_ANYHIT`, and use a single ray
  type (radiance) so the AH slot is never reachable.
- The pipeline's `OptixPipelineCompileOptions::usesPrimitiveTypeFlags`
  enables only the geometry types we actually use
  (`OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE`,
  `OPTIX_PRIMITIVE_TYPE_FLAGS_SPHERE` if using the
  built-in sphere primitive). No AH-specific
  configuration is needed.

Stage 12C+ activations (in dependency order):

1. **Shadow-ray AH** for NEE. Adds a second ray type
   ("shadow"), a second miss program (writing `occluded
   = false` to a payload bit), and an AH program for
   each HitGroup that calls `optixTerminateRay()` on the
   first hit. The §9 SBT layout grows from 2 HitGroup
   records (radiance × {sphere, triangle}) to 4
   (radiance × {sphere, triangle} × {radiance,
   shadow}).
2. **Alpha-test AH** when the texture system (master
   order #18) lands. Each HitGroup whose material has
   an opacity texture grows an AH that samples the
   opacity at the hit's UV and calls
   `optixIgnoreIntersection()` if the sample is below
   the cutout threshold. The radiance ray type's
   `OPTIX_RAY_FLAG_DISABLE_ANYHIT` is removed for those
   HitGroups by *not* setting it on `optixTrace` (or
   by an SBT-record-level enable; OptiX exposes both).
3. **Transparent-shadow AH** (far future). Accumulates
   per-hit opacity into a shadow-ray payload register
   and ignores the intersection (so the ray continues
   past). Useful for translucent shadows; needs a
   real transparency model attached to materials, which
   the project doesn't have yet.

Each of those activations grows the AH program code +
some SBT plumbing. None of them restructure §5 / §6 / §7;
the AH slot is genuinely additive.

### 8.4 Read / write summary (when AH is present)

When Stage 12C+ adds an AH program, its read/write surface
will look like this — included here so the contract is
documented even though no AH program ships in 12B:

| Surface                              | Direction       | Lifetime        |
|--------------------------------------|-----------------|-----------------|
| `optixGetPrimitiveIndex()`           | read            | per intersection |
| `optixGetTriangleBarycentrics()`     | read (tri only) | per intersection |
| `optixLaunchParams.materials` (alpha cutout) | read    | constant per launch |
| `optixLaunchParams.mesh.{vertices, triangles}` (UVs for alpha) | read | constant per launch |
| SBT AH-record header                 | read (implicit) | constant per pipeline |
| SBT AH-record user-data              | read (when present) | constant per pipeline |
| OptiX ray payload (visibility bit)   | write (shadow rays only) | per `optixTrace` |
| `optixIgnoreIntersection()`          | call (alpha cutout)      | per intersection |
| `optixTerminateRay()`                | call (shadow rays)       | per intersection |

The AH program never writes to launch params, the SBT,
the framebuffer, the accumulator, or the geometry / material
arrays — same purity stance as the CH and miss programs.

### 8.5 Scope: what's NOT in §8

- **No transparency BSDF.** Transparent shadows need a
  real opacity / IOR model on materials; the project
  currently has only `MaterialParams::transmission` as a
  reserved-but-unused float (Stage 8 reservation).
  Activating that field is its own master-order slice.
- **No alpha-cutout textures.** The cutout-geometry use
  case needs the texture system (master order #18). The
  AH program code is small (one texture sample + threshold
  + ignore call), but the texture-binding plumbing is the
  bulk of the work.
- **No specialised shadow-ray throughput.** Stage 12C+
  shadow rays return a binary visibility, not an
  attenuation. Multi-tap stochastic visibility (for
  partially-transparent occluders, area-light penumbra)
  would extend the shadow-ray payload to carry an
  accumulated transmittance instead of a single bit; that
  is a refinement of the §8.3 step 1, not its baseline.

The Stage 12B path tracer is happy to live without AH
entirely. The slot's existence in the HitGroup record is
preserved through Stage 12B by design — when 12C lands,
attaching an AH program is a config-only change, not an
SBT-layout change.

---

## 9. Shader Binding Table

The OptiX **Shader Binding Table** (SBT) is the device-side
data structure that connects ray traversal events to the
program code that handles them. Every `optixTrace` call,
every miss, and every closest-hit decision dispatches
through the SBT — it is the lookup table the OptiX runtime
walks to find "which program, given which user data, runs
for this event?".

§5 / §6 / §7 / §8 each consume the SBT from one program's
viewpoint and forward-pointed details to §9. This section
consolidates those forward-pointers, defines the Stage 12B
record layout, and documents the index math + per-object /
per-material data routing that the program-side sections
deliberately deferred.

### 9.1 What the SBT stores

OptiX's SBT is split across three (optionally four) device
arrays the host sets on `OptixShaderBindingTable`:

| Field                     | Holds                              | Stage 12B count |
|---------------------------|------------------------------------|-----------------|
| `raygenRecord`            | A single raygen record (pointer)   | 1               |
| `missRecordBase`          | An array of miss records (table)   | 1 record        |
| `hitgroupRecordBase`      | An array of HitGroup records (table) | 2 records     |
| `callablesRecordBase`     | Optional callable program records  | 0 (unused)      |

Each record is a contiguous chunk of device memory laid
out as:

```
[ OPTIX_SBT_RECORD_HEADER_SIZE bytes (= 32) | user_data ]
```

The header is opaque — the host fills it via
`optixSbtRecordPackHeader(program_group, record_ptr)`,
which writes the program-group identifier OptiX needs to
dispatch into the right code at trace time. The user-data
section is whatever the host chooses: a plain POD the
program can later read via `optixGetSbtDataPointer()`.
Records within a single table (miss / HitGroup / callable)
**must be the same stride**, so all entries in a table need
to use a uniform user-data size.

Stride math:

```
stride = OPTIX_SBT_RECORD_HEADER_SIZE          // 32 bytes
       + max_over_records(sizeof(user_data))   // 0 in Stage 12B
       rounded up to OPTIX_SBT_RECORD_ALIGNMENT (16)
```

For Stage 12B, every record is a bare 32-byte header — no
user-data anywhere — so the stride is exactly 32 bytes for
every table.

### 9.2 Per-object / per-material data linkage

OptiX exposes two distinct mechanisms for getting per-
primitive / per-material metadata from the host into the
device-side programs:

1. **Launch-params arrays + hit-time index lookup.** The
   host puts arrays (materials, spheres, mesh metadata,
   etc.) on the device via existing GpuBuffer uploads,
   stores the device pointers in `optixLaunchParams`
   (a constant-memory POD bound to a fixed device symbol
   at pipeline link time), and the CH program reads
   `optixGetPrimitiveIndex()` / instance / vertex indices
   to *index* into those arrays.
2. **Per-record SBT user-data accessed via
   `optixGetSbtDataPointer()`.** The host packs the
   per-primitive metadata directly into the HitGroup
   record's user-data slot at SBT build time. The CH
   program calls `optixGetSbtDataPointer()` to get a
   pointer to the current record's user-data, then casts
   to the expected POD.

Both work; the choice is a tradeoff:

| Concern                               | Launch-params (Stage 12B) | SBT user-data |
|---------------------------------------|---------------------------|---------------|
| SBT rebuild on scene edit?            | No (just memcpy the array) | Yes (rebuild + repack) |
| Launch-params size                    | Grows with material/mesh count | Constant     |
| Cache locality                        | All threads load same launch_params from constant memory; arrays go through global memory | OptiX guarantees record locality for the hit's HitGroup |
| Multi-mesh scaling                    | Linear array growth + indexed lookup | Per-mesh records (linear SBT growth)        |
| Native OptiX idiom                    | Atypical (most renderers use SBT data) | Canonical |

Stage 12B picks **option 1 (launch-params)** for three
reasons:

1. **No SBT rebuilds during interactive editing.** A
   future interactive viewer will want to tweak materials
   / move spheres / re-upload one mesh without
   rebuilding the SBT. Launch-params arrays let the host
   memcpy a single material slot or sphere POD without
   pipeline-side work.
2. **Small material / mesh counts.** Stage 12B's target
   scenes (Stage 11C-style: 4 spheres + 1 mesh + 5 materials
   + 3 lights) make launch-params arrays tiny — a few
   hundred bytes total. The cache-locality argument that
   favours SBT user-data only matters at hundreds-of-
   thousands of distinct records.
3. **One source of truth.** The CUDA backend
   (`CudaSceneView` in `cuda/CudaScene.cuh`) already
   threads the same arrays through the kernel by value.
   The OptiX backend reading the same arrays from
   launch-params means both backends share the host-side
   `GpuScene::device_*()` accessors verbatim — there is
   no second data path to maintain during the migration.

The migration to **option 2** is on the table for future
multi-mesh + many-material scenes (production-grade content
with thousands of distinct surfaces). When that happens,
the §7 CH program's hit-data extraction step grows a
single line — `auto* data = optixGetSbtDataPointer();` —
and the host's SBT build grows a per-record user-data
copy. Neither change restructures §5 / §6 / §7 / §8; the
material / mesh data flow is *additive*.

### 9.3 Camera and relativity params: launch params, not SBT

§5.2.1, §6.2.2, and §7.2.2 each documented camera +
observer + relativity params as living in
`optixLaunchParams`. §9 makes the rule explicit: these
fields are **not** in the SBT. Three reasons:

1. **They change per launch.** `sample_index` changes
   between every `optixLaunch` (the host's spp loop);
   `observer.velocity` changes between renders; the
   camera POD changes when the user moves the camera.
   Encoding these into SBT records would require
   repacking + re-uploading the SBT on every launch, which
   defeats the SBT's caching purpose.
2. **They are small and broadcast-friendly.** The full
   per-launch state (camera POD ~80 B, observer ~16 B,
   relativity params ~32 B, env color/intensity ~16 B,
   plus pointers + sizes) is well under 256 B — a single
   constant-memory bind, accessible from every program
   with one load.
3. **They are program-agnostic.** Raygen, miss, and CH all
   need the same observer + relativity params (raygen for
   primary-ray aberration, miss for env Doppler, CH for
   emission Doppler). Putting them in launch-params lets
   each program read the same authoritative copy without
   duplicating the data across record categories.

The SBT records carry program identifiers (mandatory) and
*per-program* metadata when present. Per-launch state lives
in launch params. The two surfaces are non-overlapping by
construction.

### 9.4 Stage 12B layout

Concrete record list for the Stage 12B SBT:

| Table        | Index | Program group         | User data    | Record size |
|--------------|------:|-----------------------|--------------|------------:|
| raygenRecord |     0 | `pathtrace_raygen`    | (none)       | 32 B        |
| missRecord   |     0 | `pathtrace_miss`      | (none)       | 32 B        |
| hitgroupRecord |   0 | `sphere_hitgroup` (CH) | (none)      | 32 B        |
| hitgroupRecord |   1 | `triangle_hitgroup` (CH) | (none)    | 32 B        |

Total SBT footprint: **128 bytes** (4 records × 32 B). The
device buffer is allocated once at pipeline build and reused
across every launch.

The HitGroup records' assignment to primitives happens at
**acceleration-structure build time**, not at SBT build
time. When the host calls `optixAccelBuild` for the sphere
GAS, the build descriptor sets `sbtOffset = 0` (sphere
HitGroup record is HitGroup table index 0); for the
triangle GAS, `sbtOffset = 1`. The SBT itself is order-
independent at build; the AS records the per-GAS offsets.

### 9.5 Stage 12C+ extensions

Two natural growth axes from the Stage 12B baseline:

#### 9.5.1 Multi-ray-type (NEE shadow rays)

NEE adds a "shadow" ray type alongside the existing
"radiance" ray type. The SBT grows from 4 records to 7:

| Table        | Index | Program group               |
|--------------|------:|-----------------------------|
| raygenRecord |     0 | `pathtrace_raygen`          |
| missRecord   |     0 | `pathtrace_miss` (radiance) |
| missRecord   |     1 | `shadow_miss` (returns "no occluder") |
| hitgroupRecord |   0 | `sphere_hitgroup` (radiance: CH only) |
| hitgroupRecord |   1 | `sphere_hitgroup_shadow` (shadow: AH only, calls optixTerminateRay) |
| hitgroupRecord |   2 | `triangle_hitgroup` (radiance: CH only) |
| hitgroupRecord |   3 | `triangle_hitgroup_shadow` (shadow: AH only)        |

OptiX convention orders HitGroup records *interleaved by
ray type within geometry*: the index for `(geometry, ray
type)` is `geometry_sbt_offset * ray_type_count +
ray_type`. The `sphere` GAS gets `sbtOffset = 0` →
records 0 and 1; the `triangle` GAS gets `sbtOffset = 1`
→ records 2 and 3 (with `ray_type_count = 2`).

`optixTrace` calls pick the ray type via the `SBToffset`
+ `SBTstride` arguments (corresponds to the ray-type
slot within a HitGroup pair). Stage 12C+ raygen will use
ray type 0 for radiance and ray type 1 for shadow.

#### 9.5.2 Multi-mesh

When `GpuScene::upload_mesh` grows multi-mesh support
(carried-forward from 10B.11), the layout has two
options:

- **Per-mesh HitGroup records.** Each mesh becomes its
  own GAS or sub-GAS; each GAS gets its own
  `sbtOffset`; the HitGroup table grows to one record
  per mesh per ray type. Each record's user-data slot
  carries the mesh's vertex / triangle device pointers
  and `material_id`. The CH program reads
  `optixGetSbtDataPointer()` to find the mesh metadata
  for the current hit. **Linear SBT growth with mesh
  count** (acceptable up to thousands of meshes; cache-
  friendly per-hit lookup).
- **launch_params.meshes[] indexed by
  optixGetInstanceId().** A single HitGroup record
  per ray type covers all meshes; the CH program uses
  the IAS-provided instance ID to index a launch-params
  array of mesh metadata. **Constant SBT size** (just
  the existing 4 records); the launch-params arrays
  grow.

The choice mirrors §9.2's "launch-params vs SBT user-
data" tradeoff one level up. For interactive editing-
heavy workflows the launch-params route wins (no SBT
rebuild on per-mesh edits); for static scenes with many
meshes the SBT route wins (per-record cache locality).
Stage 12B does not need to commit; the multi-mesh slice
makes the call when it lands.

### 9.6 SBT index math

OptiX's HitGroup index for a given ray + intersection is:

```
hitgroup_index = sbtOffset                   // from optixAccelBuild
               + ray_type_count * instance_offset
               + ray_type                    // from optixTrace
```

Stage 12B's degenerate case:

- `sbtOffset` = 0 (sphere GAS) or 1 (triangle GAS)
- `ray_type_count` = 1 (radiance only)
- `instance_offset` = 0 (no IAS)
- `ray_type` = 0 (always radiance)

→ `hitgroup_index` is just `sbtOffset` (0 for sphere
hits, 1 for triangle hits). Linear lookup.

Stage 12C+ with shadow rays:

- `sbtOffset` = 0 (sphere GAS) or 2 (triangle GAS;
  doubled because each geometry now has two ray-type
  slots)
- `ray_type_count` = 2
- `ray_type` = 0 (radiance) or 1 (shadow)

→ `hitgroup_index` is `sbtOffset + ray_type` (0 = sphere
radiance, 1 = sphere shadow, 2 = triangle radiance,
3 = triangle shadow). The interleaving is fixed by OptiX
convention; the host sets `sbtOffset` per geometry at
GAS build time.

Multi-mesh + IAS adds the third term (`instance_offset`)
which the IAS provides per-instance at build time. The
formula stays the same shape; the host just has more
knobs to set.

### 9.7 Read / write summary

The SBT is **read-only at trace time**. Every entry
documented here is read by the OptiX runtime to dispatch
into the right program; nothing in the program code
writes back to the SBT. The host owns the SBT entirely
— builds it once per pipeline (or per scene-edit when
SBT user-data carries per-record state), uploads it to
the device, and never touches it again until the next
pipeline rebuild.

| Surface                              | Direction       | Lifetime            |
|--------------------------------------|-----------------|---------------------|
| `raygenRecord` (host upload)         | write           | per pipeline build  |
| `raygenRecord` (device read)         | read by OptiX runtime | per launch    |
| `missRecord` (host upload)           | write           | per pipeline build  |
| `missRecord` (device read)           | read by OptiX runtime | per miss      |
| `hitgroupRecord` (host upload)       | write           | per pipeline build  |
| `hitgroupRecord` (device read)       | read by OptiX runtime | per hit       |
| `optixGetSbtDataPointer()` (programs) | read           | per program invocation |

Stage 12B's empty user-data means the third row is the
only meaningful payload — the program-group identifier
in each record's header — and `optixGetSbtDataPointer()`
returns a pointer the programs do not dereference.

### 9.8 Scope: what's NOT in §9

Three adjacent SBT-related concerns this section does not
cover:

- **GAS / IAS construction.** §10 (acceleration
  structures, future sub-stage) covers the build flags,
  refit-vs-rebuild policies, and how `sbtOffset` /
  `instance_offset` are set per GAS / instance. §9
  documents the SBT records' shape; §10 will document
  what feeds them at AS-build time.
- **Multi-mesh `GpuScene::upload_mesh` upgrade.** The
  §9.5.2 layout discussion assumes the GpuScene side
  has grown multi-mesh upload support. That upgrade is
  a separate slice (carried-forward from 10B.11); §9
  documents the SBT layout that *would* result, not
  the GpuScene API change.
- **Callable programs.** OptiX 7+ supports callable
  programs (continuation-call functions invokable from
  any program) as a fourth SBT category. RelativityRender
  has no use for them yet; the canonical use is BSDF
  dispatch where each material's evaluation /
  sampling / pdf functions are callable records. When
  the BSDF dispatch lands (master order #13), the SBT
  grows a callable table; §9's existing 4-record
  baseline stays intact.

The SBT is a small but pervasive piece of the OptiX
contract — Stage 12B's 4-record table is enough for the
minimum viable backend, and every future expansion path
documented here is additive against that baseline.

---

## 10. Acceleration structures

OptiX's traversal performance comes from its **acceleration
structures** (BVHs the runtime walks during `optixTrace`).
This section documents the AS hierarchy RelativityRender
plans for Stage 12B and the data flow that drives it: which
GpuScene uploads feed which GAS, how the IAS wraps them,
how transforms are applied (or deliberately not, to
preserve CUDA-backend parity), and the rebuild-vs-refit
decision for the static-scene workloads Stage 12B targets.

§10 also finalises the two pieces §9 forward-pointed to:
how `sbtOffset` gets attached to a GAS at build time, and
where the single `OptixTraversableHandle` consumed by
`optixTrace` (`optixLaunchParams.scene_handle` in §5.2.1)
comes from.

### 10.1 Two-tier AS hierarchy: GAS + IAS

Stage 12B uses the canonical OptiX two-tier layout:

```
                            IAS
                         (root handle)
                       /             \
                     /                 \
              [instance: sphere]     [instance: mesh_0]
              transform = identity   transform = identity
              sbtOffset = 0          sbtOffset = 1
                  |                       |
                  v                       v
              sphere_GAS            mesh_0_GAS
              (all spheres)         (single mesh's
                                     vertices + tris)
```

- **GAS (Geometry Acceleration Structure)** holds
  primitives of a single type. RelativityRender uses two
  GAS variants:
  - One **sphere GAS** built from the entire
    `GpuScene::device_spheres()` array via OptiX 7.5+'s
    built-in sphere primitive (`OPTIX_PRIMITIVE_TYPE_SPHERE`,
    `OptixBuildInputSphereArray`). One GAS for all
    spheres, regardless of count.
  - One **mesh GAS per visible non-empty mesh** built
    from that mesh's `GpuMesh::device_vertices()` +
    `device_triangles()` via the built-in triangle
    primitive (`OptixBuildInputTriangleArray`). Stage
    12B's `GpuScene::upload_mesh` single-slot constraint
    means at most one mesh GAS today; multi-mesh growth
    (carried-forward from 10B.11) makes this an N-GAS
    list naturally.
- **IAS (Instance Acceleration Structure)** wraps the
  GAS handles into a single traversable handle that the
  raygen passes to `optixTrace`. The IAS holds an array
  of `OptixInstance` descriptors — one per GAS — each
  carrying its own transform and `sbtOffset`.

The single `OptixTraversableHandle` for the IAS root is
what gets stored in `optixLaunchParams.scene_handle`. The
raygen passes that handle to `optixTrace`; OptiX walks the
IAS, picks the per-instance transform + GAS handle, walks
the GAS's BVH, and dispatches into the right HitGroup
record for the hit primitive.

### 10.2 GAS construction

Both GAS variants follow the same three-step OptiX pattern
(`optixAccelComputeMemoryUsage` → allocate output + temp
buffers → `optixAccelBuild`):

#### 10.2.1 Sphere GAS

```cpp
OptixBuildInputSphereArray sphere_input{};
sphere_input.vertexBuffers       = &device_sphere_centers;  // device pointer
sphere_input.vertexStrideInBytes = sizeof(Sphere);          // POD stride
sphere_input.numVertices         = scene.sphere_count();
sphere_input.radiusBuffers       = &device_sphere_radii;    // can alias above
sphere_input.radiusStrideInBytes = sizeof(Sphere);
sphere_input.singleRadius        = 0;                       // per-sphere radii
sphere_input.flags               = &kSphereGeomFlags;       // 1 entry
sphere_input.numSbtRecords       = 1;                       // one HitGroup

OptixBuildInput input{};
input.type = OPTIX_BUILD_INPUT_TYPE_SPHERES;
input.sphereArray = sphere_input;
```

The `Sphere` POD's `center` (Vec3) and `radius` (float)
fields sit at known offsets in the struct, which lets the
sphere GAS reuse the same device pointer for both
vertex and radius arrays with appropriate strides — a
zero-copy upload on top of the existing
`GpuScene::upload_spheres`.

`numSbtRecords = 1` means every primitive in this GAS
binds to the same HitGroup record at trace time (the
sphere CH from §9.4). No per-primitive HitGroup variation
in Stage 12B; that's a future BSDF-dispatch slice's
concern.

#### 10.2.2 Mesh GAS (per mesh)

```cpp
OptixBuildInputTriangleArray triangle_input{};
triangle_input.vertexFormat        = OPTIX_VERTEX_FORMAT_FLOAT3;
triangle_input.vertexStrideInBytes = sizeof(rr::geometry::Vertex);
triangle_input.numVertices         = mesh.vertex_count();
triangle_input.vertexBuffers       = &device_mesh_vertices;  // strided POD

triangle_input.indexFormat         = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
triangle_input.indexStrideInBytes  = sizeof(rr::geometry::Triangle);
triangle_input.numIndexTriplets    = mesh.triangle_count();
triangle_input.indexBuffer         = device_mesh_triangles;

triangle_input.flags               = &kTriangleGeomFlags;    // 1 entry
triangle_input.numSbtRecords       = 1;                      // one HitGroup
```

The strided pointer trick works the same way: the
existing `Vertex` POD's `position` (Vec3) sits at offset 0,
so the triangle build input reads positions directly from
the existing `GpuMesh::device_vertices()` upload without a
copy. Per-vertex normals + UVs are at later offsets in the
same POD; the CH program reads them via the same device
pointer (per §7.6.2's recipe).

The triangle indices follow the same shape: `Triangle`
POD is three `uint32_t` fields, exactly the
`OPTIX_INDICES_FORMAT_UNSIGNED_INT3` layout, no
intermediate copy.

#### 10.2.3 Geometry flags

Each build input takes a `flags` array (one entry per SBT
record covered, 1 for Stage 12B). Stage 12B sets:

- `OPTIX_GEOMETRY_FLAG_NONE` for triangle meshes (default;
  per-intersection AH calls allowed if attached, which
  Stage 12B doesn't attach — see §8).
- `OPTIX_GEOMETRY_FLAG_NONE` for spheres (same default).

Future stages activate `OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT`
on geometries the user knows are opaque-no-cutout, which
lets OptiX skip even the AH dispatch overhead. Stage 12B
relies on the per-trace `OPTIX_RAY_FLAG_DISABLE_ANYHIT`
for the same effect, so the geometry-flag default suffices.

### 10.3 IAS construction

The IAS wraps the GAS handles into a single traversable.
For Stage 12B with one sphere GAS + at most one mesh GAS:

```cpp
OptixInstance instances[2] = {};

// Instance 0: sphere GAS, identity transform, sphere HitGroup.
instances[0].instanceId        = 0;
instances[0].sbtOffset         = 0;
instances[0].visibilityMask    = 0xFF;
instances[0].flags             = OPTIX_INSTANCE_FLAG_NONE;
instances[0].traversableHandle = sphere_gas_handle;
write_identity_3x4(instances[0].transform);

// Instance 1: mesh GAS, identity transform (Stage 12B), triangle HitGroup.
instances[1].instanceId        = 1;
instances[1].sbtOffset         = 1;
instances[1].visibilityMask    = 0xFF;
instances[1].flags             = OPTIX_INSTANCE_FLAG_NONE;
instances[1].traversableHandle = mesh_gas_handle;
write_identity_3x4(instances[1].transform);

OptixBuildInput ias_input{};
ias_input.type                       = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
ias_input.instanceArray.instances    = device_instances_buffer;
ias_input.instanceArray.numInstances = 2;
```

`OptixInstance::sbtOffset` is what §9.6's HitGroup index
math reads — sphere instance gets 0 (binds to the sphere
HitGroup record at index 0 in §9.4's table), mesh instance
gets 1 (binds to the triangle HitGroup record at index 1).
Stage 12C+ multi-ray-type expansion doubles these offsets
(per §9.5.1).

`instanceId` is the per-instance opaque cookie the CH can
read via `optixGetInstanceId()`. Stage 12B uses 0 for
sphere, 1 for mesh; future multi-mesh growth uses
`instanceId` as the index into a `launch_params.meshes[]`
array (per §9.5.2's launch-params-routed alternative).

`visibilityMask = 0xFF` means "visible to all ray types";
Stage 12B has only one ray type so the value is
unconstrained, but `0xFF` is the safe default.

### 10.4 How transforms are applied

The IAS instance transform is a 3×4 row-major affine
matrix that OptiX applies at trace time: when an
`optixTrace` ray enters an instance's bounding region,
OptiX transforms the ray into the GAS's local space using
the instance's inverse transform, runs the GAS traversal
in local space, and reports hit positions / normals back
in world space. This is the per-instance transform
RelativityRender's §11 `transform` field on `SceneMesh` is
designed to drive.

**Stage 12B writes identity transforms on every
instance.** The reason is parity with the CUDA backend:
the existing `k_render_scene` reads vertex positions in
world space directly (per `RRSCENE_FORMAT.md` §9.4's
mesh-renderer note — "the per-mesh `transform` is
uploaded but not applied"). If the OptiX backend started
applying the transform, the same `.rrscene` file would
render differently between the two backends — a silent
behaviour change neither backend audited for.

The activation path, when both backends switch in sync,
is one well-understood change per backend:

- **Convert `Transform` to 3×4 matrix.** Standard
  composition: `M = T · R · S` where `T` is translation
  from `transform.position`, `R` is XYZ-Euler rotation
  from `transform.euler_rotation_radians` (per
  `RRSCENE_FORMAT.md` §11's intrinsic-XYZ convention),
  `S` is non-uniform scale from `transform.scale`. The
  conversion lives in a new `rr::math::transform_matrix`
  helper used by both backends.
- **Vertex positions become local-space.** The mesh's
  uploaded vertex positions are interpreted as
  local-space (already true for any mesh that doesn't
  pre-bake the transform). The IAS instance transform
  carries the conversion to world space.
- **CUDA backend symmetry.** The CUDA `k_render_scene`
  has to learn to apply the transform during
  intersection — either by transforming the ray into
  local space before the per-mesh triangle loop, or by
  transforming each vertex to world space at hit time
  (less efficient). The Stage 9B kernel design doesn't
  do either today.

§10 documents the architecture; the activation slice is
its own scoped change. Stage 12B's identity-transform
choice is a deliberate compatibility floor, not a
limitation of the OptiX hierarchy.

### 10.5 Build flags

`optixAccelBuild` takes flags describing how the AS will
be used. Stage 12B picks:

- `OPTIX_BUILD_FLAG_PREFER_FAST_TRACE` — prioritise
  traversal performance over build cost. This is the
  default for static scenes (offline rendering) and the
  right pick for path-tracing workloads where the AS is
  built once per scene load and traced billions of times
  per render.

Stage 12B does NOT set:

- `OPTIX_BUILD_FLAG_ALLOW_UPDATE` — required for
  refit (see §10.6). Costs ~30% more memory and makes
  initial builds slightly slower. Stage 12B targets
  static scenes; refit is a future-slice concern.
- `OPTIX_BUILD_FLAG_ALLOW_COMPACTION` — enables the
  optional post-build compaction pass that typically
  shrinks the AS by 20-30%. Stage 12B skips this for
  simplicity; production-quality memory budgets should
  enable it once the activation path is uncomplicated.

`OPTIX_BUILD_OPERATION_BUILD` is the build operation
type for both initial GAS and IAS construction; Stage 12B
never uses `OPTIX_BUILD_OPERATION_UPDATE`.

### 10.6 Rebuild vs refit

Two AS-mutation operations OptiX exposes for animated
scenes:

| Operation | When to use                                    | Cost                  | Constraint                        |
|-----------|------------------------------------------------|-----------------------|-----------------------------------|
| Rebuild   | Topology changes; first-time build             | Full build cost       | None                              |
| Refit     | Per-vertex position changes only (animation)   | ~10× faster than rebuild | Requires `ALLOW_UPDATE` build flag; topology must not change |

Refit's contract: OptiX preserves the BVH structure from
the previous build and only updates the bounding boxes at
the leaves and internal nodes. This is correct as long as
the BVH split planes still produce reasonable spatial
locality after the position update. Animation that moves
vertices a lot (e.g., a character walking across the
scene) eventually drifts the BVH out of locality and
traversal performance degrades; the canonical fix is
"refit for N frames, rebuild on frame N+1, repeat".

**Stage 12B uses rebuild only.** Static scenes mean every
AS is built once at scene load and reused unchanged for
every render. Refit becomes useful in two future slices:

- **Vertex animation** (master order #22 / animation
  slice) — mesh vertex positions change per frame; refit
  per frame avoids the O(triangles) full-build cost.
  Activates `OPTIX_BUILD_FLAG_ALLOW_UPDATE` on mesh GASes.
- **Interactive sphere editing** (interactive viewer
  slice) — the artist drags a sphere; refit the sphere
  GAS in microseconds rather than rebuilding it. Same
  flag activation.

Neither slice exists yet. §10 documents the architecture;
the activation slices flip the build-flag bit per their
scope.

### 10.7 Motion blur (minimal note)

OptiX supports motion blur via `OptixMotionOptions` on
both GAS build inputs (deformable geometry) and IAS
instances (motion transforms). The motion options describe
how the AS varies over a `[t_min, t_max]` interval; the
runtime's `optixTrace` takes a per-ray time parameter
(implicit in OptiX 7+ via the SBT) and the BVH traversal
interpolates accordingly.

**Stage 12B does not use motion blur.** No
`OptixMotionOptions` are set on any GAS or IAS; the
single AS represents a single point in time, the camera
is shutter-zero, every ray queries the same BVH. The
existing `GpuCamera` POD has no shutter/time fields.

When motion blur lands (its own master-order slice,
post-#22), the activation path is:

- Add shutter time fields to `GpuCamera` (`shutter_open`,
  `shutter_close`).
- Set `OptixMotionOptions::numKeys = 2`,
  `timeBegin = shutter_open`, `timeEnd = shutter_close`,
  `flags = OPTIX_MOTION_FLAG_NONE` on instances that
  move during the shutter window.
- Provide multi-key transforms (one per key) for IAS
  instances that animate; multi-key vertex buffers for
  GASes that deform.
- Raygen samples a random time in `[shutter_open,
  shutter_close]` per primary ray (advancing the existing
  `pathtracer::Rng`) and threads the time through
  `optixTrace`'s built-in time argument.

§10's GAS + IAS architecture survives the addition without
restructuring; motion options are an additive build-input
field.

### 10.8 Read / write summary

| Surface                                | Direction       | Lifetime              |
|----------------------------------------|-----------------|-----------------------|
| `GpuScene::device_spheres()`           | read at GAS build | per scene load (rebuild) |
| `GpuMesh::device_vertices()`           | read at GAS build | per scene load        |
| `GpuMesh::device_triangles()`          | read at GAS build | per scene load        |
| GAS output buffer (`GpuBuffer<u8>`)    | write at GAS build | per scene load        |
| GAS temp buffer                        | write at GAS build, freed after build | scratch         |
| IAS instance descriptors (host-built, uploaded) | write at IAS build | per scene load |
| IAS output buffer (`GpuBuffer<u8>`)    | write at IAS build | per scene load       |
| `OptixTraversableHandle` (root)        | host stores; uploaded into `optixLaunchParams.scene_handle` | per scene load |

The AS itself is read-only at trace time — every
`optixTrace` walks the IAS + GAS without modifying them.
Stage 12B's static-scene posture means the buffers also
don't change between renders.

### 10.9 Scope: what's NOT in §10

Three AS-related concerns this section deliberately
defers:

- **Compaction.** `OPTIX_BUILD_FLAG_ALLOW_COMPACTION` +
  the post-build `optixAccelCompact` call shrink the AS
  by 20-30%. Stage 12B's small scenes don't make this
  worth the complexity; production memory budgets
  activate it.
- **Per-primitive HitGroup variation.** A triangle GAS
  build input with `numSbtRecords > 1` plus a
  `sbtIndexOffsetBuffer` per triangle lets different
  triangles in the same mesh bind to different HitGroup
  records. Useful when one mesh contains primitives with
  different materials at different draw boundaries; the
  current `Mesh::material_id` is per-mesh, so the feature
  is unused. Master order #13 (material expansion)
  may activate it.
- **Opacity Micromaps (OMM) / Displacement Micromaps
  (DMM).** OptiX 7.6+ features for sub-triangle alpha
  and displacement. Both deferred until the texture
  system + alpha-test path lands (master order #18).

The Stage 12B AS is a deliberately simple two-tier
hierarchy: one sphere GAS, one mesh GAS, one IAS wrapping
both with identity transforms. Every future expansion
listed above is additive against that baseline.

---

## 11. Camera data

The camera is the smallest device-side data surface that
matters per launch — a few dozen bytes that drive every
primary ray's origin and direction. §5.2.1 sketched the
fields informally inside the `optixLaunchParams` struct;
§9.3 fixed the routing rule (camera lives in launch params,
not SBT). §11 consolidates: the exact POD shape, the
per-launch state that travels alongside it, and the
host-side update flow.

### 11.1 Source

The camera flows from host to device through a
single existing snapshot path:

```
rr::camera::Camera (host class, mutable basis + fov)
              │
              │  Camera::to_gpu()
              ▼
rr::camera::GpuCamera (POD, defined in camera/CameraRay.h)
              │
              │  copy by value into optixLaunchParams.camera
              ▼
optixLaunchParams (constant memory)
```

`Camera::to_gpu()` is the same RR_HD-friendly snapshot
function the CUDA backend already calls (Stage 6B's
`render_camera_rays` / Stage 11C's `k_pathtrace_sample`
both consume the result). The OptiX backend reuses it
verbatim — there is no second camera POD or second
snapshot path. The migration of the rendering layer does
not touch the camera layer at all.

### 11.2 GpuCamera POD field list

The camera POD lives in `camera/CameraRay.h` and is the
exact shape the raygen reads:

| Field           | Type   | Size | Purpose                                                        |
|-----------------|--------|-----:|----------------------------------------------------------------|
| `position`      | Vec3   | 12 B | World-space camera origin; primary-ray origin                  |
| `forward`       | Vec3   | 12 B | Unit forward direction (camera looks down `forward`)           |
| `up`            | Vec3   | 12 B | Unit up direction; orthogonal to `forward`                     |
| `right`         | Vec3   | 12 B | Unit right direction; orthogonal to both; RH coordinate system |
| `tan_half_vfov` | float  |  4 B | Precomputed `tan(vfov / 2)` — vertical FOV in tangent form     |
| `aspect`        | float  |  4 B | Width / height ratio                                           |

Total: **56 bytes** per camera POD.

The user-prompt bullet asked for "position, forward, up,
right, fov". The POD stores `fov` in its already-computed
tangent form (`tan_half_vfov`) rather than the raw degrees
— the raygen needs `tan(vfov/2)` to map pixel coordinates
to the image plane (per `generate_primary_ray` in §5 / the
existing `generate_camera_ray` in `camera/CameraRay.h`),
and precomputing it on the host saves a per-pixel
transcendental on the device. `aspect` is precomputed for
the same reason. Both are derived from the host
`Camera::vertical_fov_degrees()` and the framebuffer
dimensions at `to_gpu` time.

The basis (`forward`, `up`, `right`) is **stored
explicitly** rather than reconstructed from a single
look-direction. This matches the host `Camera::look_at`
contract: re-orthogonalise the basis once on the host,
then pass the right-handed orthonormal triple to the
device. The raygen does no basis reconstruction — its
work is `position + (right * u + up * v + forward) * t_param`
where `u, v` come from the pixel-coordinate / aspect /
tan_half_vfov maths.

### 11.3 Resolution and sample_index

Resolution and sample index are **not** part of the
camera POD. They live alongside the camera in
`optixLaunchParams` because they're per-launch state the
raygen also consumes:

| Field          | Type | Size | Purpose                                                                      |
|----------------|------|-----:|------------------------------------------------------------------------------|
| `width`        | int  |  4 B | Framebuffer width in pixels; raygen reads `optixGetLaunchIndex().x` against it |
| `height`       | int  |  4 B | Framebuffer height in pixels                                                 |
| `sample_index` | u32  |  4 B | Current spp iteration; mixed into `make_pixel_rng` per pixel for decorrelated sample streams |

Why not in the camera POD: the camera describes the
optical configuration (where, looking at what, at what
fov + aspect); resolution describes the *output surface*
the camera projects onto, and sample_index describes the
*sampling state* of the path tracer. Conflating them into
`GpuCamera` would couple the camera class to the spp
loop's iteration counter — the wrong abstraction. Keeping
them as siblings inside `optixLaunchParams` lets the
camera POD stay the same shape the CUDA backend uses
without modification.

The host populates `width` / `height` from
`scene.render_settings` (Stage 10B.2 parser output).
`sample_index` advances by 1 per `optixLaunch`, driven by
the host-side spp loop in `PathTracer::render` (Stage
11C's CUDA path tracer follows the same shape; the OptiX
migration replaces the kernel-launch primitive but not
the loop).

### 11.4 Routing: launch params, not SBT

The camera POD + `width` / `height` / `sample_index` all
live in `optixLaunchParams` — the constant-memory POD
bound to a fixed device symbol at pipeline link time. The
host populates the struct before each `optixLaunch` and
copies it to the device-side launch-params buffer.

§9.3 fixed this rule for camera + relativity params;
§11.4 inherits it for the same three reasons:

1. **Per-launch mutability.** The camera moves between
   renders (interactive editing); resolution can change
   between renders (re-render at a higher quality);
   sample_index changes between every launch in a
   single render (the spp loop). Encoding any of these
   into SBT records would force an SBT rebuild on every
   such change, defeating the SBT's caching purpose.
2. **Broadcast-friendly small size.** 56 bytes camera +
   12 bytes (width + height + sample_index) = 68 bytes
   total per launch — well under the constant-memory
   broadcast budget. Every program reads from the same
   bound symbol with one load.
3. **Program-agnostic shared state.** Raygen is the
   primary consumer (§5.5), but miss / CH (§6, §7)
   also read the camera position for emission/env
   Doppler maths if a future relativistic extension
   needs the observer's position relative to a hit
   point. Keeping the camera in launch params lets all
   programs read the same authoritative copy.

The SBT records carry zero camera data. `optixGetSbtData
Pointer()` in any program returns a pointer to the empty
user-data slot; the camera POD comes from the constant-
memory launch params instead.

### 11.5 Host-side update flow

The host populates the launch-params struct's camera
fields once per launch:

```cpp
// In the OptiX path tracer's host-side render loop:
optixLaunchParams.camera       = scene.gpu_camera();        // 56 B copy
optixLaunchParams.width        = scene.render_settings.width;
optixLaunchParams.height       = scene.render_settings.height;
optixLaunchParams.sample_index = static_cast<unsigned int>(s);
// ... other fields ...

cudaMemcpy(d_launch_params, &optixLaunchParams,
           sizeof(optixLaunchParams), cudaMemcpyHostToDevice);
optixLaunch(pipeline, stream, d_launch_params,
            sizeof(optixLaunchParams), &sbt,
            width, height, /*depth=*/1);
```

Cost per launch: one 56 + 12 byte struct write, one
cudaMemcpy of the ~few-hundred-byte launch-params struct
(camera + relativity + scene-handle + pointers), and the
optixLaunch itself. **No SBT rebuild, no AS rebuild, no
pipeline rebuild.** The launch-params copy is tens of
nanoseconds; the optixLaunch itself dominates.

This is the same shape Stage 11C's CUDA path tracer uses:
`PathTracer::render` populates per-launch state, then
calls the kernel-launch primitive. The OptiX migration
swaps `<<<grid, block>>>` for `optixLaunch` and the launch-
arg POD for the launch-params constant-memory bind; the
loop shape is identical.

### 11.6 Read / write summary

| Surface                                 | Direction      | Lifetime               |
|-----------------------------------------|----------------|------------------------|
| `rr::camera::Camera` (host class)       | host read/write | persistent across renders |
| `Camera::to_gpu()` returned `GpuCamera` | host write to launch-params | per launch |
| `optixLaunchParams.camera`              | device read (raygen + future programs) | per launch |
| `optixLaunchParams.width / height`      | device read (raygen + accumulator-sized) | per launch (typically per render) |
| `optixLaunchParams.sample_index`        | device read (raygen RNG seed) | per launch (every iteration) |
| SBT records (any table)                 | (no camera data) | n/a                  |

The camera surface is read-only on the device side; only
the host writes it. The host can mutate it freely between
launches without touching the SBT, the AS, or the
pipeline.

### 11.7 Scope: what's NOT in §11

Two camera-related concerns this section deliberately
defers:

- **Depth of field.** A real lens model needs an
  aperture radius + focal-distance pair on `GpuCamera`,
  plus raygen lens-disk sampling (`pathtracer::Rng`
  draws → disk-sample direction perturbation). The POD
  grows by 8 bytes; the routing stays the same. Future
  master-order slice; not in 12B.
- **Motion blur shutter.** Motion blur (per §10.7) needs
  `shutter_open` / `shutter_close` time fields on
  `GpuCamera`, which the raygen samples per primary ray
  to thread a time parameter through `optixTrace`.
  Activates alongside `OptixMotionOptions` on the IAS;
  scope-tied to that future slice.

Both are additive: the §11 routing (launch params, not
SBT) does not change. The POD grows; the constant-memory
bind covers it.

---

## 12. Material data

Materials are the second-largest device-side data surface
in Stage 12B (after the AS itself): one `MaterialParams`
POD per scene material, indexed by `material_index` at
hit time. §7.2.2 / §7.4 already documented the
closest-hit's material *consumption*; §9.2 fixed the
routing decision (launch-params arrays, not SBT
user-data); §12 consolidates the field list, the index
lookup path from the user's "via SBT/hit record" bullet,
and the BSDF-evaluation-location commitment.

### 12.1 Source

Materials flow from host to device through the existing
parser → upload chain:

```
.rrscene file ("materials" array)
              │
              │  Stage 10B.5 parser (apply_materials)
              ▼
rr::scene::Scene::materials  (vector<SceneMaterial>)
              │  flatten the .params field
              ▼
flat MaterialParams[] (host-side temp; in main.cpp's render handler)
              │  GpuScene::upload_materials(host, count)
              ▼
GpuScene's device_materials_ (GpuBuffer<MaterialParams>)
              │  GpuScene::device_materials()
              ▼
optixLaunchParams.materials (device pointer + count)
              │  read by closest-hit at hit time
              ▼
MaterialParams (POD, consumed by §7.4 Lambert evaluation)
```

The OptiX backend reuses every step from "Stage 10B.5
parser" through "`GpuScene::device_materials()` returns
the device pointer" verbatim. No new upload path, no new
materials POD, no new authoring API. The migration adds
exactly one wire: the device pointer from
`GpuScene::device_materials()` is stored into
`optixLaunchParams.materials` once per launch, alongside
`material_count` from `GpuScene::material_count()`.

This is the same data the CUDA backend's
`CudaSceneView` carries by value (Stage 9B
`k_render_scene` and Stage 11C `k_pathtrace_sample` both
read `scene.materials[idx]`); the OptiX backend reads the
same array through the same device pointer.

### 12.2 MaterialParams POD field list

The POD lives in `material/MaterialTypes.h` and is the
exact shape the closest-hit program reads:

| Field             | Type  | Size | Purpose                                                              | Stage 12B uses? |
|-------------------|-------|-----:|----------------------------------------------------------------------|:---------------:|
| `baseColor`       | Vec3  | 12 B | Diffuse albedo; CH writes to payload, raygen multiplies into throughput | yes             |
| `emissionColor`   | Vec3  | 12 B | Emission base colour                                                 | yes             |
| `emissionStrength`| float |  4 B | Emission scalar multiplier; combined with `emissionColor` for radiance | yes           |
| `roughness`       | float |  4 B | BRDF roughness in `[0, 1]` (0 = mirror, 1 = fully rough)             | **no**          |
| `metallic`        | float |  4 B | Conductor blend in `[0, 1]` (0 = dielectric, 1 = conductor)          | **no**          |
| `specular`        | float |  4 B | Dielectric F0 scale in `[0, 1]`                                      | **no**          |
| `transmission`    | float |  4 B | Reserved-but-unused glass / refraction placeholder (Stage 8 reservation) | **no**      |

Total: **44 bytes** per material.

Stage 12B's "diffuse only" posture (per §7.4 and the
Stage 11C prompt) means only the first three fields
(`baseColor`, `emissionColor`, `emissionStrength`) are
*consumed* by the closest-hit. The other four
(`roughness`, `metallic`, `specular`, `transmission`)
are uploaded by the parser, kept in the POD's payload,
and ignored at trace time. Activating them is a future
BSDF-dispatch slice (§12.4); the upload pipeline does
not change.

### 12.3 Material id lookup at hit time

The user's "material id lookup via SBT/hit record" bullet
deserves a precise answer: Stage 12B performs the lookup
through the **launch-params route**, not the SBT
user-data route, per §9.2's chosen architecture. The
exact closest-hit code path (already sketched in §7.6) is:

```cpp
// In the closest-hit program for a sphere hit (§7.6.1):
const unsigned int prim_idx = optixGetPrimitiveIndex();
const Sphere s              = optixLaunchParams.spheres[prim_idx];
const int material_index    = s.material_index;

// In the closest-hit program for a triangle hit (§7.6.2):
const auto& mesh         = optixLaunchParams.mesh;
const int material_index = mesh.material_id;

// Material lookup is shared across both cases:
const int mat_count = optixLaunchParams.material_count;
const MaterialParams m = (material_index >= 0 && material_index < mat_count
                          && optixLaunchParams.materials != nullptr)
                       ? optixLaunchParams.materials[material_index]
                       : MaterialParams{};   // neutral grey diffuse default
```

The chain is **two indirections**:

1. **Primitive → material_index.** The primitive's
   metadata carries the index. Sphere POD has
   `material_index` (Stage 6A); SceneMesh has
   `material_id` (Stage 7A's `Mesh::material_id` promoted
   to `SceneMesh` in Stage 10B.8). The CH reads the index
   via `optixGetPrimitiveIndex()` for spheres or
   straight from the mesh metadata for triangles.

2. **material_index → MaterialParams.** Index into
   `optixLaunchParams.materials[]` with a defensive
   range check; out-of-range indices fall back to
   `MaterialParams{}` (the neutral grey diffuse default
   the CUDA backend already uses, per Stage 11C
   `material_for`).

**Why launch-params, not SBT user-data.** §9.2 covered
this in detail; the material-specific recap:

- The HitGroup table has 2 records in Stage 12B (sphere
  + triangle). Routing per-material data through the
  SBT would require either inflating the HitGroup table
  to one record per `(primitive, material)` pair (which
  defeats the SBT's compact 128-byte footprint per §9.4)
  OR routing through `optixGetSbtDataPointer()` which
  returns the same 32-byte HitGroup record for *every*
  hit on the same primitive type — useless for per-
  primitive material variation.
- The launch-params route gives O(1) per-hit lookup
  with one global-memory load + one cached index load.
  No SBT rebuild on per-material edits (interactive
  workflow); the host can `cudaMemcpy` a single 44-byte
  slot in `materials[]` and the next launch sees the
  update.
- The CUDA backend already uses this exact lookup path
  in `k_render_scene` (Stage 9B) and `k_pathtrace_sample`
  (Stage 11C). The OptiX backend reads the same data
  through the same device pointer, so a fixture renders
  identically across both backends (modulo the
  acceleration-structure traversal, which is the whole
  reason for the migration).

When materials grow into the thousands per scene (a
production-grade asset library), the SBT-data route
becomes worth its complexity — see §9.2's "future option"
discussion. Stage 12B's material count is small enough
(typically ≤ 32 per `.rrscene` fixture) that the
launch-params route is unequivocally the right call.

### 12.4 BSDF evaluation location: closest-hit

BSDF evaluation lives in the **closest-hit program**, per
§7.4. Stage 12B implements **pure Lambert diffuse**: the
CH writes `baseColor` to the payload's `albedo.*` slots,
the raygen multiplies throughput by the albedo on the
next bounce. The cos-weighted hemisphere sampling
(`pathtracer::sample_cosine_hemisphere`) makes the BRDF
evaluation collapse to a single multiplication; no
explicit BRDF call is needed.

When the BSDF dispatch lands (master order #13), the CH
program's §7.4 evaluation step grows a small switch on
the material's BSDF type:

```cpp
// Pseudocode for the future BSDF dispatch in CH (§7.4 extension):
switch (m.bsdf_type) {
case BsdfType::Lambert:
    payload_albedo  = m.baseColor;
    payload_pdf     = pdf_cosine_hemisphere(cos_theta);
    payload_sample_hint = SampleHint::CosineHemisphere;
    break;
case BsdfType::Specular:
    // ... mirror sample, delta PDF, ...
    break;
case BsdfType::Microfacet:
    // ... GGX sample using m.roughness + m.metallic + m.specular, ...
    break;
}
```

The `roughness`, `metallic`, `specular` fields enter the
evaluation here. The §11.x payload register layout
(§7.3) has 7 unused registers in Stage 12B's 13-of-32
budget, which gives plenty of headroom for the additional
payload state (sampled PDF for MIS, per-BSDF sampling
hints for raygen-side direction generation).

The BSDF dispatch sits **inside** the closest-hit
program; the program-level routing across raygen / miss /
CH / AH does not change. §7.4's Lambert evaluation is
the foundation; non-diffuse BSDFs are an additive
extension within the same program.

### 12.5 Routing summary

For clarity, the §9.2 / §12.3 routing rule for material
data:

| Surface                                  | Stores material data?       |
|------------------------------------------|-----------------------------|
| `optixLaunchParams.materials`            | **yes** (primary route; device pointer + count) |
| SBT raygen record                        | no                          |
| SBT miss record                          | no                          |
| SBT HitGroup records (12B: empty)        | no                          |
| SBT HitGroup records (future: per-record material POD) | maybe (post-#13 / post-#18) |
| GAS / IAS                                | no (geometry only)          |
| Constant-memory broadcast (other than launch-params) | no              |

Stage 12B materials live exclusively in
`optixLaunchParams.materials`. Future routing changes are
additive — moving some materials into per-record SBT
data does not require migrating *all* materials; the
launch-params array stays the canonical source for
fallback indices.

### 12.6 Read / write summary

| Surface                                | Direction                     | Lifetime           |
|----------------------------------------|-------------------------------|--------------------|
| `Scene::materials` (host vector)       | host read/write               | persistent         |
| flat `MaterialParams[]` (host temp)    | host write at upload time     | per scene load     |
| `GpuScene::device_materials_` buffer   | host write at upload time     | per scene load     |
| `optixLaunchParams.materials` ptr      | host write per launch         | per launch         |
| `optixLaunchParams.material_count`     | host write per launch         | per launch         |
| `optixLaunchParams.materials[*]` (device read) | device read (CH program) | per hit            |

The materials buffer is read-only at trace time. Per-
material edits between launches are a single `cudaMemcpy`
of one slot (44 bytes) plus a launch-params re-bind; no
SBT, AS, or pipeline rebuild.

### 12.7 Scope: what's NOT in §12

Three material-related concerns this section deliberately
defers:

- **Real BSDF dispatch.** Activating `roughness`,
  `metallic`, `specular` in the closest-hit. Master
  order #13. The §12.4 sketch shows the integration
  point; the implementation is its own slice.
- **Texture-driven material parameters.** `base_color`
  / `roughness` / `normal` / `emission` driven by
  textures sampled at the hit's UV. Master order #18
  (texture system). Adds device-side `cudaTextureObject_t`
  arrays to `optixLaunchParams` and a per-hit texture
  sample call inside the CH; routing of the
  *texture-binding handles* mirrors materials' routing
  (launch-params arrays, indexed by id).
- **Spectral materials.** Wavelength-dependent BRDF
  responses for accurate dispersion / iridescence.
  Far-future; would extend `MaterialParams` with a
  spectral coefficient array. The launch-params route
  scales to per-material spectral data without
  restructuring.

Each of these is additive against the §12.1-§12.6
contract. The Stage 12B material data flow — parser →
GpuScene::upload_materials → launch-params pointer →
CH index lookup — is a stable foundation that future
slices grow on top of.

---

## 13. Light data

Lights are the most subtle data category in Stage 12B's
OptiX backend: they are **uploaded but not directly
sampled**. Stage 11C established this posture for the
CUDA path tracer (lights upload through the existing
GpuScene chain; the kernel relies on emissive surfaces +
the env-fallback colour for illumination); the OptiX
migration inherits the posture verbatim. §13 documents
the data flow that exists today, the env-light → env-
fallback bridge that links scene-authored environments
to the miss program's input, and the planned NEE
integration that makes lights a first-class radiance
contributor in 12C+.

### 13.1 Source

Lights flow from host to device through the existing
parser → upload chain, identical in shape to materials
(§12.1):

```
.rrscene file ("lights" array)
              │
              │  Stage 10B.7 parser (apply_lights)
              ▼
rr::scene::Scene::lights  (vector<SceneLight>)
              │  flatten the .data field (.object metadata dropped at upload)
              ▼
flat rr::lighting::Light[] (host-side temp)
              │  GpuScene::upload_lights(host, count)
              ▼
GpuScene's device_lights_ (GpuBuffer<Light>)
              │  GpuScene::device_lights()
              ▼
optixLaunchParams.lights (device pointer + count)
              │  read by closest-hit when NEE activates
              ▼
Light POD (consumed by future direct-lighting evaluation)
```

The OptiX backend reuses every step verbatim. The Light
POD itself was finalised in Stage 9A; the upload path
in 9B; the parser in 10B.7. Stage 12B adds exactly the
launch-params pointer assignment.

### 13.2 Light POD field list

The `Light` POD lives in `lighting/Light.h` and uses a
flat type-discriminated layout (no union; no virtual
dispatch) so it travels through `GpuBuffer<Light>` and
constant-memory broadcasts cleanly:

| Field        | Type        | Size | Purpose                                                           |
|--------------|-------------|-----:|-------------------------------------------------------------------|
| `type`       | LightType (i32) |  4 B | Discriminator: 0=Point, 1=Directional, 2=Area, 3=Environment   |
| `color`      | Vec3        | 12 B | Linear-space RGB radiance (HDR allowed; intensity multiplies)     |
| `intensity`  | float       |  4 B | Scalar multiplier on `color`                                      |
| `position`   | Vec3        | 12 B | World-space anchor — used by Point and Area, ignored otherwise    |
| `direction`  | Vec3        | 12 B | Unit direction — used by Directional and Area, ignored otherwise  |
| `area_width` | float       |  4 B | Area-rectangle local-frame extent (Area placeholder)              |
| `area_height`| float       |  4 B | Area-rectangle local-frame extent (Area placeholder)              |

Total: **52 bytes** per light.

The four supported types from §10 of `RRSCENE_FORMAT.md`:

- **Point** — `position` is the world-space emitter location;
  `color * intensity / d²` falloff at the receiver. NEE samples
  this as a single deterministic shadow ray (no direction
  sampling needed; the emitter is a delta in space).
- **Directional** — `direction` is the photons' propagation
  direction (a unit vector); receivers use `-direction` as
  the to-light vector. `color * intensity * max(0, N · -dir)`
  with no falloff. NEE samples this as a single deterministic
  shadow ray (the emitter is a delta in direction).
- **Area** (placeholder) — `position` + `direction` define
  the rectangle's anchor + surface normal; `area_width` /
  `area_height` are the local-frame extents. Stage 9 marks
  this as a placeholder; the kernel currently skips area
  lights. NEE will sample area lights stochastically (one
  random point on the rectangle per shadow ray; PDF over
  area-element).
- **Environment** — `color * intensity` is a flat sky tint
  in Stage 12B; HDR env-maps are the §13.4 / §6.6 future
  expansion. Stage 12B's environment light is the bridge
  between scene-authored env data and the miss program's
  `env_color` / `env_intensity` input (see §13.4).

### 13.3 Stage 12B status: uploaded but not sampled

This subsection is honest accounting: in Stage 12B's path
tracer, **scene lights upload to the device but the kernel
never reads them at trace time**. The reasons are
structural:

- **Stage 11C is diffuse-only with no NEE** (per the
  Stage 11C prompt's "Keep materials simple" + "No MIS
  yet"). The path tracer relies on:
  - Emissive surface hits (Material's
    `emissionColor * emissionStrength`, modulated by
    Doppler/searchlight in §7.5) for direct + indirect
    illumination from emissive geometry.
  - The miss program's `env_color * env_intensity`
    fallback (§6.4) for the environment radiance.
- **The upload is forward-compatible.** The host already
  uploads lights via `GpuScene::upload_lights`; the
  device-side `Light` array is in place for NEE to
  consume. The host populates
  `optixLaunchParams.lights` / `light_count` per launch
  (a single device-pointer write, costs nothing). When
  NEE activates in 12C+, no new upload path is needed —
  the data is already there.

The honest framing matters: Stage 12B does not silently
drop scene lights, nor does it pretend lights are
contributing when they aren't. The CLI render summary
(`run_render_pathtrace` in `main.cpp`) already reports
the uploaded light count alongside the sphere /
material / mesh counts, so the operator sees that lights
were parsed and uploaded even though they aren't
directly contributing to the radiance estimate.

### 13.4 Environment light → env-fallback bridge

The `Environment` light type is the special case worth
documenting in detail because it does — through a
host-side step — flow into the path tracer's radiance
output today, despite §13.3's "uploaded but not sampled"
caveat.

The flow:

```
scene.lights[*]  (some of which may have type == Environment)
        │
        │  host scan at scene-load time
        │  (proposed; not yet shipped in 11C handler)
        ▼
PathTraceConfig.environment_color
PathTraceConfig.environment_intensity
        │
        │  PathTracer::render writes them into
        ▼
optixLaunchParams.env_color
optixLaunchParams.env_intensity
        │
        │  miss program reads, applies §6.4 Doppler/searchlight
        ▼
miss-radiance contribution → raygen accumulator (per §6.3)
```

The host-side scan picks the first `Environment` light
in `scene.lights` and copies its `color * intensity` into
the `PathTraceConfig`. If no environment light is
authored, `PathTraceConfig`'s defaults apply (the moderate
cool sky tint Stage 11C set). The kernel reads
`env_color * env_intensity`, not the lights array — so
even when the scene lights are "not sampled", the
*environment one* still routes its data to the miss
program through the explicit env-fallback channel.

The Stage 11C `--render-pathtrace` handler does NOT
implement this scan today; it uses the
`PathTraceConfig` defaults verbatim. Activating the scan
is a small host-side change documented as a deferred
follow-up in the Stage 11C BUILD_PLAN entry. §13.4 is
the design contract that change will implement against.

#### 13.4.1 Future: HDR env-map textures

The bounded `env_color * env_intensity` model maps
unidirectional sky tint to a single radiance value per
miss. Stage 12B's environment-light plumbing is
adequate for this. When the texture system (master
order #18) lands, the env light grows:

- A device-side `cudaTextureObject_t` field on `Light`
  (or a separate parallel array indexed by
  `LightType::Environment` light id) carrying the HDR
  panoramic environment map.
- The miss program reads `optixGetWorldRayDirection()`,
  decodes a (longitude, latitude) UV against the env
  map, samples the texture, multiplies by `intensity`,
  applies §6.4 Doppler/searchlight as today, writes
  the result to the payload.
- The current `env_color` field becomes the fallback
  when no env-map texture is bound (preserves Stage 12B
  behaviour for env-mapless scenes).

The miss program's pure `(direction, launch_params) →
radiance` shape (§6.3) extends to env-maps without
restructuring; the only change is the env_color load
becoming a textured sample.

### 13.5 Where lights are evaluated: closest-hit (NEE, future)

When NEE activates in 12C+ (per §7.8 / §8.3 / §9.5.1's
shadow-ray expansion), light evaluation lives primarily
in the **closest-hit program**, with help from the
raygen and the any-hit shadow program:

| Step                                 | Program | Stage 12C+ flow                                                                  |
|--------------------------------------|---------|----------------------------------------------------------------------------------|
| Pick a light to sample               | raygen  | uniform sample over `[0, light_count)`; passed to CH via payload or recomputed   |
| Generate shadow ray (origin + dir)   | CH      | `origin = hit.position + N·ε`; `direction = light.position - origin` (point/area) or `-light.direction` (directional) |
| Evaluate visibility                  | AH (shadow ray-type) | calls `optixTerminateRay()` on first hit; payload visibility bit cleared if occluded |
| BRDF × cos / pdf                     | CH      | `albedo / π` for Lambert; cos(N, L); 1/d² for point lights; PDF = uniform-light selection × per-light delta |
| Doppler / searchlight modulation     | CH      | applied to the light's `color * intensity` exactly like emission in §7.5; same helpers, same gating |
| Write contribution to payload        | CH      | added to `payload.emission` slots; raygen multiplies by throughput               |

The shadow-ray expansion grows the SBT to 7 records
(per §9.5.1) and adds the AH program (per §8.3 step 1).
The closest-hit program grows §7.4's evaluation step
with a new "direct lighting" block evaluated *before*
the cos-hemisphere bounce sample (so the throughput
update on the next bounce reflects the BRDF × albedo
already, and direct lighting is added to radiance
without double-multiplying).

#### 13.5.1 Per-light-type evaluation specifics

- **Point light**: shadow ray from `hit.position` toward
  `light.position`; `t_max = length(light.position -
  hit.position)`. Contribution =
  `(albedo / π) * cos(N, L) * (color * intensity) / d²`
  modulated by Doppler. Visibility = AH-determined.
- **Directional light**: shadow ray from `hit.position`
  toward `-light.direction`; `t_max = inf`. Contribution =
  `(albedo / π) * max(0, N · -direction) * color * intensity`
  modulated by Doppler. Visibility = AH-determined.
- **Area light**: stochastic sample of a point on the
  rectangle (using `pathtracer::Rng`); shadow ray to
  the sampled point; PDF = `1 / (area_width * area_height)`
  divided by the cos / r² Jacobian of the area-to-solid-
  angle mapping. Stage 12C+ marks this as PLACEHOLDER
  until the area-light sampling slice ships.
- **Environment light**: NOT sampled by NEE shadow rays
  in 12C+ — environment radiance arrives via the miss
  program when bounce rays escape the scene (§6 / §13.4).
  A future expansion (master order post-#18) could add
  env-map importance sampling to NEE for HDR env-maps,
  but the simple fallback shape stays through 12C.

### 13.6 Read / write summary

| Surface                                 | Direction                     | Lifetime        |
|-----------------------------------------|-------------------------------|-----------------|
| `Scene::lights` (host vector)           | host read/write               | persistent      |
| flat `Light[]` (host temp)              | host write at upload time     | per scene load  |
| `GpuScene::device_lights_` buffer       | host write at upload time     | per scene load  |
| `optixLaunchParams.lights` ptr          | host write per launch         | per launch      |
| `optixLaunchParams.light_count`         | host write per launch         | per launch      |
| `optixLaunchParams.lights[*]` (device read) | (Stage 12B: never)        | n/a in 12B      |
| `optixLaunchParams.lights[*]` (device read) | (Stage 12C+ NEE: per shadow ray) | per NEE sample |
| `optixLaunchParams.env_color/env_intensity` (Stage 12B miss) | device read | per miss        |

The Stage 12B reads from `lights[*]` are deliberately
absent — no program in Stage 12B's pipeline dereferences
the lights array. The data is uploaded for forward
compatibility; the consumers join in 12C+.

### 13.7 Scope: what's NOT in §13

Three light-related concerns this section deliberately
defers:

- **NEE direct-light sampling.** Per §7.8 / §8.3 / §9.5.1
  / §13.5: the shadow-ray expansion adds a second ray
  type, an AH program, and a new direct-lighting block
  in the closest-hit. This is a substantial slice of
  its own; §13 documents the contract the slice
  implements against. Stage 12C+.
- **HDR env-map textures.** Per §6.6 / §13.4.1: needs
  the texture system (master order #18). The miss
  program's pure-function shape extends to env-maps
  without restructuring.
- **Multiple importance sampling (MIS).** When NEE +
  BSDF sampling both contribute to the same pixel, MIS
  weights combine them with reduced variance. Stage
  12C+ NEE is *direct-lighting-only*; MIS lands when
  non-Lambert BSDFs make the BSDF sampling alternative
  meaningful (post master order #13).

The Stage 12B light data flow — parser →
GpuScene::upload_lights → launch-params pointer +
optional environment-light → env-fallback bridge — is a
stable foundation that the NEE slice grows on top of.
Lights are uploaded today; lit images through them join
when the rest of the pipeline (shadow rays, MIS,
non-Lambert BSDFs) catches up.

---

## 14. Relativity parameter data

The relativity parameters are the smallest scene data
surface (28 bytes total) but the most distinctive — they
are what makes RelativityRender different from a textbook
GPU path tracer. §14 consolidates the relativity routing
that §5 / §6 / §7 / §13 each touched on from one program's
viewpoint into a single data-flow section, with explicit
field semantics, per-program application sites, and the
"where applied" answer the user's bullet asks for
(raygen-for-direction vs shading-for-radiance).

### 14.1 Source

Relativity parameters flow from host to device through
the existing parser → Scene → upload chain, identical in
shape to materials (§12.1) and lights (§13.1):

```
.rrscene file ("relativity" object)
              │
              │  Stage 10B.4 parser (apply_relativity)
              │  - canonical observer_velocity Vec3 OR
              │    betaVelocity + velocityDirection shorthand
              │    are both accepted; parser composes them into
              │    a single Vec3
              ▼
rr::scene::Scene::observer  (Observer POD)
rr::scene::Scene::relativity  (RelativityParams POD)
              │  GpuScene::upload_relativity(observer, params)
              │  - host snapshot only; no device buffer needed
              ▼
GpuScene's observer_ + params_ host members
              │  GpuScene::observer() / .params() return-by-value
              ▼
optixLaunchParams.observer   (28 B host POD copy)
optixLaunchParams.params     (16 B host POD copy)
              │  read by raygen / miss / CH on every program invocation
              ▼
Observer + RelativityParams (consumed by the relativity helpers
                             in relativity/RelativityMath.h)
```

Two notes on the path:

1. **Authoring shorthands resolve at parse-time.** The
   `.rrscene` format's §6.1 shorthand (`betaVelocity` +
   `velocityDirection` as a polar-form alternative to the
   canonical `observer_velocity` Vec3, plus
   `aberrationStrength` / `dopplerStrength` /
   `searchlightStrength` aliases) is handled entirely by
   the Stage 10B.4 parser. By the time the data reaches
   `Scene::observer` / `Scene::relativity` it is in the
   canonical POD form. The OptiX backend does not see the
   shorthand and does not need to.
2. **No device buffer for the PODs.** Unlike materials /
   lights / spheres / mesh data (which need
   `GpuBuffer<T>` allocations because they are arrays),
   the observer + params are scalar PODs that fit
   trivially in `optixLaunchParams`'s constant-memory
   bind. `GpuScene::upload_relativity` is host-only —
   it copies the snapshot into the GpuScene's host
   members; no device upload happens until the per-launch
   `optixLaunchParams` cudaMemcpy fires.

### 14.2 Observer POD field list

The Observer POD lives in `relativity/RelativityParams.h`:

| Field      | Type | Size | Purpose                                                                   |
|------------|------|-----:|---------------------------------------------------------------------------|
| `velocity` | Vec3 | 12 B | 3-velocity in c-units (each component `β = v / c`); `length(velocity) < 1` invariant |

Total: **12 bytes** per observer.

The parser produces a single Vec3 regardless of the
authoring form. The user's "betaVelocity,
velocityDirection" bullet refers to the §6.1 polar-form
shorthand; the canonical Vec3 is what the device-side
helpers consume.

### 14.3 RelativityParams POD field list

The RelativityParams POD lives in the same header:

| Field                    | Type  | Size | Purpose                                                                      |
|--------------------------|-------|-----:|------------------------------------------------------------------------------|
| `enable_aberration`      | bool  |  1 B | Master gate on primary-ray aberration (raygen)                              |
| `enable_doppler`         | bool  |  1 B | Master gate on Doppler colour shift (miss + CH; future CH for NEE)          |
| `enable_searchlight`     | bool  |  1 B | Master gate on relativistic beaming (miss + CH; future CH for NEE)          |
| `doppler_color_strength` | float |  4 B | Scalar in `[0, ∞)` mixing identity ↔ full Doppler shift                     |
| `searchlight_strength`   | float |  4 B | Scalar in `[0, ∞)` mixing identity ↔ full intensity boost                   |
| `max_beta`               | float |  4 B | Cap on `length(velocity)` so `γ` stays finite (parser-validated `< 1`)      |

Total (with C++ padding to align the floats): **16 bytes**
per params POD.

The user's "aberration, doppler, searchlight strengths"
bullet maps to:

- **Aberration**: `enable_aberration` only (boolean).
  The §6.4.1 design choice intentionally collapsed
  Stage 10B.4's `aberrationStrength` shorthand onto the
  boolean as a `> 0` gate — there is no
  `aberration_strength` float in the POD because the
  current relativity helpers (`aberrateDirection` in
  `relativity/RelativityMath.h`) do not take a strength
  parameter. Activating fractional aberration is a
  future relativity-helper change, not a POD-layout
  change today.
- **Doppler**: `enable_doppler` (gate) +
  `doppler_color_strength` (intensity blend).
  `applyDopplerColor(color, D, strength)` lerps between
  identity (strength = 0, no shift) and the full
  shift (strength = 1).
- **Searchlight**: `enable_searchlight` (gate) +
  `searchlight_strength` (intensity blend).
  `searchlightFactor(D, strength)` lerps between identity
  (strength = 0) and the full beaming factor.

### 14.4 Where applied: raygen for direction, shading for radiance

This is the central question §14 answers. The relativistic
modulation splits into two complementary kinds of effect,
applied at different program sites:

| Effect          | Acts on        | Program site(s)                            | Subsection refs              |
|-----------------|----------------|--------------------------------------------|------------------------------|
| **Aberration**  | ray direction  | raygen (primary ray only)                  | §5.5                         |
| **Doppler colour** | radiance     | miss (env contribution) + CH (emission); future CH (direct lighting via NEE) | §6.4 + §7.5 + §13.5 |
| **Searchlight** | radiance       | miss (env contribution) + CH (emission); future CH (direct lighting via NEE) | §6.4 + §7.5 + §13.5 |

The split is physical: aberration is a Lorentz
transformation of *directions* (changes which photons
the observer sees), Doppler + searchlight are
transformations of *radiance* (changes how those photons
are seen). Aberration belongs at the ray's *origin*
(raygen, where the primary ray's direction is generated);
Doppler + searchlight belong at every *radiance source*
(miss for env radiance, CH for emission, future CH for
direct light contributions).

#### 14.4.1 Aberration — raygen, primary ray only

Per §5.5 + §6.4.1, Stage 12B applies `aberrateDirection`
to the **primary ray's direction only**. Bounce rays use
the world-frame direction returned by
`pathtracer::sample_cosine_hemisphere` against the hit
normal; they do not re-enter the observer's frame.
Pseudocode in raygen:

```cpp
rr::camera::CameraRay ray = generate_primary_ray(
    optixLaunchParams.camera, x, y, width, height,
    jitter.x, jitter.y);

if (optixLaunchParams.params.enable_aberration) {
    ray.direction = rr::relativity::aberrateDirection(
        optixLaunchParams.observer.velocity,
        ray.direction);
}

// ... bounce loop with optixTrace ...
```

Bounce rays *do not* call `aberrateDirection` (per the
§6.4.1 deliberate-choice rationale: bounces are
world-frame photon-walks; re-entering the observer's
frame on every bounce has no physical justification).

#### 14.4.2 Doppler + searchlight — miss + closest-hit

Per §6.4 (miss / env) + §7.5 (CH / emission), the
Doppler + searchlight modulation runs on *every* miss
and *every* emission-bearing CH invocation. Both sites
use the same RR_HD helpers in the same canonical order
(colour-shift first, then intensity-scale), each gated
on its `params.enable_*` toggle:

```cpp
const Vec3& v   = optixLaunchParams.observer.velocity;
const Vec3  dir = optixGetWorldRayDirection();

// `radiance` is env_color * env_intensity (miss)
// or  m.emissionColor * m.emissionStrength (CH).

if (params.enable_doppler || params.enable_searchlight) {
    const float D = rr::relativity::dopplerFactor(v, dir);
    if (params.enable_doppler) {
        radiance = rr::relativity::applyDopplerColor(
            radiance, D, params.doppler_color_strength);
    }
    if (params.enable_searchlight) {
        radiance = radiance * rr::relativity::searchlightFactor(
            D, params.searchlight_strength);
    }
}
```

The "every miss / every emission hit" stance was
explained in §6.4.1 — Stage 12B picks the simplest
"every ray observer-frame" model, accepting the small
physical inaccuracy that bounce-ray misses also get
Doppler-modulated. Future stages can refine via an
`is_primary` payload bit (per §6.4.1's option 2) if
artifacts surface.

#### 14.4.3 Future: NEE direct lighting

When NEE activates in 12C+ (per §13.5), the closest-hit
program's direct-lighting block applies the same Doppler
+ searchlight modulation to the sampled light's
contribution before adding it to the payload's emission
slots. The shape is identical to §14.4.2's emission
modulation; the only difference is the radiance source
(`light.color * light.intensity / d²` instead of
`m.emissionColor * m.emissionStrength`). No new
relativity helpers; same gating; same canonical order.

### 14.5 Routing: launch params, not SBT

Same routing decision as the camera (§11.4):
**`optixLaunchParams.observer` and
`optixLaunchParams.params` live in constant memory; no
SBT records carry relativity data.** The §9.3 general
rule applies; the relativity-specific recap:

1. **Per-launch mutability.** The observer's velocity
   changes when the user moves the observer (interactive
   relativistic-perception editing); the
   `enable_*` toggles change when the artist switches
   effects on/off. Encoding either into SBT records
   would force an SBT rebuild on every such edit.
2. **Tiny size.** 12 B observer + 16 B params = 28 B
   total. Trivially broadcast through constant memory;
   reading from the same bound symbol from raygen, miss,
   and CH costs nothing per invocation.
3. **Program-agnostic shared state.** Raygen reads
   `velocity` for primary aberration; miss reads
   `velocity` + `params` for env Doppler/searchlight;
   CH reads the same for emission Doppler/searchlight.
   Future NEE CH adds direct-lighting Doppler. All four
   sites read the same authoritative copy from
   constant memory.

### 14.6 Read / write summary

| Surface                                | Direction                  | Lifetime           |
|----------------------------------------|----------------------------|--------------------|
| `Scene::observer` (host)               | host read/write            | persistent         |
| `Scene::relativity` (host)             | host read/write            | persistent         |
| `GpuScene::observer_ / params_` (host) | host write at upload time  | per scene load     |
| `optixLaunchParams.observer`           | host write per launch      | per launch         |
| `optixLaunchParams.params`             | host write per launch      | per launch         |
| `optixLaunchParams.observer.velocity` (device read) | device read (raygen, miss, CH) | per program invocation |
| `optixLaunchParams.params.*` (device read) | device read (raygen, miss, CH) | per program invocation |

Like materials and camera, the relativity surface is
read-only on the device side. Per-launch host updates
(observer position, toggle changes) are a single
launch-params re-bind with no SBT/AS/pipeline rebuild.

### 14.7 Scope: what's NOT in §14

Three relativity-related concerns this section
deliberately defers:

- **Float-valued aberration strength.** The current
  POD has only `enable_aberration` (boolean), not an
  `aberration_strength` float. §10B.4's
  `aberrationStrength` shorthand collapses to the
  boolean. A future slice can add the float field +
  extend `aberrateDirection` to take a strength
  parameter (lerping between identity and full
  aberration); the POD layout grows by 4 B; the
  routing stays the same.
- **Bounce-ray relativistic effects.** §6.4.1
  documented Stage 12B's choice to apply Doppler on
  every miss (including bounce-ray misses). Aberration
  is intentionally primary-only. A future "physically
  accurate relativity" mode could split the path into
  a primary world-frame portion + a secondary observer-
  frame transformation, but that is a substantial
  research-grade slice beyond Stage 12B's scope.
- **Time-variant observers.** Motion blur with a
  moving observer (the observer accelerates during
  the shutter window) requires multi-key observer
  states and per-primary-ray time sampling. Tied
  to the §10.7 motion-blur slice; activates the
  observer's velocity into a multi-key array.

The Stage 12B relativity data flow — parser →
GpuScene::upload_relativity → launch-params PODs →
raygen/miss/CH per-invocation reads — is a stable
foundation. The 28 bytes carry RelativityRender's
unique-selling-point physics through the OptiX
boundary unchanged.

---

## 15. Launch parameters

§5 - §14 each documented one slice of the OptiX backend
from one perspective (program / data-category). §15 is the
consolidating capstone: it inventories *everything* that
travels into the OptiX runtime per launch (`optixLaunch`),
*everything* that lives in the Shader Binding Table, and
states the general routing rule that §11.4 / §12.5 /
§13.6 / §14.5 have each been instantiating from their own
data category's viewpoint.

The user prompt frames this as "what goes in launch params
(camera, relativity, frame buffers)" vs "what goes in SBT
(materials, per-object data)". §15 answers honestly: in
Stage 12B *everything* goes through launch params; SBT
records carry program-group identifiers and nothing else.
The user's "what goes in SBT" framing is the **future**
state — the per-record materials / per-mesh metadata
roadmap covered in §9.2, §9.5, §12.3. §15.4 documents
when each data category crosses the complexity threshold
that makes SBT-data routing worth its overhead.

### 15.1 Stage 12B launch-params inventory

The per-launch state lives in a single
`optixLaunchParams` POD bound to a fixed device symbol at
pipeline link time. Stage 12B's planned layout (consolidating
§5.2.1 / §11.2 / §12.5 / §13.6 / §14.5):

```cpp
struct OptixLaunchParams {
    // ----- Output framebuffer (§5.3.2) -----
    float*  sample_pixels;     // device pointer; w*h*4 floats Rgba32F
    int     width;             // §11.3
    int     height;            // §11.3

    // ----- Per-launch sampling state (§5.2.1, §11.3) -----
    unsigned int seed;         // RNG global seed
    unsigned int sample_index; // changes between launches
    int          max_bounces;  // bounce-loop budget

    // ----- Acceleration structure root (§10.1) -----
    OptixTraversableHandle scene_handle;  // IAS handle for optixTrace

    // ----- Geometry arrays (§9.2 launch-params route, §10.4 identity transforms) -----
    const rr::geometry::Sphere*   spheres;       // device pointer
    int                           sphere_count;
    rr::cuda::CudaMeshView        mesh;          // single-mesh slot today

    // ----- Materials (§12.3 launch-params route) -----
    const rr::material::MaterialParams* materials;
    int                                 material_count;

    // ----- Lights (§13.1; uploaded but not sampled in 12B per §13.3) -----
    const rr::lighting::Light*  lights;
    int                         light_count;

    // ----- Camera (§11.2) -----
    rr::camera::GpuCamera       camera;          // 56 B by value

    // ----- Relativity (§14.2 + §14.3) -----
    rr::relativity::Observer            observer;  // 12 B by value
    rr::relativity::RelativityParams    params;    // 16 B by value

    // ----- Environment fallback (§6.4) -----
    rr::math::Vec3              env_color;
    float                       env_intensity;
};
```

Approximate total size:

| Group                        | Size estimate |
|------------------------------|--------------:|
| Output buffer pointer + dims | 16 B          |
| Sampling state               | 12 B          |
| AS handle                    |  8 B          |
| Sphere array (ptr + count)   | 16 B          |
| Mesh view (CudaMeshView)     | ~40 B         |
| Materials (ptr + count)      | 16 B          |
| Lights (ptr + count)         | 16 B          |
| Camera POD                   | 56 B          |
| Observer POD                 | 12 B          |
| RelativityParams POD         | 16 B          |
| Env fallback                 | 16 B          |
| **Total** (approximate)      | **~224 B**    |

Well under any practical constant-memory budget; the
launch-params upload is a single ~250-byte cudaMemcpy
per `optixLaunch`. Even on a slow-PCIe host the upload
cost is negligible compared to the kernel launch
itself.

### 15.2 Stage 12B SBT data inventory

The Shader Binding Table (§9.4) contains exactly four
records totalling 128 bytes:

| Record         | Index | Program identifier (32 B) | User data | Total |
|----------------|------:|---------------------------|-----------|------:|
| raygenRecord   |     0 | `pathtrace_raygen`        | (empty)   | 32 B  |
| missRecord     |     0 | `pathtrace_miss`          | (empty)   | 32 B  |
| hitgroupRecord |     0 | `sphere_hitgroup` (CH)    | (empty)   | 32 B  |
| hitgroupRecord |     1 | `triangle_hitgroup` (CH)  | (empty)   | 32 B  |

Stage 12B's SBT records carry **only the program-group
identifiers** that OptiX needs to dispatch into the right
code at trace time. There is no per-record user-data —
no per-mesh material parameters, no per-instance
transforms, no per-primitive opacity threshold.
`optixGetSbtDataPointer()` from any program returns a
pointer to the empty user-data slot, which the programs
do not dereference.

### 15.3 Separation rationale

The general routing rule that emerges from Stages §11 -
§14:

| Lives in...        | When the data is...                                                                                       |
|--------------------|-----------------------------------------------------------------------------------------------------------|
| **Launch params**  | per-launch mutable (camera moves, observer changes, sample_index advances), small enough to broadcast (≤ few KB), shared across multiple program types (raygen + miss + CH all read it) |
| **SBT user-data**  | per-pipeline / per-scene stable (changes only when the SBT itself rebuilds), per-record specific (different HitGroup records carry different data), large enough to make launch-params bloat costly, per-primitive metadata that maps cleanly onto HitGroup-record granularity |

The two surfaces are non-overlapping by construction:

- **Launch params** are read once per program invocation
  via the constant-memory bind. Every thread reads the
  same data; cache-friendly broadcast.
- **SBT user-data** is read once per program invocation
  via `optixGetSbtDataPointer()`. Each thread *might*
  read a different record (different primitive index →
  different HitGroup record); cache-friendly per-record
  locality if many primitives bind to the same record.

#### 15.3.1 Why Stage 12B picks launch-params for everything

Three reasons §11.4 / §12.3 / §13.4 / §14.5 each cite,
collected here:

1. **Per-launch mutability dominates.** Stage 12B's
   workloads are interactive editing + spp loops. The
   camera / observer / spp index / scene contents all
   shift between launches. Routing through SBT records
   would force an SBT rebuild on most edits, defeating
   the SBT's caching purpose.
2. **Small data sizes.** ~224 B launch params + 128 B
   SBT = ~350 B total per-launch device-side state.
   Launch-params arrays carrying small material/sphere/
   mesh counts (~few KB each in the pessimistic case)
   stay below the cache pressure point where SBT-data
   locality starts to matter.
3. **CUDA-backend parity.** The CUDA path tracer
   (Stage 11C) reads materials / spheres / mesh / lights
   from the same `GpuScene::device_*()` accessors via
   `CudaSceneView`. The OptiX backend reading the same
   arrays through launch params means a single source
   of truth across both backends — fixtures render
   identically (modulo the AS traversal speedup, which
   is the point of the migration).

#### 15.3.2 What the SBT-route would gain

When per-mesh / per-material counts grow to thousands,
the SBT-route gains:

- **Per-record cache locality.** OptiX guarantees the
  HitGroup record currently dispatching is hot in the
  L1 instruction cache; per-record user-data piggybacks
  on that locality.
- **No launch-params bloat.** If material count climbs
  to 10,000+, `materials` as a launch-params array
  pointer with hit-time index lookup still works, but
  the working-set across all primitive hits becomes
  large enough that per-record locality starts to
  matter. SBT user-data avoids the working-set issue by
  putting the relevant material data in the same
  record OptiX is already loading.
- **Per-record specialisation.** Different HitGroup
  records can carry different *types* of data — a
  velvet-fabric BSDF record carries different
  parameters than a metal record. Stage 12B's "every
  hit reads the same MaterialParams POD" model does
  not need this; future BSDF dispatch (master order #13)
  does.

### 15.4 Migration paths to SBT user-data

The roadmap for moving data categories from launch-params
to SBT user-data, in dependency order:

| Data category    | Migration trigger                                | Document ref         |
|------------------|--------------------------------------------------|----------------------|
| Per-mesh metadata | Multi-mesh upload on `GpuScene` activates       | §9.5.2               |
| Per-material BSDF data | BSDF dispatch (master order #13)            | §12.4 / §12.7        |
| Per-instance transforms | When per-mesh transforms activate (§10.4)  | §10.4                |
| Per-record opacity / alpha | Alpha-test cutout (master order #18)    | §8.3 step 2          |
| Per-record callable BSDF programs | When callable programs land (master #13) | §9.8       |

Each of these is **additive** against §15.1's launch-
params layout — moving per-mesh metadata into SBT
user-data does not require *removing* the per-mesh
launch-params arrays in lockstep; the launch-params
arrays can stay as the canonical source for fallback
indices while the SBT user-data carries the hot per-hit
copy. The migrations roll out one data category at a
time as their consuming features ship.

#### 15.4.1 The non-migration: launch-params permanent residents

Some data categories belong in launch-params permanently,
regardless of scene scale:

- **Camera POD** — per-launch mutable; tiny; shared across
  programs (§11.4). No SBT-route makes sense.
- **Observer + RelativityParams** — same shape as camera:
  per-launch mutable, tiny, program-agnostic (§14.5).
- **Output framebuffer pointer + dimensions** — the
  raygen needs the destination of its per-pixel write
  available before the per-launch dispatch even starts;
  this is launch-params territory by definition.
- **Per-launch sampling state** — `seed`,
  `sample_index`, `max_bounces`. The spp loop's
  iteration counter mutates every launch.
- **AS root handle** — the `OptixTraversableHandle` is
  the entry point for `optixTrace`; it cannot be in the
  SBT (the SBT is *inside* what the AS dispatches into).
- **Environment fallback** (`env_color` /
  `env_intensity`) — per-launch mutable (interactive
  scene editing), tiny, single-consumer (miss program).

Stage 12B's "everything in launch-params" picks the
right answer for *the current scene scale*. The
migration roadmap's data categories above are the ones
that change posture as scenes scale; the §15.4.1
residents stay in launch params indefinitely.

### 15.5 Read / write summary

The launch-params surface from a host's perspective
across one render:

| Operation                              | Cost                          | Frequency               |
|----------------------------------------|-------------------------------|-------------------------|
| Allocate launch-params device buffer   | one cudaMalloc (~few µs)      | once at pipeline build  |
| Populate optixLaunchParams struct      | host-side struct writes (~ns) | per launch              |
| `cudaMemcpy(d_launch_params, ...)`     | ~250 B H2D (~ns - few µs)     | per launch              |
| `optixLaunch(...)`                     | kernel-launch overhead        | per launch              |
| Free launch-params buffer              | one cudaFree                  | once at pipeline destroy |

The launch-params upload is in the noise compared to the
optixLaunch itself for any non-trivial render. Stage 12B
spp loops typically run hundreds of `optixLaunch`s per
second on a modern GPU; the launch-params upload adds
microseconds of overhead per launch, swallowed by the
kernel time.

### 15.6 Scope: what's NOT in §15

Two related concerns this section deliberately defers:

- **Pipeline configuration.** `OptixPipelineCompileOptions`
  / `OptixPipelineLinkOptions` / `OptixModuleCompileOptions`
  control how the OptiX pipeline is built (register
  pressure, debug-info inclusion, payload register count,
  etc.). These are pipeline-build parameters, not
  per-launch state. The "Planned module / file layout"
  future sub-stage covers them when documenting the
  host-side OptiX setup code.
- **Stream-level parallelism.** OptiX 7+ supports
  per-stream `optixLaunch` calls for concurrent
  multi-frame rendering or progressive preview while a
  full render is in flight. Stage 12B uses a single
  default stream; multi-stream is a future
  interactive-viewer slice.

The §15 launch-params + SBT inventory is the data-side
mental model for the migration. Combined with §5-§9's
program-side model, it gives an implementer the full
contract for the Stage 12B OptiX backend without
needing to read the rest of the doc front-to-back.

---

## 16. Migration from CUDA renderer

§5 - §15 documented the OptiX backend's design as if the
project were starting from scratch. §16 reverses the
viewpoint: the project is *not* starting from scratch.
The Stage 11C CUDA path tracer is a complete, working
renderer that produces correct images today (modulo the
build-host CUDA-toolchain caveat). §16 documents how the
OptiX migration coexists with that CUDA path —
deliberately a parallel track, not an in-place rewrite —
so the existing renderer keeps working through every
sub-slice of the migration and can serve as the
correctness reference / production fallback after the
OptiX backend ships.

### 16.1 Parallel migration: CUDA path stays as reference + fallback

The OptiX backend is added **alongside** the CUDA backend,
not as a replacement. Both backends remain compiled into
the same executable when CUDA + OptiX are both available;
the user picks one per render via a CLI flag.

The CUDA path tracer's role splits into two after the
OptiX backend ships:

- **Reference**. The CUDA backend stays the
  correctness baseline. Image regression tests render
  the same fixture through both backends and diff the
  resulting PPMs; differences should be within
  Monte-Carlo noise (a few percent per pixel for low
  spp counts, vanishing as spp climbs). Stage 11C's
  `pathtracer_tests` (host-side RNG / Sampling
  invariants) keeps working unchanged; the same RNG
  primitives drive both backends, so any device-side
  divergence isolates to either the kernel-launch
  path (CUDA `<<<...>>>` vs `optixLaunch`) or the
  closest-hit semantics (linear loop vs BVH).
- **Fallback**. The CUDA backend works on hosts where
  OptiX is unavailable: pre-RTX hardware (compute
  capability < 7.5), build hosts without the OptiX
  SDK installed, environments where the OptiX 7+
  driver-side support is missing. `--render-pathtrace`
  continues to work in these cases, against the CUDA
  path tracer; only `--render-pathtrace-optix` (or
  whatever the new flag is named) requires OptiX.

This dual-role structure is why §3.5 of this document
called CUDA "the project's correctness reference" and
OptiX "the project's performance backend". Neither
displaces the other.

### 16.2 Geometry-by-geometry stepwise replacement

The user's "Start with triangles via OptiX, keep spheres
initially in CUDA or convert later" bullet describes a
staged geometry replacement. Three possible phases:

| Phase | Triangle hits | Sphere hits | Notes                                                                 |
|-------|---------------|-------------|-----------------------------------------------------------------------|
| 1     | OptiX BVH     | CUDA linear loop | Hybrid intermediate; the kernel runs OptiX trace for triangles, then a CUDA-style sphere loop for sphere candidates. Useful only as a debugging shape if sphere IS turns out to be tricky to integrate. |
| 2     | OptiX BVH     | OptiX BVH (built-in sphere primitive in 7.5+) | The Stage 12B target. Single `optixTrace` per ray covers both primitive types via the IAS (§10.1).                       |
| 3     | OptiX BVH     | OptiX BVH   | The CUDA path tracer's `intersect_sphere` / `intersect_triangle` helpers become regression-only - reference code that the CUDA backend keeps but the OptiX backend never calls. |

Stage 12B targets **phase 2 directly**; phase 1 is
documented as an option if implementation surfaces an
unexpected obstacle to sphere-IS integration. The
built-in sphere primitive in OptiX 7.5+ makes phase 1's
hybrid posture unnecessary in the common case — sphere
GAS construction is the same shape as triangle GAS
construction (per §10.2).

The "convert later" framing is the natural fallback if
sphere-IS-via-OptiX hits a snag: ship Phase 1 first
(triangle GAS via OptiX, sphere intersections still in
the OptiX raygen via the existing
`rr::cuda::intersect_sphere` helper called per
sphere-array entry inside the closest-hit / raygen),
then add the sphere GAS in Stage 12B's second slice. The
RR_HD inline `intersect_sphere` already runs on both
host and device, so calling it from inside an OptiX
program is mechanically straightforward.

### 16.3 Stepwise replacement: intersections → shading → path tracing

The user's "Stepwise replacement (intersections →
shading → path tracing)" bullet describes a layered
activation order through the OptiX program model. Three
layers, each a possible debugging endpoint:

#### Step A — Intersections only (debug shape, not shipped)

OptiX `optixTrace` computes the closest hit; the closest-
hit program writes minimal hit data (t, world position,
world normal) into the payload; the raygen does
*nothing else* — it consumes the payload and writes
`(N · 0.5 + 0.5)` as RGB to the framebuffer. Equivalent
to the Stage 6B `--render-sphere` diagnostic, but with
the OptiX BVH as the traversal layer.

**Purpose**: validate the OptiX pipeline + AS build +
SBT layout end-to-end before any shading complexity is
in the loop. If this image is wrong, the AS build is
wrong; the rest of the renderer's complexity is not in
play.

**Status**: not a shipping endpoint. A useful shape for
the implementer's first compilable OptiX runs; not part
of the Stage 12B CLI surface.

#### Step B — Direct shading (no bounces, no accumulation)

The CH program writes Lambert albedo + emission into the
payload; the miss program writes the env fallback (with
Doppler/searchlight per §6.4); the raygen integrates one
hit per pixel:

```
radiance = throughput * emission   (CH path)
        + env_color * env_intensity  (miss path)
```

No bounce loop, no accumulation buffer, no spp loop. One
sample per pixel, one trace per pixel. Equivalent in
output shape to Stage 9B's `k_render_scene` shading
output but driven through OptiX programs instead of a
CUDA kernel.

**Purpose**: validate the §6 / §7 / §14 program-level
shading logic + Doppler/searchlight integration before
adding the bounce loop's complexity.

**Status**: not a shipping endpoint either; a milestone
for the implementer to sanity-check the shading +
relativity contract before Step C.

#### Step C — Full path tracer (Stage 12B target)

The raygen runs the iterative bounce loop (§5.1), the
CH writes payload per §7.3's register layout, the miss
writes env per §6.3, the host-side `PathTracer::render`
spp loop accumulates samples through the existing Stage
11B `AccumulationBuffer`. End-to-end equivalent of Stage
11C's `--render-pathtrace`, but on top of the OptiX
program model.

**Status**: the Stage 12B shipping target. The CLI flag
`--render-pathtrace-optix` (or a similar name decided
when the implementation slice picks it) routes through
this path; the existing `--render-pathtrace` continues
to use the Stage 11C CUDA path.

The order matters: Step A validates the OptiX
infrastructure with no shading; Step B adds shading
without bouncing; Step C adds the bounce loop. Each
step's output is comparable to an existing CUDA
diagnostic, so the implementer can spot the regression
at the layer it is introduced rather than debugging
the whole pipeline at once.

### 16.4 No breaking of existing CLI / scene flow

The migration is strictly additive at every user-facing
surface. The contracts that stay the same:

- **`.rrscene` format.** The Stage 10B parser is the
  single source of truth for both backends. No
  format changes, no new section types, no new
  shorthand. Both backends consume `Scene::*` structs
  through `GpuScene::*()` accessors.
- **`GpuScene` upload API.** `upload_camera`,
  `upload_relativity`, `upload_spheres`,
  `upload_materials`, `upload_lights`, `upload_mesh`
  all work unchanged. The OptiX backend reads the same
  device pointers the CUDA backend does (per §9.2 /
  §10.2's zero-copy reuse).
- **CLI surface for existing actions.**
  `--render-pathtrace`, `--render-from-scene`,
  `--render-full-scene`, `--render-scene`,
  `--render-direct-lighting`, `--render-relativistic`,
  `--render-mesh-scene`, `--render-material-scene`,
  `--render-triangle`, `--render-rays`,
  `--render-sphere`, `--render-rng-test`,
  `--render-accumulation-test` all continue to work
  with byte-identical output. The migration adds new
  flags; it does not change the meaning of any
  existing one.
- **Existing tests.** `math_tests`, `image_tests`,
  `gpu_tests`, `pathtracer_tests` continue to pass
  unchanged. The migration adds OptiX-specific tests
  (when ctest infrastructure for OptiX lands —
  itself a future slice), but does not delete or
  rewrite existing ones.
- **Output paths.** Existing actions write to the same
  paths they did before. Stage 12B's
  `--render-pathtrace-optix` writes to a *new*
  output path (e.g., `output/pathtrace_optix_spp_*.ppm`)
  — distinct from the CUDA path's
  `output/pathtrace_spp_*.ppm` — so users can compare
  the two artefacts side by side without one
  overwriting the other.

The audit boundary: anywhere in the codebase that an
existing test, fixture, or CLI invocation produces a
specific output, the OptiX migration leaves that
production unchanged. The only diff a user sees after
the migration is the existence of new flags + new
output paths.

### 16.5 What "reference" means in practice

A regression test framework comparing CUDA and OptiX
outputs is the natural artefact of this migration's
"keep CUDA as reference" stance. The shape:

- Pick a fixture (e.g.,
  `scenes/test_full_scene.rrscene`).
- Render through the CUDA backend at high spp (1024+);
  call this the *reference frame*.
- Render through the OptiX backend at the same spp.
- Compute per-pixel L2 distance between the two
  Rgba32F frames; aggregate as RMS error.
- Pass threshold: a few percent (loose), tightening
  toward zero as both backends mature.

Stage 12B does NOT ship this framework; it documents
that the CUDA backend's regression-baseline value
*enables* it. The framework's implementation is its
own slice when the OptiX backend stabilises enough for
side-by-side comparisons to be meaningful.

The Stage 11 audit (`docs/STAGE_11_AUDIT.md`)
recommended running the four expected artefacts on a
CUDA host before advancing; that recommendation
extends here. The CUDA backend's outputs *are* the
reference; producing them is the prerequisite for
calling any OptiX output "correct".

### 16.6 What "fallback" means in practice

After the OptiX backend ships:

- **`--render-pathtrace <file>`** continues to work
  against the CUDA path tracer on every host where
  CUDA itself works (Compute Capability ≥ 7.5 for the
  Stage 11A/B/C kernels; pre-Turing hosts already get
  a "requires CUDA" error today).
- **`--render-pathtrace-optix <file>`** requires both
  CUDA *and* the OptiX runtime/SDK. On hosts without
  OptiX, the action returns a "requires OptiX"
  error structurally identical to the existing
  "requires CUDA" pattern — same Logger::error
  message shape, same exit code.
- The build system gates OptiX on a new
  `RR_ENABLE_OPTIX` CMake option (parallel to the
  existing `RR_ENABLE_CUDA`). When it is OFF (the
  default on non-NVIDIA build hosts), the OptiX
  source files do not compile and the
  `--render-pathtrace-optix` action returns
  "requires OptiX. Rebuild with -DRR_ENABLE_OPTIX=ON
  ...". This is the host-only build path's exact
  shape today for `--render-pathtrace`; the OptiX
  variant just adds a second axis.

The long-term picture (post Stage 12C / 12D, when the
OptiX backend has full feature parity with the CUDA
path tracer): the CUDA path may relegate to
reference-only status — kept compiled, kept tested,
but no longer the recommended user-facing path.
Stage 12B does not make that call. Both backends are
first-class through Stage 12C.

### 16.7 Scope: what's NOT in §16

Three migration-related concerns this section
deliberately defers to a future sub-stage covering
"Migration risks":

- **Toolchain compatibility matrix.** Which OptiX
  versions does the implementation target? OptiX 7.5+
  for the built-in sphere primitive (§10.2.1); OptiX
  7.6+ for the 32-payload-register layout (§7.3); OptiX
  8.x for the latest pipeline features. The matrix
  with driver / CUDA-toolkit / OS dependencies belongs
  in the migration-risks sub-stage.
- **Debug story.** OptiX programs are harder to debug
  than CUDA kernels: limited `printf` support inside
  some program types, no native breakpoints in
  closest-hit / any-hit, harder to bisect issues
  between AS build and program code. Migration-risks
  sub-stage covers this.
- **Build-system complexity.** OptiX programs compile
  via PTX or OptiXIR intermediates; the CMake
  integration (custom NVCC commands, embedded
  PTX-as-cpp-string, pipeline linkage) is non-trivial.
  Migration-risks sub-stage + the file-layout sub-
  stage together cover this.

§16's scope is the **strategy**: keep the CUDA path,
add OptiX alongside it, replace geometry by geometry
and layer by layer, never break the existing flow.
The risks of executing that strategy belong in their
own sub-stage. The Stage 12B implementation slice is
where strategy meets engineering reality; for now,
the strategy is documented + safe.

---

## 17. Path tracing integration

§17 is the consolidating capstone for the program-side
chapter (§5 - §9), analogous to §15's role for the
data-side chapter. It puts the complete OptiX path tracer
into a single linear narrative — what each program does
in sequence, how the OptiX boundary connects to the host-
side spp loop and the existing Stage 11B
`AccumulationBuffer`, and where relativity slots into the
per-pixel flow. An implementer should be able to read §17
end-to-end and have a complete mental model of the
Stage 12B path tracer without flipping back through the
prior sections.

### 17.1 Per-pixel flow at a glance

For each pixel, for each spp iteration, the renderer runs
this sequence (host-side and device-side roles
interleaved):

```
HOST (PathTracer::render in rr_renderer):

  for s = 0 .. samples_per_pixel - 1:
    optixLaunchParams.sample_index = s
    cudaMemcpy(d_launch_params, &optixLaunchParams, sizeof(...))
    optixLaunch(pipeline, stream, d_launch_params, sbt, w, h, 1)

    accum.accumulate_sample(sample_pixels.device_ptr())
                    │  (Stage 11B kernel:
                    │   acc[i] += sample[i])
                    ▼
                 (one frame folded into accumulator)

  img = accum.resolve_to_image()
                    │  (Stage 11B kernel: display = acc * 1/N
                    │   + cudaMemcpy D2H)
                    ▼
  img.save_ppm(out_path)


DEVICE (per optixLaunch, per pixel - the Stage 12B raygen):

  rng = make_pixel_rng(x, y, sample_index, seed)
  jitter = next_vec2(rng)
  ray = generate_primary_ray(camera, x, y, w, h, jitter.x, jitter.y)

  if params.enable_aberration:
    ray.direction = aberrateDirection(observer.velocity, ray.direction)

  throughput = (1, 1, 1)
  radiance   = (0, 0, 0)

  for bounce = 0 .. max_bounces - 1:
    optixTrace(scene_handle, ray, ..., DISABLE_ANYHIT, ..., payload)

    if payload.hit_flag == 0:                                 # MISS
      radiance += throughput * payload.emission               # env contribution
      break

    radiance += throughput * payload.emission                 # CH-emitted radiance
    if bounce + 1 >= max_bounces:
      break

    u2        = next_vec2(rng)
    local_dir = sample_cosine_hemisphere(u2)
    world_dir = align_to_normal(local_dir, payload.normal)
    throughput = throughput * payload.albedo                  # Lambert update
    ray.origin    = payload.position + payload.normal * 1e-4f
    ray.direction = world_dir

  sample_pixels[(y*w+x)*4 + 0..2] = radiance
  sample_pixels[(y*w+x)*4 + 3]    = 1.0
```

This is the complete Stage 12B flow on one diagram. The
rest of §17 zooms in on each role.

### 17.2 Raygen drives primary rays + the sample loop

(Per the user's first bullet; consolidates §5.)

The raygen program is the per-pixel kernel and the
**sole** orchestrator on the device side. It owns:

1. **RNG seeding** from `(x, y, sample_index, seed)` via
   `pathtracer::make_pixel_rng`. The seed advances per
   spp iteration; per-pixel decorrelation comes from
   the SplitMix64 mixer in `pathtracer::RNG.h`.
2. **Sub-pixel jitter** from `pathtracer::next_vec2`
   for anti-aliasing. The spp loop produces stratified-
   by-default AA without explicit per-pixel sample
   counts.
3. **Primary ray construction** via
   `generate_primary_ray` — same maths as
   `rr::camera::generate_camera_ray` but with the
   `+0.5` pixel-centre offset replaced by the random
   jitter.
4. **Primary aberration** (per §14.4.1): if
   `params.enable_aberration`, transform the primary
   direction into the observer's frame via
   `aberrateDirection`.
5. **Bounce loop control** — `for bounce in [0, max_bounces)`.
   Each iteration calls `optixTrace`, reads the payload,
   folds the contribution into the running radiance,
   updates throughput from the payload's albedo,
   samples the next bounce direction, advances the
   ray. The loop terminates on miss (env contribution
   + break) or budget exhaustion.
6. **Per-sample output write** to
   `optixLaunchParams.sample_pixels` at the pixel's
   offset.

The raygen does NOT own the host-side spp loop — that's
`PathTracer::render` in `rr_renderer`. But it owns
*everything else*. The OptiX boundary is between "raygen
is invoked once per `optixLaunch` per pixel" and
"PathTracer::render decides how many `optixLaunch`s to
issue".

### 17.3 Closest-hit performs BSDF + next-ray generation

(Per the user's second bullet; consolidates §7.)

When `optixTrace` resolves to a hit, the closest-hit
program populates the OptiX payload with everything the
raygen needs to evaluate the hit's contribution and
produce the next ray. The CH owns the *shading-time*
responsibilities; the raygen owns the *integration-time*
responsibilities. Together they "perform next-ray
generation" as a coordinated unit.

The split (per §7.1):

| Responsibility                                | Program | Why                                                  |
|-----------------------------------------------|---------|------------------------------------------------------|
| Hit-data extraction (t, position, normal)     | CH      | OptiX intrinsics live here                           |
| Material lookup via index → MaterialParams    | CH      | Indexed read; needs primitive metadata + materials   |
| Emission evaluation + Doppler/searchlight     | CH      | Emission radiance is hit-local; Doppler needs ray dir + observer.velocity |
| Throughput update (multiply by albedo)        | raygen  | Throughput is per-thread state; lives across bounces |
| Cosine-hemisphere direction sampling          | raygen  | Needs RNG state, which lives raygen-local            |
| Tangent → world rotation around hit normal    | raygen  | Same RNG-locality reason                             |
| Next ray construction (origin + direction)    | raygen  | Composed from hit data (CH-provided) + sampled dir   |

The CH provides the *raw materials* — emission radiance
(post-modulation), albedo, hit normal, hit position. The
raygen *assembles* the next ray from those raw materials
+ its own RNG-driven hemisphere sample.

This is why the user-prompt phrase "CH performs BSDF +
next-ray generation" is true at the *logical* level
(the BSDF evaluation + the data needed for the next ray
both happen at hit time) but the *physical* split puts
the actual ray construction in the raygen so the RNG
state never has to round-trip through OptiX payload
registers.

### 17.4 Miss returns environment

(Per the user's third bullet; consolidates §6.)

When `optixTrace` finds no hit, the miss program writes
the environment radiance (with §6.4 Doppler/searchlight
modulation already applied) into the payload's emission
slots and clears the hit flag:

```
payload.emission = env_color * env_intensity, modulated by Doppler/searchlight
payload.hit_flag = 0
```

The raygen reads `payload.hit_flag == 0`, treats the
emission slots as the env contribution (multiplied by
the running throughput), accumulates into radiance, and
breaks the bounce loop. The miss program is a pure
function `(direction, observer, env_color, env_intensity)
→ radiance`.

The miss happens at **every bounce** that escapes the
scene, not just the primary ray. A primary-ray miss
gives the radiance for "what the observer sees in this
direction at infinity"; a bounce-ray miss gives the
indirect radiance from "what an interreflection
direction sees at infinity". Both modulated by Doppler
per §6.4.1's "every miss" choice.

### 17.5 Accumulation buffer remains shared with CUDA path

(Per the user's fourth bullet; the **novel content** in §17.)

This is one of the migration's quietest wins: the OptiX
backend reuses the Stage 11B `rr::renderer::Accumulation
Buffer` **byte-for-byte unchanged**. No new
accumulation infrastructure, no second resolve kernel,
no per-backend buffer type. The integration shape:

| Step                          | CUDA backend (Stage 11C)              | OptiX backend (Stage 12B)             |
|-------------------------------|---------------------------------------|---------------------------------------|
| Allocate sample buffer        | `GpuBuffer<float>` w·h·4 floats       | identical                             |
| Allocate accumulation buffer  | `AccumulationBuffer::resize`          | identical                             |
| Per-spp: produce sample frame | `<<<...>>> launch_pathtrace_sample()` | `optixLaunch(..., d_launch_params, ...)` |
| Per-spp: accumulate           | `accum.accumulate_sample(sample.device_ptr())` | identical |
| End: resolve                  | `accum.resolve_to_image()`            | identical                             |
| End: save PPM                 | `Image::save_ppm`                     | identical                             |

Only the third row changes between backends — the
sample-frame producer. Everything else (allocate,
accumulate, resolve, save) is shared.

This works because `accumulate_sample` takes a *device
pointer* to a w·h·4-float buffer in Rgba32F layout. As
long as the OptiX raygen writes to that exact layout
(which it does — see §5.3.2), the AccumulationBuffer
treats CUDA-produced and OptiX-produced sample frames
indistinguishably. The Stage 11B kernels (`k_accum_add`,
`k_accum_resolve`) read element-wise from the device
pointer; they do not care whether a CUDA kernel or an
OptiX raygen wrote those bytes.

The implication for the migration: an implementer
swapping `<<<launch_pathtrace_sample>>>` for
`optixLaunch(...)` inside `PathTracer::render` does not
have to re-validate the accumulation chain. Stage 11B's
correctness audit (`pathtracer_tests` covering the RNG
+ Sampling primitives, the gpu_accumulation_test
convergence-to-mid-gray fixture) covers both backends'
sample-frame consumers identically. The post-Stage-11
audit's recommendation to produce the four expected
artefacts on a CUDA host is what locks down the
accumulation pipeline; OptiX's adoption of that pipeline
is a structural reuse, not a new implementation.

### 17.6 Relativity at raygen (direction) and in shading (radiance)

(Per the user's fifth bullet; consolidates §14.4.)

Relativity splits across the per-pixel flow at exactly
two sites:

| Effect          | Site                              | Operates on   | Reference |
|-----------------|-----------------------------------|---------------|-----------|
| **Aberration**  | raygen (primary ray only)         | direction     | §5.5, §14.4.1 |
| **Doppler colour** | miss (env) + CH (emission); future CH (NEE) | radiance | §6.4, §7.5, §13.5 |
| **Searchlight** | miss (env) + CH (emission); future CH (NEE) | radiance | §6.4, §7.5, §13.5 |

The user's framing ("raygen for direction, shading for
radiance") is the canonical mental model. The split is
physical:

- **Aberration** = Lorentz transformation of *which
  photons* the observer sees. Lives at the ray's
  origin → raygen.
- **Doppler + searchlight** = transformation of *how*
  those photons look. Lives at every radiance source
  → miss (env) + CH (emission). Future NEE (12C+)
  adds CH-side direct-light contribution Doppler.

In the per-pixel sequence (§17.1):

```
raygen:
  ray.dir = aberrateDirection(observer.velocity, ray.dir)  # ONCE, primary only

bounce loop:
  optixTrace -> CH:    payload.emission = applyDoppler...searchlight(emit, D, ...)
  optixTrace -> miss:  payload.emission = applyDoppler...searchlight(env, D, ...)
  raygen reads payload.emission and adds to radiance     # Doppler-modulated value flows in
```

§14.4 documented the design choice that Stage 12B
applies aberration to the primary ray only (matches
Stage 6-9 single-shot kernel posture, accepts the
small physical inaccuracy that bounce-ray misses also
get Doppler-modulated). §14.4 also documented that
albedo is **not** Doppler-modulated — only emission
and env radiance are. §17.6 inherits both invariants
verbatim.

### 17.7 Per-bounce sequence diagram

A more granular trace through one pixel's path, showing
the program switches and the data hand-offs:

```
[raygen, t = 0]   primary ray gen + jitter + aberration
                  ray = primary
                  bounce = 0
                  throughput = (1,1,1)
                  radiance   = (0,0,0)

LOOP:
[raygen]          if bounce >= max_bounces: exit loop
                  optixTrace(scene_handle, ray, ..., payload)

[OptiX runtime]   walk IAS → (sphere GAS or mesh GAS) → BVH traversal
                  if no hit: dispatch [miss]
                  else:      dispatch [closest-hit]

[miss]            payload.hit_flag = 0
                  payload.emission = applyDoppler+searchlight(env_color * env_intensity, D)
                  return to raygen

[closest-hit]     extract t, position, normal from optixGet*
                  material_index = primitive metadata
                  m = optixLaunchParams.materials[material_index] (or default)
                  payload.position = position
                  payload.normal   = normal
                  payload.albedo   = m.baseColor
                  payload.emission = applyDoppler+searchlight(
                                       m.emissionColor * m.emissionStrength, D)
                  payload.hit_flag = 1
                  return to raygen

[raygen]          if payload.hit_flag == 0:
                    radiance += throughput * payload.emission
                    EXIT LOOP

                  radiance += throughput * payload.emission

                  if bounce + 1 >= max_bounces: EXIT LOOP

                  u2        = next_vec2(rng)
                  local_dir = sample_cosine_hemisphere(u2)
                  world_dir = align_to_normal(local_dir, payload.normal)
                  throughput *= payload.albedo
                  ray.origin    = payload.position + payload.normal * eps
                  ray.direction = world_dir
                  bounce        += 1
                  GOTO LOOP

[raygen, exit]    sample_pixels[(y*w+x)*4 + 0..2] = radiance
                  sample_pixels[(y*w+x)*4 + 3]    = 1.0
```

The OptiX runtime's role in this diagram is the boxed
"OptiX runtime" step: it walks the AS, picks miss vs
closest-hit, dispatches into the right SBT record. The
raygen never sees the AS traversal cost; it only sees
the payload coming back. That opacity is the migration's
performance win — the BVH traversal happens inside
hardware-accelerated OptiX runtime code, replacing the
linear sphere + linear triangle loops the Stage 11C
kernel runs.

### 17.8 Read / write summary across one complete path

Aggregating §5 / §6 / §7 / §15's per-program tables into
one full-path view:

| Surface                              | Direction          | Frequency per pixel per launch |
|--------------------------------------|--------------------|---------------------------------|
| `optixLaunchParams.*` (constant memory) | device read     | many (every program invocation) |
| `optixLaunchParams.sample_pixels[idx]` | raygen write     | once at end of path             |
| OptiX ray payload registers          | write-then-read    | once per bounce                 |
| `pathtracer::Rng` (raygen-local)     | read/write         | per RNG draw (jitter, hemisphere) |
| `radiance` / `throughput` (raygen-local) | read/write     | per bounce                      |
| AS via `optixTrace`                  | read (OptiX runtime) | per bounce                    |
| SBT records (program identifiers)    | read (OptiX runtime) | per dispatch                  |
| `optixLaunchParams.materials[idx]`   | CH read            | per hit                         |
| `optixLaunchParams.spheres[idx]` / mesh | CH read         | per hit                         |
| `optixLaunchParams.observer/params`  | raygen + miss + CH read | per relativity-using site  |
| `optixLaunchParams.env_*`            | miss read          | per miss                        |

The path-tracer host orchestration sits *outside* this
table — its only per-launch read is the launch-params
struct populate + cudaMemcpy + optixLaunch. Per-pixel
state lives entirely on the device.

### 17.9 Scope: what's NOT in §17

Five path-tracer-related concerns this section
deliberately defers:

- **Direct light sampling (NEE).** Per §13.5 / §8.3
  step 1: shadow rays, AH program calling
  `optixTerminateRay`, raygen's per-bounce light
  selection. Stage 12C+.
- **Multiple importance sampling (MIS).** Combines
  NEE with BSDF sampling under power heuristics for
  reduced variance. Needs non-Lambert BSDFs to be
  meaningful (master order #13).
- **Russian roulette path termination.** Probabilistic
  early termination after low-throughput bounces;
  reduces wasted work without bias. Adds one
  `next_float < survival_prob` check at bounce-loop
  start; trivial on top of §17.1's loop.
- **Adaptive sampling.** Per-pixel spp counts based on
  variance estimates (allocates more samples to noisy
  pixels). Needs per-pixel variance estimates in the
  AccumulationBuffer (Stage 11B's single-counter
  layout would extend to per-pixel; §11B BUILD_PLAN
  noted this).
- **Denoising.** OptiX 7+ exposes the OptiX AI
  Denoiser (a TensorRT-based neural denoiser). Adds a
  post-resolve step that takes the noisy
  AccumulationBuffer output, runs the denoiser, and
  produces a smoother frame. Master order #24.

Each of these reaches into the Stage 12B path tracer's
loop or its host orchestration; none restructure
§17.1's per-pixel flow. The loop is the foundation;
each of the five additions slots in at a defined
point.

---

## 18. OptiX backend files

The first piece of the planned `src/optix/` directory
is the OptiX device-context lifecycle owner. This is the
analogue of `cuda/CudaContext.{h,cpp}` for the OptiX
backend: a single host-only file pair that owns
`OptixDeviceContext` creation + destruction, log-callback
registration, and the runtime availability query that
the higher-level OptiX renderer uses to gate its dispatch.

| File                          | Purpose                                                                              |
|-------------------------------|--------------------------------------------------------------------------------------|
| `src/optix/OptixBackend.h`    | Host-only declarations for the OptiX device-context lifecycle: `OptixBackend::initialize` / `shutdown` / `is_available` / `device_context()` accessors. CUDA-Runtime-free + OptiX-Runtime-free header so any TU can include it without pulling `<optix.h>` onto its include path. |
| `src/optix/OptixBackend.cpp`  | Host-only implementation: wraps `optixInit` + `optixDeviceContextCreate` under `#ifdef RR_HAS_OPTIX`, mirrors the `cuda/CudaContext.cpp` pattern (compiled by the host C++ compiler with `<optix.h>` included). Returns honest "OptiX unavailable" failures when the backend isn't compiled in or the runtime fails to initialise. |

The pair lives in `src/optix/` rather than mingled into
`src/cuda/` so the OptiX runtime headers (`<optix.h>`,
`<optix_stubs.h>`, `<optix_function_table_definition.h>`)
stay isolated from the CUDA-only TUs. Future file-pair
sub-stages append the other modules (renderer, programs,
SBT, AS, launch-params) into the same directory.

---

## 19. OptiX renderer files

The second piece of the planned `src/optix/` directory
is the host-facing high-level rendering API, analogous to
`cuda/CudaRenderer.{h,cu}` for the CUDA backend. This is
where the OptiX-specific render orchestration lives:
pipeline + program-group construction, SBT build, AS
build, and the per-launch `optixLaunch` driving from
inside `PathTracer::render` (or a sibling host-side
renderer).

| File                          | Purpose                                                                              |
|-------------------------------|--------------------------------------------------------------------------------------|
| `src/optix/OptixRenderer.h`   | Host-facing declarations for the OptiX render orchestrator: `OptixRenderer::Result` (matching the existing `CudaRenderer::Result` shape), and the public render entry points the `rr_renderer` host code dispatches into. CUDA-Runtime-free + OptiX-Runtime-free header so consumers (`PathTracer.cpp`, `main.cpp` CLI handlers) can include it without pulling `<optix.h>`. |
| `src/optix/OptixRenderer.cpp` | Host-only implementation gated on `RR_HAS_OPTIX`. Owns the OptiX pipeline lifecycle + program-group construction (consuming `OptixBackend`'s device context, the embedded PTX from `OptixPrograms.cu`, the SBT from `OptixSBT.h`, the AS from `OptixAccel.h`), drives `optixLaunch` per spp iteration, and feeds the per-sample buffer into the existing Stage 11B `AccumulationBuffer` (per §17.5's "accumulation buffer remains shared with CUDA path"). |

The pair sits one layer above `OptixBackend` (§18) and
one layer below `PathTracer::render` (in `rr_renderer`).
Future file-pair sub-stages append the remaining
`src/optix/` modules (programs, SBT, AS, launch-params)
that `OptixRenderer.cpp` consumes; the header stays a
stable host-facing surface as those internals shift.

---

## 20. OptiX programs

The third piece of the planned `src/optix/` directory is
the OptiX programs translation unit — a single `.cu`
file that compiles to PTX (or OptiXIR) and is embedded
into the executable for the OptiX pipeline to load at
runtime.

| File                          | Purpose                                                |
|-------------------------------|--------------------------------------------------------|
| `src/optix/OptixPrograms.cu`  | Contains raygen, miss, closest-hit programs            |

Per Stage 12B's program-side design (§5 / §6 / §7), the
file ships exactly three OptiX program entry points: the
raygen (driving primary rays + the bounce loop), the miss
(returning Doppler-modulated environment radiance), and
the closest-hit (extracting hit data + emission +
albedo). The any-hit slot stays empty in Stage 12B per
§8.3's "no AH program" choice; the file does not declare
an AH entry. Sphere + triangle hits share the same
closest-hit entry function — geometry-specific recipes
(§7.6.1 / §7.6.2) branch on the primitive type at hit
time, so one CH file covers both HitGroup records (§9.4).

---

## Sections to come

Future Stage 12A sub-stages will append (one per slice or
small group of slices):

- Intersection program design
- Planned module / file layout under `src/optix/` + CMake
  changes
- Migration risks (toolchain, debug story, build-host
  requirements, code duplication during transition)

Stage 12B is the first slice that ships OptiX **code**. Until
then the project's renderer is exactly what it is today — a
correct CUDA path tracer that wants a faster traversal layer.

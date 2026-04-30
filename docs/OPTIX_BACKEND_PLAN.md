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

## Sections to come

Future Stage 12A sub-stages will append (one per slice or
small group of slices):

- Intersection program design
- Path-tracing integration (iterative bounce loop in raygen,
  payload layout, RNG state threading)
- Planned module / file layout under `src/optix/` + CMake
  changes
- Migration risks (toolchain, debug story, build-host
  requirements, code duplication during transition)

Stage 12B is the first slice that ships OptiX **code**. Until
then the project's renderer is exactly what it is today — a
correct CUDA path tracer that wants a faster traversal layer.

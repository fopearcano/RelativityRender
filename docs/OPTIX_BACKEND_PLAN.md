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

## Sections to come

Future Stage 12A sub-stages will append (one per slice or
small group of slices):

- Intersection program design
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

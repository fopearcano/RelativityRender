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

## Sections to come

Future Stage 12A sub-stages will append (one per slice or
small group of slices):

- Closest-hit / Any-hit / Intersection program design
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

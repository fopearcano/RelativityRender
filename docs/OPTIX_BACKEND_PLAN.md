# OptiX Backend — Migration Plan

Status: **specification only**, introduction slice. No backend code
exists yet. This document is the contract that future implementation
slices will deliver against; the rest of the spec (acceleration
structures, shader binding table, material / camera / relativity
plumbing) lands as separate doc slices before any code is written.

Module reference: `src/optix/` (module 6 in `docs/MODULE_MAP.md`,
milestone M15 in `docs/MILESTONE_ROADMAP.md`).

## 1. Purpose

RelativityRender is a CUDA / OptiX-first GPU renderer. Today the
ray-traversal path runs entirely in our own CUDA kernels: every
primary ray and every bounce iterates over the scene's primitive
arrays in straight-line code. That works for the milestones we have
shipped but stops working as soon as the scene gets non-trivial.

This document explains:

- Why the current naive CUDA path does not scale.
- Why OptiX is the right next step.
- What the three core programs in an OptiX pipeline are
  (`raygen`, `miss`, `closest-hit`) and what each one means in
  RelativityRender's architecture.

It deliberately stops there. Acceleration-structure construction
(GAS / IAS), the shader binding table, material data, camera data,
and the relativistic-perception integration each get their own
document slice with the same level of detail.

## 2. Where we are today

Two scene kernels live in `src/cuda/CudaTestKernel.cu`:

- `k_render_scene` (M10 / M11 / M12) - direct lighting only.
- `k_path_trace`   (M14)              - cosine-weighted path tracer.

Both share a brute-force `trace_closest(scene, ray, t_min)`
helper that, for every ray, walks two flat device arrays:

```
for sphere in scene.spheres:    intersect_sphere(ray, sphere)
for triangle in scene.mesh:     intersect_triangle(ray, v0, v1, v2)
```

The arrays come straight from the host's `GpuScene` upload. There
is no spatial index, no instancing, no BVH; every ray pays the cost
of touching every primitive.

## 3. Why naive intersection does not scale

The path tracer's per-pixel cost is roughly

```
spp * max_depth * (sphere_count + triangle_count)
```

intersection tests, ignoring early-`t_max` shrinkage. For the M12
test scene (4 spheres + 2 triangles) at 1280x720 with `spp = 16` and
`max_depth = 4` that is around 880 million intersections per frame.
Modern GPUs can get through that, but the slope is the problem -
add a single 100 k-triangle mesh and the same frame becomes
about 14 trillion intersection tests, even though every individual
ray would touch a small handful of triangles if it had any
spatial information at all.

Concretely, the naive path has three structural problems:

1. **No early rejection.** Each thread tests every primitive even
   when the ray's bounding interval cannot possibly intersect the
   primitive's bounding volume. The bookkeeping cost grows with
   scene size, not with the local geometric complexity around the
   ray.
2. **No instancing.** Each instance of a mesh would have its own
   per-vertex copy in the upload buffer. A scene of 1 000 copies
   of the same teapot would push its triangle buffer 1 000x.
3. **No hardware help.** RTX-class GPUs ship dedicated
   ray-traversal hardware (the "RT cores") that nothing in the
   naive path uses. We are running general-purpose ALU cycles
   for an operation the hardware can do an order of magnitude
   faster.

The current path is right for the milestones it shipped under -
the goal up to M14 has been to validate the math and the
relativistic pipeline, not the throughput. From M15 on, we need a
ray-traversal layer that scales with scene complexity, and that
means OptiX.

## 4. Why OptiX

OptiX is NVIDIA's GPU ray-traversal API. It is the path the rest
of the platform is committed to: the master architecture document
calls out OptiX as a first-class L3 backend alongside CUDA, and
the milestone roadmap places M15 ("OptiX upgrade path") immediately
after the path tracer foundation lands.

The three things OptiX gives us that we cannot build cheaply
ourselves:

- **Hardware-accelerated BVH traversal.** OptiX builds and
  consumes acceleration structures that map directly onto RTX
  cores' built-in traversal hardware. Per-ray cost stops being
  proportional to the primitive count and starts looking
  logarithmic.
- **Built-in instancing.** A two-level acceleration structure
  (geometry-AS plus instance-AS) lets one geometry buffer back
  many world-space copies for the price of one. The current
  per-mesh world transform we already carry on
  `rr::math::Transform` flows naturally into the instance
  transform.
- **A pluggable program model.** Ray generation, miss, hit, and
  intersection are separate device-side programs that the
  traversal pipeline calls back into. Splitting our existing
  monolithic kernels along those seams is what the rest of this
  plan is about.

OptiX runs on top of CUDA, so it slots in next to the existing
CUDA backend rather than replacing it. The plan is for OptiX to
become the primary traversal path; the naive CUDA kernels stay as
a fallback / regression baseline (and as a way to keep the host
unit tests covering the same `RR_HD inline` math we already
exercise).

## 5. Pipeline overview

An OptiX pipeline is a directed graph of small device-side
programs invoked by the traversal hardware. RelativityRender's
first OptiX pipeline will use just three kinds of programs:

- **`raygen`** - one launched per pixel, per sample.
- **`miss`** - called when a traced ray hits no geometry.
- **`closest-hit`** - called when a traced ray hits a primitive,
  with the nearest hit's data bound in.

(OptiX also supports any-hit and intersection programs. Custom
intersection programs are needed for non-triangle primitives -
spheres, in our case - and any-hit is needed for alpha cutout /
shadow rays. Both fall outside this slice.)

The migration replaces today's monolithic `k_path_trace` body
with these three programs. The host changes are then mostly about
wiring rather than rewriting:

```
host CudaRenderer::render_pathtrace
   -> upload GpuScene to OptiX-friendly buffers
   -> launch the OptiX pipeline (one launch per frame)
   -> the pipeline calls our raygen per pixel
   -> raygen calls optixTrace -> traversal -> miss or closest-hit
   -> raygen accumulates radiance and writes the framebuffer
host saves the framebuffer as today
```

The CPU's job - configure scene, launch, save - does not change.
The GPU's structure does.

## 6. The `raygen` program

The raygen program is the entry point of every OptiX launch. The
launch grid is the image (`width x height x spp`); OptiX invokes
the raygen program once per launch index.

In RelativityRender, the raygen program owns the work that today
sits at the top of `k_path_trace`:

- Compute the pixel coordinate from the launch index.
- Build the per-pixel RNG (the existing
  `rr::pathtracer::make_rng(x, y, sample)`).
- Generate the primary camera ray
  (`rr::camera::generate_camera_ray`).
- Apply the relativistic aberration to the primary direction.
- Drive the bounce loop: at each bounce, call `optixTrace` instead
  of our hand-written `trace_closest`, then sample a new bounce
  direction, update the throughput, and continue.
- After the loop ends, apply the relativistic Doppler colour shift
  and searchlight scaling to the integrated radiance.
- Write the per-sample radiance to the accumulation buffer (or
  directly to the framebuffer for the single-sample fast path).

Two important properties carry over unchanged:

- **All ray paths run on the GPU.** The raygen program runs on
  the device just like our existing `__global__` kernels. The CPU
  is still only doing scene upload, launch, and image save.
- **The shared math is the same.** The `RR_HD inline` helpers
  (`generate_camera_ray`, `aberrateDirection`, `dopplerFactor`,
  `searchlightFactor`, `applyDopplerColor`, `make_rng`,
  `next_float`, `sample_hemisphere_cosine`, ...) all work
  inside an OptiX program with no changes. The host unit tests
  that exercise them remain valid against the OptiX path by
  construction.

## 7. The `miss` program

OptiX calls the miss program when a traced ray's traversal completes
without hitting any geometry. It receives the ray and a per-ray
payload, and writes whatever should fill the "no hit" slot.

In RelativityRender, the miss program is where the existing
`sky_color` lives. Today `sky_color(scene, ray.direction)` returns
either the uploaded `Environment` light's colour or the vertical
sky gradient. That logic moves into the miss program more or less
verbatim, with the lights array reachable through the launch
parameters or the SBT (the wiring details land in their own slice).

The miss program does **not** apply Doppler / searchlight - those
wrap the *integrated* radiance once the whole path is done, and
the miss program is one step of one bounce. It just hands the
environment radiance back through the payload so the raygen can
fold it into the running throughput.

## 8. The `closest-hit` program

OptiX calls the closest-hit program when traversal lands on the
nearest valid intersection of a ray. It receives the hit
parameters (barycentrics, primitive id, instance id) and the
per-ray payload.

In RelativityRender, this program owns the work that today sits
inside the bounce-step branch of `trace_one_path`:

- Look up the primitive's material (today: `lookup_material`).
- Reconstruct the surface position and outward normal from the
  hit attributes.
- Add the surface's emission to the per-ray throughput-weighted
  radiance.
- Sample the next bounce direction (cosine-weighted Lambertian
  for v1; the BSDF interface arrives later).
- Update the throughput.
- Hand the new ray (or a "terminated" signal) back to the
  raygen program through the payload.

The closest-hit program does not loop - it handles one bounce.
The bounce loop stays in raygen, calling `optixTrace` until the
path either terminates or hits the depth limit. That keeps the
control flow recognisable next to today's `trace_one_path`: the
loop body becomes "fire one optixTrace call" instead of "scan two
arrays of primitives".

## 9. Acceleration structures

OptiX expresses spatial indexing as **acceleration structures
(AS)** - hardware-traversable BVHs built once per change to the
scene's geometry and read by every ray. RelativityRender's first
OptiX pipeline uses two kinds:

- **GAS** (Geometry Acceleration Structure) - a BVH over a single
  set of primitives in a fixed local space.
- **IAS** (Instance Acceleration Structure) - a BVH whose leaves
  are *instances*, each referencing a GAS plus a `3x4` world
  transform.

### 9.1 GAS

A GAS is the leaf-level structure. Each GAS contains one
*build input*:

- A **triangle GAS** consumes a vertex buffer + an index buffer
  (the `Vertex` / `Triangle` arrays our existing `GpuMesh`
  already uploads). OptiX uses the built-in triangle intersection
  - no custom intersection program needed.
- A **custom-primitive GAS** consumes an axis-aligned bounding
  box per primitive plus a custom intersection program. Spheres
  fall here; the AABB is `(center +/- radius)` and the custom
  intersection program is the existing
  `rr::cuda::intersect_sphere` lifted from
  `cuda/CudaIntersection.cuh`.

Mapping the current scene onto GAS list:

| Scene contents (today)         | GAS in v1                                  |
|--------------------------------|--------------------------------------------|
| `GpuScene::device_spheres()`   | one custom-primitive GAS over the sphere AABBs |
| `GpuScene::gpu_mesh()`         | one triangle GAS per mesh (today: a single mesh) |

GAS builds are sticky - a build is amortised over every frame
that doesn't change the underlying geometry. Vertex animation /
deformation will need *refit* later, but v1 only needs static
builds.

### 9.2 IAS

An IAS is a BVH-of-BVHs. Each leaf is an OptiX `Instance`,
carrying:

- A handle to the underlying GAS.
- A `3x4` `transform` (the same SRT decomposition our host
  `rr::math::Transform` already produces).
- A 32-bit `instanceId` used by hit programs to look up
  per-instance data (materials, etc.) through the SBT.

The IAS is what makes instancing cheap. A scene of 1 000 copies of
the same teapot uploads one vertex/index buffer and one GAS; the
IAS carries 1 000 `Instance` entries with different transforms.
The current `GpuMesh::transform` field flows straight into the
instance transform without churn.

Mapping the current scene onto an IAS:

```
IAS root
  +-- Instance(GAS = sphere_gas,   transform = identity, id = 0)
  +-- Instance(GAS = mesh0_gas,    transform = mesh0_world, id = 1)
  +-- Instance(GAS = mesh1_gas,    transform = mesh1_world, id = 2)
  ...
```

`optixTrace` from the raygen program targets the IAS; the
hardware walks the IAS down to a GAS, then walks the GAS down to
the primitive, then calls the matching hit program.

### 9.3 Why BVH is critical

A BVH is the difference between "cost grows with the scene's
local complexity around the ray" and "cost grows with total
primitive count":

| Approach                          | Per-ray work                |
|-----------------------------------|-----------------------------|
| Today's naive linear scan         | `O(N)` over all primitives  |
| Software BVH                      | `~O(log N)` plus traversal overhead |
| OptiX hardware-accelerated BVH    | `~O(log N)` on RT cores - the traversal itself is offloaded from the SM |

The middle row is what we'd be writing if we built a BVH in
plain CUDA. The bottom row is what we get for free by handing
the BVH to OptiX: the traversal hardware does the box tests and
the primitive descent in dedicated silicon, freeing the SM to
run the actual hit / shade logic.

The naive path stays as a fallback / regression baseline (it's
already validated end-to-end and the host tests cover its math),
but the OptiX path is what scales.

## 10. Shader Binding Table (SBT)

The **shader binding table** is a device-resident array of
records that connects three things at traversal time:

1. **Which geometry was hit** (instance id, GAS, primitive id).
2. **Which programs run** (raygen / miss / closest-hit /
   any-hit / intersection).
3. **Which per-geometry data the program reads** (vertex buffer
   pointer, material index, transform inverse, ...).

The traversal hardware computes an offset into the SBT from the
hit's `(instance.sbt_offset, gas.sbt_offset, ray_type,
sbt_stride)` and dispatches the program at that record. The SBT
is the *only* mechanism that wires geometry to materials; there
is no global "this primitive belongs to material X" map - the
mapping lives entirely in records.

### 10.1 Record layout

Every SBT record has the same shape: a fixed header followed by
a user-defined payload.

```
+---------------------+
| header (32 bytes)   |  <- written by optixSbtRecordPackHeader
+---------------------+
| user data (struct)  |  <- arbitrary; aligned to 16 bytes
+---------------------+
```

The header is a hash of the program group the record dispatches
to; the runtime binds it to the corresponding device function at
launch time. The payload is whatever the corresponding program
needs.

### 10.2 Record kinds in v1

RelativityRender's first OptiX pipeline ships three kinds of
records:

- **One raygen record** - the entry point of the pipeline. Its
  payload carries the launch-wide pointers the raygen program
  reads (framebuffer, accumulation buffer, scene view); details
  belong to the camera / launch-parameter slice.
- **One miss record per ray type** - in v1 there is exactly one
  ray type ("radiance"). Its payload is the data the existing
  `sky_color` reads (the scene's environment light, sky tint
  fallback).
- **One hitgroup record per (instance, ray type) pair** - the
  bundle of closest-hit + any-hit + intersection programs the
  hit dispatches to. The payload is the per-geometry data the
  closest-hit program needs.

The hitgroup payload is where the geometry / material wiring
lives. For a triangle mesh hit, the payload typically includes:

- Device pointers to the vertex and index buffers (so the hit
  program can reconstruct the surface position and normal).
- The mesh's `material_index` (the lookup key the closest-hit
  program uses to find the right `MaterialParams`).
- A transform pointer (or a flag that the GAS already lives in
  world space).

For a sphere hit, the payload includes:

- A pointer to the sphere POD array (so the hit program can read
  centre / radius / material index by primitive id).

The exact field list, alignment, and how `MaterialParams` flows
through the hit-group payload belongs to the materials slice of
this plan; this section only fixes the *shape*.

### 10.3 Why the SBT matters

In a hand-written CUDA kernel, dispatch is just an `if /
else if` ladder over primitive types. In an OptiX pipeline, the
ladder is replaced by a data structure - the SBT. Three
consequences are worth calling out:

- **Adding a new primitive type means adding an SBT record
  kind, not changing the kernel.** Curves, volumes, displaced
  surfaces all plug in the same way once the SBT is in place.
- **Material updates do not rebuild geometry.** Changing a
  sphere's `material_index` is a record-payload edit; the AS is
  untouched.
- **Multiple ray types share the same hit groups.** When shadow
  rays land later, every hitgroup gets a second record (the
  any-hit / closest-hit pair for the shadow type) without
  changing the geometry path.

## 11. Data flow

The end-to-end picture, from a `.rrscene` file to a rendered PPM,
adds an OptiX-construction step between the existing GPU upload
and the existing kernel launch. Nothing on either side of that
new step moves:

```
CPU side                            GPU side
========                            ========

1. SceneLoader::load_rrscene
       |
       v
   rr::scene::Scene (host)
       |
       v
2. GpuScene::upload_from(scene)
       |                               GpuScene buffers (device):
       |                                 - sphere array
       |                                 - mesh vertex / index buffers
       |                                 - material array
       |                                 - light array
       |                                 - camera / observer / params PODs
       v
3. OptiX backend build  -----------> AS objects (device):
   (M15 - new step)                    - sphere GAS  (custom prim, AABBs)
                                       - triangle GAS per mesh
                                       - IAS over GAS list
                                     SBT (device):
                                       - raygen record
                                       - miss record(s)
                                       - hitgroup record per (instance, ray type)
                                     Pipeline / module objects (device-resident
                                     program code)
       |
       v
4. CudaRenderer::render_pathtrace_optix
       |                               optixLaunch:
       |                                 raygen runs per pixel
       |                                 -> optixTrace -> traversal
       |                                 -> miss or closest-hit
       |                                 raygen accumulates radiance,
       |                                 writes framebuffer
       v
5. download framebuffer
       |
       v
6. Image::save_ppm
```

Steps 1, 2, 5, and 6 already exist. Steps 3 and 4 are what M15
adds. Every device-side buffer in step 3 is **read from**
GpuScene's existing device pointers - the OptiX backend does not
duplicate vertex / index / material / sphere data, it just
references it through GAS build inputs and SBT payloads.

The architectural invariants this enforces:

- **GpuScene remains the single owner of scene data on the
  device.** When materials / lights / geometry are uploaded,
  they go through `GpuScene::upload_*` exactly like today; the
  OptiX backend is a consumer.
- **`CudaRenderer` keeps its public surface.** The new entry
  point sits next to `render_pathtrace`; the CPU caller in
  `main.cpp` switches between them by config (or by build
  flag) without touching the rest of the pipeline.
- **The CPU's job does not change.** Configure + load + upload
  + launch + save - no per-pixel work crosses back to the host.

## 12. Material system integration

Materials are the data the closest-hit program reads after the
traversal hardware lands on a primitive. Two concrete questions:
how does `rr::material::MaterialParams` flow from `GpuScene`
through the SBT to the closest-hit program, and how does the
closest-hit program use it once it has the parameters?

### 12.1 What the CUDA path does today

`src/cuda/CudaTestKernel.cu` already wires this end-to-end:

- `GpuScene::upload_materials(...)` uploads a flat
  `GpuBuffer<MaterialParams>` (`src/gpu/GpuScene.{h,cpp}`).
- `CudaSceneView` (`src/cuda/CudaScene.cuh`) carries
  `const MaterialParams* materials` + `int material_count` by
  value as a kernel argument.
- `k_path_trace` and `k_render_scene` call
  `lookup_material(scene, hit.material_index) -> MaterialParams`
  (with a `-1` / out-of-range fallback to a neutral default).
- The shading function reads `.baseColor / .emissionColor /
  .emissionStrength / .roughness` directly.

### 12.2 Where each piece moves under OptiX

The host upload path is **unchanged**. The handful of pointers
`CudaSceneView` carries today gets split between two device-side
homes:

| What                                  | Today (CUDA path)         | OptiX                            |
|---------------------------------------|---------------------------|----------------------------------|
| `MaterialParams[]` array              | `CudaSceneView::materials`| Launch parameters (per-launch)   |
| Sphere POD array (incl. material id)  | `CudaSceneView::spheres`  | Launch parameters + sphere hit-group payload |
| Per-mesh `material_id`                | `CudaMeshView::material_id`| Hit-group SBT record payload     |
| Per-pixel hit lookup                  | direct `scene.materials[i]` | `launch_params.materials[i]`    |

The materials buffer itself is **launch-wide** - the same array is
read from every hit, every bounce, every pixel - so it belongs in
the launch parameters struct rather than in a per-record payload.
That keeps SBT records small.

### 12.3 How the index reaches the hit program

The closest-hit program needs an `int material_index` so it can
read `launch_params.materials[material_index]`. Two cases:

- **Triangle meshes.** Each mesh has a single `material_id` baked
  into its host `Mesh` (and its `GpuMesh::material_id()`). The
  hit-group SBT record for a mesh carries that integer in its
  payload directly. The closest-hit program reads it as
  `sbt_data->material_index` and looks the params up.
- **Spheres** (custom-primitive GAS). The per-primitive
  `material_index` is on the `Sphere` POD itself
  (`src/geometry/Sphere.h`). The hit-group SBT record for the
  sphere GAS carries a device pointer to the sphere array; the
  closest-hit program reads
  `spheres[optixGetPrimitiveIndex()].material_index`.

Either way, the closest-hit program ends up with an `int`. The
`lookup_material(materials, count, idx)` helper from the CUDA
path lifts verbatim - it's already host- and device-callable.

### 12.4 What this gives us

- The `MaterialParams` POD is unchanged; the host upload path is
  unchanged; the host material tests (`material_tests`) keep
  exercising the same struct.
- Adding more BSDF lobes (the M14 path tracer's still-pending
  `eval` / `sample` / `pdf`) is a closest-hit change - no SBT
  rebuild, no AS rebuild.
- Live material edits become a single
  `cudaMemcpy(materials_buffer, ...)` - the AS and SBT do not
  touch.
- A future texture system (M16) plugs in by adding device
  pointers to texture objects on the SBT record's payload (one
  pointer per textured slot), with the same lookup pattern.

## 13. Camera integration

Camera data is **per-launch state**: the same camera transform,
fov, and aspect apply to every pixel, every bounce, every ray
within a single frame. That makes it a natural fit for OptiX
launch parameters - a small device-resident struct passed to
`optixLaunch` and read from `__constant__` memory by every
program.

### 13.1 What the CUDA path does today

- `main.cpp` builds an `rr::camera::Camera` (or loads it via
  `SceneLoader::load_rrscene`), calls
  `Camera::to_gpu() -> GpuCamera`.
- `GpuScene::upload_camera(camera)` snapshots the
  `GpuCamera` POD into the scene's `device_camera()` slot.
- `CudaSceneView::camera` carries the POD by value as a kernel
  argument.
- `k_path_trace`/`k_render_scene` call
  `rr::camera::generate_camera_ray(scene.camera, x, y, w, h)`
  (`RR_HD inline` from `src/camera/CameraRay.h`).

### 13.2 Where each piece moves under OptiX

| What                  | Today (CUDA path)         | OptiX                       |
|-----------------------|---------------------------|-----------------------------|
| Host `GpuCamera` build| `Camera::to_gpu()`        | Unchanged                   |
| Device upload         | `GpuScene::upload_camera` | Unchanged                   |
| Per-launch carrier    | `CudaSceneView::camera`   | Launch parameters struct    |
| Per-pixel ray gen     | `generate_camera_ray(...)`| Same call from raygen       |

The raygen program reads `launch_params.camera` and calls the
existing `generate_camera_ray` helper. The function is already
`RR_HD inline` and uses only host- and device-callable
arithmetic; it works inside an OptiX program with **no source
change**.

```
// raygen body (sketch)
const auto& cam = launch_params.camera;
const uint3 idx = optixGetLaunchIndex();
auto ray = rr::camera::generate_camera_ray(
    cam, idx.x, idx.y, launch_params.width, launch_params.height);
```

### 13.3 What this gives us

- The aspect / fov / basis logic established at M7 carries over
  identically.
- `camera_tests` continues to validate the device math by
  construction - the OptiX raygen program calls the same
  `generate_camera_ray` the host suite covers.
- A future per-frame animation update is a single small
  `cudaMemcpyAsync` into the launch-params buffer - no AS / SBT
  rebuild.
- The launch-params struct stays small: pointers + a couple of
  PODs (camera, observer, relativity flags), well within the
  tens of KB OptiX caches in constant memory.

## 14. Relativity integration

The relativistic perception pipeline today wraps the kernel's
per-pixel work at five well-defined seams. Every seam stays a
**raygen-program responsibility** under OptiX; nothing moves
into the closest-hit or miss programs. The architectural
invariant is that relativity is a *per-pixel* wrapper around an
otherwise standard light-transport integrator.

### 14.1 The five seams (today and under OptiX)

Mapping each piece directly to its current
`src/cuda/CudaTestKernel.cu` location:

| Step                                | Today (CUDA `k_path_trace`)              | OptiX (raygen)                     |
|-------------------------------------|------------------------------------------|------------------------------------|
| 1. Generate primary ray             | `generate_camera_ray(...)`               | Same call, same point in raygen    |
| 2. Apply aberration (primary)       | `aberrateDirection(observer.velocity, ray.direction)` | Same call, immediately after step 1 |
| 3. Bounce loop (light transport)    | per-bounce: `trace_closest` -> shade -> sample -> repeat | per-bounce: `optixTrace` -> hit-group runs -> raygen reads payload -> sample -> repeat |
| 4. Compute Doppler factor           | `dopplerFactor(observer.velocity, primary_dir)` once before integration | Same, computed in raygen once |
| 5. Wrap integrated radiance         | `applyDopplerColor(L, D, ...)` and `L *= lerp(1, searchlightFactor(D), ...)` | Same calls, same point - after the bounce loop, before framebuffer write |

Every helper named above lives in
`src/relativity/RelativityMath.h` and is `RR_HD inline`; every
one is exercised by the host `relativity_tests` suite (52
assertions). The OptiX raygen program calls the exact same
functions in the exact same order.

### 14.2 What relativity does *not* touch

- **The closest-hit program.** It reads `MaterialParams`,
  accumulates emission, samples the next bounce, returns. No
  observer state, no Doppler, no aberration. Keeping the BSDF
  surface free of relativity makes it interchangeable with
  whatever future BSDF interface lands at M14 / M16.
- **The miss program.** It returns the environment-light
  radiance unmodified. Doppler / searchlight wrap the
  *integrated total* at the end of the path - applying them
  per-bounce inside the miss program would double-count.
- **The acceleration structures.** The geometry sees the same
  rays it would see in a non-relativistic renderer; only the
  *primary* ray's direction is aberrated, and that aberration
  happens before the AS is queried.

### 14.3 Optional second pass: per-bounce aberration

The current CUDA pipeline only aberrates the **primary** ray.
Aberrating bounce rays (so a moving observer sees relativistic
caustics, etc.) is straightforward to add later: it becomes
"call `aberrateDirection` on `ray.direction` between
`closest-hit returns` and `optixTrace next-bounce`" inside the
raygen loop. This is intentionally not in the M15 first-light
slice; the host tests will be extended to cover the per-bounce
case when it lands.

### 14.4 What remains unchanged from the CUDA backend

- Every `RR_HD inline` helper in `src/relativity/RelativityMath.h`
  (`clampBeta`, `gamma`, `lorentzContraction`, `dopplerFactor`,
  `searchlightFactor`, `aberrateDirection`,
  `applyDopplerColor`).
- The `Observer` and `RelativityParams` PODs in
  `src/relativity/RelativityParams.h`.
- The mapping from the on-disk `.rrscene` `relativity` section
  to those host structs (`SceneLoader::load_relativity`).
- The host `relativity_tests` suite (52 assertions) - it covers
  the device math by construction, since the OptiX programs
  call the same functions.

## 15. Migration plan

Step-by-step transition from the current CUDA naive path to the
OptiX backend. Each step is its own implementation slice; the
existing CUDA path stays available throughout the migration as a
fallback / regression baseline, and stays available after M15
ships.

### 15.1 Step M15.1 - SDK detection + CMake plumbing

- Add an `RR_ENABLE_OPTIX` option to the top-level
  `CMakeLists.txt` (off by default, mirroring how
  `RR_ENABLE_CUDA` was off at M5). Off keeps every existing
  build configuration unchanged.
- Add `find_package(OptiX REQUIRED)` (or the equivalent
  module-mode detection for the version we target) under the
  `RR_ENABLE_OPTIX` branch.
- Vendor or detect the OptiX SDK headers; the runtime itself is
  loaded by `optixInit()` at backend startup, like the CUDA
  Driver API.
- No source-side OptiX yet - this slice just makes the build
  configurable.

### 15.2 Step M15.2 - `rr_optix` library skeleton

- Create `src/optix/` (the placeholder directory has existed
  since the M1 skeleton).
- Add `OptixContext` (lifecycle wrapper: init, log callback,
  shutdown) and a `OptixPipeline` scaffold. Same pattern as
  `src/cuda/CudaContext.{h,cpp}`.
- Add the `rr_optix` static library to CMake. Per
  `docs/MODULE_MAP.md` the OptiX backend depends on `rr_gpu` and
  PUBLIC-links `rr_image`, `rr_camera`, `rr_material`,
  `rr_lighting` (transitively through `rr_gpu`). It MUST NOT
  depend on UI / Cinema 4D.
- No programs yet, no AS / SBT yet - just the lifecycle
  skeleton plus a tiny host smoke test that initialises and
  tears down the context cleanly.

### 15.3 Step M15.3 - Build acceleration structures from `GpuScene`

- Add `OptixSceneAS` that reads a `GpuScene` and produces:
  - one custom-primitive GAS over the sphere AABBs,
  - one triangle GAS per mesh,
  - an IAS over those GASes with the per-mesh world transforms.
- Pure AS construction; no traversal yet. The slice ships when
  the build call returns a valid `OptixTraversableHandle` and
  the host smoke test asserts the build succeeded for a
  representative scene.
- The CUDA renderer path is **untouched**.

### 15.4 Step M15.4 - Programs, modules, and SBT

- Three new `.cu` files compiled to PTX (the eventual layout
  follows the spec sections above; names are illustrative):
  - `src/optix/RaygenPathTrace.cu` - per-pixel state machine,
    primary ray, aberration, bounce loop driving `optixTrace`,
    post-loop Doppler / searchlight, framebuffer write.
  - `src/optix/MissEnvironment.cu` - lifts the existing
    `sky_color` logic.
  - `src/optix/HitClosestRadiance.cu` - lifts the existing
    material-lookup + bounce-sample logic.
- Build the SBT records that bind these programs to the IAS.
- Add a parallel host entry point
  `CudaRenderer::render_pathtrace_optix(scene, w, h)` next to
  the existing `render_pathtrace`. Same return type
  (`Result { ok, image, message }`), so callers can swap one
  for the other.
- `main.cpp` picks the backend by config (an env var or a new
  CLI flag, e.g. `--backend optix`). Default stays **CUDA**;
  the OptiX path is opt-in for this slice.

### 15.5 Step M15.5 - Validation

- Run both backends side by side on the same `.rrscene` file
  (start with `scenes/test_geometry.rrscene` and the M14
  fixtures).
- Add a comparison test in the host suite. With the same RNG
  seed, primary ray sequence, and a single sample per pixel,
  the two backends should produce bit-equal pixels for a
  trivial scene - and within sample noise envelope for the
  full path tracer.
- Document any known divergence (e.g., traversal-order
  tie-breaks at edges) in this file's
  [Out of scope](#16-out-of-scope-for-this-slice) section.

### 15.6 Step M15.6 - Promote OptiX to default

- Switch the default backend in `main.cpp` to OptiX.
- Update `docs/MODULE_MAP.md` (module 6 status) and
  `docs/MILESTONE_ROADMAP.md` (M15 marked landed).
- The CUDA path stays available behind the same flag. It is
  not deprecated and not deleted.

### 15.7 Why CUDA stays after OptiX is the default

Removing the CUDA path costs us three things we want to keep:

1. **Test coverage of the `RR_HD inline` math.** The host
   `geometry_tests`, `camera_tests`, `relativity_tests`, and
   `sampling_tests` suites cover the helper functions both
   backends call. The CUDA kernels are how those same helpers
   exercise the real GPU pipeline; without them the CI matrix
   loses its strongest guarantee that "host tests pass" implies
   "device path works".
2. **A debug fallback for non-RTX hardware.** Compute-only
   GPUs and pre-Turing cards can still drive the CUDA backend.
   That keeps the renderer usable on a wider set of dev
   machines.
3. **A regression baseline for OptiX bugs.** When an OptiX
   image looks wrong, rendering the same scene on the CUDA
   path narrows the search to "OptiX-specific" vs "shared
   math".

So: **OptiX as default, CUDA as fallback** - not "OptiX
replaces CUDA".

## 16. Out of scope for this plan

The bulk of the OptiX backend now has a design contract: the
program model (§§5-8), the AS / SBT / data flow (§§9-11),
material / camera / relativity wiring (§§12-14), and the
migration plan itself (§15). What remains explicitly deferred:

- **Specific OptiX SDK version targeting.** The plan is
  version-agnostic; `find_package` configuration in step M15.1
  picks the supported range when implementation begins.
- **OptiX denoiser API integration.** The denoiser is part of
  M22 (Denoiser Integration), not M15. The OptiX denoiser will
  consume the same framebuffer + AOV outputs the CUDA path
  produces.
- **Multi-GPU / multi-stream traversal.** Single-GPU first;
  multi-stream and multi-device traversal land with the
  renderer-server milestone (M18+) where they have a clear
  consumer.
- **Per-bounce relativistic aberration.** The first OptiX slice
  matches today's CUDA behaviour: only primary rays are
  aberrated. Per-bounce aberration is a small follow-up
  (§14.3), not a blocker for parity.
- **Curves / volumes / displaced surfaces.** v1 OptiX ships
  triangles + spheres only. Each new primitive type adds a GAS
  build input, an SBT record kind, and (for non-triangle) a
  custom intersection program; the existing scaffolding makes
  this incremental.

These are smaller follow-ups, not unresolved design questions.
M15 ships when steps M15.1 through M15.6 in the migration plan
are landed.

## 17. References

- `src/cuda/CudaTestKernel.cu` - the current naive `trace_closest`
  body that the OptiX migration replaces.
- `src/cuda/CudaRenderer.{h,cu}` - the host entry points
  (`render_scene`, `render_pathtrace`) that will gain OptiX
  variants without changing their public surface.
- `docs/MASTER_ARCHITECTURE.md` - module 6 (OptiX Backend) layer
  and dependency contract.
- `docs/MODULE_MAP.md` - per-module ownership; the OptiX backend
  may not depend on UI / Cinema 4D and sits alongside (not on
  top of) the CUDA backend.
- `docs/MILESTONE_ROADMAP.md` - milestone M15 (OptiX upgrade
  path).
- `docs/BUILD_PLAN.md` - milestone state and per-slice change log.

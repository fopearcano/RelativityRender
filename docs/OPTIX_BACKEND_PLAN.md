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

## 12. Out of scope for this slice

The following pieces are explicitly deferred to subsequent doc
slices and will be added to this file in the same incremental
style as the RRSCENE format spec:

- **Material data.** How `rr::material::MaterialParams` flows
  into hit-group records, where the BSDF interface plugs in,
  and how a v2+ texture system rides through the SBT.
- **Camera data.** Launch-parameter layout for `GpuCamera`,
  observer state, and relativity flags; how the raygen program
  reads them.
- **Relativity integration.** Exactly where in the OptiX
  pipeline each of `aberrateDirection`, `dopplerFactor`,
  `searchlightFactor`, `applyDopplerColor` runs, and the
  invariants that keep the host unit tests covering the OptiX
  path by construction.
- **Build / SDK integration.** CMake `find_package(OptiX)`
  plumbing, the new module library (`rr_optix`), how the
  existing CUDA backend stays available as a fallback /
  regression baseline.

Each of those becomes a section in this file when its design
slice is approved. Implementation work does not start until the
relevant slice is in.

## 13. References

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

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

## 9. Out of scope for this slice

The following pieces are explicitly deferred to subsequent doc
slices and will be added to this file in the same incremental
style as the RRSCENE format spec:

- **Acceleration structures.** Geometry-AS layout for triangle
  meshes (built-in) and spheres (custom intersection), instance-AS
  for the scene's per-object world transforms, build / refit
  policies.
- **Shader binding table.** Record types, per-geometry hit
  records, miss records, ray types, how the OptiX runtime maps
  hits to the right closest-hit program.
- **Material data.** How `rr::material::MaterialParams` flows
  into hit records and how the BSDF interface (M14+) plugs in.
- **Camera data.** Launch parameter layout for `GpuCamera`,
  observer state, relativity flags.
- **Relativity integration.** Exactly where in the OptiX pipeline
  each of `aberrateDirection`, `dopplerFactor`,
  `searchlightFactor`, `applyDopplerColor` runs, and the
  invariants that keep the host unit tests covering the OptiX
  path by construction.
- **Build / SDK integration.** CMake `find_package(OptiX)` plumbing,
  the new module library (`rr_optix`), how the existing CUDA
  backend stays available as a fallback / regression baseline.

Each of those becomes a section in this file when its design
slice is approved. Implementation work does not start until the
relevant slice is in.

## 10. References

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

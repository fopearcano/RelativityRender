# GPU Rendering Correctness Audit

Status: audit step 3. Focused only on CUDA kernels,
renderer host wrappers, and per-pixel logic. Confirms
which parts of the pipeline run on the GPU, which run
on the CPU, and where the rules are honoured vs. broken.

The five audit questions, answered up front:

| #   | Question                                                  | Answer                                          |
|-----|-----------------------------------------------------------|-------------------------------------------------|
| 1   | Does CUDA actually render pixels?                          | **Yes.** Three production kernels write the framebuffer end-to-end. |
| 2   | Are rays generated on GPU?                                 | **Yes.** Per-thread `generate_camera_ray` runs in the kernel via an `RR_HD inline` helper. |
| 3   | Are intersections on GPU?                                  | **Yes.** `intersect_sphere` / `intersect_triangle` are `RR_HD inline` and called from the kernels. |
| 4   | Is CPU doing ANY per-pixel or per-ray work?                | **No** in the rendering hot path. Two CPU per-pixel loops exist - both are PPM I/O after the GPU finishes. |
| 5   | Are kernels real or placeholder?                           | **Mixed.** CUDA kernels are real and used; OptiX side is lifecycle-only with a `render_placeholder` stub. |

## 1. GPU pipeline summary

The renderer's CUDA-side hot path lives in three files:

```
   src/cuda/CudaRenderer.{h,cu}    host wrappers, GpuBuffer
                                   alloc + cudaDeviceSynchronize
                                   + download into rr::image::Image.

   src/cuda/CudaTestKernel.cu      every __global__ kernel: gradient,
                                   camera-rays, sphere-vis,
                                   sphere-relativistic, render_scene,
                                   path_trace, render_aovs.

   src/cuda/CudaIntersection.cuh   RR_HD inline intersect_sphere /
                                   intersect_triangle.
```

A render goes:

```
   host                                                  device
   ----                                                  ------
   CudaRenderer::render_scene(scene, w, h)
     run_kernel_render(w, h, [scene](dev, w, h){
                                      launch_render_scene(dev, w, h,
                                                          view, stream)
       GpuBuffer<float>::allocate(w*h*4)         ->     cudaMalloc
       launch_kernel(dev, w, h)                  ->     k_render_scene<<<grid,block>>>
                                                          one thread per pixel:
                                                          - generate_camera_ray
                                                          - aberration (relativity)
                                                          - intersect_sphere x N
                                                          - intersect_triangle x M
                                                          - lights loop
                                                          - graph-eval material
                                                          - texture sample (M16)
                                                          - Doppler colour
                                                          - searchlight gain
                                                          - write 4 floats
       cudaDeviceSynchronize                     <-     barrier
       GpuBuffer<float>::download(image.data())  <-     cudaMemcpy DtoH
     })
     return Result{ok, image}
```

The host side in `run_kernel_render` (CudaRenderer.cu
lines 36-77) does exactly four things: validate dims,
allocate one device buffer, launch the kernel via the
caller-supplied lambda, drain CUDA errors, download into
a host `rr::image::Image`. **No per-pixel iteration on
the host.**

The path tracer (`launch_path_trace` ->
`k_path_trace`) and the AOV kernel
(`launch_render_aovs` -> `k_render_aovs`) follow the
exact same shape - only the kernel body differs.

## 2. Confirmed working parts

All five questions answered against actual code, with
the file:line citations that prove each.

### 2.1 CUDA kernels are real

`__global__` definitions in `src/cuda/CudaTestKernel.cu`:

| Kernel                         | Line  | Status                                    |
|--------------------------------|------:|--------------------------------------------|
| `k_gradient_rgba32f`           |    25 | M6 diagnostic; still callable.             |
| `k_camera_rays_visualize`      |    59 | M7 diagnostic; still callable.             |
| `k_sphere_visualize`           |    96 | M8 diagnostic; still callable.             |
| `k_sphere_relativistic`        |   160 | M9 diagnostic; still callable.             |
| `k_render_scene`               |   249 | **Production**. Direct lighting + relativity.|
| `k_path_trace`                 |   660 | **Production**. Cosine-weighted Lambertian path tracer.|
| `k_render_aovs`                |  ~720 | **Production**. M17 AOV launch.              |

All seven are launched with `<<<grid, block>>>` (line
50, 85, 145, 237, 453, 705, ~880). One thread per
pixel; grid is `((w + 15) / 16, (h + 15) / 16)`; block
is 16x16 (= 256 threads). The early-return for
out-of-range threads is `if (x >= width || y >= height)
return;` consistently.

### 2.2 Rays are generated on the GPU

`generate_camera_ray` in `src/camera/CameraRay.h:38` is
declared `RR_HD inline` and is invoked from inside every
production kernel:

- `k_render_scene` line 258
- `k_path_trace` -> `trace_one_path` line 563
- `k_render_aovs` (same shape as `k_render_scene`)
- `k_camera_rays_visualize` line 65

The function itself runs in registers - takes pixel
coords + GpuCamera-by-value, returns a CameraRay. No
host call, no allocation, no heap. The GpuCamera is
uploaded once per render via `Camera::to_gpu()` and
passed into `CudaSceneView`.

### 2.3 Intersections are on the GPU

`intersect_sphere` (`CudaIntersection.cuh:33`) and
`intersect_triangle` (`CudaIntersection.cuh:93`) are
both `RR_HD inline`. Both are called from the kernels'
closest-hit loops:

- `k_render_scene` lines 273-308 - inline closest-hit
  loop over `scene.spheres` + `scene.mesh.triangles`.
- `trace_closest` (`__device__`, line 463) - shared
  between the path tracer and the AOV kernel.

The triangle code is full Moller-Trumbore (no
shortcuts; back-face accepted; barycentrics returned at
`bary_u` / `bary_v` for downstream UV interpolation).
The sphere code is the standard quadratic with both
roots considered + spherical UV mapping.

The same RR_HD primitives run in the host test suite
(`tests/geometry_tests.cpp`) so the device behaviour is
verified by construction.

### 2.4 Material evaluation is on the GPU

`evaluateMaterial(view)` in `CudaMaterialGraph.cuh:99`
is `RR_HD inline`. Called from
`override_material_with_graph` (`CudaTestKernel.cu:537`),
which itself is called from each production kernel
right after the per-hit material lookup. The opcode loop
walks `view.ops[]` linearly, fills a 32-slot stack pool,
then walks `view.terminals[]` to compose
`MaterialEvalResult{baseColor, emissionColor,
emissionStrength}`. No dynamic allocation; no host
callbacks; no recursion.

### 2.5 Relativistic effects are on the GPU

`aberrateDirection`, `dopplerFactor`,
`searchlightFactor`, `applyDopplerColor` all live in
`relativity/RelativityMath.h` as `RR_HD inline`. The
kernels invoke them per-pixel:

- Aberration: `k_render_scene:262`,
  `trace_one_path:565`, `k_render_aovs` (same shape).
- Doppler colour: `k_render_scene:423`,
  `trace_one_path:642`, `k_render_aovs`.
- Searchlight: `k_render_scene:430`,
  `trace_one_path:646`, `k_render_aovs`.

### 2.6 Memory ownership is safe

Every device buffer is owned by an `rr::gpu::GpuBuffer<T>`
RAII handle. The renderer never holds a raw `cudaMalloc`-
ed pointer beyond the lifetime of the function that
called `allocate(...)`. `CudaRenderer::run_kernel_render`
allocates a `GpuBuffer<float> dev` on the stack; on
return (success or any of the `result.message = ...`
early returns) the buffer's destructor frees the device
memory. Same pattern in `render_aovs` (six parallel
`GpuBuffer`s).

CUDA error state is drained at every step: `cudaGetLastError`
both before launch (clear sticky) and after
`cudaDeviceSynchronize` (drain new errors). No silent
failures.

### 2.7 Host's only per-pixel loops are in I/O

Two CPU loops over pixels exist OUTSIDE the kernels:

- `Image::save_ppm` (`Image.cpp:89-90`): clamp HDR
  floats to 8-bit and write the P6 binary body.
- `AOV::save_ppm` (`AOV.cpp:49,63`): scalar-AOV
  normalisation + grayscale 8-bit write.

Both are PPM serialisation that runs ONCE per saved
file, AFTER the GPU has produced the framebuffer. They
are CPU **I/O**, not CPU rendering, and per the master
rules' allowed CPU work list ("save image files"). No
violation.

`main.cpp` has one CPU loop that builds a 32x32
checkerboard for the M16 textured-material demo (the
"deliverable" loop inside `--render`). This is host-side
**texture authoring** - the texture is then uploaded to
the GPU and the actual sampling happens in the kernel
(`sample_texture` call inside `k_render_scene`). No
per-pixel rendering on the CPU.

## 3. Broken / fake / scaffold parts

### 3.1 OptiX is lifecycle-only

`src/optix/OptixBackend.cpp` is real: it calls
`optixInit` + `optixDeviceContextCreate` (lines 86, 94)
when `RR_HAS_OPTIX` is defined. It manages the
`OptixDeviceContext` correctly with move semantics +
shutdown.

`src/optix/OptixRenderer.cpp` is **a stub**. The single
public function `render_placeholder(width, height)`
(line 7) returns:

> `"OptiX render: scaffold only - rendering arrives in M15.4 (see docs/OPTIX_BACKEND_PLAN.md). Runtime initialisation OK."`

No raygen / miss / closest-hit programs. No SBT. No
acceleration structure. No `optixLaunch` call. The CUDA
kernel path is the only path that actually renders
today.

This is **honestly named** and clearly documented (the
function is `render_placeholder`, not `render`; the
`OptiX_BACKEND_PLAN.md` spec exists), so it is not a
hidden stub - but anything classified as "production
GPU rendering" must come from the CUDA path, not the
OptiX path.

### 3.2 Diagnostic kernels still ship in production

Four `__global__` kernels live in
`src/cuda/CudaTestKernel.cu` that are **early-milestone
diagnostics**, not production:

| Kernel                       | Use today                                                                |
|------------------------------|---------------------------------------------------------------------------|
| `k_gradient_rgba32f`         | M6 framebuffer-writes test. Reachable via `CudaRenderer::render_gradient`.|
| `k_camera_rays_visualize`    | M7 ray-direction visualisation. Reachable via `CudaRenderer::render_camera_rays`.|
| `k_sphere_visualize`         | M8 single-sphere visualisation. Reachable via `CudaRenderer::render_sphere`.|
| `k_sphere_relativistic`      | M9 single-sphere with relativity. Reachable via `CudaRenderer::render_relativistic_sphere`.|

They are **not bugs** - they are **valid kernels** that
correctly run on the GPU. But they are no longer used by
the production `--render` path; that uses
`render_scene` / `render_pathtrace` exclusively.
Carrying them forward in the rewrite would mean
shipping four code paths whose only job is to test the
M6-M9 milestones we are already past.

### 3.3 Path-tracer's pseudo-progressive accumulation

`k_path_trace` accumulates samples in a per-thread
register Vec3 (`accum`, line 669) and writes the **mean**
to the framebuffer at the end of one launch (line 684).
Across MULTIPLE launches the kernel does NOT accumulate
- each launch overwrites the framebuffer with that
launch's mean.

This is consistent with the M14 docstring ("the kernel
itself is single-launch; across multiple launches a
caller can blend the framebuffer themselves for true
progressive refinement") - so it's not a bug per se -
but the **server's progressive-render workflow does not
exist yet** to do the host-side blending. v1 final-frame
renders work because they are one launch with `spp =
target`.

A future progressive workflow either needs:
- a host-side accumulator (Image of running mean +
  re-upload + re-launch with per-launch `spp = batch`),
  or
- a device-side persistent accumulator framebuffer the
  kernel reads + writes in place.

Until that lands, "progressive" is a single-launch
batch, not a per-frame stream.

## 4. Violations of the GPU-only rule

**None found.** Every per-pixel / per-ray operation in
the rendering hot path runs in a `__global__` kernel on
the device. The two CPU per-pixel loops that exist are
both PPM I/O (allowed CPU work per the master rules).
The `main.cpp` checkerboard loop is host-side texture
authoring, not rendering.

The rendering side respects every master-rule
constraint:

- Per-pixel work runs on the GPU.
- Per-ray work runs on the GPU.
- Intersection runs on the GPU.
- Shading runs on the GPU.
- Material graph evaluation runs on the GPU.
- Relativistic effects (aberration, Doppler,
  searchlight) run on the GPU.
- The CPU's only roles are: orchestrate launches,
  upload scene data, download the result, save PPM.

The OptiX renderer is honestly labeled as a placeholder
and does not render at all - so it cannot violate the
rule, but it also does not contribute to the rendering
output today.

## 5. Implications for the rewrite

The CUDA rendering pipeline is **fundamentally sound**:
the kernels are real, the math is shared with the host
test suite via `RR_HD inline` so per-primitive
correctness is verified, memory ownership is RAII, the
host does no rendering work. The `CudaTestKernel.cu`
file's structure (917 lines, four diagnostic + three
production kernels in one TU) is the main hygiene issue
- the rewrite should:

- Split `CudaTestKernel.cu` into per-kernel files
  (`k_render_scene.cu`, `k_path_trace.cu`,
  `k_render_aovs.cu`).
- Drop the M6-M9 diagnostic kernels from the production
  build (move them to a separate CMake target
  `rr_diagnostics` or to `tools/`).
- Replace the `render_placeholder` stub in
  `optix/OptixRenderer.cpp` with the real OptiX
  pipeline once the rest of the M15 spec slices land.
- Add the host-side or device-side accumulator the
  progressive workflow needs.

Per the focused scope of this audit step, structural
changes (CMake, server hardening, material-graph
duplication) are NOT in scope here - they are the
classification doc's territory. The rendering hot path
is **safe to reuse as-is** with the splits above.

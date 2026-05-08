# Stage 6–9 Audit B2 — CUDA kernel scan

Date: 2026-04-29
Branch: `relativity-core-v1`
Scope: enumerate where GPU rendering actually happens. Restricted
to **`src/cuda/`** and **`src/gpu/`** per the prompt; CPU renderer
files (renderer/, image/, main.cpp, scene/, geometry/) are out of
scope for B2 (B1 covered them).

This doc is documentation. No source files are modified.

---

## 1. CUDA kernels (`__global__`)

All five kernels live in **`src/cuda/CudaTestKernel.cu`**. There are
no `__global__` declarations anywhere else under `src/cuda/` or
`src/gpu/`.

| # | Kernel | File:line | Stage | Real impl? |
|---|---|---|---|:---:|
| 1 | `k_gradient_rgba32f` | `CudaTestKernel.cu:34` | 6 | ✓ |
| 2 | `k_camera_rays_visualize` | `CudaTestKernel.cu:72` | 7 | ✓ |
| 3 | `k_sphere_visualize` | `CudaTestKernel.cu:119` | 8 | ✓ |
| 4 | `k_sphere_relativistic` | `CudaTestKernel.cu:192` | 10 | ✓ |
| 5 | `k_render_scene` | `CudaTestKernel.cu:284` | 6B / 7C / 8B / 9B (incremental) | ✓ |

All five are real implementations. None are stubs / placeholders.

For each kernel, the corresponding host launcher (the function the
host calls to enqueue the kernel) lives in the same translation
unit and is declared in `src/cuda/CudaKernels.cuh`:

| Kernel | Launcher | File:line |
|---|---|---|
| `k_gradient_rgba32f` | `launch_gradient_rgba32f` | `CudaTestKernel.cu:51` |
| `k_camera_rays_visualize` | `launch_camera_rays_visualize` | `CudaTestKernel.cu:89` |
| `k_sphere_visualize` | `launch_sphere_visualize` | `CudaTestKernel.cu:157` |
| `k_sphere_relativistic` | `launch_sphere_relativistic` | `CudaTestKernel.cu:258` |
| `k_render_scene` | `launch_render_scene` | `CudaTestKernel.cu:467` |

Each launcher: validates dimensions, computes a `dim3 block(16, 16)`
+ `dim3 grid` covering the framebuffer, and issues
`<<<grid, block, 0, stream>>>`. No state hidden in the launcher;
the kernel is the only place the per-pixel work happens.

The host-side render entry points sit one level above the
launchers, in `src/cuda/CudaRenderer.{h,cu}`:

| `CudaRenderer::*` | File:line | Calls launcher |
|---|---|---|
| `render_gradient` | `CudaRenderer.cu:78` | `launch_gradient_rgba32f` |
| `render_camera_rays` | `CudaRenderer.cu:85` | `launch_camera_rays_visualize` |
| `render_sphere` | `CudaRenderer.cu:95` | `launch_sphere_visualize` |
| `render_relativistic_sphere` | `CudaRenderer.cu:111` | `launch_sphere_relativistic` |
| `render_scene` | `CudaRenderer.cu:135` | `launch_render_scene` |

Each `render_*` allocates a `GpuBuffer<float>`, calls the launcher,
drains CUDA errors, `cudaDeviceSynchronize`s, and downloads the
framebuffer into an `rr::image::Image`. None of these per-frame
host helpers iterate over pixels or rays.

## 2. Device functions

There are **no standalone `__device__`-only functions** under
`src/cuda/`. There are also no plain `__device__` declarations
(verified by `grep -rn "^__device__\|^[[:space:]]*__device__" src/cuda/`
— zero matches).

What does exist in `src/cuda/` and is callable from kernels:

| Symbol | File:line | Category | Real impl? |
|---|---|---|:---:|
| `intersect_sphere(ray, sphere, t_min, t_max) -> Hit` | `CudaIntersection.cuh:37` | RR_HD inline | ✓ |
| `intersect_triangle(ray, v0, v1, v2, t_min, t_max) -> Hit` | `CudaIntersection.cuh:100` | RR_HD inline | ✓ |

`RR_HD inline` means: the macro expands to `__host__ __device__`
under nvcc and to nothing on a host-only compiler, so the function
is callable from both host and device. The kernel is the only
caller in the rewrite tree (verified in audit B1: zero host call
sites).

The kernel additionally calls these RR_HD helpers, which are
**defined outside `src/cuda/`** (in their respective module
headers, included from `src/cuda/CudaKernels.cuh` /
`src/cuda/CudaTestKernel.cu`). The prompt restricts inspection to
`src/cuda/` + `src/gpu/`, so they are listed here only as call
sites, not deep-inspected:

| Helper | Defined in (out of B2 scope) | Called by kernels in |
|---|---|---|
| `generate_camera_ray` | `camera/CameraRay.h` (RR_HD inline) | `CudaTestKernel.cu` (4 sites) |
| `aberrateDirection` | `relativity/RelativityMath.h` (RR_HD inline) | `CudaTestKernel.cu` (2 sites) |
| `dopplerFactor` | `relativity/RelativityMath.h` (RR_HD inline) | `CudaTestKernel.cu` (2 sites) |
| `applyDopplerColor` | `relativity/RelativityMath.h` (RR_HD inline) | `CudaTestKernel.cu` (2 sites) |
| `searchlightFactor` | `relativity/RelativityMath.h` (RR_HD inline) | `CudaTestKernel.cu` (2 sites) |

Three "thin re-export" headers exist under `src/cuda/` so kernel
TUs can `#include` from cuda/ namespaces and signal intent. They
declare no new functions today; they are forward-compatible
landing pads for device-specific overrides:

| File | Re-exports |
|---|---|
| `src/cuda/CudaLight.cuh` | `lighting/Light.h` |
| `src/cuda/CudaMaterial.cuh` | `material/MaterialTypes.h` |
| `src/cuda/CudaMesh.cuh` | (defines `CudaMeshView` POD; pulls `geometry/Mesh.h` + `geometry/Triangle.h` + `math/Transform.h`) |

## 3. Which kernel generates camera rays

**Four kernels generate camera rays on device, all via
`rr::camera::generate_camera_ray`.** The function is `RR_HD inline`
and is called from inside the kernel body, so ray generation
happens on the GPU.

| Kernel | Call site | Expression |
|---|---|---|
| `k_camera_rays_visualize` | `CudaTestKernel.cu:78` | `auto ray = generate_camera_ray(cam, x, y, width, height);` |
| `k_sphere_visualize` | `CudaTestKernel.cu:126` | `auto ray = generate_camera_ray(cam, x, y, width, height);` |
| `k_sphere_relativistic` | `CudaTestKernel.cu:204` | `auto ray = generate_camera_ray(cam, x, y, width, height);` |
| `k_render_scene` | `CudaTestKernel.cu:293` | `auto ray = generate_camera_ray(scene.camera, x, y, width, height);` |

`k_gradient_rgba32f` does not generate rays — it only writes a
UV pattern and is the diagnostic kernel.

## 4. Which function intersects spheres

**`rr::cuda::intersect_sphere` in `src/cuda/CudaIntersection.cuh:37`.**
`RR_HD inline`. Solves the quadratic `a t² + 2 b t + c = 0` with
near-root preference; falls back to the far root when the near root
is out of range. Populates `Hit::position`, `normal` (outward unit
normal via `(p − c) / r`), `t`, `material_index`, and a spherical
`uv`.

Real implementation — not a stub.

Called from these kernel sites:

| Kernel | Call site | Notes |
|---|---|---|
| `k_sphere_visualize` | `CudaTestKernel.cu:127` | Single-sphere diagnostic. |
| `k_sphere_relativistic` | `CudaTestKernel.cu:213` | Single-sphere relativistic. |
| `k_render_scene` | `CudaTestKernel.cu:310` | Inside the sphere closest-hit loop over `scene.spheres[0 .. sphere_count − 1]`. |

## 5. Which function intersects triangles

**`rr::cuda::intersect_triangle` in `src/cuda/CudaIntersection.cuh:100`.**
`RR_HD inline`. Möller-Trumbore. Treats the triangle as
double-sided (only edge-parallel rays are rejected). Returns the
front-face outward normal of the CCW winding `(v0, v1, v2)` and
populates `Hit::bary_u` / `bary_v`.

Real implementation — not a stub.

Called from one kernel site:

| Kernel | Call site | Notes |
|---|---|---|
| `k_render_scene` | `CudaTestKernel.cu:328` | Inside the triangle closest-hit loop over `mesh.triangles[0 .. mesh.triangle_count − 1]`, using the running `t_max` from the sphere loop so spheres and triangles compete for the nearest-hit slot. |

## 6. Which function evaluates materials

**There is no dedicated function for material evaluation.** The
kernel reads `MaterialParams` inline at the base-shade step inside
`k_render_scene`:

```
CudaTestKernel.cu:356
    if (best.material_index >= 0
     && best.material_index < scene.material_count
     && scene.materials != nullptr) {
        const auto& mat = scene.materials[best.material_index];
        albedo   = mat.baseColor;
        emission = mat.emissionColor * mat.emissionStrength;
    }
```

| Kernel | Section | Real impl? |
|---|---|:---:|
| `k_render_scene` | base-shade step (lines 356–362) | ✓ |

When `material_index` is out of range or `scene.materials ==
nullptr`, the kernel falls back to the neutral default
(`baseColor = (0.8, 0.8, 0.8)`, no emission), matching
`MaterialParams`'s default-constructed values. The kernel reads
only `baseColor` and `emissionColor * emissionStrength` —
`roughness`, `metallic`, `specular`, and `transmission` are
uploaded for forward compatibility but unused by Stage 9B's
shading.

The material lookup is local to `k_render_scene` only;
`k_sphere_relativistic` uses the older normal-as-color shade and
does not consume `MaterialParams` even when `Sphere::material_index`
is set.

`src/cuda/CudaMaterial.cuh` is a thin re-export of
`material/MaterialTypes.h` — it does **not** declare any
evaluation helper today; that is a forward-compat landing pad for
the future BSDF.

## 7. Which function evaluates lights

**There is no dedicated function for light evaluation.** The
kernel iterates `scene.lights` inline inside `k_render_scene` with
a `switch` on `LightType`:

```
CudaTestKernel.cu:364
    if (scene.light_count > 0 && scene.lights != nullptr) {
        Vec3 direct  = (0, 0, 0);
        Vec3 ambient = (0, 0, 0);
        bool has_env = false;

        for (int li = 0; li < scene.light_count; ++li) {
            const Light& L = scene.lights[li];
            const Vec3 light_color = L.color * L.intensity;
            switch (L.type) {
                case LightType::Directional: { ... direct += light_color * lambert; }
                case LightType::Point:       { ... direct += light_color * (lambert / d²); }
                case LightType::Environment: { ambient += light_color; has_env = true; }
                case LightType::Area:        { /* PLACEHOLDER, skipped */ }
            }
        }

        if (!has_env) ambient += (0.05, 0.05, 0.05);   // implicit floor

        color = albedo * (direct + ambient) + emission;
    } else {
        // Stage 8B facing-ratio fallback when no lights uploaded.
        ...
    }
```

| Kernel | Section | Real impl? |
|---|---|:---:|
| `k_render_scene` | lighting step (lines 364–432) | ✓ for Point + Directional + Environment; Area is a PLACEHOLDER (skipped) |

Per-LightType behaviour:

| `LightType` | Status | Notes |
|---|---|---|
| `Point` | real | `light_color * max(0, N·L) / d²`, with a small `1e-4` epsilon floor on `d²` to avoid blow-up at the singular point. |
| `Directional` | real | `light_color * max(0, N·-direction)`. Photons travel along `L.direction`; "to-light" is its negation. |
| `Environment` | real, basic | Treated as ambient. Adds `light_color` to an ambient accumulator, no directional dependence. |
| `Area` | **PLACEHOLDER** | Switch case is empty; the kernel skips area lights. Real area-light sampling lands with the path tracer. |

No shadow rays. The kernel adds each contribution unconditionally,
per the Stage 9B "no shadows yet" rule.

`src/cuda/CudaLight.cuh` is a thin re-export of `lighting/Light.h`
— it does not declare evaluation helpers; that is forward-compat
landing pad for sampling / eval / pdf helpers when the path tracer
arrives.

## 8. Which function applies relativity effects

**There is no dedicated relativity function inside `src/cuda/`.**
The kernels call the four `RR_HD inline` helpers from
`src/relativity/RelativityMath.h` (out of B2's strict scope, but
the call sites are in cuda/):

| Helper | Phase | Used by which kernel | Call sites in `CudaTestKernel.cu` |
|---|---|---|---|
| `aberrateDirection(beta_vec, dir)` | step 2 (pre-intersection) | `k_sphere_relativistic`, `k_render_scene` | lines 208, 298 |
| `dopplerFactor(beta_vec, dir)` | step 5 (post-shade) | `k_sphere_relativistic`, `k_render_scene` | lines 232, 441 |
| `applyDopplerColor(rgb, D, strength)` | step 6 (post-shade) | `k_sphere_relativistic`, `k_render_scene` | lines 237, 446 |
| `searchlightFactor(D)` (returns `D⁴`) | step 7 (post-shade) | `k_sphere_relativistic`, `k_render_scene` | lines 243, 452 |

All four helpers are guarded by the `enable_aberration` /
`enable_doppler` / `enable_searchlight` toggles on
`RelativityParams`, so an observer with `enable_*` all `false`
runs the kernel as a non-relativistic renderer. Real
implementations — not stubs.

The other two kernels (`k_gradient_rgba32f`, `k_camera_rays_visualize`,
`k_sphere_visualize`) **do not** apply relativity; they are the
unit / diagnostic kernels.

## Inventory: real vs placeholder

| Item | File | Status |
|---|---|---|
| `k_gradient_rgba32f` | `cuda/CudaTestKernel.cu` | real |
| `k_camera_rays_visualize` | `cuda/CudaTestKernel.cu` | real |
| `k_sphere_visualize` | `cuda/CudaTestKernel.cu` | real |
| `k_sphere_relativistic` | `cuda/CudaTestKernel.cu` | real |
| `k_render_scene` | `cuda/CudaTestKernel.cu` | real (sphere + triangle + materials + lights + relativity all wired) |
| `intersect_sphere` | `cuda/CudaIntersection.cuh` | real |
| `intersect_triangle` | `cuda/CudaIntersection.cuh` | real |
| Material evaluation | inline in `k_render_scene` | real (reads `baseColor` + emission only) |
| Light evaluation | inline in `k_render_scene` | real for Point + Directional + Environment; **placeholder for Area** (skipped) |
| Relativity application | call sites in `k_sphere_relativistic` + `k_render_scene` | real (helpers in `relativity/`, not `cuda/`) |
| `cuda/CudaMaterial.cuh` | thin re-export | forward-compat landing pad; no helpers yet |
| `cuda/CudaLight.cuh` | thin re-export | forward-compat landing pad; no helpers yet |
| `cuda/CudaMesh.cuh` | `CudaMeshView` POD | real (data only) |
| `cuda/CudaScene.cuh` | `CudaSceneView` POD | real (data only) |

## `src/gpu/` host-side surface (no kernels live here)

`src/gpu/` is the backend-agnostic device + memory layer; it
contains no `__global__` kernels and no `__device__` code. Listed
here for completeness because B2's scope includes `src/gpu/`:

| Symbol | File:line | Role |
|---|---|---|
| `gpu_alloc / gpu_free` | `gpu/GpuBuffer.cpp:9, 18` | Byte-level dispatch to `cuda::cuda_alloc / cuda_free` (or no-op when `RR_HAS_CUDA` undefined). |
| `gpu_copy_host_to_device / gpu_copy_device_to_host` | `gpu/GpuBuffer.cpp:26, 35` | Same dispatch pattern for `cudaMemcpy`. |
| `GpuBuffer<T>` | `gpu/GpuBuffer.h` | Move-only RAII typed wrapper around the byte primitives. |
| `GpuDevice` | `gpu/GpuDevice.{h,cpp}` | Device POD + `enumerate_devices()` (delegates to `cuda::query_devices`). |
| `GpuMesh::upload_vertices / upload_triangles / set_metadata / upload_from` | `gpu/GpuMesh.cpp:5, 23, 41, 46` | Host-side mesh upload manager; owns two `GpuBuffer<T>` instances. |
| `GpuScene::upload_camera / upload_relativity / upload_spheres / upload_mesh / upload_materials / upload_lights / reset_device / clear` | `gpu/GpuScene.cpp:7, 13, 22, 46, 50, 71, 92, 106` | Host-side scene upload manager. |

None of these run rendering. They allocate device memory, copy host
→ device, and surface accessors that the renderer reads at launch
time. All host-side. All real implementations.

## Verdict

GPU rendering happens in exactly **one file**:
`src/cuda/CudaTestKernel.cu`, across **five `__global__` kernels**.
Two `RR_HD inline` device functions in `src/cuda/CudaIntersection.cuh`
(`intersect_sphere`, `intersect_triangle`) are the geometry-side
helpers; everything else (camera rays, relativity, light + material
evaluation) is either inline inside the kernel or imported from
sibling modules' RR_HD headers (`camera/`, `relativity/`).

There is **one acknowledged placeholder**: `LightType::Area` in
`k_render_scene`'s light loop. Every other piece audited is a
real implementation.

`src/gpu/` contains no kernels — by design. It is the backend-
agnostic device + memory layer.

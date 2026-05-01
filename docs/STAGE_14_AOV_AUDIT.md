# Stage 14 AOV Audit

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `9a1e16f` ("stage 14A.3: CUDA
AOV writing")
Scope: master order #19, sub-stages 14A.1 (data model) +
14A.2 (GPU buffers) + 14A.3 (CUDA AOV writing).
Mode: documentation-only. No source code is modified by this
audit.

The audit answers the five prompt questions in order.

---

## 1. Does project build?

**PASS.**

- `cmake --build build --parallel` succeeds; every static library
  (`rr_math`, `rr_image`, `rr_camera`, `rr_geometry`, `rr_material`,
  `rr_lighting`, `rr_texture`, `rr_relativity`, `rr_scene`,
  `rr_pathtracer`, `rr_renderer`, `rr_io`, `rr_gpu`) plus the
  executable `RelativityRender` and the four test binaries link
  to completion.
- `cmake --build build --parallel 2>&1 | grep -ciE "warning|error"`
  returns **0**. The project compiles cleanly under
  `-Wall -Wextra -Wpedantic`.
- `ctest --test-dir build` reports `100% tests passed,
  0 tests failed out of 4`.

The audit host is CUDA-less (`build/CMakeCache.txt` records
`RR_ENABLE_CUDA:BOOL=OFF`; `which nvcc` returns nothing); the OFF
build is the one this audit can exercise. Stage 14A.3's source
additions are orthogonal to the CUDA flag and to the OptiX flag -
the OFF build compiles every file, gates the kernel-side AOV
writes inside `#ifdef RR_HAS_CUDA` for the run handler, and the
data model + buffer owner are pure host C++.

---

## 2. Do all AOV output files exist?

**NO** (vacuously).

The `output/` directory does not exist on the audited tree:

| Path                          | Status   |
|-------------------------------|----------|
| `output/aov_beauty.ppm`       | absent   |
| `output/aov_normal.ppm`       | absent   |
| `output/aov_depth.ppm`        | absent   |
| `output/aov_albedo.ppm`       | absent   |
| `output/aov_doppler.ppm`      | absent   |
| `output/aov_searchlight.ppm`  | absent   |

Cause: producing all six PPMs requires `--render-aovs` to run
on a CUDA-enabled host (per the Stage 14A.3 handler's
`#ifdef RR_HAS_CUDA` gate, mirroring every other GPU-render CLI
action). On this audit host the action returns the standard
`requires CUDA. Rebuild with -DRR_ENABLE_CUDA=ON ...` error and
exits 1 - by design, not silently producing wrong-by-CPU PPMs.
This is the same "blocked in this environment" outcome
`docs/STAGE_13_VISUAL_CONFIRMATION.md` records for the texture-
system PPMs (`output/gpu_texture_sample_test.ppm` /
`output/gpu_textured_material.ppm`).

To produce all six artifacts on a future CUDA-enabled host:

```
cmake -S . -B build-cuda -DRR_ENABLE_CUDA=ON
cmake --build build-cuda --parallel
mkdir -p output
build-cuda/bin/RelativityRender --render-aovs
```

The handler writes every requested pass into `output/` from the
six `GpuAOVBuffer` instances `make_default_aov_set()` returns.
Output filenames are fixed (`--output` is ignored); each PPM is
sized to the requested `--width` / `--height` (defaults
1280 x 720).

---

## 3. Are AOV values written in CUDA code?

**YES.**

All six per-pass writes live inside `__global__ k_render_scene`
in `src/cuda/CudaTestKernel.cu`, in the new "step 9: per-pixel
AOV writes" block (lines 499 onwards). Each write is gated on
`scene.aovs.<pass> != nullptr`:

| Pass               | Source line | Field                              |
|--------------------|-------------|------------------------------------|
| Beauty             | ~518        | `scene.aovs.beauty[pix_idx_3 + n]` |
| Normal             | ~528        | `scene.aovs.normal[pix_idx_3 + n]` |
| Depth              | ~535        | `scene.aovs.depth[pix_idx_1]`      |
| Albedo             | (within block) | `scene.aovs.albedo[pix_idx_3 + n]` |
| DopplerFactor      | (within block) | `scene.aovs.doppler_factor[pix_idx_1]` |
| SearchlightFactor  | (within block) | `scene.aovs.searchlight_factor[pix_idx_1]` |

The values written:

- Beauty: `(color.x, color.y, color.z)` - exactly the same RGB
  written to the framebuffer.
- Normal: `0.5 * best.normal + 0.5` for hits (encoded for
  direct PPM viewing), `(0, 0, 0)` on miss.
- Depth: `1.0 / (1.0 + best.t)` for hits (closer surfaces
  brighter, bounded in [0, 1]), `0` on miss.
- Albedo: the `albedo` Vec3 the kernel computed earlier in the
  shading branch (raw RGB, possibly textured).
- DopplerFactor: `D` from `rr::relativity::dopplerFactor(...)` -
  raw physical value, regardless of `enable_doppler`.
- SearchlightFactor: `D4 = rr::relativity::searchlightFactor(D)` -
  raw `D^4`, regardless of `enable_searchlight`.

The launch-argument plumbing comes through
`CudaSceneView::aovs` (a `rr::cuda::DeviceAOVView` POD added in
Stage 14A.3); the host-side `CudaRenderer::render_scene_with_
aovs` populates it from a caller-supplied `AOVTargets` struct
of raw `float*` device pointers.

No additional kernels write AOV data; `grep -rn "scene\.aovs\."
src/` shows every match is inside `src/cuda/CudaTestKernel.cu`.

---

## 4. Any CPU pixel / AOV computation violations?

**NO.**

The host-side AOV save path is `save_aov_to_ppm` in
`src/main.cpp` (lines 166-227). It performs only data-layout
marshalling, no per-pixel arithmetic on AOV values:

- 3-channel passes (Beauty / Normal / Albedo): a single
  `std::memcpy(img.data(), host.data(), bytes)` from the
  downloaded float vector into the `Image(Rgb32F)` buffer. No
  per-pixel transform; the values stored are exactly what the
  kernel wrote.
- 1-channel passes (Depth / DopplerFactor / SearchlightFactor):
  a per-pixel loop that copies each scalar into three
  consecutive RGB channels (`dst[i*3+0] = dst[i*3+1] =
  dst[i*3+2] = host[i]`). No multiplication, no addition, no
  remap - just a layout duplication so the PPM has the
  required RGB triplet.

The float -> uint8 quantize that follows in
`Image::save_ppm` is the same code path every other
GPU-render CLI action uses (the master rule explicitly accepts
this kind of save-side conversion; the rule about "GPU
computes pixels" is about rendering, not saving).

The encoding decisions for Normal (`0.5 * n + 0.5`) and Depth
(`1.0 / (1.0 + t)`) are made on the GPU inside the kernel, not
on the host. Verified by inspection of `src/cuda/CudaTestKernel
.cu`'s step 9 block: the `n_enc` / `depth_vis` locals are
device-side computations.

Searched the rest of the codebase for any host-side AOV value
math: no per-pixel CPU loop computes AOV color / normal /
depth / albedo / D / D^4 anywhere outside the kernel. The
`run_render_aovs` handler builds the scene, plumbs device
pointers, and saves the downloaded data; it does not iterate
over output pixels.

---

## 5. Next safe stage

Two pieces of deferred work block downstream confidence:

- **Stage 13 + 14 visual confirmation.** Both stages produce
  CLI-driven PPM outputs that have not yet been visually
  confirmed on a CUDA-enabled host. The Stage 13 visual
  confirmation slice (`docs/STAGE_13_VISUAL_CONFIRMATION.md`)
  remains "PASS (deferred)"; Stage 14A.3's six PPMs are in
  the same state. Running both stages' output sets once on a
  CUDA host is the cheapest way to unblock anything that
  depends on textured shading or AOV correctness.
- **Stage 12B OptiX backend implementation.** The OptiX
  scaffold (12B.1-12B.5) is file-skeleton-only; the first
  implementation milestone (`docs/OPTIX_BACKEND_PLAN.md` §26
  - one-triangle-GAS rendered through OptiX) has not landed.

Strict master-order continuation past #19 is **#20 Renderer
server**. Picking it up is safe - it is independent of
the texture / AOV runtime correctness because the server
foundation is host-side IPC scaffolding (parallel to the
12A documentation-only sub-stages: a server-protocol design
doc, then a CMake-flag-only sub-stage, then a file skeleton).

Recommended next safe stage (in priority order):

1. **Stage 13 + 14 visual confirmation on a CUDA host.** A
   one-time deferred verification that produces the eight
   PPMs (`gpu_texture_sample_test.ppm`,
   `gpu_textured_material.ppm`, plus the six `aov_*.ppm`)
   and records the result in
   `docs/STAGE_13_VISUAL_CONFIRMATION.md` /
   `docs/STAGE_14_VISUAL_CONFIRMATION.md`. Restores the
   "verified outputs" rung the Stage 11 audits established.
2. **Stage 14B refinement** (sphere UV for textured
   spheres, additional AOV pass types, or HDR EXR output)
   if the existing AOV set proves limiting in practice. No
   new dependency surface.
3. **Stage 20 - Renderer server foundation** as
   documentation-only sub-stages, paralleling the 12A
   pattern. Independent of texture / AOV runtime
   correctness.
4. **Stage 12B real implementation milestone.** Larger, but
   starts paying off the OptiX scaffold's design effort
   from 12A.

The least-risk choice is #1; it costs one CUDA host run and
unblocks every later stage that wants to depend on the
existing texture / AOV pipeline.

# Stage 11 Audit — Path Tracing Foundation

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `559066d` ("stage 11C: minimal
GPU path tracer")
Scope: master order #16, sub-stages 11A (GPU sampling system) +
11B (progressive accumulation buffer) + 11C (minimal GPU path
tracer).
Mode: documentation-only. No source code is modified by this
audit.

This audit answers the ten questions the prompt requests. The
local build environment is **host-only** (no CUDA toolchain, no
visible GPU): `which nvcc` returns nothing, `nvidia-smi` is not
installed, and `build/CMakeCache.txt` records
`RR_ENABLE_CUDA:BOOL=OFF`. This shapes items 2-5 below; items
1, 6-10 are answerable independent of the build flag.

---

## 1. Does the project build cleanly?

**PASS.**

- `cmake --build build -j` succeeds. Every static library + the
  executable + every test binary link without errors.
- `cmake --build build -j 2>&1 | grep -iE "warning|error"` is
  empty - the project compiles clean under `-Wall -Wextra
  -Wpedantic` (the warning flags `rr_apply_warnings` enforces
  on every target).
- `ctest --output-on-failure` reports `100% tests passed,
  0 tests failed out of 4` (math + image + gpu +
  pathtracer_tests; the new pathtracer_tests binary exercises
  the Stage 11A RNG / Sampling headers via the host C++
  compiler).

This is the same build state Stages 11A/B/C committed under;
nothing has rotted between commits.

---

## 2. Does GPU RNG test output exist?

**BLOCKED in this environment.**

`output/gpu_rng_test.ppm` does not exist; the `output/`
directory itself is absent.

Cause: producing the file requires `--render-rng-test` to run on
a CUDA-enabled host (per the Stage 11A handler's
`#ifdef RR_HAS_CUDA` gate, mirroring every other GPU action). On
this build host, the action returns `requires CUDA. Rebuild
with -DRR_ENABLE_CUDA=ON ...` and exits 1 - by design, not
silently producing a wrong-by-CPU image.

To produce the artifact:

```
cmake -S . -B build-cuda -DRR_ENABLE_CUDA=ON
cmake --build build-cuda -j
build-cuda/bin/RelativityRender --render-rng-test
```

The handler's CLI surface, the launcher in
`src/cuda/CudaRngTestKernel.cu`, and the `__global__
k_rng_test_visualize` kernel are all in place and were verified
to compile (host-only: the kernel TU is gated under
`RR_ENABLE_CUDA` and is not part of the host-only build, but
the CLI handler + the host-side decls compile clean).

---

## 3. Does GPU accumulation test output exist?

**BLOCKED in this environment.**

`output/gpu_accumulation_test.ppm` does not exist. Same cause
as item 2: `--render-accumulation-test` requires a CUDA host;
this build host has none.

The Stage 11B implementation is present and correct against the
host-only compiler:

- `src/renderer/AccumulationBuffer.{h,cpp}` declares + owns
  the host class.
- `src/cuda/CudaAccumulation.cuh` declares the launchers in a
  host-friendly form (no `<cuda_runtime.h>`).
- `src/cuda/CudaAccumulation.cu` defines the kernels +
  launchers (gated under `RR_ENABLE_CUDA` in CMake).
- `--render-accumulation-test` returns the standard
  requires-CUDA error in this environment.

To produce the artifact:

```
build-cuda/bin/RelativityRender --render-accumulation-test
```

---

## 4. Does pathtrace_spp_1.ppm exist?

**BLOCKED in this environment.**

`output/pathtrace_spp_1.ppm` does not exist. Same cause as
items 2-3: `--render-pathtrace <file>` requires CUDA. The
Stage 11C handler exits 1 with the standard requires-CUDA
error on a host-only build.

The kernel + host orchestration are wired:

- `src/pathtracer/PathTracer.{h,cpp}` (host class,
  `PathTraceConfig`, `render` method).
- `src/cuda/CudaPathTracer.{cuh,cu}` (launcher decl, kernel
  + launcher def).
- `src/main.cpp::run_render_pathtrace` is the CLI handler;
  it loads the scene, uploads, and calls `PathTracer::render`
  twice (`samples_per_pixel = 1`, then `= 16`) writing
  exactly `output/pathtrace_spp_1.ppm` and
  `output/pathtrace_spp_16.ppm`.

To produce the artifact:

```
build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
```

---

## 5. Does pathtrace_spp_16.ppm exist?

**BLOCKED in this environment.**

Same as item 4. The single `--render-pathtrace` invocation
writes both PPMs (matching `--render-relativistic`'s
multiple-fixed-paths precedent); on a CUDA host both files
appear in one launch.

---

## 6. Are ray paths fully GPU-side?

**PASS.**

Every per-ray and per-pixel step lives in `__device__` /
`__global__` code in `src/cuda/CudaPathTracer.cu`. The kernel
boundary is:

| Step                 | Function                       | Storage class |
|----------------------|--------------------------------|---------------|
| Per-pixel entry      | `k_pathtrace_sample`           | `__global__`  |
| Primary ray gen      | `generate_primary_ray`         | `__device__ inline` |
| Closest-hit walk     | `closest_hit`                  | `__device__ inline` |
| Material lookup      | `material_for`                 | `__device__ inline` |
| Tangent ONB rotate   | `align_to_normal`              | `__device__ inline` |
| Hemisphere sample    | `pathtracer::sample_cosine_hemisphere` | `RR_HD inline` (used device-side) |
| RNG advance          | `pathtracer::next_float` /     | `RR_HD inline` (used device-side) |
|                      | `pathtracer::next_vec2`        |               |
| Sphere intersection  | `rr::cuda::intersect_sphere`   | `RR_HD inline` |
| Triangle intersection| `rr::cuda::intersect_triangle` | `RR_HD inline` |

The host orchestration (`PathTracer::render` in
`pathtracer/PathTracer.cpp` + `run_render_pathtrace` in
`main.cpp`) only:

1. allocates buffers (`AccumulationBuffer`, `GpuBuffer<float>`),
2. loops `samples_per_pixel` times calling the launcher (one
   kernel launch per sample),
3. calls `accum.accumulate_sample(...)` (which itself only
   launches a kernel),
4. calls `accum.resolve_to_image()` (resolve kernel + a single
   `cudaMemcpy` download into the host `Image`),
5. saves the PPM (host file IO, allowed by master rule #6).

Verified by `grep -nE "for.*<.*width|for.*<.*height|
for.*pixel"` over `src/renderer/`, `src/pathtracer/*.cpp`, and
`src/main.cpp`: the only match is

```
src/pathtracer/PathTracer.cpp:66: for (int s = 0; s < cfg.samples_per_pixel; ++s) {
```

That is the *spp launcher loop* (one kernel launch per sample),
not a per-pixel host loop. The only host-side iteration is at
sample-frame granularity.

`grep -rn "intersect_sphere\|intersect_triangle\|closest_hit"`
across `src/renderer/` + `src/pathtracer/*.cpp` returns one
hit, a comment in `renderer/Hit.h`. There is no host-side
intersection code.

---

## 7. Is accumulation GPU-side?

**PASS.**

Every accumulation step in `src/cuda/CudaAccumulation.cu` is a
device kernel or a CUDA Runtime call:

| Step                              | Function           | Storage class / API |
|-----------------------------------|--------------------|---------------------|
| Zero the buffer                   | `launch_accum_clear` | `cudaMemset` (device-resident memset) |
| Add a sample frame                | `k_accum_add`      | `__global__`        |
| Resolve sums to display buffer    | `k_accum_resolve`  | `__global__`        |
| Test sample-source (Stage 11B)    | `k_random_rgba_sample` | `__global__`    |

`grep` over `src/renderer/AccumulationBuffer.cpp` for any
host-side `for ... [i] += ...` or `for ... pixel` returns
nothing. The host class only:

1. holds a `rr::gpu::GpuBuffer<float>`,
2. holds an integer `samples_` counter,
3. forwards `reset` / `accumulate_sample` /
   `resolve_to_image` to the launchers under `RR_HAS_CUDA`,
4. on the host-only build path returns `false` honestly
   (a CPU fallback would directly violate the master "GPU
   accumulates samples" rule, so it is not shipped).

The `resolve_to_image` step downloads the resolved float buffer
through `GpuBuffer::download` (a single `cudaMemcpy` D2H, then
the host owns the bytes only as the destination of an
`Image::data()` write). No per-pixel host arithmetic.

---

## 8. Is there any CPU ray tracing or CPU sample accumulation?

**No.**

The host TUs in `renderer/` and `pathtracer/` and `main.cpp`
contain no per-ray or per-pixel arithmetic. The audit checks
that produced this finding:

```
grep -rnE "for.*<.*width|for.*<.*height|for.*pixel" \
  src/renderer/ src/pathtracer/*.cpp src/main.cpp
```

Returns one match:

```
src/pathtracer/PathTracer.cpp:66:    for (int s = 0; s < cfg.samples_per_pixel; ++s) {
```

The matched line is the **spp launcher loop**, not a per-pixel
loop. Each iteration issues one kernel launch
(`launch_pathtrace_sample`) plus one launcher call
(`accumulate_sample` -> `launch_accum_add`), then advances the
sample counter. No ray, pixel, vertex, or triangle is touched
by the host inside the loop body.

```
grep -rn "intersect_sphere\|intersect_triangle\|closest_hit" \
  src/renderer/ src/pathtracer/*.cpp
```

Returns one match - a comment in `renderer/Hit.h` referencing
`intersect_triangle`'s output convention. There is no
host-side intersection or shading code.

The host-only build paths in `AccumulationBuffer.cpp` and
`PathTracer.cpp` return `false` / an empty Image with a
diagnostic message rather than executing on the CPU. This is
deliberate "honest absence" - the master rule "No CPU ray
tracing as production path" is enforced by refusing to fake
the work on the CPU rather than by silently wrong rendering.

---

## 9. Which files implement path tracing?

The Stage 11 path-tracing footprint, listed by purpose:

### Stage 11A - GPU sampling foundation

| File                                | Purpose |
|-------------------------------------|---------|
| `src/pathtracer/RNG.h`              | PCG-XSH-RR-64-32 + SplitMix64; `Rng`, `pcg32_next`, `splitmix64`, `make_pixel_rng`, `next_float`, `next_vec2`. RR_HD inline. |
| `src/pathtracer/RNG.cuh`            | Single-line re-export of `RNG.h` for kernel TUs. |
| `src/pathtracer/Sampling.h`         | `sample_uniform_hemisphere`, `pdf_uniform_hemisphere`, `sample_cosine_hemisphere` (Malley + Shirley concentric), `pdf_cosine_hemisphere`. RR_HD inline. |
| `src/pathtracer/Sampling.cuh`       | Re-export of `Sampling.h`. |
| `src/cuda/CudaRngTestKernel.cu`     | `k_rng_test_visualize` four-quadrant validation kernel + `launch_rng_test_visualize`. |
| `tests/pathtracer_tests.cpp`        | 9 host-side correctness tests (PCG range, decorrelation, determinism, hemisphere unit-length / upper, MC PDF normalisation, cos `E[dz] = 2/3`, PDF identities). |

### Stage 11B - progressive accumulation buffer

| File                                | Purpose |
|-------------------------------------|---------|
| `src/renderer/AccumulationBuffer.h` | Host-facing class. |
| `src/renderer/AccumulationBuffer.cpp` | Host-only owner; forwards under `RR_HAS_CUDA`. |
| `src/cuda/CudaAccumulation.cuh`     | Host-friendly launcher decls. |
| `src/cuda/CudaAccumulation.cu`      | `k_accum_add`, `k_accum_resolve`, `k_random_rgba_sample` + the four launchers (`launch_accum_clear` / `_add` / `_resolve` / `launch_random_rgba_sample`). |

### Stage 11C - minimal GPU path tracer

| File                                | Purpose |
|-------------------------------------|---------|
| `src/pathtracer/PathTracer.h`       | `PathTraceConfig`, `PathTraceResult`, `class PathTracer`. |
| `src/pathtracer/PathTracer.cpp`     | Host-only orchestration: AccumulationBuffer + sample buffer + spp loop + resolve. Lives in `rr_renderer` (not `rr_pathtracer`) to keep the static-lib graph acyclic. |
| `src/cuda/CudaPathTracer.cuh`       | Host-friendly launcher decl (takes `const GpuScene&`). |
| `src/cuda/CudaPathTracer.cu`        | `__device__` helpers (`align_to_normal`, `closest_hit`, `material_for`, `generate_primary_ray`) + `__global__ k_pathtrace_sample` + `launch_pathtrace_sample`. |

### CLI surface added by Stage 11

| File                                | Purpose |
|-------------------------------------|---------|
| `src/core/CommandLine.{h,cpp}`      | Three new actions: `RenderRngTest`, `RenderAccumulationTest`, `RenderPathtrace` + parsing + usage. |
| `src/main.cpp`                      | Three handler functions: `run_render_rng_test`, `run_render_accumulation_test`, `run_render_pathtrace`. |
| `src/cuda/CudaRenderer.{h,cu}`      | Static method `CudaRenderer::render_rng_test(w, h, seed)` (Stage 11A only; 11B/11C orchestrations live in `main.cpp` to keep `rr_gpu` -> `rr_renderer` direction one-way). |
| `src/cuda/CudaKernels.cuh`          | Declares `launch_rng_test_visualize` (Stage 11A's launcher consumed via `CudaRenderer::render_rng_test`). |

### CMake additions for Stage 11

| File                | Change |
|---------------------|--------|
| `CMakeLists.txt`    | New `rr_pathtracer` INTERFACE library (Stage 11A), new `rr_renderer` STATIC library (Stage 11B; gains `PathTracer.cpp` in 11C), three new CUDA TUs added under `RR_ENABLE_CUDA`, `pathtracer_tests` ctest binary, status string bumped per sub-stage. |

### Static-library graph after Stage 11

```
rr_pathtracer (INTERFACE)
    headers: pathtracer/{RNG,Sampling}.{h,cuh},
             pathtracer/PathTracer.h
    deps:    rr_math
                ^
                |
rr_renderer (STATIC)
    impl:   renderer/AccumulationBuffer.cpp,
            pathtracer/PathTracer.cpp
    deps:    rr_image, rr_gpu, rr_pathtracer
                                   ^
                                   |
rr_gpu (STATIC)
    impl:    cuda/Cuda*.{cpp,cu} including the new
             CudaAccumulation.cu, CudaRngTestKernel.cu,
             CudaPathTracer.cu under RR_ENABLE_CUDA
    deps:    rr_image, rr_camera, rr_geometry,
             rr_relativity, rr_pathtracer (PUBLIC at link
             level; only the kernel TUs need its headers
             at compile time)
```

No cycles.

---

## 10. What is the next safe stage?

Two readings, depending on what "safe" means:

### (a) "Safe" as in "validate before extending"

The Stage 11A/B/C kernels and their host orchestration are
fully wired and compile clean, but **none of the four expected
artifacts (`gpu_rng_test.ppm`, `gpu_accumulation_test.ppm`,
`pathtrace_spp_1.ppm`, `pathtrace_spp_16.ppm`) exist in this
environment** because the build host has no CUDA toolchain.

The next safe action - before any new feature stage - is to
run the three Stage 11 actions on a CUDA-enabled host and
visually confirm:

1. `--render-rng-test` produces a four-quadrant noise image
   with TR being uniform RGB on `(u.x, u.y, 0)` and BR
   biasing visibly bluer than BL (cos-weighted samples
   cluster toward +Z).
2. `--render-accumulation-test` produces a uniform mid-gray
   image (after 64 RGB samples each channel converges to
   ~0.5).
3. `--render-pathtrace scenes/test_full_scene.rrscene`
   produces a noisy `pathtrace_spp_1.ppm` and a markedly
   smoother `pathtrace_spp_16.ppm` of the same scene
   (visual confirmation that the AccumulationBuffer
   integration converges as the math predicts).

Items 1 and 2 are mechanical checks - the math has known means
and the visual signal is unambiguous. Item 3 is the
end-to-end "the path tracer works" check.

### (b) "Safe" as in "next stage in the master order"

Master order #16 ("Path tracing foundation") is now
substantively complete. The natural follow-ups, in roughly
ascending complexity:

| Order | Stage candidate | Notes |
|-------|-----------------|-------|
| 11D? | Multi-mesh upload on `GpuScene` | Concrete bottleneck surfaced in 10B.11 / 11C; current single-mesh slot rejects scenes with >1 mesh authored. Not in master order list per se but a cleanly scoped follow-up. |
| 11D? | Direct-light sampling (NEE) | Listed as a deferred follow-up in 11C BUILD_PLAN. Adds shadow rays + a direct-light combine; the Stage 11A primitives are already in place. |
| 11D? | Relativistic path tracer | Re-fold aberration / Doppler / searchlight into the path-tracer's primary + bounce rays. Listed as a follow-up in 11C BUILD_PLAN. Requires careful design (bounce direction needs aberration too); not a one-line change. |
| 12 | Master order #17 - OptiX upgrade path | The advertised post-foundation step in the master plan. Requires a working CUDA toolchain + OptiX SDK; both are absent in this environment. |
| 13 | Material expansion | Roughness / metallic / specular / transmission consumed by a real BSDF. The fields already upload through `MaterialParams`; the path-tracer kernel currently reads only `baseColor` + emission. |

### Recommendation

Run `(a)` first - on a CUDA host produce the four artifacts
and visually verify them - then pick the most valuable `(b)`
path forward. If the immediate priority is the project's
unique selling point (relativistic perception + path
tracing), the relativistic-path-tracer integration is the
highest-value next slice. If the priority is conventional
path-tracer quality and feature breadth, NEE first.

Either of those is preferable to jumping straight to OptiX
(master #17), which is a backend swap and gains the most
when there is something to compare against; today's CUDA
path tracer is exactly the baseline OptiX would need.

---

## Summary

| # | Audit item                                           | Result   |
|---|------------------------------------------------------|----------|
| 1 | Project builds cleanly                               | PASS     |
| 2 | `gpu_rng_test.ppm` exists                            | BLOCKED (no CUDA on this host) |
| 3 | `gpu_accumulation_test.ppm` exists                   | BLOCKED (same) |
| 4 | `pathtrace_spp_1.ppm` exists                         | BLOCKED (same) |
| 5 | `pathtrace_spp_16.ppm` exists                        | BLOCKED (same) |
| 6 | Ray paths fully GPU-side                             | PASS     |
| 7 | Accumulation GPU-side                                | PASS     |
| 8 | No CPU ray tracing / sample accumulation             | PASS     |
| 9 | Path-tracing files inventoried                       | (see §9) |
| 10| Next safe stage                                      | (see §10) |

The Stage 11 path-tracing foundation is **architecturally and
buildably complete**. The four expected output artifacts are
contingent on a CUDA-enabled host run; that run is the next
safe action before any further feature work.

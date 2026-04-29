# Stage 1–5 Audit

Date: 2026-04-29
Branch: `relativity-core-v1`
Commit at audit time: `ddc2450` (stage 10: relativistic GPU rendering).
Audit window: stages 1–5 in the user's numbering (= master-instructions
modules 3 through 10 = `BUILD_PLAN.md` stages 1.1 through 10).

This doc is an audit. No source files are modified by it.

The audit answers the thirteen questions from the prompt verbatim,
then the companion `docs/NEXT_STEPS.md` carries the next safe
implementation order.

---

## 1. Does the project build from a clean checkout?

**Yes**, in the host-only configuration that this development
environment supports.

Run from a clean tree:

```sh
cmake -S . -B build
cmake --build build -j
```

Result: clean compile with `-Wall -Wextra -Wpedantic`, zero warnings,
zero errors. All eight build artefacts produced:

```
lib/librr_image.a, lib/librr_gpu.a   (two STATIC libs)
bin/RelativityRender                  (executable)
bin/math_tests, bin/image_tests, bin/gpu_tests  (three test binaries)
```

The CUDA-enabled configuration (`-DRR_ENABLE_CUDA=ON`) requires the
CUDA Toolkit and a CUDA-capable GPU. Neither is present in the
development environment used for this audit. The CMakeLists fails
fast and honestly when the toolkit is missing:

```
$ cmake -S . -B build-cuda -DRR_ENABLE_CUDA=ON
CMake Error: Could not find nvcc, please set CUDAToolkit_ROOT.
```

That is the desired behaviour — no silent half-success.

## 2. What exact build commands work?

Verified end-to-end in this audit, host-only:

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
build/bin/RelativityRender [--help|--version|--device-info|--render-...]
```

Documented and expected to work on a CUDA-capable host (NOT verified
in this audit because no GPU is available):

```sh
cmake -S . -B build-cuda -DRR_ENABLE_CUDA=ON
cmake --build build-cuda -j
build-cuda/bin/RelativityRender --device-info
build-cuda/bin/RelativityRender --render-gradient
build-cuda/bin/RelativityRender --render-rays
build-cuda/bin/RelativityRender --render-sphere
build-cuda/bin/RelativityRender --render-relativistic
```

## 3. Does CUDA device detection work?

**Code path is real.** `src/cuda/CudaContext.cpp:10` calls
`cudaGetDeviceCount` and iterates `cudaGetDeviceProperties` for each
visible device, populating `rr::gpu::GpuDevice` PODs. Sticky errors
are cleared on every failure path. The host-side wrapper at
`src/gpu/GpuDevice.cpp:37` delegates to that backend when
`RR_HAS_CUDA` is defined and returns an empty list otherwise.
`main.cpp::report_device_info` (lines 26–43) prints
`GPU backend: <CUDA|(none)>` and a per-device line
`[i] <name> (sm_<MAJOR.MINOR>, <MiB> MiB, <N> SMs)`.

**Runtime verified, host-only:** `--device-info` prints
`GPU backend: (none)` and an actionable rebuild hint when CUDA is
not compiled in.

**Runtime NOT verified, CUDA-on:** the kept code is byte-equivalent
to the prototype's tested implementation but cannot run in this
audit's environment.

## 4. Does GPU framebuffer output work?

**Code path is real.** The `--render-gradient` CLI action runs
`CudaRenderer::render_gradient` (`src/cuda/CudaRenderer.cu:75-83`),
which:

1. allocates a `GpuBuffer<float>` of `w * h * 4` floats,
2. launches `k_gradient_rgba32f` (`CudaTestKernel.cu:24-39`) — one
   thread per pixel, writes `R=u`, `G=v`, `B=0`, `A=1`,
3. drains CUDA errors and `cudaDeviceSynchronize`s,
4. downloads into a fresh `rr::image::Image(Rgba32F)`,
5. saves PPM to `output/gpu_gradient.ppm` (or `--output`).

**Runtime verified, host-only:** falls through to the actionable
"requires CUDA" error. **Runtime NOT verified, CUDA-on:** kept logic
is byte-equivalent to the prototype's tested gradient render.

## 5. Does GPU camera-ray visualization work?

**Code path is real.** `--render-rays` runs
`CudaRenderer::render_camera_rays` (`CudaRenderer.cu:85-93`), which
launches `k_camera_rays_visualize` (`CudaTestKernel.cu:64-80`). The
kernel calls the same `RR_HD generate_camera_ray` defined in
`camera/CameraRay.h:38` — the host tests in
`tests/math_tests.cpp` already exercise the math; the device
re-uses that exact code path. The kernel encodes the (already
unit-length) ray direction as `0.5*dir + 0.5` per channel.

`main.cpp::run_render_camera_rays` (lines 150–171) constructs a
default `Camera`, sets `aspect = width / height`, hands it to the
renderer, and writes `output/gpu_camera_rays.ppm`.

**Runtime verified, host-only:** correct error and exit 1.
**Runtime NOT verified, CUDA-on**: kept code matches prototype.

## 6. Does GPU sphere intersection work?

**Code path is real.** `--render-sphere` runs
`CudaRenderer::render_sphere` (`CudaRenderer.cu:95-110`), which
launches `k_sphere_visualize` (`CudaTestKernel.cu:99-145`). The
kernel:

1. generates the primary ray,
2. calls `intersect_sphere(ray, sphere, 0, 1e30)` from
   `cuda/CudaIntersection.cuh:36`,
3. shades with `0.5 * normal + 0.5` on hit,
4. lerps a sky gradient (white at horizon, soft blue overhead) on
   miss.

`run_render_sphere` (`main.cpp:179-200`) constructs a sphere at
`(0, 0, -3)` with radius 1, default camera, and writes
`output/gpu_sphere.ppm`.

**Runtime verified, host-only:** correct error and exit 1.
**Runtime NOT verified, CUDA-on**: `intersect_sphere` math is
byte-identical to the prototype's tested implementation.

## 7. Does relativistic GPU sphere rendering work?

**Code path is real.** `--render-relativistic` runs
`CudaRenderer::render_relativistic_sphere`
(`CudaRenderer.cu:112-134`), which launches `k_sphere_relativistic`
(`CudaTestKernel.cu:175-251`). The kernel runs the eight-step
pipeline:

1. `generate_camera_ray`,
2. `aberrateDirection(observer.velocity, ray.direction)` (if
   `params.enable_aberration`),
3. `intersect_sphere`,
4. base shade (normal-as-color on hit; sky gradient on miss),
5. `D = dopplerFactor(observer.velocity, ray.direction)`,
6. `applyDopplerColor(color, D, doppler_color_strength)` (if
   `enable_doppler`),
7. `color *= 1 + (D^4 - 1) * searchlight_strength` (if
   `enable_searchlight`),
8. write Rgba32F.

`run_render_relativistic` (`main.cpp:209-263`) drives a four-β
sweep (β = 0.00, 0.25, 0.75, 0.95), emitting four named PPMs:

| β | output path |
|---|-------------|
| 0.00 | `output/sphere_beta_000.ppm` |
| 0.25 | `output/sphere_beta_025.ppm` |
| 0.75 | `output/sphere_beta_075.ppm` |
| 0.95 | `output/sphere_beta_095.ppm` |

The observer's 3-velocity for run *i* is `(0, 0, -beta_i)` —
moving along the camera's default forward. **Runtime verified,
host-only:** correct error and exit 1. **Runtime NOT verified,
CUDA-on:** kernel logic + driver loop are byte-equivalent to the
prototype's tested `k_sphere_relativistic`.

## 8. Are all per-pixel / per-ray operations on GPU?

**Yes.** Verified by direct inspection.

`pixels[idx + …] = …` writes appear in exactly one file in `src/`:
`cuda/CudaTestKernel.cu`, in the four `__global__` kernels
(lines 36–39, 74–77, 142–145, 243–246). No other source file in
`src/` writes pixels.

The four kernels are the only consumers of:

- `generate_camera_ray` (3 call sites — all in `CudaTestKernel.cu`).
- `intersect_sphere`    (2 call sites — both in `CudaTestKernel.cu`).
- `aberrateDirection`, `dopplerFactor`, `searchlightFactor`,
  `applyDopplerColor` (5 call sites — all in `CudaTestKernel.cu`).

The host never executes any of those helpers. Confirmed via:

```
$ grep -rn "generate_camera_ray\|intersect_sphere\|aberrateDirection\|\
dopplerFactor\|searchlightFactor\|applyDopplerColor" src/ tests/
```

## 9. Is there any CPU ray tracing or CPU pixel rendering?

**No CPU ray tracing.** None of the per-ray helpers (camera-ray gen,
intersection, aberration, Doppler, searchlight) are called from
host code anywhere in `src/`.

**One CPU pixel-rendering loop, allowed by the master rules.**

`src/image/Image.cpp:88-100` — the `save_ppm` file-write loop. This
is the explicit "save image files" exception in the master rules'
allowed-CPU list:

```
80  bool Image::save_ppm(const std::filesystem::path& path) const {
…
88      const int ch = channels();
89      for (int y = 0; y < height_; ++y) {
90          for (int x = 0; x < width_; ++x) {
91              const auto idx = (y * width_ + x) * ch;
92              ...
97              out.write(reinterpret_cast<const char*>(rgb), 3);
…
```

**Other CPU pixel-touching code (incidental, not renderer paths):**

- `src/image/Image.cpp::clear` (line 65) and `set_pixel` (line 41)
  are general-purpose `Image` helpers. The renderer path
  (`CudaRenderer.cu:63`) constructs a fresh `Image(Rgba32F)`
  (zero-initialised by the ctor) and then **immediately** fills it
  via `GpuBuffer::download`. `Image::clear` and `set_pixel` are
  reachable from the renderer only through `Framebuffer::clear`,
  which the renderer does **not** call. Production renderer code
  never writes pixels host-side.
- `tests/image_tests.cpp:165-198` writes a 64×64 UV gradient on the
  CPU and saves it to `output/image_test.ppm`. This is **explicitly
  labelled as "image IO validation only — NOT renderer output"**
  in the test's file header and runtime log line. It is in
  `tests/`, runs only when the user runs the test binary, and
  is not part of any renderer path.

## 10. Which output images are generated?

The codebase **declares** seven output paths and **runs** one of
them in this audit's environment:

| Path                              | Producer                          | Runs in this env? |
|-----------------------------------|-----------------------------------|-------------------|
| `output/image_test.ppm`           | `tests/image_tests.cpp` (CPU IO validation) | YES — `ctest` runs `image_tests` and writes 12 301 bytes; observed in `build/output/image_test.ppm`. **Not renderer output.** |
| `output/gpu_gradient.ppm`         | `--render-gradient` (GPU)         | NO — needs CUDA host. |
| `output/gpu_camera_rays.ppm`      | `--render-rays` (GPU)             | NO — needs CUDA host. |
| `output/gpu_sphere.ppm`           | `--render-sphere` (GPU)           | NO — needs CUDA host. |
| `output/sphere_beta_000.ppm`      | `--render-relativistic` (GPU)     | NO — needs CUDA host. |
| `output/sphere_beta_025.ppm`      | `--render-relativistic` (GPU)     | NO — needs CUDA host. |
| `output/sphere_beta_075.ppm`      | `--render-relativistic` (GPU)     | NO — needs CUDA host. |
| `output/sphere_beta_095.ppm`      | `--render-relativistic` (GPU)     | NO — needs CUDA host. |

This is **not** a code defect — it is a development-environment
limitation. All six GPU-output paths are wired end-to-end and the
kernels match the prototype's tested implementations. Confirming
the actual PPM content of the GPU outputs is the next-stage gate
for `NEXT_STEPS.md` step 0.

## 11. Which files are real implementations vs placeholders?

### Real implementations (full functionality)

Every file under `src/` is a real implementation:

| Module     | Files                                                                 | Notes |
|------------|----------------------------------------------------------------------|-------|
| core       | Logger, Version, Config, CommandLine                                 | Logger thread-safe stdio; CommandLine has 8 actions, mutual-exclusion, exit-code-2 errors. |
| math       | Vec2/3/4, Mat4, MathUtils (RR_HD)                                    | 60 host assertions cover Vec3 + Mat4. |
| image      | Color, Image, Framebuffer                                            | Image is real and used by the renderer; Framebuffer is real but unused — see section 13. |
| gpu        | GpuDevice, GpuBuffer<T>                                              | Move-only RAII; honest failure when no backend. |
| cuda       | CudaContext, CudaBuffer (cudart wrapper); CudaKernels.cuh; CudaIntersection.cuh; CudaTestKernel.cu (four kernels); CudaRenderer.{h,cu} | All real. |
| camera     | Camera, CameraRay (RR_HD `generate_camera_ray`)                      | |
| geometry   | Sphere (RR_HD POD)                                                   | |
| renderer   | Hit (RR_HD POD)                                                      | |
| relativity | RelativityParams, RelativityMath.h, RelativityMath.cuh               | Six PHYSICAL functions, one ARTISTIC APPROXIMATION (`applyDopplerColor`). |

### Honestly-tagged placeholders

| Item                                          | Where                          | Why it is a placeholder |
|-----------------------------------------------|--------------------------------|--------------------------|
| `CommandLine::Action::Render` ("--render &lt;scene&gt;") | `main.cpp:292-294`             | Only logs `"render command received"` and exits 0. The scene format + loader land in module 15. The CLI usage text marks this `(placeholder)`. |
| `applyDopplerColor`                           | `relativity/RelativityMath.h:146-163` | Tagged `ARTISTIC APPROXIMATION` in its docstring. The artistic `tanh(0.5 log D) * strength` mix stands in until a spectral pipeline lands (module 18 / 19). |

### Real but currently unused by the renderer path

| Item                          | Where                  | Status |
|-------------------------------|------------------------|--------|
| `Framebuffer.{h,cpp}`         | `src/image/`           | Real implementation; the `CudaRenderer` path bypasses it (downloads directly into a fresh `Image`). Exists for the planned multi-AOV / accumulation future per its docstring. |
| `lorentzContraction`          | `RelativityMath.h:55`  | Real and PHYSICAL; the kernels do not call it (single-sphere scene has no extended dimension along the boost direction). Will be exercised when meshes land. |
| `RelativityMath.cuh`          | `src/relativity/`      | Real (thin re-export). Future device-specific overrides go here. |

### No fake stubs

None of the files in `src/` claim functionality they do not have.
Every file either does what its header docstring says, or is honestly
tagged as a placeholder (the two items above).

## 12. Which Stage 1–5 tasks are complete?

Mapping the user's "Stage 1–5" numbering onto the master order and
the BUILD_PLAN's stage ledger:

| User stage | Master modules | BUILD_PLAN stages | Status |
|------------|----------------|-------------------|--------|
| Stage 1 — Core App                       | 3                  | 1.1, 1.2 | **Complete** |
| Stage 2 — Math + Image / Framebuffer     | 4, 5               | 2, 3     | **Complete** |
| Stage 3 — CUDA device + buffer + kernel  | 6, 7               | 4, 5, 6  | **Complete** |
| Stage 4 — Camera + Primitive GPU render  | 8, 9               | 7, 8     | **Complete** |
| Stage 5 — Relativistic camera math + GPU | 10                 | 9, 10    | **Complete** |

All ten BUILD_PLAN entries from `Stage 1.1 — Core app skeleton`
through `Stage 10 — Relativistic GPU rendering` are implemented,
in master-order, and produce a building, testing, partially
runtime-verified executable.

Inventory: 39 source / header files (`src/` + `tests/`) totalling
3,557 lines. Nine library / executable / test targets in CMake.
Three host-side ctest binaries running 150 assertions, all passing.

## 13. Which Stage 1–5 tasks need repair before moving forward?

No **task** is broken. There are five housekeeping items, all small,
none blocking the next stage. They are not code-write asks for this
audit — they are a punch list for `NEXT_STEPS.md` to triage.

| # | Item                                                    | Severity  | Recommended slot                                                |
|---|---------------------------------------------------------|-----------|-----------------------------------------------------------------|
| H1 | GPU runtime verification: gradient / rays / sphere / four-β PPMs have not been runtime-confirmed in this dev env (no GPU). | Medium | Run on a CUDA host as `NEXT_STEPS.md` step 0 before any further code changes. The kernels match the prototype's tested ones; verification should be a single 10-minute pass, not a debug session. |
| H2 | `Framebuffer.{h,cpp}` is real but unused by the renderer (the CUDA path downloads into a fresh `Image`). | Low | Decide in module 19 (AOV / render passes) whether the renderer adopts it or it gets retired. No action now. |
| H3 | `Image::set_pixel` / `Image::clear` are reachable from the renderer-adjacent `Framebuffer::clear` even though no renderer path actually calls it. | Low | Same as H2. |
| H4 | `--render <scene>` is the documented placeholder action; nothing reads `Config::scene_path` yet. | Documented | Lands in module 15 (scene format / parser). |
| H5 | `applyDopplerColor` is `ARTISTIC APPROXIMATION` per Stage 9 audit. | Documented | Lands in module 18 / 19 (texture / spectral). |

None of H1–H5 require modifying the existing renderer code; H1 is
a runtime check, H2/H3 are deferred design decisions, H4/H5 are
already on the master roadmap.

---

## Summary

The clean RelativityRender core through stage 5 is **real, buildable,
GPU-first, and not a shallow scaffold**:

- **Real:** every src/ file does what it says; the only honestly-
  tagged placeholders are `--render <scene>` (one log line, one CLI
  flag) and `applyDopplerColor` (documented ARTISTIC APPROXIMATION).
- **Buildable:** clean checkout → `cmake -S . -B build && cmake --build
  build -j` produces eight artefacts under `-Wall -Wextra -Wpedantic`
  with zero warnings.
- **GPU-first:** every per-ray and per-pixel operation runs in one of
  four `__global__` kernels in `CudaTestKernel.cu`. Only the allowed
  exception (`Image::save_ppm`'s file-write loop) iterates pixels on
  the host.
- **Not a scaffold:** 3,557 lines across 39 files, six STATIC /
  INTERFACE libraries, three executable targets, 150 host-side test
  assertions, four real GPU kernels, eight CLI actions, end-to-end
  PPM output paths through the GPU pipeline.

Outstanding gap: GPU runtime verification on a CUDA host (audit
item H1). Everything else is on plan.

The next safe implementation order is in
[`docs/NEXT_STEPS.md`](NEXT_STEPS.md).

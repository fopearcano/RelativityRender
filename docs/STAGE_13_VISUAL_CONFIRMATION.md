# Stage 13 Visual Confirmation

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `761939a` ("stage 13 audit F:
verdict")
Goal: produce the two Stage 13 GPU output PPMs once on a
CUDA-enabled host before continuing past Stage 13.
Mode: documentation-only. No source code is modified by this
slice.

The audit host this slice runs on is **CUDA-less**: `which
nvcc` returns nothing, `nvidia-smi` is not installed,
`/usr/local/cuda` does not exist, and `RR_ENABLE_CUDA:BOOL=OFF`
in the existing CMake cache. A live attempt to reconfigure the
project with `-DRR_ENABLE_CUDA=ON` from this host fails at
`find_package(CUDAToolkit)` with:

```
CMake Error at /usr/share/cmake-3.28/Modules/FindCUDAToolkit.cmake:855 (message):
  Could not find nvcc, please set CUDAToolkit_ROOT.
Call Stack (most recent call first):
  CMakeLists.txt:64 (find_package)
-- Configuring incomplete, errors occurred!
```

This means the two Stage 13 output PPMs **cannot be produced
here**. The build is not broken (no source-side fix would
unblock a missing toolchain); the per-Stage-13 audit family
already established that the OFF build is clean and `ctest`
passes 4/4. This slice therefore documents the commands a
CUDA-enabled host would run, and records the absence of the
output files honestly per the prompt's questions.

---

## 1. Build command used

On a CUDA-enabled host (a Linux box with the NVIDIA CUDA Toolkit
installed and at least one CUDA-capable GPU visible to the
driver):

```
cmake -S . -B build-cuda -DRR_ENABLE_CUDA=ON
cmake --build build-cuda --parallel
```

The configure step pulls in `find_package(CUDAToolkit)`, picks
up `nvcc` from the toolkit's `bin/` directory, links
`CUDA::cudart` into `rr_gpu`, and compiles the eight CUDA
translation units (`CudaContext.cpp`, `CudaBuffer.cpp`,
`CudaTestKernel.cu`, `CudaRngTestKernel.cu`,
`CudaAccumulation.cu`, `CudaPathTracer.cu`,
`CudaTextureSampleTestKernel.cu`, `CudaRenderer.cu`) into
`rr_gpu` with `RR_HAS_CUDA` as a PUBLIC compile definition.

On this audit host the same command fails as recorded above.
**Not run.**

## 2. Run commands used

```
mkdir -p output
build-cuda/bin/RelativityRender --render-texture-sample-test
build-cuda/bin/RelativityRender --render-textured-material
```

Each command is a one-shot dispatch into the corresponding
Stage 13B.x handler (`run_render_texture_sample_test` for the
2x2 four-colour validation pattern; `run_render_textured_
material` for the multi-sphere + textured-quad scene). Both
default to writing into the `output/` directory; both honour
`--width` / `--height` (defaults 1280 x 720) and `--output
<path>` if the operator wants a non-default destination.

On this audit host the binary `build-cuda/bin/RelativityRender`
does not exist (the build step failed at configure), so neither
command is run. The CUDA-less binary at `build/bin/Relativity
Render` correctly short-circuits both actions to the standard
"requires CUDA. Rebuild with -DRR_ENABLE_CUDA=ON ..." error
and exits 1, as documented in `docs/STAGE_13_AUDIT_E.md`.
**Not run.**

## 3. Does `output/gpu_texture_sample_test.ppm` exist?

**NO.** The `output/` directory does not exist on the audited
tree. (Same finding as `docs/STAGE_13_AUDIT_E.md` question 1;
no change in environment since.)

## 4. Does `output/gpu_textured_material.ppm` exist?

**NO.** Same cause as question 3.

## 5. Are both files non-empty?

**NO** (vacuously: neither file exists).

A future CUDA-enabled host run is expected to produce two
non-empty P6 (binary) PPM files sized to the requested
resolution (1280 x 720 by default), via the existing
`rr::image::Image::save_ppm` helper every other GPU-render CLI
action uses.

## 6. PASS / REPAIR

**PASS (deferred).**

The Stage 13 system is structurally complete per the prior
five-slice audit (`docs/STAGE_13_AUDIT_VERDICT.md` returned
PASS): files exist, the project builds clean under
`RR_ENABLE_CUDA=OFF`, the GPU upload path / device sampler /
material-texture branch are wired by name. The visual
confirmation **cannot be performed in this environment** because
the host has no CUDA toolchain, but the absence is environmental
- not a code defect. The renderer code is intentionally not
modified by this slice (the prompt's "do not change renderer
code unless build is broken" rule applies; the build is not
broken under OFF, and no source-side fix would conjure a
missing toolchain).

When this slice is re-run on a CUDA-enabled host:

- The two `cmake` commands in section 1 should succeed and
  produce `build-cuda/bin/RelativityRender` plus the four test
  binaries; `ctest` should report 4/4 passing.
- The two CLI invocations in section 2 should each exit 0 and
  write a non-empty PPM into `output/`.
- The expected visuals are documented in
  `docs/BUILD_PLAN.md`'s Stage 13B.2 + 13B.3 entries:
  - `gpu_texture_sample_test.ppm`: exactly four solid colour
    quadrants (top-left red, top-right green, bottom-left
    blue, bottom-right yellow) under clamp-to-edge nearest
    sampling on the 2x2 reference texture.
  - `gpu_textured_material.ppm`: the textured quad behind the
    four flat-coloured spheres + emissive ground sphere from
    `--render-material-scene`'s palette, with the four
    texture quadrants visible across the quad surface.

Until that confirmation is recorded, downstream stages should
not depend on visually-correct textured shading.

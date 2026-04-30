# Stage 13 Audit C — GPU Sampling

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `206f862` ("stage 13 audit B:
GPU upload")
Scope: master order #18, sub-stages 13A / 13B.1 / 13B.2 / 13B.3.
This is **Audit C** of the Stage 13 audit family: GPU sampling
only - the device-side texture sampler and its kernel call
sites. Per the prompt, the audit lists function names; it does
**not** inspect materials (that is a separate audit slice) and
does not inspect the contents or correctness of the sampler
implementation.
Mode: documentation-only. No source code is modified.

The audit answers the three prompt questions in order.

---

## 1. Is there a CUDA / device function for texture sampling?

**PASS - one device-callable sampler exists by name.**

- `rr::cuda::sampleTextureNearest(...)` - declared and defined
  in `src/cuda/CudaTexture.cuh`. Marked `RR_HD inline` so it is
  callable from CUDA `__device__` / `__global__` translation
  units (and, by virtue of the same macro, from host TUs).
- A companion validity predicate sits in the same file:
  - `rr::cuda::device_texture_view_valid(...)` (also `RR_HD
    inline`).

No other texture-sampling function is declared anywhere in the
audited tree. (Verified with `grep -rn "sampleTexture\|tex2D"`
across `src/`; the only matches are the function name above
and documentation references.)

---

## 2. Is nearest sampling implemented?

**PASS - the sampler's name and surface declare nearest
sampling, and there is no second / competing sampler.**

- `rr::cuda::sampleTextureNearest(...)` (in
  `src/cuda/CudaTexture.cuh`) - the function name itself
  declares the sampling kernel as nearest-neighbor.
- No `sampleTextureBilinear`, `sampleTextureLinear`,
  `sampleTextureTrilinear`, `sampleTextureAnisotropic`, or
  hardware `tex2D` invocation exists in the audited tree
  (verified with `grep -rn "sampleTexture\|tex2D"` across
  `src/`).

The audit lists the function name and does not inspect the
sampler's body, UV-addressing mode, or pixel-format dispatch
- the prompt's "list function names only" rule confines this
slice to identification of the sampler's identity, which the
name unambiguously gives.

---

## 3. Is sampling used inside a CUDA kernel?

**PASS - two `__global__` kernels invoke
`sampleTextureNearest` by name.**

- `rr::cuda::k_texture_sample_test` - `__global__` kernel
  defined in `src/cuda/CudaTextureSampleTestKernel.cu`. The
  Stage 13B.2 validation kernel; calls
  `sampleTextureNearest(view, uv)` per pixel (line 56 of the
  file).
  - Host-callable launcher: `rr::cuda::launch_texture_sample_test`
    (declared in `src/cuda/CudaKernels.cuh`, defined in
    `src/cuda/CudaTextureSampleTestKernel.cu`).
- `rr::cuda::k_render_scene` - `__global__` kernel defined in
  `src/cuda/CudaTestKernel.cu`. The shared scene-render kernel
  also calls `rr::cuda::sampleTextureNearest(...)` (line 383 of
  the file). The audit notes the call site exists by name; the
  surrounding shading branch (and any material-flag gating
  around it) is **out of scope per the "do not inspect
  materials" rule**.
  - Host-callable launcher: `rr::cuda::launch_render_scene`
    (declared in `src/cuda/CudaKernels.cuh`, defined in
    `src/cuda/CudaTestKernel.cu`).

No host-side caller of `sampleTextureNearest` exists in the
audited tree. (Verified with `grep -rn "sampleTextureNearest"
src/`; every matching call site is inside a `.cu` translation
unit, and every match outside the function's own declaration
is inside a `__global__` kernel body.)

---

## Verdict

All three Audit C checks pass on the audited tree. The
device-side sampler is named `rr::cuda::sampleTextureNearest`,
its declared sampling mode is nearest (no other sampler exists),
and the function is invoked by name inside two `__global__`
kernels (`k_texture_sample_test` and `k_render_scene`). No
host-side call site exists.

What this audit does **not** answer (deferred to follow-up
slices):
- Whether the sampler produces correct pixel values for any
  given UV / texture-size / format combination (output-image
  audit; needs a CUDA-enabled host).
- Whether the sampler's UV-addressing or fallback behaviour
  matches its documented contract (deeper behavioural audit).
- Whether the material system's texture branch is wired to the
  sampler correctly (explicitly out of scope here; material
  audit).
- Whether `output/gpu_texture_sample_test.ppm` and
  `output/gpu_textured_material.ppm` exist on a CUDA-enabled
  host (output audit).

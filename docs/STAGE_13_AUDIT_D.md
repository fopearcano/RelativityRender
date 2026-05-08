# Stage 13 Audit D — Material Integration

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `fa7b771` ("stage 13 audit C:
GPU sampling")
Scope: master order #18, sub-stages 13A / 13B.1 / 13B.2 / 13B.3.
This is **Audit D** of the Stage 13 audit family: material -
texture integration only. Per the prompt, the audit answers
each question yes/no with the relevant file name(s); it does
not inspect implementation behaviour beyond the presence of
the named field / call site.
Mode: documentation-only. No source code is modified.

---

## 1. Do materials reference texture IDs?

**YES.**

- `src/material/MaterialTypes.h` - declares
  `MaterialParams::baseColorTextureId` (an `int`; -1 means "no
  texture bound", any non-negative value is an index into the
  scene-side texture table).

---

## 2. Is there a flag to enable texture usage?

**YES.**

- `src/material/MaterialTypes.h` - declares
  `MaterialParams::useBaseColorTexture` (a `bool`; defaults to
  `false`, must be set to `true` for the texture branch to
  fire in shading).

---

## 3. Is texture sampling used in shading?

**YES.**

- `src/cuda/CudaTestKernel.cu` - inside the `__global__`
  `k_render_scene` kernel, the per-hit shading branch reads
  the material's `useBaseColorTexture` + `baseColorTextureId`
  and calls `rr::cuda::sampleTextureNearest(...)` (the device
  sampler from Audit C) to set the albedo when the gate is
  on.

The sampler itself is declared in `src/cuda/CudaTexture.cuh`
(see Audit C); this audit confirms only that the call site
exists in shading, by file + function name.

---

## Verdict

All three Audit D checks pass. Materials carry a texture-id
field (`baseColorTextureId`) and an enable flag
(`useBaseColorTexture`) in `src/material/MaterialTypes.h`, and
the shading path in `src/cuda/CudaTestKernel.cu`'s
`k_render_scene` kernel calls `sampleTextureNearest` against
the scene-side texture table when the flag is on.

What this audit does **not** answer (deferred to follow-up
slices):
- Whether the material gating logic is correct in detail
  (out-of-range fallback, null-pointer fallback, default-
  value behaviour) - behavioural audit.
- Whether the per-scene texture upload + view array is
  populated correctly at launch time - integration audit.
- Whether `output/gpu_textured_material.ppm` exists and looks
  right on a CUDA-enabled host - output-image audit.

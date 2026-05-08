# Stage 13 Audit E — Output

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `75625a2` ("stage 13 audit D:
material integration")
Scope: master order #18, sub-stages 13A / 13B.1 / 13B.2 / 13B.3.
This is **Audit E** of the Stage 13 audit family: output-image
existence only. Per the prompt, the audit answers each question
yes/no.
Mode: documentation-only. No source code is modified.

The audit tree's host environment is **CUDA-less**:
`build/CMakeCache.txt` records `RR_ENABLE_CUDA:BOOL=OFF`,
`which nvcc` returns nothing, and `nvidia-smi` is not
installed. The two CLI actions that produce these PPMs
(`--render-texture-sample-test` for Stage 13B.2 and
`--render-textured-material` for Stage 13B.3) both short-circuit
to a "requires CUDA" error and exit code 1 in this environment;
neither writes a file. This shapes every answer below.

---

## 1. Does `output/gpu_texture_sample_test.ppm` exist?

**NO.**

The `output/` directory itself does not exist on the audited
tree, and `output/gpu_texture_sample_test.ppm` is therefore
absent.

Reason: producing this artifact requires
`--render-texture-sample-test` to run on a CUDA-enabled host,
per the Stage 13B.2 handler's `#ifdef RR_HAS_CUDA` guard
(mirroring every other GPU-render action). On this audit host
the action returns "requires CUDA. Rebuild with
-DRR_ENABLE_CUDA=ON ..." and exits 1 - by design, not silently
producing a wrong-by-CPU image. The Stage 13B.2 entry in
`docs/BUILD_PLAN.md` documents the CUDA-enabled run that would
produce the file.

---

## 2. Does `output/gpu_textured_material.ppm` exist?

**NO.**

Same cause as question 1. The Stage 13B.3 handler
(`--render-textured-material`) is also gated on `RR_HAS_CUDA`
and short-circuits with the standard requires-CUDA error in
this environment. The Stage 13B.3 entry in
`docs/BUILD_PLAN.md` documents the CUDA-enabled run that would
produce the file.

---

## 3. Are images non-empty?

**NO** (vacuously: the images do not exist, so they cannot be
non-empty).

The audit does not synthesise the files; doing so would
require either modifying source code (forbidden) or running on
a CUDA-enabled host (not available). On a future CUDA-enabled
host both actions are expected to write non-empty PPM (P6) files
sized to the requested width / height (defaults 1280 x 720)
- the output of the existing `Image::save_ppm` helper used by
every other GPU-render handler.

---

## Verdict

Both Stage 13 output artifacts are absent on the audited tree.
This is the documented "blocked in this environment" outcome
for any GPU-render audit on a CUDA-less host, and matches the
behaviour the Stage 11 audit (`docs/STAGE_11_AUDIT.md`)
recorded for `output/gpu_rng_test.ppm` /
`output/gpu_accumulation_test.ppm` under the same conditions.

What this audit does **not** answer (deferred):
- Whether either PPM, when produced on a CUDA-enabled host, is
  visually correct (output validity audit).
- Whether either PPM matches its predicted layout (Stage 13B.2:
  four solid colour quadrants from the 2x2 reference texture;
  Stage 13B.3: textured quad behind four flat-coloured
  spheres) - separate visual-correctness audit, also blocked
  in this environment.

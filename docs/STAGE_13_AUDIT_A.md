# Stage 13 Audit A — Files + Build

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `745d640` ("stage 13B.3: material
texture integration")
Scope: master order #18, sub-stages 13A (texture data model) +
13B.1 (GPU texture upload) + 13B.2 (GPU texture sampling) + 13B.3
(material texture integration). This is **Audit A** of the
Stage 13 audit family: files + build + executable only.
Implementation behaviour, GPU sampling correctness, output
images, CPU-violation searches, and "next safe stage" all live in
follow-up audit slices (B / C / ...).
Mode: documentation-only. No source code is modified by this
audit; no implementation details are inspected.

The audit answers the three questions the prompt requests in
order.

---

## 1. Do the required texture files exist?

**PASS.**

The Stage 13 prompt called for five new source files. All five
are present at the audited commit, with file sizes recorded for
reference:

| Path                            | Status   | Size      |
|---------------------------------|----------|-----------|
| `src/texture/Texture.h`         | present  | 91 lines  |
| `src/texture/Texture.cpp`       | present  | 39 lines  |
| `src/texture/ImageTexture.h`    | present  | 90 lines  |
| `src/texture/ImageTexture.cpp`  | present  | 44 lines  |
| `src/cuda/CudaTexture.cuh`      | present  | 135 lines |

(Verified via `for f in <paths>; do test -f "$f" && wc -l "$f"; done`
on the audited tree.)

The `src/texture/` directory itself was created by Stage 13A; it
did not exist on `relativity-core-v1` before commit `f2e4302`
("stage 13A: texture data model"). `src/cuda/CudaTexture.cuh`
was originally a thin re-export added by Stage 13A and was
rewritten by Stage 13B.2 ("stage 13B.2: GPU texture sampling")
to add the device-side view + sampler. Both forms are within
scope of "the file exists at the audited commit" - this audit
does not inspect their contents beyond confirming presence.

No file in the prompt's required list is missing. No additional
texture files are listed in the prompt; the audit does not
enumerate other files (e.g. `src/gpu/GpuTexture.{h,cpp}`,
`tests/gpu_tests.cpp` updates) which are out of scope for
question 1.

---

## 2. Does the project build?

**PASS.**

- `cmake --build build --parallel` succeeds. Every static library
  the project defines (`rr_math`, `rr_image`, `rr_camera`,
  `rr_geometry`, `rr_material`, `rr_lighting`, `rr_texture`,
  `rr_relativity`, `rr_scene`, `rr_pathtracer`, `rr_renderer`,
  `rr_io`, `rr_gpu`) plus the executable `RelativityRender` and
  the four test binaries (`math_tests`, `image_tests`,
  `gpu_tests`, `pathtracer_tests`) all build to completion.
- `cmake --build build --parallel 2>&1 | grep -ciE "warning|error"`
  returns **0**. The project compiles cleanly under the
  `-Wall -Wextra -Wpedantic` flags `rr_apply_warnings` enforces
  on every target.
- `ctest --test-dir build` reports `100% tests passed,
  0 tests failed out of 4`.

This is the same build configuration recorded in
`build/CMakeCache.txt`:
```
RR_ENABLE_CUDA:BOOL=OFF
```

The build host is **CUDA-less** (`which nvcc` returns nothing,
`nvidia-smi` is not installed, `/usr/local/cuda` does not exist),
so the OFF configuration is the one the audit can exercise. A
prior interactive verification at the end of Stage 13B.3
re-configured cleanly with `-DRELATIVITYRENDER_ENABLE_OPTIX=ON`
and observed the same clean build + `100% tests passed` result;
that branch is reproducible by reconfiguring but is not the
default state of the audited tree. Stage 13's source additions
are orthogonal to both the CUDA flag and the OptiX flag.

---

## 3. Does the executable run?

**PASS.**

The built binary `build/bin/RelativityRender` starts, parses
arguments, executes its action handler, and exits cleanly with a
documented exit code on every invocation tested:

| Invocation                                       | Exit code | Behaviour                                                         |
|--------------------------------------------------|-----------|-------------------------------------------------------------------|
| `--version`                                      | 0         | Prints `RelativityRender 0.1.0` and exits.                        |
| `--device-info`                                  | 0         | Prints GPU backend / device / OptiX availability stanza, exits.   |
| `--render-texture-sample-test` (Stage 13B.2 CLI) | 1         | Logs the standard "requires CUDA. Rebuild with -DRR_ENABLE_CUDA=ON ..." error and exits. |
| `--render-textured-material` (Stage 13B.3 CLI)   | 1         | Same requires-CUDA error path.                                    |

Exit code 0 is the documented "success" return for purely
informational actions; exit code 1 is the documented "the action
needs a CUDA backend that is not compiled in" return for the two
new GPU validation actions (matching the precedent
`--render-rng-test` set in Stage 11A and every other
GPU-render action). Neither GPU action crashes, hangs, or
attempts to write a partial PPM in the no-CUDA environment - the
guard at the top of each handler short-circuits before any GPU
allocation is requested.

The executable also responds to `--help`: the usage text lists
both new actions (`--render-texture-sample-test`,
`--render-textured-material`) under their dedicated paragraphs
and the closing "all action flags are mutually exclusive" line
includes both. (Verified by visual inspection of the printed
help text; details of help-text content are an Audit B concern.)

---

## Verdict

All three Audit A checks pass on the audited commit
(`745d640`):

- All five files the prompt requires exist.
- The project builds clean (no warnings, no errors); ctest 4/4.
- The executable runs and routes the new Stage 13B.2 / 13B.3 CLI
  actions through their guarded code paths with documented exit
  codes.

What this audit does **not** answer (deferred to follow-up
slices):
- Whether the texture data model's *contents* are well-formed
  (Audit B).
- Whether GPU nearest sampling produces correct pixel values on
  a CUDA-enabled host (Audit B / C; needs a GPU).
- Whether the material baseColor texture path actually fires
  inside the kernel on a real run (Audit B / C).
- Whether `output/gpu_texture_sample_test.ppm` and
  `output/gpu_textured_material.ppm` exist and look right
  (blocked in this environment; Audit B / C).
- Whether any CPU-side sampling violates the master rule
  (Audit B).
- What the next safe stage is (Audit B's "next stage" section
  or a dedicated planning slice).

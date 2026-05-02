# Stage 19 Denoiser Audit

Date: 2026-05-02
Branch: `relativity-core-v1`
Last commit on the audited tree: `9a576c7` ("stage 19C.3:
denoiser fallback")
Scope: master order #24 — sub-stages 19A.1–19A.3 (planning),
19B.1–19B.4 (implementation), 19C.1–19C.3 (timing + memory
audit + fallback).
Mode: documentation-only. No source code is modified by this
audit.

The audit answers the four prompt questions in order. Where
visual-evidence verification requires a CUDA + OptiX-SDK host
that this audit host does not have (`which nvcc` returns
nothing; `RR_ENABLE_CUDA:BOOL=OFF` in `build/CMakeCache.txt`),
the documented expected behaviour is recorded with a clear
"deferred-to-CUDA-host" gate so a future operator can finish
the verification on the right hardware.

---

## 1. Does `output/denoised.ppm` exist?

**PARTIAL** — the file-write contract is verifiable end-to-end
on this audit host only on the failure path (which produces
the documented error and **no** PPM, by design); the success
path (which would produce the PPM) is gated on a CUDA +
OptiX-SDK host.

What the audit host can and did verify:

- `--render-aovs --denoise` and `--render-denoise`, run on
  the OFF build (`RR_ENABLE_CUDA=OFF`,
  `RELATIVITYRENDER_ENABLE_OPTIX=OFF`), return the documented
  "requires CUDA + OptiX" error and exit non-zero. No PPM is
  written. This is intentional: the AOV pipeline that produces
  the denoiser's input AOVs requires CUDA, so without it
  there is nothing to denoise.
- The audit-host ON build
  (`RELATIVITYRENDER_ENABLE_OPTIX=ON`, no SDK located) hits
  the same CUDA gate before reaching the denoiser; same exit
  shape.
- The fallback contract added in Stage 19C.3 — write the
  noisy Beauty AOV to `out_path` when any denoiser-side step
  fails — is implemented as the
  `denoise_aov_buffers_to_ppm::save_noisy_fallback` lambda;
  the six failure call sites all return through it. This
  guarantees the PPM exists whenever the AOV-render step
  succeeded, even if the OptiX denoiser invocation itself
  fails.

What is deferred to a CUDA + OptiX-SDK host run:

- The actual `optixDenoiserInvoke` -> download -> save_ppm
  end-to-end. On a CUDA + OptiX-SDK host:
  - `--render-denoise` writes
    `output/denoised.ppm`.
  - `--render-aovs --denoise` writes the standard six
    `output/aov_*.ppm` files **and**
    `output/denoised.ppm`.
  - On any denoiser-side failure (Stage 19C.3 fallback path),
    `output/denoised.ppm` still exists; its content is the
    noisy Beauty AOV; the log line
    `[WARN] denoise: <reason>; falling back to noisy Beauty
    AOV (no denoising applied)` documents the cause.

Verdict: file-existence contract is correctly designed (Stage
19B.3 success path + Stage 19C.3 fallback path). Empirical
file-write verification gated on a CUDA + OptiX-SDK host run.

---

## 2. Is `denoised.ppm` visually smoother than the input?

**DEFERRED** — visual diff requires a CUDA + OptiX-SDK host
run; the audit host cannot perform it.

What the audit host can and did verify:

- The OptiX denoiser is configured with the project's three
  required guide layers (Albedo + Normal in addition to
  Beauty) per Stage 19A.2 §8.1; `OptixDenoiserOptions`
  carries `guideAlbedo = 1` and `guideNormal = 1` (Stage
  19B.1 `OptixDenoiser.cpp::initialize` lines 153-160). This
  is the configuration that produces the strongest spatial
  noise reduction in the OptiX 7.5+ AI denoiser model.
- The model is `OPTIX_DENOISER_MODEL_KIND_HDR` (Stage 19B.1
  lines 162-168). HDR mode preserves bright values rather
  than clipping them; the project's relativistic-shading
  output (DENOISER_PLAN §1.1) needs this.
- The `denoise_aov_buffers_to_ppm` helper hands the AOV
  buffers in untouched (Stage 19B.2 / 19B.3); no host-side
  pre-filtering, no precision narrowing.

What is deferred:

- The actual visual comparison. On a CUDA + OptiX-SDK host:
  - Run `--render-aovs --denoise`. This produces both the
    noisy Beauty (`output/aov_beauty.ppm`) and the denoised
    output (`output/denoised.ppm`) at the same resolution
    and from the same render pass.
  - Open both side by side. The denoised image should show
    visibly reduced high-frequency speckle while preserving
    sphere-edge boundaries and the material colour
    palette (Stage 19A.2 §8.1.1 / §8.1.2 / §8.1.3 documents
    the expected qualitative effect of each guide layer).
  - The Stage 19C.3 fallback case is distinguishable: the
    log emits `[WARN] denoise: ... falling back to noisy
    Beauty AOV ...` and the saved PPM is byte-identical to
    `output/aov_beauty.ppm`. If a denoised output looks
    noisy, check the log for the warning line.

Verdict: configuration is correct for "visually smoother"
output. Empirical visual-quality verification gated on a CUDA
+ OptiX-SDK host run.

---

## 3. Does the renderer still work without the denoiser?

**PASS** — fully verifiable on this audit host.

What the audit host can and did verify:

- The `--denoise` flag (Stage 19B.4) is a modifier, not an
  action; without it, every existing render-* CLI action runs
  byte-for-byte identically to the Stage 19A.3 / 18A.4
  baseline. `Config::denoise_enabled` defaults to `false`
  (`src/core/Config.h`); no handler reads it without that
  explicit opt-in. Confirmed by reading the parser
  (`CommandLine.cpp` `--denoise` branch sets only
  `r.config.denoise_enabled = true`; no other side effects)
  and the consumer (`run_render_aovs`'s
  `if (cfg.denoise_enabled)` branch is the only place the
  bit is read).
- The OFF build (`RR_ENABLE_CUDA=OFF`,
  `RELATIVITYRENDER_ENABLE_OPTIX=OFF`) compiles cleanly with
  ctest 4/4 green. None of the new Stage 19 source files
  (`OptixDenoiser.{h,cpp}`, `denoise_aov_buffers_to_ppm`,
  `format_denoiser_timing_line`, the `--denoise` flag
  plumbing) are reachable in this build because rr_optix is
  not built and the helper is gated on
  `RR_HAS_CUDA && RELATIVITYRENDER_ENABLE_OPTIX`.
- The audit-host ON build
  (`RELATIVITYRENDER_ENABLE_OPTIX=ON`, no SDK) compiles via
  the existing two-layer fallback pattern (Stage 17A.1 /
  every rr_optix subsystem); ctest 4/4 green. Smoke-tested:
  `--render-aovs` (without `--denoise`) returns the
  documented "requires CUDA" error, exactly as it did before
  Stage 19 landed.
- Smoke-tested: `--help` lists every existing action
  unchanged plus the two Stage 19 additions
  (`--render-denoise` action, `--denoise` modifier). The
  mutual-exclusion error message includes
  `--render-denoise` but not `--denoise` (correct: the
  modifier flag is exempt from action-mutex).
- The build-banner status message correctly tracks the
  current Stage 19C.3 label.

What did not need verification:

- No existing render-* handler was modified by Stage 19
  except `run_render_aovs`, which only added an
  `if (cfg.denoise_enabled)` branch *after* the existing
  6-AOV save loop. The denoise branch is dead code when
  the flag is off; the AOV save loop runs unchanged.
- No CUDA kernel was modified by Stage 19. The Stage 14A.3
  `k_render_scene` kernel + the Stage 11C
  `launch_pathtrace_sample` kernel + the Stage 11B
  accumulation primitives + the relativity math leaf are
  byte-identical to their Stage 18A.4 final form.
- No OptiX program was modified by Stage 19. The Stage
  17A.5 OptiX programs (`OptixPrograms.cu`) and pipeline
  / GAS / SBT scaffolding are byte-identical to their
  Stage 17A.5 / 18A.3 final form.

Verdict: PASS. The renderer's existing surface is fully
preserved; the denoiser is strictly additive.

---

## 4. Any GPU/CPU violations?

**PASS with one documented exception** — every per-pixel
*shading* operation runs on the GPU; one host-side per-pixel
constant-alpha fill is justified as IO not rendering, per the
master rule's "CPU may only ... save image files" allowance.

The master rule is:
> 5. No CPU ray tracing as production path.
> 6. CPU may only:
>    - orchestrate execution
>    - parse/load scenes
>    - manage IO
>    - upload data to GPU
>    - launch CUDA/OptiX kernels
>    - receive framebuffers
>    - save image files
>    - run server/IPC
> 7. All per-pixel/per-ray rendering must happen on GPU.

What the audit reviewed and confirmed:

- **OptiX denoiser invoke**: GPU. `optixDenoiserInvoke`
  (Stage 19B.3 `OptixDenoiser.cpp` lines 466-493) runs the
  AI-denoiser kernels on the device. The Stage 19A.2 §8.4
  ownership rule is honoured: the helper takes `const float*`
  device pointers; the renderer keeps the underlying
  `GpuAOVBuffer` / `GpuBuffer<float>` alive across the call.
- **AOV-render kernel** (`k_render_scene`): GPU, unchanged
  from Stage 14A.3. Writes Beauty / Albedo / Normal AOVs
  per-pixel on the device.
- **Denoiser context init / shutdown**: device-side resource
  lifecycle (`optixDenoiserCreate` / `optixDenoiserDestroy`,
  `cudaMalloc` / `cudaFree` for state + scratch), all driven
  from the host but executed against the device. Counts as
  "orchestrate execution" per rule 6.
- **Stage 19C.3 fallback download**: device-to-host copy of
  the noisy Beauty AOV. Counts as "receive framebuffers" per
  rule 6. The OptiX denoiser is bypassed; no per-pixel
  shading happens on the host.
- **Host-side FLOAT3 → RGBA32F widen loop** (both success
  path in Stage 19B.3 and Stage 19C.3 fallback path):
  `dst[i*4 + 3] = 1.0f` per pixel. This is constant-alpha
  fill, **not shading**. The loop does not compute
  radiance, does not sample materials, does not trace rays,
  does not interact with lighting. It is the same kind of
  per-pixel host arithmetic that `Image::save_ppm`'s
  existing `float -> uint8` clamp performs (taking a
  Rgba32F buffer to a PPM). Both fall under "save image
  files" / "manage IO" per rule 6.

The one documented exception (host-side widen loop) is
explicitly called out in `denoise_aov_buffers_to_ppm`'s
in-source comment (Stage 19B.3 lines 2668-2673) and in
DENOISER_PLAN §9.5. A future slice could replace the loop
with a tiny CUDA kernel if it ever becomes a hotspot; today
it is a 921k-iteration trivial-arithmetic loop on the host
side, called once per render.

What the audit ruled out:

- No CPU ray tracing, no CPU intersection, no CPU shading
  on the denoiser path.
- No host-side noise reduction, no host-side filtering, no
  host-side colour grading.
- No host-side reads of the denoiser's intermediate
  buffers (state / scratch are cudaMalloc'd and cudaFree'd
  in `OptixDenoiser::invoke`; the host never touches them).

Verdict: PASS. The denoiser slice respects the GPU/CPU
boundary; the one host-side per-pixel operation is
constant-alpha fill, justified under the "save image files"
rule and called out in-source.

---

## Summary

| # | Question | Verdict |
|---|----------|---------|
| 1 | Does `output/denoised.ppm` exist? | PARTIAL (audit-host: failure-path verified; success-path deferred to CUDA + OptiX-SDK host) |
| 2 | Is `denoised.ppm` visually smoother than input? | DEFERRED (configuration verified correct; visual diff gated on CUDA + OptiX-SDK host) |
| 3 | Does the renderer still work without the denoiser? | PASS |
| 4 | Any GPU/CPU violations? | PASS (one documented exception: host-side constant-alpha fill, justified as IO not rendering) |

The two deferred / partial verdicts are gated on running the
project on a CUDA + OptiX-SDK host. All design / code review
items are PASS. A future Stage 19E (or a CI integration
step) can run the two CLI commands
(`--render-aovs --denoise`, `--render-denoise`) on a
CUDA + OptiX-SDK host and pin the visual outputs as
regression baselines, completing items 1 and 2.

# Denoiser — Scope and Plan

Date: 2026-05-02
Branch: `relativity-core-v1`
Status: **planning only**. This document is being built up
incrementally across the Stage 19A sub-stages; no denoiser code
is implemented in any of them. The path-tracer +
AccumulationBuffer pipeline that landed in Stage 11B/11C and
the AOV pipeline from Stage 14A remain the project's canonical
render output until at least Stage 19B.

This sub-stage (19A.1) covers only the four sections requested
by the prompt — Purpose, Modes, Backend, Constraints. Subsequent
sub-stages (19A.2+) append the API surface, integration plan,
buffer-flow diagrams, error-handling, and migration risks before
any denoiser code lands. Stage 19B is the minimum-viable
implementation slice.

---

## 0. Where this fits

Master order #24 in
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` is
"Denoising". The master rules also place denoising **after**
the OptiX upgrade path (#17), texture system (#18), AOV /
render passes (#19), renderer server (#20), Cinema 4D bridge
(#21), preview UI (#22), and the material node graph (#23).

That ordering is intentional: a denoiser's value is bounded
by what the renderer can already produce. Without AOVs there
is no normal / albedo guidance for the denoiser; without an
OptiX backend there is no shared device context to host the
OptiX denoiser cheaply; without a stable scene + material
pipeline the denoiser would be smoothing over data that the
renderer is still busy fixing. The 19A planning slice is
deliberately narrow — define the role and the integration
shape before the implementation slice writes any code.

This document is the design reference for the denoising
slice. It captures the *why*, *what*, and (later) *how* of
adding a denoiser to the existing render pipeline before any
denoiser code is written.

---

## 1. Purpose

### 1.1 Reduce noise in path-traced output

The project's path tracer (Stage 11C, `src/pathtracer/
PathTracer.{h,cpp}` + `src/cuda/CudaPathTracer.cu`) is a
minimal cosine-weighted diffuse Monte Carlo integrator. Its
per-frame variance behaviour is the textbook one:

```
noise_amplitude  ∝  1 / sqrt(samples_per_pixel)
```

Doubling the sample count halves the standard deviation of
the per-pixel estimate; quartering the noise needs **16x**
the samples. Even moderate per-pixel noise targets — say a
visual SNR comparable to a well-exposed photograph — drive
spp into the hundreds for moderately complex shading. With
the relativistic camera model layered on top (Doppler
colour shift, searchlight beaming) the human-visible noise
floor is pushed even higher because the searchlight scale
(`1 + (D⁴ - 1) * strength`) amplifies any radiance
fluctuation in the bright regions of the image.

A denoiser breaks the `1/sqrt(N)` link between sample count
and image quality. With trained spatio-temporal priors plus
auxiliary AOV guidance (normal, albedo) the residual
high-frequency noise from a low-spp render can be attenuated
without losing edge detail, surface texture, or the
relativistic shading cues. The renderer keeps producing
physically-grounded radiance estimates; the denoiser
post-processes those estimates into a viewer-grade image.

### 1.2 Enable low-sample renders

The two consumers that benefit most from low-sample
rendering are the renderer server (Stage 15) and the
eventual preview UI (master order #22):

- **Server preview pass.** The Stage 15 server's `render`
  command drives `CudaRenderer::render_scene` per request.
  An interactive workflow (artist tweaks a material, asks
  for a fresh frame) wants the lowest spp the denoiser can
  forgive — typically 1-4 spp for a quick look, 16-32 spp
  for a refined preview, with the final pass left at the
  scene's authored `samples_per_pixel`. Without a denoiser
  the 1-4 spp output is unusable; with one it is the
  artist's primary feedback channel.
- **Cinema 4D bridge / future native renderer integration.**
  An external DCC integration (master order #21 / #25) runs
  re-renders dozens of times in a single artist session.
  The denoiser is what makes those re-renders cheap enough
  for an iterative workflow without compromising final-
  frame quality.

This gives the denoiser two distinct quality targets — fast
preview vs final-frame archival — that subsequent sections
formalise as the two **modes**.

### 1.3 What it is not for

The denoiser is post-processing. It does not:

- **Replace path-trace bounce budget**: aggressive denoising
  of a `max_bounces = 1` direct-only render still produces
  a direct-only image; the missing global-illumination
  signal is not invented. The renderer must still trace the
  light transport the artist asked for.
- **Hide bugs in shading or sampling**: a missing emission
  contribution, a dropped material sample, a wrong colour-
  space conversion — all of these survive the denoiser
  because they are signal, not noise. The Stage 13 / 14 /
  18A audits remain the authority on whether the renderer
  output is correct; the denoiser only addresses Monte
  Carlo variance.
- **Fix the relativistic camera model**: aberration,
  Doppler, and the bolometric searchlight scale produce
  legitimate per-pixel intensity / colour variation that
  the denoiser must preserve. The auxiliary AOV inputs
  (normal, albedo, doppler-factor, searchlight-factor) are
  the channel through which the renderer tells the
  denoiser which "noise-looking" structure is actually
  signal. §4.3 details the AOV contract.
- **Substitute for a tone mapper**: linear radiance comes
  in, linear radiance comes out. Tone mapping / display
  encoding stay where they are today
  (`Image::save_ppm`'s float→uint8 clamp).

---

## 2. Modes

The slice ships in two stages, only the first of which lands
in 19B. The progressive mode is documented up-front so the
API surface is stable across both.

### 2.1 Final-frame denoise (Stage 19B target)

**Trigger.** A single explicit call after the path tracer's
spp loop finishes and `AccumulationBuffer::resolve_to_image`
has produced the resolved Beauty pass (or directly off the
resolved float buffer; §4.3 finalises the buffer-flow
direction).

**Inputs.**

| Input | Source | Layout | Required? |
|-------|--------|--------|-----------|
| Beauty | `PathTracer::render` resolve output | Rgba32F, `width × height × 4` floats | **yes** |
| Normal | `GpuAOVBuffer` for `AOV::Normal`, populated by the renderer's AOV pass | 3 floats / pixel, world-space | recommended |
| Albedo | `GpuAOVBuffer` for `AOV::Albedo`, populated by the renderer's AOV pass | 3 floats / pixel, linear-space | recommended |

The Stage 14A AOV pipeline already produces Normal and
Albedo (`render_scene_with_aovs`). The denoiser consumes
the same `GpuAOVBuffer::device_ptr()` pointers the renderer
populates, so no new AOV plumbing is needed.

**Outputs.** A single denoised Rgba32F device buffer. The
host-side caller downloads it via the existing `Image`
machinery and writes it through `save_ppm` exactly the way
the un-denoised path does today. Channels match the input
(linear-space radiance estimates).

**Cost profile.** OptiX's denoiser runs once per frame after
the renderer is done. Latency is bounded by a single
OptiX denoiser invocation; today's per-render
`PathTracer::render` time is dominated by the spp loop, so
adding the denoiser pass at the end is additive rather
than recursive. The Stage 18A.1 timing line (`[GPU] ...
render time = X.XXX ms`) extends naturally — the denoiser
gets its own timed segment under the same fixture.

**Scope of 19B (planned).** Final-frame mode only. Single
denoiser instance, single set of input AOVs, single output
target. No streaming, no temporal accumulation, no
denoiser-aware adaptive sampling.

### 2.2 Progressive denoise (future)

**Trigger.** During the path tracer's spp loop, after every
N samples are accumulated (or at fixed wall-clock
intervals), the host invokes the denoiser on the current
partial-sum frame and presents the denoised result through
the renderer-server's preview channel. The artist sees a
progressively-cleaner image as the spp count rises; the
final frame is still produced by the same denoiser pass at
the end of the loop.

**Why this is "future" and not 19B.** Three reasons:

1. **Temporal accumulation requires a stable buffer
   identity.** The denoiser model OptiX ships supports a
   "temporal" mode that re-uses the previous frame's
   denoised result + motion vectors as a prior. The
   project does not produce motion vectors today (master
   order #25). Until that lands, "progressive" is just
   "run the final-frame denoiser repeatedly" and the
   visible quality improvement vs final-frame-only is
   small.
2. **Renderer-server integration shape.** Streaming
   denoised partials to a connected client requires the
   server to expose a frame-progress channel, which the
   Stage 15 protocol does not have today.
3. **Adaptive-sampling coupling.** The natural pairing
   for a progressive denoiser is variance-driven adaptive
   sampling — the denoiser tells the renderer where to
   spend its next batch of samples. Adaptive sampling is
   itself a multi-slice piece of work; it should not block
   final-frame denoising.

Stage 19B ships the final-frame mode end-to-end. Stage
19C+ (after motion vectors and the server's progress
channel exist) layers progressive on top using the same
backend abstraction, the same AOV inputs, and the same
output buffer shape.

### 2.3 Mode selection at the API level

The 19A.2 sub-stage (still planning) will land the API
surface. The intent is that the renderer server / CLI
handler picks the mode by passing an enum to a single
`Denoiser::run(...)` method:

```text
enum class Mode {
    FinalFrame,    // 19B
    Progressive,   // 19C+ (declared now; rejected by 19B impl)
};
```

The 19B implementation rejects `Progressive` with a
documented "not yet supported" error rather than falling
through to the final-frame path. Same audit-host fallback
posture as the OptiX backend: declare the surface, gate the
implementation on the next slice.

---

## 3. Backend

### 3.1 NVIDIA OptiX denoiser (primary)

OptiX 7.5+ ships the AI denoiser as a first-class API
(`OptixDenoiser`, `optixDenoiserSetup`,
`optixDenoiserInvoke`, `optixDenoiserComputeIntensity`). It
runs on the same `OptixDeviceContext` the project's OptiX
backend already creates in
`src/optix/OptixBackend.{h,cpp}` (Stage 17A.1), so adding
the denoiser does **not** require a second runtime
context, a second CUDA primary context, or duplicated
device-state machinery.

Why this is the primary backend:

- **Device context already exists.** `OptixBackend` was
  built precisely so OptiX-native subsystems could share
  it. The denoiser's `OptixDeviceContext` argument can
  reuse `backend.device_context()`; no new init/shutdown
  pair is added.
- **AOV inputs are already on the device.** The Stage 14A
  AOV pipeline (`GpuAOVBuffer` + `CudaRenderer::render_
  scene_with_aovs`) already populates Beauty / Normal /
  Albedo on the device. The OptiX denoiser consumes them
  via `OptixImage2D` descriptors that point directly at
  those device buffers — no host round-trip, no
  reformatting.
- **PSNR for a small budget.** OptiX's NRD-derived model
  is a strong baseline at 1-16 spp on diffuse / glossy
  scenes; it is the industry-default starting point for
  any GPU renderer and matches the project's quality
  ambitions.
- **Audit-host fallback already designed.** The
  `RELATIVITYRENDER_OPTIX_SDK_FOUND` macro (Stage 12B.5)
  + the existing audit-host fallback for every other
  rr_optix subsystem give the denoiser a ready home: when
  the SDK is not located, the public API returns the
  documented "requires OptiX SDK" error and the
  `--render-pathtrace` / server preview paths skip the
  denoise step.

Two-layer compile-time gating mirrors the rest of rr_optix:

- `RELATIVITYRENDER_ENABLE_OPTIX` undefined → the denoiser
  is not compiled at all (`rr_denoiser` static lib is not
  built).
- `RELATIVITYRENDER_OPTIX_SDK_FOUND` undefined → the
  denoiser library compiles via a stub branch that returns
  failure with a clear `last_error()` message, exactly the
  way `OptixPipeline::create` / `build_mesh_gas` /
  `OptixRenderer::render_*` already do.

### 3.2 CPU fallback (future, optional)

A pure-CPU denoiser (the obvious candidate is Intel Open
Image Denoise / OIDN) is **not** in scope for the 19A
planning slice or the 19B implementation slice. The
project's master rule "All per-pixel/per-ray rendering
must happen on GPU" applies primarily to the renderer
itself, but the spirit of the rule — host code orchestrates,
device code computes — extends naturally to post-process
filters.

That said, a CPU fallback has two legitimate roles a future
slice may pursue:

1. **Headless / no-GPU CI**. The audit host has no CUDA
   and no OptiX SDK. Today denoising is documented as
   "skipped on the audit host". A CPU fallback would let
   the audit-host build still produce a denoised PPM,
   which improves the test surface for end-to-end
   integration tests.
2. **GPU-side OOM / scale-up**. Beyond a certain
   resolution / AOV count the OptiX denoiser's memory
   budget exceeds the available VRAM. A CPU fallback
   serves as a graceful-degrade target rather than a
   "render fails" error.

Both are deferred. When a CPU fallback eventually lands,
it is gated behind the same `Backend` enum the OptiX
denoiser uses; the Beauty / Normal / Albedo buffer shape
is identical (just download to host memory before invoking
the CPU path); the audit-host build already understands
the "no GPU available" path so the new fallback slots in
without a rebuild of the gating layer.

### 3.3 Backend selection at the API level

Subsequent sub-stages (19A.2+) finalise the surface;
today's intent is one factory function on the host side
that returns a `std::unique_ptr<DenoiserBackend>` based on
build-time gating + runtime queries, in the same shape as
`gpu::gpu_backend_name()`:

```text
enum class Backend {
    Optix,    // 19B; falls back to error when SDK not found
    Cpu,      // future
};

std::unique_ptr<DenoiserBackend> make_denoiser(Backend b);
```

19B implements `Backend::Optix` only; `Backend::Cpu`
returns a "not yet implemented" stub.

---

## 4. Constraints

The denoiser slice operates under three hard rules
inherited from the master engineering rules + one
slice-specific rule.

### 4.1 GPU-only

Per master rule "All per-pixel/per-ray rendering must
happen on GPU", the denoiser's filter math runs on the
GPU. The Beauty / Normal / Albedo buffers live on the
device; the OptiX denoiser consumes them in place; the
denoised output is a fresh device buffer; the host's only
role is the device-allocation / launch-orchestration /
download / save shape every other render path uses.

This rule has consequences:

- The denoiser's per-pixel work is never executed on the
  host, even on the audit-host fallback. The fallback
  short-circuits with a clear error rather than running a
  CPU loop.
- Intermediate working buffers (the denoiser's scratch
  and state buffers from `optixDenoiserComputeMemory
  Resources`) are device-resident and follow the existing
  RAII pattern (`GpuBuffer<std::byte>` or a dedicated
  owner; finalised in 19A.2).
- The CPU fallback (§3.2) is the only exception, and is
  not in 19B scope.

### 4.2 Must not modify core renderer logic

The existing path tracer + renderer pipeline is the
project's correctness-anchored baseline. The denoiser is
a strict post-process: it consumes the renderer's
already-resolved output buffer and produces a derived
output buffer. **No** denoiser-driven changes to:

- `PathTracer::render`'s spp loop or accumulation flow.
- The CUDA closest-hit / shading kernels (`k_render_scene`,
  `k_pathtrace_sample`, etc.).
- The OptiX programs (`__raygen__pinhole`,
  `__closesthit__radiance`, `__miss__radiance`).
- The relativity math leaf (`relativity/RelativityMath.h`)
  or the precompute POD added in Stage 18A.3.
- The AOV pipeline (`GpuAOVBuffer`, `AOV.{h,cpp}`,
  `render_scene_with_aovs`) — the denoiser is a consumer
  of the existing AOVs, not a producer of new ones.
- `AccumulationBuffer` (Stage 11B / Stage 18A.4 fast
  paths). The denoiser runs after `resolve_to_image`.

The denoiser slice ships its own translation unit / static
library (working name `rr_denoiser`) and links into the
executable + the eventual server. The renderer libraries
(`rr_pathtracer`, `rr_renderer`, `rr_gpu`, `rr_optix`)
are unaffected by the slice's source-file additions
beyond the new library's own source files.

This is the strict version of the master rule
"Do not overbuild a later system before the current layer
works": the denoiser must not require renderer changes to
function. If a 19B audit reveals that the renderer needs
to expose a buffer differently to make the denoiser work,
that change is an explicit follow-up slice with its own
review — not a 19B "while we're here" change.

### 4.3 Operates on AOV buffers

The denoiser's input contract is the project's existing
AOV buffer set. The Stage 14A pipeline already produces
the three buffers the OptiX denoiser consumes:

| AOV | Component count | Source | Stage |
|-----|-----------------|--------|-------|
| Beauty | 3 (RGB) | `render_scene_with_aovs` populates `targets.beauty` | 14A.3 |
| Normal | 3 (XYZ, world-space) | `render_scene_with_aovs` populates `targets.normal` | 14A.3 |
| Albedo | 3 (RGB, linear) | `render_scene_with_aovs` populates `targets.albedo` | 14A.3 |

The denoiser does **not** introduce new AOV types. It does
**not** dictate how Beauty / Normal / Albedo are computed
— those contracts belong to the renderer + the AOV
documentation in `STAGE_14_AOV_AUDIT.md`. The denoiser
just reads what the renderer produced.

The relativity-specific AOVs (`DopplerFactor`,
`SearchlightFactor` from Stage 14A.3) are **not** denoiser
inputs in 19B. They are available for human inspection
and for the AOV-test fixture, but their information
content is already encoded in Beauty by construction (the
beauty pass applies both effects on the device). A future
slice may experiment with feeding the relativity factors
as auxiliary "user-defined" inputs to OptiX's denoiser if
that turns out to improve quality on heavily-boosted
relativistic frames; for now it stays a known-future
experiment, not a 19B requirement.

The Beauty buffer the denoiser reads is the renderer's
already-resolved frame. For path-traced renders that is
`AccumulationBuffer::resolve_to_image()`'s internal
display buffer (Stage 18A.4 added a float4 fast path for
that resolve). For non-pathtrace renders that is the
`run_kernel_render` framebuffer the existing CUDA kernels
write. The denoiser slice picks the buffer-flow direction
(consume the device buffer directly vs. consume an `Image`
the host owns) in 19A.2; today's commitment is just that
**no Beauty / Normal / Albedo recomputation happens
inside the denoiser slice**.

### 4.4 No code in this sub-stage

Per the prompt's "Do not implement code. Documentation
only." rule, this sub-stage adds no source files. The
gating on whether `rr_denoiser` exists at all is a Stage
19B decision; today the project has no denoiser
translation unit, no `Denoiser` class, no CMake target.
The 19A.2+ sub-stages append API-surface design,
buffer-flow design, and integration-risk audits — still
documentation-only — before 19B starts implementing.

---

## 5. Out-of-scope (for both 19A and 19B)

Spelled out so a future reader does not chase a missing
feature:

- **Temporal stability / motion vectors.** Documented as
  19C+ work in §2.2. The renderer does not produce motion
  vectors today.
- **Variance-driven adaptive sampling.** Pairing of the
  denoiser with the renderer's sample distribution is
  itself a multi-slice piece of work; deferred.
- **Network-streamed denoised previews.** Requires a
  server-side progress channel; the Stage 15 protocol
  does not have one. Deferred.
- **Denoiser-driven AOV variants.** No new AOV types are
  added by the denoiser slice; §4.3 is the binding rule.
- **Integration with the OptiX render path's
  `--render-optix-*` actions.** Stage 19B targets the
  CUDA path tracer's `--render-pathtrace` output first,
  because that is where the noise problem actually is.
  The OptiX render actions today produce single-bounce
  results without RNG and do not need denoising.
- **Cinema 4D bridge integration.** The bridge (master
  order #21) consumes the renderer's stable output
  buffer; whether that buffer is denoised or not is a
  bridge-side decision in a later slice.

---

## 6. Acceptance criteria for Stage 19A.1 (this slice)

This is the planning-only slice. The criteria are:

- A `docs/DENOISER_PLAN.md` exists at `relativity-core-v1`
  with the four sections the prompt requested (Purpose,
  Modes, Backend, Constraints) plus the supporting
  framing the master engineering rules require (where it
  fits in the order, what is out of scope, acceptance
  criteria, follow-up sub-stages).
- The document does not commit to any code. Subsequent
  sub-stages can append to it without re-litigating the
  scope set here.
- `docs/BUILD_PLAN.md` carries the matching status row +
  Stage 19A.1 entry.
- The build banner shows `Stage 19A.1: denoiser scope`
  in both the `project(...)` description and the
  configure-time status message.
- Both Linux build configurations (`RR_ENABLE_CUDA=OFF
  /-DRELATIVITYRENDER_ENABLE_OPTIX=OFF` and the audit-host
  `-DRELATIVITYRENDER_ENABLE_OPTIX=ON`) compile cleanly,
  with `ctest` 4/4 green. (No source changes; the
  verification confirms the doc-only edits do not
  accidentally break anything.)

---

## 7. Follow-up sub-stages

The 19A planning bucket extends as needed; today's plan:

- **19A.2 — API surface.** `Denoiser` class shape, the
  `make_denoiser(Backend)` factory, the `Result` struct,
  the buffer-flow direction (device-buffer-in /
  device-buffer-out vs Image-in / Image-out).
- **19A.3 — Buffer-flow design.** Concrete diagram of how
  Beauty / Normal / Albedo move from the renderer's
  device-side AOV buffers through the denoiser to the
  PPM file, including which buffer the resolve writes to
  and which buffer the denoiser writes to.
- **19A.4 — Integration with existing CLI handlers.**
  Where the denoiser plugs into `--render-pathtrace`, how
  the server's `render` verb selects denoise on / off,
  the `--denoise` / `--no-denoise` CLI surface, the
  `[GPU] denoiser: time = X.XXX ms` log line shape.
- **19A.5 — Audit + risk review.** Documented before any
  code lands: the OptiX denoiser's memory footprint vs
  current device-VRAM tally, version-pinning of the
  OptiX SDK, the SDK-not-found audit-host fallback, the
  AOV-format mismatches the denoiser cares about, and
  the test-coverage gaps the slice introduces.
- **19B — Minimum-viable implementation.** Final-frame
  denoise only (per §2.1). The simplest possible
  end-to-end: path tracer → AOVs → denoiser → PPM.

Subsequent C-bucket slices (19C, 19D, …) layer
progressive mode, motion-vector integration, and adaptive
sampling on top of 19B's API.

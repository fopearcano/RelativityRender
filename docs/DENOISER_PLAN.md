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
sub-stages (19A.2+) append the input contract (19A.2),
the pipeline placement (19A.3), the API surface (19A.4),
the buffer-flow diagram (19A.5), CLI integration (19A.6), and
the audit + risk review (19A.7) before any denoiser code lands.
Stage 19B is the minimum-viable implementation slice.

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

The 19A.4 sub-stage (still planning) will land the API
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

Subsequent sub-stages (19A.4+) finalise the surface;
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
  owner; finalised in 19A.4).
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
the host owns) in 19A.5; today's commitment is just that
**no Beauty / Normal / Albedo recomputation happens
inside the denoiser slice**.

### 4.4 No code in this sub-stage

Per the prompt's "Do not implement code. Documentation
only." rule, this sub-stage adds no source files. The
gating on whether `rr_denoiser` exists at all is a Stage
19B decision; today the project has no denoiser
translation unit, no `Denoiser` class, no CMake target.
The 19A.4+ sub-stages append API-surface design,
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
  /-DRR_ENABLE_OPTIX=OFF` and the audit-host
  `-DRR_ENABLE_OPTIX=ON`) compile cleanly,
  with `ctest` 4/4 green. (No source changes; the
  verification confirms the doc-only edits do not
  accidentally break anything.)

---

## 7. Follow-up sub-stages

The 19A planning bucket extends as needed; today's plan:

- **19A.2 — Denoiser inputs.** Formal definition of the
  required vs optional input set, mapped concretely to the
  Stage 14A AOV buffers. Format / layout / unit notes for
  each input. Documented in §8 below.
- **19A.3 — Denoiser pipeline.** Where the denoiser runs in
  the project's render flow: GPU render → AOV buffers →
  denoiser → final image. Trigger modes (manual vs
  automatic-after-render). Output path. Documented in §9
  below.
- **19A.4 — API surface.** `Denoiser` class shape, the
  `make_denoiser(Backend)` factory, the `Result` struct,
  the buffer-flow direction (device-buffer-in /
  device-buffer-out vs Image-in / Image-out).
- **19A.5 — Buffer-flow design.** Concrete diagram of how
  Beauty / Normal / Albedo move from the renderer's
  device-side AOV buffers through the denoiser to the
  PPM file, including which buffer the resolve writes to
  and which buffer the denoiser writes to.
- **19A.6 — Integration with existing CLI handlers.**
  Where the denoiser plugs into `--render-pathtrace`, how
  the server's `render` verb selects denoise on / off,
  the `--denoise` / `--no-denoise` CLI surface, the
  `[GPU] denoiser: time = X.XXX ms` log line shape.
- **19A.7 — Audit + risk review.** Documented before any
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

---

## 8. Required and optional inputs (Stage 19A.2)

This section is the formal version of the input contract
sketched in §4.3. §4.3 says "the denoiser consumes existing
Stage 14A AOVs"; this section is the concrete mapping —
exactly which AOV feeds which denoiser slot, in what layout,
in what units, and with what optional-vs-required posture.

The slice is documentation-only; no code, no header
additions, no kernel changes. The mapping is what 19B's
implementation slice will program against.

### 8.1 Required inputs

The OptiX denoiser's "RGB + albedo + normal" model — the
project's primary 19B target — has one required input
(Beauty) and two optional inputs (Albedo, Normal) that are
recommended in practice. We treat all three as **required**
for the project's 19B contract because (a) the existing AOV
pipeline produces all three side-by-side without extra work
and (b) running the denoiser on Beauty alone produces
visibly worse output on the relativistic-shading edge cases
this project specifically wants to handle.

| # | Input | OptiX role          | Component count | Required for 19B |
|---|-------|---------------------|-----------------|------------------|
| 1 | Beauty (noisy) | input layer            | RGB (3 floats / pixel) | **yes** |
| 2 | Albedo         | guide layer (denoise hint) | RGB (3 floats / pixel) | **yes** |
| 3 | Normal         | guide layer (denoise hint) | XYZ (3 floats / pixel) | **yes** |

The "noisy" qualifier on Beauty is the OptiX terminology:
the denoiser's input layer is the un-cleaned Monte Carlo
estimate that came out of the path tracer's spp loop. The
denoised result is what 19B writes back to the host.

#### 8.1.1 Beauty (noisy)

**Definition.** The path tracer's resolved per-pixel
radiance estimate. After `AccumulationBuffer::resolve_to
_image()` divides the per-pixel sum by `samples_count()`,
this is the buffer that today's CLI handler downloads
and writes to PPM. Linear-space RGB; no tone mapping
applied; no gamma encoding; values are unbounded floats
representing physical-ish radiance (the path tracer is
not energy-calibrated but the units are internally
consistent).

**Source.** Per §1.2 of `RELATIVITYRENDER_CLAUDE_MASTER_
INSTRUCTIONS.txt`, the path tracer is the primary noise
source the denoiser exists to clean up. Today's path
tracer produces this buffer through two device-side
steps:

1. `launch_pathtrace_sample(...)` writes one sample
   frame into a per-launch sample buffer (Rgba32F).
2. `AccumulationBuffer::accumulate_sample(...)` adds it
   to the running sum (Stage 18A.4 fast path on the
   first sample).
3. `AccumulationBuffer::resolve_to_image()` divides by
   the sample count via `launch_accum_resolve` (Stage
   18A.4 float4 fast path) into a fresh device-side
   display buffer.

The denoiser consumes the resolve output (or its
direct device-side equivalent) — see §8.4 for the
buffer-flow choice 19A.5 will finalise.

**Special-case note for non-pathtrace renders.** The
existing scene-render path (`CudaRenderer::render
_scene{,_with_aovs}`) produces a Beauty buffer directly
without going through `AccumulationBuffer`. The Stage
14A AOV pipeline already populates a separate
`GpuAOVBuffer` for `AOVType::Beauty` when the caller
requests it (`render_scene_with_aovs`). For
non-pathtrace inputs the denoiser is essentially a
no-op (single-sample renders have no Monte Carlo
variance), but the input contract is the same shape.

#### 8.1.2 Albedo

**Definition.** The base reflectance colour at the hit
point, **before** lighting and before the relativistic
Doppler / searchlight transforms. Linear-space RGB,
typical values in `[0, 1]` for physically-plausible
materials; HDR base colours (>1) are allowed and the
denoiser tolerates them.

**Why it helps.** The OptiX denoiser uses albedo as a
guide for "where there are sharp colour edges that the
beauty noise should be denoised *along*, not *across*".
Without it, two adjacent pixels with different
materials but similar noisy radiance get smoothed
together; with it, the denoiser preserves the material
boundary.

**Source.** The Stage 14A.3 kernel
`render_scene_with_aovs` writes per-pixel albedo into
the `targets.albedo` device pointer. The kernel reads
the hit's `MaterialParams::baseColor` (or, when
`useBaseColorTexture` is set, samples the texture at
the hit's interpolated UV) and writes the un-lit value
into the AOV buffer. Per the AOV documentation in
`AOV.h`, the field is "RGB (3 floats). Base colour at
the hit *before* lighting, after texture lookup."

This is exactly the OptiX denoiser's albedo
expectation — no additional preprocessing is needed.

#### 8.1.3 Normal

**Definition.** The shading normal at the hit point in
**world space**, components in `[-1, 1]`. The OptiX
denoiser explicitly documents its normal input as
world-space XYZ, not view-space; the project's AOV
already produces world-space normals so the contract
matches.

**Why it helps.** The denoiser uses surface-normal
similarity as its second "smooth along, not across"
guide. Curved surfaces with varying normal direction
produce noise that the denoiser must respect rather
than flatten; sharp normal discontinuities (a cube's
edge, a hard normal break) are similarly preserved.

**Source.** The Stage 14A.3 kernel writes the hit's
geometric or shading normal into `targets.normal`. The
existing CUDA code path uses `Hit::normal` directly,
which is the world-space hit-normal `intersect_sphere`
/ `intersect_triangle` produce. No transform / re-frame
step is required between AOV write and denoiser read.

The world-space convention is consistent across all
the project's render paths (CUDA closest-hit and OptiX
closest-hit), so the denoiser sees the same field
shape regardless of which backend produced the AOV.

### 8.2 Optional inputs

Two optional inputs are **declared** today so the API
surface lands stably in 19A.4, but neither is consumed
by the 19B implementation. Both are deferred for
documented reasons.

| # | Input | OptiX role | Component count | Status |
|---|-------|------------|-----------------|--------|
| 4 | Depth  | (not a standard OptiX denoiser input today) | 1 float / pixel | **future** |
| 5 | Motion | guide layer (temporal flow vectors) | 2 floats / pixel | **future** |

#### 8.2.1 Depth (future)

**Why optional / future.** The OptiX 7.5+ AI denoiser
does **not** consume a depth buffer as part of its
documented input set. The standard "Beauty + Albedo +
Normal" model has no depth slot; the temporal model
adds a flow buffer (motion vectors) but not depth.

The reason to keep depth on the input list at all is:

- **Future custom-denoiser experiments.** A
  spatiotemporal filter built around a custom kernel
  (e.g. an A-trous wavelet pass with edge-stopping
  functions) frequently uses depth to identify
  surfaces; reserving the slot now means the API
  surface designed in 19A.4 already has the input
  channel and 19C+ work doesn't need to re-litigate.
- **Future adaptive-sampling driver.** A future slice
  that pairs the renderer with variance-driven sample
  redistribution would benefit from depth as a
  surface-identity signal; same plumbing.
- **Author / debug visualisation.** The Stage 14A AOV
  is already authored and saved as `aov_depth.ppm`;
  the denoiser slice does not need to re-introduce
  it, just declare the optional consumer slot.

19B will reject any caller that provides Depth as a
denoiser input and document that "Depth is an
optional future input; no current denoiser backend
consumes it."

**Source (already exists).** The Stage 14A.3 kernel
writes per-pixel depth into `targets.depth`. The unit
choice ("ray `t`, view-space Z, or true distance") is
explicitly the renderer's, per `AOV.h`; today the
implementation uses the hit's `t` value directly.
Whatever the chosen unit ends up being, it is what
the denoiser sees if a future slice flips the
optional-input switch on.

#### 8.2.2 Motion (future)

**Why optional / future.** The OptiX denoiser's
**temporal** mode (the same one progressive denoise
in §2.2 will eventually use) consumes a 2-channel
motion-vector buffer that says, per pixel,
"this pixel's image-plane projection moved by
(`dx`, `dy`) since the previous frame." Without
that buffer, temporal denoising falls back to
re-running the spatial denoiser on each frame
independently — same quality as final-frame mode,
no temporal stability.

The project does not produce motion vectors today
(documented as out-of-scope in §5; the prerequisites
land in master-order #25, native Cinema 4D renderer
integration). The denoiser slice declares the slot
so 19A.4's API surface includes the optional motion
input from day one; 19B refuses callers that try to
supply it; 19C ships the actual temporal denoiser
once the renderer can populate the buffer.

**No existing AOV.** Unlike Depth, **Motion is not
a Stage 14A AOV today.** The Stage 14A enum has six
entries (Beauty / Normal / Depth / Albedo /
DopplerFactor / SearchlightFactor); none of them
encodes per-pixel image-plane velocity. A future
slice (likely 19C or its prerequisite) extends the
AOV enum with `AOVType::Motion` (component count =
2), wires `render_scene_with_aovs` to populate it
from per-pixel previous-frame reprojection, and
plumbs the resulting `GpuAOVBuffer` through to the
denoiser.

This is the only required-but-missing AOV the
denoiser plan calls for. Spelling it out here means
the 19A.4 API surface and the eventual 19C work
have a known dependency to schedule.

### 8.3 Mapping to existing Stage 14A AOV buffers

For every input the denoiser consumes today, this is
the concrete plumbing — which `GpuAOVBuffer` the
denoiser reads and which `render_scene_with_aovs`
target field populates it.

| Denoiser input | `AOVType` | `GpuAOVBuffer` source | Renderer write path | Component count |
|----------------|-----------|------------------------|---------------------|-----------------|
| Beauty (noisy) | `AOVType::Beauty` | `make_default_aov_set()[0]` | `render_scene_with_aovs(...).targets.beauty` | 3 floats / pixel |
| Albedo         | `AOVType::Albedo` | `make_default_aov_set()[3]` | `render_scene_with_aovs(...).targets.albedo` | 3 floats / pixel |
| Normal         | `AOVType::Normal` | `make_default_aov_set()[1]` | `render_scene_with_aovs(...).targets.normal` | 3 floats / pixel |
| Depth (future) | `AOVType::Depth`  | `make_default_aov_set()[2]` | `render_scene_with_aovs(...).targets.depth` | 1 float / pixel |
| Motion (future) | (not yet defined) | (NEW: future `AOVType::Motion`) | (NEW: future `targets.motion`)                          | 2 floats / pixel |

The first three rows define the 19B input contract
end-to-end: the host-side caller allocates the
`make_default_aov_set()` buffers, the renderer's AOV
pass writes into them, the denoiser reads from them.
No new AOV plumbing is added by the denoiser slice
itself.

The fourth row is the depth row that exists in the AOV
set today but is not consumed by 19B. The fifth row is
the only missing piece; it is gated on the future
motion-vector slice.

#### 8.3.1 Component-count cross-check vs OptiX

The OptiX denoiser's `OptixImage2D` struct has a
`format` field that selects channel count.

| Input | OptiX format expected | AOV component count | Match? |
|-------|------------------------|---------------------|--------|
| Beauty | `OPTIX_PIXEL_FORMAT_FLOAT3` (or `FLOAT4`) | 3 | **yes** (FLOAT3) |
| Albedo | `OPTIX_PIXEL_FORMAT_FLOAT3` | 3 | **yes** |
| Normal | `OPTIX_PIXEL_FORMAT_FLOAT3` | 3 | **yes** |

The Stage 14A AOV layout is already exactly what the
OptiX denoiser expects for the three required inputs;
no padding, no swizzling, no per-channel conversion is
needed between the renderer's write and the denoiser's
read.

The non-trivial format decision is on Beauty: OptiX
accepts both FLOAT3 and FLOAT4. The 14A AOV is
FLOAT3, which is the smallest viable layout. The
path-tracer's `resolve_to_image()` produces an
`Image::PixelFormat::Rgba32F` (FLOAT4) device buffer
internally before download. **The 19B implementation
slice picks one of two routes**:

- **(A) Read the FLOAT3 AOV directly.** Requires
  `render_scene_with_aovs` to produce a Beauty AOV.
  The path tracer does not currently call into
  `render_scene_with_aovs` — it has its own kernel
  + `AccumulationBuffer::resolve` flow. Wiring the
  path tracer to also write the AOV is renderer-side
  work that violates §4.2's "must not modify core
  renderer logic" rule.
- **(B) Read the FLOAT4 resolve output.** Tells
  OptiX `OPTIX_PIXEL_FORMAT_FLOAT4` and reads the
  resolve buffer directly; the alpha channel is
  ignored by the denoiser. No renderer changes
  required; the denoiser slice strictly post-
  processes what `resolve_to_image()` produced.

19A.5 (buffer-flow design) finalises the choice;
today's strong recommendation is (B) for §4.2
compliance. This is the kind of decision the
follow-up sub-stage exists to lock in before
implementation starts.

### 8.4 Buffer-flow direction (preview)

Detailed wiring lands in 19A.5 (buffer-flow design),
but the input contract drives one decision now: the
denoiser reads device pointers, not host buffers. For
each required input the denoiser slice's API takes a
`const float*` device pointer plus dimensions. The
caller — the renderer host orchestration — keeps the
underlying `GpuAOVBuffer` (or the path tracer's
internal display buffer) alive across the denoiser
call.

This matches the existing `CudaRenderer::AOVTargets`
pattern: raw `float*` pointers passed into a CUDA-
side kernel, the host owns the `GpuAOVBuffer` /
`GpuBuffer`. The denoiser is structurally identical
on the input side.

### 8.5 What this sub-stage commits to

- **Required-input set.** Beauty, Albedo, Normal —
  all three are required for the 19B implementation,
  per the rationale in §8.1.
- **Optional-input set.** Depth and Motion are
  declared so the API surface designed in 19A.3
  includes the optional channels, but neither is
  consumed by 19B. Depth's optional-future status
  is documented; Motion needs a new AOV
  (`AOVType::Motion`, 2 floats / pixel) that is
  out of scope for any 19A or 19B sub-stage.
- **AOV mapping.** Concrete, one-to-one mapping
  between each input and the existing Stage 14A
  AOV buffer that supplies it. No new AOV types
  are added by the denoiser slice; the only gap
  (Motion) is owned by a future slice and tracked
  here.
- **Format-mismatch risk.** Beauty's FLOAT3 vs
  FLOAT4 ambiguity is documented; 19A.4 picks
  the buffer-flow route, with route (B) — read
  the FLOAT4 path-tracer resolve output —
  recommended to preserve the §4.2 "no renderer
  changes" rule.
- **No code, no API headers.** Per the prompt's
  "Do not implement code" rule, this sub-stage
  ships only documentation. The
  `Denoiser::Inputs` struct shape and the
  `make_denoiser` factory signature are 19A.4's
  output; the buffer-flow diagram is 19A.5's;
  the implementation is 19B's.

---

## 9. Pipeline (Stage 19A.3)

§8 fixes the input contract; §9 fixes **where** the
denoiser sits in the project's render flow, **when** it
runs, and **where** its output goes. The buffer-flow
direction (which device buffer the denoiser reads, which
device buffer it writes to) is intentionally deferred
to 19A.5; this section is the higher-level pipeline
placement that 19A.5 will plug a concrete diagram into.

### 9.1 Pipeline placement

The denoiser is a strict post-process stage at the tail
of the render pipeline. The data flow:

```text
┌───────────┐   ┌──────────────┐   ┌──────────┐   ┌───────────┐
│ GPU render│──▶│ AOV buffers  │──▶│ Denoiser │──▶│Final image│
└───────────┘   └──────────────┘   └──────────┘   └───────────┘
   (Stage 11C   (Stage 14A.3       (Stage 19B    (host-side
    PathTracer  GpuAOVBuffer x N    on the same   Image::save_ppm
    + Stage     populated by        OptixDevice   write to PPM,
    11B accum   render_scene_       Context the   exactly the
    + Stage     with_aovs)          renderer      same path the
    18A.4 fast                      already       un-denoised
    paths)                          owns)         output uses)
```

**Stage by stage:**

1. **GPU render.** The path tracer (or any other
   render-* CLI handler that consumes
   `render_scene_with_aovs`) produces a per-pixel
   noisy radiance estimate on the device. Today the
   spp loop + `AccumulationBuffer::resolve_to_image()`
   yield the resolved Beauty buffer; the same kernel
   pass populates the side-channel `GpuAOVBuffer`s for
   Normal / Albedo. No new GPU work is added by 19A.3.
2. **AOV buffers.** The Stage 14A.3 `GpuAOVBuffer`
   set is the data hand-off layer. The denoiser
   reads from these device pointers in place; the
   renderer is the buffer's writer, the denoiser is
   its reader. §8.3 gives the concrete
   AOVType-to-buffer mapping; this section stays
   higher-level.
3. **Denoiser.** A single `Denoiser::run(...)` call
   on the host that ferries the device pointers into
   `optixDenoiserInvoke` (or the future CPU
   fallback). The denoised result lands in a fresh
   device buffer the caller owns. Time is captured
   under the same Stage 18A.1 `[GPU] denoiser:
   render time = X.XXX ms` line as every other
   GPU pass.
4. **Final image.** The host downloads the denoised
   device buffer into an `rr::image::Image`
   (Rgba32F) and writes it through
   `Image::save_ppm`, exactly the same path
   `--render-pathtrace` and every other CLI handler
   uses today. The denoiser slice does not introduce
   a new image-IO format, a new colour-space hook,
   or a new tone-mapper.

The pipeline shape is identical to the un-denoised
flow (`GPU render → host download → save PPM`) plus
one extra stage between "AOV buffers" and "save". The
denoiser does **not** sit anywhere else - in
particular, it does not run before resolution, it
does not run inside the spp loop, and it does not
modify the renderer's per-sample buffer. Per §4.2,
no renderer-side change is needed to land 19A.3's
pipeline.

### 9.2 Trigger modes

Two ways to launch the denoiser stage. Both share the
same `Denoiser::run(...)` API surface designed in
19A.4; they differ only in **who** calls it and
**when**.

#### 9.2.1 Manual trigger

The CLI / API caller decides whether to denoise.
Concrete shape: a `--denoise` flag on the existing
`--render-pathtrace` action (and any other render-*
action that exposes Beauty / Normal / Albedo AOVs
via `render_scene_with_aovs`). When the flag is
present the handler runs the spp loop, captures
the AOV buffers, calls `Denoiser::run`, and writes
the denoised PPM. When the flag is absent the
handler short-circuits the denoiser and writes the
un-denoised PPM exactly the way it does today.

A symmetric `--no-denoise` flag is documented in
the 19A.6 CLI-integration sub-stage; it is the
default for handlers that opt into automatic mode
(see §9.2.2). Together the two flags let a caller
override the action's default in either direction
without touching any other configuration.

The server's `render` verb (Stage 15) gets the same
manual-trigger control: the protocol gains a
boolean `denoise` argument that maps to the same
host-side switch. The protocol surface is finalised
in 19A.6.

**Why manual is the default for 19B.** The first
implementation slice cares about *correctness* of
the pipeline placement, not artist-grade defaults.
Forcing the caller to opt in keeps the un-denoised
behaviour identical to the Stage 18A.4 baseline
(useful for the existing CLI tests + the BUILD_PLAN
"visual outputs match the Stage NNNN baseline
byte-for-byte" guarantees) and means 19B's
acceptance criteria can be a strict superset of
19A.x without any visual-baseline regression.

#### 9.2.2 Automatic after render

A render-time flag on the action that says "always
denoise". When set, the handler runs the same
end-of-render denoise pass without the caller
having to remember `--denoise` on every CLI line.

This is the right default for an interactive /
artist-facing workflow:

- **Server preview pass.** The Stage 15 `render`
  verb's preview mode (low-spp, tied to the
  artist's edit cadence) is unusable without
  denoising at 1-4 spp. Automatic mode is what
  makes the server's preview channel correct.
- **Cinema 4D bridge.** A future bridge slice
  (master order #21) calls into the server
  per-frame; the bridge's render-button is
  semantically "produce a clean image", not
  "produce a noisy image and a denoised image".
  Automatic mode matches that contract.
- **`--render-pathtrace` for headless batch.** The
  un-decorated CLI command "render this scene"
  reasonably means "produce a clean image"; the
  manual flag exists to opt **out** for
  diagnostic / training / before-after compare
  purposes.

Automatic mode is **not** silent: it still emits
the Stage 18A.1 `[GPU] denoiser: ...` timing line
and the `wrote denoised: <path>` log message. The
artist sees that the denoiser ran; the absence of
that log line on a render that should have hit
automatic mode is a regression signal.

#### 9.2.3 Mode-selection precedence

Within a single render dispatch the precedence is
explicit-flag > action-default > project-wide
default. Concretely:

1. `--no-denoise` on the CLI → never denoise, even
   if the action defaults to automatic.
2. `--denoise` on the CLI → always denoise, even if
   the action defaults to manual / no-denoise.
3. Action-default → the per-action policy (e.g.
   `--render-pathtrace` defaults to automatic if
   19A.6 lands that way; the bare `--render-scene`
   defaults to manual because there is no spp
   loop and therefore no noise to remove).
4. Project-wide default → today, manual everywhere
   (no flag = un-denoised), so the existing CLI
   acceptance tests stay green for the 19B slice.

The 19A.6 CLI-integration sub-stage finalises the
per-action defaults and the precedence rules; this
section commits to the precedence shape, not the
specific defaults.

### 9.3 Output

The denoised result goes to `output/denoised.ppm`
by default. The full output rules:

| Action | Default un-denoised path | Default denoised path |
|--------|---------------------------|------------------------|
| `--render-pathtrace`        | `output/pathtrace_spp_*.ppm` (one per spp run) | `output/denoised.ppm` (single, of the highest-spp run) |
| `--render-scene` / `--render-scene-from-file` | `output/gpu_scene_spheres.ppm` / `output/from_scene_spheres.ppm` | `output/denoised.ppm` |
| Server `render` verb         | `output/server_render.ppm` | `output/denoised.ppm` |
| Any action with `--output`   | `--output` value | the same value with `_denoised` inserted before the extension, e.g. `output/foo.ppm` → `output/foo_denoised.ppm` |

Three rules the table encodes:

1. **`output/denoised.ppm` is the project-wide
   default name** when the caller does not pass
   `--output` and the action's denoise mode is on.
   Fixed name; one image per render dispatch. The
   denoiser does not retain previous frames.
2. **`--output` overrides the path but injects
   `_denoised` into the stem** so a single render
   dispatch with both un-denoised and denoised
   outputs (a future "compare" mode, deferred)
   does not collide on the same file.
3. **Existing un-denoised outputs are unchanged**
   when denoising is off. The Stage 18A.4 baseline
   PPM bytes are preserved exactly when no denoise
   flag is present.

Format: PPM, RGBA32F internal → uint8 clamp via
the existing `Image::save_ppm` path. Linear-space
radiance in, sRGB-display PPM out (the float→uint8
clamp's existing behaviour, unchanged from
today). The denoiser slice does **not** introduce
EXR / HDR output, a colour-space hook, or a tone
mapper - all are deferred per §4.4.

When the OptiX SDK is not present (audit-host
fallback) and the denoise mode is on, the handler
returns a documented "requires OptiX SDK" error
without producing the denoised PPM. The
un-denoised output is also not produced (the
caller asked for denoised output; refusing the
request is more honest than silently dropping the
denoise step). When the denoise mode is off the
handler runs exactly as it does today.

### 9.4 Where this slice does NOT change the pipeline

For symmetry with §4 and §8.5, an explicit list of
what 19A.3 / 19B leave untouched:

- The renderer's per-sample kernel
  (`launch_pathtrace_sample`, `k_render_scene`,
  the OptiX programs). No denoiser-aware sampling,
  no variance feedback, no early termination.
- `AccumulationBuffer`'s clear / accumulate /
  resolve flow (Stage 11B / Stage 18A.4 fast
  paths). The denoiser reads what `resolve_to
  _image()` produces; it does not pre-empt the
  resolve.
- The AOV pipeline (`render_scene_with_aovs`,
  `GpuAOVBuffer`, `AOV.{h,cpp}`). The denoiser
  consumes existing AOVs without adding new
  types; the only future-required AOV (Motion;
  see §8.2.2) is owned by a separate slice.
- `Image::save_ppm`. The denoiser writes through
  the same host-side IO path; no PPM-format hook,
  no compression flag, no metadata sidecar.
- Server protocol verbs other than `render` (Stage
  15A.2's `ping`, Stage 15B.1's `load_scene`,
  Stage 15B.3's `set_beta`). The denoiser slice
  adds at most a `denoise` argument to the
  existing `render` verb in 19A.6.

### 9.5 What this sub-stage commits to

- **Pipeline placement.** Denoiser is the last
  device-side stage before host download +
  PPM save. Reads the existing AOV buffers; writes
  to a fresh device buffer the caller downloads.
- **Two trigger modes.** Manual (explicit
  `--denoise` flag) and automatic (action-default
  on for path-traced / preview render paths).
  Precedence rule: explicit flag > action-default
  > project-wide default.
- **Output path.** `output/denoised.ppm` by
  default; `--output` overrides with
  `_denoised`-suffixed stem. Existing un-denoised
  paths unchanged when denoising is off.
- **No code.** Per the prompt's "Do not
  implement code" rule. The `Denoiser::run` API
  surface is 19A.4's; the per-action defaults
  are 19A.6's; the implementation is 19B's.
- **No renderer-side change.** Per §4.2 +
  §9.4, the existing render kernels +
  AccumulationBuffer + AOV pipeline +
  `Image::save_ppm` are all unaffected by 19A.3
  / 19B.

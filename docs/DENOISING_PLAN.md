# Denoising — Research / Design Plan

Status: **specification only**, design slice. No denoiser code
exists yet. This document is the contract that future
implementation slices will deliver against; the roadmap
flags M22 as the milestone, with OptiX as the v1 target and
OIDN as a possible later peer.

Module reference: `src/denoise/` (module 16 in
`docs/MODULE_MAP.md`, milestone M22 in
`docs/MILESTONE_ROADMAP.md`).

## 1. Purpose

RelativityRender's path tracer (M14) is unbiased but
samples noisily at low spp. The two real-world preview
paths the renderer ships - the C4D bridge dialog (M19) and
the eventual standalone preview (M20) - both want a
recognisable image after one or two seconds of work, not
after the converged 1-min render. The integrator's hot
path can't fix that on its own; what fixes it is a
post-process denoiser that consumes the noisy beauty
buffer plus a small set of auxiliary AOVs and produces a
much cleaner image.

This document explains:

- Why a denoiser is the right next step after the path
  tracer + AOVs land.
- What the OptiX denoiser is, what its variants do, and
  how it would sit inside RelativityRender.
- Which AOVs the denoiser needs and how they relate to
  the AOVs the renderer already produces (M17).
- How beauty, albedo, and normal each contribute to the
  denoised result, and the encoding / coordinate-space
  contract each must satisfy.
- The unique constraint RelativityRender adds compared
  to a vanilla path tracer (relativistic effects modify
  the perceived radiance) and how the denoiser pipeline
  handles it without retraining.
- The progressive render workflow that ties the per-
  sample integrator and the per-frame denoiser together.

It deliberately stops there. Algorithm-internal details
(temporal accumulation, motion vector generation,
upscaling, learned priors) and the OIDN peer wrapper get
their own document slices when each becomes relevant.

## 2. Why denoise

Three concrete pressures push the renderer towards a
denoiser, in order of immediacy:

- **Preview UX.** The C4D bridge today blocks the C4D UI
  for the entire render (M19 ext 6); a usable preview
  needs a recognisable image at one to two samples per
  pixel. At those sample counts the path tracer's
  variance is large; a denoiser collapses the variance
  while preserving texture / edge detail through the
  auxiliary AOVs.
- **Time-to-final cost.** Even on a converged offline
  render, the integrator needs many more samples to
  reach a noise floor a denoiser would produce in one
  pass over a low-spp result. That gap is significant
  on heavy scenes.
- **Honesty about the relativistic case.** The
  relativistic perception path multiplies the per-hit
  radiance by a Doppler factor and a searchlight gain
  (M9 / M14). The integrator's variance is the same as
  a vanilla path tracer, but the multipliers can amplify
  remaining noise dramatically when `|beta|` is large.
  The denoiser tames that without changing the physics.

A cheap CPU box-blur on the beauty buffer cannot replace
a denoiser: it does not preserve edges, it cannot
distinguish noise from real high-frequency texture, and
it has no notion of geometric edges separating two
overlapping albedos. The auxiliary AOVs (albedo / normal)
are how a denoiser preserves those signals.

## 3. The OptiX denoiser at a glance

The OptiX denoiser is NVIDIA's GPU-resident, AI-trained
post-process that consumes a noisy radiance buffer and
optional auxiliary buffers, and produces a denoised
radiance buffer of the same resolution. It runs on the
device, plays cleanly with the CUDA / OptiX path the
renderer is migrating to (M15 plan), and ships with the
OptiX SDK.

The SDK exposes a small handful of denoiser kinds; the
ones relevant to a v1 integration are:

| Kind         | What it does                                             | When v1 cares                         |
|--------------|----------------------------------------------------------|---------------------------------------|
| **HDR**      | Single-frame denoise of an HDR linear-RGB beauty buffer. | Default v1 target.                     |
| **AOV**      | HDR plus auxiliary buffers (albedo, normal). Significantly better quality at low spp. | Lights up once the M17 AOVs flow in.  |
| **Temporal** | AOV plus prev-frame inputs + motion vectors.             | Animation; out of scope for v1.        |
| **Upscale**  | AOV plus 2x output resolution.                           | Future preview path; out of scope.     |

v1 ships the **AOV variant**. The HDR-only path is a
fallback for legacy scenes that do not produce auxiliary
buffers (none of RelativityRender's scenes today fall in
that bucket - M17 already produces albedo + normal AOVs).
Temporal and Upscale wait until the renderer has motion
vectors and a progressive-stream UI to consume them.

The denoiser's call shape is straight-line:

```
   1. optixDenoiserCreate(...)
   2. optixDenoiserComputeMemoryResources(width, height, ...)
   3. allocate scratch + state buffers
   4. optixDenoiserSetup(stream, w, h, state, scratch, ...)
   5. for each frame:
        optixDenoiserInvoke(stream,
                            params,
                            state, scratch,
                            { beauty, albedo, normal },
                            output)
        cudaStreamSynchronize(stream)
        copy output -> host (or display from device)
```

Setup happens once per (width, height); invoke runs every
frame. Memory cost scales with resolution; at 1920x1080
the working set is in the low tens of MB on top of the
renderer's existing framebuffer / AOV / scene allocations.

## 4. Required AOVs

The denoiser eats up to three buffers per invocation. v1
ships exactly these three; the renderer's M17 AOV
foundation already produces close approximations of all
three, with one new addition needed for the relativity
case (see section 6.4).

| Denoiser input | Format         | Role                                                     | Sourced from                     |
|----------------|----------------|----------------------------------------------------------|----------------------------------|
| Beauty         | HDR linear RGB | The noisy radiance buffer to denoise.                    | New `PhysicalBeauty` AOV (6.4).  |
| Albedo         | Linear RGB     | Per-hit surface base colour, pre-lighting.               | M17 `Albedo` AOV.                |
| Normal         | Vec3, unit-length | Per-hit surface normal in a chosen space (6.3).       | New raw-normal variant of M17 `Normal`. |

The four other M17 AOVs (`Beauty` post-relativity,
`Depth`, `DopplerFactor`, `SearchlightFactor`) are NOT
denoiser inputs. `Depth` could plug into a future
temporal / upscale path; the relativity-factor AOVs are
inspection / debugging surfaces.

The **denoiser's output** is a single buffer the same
resolution as the beauty input. v1 stores it as a new
host-side `Image` (Rgba32F) so the existing
`save_ppm` / display paths consume it identically to a
non-denoised render.

## 5. Per-AOV contract

This section pins what each AOV the denoiser eats MUST
look like at the byte level. The contracts mirror what
the OptiX denoiser expects and what the M17 AOV
foundation already produces; deviations are called out
explicitly so a future implementation slice has zero
ambiguity.

### 5.1 Beauty

Three-channel linear-RGB HDR buffer. Per-pixel value is
the integrator's running mean of the noisy radiance
estimate.

The denoiser is robust to HDR magnitudes - direct light
pixels, fireflies, emissive surfaces all keep their
brightness through the network. Crucial property:
**linear** RGB. Tone-mapping or sRGB-encoding the input
defeats the denoiser; the renderer's `Image::save_ppm`
does its own clamp + 8-bit quantisation only after
denoising.

The buffer's storage layout matches the rest of the
renderer's framebuffer: `Rgba32F`, row-major, top-left
origin, channel-interleaved. Alpha is unused by the
denoiser; the renderer keeps it as `1.0` so existing
consumers (PPM save, future EXR save) read a sensible
value.

### 5.2 Albedo

Three-channel linear-RGB buffer. Per-pixel value is the
surface's pre-lighting base colour at the closest hit -
the texture-sampled colour from `MaterialParams::baseColor`
(or, M21+, the graph evaluator's resolved albedo). Misses
get the resolved sky / environment colour so the buffer
is non-empty even on background pixels.

This is what M17's `Albedo` AOV already produces (the
catalogue's `aov_kind_name(AOVKind::Albedo) == "albedo"`
entry). v1 reuses it directly; no new kernel work
required.

### 5.3 Normal

Three-channel vec3 buffer. Per-pixel value is the
surface normal at the closest hit, **unit-length**, in a
fixed coordinate space pinned below.

The OptiX denoiser accepts either world-space or
camera-space normals; v1 picks **world-space** to mirror
the rest of the renderer's geometry contract
(`Hit::normal` is world-space per `src/renderer/Hit.h`).
The choice is documented so a future denoiser-pipeline
slice doesn't silently drift.

M17's `Normal` AOV stores the encoded form
`0.5 * N + 0.5` for human-readable visualisation. The
denoiser wants the **raw, unit-length** normal. Two
options for v1; future slice picks one:

- **(a)** Write a second AOV (`AOVKind::RawNormal`) at
  closest-hit time alongside the existing `Normal`.
  Doubles the bytes; no decode pass needed.
- **(b)** Decode `2 * N - 1` from the existing AOV at
  denoise time. One extra kernel pass; no new buffer.

Either option produces the same input to the denoiser.
The implementation slice picks based on whether the M17
normal AOV is needed for visualisation in the same
session as the denoised render.

### 5.4 The PhysicalBeauty addition

This is the project-specific addition. The OptiX
denoiser is trained on standard photographic lighting.
When the renderer's relativistic path multiplies a hit's
radiance by `D` (Doppler factor) and `D^4` (searchlight
gain), the resulting beauty buffer at high `|beta|` is
not in the regime the network expects: extreme blue-shift
or red-shift, multi-stop searchlight gain, anisotropic
intensity across the frame. The denoiser still runs, but
quality drops.

The cleanest fix is a **physical** beauty AOV: the
integrator's running mean WITHOUT Doppler / searchlight
applied. The pipeline becomes:

```
   integrator -> PhysicalBeauty + Albedo + RawNormal
   denoise(PhysicalBeauty, Albedo, RawNormal) -> DenoisedPhysical
   apply_doppler(DenoisedPhysical, D)         -> DenoisedShifted
   apply_searchlight(DenoisedShifted, D^4)    -> Beauty (final)
```

The relativistic effects are deterministic functions of
the (per-pixel, per-frame) primary ray direction and the
observer velocity. Reapplying them after denoising
preserves the visible relativistic look at no
denoiser-quality cost.

`PhysicalBeauty` joins the M17 AOV catalogue as a new
kind; the kernel produces it for free (it is already
the integrator's pre-relativity radiance). The existing
post-relativity `Beauty` AOV stays as the
"what the camera sees, no denoise" reference.

A FALLBACK path exists for callers that disable the
relativistic effects entirely (the strength sliders at
zero): in that mode `PhysicalBeauty == Beauty` byte-for-
byte and the post-denoise reapply step is the identity.

## 6. Progressive render workflow

A progressive render produces a sequence of frames of
increasing sample count. The denoiser slots into that
sequence at a frequency the implementation chooses. v1
pins the contract; the cadence is an implementation
slice's call.

### 6.1 The sample-accumulation loop

The integrator's `render_pathtrace` (M14) already takes
`spp` and `seed_offset` parameters; the progressive path
runs it in chunks:

```
   total_spp = 0
   accum     = zero-init Image
   while running:
     batch  = render_pathtrace(scene, w, h, spp=B, seed_offset=total_spp)
     accum  = (accum * total_spp + batch * B) / (total_spp + B)
     total_spp += B
     if total_spp in {1, 2, 4, 8, 16, 32, ...}:
        emit_progressive_frame(accum, aovs)
```

Each `emit_progressive_frame` call runs the denoiser on
the current accumulator + the current AOVs and surfaces
the denoised image to the consumer (the C4D bridge, the
preview UI, the renderer-server protocol).

The auxiliary AOVs (`Albedo`, `RawNormal`,
`PhysicalBeauty`) accumulate the same way - their values
are deterministic given the primary ray direction at
each sample, so they converge fast. v1 accumulates them
in the same loop; future variants may freeze them after
N samples to save bandwidth.

### 6.2 Denoise frequency

Two regimes:

- **Per-frame-emit denoise.** Run the denoiser every time
  the loop emits a frame (powers of two, as the snippet
  above suggests). Cheap at low resolutions; the
  denoiser's per-frame cost is small compared to the
  integrator's per-sample cost.
- **Final-frame-only denoise.** Skip the denoiser for
  most progressive frames; run it on the last frame
  only. v1 picks this for the offline / final-output
  path so the on-disk image is the denoised one
  without intermediate noise.

Implementation chooses per-render mode based on whether
the consumer is interactive (preview) or offline
(final). Both modes share the same denoiser setup and
input AOVs.

### 6.3 Server-protocol implications

The renderer server's M18 protocol today carries one
final frame per `render` command. A progressive flow
needs a new command (`render_progressive`?) that streams
frames as they emerge. That extension is a future slice;
v1 of the denoiser ships behind the existing single-
frame `render` command and runs final-frame-only.

The C4D bridge dialog (M19 ext 5) maps cleanly: the
existing `Render` button issues `render`, gets back one
denoised frame, and surfaces it. Adding progressive is
a future C4D-bridge slice.

## 7. Integration shape

`src/denoise/` is the new module. Per the module map's
forbidden-imports rule it MUST NOT touch UI or Cinema 4D
or Path Tracer internals; it consumes the renderer's
output buffers + AOVs and depends on:

- **Image** module (host buffer types).
- **AOV** module (the M17 AOV foundation).
- **GPU Device Layer** (`GpuBuffer<float>` for staging
  the denoiser's inputs / outputs).
- **OptiX Backend** (`src/optix/`, M15) for the
  device-context handle the denoiser piggybacks on.
- **Core Engine** (logging, error reporting).

Public surface, per the module map:

```cpp
namespace rr::denoise {

struct DenoiseInputs {
    rr::image::Image beauty;       // PhysicalBeauty AOV
    rr::image::Image albedo;       // M17 Albedo AOV
    rr::image::Image normal;       // raw, unit-length, world-space
};

struct DenoiseOutputs {
    rr::image::Image denoised;     // denoised radiance, HDR linear
};

class Denoiser {
public:
    bool init(int width, int height);
    DenoiseOutputs run(const DenoiseInputs& inputs);
    void destroy();
};

}
```

The renderer's progressive loop owns one `Denoiser`
instance per (width, height); on resize, `destroy` +
`init` rebuild. The `OptixBackend` lifecycle (M15)
provides the underlying `OptixDeviceContext`.

Integration with the existing `--render` and `--serve`
paths is gated behind a config flag (e.g.
`Config::denoise = true`) so callers opt in. The
default-off / opt-in policy lets the denoised output
ship alongside the non-denoised one without breaking
existing renders / tests.

## 8. Open questions

The following decisions deserve resolution before
implementation begins; each gets a one-line entry here
so the impl slice has a checklist rather than rediscover:

- **Normal-space convention.** Section 5.3 picks
  world-space; confirm this matches OptiX's expectation
  on the deployment target (some OptiX denoiser variants
  prefer camera-space; SDK docs vary by release).
- **Albedo at miss.** v1 says "use resolved sky /
  environment colour"; pin whether that's the
  flat sky gradient or the M12 Environment light value.
- **Tile mode.** OptiX denoiser supports tiled invocation
  for memory savings. v1 starts non-tiled; pick a
  resolution above which tiling kicks in (1080p? 4K?).
- **Float precision.** OptiX denoiser supports half-float
  inputs (memory savings); v1 stays at fp32 for
  consistency with the renderer's Rgba32F storage. Worth
  revisiting if memory pressure surfaces.
- **OIDN alternative.** Roadmap M22 mentions OIDN as a
  peer. Decide whether v1 ships an OIDN wrapper at the
  same time or follows in a later slice. Current bias:
  OptiX-only for v1; OIDN once cross-vendor support
  matters.
- **Relativity-aware kernel for the apply step.**
  Section 5.4's reapply (Doppler + searchlight after
  denoising) is itself a kernel. Pick whether it shares
  the existing `applyDopplerColor` /
  `searchlightFactor` device helpers from
  `relativity/RelativityMath.cuh` (yes - they are
  already RR_HD inline) or gets its own copy.

## 9. Out of scope for v1

Listed for clarity so the v1 implementation slice does
not creep:

- **Temporal denoising.** Requires motion vectors plus
  cross-frame state. Animation pipeline is itself a
  future concern; the temporal denoiser variant lands
  alongside.
- **Upscale denoiser.** 2x output resolution requires UI
  paths the renderer does not have yet. v1 outputs at
  input resolution.
- **OIDN parity.** Mentioned as future; not in the v1
  delivery.
- **Adaptive sampling driven by denoiser confidence.**
  Some denoiser variants emit a per-pixel confidence
  buffer; v1 ignores it.
- **Multi-light denoising.** Some advanced denoisers
  treat direct / indirect / specular as separate
  buffers and denoise per-light. v1 denoises beauty as
  one buffer.
- **Volume / SSS denoising.** The renderer has no volume
  pipeline (per the material-graph spec section 11);
  volume-aware denoising is a parallel future concern.
- **Progressive-stream protocol on the server.** v1 of
  the denoiser ships behind the existing single-frame
  `render` command; the streaming protocol is a future
  M18 extension.
- **Spectral denoising.** The renderer is RGB-only; a
  future spectral pipeline would need a denoiser that
  understands spectra. Not in v1.

The above keep the v1 deliverable tight: an OptiX-AOV
denoiser invocation on a fully-rendered frame, with
beauty / albedo / normal inputs sourced from the
existing M17 AOV foundation plus one new
`PhysicalBeauty` AOV that captures the integrator's
pre-relativity radiance, and a progressive workflow
that emits final frames after a configurable sample
count.

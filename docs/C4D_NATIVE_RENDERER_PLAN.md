# Cinema 4D Native Renderer — Integration Plan

Status: **specification only**, introduction slice. No native
plugin code exists yet. This document is the contract that
future implementation slices will deliver against; the rest
of the spec (Cinema 4D registration paths, framebuffer
integration, scene translation, live update, SDK version
constraints) lands as separate doc slices before any code is
written.

Module reference: `bridges/c4d_native/` (module 21 in
`docs/MODULE_MAP.md`, milestone M23 in
`docs/MILESTONE_ROADMAP.md`).

## 1. Purpose

A **native Cinema 4D renderer integration** registers
RelativityRender as a renderer Cinema 4D selects from its
own Render Settings, on the same level as Standard / Physical
/ any third-party renderer. The user picks
`RelativityRender` from the dropdown; pressing **Render**
in C4D's UI drives our path tracer and the rendered frames
appear in C4D's Picture Viewer. No external server, no
external file. The integration is in-process: a C++ plugin
loaded by Cinema 4D, calling RelativityRender's renderer
through its public façade.

This document explains:

- What "native integration" means in Cinema 4D's plugin
  architecture, in conceptual terms (the technical
  surface of `VideoPostData` / scene-renderer
  registration is a future slice).
- Why RelativityRender should eventually grow this path
  even though the Python bridge already works.
- How the native integration differs from the Python
  bridge the project ships today (M19), and how the two
  paths coexist rather than replace each other.
- The high-level goals the native path solves for, in a
  form an implementation slice can refer back to.

It deliberately stops there. Plugin id allocation,
`VideoPost` vs renderer-plugin mechanics, framebuffer
copy-out paths, scene-translation contracts, live-update
event handling, and the SDK version constraints each get
their own document slice with the same level of detail.
No implementation is committed to in this document.

## 2. What a native Cinema 4D renderer integration is

In Cinema 4D's plugin model, a "native renderer" is a
plugin that registers itself with the application's
renderer registry. From the artist's point of view it
behaves as a peer of the renderers Cinema 4D ships with:

- It appears in the **Render Settings -> Renderer**
  dropdown.
- The standard **Render** / **Render in Picture Viewer**
  / **Render Region** commands drive it.
- Frames it produces land in **C4D's Picture Viewer**
  (the standard pictureviewer surface). Multi-pass
  output - beauty / albedo / normal / etc. - lands in
  the picture viewer's pass selector.
- It reads the document's camera / lights / materials /
  geometry directly from `BaseDocument`. There is no
  separate "Export" step the user has to invoke.
- It honours the **Render Settings** dialog's resolution
  / output path / pass selection without a parallel
  configuration UI.

In implementation terms - which the next doc slice covers
in full - this is a C++ plugin, loaded by Cinema 4D at
startup, that registers an entry point conforming to
Cinema 4D's renderer API. The plugin runs in C4D's process
and is effectively a wrapper around the existing
RelativityRender renderer: it translates the live document
state into RelativityRender's scene representation, drives
the path tracer, and pushes results back into a Cinema 4D
`BaseBitmap`. The C4D SDK is the only place this plugin
links Cinema 4D code; per the project's dependency rules
(`docs/MODULE_MAP.md`), nothing outside this directory
knows that Cinema 4D exists.

The native renderer differs from the *bridge* (M19) the
project already ships in two structural ways: it lives
**inside** Cinema 4D's process, and it talks to
RelativityRender's renderer through the renderer's
**public façade** (the `Path Tracer public API` plus the
Scene File Format) rather than through the renderer
**server** that runs in a separate process. Section 4
expands on the comparison.

## 3. Why RelativityRender needs one

The Python bridge already lets a Cinema 4D user export a
scene, send it to the running RelativityRender server,
and pull a rendered image back. That path works
end-to-end (M19's six implementation slices), and it is
the right path for the foreseeable future. The native
renderer does not replace it; it covers cases the bridge
inherently cannot.

Three pressures justify the eventual native integration:

- **No external process to manage.** The bridge requires
  the user to run `RelativityRender --serve` alongside
  Cinema 4D. That is a perfectly reasonable workflow for
  a developer and an acceptable workflow for a power
  user, but it is friction for the median artist who just
  wants to render. A native renderer plugin is a single
  install: copy the plugin into the C4D plugins folder,
  Cinema 4D loads it on next start, and the renderer
  appears in the dropdown.
- **First-class integration with C4D's render pipeline.**
  Render queue, takes, render-region, picture-viewer
  multi-pass, network rendering with Team Render, render
  presets - all of these are concepts Cinema 4D owns. A
  native renderer participates in them by definition: the
  plugin is the renderer Cinema 4D drives, so every
  feature C4D's pipeline supports works as soon as the
  plugin honours the corresponding API calls. The
  bridge cannot reach those concepts; it can only see
  the document state at the moment the user clicks
  **Send Scene**.
- **Live, in-viewport feedback.** The bridge today shows
  the rendered image in its own dialog (M19 ext 6's
  preview panel). That works for a one-shot render but
  is not how Cinema 4D's interactive preview surfaces
  work. The native path can drive Cinema 4D's
  in-viewport interactive preview (the IRR / interactive
  render region surface, depending on SDK version) so
  that scrubbing a slider in the C4D UI updates the
  preview immediately - the same UX users expect from
  Octane / Redshift / V-Ray / any other in-process
  renderer.

The native integration is the milestone that makes
RelativityRender feel like a Cinema 4D renderer rather
than a separate application a Cinema 4D user happens to
talk to. Until it lands, the bridge remains the right
shipping vehicle; once it lands, the bridge stays for
remote / headless / multi-machine workflows.

## 4. Python bridge vs native C++ integration

| Concern              | Python bridge (M19, today)                                  | Native renderer (M23, future)                                   |
|----------------------|-------------------------------------------------------------|------------------------------------------------------------------|
| Plugin language      | Python                                                      | C++ (Cinema 4D's native plugin language)                         |
| Process model        | C4D + a separate `RelativityRender --serve` process         | A single C4D process that loads the plugin                       |
| Wire to renderer     | Renderer server protocol (M18) over TCP                     | Renderer's public C++ façade + Scene File Format                |
| Scene movement       | Bridge writes a `.rrscene` file to disk; server loads it    | Plugin builds a `Scene` in memory; renderer consumes it directly |
| Result delivery      | Server saves PPM; bridge reads it from disk; dialog displays| Renderer fills a Cinema 4D `BaseBitmap`; C4D shows it natively   |
| Where the user picks | Plugins menu commands (`Ping Server` / `Send Scene` / ...)  | Render Settings dropdown - same place every other renderer is    |
| Live update          | Manual: click Render again                                  | Driven by C4D scene-change events                                |
| Install              | One Python plugin folder + run a separate executable        | One C++ plugin file in C4D's plugins folder                      |
| Portability ceiling  | Any Cinema 4D version supporting the Python plugin API     | Constrained by the C++ SDK version of Cinema 4D the plugin built against |
| Failure mode         | Server unreachable -> plugin reports error in a dialog      | Plugin fails to load -> Cinema 4D logs it; renderer absent       |
| Use cases it owns    | Remote rendering, headless / batch, multi-machine farms     | Interactive in-viewport preview, render queue, takes             |

The two paths are **complementary**, not exclusive. A
mature RelativityRender ships both:

- The native plugin runs the in-process interactive case.
- The bridge runs the out-of-process case (a render farm
  worker, a remote box on the LAN, the developer-tools
  use case).

Both paths share the same scene representation (the
Scene File Format) and the same renderer (the public
C++ façade). The renderer itself does not know which
path is driving it - that's the entire point of the
public-façade rule documented in
`docs/MODULE_MAP.md`'s cross-cutting rules. Maintaining
two paths therefore costs only the per-path glue, not
two parallel renderer codebases.

## 5. Goals

The native integration's top-level goals - the contract
the future implementation slices deliver against - are:

- **Live rendering inside Cinema 4D.** The user picks
  `RelativityRender` from the Render Settings dropdown
  and presses Render. Frames appear in the Cinema 4D
  Picture Viewer. Interactive in-viewport preview
  (IRR / "render region") updates as the user scrubs.
  No separate window, no separate file, no separate
  process to start.
- **Minimal friction for artists.** Install the plugin
  once. From that point on, RelativityRender is just
  another renderer in the dropdown. The artist needs
  zero understanding of `.rrscene` files, sockets, the
  renderer server, or our project's layered build. The
  C4D-native UX they already know is the only UX they
  need to learn.
- **Reuse the existing RelativityRender GPU backend.**
  The native plugin does NOT reimplement the path
  tracer, the relativistic camera model, the material
  graph, the AOV pipeline, or the GPU upload paths. It
  drives the SAME renderer the standalone executable
  drives - same kernels, same OptiX path (M15+), same
  AOVs, same denoiser (M22+), same relativity model.
  The plugin is a translator on the C4D side and an
  invoker on the renderer side; everything in between
  is the renderer that already ships.

These three goals shape every implementation decision in
later slices. A choice that violates "minimal friction"
or "reuse the GPU backend" is reconsidered before it
lands in code.

## 6. Cinema 4D plugin registration paths

This section pins HOW a Cinema 4D plugin gets to be the
thing the artist's **Render** click drives. It stays at
the conceptual level - which API to register against,
which lifecycle callbacks frame the rendering, where the
plugin slots into C4D's pipeline. Per the prompt the
framebuffer mechanics and scene-translation contract are
NOT pinned here; they get their own slices once this
mechanism is settled.

### 6.1 Cinema 4D's plugin model

A C++ plugin in Cinema 4D is a shared library (`.xdl64`
on Windows / `.dylib` on macOS) that lives under the C4D
plugins directory. Cinema 4D enumerates that directory at
startup, loads each plugin, and calls a well-known entry
point (`PluginStart()`) where the plugin registers itself
with one or more of C4D's plugin registries.

The SDK ships a small set of plugin "types", each with
its own register call (`RegisterCommandPlugin`,
`RegisterObjectPlugin`, `RegisterTagPlugin`,
`RegisterMaterialPlugin`, `RegisterVideopostPlugin`, ...).
Each registration carries:

- a globally-unique 32-bit **plugin id** (allocated via
  Maxon's PluginCafe registry; the project already uses
  placeholder ids for the M19 bridge and will allocate
  real ids before any public release);
- a **display name** the artist sees in C4D's UI;
- a **flags / info** word telling C4D what the plugin
  can do;
- an **allocator** function that returns an instance of
  the plugin's class (Cinema 4D owns the lifetime;
  the plugin yields heap-allocated objects to it).

The renderer-replacement role is owned by the
**`VideoPostData`** type (section 6.2). All other plugin
types are about adding capabilities to C4D rather than
taking over rendering, so they are not relevant here.

### 6.2 The `VideoPostData` plugin type

Cinema 4D's `VideoPostData` is the C++ base class for
plugins that participate in the render pipeline. It is
the API every renderer the C4D ecosystem ships against
- the bundled Standard / Physical renderers, every
third-party renderer, every post-effect like depth-of-
field or vignette - hangs off. The base class has been
in the SDK long enough that targetting it gives the
plugin the broadest forward / backward compatibility
across SDK releases (section 9 - SDK version
constraints, separate slice).

A `VideoPostData` plugin is registered with
`RegisterVideopostPlugin(...)`, with arguments that
include:

- the unique plugin id;
- the display name shown in C4D's UI;
- an **info** flags word (declares what the plugin
  produces - multi-pass output / depth output /
  motion-vector output / etc.);
- an **allocator** for the plugin's class;
- a **priority** constant (section 6.4) that decides
  WHERE in C4D's render pipeline the plugin runs.

Once registered, C4D considers the plugin a candidate
for the render pipeline. Whether it actually drives
rendering depends on the priority constant and on the
**Render Settings -> Renderer** dropdown (section 6.5).

`VideoPostData` exposes a small lifecycle API: an
`Init` / `Execute` / `Free` triple, plus a handful of
capability-query callbacks Cinema 4D consults to learn
what the plugin can produce. The Execute callback is
where rendering actually runs (section 6.6); Init does
per-render preparation; Free cleans up. None of those
callbacks expose a CPU pixel loop the plugin has to fill
- the plugin's job is to coordinate, and the rendering
itself is whatever the plugin's implementation chooses
(in our case, the existing RelativityRender public
façade running on the GPU).

### 6.3 Alternative: a "renderer plugin" API

Maxon has explored more direct renderer-registration
APIs across recent SDK generations - the names and
shapes differ release to release, but the family
includes:

- A `Maxon::Renderer` interface (newer, registry-style)
  exposed by some R20+ SDKs.
- Direct registration via `c4d::renderer::*` functions
  in some SDK releases.
- The classic `VideoPostData` route this plan picks.

The advantages of those direct routes are real - cleaner
type contracts, fewer historical edge cases. The
disadvantages are also real:

- The shape changes between SDK releases more than the
  `VideoPostData` API does.
- Documentation and examples are thinner; community-
  published plugins still overwhelmingly use
  `VideoPostData`.
- Forward compatibility is less proven; if Maxon
  reshuffles the renderer-plugin surface, the plugin
  needs to track the change, which costs maintenance
  time.

For v1, the plan picks **`VideoPostData`**. It is the
most portable, most documented, most-vetted-by-real-
plugins path. A future spec slice can re-examine the
direct renderer-plugin API once the v1 plugin is
stable on a chosen SDK version and once Maxon's
renderer-API roadmap settles.

### 6.4 Render-pipeline hooks: priority constants

Cinema 4D's render pipeline is divided into stages. A
`VideoPostData` plugin's **priority** declares which
stage the plugin runs at. The SDK exposes a small set
of priority constants whose names map to pipeline
phases:

| Priority class                  | When it fires                         | Typical plugin role                  |
|---------------------------------|---------------------------------------|--------------------------------------|
| Pre-render                      | Before geometry / shading work begins | Scene-prep effects.                  |
| **Renderer-replacement**        | Takes over rendering entirely         | Third-party renderers (our slot).    |
| Light-stage                     | During lighting calculation           | Custom lights / illumination passes. |
| Post-effects                    | After the renderer produces a frame   | Glow / DoF / vignette / film grain.  |

The exact constant names (`VPPRIORITY_*`-style symbols)
are SDK-release dependent and pinned in the impl slice.
What matters at the plan level is the **role**: a
plugin that wants to BE the renderer registers at the
**renderer-replacement** priority. C4D understands
"this plugin is producing the entire output frame", and
the artist's **Render** click dispatches to it. A
plugin at any other priority is part of the pipeline
but does not own it.

RelativityRender is a renderer, not a post-effect. It
registers at the renderer-replacement priority. Its
`Execute` callback is where the GPU path tracer runs.

### 6.5 How Cinema 4D invokes rendering

With the plugin registered at the renderer-replacement
priority, the user-facing flow is:

```
   1. Artist opens Render Settings.
   2. Artist picks "RelativityRender" from the
      Renderer dropdown.
   3. Artist clicks Render (or Render-in-Picture-Viewer,
      or Render-Region, or any other render command).
   4. Cinema 4D's render manager:
        a. consults Render Settings -> Renderer to find
           the active renderer (our plugin id);
        b. allocates the plugin instance via its
           registered allocator;
        c. calls Init(...) with the scene, the resolved
           render settings, and the target framebuffer
           parameters;
        d. calls Execute(...) - rendering happens here;
        e. calls Free(...) and releases the instance.
   5. The frame the plugin filled lands in the
      destination Cinema 4D chose (Picture Viewer,
      Render Queue output slot, etc.).
```

The render manager calls the plugin's callbacks **in
C4D's render thread context**. The plugin can spawn its
own threads / GPU work freely; it just needs to obey
Cinema 4D's progress-reporting and cancellation
contract (a SHOULD-call-back-into-C4D-periodically
rule pinned in the live-update slice). The
RelativityRender renderer is already async-friendly -
it returns from `render_scene` / `render_pathtrace`
when the GPU launches complete - so plugging it in
amounts to forwarding the Execute call to those
functions.

### 6.6 Intercept vs replace

C4D's `VideoPostData` plugins divide cleanly into two
families that do very different things:

- **Post-effect plugins** *intercept* an already-
  rendered frame and modify it. Glow, DoF, film grain,
  vignette - all of these run AFTER a renderer has
  produced pixels. The plugin's input is "here is a
  buffer; modify it"; the plugin's job is per-pixel
  filtering.
- **Renderer-replacement plugins** *replace* rendering
  entirely. The plugin's Execute is where rendering
  begins; nothing produced the input buffer before it,
  and the plugin's output is the entire frame.

The difference is the priority constant (section 6.4)
plus the shape of the Execute call. A renderer-
replacement plugin receives an empty / about-to-be-
filled buffer; a post-effect plugin receives a buffer
with content.

RelativityRender is a renderer-replacement plugin. Its
Execute does NOT post-process Cinema 4D's render output;
it runs the path tracer against the document and fills
the framebuffer with what the path tracer produced.
Cinema 4D's bundled renderers do not run when our
plugin is the active renderer.

### 6.7 Where RelativityRender connects, in one paragraph

The plugin registers a `VideoPostData` subclass at the
renderer-replacement priority. When the artist picks
**RelativityRender** in the Render Settings dropdown
and clicks **Render**, Cinema 4D allocates an instance,
calls **Init** to hand over the scene + render settings,
calls **Execute** to ask for the frame, and calls
**Free** to clean up. The plugin's **Execute** is the
single coordination point where C4D's request crosses
into RelativityRender: it converts the live document
state into our scene representation (separate slice),
calls the existing public renderer façade (the same one
the standalone executable uses), and hands the
resulting framebuffer back to Cinema 4D (separate
slice). Everything else - the path tracer, the
relativistic camera model, the material graph, AOVs,
denoising, OptiX path - is the renderer that already
ships, unchanged.

## 7. Framebuffer integration

This section pins HOW pixels move from the GPU path
tracer's output into the bitmap surface Cinema 4D
expects to display. It stays at the conceptual level -
storage shapes, ownership, the host vs device round
trip, the resolution and progressive contracts - and
defers the multi-pass / AOV channel mapping, the tone-
mapping / view-transform interplay, scene translation,
and live update to their own slices.

### 7.1 What RelativityRender produces today

The renderer's hot path produces pixels on the GPU. The
shape, recap (`docs/MASTER_ARCHITECTURE.md`,
`src/cuda/CudaRenderer.{h,cu}`):

- A device-resident `GpuBuffer<float>` of size
  `width * height * 4` floats. Layout is `Rgba32F`,
  row-major, **channel-interleaved**, **top-left
  origin** - the same layout the M17 AOV foundation
  uses for every AOV slot.
- Per-pixel writes happen inside the path-trace
  kernel; one thread per pixel. The kernel writes
  the four channels (RGBA) directly into its slot in
  the device buffer.
- After the launch, the host calls
  `cudaDeviceSynchronize`, allocates an
  `rr::image::Image` (`Rgba32F`, same layout), and
  downloads the device buffer into it.
- `Image` is the host-side container the rest of the
  project consumes: `Image::save_ppm` writes 8-bit
  PPM, `AOV::save_ppm` writes a normalised grayscale
  PPM for scalar AOVs, the bridge wraps it in BMP for
  the C4D preview dialog.

The native renderer plugin starts from this same
`Image`. The renderer's public façade does not need to
change: the plugin calls `CudaRenderer::render_scene`
or `render_pathtrace` and receives a `Result` whose
`image` field is the same `rr::image::Image` every
other consumer reads.

The M17 AOV foundation (six v1 kinds: Beauty / Normal /
Depth / Albedo / DopplerFactor / SearchlightFactor)
extends the same shape: each AOV is its own
`GpuBuffer<float>` and downloads into its own `Image`.
Mapping AOVs onto C4D's multi-pass surface is the next
slice; this section pins ONLY the beauty / single-
output path.

### 7.2 What Cinema 4D expects

A `VideoPostData` plugin's `Execute` callback (per
section 6.2) receives the target framebuffer Cinema 4D
wants filled. The container is one of:

- **`BaseBitmap`** for the simple case - a single
  beauty buffer, no extra channels. Cinema 4D
  pre-allocates it at the render's resolution and
  hands it to the plugin.
- **`MultipassBitmap`** for the multi-pass case. Each
  pass (RGB / Alpha / Depth / Object Buffer / etc.)
  is a separately-addressable channel. AOVs map
  here.

For both, the SDK exposes per-pixel write APIs the
plugin uses to populate the bitmap. The exact symbol
names vary across SDK releases; what's stable is the
contract:

- Cinema 4D **owns** the bitmap; the plugin writes
  into it but does not allocate / free.
- Cinema 4D **dictates** the bitmap's resolution and
  pixel format. The plugin honours both; it does not
  resize the bitmap or change its format.
- The plugin **MUST** signal completion (or partial
  progress, in progressive mode) so C4D's UI updates.
  The signal mechanism is the same one the
  cancellation contract from section 6.5 uses; the
  details are in the live-update slice.

### 7.3 Mapping the GPU framebuffer to a Cinema 4D bitmap

The plugin's `Execute` runs the equivalent of:

```
   1. Read width / height + format from the C4D bitmap.
   2. Drive RelativityRender's renderer for that size:
        result = CudaRenderer::render_scene(scene, w, h)
        // or render_pathtrace(...) for the progressive case.
   3. Translate result.image -> bitmap pixels.
   4. Notify C4D the bitmap is ready (or partially ready,
      progressive mode).
```

The translation in step 3 is a per-pixel copy. For each
of the bitmap's pixels:

- **Channel order.** Both sides use RGBA (or RGB) in
  the same component order. No swap; channel index 0
  on the renderer side is channel index 0 on the
  bitmap side.
- **Origin.** Both surfaces use **top-left origin**
  with row-major rows. No vertical flip is needed.
  (This matches `rr::image::Image`'s
  documented convention; if a future Cinema 4D SDK
  release exposes a different origin convention, the
  plugin's translation step is the only place that
  knows about it.)
- **Float-to-X conversion.** RelativityRender writes
  HDR linear `float32`. Cinema 4D's bitmap may be
  8-bit-per-channel, 16-bit, or 32-bit float
  depending on the artist's render-settings choice.
  The plugin converts in step 3:
  - 8-bit: clamp `[0, 1]`, scale by 255, round to
    `uint8`. Same logic `Image::save_ppm` already
    uses; the v1 plugin will share the same helper.
  - 16-bit / 32-bit float: identity copy (or a
    half-conversion for the 16-bit case).
- **Alpha.** RelativityRender writes alpha as `1.0`
  for hit pixels and `1.0` for miss pixels (the
  current kernels do not distinguish). The plugin
  forwards it as-is. A future "transparent
  background" feature would land in the renderer
  before the bitmap copy sees it.
- **Buffer ownership.** The host `Image` allocated
  by the renderer's `Result` is short-lived: it lives
  long enough to feed the bitmap and is then
  discarded. No long-term double-buffering.

A future optimisation worth flagging: skipping the
host round trip and copying GPU buffer -> Cinema 4D
bitmap directly. Modern Cinema 4D SDKs occasionally
expose device-friendly bitmap interfaces (CUDA /
OpenGL interop on a per-platform basis); when one is
available + stable, the plugin can `cudaMemcpy` from
its device buffer straight into the bitmap's memory
without touching host RAM. v1 ships the host
round-trip for portability; the optimisation is
opt-in once the rest of the plugin is stable.

### 7.4 Resolution handling

The plugin does **not** decide its own resolution. The
artist sets resolution in C4D's Render Settings; the
render manager passes it through to the plugin via
Init (and the bitmap C4D allocates is sized
accordingly). The plugin's job is:

- **Honour the requested resolution exactly.** If
  C4D asks for 1920 x 1080, the plugin invokes
  `render_scene(scene, 1920, 1080)` - no implicit
  rescale, no border. The renderer's framebuffer is
  reallocated by `CudaRenderer` for the requested
  size on every Execute (today's behaviour; cheap
  enough for v1).
- **Handle render-region.** When C4D's render-region
  feature is on, the active region is a sub-
  rectangle of the bitmap. v1 strategy: render the
  full frame at the requested resolution and copy
  only the in-region pixels into the bitmap. (A
  future optimisation does region-only rendering by
  passing the region to the kernel; that's a
  follow-up because it requires per-region camera
  ray generation.)
- **Handle resolution changes between renders.** The
  artist can change Render Settings between two
  Render clicks. v1 just re-allocates the renderer
  framebuffer per Execute; the renderer's
  `GpuBuffer<float>` already supports
  `allocate(N)` re-sizing without leaks.

The plugin **does not** rescale the renderer's output
to match the bitmap. Both sides agree on dimensions
because the plugin drove the renderer with the
bitmap's dimensions. This invariant is the plugin's
contract; a violation indicates a bug.

### 7.5 Progressive rendering vs final frame

The path tracer (M14) supports per-launch sample
counts via `render_pathtrace(scene, w, h, spp,
max_depth, seed_offset)`. Progressive rendering
becomes a loop over launches:

```
   total_spp = 0
   for each batch in [1, 1, 2, 4, 8, 16, ...]:
       result = render_pathtrace(scene, w, h, batch,
                                 max_depth,
                                 seed_offset = total_spp)
       accumulate(result.image, accum)
       copy_to_bitmap(accum, bitmap)
       notify_c4d(progress = total_spp / total_target)
       total_spp += batch
       if cancelled: break
```

The structure is the same one the
`docs/DENOISING_PLAN.md` section 6 sketched - because
it's the same renderer path. The plugin's progressive
mode runs the same loop and updates the C4D bitmap
between batches.

For **final-frame** rendering, the loop collapses to a
single call: one launch with the full target sample
count, fill the bitmap once, return. v1 picks final-
mode when Cinema 4D dispatches a one-shot Render (the
default path); progressive mode for IRR (interactive
preview, section 7.6).

The differences live in the per-batch knobs, not in
two parallel code paths:

- **Final**: one batch of `spp_target` samples; no
  intermediate bitmap updates; no cancellation poll
  inside the launch (cancellation between launches
  only); denoiser (M22+) runs once on the final
  frame.
- **Progressive**: many small batches; bitmap
  updates after each; cancellation poll between
  batches; denoiser (M22+) runs every batch in
  interactive mode and once at the end in offline
  mode.

The plugin reads which mode Cinema 4D wants from the
`Execute` flags - C4D's IRR / Picture-Viewer / Render
Queue contexts each carry their own hints.

### 7.6 Preview vs final render

Two distinct UX surfaces in C4D drive the plugin's
`Execute`. They share most of the code path but pick
different parameters from Render Settings:

| Surface                | Destination          | Resolution                | Sample target              | Denoise frequency           | Cancellation                       |
|------------------------|----------------------|---------------------------|----------------------------|------------------------------|------------------------------------|
| **IRR / preview**      | C4D viewport overlay | C4D's IRR resolution (often lower) | Low (e.g. 4-32 spp)          | Per progressive batch        | Aggressive (every parameter change)|
| **Picture Viewer**     | Picture Viewer       | Render Settings           | Render Settings              | Per batch (progressive)      | On user request                    |
| **Render Queue / Take**| Output file path     | Render Settings           | Render Settings              | Once on final frame          | Whole render at once               |

All three surfaces invoke the same plugin Execute.
The parameter selection and the bitmap update cadence
differ; the renderer the plugin drives is identical
(the standalone-executable's renderer, unchanged).

A few specifics worth flagging at the plan level:

- **IRR's resolution is dynamic.** As the artist
  resizes the viewport, the IRR resolution changes;
  the plugin re-allocates buffers on each Execute
  request. v1's per-Execute reallocation policy
  (section 7.4) covers this case for free.
- **Render Queue runs unattended.** No user is
  watching for IRR-style feedback; the plugin can
  prefer offline-mode parameters (final-frame
  denoising, fewer bitmap updates).
- **Take rendering reuses the same plugin.** Each
  take is a separate Execute call with the take's
  resolved scene; the plugin does not need
  take-specific code beyond the standard scene
  translation (separate slice).

### 7.7 What this slice does NOT cover

Listed for clarity so the v1 framebuffer-integration
implementation slice does not creep:

- **Multi-pass / AOV channel mapping.** The M17 AOV
  set (Beauty / Normal / Depth / Albedo /
  DopplerFactor / SearchlightFactor) maps onto
  C4D's `MultipassBitmap` slots. That mapping -
  which pass index gets which AOV, how scalar AOVs
  encode into multi-channel slots, what the
  pass-name strings are - is its own slice once the
  v1 single-buffer path is settled.
- **Tone-mapping / view-transform integration.**
  Cinema 4D's color-management pipeline (linear /
  sRGB / OCIO depending on version) overlays the
  renderer's HDR linear output. v1 hands C4D the
  raw linear values and lets C4D's view transforms
  apply; whether the plugin should run its own
  tone-map first is a calibration question for a
  future slice.
- **HDR EXR write-out.** When C4D's render destination
  is an EXR file (HDR), the plugin's float32 output
  is exactly what the file format wants. The on-disk
  format mapping is a follow-up.
- **Tile / scanline progressive granularity.** v1
  updates the entire bitmap after each progressive
  batch. Per-tile updates (which most production
  renderers support for very large frames) is a
  bandwidth optimisation; out of scope for v1.
- **Scene translation and live update.** Out of scope
  per the prompt; both have their own slices.

## 8. Scene translation

This section pins WHAT pieces of the live Cinema 4D
document the plugin reads, and HOW each maps onto
RelativityRender's scene representation. It stays at
the conceptual level: which document elements are
sources, which target types they land in, where the
unsupported set boundaries sit. The byte-level field
shapes are the bridge's M19 extension slices'
contribution; this section pins that the native plugin
**reuses the same mapping** rather than reinventing
it.

### 8.1 What translation does

The plugin walks the active `BaseDocument`, finds the
elements RelativityRender knows how to render, and
populates an `rr::scene::Scene` host container. The
container is the same struct `src/io/SceneLoader.cpp`
produces today from `.rrscene` files; nothing about its
shape changes. The plugin then calls the renderer's
public façade with the scene - the existing
`GpuScene::upload_from(scene)` path on the renderer
side does the GPU work unchanged.

Three structural decisions follow:

- The plugin does NOT write to the internal
  `rr::scene::Scene` directly across renderer-internal
  layers - per the module map's forbidden imports rule
  (`docs/MODULE_MAP.md` module 21), it touches only the
  renderer's public types (the scene container is
  public, the renderer entry points are public; the
  GPU upload + kernel launch internals are not).
- The plugin does NOT round-trip through `.rrscene`
  (no JSON write, no JSON read). That round-trip is
  the bridge's mechanism for the cross-process case;
  the native plugin lives in-process and benefits
  from skipping the serialisation cost (section 8.8).
- The plugin's translator is a pure function of
  document state. Same document -> same scene; no
  hidden state, no caches that survive a render, no
  cross-render state pollution.

### 8.2 Camera

Cinema 4D exposes the active rendering camera through
its document API. The translator maps:

| C4D source                                  | rrscene target                                  |
|---------------------------------------------|--------------------------------------------------|
| `CameraObject::GetMg()` (global matrix)     | `camera.position` + `camera.forward` + `camera.up` (after the C4D->renderer Z-flip the bridge already pins) |
| `CAMERAOBJECT_FOV_VERTICAL`                 | `camera.fov` (degrees)                           |
| `RDATA_XRES` / `RDATA_YRES` (render data)   | `render_settings.width` / `.height`              |

These are the same mappings the bridge's M19 ext 1
slice landed on the Python side. The native plugin
reads the same fields through the C++ SDK and applies
the same coordinate-flip rule
(`integrations/c4d/RelativityRenderBridge/rrscene_writer.py`'s
`convert_c4d_camera_basis` is the canonical reference;
the plugin transcribes it to C++).

### 8.3 Geometry (polygon meshes)

Polygon objects map onto rrscene meshes. The native
translator reads the same fields the bridge's M19 ext
2 slice already pinned:

| C4D source                                       | rrscene target                              |
|--------------------------------------------------|----------------------------------------------|
| `PolygonObject::GetAllPoints()`                  | `mesh.vertices[]` (after global-matrix bake + Z-flip) |
| `PolygonObject::GetAllPolygons()` (`CPolygon`s)  | `mesh.triangles[]` (quads triangulated `a-c` diagonal) |
| First `Ttexture` tag's bound material id         | `mesh.material_id` (integer index into `materials[]`) |

Two notes the native path inherits from the bridge:

- **Quads triangulate along the a-c diagonal.** A
  Cinema 4D `CPolygon` with `c == d` is a triangle
  (one output); otherwise a quad (two outputs:
  `(a, b, c)` + `(a, c, d)`). Degenerate triangles
  are pruned. Same `triangulate_cpolygon` rule the
  bridge uses.
- **The global matrix is baked into vertex
  positions.** v1 does not write a separate per-mesh
  transform; vertices are already in world space.
  This matches the bridge's choice and keeps the
  scene representation self-contained.

### 8.4 Materials

Materials are the most version / type sensitive part
of the translation. v1 inherits the bridge's mapping
table (M19 ext 3):

| C4D source                                                            | rrscene target                                                        |
|-----------------------------------------------------------------------|-----------------------------------------------------------------------|
| Standard `Mmaterial`, color channel                                   | `material.base_color` from `MATERIAL_COLOR_COLOR * MATERIAL_COLOR_BRIGHTNESS` |
| Standard `Mmaterial`, luminance channel (when on)                     | `material.emission_color` + `.emission_strength`                      |
| Object's `ID_BASEOBJECT_USECOLOR` mode 2 / 3 (Always / Layer)         | "Viewport: r, g, b" stub material; `mesh.material_id` references it.  |
| Standard `Mmaterial` with bitmap shader in color channel (future)     | A `TextureSample` graph node feeds `Diffuse.albedo` (M21 graph path). |

Material-graph integration is where the native plugin
gains over the bridge: in-process, it can construct a
v1 graph (`material::graph::Graph`) directly and hand
it to the renderer's compile path
(`compile_graph_to_gpu_material`), so the kernel
shades through the graph from the very first render.
The bridge's path goes through the .rrscene's flat
material section + the auto-synthesise step on
upload; the native path reaches the same destination
without that round trip.

### 8.5 Lights

Cinema 4D lights map onto the v1 rrscene types per the
bridge's M19 ext 3 mapping:

| C4D source                                  | rrscene target                                |
|---------------------------------------------|------------------------------------------------|
| `LIGHT_TYPE_OMNI`                           | `light.type = "point"`, position from `mg.off` |
| `LIGHT_TYPE_DISTANT` / `LIGHT_TYPE_PARALLEL`| `light.type = "directional"`, direction from `mg.v3` |
| `LIGHT_TYPE_AREA` / `LIGHT_TYPE_TUBE`       | Degraded to `"point"` at the area's origin (lossy; warn) |
| `LIGHT_TYPE_SPOT` / `LIGHT_TYPE_PARSPOT`    | Skipped (no spot cone in v1; warn)             |

`LIGHT_COLOR` -> `light.color`; `LIGHT_BRIGHTNESS` ->
`light.intensity`. Same non-negative clamps as the
bridge.

### 8.6 Handling unsupported features

Cinema 4D's scene model is large; RelativityRender's
v1 covers a small subset. The translator's job on
features outside that subset is to **degrade
gracefully and warn**, not to fail.

The bridge already pins the ignore-and-warn taxonomy
(M19 ext 2 + ext 3); the native renderer inherits it
and adds two cases unique to its in-process role:

| C4D feature                            | v1 native handling                                              |
|----------------------------------------|------------------------------------------------------------------|
| Generators (`OBJECT_GENERATOR` flag)   | Skipped. (Future: bake via `GetCache()`, same as bridge.)        |
| Deformers (`OBJECT_MODIFIER` flag)     | Skipped as standalone objects. Polygon objects whose subtrees contain deformers export the **undeformed** mesh and warn. (No `GetDeformCache`.) |
| Volume objects (Ovolume*)              | Skipped + warn. v1 has no volume pipeline.                       |
| Hair (`Ohair`)                         | Skipped + warn.                                                  |
| Cinema 4D node-based materials         | Falls back to flat material; warn. Future slice translates the C4D node tree into a v1 `material::graph::Graph`. |
| Tracer / Field / MoGraph procedurals   | Skipped + warn. Static geometry only in v1.                      |
| Take system (multiple takes)           | Each take is a separate Execute call with the take's resolved scene; translator does not need take-specific code. |
| Animation timeline (per-frame motion)  | Each frame is a separate Execute call. Motion blur not in v1.    |

The "warn" channel is Cinema 4D's standard one - the
plugin emits messages through C4D's logging facility
so the artist sees them in the C4D console / render
log. The exact API call is an SDK-version detail
deferred to the SDK-constraints slice.

### 8.7 Reuse of the bridge's mapping

The bridge (`integrations/c4d/RelativityRenderBridge/`)
has shipped six implementation slices that landed and
verified the same translation rules listed above.
Reusing them in C++ is a structural design choice the
native plan commits to:

- **Same mapping table.** Every entry in sections
  8.2-8.6 is the bridge's table, transcribed. A
  difference between the two is a bug, not a design
  choice.
- **Same warning taxonomy.** The bridge's dialog
  surfaces `(generator)` / `(deformer)` / `(volume)`
  / `(hair)` / `(unsupported)` reasons; the native
  plugin emits the same labels through C4D's
  logging.
- **Same ignore-set.** The bridge's classification
  in `_classify` is the contract; the native
  plugin's C++ classifier produces identical
  decisions.

This reuse is a maintenance lever: when a feature is
added to one path, it lands in the other through the
same translation logic, not through divergent
rewrites.

### 8.8 In-memory vs file-based path

The bridge writes a `.rrscene` JSON file and asks the
renderer server to load it (M18 protocol). The native
plugin **skips the file altogether**: it builds an
`rr::scene::Scene` in memory and hands it to
`GpuScene::upload_from(scene)` directly. Two
consequences:

- **No serialisation cost.** Building the host scene
  in memory is one allocation per node + one vector
  copy of vertex / index data. JSON encode + decode
  + filesystem IO is several orders of magnitude
  more work for the same logical data.
- **Less robust on cross-machine deployment.** The
  native plugin works only when the renderer is on
  the same machine (no network required). The
  bridge's file-based path is necessary for the
  out-of-process case (separate machine, different
  filesystem). The two paths are deliberately
  different to play to their respective strengths
  (section 4's "complementary, not exclusive" pin).

The plugin's preference for the in-memory path is the
default; an opt-in "write a `.rrscene` for inspection"
diagnostic mode is a future slice for debugging.

## 9. Live update strategy

A native renderer earns its keep by reflecting scene
edits in the rendered preview without forcing the
artist to click Render twice. This section pins the
detection mechanism (how the plugin learns the
document changed) and the application strategy (what
it does with the change).

### 9.1 What changes between renders

Three categories of edits the artist makes during a
session:

- **Camera moves.** Tumble / pan / zoom in the C4D
  viewport. Camera matrix changes; everything else
  is identical.
- **Object moves.** Drag a sphere in the viewport.
  Object's global matrix changes; vertex data and
  topology unchanged.
- **Topology / material edits.** Add / delete an
  object, change a material colour, edit polygon
  geometry. Vertex data, material parameters,
  geometry topology change.

Plus three implicit edits the plugin handles for free:

- **Render Settings changes** (resolution, sample
  count). Plugin's Init carries the new settings on
  the next Render.
- **Take switch.** New take = new resolved scene =
  new Execute call.
- **Animation frame change.** Per-frame Execute call
  with the resolved scene at that frame.

### 9.2 Detecting C4D scene changes

Cinema 4D's plugin SDK exposes change-event hooks the
plugin subscribes to. The exact hook the v1 native
plugin picks depends on the SDK version, but the
shape is consistent across releases. The candidates:

- **Document-level change events.** Cinema 4D
  dispatches a coarse-grained "document changed"
  message; the plugin's hook receives it and
  triggers a re-render. Coarse but reliable; the v1
  default.
- **Per-object change events** (`MSG_UPDATE`-family).
  Object's `Message()` callback fires with the
  details of what changed. Finer-grained; lets the
  plugin distinguish camera-only changes from
  topology changes. Useful for the partial-update
  optimisation in 9.3.
- **`SceneHook`-type plugin** (a separate plugin
  type living alongside the VideoPostData renderer).
  Receives every document execute / draw call.
  Heavier but gives the plugin a guaranteed hook on
  every change.

For v1: the plugin subscribes to the **document-level
change event** and re-renders on every signal. The
finer hooks (per-object messages, scene hooks) are
follow-up optimisations that buy partial-update
performance once v1 measures show full re-upload is
the bottleneck.

The plugin's render loop, conceptually:

```
   on_document_changed:
       cancel_in_flight_render()      // if any
       reset_sample_accumulation()
       schedule_render(immediately)
```

Cancellation is the same mechanism section 6.5
flagged for the user-cancellation case; the plugin's
Execute polls the cancellation flag between
progressive batches.

### 9.3 Application: full vs partial re-upload

Once the plugin learns the document changed, it has
to update the renderer's scene state. Two strategies:

- **Full re-upload.** Re-translate the entire
  document + call `GpuScene::upload_from(scene)`
  again. Simple, robust, correct by construction.
  Cost is proportional to scene size: cheap on
  small scenes, expensive on heavy ones.
- **Dirty-tracking partial re-upload.** Track which
  document elements changed since the last render
  and re-translate / re-upload only those. The GPU
  scene supports per-section uploads
  (`upload_camera`, `upload_materials`,
  `upload_textures`, etc.) so partial updates are
  reachable through the existing public façade.
  Adds significant plugin-side bookkeeping;
  payoff scales with scene size.

For v1 the plugin picks **full re-upload**. The
choice is justified two ways:

- **Correctness first.** A full re-upload cannot get
  out of sync with the document; a partial upload
  can. Bugs in dirty-tracking surface as render
  artefacts that look like real renderer bugs.
  v1 ships the simple path so the plugin's
  correctness is auditable from the document state
  alone.
- **Most edits are cheap to re-translate.** Cinema
  4D scenes the v1 plugin targets fit in tens of
  thousands of vertices and a handful of materials;
  re-translating that in C++ is well under a
  millisecond. The expensive part is the GPU
  re-upload, which the partial path would also pay
  on the changed pieces.

The dirty-tracking path is a future slice once
v1 measurements show the full-upload cost matters.
Pieces of the upload pipeline most likely to benefit:
mesh vertex / index buffers (cheaper to keep when
unchanged), texture pixel buffers (very expensive on
HD textures).

### 9.4 Sample-accumulation invalidation

The path tracer accumulates samples per pixel across
launches (M14's `seed_offset` knob). When the document
changes, the previously-accumulated samples are no
longer valid for the new scene; they describe a
different image.

v1's policy: **reset accumulation on every document
change**. The next render starts at sample 0 with the
new scene. Combined with the M22 denoising plan's
progressive workflow, the artist sees an immediately-
denoised low-spp frame within a fraction of a second
of the edit, with the frame quality climbing as
samples accumulate against the new scene.

A future enhancement is partial invalidation: if
ONLY the camera changed, the path tracer's hit
counts and accumulated radiance per surface point
are still reusable through reprojection. That is
temporal-denoiser territory and out of scope for
v1 (per the M22 plan's section 9).

### 9.5 Performance considerations

Three pressure points the v1 design plays defensively
against:

- **Translator latency.** Every document change runs
  the translator. C++ translation of the bridge's
  Python mapping is roughly an order of magnitude
  faster, so the v1 budget for translation alone is
  comfortable. Heavy mesh translation
  (`GetAllPoints` + `GetAllPolygons` reads) is
  the dominant cost; static-mesh edits are rare in
  an interactive session.
- **GPU re-upload bandwidth.** PCIe upload bandwidth
  caps how fast vertex / texture data can land on
  the GPU. Full re-upload of a heavy scene every
  edit can saturate it. The v1 plugin's mitigation:
  re-upload only when the document actually changed
  (the plugin owns its own dirty flag, set by the
  change-event hook). The dirty-tracking partial
  re-upload (9.3) is the fix once measurements
  warrant.
- **Render-loop responsiveness.** The plugin must
  cancel an in-flight render fast when the document
  changes again before the previous render
  finishes. v1 polls the cancellation flag between
  per-batch progressive launches; the maximum
  responsiveness latency is one batch's render
  time. For interactive previews at low-spp, that
  is well under 100ms on the renderer's target
  hardware.

### 9.6 What this slice does NOT cover

Listed for clarity so the v1 implementation slice
does not creep:

- **Cinema 4D SDK version constraints.** The exact
  message ids, hook types, and message-dispatch
  function names depend on the SDK version. The
  v1 SDK target + the deprecation policy are pinned
  in the SDK-constraints slice.
- **Multi-pass / AOV channel mapping.** Out of scope
  per section 7.7; the AOV mapping slice covers it.
- **Per-pixel motion vectors / temporal denoising.**
  Out of scope per the M22 denoising plan section 9.
- **Network / Team Render integration.** Out of
  scope per section 9 of the intro.
- **Animation timeline integration beyond per-frame
  Execute calls.** Per-frame state scrubbing works
  at v1; motion blur and per-frame motion vectors
  are future.

## 10. SDK and platform constraints

This section pins the constraints the Cinema 4D SDK,
the supported operating systems, and the renderer's
GPU dependency impose on the native plugin. The exact
SDK version and toolchain targets are an
implementation-slice decision; this slice pins the
shape of the constraints + the policy v1 takes.

### 10.1 Cinema 4D SDK version compatibility

Cinema 4D's C++ SDK has changed significantly across
releases. The major boundaries the plugin author has
to navigate (without naming exact version numbers,
since they vary by Maxon's release cadence):

- **Renderer-API generation.** Different SDK
  generations expose different renderer-registration
  surfaces. Older SDKs lean on `VideoPostData`
  (section 6.2's v1 target); newer SDKs add direct
  renderer-plugin APIs (section 6.3). The plugin
  source compiled against one generation does NOT
  compile cleanly against another; the renderer-API
  helper namespace, the message ids, and the entry-
  point macros all shift.
- **Multi-pass / framebuffer surface generation.**
  `BaseBitmap` is stable; `MultipassBitmap` and the
  pass-naming conventions evolve. Plugin code
  touching the framebuffer write path is the most
  exposed to SDK drift.
- **Scene API generation.** `BaseDocument`,
  `BaseObject`, `BaseMaterial`, light-type
  constants - the names are stable but the field
  ids, the message constants, and the helper
  functions are not. The translator (section 8) is
  exposed at every per-element mapping.

Maxon's deprecation cycle is approximate but
predictable: APIs marked deprecated in one release
are typically removed two releases later. A plugin
that compiles cleanly against the current SDK can
still break on an older SDK (missing API) or a newer
SDK (deprecated API removed).

**v1 picks ONE SDK version target.** That decision
is the SDK-target slice's job, not this one. The
constraint here is: every API the plugin touches MUST
be available + stable on that target. Bumps to a
newer SDK are explicit follow-up slices; they're
expected to require code changes.

### 10.2 Plugin compilation per SDK release

Cinema 4D plugins are platform-specific shared
libraries (`.xdl64` on Windows, `.dylib` on macOS).
Building them ties three things together that an
ordinary CMake project would not have to think
about:

- **The C4D SDK headers + framework.** Each SDK
  release ships its own header tree under a
  Maxon-supplied directory; the plugin's include
  path points there. The headers reference internal
  layout that the SDK's macros + helpers wire up.
  Different SDKs cannot share an include tree.
- **The C4D SDK's required compiler.** Maxon
  certifies specific compiler versions per SDK
  release (a Visual Studio version on Windows, an
  Xcode / clang version on macOS). Building with an
  uncertified compiler may work, may produce
  binary-incompatible plugins, may fail to link.
- **License terms.** Maxon's SDK license includes
  redistribution / NDA clauses the plugin's
  packaging must respect. v1's plugin distribution
  starts as hand-installed (drop into the user's
  plugins folder); marketplace / Plugin Cafe
  distribution is a future slice with its own
  packaging requirements.

Continuous-integration consequences:

- Running CI against multiple SDK versions = N
  parallel build pipelines, one per
  (SDK, compiler, platform) tuple. v1 starts with
  one tuple; expanding the matrix is a
  maintenance-cost decision.
- The renderer's existing `host-only` test paths
  (the standalone build that does NOT need CUDA or
  C4D) keep working unchanged. The C4D plugin
  builds are ADDITIONAL pipelines, not replacements.

### 10.3 Platform constraints (Windows / macOS)

Cinema 4D officially supports Windows and macOS.
Linux support varies by SDK release and Maxon's
official position; the v1 plugin does not target
Linux.

| Platform        | Architectures             | Toolchain expectation         | GPU compatibility         |
|-----------------|---------------------------|-------------------------------|---------------------------|
| **Windows**     | x86_64                    | Visual Studio (per SDK cert)  | NVIDIA + CUDA available   |
| **macOS Intel** | x86_64                    | Xcode (per SDK cert)          | Older NVIDIA / unavailable|
| **macOS ARM**   | arm64 (Apple Silicon)     | Xcode (per SDK cert)          | **No CUDA support**       |

**Apple Silicon is the conspicuous gap.** The
RelativityRender renderer is CUDA / OptiX-first
(`docs/MASTER_ARCHITECTURE.md`); CUDA does not run on
ARM Macs. The plugin's load-time check on Apple
Silicon must:

- detect that no compatible GPU is reachable,
- log a clear, single-line message in C4D's render
  log explaining the absence,
- gracefully decline to render rather than crash or
  produce a frame of garbage.

A future slice can revisit the Apple Silicon path -
either by gaining a non-CUDA GPU backend (Metal
Performance Shaders, MPS-based path tracer) or by
shipping the plugin with a clear "Intel macOS only"
constraint until that backend exists. v1 ships
Windows-only; the macOS Intel build is a follow-up;
the Apple Silicon build needs the alternative
backend the renderer does not have.

### 10.4 GPU / driver dependencies

The renderer's hard requirements at runtime:

- **NVIDIA GPU.** OptiX 7.x (the M15 plan's target)
  requires Pascal-generation hardware or newer
  (compute capability 6.0+); CUDA-only paths run
  further back but the GPU has to be NVIDIA.
- **CUDA Toolkit at compile time.** The renderer's
  CUDA-enabled build pulls headers and the Toolkit
  runtime stubs; the plugin's build inherits this.
  The Toolkit version is pinned per the project's
  build settings; a mismatch with the runtime
  driver version produces failures the plugin
  surface needs to translate into something an
  artist can act on.
- **Matching NVIDIA driver at runtime.** The driver
  on the artist's machine must be compatible with
  the CUDA Toolkit version the plugin shipped
  with. Mismatches produce CUDA load failures the
  plugin reports through C4D's render log.
- **OptiX SDK (when M15 lands).** OptiX requires
  its own SDK at build time and a matching driver
  at runtime. The plugin's load path needs to
  validate both before declaring readiness.

Plugin behaviour on unsupported hardware /
mismatched drivers, in priority order:

1. Detect the failure as early as possible
   (preferably plugin load).
2. Log a clear, single-line message identifying
   the root cause (no GPU, driver too old,
   incompatible CUDA Toolkit, missing OptiX
   runtime).
3. Decline to render. The renderer dropdown still
   shows `RelativityRender`; choosing it produces
   the error message rather than a hang or a
   crash.

The renderer's existing CUDA-detection helper
(`rr::gpu::enumerate_devices`,
`gpu_backend_available`) is the building block; the
plugin's load-time check wraps it.

## 11. Limitations

This section is the v1 honest-limitations list:
features Cinema 4D supports that the v1 native
renderer plugin will deliberately NOT cover, the
complexity cost of full coverage, and the
performance trade-offs the v1 design accepts.

### 11.1 Unsupported Cinema 4D features

The bridge's M19 extension slices already pin most
of these; the native plugin inherits the same
ignore-set + adds two native-specific entries
(section 8.6). The summary, for v1:

- **Generators** (Cloner / Subdivision / Boole /
  Sweep / Loft / ...). Skipped; future slice can
  bake them through `GetCache()`.
- **Deformers** (Bend / Twist / Bulge / ...).
  Skipped as standalone objects. Polygons whose
  subtree contains a deformer export the
  **undeformed** mesh; the v1 plugin warns rather
  than calling `GetDeformCache`.
- **Spot lights and parallel-spot lights.**
  Skipped + warn (rrscene v1 has no spot cone).
- **Area / tube lights.** Degraded to a point
  light at the area's origin + warn.
- **Volume objects** (Volume / VolumeBuilder /
  VolumeMesher). Skipped + warn; v1 has no
  volume pipeline.
- **Hair.** Skipped + warn.
- **Cinema 4D node-based materials.** Falls back
  to flat material reads + warns. Future slice
  translates the C4D node tree into a v1
  `material::graph::Graph`.
- **Tracer / Field / MoGraph procedural systems.**
  Skipped + warn. Static geometry only in v1.
- **Subsurface scattering / participating media.**
  Materials with SSS channels fall back to
  diffuse (no SSS BSDF in the v1 catalogue per the
  material-graph spec section 11).
- **Custom procedural noise / shader networks
  beyond the standard channels.** Falls back to
  the channel's constant colour.

Some Cinema 4D features the v1 plugin **does**
honour without explicit work, because the plugin
treats them as "rendering boundaries":

- **Takes.** Each take is a separate Execute call
  with the take's resolved scene (section 8.6).
- **Animation timeline (per-frame state).** Each
  frame is a separate Execute call (per-frame
  motion blur is NOT covered; that needs motion
  vectors).
- **Render Region.** Section 7.4's "render full +
  copy in-region" strategy.

### 11.2 Complexity of full integration

Integrating natively with Cinema 4D's render
pipeline is structurally larger than the bridge.
The plugin touches surfaces the bridge never
needs to:

- **C4D's threading model.** The plugin's
  callbacks run in different threads at different
  times - the UI thread for setup messages, the
  render thread for Execute, plugin-internal
  threads if the plugin spawns them. The plugin
  must respect Cinema 4D's "what can be called on
  which thread" contract; violating it produces
  rare, hard-to-reproduce hangs.
- **Cancellation contract.** Cinema 4D dispatches
  cancellation requests asynchronously. The plugin
  polls a cancellation flag between progressive
  batches (section 7.5) and during long
  operations. Failing to poll fast enough gives
  the artist a "stuck render" experience that is
  hard to distinguish from a real plugin bug.
- **Picture Viewer / IRR / Render Queue surface
  parity.** Each surface has its own SDK API quirks
  (resolution dispatch, progress reporting,
  multi-pass slot allocation). The plugin's Execute
  must work for all of them, even though the
  artist never directly invokes the
  per-surface differences.
- **Documentation gaps.** Cinema 4D's C++ SDK
  documentation is uneven; some surfaces have
  reference docs, others have only header files +
  community-published examples. A plugin author
  works partly from official docs, partly from
  empirical testing, partly from reverse-
  engineering existing renderers.
- **Maintenance burden across SDK versions.**
  Section 10.1's deprecation cycle means the
  plugin's source needs touchups every 1-2
  Cinema 4D releases. The bridge's Python source
  has been more SDK-stable historically because
  Maxon's Python API is a thinner layer; C++
  inherits all of the deeper SDK churn.

These costs are why M23 is at the END of the
roadmap (section 12). Every step before it makes
shipping the native plugin easier; reordering the
sequence would mean paying the integration cost on
an unstable foundation.

### 11.3 Performance trade-offs

The v1 design accepts several performance trade-offs
deliberately, in exchange for correctness +
simplicity. Each is reachable through a future
optimisation slice once measurements warrant.

- **Full re-upload on every change** (section 9.3).
  Trade: bandwidth-bound on heavy scenes.
  Mitigation: dirty-tracking partial re-upload,
  future slice.
- **Host round trip on framebuffer copy**
  (section 7.3). Trade: a `cudaMemcpy`-to-host
  + `BaseBitmap`-write pass per frame, on the
  order of milliseconds at HD and tens of
  milliseconds at 4K. Mitigation: device-direct
  copy through the SDK's device-friendly bitmap
  interface (when available + stable), future
  slice.
- **Sample-accumulation reset on every change**
  (section 9.4). Trade: previously-rendered samples
  are thrown away even when only the camera moved.
  Mitigation: temporal denoiser + reprojection,
  M22 plan section 9 future slice.
- **Render-region renders the full frame +
  crops** (section 7.4). Trade: pays full-frame
  per-pixel cost even when the region is small.
  Mitigation: region-aware kernel launches, future
  slice.
- **Per-Execute renderer-buffer reallocation**
  (section 7.4). Trade: a few `cudaMalloc` /
  `cudaFree` calls per Execute, on the order of
  microseconds. Mitigation: persistent buffers
  reset only on resolution change.

The trade-offs are not unique to RelativityRender;
every commercial GPU renderer hits the same
optimisation ladder. v1 climbs the first rung
(correctness); each subsequent rung is its own
slice.

## 12. Recommended development order

The native plugin (this milestone, M23) is the
**last** integration the project ships. Every step
before it produces something the native plugin
depends on; reordering the sequence would mean
paying the C++ integration cost on an unstable
foundation. The recommended order is:

### 12.1 Step 1: Python bridge

**Status:** shipped (M19, six implementation slices).
**What it produces:** an end-to-end demonstration
that a Cinema 4D scene CAN be translated into the
RelativityRender scene format and rendered. The
bridge's Python source is the canonical reference
for the per-element mapping (camera / geometry /
materials / lights) the native plugin then
transcribes to C++.

**Why first:** Python is fast to iterate on. The
bridge stress-tests the translation rules without
committing the project to a C++ build pipeline that
would slow down any change. By the time the native
plugin starts, the bridge has already proven that
the translation rules are correct + complete enough
for the v1 catalogue.

**What it gates for the next steps:** the
translation rules (section 8.7's reuse pin), the
unsupported-feature taxonomy (section 11.1), the
warning channel + dialog wording.

### 12.2 Step 2: Renderer server

**Status:** shipped foundation (M18 + M19 wiring
slice).
**What it produces:** a stable line protocol the
bridge talks to. Stabilises the renderer's external
contract (the public façade in protocol form). The
bridge exercises it under real workloads, surfacing
bugs that would otherwise hide until the native
plugin tried to use the renderer.

**Why second:** the protocol is the renderer's
test harness for the OUT-of-process case. Even
though the native plugin runs IN-process and skips
the protocol entirely, the protocol's existence
tightens the renderer's public façade. A renderer
that talks cleanly over a socket has a public API
that's already been disciplined.

**What it gates for the next steps:** the
renderer's external public-facade contract; the
protocol's `render` / `set_beta` / etc. command
shapes that the native plugin's coordination code
takes inspiration from.

### 12.3 Step 3: Stable standalone renderer

**Status:** in progress (M14 + M15 + M21 paths;
M22 denoiser plan landed). The "stable" criterion is
that the renderer's path-traced output, with the
relativistic camera + materials + AOVs + denoiser,
matches its tested reference values across hardware
the v1 plugin targets.

**What it produces:** the renderer the native plugin
will drive. CUDA path mature, OptiX path mature,
material graph evaluator integrated (M21 done),
denoiser integrated (M22 future), AOVs producing
the channel set the multi-pass mapping needs.

**Why third:** a native plugin's only value is the
render quality it delivers. If the underlying
renderer is unstable, the plugin INHERITS that
instability and the artist blames the plugin. The
renderer needs to be solid first.

**What it gates for step 4:** the public façade
the plugin uses; the GPU upload paths
(`GpuScene::upload_from`); the AOV catalogue (the
multi-pass mapping slice's input); the denoiser
(progressive workflow's quality lever); the
relativistic camera model.

### 12.4 Step 4: Native C++ integration

**Status:** planned (this document).
**What it produces:** the deepest integration -
the plugin this whole spec describes.

**Why last:** by this point steps 1-3 have already
produced the translation rules, the renderer
contract, and a stable renderer. The native plugin
is then "transcribe + glue", not "discover + design
under uncertainty". Implementation slices for the
plugin can land incrementally, each guarded by
tests against the renderer that already works.

**What it ships:** the user-facing experience the
goals in section 5 pinned - live rendering inside
C4D, minimal artist friction, full reuse of the
existing renderer.

### 12.5 Why this order is the cheapest path

Each step amortises a cost across the plugin
implementation that would otherwise land on the
plugin's plate alone:

- **Translation correctness** ends up in the
  bridge (cheap to iterate); the plugin
  transcribes.
- **Public-facade hardening** ends up in the
  server (cheap to test against); the plugin
  drives a stable API.
- **Renderer maturation** ends up in the
  renderer's own development (cheap because it's
  already happening); the plugin inherits a
  stable backend.

Reordering would mean:

- Doing translation correctness AND C++
  integration AND renderer hardening AT THE SAME
  TIME, in a debug surface where the plugin's
  failure modes are hardest to isolate (any of
  the three can be the culprit).
- Paying the C++ build pipeline cost while the
  translation rules are still in flux.
- Discovering bugs in the renderer's API surface
  via the most expensive surface to debug
  (a Cinema 4D plugin running in C4D's process).

The 1 -> 4 order is the cheapest path. Sections
8 + 9's reuse-from-the-bridge rule + step 3's
maturation gate are how the plugin's
implementation slice arrives at "transcribe + glue"
rather than "build + discover".

## 13. What this slice covers

This and the previous slices together establish:

- The native integration at the conceptual level:
  what it is, why the project should grow it, how it
  relates to the Python bridge that ships today, and
  the three goals it must satisfy (sections 1-5).
- The Cinema 4D plugin registration mechanism:
  `VideoPostData` at the renderer-replacement
  priority, the lifecycle the plugin honours, and
  where its `Execute` slot is the single coordination
  point with the RelativityRender renderer
  (section 6).
- The framebuffer integration: how the GPU framebuffer
  reaches Cinema 4D's bitmap, resolution handling,
  progressive vs final rendering, and the IRR /
  Picture Viewer / Render Queue surfaces the same
  Execute path serves (section 7).
- The scene-translation contract: camera / geometry /
  materials / lights mappings reused from the bridge,
  the unsupported-feature taxonomy, and the
  in-memory `rr::scene::Scene` path the native plugin
  takes instead of round-tripping through `.rrscene`
  (section 8).
- The live-update strategy: document-level change
  events drive a full re-translate + re-upload in v1,
  with cancellation-of-in-flight-render and sample-
  accumulation reset on every change (section 9).
- SDK and platform constraints: SDK-version
  compatibility cycle, per-version compilation
  requirements, Windows / macOS coverage (with the
  Apple Silicon CUDA gap explicitly flagged), and
  the GPU / driver / OptiX requirements with the
  load-time validation policy (section 10).
- v1 limitations: the unsupported-feature list
  inherited from the bridge, the integration
  complexity costs (threading model, cancellation
  contract, surface-parity, documentation gaps,
  cross-SDK maintenance burden), and the
  performance trade-offs v1 deliberately accepts
  in exchange for correctness + simplicity
  (section 11).
- Recommended development order: a 1 -> 4
  staircase from the Python bridge (M19, shipped),
  through the renderer server (M18, shipped),
  through a stable standalone renderer (M14 +
  M15 + M21 + M22), to the native plugin (M23).
  Each step amortises a cost the next step would
  otherwise pay; reordering means paying every
  cost on a less-stable foundation (section 12).

It deliberately does NOT pin:

- The multi-pass / AOV channel mapping into Cinema 4D's
  `MultipassBitmap` slots.
- The relationship to the existing bridge once both
  ship in parallel (which workflows belong to which
  - section 12 sketches the development-order
  rationale; the long-term workflow split is its
  own slice).

Each of those lands as its own doc slice. Implementation
work begins only after the slices that constrain it have
landed - the same incremental rule the rest of the
project follows.

## 14. Out of scope for v1 of the spec

Listed for clarity so the v1 implementation slice does
not creep:

- **Cinema 4D plugin marketplace distribution.** Maxon's
  Plugin Cafe / store flow has its own onboarding,
  signing, and licensing requirements. The first
  shipping plugin is hand-installed (drop the file in
  the plugins folder); marketplace distribution is its
  own slice once the plugin is stable.
- **Cross-platform builds.** Cinema 4D targets Windows
  and macOS officially; Linux support varies by SDK
  release. The v1 native plugin picks one platform and
  ships there first; the second platform is a follow-up.
- **Cinema 4D node-based material translation.** Cinema
  4D's own node-based materials are themselves a
  separate translation problem; the bridge already
  lists this as deferred (M19 ext 3's "future advanced
  mapping"). The native renderer inherits the same
  scope.
- **Cinema 4D Tracer / Field / MoGraph integration.**
  Procedural-cloning systems are scene-graph features
  the v1 native renderer does not yet translate. Static
  geometry (polygon meshes, the M19 ext 2 export path)
  is the v1 surface.
- **Network rendering / Team Render.** Cinema 4D's
  Team Render protocol is the renderer's own concern.
  The native plugin's first shipping version targets
  single-machine rendering; Team Render integration is
  later.
- **Animation timeline integration.** Per-frame state
  scrubbing through C4D's timeline drives a sequence
  of single-frame renders in v1. Time-aware features
  (motion blur, motion-vector AOVs for the temporal
  denoiser) join later.
- **Interactive material editing.** Editing a material
  parameter in C4D's Attribute Manager triggers a
  re-render in v1, but the renderer does not yet
  expose live material editing through its API. The
  material-graph spec's evaluator is the lever; the
  native plugin uses it once the live-edit slice
  lands on the renderer side.
- **OS / GPU vendor compatibility matrix.** The
  RelativityRender renderer is CUDA / OptiX-first.
  Apple Silicon Macs (which Cinema 4D supports) cannot
  run CUDA. The native plugin's behaviour on
  unsupported hardware (graceful absence vs prominent
  error) is its own slice.
- **The bridge's deprecation timeline.** The bridge
  (M19) and the native renderer coexist. Deciding
  whether to deprecate the bridge once the native path
  is stable is a separate strategic call; this spec
  does not commit to one.

The above keep the v1 spec tight: a single-renderer
integration that uses Cinema 4D's standard plugin
mechanism, drives RelativityRender's existing
public façade, fills C4D's standard framebuffer
surfaces, and honours the live-update events Cinema 4D
dispatches.

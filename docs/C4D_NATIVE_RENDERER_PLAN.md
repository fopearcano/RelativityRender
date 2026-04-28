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

## 7. What this slice covers

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

It deliberately does NOT pin:

- The framebuffer integration shape (per-pixel
  `BaseBitmap` writes vs bulk upload vs multi-pass
  output via Cinema 4D's `MultipassObject` API).
- The scene-translation contract (which C4D types map
  to which RelativityRender types, which features fall
  back to defaults, which features are skipped).
- The live-update mechanism (`MSG_UPDATE` /
  `EVMSG_DOCUMENTRECALCULATED` / scene-hook
  message-handling interplay with the renderer's
  per-frame upload path).
- The limitations the v1 native path will deliberately
  ship with (procedural noise, hair, volumes, SSS,
  takes / network rendering, ...).
- The Cinema 4D SDK version this plugin targets.
  Different SDK generations expose different renderer
  APIs; the v1 target version + the deprecation
  policy are pinned in their own slice.
- The relationship to the existing bridge once both
  ship in parallel (which workflows belong to which).

Each of those lands as its own doc slice. Implementation
work begins only after the slices that constrain it have
landed - the same incremental rule the rest of the
project follows.

## 8. Out of scope for v1 of the spec

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

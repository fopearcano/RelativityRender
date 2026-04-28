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

## 6. What this slice covers

This slice introduces the native integration at the
conceptual level: what it is, why the project should
grow it, how it relates to the Python bridge that ships
today, and the three goals it must satisfy.

It deliberately does NOT pin:

- The Cinema 4D registration mechanism (`VideoPostData`
  vs renderer-plugin entry point vs scene-hook). That
  is the next doc slice.
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

## 7. Out of scope for v1 of the spec

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

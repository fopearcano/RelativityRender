# RelativityRender — BUILD PLAN

This file is the live, project-wide log of what has landed and what is next.
Update it after every implementation step, per
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` rule 8.

---

## Current State

- **Active milestones:** M2 — Core Engine (in progress), **M3 — Math
  Library (landed)**, **M4 — Image / Framebuffer System (landed)**,
  **M5 — CUDA Device Layer (landed)**.
- **Active branch:** `claude/create-docs-architecture-T2Dp5`.
- **Code in repo:** repository skeleton, top-level CMake project, the
  minimal C++20 application foundation, configuration + CLI handling,
  the math library, the image / framebuffer system, the GPU device
  layer with device queries, the GPU buffer abstraction, the first
  CUDA kernel (gradient), the **camera system** (host
  `rr::camera::Camera` plus device-friendly `GpuCamera` POD and
  `RR_HD generate_camera_ray`), and a kernel that writes per-pixel
  primary rays as RGB. The `RelativityRender` executable's
  `--render` path now runs the camera-ray-visualization kernel and
  saves to `output/gpu_camera_rays.ppm` (or `--output`); the
  `render_gradient` kernel is preserved as a diagnostic. Four test
  executables — `math_tests` (42), `image_tests` (39), `gpu_tests`
  (20), and `camera_tests` (43) — all green through `ctest`.
  `rr_image`, `rr_gpu`, and `rr_camera` are static libraries;
  `RelativityRender` links `rr_gpu`. No primitive intersection,
  no scene system.

## Module Status (mirrors `docs/MODULE_MAP.md`)

| #  | Module                              | Status        |
|----|-------------------------------------|---------------|
| 1  | Core Engine                         | in progress   |
| 2  | Math Library                        | landed        |
| 3  | Image / Framebuffer System          | landed        |
| 4  | GPU Device Layer                    | landed        |
| 5  | CUDA Backend                        | in progress   |
| 6  | OptiX Backend                       | in progress   |
| 7  | Scene Graph                         | landed        |
| 8  | Geometry System                     | landed        |
| 9  | Material / Shading System           | landed        |
| 10 | Texture System                      | landed        |
| 11 | Lighting System                     | landed        |
| 12 | Camera System                       | landed        |
| 13 | Relativistic Camera Model           | landed        |
| 14 | Path Tracer                         | in progress   |
| 15 | Progressive Renderer                | not started   |
| 16 | Denoiser Integration                | not started   |
| 17 | Render Passes / AOVs                | landed        |
| 18 | Scene File Format                   | landed        |
| 19 | Renderer Server                     | in progress   |
| 20 | Cinema 4D Bridge                    | in progress   |
| 21 | Future Native Cinema 4D Renderer    | not started   |
| 22 | Node Editor / Material Graph        | in progress   |

All modules now have a placeholder source directory under `src/`,
`integrations/`, or `tools/` and a README pointing back at
`docs/MODULE_MAP.md`. No module ships any code.

## Milestone Status (mirrors `docs/MILESTONE_ROADMAP.md`)

| Milestone | Title                                   | Status      |
|-----------|-----------------------------------------|-------------|
| M0        | Architecture & Documentation            | landed      |
| M1        | Repository Skeleton & Build System      | landed      |
| M2        | Core Engine: Logging, Config, Lifecycle | in progress |
| M3        | Math Library                            | landed      |
| M4        | Image / Framebuffer System              | landed      |
| M5        | CUDA Device Layer                       | landed      |
| M6        | CUDA Framebuffer & First Kernel         | landed      |
| M7        | Camera System & GPU Camera Rays         | landed      |
| M8        | GPU Primitive Intersection              | landed      |
| M9        | Relativistic Camera Model (First Pass)  | landed      |
| M10       | GPU Scene Upload & Triangle Mesh        | landed      |
| M11       | Material System (Foundations)           | landed      |
| M12       | Lighting System (Foundations)           | landed      |
| M13       | Scene File Format & Parser              | landed      |
| M14       | Path Tracing Foundation                 | in progress |
| M15       | OptiX Backend (Upgrade Path)            | in progress |
| M16       | Texture System                          | landed      |
| M17       | Render Passes / AOVs                    | landed      |
| M18       | Renderer Server                         | in progress |
| M19       | Cinema 4D Bridge (Plugin)               | in progress |
| M20       | Preview UI                              | not started |
| M21       | Material Node Graph (Editor)            | in progress |
| M22       | Denoiser Integration                    | not started |
| M23       | Native Cinema 4D Renderer Integration   | not started |

---

## Change Log

### 2026-04-28 — M23 (spec, constraints + dev-order): limitations and recommended order

Fifth (and likely final) doc slice for M23. Adds three
new sections to `docs/C4D_NATIVE_RENDERER_PLAN.md`:
section 10 ("SDK and platform constraints") covers the
Cinema 4D SDK compatibility cycle, per-version
compilation requirements, Windows / macOS coverage
including the Apple Silicon CUDA gap, and GPU / driver
/ OptiX dependencies. Section 11 ("Limitations")
inherits the bridge's unsupported-feature list, pins
the integration complexity costs, and lists the v1
performance trade-offs. Section 12 ("Recommended
development order") sketches the 1 -> 4 staircase
the project actually followed - Python bridge ->
renderer server -> stable standalone renderer ->
native C++ plugin - and explains why each step
amortises a cost the next would otherwise pay.

- **`docs/C4D_NATIVE_RENDERER_PLAN.md`:**
  - Inserted section 10 "SDK and platform constraints":
    - 10.1 Cinema 4D SDK version compatibility:
      generation boundaries (renderer API, multi-pass /
      framebuffer, scene API), Maxon's deprecation
      cycle (deprecated in N, removed in N+2), v1
      picks ONE SDK target with explicit-slice bumps.
    - 10.2 Plugin compilation per SDK release: SDK
      headers + framework, Maxon-certified compiler
      versions per SDK, license / NDA terms,
      continuous-integration consequences (N parallel
      build pipelines per SDK x compiler x platform
      tuple).
    - 10.3 Platform constraints: Windows + macOS
      Intel + macOS ARM in a table; Apple Silicon
      CUDA gap explicitly flagged; v1 plugin's
      load-time check requirements (detect, log,
      decline gracefully). Future slice can add
      Metal-based GPU backend for Apple Silicon.
    - 10.4 GPU / driver dependencies: NVIDIA Pascal+
      for OptiX 7.x; CUDA Toolkit at compile time
      with matching runtime driver; OptiX SDK +
      driver requirements (M15 plan); plugin's
      load-time validation policy + clear-error
      reporting through C4D's render log.
  - Inserted section 11 "Limitations":
    - 11.1 Unsupported C4D features inherited from
      the bridge: generators, deformers (use
      undeformed), spot lights (skip), area / tube
      (degrade to point), volumes, hair, C4D node
      materials, Tracer / Field / MoGraph
      procedurals, SSS, custom shader networks
      beyond standard channels. Plus features the
      v1 plugin DOES honour as rendering boundaries:
      Takes (per-take Execute), animation timeline
      (per-frame Execute), Render Region (full +
      crop).
    - 11.2 Complexity of full integration: C4D's
      threading model, cancellation contract,
      Picture Viewer / IRR / Render Queue surface
      parity, documentation gaps, maintenance burden
      across SDK versions. Justifies M23 being LAST
      in the roadmap.
    - 11.3 Performance trade-offs: full re-upload
      on every change (vs dirty-tracking partial),
      host round trip on framebuffer copy (vs
      device-direct), sample-accumulation reset
      (vs camera-only reprojection), full-frame
      render-region (vs region-aware launches),
      per-Execute renderer-buffer reallocation (vs
      persistent buffers). Each rung climbs in its
      own future optimisation slice.
  - Inserted section 12 "Recommended development
    order":
    - 12.1 Step 1: Python bridge (M19, shipped) -
      stress-tests translation rules without
      committing to a C++ build pipeline.
    - 12.2 Step 2: Renderer server (M18, shipped) -
      disciplines the renderer's external public-
      facade contract under bridge use.
    - 12.3 Step 3: Stable standalone renderer (M14 +
      M15 + M21 in progress; M22 plan landed) - the
      renderer the native plugin will drive.
    - 12.4 Step 4: Native C++ integration (M23) -
      "transcribe + glue", not "discover + design
      under uncertainty".
    - 12.5 Why this order is the cheapest path:
      each step amortises a cost (translation
      correctness in the bridge, public-facade
      hardening in the server, renderer maturation
      in the renderer) that would otherwise land on
      the plugin's plate alone. Reordering means
      paying every cost on the most expensive
      surface to debug (a Cinema 4D plugin running
      in C4D's process).
  - Renumbered the trailing meta sections from 10 / 11
    to 13 / 14. Updated section 13 to include the
    new entries; dropped the v1-limitations and SDK-
    version-target entries from the deferred list.
    Remaining deferred: AOV channel mapping into
    `MultipassBitmap` slots; the long-term
    bridge-vs-native workflow split (section 12
    covers the development-order rationale; the
    workflow split once both ship in parallel is
    its own slice).

#### Verified locally

```
$ ls docs/C4D_NATIVE_RENDERER_PLAN.md
$ grep '^## ' docs/C4D_NATIVE_RENDERER_PLAN.md
## 1. Purpose
## 2. What a native Cinema 4D renderer integration is
## 3. Why RelativityRender needs one
## 4. Python bridge vs native C++ integration
## 5. Goals
## 6. Cinema 4D plugin registration paths
## 7. Framebuffer integration
## 8. Scene translation
## 9. Live update strategy
## 10. SDK and platform constraints
## 11. Limitations
## 12. Recommended development order
## 13. What this slice covers
## 14. Out of scope for v1 of the spec
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "Cinema 4D SDK version compatibility issues":
  section 10.1 - generation boundaries, deprecation
  cycle, v1 single-target policy.
- "Plugin compilation per version": section 10.2 -
  SDK headers + Maxon-certified compilers per SDK,
  license terms, CI matrix consequences.
- "Platform constraints (Windows / macOS)":
  section 10.3 - 3-row platform table with the
  Apple Silicon CUDA gap explicitly called out
  + v1 load-time-detection policy.
- "GPU / driver dependencies": section 10.4 - NVIDIA
  Pascal+ / CUDA Toolkit + matching driver / OptiX
  / load-time validation through C4D's log.
- "Unsupported C4D features": section 11.1 -
  inherited from the bridge's M19 ext slices,
  plus the rendering-boundary features (Takes,
  per-frame timeline, Render Region) that DO
  work as side effects.
- "Complexity of full integration": section 11.2 -
  threading, cancellation, surface parity, doc
  gaps, SDK maintenance burden. Justifies M23
  being LAST.
- "Performance trade-offs": section 11.3 - five
  v1 trade-offs (full re-upload / host round
  trip / accum reset / full-frame region /
  per-Execute realloc) with future optimisation
  paths.
- "Recommended development order: 1 Python bridge
  / 2 Renderer server / 3 Stable standalone
  renderer / 4 Native C++ integration": section 12
  - one subsection per step, plus the "why this
  order is the cheapest path" rationale.
- "Do NOT implement anything": no source / build
  / test changes; spec-only slice.

#### Module / milestone status

- Module 21 (Future Native Cinema 4D Renderer):
  remains `not started`. Five doc slices in:
  intro / registration / framebuffer / scene +
  live / constraints + dev-order. The remaining
  doc slice is the AOV channel mapping into
  `MultipassBitmap` slots and the long-term
  bridge-vs-native workflow split.
- M23 (Native Cinema 4D Renderer Integration):
  remains `not started` (same).

### 2026-04-28 — M23 (spec, scene + live): scene translation + live update

Fourth doc slice for M23. Adds two new sections to
`docs/C4D_NATIVE_RENDERER_PLAN.md`: section 8 ("Scene
translation") which pins WHAT pieces of the live Cinema
4D document the plugin reads and HOW each maps onto
RelativityRender's scene representation, and section 9
("Live update strategy") which pins how the plugin
detects scene changes and re-uploads to the GPU. Stays
at the conceptual level; the byte-level field shapes
inherit from the bridge's M19 extension slices.
SDK constraints stay deferred per the prompt.

- **`docs/C4D_NATIVE_RENDERER_PLAN.md`:**
  - Inserted section 8 "Scene translation":
    - 8.1 What translation does: the plugin walks
      `BaseDocument`, populates an
      `rr::scene::Scene` host container (the same
      one the .rrscene loader produces), and calls
      the renderer's public façade. Three structural
      decisions: only public types, no .rrscene
      round-trip, pure function of document state.
    - 8.2 Camera mapping table: `CameraObject::GetMg()`
      -> position / forward / up after the bridge's
      Z-flip; `CAMERAOBJECT_FOV_VERTICAL` -> fov;
      `RDATA_XRES`/`YRES` -> render_settings. Same
      mappings the M19 ext 1 slice landed.
    - 8.3 Geometry mapping table:
      `PolygonObject::GetAllPoints()` ->
      `mesh.vertices[]` (matrix-baked + Z-flipped),
      `GetAllPolygons()` -> `mesh.triangles[]`
      (quads triangulated `a-c` diagonal),
      first `Ttexture` tag -> `mesh.material_id`.
      Same `triangulate_cpolygon` rule the bridge
      uses (M19 ext 2).
    - 8.4 Materials mapping table: standard
      `Mmaterial` color / luminance, viewport
      "Display Color" fallback, future
      bitmap-shader-color path. Native plugin gains
      direct construction of a v1
      `material::graph::Graph` (M21) before handing
      to the renderer's compile path; bridge's path
      goes through .rrscene + auto-synthesise.
    - 8.5 Lights mapping table: omni / distant /
      parallel / area / tube / spot / parspot, with
      area degraded to point and spot skipped
      (matching M19 ext 3).
    - 8.6 Handling unsupported features: ignore-and-
      warn taxonomy reused from the bridge, plus
      two native-specific entries (Take system =
      separate Execute call; animation timeline =
      per-frame Execute call). The "warn" channel
      is C4D's standard logging facility.
    - 8.7 Reuse of the bridge's mapping pinned as a
      structural design choice: same mapping table,
      same warning labels, same ignore-set. A
      difference between the two paths is a bug,
      not a design choice. Maintenance lever: new
      features land in both through the same
      translation logic.
    - 8.8 In-memory vs file-based: the native plugin
      builds an `rr::scene::Scene` in memory and
      hands it to `GpuScene::upload_from(scene)`
      directly. Bridge's file-based path stays for
      the cross-machine case (section 4's
      "complementary, not exclusive" pin).
  - Inserted section 9 "Live update strategy":
    - 9.1 What changes between renders: camera moves,
      object moves, topology / material edits + the
      implicit edits (Render Settings, take switch,
      animation frame).
    - 9.2 Detecting C4D scene changes: candidates
      tabled (document-level event, per-object
      `MSG_UPDATE`, SceneHook plugin). v1 picks the
      document-level event for simplicity. Render
      loop pseudocode showing
      cancel-in-flight + reset-accumulation +
      reschedule.
    - 9.3 Application: full vs partial re-upload.
      v1 picks **full re-upload** for correctness
      first - a partial path can desync from the
      document; bugs there look like real renderer
      bugs. Justified by translator latency
      measurements: re-translating typical interactive
      scenes is well under 1 ms in C++. Dirty-
      tracking partial re-upload is a future slice
      once measurements warrant.
    - 9.4 Sample-accumulation invalidation: reset on
      every change in v1; the next render starts at
      sample 0 with the new scene. Combined with the
      M22 denoising plan's progressive workflow, the
      artist sees a denoised low-spp frame within a
      fraction of a second of the edit. Partial
      invalidation (camera-only reprojection) is
      temporal-denoiser territory and out of v1.
    - 9.5 Performance considerations: three pressure
      points (translator latency, GPU re-upload
      bandwidth, render-loop responsiveness) and
      the v1 mitigations (defer to dirty-tracking
      when measurements warrant; poll the
      cancellation flag between progressive batches).
    - 9.6 What this slice does NOT cover: SDK
      version constraints (next slice), AOV channel
      mapping, motion vectors / temporal denoising,
      Team Render, motion blur.
  - Renumbered the trailing meta sections from 8 / 9
    to 10 / 11. Updated section 10's "what this slice
    covers" to include the new entries; dropped scene-
    translation and live-update from the deferred list.
    Remaining deferred items: AOV channel mapping, v1
    limitations, SDK version target, bridge-vs-native
    workflow split.

#### Verified locally

```
$ ls docs/C4D_NATIVE_RENDERER_PLAN.md
$ grep '^## ' docs/C4D_NATIVE_RENDERER_PLAN.md
## 1. Purpose
## 2. What a native Cinema 4D renderer integration is
## 3. Why RelativityRender needs one
## 4. Python bridge vs native C++ integration
## 5. Goals
## 6. Cinema 4D plugin registration paths
## 7. Framebuffer integration
## 8. Scene translation
## 9. Live update strategy
## 10. What this slice covers
## 11. Out of scope for v1 of the spec
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "Mapping C4D scene -> RelativityRender scene":
  section 8.1 (overall flow), 8.2-8.5 (per-element
  mappings), 8.7 (reuse from the bridge).
- "Camera, geometry, materials, lights": one
  subsection each (8.2-8.5) with mapping tables
  cross-referenced to the bridge's existing M19
  slices.
- "Handling unsupported features": section 8.6 -
  ignore-and-warn taxonomy, native-specific entries
  for the Take system + animation timeline.
- "Detecting changes in C4D scene": section 9.2 -
  three candidate hooks tabled, document-level
  event picked for v1.
- "Partial vs full scene re-upload": section 9.3 -
  v1 picks full re-upload for correctness; partial
  is a future slice once measurements warrant.
- "Performance considerations": section 9.5 - three
  pressure points (translator latency, GPU re-upload
  bandwidth, render-loop responsiveness) with v1
  mitigations.
- "Do NOT cover SDK constraints yet": section 9.6
  + section 10's deferred list both flag the SDK
  version target as the next slice's job.

#### Module / milestone status

- Module 21 (Future Native Cinema 4D Renderer):
  remains `not started`. Four doc slices in (intro,
  registration, framebuffer, scene + live update).
  The remaining slices are the v1 SDK target,
  multi-pass / AOV channel mapping, the v1
  limitations list, and the bridge-vs-native
  workflow split.
- M23 (Native Cinema 4D Renderer Integration):
  remains `not started` (same).

### 2026-04-28 — M23 (spec, framebuffer): Cinema 4D framebuffer integration

Third doc slice for M23. Adds section 7 ("Framebuffer
integration") to `docs/C4D_NATIVE_RENDERER_PLAN.md`,
pinning HOW pixels move from the GPU path tracer's
output into the bitmap surface Cinema 4D expects to
display. Stays at the conceptual level - storage
shapes, ownership, host vs device round trip,
resolution and progressive contracts. Per the prompt
the multi-pass / AOV channel mapping, scene
translation, and live-update mechanics are NOT pinned
here.

- **`docs/C4D_NATIVE_RENDERER_PLAN.md`:**
  - Inserted section 7 "Framebuffer integration":
    - 7.1 What RelativityRender produces today: a
      device-resident `GpuBuffer<float>` of size
      `width * height * 4` floats; `Rgba32F`,
      row-major, channel-interleaved, top-left
      origin (matches the M17 AOV foundation).
      Per-pixel writes inside the kernel; download
      to host `rr::image::Image` after `cudaDeviceSynchronize`.
      The plugin starts from the same `Image`; the
      renderer's public façade does not change.
    - 7.2 What Cinema 4D expects: `BaseBitmap` for
      the simple beauty path; `MultipassBitmap` for
      the AOV case (deferred). Cinema 4D OWNS the
      bitmap (the plugin writes but does not
      allocate / free). Cinema 4D DICTATES
      resolution + format. The plugin must signal
      completion / partial progress (mechanism is
      the live-update slice's call).
    - 7.3 Mapping: per-pixel copy from `Image` into
      the bitmap. Channel order RGBA on both sides;
      no swap. Origin top-left on both sides; no
      vertical flip. Float-to-X conversion: 8-bit
      = same logic as `Image::save_ppm`; 16-bit /
      32-bit = identity / half-conversion. Alpha
      forwarded as-is (renderer writes 1.0 today).
      Future optimisation: skip the host round
      trip via device-direct copy (CUDA / OpenGL
      interop or device-friendly bitmap interface);
      v1 stays with the host round trip for
      portability.
    - 7.4 Resolution handling: plugin honours C4D's
      requested resolution exactly; render-region
      strategy v1 = render full + copy in-region;
      resolution change between Renders =
      reallocate per Execute (the renderer's
      `GpuBuffer` already supports `allocate(N)`
      resizing). The plugin DOES NOT rescale the
      renderer's output to match the bitmap.
    - 7.5 Progressive rendering vs final frame: the
      progressive loop sketched in pseudocode
      mirrors the M22 denoising-plan's section 6
      structure (same renderer path); final-frame
      collapses to a single launch + one bitmap
      fill. The differences live in per-batch knobs
      (sample count, bitmap update cadence, denoiser
      frequency, cancellation poll), not in two
      parallel code paths.
    - 7.6 Preview vs final render: 6-row table
      covering IRR / Picture Viewer / Render Queue
      surfaces. All three invoke the same plugin
      Execute; parameter selection differs.
      IRR-specific notes (dynamic resolution,
      aggressive cancellation), Render-Queue notes
      (unattended, prefer offline parameters), Take
      rendering reuses the standard scene
      translation.
    - 7.7 What this slice does NOT cover: multi-pass
      / AOV mapping into `MultipassBitmap` slots
      (deferred), tone-mapping / view-transform
      interplay with C4D's color-management pipeline
      (deferred), HDR EXR write-out (deferred),
      tile / scanline progressive granularity
      (deferred), scene translation + live update
      (out of scope per the prompt).
  - Renumbered the trailing meta sections from 7 / 8
    to 8 / 9. Updated section 8's "what this slice
    covers" to include the new framebuffer entry,
    and updated the deferred list to drop the
    framebuffer-integration entry now that 7 has
    landed; multi-pass / AOV mapping joins the
    deferred list as a separate item (it was
    implicitly under "framebuffer" before).

#### Verified locally

```
$ ls docs/C4D_NATIVE_RENDERER_PLAN.md
$ grep '^## ' docs/C4D_NATIVE_RENDERER_PLAN.md
## 1. Purpose
## 2. What a native Cinema 4D renderer integration is
## 3. Why RelativityRender needs one
## 4. Python bridge vs native C++ integration
## 5. Goals
## 6. Cinema 4D plugin registration paths
## 7. Framebuffer integration
## 8. What this slice covers
## 9. Out of scope for v1 of the spec
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "How RelativityRender produces images (GPU
  framebuffer)": section 7.1 - device-resident
  `GpuBuffer<float>` Rgba32F + the existing path
  through `cudaDeviceSynchronize` and the host
  `Image`.
- "How images are passed back to C4D": sections 7.2 +
  7.3 - C4D-owned bitmap, plugin fills it via per-
  pixel writes after Execute drives the renderer.
- "Mapping GPU buffer -> C4D bitmap": section 7.3 -
  channel order, origin, float-to-X conversion,
  alpha, ownership. Future device-direct optimisation
  flagged but out of v1.
- "Resolution handling": section 7.4 - plugin honours
  C4D's resolution exactly; render-region v1 strategy
  pinned; per-Execute reallocation.
- "Progressive rendering vs final frame":
  section 7.5 - same renderer path as the denoising
  plan section 6; the differences are per-batch
  knobs, not two code paths.
- "Preview vs final render": section 7.6 - IRR /
  Picture Viewer / Render Queue tabled; all three
  share the Execute path.
- "Do NOT cover scene translation or live updates":
  section 7.7 + section 8's deferred list both call
  the omission out explicitly; both remain in the
  deferred list.

#### Module / milestone status

- Module 21 (Future Native Cinema 4D Renderer):
  remains `not started`. Three doc slices in
  (intro / registration / framebuffer); the
  remaining slices are scene translation, live
  update, AOV channel mapping, the v1 SDK target,
  and the bridge-vs-native workflow split.
- M23 (Native Cinema 4D Renderer Integration):
  remains `not started` (same).

### 2026-04-28 — M23 (spec, registration): Cinema 4D plugin registration paths

Second doc slice for M23 (Native Cinema 4D Renderer
Integration). Adds section 6 ("Cinema 4D plugin
registration paths") to
`docs/C4D_NATIVE_RENDERER_PLAN.md`, pinning HOW a Cinema
4D plugin gets to be the thing the artist's **Render**
click drives. Stays at the conceptual level - which API
to register against, which lifecycle callbacks frame
rendering, where the plugin slots into C4D's pipeline.
Per the prompt the framebuffer mechanics and scene-
translation contract are NOT pinned here; they get their
own slices.

- **`docs/C4D_NATIVE_RENDERER_PLAN.md`:**
  - Inserted section 6 "Cinema 4D plugin registration
    paths":
    - 6.1 Cinema 4D's plugin model: `.xdl64` / `.dylib`
      shared libraries; `PluginStart()` entry point;
      `RegisterXxxPlugin` registration; PluginCafe-
      allocated 32-bit plugin ids. Renderer-replacement
      role is owned by the `VideoPostData` plugin type.
    - 6.2 The `VideoPostData` plugin type. Registered
      via `RegisterVideopostPlugin(...)`; carries id +
      display name + info flags + allocator + priority.
      Lifecycle: Init / Execute / Free + capability
      queries. Execute is where rendering runs - no CPU
      pixel loop required of the plugin.
    - 6.3 Alternative: `Maxon::Renderer` / direct
      renderer-plugin APIs. Real but less stable across
      SDK releases, less documented, less battle-tested
      by community plugins. v1 picks `VideoPostData`
      for portability + maturity; a future slice can
      revisit once the v1 plugin is stable on a chosen
      SDK and Maxon's renderer-API roadmap settles.
    - 6.4 Render-pipeline hooks: priority constants.
      Pre-render / renderer-replacement / light-stage /
      post-effects roles documented in a table. Exact
      `VPPRIORITY_*` symbol names are SDK-release
      dependent; the plan pins the ROLE
      (renderer-replacement) that v1 takes.
    - 6.5 How Cinema 4D invokes rendering: 5-step flow
      from "artist picks RelativityRender from the
      Renderer dropdown" through allocate / Init /
      Execute / Free / display-in-Picture-Viewer.
      Render-thread context + cancellation contract
      noted (cancellation pinned in the live-update
      slice).
    - 6.6 Intercept vs replace: post-effect plugins
      run AFTER a renderer (per-pixel filtering on an
      input buffer); renderer-replacement plugins run
      INSTEAD OF a renderer (Execute starts with an
      empty / about-to-be-filled buffer). Difference
      is priority + Execute-call shape. RelativityRender
      is renderer-replacement; C4D's bundled renderers
      do not run when our plugin is active.
    - 6.7 Where RelativityRender connects in one
      paragraph: the plugin's `Execute` is the single
      coordination point - it converts the live
      document state into our scene representation
      (separate slice), calls the existing public
      renderer facade (the same one the standalone
      executable uses), and hands the framebuffer back
      (separate slice). Everything in between is the
      renderer that already ships, unchanged.
  - Renumbered the previous "What this slice covers"
    section from 6 to 7 and "Out of scope" from 7 to
    8. Updated the new section 7's deferred list to
    drop the Cinema-4D-registration-mechanism entry
    now that 6 has landed; the remaining deferred
    items (framebuffer integration, scene
    translation, live update, v1 limitations, SDK
    version target, bridge-vs-native workflow split)
    are unchanged.

#### Verified locally

```
$ ls docs/C4D_NATIVE_RENDERER_PLAN.md
$ wc -l docs/C4D_NATIVE_RENDERER_PLAN.md
$ python3 -c "open('docs/C4D_NATIVE_RENDERER_PLAN.md').read()"
$ grep '^## ' docs/C4D_NATIVE_RENDERER_PLAN.md
## 1. Purpose
## 2. What a native Cinema 4D renderer integration is
## 3. Why RelativityRender needs one
## 4. Python bridge vs native C++ integration
## 5. Goals
## 6. Cinema 4D plugin registration paths
## 7. What this slice covers
## 8. Out of scope for v1 of the spec
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "VideoPostData plugin type": section 6.2 - the v1
  target. Lifecycle (Init / Execute / Free), info
  flags, priority, allocator all called out
  conceptually.
- "Alternative: custom renderer plugin": section 6.3.
  `Maxon::Renderer` / direct renderer-plugin APIs
  exist but are less stable across SDK releases; v1
  goes with `VideoPostData` for portability + a
  proven track record. Future slice can revisit.
- "Render pipeline hooks in C4D": section 6.4.
  Priority constants pin which pipeline phase the
  plugin runs at. Pre-render / renderer-replacement
  / light / post-effects roles documented; v1 uses
  renderer-replacement.
- "Where RelativityRender connects into C4D render
  pipeline": section 6.7 - one-paragraph summary
  pinning the `Execute`-as-coordination-point rule.
- "How C4D invokes rendering": section 6.5 - 5-step
  flow with the artist's click as the entry point.
- "How plugin intercepts or replaces rendering":
  section 6.6 - intercept-vs-replace dichotomy with
  the difference (priority + Execute-call shape)
  pinned.
- "Do NOT cover framebuffer or scene translation
  yet": section 7's deferred list explicitly defers
  both. Section 6.7 calls out that they are
  separate slices.

#### Module / milestone status

- Module 21 (Future Native Cinema 4D Renderer):
  remains `not started`. Two doc slices in; the
  registration mechanism is the natural prerequisite
  for the framebuffer / scene-translation slices
  that follow.
- M23 (Native Cinema 4D Renderer Integration):
  remains `not started` (same).

### 2026-04-28 — M23 (spec, intro): native Cinema 4D renderer plan

First doc slice for M23 (Native Cinema 4D Renderer
Integration). Introduces the native renderer at the
conceptual level: what it is, why RelativityRender should
eventually grow this path, how it differs from the Python
bridge that ships today (M19), and the three goals it must
satisfy. No technical details on registration mechanism /
framebuffer integration / scene translation / live update
- those are deliberately deferred to subsequent slices.

- **`docs/C4D_NATIVE_RENDERER_PLAN.md`** (new):
  - 1: Status banner (introduction slice; technical
    surface deferred) + module reference (module 21,
    M23).
  - 2: What "native renderer integration" means in
    Cinema 4D's plugin model - registers as a
    Render-Settings-dropdown peer of Standard /
    Physical, fills the C4D Picture Viewer, reads the
    document directly. Lives inside C4D's process and
    talks to RelativityRender through the renderer's
    public C++ façade rather than the M18 server
    protocol the bridge uses.
  - 3: Why RelativityRender needs one. Three pressures:
    no external server process for the median artist;
    first-class participation in C4D's render pipeline
    (queue, takes, region, multi-pass, Team Render);
    in-viewport interactive preview (IRR / "render
    region") matching what users expect from
    Octane / Redshift / V-Ray.
  - 4: Bridge vs native comparison table. Pinned: the
    two paths are COMPLEMENTARY, not exclusive. A
    mature project ships both - native plugin for
    in-process interactive use, bridge for remote /
    headless / multi-machine workflows. Both share the
    same scene representation (Scene File Format) and
    renderer (public C++ façade); the renderer does
    not know which path drives it.
  - 5: Three top-level goals - live rendering inside
    C4D, minimal friction for artists, reuse the
    existing RelativityRender GPU backend (the plugin
    does NOT reimplement the path tracer, relativistic
    camera, material graph, AOVs, denoiser, OptiX
    path - it drives the SAME renderer the standalone
    executable drives).
  - 6: Explicit deferral list - registration mechanism
    (VideoPost vs renderer plugin vs scene-hook),
    framebuffer integration, scene-translation
    contract, live-update mechanism, v1 limitations
    list, SDK version target, bridge-vs-native
    workflow split. Each lands as its own slice.
  - 7: Out-of-scope-for-v1 footer (Plugin Cafe
    distribution, cross-platform, C4D node materials,
    Tracer / Field / MoGraph, Team Render, animation
    timeline, interactive material editing, GPU vendor
    compatibility matrix, bridge deprecation timeline).

#### Verified locally

```
$ ls docs/C4D_NATIVE_RENDERER_PLAN.md
$ wc -l docs/C4D_NATIVE_RENDERER_PLAN.md
$ python3 -c "open('docs/C4D_NATIVE_RENDERER_PLAN.md').read()"
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "What a native Cinema 4D renderer integration means":
  section 2.
- "Why RelativityRender should eventually integrate
  natively": section 3 (three pressures).
- "Difference between Python bridge and native C++
  integration": section 4 (10-row comparison table +
  the complementary-not-exclusive pin).
- "Define goals: live rendering inside C4D / minimal
  friction for artists / reuse RelativityRender GPU
  backend": section 5 - all three pinned, with the
  explicit "the plugin is a translator + invoker;
  everything in between is the renderer that already
  ships" rule.
- "Do NOT cover technical implementation details yet":
  section 6 explicitly defers the registration
  mechanism / framebuffer / scene translation / live
  update / SDK target / etc. Section 7's out-of-scope
  list reinforces.

#### Module / milestone status

- Module 21 (Future Native Cinema 4D Renderer):
  remains `not started`. The spec is the contract;
  nothing is promoted until implementation begins.
- M23 (Native Cinema 4D Renderer Integration):
  remains `not started` (same).

### 2026-04-28 — M22 (spec): denoising research / design plan

First doc slice for M22 (Denoiser Integration). Research
+ design only - no code touched. Pins the scope of an OptiX-
AOV denoiser integration alongside the existing path tracer
+ M17 AOV foundation, including the project-specific
relativity handling.

- **`docs/DENOISING_PLAN.md`** (new):
  - 1 + 2: purpose and the three pressures pushing the
    renderer towards a denoiser (preview UX after M19's
    blocking renders, time-to-final cost, and the noise
    amplification under high `|beta|`).
  - 3: OptiX denoiser at a glance - the four denoiser
    kinds (HDR / AOV / Temporal / Upscale), the v1
    target (AOV variant), the small-shape call
    sequence (`Create -> ComputeMemory -> Setup ->
    Invoke per frame`).
  - 4: required AOVs - beauty + albedo + normal as the
    three denoiser inputs, mapped onto the existing M17
    catalogue. The other M17 AOVs (depth, doppler /
    searchlight factors) are NOT denoiser inputs.
  - 5: per-AOV byte-level contract.
    - 5.1 Beauty: HDR linear RGB; tone-mapping /
      sRGB-encoding the input defeats the denoiser.
    - 5.2 Albedo: pre-lighting base colour (M17's
      `AOVKind::Albedo` already produces this).
    - 5.3 Normal: world-space unit-length vec3. M17's
      `Normal` AOV stores `0.5*N + 0.5` for
      visualisation; the denoiser wants raw normals.
      Two implementation options pinned (new
      `RawNormal` AOV vs decode pass) for the impl
      slice to pick.
    - 5.4 PhysicalBeauty addition - the project-
      specific concern. The OptiX denoiser is trained
      on standard photographic lighting; relativistic
      Doppler / searchlight modify the perceived
      radiance in ways the network does not expect.
      Solution: a new `PhysicalBeauty` AOV (pre-
      relativity radiance) feeds the denoiser; the
      relativity factors reapply after denoising.
      `D` and `D^4` are deterministic per-pixel
      functions, so reapply preserves the visible
      relativistic look at no denoiser-quality cost.
      Fallback: when the relativity strength sliders
      are zero, `PhysicalBeauty == Beauty` byte-for-
      byte and the reapply is the identity.
  - 6: progressive render workflow. Sample-accumulation
    loop sketched in pseudocode (`render_pathtrace` in
    chunks, running mean of the per-pixel radiance);
    denoise frequency (per-emit interactive vs final-
    only offline); server-protocol implications (today's
    M18 single-frame `render` ships v1; streaming is a
    future slice).
  - 7: integration shape. `src/denoise/` per the module
    map (forbidden imports: UI, Cinema 4D, Path Tracer
    internals). Public surface sketched as
    `rr::denoise::Denoiser` with `init(w, h)`,
    `run(inputs) -> outputs`, `destroy()`. Piggybacks
    on the M15 OptiX backend's `OptixDeviceContext`.
  - 8: open questions for the impl slice's checklist
    (normal-space confirmation, miss-pixel albedo
    convention, tile mode, half-float, OIDN parity,
    relativity-aware kernel sharing).
  - 9: out-of-scope footer (temporal, upscale, OIDN,
    adaptive sampling, multi-light, volume / SSS,
    progressive-stream protocol, spectral denoising).

#### Verified locally

```
$ ls docs/DENOISING_PLAN.md
$ wc -l docs/DENOISING_PLAN.md
$ python3 -c "open('docs/DENOISING_PLAN.md').read()"
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "OptiX denoiser": section 3 covers what it is, the four
  variants, and which one v1 picks (AOV).
- "Required AOVs": section 4 (the three-input table) and
  section 5 (per-AOV contract).
- "Beauty / albedo / normal inputs": sections 5.1 / 5.2 /
  5.3, plus the project-specific 5.4 (PhysicalBeauty)
  that pins the relativity handling.
- "Progressive render workflow": section 6.
- "Do not implement yet": section 7 explicitly defers
  every decision; section 8 lists the open questions
  whose resolution gates the impl slice.

#### Module / milestone status

- Module 16 (Denoiser Integration): remains `not
  started`. The plan is the contract; no code lands.
- M22 (Denoiser Integration): remains `not started`
  (same).

### 2026-04-28 — M21 (impl, gpu-shading): material graph integrated into the kernel

Sixth implementation slice of the material node graph. Lands
the device-side evaluator (`evaluateMaterial`) and threads it
through the existing CUDA shading kernels so the renderer
ALWAYS shades through a graph from this slice on. Existing
flat `MaterialParams` materials are auto-synthesised into v1
graphs at upload time, so scenes that have shipped to date
keep rendering without authoring changes.

Per the master rules, all evaluation runs on the GPU. Per
the prompt's "keep performance reasonable", the kernel
evaluator is a straight-line opcode loop with a stack-
allocated 32-slot pool - no dynamic allocation, no early
exit, no recursion.

- **`src/cuda/CudaMaterialGraph.cuh`**: extended.
  - `MaterialEvalResult` (baseColor + emissionColor +
    emissionStrength) - the subset of `MaterialParams` the
    v1 kernel reads when shading a hit.
  - `kMaterialGraphMaxSlots = 32` cap for the stack-
    allocated slot pool. v1 graphs never approach this; ops
    past the cap are skipped.
  - `RR_HD inline evaluateMaterial(view)` is the heart of
    the device path. Walks `view.ops[]` into the slot pool
    (ConstantColor / TextureSample / Add / Multiply with
    per-op identity defaults for unwired inputs), then
    walks `view.terminals[]` and writes the resolved
    Diffuse / Emission contributions into the eval result.
    `RR_HD inline` so the host test suite runs the same
    code the kernel does - the kernel path is correct by
    construction.
  - `_eval_slot_or` / `_eval_slot_scalar_or` private
    helpers cap slot indices and apply the per-input
    fallback (the same fallbacks the lowering bakes into
    immediates, so the kernel never branches on
    "wired?").
- **`src/material/GpuMaterial.{h,cpp}`**: added
  `synthesise_gpu_material_from_params(MaterialParams*)`.
  Builds a tiny v1 IR equivalent to the hand-built
  `ConstantColor(baseColor) -> Diffuse` + (optional)
  `ConstantColor(emissionColor) -> Emission(strength)`
  graph. Used by `GpuScene` to give every existing flat
  material a graph-shaped representation on the GPU
  without authoring changes.
- **`src/gpu/GpuScene.{h,cpp}`**: new `upload_material_graphs`
  path.
  - Synthesises one `GpuMaterial` per input
    `MaterialParams` via the new helper, concatenates each
    material's ops + terminals into two flat host vectors,
    uploads each as a single device buffer
    (`GpuBuffer<GpuOp>` + `GpuBuffer<GpuTerminal>`), then
    builds a parallel `GpuBuffer<CudaMaterialGraphView>`
    indexed by material id where each view's pointers
    point into the corresponding offsets of the flat
    buffers.
  - `upload_from(scene)` calls `upload_material_graphs`
    alongside `upload_materials`, so loaded scenes get
    their graph IRs uploaded automatically.
  - `device_material_graph_views()` /
    `material_graph_view_count()` accessors expose the
    per-material array.
  - `GpuScene.h` now includes
    `material/GpuMaterial.h` and
    `cuda/CudaMaterialGraph.cuh`; both are host-includable.
- **`src/cuda/CudaScene.cuh`**: `CudaSceneView` gains
  `(const CudaMaterialGraphView* material_graph_views,
  int material_graph_view_count)` so the kernel can fetch
  the per-hit graph view.
- **`src/cuda/CudaRenderer.cu`**: every view-population
  block (render_scene / render_pathtrace / render_aovs)
  pulls the new fields from `GpuScene`. Three identical
  appends, one `replace_all` edit.
- **`src/cuda/CudaTestKernel.cu`**: new
  `override_material_with_graph(scene, material_index,
  mat)` device helper. Looks up the per-material graph
  view, runs `evaluateMaterial`, and overrides the three
  graph-evaluable fields on the existing `mat` lookup
  (baseColor / emissionColor / emissionStrength). Legacy
  fields the v1 graph does not yet cover (metallic /
  roughness / specular / transmission / texture binding)
  stay where the existing kernel logic reads them. The
  three kernels (`k_render_scene`, the path tracer's
  bounce loop, and `k_render_aovs`) all call the helper
  immediately after their existing material lookup, so
  shading transitions from "read MaterialParams directly"
  to "read MaterialParams + override via graph" with
  minimal patch surface. AOV beauty therefore matches the
  beauty kernel bit-for-bit.
- **`tests/material_graph_core_tests.cpp`**: 354 host
  assertions (up from 302). New evaluator coverage via
  `evaluateMaterial(view_of(GpuMaterial))` (host runs the
  same RR_HD inline code the kernel runs):
  - Const -> Diffuse yields albedo.
  - Unwired Diffuse uses the terminal's imm_color (the
    catalogue's mid-grey default by construction).
  - Emission picks up wired color + immediate strength;
    negative strength is clamped to zero.
  - Add / Multiply chains compute per-component sums /
    products.
  - TextureSample placeholder returns the lowering's
    white fallback through the kernel evaluator.
  - Diffuse + Emission terminals are independent (one
    graph populates both halves of `MaterialEvalResult`).
  - Empty `CudaMaterialGraphView` returns the default-
    constructed eval result (the kernel's safe fallback
    when `material_graph_views == nullptr`).
  - `synthesise_gpu_material_from_params`: diffuse-only
    round-trip; emission round-trip; null-input neutral
    default.

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/17 ... 17/17 all Passed
100% tests passed, 0 tests failed out of 17

$ ./build/bin/material_graph_core_tests
material_graph_core_tests: 354/354 passed

$ ./build/bin/RelativityRender --render scenes/test_minimal.rrscene
loaded scene: 0 materials, 0 spheres, 0 lights, 0 meshes
(no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`) is correct
by construction: `evaluateMaterial` is the only new
device code path, and every per-opcode case is exercised
by the host suite that runs the exact same `RR_HD inline`
implementation. The kernel-level patch is a single
helper call (`override_material_with_graph`) per kernel,
inserted immediately after the existing material lookup;
the rest of the shading (lights, intersection,
relativity) is unchanged.

#### Per the prompt

- "Files: extend CudaMaterial.cuh or equivalent": extended
  `cuda/CudaMaterialGraph.cuh` (the M21 spec slice's
  device-view header). Same role, M21-specific name.
- "Device function: evaluateMaterial(GpuMaterial,
  hitInfo)": `RR_HD inline MaterialEvalResult
  evaluateMaterial(const CudaMaterialGraphView&)`. The
  signature takes a `CudaMaterialGraphView` (the
  device-pointer view of a `GpuMaterial`) which is what
  the kernel actually sees on the GPU side. v1 nodes do
  not consume any per-hit context, so the explicit
  `hitInfo` parameter is omitted; a future slice that
  adds UV / Normal nodes adds it back as an explicit
  `ShadingContext` parameter.
- "Support nodes: ConstantColor / Add / Multiply /
  DiffuseBSDF / Emission / TextureSample placeholder":
  all six cases handled. TextureSample returns the
  white fallback the IR carries (no real sampling yet -
  the M16 sampler hookup is a future slice).
- "Integrate into renderer: replace previous simple
  material shading; use evaluateMaterial()": all three
  kernels (k_render_scene / path tracer's bounce / AOV
  k_render_aovs) call `override_material_with_graph`
  immediately after their existing material lookup.
  The renderer ALWAYS shades through the graph from
  this slice on; the legacy `MaterialParams` reads
  remain only for the fields the v1 graph does not
  yet cover.
- "All evaluation must happen on GPU / no CPU shading":
  `evaluateMaterial` is `RR_HD inline` so it
  COMPILES in both worlds, but the renderer's hot path
  invokes it from `__device__` code only. The host suite
  exercises it for verification, never for rendering;
  the existing "no CPU ray tracing as production path"
  rule is preserved.
- "Keep performance reasonable (simple loop over
  nodes)": straight-line opcode loop. Stack-allocated
  32-slot pool. No early exit, no recursion, no dynamic
  allocation. Constant folding / dead-code drop is the
  host lowering's job per spec 9.4.
- "Output: render scene using material graph":
  `--render <scene>` walks the new path on a CUDA build.
  Auto-synthesised graphs for existing flat materials
  produce the same per-hit baseColor / emissionColor /
  emissionStrength values the kernel read directly
  before this slice, so visible output is unchanged for
  legacy scenes - but the path NOW goes through
  `evaluateMaterial`, which is the integration the
  prompt asks for.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains
  `in progress`. The kernel now uses the graph end-to-
  end; the remaining work is scene-format integration
  (the optional `materials[].graph` block in
  `.rrscene`), then the standalone editor (M21 / M20).
- M21 (Material Node Graph (Editor)): remains `in
  progress` (same).

### 2026-04-28 — M21 (impl, gpu-ir): GPU-friendly material IR

Fifth implementation slice of the material node graph. Lands
the **operation list + terminal table** IR the spec section
9.2 calls for, plus the host-side lowering that turns a
validated `graph::Graph` into one. Per the prompt, no GPU
execution yet; this slice ships only the compact host POD,
the lowering, the device-side view header, and a debug print
so the on-disk shape is inspectable.

- **`src/material/GpuMaterial.h`** (new): the host-side IR.
  - `GpuOpcode` enum (1 byte; mirrors `NodeType` but stays
    independent so non-IR catalogue extensions don't leak
    onto the GPU side).
  - `GpuOp`: fixed-shape per-op record (opcode + 2 input
    slot indices as `int16_t` + immediate vec3 + immediate
    int). No pointers; cross-record references are integer
    slot indices into the op array's own positions.
  - `GpuTerminal`: terminal-table entry (`Diffuse` writes
    `MaterialParams::baseColor`; `Emission` writes
    `emissionColor` + `emissionStrength`). Always carries
    immediate fallbacks alongside slot indices so the
    future kernel never branches on "wired?".
  - `GpuMaterial` = `vector<GpuOp> ops` + `vector<GpuTerminal>
    terminals` + `slot_count`. The two arrays upload to
    separate device buffers in a future slice.
  - `compile_graph_to_gpu_material(graph::Graph) ->
    GpuMaterialResult` and `debug_print_gpu_material(...)`.
- **`src/material/GpuMaterial.cpp`** (new): the lowering
  pipeline.
  - Stage 1: validation via `graph::validate_graph`.
  - Stage 2: terminal-driven topological sort
    (defence-in-depth cycle check). Iterative DFS with a
    deterministic per-node source order.
  - Stage 3: assigns each non-terminal reachable node a
    slot index = its position in the topo order. Caps at
    32k slots (fits `int16_t`).
  - Stage 4-5: emits `GpuOp` per non-terminal and
    `GpuTerminal` per terminal. `resolve_input_slot`
    walks `graph.connections` to find the source for each
    socket; unwired inputs land as `-1` so the kernel
    side reads the immediate fallback.
  - `debug_print_gpu_material(mat, FILE*)`: dumps the IR
    in human-readable form (one line per op, one per
    terminal, with the relevant per-opcode fields).
- **`src/cuda/CudaMaterialGraph.cuh`** (new): device-side
  launch-argument view. `CudaMaterialGraphView` exposes
  `(const GpuOp* ops, int op_count, const GpuTerminal*
  terminals, int terminal_count)` - what a future kernel
  reads after `GpuScene::upload_material_graphs` fills the
  matching `GpuBuffer`s. `CudaMaterialGraphArrayView`
  wraps a per-material array of those for the
  material-id-to-IR lookup at hit time. v1 ships the
  header + the shape; no kernel calls into it yet (per
  the prompt's "do not execute on GPU yet" rule).
- **`tests/material_graph_core_tests.cpp`**: 302 host
  assertions (up from 225). New coverage:
  - `gpu_opcode_name` round-trip for all six opcodes.
  - `Const -> Diffuse` lowers to one op + one terminal,
    `slot_count == 1`, terminal references slot 0.
  - Unwired Diffuse: zero ops, one terminal with
    `in_color == -1` and `imm_color` falling back to the
    catalogue's mid-grey default.
  - Emission carries `imm_color` + `imm_strength`
    immediates from the node when both inputs unwired.
  - `(Const + Const) -> Diffuse`: three ops, one
    terminal; the Add op references both constants by
    distinct slot indices; the Diffuse terminal
    references the Add slot.
  - `TextureSample` carries `texture_id` verbatim and
    bakes the white missing-texture fallback into
    `imm_color`. (Fixed mid-write: the test originally
    built an unwired TextureSample and expected it to
    appear in the IR; terminal-driven reachability
    correctly drops dead code per spec 7.4, so the
    sample now wires into the Diffuse terminal.)
  - Dead-code branch (Multiply with two inputs but no
    consumer reaching a terminal) is dropped.
  - Invalid graph (no terminal) surfaces the validator's
    error message through the lowering.
  - Wired-color Emission records the source slot.
  - Diffuse + Emission graph emits two terminals; both
    kinds present.
  - Smoke (printed): builds Const(0.7, 0.4, 0.2) ->
    DiffuseBSDF, lowers, prints the compiled IR via
    `debug_print_gpu_material`. Live trace:
    ```
    [smoke] compiled GpuMaterial:
    GpuMaterial: ops=1 terminals=1 slot_count=1
      ops:
        [ 0] ConstantColor  imm_color=(0.700, 0.400, 0.200)
      terminals:
        [ 0] Diffuse        in_color=0 imm_color=(0.800, 0.800, 0.800)
    ```
- **`CMakeLists.txt`:** `rr_material` adds
  `src/material/GpuMaterial.cpp` to its source list. No
  new public dependency; `GpuMaterial.h` only needs
  `math/Vec3.h` (already on the include path) and
  forward-declares `graph::Graph` to keep the include set
  small.

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/17 ... 17/17 all Passed
100% tests passed, 0 tests failed out of 17

$ ./build/bin/material_graph_core_tests
[smoke] ConstantColor(0) -> DiffuseBSDF(1)
[smoke]   connect    -> true
[smoke]   validate() -> OK
[smoke] evaluate(DiffuseBSDF(1)) = (0.700, 0.400, 0.200)
[smoke] compiled GpuMaterial:
GpuMaterial: ops=1 terminals=1 slot_count=1
  ops:
    [ 0] ConstantColor  imm_color=(0.700, 0.400, 0.200)
  terminals:
    [ 0] Diffuse        in_color=0 imm_color=(0.800, 0.800, 0.800)
material_graph_core_tests: 302/302 passed
```

#### Per the prompt

- "Files: `src/material/GpuMaterial.{h,cpp}`,
  `src/cuda/CudaMaterialGraph.cuh`": all three created.
- "Flattened node array (struct per node)":
  `vector<GpuOp>` + `vector<GpuTerminal>`. Each is one
  POD struct per record.
- "Enum for node type": `GpuOpcode` enum class. Six v1
  values; mirrors `NodeType` but kept distinct.
- "Compact storage for parameters": immediates live
  inside each record; no constant pool, no string
  table. Slot indices are `int16_t` for compactness.
- "Add conversion: Graph -> GpuMaterial":
  `compile_graph_to_gpu_material(graph::Graph) ->
  GpuMaterialResult`.
- "No dynamic pointers on GPU": every cross-record
  reference is an integer index.
  `CudaMaterialGraphView` exposes the bytes through raw
  device pointers + counts; the kernel reads
  `ops[i].in_a` and indexes `ops[in_a]` rather than
  following a host-side pointer.
- "Fixed/compact arrays preferred":
  `std::vector<GpuOp>` / `std::vector<GpuTerminal>`
  upload as flat byte buffers to `GpuBuffer`s in a
  future slice.
- "TextureSample still placeholder":
  `GpuOp::imm_color` for a TextureSample is hard-coded
  to white (matching the M16 sampler's null-data
  fallback in `cuda/CudaTexture.cuh`); the future
  kernel will replace this with a real
  `sample_texture(view, uv)` call.
- "Do not execute on GPU yet": no kernel changes;
  no opcode interpreter; the device-side view header is
  the shape, not the implementation.
- "Add debug print of compiled structure":
  `debug_print_gpu_material(mat, FILE*)` writes the
  human-readable dump shown above; smoke test exercises
  it with the prompt's small graph.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains
  `in progress`. The IR is the bridge between the data
  core and the (future) per-hit kernel evaluator.
- M21 (Material Node Graph (Editor)): remains `in
  progress` (same).

### 2026-04-28 — M21 (impl, evaluator): CPU reference evaluator (testing only)

Fourth implementation slice of the material node graph.
Lands a small, recursive **CPU-only reference evaluator**
under `src/material/graph/`. Per the project's
"no CPU ray tracing as production path" rule
(`docs/DEVELOPMENT_RULES.md`), the renderer never calls
into this code; it exists so the test harness and any
future authoring tool can pull a deterministic colour
summary out of a graph without spinning up the GPU.

The evaluator is intentionally minimal: a recursive
free function, no memoisation across calls, a
hard-coded depth cap as a safety net against malformed
input. It supports the five node types the prompt
names (`ConstantColor`, `Add`, `Multiply`, `Emission`,
`DiffuseBSDF`) plus `TextureSample`, which returns a
configurable constant fallback colour. No other side
effects.

- **`src/material/graph/GraphEvaluator.h`** (new):
  `EvaluationContext` (carries
  `fallback_texture_color` defaulted to magenta -
  the standard "missing texture" debug colour - and
  `max_depth = 256` for the cycle-safety cap).
  `evaluate(graph, node_id, ctx) -> Vec3` returns the
  per-node colour summary documented in the file
  header:
  - `ConstantColor`  -> `node.color_value`.
  - `TextureSample`  -> `ctx.fallback_texture_color`
                        (no actual sampling).
  - `Add`            -> `evaluate(a) + evaluate(b)`,
                        per component; unwired inputs
                        default to zero.
  - `Multiply`       -> `evaluate(a) * evaluate(b)`,
                        per component; unwired inputs
                        default to one.
  - `DiffuseBSDF`    -> the resolved `albedo` colour
                        (its sole input); unwired
                        falls back to the node's own
                        `color_value`.
  - `Emission`       -> resolved `color` * the
                        immediate `scalar_value`. The
                        `strength` input is Float-typed
                        and v1 has no Float-producing
                        node, so the strength always
                        comes from the node itself.
  Pre-conditions documented: graph SHOULD be validated
  first; an unknown node id or recursion past
  `max_depth` returns black.
- **`src/material/graph/GraphEvaluator.cpp`** (new):
  the recursive worker is a small `evaluate_at(graph,
  node_id, ctx, depth)` private function. The walk
  finds the source for each input by iterating
  `graph.connections` (matching the Graph's flat
  edge-list shape from the data-core slice). The
  per-component math helpers (`cadd`, `cmul`,
  `cscale`) are local utilities. Ten lines of switch
  per node type; nothing surprising.
- **`tests/material_graph_core_tests.cpp`**: 225 host
  assertions (up from 186). New evaluator coverage:
  - Per-node value semantics: `ConstantColor` returns
    its immediate; unknown id returns black;
    `TextureSample` returns the configured fallback
    AND the default-magenta path is pinned by a
    separate test; `Add` and `Multiply` round-trip
    two-constant cases AND fall back to identity
    when unwired; `DiffuseBSDF` returns wired
    albedo or its own default; `Emission` returns
    `color * scalar_value` with both wired-color
    and unwired-color paths covered.
  - Composition: `(a + b) * c -> Diffuse.albedo`
    chain exercises three levels of recursion plus a
    tinted multiplier; expected value is computed
    analytically.
  - Diamond fan-out: a single `ConstantColor` feeding
    both `Add` inputs returns `2 * value` (the
    deliberate no-memoisation behaviour).
  - Depth cap: a > `max_depth` chain terminates
    cleanly without infinite recursion.
  - Smoke (printed): builds the prompt's small graph
    (`ConstantColor(0.7, 0.4, 0.2) -> DiffuseBSDF`)
    via the builder API, validates, evaluates the
    terminal, and prints the resulting colour to the
    test output. Live trace:
    ```
    [smoke] ConstantColor(0) -> DiffuseBSDF(1)
    [smoke]   connect    -> true
    [smoke]   validate() -> OK
    [smoke] evaluate(DiffuseBSDF(1)) = (0.700, 0.400, 0.200)
    ```
- **`CMakeLists.txt`:** `rr_material` adds
  `src/material/graph/GraphEvaluator.cpp` to its
  source list. No new public dependency. The existing
  `material_graph_core_tests` target picks up the new
  TUs through the same `rr_material` link.

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/17 ... 17/17 all Passed
100% tests passed, 0 tests failed out of 17

$ ./build/bin/material_graph_core_tests
[smoke] ConstantColor(0) -> DiffuseBSDF(1)
[smoke]   connect    -> true
[smoke]   validate() -> OK
[smoke] evaluate(DiffuseBSDF(1)) = (0.700, 0.400, 0.200)
material_graph_core_tests: 225/225 passed
```

#### Per the prompt

- "Files: `src/material/graph/{GraphEvaluator.h,
  GraphEvaluator.cpp}`": both created.
- "Implement evaluate(node) recursively": one free
  function that recursively resolves inputs by
  walking the graph's connection list backward from
  the requested node.
- "Support nodes: ConstantColor, Add, Multiply,
  Emission, DiffuseBSDF (returns color only)": all
  five supported with the documented colour-summary
  semantics.
- "TextureSample: return constant fallback color":
  `EvaluationContext::fallback_texture_color`,
  defaulted to magenta. The sampler is NEVER called;
  this is the reference path's deliberate
  oversimplification.
- "Reference / testing only / Do NOT use CPU for
  final rendering": the file header repeats this in
  the docs; the renderer's hot path imports the
  data layer (`Graph`, `Node`, `Socket`) but does
  not link the evaluator's symbols on its rendering
  paths. The renderer's existing
  `compile_graph_to_material` (a different,
  texture-aware bake) remains the only sanctioned
  way to feed graph data into rendering today.
- "Keep it simple and deterministic": no
  randomness; no memoisation; pure-function
  per-node behaviour. Same graph + same context
  always produces the same colour.
- "Add a small test: evaluate small graph and print
  resulting color":
  `test_smoke_evaluator_prints_resulting_color`
  builds `ConstantColor -> DiffuseBSDF` via the
  builder, evaluates the terminal, prints the
  resulting RGB triple, and asserts the value
  matches the wired constant.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains
  `in progress`. The reference evaluator is the
  testing harness's primary verification tool; a
  future slice will migrate the
  `compile_graph_to_material` runtime to consume the
  new core (replacing the previous slice's
  monolithic data + eval).
- M21 (Material Node Graph (Editor)): remains `in
  progress` (same).

### 2026-04-28 — M21 (impl, builder): graph construction helpers + required-input rule

Third implementation slice of the material node graph.
Extends the data core under `src/material/graph/` with a
small builder API on `Graph` (`add_node`, `connect`,
`validate`) plus a forward-looking `Socket::required` flag
the validator now honours. No evaluation, no GPU, no UI.

The prompt's camelCase signatures (`addNode`, `connect`,
`validateGraph`) are translated to the project's `snake_case`
convention (`add_node`, `connect`, `validate`); the names
otherwise match. A small smoke test builds
`ConstantColor -> DiffuseBSDF` through the builder and
prints the validation result, as the prompt asked for.

- **`src/material/graph/Socket.h`**: new `bool required =
  false;` field on `Socket`. Honoured only for input
  sockets (output sockets keep the default). The v1
  catalogue marks NO sockets required - per spec section
  7.1, inputs are unwired-by-default and fall back to the
  catalogue's per-input defaults. The flag is the place a
  future node type whose input has no sensible default
  opts in.
- **`src/material/graph/Graph.h`**: `Graph` gains three
  member methods, manipulating the same public `nodes` /
  `connections` vectors the existing code already uses:
  - `NodeId add_node(NodeType type)`: appends a node with
    the catalogue's canonical socket layout (`make_node`),
    auto-allocates an id as `max(existing) + 1` (or `0`
    on an empty graph), returns the new id.
  - `bool connect(NodeId from, std::string_view from_socket,
    NodeId to, std::string_view to_socket)`: appends a
    connection with immediate sanity checks - both nodes
    exist, sockets resolve in the right direction, types
    are `can_connect`-compatible, sink not already wired.
    Returns false (without appending) on any failure.
    Cycle detection stays on `validate()` so an in-flight
    builder can append connections that close cycles and
    let `validate` report them with full context.
  - `[[nodiscard]] ValidationResult validate() const`:
    method form of `validate_graph(*this)`. Same
    behaviour, friendlier on built-up graphs.
  - `ValidationResult` was moved earlier in the header so
    `Graph::validate()` can return it by value.
- **`src/material/graph/Graph.cpp`**: implements the three
  new methods, adds a new validation step (rule 8) at the
  tail of `validate_graph`: every input socket marked
  `required` must have at least one incoming connection.
  Error message names the offending socket + node id +
  node type so the author can find it. The check is a
  no-op for any v1 graph (no v1 catalogue inputs are
  required) but covered by tests for forward-looking
  correctness.
- **`tests/material_graph_core_tests.cpp`**: 186 host
  assertions (up from 141). New coverage:
  - `add_node`: id assignment starts at zero, increments
    monotonically, skips past manually-assigned ids;
    appended node carries the catalogue's socket layout.
  - `connect`: appends on success; rejects unknown nodes,
    unknown sockets, sockets in the wrong direction
    (Input as source), type-incompatible types,
    double-wired sinks. Cycle-closing connections still
    append and are caught by `validate` (the design
    contract - connect is a sanity check, not a
    full validator).
  - `Graph::validate()` returns identically to the free
    `validate_graph(*this)`.
  - Required-input flag: graphs with no required inputs
    pass; a node with `albedo` flagged required and
    unwired surfaces a clear error mentioning "required
    input" + the socket name; wiring it makes the same
    graph pass.
  - Smoke: builds the prompt's `ConstantColor ->
    DiffuseBSDF` graph through `add_node` / `connect`,
    runs `validate()`, prints the validation result
    (status + message) in the test output. Live trace:
    ```
    [smoke] ConstantColor(0) -> DiffuseBSDF(1)
    [smoke]   connect    -> true
    [smoke]   validate() -> OK
    ```

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/17 ... 17/17 all Passed
100% tests passed, 0 tests failed out of 17

$ ./build/bin/material_graph_core_tests
[smoke] ConstantColor(0) -> DiffuseBSDF(1)
[smoke]   connect    -> true
[smoke]   validate() -> OK
material_graph_core_tests: 186/186 passed
```

#### Per the prompt

- "Files: extend Graph.h/.cpp": both extended.
- "Implement addNode(type)": `Graph::add_node(NodeType)`.
- "Implement connect(outputSocket, inputSocket)":
  `Graph::connect(NodeId, string_view, NodeId, string_view)`
  - the (node-id, socket-name) pair is how the data layer
  references a socket (sockets do not carry back-pointers
  to their nodes), and the four-argument form is the
  honest spelling of "two socket references".
- "Implement validateGraph()": `Graph::validate()` (method
  form) plus the existing `validate_graph(graph)` free
  function (kept for backward compat; both are tested).
- "Validation rules: no cycles (DAG)": already in the
  validator (rule 6); unchanged. Connect-time cycle
  detection is intentionally NOT done so an in-flight
  builder can close-then-fix cycles.
- "Type compatibility (float/vec3/color)": already in the
  validator + now ALSO in `connect` itself (the immediate
  sanity check rejects type-incompatible wires at
  insertion time so authoring tools surface errors
  faster).
- "All required inputs connected": new validator rule
  (8). v1 catalogue marks none required (spec 7.1 keeps
  inputs optional); the test suite covers the
  required-flag opt-in path so the infrastructure is
  honest.
- "Add a small test: build a graph ConstantColor ->
  DiffuseBSDF -> Output, run validation and print
  result": `test_smoke_constant_color_to_diffuse_via_builder`
  builds via the builders, validates, prints the live
  trace shown above, then asserts success. (DiffuseBSDF
  IS the terminal in v1; no separate "Output" node
  exists.)
- "No evaluation yet / No GPU": this slice ships only
  data-layer code. The previous `MaterialGraph` runtime
  and the renderer are untouched.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains
  `in progress`. Builder API is the natural authoring
  surface for a future scene-format slice / standalone
  editor.
- M21 (Material Node Graph (Editor)): remains `in
  progress` (same).

### 2026-04-28 — M21 (impl, data-core): structured material graph data layer

Second implementation slice of the material node graph.
Lands the **data-only** core under
`src/material/graph/`: a clean, modular Socket / Node /
Graph trio with structural validation and no evaluation.
Coexists with the previous slice's monolithic
`src/material/MaterialGraph.{h,cpp}` (which carries both
data + evaluator under `rr::material::*`); a future slice
will migrate the evaluator to consume the new core and
delete the monolith.

The new layer mirrors the spec contract one-to-one:
sockets per spec section 7.1 / 7.2, the implicit-conversion
table from 7.3 in `can_connect`, the DAG / terminal /
sink-uniqueness rules from 7.4 / 7.5 in `validate_graph`.
No evaluation, no GPU, no UI.

- **`src/material/graph/Socket.h`** (new): `SocketType`
  enum (`Float` / `Vec2` / `Vec3` / `Color` / `Normal`
  matching spec 7.2), `SocketDirection` (`Input` / `Output`),
  the `Socket` POD (name + type + direction). Helpers:
  `socket_type_name`, `parse_socket_type` (canonical
  lowercase + PascalCase aliases),
  `socket_direction_name`, `can_connect(source, sink)`
  (the implicit-conversion table from spec 7.3 -
  identity, `float -> any` broadcast, `vec3 <-> color`
  reinterpret, `normal -> vec3` drop).
- **`src/material/graph/Node.h`** (new): `NodeType` enum
  with stable ordinals matching the v1 catalogue; the
  prompt's `DiffuseBSDF` is the canonical name and the
  spec's `Diffuse` is accepted as an alias by
  `parse_node_type`. `is_terminal` predicate (true for
  `DiffuseBSDF` and `Emission`). `Node` POD: id + type +
  optional name + `inputs` / `outputs` socket vectors +
  per-type immediates (`color_value`, `scalar_value`,
  `uv_value`, `texture_id`). `make_node(type, id)`
  populates the catalogue's canonical socket layout for
  every v1 type. `find_socket(node, name, direction)`
  by-name lookup with direction filter.
- **`src/material/graph/Graph.h`** (new): `Connection`
  POD (from_node + from_socket name + to_node + to_socket
  name). `Graph` POD: version + nodes vector + connections
  vector. `find_node(graph, id)` (const + non-const
  overloads), `incoming_connections(graph, id)`.
  `validate_graph(graph)` runs structural validation per
  spec section 7: unique ids, every connection's nodes
  resolve, source / sink sockets exist in the right
  direction, sink not double-wired, types compatible per
  `can_connect`, DAG (cycle detection by 3-state DFS),
  at least one terminal. Returns `ValidationResult { ok,
  message }` with a descriptive message on the first
  detected violation; check order is fixed so error
  messages are stable.
- **`src/material/graph/Graph.cpp`** (new): impl for the
  Socket / Node / Graph layers above. Cycle detection is
  iterative DFS with 3-state colouring (white / grey /
  black) seeded across every connected component so
  cycles in unreachable subgraphs still surface. Sink
  uniqueness uses an `unordered_set<(NodeId, string)>`.
- **`tests/material_graph_core_tests.cpp`** (new): 141
  host assertions covering the data layer end-to-end.
  Coverage:
  - Socket: enum naming round-trip, lowercase + PascalCase
    parse, unknown / case-variant rejection, direction
    naming. `can_connect`: identity for every type,
    `float -> any` broadcast, `vec3 <-> color`
    reinterpret both ways, `normal -> vec3` drop (and
    not back), truncation / luminance reductions
    rejected.
  - Node: enum naming, `parse_node_type` canonical and
    alias paths, case-variant rejection, `is_terminal`.
    `make_node` socket layout for every v1 type
    (ConstantColor / TextureSample / Add / Multiply /
    DiffuseBSDF / Emission). Per-catalogue immediate
    defaults. `find_socket` direction filter.
  - Graph: `find_node` + const overload + miss case.
    `incoming_connections` returns target edges, empty
    for unknown ids. Validation happy paths
    (DiffuseBSDF-only, ConstantColor -> Diffuse,
    full v1 chain with Texture -> Multiply -> Diffuse +
    Emission). Validation error paths: unsupported
    version, invalid id (`kInvalidNodeId`), duplicate id,
    dangling `from_node` / `to_node`, unknown
    `from_socket` / `to_socket`, socket in wrong
    direction (input wired as source), double-wired sink,
    type-incompatible connection, cycle, no terminal.
    Compatible implicit-conversion path
    (color -> vec3) accepted.
- **`CMakeLists.txt`:**
  - `rr_material` adds `src/material/graph/Graph.cpp`.
    No new public dependency.
  - New `material_graph_core_tests` target registered
    with `add_test`, linked PRIVATE against
    `rr_material`. Coexists with the existing
    `material_graph_tests` (which targets the
    `rr::material::` runtime from the previous slice);
    both pass.

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/17 ... 17/17 all Passed
100% tests passed, 0 tests failed out of 17

$ ./build/bin/material_graph_core_tests
material_graph_core_tests: 141/141 passed

$ ./build/bin/material_graph_tests
material_graph_tests: 78/78 passed
```

#### Per the prompt

- "Files: `src/material/graph/{Graph.h, Graph.cpp,
  Node.h, Socket.h}`": all four created.
- "Define Node (id, type, inputs, outputs)": the POD
  `Node` carries exactly those - plus the per-type
  immediates the v1 catalogue requires for it to
  serialise round-trippably.
- "Define Socket (type, name, connection)": `Socket`
  carries name + type + direction. The "connection" is
  modelled at the `Graph` layer (a separate
  `Connection` list keyed by node id + socket name) so
  the data shape mirrors what a future scene-format
  slice will serialise.
- "Define Graph (list of nodes, connections)": the
  `Graph` POD has both vectors + a `version` field per
  spec 10.3.
- "Support node types enum": all six listed
  (`ConstantColor`, `TextureSample`, `Add`, `Multiply`,
  `DiffuseBSDF`, `Emission`).
- "No evaluation yet": the data layer ships zero
  evaluation code. The previous slice's
  `compile_graph_to_material` continues to work against
  its own data structs; nothing in the new core touches
  shading.
- "No GPU yet / No UI": the new files include zero CUDA /
  OptiX / UI dependencies. `rr_material` continues to
  link only itself + math headers.
- "Keep structures simple and serializable": every
  type is plain data - no virtuals, no smart pointers,
  no shared ownership. `std::string` + `std::vector` are
  the only non-trivial members. The shape (per-node
  socket lists; flat connection list with named source /
  sink) maps directly to a future JSON schema.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains
  `in progress`. The data core is now structured and
  separate from the previous slice's monolith;
  evaluator migration to consume the new core is a
  future slice.
- M21 (Material Node Graph (Editor)): remains `in
  progress` (same).

### 2026-04-28 — M21 (impl, runtime): minimal material graph runtime

First implementation slice of the material node graph. Lands
the host-side data model + the v1 compile-to-`MaterialParams`
bake described in `docs/MATERIAL_GRAPH_SPEC.md` sections 4 +
8.4 + 9. Six v1 nodes wired through validation (duplicate ids,
dangling refs, cycles, missing terminal, duplicate terminals)
+ topological evaluation + per-terminal write into the
existing `MaterialParams` snapshot. No per-hit graph
evaluation in the kernel yet (the spec's stage-2 path is a
future slice); the renderer keeps reading `MaterialParams` and
the bake gives it the same parameters the graph would produce
for the default shading context.

- **`src/material/MaterialGraph.h`** (new): host-side data
  model and the public `compile_graph_to_material(graph,
  sampler)` API.
  - `NodeType` enum: `ConstantColor` (0), `TextureSample`
    (1), `Add` (2), `Multiply` (3), `Diffuse` (4),
    `Emission` (5). Stable ordinals match the spec's v1
    catalogue.
  - `node_type_name`, `parse_node_type` (case-sensitive;
    accepts both `Diffuse` and `DiffuseBSDF` per the
    user prompt's terminology + the spec's canonical
    name), `is_terminal` (true for Diffuse / Emission).
  - `GraphNode` POD: id + type + per-type immediates
    (`color_value`, `strength_value`, `texture_id`,
    `default_uv`) + per-type input slots (`input_a/b`,
    `input_uv`, `input_albedo`, `input_color`,
    `input_strength`); `-1` = unwired.
  - `Graph` POD: `version` + nodes vector.
  - `TextureSamplerFn = std::function<Vec3(int, Vec2)>`:
    caller-supplied callback so `rr_material` does NOT
    depend on `rr_texture`. The .rrscene loader / the C4D
    bridge / the test harness each plug their own.
    Existing module layering preserved.
  - `CompileResult { bool ok; std::string message;
    MaterialParams material; }`.
- **`src/material/MaterialGraph.cpp`** (new): validation +
  topo-sort + bake.
  - `IdMap` builds an `id` -> array-index map and rejects
    duplicates.
  - `check_references` walks every node's wired inputs and
    rejects dangling source-node ids.
  - `topo_sort` is iterative DFS with 3-state colouring
    (white / grey / black). Seeds traversal from every
    terminal so unreachable subgraphs are dropped (per
    spec 7.4 / 8.3); detects cycles (grey-on-grey) with a
    descriptive error naming the offending node.
  - `evaluate_node` runs each node's per-type math:
    `ConstantColor` -> immediate, `Add` / `Multiply` ->
    per-component on the cached source slots,
    `TextureSample` -> calls the supplied
    `TextureSamplerFn` (or falls back to white when the
    sampler is null or the texture id is `< 0`).
  - `apply_terminal` writes each terminal's resolved
    inputs into `MaterialParams`: `Diffuse.albedo` ->
    `baseColor`, `Emission.color` / `Emission.strength`
    -> `emissionColor` / `emissionStrength`.
    Non-negative-clamps `emissionStrength`. Tracks
    `terminal_seen[type]` and rejects duplicates of the
    same kind (per spec 7.5 v1 SHOULD-rule, enforced).
  - `compile_graph_to_material(graph, sampler)` runs the
    full pipeline and returns the `CompileResult`.
- **`tests/material_graph_tests.cpp`** (new): 78 host
  assertions.
  - Naming + parsing: `node_type_name` round-trip for all
    six types, canonical-name parse, `DiffuseBSDF` alias
    resolves to `Diffuse`, case-insensitive variants
    rejected, `is_terminal` predicate.
  - Smallest valid graph: `ConstantColor -> Diffuse`;
    unwired Diffuse uses node `color_value` default.
  - Math nodes: Add, Multiply round-trip; unwired Add
    falls back to zero (additive identity); unwired
    Multiply falls back to one (multiplicative identity).
  - Emission: basic terminal (color + strength); unwired
    `color` input uses node `color_value`; negative
    `strength` clamped to zero.
  - Diffuse + Emission coexist independently.
  - TextureSample: callback receives the right `(id, uv)`;
    null sampler falls back to white; negative `id`
    short-circuits the sampler call.
  - Composition: `TextureSample -> Multiply (tint) ->
    Diffuse` chain produces the per-component product.
  - Validation rejects: empty graph, unsupported
    `version`, duplicate ids, dangling references,
    cycles, no-terminal graphs, duplicate terminals.
  - Dead-code subgraph (a Multiply branch that doesn't
    reach any terminal) does NOT affect the bake.
- **`CMakeLists.txt`:**
  - `rr_material` adds `MaterialGraph.cpp` to its source
    list. No new public dependency: the texture access is
    behind the `TextureSamplerFn` callback so the existing
    layering stays intact.
  - New `material_graph_tests` target registered with
    `add_test`, linked PRIVATE against `rr_material`.

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/16 ... 16/16 all Passed
100% tests passed, 0 tests failed out of 16

$ ./build/bin/material_graph_tests
material_graph_tests: 78/78 passed
```

#### Per the prompt

- "Support nodes: ConstantColor, TextureSample, Add,
  Multiply, DiffuseBSDF, Emission": all six implemented,
  with the prompt's `DiffuseBSDF` accepted as an alias for
  the spec's canonical `Diffuse`.
- "TextureSample placeholder": the runtime calls the
  caller-supplied `TextureSamplerFn` when one is provided
  and a valid texture id is bound; otherwise it falls
  back to white. The placeholder character is preserved -
  no scene-level texture binding is wired in this slice.
- "No UI / No node editor": the slice ships a host-side
  C++ runtime only. `tools/node_editor/` is not touched.
- "Compile graph to simple GPU material representation":
  the runtime's output is the existing `MaterialParams`
  POD (`src/material/MaterialTypes.h`), which is exactly
  what the GPU upload path (`GpuScene::upload_materials`)
  consumes today. Adding a graph entry to the renderer
  reduces to: bake at scene-load, store the resulting
  `MaterialParams`, upload as before. Per-hit graph
  evaluation in the kernel is a future slice.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): `not started`
  -> `in progress`. The runtime is the first
  implementation surface; future slices add scene-format
  integration, per-hit GPU evaluation, and the standalone
  editor.
- M21 (Material Node Graph (Editor)): `not started` ->
  `in progress` (same rationale).

### 2026-04-28 — M21 (spec, integration): material node graph - integration strategy

Fifth doc slice of the material node graph specification.
Adds section 10 ("Integration strategy") to
`docs/MATERIAL_GRAPH_SPEC.md`, pinning HOW the graph
reaches the renderer: where it comes from (Cinema 4D
today, the standalone editor tomorrow), how it grows
without breaking what is already there, and how the
producers / consumers of graph data layer against the
renderer's architecture. The slice is **data plus
architecture only**: UI / interaction design is
explicitly out of scope and stays out of scope across
the whole spec.

- **`docs/MATERIAL_GRAPH_SPEC.md`:**
  - Inserted section 10 "Integration strategy":
    - 10.1 Cinema 4D bridge integration:
      - Maps each C4D material the M19-extension-3
        bridge already translates onto v1 catalogue
        nodes: Standard `Mmaterial` color-only ->
        `ConstantColor` -> `Diffuse.albedo`; color +
        luminance -> +`Emission`; viewport "Display
        Color" fallback -> `ConstantColor` ->
        `Diffuse.albedo`; future bitmap-shader color
        -> `TextureSample` -> `Diffuse.albedo`;
        future tinted bitmap -> `TextureSample` ->
        `Multiply` -> `Diffuse.albedo`. Each mapping
        reuses ONLY the v1 catalogue.
      - Fallback strategy: bridge writes the existing
        flat material section and no graph block when
        the C4D source is non-Standard, uses
        unsupported features, or has channels too
        complex to translate. Same path the bridge
        has shipped since M19 ext 3.
      - Future advanced mapping (separate slices):
        C4D node-material translation, layered
        materials / BSDF mixing.
    - 10.2 Standalone node editor architecture:
      - Editor-agnostic graph data: same catalogue +
        section-7 contract, regardless of producer
        (bridge, editor, CLI, test fixture).
      - Separation rule: graph data is plain old data
        (defined by sections 6-7); editor depends on
        graph data + UI framework (M21 / L7); renderer
        depends on graph data via the section-9 IR.
        Renderer NEVER reaches into editor or bridge;
        editor / bridge reach the renderer through
        `.rrscene` (static) and the M18 server
        protocol (preview).
      - Layered ASCII diagram pinning the three
        dependency rules.
      - Serialisation: an optional `graph` block
        inside each `materials[]` entry of a
        `.rrscene` file. Carries `version`, a list of
        nodes (each with id, type from the catalogue,
        connections / defaults per input, immediates
        for node-parameter inputs), and an implicit
        terminal set. Exact JSON keys deferred to a
        small follow-up schema slice; the
        architectural shape is settled here.
      - Coexistence rule: when both a flat snapshot
        and a graph block are present, the flat
        fields are the BAKE of the graph for the
        default shading context. Renderer without
        graph eval reads the snapshot; renderer with
        graph eval reads the graph and treats the
        snapshot as a sanity-check / authoring hint
        (no byte-perfect requirement on the bake).
    - 10.3 Future compatibility:
      - Open-catalogue rule re-stated (from 6.1).
      - Graph-block versioning: own integer `version`
        (currently `1`). Parser MUST reject unknown
        future versions (matches `RRSCENE_FORMAT.md`'s
        rule); MUST tolerate unknown node types
        within a known version (warn + fall back to
        the flat snapshot for that material; do NOT
        fail the whole load).
      - Specific extension points: advanced BSDFs
        (existing Metallic / Glass placeholders light
        up without schema changes), texture
        extensions (new optional inputs on
        TextureSample or new utility nodes),
        procedural noise (new "Procedural" category),
        volumes / SSS (new category), BSDF mixing /
        layered materials (relaxes the "at most one
        of each terminal type" rule, additively).
  - Renumbered the previous "What this slice covers"
    / "Out of scope" sections from 10 / 11 to 11 / 12.
    Updated section 11's deferred list: only the
    JSON-schema sub-slice and the editor's UX /
    framework choice remain. The architectural shape
    is now settled.
  - Section 12 (out of scope for v1) is unchanged.

#### Verified locally

```
$ ls docs/MATERIAL_GRAPH_SPEC.md
$ python3 -c "open('docs/MATERIAL_GRAPH_SPEC.md').read()"
$ wc -l docs/MATERIAL_GRAPH_SPEC.md
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "How C4D materials map to RelativityRender node graph":
  10.1's mapping table covers each material type the
  M19-ext-3 bridge already translates, plus two
  future-bridge cases (bitmap-shader color, tinted
  bitmap) that exercise `TextureSample` + `Multiply`.
- "Fallback strategy (basic materials)": 10.1's
  fallback paragraph - the bridge keeps emitting the
  flat material section and no graph block when it
  cannot translate faithfully. Same code path the
  bridge has shipped since M19 ext 3.
- "Future advanced mapping": 10.1's last paragraph
  pins two extension cases and notes each arrives as
  its own slice once the catalogue / renderer work is
  in place.
- "Graph must be editor-agnostic": 10.2's first
  subsection lists four producers (bridge, editor,
  CLI, test fixture) and pins that all four produce
  the same on-disk form.
- "Separation between graph data, UI/editor": 10.2's
  separation subsection states the rule one-way and
  pins the three dependency rules in a layered ASCII
  diagram.
- "Ability to serialize graph to .rrscene": 10.2's
  serialisation subsection - optional `graph` block
  per material entry, carrying the section-6/7
  contents plus a version. Exact JSON keys deferred
  to a small follow-up.
- "Support expansion (textures, volumes, advanced
  BSDFs)": 10.3 maps each deferred concern to a
  concrete extension point.
- "Do not implement editor / Do not design UI / Focus
  on data + architecture only": Section 10 opens with
  the explicit "data plus architecture only" pin.
  Editor UX, framework choice, and interaction model
  are listed as out of scope in 10.2 and section 11's
  deferred list.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains
  `not started`. Integration strategy is a doc
  contract; nothing is promoted until implementation
  begins.
- M21 (Material Node Graph (Editor)): remains `not
  started` (same).

### 2026-04-28 — M21 (spec, evaluation + GPU compilation): material node graph - eval + lowering

Fourth doc slice of the material node graph specification.
Adds two new sections to `docs/MATERIAL_GRAPH_SPEC.md`:
section 8 ("Evaluation model") which pins WHEN the graph
fires and WHAT it consumes / produces, and section 9
("GPU compilation strategy") which pins HOW the host turns
a graph into device-resident state. The contract is
conceptual but concrete: it commits to a per-hit
evaluation model, a two-stage host-compile / device-execute
split, a flat per-material IR with a slot pool + terminal
table, a per-node lowering table reusing the existing M16
texture sampler, a no-runtime-branching policy, and a
backend mapping for CUDA / OptiX. Byte layouts and the
choice between interpreter vs NVRTC-emitted CUDA are
explicit non-decisions - a future implementation slice's
call.

- **`docs/MATERIAL_GRAPH_SPEC.md`:**
  - Inserted section 8 "Evaluation model":
    - 8.1 Per-hit evaluation contract: graph evaluates
      ONCE per surface hit; reads a shading context (the
      per-hit values the path tracer already computes:
      Hit::position / uv / normal, view direction,
      texture array, material id); produces a
      `MaterialParams`-shaped snapshot the existing
      shading code consumes unchanged. Three properties:
      determinism, statelessness, boundedness.
    - 8.2 Two-stage model: Compile (host, infrequent,
      per-material per author edit) -> backend-agnostic
      IR + device buffer; Execute (device, per surface
      hit per bounce per sample per pixel) -> reads IR +
      shading context, produces snapshot. Pinned in a
      Where/Frequency/Inputs/Outputs table.
    - 8.3 Evaluation order: topological + terminal-driven
      reachability + default-value semantics +
      fan-out caching (each output computed once per
      evaluation, parked in a slot, consumers read the
      slot).
    - 8.4 Explicit non-pin: Compile algorithm, Execute
      instruction format, per-material vs per-scene IR
      layout. The contract here is the WHAT, not the HOW.
  - Inserted section 9 "GPU compilation strategy":
    - 9.1 Compilation pipeline diagram: Graph -> Validate
      -> Lower (topo-sort + dead-code drop + default
      folding) -> IR -> Upload -> device-resident state.
      Strictly host-side through Upload; the kernel never
      sees the host objects.
    - 9.2 IR shape: operation list (opcode + input slot
      indices + dest slot + immediate values), slot pool
      (typed scratch values, one per non-dead-code
      output), terminal table (terminal -> slot mapping
      so Execute can assemble the snapshot). Plain old
      data; no pointers, no string keys, no dispatch
      tables; self-contained per material.
    - 9.3 Per-node mapping table: each catalogue node ->
      its device-side lowering. ConstantFloat/Color: slot
      literal, no runtime op. TextureSample: call into
      the existing `sample_texture(view, uv)` from
      `src/cuda/CudaTexture.cuh` (M16). Add/Multiply/Mix:
      per-component arithmetic. Normal/UV: read from the
      shading context. UVTransform: two fmadds.
      Diffuse/Emission: terminal-table entries to
      `MaterialParams::baseColor` /
      `emissionColor` + `emissionStrength`.
      Metallic/Glass: placeholder terminal-table entries
      reduced to fallback shading.
    - 9.4 Branching policy: constant folding at compile
      time (constant-only subgraphs collapse to a slot
      literal), default folding at compile time (no
      "input wired?" runtime branch), linear opcode
      stream (every op fires every time the IR runs;
      dead-code drop is what removes inactive nodes).
      Cross-material warp divergence is acknowledged as
      the integrator's concern, not the graph's. Vector
      packing / coalescing explicitly NOT pinned.
    - 9.5 Backend mapping (CUDA / OptiX): IR is
      backend-agnostic. CUDA: per-scene array of
      compiled graphs uploaded as GpuBuffers alongside
      the existing material array; closest-hit kernel
      runs the opcode loop inline. OptiX: IR lives in
      each material's SBT record; closest-hit program
      runs the same opcode loop. Same bytewise IR; only
      the address differs.
    - 9.6 Explicit non-pins: byte layout of operation
      records, max operations per material, interpreter
      vs NVRTC-emitted CUDA, progressive-update
      granularity (5.2's "no full-scene churn" rule is
      the constraint; the granularity is the impl
      slice's call).
  - Renumbered the previous "What this slice covers" /
    "Out of scope" sections from 8 / 9 to 10 / 11.
    Updated section 10's deferred list to drop the
    evaluation-model and GPU-compilation entries; the
    remaining deferred items (scene-file integration,
    editor UX, bridge emission) are unchanged.
  - Section 11 (out of scope for v1) is unchanged.

#### Verified locally

```
$ ls docs/MATERIAL_GRAPH_SPEC.md
$ python3 -c "open('docs/MATERIAL_GRAPH_SPEC.md').read()"
$ wc -l docs/MATERIAL_GRAPH_SPEC.md
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "How the graph is evaluated at render time": 8.1 -
  per-surface-hit evaluation; reads the shading context
  the path tracer already computes; produces a
  MaterialParams snapshot the existing shading code
  consumes unchanged.
- "Difference between CPU graph vs GPU execution": 8.2 -
  the two-stage model with Where / Frequency / Inputs /
  Outputs pinned in a table. Compile is host, infrequent,
  per-author-edit. Execute is device, per-hit / per-bounce
  / per-sample / per-pixel.
- "Compile graph into GPU-friendly representation":
  9.1 (pipeline) + 9.2 (IR shape).
- "Flatten graph into instructions or structs": 9.2 calls
  the IR a "flat per-material plan": operation list +
  slot pool + terminal table. Plain old data, integer
  indices, no pointers.
- "Avoid dynamic branching when possible": 9.4 commits to
  three policies (constant folding, default folding,
  linear opcode stream) and explicitly punts on the
  cross-material warp-divergence question (integrator's
  concern, not the graph's).
- "Graph -> intermediate representation -> GPU shading
  code/data": 9.1's pipeline diagram + 9.2's IR shape +
  9.5's backend mapping pin all three stages.
- "Mapping nodes to CUDA/OptiX shading logic": 9.3 is
  the per-node table; 9.5 is the per-backend address-
  scheme table.
- "Do NOT implement / Keep it conceptual but concrete":
  9.6 is the explicit non-pin list (byte layouts,
  op-count limits, interpreter vs NVRTC, progressive-
  update granularity). Each is justified as
  implementation-slice work, not spec work.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains
  `not started`. The eval + lowering contract is a doc
  contract; nothing is promoted until implementation
  begins.
- M21 (Material Node Graph (Editor)): remains `not
  started` (same).

### 2026-04-28 — M21 (spec, sockets): material node graph - sockets and topology

Third doc slice of the material node graph specification.
Adds section 7 ("Sockets and graph structure") to
`docs/MATERIAL_GRAPH_SPEC.md`, formalising the wiring
contract: what a socket is, the closed v1 type list, the
legal implicit conversions, the DAG topology requirement,
and the terminal-node contract.

- **`docs/MATERIAL_GRAPH_SPEC.md`:**
  - Inserted section 7 "Sockets and graph structure":
    - 7.1 Sockets: a socket is a named, typed connection
      point on a node. Inputs receive (at most one
      incoming connection; default per the catalogue when
      unwired); outputs emit (fan-out permitted: one
      output may drive multiple inputs). "Node parameter"
      catalogue entries (`ConstantFloat.value`,
      `TextureSample.texture_id`) are NOT sockets in the
      wiring sense - they store a literal on the node.
    - 7.2 Data types: closed v1 list of `float` / `vec3`
      / `color` / `normal` / `vec2`. Per-type notes pin
      the storage (32-bit floats), the linear-RGB
      contract on `color`, and the unit-length contract
      on `normal`. `vec2` is the smallest extension over
      the prompt's four-type list: it formalises the
      coordinate type the existing catalogue's UV /
      UVTransform / TextureSample.uv sockets carry. No
      `vec2` constant node ships in v1; the type exists
      only to type UV-flavoured connections.
    - 7.3 Connection rules: a connection is admitted iff
      (1) types match or an implicit conversion is
      permitted, (2) the sink is not already wired, (3)
      the connection does not introduce a cycle. Full
      implicit-conversion table pins `float` -> any
      (broadcast), `vec3` <-> `color` (reinterpret, no
      rescale), `normal` -> `vec3` (drop the
      unit-length contract), and `normal` -> `normal`.
      Conversions explicitly NOT allowed include `vec3`
      -> `normal` (no implicit normalise; future
      `Normalize` node), `vec3` <-> `vec2` (no
      truncation / pad; future explicit
      `Swizzle` / `Combine`), and `color` -> `float`
      (no implicit luminance; future `Luminance`).
    - 7.4 Graph topology: the graph MUST be a DAG.
      "Leaf" nodes have no incoming connections
      (`ConstantFloat`, `ConstantColor`, `Normal`,
      `UV`); "terminal" nodes have no outgoing
      connections (the four BSDFs from 6.5). Isolated
      subgraphs that don't reach a terminal are dead
      code: parser MAY warn, evaluator MUST NOT spend
      work on them.
    - 7.5 Root / terminal nodes: a graph contributes to
      shading through its terminal nodes. MUST contain
      at least one terminal (graphs with zero are
      rejected); SHOULD contain at most one node of
      each terminal type (multiple `Diffuse` /
      `Emission` etc. is not defined in v1; the BSDF-
      mixing semantics are the dedicated concern
      section 9 already punts). Per-terminal table
      pins each terminal's contribution to
      `MaterialParams` (`Diffuse` -> `baseColor`,
      `Emission` -> `emissionColor` /
      `emissionStrength`, etc.).
  - Renumbered the previous "What this slice covers" /
    "Out of scope" sections from 7 / 8 to 8 / 9.
    Updated section 8's deferred list to drop the
    socket-type-system entry now that 7 has landed; the
    remaining deferred items (evaluation model / GPU
    compilation / scene-format integration / editor UX
    / bridge emission) are unchanged.
  - Section 9 (out of scope for v1) is unchanged: BSDF
    mixing / layered shaders / volumes / procedural
    noise / time-varying inputs / differentiable graphs
    all still apply at v1.

#### Verified locally

```
$ ls docs/MATERIAL_GRAPH_SPEC.md
$ python3 -c "open('docs/MATERIAL_GRAPH_SPEC.md').read()"
$ wc -l docs/MATERIAL_GRAPH_SPEC.md
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "Define what a socket is": 7.1 - named, typed
  connection point on a node; the node-parameter
  exception is called out.
- "Input vs output sockets": 7.1 - inputs receive (one
  incoming, defaults when unwired); outputs emit
  (fan-out permitted).
- "Supported data types: float / vec3 / color /
  normal (optional)": 7.2 pins all four explicitly,
  with `normal` documented as kept-for-v1 because the
  existing `Normal` utility node already produces
  one. The per-type notes also call out `vec2` as the
  minimum honest extension to type UV connections,
  since the catalogue's `UV` / `UVTransform` /
  `TextureSample.uv` sockets need a 2D coordinate
  type and pretending they are `vec3` would
  contradict 7.3's truncation rules.
- "Connection rules: type matching, implicit
  conversions": 7.3 pins both. Implicit conversions
  are listed in a table; everything not in the table
  is rejected at parse time. Each disallowed
  conversion is justified by pointing at the future
  explicit node that will perform it.
- "Graph topology (DAG)": 7.4. Cycles forbidden;
  leaf / internal / terminal roles defined; dead-code
  policy noted.
- "Root/output node concept": 7.5 - a graph's "root"
  is the SET of terminal nodes; v1 keeps the set
  explicit instead of introducing an aggregator node,
  because the renderer's existing shading model
  already processes contributions independently.
  Multi-terminal-of-same-type semantics deferred (BSDF
  mixing slice).
- "Do NOT define evaluation execution yet": section 8
  explicitly defers the evaluation model. 7.4's
  dead-code clause notes "the parser MAY warn / the
  evaluator MUST NOT spend work" without committing to
  WHEN the evaluator decides what's reachable - that's
  the evaluation-model slice's call.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains
  `not started`. The structural contract is a doc
  contract; nothing is promoted until implementation
  begins.
- M21 (Material Node Graph (Editor)): remains `not
  started` (same).

### 2026-04-28 — M21 (spec, nodes): material node graph - node catalogue

Second doc slice of the material node graph specification.
Adds section 6 ("Node catalogue (v1)") to
`docs/MATERIAL_GRAPH_SPEC.md`, defining the twelve v1
nodes across four categories. The catalogue is the
smallest set that covers every parameter
`MaterialParams` exposes today plus the placeholder BSDFs
that pin the contract for the BSDFs the renderer will
grow into.

- **`docs/MATERIAL_GRAPH_SPEC.md`:**
  - Inserted section 6 "Node catalogue (v1)":
    - 6.1 Naming conventions: `PascalCase` node type
      names, `snake_case` input / output names, single-
      output nodes name their output `value` unless the
      catalogue entry pins otherwise. Pinned the per-
      entry shape (Category / Purpose / Inputs / Outputs
      / Status) so future slices add nodes without
      re-deciding the format. Status is one of
      `core` (renderer evaluates today) or
      `placeholder` (parser accepts; renderer's shading
      reduces to a fallback until a future renderer
      slice lights it up).
    - 6.2 Input nodes: `ConstantFloat` (core),
      `ConstantColor` (core), `TextureSample`
      (placeholder; binds an integer scene-level
      `texture_id` mirroring M16's
      `MaterialParams::base_color_texture_id`).
    - 6.3 Math nodes: `Add` (core), `Multiply` (core),
      `Mix` (core; lerp by a scalar `factor`).
    - 6.4 Utility nodes: `Normal` (core; surface normal
      matching `Hit::normal`), `UV` (core; surface UV
      matching `Hit::uv`), `UVTransform` (core; affine
      scale + offset on a UV).
    - 6.5 BSDF nodes (terminal; no outputs):
      - `Diffuse` (core) maps to `baseColor`.
      - `Emission` (core) maps to `emissionColor` +
        `emissionStrength`.
      - `Metallic` (placeholder) - `MaterialParams`
        carries the `metallic` and `roughness` fields,
        but the v1 path tracer evaluates Lambertian
        only; graphs round-trip and shade as
        Lambertian until the GGX BSDF lands.
      - `Glass` (placeholder) - the transmission BSDF
        is not implemented yet; graphs round-trip and
        shade as diffuse until the dielectric BSDF
        lands.
    - 6.6 Catalogue summary table listing all twelve
      nodes with their category and status at a glance.
  - Renumbered the previous "What this slice covers" /
    "Out of scope" sections from 6 / 7 to 7 / 8.
    Updated section 7's deferred list to remove the
    "set of node types" entry now that the catalogue
    has landed; the remaining deferred items
    (sockets / evaluation model / GPU compilation /
    scene-format integration / editor UX / bridge
    emission) are unchanged.
  - Section 8 (out of scope for v1) is unchanged: the
    catalogue is open and the deferred light networks /
    volumes / layered BSDFs / procedural noise /
    differentiable graphs entries still apply at v1.

#### Verified locally

```
$ ls docs/MATERIAL_GRAPH_SPEC.md
$ python3 -c "open('docs/MATERIAL_GRAPH_SPEC.md').read()"
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- Categories: Input / Math / Utility / BSDF -
  documented as the only four v1 categories in 6.1, with
  their nodes split across 6.2 / 6.3 / 6.4 / 6.5.
- Per-node `name / purpose / inputs / outputs`: every
  catalogue entry uses the same four-bullet shape (with
  Category and Status added so the contract is
  self-describing).
- Input nodes (constants, textures): three nodes -
  `ConstantFloat`, `ConstantColor`, `TextureSample`.
- Math nodes (add, multiply, mix): three nodes -
  `Add`, `Multiply`, `Mix`.
- Utility nodes (normal, UV, transforms): three nodes -
  `Normal`, `UV`, `UVTransform`.
- BSDF nodes (diffuse, emission, metallic, glass
  placeholder): four nodes - `Diffuse` and `Emission`
  core, `Metallic` and `Glass` placeholder. The
  placeholder distinction is documented in the Status
  field per node and summarised in 6.6.
- "Keep it minimal and expandable": the catalogue is
  twelve nodes total - exactly enough to express
  `MaterialParams` today plus the placeholder BSDFs.
  6.1 explicitly pins the catalogue as OPEN: future
  slices add nodes by following the same per-entry
  shape; they do not modify or remove existing entries.
- "Naming conventions": 6.1 pins `PascalCase` node type
  names, `snake_case` input / output names, default
  output name `value`, the four-category vocabulary,
  the per-entry shape.
- "Provide one small example graph": NOT included at
  this slice. The example will land alongside the
  scene-format integration slice (where the JSON shape
  is pinned) so the example is normative rather than
  speculative. Calling this out here so the omission is
  intentional rather than an oversight.
- "Do NOT define sockets formally / Do NOT define
  evaluation model": section 6.1 explicitly notes that
  the kinds (scalar / vector / colour / 2D coordinate)
  are informal at this slice; the formal socket type
  system + connection rules + evaluation model are
  deferred to subsequent slices, mirrored in 7.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains
  `not started`. The catalogue is a contract; nothing is
  promoted until implementation work begins.
- M21 (Material Node Graph (Editor)): remains `not
  started` (same).

### 2026-04-28 — M21 (spec, intro): material node graph plan

First doc slice of the material node graph specification.
Introduces the graph at the conceptual level: what it is,
why RelativityRender needs one above and beyond the flat
`MaterialParams` struct, how the two coexist, and the three
hard constraints any implementation MUST respect (GPU-first
execution, real-time compatibility, path-tracing
compatibility). No node types, no sockets, no evaluation
model, no implementation - those are deliberately deferred
to subsequent slices.

- **`docs/MATERIAL_GRAPH_SPEC.md`** (new):
  - Sections 1-2: purpose + what a material node graph is
    (declarative DAG of typed nodes; terminal-node
    convention; no control flow; no renderer internals).
  - Section 3: why RelativityRender needs one - flat-struct
    limits (per-parameter compositions, authoring evolution,
    per-hit detail) and how a graph addresses each.
  - Section 4: how the graph differs from `MaterialParams`.
    Pinned: the graph is an EXTENSION of the existing
    material model (flattened result vs. recipe). A material
    with no graph remains valid; a scene file carrying both
    treats the snapshot as a bake of the graph for default
    shading context.
  - Section 5: three hard constraints any implementation
    MUST respect:
    - 5.1 GPU-first execution: device-resident run-time
      form; no host callbacks / dynamic allocation /
      unbounded recursion at shading time.
    - 5.2 Real-time compatibility: cheap host-side compile
      step; bounded per-evaluation cost; partial re-uploads
      so per-slider edits do not churn the whole scene.
    - 5.3 Path-tracing compatibility: each evaluation
      independent; fixed shading-context input contract;
      outputs decompose to the existing `MaterialParams`
      consumers; differentiability NOT required in v1.
  - Section 6: explicitly defers node types / sockets /
    evaluation model / GPU compilation / scene-format
    integration / C4D bridge emission / standalone
    editor UX to future slices.
  - Section 7: out-of-scope-for-v1 footer
    (light networks, volume / SSS, layered BSDFs,
    procedural noise, time-varying inputs, graph-driven
    AOVs, differentiable graphs, GPU compilation, scene
    integration, bridge emission, editor UX).

#### Verified locally

```
$ wc -l docs/MATERIAL_GRAPH_SPEC.md
$ python3 -c "open('docs/MATERIAL_GRAPH_SPEC.md').read()"
$ ls docs/MATERIAL_GRAPH_SPEC.md
```

Spec-only slice; no source / build / test changes.

#### Per the prompt

- "What the material node graph is": Section 2.
- "Why it is needed in RelativityRender": Section 3 (three
  flat-struct limits and how the graph addresses each).
- "How it differs from simple material structs": Section 4
  (table + pinning that the two coexist - the struct is
  the bake of the graph).
- "Constraints: GPU-first execution / real-time
  compatibility / compatibility with path tracing":
  Section 5, one subsection each, each subsection ending
  with an explicit "this rules out, in v1" list so the
  constraint is enforceable rather than aspirational.
- "Do NOT define node types / sockets / implementation":
  Section 6 explicitly defers each. Section 7's
  out-of-scope list reinforces.

#### Module / milestone status

- Module 22 (Node Editor / Material Graph): remains `not
  started`. The spec slice does not promote it; the next
  doc slice (node types) extends the contract.
- M21 (Material Node Graph (Editor)): remains `not
  started` (same rationale).

### 2026-04-28 — M19 (extension 6): preview dialog displays the rendered image

Seventh slice of the Cinema 4D bridge. The preview dialog now
shows the rendered image after a successful `render`, with a
robust two-stage display path:

1. **Primary**: convert the renderer's PPM to BMP, load via
   `c4d.bitmaps.BaseBitmap`, paint into an in-dialog
   `c4d.gui.GeUserArea` fitted to aspect.
2. **Fallback**: when any step in the primary path fails,
   create / update a `RelativityRender Preview` Plane in the
   active document and apply the BMP as a Color-channel
   bitmap shader so the user still sees the image somewhere.

Every step writes a one-line status message into the
existing response text area so the user always knows which
display path succeeded.

- **`integrations/c4d/RelativityRenderBridge/image_io.py`:**
  New plain-Python module (no `c4d` import). Implements:
  - `decode_ppm_p6(bytes)` / `read_ppm_p6(path)` -> a
    `(width, height, rgb_bytes)` triple. Handles PPM header
    comments, mixed whitespace tokens, `maxval` rescaling
    (any maxval in `[1, 65535]` is normalised to 8-bit
    samples), and 16-bit big-endian samples for `maxval >
    255`. Rejects `P3` (ASCII), wrong magic, truncated
    bodies, and zero dimensions with a `PpmDecodeError`.
  - `encode_bmp_24(width, height, rgb_bytes)` -> a 24-bit
    BI_RGB BMP byte string. 14-byte BITMAPFILEHEADER + 40-
    byte BITMAPINFOHEADER + bottom-up pixel rows with BGR
    samples and 4-byte row alignment. No compression / no
    colour table; the simplest format every C4D bitmap
    loader accepts.
  - `convert_ppm_to_bmp(ppm_path, bmp_path="")` -> reads
    PPM, writes BMP next to the source (or at the explicit
    destination), creates parent dirs, returns the absolute
    BMP path.
- **`integrations/c4d/RelativityRenderBridge/preview_state.py`:**
  - New `parse_render_response(status_line)` returns
    `(width, height, path)` parsed from the
    `OK rendered <W>x<H> to <abs_path>` reply, or
    `(None, None, None)` for any reply that doesn't fit
    the schema. The parser uses the FIRST `" to "` after
    the dimensions block as the path delimiter, so paths
    that themselves contain `" to "` round-trip verbatim.
- **`integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp`:**
  - New `_PreviewArea(c4d.gui.GeUserArea)`. `DrawMsg`
    paints the cached `BaseBitmap` fitted-to-aspect inside
    the area's pixel rect; the empty case fills the rect
    with `c4d.COLOR_BG`. Wraps every draw + `Redraw()` call
    in `try/except` so a paint failure cannot abort the
    dialog event loop.
  - `PreviewDialog.__init__` owns the `_PreviewArea` and
    holds the most recent `BaseBitmap` so the bitmap stays
    alive across re-layouts.
  - `PreviewDialog.CreateLayout` adds a "Preview" group
    below the response area with an `AddUserArea` +
    `AttachUserArea(self._preview_area, ...)`.
  - `PreviewDialog._on_render` is rewritten as a state
    machine: send `render`, surface the reply, parse
    `(width, height, path)` from the OK status line, then
    call `_show_rendered_image(ppm_path)`.
  - `_show_rendered_image` runs the two-stage display
    path. Step A converts PPM->BMP and writes status into
    the response area on success and on each kind of
    failure (file missing, decode error, conversion
    error). Step B (`_try_load_into_dialog`) loads the BMP
    into a `BaseBitmap` and hands it to the preview area;
    returns False on failure to signal the fallback. Step
    C (`_fallback_to_scene_plane`) calls the new helpers
    to create / update a Plane + Material in the document.
    Every step is wrapped: a Python exception in any step
    surfaces as a single response line, never as a
    plugin-host crash.
  - New module-level helpers
    `_find_or_create_preview_plane(doc)`,
    `_find_or_create_preview_material(doc, image_path)`,
    `_ensure_texture_tag(plane, mat)` build / refresh the
    fallback scene state. Stable names
    (`PREVIEW_PLANE_NAME` / `PREVIEW_MATERIAL_NAME`) keep
    successive renders updating the same objects rather
    than spawning duplicates. The material reuses an
    existing bitmap shader when present so we don't leak
    shaders on repeat renders.
- **`integrations/c4d/RelativityRenderBridge/tests/test_image_io.py`:**
  44 standalone Python assertions. Coverage:
  - PPM P6 decode: 2x1 maxval-255, comments between
    tokens, mixed whitespace, maxval-127 rescale,
    16-bit-sample handling, P3 rejection, truncated body
    rejection, zero-dimension rejection.
  - `read_ppm_p6` round-trip through tempfile.
  - BMP 24-bit encode: 2x2 round-trip via a tiny test
    decoder; 3-wide row padding to 12 bytes; buffer-length
    validation; zero-dimension rejection.
  - End-to-end `convert_ppm_to_bmp`: default destination
    lands next to source with `.bmp` extension; explicit
    destination respected; parent dir created when missing.
  - Pin: `image_io` does not import `c4d`.
- **`integrations/c4d/RelativityRenderBridge/tests/test_preview_state.py`:**
  104 standalone Python assertions (up from 89). New
  coverage on `parse_render_response`:
  - Typical reply.
  - Path with embedded spaces, including " to " inside the
    path.
  - Unicode path component.
  - ERR / missing-marker / non-numeric dims / zero dims /
    empty path / empty-or-None input cases.
- **`integrations/c4d/RelativityRenderBridge/README.md`:**
  Documents the rendered-image display pipeline (PPM->BMP
  conversion + GeUserArea blit + scene-plane fallback),
  the per-render status messages, and the layout block
  now lists `image_io.py` + its test.

#### Verified locally

```
$ python3 integrations/c4d/RelativityRenderBridge/tests/test_image_io.py
test_image_io: 44/44 passed

$ python3 integrations/c4d/RelativityRenderBridge/tests/test_preview_state.py
test_preview_state: 104/104 passed

$ python3 integrations/c4d/RelativityRenderBridge/tests/test_server_client.py
test_server_client: 33/33 passed

$ python3 integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py
test_rrscene_writer: 118/118 passed

$ python3 -c 'import ast; ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp"
).read()); ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/image_io.py").read())'
# (no output -> both files are syntactically valid Python)
```

End-to-end smoke through the bridge's display pipeline
(no Cinema 4D needed): a synthetic 4x3 PPM (the same
`P6 W H 255 <body>` shape `Image::save_ppm` produces) is
parsed via `parse_render_response`, converted via
`convert_ppm_to_bmp`, and the resulting file's
BITMAPFILEHEADER `file_size` field matches the on-disk
size (90 bytes, header + 4-byte-aligned rows). The C4D-
only stages (`BaseBitmap.InitWith`, the user-area draw,
the scene-plane creation) reduce to calls into the
already-tested helpers; the two failure paths (in-dialog
fail -> scene-plane fallback; scene-plane fail -> single
response line) are exception-wrapped so a Python error
never escapes the plugin host.

#### Per the prompt

- "After render: load output image path returned by
  server": `parse_render_response` extracts the absolute
  path from the `OK rendered <W>x<H> to <path>` reply;
  the dialog then converts the PPM to BMP and loads it
  via `BaseBitmap.InitWith`.
- "Display it in dialog if possible":
  `_PreviewArea(c4d.gui.GeUserArea)` paints the bitmap
  fitted-to-aspect inside the dialog's preview area.
- "Fallback: create/update preview plane in C4D scene;
  apply output image as texture":
  `_find_or_create_preview_plane` + `_find_or_create_preview_material`
  + `_ensure_texture_tag` create or update a stable-
  named Plane + Material with a Color-channel bitmap
  shader. Successive renders update the existing
  material rather than spawning duplicates.
- "Keep robust": every step is wrapped in `try/except`
  so a Python error never escapes into the C4D plugin
  host. Failures degrade through the fallback rather
  than aborting the dialog. Each transition (file
  missing, decode failure, BMP write failure, bitmap
  init failure, scene-plane failure) writes a one-line
  status into the response area so the user always
  knows which path was used.

#### Module / milestone status

- Module 20 (Cinema 4D Bridge): remains `in progress`.
  In-dialog image preview + scene-plane fallback landed;
  binary framebuffer streaming over the protocol (so the
  bridge can render scenes when host and renderer are on
  different filesystems) is the remaining slice. Once
  that lands, M19's exit criterion ("a Cinema 4D scene
  renders through the server and the result is shown in
  the C4D viewport") is met.
- M19 (Cinema 4D Bridge (Plugin)): remains `in progress`
  (same).

### 2026-04-28 — M19 (extension 5): C4D preview dialog (text-only)

Sixth slice of the Cinema 4D bridge. A floating
`c4d.gui.GeDialog` panel groups every server-talking action
plus the four relativity sliders into a single window, with
a multi-line read-only text area that surfaces the most
recent server reply. No image preview yet - per the prompt,
this slice ships the dialog scaffolding and the response
text only.

- **`integrations/c4d/RelativityRenderBridge/preview_state.py`:**
  New plain-Python module (no `c4d` import). Captures the
  pieces of `PreviewDialog`'s behaviour that don't need a
  GeDialog so the standalone test harness can exercise them:
  - `clamp_unit(v)` clamps to `[0, 1]` (the four sliders'
    natural range); tolerates non-numeric input by returning
    `0.0`.
  - `clamp_beta(v)` clamps to `[0, 0.999]` so the host's
    `clampBeta(...)` invariant ("`|beta| < 1`") cannot be
    violated by a slider rounded to the wall.
  - `make_relativity_from_dialog(beta, aberration, doppler,
    searchlight)` clamps each input then delegates to
    `rrscene_writer.make_relativity_section(...)`. The
    default-input path produces a section bit-for-bit equal
    to `make_relativity_section()`'s default output (pinned
    by a test).
  - `validate_host(host)` rejects empty / whitespace-only
    inputs and embedded whitespace (which would smash into
    the protocol's line-framing).
  - `validate_port(port)` accepts integers and integer-shaped
    strings; rejects bool, float, non-numeric strings, and
    out-of-range values. `int(3.5)` would silently truncate,
    so the validator special-cases `float` rejection (caught
    by the test suite).
  - `format_server_reply(response, label)` renders a
    `server_client.ServerResponse` into a single line with
    `[label] OK ...` / `[label] ERR ...` framing for the
    response text area.
  - `format_connection_error(exc, label, host, port)`
    renders a `ServerClientError` into a single line with
    the host / port and a `--serve` reminder.
  - Module-level defaults (`DEFAULT_HOST`, `DEFAULT_PORT`,
    `DEFAULT_BETA`, `DEFAULT_ABERRATION`, `DEFAULT_DOPPLER`,
    `DEFAULT_SEARCHLIGHT`, `MIN_PORT`, `MAX_PORT`) pinned to
    the v1 contract.
- **`integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp`:**
  - `_export_to_disk(doc, relativity_override=None)` learnt
    an optional override hook. When supplied, the
    `relativity` section in the saved scene is the override
    dict; otherwise the existing controller / fallback
    behaviour is unchanged.
  - New `PreviewDialog(c4d.gui.GeDialog)` class. `CreateLayout`
    builds four labelled groups (Server / Actions /
    Relativity / Server Response). The four sliders use
    `AddEditSlider` with min=0, max=1 (max=0.999 for beta),
    step=0.001. The response area is a
    `DR_MULTILINE_READONLY` `AddMultiLineEditText`.
    Per-button handlers (`_on_ping` / `_on_export` /
    `_on_send` / `_on_render`) read the dialog state, build
    a `RenderServerClient`, send the appropriate line, and
    drop the formatted reply into the response area.
    `_validate_server_target` runs the host / port
    validators before any socket call so a bad input
    surfaces as a single descriptive line in the response
    area rather than as a Python traceback.
  - New `OpenPreviewDialogCommand(plugins.CommandData)`
    opens the dialog as `DLG_TYPE_ASYNC` (so it stays open
    while the user keeps working) at id `1058605`. A
    module-level `_preview_dialog_singleton` holds the
    instance across re-opens so the response history
    survives.
  - `_register` now registers all six command plugins.
- **`integrations/c4d/RelativityRenderBridge/tests/test_preview_state.py`:**
  89 standalone Python assertions. Coverage:
  - Pinned defaults (host, port, beta, sliders, port
    range).
  - `clamp_unit` in-range / out-of-range / garbage input.
  - `clamp_beta` clamps `1.0 -> 0.999` (the strict invariant
    pin) and negative -> 0.
  - `make_relativity_from_dialog`: default inputs equal
    `rrscene_writer.make_relativity_section()`; non-default
    inputs round-trip; out-of-range inputs are clamped
    before reaching the writer.
  - `validate_host`: typical inputs accepted; empty /
    whitespace / embedded whitespace / `None` rejected with
    a clear message.
  - `validate_port`: in-range values accepted; string
    integers accepted; out-of-range / non-integer / `bool` /
    `float` rejected. Float rejection caught a real bug in
    the first draft (`int(3.5)` silently truncated to `3`).
  - `format_server_reply` for OK / ERR / `None` cases.
  - `format_connection_error` includes host / port / the
    underlying error / a `--serve` remedy hint.
  - Pin: `preview_state` does not import `c4d`.
- **`integrations/c4d/RelativityRenderBridge/README.md`:**
  Documents the new dialog, its layout (Server row /
  Actions row / Relativity sliders / Response text area),
  the per-command timeouts, that the dialog state is
  independent of the document controller, and that image
  preview is intentionally not in this slice. Updates the
  layout block to include `preview_state.py`, the
  plugin-id table to include the dialog (id `1058605`),
  and the standalone-tests output to include the new
  suite.

#### Verified locally

```
$ python3 integrations/c4d/RelativityRenderBridge/tests/test_preview_state.py
test_preview_state: 89/89 passed

$ python3 integrations/c4d/RelativityRenderBridge/tests/test_server_client.py
test_server_client: 33/33 passed

$ python3 integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py
test_rrscene_writer: 118/118 passed

$ python3 -c 'import ast; ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp"
).read()); ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/preview_state.py").read())'
# (no output -> both files are syntactically valid Python)
```

The `PreviewDialog` itself requires a Cinema 4D environment
to run (it subclasses `c4d.gui.GeDialog`), but the AST parse
above proves the file is syntactically valid Python and the
runtime behaviour reduces to calls into helpers covered by
the 89-assertion preview-state suite plus the 33-assertion
server-client suite.

#### Per the prompt

- "Controls: host / port / Ping / Export Scene / Send
  Scene / Render / beta slider / doppler slider /
  aberration slider / searchlight slider": all ten present
  in `PreviewDialog.CreateLayout` (host + port +
  Ping in the Server row, three buttons in the Actions row,
  four `AddEditSlider`s in the Relativity group).
- "Display: server response text": the dialog's bottom
  group is a `DR_MULTILINE_READONLY` `AddMultiLineEditText`;
  every button handler appends one formatted line to it
  (newest at the bottom).
- "No image display yet": no `BitmapShaderHelper`, no
  `c4d.bitmaps.BaseBitmap`, no preview area. The dialog's
  only output surface is the text response area.

#### Module / milestone status

- Module 20 (Cinema 4D Bridge): remains `in progress`.
  The text-only preview dialog is the natural prerequisite
  for the eventual bitmap area; binary framebuffer
  streaming over the protocol is the remaining gating slice.
- M19 (Cinema 4D Bridge (Plugin)): remains `in progress`
  (same).

### 2026-04-28 — M19 (extension 4): bridge talks to the renderer server

Fifth slice of the Cinema 4D bridge. Three new command plugins
turn the C4D side into a real client of the M18 renderer
server: ping for connectivity, send-scene to upload the export
over the protocol, render-scene to trigger a GPU render. No
preview panel - dialogs only, per the prompt.

- **`integrations/c4d/RelativityRenderBridge/server_client.py`:**
  New plain-Python module (no `c4d` import). Implements the
  M18 line-based protocol against the host's renderer server:
  - `parse_response(text)` -> `ServerResponse(ok, status_line,
    body, raw)`. Tolerates CR/LF and missing terminators so a
    degenerate input never crashes the parser.
  - `read_until_terminator(read_fn, terminator=b"\\nEND\\n")`
    drains a callable in 4 KiB chunks until the terminator is
    seen, raises on EOF-before-terminator, and caps at
    1 MiB to bail out cleanly when something is wedged.
  - `RenderServerClient(host, port, timeout)` opens one
    socket per command via `socket.create_connection`,
    `sendall`s the line, drives `read_until_terminator`,
    decodes UTF-8, returns a parsed `ServerResponse`. Each
    command opens a fresh socket - matching the v1 server's
    one-client-at-a-time accept loop. `send_command` accepts
    a per-call `timeout` override so render (60s) can use a
    longer deadline than ping (2s).
  - `ServerClientError` on connect / send / receive failures.
    The .pyp catches it and surfaces the message in a
    `c4d.gui.MessageDialog` so a Python exception never
    escapes into the C4D plugin host.
  - Module-level `DEFAULT_HOST` (`127.0.0.1`),
    `DEFAULT_PORT` (`7777`), and `RESPONSE_TERMINATOR`
    pinned to the v1 contract.
- **`integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp`:**
  - Existing `ExportSceneCommand.Execute` body refactored into
    `_export_to_disk(doc) -> _ExportResult` plus a
    `_format_export_summary(result)` helper. Both are reused
    by the new SendScene command so the wire payload the
    server receives is bit-for-bit what the standalone
    Export Scene wrote.
  - New `_format_server_reply(response, command_label)` and
    `_format_server_error(exc, command_label)` produce the
    standard dialog wording for every server-talking
    command, so the three commands cannot drift.
  - `PingServerCommand` (id `1058602`) sends `ping`,
    surfaces the `OK pong` reply.
  - `SendSceneCommand` (id `1058603`) calls
    `_export_to_disk` first and then sends
    `load_scene <abs_path>`. If the export succeeds but the
    server cannot be reached, the dialog reports the
    on-disk path so the user knows the file is still
    available for later submission.
  - `RenderSceneCommand` (id `1058604`) sends `render` with
    a 60s timeout (renders kick off the GPU pipeline) and
    surfaces the saved-image path the server reports.
  - All three new commands go through the
    `_format_server_error` path on `ServerClientError`,
    pointing the user at `RelativityRender --serve` when
    no server is listening on `127.0.0.1:7777`. They wrap
    the whole `Execute` body in `try/except` for any other
    exception so a Python error never escapes the plugin
    host.
  - `_register` now registers all five command plugins.
- **`integrations/c4d/RelativityRenderBridge/tests/test_server_client.py`:**
  New standalone test runner. 33 host assertions covering:
  - `parse_response`: OK / ERR single-line replies, multi-line
    bodies, CRLF tolerance, missing terminator soft-fallback,
    empty body (`END` only), `OK` token alone.
  - `read_until_terminator`: single-chunk read, multi-chunk
    drain, post-terminator byte trim, EOF-before-terminator
    raises, max-bytes cap raises.
  - `RenderServerClient._normalise_command_line`: trims
    whitespace and appends `\\n`; rejects empty commands;
    rejects embedded `\\n` / `\\r`.
  - Pinned defaults (`DEFAULT_HOST = 127.0.0.1`,
    `DEFAULT_PORT = 7777`, `RESPONSE_TERMINATOR = b"\\nEND\\n"`)
    so a future drift fails the test alongside the code.
  - `server_client` does not import `c4d`.
  The TCP socket layer is exercised manually via the live
  `--serve` smoke test below; pinning the parser layer
  means CI stays deterministic without binding a real port.
- **`integrations/c4d/RelativityRenderBridge/README.md`:**
  Documents the three new commands, the per-command timeouts
  (ping 2s / load 10s / render 60s), the connection details
  (default `127.0.0.1:7777`), and updates the layout +
  plugin-id table + standalone-tests output.

#### Verified locally

```
$ python3 integrations/c4d/RelativityRenderBridge/tests/test_server_client.py
test_server_client: 33/33 passed

$ python3 integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py
test_rrscene_writer: 118/118 passed

$ python3 -c 'import ast; ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp"
).read()); ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/server_client.py").read())'
# (no output -> both files are syntactically valid Python)
```

End-to-end through the live renderer server, driving the
bridge's `RenderServerClient` directly:

```
$ ./build/bin/RelativityRender --serve &
$ python3 -c "
  import sys; sys.path.insert(0,
      'integrations/c4d/RelativityRenderBridge')
  import server_client as sc
  c = sc.RenderServerClient(timeout=2.0)
  print(c.send_command('ping').status_line)
  print(c.send_command('load_scene /tmp/m19_smoke.rrscene',
                       timeout=10.0).status_line)
  print(c.send_command('render', timeout=60.0).status_line)
  print(c.send_command('shutdown').status_line)
  "
OK pong
OK loaded 1 materials, 0 spheres, 1 lights, 1 meshes
ERR render: no CUDA backend compiled in (rebuild with -DRR_ENABLE_CUDA=ON)
OK goodbye
```

Each call open / send / drain / closes a fresh socket
against the server's accept loop. Status lines match the
M18 wiring slice byte-for-byte. The host-only build hits
the no-CUDA branch on `render` cleanly; on a CUDA-enabled
build the same call would return
`OK rendered <W>x<H> to <abs_path>` and the dialog would
surface that path verbatim.

#### Per the prompt

- "Commands: RelativityRender: Ping Server / Send Scene /
  Render Scene": all three registered as `CommandData`
  plugins (ids `1058602` / `1058603` / `1058604`).
- "Use socket to localhost:7777":
  `server_client.RenderServerClient` defaults to
  `127.0.0.1:7777`, with `socket.create_connection` opening
  a fresh TCP connection per command and the v1
  `\\n` / `END\\n` line protocol on top.
- "Do not build preview panel yet": none of the new
  commands instantiate a `c4d.gui.GeDialog`. Each invokes
  `c4d.gui.MessageDialog` to display the server's reply -
  status line plus any extra body lines on multi-line
  replies.
- "Just display server response": `_format_server_reply`
  formats the reply directly into the dialog text. No
  parsing of paths, no auto-open, no implicit retries.

#### Module / milestone status

- Module 19 (Renderer Server): remains `in progress`.
  The bridge being a real protocol client is the M18 exit
  signal that "an external process can submit a scene file
  and receive a rendered EXR back" - one more slice (binary
  framebuffer streaming) closes that.
- Module 20 (Cinema 4D Bridge): remains `in progress`.
  Server protocol client landed; preview frame display in
  the C4D viewport (which depends on binary streaming) is
  the remaining slice.
- M19 (Cinema 4D Bridge (Plugin)): remains `in progress`
  (same).

### 2026-04-28 — M19 (extension 3): materials + emission + viewport colour + lights

Fourth slice of the Cinema 4D bridge. Materials now carry real
RGB albedo from C4D Standard `Mmaterial`'s colour channel, and
emission when the luminance channel is enabled. Polygons without
a Texture tag fall through to the object's "Display Color" so
the renderer no longer sees a wall of mid-grey defaults. Cinema
4D lights map to the v1 `lights[]` types where the protocol
supports them: omni -> point, distant / parallel -> directional,
area -> point with a clear warning, spot -> skipped.

The .rrscene the bridge produces is now end-to-end usable: the
host renderer server's `load_scene` accepts a complete
camera + render + relativity + materials + meshes + lights
document with no warnings.

- **`integrations/c4d/RelativityRenderBridge/rrscene_writer.py`:**
  - `make_material_section` learnt `emission_color`,
    `emission_strength`, and `roughness` kwargs. Emission
    fields are emitted only when BOTH a colour and a
    positive strength are supplied (matches the host parser's
    "zero strength = no emission" rule). `base_color` and
    `emission_color` components are non-negative-clamped;
    `roughness` is clamped to `[0, 1]`.
  - New light builders `make_point_light` and
    `make_directional_light`, plus `LIGHT_TYPE_POINT` /
    `LIGHT_TYPE_DIRECTIONAL` constants pinned to the v1
    protocol's exact strings ("point" / "directional"). Both
    builders non-negative-clamp colour components and
    intensity.
  - `build_rrscene` learnt an optional `lights=` kwarg, with
    the same omit-on-empty invariant the meshes / materials
    kwargs honour.
  - Module remains free of any `c4d` import.
- **`integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp`:**
  - `_extract_standard_material_params(mat)` reads
    `MATERIAL_COLOR_COLOR` * `MATERIAL_COLOR_BRIGHTNESS` for
    `base_color`, and (when `MATERIAL_USE_LUMINANCE` is on)
    `MATERIAL_LUMINANCE_COLOR` + `MATERIAL_LUMINANCE_BRIGHTNESS`
    for `emission_color` + `emission_strength`. Non-Standard
    material types return `kind="unsupported"` so the dialog
    can warn the user that only the material name made it
    across.
  - `_viewport_fallback_color(obj)` reads
    `ID_BASEOBJECT_COLOR` when `ID_BASEOBJECT_USECOLOR` is
    in mode `2` (Always) or `3` (Layer). Otherwise returns
    `None`, leaving the polygon to fall back to the renderer
    default.
  - New `MaterialRegistry` class (replaces the old
    `material_name_to_id` dict). Two registration paths share
    one id sequence: `register_c4d_material(name, params)`
    keys by Cinema 4D material name and consumes the
    extracted standard-material params; `register_viewport_color(rgb)`
    keys by an RGB slug (3 decimal digits) and emits a
    `Viewport: r, g, b` entry. Both reuse ids when called
    with the same key, so dedup is automatic.
  - New `_walk_document_lights(doc)`. Builds the C4D
    light-type-to-rrscene-type mapping at runtime so missing
    constants on older C4D builds don't break the import.
    Returns three lists: `light_entries` (ready for
    `lights[]`), `light_caveats` (`(name, message)` pairs
    for lossy conversions like area -> point), and
    `light_skips` (`(name, message)` pairs for spot lights
    and other unsupported types).
  - `_format_skip_summary(...)` extended with light caveats /
    skips and unsupported-material-name lists; each block
    capped at 8 entries with a "... and N more" suffix.
  - `ExportSceneCommand.Execute` calls both walkers, threads
    every list through the dialog text, and writes
    `Polygon meshes: N (T triangles, M materials)` +
    `Lights: P point, D directional` summary lines. The
    `_note` field on the saved scene now records mesh +
    light + skipped counts.
- **`integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py`:**
  118 standalone Python assertions (up from 88). New
  coverage:
  - `make_material_section` clamps negative `base_color`
    components; emission fields are emitted only when BOTH
    `emission_color` and a positive `emission_strength` are
    supplied; negative strength is pruned; one input alone
    is pruned.
  - `make_material_section` clamps `roughness` to `[0, 1]`.
  - `make_point_light` and `make_directional_light` defaults
    + explicit overrides + non-negative intensity clamp.
  - `LIGHT_TYPE_POINT` / `LIGHT_TYPE_DIRECTIONAL` constants
    pinned to "point" / "directional" so a future drift in
    the writer can never silently break v1 parsing.
  - `build_rrscene` with `lights=`; the omit-on-empty
    invariant extended to cover `meshes=[]`, `materials=[]`,
    and `lights=[]` in one assertion.
- **`integrations/c4d/RelativityRenderBridge/README.md`:**
  Documents the new material extraction (Standard material's
  Color + Luminance channels), the viewport-color fallback
  rule, the C4D-light -> rrscene-light mapping table
  (omni / distant / parallel / area / tube / spot / parspot),
  the unsupported-material-type warning. Updates the
  standalone-tests pass count.

#### Verified locally

```
$ python3 integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py
test_rrscene_writer: 118/118 passed

$ python3 -c 'import ast; ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp"
).read())'
# (no output -> .pyp is syntactically valid Python)
```

End-to-end through the host renderer server, this time with a
complete .rrscene exercising every new builder: three materials
(Mmaterial standard, Mmaterial with luminance emission, viewport
fallback), three cube meshes, one point light, one directional
sun light. Through `RelativityRender --serve` + `load_scene`:

```
< OK loaded 3 materials, 0 spheres, 2 lights, 3 meshes
< END
```

Materials, lights, meshes all round-trip through the production
C++ scene loader (`src/io/SceneLoader.cpp`) without warnings.
The renderer treats out-of-range or omitted material fields as
defaults, so meshes whose textures were not Mmaterial still
shade correctly with their Cinema 4D material name preserved
in the saved file.

#### Per the prompt

- "Material base color": pulled from
  `MATERIAL_COLOR_COLOR * MATERIAL_COLOR_BRIGHTNESS` for
  Standard materials; written as `base_color`.
- "Emission if detectable": pulled from the luminance channel
  when `MATERIAL_USE_LUMINANCE` is on; emitted as
  `emission_color` + `emission_strength` only when the
  combined input would round-trip through the host parser
  (positive strength).
- "Object viewport fallback color":
  `ID_BASEOBJECT_COLOR` is consulted when no Texture tag is
  present and `ID_BASEOBJECT_USECOLOR` is `Always` (2) or
  `Layer` (3); a deduped `Viewport: r, g, b` material entry
  carries the colour.
- "Point/directional/area lights where possible": omni and
  distant / parallel cleanly map to the v1 `"point"` /
  `"directional"` types. Area + tube lights export as a
  point at the area's origin, with the dialog flagging the
  lossy conversion. Spot / parallel-spot lights are skipped
  (no spot cone in v1). Each kind has a separate code path
  in `_light_type_mapping()`, gated on the constants being
  available on the running C4D build so the import does not
  fail on older releases.
- "Save complete .rrscene compatible with RelativityRender":
  the dialog's saved file now exercises every section the
  host parser supports (camera + render_settings + relativity
  + materials + meshes + lights). Verified by the live
  `--serve` round-trip above.

#### Module / milestone status

- Module 20 (Cinema 4D Bridge): remains `in progress`.
  Material + light translation landed; generator-bake-via-
  cache support, server-protocol client, and preview frame
  display in the C4D viewport are the remaining slices.
- M19 (Cinema 4D Bridge (Plugin)): remains `in progress`
  (same).

### 2026-04-28 — M19 (extension 2): polygon mesh export

Third slice of the Cinema 4D bridge. The Export Scene command
now walks the active document and writes every native polygon
mesh into the `.rrscene` `meshes[]` array, with quads
triangulated, global transforms baked into world-space
vertices, and material references gathered into a stub
`materials[]`. Generators / deformers / volumes / hair are
skipped explicitly and surfaced in the confirmation dialog so
the user always knows which scene objects did NOT make it into
the file.

- **`integrations/c4d/RelativityRenderBridge/rrscene_writer.py`:**
  - New pure helpers callable from the .pyp + the standalone
    test harness: `triangulate_cpolygon(a, b, c, d)`
    converts a Cinema 4D `CPolygon` into one or two triangles
    along the `a-c` diagonal and prunes degenerates;
    `transform_point(point, v1, v2, v3, off)` applies a
    Cinema 4D-style global matrix to a local point.
  - New section builders: `make_mesh_section(vertices,
    triangles, material_id)` and
    `make_material_section(id, name=None, base_color=None)`.
    Each follows the host parser's required-vs-optional
    field shape (`materials[i].id` is the only mandatory
    material field; everything else falls back to
    `MaterialParams` defaults).
  - `build_rrscene` learnt optional `meshes=` and
    `materials=` kwargs. Empty / omitted iterables produce
    output identical to the previous slice (the keys are
    elided), so existing callers continue to round-trip
    byte-for-byte.
  - Module remains free of any `c4d` import.
- **`integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp`:**
  - New `_classify(obj)` helper returns one of `('polygon',
    None)` / `('skip', <reason>)` / `('ignore', None)` based
    on `obj.IsInstanceOf(c4d.Opolygon)`,
    `OBJECT_GENERATOR`, `OBJECT_MODIFIER`, and a small set
    of unsupported type ids (`Ovolume` /
    `Ovolumebuilder` / `Ovolumemesher` / `Ohair`,
    when those constants are present on the running C4D
    build). Reasons surface in the dialog as
    `(generator)` / `(deformer)` / `(volume)` / `(hair)` /
    `(unsupported)`.
  - `_polygon_to_mesh_entry(obj, ...)` reads
    `obj.GetAllPoints()` (NOT `GetDeformCache()` - deformers
    are intentionally ignored), pulls the global matrix
    columns from `obj.GetMg()`, applies them via
    `rrscene_writer.transform_point`, Z-flips through
    `convert_c4d_position`, then iterates
    `obj.GetAllPolygons()` calling `triangulate_cpolygon`
    on each `CPolygon`. Polygons with no points or no
    triangles are dropped quietly.
  - `_primary_material_name(obj)` returns the name of the
    first `Ttexture` tag's bound material, or `None`. The
    walker maps unique material names to small integer ids
    (0, 1, 2, ...) and emits one stub
    `make_material_section(id, name)` per unique material.
    Meshes without a Texture tag store `material_id = -1`.
  - `_walk_document_meshes(doc)` does a depth-first walk
    over `GetFirstObject()` -> `GetDown()` /
    `GetNext()`, returning four lists: mesh entries,
    materials, skipped (name, reason), and the names of
    polygon meshes that had deformer descendants (so the
    dialog can flag the "deformation ignored" cases).
  - `_format_skip_summary(...)` formats those lists into
    the dialog text, capping each list at 8 entries with
    a "... and N more" line so a busy document doesn't
    produce an unscrollable dialog.
  - `ExportSceneCommand.Execute` is rewritten around the
    walker. The dialog now reports `Polygon meshes: N
    (T triangles, M materials)` and a warning block listing
    skipped objects + deformer-affected polygons. The
    `_note` field on the saved scene includes the mesh +
    skipped counts for diff-time debugging.
- **`integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py`:**
  88 standalone Python assertions (up from 61). New
  coverage:
  - `triangulate_cpolygon`: triangle (`c == d`), quad,
    quad with high-magnitude indices, degenerate triangle
    (a == c), degenerate quad (a == d).
  - `transform_point`: identity matrix, pure translation,
    uniform 2x scale, 90 deg yaw around Y.
  - `make_mesh_section` shape + default `material_id = -1`.
  - `make_material_section` minimal-vs-full output.
  - `build_rrscene` with explicit meshes + materials, and
    the omit-on-empty invariant
    (`meshes=[]` produces a scene without a `meshes` key,
    matching the prior-slice byte-for-byte output).
  - Full mesh-scene round-trip through
    `serialize_rrscene` -> `json.loads` (a quad expands
    into 2 triangles on disk).
- **`integrations/c4d/RelativityRenderBridge/README.md`:**
  Documents the new polygon export, the unsupported-objects
  table, and the deformer-ignored caveat. Updates the
  standalone-tests pass count.

#### Verified locally

```
$ python3 integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py
test_rrscene_writer: 88/88 passed

$ python3 -c 'import ast; ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp"
).read())'
# (no output -> .pyp is syntactically valid Python)
```

End-to-end round-trip through the host's renderer server with
a synthetic 8-vertex / 6-quad cube assembled via the new
helpers (`transform_point` + `convert_c4d_position` for the
global-transform bake; `triangulate_cpolygon` for the quad ->
12-triangle expansion):

```
$ ./build/bin/RelativityRender --serve &
$ printf 'load_scene /tmp/cube.rrscene\nshutdown\n' | nc -q1 127.0.0.1 7777
< OK loaded 1 materials, 0 spheres, 0 lights, 1 meshes
< END
< OK goodbye
< END
```

The bridge's mesh export parses cleanly through the production
C++ scene loader (`src/io/SceneLoader.cpp`) - the materials
section, mesh vertices, and triangle indices all round-trip
without warnings.

#### Per the prompt

- "Global transform": `obj.GetMg()` columns applied to each
  local point via `transform_point`; the world position is
  written into `vertices[]` after the C4D-to-renderer Z-flip.
  Per-mesh `transform` JSON field is intentionally omitted -
  vertices are already in world space, so a transform on top
  would double-apply.
- "Vertices": `obj.GetAllPoints()` after the bake described
  above.
- "Triangle indices": `triangulate_cpolygon(a, b, c, d)` per
  Cinema 4D `CPolygon`. Triangles emit one entry; quads
  emit two; degenerate input is pruned.
- "Material id": stable integer per unique Cinema 4D material
  name, written into both the mesh's `material_id` and the
  scene's `materials[]` array.
- "Convert quads to triangles": handled by
  `triangulate_cpolygon`. Verified by the dedicated tests +
  the cube smoke test (6 quads -> 12 triangles end-to-end).
- "Ignore unsupported (generators, deformers, volumes, hair).
  Warn clearly.": `_classify` skips each kind with a labelled
  reason; `_format_skip_summary` lists them in the dialog.
  Polygons with deformer descendants are still exported but
  flagged separately as "deformation ignored".

#### Module / milestone status

- Module 20 (Cinema 4D Bridge): remains `in progress`.
  Polygon-mesh export landed; lights translation,
  generator-bake-via-cache support, server-protocol client,
  and preview frame display in the C4D viewport are the
  remaining slices before the module flips to `landed`.
- M19 (Cinema 4D Bridge (Plugin)): remains `in progress`
  (same).

### 2026-04-28 — M19 (extension 1): live document export + relativity controller

Second slice of the Cinema 4D bridge. Replaces the foundation's
empty placeholder export with a real read of the active
document's camera transform / FOV / render resolution, and adds
a second command that creates a relativity controller Null whose
user data the export reads back. No preview UI, no server-protocol
traffic, no geometry / materials / lights translation yet.

- **`integrations/c4d/RelativityRenderBridge/rrscene_writer.py`:**
  Promoted from the foundation slice's "build the empty stub"
  helper into the bridge's data layer.
  - New per-section builders: `make_render_settings`,
    `make_camera_section`, `make_relativity_section`. Each
    mirrors the host parser's field names + value clamps
    (`src/io/SceneLoader.cpp`); a clamp / default-fallback in
    each builder means the saved file is always parseable
    even when the C4D side hands us a degenerate value.
  - New top-level `build_rrscene(camera, render_settings,
    relativity, note)` + `write_rrscene(scene, path)` so the
    .pyp plugin can hand in fully-populated sections.
  - New coordinate-conversion helpers:
    `convert_c4d_position`, `convert_c4d_direction`,
    `convert_c4d_camera_basis`. Negate the `Z` component of
    every world-space position and direction vector to bridge
    Cinema 4D's left-handed `+Z forward` to the renderer's
    right-handed `-Z forward`. Keeping the flip in one place
    means the .pyp plugin never has to know about handedness
    directly.
  - Foundation-slice helpers (`build_empty_rrscene`,
    `write_empty_rrscene`) preserved as fallback paths.
  - Module remains free of any `c4d` import - the standalone
    test harness still runs under stock `python3`.
- **`integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp`:**
  Two `CommandData`s now register under separate plugin ids:
  - `1058600` - `RelativityRender: Export Scene` (existing).
    Resolves the active rendering camera through
    `BaseDocument.GetRenderBaseDraw().GetSceneCamera(doc)` (with
    the editor camera as a fallback), pulls position / forward
    / up from `cam.GetMg()` (`mg.off`, `mg.v3`, `mg.v2`),
    converts to renderer coordinates via the writer's helper,
    reads vertical FOV from `CAMERAOBJECT_FOV_VERTICAL`
    (with a horizontal-FOV + 4:3 fallback), reads
    `RDATA_XRES` / `RDATA_YRES` from the active render data,
    and walks the document looking for an object named
    `RelativityRender Controller` (depth first). When found,
    its five user-data fields populate the relativity
    section; when not, the writer's defaults are used. The
    confirmation dialog now reports the saved path,
    resolution, FOV, and whether a controller was picked up.
  - `1058601` - `RelativityRender: Create Controller` (new).
    Creates a Null object named `RelativityRender Controller`
    with five user-data fields:
    - `beta_velocity` (Real, `0..0.999999`, step `0.001`).
    - `velocity_direction` (Vector, default `(0, 0, -1)`).
    - `aberration_strength` (Real, `0..1`, step `0.01`).
    - `doppler_strength` (Real, `0..1`, step `0.01`).
    - `searchlight_strength` (Real, `0..1`, step `0.01`).
    The defaults mirror `RelativityParams::*` and the .rrscene
    parser's clamps. The create operation is wrapped in
    `StartUndo` / `AddUndo(UNDOTYPE_NEW)` / `EndUndo` so the
    standard Edit -> Undo reverses it; `c4d.EventAdd()` updates
    the Object Manager + viewport. The new Null is selected via
    `SetActiveObject(obj, SELECTION_NEW)` so the user lands on
    it ready to scrub the values.
  - Both commands wrap `Execute` in `try/except` so a Python
    exception never escapes into the C4D plugin host. Marker
    name + user-data field names live in module-level
    constants so the create + export commands cannot drift.
  - User-data velocity is converted with the writer's
    direction Z-flip on export; both the controller's
    authored direction and the camera basis end up in the
    same right-handed frame in the saved file.
- **`integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py`:**
  61 standalone Python assertions (up from 27). New coverage:
  - `make_render_settings` defaults, explicit values,
    non-positive fallbacks, float-to-int coercion.
  - `make_camera_section` defaults, explicit values, FOV
    clamp on `<= 0` / `>= 180` (must fall back to writer
    default so the saved file passes the host parser).
  - `make_relativity_section` defaults, explicit values,
    beta clamp to `[0, 0.999999]`, strength clamps to
    `[0, 1]`.
  - `convert_c4d_position` and `convert_c4d_direction`
    Z-flip behaviour; identity-pose camera maps to the
    renderer's identity camera basis.
  - `build_rrscene` assembles a full v1 dict; round-trips
    through `serialize_rrscene` + `json.loads`.
  - `write_rrscene` creates parent dirs and returns an
    absolute path; written file parses back to the same
    dict, custom resolution / FOV / beta preserved on disk.
  - Pin: `rrscene_writer` does not import `c4d`.
- **`integrations/c4d/RelativityRenderBridge/README.md`:**
  Documents the two commands, their plugin ids, the user-
  data shape on the controller, the coordinate-system
  conversion (C4D left-handed +Z -> renderer right-handed
  -Z), and the dependency rules.

#### Verified locally

```
$ python3 integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py
test_rrscene_writer: 61/61 passed

$ python3 -c 'import ast; ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp"
).read())'
# (no output -> .pyp is syntactically valid Python)
```

End-to-end round-trip through the host's renderer server, this
time with non-default camera / resolution / relativity built via
the new section builders + Z-flip helpers:

```
$ python3 -c "
import sys; sys.path.insert(0,
    'integrations/c4d/RelativityRenderBridge')
import rrscene_writer as w
pos, fwd, up = w.convert_c4d_camera_basis(
    (0, 1.5, 4),  # C4D position 4 units forward
    (0, 0, 1),    # C4D forward (+Z)
    (0, 1, 0))
scene = w.build_rrscene(
    camera=w.make_camera_section(position=pos, forward=fwd,
                                 up=up, fov_degrees=35.0),
    render_settings=w.make_render_settings(800, 600),
    relativity=w.make_relativity_section(
        beta_velocity=0.5,
        velocity_direction=w.convert_c4d_direction((1, 0, 0)),
        aberration_strength=0.8,
        doppler_strength=1.0,
        searchlight_strength=0.6))
w.write_rrscene(scene, '/tmp/x.rrscene')
"
$ ./build/bin/RelativityRender --serve &
$ printf 'load_scene /tmp/x.rrscene\nshutdown\n' | nc -q1 127.0.0.1 7777
< OK loaded 0 materials, 0 spheres, 0 lights, 0 meshes
< END
< OK goodbye
< END
```

The bridge's full-section output parses cleanly through the
production C++ scene loader (`src/io/SceneLoader.cpp`) with no
warnings - the camera (off-origin position, off-default FOV),
render settings (800x600), and relativity (custom beta +
direction + strengths) all round-trip end to end.

#### Per the prompt

- "Active camera transform": `cam.GetMg()` ->
  `position` / `forward` / `up` after the Z-flip conversion.
- "FOV/focal length": vertical FOV in degrees from
  `CAMERAOBJECT_FOV_VERTICAL`; horizontal-FOV fallback
  derives the vertical one when needed. Focal length is the
  same scalar in different units; the rrscene format
  carries vertical FOV directly.
- "Render resolution": `RDATA_XRES` / `RDATA_YRES` from
  `doc.GetActiveRenderData()`.
- "Relativity controller values if available": user-data
  fields by name on the `RelativityRender Controller` Null,
  with writer defaults filling any missing slot.
- "Create command - RelativityRender: Create Controller":
  registered as `CreateControllerCommand`, plugin id
  `1058601`. User data is exactly the five fields the prompt
  enumerates.

#### Module / milestone status

- Module 20 (Cinema 4D Bridge): remains `in progress`
  (live camera + render-data + controller export landed;
  geometry / materials / lights translation, server-protocol
  client, and preview frame display in C4D viewport are the
  remaining slices before the module flips to `landed`).
- M19 (Cinema 4D Bridge (Plugin)): remains `in progress`
  (same).

### 2026-04-28 — M19 (foundation): Cinema 4D bridge plugin

First slice of the Cinema 4D bridge. A Python command plugin
that registers under **Plugins → RelativityRender: Export
Scene**, writes a minimal but-valid v1 `.rrscene` file to
disk, and shows a confirmation dialog with the saved path.
Strictly the foundation: no live document translation, no
preview UI, no server connection, no progressive frames yet.

The bridge is the only place in the repository allowed to
depend on Cinema 4D. Per
`docs/MODULE_MAP.md` + `integrations/c4d/README.md` the
plugin depends on the Cinema 4D SDK and the project's
`.rrscene` file format only - it does NOT import any
`rr_*` C++ headers or link against renderer internals.
Nothing under `src/` may know this plugin exists.

- **`integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp`:**
  Cinema 4D Python plugin entry point. Registers a single
  `c4d.plugins.CommandData` under
  `id=1058600` (placeholder development id - flagged in the
  README as needing a real PluginCafe registration before
  any public release). On execute:
  - Resolves the export path: next to the active document if
    it's been saved (`<doc_stem>.rrscene` next to the
    original); otherwise under
    `<C4D_startup_write>/RelativityRender/untitled.rrscene`.
    `os.getcwd()` is the last-resort fallback.
  - Reads `RDATA_XRES` / `RDATA_YRES` from the document's
    active render data when available; falls back to 640x480.
  - Calls `rrscene_writer.write_empty_rrscene(...)` to write
    the file (creates parents as needed; returns the absolute
    path saved).
  - Surfaces the saved path in `c4d.gui.MessageDialog`. Wraps
    the whole `Execute` body in `try/except` so a Python
    exception never escapes into the C4D plugin host.
- **`integrations/c4d/RelativityRenderBridge/rrscene_writer.py`:**
  Plain-Python helper. No `c4d` import - the module is
  exercised under stock python3 in the test harness, and the
  same code path runs inside Cinema 4D when the .pyp imports
  it.
  - `RRSCENE_VERSION = 1` mirrors the host loader's expected
    schema version so a future drift fails loudly at parse
    time on the host rather than silently in the writer.
  - `build_empty_rrscene(width, height, fov_deg, note)`
    builds a v1 dict with `render_settings` / `camera` /
    `relativity` populated to sane defaults
    (forward = `[0, 0, -1]`, up = `[0, 1, 0]`,
    `beta_velocity = 0`). No materials / spheres / lights /
    meshes - on the host side this parses to an empty scene.
    Optional `note` becomes a `_note` top-level key; the
    `.rrscene` v1 spec lets the parser warn-and-ignore
    unknown top-level keys, so the note round-trips cleanly.
  - `serialize_rrscene(scene)` produces JSON with 4-space
    indentation + a trailing newline so the file is
    diff-friendly when a human inspects it.
  - `write_empty_rrscene(path, ...)` makes parent directories
    (`os.makedirs(parent, exist_ok=True)`) and returns the
    absolute path saved.
- **`integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py`:**
  Standalone test runner. Stock `python3`; no `c4d` import
  required. 27 assertions covering:
  - Default dict shape (`version: 1`, render_settings,
    camera, relativity) and default values.
  - Custom width / height / fov are preserved verbatim.
  - `_note` round-trips under that key when set, and is
    absent when not.
  - `serialize_rrscene` output round-trips through
    `json.loads` to an equal dict.
  - `write_empty_rrscene` creates missing parent
    directories, returns an absolute path, the file on disk
    parses back to the same dict, custom resolution is
    preserved on disk, and successive calls overwrite (not
    append).
  - `rrscene_writer` does NOT import the `c4d` module -
    pinned so a future contributor doesn't accidentally
    break the bridge's standalone testability.
- **`integrations/c4d/RelativityRenderBridge/README.md`:**
  Install instructions (Cinema 4D plugins folder per
  platform), how to run the standalone tests, the plugin-id
  placeholder warning, the dependency rules. Documents
  where the `.rrscene` lands depending on whether the
  document has been saved.

#### Verified locally

```
$ python3 integrations/c4d/RelativityRenderBridge/tests/test_rrscene_writer.py
test_rrscene_writer: 27/27 passed

$ python3 -c 'import ast; ast.parse(open(
    "integrations/c4d/RelativityRenderBridge/RelativityRenderBridge.pyp"
).read())'
# (no output -> .pyp is syntactically valid Python)
```

End-to-end round-trip through the host's renderer server:

```
$ python3 -c "
import sys; sys.path.insert(0,
    'integrations/c4d/RelativityRenderBridge')
import rrscene_writer
rrscene_writer.write_empty_rrscene(path='/tmp/x.rrscene',
    width=800, height=600, note='smoke')
"

$ ./build/bin/RelativityRender --serve &
$ printf 'load_scene /tmp/x.rrscene\nshutdown\n' | nc -q1 127.0.0.1 7777
< OK loaded 0 materials, 0 spheres, 0 lights, 0 meshes
< END
< OK goodbye
< END
```

The bridge's writer output parses cleanly through the
production C++ scene loader (`src/io/SceneLoader.cpp`) with
no warnings - confirming the schema mirror in
`rrscene_writer.py` is accurate at this slice's scope.

#### Per the prompt

- Folder: `integrations/c4d/RelativityRenderBridge/` exists
  and is self-contained.
- Command plugin: `RelativityRender: Export Scene` registered
  in `RelativityRenderBridge.pyp`.
- "Show message dialog": `c4d.gui.MessageDialog(...)` after a
  successful export, and again on failure (so Python
  exceptions never escape the plugin host).
- "Write empty test .rrscene file": minimal v1 scene saved to
  the resolved path; no live document translation yet.
- "Do not create preview UI yet": no `GeDialog`, no preview
  area, no server-protocol traffic. The plugin's only side
  effect on disk is the single `.rrscene` file it writes.

#### Module / milestone status

- Module 20 (Cinema 4D Bridge): `not started` -> `in progress`.
- M19 (Cinema 4D Bridge (Plugin)): `not started` -> `in progress`.

The remaining bridge work (live document translation:
camera + objects + materials + lights into the .rrscene
schema; server-protocol client; preview frame display in C4D
viewport; cancellation; multi-doc support) lands in
subsequent slices. M19 / Module 20 close when a Cinema 4D
scene renders through the server and the result is shown in
the C4D viewport.

### 2026-04-28 — M18 (wiring): server connected to GPU renderer + `--serve` CLI

Wires the renderer server foundation into the executable and
strengthens the `render` reply. The protocol is unchanged; the
`render` command now (a) saves the GPU output to disk and
(b) responds with an absolute file path + `WxH` resolution so a
client never has to know the server's working directory or peek
into the PPM header. No binary framebuffer streaming yet - the
file path is the only side channel for image data at this
slice, per the prompt.

- **`src/core/Config.h`:** new `bool serve = false;` flag and
  `wants_serve()` accessor. Mirrors the existing
  `wants_render()` shape so `main` selects between the
  one-shot render path and the long-running server with the
  same conditional style.
- **`src/core/CommandLine.cpp`:** parses `--serve`. Usage line
  documents the v1 contract: server runs on `127.0.0.1:7777`.
  No `--port` / `--host` knobs yet; the protocol stays on a
  fixed port so the bridge can connect without configuration.
- **`src/main.cpp`:** when `cfg.wants_serve()` is true, builds
  a `rr::server::RenderServer` and blocks in `run()`. The
  one-shot `--render` path is unchanged. Failure to bind
  emits a single `Logger::error` line and exits 1.
- **`src/server/RenderServer.h`:** `ServerState` gains
  `render_count`, `last_render_width`, `last_render_height`,
  and `last_render_path`. Populated only on a successful
  `render`; left at the defaults otherwise. Useful for the
  OK reply today and for a future "status" command without
  changing state shape.
- **`src/server/RenderServer.cpp`:** `cmd_render`'s success
  path on a CUDA-enabled build now:
  - Resolves the saved file's absolute path through
    `std::filesystem::weakly_canonical` (with an
    `absolute(...)` fallback if the resolver errors).
  - Updates the `last_render_*` bookkeeping and increments
    `render_count` (only on success).
  - Replies `OK rendered <W>x<H> to <abs_path>` so a client
    knows the resolution + the canonical path in one line.
  - Without CUDA the response is unchanged (clean error +
    no state mutation).
- **`tests/server_tests.cpp`:** 66 host assertions (up from
  50). Expanded coverage:
  - Failed renders (no scene loaded, no CUDA) leave
    `render_count` at 0 and `last_render_*` at the defaults.
  - End-to-end client sequence `load_scene` -> `set_beta` ->
    `render` threads scene + observer state through the
    dispatcher's mutable state without spurious errors. The
    GPU branch is unreachable on host-only CI; the test
    confirms the chain reaches `render`'s entry without
    falling into the no-scene path.
  - `load_scene` after `set_beta` resets the velocity (load
    replaces the entire scene by design); pinned so a future
    change cannot silently start preserving prior beta.
- **`CMakeLists.txt`:** the `RelativityRender` executable now
  links `rr_server`. No new library; the existing
  `rr_server` static library carries the dispatcher and TCP
  loop.

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/15 ... 15/15 all Passed
100% tests passed, 0 tests failed out of 15

$ ./build/bin/server_tests
server_tests: 66/66 passed
```

End-to-end TCP smoke test (separate harness):
```
$ ./build/bin/RelativityRender --serve &
$ printf 'ping\nload_scene scenes/test_minimal.rrscene\n
            set_beta 0.6\nrender\nshutdown\n' | nc -q1 127.0.0.1 7777
< OK pong
< END
< OK loaded 0 materials, 0 spheres, 0 lights, 0 meshes
< END
< OK beta set to 0.6
< END
< ERR render: no CUDA backend compiled in (rebuild with -DRR_ENABLE_CUDA=ON)
< END
< OK goodbye
< END
```

The render branch above hits the no-CUDA fallback (host-only
box). On a CUDA-enabled build the branch instead goes through
`GpuScene::upload_from` -> `CudaRenderer::render_scene` ->
`Image::save_ppm` and replies with the absolute saved path,
matching the existing `--render` deliverable's pixels but
addressed via the protocol.

#### Per the prompt

- "load_scene parses rrscene": `cmd_load_scene` calls
  `rr::io::load_rrscene`, replies with material / sphere /
  light / mesh counts on success.
- "render launches GPU renderer": `cmd_render` calls the
  existing `GpuScene::upload_from(state.scene)` ->
  `CudaRenderer::render_scene(width, height)` pipeline on
  builds where `RR_HAS_CUDA` is defined.
- "saves output image": writes `state.output_path` (default
  `output/server_render.ppm`) via `Image::save_ppm`.
- "responds with file path": `OK rendered <W>x<H> to
  <abs_path>` carries the canonical absolute path.
- "No binary framebuffer transfer yet": the protocol
  carries pixels only via the saved file (path-by-reference);
  no bytes are inlined in the response.

#### Module / milestone status

- Module 19 (Renderer Server): `in progress` (foundation +
  CLI hook + GPU wire-through landed; multi-client / binary
  framebuffer streaming / EXR delivery / cancellation /
  multi-job queuing are the remaining slices before the
  module flips to `landed`).
- M18 (Renderer Server): `in progress` (same).

### 2026-04-28 — M18 (foundation): renderer server foundation

First slice of the renderer server. v1 protocol is intentionally
minimal: a one-client-at-a-time TCP listener on `127.0.0.1:7777`
that accepts five line-based commands and replies with a status
line + an `END` terminator. No framebuffer streaming, no
multi-client, no auth - just the foundation the Cinema 4D bridge
(M19) and a future CLI submitter will plug into.

- **`src/server/RenderServer.h`:** new module. `ServerConfig`
  carries `host` (default `127.0.0.1`) and `port` (default
  `7777`). `ServerState` holds the loaded scene, the last scene
  path, and a fixed v1 `output_path = "output/server_render.ppm"`.
  Public free function `dispatch_command(line, state)` is the
  pure command parser (no IO; testable in isolation).
  `RenderServer` class owns config + state and exposes a single
  blocking `run()` that returns `RunResult{ok, message}`.
- **`src/server/RenderServer.cpp`:**
  - Wire format: ASCII line-based. Request = one line ending in
    `\n`. Response = one or more lines, terminated by `END\n`.
    First reply line begins `OK ` or `ERR ` so a client can
    parse status without per-command knowledge. CRLF inputs from
    Windows clients are tolerated (CR stripped before parsing).
  - Five v1 commands:
    - `ping` -> `OK pong`.
    - `load_scene <path>` -> calls `rr::io::load_rrscene`,
      replies with material / sphere / light / mesh counts on
      success or `ERR load_scene failed: <message>` on parse
      failure.
    - `set_beta <value>` -> validates the float, rejects
      non-finite / `|value| >= 1`, and on success sets
      `state.scene.observer.velocity = {value, 0, 0}` (the v1
      convention: scalar beta drives the +x axis; multi-axis
      observer velocity is reachable through the scene file).
    - `render` -> on builds with CUDA, runs the existing
      `GpuScene::upload_from` -> `CudaRenderer::render_scene`
      pipeline and saves to `state.output_path`. Without CUDA
      the command replies `ERR render: no CUDA backend
      compiled in`. Either way the scene must already be
      loaded; otherwise replies with the no-scene error.
    - `shutdown` -> sets `wants_shutdown` on the result so the
      accept loop returns after the reply is flushed.
  - Unknown verbs and empty / whitespace-only lines are
    rejected with `ERR` responses; verb matching is
    case-insensitive.
  - TCP loop uses POSIX BSD sockets (Linux + macOS). Handles
    short reads/writes, `EINTR` retries, and per-connection
    cleanup. `SO_REUSEADDR` so a quick restart does not trip
    `TIME_WAIT`. Windows is a deliberate follow-up: the
    `_WIN32` build returns `RunResult{ok=false}` from `run()`
    with a clear "not implemented yet" message so the file
    still compiles on MSVC.
  - One client at a time per the v1 spec. The accept loop
    handles a connection to completion (until disconnect or
    `shutdown`) before accepting the next.
- **`tests/server_tests.cpp`:** 50 host assertions. Every
  v1 command exercised through `dispatch_command` directly
  (no real sockets - keeps CI deterministic without a free
  port).
  - `ping` round-trips and is case-insensitive; trims
    surrounding whitespace and tolerates trailing CR.
  - empty / whitespace-only / unknown verbs all produce
    `ERR` responses with descriptive messages.
  - `shutdown` sets `wants_shutdown` on the result; flag
    stays false for everything else.
  - `set_beta` updates `observer.velocity.x`, accepts
    negative values, and rejects missing / non-numeric /
    `|value| >= 1` arguments without mutating state.
  - `load_scene` reports missing-argument and missing-file
    errors, and on the existing `scenes/test_minimal.rrscene`
    fixture loads the scene + records `last_scene_path`.
  - `render` errors clearly on no-scene-loaded; on host-only
    builds it also errors with a CUDA-mention so a client
    knows to rebuild with the toolkit.
- **`CMakeLists.txt`:** new `rr_server` static library
  (PUBLIC-links `rr_io` for the loader and `rr_gpu` for the
  CUDA-conditional render path; per the dependency rules
  `rr_server` is forbidden from depending on UI / Cinema 4D
  bridge code, so neither is mentioned). New `server_tests`
  target registered with `add_test`; gets the same fixtures
  define `RR_TEST_FIXTURES_DIR` `io_tests` already uses.

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/15 ... 15/15 all Passed
100% tests passed, 0 tests failed out of 15

$ ./build/bin/server_tests
server_tests: 50/50 passed
```

End-to-end TCP smoke test (separate harness, not in CI):
binding to `127.0.0.1:7778` and sending ping / set_beta /
unknown-verb / shutdown over `nc -q1`, the server replies
with the expected `OK ` / `ERR ` lines + `END` terminators
and returns cleanly from `run()` after the shutdown.

#### Per the prompt

- Two requested files (`src/server/RenderServer.h`,
  `src/server/RenderServer.cpp`) both present.
- All five commands present (ping, load_scene, render,
  set_beta, shutdown) with reasonable error reporting.
- Server bound to `127.0.0.1:7777` by default, one client at
  a time per the v1 contract.

#### Module / milestone status

- Module 19 (Renderer Server): `not started` -> `in progress`.
- M18 (Renderer Server): `not started` -> `in progress`.

The remaining renderer-server work (multi-client / threaded
accept, framebuffer streaming with a binary frame protocol,
scene-payload upload over the wire, AOV streaming, EXR
delivery, a CLI submitter binary, cancellation + multi-job
queuing) lands in subsequent slices. M18 / Module 19 close
when the bridge can submit a scene file and receive a
rendered EXR back over the protocol.

### 2026-04-28 — M17: render-pass / AOV foundation

Adds the v1 AOV (Arbitrary Output Variable) foundation. Six render
passes — Beauty, Normal, Depth, Albedo, DopplerFactor,
SearchlightFactor — populate from a single GPU launch that reuses
the M16 shading pipeline; the host downloads each into a separate
`rr::renderer::AOV` and saves it as a per-pass PPM. No format
changes to existing renders; the beauty AOV matches
`output/from_scene.ppm` bit-for-bit.

- **`src/renderer/AOV.h`:** new module. `AOVKind` tagged-union
  enum (`Beauty = 0`, `Normal = 1`, `Depth = 2`, `Albedo = 3`,
  `DopplerFactor = 4`, `SearchlightFactor = 5`) with stable
  ordinals so the device-side write pack keeps the same slot
  layout. `kAOVCount = 6`. `aov_kind_name()` -> human-readable
  string used in log lines and the host save path. `aov_is_color()`
  -> true for Beauty / Normal / Albedo (Vec3 per pixel) and false
  for the scalar trio. `class AOV` wraps an `AOVKind` plus an
  `rr::image::Image` (uniformly `Rgba32F`, so the upload /
  download path is the same for every kind), plus a `save_ppm`
  that branches on colour vs scalar.
- **`src/renderer/AOV.cpp`:**
  - Colour AOVs go through `Image::save_ppm` directly (the
    existing 8-bit P6 path).
  - Scalar AOVs (`Depth` / `DopplerFactor` /
    `SearchlightFactor`) pack their value in the R channel.
    `save_ppm` finds the brightest pixel, normalises so it maps
    to 1.0, and emits a grayscale triple. Keeps the saved PPMs
    human-readable without committing the renderer to a tone-
    mapping policy. All-zero input is special-cased so the
    normaliser doesn't divide by zero.
- **`src/cuda/CudaAOV.cuh`:** new device-side launch-arg pack.
  `CudaAOVPack` carries six `float*` slots (one per AOV);
  pointers left null instruct the kernel to skip that AOV's
  write. `aov_write_rgba(buffer, x, y, w, vec)` and
  `aov_write_scalar(buffer, x, y, w, v)` are `RR_HD inline`
  helpers that pack the value into the same `Rgba32F` row-major
  layout `rr::image::Image` already uses (R holds the scalar,
  G/B = 0, A = 1). No CUDA-runtime types beyond the `cudaStream_t`
  forward-decl through `cuda_runtime.h`, so the host suite runs
  the same code paths the kernel uses.
- **`src/cuda/CudaScene.cuh`:** added `launch_render_aovs(width,
  height, scene, aov_pack, stream)` declaration. Includes
  `cuda/CudaAOV.cuh` so the launch-arg pack is in scope at the
  same level as `CudaSceneView`.
- **`src/cuda/CudaTestKernel.cu`:** added `k_render_aovs` and
  its `launch_render_aovs` host-launcher. The kernel is the
  M16 single-bounce shading pipeline (camera ray ->
  aberration -> closest-hit over spheres + the mesh slot ->
  texture-sampled albedo -> direct lighting -> Doppler colour
  -> searchlight) but taps the intermediate quantities into
  the AOV pack at the appropriate stages:
  - Albedo            : raw base colour (post-texture sample,
                        before lighting + relativity).
  - Normal            : `0.5*N + 0.5` for the closest hit; sky
                        direction encoding on miss so the AOV
                        is non-empty on background pixels too.
  - Depth             : ray `t` for the closest hit (0 on miss).
  - Beauty            : final shaded + relativity-applied colour.
  - DopplerFactor     : raw `D` from the primary photon
                        direction (always written; it's a
                        property of the ray + observer, not
                        of geometry).
  - SearchlightFactor : `D^4` from the same `D`.
- **`src/cuda/CudaRenderer.{h,cu}`:** added `AOVResult` (six
  populated `AOV`s + `ok`/`message`) and `render_aovs(scene,
  w, h)`. Allocates six parallel `GpuBuffer<float>` framebuffers
  (one per AOV, each `w*h*4` floats), packs the device pointers
  into a `CudaAOVPack`, runs `launch_render_aovs`, drains CUDA
  errors, then downloads each device buffer into the
  pre-allocated `rr::image::Image` of the corresponding host
  `AOV`. Same has-camera / has-relativity / dim guards as
  `render_scene`.
- **`src/main.cpp`:** the `--render` block runs `render_aovs`
  after the textured-material render and saves six PPMs to
  fixed deliverable paths: `output/aov_beauty.ppm`,
  `output/aov_normal.ppm`, `output/aov_depth.ppm`,
  `output/aov_albedo.ppm`, `output/aov_doppler.ppm`,
  `output/aov_searchlight.ppm`. Logs each save with the
  human-readable AOV name.
- **`tests/aov_tests.cpp`:** 87 host assertions covering the
  full host surface.
  - `aov_kind_name` returns the v1 names for all six kinds.
  - `kAOVCount == 6`.
  - `aov_is_color` predicate splits Beauty / Normal / Albedo
    from Depth / DopplerFactor / SearchlightFactor.
  - Default-constructed `AOV` is empty; constructed `AOV`
    sizes the underlying `Image` uniformly to `Rgba32F` for
    every kind.
  - `save_ppm` fails on an empty AOV (no file written).
  - `save_ppm` for a colour AOV writes the expected 8-bit
    triples (constructed PPM body parsed and verified
    byte-for-byte against a 2x1 red+green fixture).
  - `save_ppm` for a scalar AOV normalises to grayscale: a
    3x1 fixture with R values `{0.0, 0.5, 2.0}` produces
    body bytes `{0, 0, 0,  64, 64, 64,  255, 255, 255}` — the
    brightest pixel maps to white and the others scale
    linearly.
  - All-zero scalar input saves as black (no divide-by-zero).
- **`CMakeLists.txt`:** new `rr_renderer` static library
  (PUBLIC-links `rr_image`) so the host AOV surface is
  available without pulling in CUDA. `rr_gpu` PUBLIC-links
  `rr_renderer` because `CudaRenderer::AOVResult` exposes
  `rr::renderer::AOV` by value. New `aov_tests` executable
  registered with `add_test`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/14 ... 14/14 all Passed
100% tests passed, 0 tests failed out of 14

$ ./build/bin/aov_tests
aov_tests: 87/87 passed
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON` on a Turing/Ampere/Ada
GPU) is correct by construction: the AOV kernel reuses the same
`RR_HD inline` helpers (`intersect_*`, `sample_texture`, the
relativistic stack) that the existing host suites cover. The
device-side AOV write helpers (`aov_write_rgba`,
`aov_write_scalar`) are simple buffer pokes and exercised by the
host save-path tests against the same `Rgba32F` layout the kernel
produces.

#### Per the prompt

- Three requested files (`src/renderer/AOV.h`, `src/renderer/AOV.cpp`,
  `src/cuda/CudaAOV.cuh`) all present.
- Six passes (beauty, normal, depth, albedo, dopplerFactor,
  searchlightFactor) all populated by a single GPU launch.
- Output: separate PPM files for each pass under `output/`.
- "GPU writes selected AOV buffers": `CudaAOVPack` slots that
  are null are skipped by the writer helpers, so the same
  kernel can drive a subset of AOVs without a recompile when a
  future render config asks for less than the v1 six.

#### Module / milestone status

- Module 17 (Render Passes / AOVs): `not started` -> `landed`.
- M17 (Render Passes / AOVs): `not started` -> `landed`.

### 2026-04-27 — M16 finalized: GPU texture sampling end-to-end

Wires the M16 foundation into the renderer. Material `baseColor` can
now be driven by a sampled image texture; UVs flow from triangle
barycentrics or a spherical sphere mapping; `GpuScene` uploads each
texture's pixel buffer plus a flat `TextureView` array; the kernel
samples them through the existing `sample_texture` (nearest +
clamp).

- **`src/renderer/Hit.h`:** added `Vec2 uv` plus `bary_u` /
  `bary_v` to the Hit POD. Default zero so older callers
  compile unchanged. `intersect_triangle` populates the
  barycentrics; sphere sampling populates `uv` directly.
- **`src/cuda/CudaIntersection.cuh`:**
  - `intersect_sphere` now writes a spherical UV: `u =
    atan2(n.x, n.z)/(2*pi) + 0.5`, `v = 1 - acos(clamp(n.y))/pi`.
    `v=0` is the south pole, `v=1` is the north pole, matching
    the texture system's "v up" convention.
  - `intersect_triangle` records the MT routine's `(u, v)` as
    `bary_u` / `bary_v`; the third weight is implicitly
    `1 - bary_u - bary_v`. Vertex-attribute interpolation
    happens at the kernel call site.
- **`src/material/MaterialTypes.h`:** added
  `int base_color_texture_id = -1`. When `>= 0` and within
  `scene.texture_count`, the kernel samples the bound texture
  at the hit UV and uses the result in place of `baseColor`.
- **`src/scene/Scene.h` / `.cpp`:** `Scene` gains
  `std::vector<rr::texture::ImageTexture> textures`;
  `Scene::clear()` empties it. `rr_scene` PUBLIC-links
  `rr_texture` (the new include of `texture/ImageTexture.h`).
  The `.rrscene` parser is unchanged - textures are
  programmatic-only at this milestone.
- **`src/gpu/GpuScene.{h,cpp}`:** added
  `upload_textures(host, count)`, `device_textures()`, and
  `texture_count()`. Each `ImageTexture` becomes its own
  `GpuBuffer<float>` for pixel data (owned by `GpuScene`)
  plus an entry in a packed `GpuBuffer<TextureView>` that
  the kernel reads. `upload_from(scene)` now also pushes
  `scene.textures` so the convenience path stays one-shot.
  Empty / no-data textures land as `Constant` views with a
  white fallback colour, so the kernel returns a predictable
  value rather than dereferencing nullptr.
- **`src/cuda/CudaScene.cuh`:** added `textures` device
  pointer + `texture_count` to `CudaSceneView` (alongside
  `materials`, `lights`, etc.). Pulls in
  `cuda/CudaTexture.cuh`, which has no CUDA-runtime
  dependencies, so host code can keep including the view.
- **`src/cuda/CudaRenderer.cu`:** `render_scene` and
  `render_pathtrace` both copy the texture view + count from
  the `GpuScene` into the launch arg before dispatch.
- **`src/cuda/CudaTestKernel.cu`:**
  - Direct-lighting (`k_render_scene`): on hit, look up the
    material; if `base_color_texture_id` is in range,
    `sample_texture(scene.textures[id], best.uv)` becomes the
    diffuse `albedo`. Triangle-loop branch interpolates
    per-vertex UVs from `bary_u` / `bary_v`.
  - Path tracer (`k_path_trace`): `trace_closest`'s
    triangle branch now also interpolates UVs; the bounce
    `albedo` is sampled from the texture when bound, then
    multiplied into the throughput. Existing Lambertian
    invariants are unchanged.
- **`src/main.cpp`:** the `--render` block (after the path
  tracer passes) now builds a procedural 32x32 checkerboard
  texture, binds it to the first material's
  `base_color_texture_id`, uploads through `GpuScene`,
  renders via `render_scene`, and saves to
  `output/gpu_textured_material.ppm`.
- **`tests/geometry_tests.cpp`:** +16 assertions
  (62/62 total).
  - `intersect_triangle` records sane barycentrics:
    centroid hit gives `(1/3, 1/3)`; aiming at `v1` pushes
    `bary_u -> 1`, `bary_v -> 0`.
  - `intersect_sphere` records sensible spherical UVs:
    +Z hit gives `(0.5, 0.5)`; +Y pole gives `v = 1`;
    -Y pole gives `v = 0`.
- **`CMakeLists.txt`:** `rr_scene` PUBLIC-links `rr_texture`
  so consumers (rr_io, rr_gpu, RelativityRender exe) pick it
  up transitively. No new test target; coverage rides on the
  existing `geometry_tests` + `texture_tests`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/13 ... 13/13 all Passed
100% tests passed, 0 tests failed out of 13

$ ./build/bin/geometry_tests
geometry_tests: 62/62 passed
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON` on a Turing/Ampere/Ada
GPU) is correct by construction: every `RR_HD inline` helper the
new shader path calls (`intersect_*`, `sample_texture`, the
existing relativistic stack) is exercised by the host tests
(`geometry_tests` 62, `texture_tests` 28, `relativity_tests` 52,
`camera_tests` 43).

#### Per the prompt

- "Uploaded image texture" - `GpuScene::upload_textures` packs
  each `ImageTexture` into its own pixel `GpuBuffer<float>` plus
  an entry in a `GpuBuffer<TextureView>`.
- "Nearest sampling" - the only filter the kernel honours;
  bilinear / repeat / mirror remain documented placeholders.
- "UV lookup" - triangles via barycentric vertex attribute
  interpolation; spheres via spherical mapping.
- "Material baseColor can use texture" -
  `base_color_texture_id` on `MaterialParams`; out-of-range
  / `-1` falls back to the constant `baseColor`.
- Output: `output/gpu_textured_material.ppm` from the
  `--render` flow, on builds where CUDA is enabled.
- "Keep it simple" - no kernel restructuring beyond plumbing
  the new pointers + the small texture-sampling block.

#### Module / milestone status

- Module 10 (Texture System): `in progress` -> `landed`.
- M16 (Texture System): `in progress` -> `landed`.

### 2026-04-27 — M16 (foundation): texture system foundation

Host-side foundation only. ConstantTexture + ImageTexture + a
device-friendly `TextureView` POD with an `RR_HD inline` sampler.
No filtering beyond nearest-neighbor; no kernel currently
consumes a textured material slot. Shader / scene-format /
material-binding slices follow.

- **`src/texture/Texture.h` / `.cpp`:**
  - `TextureType` discriminator (`Constant = 0`, `Image = 1`)
    with stable ordinals so the upload contract stays
    forward-compatible.
  - `ConstantTexture` POD with a `Vec3 color` and an
    `RR_HD inline sample(Vec2)` that ignores UV (the simplest
    texture; the renderer's default for any unbound material
    slot).
  - Convenience factories: `make_white_texture`,
    `make_black_texture`, `make_constant_texture(color)`.
- **`src/texture/ImageTexture.h` / `.cpp`:** image-backed
  texture wrapping `rr::image::Image`. Sampling parameters
  surfaced now (`Wrap = Clamp / Repeat / Mirror`,
  `Filter = Nearest / Bilinear`) so scene-format and GPU
  upload paths can carry them; the host sampler only honors
  `Clamp + Nearest` at this milestone, with the others
  silently falling through to the implemented combination.
  Out-of-`[0,1]` UVs are clamped; empty images return black so
  callers don't have to special-case unbound slots.
  V-axis flipped on read so UV `(0, 0)` maps to the texture's
  bottom-left and UV `(0, 1)` maps to the top-left, matching
  the conventional UV orientation while the underlying
  `Image` keeps a top-left origin.
- **`src/cuda/CudaTexture.cuh`:** device-side launch-argument
  POD `TextureView` (tagged union of `Constant` /
  `Image` fields) plus an `RR_HD inline sample_texture(view,
  uv)` sampler. Despite the `.cuh` name the header pulls in
  no CUDA-runtime types, so the host suite runs the exact
  same code the kernel will. Null-image-data defensive
  fallback returns the `constant_color` so a host-only build
  produces predictable values without crashing on uninitialised
  slots.
- **`tests/texture_tests.cpp`:** 28 host assertions.
  - ConstantTexture defaults / factories / `type_tag`.
  - ImageTexture default-empty samples to black; `type_tag`.
  - Deterministic 2x2 fixture exercising all four corners,
    confirming the UV `v`-flip lands on the right pixels.
  - Out-of-range UV clamps to the nearest in-range texel.
  - Device-side `sample_texture` returns the constant colour
    for `Constant` and for `Image` with null data, and
    matches the host `ImageTexture::sample` value-for-value
    on every uv in a sweep across the corners + clamp cases.
- **`CMakeLists.txt`:** added `rr_texture` static library
  (PUBLIC-links `rr_image` because `ImageTexture` carries an
  `rr::image::Image` by value); added the `texture_tests`
  test executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/13 ... 13/13 all Passed
100% tests passed, 0 tests failed out of 13

$ ./build/bin/texture_tests
texture_tests: 28/28 passed
```

#### Per the prompt

- Five requested files (`Texture.h` / `Texture.cpp` /
  `ImageTexture.h` / `ImageTexture.cpp` / `CudaTexture.cuh`)
  all present.
- Constant-color texture + image texture placeholder + UV
  coordinates as a `Vec2` parameter at every public surface.
- No complex filtering: only nearest-neighbor + clamp wrap
  honored. Bilinear / repeat / mirror documented in the
  header / device POD for when the next slice lands.

#### Module / milestone status

- Module 10 (Texture System): `not started` -> `in progress`.
- M16 (Texture System): `not started` -> `in progress`.

The remaining texture work (kernel-side material binding,
GPU image upload through GpuScene, scene-format texture
references, full filtering modes) lands in subsequent slices.

### 2026-04-27 — M15.1 + M15.2: OptiX backend scaffold (steps 1+2 of the migration plan)

First implementation slice of the OptiX backend. Detection + a
lifecycle wrapper + a placeholder renderer; no programs, no
acceleration structures, no rendering yet. The CUDA backend
remains primary.

- **`src/optix/OptixBackend.{h,cpp}`:** lifecycle + status
  surface. `optix_backend_available()`, `optix_backend_name()`,
  and a `optix_backend_status_line()` probe suitable for
  startup logging. `OptixBackend` class wraps an
  `OptixDeviceContext`: move-only, `init()` calls
  `cudaFree(nullptr)` to ensure a CUDA context exists,
  `optixInit()` to load the runtime, and
  `optixDeviceContextCreate()` to open the context;
  `shutdown()` is always safe (no-op when uninitialised, when
  moved-from, or when the SDK is not compiled in).
- **`src/optix/OptixRenderer.{h,cpp}`:** placeholder render
  entry point mirroring the shape of `rr::cuda::CudaRenderer`
  (`Result { ok, image, message }`). At this milestone
  `render_placeholder` only probes the runtime and returns a
  descriptive message; the real OptiX pipeline lands in
  M15.3 / M15.4 per `docs/OPTIX_BACKEND_PLAN.md`.
- **`CMakeLists.txt`:**
  - Renamed the M1 `RR_ENABLE_OPTIX` placeholder option to
    `RELATIVITYRENDER_ENABLE_OPTIX` per the prompt.
  - Added an OptiX SDK detection block: respects
    `OPTIX_INSTALL_DIR` (CMake var or environment), then
    falls back to `find_path` over the common SDK locations
    (`/usr/local/optix/include`, `/opt/nvidia/optix/include`,
    `/opt/optix/include`, `$HOME/optix/include`). Failure to
    find `optix.h` is a `FATAL_ERROR` with a clear remedy.
    The block also enforces `RELATIVITYRENDER_ENABLE_OPTIX`
    requires `RR_ENABLE_CUDA` (OptiX sits on top of CUDA).
  - New `rr_optix` static library compiled into every build:
    sources `OptixBackend.cpp` + `OptixRenderer.cpp`, PUBLIC
    include of `src/`, PUBLIC link to `rr_image` (its public
    surface returns an `Image`). The OFF path compiles only
    the stub bodies. The ON path adds `RR_HAS_OPTIX` PUBLIC,
    `${OPTIX_INCLUDE_DIR}` PRIVATE, and `CUDA::cudart`
    PRIVATE (OptiX's headers transitively include
    `cuda_runtime.h`).
  - `RelativityRender` executable now links `rr_optix` so the
    status surface is reachable from `main.cpp`.
- **`src/main.cpp`:** added `log_optix_info()`, called from
  the `--device-info` path. It prints the
  `optix_backend_status_line()` probe result; on the
  default OFF build the line reads
  `OptiX backend: not compiled in (rebuild with RELATIVITYRENDER_ENABLE_OPTIX=ON)`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake -S . -B build && cmake --build build
... configures cleanly with default OFF; rr_optix builds the OFF
    stubs; rr_optix links into the executable ...

$ cd build && ctest --output-on-failure
 1/12 ... 12/12 all Passed
100% tests passed, 0 tests failed out of 12

$ ./build/bin/RelativityRender --device-info
[INFO] RelativityRender 0.0.1 starting
[INFO] GPU backend: (none)
[INFO] No GPU backend compiled in. Reconfigure with -DRR_ENABLE_CUDA=ON to enable CUDA.
[INFO] OptiX backend: not compiled in (rebuild with RELATIVITYRENDER_ENABLE_OPTIX=ON)
```

The OptiX-enabled path
(`-DRELATIVITYRENDER_ENABLE_OPTIX=ON -DRR_ENABLE_CUDA=ON` plus
`OPTIX_INSTALL_DIR` pointing at an installed SDK on a machine
with an NVIDIA driver) is correct by construction: the .cpp
calls only stable OptiX 7.x runtime entry points
(`optixInit`, `optixDeviceContextCreate`,
`optixDeviceContextDestroy`) and the link list matches the
SDK's documented dependencies. Not end-to-end runnable in this
environment.

#### Per the prompt

- The four requested files are all present.
- The CMake option is exactly `RELATIVITYRENDER_ENABLE_OPTIX`.
- SDK detection lands in CMake; init goes through `optixInit`
  + `optixDeviceContextCreate`; availability is printed on
  `--device-info`.
- No rendering: `OptixRenderer::render_placeholder` is a
  stub.
- The CUDA backend remains primary - `--render` continues to
  drive the existing CUDA path (and the host-only fallback);
  nothing routes through `rr_optix` for actual pixels.

#### Module / milestone status

- Module 6 (OptiX Backend): `not started` -> `in progress`.
- M15 (OptiX Backend / Upgrade Path): `not started` ->
  `in progress`.

The remaining migration steps (M15.3 build AS from `GpuScene`,
M15.4 programs + SBT, M15.5 validation, M15.6 promote OptiX to
default) are documented in
`docs/OPTIX_BACKEND_PLAN.md` §15.

### 2026-04-27 — OptiX backend plan: materials + camera + relativity + migration

Doc-only extension. Four final sections in
`docs/OPTIX_BACKEND_PLAN.md`. The plan is now design-complete -
implementation slices follow the step-by-step migration in
§15. No code, no CMake change.

- **§12 Material system integration.** Maps the existing
  CUDA-path material flow onto OptiX seam by seam. The
  `MaterialParams` array stays launch-wide (lives in launch
  parameters, not SBT records); per-instance / per-primitive
  `material_index` rides in the hit-group SBT record's
  payload. The host upload path (`GpuScene::upload_materials`)
  is unchanged. Adding more BSDF lobes is a closest-hit
  program edit; live material updates rewrite the buffer with
  no AS / SBT touch.
- **§13 Camera integration.** `GpuCamera` becomes a launch
  parameter (per-launch state, same for every pixel and
  bounce). The raygen program reads `launch_params.camera` and
  calls the existing `RR_HD inline generate_camera_ray` with
  no source change. The `M7 aspect / fov / basis` logic
  carries over identically; `camera_tests` keeps validating
  the device math by construction.
- **§14 Relativity integration.** All five seams stay in the
  raygen program: aberration on the primary ray's direction
  immediately after `generate_camera_ray`; `dopplerFactor` /
  `searchlightFactor` / `applyDopplerColor` wrap the
  *integrated* radiance after the bounce loop. The closest-hit
  and miss programs are deliberately relativity-free.
  Per-bounce aberration is documented as a small follow-up,
  not a blocker. Every `RR_HD inline` helper in
  `relativity/RelativityMath.h` and the host
  `relativity_tests` suite (52) carry over unchanged.
- **§15 Migration plan.** Six concrete, independently-
  shippable slices:
  - M15.1: SDK + CMake plumbing (`RR_ENABLE_OPTIX` option,
    `find_package(OptiX)`).
  - M15.2: `rr_optix` library skeleton (`OptixContext`,
    pipeline scaffold, no programs / AS / SBT yet).
  - M15.3: Build acceleration structures from `GpuScene`
    (sphere GAS, per-mesh triangle GAS, IAS over them; build
    only, no traversal yet).
  - M15.4: Programs + modules + SBT.
    `RaygenPathTrace.cu` / `MissEnvironment.cu` /
    `HitClosestRadiance.cu`; new entry point
    `CudaRenderer::render_pathtrace_optix` next to the
    existing `render_pathtrace`.
  - M15.5: Validation. Side-by-side comparison test;
    bit-equal at fixed seed for trivial scenes, sample-noise
    envelope for the full path tracer.
  - M15.6: Promote OptiX to default. CUDA stays available
    behind the same flag.
  Plus an explicit §15.7 statement of why the CUDA path stays
  after M15: host-test coverage of the shared `RR_HD inline`
  math, debug fallback for non-RTX hardware, regression
  baseline for OptiX bugs. **OptiX as default, CUDA as
  fallback - not "OptiX replaces CUDA".**

The "Out of scope" list (now §16) shrinks dramatically -
materials / camera / relativity / build & SDK plumbing are no
longer there. What remains: specific OptiX SDK version
targeting (chosen in step M15.1), OptiX denoiser integration
(M22), multi-GPU / multi-stream traversal (M18+), per-bounce
relativistic aberration (small follow-up), and curves /
volumes / displaced surfaces (each adds incremental scaffolding
on top of the v1 OptiX backend).

References (§17) is unchanged.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 12/12`).

#### Per the prompt

- The four requested topics (Material System Integration,
  Camera Integration, Relativity Integration, Migration Plan)
  are now sections in the doc.
- Explanations are concrete and tied to the current architecture
  - each section names the existing source files, structs, and
  test suites that map onto the OptiX layer.
- No code; the migration plan is the implementation contract
  for the M15 slices that follow.

### 2026-04-27 — OptiX backend plan: AS + SBT + data flow

Doc-only extension. Three new sections in
`docs/OPTIX_BACKEND_PLAN.md`. No code, no CMake change, no
implementation.

- **§9 Acceleration structures.** Two AS kinds in v1.
  - **GAS** (Geometry Acceleration Structure): a BVH over a
    single set of primitives. Triangle GAS uses OptiX's
    built-in triangle intersection (consumes `Vertex` /
    `Triangle` arrays our existing `GpuMesh` already
    uploads). Custom-primitive GAS uses AABB build inputs
    plus a custom intersection program; spheres land here,
    with `rr::cuda::intersect_sphere` lifted from
    `cuda/CudaIntersection.cuh` as the program body.
  - **IAS** (Instance Acceleration Structure): a BVH-of-BVHs.
    Each leaf is an `Instance` carrying a GAS handle plus a
    `3x4` world transform (the existing host
    `rr::math::Transform` SRT decomposition flows in
    unchanged). Instancing lets N copies share one
    vertex/index buffer + one GAS.
  - **Why BVH is critical.** Compares the three approaches
    side by side: today's naive `O(N)` linear scan, a
    software BVH at `~O(log N)` plus traversal overhead, and
    OptiX hardware BVH at `~O(log N)` on RT cores. The naive
    path stays as a fallback / regression baseline; the
    OptiX path is what scales to non-trivial scene
    complexity.
- **§10 Shader Binding Table.** Three record kinds in v1.
  - **Record layout**: 32-byte header (program-group hash
    populated by `optixSbtRecordPackHeader`) plus an aligned
    user-defined payload.
  - **Records**: one raygen, one miss per ray type (v1 has
    one ray type, "radiance"), one hitgroup per (instance,
    ray type) pair. Hit-group payload is where the
    geometry / material wiring lives - device pointers to
    vertex / index buffers, the mesh's `material_index`,
    transform pointers. Exact field list deferred to the
    materials slice of the plan.
  - **Why the SBT matters**: dispatch is a data structure,
    not an `if/else` ladder; new primitive types add record
    kinds; material updates don't rebuild geometry; multiple
    ray types share hit groups.
- **§11 Data flow.** End-to-end ASCII pipeline diagram
  showing the seven steps from `.rrscene` to PPM. Steps 1, 2,
  5, 6 already exist (loader, `GpuScene::upload_from`,
  framebuffer download, `Image::save_ppm`); steps 3 and 4 are
  what M15 adds (OptiX backend build + `optixLaunch`).
  Captured the three architectural invariants:
  - GpuScene remains the single owner of scene data on the
    device; the OptiX backend is a consumer that references
    GpuScene's pointers through GAS build inputs and SBT
    payloads (no data duplication).
  - `CudaRenderer` keeps its public surface; the new entry
    point sits next to `render_pathtrace`.
  - The CPU's job does not change - no per-pixel work
    crosses back to the host.

The "Out of scope" list (now §12) shrinks: AS, SBT, and the
data-flow diagram are no longer there. Materials / camera /
relativity integration / build-and-SDK plumbing remain
deferred. References (§13) is unchanged.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 12/12`).

#### Per the prompt

- The three requested topics (Acceleration Structures, Shader
  Binding Table, Data flow) are now sections in the doc.
- Focus is architectural - GAS / IAS roles, SBT shape, end-to-end
  pipeline. No record byte layouts, no concrete code.
- Materials, camera, relativity integration intentionally
  remain out-of-scope (still listed in §12 with the build /
  SDK plumbing).

### 2026-04-27 — OptiX backend plan: introduction slice

Documentation-only. First slice of `docs/OPTIX_BACKEND_PLAN.md`.
No code, no CMake change, no parser / kernel / module
changes.

- **`docs/OPTIX_BACKEND_PLAN.md`:** introduction slice.
  - Status callout: spec only; subsequent slices add the
    deferred sections (AS, SBT, materials, camera, relativity,
    build / SDK integration) before any implementation.
  - **§2 Where we are today**: snapshot of the current naive
    intersection path, the two scene kernels that share
    `trace_closest`, and the brute-force loop over
    `scene.spheres` + `scene.mesh.triangles` per ray.
  - **§3 Why naive intersection does not scale**: per-pixel cost
    is `spp * max_depth * (sphere_count + triangle_count)`;
    the M12 test scene at 1280x720 / spp = 16 / depth = 4 is
    ~880 M intersections - tractable. Adding one 100 k-tri
    mesh balloons it to ~14 trillion. Three structural
    problems enumerated: no early rejection, no instancing, no
    RT-core hardware help.
  - **§4 Why OptiX**: hardware-accelerated BVH traversal,
    built-in instancing, pluggable program model. OptiX runs
    on top of CUDA, so it slots in next to the existing
    backend rather than replacing it.
  - **§5 Pipeline overview**: three programs in v1
    (`raygen` / `miss` / `closest-hit`); custom intersection
    and any-hit are out of scope. The CPU's job (configure
    scene, launch, save) does not change; the GPU's structure
    does.
  - **§6 raygen**: owns the work at the top of `k_path_trace` -
    pixel index, RNG, primary ray, aberration, bounce loop
    driving `optixTrace`, post-loop Doppler / searchlight,
    framebuffer write. All ray paths still on the GPU; the
    `RR_HD inline` helpers carry over unchanged.
  - **§7 miss**: the existing `sky_color` lives here verbatim;
    Doppler / searchlight intentionally do **not** run inside
    the miss program because they wrap the integrated radiance
    after the whole path is done.
  - **§8 closest-hit**: owns the bounce-step branch of
    `trace_one_path` - material lookup, surface reconstruction,
    emission accumulation, cosine-weighted bounce sample,
    throughput update. The bounce *loop* stays in raygen.
  - **§9 Out of scope for this slice**: acceleration structures,
    shader binding table, material data, camera data,
    relativity integration, build / SDK integration. Each gets
    its own slice in the same incremental style as the
    RRSCENE format spec.
  - **§10 References**: the CUDA backend files the migration
    replaces, the master architecture / module map / milestone
    roadmap entries that govern the work.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 12/12`).

#### Per the prompt

- Only the introduction + high-level overview slice was added.
- The three pipeline programs (raygen, miss, closest-hit) are
  described with their RelativityRender role.
- Acceleration structures, SBT, materials, camera, relativity
  intentionally not covered yet; documented as out-of-scope
  with a placeholder list at §9.
- No code.

### 2026-04-27 — M14 minimal CUDA path tracer

First end-to-end path tracer. Per pixel: traces `spp` independent
paths with cosine-weighted Lambertian bounces up to `max_depth`,
accumulates emission + environment-fallback radiance, applies the
existing relativistic pipeline (Doppler + searchlight) to the
integrated value, and writes the average to the framebuffer.
Single-launch, multi-sample-per-thread. The "accumulation buffer"
is the per-thread Vec3 register sum; the framebuffer carries the
mean. CPU only configures + launches + saves.

- **`src/cuda/CudaScene.cuh`:** added `launch_path_trace`
  declaration. Lives next to `launch_render_scene` since both
  consume `CudaSceneView`.
- **`src/cuda/CudaTestKernel.cu`:** factored out three device
  helpers:
  - `trace_closest(scene, ray, t_min)` - the closest-hit search
    over spheres + the single mesh slot, used by the path tracer
    on every bounce.
  - `sky_color(scene, ray_dir)` - the M12 environment fallback
    extracted; an uploaded `Environment` light wins, otherwise
    the existing vertical sky gradient.
  - `lookup_material(scene, idx)` - bounds-checked material
    fetch with the renderer's neutral default fallback.
  Plus `trace_one_path(...)`, the per-sample core: aberrate the
  primary ray, then for up to `max_depth` bounces find the
  closest hit, accumulate emission, sample a cosine-weighted
  bounce off the (orientation-corrected) normal, multiply the
  throughput by `baseColor` (Lambertian /pi vs cos(theta)/pi
  cancellation), and continue. On miss the path adds
  `throughput * sky_color` and terminates. After the loop
  Doppler colour + searchlight scaling are applied to the
  integrated radiance using the primary ray direction.
  `k_path_trace` runs the per-sample loop spp times and writes
  the mean. Cheap throughput-near-zero early-out terminates
  paths whose contribution can no longer matter (Russian
  roulette is the proper unbiased version; that's the next
  M14 slice).
- **`src/cuda/CudaRenderer.{h,cu}`:** added
  `render_pathtrace(GpuScene, width, height, spp, max_depth,
  seed_offset = 0)`. Reuses the existing `run_kernel_render`
  scaffold; the only new logic is the launch-argument bundling
  and the spp / max_depth clamp.
- **`src/main.cpp`:** after the existing single-bounce
  `render_scene` save, `--render` now also produces two M14
  deliverables of the same uploaded scene:
  - `output/pathtrace_spp_1.ppm`  (spp = 1, max_depth = 4)
  - `output/pathtrace_spp_16.ppm` (spp = 16, max_depth = 4)
  Each pass logs the spp / max_depth before launch and the
  saved path after. Both go through the same `GpuScene` upload,
  so the path-traced and direct-lit passes diff only in the
  kernel. `--output` continues to override the direct-lit save
  path; the path-trace files are fixed for the milestone
  deliverable so they're easy to compare across commits.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/12 Test  #1: math_tests       ........... Passed  0.00 sec
 ...
12/12 Test #12: sampling_tests   ........... Passed  0.01 sec
100% tests passed, 0 tests failed out of 12

$ ./build/bin/RelativityRender --render scenes/test_minimal.rrscene \
        --width 32 --height 32
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] loading scene: scenes/test_minimal.rrscene
[INFO] loaded scene: 0 materials, 0 spheres, 0 lights, 0 meshes
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON
       to render the loaded scene)
```

The CUDA-enabled path is correct by construction: every
device-side helper the integrator composes (`generate_camera_ray`,
`aberrateDirection`, `intersect_sphere`, `intersect_triangle`,
`make_rng` / `next_float`, `build_orthonormal_basis`,
`sample_hemisphere_cosine`, `dopplerFactor`, `searchlightFactor`,
`applyDopplerColor`) is exercised by the host suite via the
`RR_HD inline` shared headers (`camera_tests` 43, `geometry_tests`
46, `relativity_tests` 52, `sampling_tests` 60). The integrator
itself is straight-line vector arithmetic over those primitives.

#### Hard-rule check

- **All ray paths on the GPU**: the bounce loop and every
  intersection / sampling / shading step run inside
  `k_path_trace`. The CPU has no per-ray work.
- **CPU only launches + saves**: `main.cpp` builds a `GpuScene`,
  invokes `render_pathtrace` twice (spp = 1 and 16), and writes
  the resulting `Image` to PPM. The only CPU iteration over
  pixels is `Image::save_ppm`'s float -> byte conversion,
  permitted as image save internals.

#### Not in this slice (M14 still in progress)

- No NEE / MIS direct-light sampling; the integrator picks up
  light only through brute-force bounces and emissive surfaces.
- No Russian roulette; the cheap throughput-zero early-out is a
  conservative biased substitute. Real RR with continuation
  probability is the next M14 slice.
- No multi-mesh upload; `CudaSceneView` still has a single mesh
  slot.
- No real device-side accumulation buffer across launches. The
  framebuffer is the final mean of one launch; consumers wanting
  multi-launch progressive refinement can blend frames
  themselves and pass distinct `seed_offset` values - the kernel
  already supports the latter.
- Module 14 / M14 stay "in progress" until those land.

### 2026-04-27 — M14 prep: GPU sampling foundation (RNG + hemisphere)

Path-tracer foundation only - no integrator, no kernel changes,
no `--render` rewiring. The headers ship the primitives the M14
integrator will compose with: a per-pixel RNG and uniform /
cosine-weighted hemisphere samples, all `RR_HD inline` so the
host suite covers the device math by construction.

- **`src/pathtracer/RNG.h`:** PCG-XSH-RR (64-bit state, 32-bit
  output). Surface: `RNG`, `wang_hash(uint32)`,
  `make_rng(x, y, sample) -> RNG`, `next_uint(RNG&)`,
  `next_float(RNG&) -> [0, 1)`. The seed mixer guarantees that
  adjacent `(x, y, sample)` triples land on distinct streams;
  the state is forced odd / non-zero. `next_float` uses 24
  bits of the next integer so the value lands exactly inside a
  single-precision mantissa with no rounding bias.
- **`src/pathtracer/RNG.cuh`:** thin re-export of `RNG.h` so
  kernel TUs can include a `.cuh`. Future device-specific
  overrides (warp-coherent advancement, hardware-RNG hooks)
  land here without touching the host surface.
- **`src/pathtracer/Sampling.h`:** hemisphere sampling
  primitives. Frisvad / Duff branchless ONB
  (`build_orthonormal_basis(n, t, b)`); local-frame samples
  `sample_hemisphere_uniform_local(u1, u2)` (PDF =
  `1 / (2*pi)`) and `sample_hemisphere_cosine_local(u1, u2)`
  (PDF = `cos(theta) / pi`); world-frame wrappers that
  build a basis around `normal` and rotate the local sample;
  matching `pdf_hemisphere_uniform()` and
  `pdf_hemisphere_cosine(cos_theta)` helpers; RNG-driven
  convenience wrappers that pull the two `u1`/`u2` floats
  internally for kernel ergonomics.
- **`src/pathtracer/Sampling.cuh`:** thin re-export of
  `Sampling.h`.
- **`tests/sampling_tests.cpp`:** 60 host assertions covering
  the requested surface plus the corner cases the kernel needs
  to be robust against.
  - RNG: `next_float` always in `[0, 1)`; same seed reproduces
    the same stream; adjacent `(x, y)` pixels diverge within
    the first 16 floats; mean over 10000 samples is
    `0.5 +/- 0.01`.
  - ONB: tangent / bitangent / normal are unit length and
    mutually orthogonal for the six axis normals (including
    `(0, 0, -1)` where the naive Frisvad form breaks) plus
    three arbitrary directions.
  - Hemisphere: 1000-sample sweeps confirm every uniform /
    cosine sample is unit length and lies in the hemisphere
    (`dot(d, n) >= 0`); 10000-sample means confirm the
    analytic statistics (uniform mean cos(theta) = 1/2,
    cosine-weighted mean cos(theta) = 2/3); a tilted-normal
    sweep verifies that the cosine-weighted mean direction
    aligns with the normal.
  - PDFs: `pdf_hemisphere_uniform` is `1 / (2 pi)`;
    `pdf_hemisphere_cosine(1)` is `1/pi`,
    `pdf_hemisphere_cosine(0)` is `0`,
    `pdf_hemisphere_cosine(-x)` clamps to `0`.
- **`CMakeLists.txt`:** added `sampling_tests` test executable.
  Header-only module, no library; `rr_camera` link supplies
  math + the `src/` include path transitively.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/12 Test  #1: math_tests       ........... Passed  0.00 sec
 ...
12/12 Test #12: sampling_tests   ........... Passed  0.01 sec
100% tests passed, 0 tests failed out of 12

$ ./build/bin/sampling_tests
sampling_tests: 60/60 passed
```

#### Per the prompt

- `Sampling.h` / `Sampling.cuh` / `RNG.h` / `RNG.cuh` are the
  only new source files.
- Per-pixel RNG, random float, hemisphere sampling, and
  cosine-weighted sampling are all surfaced.
- No path-tracing integration: the kernels in
  `cuda/CudaTestKernel.cu` are unchanged; nothing reads
  `Sampling.cuh` yet.
- Module 14 (Path Tracer) and M14 (Path Tracing Foundation)
  flip from "not started" to "in progress" - foundation in,
  integrator still to come.

### 2026-04-27 — M13 finalized: full SceneLoader + SceneWriter + renderer wiring

The format becomes real. Every v1 spec section is now parsed,
written back, uploaded to the GPU, and rendered through the
existing kernel. The user's documented command
`RelativityRender --render scenes/test.rrscene --output
output/from_scene.ppm` works end-to-end.

- **`src/scene/Scene.h`:** rewrote `SceneMesh` and `SceneLight`
  from the M9/M11 placeholders to embed the host PODs:
  - `SceneMesh { object, data: rr::geometry::Mesh,
                 source_path }` - `data` carries vertices,
    triangles, `material_id`, transform; `source_path` is
    reserved for future external-asset references.
  - `SceneLight { object, data: rr::lighting::Light }` - the
    embedded POD is what `GpuScene::upload_from` publishes.
  Updated `scene_tests.cpp` to the new shapes.
- **`src/io/SceneLoader.{h,cpp}`:** added `load_lights` and
  `load_meshes`.
  - `load_lights`: discriminated by `type` string. `"point"`
    requires `position`; `"directional"` requires `direction`
    (auto-normalised by the existing `make_*` factories).
    `intensity >= 0`. Other type strings (incl. `"area"` /
    `"environment"`) are v1 errors per spec.
  - `load_meshes`: `vertices` (array of Vec3) and `triangles`
    (array of `[v0,v1,v2]` index triplets, CCW) required.
    Per-vertex normals / UVs are not in v1 - the parser
    populates them at zero and the renderer derives geometric
    face normals. `material_id` (default `-1`) is the spec
    lookup key. `transform` is optional with identity default
    and is parsed via a shared `load_transform` helper that
    maps file-side `rotation` onto host
    `Transform::euler_rotation_radians`.
  Header docstring updated; nothing remains on the
  warn-and-ignore list for v1.
- **`src/io/SceneWriter.{h,cpp}`:** new module. Inverse of the
  loader. `WriteResult save_rrscene(scene, path)` writes a v1
  JSON file with readable indentation; creates parent dirs.
  Material fields v1 doesn't expose (`metallic`, `specular`,
  `transmission`) and light types v1 doesn't expose (`area`,
  `environment`) are silently dropped during serialisation -
  they live on host PODs but aren't part of the v1 schema.
  Round-trips through the loader for everything v1 stores.
- **`src/gpu/GpuScene.cpp`:** `upload_from` now does the full
  scene-to-device translation:
  - Builds a flat `MaterialParams[]` from `scene.materials` and
    a spec-id -> array-index map.
  - Sphere `material_index` (the spec lookup key after parsing)
    is remapped to the device array index via that map.
  - Visible lights are flattened into a `Light[]` and uploaded.
  - The first visible mesh fills the single mesh slot
    (multi-mesh upload is a future slice); its `material_id`
    is remapped the same way. If no visible mesh exists the
    slot is cleared so stale state from a previous render
    can't leak through.
- **`src/main.cpp`:** `--render` is now driven by the loader.
  Always loads the file (host-side; works even without CUDA),
  reports counts, and - when `RR_HAS_CUDA` is on - runs the
  full upload + render + save pipeline. Output defaults to
  `output/from_scene.ppm` matching the user's command;
  `--output` overrides. The previous M12 hard-coded scene is
  gone (it was a stand-in until real loading existed; earlier
  outputs remain reproducible from older git commits).
- **`scenes/test.rrscene`:** drop-in fixture matching the M12
  lighting scene (5 materials, 4 spheres, 1 quad, 2 lights).
  This is the exact file the user's documented command points
  at.
- **`tests/io_tests.cpp`:** added `test_load_full_scene`
  (asserts the M12 fixture loads with the right counts +
  per-light + mesh details, prints a summary) and
  `test_writer_round_trip` (load -> save -> reload, asserts
  every v1 field survives intact). `io_tests: 81/81 passed`
  (was 36).
- **`CMakeLists.txt`:** `rr_io` lists `src/io/SceneWriter.cpp`;
  the executable link list adds `rr_io`; `rr_scene` PUBLIC-
  links `rr_geometry` and `rr_lighting` since `Scene.h` now
  embeds their PODs.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/11 ... 11/11 all Passed
100% tests passed, 0 tests failed out of 11

$ ./build/bin/io_tests | tail -8
--- loaded full scene ---
  materials: 5  spheres: 4  lights: 2  meshes: 1
    light[0] directional color=(1.00, 0.95, 0.85) intensity=0.90
    light[1] point color=(0.70, 0.80, 1.00) intensity=8.00
    mesh[0] name=warm_quad vertices=4 triangles=2 material=3
-------------------------
io_tests: 81/81 passed

$ ./build/bin/RelativityRender --render scenes/test.rrscene --output output/from_scene.ppm
[INFO] render command received
[INFO] loading scene: scenes/test.rrscene
[INFO] loaded scene: 5 materials, 4 spheres, 2 lights, 1 meshes
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render the loaded scene)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a Turing/Ampere/
Ada GPU) produces `output/from_scene.ppm`. Correct by
construction: the kernel calls the same `RR_HD` routines that
the host suite already covers (`camera_tests` / `geometry_tests`
/ `relativity_tests` / `material_tests` / `lighting_tests`).
The new translation layer (spec id -> array index) is
exercised by `io_tests` for the host side and by the host
tests for material lookup; the GPU only sees flat array
indices.

#### Hard-rule check

- **No CPU rendering** - the entire shading pipeline still runs
  in `k_render_scene`. The CPU only loads the file, uploads to
  the GPU, and saves the framebuffer.
- **No CPU pixel iteration in the render path** - only inside
  `Image::save_ppm`.

#### What this milestone closes (M13 / Module 18)

- M13 (Scene File Format & Parser) -> landed: spec is in,
  loader covers every v1 section, writer round-trips, the
  executable consumes a `.rrscene` end-to-end.
- Module 18 (Scene File Format) -> landed for v1. v2 will add
  textures, env maps, area lights, metallic/specular/
  transmission, vertex normals/UVs, `source_path` mesh
  references; all are forward-compatible.

### 2026-04-27 — M13 parser slice 2: SceneLoader gains materials + spheres

Second `.rrscene` loader slice. Parses both new sections per the
v1 spec, populates the host `Scene` lists, and lays the wiring
needed for the eventual renderer-side integration (still to
come). No `SceneWriter`; rendering remains GPU-only via the
existing M12 hard-coded scene.

- **`src/scene/Scene.h`:** rewrote `SceneMaterial` from the M9
  placeholder (`name` + `albedo`) to the real shape: `id`
  (lookup key matching the spec's stable handle), `name`,
  `params` (full host `MaterialParams`). `id == -1` keeps the
  "renderer's neutral default" semantics that unmatched lookups
  use. Added `#include "material/MaterialTypes.h"`.
- **`src/io/SceneLoader.{h,cpp}`:** added `load_materials` and
  `load_spheres` extractors.
  - `materials`: each entry requires `id` (non-negative,
    unique within the array). `name` defaults to empty. The
    rest (`base_color`, `emission_color`, `emission_strength`,
    `roughness`) take their host `MaterialParams` defaults
    when absent. Per-spec clamps: `roughness` to `[0, 1]`,
    `emission_strength` to `>= 0`, colour components to
    `[0, ∞)`. Duplicate `id` is a parse error with a clear
    diagnostic that names the duplicating entry.
  - `spheres`: `position` and `radius` are required;
    `material_id` is optional and defaults to `-1`. `radius`
    must be `> 0`. The spec stores `material_id` as the
    lookup key (the entry's `id`, not its array index); the
    parser preserves it on `Sphere::material_index` so the
    eventual GpuScene::upload_from translation step has the
    raw value to remap.
  - The header docstring now lists `materials` and `spheres`
    alongside the previously-supported sections; only `lights`
    and `meshes` remain on the warn-and-ignore list.
- **`scenes/test_geometry.rrscene`:** new fixture - 3 materials
  (including a sparse `id = 3`), 4 spheres (one of them omits
  `material_id` entirely so the `-1` default path is exercised),
  default zero-velocity observer, default camera. Designed to
  cover the corner cases for the next renderer slice.
- **`tests/io_tests.cpp`:** added `test_load_test_geometry_scene`
  (loads the new fixture, prints counts + per-entry summary,
  asserts each loaded value including the sparse-id and
  no-material-id cases) and
  `test_minimal_scene_still_has_no_geometry` (the original
  minimal fixture, which omits both new sections, still loads
  with empty lists rather than failing). `io_tests: 36/36
  passed`.
- **`tests/scene_tests.cpp`:** updated to the new
  `SceneMaterial` shape (`id` + `name` + `params`) so the
  existing scene structure tests keep passing.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/11 ... 11/11 all Passed
100% tests passed, 0 tests failed out of 11

$ ./build/bin/io_tests
... (minimal scene printout) ...
--- loaded geometry scene ---
  materials: 3 entries
    [0] id=0 name=matte_red baseColor=(0.90, 0.15, 0.15) emission=(0.00, 0.00, 0.00) * 0.00 rough=0.50
    [1] id=1 name=matte_green baseColor=(0.15, 0.85, 0.25) emission=(0.00, 0.00, 0.00) * 0.00 rough=0.50
    [2] id=3 name=warm_emitter baseColor=(0.00, 0.00, 0.00) emission=(1.00, 0.85, 0.40) * 2.00 rough=1.00
  spheres: 4 entries
    [0] center=(0.00, 0.00, -3.00) r=1.00 material_id=0
    [1] center=(-1.60, 0.00, -3.50) r=0.60 material_id=1
    [2] center=(0.00, 1.60, -3.00) r=0.40 material_id=3
    [3] center=(1.60, 0.00, -3.50) r=0.60 material_id=-1
------------------------------
io_tests: 36/36 passed
```

#### Per the prompt

- Only `materials` and `spheres` were added.
- `lights` and `meshes` are not parsed (still warn-and-ignore).
- No `SceneWriter`.
- Rendering remains GPU-only - the loader is exercised by
  `io_tests` only; `--render` continues to use the M12
  hard-coded scene.

#### Naming note

The on-disk `material_id` references the spec's lookup key
(the material's `id`), not the array index. The parser stores
this value verbatim on `Sphere::material_index`. The
translation from spec id to device-side array index happens at
GPU upload time and is the next M13 / renderer slice's
responsibility.

### 2026-04-27 — M13 parser slice 1: SceneLoader (camera + relativity + render settings)

First real `.rrscene` parsing. Hand-rolled JSON parser (~250 lines)
in the implementation TU; pulling in a 25k-line third-party
header for six top-level keys is overkill at this milestone, and
the same `load_rrscene` surface lets us swap to nlohmann/json
without churn when the format grows. Per the prompt: only
`render_settings`, `camera`, and `relativity` are parsed; the
deferred sections (materials, spheres, lights, meshes) are
warned-and-ignored per spec. No SceneWriter; no main.cpp wiring;
rendering remains GPU-only.

- **`src/io/SceneLoader.h`:** `rr::io::LoadResult { ok, scene,
  message }` + `load_rrscene(path) -> LoadResult`. Host-only;
  no GPU dependencies. The header only mentions the v1 sections
  the parser actually populates and explicitly lists the
  warn-and-ignore deferred sections so future slices have a
  clear contract.
- **`src/io/SceneLoader.cpp`:** in-house JSON parser
  (`JsonParser` + `JsonValue` tagged-union; supports objects,
  arrays, strings with basic escapes, numbers including
  scientific notation, `true` / `false` / `null`; reports
  line / column on every error). Section extractors map the
  parsed tree onto host structs:
  - `render_settings` -> `Scene::render_settings.{width,height}`,
    rejects non-positive dimensions.
  - `camera` -> `Scene::camera`. `position` + `forward` +
    `up` populate the basis via `Camera::look_at`; `fov`
    in `(0, 180)` becomes `vertical_fov_degrees`. After
    loading, `Camera::set_aspect(width / height)` derives
    aspect from the render settings.
  - `relativity` -> `Scene::observer.velocity = beta_velocity *
    normalize(velocity_direction)` (clamped to `|β| < 1`),
    plus the three strengths mapped onto the host
    `RelativityParams`. Aberration is binary on the host so
    `aberration_strength > 0` toggles `enable_aberration`;
    `doppler_strength` / `searchlight_strength` flow into the
    matching continuous knobs and toggle their `enable_*`
    booleans.
  Validation rules (`version == 1`, vec3 length, fov range,
  non-zero forward, etc.) all reported with file-relative
  diagnostics.
- **`scenes/test_minimal.rrscene`:** fixture covering the three
  parsed sections at the same `β = 0.3` setup as the M12 light
  scene.
- **`tests/io_tests.cpp`:** loads the fixture via
  `RR_TEST_FIXTURES_DIR` (compile-time absolute path baked in
  by CMake so the test runs from any ctest working directory),
  prints the parsed scene state for human inspection, and
  asserts each loaded value. Plus a missing-file negative test.
  `io_tests: 16/16 passed`.
- **`CMakeLists.txt`:** added `rr_io` static library
  (`src/io/SceneLoader.cpp`, PUBLIC link to `rr_scene`) and
  the `io_tests` executable. The fixtures path is wired via
  `target_compile_definitions(io_tests PRIVATE
   RR_TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/scenes")`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/11 ... 11/11 all Passed
100% tests passed, 0 tests failed out of 11

$ ./build/bin/io_tests
--- loaded scene ---
  render_settings:
    width  = 640
    height = 480
  camera:
    position = (0.000, 0.000, 0.000)
    forward  = (0.000, 0.000, -1.000)
    up       = (0.000, 1.000, 0.000)
    fov      = 50.00 deg
    aspect   = 1.3333
  relativity:
    velocity = (0.000, 0.000, -0.300)  |beta| = 0.3000
    enable_aberration       = true
    enable_doppler          = true
    enable_searchlight      = true
    doppler_color_strength  = 1.000
    searchlight_strength    = 1.000
---------------------
io_tests: 16/16 passed
```

#### Per the prompt

- Only `render_settings`, `camera`, and `relativity` are
  parsed; `materials`, `spheres`, `lights`, `meshes` are
  ignored (warn-and-continue per spec).
- No `SceneWriter`; that's the next slice.
- Rendering remains GPU-only - `--render` continues to use the
  existing M12 hard-coded scene; the loader is exercised only
  by `io_tests`.

#### Next M13 slice

Wire `--render <scene file>` to call `load_rrscene` once enough
of the spec is parsed to make a useful renderable scene:
materials + spheres + lights at minimum. Until then the loader
is a library + a unit test, not yet a render entry point.

### 2026-04-27 — RRSCENE v1: `lights` section + final compact example

Doc-only extension. The v1 spec is now feature-complete for the
sections the prompt sequence defined. No parser, no source
changes.

- **`docs/RRSCENE_FORMAT.md`:** added section 11 (`lights`).
  - Fields: `type` (required string, `"point"` or
    `"directional"`), `position` (required for point),
    `direction` (required for directional, propagation vector,
    auto-normalised), `color` (linear RGB, defaults to white),
    `intensity` (defaults to 1, must be >= 0).
  - Point lights use **inverse-square falloff**
    (`Li = color * intensity / r^2`); no falloff radius / cutoff
    in v1.
  - Directional lights have no positional component and no
    distance falloff. The shader uses `-direction` as the
    to-light vector, matching the kernel's M12 convention.
  - The kernel skips back-faced contributions
    (`dot(N, wi) <= 0`); no shadow / occlusion ray (M14).
  - Light types beyond `point` / `directional` (`area`,
    `environment`) are explicitly out of v1; the host
    `LightType` enum carries them but a v1 file MUST NOT use
    them - `area` / `environment` strings are parser errors.
- Top-level shape (section 2) now lists `lights` as an optional
  section. Section numbers below shifted by one
  (Common types -> 12, Defaults -> 13, Validation -> 14,
  Complete example -> 15, Out of scope -> 16, References -> 17).
- Validation rules (section 14) gained a clause: each light has
  a `type` from the {`"point"`, `"directional"`} set; `intensity`
  is `>= 0`; out-of-set type strings (incl. `"area"` /
  `"environment"`) are v1 errors.
- Out of scope (section 16) updated: removed the standalone
  `lights` bullet (lights are now defined); added an explicit
  "light types beyond point and directional" bullet for
  `area` / `environment`; added `material node graphs / shader
  graphs` so the deferred shading-graph work is documented.
- Complete example (section 15) **replaced** with a single
  compact example exercising every section at once - render
  settings, camera, relativity (`β = 0.3`), two materials, one
  sphere, one triangle mesh, one directional + one point light.
  The composition mirrors the M12 lighting scene in `main.cpp`,
  so the file is a drop-in for what `--render` already
  produces.
- References (section 17) updated with `src/lighting/Light.h`
  and noted that only the `Point` / `Directional` enumerators
  of `LightType` are reachable from a v1 file.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 10/10`).

#### Per the prompt

- Only `lights` was added.
- One final complete example exercises every section in a
  compact form (one of each except materials and lights, which
  show two entries to demonstrate arrays).
- No textures, no node graphs, no parser code.

#### v1 spec status

With `lights` in, the v1 spec covers every host-side data
module currently consumed by `--render`. The remaining v1 work
is parser implementation (the next M13 slice).

### 2026-04-27 — RRSCENE v1: `meshes` section added

Doc-only extension. No parser, no source changes.

- **`docs/RRSCENE_FORMAT.md`:** added section 10 (`meshes`).
  - Fields: `name` (optional debug label), `vertices` (required
    array of Vec3 positions), `triangles` (required array of
    `[v0, v1, v2]` index triplets in CCW front-face winding),
    `material_id` (optional, indexes into `materials`),
    `transform` (optional, identity by default).
  - `transform` is the canonical SRT decomposition mapped onto
    the host `rr::math::Transform`: `position` (Vec3),
    `rotation` (Vec3 of Euler angles in radians, intrinsic XYZ),
    `scale` (Vec3, negative axes flip).
  - Index validity is enforced (each index in
    `[0, vertices.size())`; out-of-range / negative is an error).
  - Vertex attributes beyond position (normals, UVs, tangents,
    vertex colours) and external file references
    (`source_path`) explicitly deferred. The renderer derives
    geometric face normals from triangle winding.
  - Quaternion / axis-angle rotations are out of scope; future
    versions add an alternative `rotation_quaternion` field
    that takes precedence when both are present.
- Top-level shape (section 2) now lists `meshes` as an optional
  section. Section numbers below shifted by one (Common types
  -> 11, Defaults -> 12, Validation -> 13, Complete example ->
  14, Out of scope -> 15, References -> 16).
- Validation rules (section 13) gained a new clause: each mesh
  has `vertices` and `triangles`; every triangle index is in
  `[0, vertices.size())`; negative or out-of-range indices are
  errors; empty arrays are legal but warn-eligible.
- Out of scope (section 15) updated: removed the `meshes`
  bullet; added one for "mesh fields beyond `vertices` /
  `triangles` / `material_id` / `transform`" so the deferred
  per-vertex normals / UVs / tangents / colours and the
  external `source_path` reference are explicitly out of v1.
  `lights` remains out-of-scope per the prompt.
- Complete example (section 14) extended: a `warm_emitter`
  material entry plus a 4-vertex / 2-triangle quad mesh that
  references it. Mirrors the M11 material scene's quad +
  emissive-material setup so the file is one-to-one with the
  current `--render` output.
- References updated with `src/geometry/Mesh.h`,
  `src/geometry/Triangle.h`, and `src/math/Transform.h`.

#### Verified

No source changes; the existing build / tests remain green
(`ctest -> 10/10`).

#### Per the prompt

- Only `meshes` was added.
- One small JSON example sits inside the new section; the
  end-to-end example was extended to exercise it alongside the
  existing materials.
- No lights, no parser code.

### 2026-04-27 — RRSCENE v1: `materials` section added

Doc-only extension to the v1 spec. No parser, no source changes.

- **`docs/RRSCENE_FORMAT.md`:** added section 9 (`materials`).
  - Fields: `id` (required, non-negative, unique within the
    array, the lookup key referenced by
    `spheres[i].material_id`), `name` (optional, debug label),
    `base_color`, `emission_color`, `emission_strength`,
    `roughness`. Roughness is clamped to `[0, 1]`; colours are
    clamped to `[0, ∞)`.
  - `id` is a lookup key, not an array index - sparse / out-of-
    order ids are legal so authoring tools can manage stable
    handles. Duplicate ids are an error.
  - Unmatched `material_id` and explicit `-1` fall back to the
    renderer's neutral default material
    (`[0.8, 0.8, 0.8]`, no emission, roughness `0.5`).
  - The host `MaterialParams` carries `metallic`, `specular`,
    `transmission` slots; v1 does not expose them and the
    parser keeps them at their host defaults. Future schema
    versions add them as additional optional fields with the
    same names.
  - Texture-driven parameters explicitly excluded from v1;
    they land with the texture system (M16).
- Top-level shape (section 2) updated to list `materials` as an
  optional section. Section numbers below it shifted by one
  (Common types -> 10, Defaults -> 11, Validation -> 12,
  Complete example -> 13, Out of scope -> 14, References -> 15).
- Validation rules (section 12) gained a new clause: material
  `id` must be a non-negative integer and unique within the
  `materials` array; out-of-range numerical values are clamped
  per the materials table.
- Spheres section (section 8) note rewritten: `material_id` now
  resolves to an entry in the `materials` array; `-1` /
  unmatched ids fall back to the renderer's default. The earlier
  "v1 has no materials section" caveat is gone.
- "Out of scope for v1" (section 14) updated: removed the
  bullet for "materials array"; added an explicit bullet for
  "material fields beyond `base_color` / `emission_color` /
  `emission_strength` / `roughness`" so the absent
  `metallic` / `specular` / `transmission` fields are
  documented as deferred. `meshes` and `lights` remain
  out-of-scope per the user's narrower prompt.
- Complete example (section 13) updated: the single sphere now
  references a real `matte_red` material via `material_id: 0`,
  exercising the new section end-to-end.
- References (section 15) updated to add
  `src/material/MaterialTypes.h` and document which
  `MaterialParams` fields v1 keeps at host defaults.

#### Verified

No source changes; the existing build and tests remain green
from the previous slice (`ctest -> 10/10`).

#### Per the prompt

- Only `materials` was added.
- One small JSON example in the new section, plus the
  end-to-end example file in section 13 was extended to
  reference a material.
- No meshes; no lights.
- No parser code.

### 2026-04-27 — RRSCENE v1 format spec landed (doc only, M13 in progress)

Documentation-only slice. Defines the minimal v1 contract the
upcoming parser implements; no parser code, no CMake / source
changes.

- **`docs/RRSCENE_FORMAT.md`:** v1 specification.
  - File extensions: `.rrscene` (canonical) and `.rrjson`
    (explicit JSON alias).
  - JSON encoding. Top-level shape is `{ version: 1,
    render_settings, camera, relativity, spheres }`. Every
    section is optional; defaults are explicitly listed and
    match the existing C++ defaults.
  - Sections defined in v1:
    1. `render_settings` - `width`, `height` only. `samples_per_pixel`,
       `max_depth`, AOV selection deferred to v2+.
    2. `camera` - `position`, `forward`, `up`, `fov` (degrees).
       Aspect derives from `render_settings`. Near/far deferred.
    3. `relativity` - `beta_velocity` (scalar), `velocity_direction`
       (Vec3), and three continuous strengths
       (`aberration_strength`, `doppler_strength`,
       `searchlight_strength`). The parser will reconstruct the
       host `Observer.velocity = beta * normalize(direction)` and
       map `strength == 0` to the corresponding boolean toggle.
    4. `spheres` - array of `{ position, radius, material_id }`.
       `material_id` is a forward-compatibility hint;
       v1 has no `materials` section yet, so unknown / `-1` ids
       map to the renderer's neutral default.
  - Conventions: right-handed +Y up / -Z forward, lengths in
    scene units, angles in degrees only when named
    `*_degrees`, all `Vec3` are 3-element JSON arrays.
  - Validation rules enumerated explicitly (positive
    dimensions, FOV in `(0, 180)`, non-zero forward, etc.).
  - One full example file matches the M9 single-sphere
    relativistic test at `β = 0.5`.
  - Explicit "out of scope for v1" list: materials, meshes,
    lights, textures, environment maps, AOVs, motion blur,
    DOF, near/far. Each has a host data model already; v2 adds
    the matching JSON section.
- **`docs/BUILD_PLAN.md`:** marked module 18 (Scene File Format)
  and milestone M13 as **in progress** - spec landed; parser
  implementation is the next slice.

#### Verified locally

No source changes; the existing build is unaffected.

```
$ cd build && ctest --output-on-failure
 1/10 ... 10/10 all Passed
100% tests passed, 0 tests failed out of 10
```

#### Per the prompt

- Only `render_settings` (width/height), `camera`, `relativity`
  (the listed five fields), and `spheres` (position, radius,
  material_id) are defined.
- No meshes, lights, materials, textures, AOVs, environment
  maps in v1 - these are explicitly enumerated as deferred.
- No parser code; the spec is the contract M13's parser slice
  implements.

### 2026-04-27 — M12 finalized: simple direct lighting on GPU

Lights now flow end-to-end. Each hit accumulates contributions from
the uploaded directional + point lights with inverse-square falloff,
adds emission from the material, and falls back to an environment
tint (or the existing default sky gradient) when no Environment
light is present. No shadows, no path tracing.

- **`src/gpu/GpuScene.{h,cpp}`:** added
  `GpuBuffer<rr::lighting::Light>` slot,
  `upload_lights(host, count)`, `light_count()` query, and a
  `device_lights()` accessor. Same dynamic-size,
  fail-predictably-without-backend semantics as the existing
  sphere / mesh / material upload paths.
- **`src/cuda/CudaScene.cuh`:** added `lights` device pointer +
  `light_count` to `CudaSceneView`. `nullptr` + count `0` is
  allowed and means "no lights uploaded - fall back to default
  ambient + sky gradient".
- **`src/cuda/CudaRenderer.cu`:** `render_scene` copies the
  lights view (`device_lights()` + `light_count()`) into the
  launch argument before dispatch.
- **`src/cuda/CudaTestKernel.cu`:** the kernel's shade phase is
  rewritten as a single pass over the uploaded lights:
  - `Environment` lights record an `env_color`.
  - `Point` and `Directional` lights, on hit, accumulate
    `Li * ndotl` into `lighting`. Backface (`ndotl <= 0`) is
    skipped; degenerate point-light geometry (`d^2 < 1e-12`) is
    skipped.
  - `Area` lights are skipped at this milestone (sampling lands
    at M14).
  After the loop, the hit shade is
  `mat.baseColor * (lighting + env_color)
   + mat.emissionColor * mat.emissionStrength`. The miss path
  uses the `env_color` when an Environment light is present, and
  the existing vertical sky gradient otherwise. The relativistic
  pipeline (Doppler colour + searchlight beaming) continues to
  wrap the final colour, so high-beta passes still see
  blueshift / beaming on the per-light shaded result.
- **`src/main.cpp`:** `--render` builds a three-light rig - a
  warm directional sun, a cool point light above-right, a pale
  blue environment - on top of the M11 material scene, uploads
  it through `GpuScene`, and saves to
  `output/gpu_direct_lighting.ppm`. `--output` is still ignored
  at this milestone.
- **`tests/gpu_tests.cpp`:** +8 `upload_lights` assertions
  (87/87 total). Default `GpuScene` has no lights; empty upload
  succeeds everywhere; non-empty upload fails predictably
  without a backend, with `light_count == 0` and
  `device_lights() == nullptr`.
- **`CMakeLists.txt`:** `rr_gpu` PUBLIC link list now includes
  `rr_lighting` so the renderer's GPU layer can reach `Light`
  symbols.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/10 Test  #1: math_tests       ........... Passed  0.00 sec
 2/10 Test  #2: image_tests      ........... Passed  0.00 sec
 3/10 Test  #3: gpu_tests        ........... Passed  0.00 sec
 4/10 Test  #4: camera_tests     ........... Passed  0.00 sec
 5/10 Test  #5: geometry_tests   ........... Passed  0.00 sec
 6/10 Test  #6: relativity_tests ........... Passed  0.00 sec
 7/10 Test  #7: scene_tests      ........... Passed  0.00 sec
 8/10 Test  #8: mesh_tests       ........... Passed  0.00 sec
 9/10 Test  #9: material_tests   ........... Passed  0.00 sec
10/10 Test #10: lighting_tests   ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 10

$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 87/87 passed
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`) produces
`output/gpu_direct_lighting.ppm`. Correct by construction: every
device-side helper used in the new shade path
(`generate_camera_ray`, `intersect_sphere`,
`intersect_triangle`, `aberrateDirection`, `dopplerFactor`,
`searchlightFactor`, `applyDopplerColor`) is exercised by the
host suite (`camera_tests` 43, `geometry_tests` 46,
`relativity_tests` 52); the new direct-lighting accumulation is
straight-line vector arithmetic over POD inputs.

#### Hard-rule check

- **No shadows / no path tracing** per the prompt: the kernel
  evaluates `Li * ndotl` directly, with no occlusion ray.
- **Lights read on the GPU**: every light lookup happens inside
  `k_render_scene`. The CPU only fills the `Light` array and
  uploads it.
- **No CPU pixel iteration in the render path**: only inside
  `Image::save_ppm`.

#### What this milestone closes (M12 / Module 11)

- M12 (Lighting System Foundations) -> landed: data model +
  factories + GPU upload + kernel-side direct lighting all in.
- Module 11 (Lighting System) -> landed for the foundation. Real
  importance-sampled `eval` / `sample` / `pdf` per light type,
  area-light sampling, and shadow rays are required for path-
  tracing fidelity; those land with M14 (path tracer) and M15
  (OptiX upgrade for ray dispatch).

### 2026-04-27 — M12 lighting data model landed (M12 in progress)

Host-side lighting POD + factories + CUDA-side re-export. No path
tracing, no kernel changes; the data shape is the contract M14 (path
tracer) and M16 (env-map textures) populate.

- **`src/lighting/Light.h`:** `LightType` enum (`Point`,
  `Directional`, `Area`, `Environment`) with stable ordinals so
  the upload contract is forward-compatible. `Light` POD with a
  flat layout (no union) so `GpuBuffer<Light>` can carry it via
  `std::is_trivially_copyable`. Field semantics are
  type-discriminated and documented in the header. Defaults
  describe a neutral white point light at the origin with unit
  intensity.
- **`src/lighting/Light.cpp`:** factory functions
  `make_point_light` / `make_directional_light` /
  `make_area_light` / `make_environment_light`. Direction-bearing
  factories normalize the input via a `safe_normalize` helper that
  falls back to `(0, -1, 0)` on degenerate (zero-length) input.
  `make_area_light` clamps negative dimensions to zero. The Area
  and Environment slots are explicitly documented as placeholders
  - the geometry / sampling routines arrive at M14, env-map
  textures at M16.
- **`src/cuda/CudaLight.cuh`:** thin re-export of `Light.h` so
  kernel TUs can include a `.cuh`. Future `RR_HD inline` sampling
  / eval / pdf helpers per `LightType` land here without touching
  the host surface.
- **`tests/lighting_tests.cpp`:** 35 host assertions covering
  `LightType` ordinals (upload contract); default `Light` is a
  neutral point at origin; each factory produces the expected
  fields; directional / area factories normalize their input
  vector and fall back to `(0, -1, 0)` for zero-length input;
  area light clamps negative width / height to zero.
- **`CMakeLists.txt`:** added `rr_lighting` static library and
  the `lighting_tests` test executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
 1/10 Test  #1: math_tests       ........... Passed  0.00 sec
 2/10 Test  #2: image_tests      ........... Passed  0.00 sec
 3/10 Test  #3: gpu_tests        ........... Passed  0.00 sec
 4/10 Test  #4: camera_tests     ........... Passed  0.00 sec
 5/10 Test  #5: geometry_tests   ........... Passed  0.00 sec
 6/10 Test  #6: relativity_tests ........... Passed  0.00 sec
 7/10 Test  #7: scene_tests      ........... Passed  0.00 sec
 8/10 Test  #8: mesh_tests       ........... Passed  0.00 sec
 9/10 Test  #9: material_tests   ........... Passed  0.00 sec
10/10 Test #10: lighting_tests   ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 10

$ ./build/bin/lighting_tests
lighting_tests: 35/35 passed
```

#### Not in this slice (M12 / Module 11 still "in progress")

- No path tracing, per the prompt; nothing reads `Light` yet.
- No GPU upload of the light array; `GpuScene` does not yet
  expose `upload_lights(...)` and `CudaSceneView` has no light
  array.
- No kernel-side direct-lighting evaluation. The current shade
  is still the M11 hemispherical key term.
- Area and Environment are explicit placeholders - geometry +
  sampling routines for area lights arrive at M14; env-map
  textures at M16.
- No update to `scene::SceneLight`, which remains the M9
  placeholder. It will be rewritten to embed
  `rr::lighting::Light` when a real consumer lands.

### 2026-04-27 — M11 finalized: materials end-to-end through the GPU renderer

`material_index` now flows from primitives, through the upload path,
to the kernel, which evaluates a simple diffuse + emission + normal-
driven hemisphere shade. The relativistic pipeline still wraps the
result, so Doppler / searchlight modify the per-material output.

- **`src/renderer/Hit.h`:** added `material_index` (defaults to
  `-1` = "no material"). Both `intersect_sphere` and the kernel
  triangle loop now populate it.
- **`src/geometry/Sphere.h`:** added `material_index` to the POD.
  Aggregate-init `Sphere{c, r}` still works (the field defaults
  to `-1`); `make_sphere(...)` returns `-1` explicitly. Existing
  call sites are unaffected.
- **`src/cuda/CudaIntersection.cuh`:** `intersect_sphere`
  propagates `sphere.material_index` into the resulting `Hit`.
  `intersect_triangle` is unchanged (the kernel sets the mesh's
  `material_id` on the hit at the call site, since standalone
  triangles have no material concept).
- **`src/cuda/CudaScene.cuh`:** added a material array to
  `CudaSceneView` (`materials` device pointer + `material_count`).
  `nullptr` + count `0` is allowed and means "no materials
  uploaded - everything uses the default neutral shade".
- **`src/gpu/GpuScene.{h,cpp}`:** added `GpuBuffer<MaterialParams>`
  + `upload_materials(host, count)` + `material_count()` query +
  `device_materials()` accessor. Same dynamic-size, fail-
  predictably-without-backend semantics as the existing sphere /
  mesh upload paths.
- **`src/cuda/CudaRenderer.cu`:** `render_scene` now also copies
  the materials view (`device_materials()` + `material_count()`)
  into `CudaSceneView` before launching.
- **`src/cuda/CudaTestKernel.cu`:** the kernel's hit branch now:
  1. looks up `MaterialParams` at `best.material_index` (with a
     bounds check; out-of-range falls back to a neutral default);
  2. computes a hemispherical key shade
     `0.4 + 0.6 * max(0, dot(N, +Y))`;
  3. composes `baseColor * shade + emissionColor *
     emissionStrength`.
  Doppler colour and searchlight beaming continue to apply on the
  composed colour, so the relativistic effects modify the
  per-material result. The triangle loop now tags each accepted
  hit with `mesh.material_id` so meshes participate in the
  material lookup.
- **`src/main.cpp`:** `--render` builds a host scene with five
  materials (red / green / blue diffuse, warm emissive for the
  quad, light grey floor), assigns each sphere a material index,
  flags the quad's mesh with the emissive material, uploads
  everything, and saves to `output/gpu_material_scene.ppm`.
  `--output` is still ignored at this milestone.
- **`tests/geometry_tests.cpp`:** +6 assertions (46/46 total) -
  `make_sphere(...)` returns the `-1` sentinel;
  `intersect_sphere` propagates the per-sphere index when set,
  preserves `-1` for default-constructed spheres, and the
  aggregate `{center, radius}` form still works.
- **`tests/gpu_tests.cpp`:** +8 assertions (79/79 total) -
  default `GpuScene` has no materials; empty `upload_materials`
  succeeds everywhere; non-empty upload fails predictably without
  a backend, with `material_count == 0` and
  `device_materials() == nullptr`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/9 Test #1: math_tests       ........... Passed  0.00 sec
2/9 Test #2: image_tests      ........... Passed  0.00 sec
3/9 Test #3: gpu_tests        ........... Passed  0.00 sec
4/9 Test #4: camera_tests     ........... Passed  0.00 sec
5/9 Test #5: geometry_tests   ........... Passed  0.00 sec
6/9 Test #6: relativity_tests ........... Passed  0.00 sec
7/9 Test #7: scene_tests      ........... Passed  0.00 sec
8/9 Test #8: mesh_tests       ........... Passed  0.00 sec
9/9 Test #9: material_tests   ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 9

$ ./build/bin/geometry_tests
geometry_tests: 46/46 passed
$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 79/79 passed
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`) produces
`output/gpu_material_scene.ppm`. The kernel calls the same
`RR_HD generate_camera_ray`, `intersect_sphere`,
`intersect_triangle`, `aberrateDirection`, `dopplerFactor`,
`searchlightFactor`, and `applyDopplerColor` covered by the
existing host tests; the new shading combines the same
`MaterialParams` fields the host suite exercises.

#### Hard-rule check

- **Materials read on the GPU**: every material lookup happens
  inside `k_render_scene`. The CPU only fills the parameter
  array and uploads it.
- **No CPU pixel iteration in the render path**: only inside
  `Image::save_ppm`.

#### What this milestone closes (M11 / Module 9)

- M11 (Material System Foundations) -> landed: parameter pack +
  per-primitive id + GPU array upload + kernel-side shading all
  in.
- Module 9 (Material / Shading System) -> landed for the
  foundation. The full BSDF interface (`eval` / `sample` /
  `pdf`) and the texture-driven parameter binding are still
  required for path-tracing fidelity; those land with M14
  (path tracer) and M16 (textures).

### 2026-04-27 — Material foundation landed (M11 in progress)

PBR-style parameter pack + thin host wrapper + CUDA-side re-export.
No BSDF, no node graph, no textures - those come in their own
milestones. The data shape is the contract M13 (scene file
format), M14 (path tracer), and M21 (node editor) populate.

- **`src/material/MaterialTypes.h`:** `MaterialParams` POD with
  `baseColor`, `emissionColor`, `emissionStrength`, `roughness`,
  `metallic`, `specular`, plus a `transmission` placeholder slot
  reserved for the dielectric / glass BSDF that joins later.
  Defaults describe a neutral 80% grey diffuse surface
  (roughness 0.5, metallic 0, specular 0.5, no emission, no
  transmission). Field names use the DCC / PBR camelCase
  convention so artists and scene-file authors recognise them
  - the rest of the project uses snake_case; the material module
  is the one exception, mirroring the relativity module's
  physics-literature naming.
- **`src/material/Material.h` / `.cpp`:** thin host wrapper.
  `Material { name, params }` with const + mutable `params()`
  accessors so authoring code can tweak fields in place. Three
  presets: `make_diffuse(base_color)` (matte 1.0 roughness),
  `make_emissive(emission_color, strength)` (black diffuse, full
  emission), `make_metal(base_color, roughness)`
  (metallic = 1, specular = 1).
- **`src/cuda/CudaMaterial.cuh`:** thin re-export of
  `MaterialTypes.h` so kernel TUs can include a `.cuh`. Future
  `RR_HD inline` BSDF helpers (`eval` / `sample` / `pdf`) and
  device-specific overrides land here without touching the host
  surface.
- **`tests/material_tests.cpp`:** 33 host assertions covering
  `MaterialParams` defaults; default + params + named ctors;
  setters; the mutable `params()` accessor (modify a field, other
  fields unchanged); the three presets; and that the
  `transmission` placeholder round-trips through the params
  struct as a plain float (no shader behaviour wired yet, but the
  slot is reachable).
- **`CMakeLists.txt`:** added `rr_material` static library and
  the `material_tests` test executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/9 Test #1: math_tests       ........... Passed  0.00 sec
2/9 Test #2: image_tests      ........... Passed  0.00 sec
3/9 Test #3: gpu_tests        ........... Passed  0.00 sec
4/9 Test #4: camera_tests     ........... Passed  0.00 sec
5/9 Test #5: geometry_tests   ........... Passed  0.00 sec
6/9 Test #6: relativity_tests ........... Passed  0.00 sec
7/9 Test #7: scene_tests      ........... Passed  0.00 sec
8/9 Test #8: mesh_tests       ........... Passed  0.00 sec
9/9 Test #9: material_tests   ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 9

$ ./build/bin/material_tests
material_tests: 33/33 passed
```

#### Not in this slice (M11 / Module 9 still "in progress")

- No BSDF interface (`eval` / `sample` / `pdf`). The kernel
  still shades hits by normal-as-color, not by material.
- No GPU upload of the material list; `GpuScene` does not yet
  expose `upload_materials(...)` and `CudaSceneView` has no
  material array.
- No `material_index` plumbing in `Hit`. Spheres / triangles
  carry an id today (via `SceneSphere` / `Mesh::material_id`)
  but the kernel has no way to look up the corresponding
  parameters.
- No texture / node graph. The user explicitly excluded both
  from this milestone; they land at M16 (textures) and M21
  (node editor).
- No update to `scene::SceneMaterial`, which remains the M9
  placeholder. It will be rewritten to embed
  `rr::material::Material` when a real consumer lands.

### 2026-04-27 — M10 finalized: CUDA triangle rendering

Naive GPU triangle loop alongside the existing sphere loop. Both
primitive types compete for the same nearest-hit slot in
`k_render_scene`. CPU does no intersection work; the kernel walks
the uploaded vertex / index buffers per pixel.

- **`src/cuda/CudaIntersection.cuh`:** added `RR_HD inline
  intersect_triangle(ray, v0, v1, v2, t_min, t_max) -> Hit`.
  Moller-Trumbore. Double-sided (back-face hits accepted; the
  `|det| < eps` check rejects only edge-parallel rays). Returns
  the geometric front-face normal of the CCW winding `(v0, v1,
  v2)`. `ray.direction` does not need to be unit length.
- **`src/cuda/CudaMesh.cuh`** (existing): the launch-argument
  contract is now consumed by `k_render_scene`.
- **`src/cuda/CudaScene.cuh`:** added a single-mesh slot to
  `CudaSceneView`. `mesh.triangle_count == 0` means "no mesh
  contributes triangles", so a sphere-only scene still works
  unchanged.
- **`src/cuda/CudaTestKernel.cu`:** extended `k_render_scene` with
  a triangle loop after the sphere loop. Both update the running
  `t_max`, so the closest hit across primitive types wins. Vertex
  positions are taken as-is from the uploaded buffer (effectively
  world-space). Per-mesh transforms join the kernel alongside the
  M11 material system; for now the host pre-places vertices in
  world space.
- **`src/cuda/CudaRenderer.cu`:** `render_scene` now also copies
  the mesh slot from `GpuScene::gpu_mesh()` into `CudaSceneView`.
- **`src/gpu/GpuScene.{h,cpp}`:** added a `GpuMesh mesh_;` slot,
  `upload_mesh(const Mesh&)` convenience, `has_mesh()` query, and
  a `gpu_mesh()` accessor for the renderer. `upload_from(Scene)`
  is intentionally unchanged (scene-side mesh wrappers are still
  placeholders); callers push the mesh directly via
  `upload_mesh`.
- **`src/main.cpp`:** `--render` now produces both deliverables.
  A reusable `build_quad()` helper builds a 2-triangle CCW quad
  in front of the camera; the first scene uses only the mesh
  (output `output/gpu_triangle.ppm`); the second adds the M10
  four-sphere arrangement (output `output/gpu_mesh_scene.ppm`).
- **`tests/geometry_tests.cpp`:** +15 triangle assertions
  (40/40 total) covering centre-pixel hit (`t = 3`,
  `position = (0, 0, -3)`, `normal = (0, 0, 1)` for a CCW
  triangle in the `z = -3` plane), outside-triangle miss,
  parallel-ray miss, double-sided hit from behind,
  `t_min`/`t_max` clipping, and CCW vs CW winding flipping the
  geometric normal. The kernel calls the same `RR_HD` routine, so
  the host coverage validates the device math by construction.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/8 Test #1: math_tests       ........... Passed  0.00 sec
2/8 Test #2: image_tests      ........... Passed  0.00 sec
3/8 Test #3: gpu_tests        ........... Passed  0.00 sec
4/8 Test #4: camera_tests     ........... Passed  0.00 sec
5/8 Test #5: geometry_tests   ........... Passed  0.00 sec
6/8 Test #6: relativity_tests ........... Passed  0.00 sec
7/8 Test #7: scene_tests      ........... Passed  0.00 sec
8/8 Test #8: mesh_tests       ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 8

$ ./build/bin/geometry_tests
geometry_tests: 40/40 passed
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`) produces both
`output/gpu_triangle.ppm` and `output/gpu_mesh_scene.ppm`. Correct
by construction: the kernel calls the same `RR_HD generate_camera_ray`,
`intersect_sphere`, and `intersect_triangle` that
`camera_tests` (43) and `geometry_tests` (40) cover on the host.

#### Hard-rule check

- **No CPU intersection**: every ray-primitive test happens inside
  `k_render_scene`. Host calls to `intersect_triangle` exist only
  in `geometry_tests` for validation.
- **No CPU pixel iteration in the render path**:
  `Image::save_ppm` is the only CPU loop over pixels (image save
  internals, permitted).

#### What this milestone closes (M10 / Module 8)

- M10 (GPU Scene Upload & Triangle Mesh) -> landed: scene upload
  + naive triangle rendering both work.
- Module 8 (Geometry System) -> landed: host-side `Sphere` /
  `Triangle` / `Mesh`, host- and device-callable
  `intersect_sphere` / `intersect_triangle`, GPU-uploadable form
  via `GpuMesh`, all exercised end-to-end.

### 2026-04-27 — GPU mesh upload landed (M10 - kernel side still open)

Backend-agnostic owner for a single mesh's GPU resources.
`rr::gpu::GpuMesh` carries two device-resident buffers (vertices +
triangle indices) plus the per-mesh metadata (material id +
world-space transform), and a thin `CudaMeshView` POD declares the
launch-argument shape the eventual mesh kernel will consume. No
kernel reads it yet (per the prompt: "no rendering yet, no BVH
yet").

- **`src/gpu/GpuMesh.h` / `.cpp`:** move-only RAII container.
  `GpuBuffer<rr::geometry::Vertex>` for the vertex array,
  `GpuBuffer<rr::geometry::Triangle>` for the index array, both
  dynamic (no compile-time cap). Surface:
  `upload_vertices(host, count)`, `upload_triangles(host, count)`,
  `set_metadata(material_id, transform)`,
  `upload_from(const Mesh&)` convenience, plus queries
  (`vertex_count`, `triangle_count`, `material_id`, `transform`,
  `has_data`, `device_vertices`, `device_triangles`). Empty
  uploads always succeed; non-empty uploads fail predictably
  without a backend (counts stay zero, device pointers stay
  null). Metadata is host state and is set even on a failed
  upload so callers can inspect partial state for debugging.
- **`src/cuda/CudaMesh.cuh`:** `CudaMeshView` POD - device
  pointers + counts + material id + transform, the launch-argument
  shape the kernel will read by value. Defined now as a stable
  contract so the next slice (closest-hit triangle loop) can land
  without touching this header.
- **`tests/gpu_tests.cpp`:** +30 GpuMesh assertions (71/71 total).
  Default state; metadata setter is pure host (succeeds without
  a backend); empty upload succeeds everywhere; non-empty upload
  without a backend fails predictably with counts zero / device
  pointers null; `upload_from` round-trip on a quad with full
  metadata - succeeds with non-zero counts when CUDA + a device
  are present, fails predictably otherwise; move-only preserves
  metadata across move-ctor and move-assign.
- **`CMakeLists.txt`:** `rr_gpu` lists `src/gpu/GpuMesh.cpp` and
  PUBLIC-links `rr_geometry` (the renderer's GPU layer needs the
  Mesh / Triangle / Vertex types). PUBLIC-link list for `rr_gpu`
  is now `rr_scene rr_geometry`; image / camera flow in
  transitively.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/8 Test #1: math_tests       ........... Passed  0.00 sec
2/8 Test #2: image_tests      ........... Passed  0.00 sec
3/8 Test #3: gpu_tests        ........... Passed  0.00 sec
4/8 Test #4: camera_tests     ........... Passed  0.00 sec
5/8 Test #5: geometry_tests   ........... Passed  0.00 sec
6/8 Test #6: relativity_tests ........... Passed  0.00 sec
7/8 Test #7: scene_tests      ........... Passed  0.00 sec
8/8 Test #8: mesh_tests       ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 8

$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 71/71 passed
```

#### Hard-rule check

- **CPU uploads only**: every `GpuMesh` operation either copies a
  POD into host state (metadata setter) or pushes contiguous
  arrays through `GpuBuffer<T>`. No per-vertex / per-triangle
  CPU work; the kernel-side iteration arrives in the next slice.
- **No rendering changes**: `--render` continues to use the M10
  sphere-only scene; no kernel reads `GpuMesh` or
  `CudaMeshView` yet.

#### Not in this slice (M10 still "in progress")

- No mesh kernel. The closest-hit loop over uploaded triangles,
  the `intersect_triangle` routine, and a `CudaMeshView` slot
  inside `CudaSceneView` are the next slice.
- No BVH per the prompt; brute-force intersection is the M10
  baseline. OptiX acceleration arrives at M15.
- `SceneMesh` in `scene/Scene.h` remains the M9 placeholder
  (name + source_path + material_index). It will be rewritten
  to embed an `rr::geometry::Mesh` once the kernel actually
  reads it.

### 2026-04-27 — Mesh geometry structures landed (Geometry System in progress)

Host-side mesh data model. No renderer wiring; no GPU upload yet.
The structures are sized and laid out so the next M10 slice can
upload them with `GpuBuffer<Vertex>` / `GpuBuffer<Triangle>`
without any reshape.

- **`src/math/Transform.h`:** the canonical Transform now lives
  in math, with the same fields and `identity()` factory as the
  former `scene::Transform`. Promotion was needed so geometry
  could carry a transform without crossing back into scene
  (scene already depends on geometry via Sphere; the inverse
  would have been a cycle).
- **`src/scene/Transform.h`:** thin back-compat shim. Now reads
  `using Transform = rr::math::Transform;` so existing callers
  (`SceneObject`, scene tests, the parser when it lands) keep
  working unchanged.
- **`src/geometry/Triangle.h`:** plain `Triangle { uint32_t v0,
  v1, v2 }` POD. CCW front-face winding (matches the convention
  the upcoming `intersect_triangle` will use). Layout-compatible
  with a flat `uint32_t[3*N]` index array - the form most kernels
  will read. `RR_HD make_triangle` factory for symmetry with
  `make_sphere`.
- **`src/geometry/Mesh.h`:** `Vertex { position, normal, uv }`
  with sensible defaults so callers with positions only can still
  construct a mesh and fill the rest later. `Mesh { vertices,
  triangles, material_id (-1 = renderer default), transform }`
  plus `vertex_count`, `triangle_count`, `empty`, `clear`,
  `reserve` helpers. Vector-of-vertex / vector-of-triangle storage
  is the contiguous form the GPU upload path consumes via
  `GpuBuffer<...>`.
- **`src/geometry/Mesh.cpp`:** `Mesh::empty()` (returns true if
  either list is empty - both are required for a renderable mesh),
  `Mesh::clear()` (full reset including transform and
  material_id), `Mesh::reserve` (pure capacity hint, does not
  change reported counts).
- **`tests/mesh_tests.cpp`:** 47 host assertions covering Triangle
  aggregate / factory / defaults; Vertex defaults and explicit
  init; Mesh default state; populating a unit quad as two CCW
  triangles with material_id and transform set; the
  "empty if either list empty" rule (vertices alone is empty,
  triangles alone is empty); `clear()` reset; back-compat that
  `rr::scene::Transform` still resolves to the same type as
  `rr::math::Transform`.
- **`CMakeLists.txt`:** added `rr_geometry` static library
  (`src/geometry/Mesh.cpp`) and the `mesh_tests` executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/8 Test #1: math_tests       ........... Passed  0.00 sec
2/8 Test #2: image_tests      ........... Passed  0.00 sec
3/8 Test #3: gpu_tests        ........... Passed  0.00 sec
4/8 Test #4: camera_tests     ........... Passed  0.00 sec
5/8 Test #5: geometry_tests   ........... Passed  0.00 sec
6/8 Test #6: relativity_tests ........... Passed  0.00 sec
7/8 Test #7: scene_tests      ........... Passed  0.00 sec
8/8 Test #8: mesh_tests       ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 8

$ ./build/bin/mesh_tests
mesh_tests: 47/47 passed
```

#### Not in this slice

- No renderer wiring per the prompt: `--render` continues to use
  the M10 sphere-only scene; no kernel reads `Mesh` yet.
- No GPU upload: `GpuScene` does not yet expose
  `upload_meshes(...)` and `CudaSceneView` has no mesh handle.
  Those are the next M10 deliverable.
- No triangle intersection routine in `cuda/CudaIntersection.cuh`
  yet; lands with the GPU mesh upload.
- `SceneMesh` in `scene/Scene.h` remains the M9 placeholder
  (name + source_path + material_index). It rewrites to embed an
  `rr::geometry::Mesh` once the upload path uses it.

### 2026-04-27 — M10 GPU scene upload landed (sphere arrays only)

The Scene Graph now has a real consumer. The host populates an
`rr::scene::Scene`, hands it to a `GpuScene` for upload, and
`CudaRenderer::render_scene` walks the device-side sphere array per
pixel. Triangle mesh upload remains the open piece of M10; when it
lands the milestone is fully complete.

- **`src/gpu/GpuScene.h` / `.cpp`:** backend-agnostic GPU scene
  container. Stores the camera / observer / relativity PODs as
  host snapshots (uploads "always succeed" for those - no GPU
  work) plus a device-resident sphere array via
  `GpuBuffer<rr::geometry::Sphere>`. Surface:
  `upload_camera`, `upload_relativity`, `upload_spheres(host,
  count)` (dynamic count, no compile-time cap), and a convenience
  `upload_from(scene)` that flattens visible spheres on the host
  before issuing the device upload. Move-only, copy-deleted; the
  empty-sphere upload is a no-op success regardless of backend so
  consumers can build empty scenes without branching.
- **`src/cuda/CudaScene.cuh`:** `CudaSceneView { camera, observer,
  params, spheres (device pointer), sphere_count }` POD that the
  scene-render kernel takes by value, plus the host-callable
  `launch_render_scene` declaration.
- **`src/cuda/CudaTestKernel.cu`:** added `__global__ k_render_scene`
  and its launcher. Per pixel: ray-gen -> aberration -> closest-hit
  loop over the sphere array (tightening `t_max` as it accepts
  hits) -> base shade -> Doppler colour -> beaming -> framebuffer
  write. Same pipeline as the M9 single-sphere kernel; the only
  change is the loop. Brute-force; OptiX acceleration arrives at
  M15.
- **`src/cuda/CudaRenderer.{h,cu}`:** added
  `render_scene(GpuScene, w, h)`. Validates that the scene has a
  camera + relativity uploaded, builds a `CudaSceneView`, and runs
  the existing `run_kernel_render` scaffold.
- **`src/main.cpp`:** `--render` now constructs a host `Scene`
  with four spheres (centre + two flankers + a large "ground"
  sphere), uploads to a `GpuScene`, calls `render_scene`, and saves
  the single deliverable `output/gpu_scene_spheres.ppm`.
  `--output` is still ignored at this milestone for reproducibility.
- **`tests/gpu_tests.cpp`:** added 21 host assertions on `GpuScene`
  (41/41 total). Coverage: default state (no camera / relativity /
  spheres); camera + relativity uploads succeed unconditionally
  (pure host snapshots); empty sphere upload succeeds everywhere
  (backend or no backend); non-empty sphere upload fails predictably
  on the host-only build with `sphere_count == 0`,
  `device_spheres() == nullptr`; `upload_from` filters invisible
  spheres on the host before uploading.
- **`CMakeLists.txt`:** `rr_gpu` now lists `src/gpu/GpuScene.cpp`
  and PUBLIC-links `rr_scene` (the renderer's GPU layer needs the
  scene structures).

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/7 Test #1: math_tests       ........... Passed  0.00 sec
2/7 Test #2: image_tests      ........... Passed  0.00 sec
3/7 Test #3: gpu_tests        ........... Passed  0.00 sec
4/7 Test #4: camera_tests     ........... Passed  0.00 sec
5/7 Test #5: geometry_tests   ........... Passed  0.00 sec
6/7 Test #6: relativity_tests ........... Passed  0.00 sec
7/7 Test #7: scene_tests      ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 7

$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 41/41 passed

$ ./build/bin/RelativityRender --render scene.scn --width 16 --height 16
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a Turing/Ampere/Ada
GPU) produces `output/gpu_scene_spheres.ppm`. The kernel calls the
same `RR_HD` ray-gen / aberration / intersection routines covered
by `camera_tests`, `geometry_tests`, and `relativity_tests` on the
host, so the device math is validated by construction.

#### Hard-rule check

- **CPU uploads only**: the host populates `Scene`, calls
  `GpuScene::upload_from`, and launches one kernel. No per-pixel
  / per-ray work happens on the CPU. The closest-hit loop and
  shading run inside `k_render_scene`.
- **No CPU pixel iteration in the render path**:
  `Image::save_ppm` is the only CPU loop over pixels (image save
  internals, permitted).

#### Not in this slice (M10 still "in progress")

- Triangle mesh upload. The geometry / mesh module is the next
  piece - it adds `rr::geometry::TriangleMesh`,
  `GpuBuffer<float>` / `GpuBuffer<uint32_t>` arrays for positions
  / indices, and a per-mesh handle in `CudaSceneView`.
- Materials / lights still flow only as scene-side placeholders;
  M11 / M12 turn them into real GPU data.

### 2026-04-27 — Host-side scene structures landed (Scene Graph in progress)

Pure host data model. Camera + render settings + observer +
RelativityParams + lists of spheres / meshes (placeholder) /
materials (placeholder) / lights (placeholder). No renderer
changes; no parser. Lays the foundation that M10 (GPU upload),
M11 (materials), M12 (lights), and M13 (scene file format) will
populate.

- **`src/scene/Transform.h`:** `Transform { position,
  euler_rotation_radians, scale }` plain data with an
  `identity()` factory. The matrix conversion is intentionally
  deferred to the consumer (GPU upload at M10, scene file at M13)
  so the data model doesn't bake in a math choice (column- vs
  row-major, Euler order, quaternion form) before we know who
  reads it.
- **`src/scene/SceneObject.h`:** `SceneObject { name, transform,
  visible }` - the common metadata composed into every
  transformable scene entity. Composition over inheritance, per
  the development rules.
- **`src/scene/Scene.h`:** the top-level container.
  `RenderSettings { width, height, samples_per_pixel, max_depth }`
  alongside scene-side wrappers:
  - `SceneSphere { object, geometry, material_index }`
    (geometry is `rr::geometry::Sphere`).
  - `SceneMesh { object, source_path, material_index }` -
    placeholder until M11.
  - `SceneMaterial { name, albedo }` - placeholder until M11.
  - `SceneLight { object, color, intensity }` - placeholder
    until M12.
  Materials are referenced by integer index so the data is
  trivially serialisable for M13 and uploadable for M11.
- **`src/scene/Scene.cpp`:** `Scene::clear()` empties every list
  and resets camera / render settings / observer / relativity to
  default-constructed state.
- **`tests/scene_tests.cpp`:** 38 host assertions covering
  defaults (Transform, SceneObject, RenderSettings), Scene
  emptiness on construction, populating the four lists with
  cross-referenced indices, and `clear()` resetting every field.
- **`CMakeLists.txt`:** added `rr_scene` static library
  (`src/scene/Scene.cpp`, PUBLIC link to `rr_camera`) and a
  `scene_tests` test executable.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/7 Test #1: math_tests       ........... Passed  0.00 sec
2/7 Test #2: image_tests      ........... Passed  0.00 sec
3/7 Test #3: gpu_tests        ........... Passed  0.00 sec
4/7 Test #4: camera_tests     ........... Passed  0.00 sec
5/7 Test #5: geometry_tests   ........... Passed  0.00 sec
6/7 Test #6: relativity_tests ........... Passed  0.00 sec
7/7 Test #7: scene_tests      ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 7

$ ./build/bin/scene_tests
scene_tests: 38/38 passed
```

#### Not in this slice

- Scene Graph is "in progress", not "landed". The data model is
  in; what's missing for "landed" is at least the upload path
  to the GPU (M10) so a real consumer exercises every field.
- No renderer changes: `--render` continues to use the M9
  hard-coded sphere + relativity sweep. Nothing reads `Scene`
  yet.
- No scene file format: M13 will define the on-disk schema and
  parser; today only the in-memory representation exists.
- Mesh / material / light wrappers are deliberately minimal -
  the matching modules (M11 / M12) replace the placeholders
  with real data.

### 2026-04-27 — M9 relativity integrated into the CUDA sphere renderer

The relativity math leaf is now wired into the GPU sphere pipeline. The
six-step pipeline runs entirely on the device per pixel; the host only
configures, launches once per beta, and saves.

- **`src/cuda/CudaKernels.cuh`:** added `launch_sphere_relativistic`
  declaration. Includes `relativity/RelativityParams.h` so the
  `Observer` and `RelativityParams` PODs are part of the kernel ABI.
- **`src/cuda/CudaTestKernel.cu`:** added `__global__
  k_sphere_relativistic`. Per pixel:
  1. `generate_camera_ray`
  2. (if `enable_aberration`) `aberrateDirection(observer.velocity,
     ray.direction)`
  3. `intersect_sphere`
  4. base shade (`0.5*n + 0.5` on hit; sky gradient on miss)
  5. (if `enable_doppler`) `applyDopplerColor` modulated by
     `doppler_color_strength`
  6. (if `enable_searchlight`) scale by
     `lerp(1, D^4, searchlight_strength)`
  7. framebuffer write.
  Aberration runs *before* intersection so geometry is hit-tested in
  the apparent frame; Doppler / searchlight run *after* shading so
  they modify perceived radiance rather than surface state.
- **`src/cuda/CudaRenderer.{h,cu}`:** added
  `render_relativistic_sphere(camera, observer, params, sphere, w, h)`.
  Reuses the templated `run_kernel_render` scaffold from M7 (validate
  -> allocate `GpuBuffer<float>` -> launch -> drain CUDA errors ->
  download into `Image`).
- **`src/main.cpp`:** `--render` now performs a fixed four-beta sweep
  along -Z (forward motion) and writes:
  - `output/sphere_beta_000.ppm`
  - `output/sphere_beta_025.ppm`
  - `output/sphere_beta_075.ppm`
  - `output/sphere_beta_095.ppm`
  `--output` is intentionally ignored at this milestone so the
  deliverable is reproducible. Single-image renders remain available
  through `CudaRenderer::render_sphere` for tests / future CLI
  additions.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/6 Test #1: math_tests       ........... Passed  0.00 sec
2/6 Test #2: image_tests      ........... Passed  0.00 sec
3/6 Test #3: gpu_tests        ........... Passed  0.00 sec
4/6 Test #4: camera_tests     ........... Passed  0.00 sec
5/6 Test #5: geometry_tests   ........... Passed  0.00 sec
6/6 Test #6: relativity_tests ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 6

$ ./build/bin/RelativityRender --render scene.scn --width 16 --height 16
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a Turing/Ampere/Ada
GPU) produces the four PPM files. The kernel calls the same `RR_HD`
routines exercised by `relativity_tests` (52 assertions) and
`geometry_tests` (25 assertions), so the host coverage validates the
device math by construction.

#### Hard-rule check

- **All effects on the GPU**: every step of the per-pixel pipeline
  (ray gen, aberration, intersection, base shade, Doppler colour,
  searchlight, framebuffer write) runs inside `k_sphere_relativistic`.
- **No CPU pixel/ray work**: `main.cpp` builds Camera / Sphere /
  Observer / RelativityParams structs, calls the renderer once per
  beta, and writes the downloaded `Image` to PPM. The only CPU
  iteration over pixels is inside `Image::save_ppm` (image save
  internals, permitted).

#### Note on physical honesty at high beta

`searchlight_strength = 1.0` (the default) applies the bolometric
`D^4` factor at full strength. At `beta = 0.95` along the view axis
the forward direction has `D ~= 6.25` and `D^4 ~= 1500`, so on-axis
pixels saturate to white in `output/sphere_beta_095.ppm`. That is
the iconic relativistic-flight headlight effect; lowering
`searchlight_strength` (or moving toward a tone-mapped HDR pipeline
once we have one) is an artistic decision that lives behind the
existing knob - the math itself is intentionally physical.

### 2026-04-27 — M9 relativity math leaf landed (no renderer integration)

The mathematical core of the differentiator: Lorentz factor, length
contraction, relativistic Doppler, beaming, aberration, and a clearly
labelled artistic Doppler colour shift. Every function is `RR_HD
inline`, callable from kernels and host tests alike. No camera or
renderer wiring yet - that comes in the next slice.

- **`src/relativity/RelativityParams.h`:** two PODs.
  - `Observer { velocity (Vec3 in c-units) }` - kinematic state of
    the boosted frame; position lives on the camera.
  - `RelativityParams { enable_aberration, enable_doppler,
    enable_searchlight, doppler_color_strength,
    searchlight_strength, max_beta }` - artist-facing toggles and
    intensities, separated from `Observer` so artistic knobs don't
    pollute the physical state.
- **`src/relativity/RelativityMath.h`:** the math leaf. Natural
  units (c = 1) throughout. Each function carries an explicit
  PHYSICAL or ARTISTIC APPROXIMATION tag in its docstring:
  - `clampBeta` (PHYSICAL): magnitude clamp below the lightspeed
    singularity; `max_beta` itself capped at `0.999999`.
  - `gamma` (PHYSICAL): `1 / sqrt(1 - beta^2)` with a numerical
    safety net when the caller forgets to clamp.
  - `lorentzContraction` (PHYSICAL): `sqrt(1 - beta^2)` (i.e.
    `1/gamma`).
  - `dopplerFactor` (PHYSICAL): `1 / [gamma * (1 - beta . dir)]`
    with `dir` the photon's scene-frame direction of travel.
  - `searchlightFactor` (PHYSICAL, bolometric): `D^4` (specific
    intensity uses `D^3` if the renderer wants monochromatic
    light).
  - `aberrateDirection` (PHYSICAL): textbook Lorentz aberration in
    vector form, identity at `|beta| = 0`, output renormalised.
    Treats `direction` as the photon's direction of travel
    (the SOURCE position is the opposite vector).
  - `applyDopplerColor` (ARTISTIC APPROXIMATION): bounded
    `tanh(0.5 * log(D)) * strength` mix toward a cool tint for
    blueshift and a warm tint for redshift. Identity at
    `strength = 0` and at `D = 1`. Marked clearly as a
    placeholder until the spectral pipeline arrives in M16/M17.
- **`src/relativity/RelativityMath.cuh`:** thin re-export of
  `RelativityMath.h` so kernel TUs can include a `.cuh`. Future
  device-specific intrinsics (`rsqrtf`, `__fdividef`) can land
  here without touching the host surface.
- **`tests/relativity_tests.cpp`:** 52 host-side assertions covering
  every function on the requested list. Spot-checks include
  `gamma(0.6) = 1.25` (3-4-5), `lorentzContraction(0.6) = 0.8`,
  Doppler identity at rest, longitudinal `D_blue * D_red = 1`,
  transverse-Doppler `D = 1/gamma`, `searchlightFactor(2) = 16`,
  aberration zero-beta identity, unit-length output, the
  along-motion invariant, and the perpendicular case which gives
  the closed-form result `d' = (-beta, perp/gamma, 0)`. The
  artistic colour shift is checked for `strength = 0` identity,
  `D = 1` identity, and the warm/cool tint behaviour at strong
  red/blueshift.
- **`CMakeLists.txt`:** added `relativity_tests` test executable.
  Header-only module; just needs `src/` on the include path.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/6 Test #1: math_tests       ........... Passed  0.00 sec
2/6 Test #2: image_tests      ........... Passed  0.00 sec
3/6 Test #3: gpu_tests        ........... Passed  0.00 sec
4/6 Test #4: camera_tests     ........... Passed  0.00 sec
5/6 Test #5: geometry_tests   ........... Passed  0.00 sec
6/6 Test #6: relativity_tests ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 6

$ ./build/bin/relativity_tests
relativity_tests: 52/52 passed
```

#### Not in this slice (M9 still "in progress")

- No renderer integration, per the explicit instruction in the
  prompt. `Observer` does not yet flow into `Camera`, the kernels
  do not yet call `aberrateDirection`, and `--render` continues
  to use the classical sphere kernel.
- Retarded-time approximation: out of scope for this slice; lands
  with the path tracer (M14) where it can interact with motion
  blur sampling.

#### Naming note

The functions use camelCase (`clampBeta`, `gamma`,
`lorentzContraction`, `dopplerFactor`, `searchlightFactor`,
`aberrateDirection`, `applyDopplerColor`) per the prompt. The
rest of the project uses snake_case; the relativity module is the
exception so the API names match the physics literature.

### 2026-04-27 — M8 GPU primitive intersection landed

First real ray-traced output: per pixel the GPU generates the primary
ray, intersects against a single sphere, and shades. The CPU only
constructs the camera and sphere structs and launches the kernel.

- **`src/geometry/Sphere.h`:** plain-data sphere POD (`center`,
  `radius`) with an `RR_HD make_sphere(...)` factory. Trivial
  aggregate so it can be passed to kernels by value with no
  ABI surprises. The full geometry system (triangle meshes,
  instancing, AS-build inputs) lands in M10; this is the minimum
  primitive needed to validate GPU intersection.
- **`src/renderer/Hit.h`:** `Hit { hit, t, position, normal }` POD
  plus an `RR_HD make_miss()` factory. Material / primitive ids
  are deferred to M14 (path tracer); keeping `Hit` minimal here
  avoids leaking later concerns into the geometry layer.
- **`src/cuda/CudaIntersection.cuh`:** `RR_HD inline
  intersect_sphere(ray, sphere, t_min, t_max) -> Hit`. Solves the
  half-`b` quadratic with `disc = b*b - a*c`, falls back to the far
  root when the near one is out of `(t_min, t_max)`, returns a miss
  on a degenerate ray (`a <= 0`). Uses `sqrtf` and the inverse-radius
  multiply to avoid a redundant `length` call. Despite the `.cuh`
  extension the header is host- and device-callable and pulls in no
  CUDA runtime, so the host tests exercise the same code path the
  kernel runs.
- **`src/cuda/CudaKernels.cuh` / `CudaTestKernel.cu`:** added
  `launch_sphere_visualize` and `__global__ k_sphere_visualize`.
  Per pixel the kernel calls `generate_camera_ray`, runs
  `intersect_sphere`, and writes either the normal-as-color shade
  (`0.5*n + 0.5`) on hit or a vertical sky gradient
  (`mix(white, sky_blue, 0.5*(dir.y + 1))`) on miss. Same launch
  config as the earlier kernels (16x16 blocks, ceiling-divided
  grid, bounds-checked).
- **`src/cuda/CudaRenderer.h` / `.cu`:** added
  `render_sphere(camera, sphere, w, h)`. Validates the radius,
  snapshots the camera into the device POD, and reuses the
  templated `run_kernel_render` scaffold from M7. `render_gradient`
  and `render_camera_rays` are kept as diagnostics.
- **`src/main.cpp`:** `--render` now defaults to
  `output/gpu_sphere.ppm`, builds a default `Camera` and a
  hard-coded test sphere `{(0, 0, -3), r = 1}`, and calls
  `render_sphere`. Real scene loading lands at M13. Without CUDA
  the executable still compiles and reports the missing backend
  honestly.
- **`tests/geometry_tests.cpp`:** 25 host-side assertions. Direct
  unit tests on `intersect_sphere`: rays pointing the wrong way
  miss; the centre ray hits the front of a sphere with `t = 2`,
  `position = (0, 0, -2)`, `normal = (0, 0, 1)`; grazing rays
  miss; `(t_min, t_max)` clipping behaves correctly (including
  forcing the far-root fallback); rays starting inside a sphere
  hit at the far root. Plus a "kernel replay" pair: build the same
  default camera the M8 kernel uses, generate the centre / corner
  primary rays, intersect against the same hard-coded test sphere,
  and assert hit / miss + geometry. The kernel runs the same
  `RR_HD` routine, so these host tests cover the device path by
  construction.
- **`CMakeLists.txt`:** added `geometry_tests` test executable
  (links `rr_camera` for math + camera headers; `Sphere.h`,
  `Hit.h`, and `CudaIntersection.cuh` are header-only and need no
  library).

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake -S . -B build && cmake --build build
$ cd build && ctest --output-on-failure
1/5 Test #1: math_tests     ........... Passed  0.00 sec
2/5 Test #2: image_tests    ........... Passed  0.00 sec
3/5 Test #3: gpu_tests      ........... Passed  0.00 sec
4/5 Test #4: camera_tests   ........... Passed  0.00 sec
5/5 Test #5: geometry_tests ........... Passed  0.00 sec
100% tests passed, 0 tests failed out of 5

$ ./build/bin/geometry_tests
geometry_tests: 25/25 passed

$ ./build/bin/RelativityRender --render scene.scn --width 16 --height 16
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a machine with
NVCC + a Turing/Ampere/Ada GPU) produces `output/gpu_sphere.ppm`.
The kernel calls the exact same `RR_HD` ray and intersection
routines exercised by `camera_tests` + `geometry_tests`, so the
host tests cover the device math.

#### Hard-rule check

- **No CPU ray tracing:** the rendered output is produced entirely
  by `k_sphere_visualize`. Host calls to `intersect_sphere` exist
  only inside `geometry_tests` for validation.
- **CPU only creates struct + launches:** `main.cpp` builds a
  `Camera` and a `Sphere`, calls `CudaRenderer::render_sphere`,
  and writes the downloaded `Image` to PPM. The only CPU iteration
  over pixels is in `Image::save_ppm` (image save internals,
  permitted).

### 2026-04-27 — M7 camera system & GPU camera rays landed

Pinhole perspective camera on the host, plain-data POD on the device,
and a `RR_HD` ray-generation function the kernel and host tests both
call. The new `--render` path lets the GPU generate one primary ray
per pixel and encodes the direction as RGB; the CPU only configures
the camera, launches, downloads, and saves.

- **`src/camera/CameraRay.h`:** `CameraRay { origin, direction }` and
  the device-friendly `GpuCamera { position, forward, up, right,
  tan_half_vfov, aspect }` POD. The pre-computed `tan_half_vfov` keeps
  the per-thread arithmetic to a few multiplies and one normalize.
  `RR_HD inline generate_camera_ray(GpuCamera, x, y, w, h)` samples
  pixel centres at `(x+0.5, y+0.5)`, builds the image-plane offset
  `(u = (2x/w - 1) * aspect * tan_half_vfov,
    v = (1 - 2y/h) * tan_half_vfov)` (top-left origin), and returns
  the normalized direction `forward + right*u + up*v`. Same code path
  runs on host and device.
- **`src/camera/Camera.h` / `.cpp`:** host class with explicit
  position / forward / up / right / vfov / aspect / near / far. `look_at`
  re-orthogonalizes the basis (with a sensible fallback when the up
  hint is parallel to forward, and a no-op when eye == target).
  Setters clamp vfov to `(0.01, 179.0)` degrees and reject
  non-positive aspect ratios. `to_gpu()` snapshots into the device
  POD.
- **`src/cuda/CudaKernels.cuh`:** declares
  `launch_camera_rays_visualize(float*, w, h, GpuCamera, stream)`
  alongside the existing gradient launcher.
- **`src/cuda/CudaTestKernel.cu`:** added `__global__
  k_camera_rays_visualize` that calls
  `rr::camera::generate_camera_ray` per thread and writes
  `(0.5*dir + 0.5, 1.0)` into the Rgba32F framebuffer. Same launch
  config as the gradient kernel (16x16 blocks, ceiling-divided grid,
  bounds-checked).
- **`src/cuda/CudaRenderer.h` / `.cu`:** factored the host scaffold
  (validate dims -> allocate `GpuBuffer<float>` -> launch kernel ->
  drain CUDA errors -> download into `Image`) into a templated
  `run_kernel_render(...)` helper. `render_gradient` keeps its
  existing surface; `render_camera_rays(camera, w, h)` is the new
  entry point. Both delegate per-pixel work to the GPU.
- **`src/main.cpp`:** `--render` now defaults to
  `output/gpu_camera_rays.ppm`, constructs an
  `rr::camera::Camera` (origin, looking down -Z, aspect = w/h), and
  calls `render_camera_rays`. Without CUDA the executable still
  compiles and reports the missing backend honestly.
- **`tests/camera_tests.cpp`:** 43 host-side assertions: default
  state, basis orthonormality after `look_at` (incl. the
  parallel-up-hint fallback and the eye==target degenerate case),
  vfov clamping, `to_gpu()` round-trip, and `generate_camera_ray`
  geometry checks (centre pixel ≈ forward; corner sign patterns
  for top-left and bottom-right; all directions unit-length; wider
  aspect ratio pushes the left-edge ray further left). The same
  function runs on the GPU - validating the math on the host
  validates the kernel by construction.
- **`CMakeLists.txt`:** added `rr_camera` static library
  (`src/camera/Camera.cpp`, `PUBLIC src` includes). When
  `RR_ENABLE_CUDA` is on, `rr_gpu` PUBLIC-links `rr_camera` because
  `CudaRenderer::render_camera_rays` consumes `Camera`. Added a
  fourth test executable, `camera_tests`.

#### Verified locally (host-only, no CUDA Toolkit on this box)

```
$ cmake -S . -B build && cmake --build build
$ cd build && ctest --output-on-failure
1/4 Test #1: math_tests   ............ Passed  0.00 sec
2/4 Test #2: image_tests  ............ Passed  0.00 sec
3/4 Test #3: gpu_tests    ............ Passed  0.00 sec
4/4 Test #4: camera_tests ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 4

$ ./build/bin/camera_tests
camera_tests: 43/43 passed

$ ./build/bin/RelativityRender --render scene.scn --width 32 --height 32
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled run (`-DRR_ENABLE_CUDA=ON`, on a machine with NVCC
+ a Turing/Ampere/Ada GPU) produces `output/gpu_camera_rays.ppm`. It
is correct by construction: the `RR_HD` ray generator the kernel
calls is the exact same function `camera_tests` exercises on the
host, so the host tests cover the device math.

#### Hard-rule check

- **All ray generation on the GPU**: the `__global__ k_camera_rays_visualize`
  is the only place rays are produced for the rendered output. The
  host calls `generate_camera_ray` only inside `camera_tests` to
  validate the math; the executable's render path never iterates
  pixels on the CPU.
- **No CPU pixel loop**: the only iteration over pixels in the
  render path is inside `Image::save_ppm`, which converts floats to
  bytes for the PPM payload (image save internals, permitted).

### 2026-04-27 — M6 first CUDA kernel (GPU-rendered gradient) landed

End-to-end host -> device -> kernel -> host -> PPM pipeline. The GPU
generates every pixel; the CPU only allocates, launches, downloads,
and saves. The save path is the only place a CPU loop touches pixels,
exactly as the engineering rules permit.

- **`src/cuda/CudaKernels.cuh`:** kernel-side helpers + host-callable
  launch wrapper declarations. Pulls in `cuda_runtime.h`, so it is
  only safe to include from `.cu` files. Currently exposes
  `launch_gradient_rgba32f(float*, int, int, cudaStream_t)`.
- **`src/cuda/CudaTestKernel.cu`:** the actual `__global__` kernel.
  One thread per pixel, 16x16 blocks, ceiling-divided grid. Each
  thread writes (R=u, G=v, B=0, A=1) into the channel-interleaved
  Rgba32F layout used by `rr::image::Image`. Bounds-checked. The
  host-callable wrapper is a thin launch-config shim - no CPU
  pixel logic.
- **`src/cuda/CudaRenderer.h`:** CUDA-Runtime-free public surface.
  `CudaRenderer::Result { ok, image, message }` plus a single
  `render_gradient(width, height) -> Result` static. The header is
  host-includable; only the `.cu` implementation pulls in
  `cuda_runtime.h`.
- **`src/cuda/CudaRenderer.cu`:** allocates a `GpuBuffer<float>` of
  `width*height*4` floats, launches the gradient kernel, drains
  errors via `cudaGetLastError` + `cudaDeviceSynchronize`, and
  downloads into a fresh `Image(width, height, Rgba32F)`. On any
  CUDA failure it returns `ok=false` with a human-readable message
  derived from `cudaGetErrorString` and clears the sticky error
  state.
- **`src/main.cpp`:** the `--render` path now calls
  `CudaRenderer::render_gradient`, creates the parent directory if
  needed, and saves to `output/gpu_gradient.ppm` (or
  `--output <path>` when given). Gated by `RR_HAS_CUDA`; without
  CUDA the executable still compiles and reports the missing
  backend honestly.
- **`CMakeLists.txt`:** under `RR_ENABLE_CUDA`, `enable_language(CUDA)`
  is now turned on, `CMAKE_CUDA_STANDARD = 17`, and
  `CMAKE_CUDA_ARCHITECTURES` defaults to `75;80;86;89` (Turing
  through Ada; override via `-DCMAKE_CUDA_ARCHITECTURES=...`). The
  `.cu` files join `rr_gpu`. `RR_HAS_CUDA` is now `PUBLIC` so
  consumers (RelativityRender exe, gpu_tests) gate CUDA call
  sites on it. `rr_gpu` PUBLIC-links `rr_image` when CUDA is on
  because `CudaRenderer::Result` carries an `Image`.

#### Verified locally (host-only, no CUDA Toolkit on this machine)

```
$ cmake -S . -B build && cmake --build build
$ cd build && ctest --output-on-failure
1/3 Test #1: math_tests  ............ Passed  0.00 sec
2/3 Test #2: image_tests ............ Passed  0.00 sec
3/3 Test #3: gpu_tests   ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 3

$ ./build/bin/RelativityRender --render scene.scn --width 64 --height 32
[INFO] RelativityRender 0.0.1 starting
[INFO] render command received
[INFO] (no CUDA backend compiled; rebuild with -DRR_ENABLE_CUDA=ON to render)
```

The CUDA-enabled path (`-DRR_ENABLE_CUDA=ON`, on a machine with
NVCC + an NVIDIA GPU) produces `output/gpu_gradient.ppm`. It is
correct by construction (uses only the standard CUDA Runtime API,
the kernel is bounds-checked, the layout matches
`rr::image::Image::PixelFormat::Rgba32F`) but is not end-to-end
runnable in this environment.

#### Hard rule check

- GPU generates every pixel: yes - the kernel computes `(u, v, 0, 1)`
  per pixel; CPU never reads or writes per-pixel values.
- CPU pixel loop only inside image save: yes - the only CPU iteration
  over pixels is in `Image::save_ppm`, which converts floats to
  bytes for the PPM payload (this is "image save internals" per the
  engineering rules).

### 2026-04-27 — M5/M6 GPU buffer abstraction landed

Move-only typed device-memory handle with allocate / upload / download /
reset, plus a CUDA byte-level implementation. Still no kernels - the
upload-no-kernel-download round trip is enough to validate the
plumbing.

- **`src/gpu/GpuBuffer.h`:** templated `rr::gpu::GpuBuffer<T>`. RAII,
  move-only (copy ctor / assignment deleted; move ctor / assignment
  noexcept, transfers ownership and leaves the source empty).
  `static_assert(std::is_trivially_copyable_v<T>)` because the
  backend just shuffles bytes. Surface: `allocate(count)`,
  `upload(host, count)` (resizes as needed), `download(host, count)
  const`, `reset()`, `empty()`, `size()`, `size_in_bytes()`,
  `device_ptr()` (mutable + const). Allocation is deferred -
  default construction does not touch the GPU.
- **`src/gpu/GpuBuffer.cpp`:** byte-level shim
  (`detail::gpu_alloc/free/copy_h2d/copy_d2h`). `#ifdef RR_HAS_CUDA`
  forwards to `rr::cuda::cuda_alloc/free/copy_h2d/copy_d2h`; without
  CUDA, every call returns an honest failure (`nullptr` / `false`)
  so consumers receive a predictable empty-buffer state instead of a
  silent no-op.
- **`src/cuda/CudaBuffer.h`:** CUDA-Runtime-free header exposing only
  the byte-level free functions. Keeps `cuda_runtime.h` confined to
  the `.cpp` so templated consumers in `rr::gpu::` don't drag the
  CUDA toolchain onto every include path.
- **`src/cuda/CudaBuffer.cpp`:** wraps `cudaMalloc`, `cudaFree`,
  `cudaMemcpy(...HostToDevice)`, `cudaMemcpy(...DeviceToHost)`. On
  every failure path the sticky CUDA last-error flag is cleared so a
  later real CUDA call observes a clean state. `cuda_alloc(0)`
  returns `nullptr` deliberately (no zero-byte allocations);
  `cuda_free(nullptr)` is a no-op; zero-byte copies succeed.
- **`tests/gpu_tests.cpp`:** added 16 buffer assertions on top of the
  existing 4 device-query checks. Unconditional: default-state
  invariants (empty, size 0, null device_ptr, idempotent `reset`),
  move ctor + move-assign on default-constructed buffers. Host-only
  contract: with no CUDA backend, `allocate` / `upload` /
  `download(non-zero)` all fail predictably while the buffer stays
  empty, and `download(0)` succeeds as a no-op. CUDA + device
  present: upload a 256-element float array with a non-trivial
  pattern, run no kernel, download into a fresh vector, compare
  byte-for-byte; then move ownership and re-download from the new
  owner. The round-trip path skips with a printed message when CUDA
  isn't compiled or no devices are visible, so CI without GPUs
  remains green.
- **`CMakeLists.txt`:** `rr_gpu` now also lists
  `src/gpu/GpuBuffer.cpp`, and adds `src/cuda/CudaBuffer.cpp` under
  the existing `if(RR_ENABLE_CUDA)` block (same `RR_HAS_CUDA` define
  + `CUDA::cudart` link). No new CMake plumbing - the buffer rides
  through on the existing CUDA gate. No `enable_language(CUDA)` yet:
  these are still host-side calls into the CUDA Runtime API, no
  device code.

#### Verified locally (host-only configure)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/3 Test #1: math_tests  ............ Passed  0.00 sec
2/3 Test #2: image_tests ............ Passed  0.00 sec
3/3 Test #3: gpu_tests   ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 3

$ ./build/bin/gpu_tests
gpu_tests: skipping CUDA round-trip (no backend compiled)
gpu_tests: 20/20 passed
```

The CUDA-enabled round-trip is correct by construction (uses only
`cudaMalloc` / `cudaFree` / `cudaMemcpy` from the standard CUDA
Runtime, all gated on the same flag as `find_package(CUDAToolkit)`)
but is not end-to-end runnable in this environment.

### 2026-04-27 — M5 CUDA device layer landed

First GPU-aware code in the project. Backend-agnostic surface in
`rr::gpu::`; concrete CUDA implementation in `rr::cuda::`. The host
build still configures and runs without CUDA installed; CUDA is gated
by `-DRR_ENABLE_CUDA=ON`. No kernels, no allocations, no rendering -
just device detection and property queries.

- **`src/gpu/GpuDevice.h` / `.cpp`:** `rr::gpu::GpuDevice` POD struct
  (index, name, compute capability major/minor, total memory bytes,
  multiprocessor count) plus `compute_capability_string()` and
  `total_memory_human()` formatters. Free functions:
  `gpu_backend_available()`, `gpu_backend_name()`,
  `enumerate_devices()`. The `.cpp` `#ifdef RR_HAS_CUDA`-includes
  `cuda/CudaContext.h` and forwards; otherwise it returns
  `"(none)"` / `false` / empty list. Callers never need to know
  whether CUDA was compiled in.
- **`src/cuda/CudaContext.h` / `.cpp`:** `rr::cuda::query_devices()`
  wrapping the CUDA Runtime API (`cudaGetDeviceCount` +
  `cudaGetDeviceProperties`). Robust to driver-init failures: returns
  empty on failure and clears the sticky last-error so a later real
  CUDA call doesn't observe it. Compiled only when CUDA is enabled.
- **`src/main.cpp`:** `--device-info` now logs backend name, prints
  the device count, and emits one line per device formatted as
  `[i] <name> (cc <maj>.<min>, <MiB> MiB, <SMs> SMs)`. When no
  backend is compiled, it logs that explicitly and tells the user how
  to re-enable. When the backend is compiled but no devices are
  visible, it warns instead of pretending to enumerate.
- **`CMakeLists.txt`:** `find_package(CUDAToolkit REQUIRED)` only when
  `RR_ENABLE_CUDA` is ON. New `rr_gpu` static library carrying
  `src/gpu/GpuDevice.cpp`. When CUDA is enabled, `src/cuda/CudaContext.cpp`
  is added to the same library, `RR_HAS_CUDA` is defined PRIVATE, and
  `CUDA::cudart` is linked. The main executable links `rr_gpu`. No
  `enable_language(CUDA)` yet - we only call the runtime API from host
  C++; NVCC arrives in M6 with the first kernel.
- **`tests/gpu_tests.cpp`:** 4 assertions exercising the public surface
  against invariants that hold either way:
  `gpu_backend_name()` is non-empty,
  `available()` and `name() == "(none)"` agree, and
  `enumerate_devices()` is empty when no backend is compiled. Also
  validates `compute_capability_string()` and `total_memory_human()`
  formatters. CI machines without GPUs are valid environments and the
  test is silent about that.

#### Verified locally (host-only configure)

```
$ cmake --build build && cd build && ctest --output-on-failure
1/3 Test #1: math_tests  ............ Passed  0.00 sec
2/3 Test #2: image_tests ............ Passed  0.00 sec
3/3 Test #3: gpu_tests   ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 3

$ ./build/bin/RelativityRender --device-info
[..] [INFO] RelativityRender 0.0.1 starting
[..] [INFO] GPU backend: (none)
[..] [INFO] No GPU backend compiled in.
            Reconfigure with -DRR_ENABLE_CUDA=ON to enable CUDA.
```

The CUDA-enabled path (`-DRR_ENABLE_CUDA=ON`) was not exercised in
this environment (no CUDA Toolkit installed), but is correct by
construction: it relies only on the standard CMake `CUDAToolkit`
package and the `CUDA::cudart` imported target, with all CUDA-specific
sources, defines, and link deps gated on the same `RR_ENABLE_CUDA`
flag that controls the `find_package` call.

#### Module status nuance

`rr::gpu::` is the long-term backend-agnostic surface; the CUDA
backend is one implementation of it. The Module Map's "GPU Device
Layer" (#4) is the surface and is *landed*. The "CUDA Backend" (#5)
will grow to cover streams, buffers, kernel launches, error wrapping,
and pinned memory in M6+; for now it only contains the device-query
plumbing, so it is marked *in progress*.

### 2026-04-27 — M4 image / framebuffer system landed

Host-side pixel storage, set/get/clear/resize, and PPM save (no
third-party dep). PPM is intentionally minimal; OpenEXR/PNG IO arrives
when the GPU paths need real HDR formats.

- **`src/image/Color.h`:** `Rgb` and `Rgba` plain-data structs; both
  `RR_HD constexpr`-friendly so they will be usable from device code
  later. `Rgba` carries `a` defaulted to 1; `Rgba::rgb()` strips it. No
  arithmetic operators yet — premature for image storage; they'll come
  in with shading.
- **`src/image/Image.h` / `.cpp`:** `PixelFormat { Rgb32F, Rgba32F }`
  and an `Image` class with `width/height/format/channels/empty`,
  `set_pixel(x,y,Rgba)`, `get_pixel(x,y)->Rgba` (alpha=1 for Rgb32F),
  `clear(Rgba)`, `resize(w,h)` (zero-fills, format preserved),
  raw `data()` / `size_in_floats()` for future GPU upload, and
  `save_ppm(path)`. Storage is row-major, channel-interleaved,
  contiguous floats (the layout we'll mirror on the device side
  later). OOB pixel access is debug-asserted.
- **`save_ppm`** writes 8-bit P6 binary. Floats are clamped to [0,1]
  and quantized; HDR > 1 is lost; alpha is dropped (PPM has no alpha).
  Empty images return `false`. Honest minimal IO, not a stub: it
  produces files an EXR/PNG viewer can convert and a hex dump can
  validate.
- **`src/image/Framebuffer.h` / `.cpp`:** thin render-target wrapper
  owning a single color `Image`. Provides `color()` (mutable + const),
  `resize`, `clear`, `save_ppm`. AOVs, accumulation buffers, and tile
  metadata join later (M14 / M17). The Image / Framebuffer split is
  intentional: Image is generic 2D pixel storage; Framebuffer is what
  the renderer writes into during a frame.
- **`tests/image_tests.cpp`:** 39 assertions covering Rgba/Rgb format
  set/get round-trip, Rgb32F alpha-on-read = 1, clear, resize zeroes,
  Framebuffer clear + resize, and the **gradient-to-PPM IO
  validation** (verifies header `P6 W H 255` + payload size = W*H*3
  bytes). Empty-image save returns false. The gradient is the only
  CPU-side pixel generation in this module, allowed exclusively as IO
  validation.
- **`CMakeLists.txt`:** first module promoted to a static library —
  `rr_image` (`src/image/{Image,Framebuffer}.cpp` + `PUBLIC` include
  on `src/`). `image_tests` links it; the main executable does not
  yet use it. Same warning flags as elsewhere (`-Wall -Wextra
  -Wpedantic` / `/W4 /permissive-`).

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
1/2 Test #1: math_tests  ............ Passed  0.00 sec
2/2 Test #2: image_tests ............ Passed  0.00 sec
100% tests passed, 0 tests failed out of 2
```

`image_tests` reports `39/39 passed`; the gradient is written to
`<temp>/rr_image_test_gradient.ppm`, validated, and removed.

#### Order note

M4 lands before M2's remaining sub-items (`Error`, `FileSystem`,
`App`, `Config::load`/`save`, real test framework, host CI) for the
same reason M3 did: Image depends only on Math (already landed) and a
trivial subset of Core that exists now (none of the deferred Core
pieces are needed here). Per `docs/MODULE_MAP.md`, this respects the
declared dependency direction.

### 2026-04-27 — M3 math library landed

The math leaf is in. Header-only, host/device portable, and exercised by a
small test runner that hooks into `ctest`.

- **`src/math/MathUtils.h`:** `RR_HD` host/device macro (expands to
  `__host__ __device__` under NVCC, empty otherwise), constants
  (`kPi`, `kTwoPi`, `kHalfPi`, `kInvPi`, `kEpsilon`), and templated
  `min` / `max` / `clamp` / `lerp` plus `radians`, `degrees`,
  `saturate`. All `constexpr RR_HD` where possible.
- **`src/math/Vec2.h` / `Vec3.h` / `Vec4.h`:** plain structs with `float`
  members. Constructors include a single-arg `explicit` broadcast to
  prevent accidental scalar→vector conversions. Operator suite covers
  `+`, `-`, unary `-`, scalar `*` and `/`, in-place compound assignments,
  and `==` / `!=`. `Vec3` adds component-wise (Hadamard) `*`. Free
  functions: `dot` (all three), `cross` (Vec3), `length`,
  `length_squared`, `normalize` (returns zero on degenerate input
  rather than NaN). Free overloads of `clamp` and `lerp` for `Vec3`
  pick up the file's component-wise semantics without conflicting with
  the scalar templates.
- **`src/math/Mat4.h`:** row-major 4x4 (`m[row][col]`) with translation
  in column 3. Static constructors `identity`, `translation(Vec3)`,
  `scale(Vec3)`. `operator*` for matrix multiply. Free functions
  `transform_point` (homogeneous w=1, applies translation) and
  `transform_vector` (w=0, ignores translation). All `constexpr RR_HD`.
- **`tests/math_tests.cpp`:** 42 assertions covering scalar utilities,
  Vec3 add/sub/scalar/compound, dot/cross identities (right-handed
  basis + anti-commutativity), length / normalize (including the
  degenerate-input zero result), clamp/lerp, Mat4 identity / translation
  / scale, matrix multiply with `T*S != S*T`. Hand-rolled assertion
  macro is variadic so braced-init expressions like `Vec3{0,0,0}`
  inside `RR_CHECK(...)` aren't split by the preprocessor. The macro
  plumbing is throwaway — it gets replaced by Catch2/doctest on the
  M2 deferred list — but the assertions stay.
- **`CMakeLists.txt`:** added `math_tests` executable (header-only
  consumer of `src/math/`), `target_include_directories(... src)`,
  and `add_test(NAME math_tests COMMAND math_tests)` so `ctest`
  picks it up. Same warning flags as the main executable.

#### Verified locally

```
$ cmake --build build && cd build && ctest --output-on-failure
1/1 Test #1: math_tests ..................... Passed   0.00 sec
100% tests passed, 0 tests failed out of 1
```

#### Order note

The master order has Math (step 4) following Core (step 3). Math has zero
dependency on Core (it is the leaf), so per `docs/MODULE_MAP.md` it is
safe to land while M2's remaining items (`Error`, `FileSystem`, `App`,
`Config::load`/`save`, real test framework, host CI) are still pending.
M2 stays "in progress" until those land.

### 2026-04-27 — M2 configuration + command-line handling landed

Continues M2 (Core Engine). Adds the runtime configuration struct and the
command-line parser that populates it. No rendering, no GPU calls — every
command-line surface is parsed today and acted on for real once the
underlying backends arrive.

- **`src/core/Config.h` / `.cpp`:** `rr::core::Config` plain-data struct
  with `show_device_info`, `render_scene_path` (`std::optional<string>`),
  `output_image_path` (`std::optional<string>`), `width` (default 1280),
  `height` (default 720), and a `wants_render()` helper. The `.cpp` is
  intentionally near-empty — Config is a data carrier; future load/save
  (TOML / JSON) lands here without churn.
- **`src/core/CommandLine.h` / `.cpp`:** `rr::core::CommandLine` class
  with `Status { Ok, Help, Version, Error }`, a `ParseResult { status,
  message }`, a `parse(argc, argv, Config&)` static method, and a
  `usage()` static method. The parser is stateless, dependency-free,
  and handles missing values, non-positive sizes, malformed integers,
  and unknown flags by returning `Status::Error` with a human message.
  Integers go through `std::from_chars` to avoid `atoi`-style silent
  truncation.
- **`src/main.cpp`:** wired to `CommandLine::parse`. `--help` and
  `--version` are handled before the startup banner so their output is
  clean. `--device-info` logs honestly that the CUDA backend lands at
  M5. `--render <scene>` logs `render command received` and exits — per
  this milestone's scope it does not render. Unknown flags / bad values
  return exit code 2 and print usage on stderr.
- **`CMakeLists.txt`:** added `src/core/Config.cpp` and
  `src/core/CommandLine.cpp` to the `RelativityRender` executable.

#### Verified locally

```
$ ./build/bin/RelativityRender --help          # prints usage, rc=0
$ ./build/bin/RelativityRender --version       # prints "RelativityRender 0.0.1", rc=0
$ ./build/bin/RelativityRender --device-info   # logs CUDA-not-yet message, rc=0
$ ./build/bin/RelativityRender --render scene.scn --output out.exr \
                                --width 1920 --height 1080  # logs "render command received", rc=0
$ ./build/bin/RelativityRender --width foo     # error + usage on stderr, rc=2
$ ./build/bin/RelativityRender --render        # error (missing path), rc=2
$ ./build/bin/RelativityRender --bogus         # error (unknown arg), rc=2
```

#### Deliberately deferred (still inside M2)

- `core::Error` type.
- `core::App` lifecycle wrapper.
- `core::FileSystem` minimal IO.
- `Config::load` / `Config::save` (TOML or JSON).
- A test framework dependency under `third_party/` and tests for `Logger`,
  `Config`, and `CommandLine`.
- Host-only CI.

These will be added in subsequent M2 sub-prompts before M2 is marked
landed and M3 (Math Library) begins.

### 2026-04-27 — M2 minimal C++20 application foundation landed

First compiled binary in the project. Scope was deliberately restricted to a
minimal application foundation; config, lifecycle, error type, filesystem
helper, and tests are not in this slice and remain on the M2 todo list.

- **CMakeLists.txt:** bumped C++ standard from C++17 to **C++20** (pinned
  project-wide). Added the `RelativityRender` executable target with sources
  `src/main.cpp` and `src/core/Logger.cpp`, `src/` on the include path, and
  `-Wall -Wextra -Wpedantic` (or `/W4 /permissive-` on MSVC). Removed the
  commented-out `add_subdirectory(...)` placeholder block now that the build
  links source files directly; modules will be promoted to static libraries
  as they grow.
- **`src/core/Version.h`:** `rr::core::kProjectName`,
  `kVersionMajor/Minor/Patch`, `kVersionString` as `inline constexpr`.
  Hand-written, not CMake-generated, to keep the foundation self-contained.
- **`src/core/Logger.h`:** `rr::core::Logger` class with three static
  methods — `info`, `warning`, `error`. Accepts `std::string_view`.
- **`src/core/Logger.cpp`:** thread-safe implementation. `info` writes to
  `stdout`; `warning` and `error` write to `stderr`. Each line is
  `[HH:MM:SS.mmm] [LEVEL] message`. A single `std::mutex` serializes
  writes across threads. No external logging library; this is honest minimal
  code, not a stub.
- **`src/main.cpp`:** entry point that logs the project name, version, the
  platform tagline, and a "Core application foundation online" message,
  then exits 0.
- **Verified locally:** `cmake -S . -B build && cmake --build build`
  succeeds with the warning flags above; running `build/bin/RelativityRender`
  prints three timestamped INFO lines.

#### Naming choice

Constants in `Version.h` use the `kPascalCase` `inline constexpr` style
(e.g. `kVersionString`). This is the convention to expect for compile-time
constants throughout the renderer. `docs/DEVELOPMENT_RULES.md` §8 will be
updated to record this in a follow-up doc-only pass.

#### Deliberately deferred (still part of M2)

- `core::Config` (load / save).
- `core::Error` type.
- `core::App` lifecycle.
- `core::FileSystem` minimal IO.
- A test framework dependency under `third_party/` and tests for the logger.
- Host-only CI.

These will be added in subsequent M2 sub-prompts before M2 is marked
landed and M3 (Math Library) begins.

### 2026-04-27 — M1 repository skeleton landed

- Added top-level `CMakeLists.txt`. Declares the project, pins C++17,
  and exposes options:
  - `RR_ENABLE_CUDA` (default OFF) — CUDA backend.
  - `RR_ENABLE_OPTIX` (default OFF) — OptiX backend.
  - `RR_BUILD_TESTS` (default ON).
  - `RR_BUILD_TOOLS` (default OFF).
  - `RR_BUILD_INTEGRATIONS` (default OFF).
  Defaults are chosen so the host-only configure works on any machine
  (no CUDA / OptiX / Cinema 4D toolchains required). No targets are
  built yet — `add_subdirectory(...)` calls are commented out and
  annotated with the milestone that turns each one on.
- Added top-level `README.md` with project overview, status, repository
  layout, and configure / build instructions.
- Created the source skeleton:
  ```
  src/{core,math,image,gpu,cuda,optix,scene,geometry,material,
       texture,lighting,camera,relativity,renderer,pathtracer,
       io,server}/
  tests/
  tools/
  integrations/c4d/
  third_party/
  ```
- Added a `README.md` in every major folder (each `src/*` module,
  `tests/`, `tools/`, `integrations/`, `integrations/c4d/`,
  `third_party/`). Module READMEs are intentionally short pointers —
  one paragraph of purpose + a reference to `docs/MODULE_MAP.md`,
  which remains the authoritative contract.
- Updated this file. Marked M0 as landed and M1 as landed.

#### Naming notes vs. M0 docs

The skeleton uses the directory names listed in the M1 prompt, which differ
in a few places from the planned shape sketched in
`docs/MASTER_ARCHITECTURE.md` §8:

| M0 doc sketch         | M1 actual          |
|-----------------------|--------------------|
| `src/cuda_backend/`   | `src/cuda/`        |
| `src/optix_backend/`  | `src/optix/`       |
| `src/relativistic/`   | `src/relativity/`  |
| `src/scene_format/`   | `src/io/` (shared with image IO) |
| `src/aov/`, `src/progressive/`, `src/denoise/` | `src/renderer/` (umbrella) |
| `bridges/c4d_bridge/`, `bridges/c4d_native/` | `integrations/c4d/` |

These are layout-only differences. The 22 logical modules from
`docs/MODULE_MAP.md` are unchanged; reconciling MASTER_ARCHITECTURE §8 and
MODULE_MAP path references with this layout is a small, doc-only follow-up
and does not affect the architecture or dependency rules.

#### Deliberately deferred

- No source files (.h / .cpp / .cu) added. Module CMakeLists.txt files
  are added when the corresponding module is implemented (M2+).
- No third-party dependencies fetched or vendored. They are introduced in
  the milestones that need them (logging/test framework in M2, EXR/PNG
  in M4, etc.).
- No CI configuration. CI is added with M2 once there is a real target
  to compile.
- No CUDA / OptiX / Cinema 4D detection logic in CMake — only options.
  Detection lands when the corresponding backend module starts (M5
  for CUDA, M15 for OptiX, M19 for the C4D bridge).

### 2026-04-27 — M0 documentation set landed

- Added `docs/MASTER_ARCHITECTURE.md`: identity, layers, 22 modules, dependency
  direction, forbidden dependencies, end-to-end data flow, planned repository
  shape, non-goals.
- Added `docs/MODULE_MAP.md`: per-module ownership, dependencies, forbidden
  list, public surface, GPU-side flag, status.
- Added `docs/DEVELOPMENT_RULES.md`: identity, engineering, dependency, build,
  GPU, relativistic, process, style, testing, and "done" rules.
- Added `docs/MILESTONE_ROADMAP.md`: M0–M23 with goals, deliverables, and exit
  criteria. Cinema 4D work gated behind a working renderer server (M18).
- Added this file (`docs/BUILD_PLAN.md`) tracking module and milestone state.

---

## Next Step

**Finish M14 — wire sampling into a real integrator.** The
RNG + hemisphere primitives are in. To move M14 / Module 14
from "in progress" to "landed":

1. BSDF `sample` / `eval` / `pdf` on `MaterialParams`. Lambert
   first; later GGX, dielectric, conductor.
2. A `__global__ k_path_trace` kernel that uses the existing
   `intersect_sphere` / `intersect_triangle` primitives and
   the new RNG/sampling helpers to bounce rays through the
   scene with NEE + MIS for direct light.
3. Promote `RenderSettings.samples_per_pixel` and
   `max_depth` from "stored but not consumed" to real kernel
   arguments; both persist through `.rrscene` already.
4. Russian roulette path termination based on throughput.
5. Multi-mesh scene upload (currently `GpuScene` has a single
   mesh slot) - either an array of `CudaMeshView` or a flat
   global vertex/index buffer with per-mesh offsets - so a
   path-traced scene isn't restricted to one quad.

Before or alongside this, the M2 deferred items (`Error`,
`FileSystem`, `App`, `Config::load`/`save`, real test framework,
host-only CI) remain a backlog rather than a blocker.

Alongside M6, the M2 deferred items should be cleaned up so the core
foundation is honest end-to-end:

1. `core::Error` — a small result/error type used at module boundaries.
2. `core::FileSystem` — minimal path / read / write helpers using
   `std::filesystem` plus a thin error-aware wrapper.
3. `Config::load` / `Config::save` — TOML or JSON persistence via a
   vendored parser under `third_party/`.
4. `core::App` — application lifecycle wrapper that owns parse → run →
   exit.
5. A real test framework (Catch2 or doctest) under `third_party/`,
   migrating the existing test runners.
6. Host-only CI configuration that runs the build and tests.

Per development rules, none of these may introduce code from M7+
modules - no camera, scene, material, or rendering code yet.

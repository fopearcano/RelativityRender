# Next Steps

Date: 2026-04-29
Branch: `relativity-core-v1`
Companion to `docs/STAGE_1_5_AUDIT.md`.

This is the ordered, safe implementation queue after the Stage 1–5
audit. Each step is a self-contained slice with a clear deliverable,
in master-instructions module order, with no future systems pulled
forward. Steps do not bundle work.

---

## Step 0 — GPU runtime verification (no code)

**Goal:** close audit item H1 by actually running the four GPU
actions on a CUDA host.

This step writes **zero source code**. It exercises code that is
already in the tree.

Run on a host with the CUDA Toolkit and a CUDA-capable GPU:

```sh
cmake -S . -B build-cuda -DRR_ENABLE_CUDA=ON
cmake --build build-cuda -j
ctest --test-dir build-cuda --output-on-failure
build-cuda/bin/RelativityRender --device-info
build-cuda/bin/RelativityRender --render-gradient
build-cuda/bin/RelativityRender --render-rays
build-cuda/bin/RelativityRender --render-sphere
build-cuda/bin/RelativityRender --render-relativistic
```

Acceptance:

- ctest still 3/3.
- `--device-info` prints `GPU backend: CUDA` and at least one
  device line.
- The five GPU PPMs land in `output/` with non-zero size and a
  `P6` header.
- Visual sanity:
    - `gpu_gradient.ppm` — UV gradient (R=u, G=v, B=0).
    - `gpu_camera_rays.ppm` — direction-encoded RGB; centre olive,
      edges shifted.
    - `gpu_sphere.ppm` — normal-shaded sphere on a sky gradient.
    - `sphere_beta_000.ppm` — identical to `gpu_sphere.ppm`.
    - `sphere_beta_{025,075,095}.ppm` — increasing blueshift +
      forward aberration + searchlight brightening.

If any of those visual checks fails, fix in place before moving to
Step 1. The next steps depend on this baseline working.

**Outcome:** the audit's item H1 is closed. The rest of the queue is
unblocked.

---

## Step 1 — GPU scene upload (master module 11)

**Goal:** allow the renderer to operate on a multi-sphere scene
uploaded once into device memory, instead of a single hard-coded
sphere baked into a launch argument.

**Why this is next:** every later module (mesh system, materials,
lights, scene format, path tracer) consumes a scene container. The
renderer cannot grow further until it has one.

Deliverables (this slice only):

- `src/scene/Scene.{h,cpp}` — host-side container: `Camera`,
  `Observer`, `RelativityParams`, `std::vector<Sphere>`. No materials
  / lights / meshes yet.
- `src/gpu/GpuScene.{h,cpp}` — move-only RAII upload manager that
  owns one `GpuBuffer<Sphere>` plus by-value PODs (camera, observer,
  params).
- `src/cuda/CudaScene.cuh` — device-side `CudaSceneView` POD: raw
  device pointers + counts + the per-frame PODs. Passed to the
  kernel by value.
- A new kernel `k_render_scene` in `CudaTestKernel.cu` that runs the
  Stage-10 relativistic pipeline but loops `intersect_sphere` over
  the uploaded sphere array (closest-hit). One thread per pixel.
- `launch_render_scene` in `CudaKernels.cuh` /
  `CudaTestKernel.cu`.
- `CudaRenderer::render_scene(const GpuScene&, w, h)`.
- `--render-scene` CLI action that builds a hard-coded multi-sphere
  scene in main and renders it to `output/gpu_scene.ppm`. The actual
  scene file format / loader is module 15, **not** this step.
- Tests: extend `gpu_tests.cpp` with a host-only check that
  `GpuScene::upload_spheres` round-trips correctly when CUDA is on
  and refuses honestly when it is not.

Hard rules carried forward:

- All per-ray / per-pixel work on GPU.
- CPU only constructs the scene PODs, uploads, launches, syncs,
  downloads, saves.
- No materials, no lights, no meshes, no scene parser.

**Out of scope for Step 1:** mesh primitives, material system,
light sampling, scene format / parser, path tracer, AOVs, server,
C4D.

---

## Step 2 — Mesh system (master module 12)

**Goal:** a single triangle mesh on the device alongside the sphere
array, with closest-hit competition between the two primitive types.

Deliverables:

- `src/geometry/Triangle.h` — RR_HD POD; vertex indices into a
  parallel `Vertex` array.
- `src/geometry/Mesh.{h,cpp}` — host-side `Vertex` + `Triangle`
  arrays plus a transform.
- Restore `intersect_triangle` in `cuda/CudaIntersection.cuh`
  (Möller-Trumbore — was deferred from Stage 8, audited clean in
  the prototype).
- `src/cuda/CudaMesh.cuh` — device-side mesh view POD.
- `src/gpu/GpuMesh.{h,cpp}` — move-only RAII upload of vertices +
  triangles.
- Extend `Scene` / `GpuScene` / `CudaSceneView` with one mesh slot.
- Extend `k_render_scene` to run a triangle closest-hit loop after
  the sphere loop, taking `t_max` from the running best.
- `tests/mesh_tests.cpp` (host-side `intersect_triangle` checks).
- A `--render-mesh` CLI action or a flag to load a built-in cube /
  tetrahedron into the scene. **Still no scene file format.**

**Out of scope:** instancing, BVH, animation, transforms more than a
single per-mesh model matrix.

---

## Step 3 — Materials (master module 13)

**Goal:** a tiny material system the kernel can read at hit time.
Diffuse-only, constant colour. Texture access is module 18.

Deliverables:

- `src/material/MaterialTypes.h` — RR_HD POD `MaterialParams`:
  `albedo`, `emission`. No transmission yet.
- `src/material/Material.{h,cpp}` — host-side wrapper that returns
  `MaterialParams`.
- Extend `Scene` with `std::vector<MaterialParams>` and the kernel
  to read `Hit::material_index` (already populated by
  `intersect_sphere`) into a colour. Back to no normal-as-color
  diagnostic — the renderer now produces real shading.
- `tests/material_tests.cpp`.

The `Sphere::material_index` field already exists (defaulted to
`-1` since Stage 8) — Step 3 finally consumes it.

**Out of scope:** BRDFs beyond Lambertian, textures, material node
graph (the prototype's data-core + GpuMaterial work is in
`PROTOTYPE_REUSE_AUDIT.md` for module 23).

---

## Step 4 — Lights (master module 14)

**Goal:** at least one direct-light evaluation on the device — point
or directional, no area sampling yet.

Deliverables:

- `src/lighting/Light.{h,cpp}` — RR_HD POD union of point /
  directional. The prototype's `Area` / `Environment` flags stay
  deferred (placeholders).
- Extend `Scene` / `GpuScene` / `CudaSceneView` with a small
  `std::vector<Light>`.
- The kernel evaluates direct lighting at each hit (visibility test
  via a shadow ray reusing `intersect_sphere` /
  `intersect_triangle`).
- `tests/lighting_tests.cpp`.

**Out of scope:** environment lighting, area-light sampling, light
tree, multiple-importance sampling.

---

## Step 5 — Scene format / parser (master module 15)

**Goal:** load a tiny `.rrscene` JSON file. The Step-1 hard-coded
scene becomes a fixture file under `scenes/`.

Deliverables:

- `src/io/SceneLoader.{h,cpp}` — populates `rr::scene::Scene` from a
  JSON file. The prototype's hand-rolled parser was the project's
  biggest single piece of structural debt; the rewrite uses a real
  JSON library (vendored `nlohmann/json` single-header).
- `src/io/SceneWriter.{h,cpp}`.
- `scenes/test_minimal.rrscene` — one sphere, default camera.
- `scenes/test_relativistic.rrscene` — one sphere, observer with
  β=0.75 along forward.
- `--render <scene>` finally **does something**: loads the scene,
  hands it to `CudaRenderer::render_scene`, saves to `--output`.
- `tests/io_tests.cpp` — round-trip: write → read → compare.

**Out of scope:** the full v1 scene spec from the prototype's
`docs/RRSCENE_FORMAT.md` (most fields wait on materials being more
than albedo + emission).

---

## After Step 5

Past Step 5 the project has parity with the master order's
modules 3–15. The next phase is module 16 (path tracer foundation),
which is a substantial piece of work and gets its own `NEXT_STEPS.md`
revision once Steps 1–5 have landed.

The deferred placeholders (`Framebuffer` reach, `applyDopplerColor`
artistic mix, the inert `material_index` field on `Hit`) are
revisited in their respective master-order modules:

- `Framebuffer` — module 19 (AOVs / render passes).
- `applyDopplerColor` spectral upgrade — module 18 (texture system)
  or its successor.
- `Hit::material_index` consumption — Step 3 above (master
  module 13).

---

## Constraints carried forward to every step below

(From `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`, repeated
because they are easy to forget mid-sprint.)

- Build incrementally. Keep every step compilable.
- No fake stubs. If a system is added, it has to do real work.
- No CPU per-pixel or per-ray work as a production path. The single
  allowed exception is image-saving IO (`Image::save_ppm`).
- Core modules never depend on Cinema 4D, UI, node editor, or any
  DCC.
- Update `docs/BUILD_PLAN.md` after every implementation step.

---

## What this list deliberately does NOT include

These are **not** the next safe steps; they are stages further down
the master order and would violate "do not overbuild a later system
before the current layer works" if pulled forward:

- Path tracer (module 16).
- OptiX backend (module 17).
- Texture system (module 18).
- AOVs / render passes (module 19).
- Renderer server (module 20).
- C4D bridge (module 21).
- UI (module 22).
- Material node graph (module 23).
- Denoising (module 24).
- Native C4D renderer (module 25).

Each of those has a plan in `prototype_v0`'s docs (preserved as
reference) and will be picked up in order, after Steps 1–5 land.

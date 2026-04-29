# Stage 6–9 Audit B1 — CPU violation scan

Date: 2026-04-29
Branch: `relativity-core-v1`
Scope: **forbidden CPU rendering logic only**, restricted to the
five host-side locations the prompt named:

- `src/renderer/`
- `src/image/`
- `src/main.cpp`
- `src/scene/`
- `src/geometry/`

CUDA files (`src/cuda/`) are **out of scope** per the prompt.
Other host-side modules (`relativity/`, `lighting/`, `material/`,
`gpu/`, `camera/`, `math/`, `core/`) are also out of scope.

This doc is documentation. No source files are modified.

The four classes the prompt called out:

1. CPU loops over pixels.
2. CPU loops over rays.
3. CPU intersection tests used for rendering.
4. CPU shading used for rendering.

---

## Findings

Every loop / function with non-trivial pixel or ray semantics in
the five inspected locations is recorded below, with a verdict.

### `src/renderer/Hit.h`

Plain POD `struct Hit` plus an `RR_HD inline make_miss()` factory.
No loops, no rendering logic.

| File | Function / line | What it does | Violation? |
|---|---|---|---|
| `src/renderer/Hit.h` | (data only) | POD intersection result + `make_miss()` factory. No host code reads or writes pixels. | **No** |

### `src/image/Color.h`

POD `Rgb` and `Rgba` structs. No code that loops or shades.

| File | Function / line | What it does | Violation? |
|---|---|---|---|
| `src/image/Color.h` | (data only) | `Rgb` / `Rgba` PODs with constructors. | **No** |

### `src/image/Image.h` and `Image.cpp`

| File | Function / line | What it does | Violation? |
|---|---|---|---|
| `src/image/Image.cpp` | `Image::Image(int, int, PixelFormat)` (line 20) | `data_.assign(... , 0.0f)` — zero-fills the host buffer at construction. Not a rendered output; a buffer-init. | **No** |
| `src/image/Image.cpp` | `Image::set_pixel(x, y, color)` (line 31) | Writes one pixel to `data_[idx ..]`. Not a loop. Per `Image.h`'s docstring, intended for clearing / debug fills / IO validation; never called from any renderer path inspected here. | **No** |
| `src/image/Image.cpp` | `Image::get_pixel(x, y)` (line 46) | Reads one pixel. Not a loop. | **No** |
| `src/image/Image.cpp` | `Image::clear(color)` (line 60) | `for (i = 0; i < data_.size(); i += ch)` writes the same constant colour to every channel. Per-element on the buffer, but the value is constant — **not** a render result. Master rules permit "clearing / debug fills". | **No** |
| `src/image/Image.cpp` | `Image::resize(w, h)` (line 72) | `data_.assign(... , 0.0f)` — zero-fills after resizing. | **No** |
| `src/image/Image.cpp` | `Image::save_ppm(path)` (lines 80–102) | **Nested `for (y) for (x)`** reading `data_` and writing 8-bit RGB triples to `out`. Iterates every pixel on the host. | **No** — explicitly the master rules' allowed exception ("save image files"). The values are *already-rendered* floats produced by a GPU kernel; this loop only quantises and writes them out. |

### `src/image/Framebuffer.h` and `Framebuffer.cpp`

| File | Function / line | What it does | Violation? |
|---|---|---|---|
| `src/image/Framebuffer.cpp` | `Framebuffer::Framebuffer(int, int, PixelFormat)` | Constructs an `Image`. No loop. | **No** |
| `src/image/Framebuffer.cpp` | `Framebuffer::resize` / `clear` / `save_ppm` | One-line forwards to the contained `Image`. The actual loops live inside `Image::*` (covered above). | **No** |

### `src/main.cpp`

`main.cpp` is host orchestration: parse CLI, build scenes, call
`GpuScene::upload_*`, dispatch to `CudaRenderer::render_*`, save
PPMs. Below is every `for` / `while` loop in the file.

| File | Function / line | What it does | Violation? |
|---|---|---|---|
| `src/main.cpp` | `report_device_info`, line 58 (`for (const auto& d : devices)`) | Iterates the (small) list of CUDA devices to print one line each. | **No** — orchestration / device enumeration. |
| `src/main.cpp` | `run_render_relativistic`, line 325 (`for (const auto& run : kRuns)`) | Iterates the four-element β sweep `{0.00, 0.25, 0.75, 0.95}`. Each iteration constructs an `Observer` POD and calls `CudaRenderer::render_relativistic_sphere`. | **No** — orchestration: 4 GPU-launch dispatches. |
| `src/main.cpp` | `run_render_scene`, line 379 (`for (const auto& s : scene.spheres)`) | Pulls `rr::geometry::Sphere` PODs out of `SceneSphere` wrappers (filtering invisible) into a flat `std::vector<Sphere>` for `GpuScene::upload_spheres`. | **No** — data extraction for GPU upload. |
| `src/main.cpp` | `run_render_mesh_scene`, line 494 (same pattern) | Same as above; extracts spheres for upload. | **No** |
| `src/main.cpp` | `run_render_material_scene`, lines 619 + 627 | Extracts spheres + materials into flat arrays for upload. | **No** |
| `src/main.cpp` | `run_render_direct_lighting`, lines 770 / 775 / 778 | Extracts spheres + materials + lights into flat arrays for upload. | **No** |

None of `main.cpp`'s loops iterate over pixels or rays. They iterate
over (a) devices for printing, (b) the four-β sweep, or (c) scene
container vectors to flatten authoring wrappers into device-ready
PODs. All of these are explicitly permitted by the master rules
("orchestrate execution", "upload data to GPU", "launch CUDA / OptiX
kernels").

### `src/scene/`

| File | Function / line | What it does | Violation? |
|---|---|---|---|
| `src/scene/Transform.h` | (alias only) | `using Transform = rr::math::Transform`. No loops. | **No** |
| `src/scene/RenderSettings.h` | (POD only) | Width / height / SPP / depth fields. No loops. | **No** |
| `src/scene/SceneObject.h` | (POD only) | Name / transform / visible. No loops. | **No** |
| `src/scene/Scene.h` | (POD only) | The container: `Camera`, `RenderSettings`, `Observer`, `RelativityParams`, four `std::vector<Scene*>`. No loops. | **No** |
| `src/scene/Scene.cpp` | `Scene::clear()` | Calls `.clear()` on each list and resets the four scalar fields to default-constructed state. No loops in user code (the `vector::clear()` that the standard library runs is not rendering). | **No** |

### `src/geometry/`

| File | Function / line | What it does | Violation? |
|---|---|---|---|
| `src/geometry/Sphere.h` | (POD + `make_sphere`) | `RR_HD` factory; no loops. | **No** |
| `src/geometry/Triangle.h` | (POD + `make_triangle`) | `RR_HD` factory; no loops. | **No** |
| `src/geometry/Mesh.h` | (POD + small inline accessors) | `vertex_count`, `triangle_count`, `empty`, no loops. | **No** |
| `src/geometry/Mesh.cpp` | `Mesh::clear()` | Calls `vector::clear()` on `vertices` / `triangles` and resets metadata. No user loops. | **No** |
| `src/geometry/Mesh.cpp` | `Mesh::reserve(...)` | `vector::reserve` calls. No loops. | **No** |
| `src/geometry/Mesh.cpp` | `Mesh::local_bounds()` (line 31, `for (std::size_t i = 1; i < vertices.size(); ++i)`) | **Single linear pass over the vertex positions** to compute the local-space AABB. CPU per-vertex work — **not** per-ray, **not** per-pixel. Mesh-authoring / preprocessing helper. | **No** — preprocessing of authoring data, not rendering. |

---

## Cross-cutting checks

### CPU loops over pixels

Two host-side loops touch every pixel on the CPU in the inspected
files:

1. `Image::clear(color)` — fills with a **constant** colour. Allowed
   (clearing / debug fill).
2. `Image::save_ppm(path)` — quantises an already-rendered float
   buffer and writes 8-bit triples to disk. Allowed (the master
   rules' "save image files" exception).

Neither produces a *rendered* pixel. **No violation.**

### CPU loops over rays

**None** in the inspected files. No host-side code constructs,
intersects, or otherwise iterates over rays. The host's only
ray-related touches are:

- Constructing `Camera` PODs (`Camera::look_at`, `set_*`) — single
  function calls, not per-ray loops.
- Snapshotting a `Camera` into a `GpuCamera` POD via
  `Camera::to_gpu()` for a launch argument — no ray-gen on host.

### CPU intersection tests used for rendering

**None** in the inspected files. The two intersection routines
(`intersect_sphere`, `intersect_triangle`) live in
`src/cuda/CudaIntersection.cuh` (explicitly out of scope) and are
`RR_HD inline`, so they are *technically* host-callable; but no
host code in the five inspected locations calls them.

### CPU shading used for rendering

**None** in the inspected files. The renderer's shading pipeline
(material lookup, light evaluation, Doppler / searchlight
modulation) is implemented entirely in `src/cuda/CudaTestKernel.cu`
(out of scope). No host code in the five inspected locations
computes pixel colours from materials / lights / rays.

---

## Verdict

**Zero violations** of the "no CPU per-pixel rendering / no CPU
per-ray rendering / no CPU intersection or shading for rendering"
rules in the five locations the prompt named.

The two CPU per-pixel loops in `src/image/Image.cpp` are both
master-rules-allowed (clearing for `Image::clear`, saving image
files for `Image::save_ppm`). They write either a constant colour
(no rendering happens) or an already-rendered float framebuffer to
disk (the rendering already happened on the GPU; the host only
quantises + writes bytes).

`main.cpp` contains nine `for` loops, none of which iterate over
pixels or rays. They iterate over devices, the four-β sweep, or
scene-container vectors — orchestration that the master rules
explicitly permit.

`Mesh::local_bounds` does a single per-vertex CPU pass to compute
an axis-aligned bounding box. It is preprocessing of authoring
data (no rays, no pixels involved) and is not part of the renderer
path.

Audit B1 finds the host-side surface compliant with the GPU-first
rule. The rendering-correctness questions (kernel sanity, lighting
math, Doppler correctness, etc.) are out of scope for B1 and
belong to subsequent audit slices.

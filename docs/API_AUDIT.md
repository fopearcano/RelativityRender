# Prototype Reuse Audit — Step 8: Naming and API

Date: 2026-04-29
Branch: `claude/create-docs-architecture-T2Dp5`
Scope: naming consistency, module naming alignment, API cleanliness, future
compatibility with the serious-rewrite architecture.
Sources: `src/` headers, `docs/MASTER_ARCHITECTURE.md`,
`docs/MODULE_MAP.md`, `docs/PROTOTYPE_FILE_INDEX.md`,
`docs/ARCHITECTURE_AUDIT.md`.

This step looks at the *shape* of the public surface (file names, namespaces,
type names, function names, return conventions, ABI hygiene) — not at what the
code does. No code is modified.

---

## 1. Naming consistency

### Files: PascalCase (consistent)

All headers and source files are PascalCase: `GpuBuffer.h`, `CudaRenderer.h`,
`OptixBackend.h`, `RenderServer.h`, `MaterialGraph.h`, `RelativityParams.h`,
`SceneLoader.cpp`, etc. CUDA-only headers use `.cuh`, CUDA sources `.cu`. No
mixed casing, no snake_case file names. **Clean.**

### Types: PascalCase (consistent)

Classes and structs are PascalCase across the tree: `GpuBuffer<T>`,
`CudaRenderer`, `OptixBackend`, `Image`, `Camera`, `Mesh`, `Sphere`,
`Triangle`, `Vertex`, `Vec3/Vec4/Mat4`, `Transform`, `Light`, `Texture`,
`RenderServer`, `Graph`, `Node`, `Socket`, `GpuMaterial`, `MaterialParams`,
`RelativityParams`, `Observer`, `AOV`. **Clean.**

### Functions and members: snake_case (consistent)

Member functions and free functions use snake_case: `render_scene`,
`render_pathtrace`, `render_aovs`, `upload_camera`, `upload_relativity`,
`set_image`, `set_position`, `vertical_fov_degrees`, `last_error`,
`is_initialized`, `save_ppm`, `gpu_alloc`, `gpu_free`,
`optix_backend_available`, `optix_backend_status_line`. **Clean** — no Java/Qt
camelCase leaks, no `getX()` getters mixed with `x()` accessors.

### Members: trailing-underscore (consistent)

Private data is `name_`: `ptr_`, `count_`, `width_`, `height_`,
`is_initialized_`, `last_error_`, `wrap_u_`, `vfov_deg_`, `position_`. **Clean.**

### Constants: kPascalCase (consistent)

`kInvalidNodeId`, `kNodeTypeCount`, `kAOVCount`. **Clean.**

### Macros: `RR_*` prefix (consistent)

`RR_HD`, `RR_HAS_CUDA`, `RR_HAS_OPTIX`, plus the CMake-side
`RELATIVITYRENDER_ENABLE_*` cache variables. No bare `HAS_CUDA` or `ENABLE_*`
leakage. **Clean.**

### One real inconsistency: legacy graph naming

In `src/material/MaterialGraph.h` the host-side authoring node is `GraphNode`,
while in `src/material/graph/Node.h` the new core uses `Node` directly inside
`rr::material::graph::`. Two structs with related semantics, two different
names. Already covered: legacy `MaterialGraph.{h,cpp}` is **REWRITE** per step
2; the rewrite resolves this by deleting `GraphNode` and keeping
`graph::Node`.

---

## 2. Module naming alignment

### `src/` directory layout vs library names vs namespaces

| Directory | CMake target | Namespace |
|---|---|---|
| `core/` | `rr_core` | `rr::core` |
| `math/` | `rr_math` | `rr::math` |
| `image/` | `rr_image` | `rr::image` |
| `geometry/` | `rr_geometry` | `rr::geometry` |
| `camera/` | `rr_camera` | `rr::camera` |
| `lighting/` | `rr_lighting` | `rr::lighting` |
| `texture/` | `rr_texture` | `rr::texture` |
| `material/` | `rr_material` | `rr::material`, `rr::material::graph` |
| `relativity/` | `rr_relativity` | `rr::relativity` |
| `pathtracer/` | `rr_pathtracer` | `rr::pathtracer` |
| `gpu/` | `rr_gpu` | `rr::gpu`, `rr::gpu::detail` |
| `cuda/` | `rr_cuda` | `rr::cuda` |
| `optix/` | `rr_optix` | `rr::optix` |
| `renderer/` | `rr_renderer` | `rr::renderer` |
| `scene/` | `rr_scene` | `rr::scene` |
| `io/` | `rr_io` | `rr::io` |
| `server/` | `rr_server` | `rr::server` |

The trio is one-to-one across the entire codebase: directory name = library
name (with the `rr_` prefix) = namespace tail. There is no module that lives
under one name and is exposed under another. **Clean.**

### Where module *contents* don't match module *name*

Already flagged by `ARCHITECTURE_AUDIT.md`, but visible at the API level too:

- `renderer/` exposes only `AOV` and `Hit`. The actual integrator and the
  ray-gen / closest-hit code live in `cuda/CudaTestKernel.cu`. The module name
  promises a renderer; the public surface is two POD headers.
- `pathtracer/` exposes only `RNG` and `Sampling` headers. The path tracer
  itself is, again, in `cuda/CudaTestKernel.cu`.
- `cuda/` therefore carries the full integrator under a name that should mean
  "CUDA backend support" (memory, contexts, low-level kernels), not "where the
  renderer lives".

This is a *content* problem, not a *naming* problem — but it shows up as a
naming-vs-reality gap that the rewrite must close: keep the names, move the
code.

---

## 3. API cleanliness

### Return-value conventions

- **Boolean + last_error string** for stateful objects:
  `OptixBackend::init() -> bool` + `last_error()`,
  `GpuScene::upload_*` -> `bool`.
- **Boolean + outparam image** via a `Result` struct for one-shot renders:
  `CudaRenderer::Result { ok, image, message }`.
- **Boolean only** for trivially-byte-shuffling primitives:
  `gpu_copy_host_to_device(...) -> bool`.
- **Dedicated result structs** for compound operations: `LoadResult`,
  `WriteResult`, `ParseResult`, `CompileResult`, `ValidationResult`,
  `RunResult`, `AOVResult`, `GpuMaterialResult`, `CommandResult`.

Two patterns coexist — `(bool, last_error)` for long-lived objects,
`Result{ok,…,message}` for one-shot calls. They serve different purposes and
the split is consistent. **Acceptable.**

### `[[nodiscard]]` discipline

Used on `GpuBuffer::allocate / upload / download`, `gpu_alloc / gpu_copy_*`,
`OptixBackend::init`, `optix_backend_available`,
`optix_backend_status_line`, `last_error`, `is_initialized`, `empty`, `size`,
`size_in_bytes`, `device_ptr`, `wants_render`. **Clean** — every "must
inspect" return is annotated.

### Move-only / RAII

`GpuBuffer<T>` and `OptixBackend` are explicitly move-only with deleted copy
ops; `GpuBuffer` has a `noexcept` move and a `noexcept` `reset()`. **Clean.**

### `noexcept` discipline

`reset()`, move constructors, move assignments, accessors, and the
`gpu_free` byte-level free are all `noexcept`. Functions that can fail at the
device level (`init`, `allocate`, `upload`, `download`) are not. **Clean.**

### Header hygiene

- `CudaRenderer.h` is host-only; the implementation lives in `CudaRenderer.cu`
  and the `RR_HAS_CUDA` macro gates use. CUDA-Runtime types do not leak into
  host headers.
- `OptixBackend.h` is host-only; OptiX types are stored as `void*` and the
  comment block explicitly documents the on/off behaviour. **Clean.**
- `.cuh` is reserved for headers that need device code (e.g.
  `CudaMaterialGraph.cuh`, `RelativityMath.cuh`, `Sampling.cuh`). **Clean.**

### Forward declarations

Used where they belong: `namespace rr::gpu { class GpuScene; }` in
`CudaRenderer.h`, `namespace rr::geometry { struct Mesh; }`,
`namespace rr::material::graph { struct Graph; }`. No "include the world"
headers. **Clean.**

### Unit-bearing names

Camera API names units explicitly: `vertical_fov_degrees()`,
`vertical_fov_radians()`, `near_plane()`, `far_plane()`. RelativityParams /
Observer use `beta` and `max_beta` with documented "|beta| < 1" semantics.
**Clean.**

### One small blemish

`CudaRenderer` is a class with **only static methods** plus an inner `Result`
type. That is really a free-function set in a class wrapper. It will not break
anything, but the rewrite should pick one shape: either turn the static
methods into free functions in `rr::cuda::renderer::`, or give the renderer
real instance state (device id, stream, persistent allocations). The current
form was right for the prototype's per-call lifecycle; it is wrong for a
serious renderer that wants to keep a CUDA stream alive across launches.

---

## 4. Future compatibility with the serious architecture

### What survives unchanged

- **Naming conventions** (PascalCase types, snake_case functions,
  trailing-underscore members, kPascalCase constants, `RR_*` macros). No churn
  needed.
- **Module → library → namespace mapping** (`foo/`, `rr_foo`, `rr::foo`).
  Already aligned with what `MASTER_ARCHITECTURE.md` and `MODULE_MAP.md`
  describe for the rewrite.
- **`RR_HD` shared-host+device pattern** in `math/`, `relativity/`,
  `pathtracer/`, and `material/graph/`. The exact thing the rewrite wants —
  it makes host tests verify device behaviour by construction.
- **Result-type pattern** (`Result{ok, payload, message}`) for one-shot calls
  and **`bool` + `last_error()`** for long-lived backends. Both translate
  directly into the rewrite without rename.
- **`GpuBuffer<T>` API** (`allocate`/`upload`/`download`/`reset`/`size`/
  `device_ptr`). This is the right shape for the rewrite to keep; only the
  internals (streams, pinned host memory, error context recovery) change.
- **`[[nodiscard]]`, move-only, `noexcept`** discipline. Already at the level
  the rewrite needs.
- **View-POD pattern** (`CudaSceneView`, `CudaMaterialGraphView`,
  `TextureView`, `CudaAOVPack`). Names are consistent (`*View` for
  device-readable POD slices) and the pattern is portable to OptiX SBT
  records.

### What needs to change in the rewrite (naming/API only)

1. **`CudaRenderer`'s static-only class shape.** Convert to either free
   functions in `rr::cuda::renderer::` or a real object that owns device,
   stream, and persistent buffers. Pick before the integrator moves out of
   `CudaTestKernel.cu`.
2. **`renderer/` and `pathtracer/` empty surfaces.** These names need to
   match contents — move the integrator code into them so the public API of
   `rr_renderer` and `rr_pathtracer` stops being "two POD headers".
3. **Legacy `MaterialGraph.{h,cpp}` API (`GraphNode`, `compile_*`,
   `TextureSamplerFn`).** Already classified `REWRITE` (step 2). New
   `material::graph::` API (`Graph`, `Node`, `Socket`, `GraphEvaluator`) is
   the keeper.
4. **`OptixRenderer::Result::message`-only failure surface.** The placeholder
   reports diagnostics through `message` only; once OptiX renders for real,
   align it with `CudaRenderer::Result { ok, image, message }`.
5. **Server protocol surface.** Names (`HELLO`, `RENDER`, `STATUS`, `QUIT`)
   are fine for v1, but the C++ surface needs widening for streaming AOVs,
   cancellation, and progress (KEEP_FOR_LATER per step 7).
6. **CUDA-arch / device-info accessors.** Not currently surfaced. The rewrite
   will need `rr::gpu::DeviceInfo`-style queries; pick the names early so
   they don't grow ad hoc out of `GpuScene`.

### What the rewrite should *not* re-litigate

- File-/type-/function-/macro-/namespace casing. Stable across 159 files.
- The `rr_` library prefix. Stable.
- The `RR_HAS_CUDA` / `RR_HAS_OPTIX` capability-macro pattern. Stable.
- The `Result{ok,…,message}` failure shape. Stable.

---

## Summary

| Area | Verdict |
|---|---|
| Naming consistency (files, types, funcs, members, constants, macros) | Clean |
| Module ↔ library ↔ namespace alignment | Clean (one-to-one) |
| `[[nodiscard]]` / `noexcept` / move-only / RAII | Clean |
| Header hygiene (host-only headers, `.cuh` for device, forward decls) | Clean |
| Result-type and error conventions | Clean (two patterns, each used in the right place) |
| `CudaRenderer` static-only class shape | Needs reshape in rewrite |
| `renderer/` + `pathtracer/` public surfaces | Need to grow — names match plan, contents don't yet |
| Legacy `MaterialGraph` (`GraphNode` etc.) | Already `REWRITE` per step 2 |
| `OptixRenderer` failure surface | Aligns with CUDA path once real |
| Server C++ API | KEEP_FOR_LATER per step 7 — needs v2 widening |

No naming or API decision in the prototype is a load-bearing mistake. The
conventions are consistent and the rewrite can adopt them as-is. The two
genuinely API-shaped issues — `CudaRenderer`'s static-only shell and the
content gap in `renderer/` + `pathtracer/` — are already implicit in the
architecture audit; this step just confirms that they show up as API smells
too, not just structural ones.

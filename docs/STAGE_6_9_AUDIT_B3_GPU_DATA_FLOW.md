# Stage 6–9 Audit B3 — GPU data flow scan

Date: 2026-04-29
Branch: `relativity-core-v1`
Scope: trace the complete host → device → kernel → host data flow
for Stages 6–9. Restricted to:

- `src/gpu/`
- `src/cuda/`
- `src/scene/`
- `src/geometry/`
- `src/material/`
- `src/lighting/`

Other host modules (`renderer/`, `image/`, `main.cpp`) are out of
B3 scope.

This doc is documentation. No source files are modified.

---

## Layered overview

```
┌──────────────────────────────────────────────────┐
│ Host: rr::scene::Scene + Camera/Mesh/...         │
│   POD wrappers (SceneSphere/Mesh/Material/Light) │
└───────────────┬──────────────────────────────────┘
                │  (extracted in main.cpp; out of B3 scope)
                ▼
┌──────────────────────────────────────────────────┐
│ rr::gpu::GpuScene (RAII, move-only)              │
│   GpuBuffer<Sphere>     spheres_                 │
│   GpuMesh               mesh_                    │
│     ├─ GpuBuffer<Vertex>     vertices_           │
│     └─ GpuBuffer<Triangle>   triangles_          │
│   GpuBuffer<MaterialParams> materials_           │
│   GpuBuffer<Light>          lights_              │
│   + by-value: GpuCamera, Observer, Params        │
└───────────────┬──────────────────────────────────┘
                │  upload_*  → GpuBuffer<T>::upload
                │             → detail::gpu_copy_host_to_device
                │             → cuda::cuda_copy_h2d (cudaMemcpy)
                ▼
┌──────────────────────────────────────────────────┐
│ Device memory (cudaMalloc-allocated)             │
│   one cudaMalloc per non-empty GpuBuffer<T>      │
└───────────────┬──────────────────────────────────┘
                │  CudaRenderer::render_scene snapshots
                │  device pointers + counts into a
                │  CudaSceneView, passed by value as
                │  the launch argument
                ▼
┌──────────────────────────────────────────────────┐
│ k_render_scene (one thread per pixel)            │
│   reads scene.camera/observer/params by value    │
│   reads scene.spheres/mesh/materials/lights via  │
│   raw device pointers; writes pixels[]           │
└───────────────┬──────────────────────────────────┘
                │  cudaDeviceSynchronize
                │  GpuBuffer<float>::download (d2h memcpy)
                ▼
┌──────────────────────────────────────────────────┐
│ Host: rr::image::Image (Rgba32F)                 │
└──────────────────────────────────────────────────┘
```

---

## Findings (10 questions)

### 1. Camera upload

| Field | Value |
|---|---|
| Implemented | **Yes** |
| File / function | `src/gpu/GpuScene.cpp:7` — `GpuScene::upload_camera(const Camera&)` |
| Mechanism | Calls `Camera::to_gpu()` to snapshot into a `GpuCamera` POD; sets `has_camera_ = true`. **Pure host write** — no device allocation, no device copy. The `GpuCamera` is stored by value on the `GpuScene`; the kernel reads it by value as a launch argument (`CudaSceneView::camera`). |
| Risk / issue | Low. The kernel does **not** check `has_camera_`; if the host calls `render_scene` without a prior `upload_camera`, the kernel reads a default-constructed `GpuCamera` (zero `tan_half_vfov`, zero aspect → degenerate ray-gen, image collapses to a single direction). Documented as a precondition in `CudaRenderer.h:49–53`. |

### 2. Relativity params upload

| Field | Value |
|---|---|
| Implemented | **Yes** |
| File / function | `src/gpu/GpuScene.cpp:13` — `GpuScene::upload_relativity(const Observer&, const RelativityParams&)` |
| Mechanism | Pure host writes into `observer_` + `params_`; sets `has_relativity_ = true`. Both PODs are passed to the kernel by value via `CudaSceneView::observer` / `params`. |
| Risk / issue | Low. The kernel does not check `has_relativity_`; if the host skips this call, the kernel reads default-constructed values (`Observer.velocity = (0,0,0)` → β=0, no relativistic effects; all `enable_*` flags = true; `max_beta = 0.999999`). Default behaviour is "no relativity applied" rather than crashy, which is the desired honest fallback. |

### 3. Sphere upload

| Field | Value |
|---|---|
| Implemented | **Yes** |
| File / function | `src/gpu/GpuScene.cpp:22` — `GpuScene::upload_spheres(const Sphere*, std::size_t)` |
| Mechanism | Forwards to `GpuBuffer<Sphere>::upload` (`src/gpu/GpuBuffer.h:75`). That in turn calls `detail::gpu_alloc` (if size differs) → `cuda_alloc` (`src/cuda/CudaBuffer.cpp` — `cudaMalloc`) and then `detail::gpu_copy_host_to_device` → `cuda_copy_h2d` (`cudaMemcpy(... cudaMemcpyHostToDevice)`). Synchronous. Backend-honest: `count == 0` clears + always succeeds; non-zero needs a working backend. |
| Risk / issue | Low. On any failure path, `GpuScene::upload_spheres` calls `spheres_.reset()` and sets `sphere_count_ = 0` so the kernel never sees a stale device pointer paired with a non-zero count. |

### 4. Mesh upload

| Field | Value |
|---|---|
| Implemented | **Yes** (single-mesh slot; multi-mesh deferred). |
| File / function | `src/gpu/GpuScene.cpp:46` — `GpuScene::upload_mesh(const Mesh&)` → `src/gpu/GpuMesh.cpp:46` — `GpuMesh::upload_from(const Mesh&)`. Delegates to `upload_vertices` (line 5) + `upload_triangles` (line 23) + `set_metadata` (line 41). |
| Mechanism | Two independent `GpuBuffer<T>::upload` calls (one for `Vertex`, one for `Triangle`). Each uses `cudaMalloc` + `cudaMemcpy(H2D)`. `set_metadata` is a pure host write of `material_id` + `Transform`. |
| Risk / issue | **Partial-failure semantics**. `GpuMesh::upload_from` ANDs the two upload return values: if vertex upload succeeds and triangle upload fails (or vice versa), the mesh is left in a state where one buffer holds data and the other is reset. The kernel reads `mesh.triangle_count` for its triangle loop, so a vertex-only mesh would render **nothing** (loop runs zero iterations). The state is render-safe but "half allocated" — the orphan vertex buffer still owns its `cudaMalloc` until the next `upload_vertices` call or `GpuMesh` destruction. **Documented in `GpuMesh.cpp:46–53`** as an explicit design choice ("metadata is host-only and always safe to write; we set it even on a failed upload so callers can inspect the partial state for debugging"). |

### 5. Material upload

| Field | Value |
|---|---|
| Implemented | **Yes** |
| File / function | `src/gpu/GpuScene.cpp:50` — `GpuScene::upload_materials(const MaterialParams*, std::size_t)` |
| Mechanism | Same pattern as sphere upload: `GpuBuffer<MaterialParams>::upload` → `cudaMalloc` + H2D `cudaMemcpy`. |
| Risk / issue | Low. Same backend-honest semantics as spheres: failure path resets to zero count. The kernel reads `scene.materials[best.material_index]` only when `material_index >= 0 && material_index < material_count`; out-of-range indices fall back to the neutral default `MaterialParams` (`baseColor = (0.8, 0.8, 0.8)`, no emission). |

### 6. Light upload

| Field | Value |
|---|---|
| Implemented | **Yes** |
| File / function | `src/gpu/GpuScene.cpp:71` — `GpuScene::upload_lights(const Light*, std::size_t)` |
| Mechanism | Same pattern as material upload: `GpuBuffer<Light>::upload` → `cudaMalloc` + H2D `cudaMemcpy`. |
| Risk / issue | Low. Same backend-honest semantics. The kernel iterates `scene.lights[0 .. light_count − 1]` only when `light_count > 0 && lights != nullptr`; otherwise it falls through to the Stage 8B facing-ratio shade. |

### 7. Device memory ownership

| Field | Value |
|---|---|
| Implemented | **Yes** |
| File / function | `src/gpu/GpuBuffer.h` — move-only RAII template; copy ctor + copy assign explicitly deleted (`GpuBuffer.h:39–40`); move ctor + move assign defaulted as `noexcept` (`:42–57`). One `cudaMalloc` allocation per non-empty `GpuBuffer<T>`; `ptr_` is `nullptr` when empty. |
| Mechanism | A `GpuScene` owns five live device allocations at most, all reachable through value-typed members: `spheres_`, `materials_`, `lights_`, plus the two buffers inside `mesh_` (`vertices_`, `triangles_`). No raw `void*` lives outside a `GpuBuffer<T>`. Move-only enforces single-ownership at compile time. |
| Risk / issue | Low. Move semantics are correct: the moved-from buffer has `ptr_ = nullptr` and `count_ = 0`, so its dtor's `reset()` early-outs and does not double-free. `GpuMesh` is also move-only by composition (its two `GpuBuffer<T>` members make it implicitly move-only-able). |

### 8. Device memory cleanup

| Field | Value |
|---|---|
| Implemented | **Yes** |
| File / function | `src/gpu/GpuBuffer.h:37, 94` — `~GpuBuffer()` calls `reset()`, which calls `detail::gpu_free` → `cuda_free` (`src/cuda/CudaBuffer.cpp:18` — `cudaFree`). `src/gpu/GpuScene.cpp:92` — `GpuScene::reset_device()` resets every device buffer; `:106` — `GpuScene::clear()` does the same plus host-state reset. `GpuScene::reset_device` replaces `mesh_` with a default-constructed `GpuMesh{}`, which triggers the move-replacement and frees the old mesh's two `GpuBuffer<T>` allocations indirectly via their dtors. |
| Mechanism | Three converging paths: (a) explicit `reset()` / `clear()` calls; (b) the implicit `GpuScene` dtor at scope end; (c) `GpuBuffer<T>::upload` calling `reset()` internally when `count_ != count`. All three converge on `cudaFree`. |
| Risk / issue | Low. `cudaFree(nullptr)` is well-defined (no-op) per CUDA spec, plus `cuda_free` early-returns on null. `reset()` is safe to call repeatedly. `GpuMesh` does not expose a "drop only the device buffers" method — `reset_device` indirects via `mesh_ = GpuMesh{}`, which works correctly but is non-obvious; documented in `GpuScene.cpp:96–98`. |

### 9. Host-to-device copies

| Field | Value |
|---|---|
| Implemented | **Yes** |
| File / function | Top: `GpuBuffer<T>::upload` (`src/gpu/GpuBuffer.h:75`). Dispatch: `detail::gpu_copy_host_to_device` (`src/gpu/GpuBuffer.cpp:26`). CUDA backend: `cuda_copy_h2d` (`src/cuda/CudaBuffer.cpp:37`). |
| Mechanism | Synchronous `cudaMemcpy(device, host, bytes, cudaMemcpyHostToDevice)`. On failure, the sticky CUDA last-error flag is cleared (`clear_last_error()`) so a later real CUDA call doesn't observe a stale error; the call returns `false`, `GpuBuffer<T>` propagates `false`, the calling `GpuScene::upload_*` resets the corresponding count to zero. |
| Risk / issue | Low. Synchronous (no `cudaMemcpyAsync`); fine for Stage 6–9 scope. Bytes count is `count * sizeof(T)` — `T` is static-asserted trivially copyable in `GpuBuffer<T>`'s template (`GpuBuffer.h:32–33`), so byte-shuffle copies are safe. |

### 10. Device-to-host framebuffer copy

| Field | Value |
|---|---|
| Implemented | **Yes** |
| File / function | `src/cuda/CudaRenderer.cu:66` — `dev.download(img.data(), img.size_in_floats())` inside the `run_kernel_render` template. Path: `GpuBuffer<float>::download` (`src/gpu/GpuBuffer.h:86`) → `detail::gpu_copy_device_to_host` (`src/gpu/GpuBuffer.cpp:35`) → `cuda_copy_d2h` (`src/cuda/CudaBuffer.cpp:46`). |
| Mechanism | The render scaffold allocates a `GpuBuffer<float>` of `width * height * 4` floats, launches the kernel, calls `cudaDeviceSynchronize` (`CudaRenderer.cu:59`), then `download`s. Synchronous. The `Image` is constructed with `PixelFormat::Rgba32F` so `size_in_floats() = w*h*4` matches the buffer count. |
| Risk / issue | Low. `download(..., count)` checks `count > count_` and returns false on overflow (`GpuBuffer.h:87`), so the size-mismatch case is caught. The `Image::data()` host pointer is owned by `std::vector<float>` inside `Image`; lifetime is the entire render call. |

---

## Cross-cutting risks (not in the 10 questions, but worth recording)

These are implementation-detail observations that came up during the
data-flow trace. None block Stage 6–9 forward progress.

### R1 — `int` cast on counts in `CudaSceneView`

`CudaRenderer::render_scene` casts `std::size_t` counts to `int`:

```cpp
view.sphere_count   = static_cast<int>(scene.sphere_count());
view.material_count = static_cast<int>(scene.material_count());
view.light_count    = static_cast<int>(scene.light_count());
view.mesh.vertex_count   = static_cast<int>(m.vertex_count());
view.mesh.triangle_count = static_cast<int>(m.triangle_count());
```

`size_t > INT_MAX` would silently wrap. Practical concern is zero —
no scene at this stage has 2 billion of anything — but worth knowing
when the path tracer or BVH stages later push the entity counts.

### R2 — Asynchronous launches not used

Every host→device and device→host copy uses synchronous
`cudaMemcpy`, not `cudaMemcpyAsync`. Streams are not used; every
launch goes on the default stream. Acceptable for Stages 6–9
("no async streaming" is implied by the kernel-launch design); a
streams + pinned-memory pass is a future optimization slice.

### R3 — `GpuMesh` partial-failure leaves orphan buffer on the GPU

Already noted in §4. If `upload_from`'s vertex upload succeeds but
triangle upload fails, the vertex buffer keeps its `cudaMalloc`
allocation until the next `upload_vertices` call or `GpuMesh`
destruction. Render-safe (kernel sees `triangle_count == 0` and
loops zero times) but data-unsafe (memory is held). The next
`upload_from` or `GpuScene::reset_device` reclaims it.

### R4 — `has_camera_` / `has_relativity_` flags are tracked but never enforced

`GpuScene` records both, but `CudaRenderer::render_scene` doesn't
check either before launching. If the host forgets to call
`upload_camera`, the kernel reads a default-constructed `GpuCamera`
(zero `tan_half_vfov` → degenerate / dark output). Documented as a
precondition in `CudaRenderer.h:49–53`.

### R5 — Dropped uploads when sphere/material/light count is zero

The kernel's three `if (count > 0 && ptr != nullptr)` guards are
correct: a zero-count upload is a successful no-op, and the kernel
falls through to the corresponding fallback (no spheres → only the
sky gradient is visible; no materials → neutral default; no lights
→ Stage 8B facing-ratio shade).

---

## Verdict

All ten data-flow paths are **implemented and working**. Five
synchronous H2D paths (camera, relativity, spheres, mesh, materials,
lights) feed device buffers owned by a single `GpuScene` instance;
device memory is move-only RAII through `GpuBuffer<T>` and is
released either explicitly via `reset_device` / `clear` or
implicitly at `GpuScene` scope end. The kernel reads through raw
device pointers carried in a `CudaSceneView` POD; the framebuffer
returns to the host via one synchronous D2H copy after
`cudaDeviceSynchronize`.

The five cross-cutting risks (R1–R5) are all minor: integer-narrowing
on counts, lack of async streams, the documented `GpuMesh`
partial-failure window, the un-enforced `has_*` flags, and the
zero-count fallback. None of them require code changes before the
scene-parser stage.

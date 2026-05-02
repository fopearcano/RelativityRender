# GPU Memory Audit (Stage 18A.2)

**Status:** Documentation-only audit. No code changes. The audit
reflects the codebase at the head of `relativity-core-v1` after
Stage 18A.1.

**Scope:** every device-resident allocation produced by the
RelativityRender code base, the matching free, the lifetime
owner, and a cross-check for leaks and duplication. Audit-host
fallbacks (`RR_ENABLE_CUDA=OFF` and
`RELATIVITYRENDER_OPTIX_SDK_FOUND` undefined) are included for
completeness; without a CUDA-capable host both paths reduce to
honest "no-op / failure" behaviour and never call into the
runtime, so they have no allocation surface to audit.

**Scope exclusions:** host-side image / scene / material vectors
(those live in `std::vector` / `std::array` and are bounded by
the C++ runtime, not the GPU). Per-launch CUDA event markers
(create / destroy paired in `rr::gpu::GpuTimer` RAII) are
covered briefly under §6.

**Result:** no leaks, no double-frees, no orphan allocations.
Every device-resident byte is owned by a move-only RAII handle
or freed manually on every exit path of the function that
allocated it. Two duplications are documented (§4) - both are
intentional and tied to specific stage decisions; neither leaks.

---

## 1. Backend primitives

The byte-level allocation primitives live in two files:

| File | Function | Underlying API | Purpose |
|------|----------|----------------|---------|
| `src/cuda/CudaBuffer.cpp` | `cuda_alloc(bytes)` | `cudaMalloc` | Typed-buffer allocator |
| `src/cuda/CudaBuffer.cpp` | `cuda_free(ptr)`   | `cudaFree`   | Paired free            |
| `src/cuda/CudaBuffer.cpp` | `cuda_copy_h2d`    | `cudaMemcpy` | H2D copy               |
| `src/cuda/CudaBuffer.cpp` | `cuda_copy_d2h`    | `cudaMemcpy` | D2H copy               |
| `src/cuda/CudaTiming.cpp` | `cuda_event_create()`  | `cudaEventCreate`   | Timer event            |
| `src/cuda/CudaTiming.cpp` | `cuda_event_destroy(e)`| `cudaEventDestroy`  | Paired free            |

These are bridged by host-only thin wrappers in `src/gpu/`:

| File | Function | Forwards to | When CUDA is OFF |
|------|----------|-------------|------------------|
| `src/gpu/GpuBuffer.cpp` | `detail::gpu_alloc / gpu_free / gpu_copy_*` | `rr::cuda::cuda_*` | Returns `nullptr` / `false`; no allocation happens |
| `src/gpu/GpuTiming.cpp` | `GpuTimer` methods                          | `rr::cuda::cuda_event_*` | All methods are no-ops; `elapsed_ms()` returns 0 |

Failure paths in `cuda_alloc` / `cuda_event_create` clear the
sticky CUDA last-error flag (`cudaGetLastError()`) before
returning `nullptr`, so a later real CUDA call does not observe
a stale error from the failed allocation attempt.

The OptiX backend reuses `<cuda_runtime.h>` directly for its
auxiliary buffers (GAS storage, SBT records, launch-param
buffer); see §3 below.

`OptixBackend::initialize()` calls `cudaFree(0)` on entry. This
is the canonical CUDA-7+ idiom for forcing CUDA primary-context
creation on the current device before
`optixDeviceContextCreate(0, ...)` inherits it. It is **not** a
real free - the argument is the literal integer 0, not a
device-pointer. No allocation is involved.

---

## 2. RAII owners (host-side)

Every device allocation in the project is held by exactly one
move-only RAII handle. Move is supported (the source becomes
empty / nullptr after move-from); copy is deleted everywhere.
The destructor of every owner unconditionally frees the
underlying device memory.

### 2.1 `rr::gpu::GpuBuffer<T>` — primary typed buffer

- **File:** `src/gpu/GpuBuffer.h` (template-only).
- **Owns:** one device-resident array of `count_` `T` elements.
- **Allocate:** `allocate(count)` calls `gpu_alloc(count *
  sizeof(T))`; `upload(host, count)` reallocates if the size
  changes.
- **Free:** `reset()` calls `gpu_free(ptr_)`; the destructor
  calls `reset()`. Move ops also `reset()` the destination
  before stealing.
- **Empty allocation:** `allocate(0)` is a successful clear
  (`gpu_alloc(0)` returns `nullptr`, count goes to 0).
- **Audit-host:** without CUDA `gpu_alloc` returns `nullptr`,
  `allocate` returns `false`, and `reset` is a no-op (no `ptr_`
  to free).

### 2.2 `rr::gpu::GpuMesh` — vertex + triangle pair

- **File:** `src/gpu/GpuMesh.h` / `GpuMesh.cpp`.
- **Owns:** two `GpuBuffer<T>` members
  (`vertices_: GpuBuffer<Vertex>`,
   `triangles_: GpuBuffer<Triangle>`) plus host metadata
   (`material_id`, `Transform`).
- **Allocate:** `upload_vertices` / `upload_triangles` /
  `upload_from(Mesh)` forward to the inner GpuBuffers.
- **Free:** default destructor; the two GpuBuffers free
  themselves. No `cudaFree` is reachable from this class.

### 2.3 `rr::gpu::GpuTexture` — texture pixel storage

- **File:** `src/gpu/GpuTexture.h` / `GpuTexture.cpp`.
- **Owns:** one `GpuBuffer<std::byte>` plus
  `width_ / height_ / format_` host metadata.
- **Allocate:** `upload(...)` forwards to `pixels_.upload(...)`.
- **Free:** `reset()` calls `pixels_.reset()`; destructor
  follows.

### 2.4 `rr::gpu::GpuScene` — scene aggregate

- **File:** `src/gpu/GpuScene.h` / `GpuScene.cpp`.
- **Owns:**
  - `GpuBuffer<rr::geometry::Sphere> spheres_`
  - `GpuMesh                          mesh_`         (single-mesh slot)
  - `GpuBuffer<rr::material::MaterialParams> materials_`
  - `GpuBuffer<rr::lighting::Light> lights_`
  - `std::vector<GpuTexture>        textures_`        (one entry per uploaded texture)
- **Allocate:** `upload_*` methods forward to the inner owners.
  Each upload follows the same pattern: clear on `count == 0`,
  fail-and-clear on `host == nullptr`, fail-and-clear on
  upload failure (no partial state ever reaches the kernel).
- **Free:** default destructor (every member is RAII).
  `reset_device()` explicitly clears every device-side member
  while preserving host snapshots (camera / observer / params).
  `clear()` resets host snapshots too.
- **Note:** GpuScene is `default`-move-constructible; moving a
  GpuScene transfers ownership of every inner buffer + texture
  vector at once.

### 2.5 `rr::renderer::AccumulationBuffer`

- **File:** `src/renderer/AccumulationBuffer.{h,cpp}`.
- **Owns:** one `GpuBuffer<float> device_` sized to
  `width * height * 4` floats (Rgba32F sums + sample weights).
- **Allocate:** `resize(w, h)` -> `device_.allocate(...)`.
- **Free:** default destructor.
- **Aux allocation:** `resolve_to_image()` allocates a
  function-scope `GpuBuffer<float> display`; freed on RAII
  exit (every return path).

### 2.6 `rr::renderer::GpuAOVBuffer`

- **File:** `src/renderer/GpuAOVBuffer.{h,cpp}`.
- **Owns:** one `GpuBuffer<float> device_` sized to
  `width * height * components` floats. Per AOV's
  `aov_component_count` mapping (Beauty / Normal / Albedo = 3,
  Depth / DopplerFactor / SearchlightFactor = 1).
- **Allocate:** `resize(w, h)` -> `device_.allocate(...)`.
- **Free:** default destructor.

### 2.7 `rr::optix::OptixBackend`

- **File:** `src/optix/OptixBackend.{h,cpp}`.
- **Owns:** the `OptixDeviceContext` (real type:
  `OptixDeviceContext`; stored as `void*` to keep `<optix.h>`
  out of the public header).
- **Allocate:** `initialize()` calls `optixDeviceContextCreate
  (0, ...)`.
- **Free:** `shutdown()` calls `optixDeviceContextDestroy(...)`
  if `context_ != nullptr`. Idempotent. Destructor calls
  `shutdown()`.
- **Note:** the SDK's primary CUDA context (primed by
  `cudaFree(0)`) is *not* destroyed by us - that lives until
  process exit. OptiX docs explicitly support this.

### 2.8 `rr::optix::OptixGas`

- **File:** `src/optix/OptixAccel.{h,cpp}`.
- **Owns:** the `device_buffer_` `void*` produced by
  `optixAccelBuild` (the AS output storage) plus the
  `OptixTraversableHandle` (`uint64_t`) and a `output_size
  _bytes_` book-keeping field.
- **Allocate:** populated externally via `assign(handle,
  buffer, size)`, called from `build_mesh_gas` after
  `cudaMalloc` + `optixAccelBuild` succeed.
- **Free:** `reset()` calls `cudaFree(device_buffer_)` (only in
  the SDK-found build; the audit-host fallback never produces
  a non-null buffer). Destructor calls `reset()`. Move ops
  reset the destination before stealing.

### 2.9 `rr::optix::OptixPipeline`

- **File:** `src/optix/OptixPipeline.{h,cpp}`.
- **Owns** (5 device-side + 2 host-side resources):
  | Member | Type | Free call |
  |--------|------|-----------|
  | `module_`         | `OptixModule`                  | `optixModuleDestroy` |
  | `prog_raygen_`    | `OptixProgramGroup`            | `optixProgramGroupDestroy` |
  | `prog_miss_`      | `OptixProgramGroup`            | `optixProgramGroupDestroy` |
  | `prog_hitgroup_`  | `OptixProgramGroup`            | `optixProgramGroupDestroy` |
  | `pipeline_`       | `OptixPipeline_t*`             | `optixPipelineDestroy` |
  | `sbt_record_buf_` | `void*` (cudaMalloc'd)         | `cudaFree` |
  | `sbt_descriptor_` | `OptixShaderBindingTable*` (host `new`)| `delete` |
  | `launch_params_`  | `void*` (cudaMalloc'd)         | `cudaFree` |
- **Allocate:** `create(backend)` builds them in dependency
  order; on partial-failure paths it frees every resource
  built so far before returning. The order is replicated below
  for cross-checking against the .cpp.
- **Free:** `reset()` releases them in reverse order
  (launch_params_ -> sbt_descriptor_ -> sbt_record_buf_ ->
   pipeline_ -> hitgroup -> miss -> raygen -> module_).
  Destructor calls `reset()`. Move ops null the source's
  pointers and call `reset()` on the destination before stealing.

### 2.10 `rr::gpu::GpuTimer`

- **File:** `src/gpu/GpuTiming.{h,cpp}`.
- **Owns:** two `cudaEvent_t` handles (stored as `void*`).
- **Allocate:** constructor calls `cuda_event_create()` twice.
  If either fails the survivor is freed and both pointers go
  to null so `valid()` is honest.
- **Free:** destructor calls `cuda_event_destroy` on each. Move
  ops null the source's pointers and free the destination's
  events before stealing.

---

## 3. Function-scope `cudaMalloc` / `cudaFree` (no RAII wrapper)

A small set of OptiX-side functions allocate device memory with
raw `cudaMalloc` and free it manually on every exit path. They
are not wrapped in RAII because the allocation pattern is
strictly local to one function and the lifetime is bounded by
that function's scope (or transferred to an `OptixGas` owner on
success).

### 3.1 `OptixRenderer::render_test(int, int)`

`src/optix/OptixRenderer.cpp:46-148` (SDK-found body).

| Allocation | Lifetime | Free locations |
|------------|----------|----------------|
| `d_framebuffer` | function scope | every `return r;` after the cudaMalloc; `cudaFree(d_framebuffer)` + return on each failure path; final `cudaFree` before successful return |

5 free sites total (1 success path + 4 failure paths). All
matched.

### 3.2 `OptixRenderer::render_triangle(int, int)`

`src/optix/OptixRenderer.cpp:152-345`.

| Allocation | Lifetime | Free locations |
|------------|----------|----------------|
| `d_vertices` | function scope | every failure path after the cudaMalloc; final `cudaFree` before successful return |
| `d_indices`  | function scope | every failure path after the cudaMalloc; final `cudaFree` before successful return |
| `d_framebuffer` | function scope | every failure path after the cudaMalloc; final `cudaFree` before successful return |
| `d_output` (GAS) | transferred to `r.gas` on success, freed by `OptixGas::reset()` later | the local `BuildGasResult gas_result` owner takes ownership; on failure `build_mesh_gas` itself frees the temp + output before returning |

Every `cudaMalloc` site is followed by the matching `cudaFree`
on every reachable `return r;` below it - confirmed by grep
(see audit transcript). 14 distinct `cudaFree(d_*)` sites in
this function; each balances a `cudaMalloc(d_*)` above.

### 3.3 `OptixRenderer::render_relativistic(int, int)`

`src/optix/OptixRenderer.cpp:349-540`.

Same allocation shape and free pattern as `render_triangle`
(this is one of the documented duplications - see §4).

### 3.4 `build_mesh_gas(...)`

`src/optix/OptixAccel.cpp:92-243` (SDK-found body).

| Allocation | Lifetime | Free locations |
|------------|----------|----------------|
| `d_temp`    | function scope (build scratch) | every failure path; unconditional `cudaFree(d_temp)` before successful return |
| `d_output`  | transferred to `OptixGas::assign` on success | failure path frees `d_output` before return; success path hands ownership to `r.gas` |

`d_temp` is freed in **all** paths - including the success
path - because OptiX 7+ does not need the build-temp buffer
after `optixAccelBuild` returns.

### 3.5 `OptixPipeline::create(OptixBackend&)` SBT + launch-params buffers

`src/optix/OptixPipeline.cpp:106-419` (SDK-found body).

| Allocation | Lifetime | Free locations |
|------------|----------|----------------|
| `d_records` (one device buffer of [raygen][miss][hitgroup] records) | committed to `OptixPipeline::sbt_record_buf_` on success; freed by `reset()` | failure paths free `d_records` before destroying every prior resource (program groups, module) and returning |
| `d_launch_params` (single `OptixLaunchParams`-sized buffer) | committed to `OptixPipeline::launch_params_` on success; freed by `reset()` | only one allocation site, only one failure path; freed there before returning |

The host-side `OptixShaderBindingTable` descriptor is allocated
with `new` rather than `cudaMalloc` (it is a host POD that
points at device memory) and freed with `delete` in `reset()`.
Pairing is direct: `new` at `OptixPipeline.cpp:371`, `delete`
at `OptixPipeline.cpp:427`.

---

## 4. Duplication

Two intentional duplications exist; neither leaks memory and
both are tracked as future-cleanup candidates rather than bugs.

### 4.1 OptiX triangle-render boilerplate

`OptixRenderer::render_triangle` and
`OptixRenderer::render_relativistic` share an identical
allocation prologue:

```text
static const float kVertices[3 * 3] = { ... };
static const std::uint32_t kIndices[3] = { 0, 1, 2 };
cudaMalloc(&d_vertices, sizeof(kVertices));
cudaMemcpy(...);
cudaMalloc(&d_indices, sizeof(kIndices));
cudaMemcpy(...);
build_mesh_gas(...);
```

The two functions diverge only in the launch-params
population (relativity adds a non-zero observer + default
RelativityParams) and in error message strings. The shared
prologue / cleanup are duplicated verbatim.

**Why it's tolerated today:** Stage 17A.4 / 17A.5's hard rules
forbade introducing new abstractions in those slices;
extracting a private `upload_demo_triangle_gas(backend)` /
`with_triangle_scene(backend, fn)` helper is on the explicit
list of post-Stage-17 cleanups. No memory hazard - both
functions free everything they allocate.

**Future fix:** factor out the GAS-build prologue once a third
OptiX-render call site lands.

### 4.2 CUDA vs OptiX triangle storage

The CUDA path uploads triangles via `rr::gpu::GpuMesh` (two
`GpuBuffer<...>` for vertices + indices, plus host metadata).
The OptiX path uploads triangles via raw `cudaMalloc` /
`cudaMemcpy` of `float[3*N]` + `uint32_t[3*M]` arrays in the
shape `optixAccelBuild` consumes.

The two storage layouts are NOT the same buffer:

- The CUDA path's `Vertex` carries position + normal + UV;
  `Triangle` carries three vertex indices.
- The OptiX path's vertex array is a tight `float3` packed
  array (no normal / UV) because the GAS only consumes
  position; per-vertex normal / UV interpolation is done
  inside the closest-hit program via `optixGetTriangleVertex
  Data` (or future per-vertex attribute buffers).

**Why it's tolerated today:** different backend formats are
a fact of life when moving from a hand-written CUDA closest-
hit kernel to OptiX's built-in triangle intersection. A
unified storage layer would either pay a copy cost on one
side or constrain the other. No memory hazard - the two
backends run separate processes / passes; no buffer is held
twice simultaneously today.

**Future fix:** the long-term plan in `OPTIX_BACKEND_PLAN.md`
is for `GpuScene` to grow an `OptixGas` slot built from the
same `GpuMesh` device buffers (the OptiX `vertexBuffers` array
can point straight at `GpuMesh::device_vertices()`'s position
field if the stride is set to `sizeof(Vertex)`). That removes
the duplication without a host-side copy. Tracked as Stage
17B+ work.

---

## 5. Per-render allocations (hot paths)

The renderer allocates fresh device buffers per render call.
This is correct (every buffer is RAII-freed before the call
returns; no inter-call accumulation) but worth recording as a
future-optimization target for interactive use.

| Call | Allocation | Bytes (1280x720) | Owner |
|------|------------|------------------|-------|
| `run_kernel_render` | `GpuBuffer<float>` framebuffer | ~3.5 MiB | RAII (function-scope) |
| `CudaRenderer::render_scene_with_aovs` | per-AOV buffer (1 or 3 floats / pixel) | up to 6 buffers, ~10.5 MiB total | `GpuAOVBuffer` (caller-owned) |
| `CudaRenderer::render_scene{,_with_aovs}` | `GpuBuffer<DeviceTextureView>` flat array | bytes = `texture_count * sizeof(DeviceTextureView)`, typically <1 KiB | RAII (function-scope) |
| `OptixRenderer::render_*` | `d_framebuffer` + `d_vertices` + `d_indices` + GAS output | ~3.5 MiB framebuffer + ~kB triangle data | manual / RAII via OptixGas |
| `PathTracer::render` | `AccumulationBuffer` + sample buffer + `display` | 3 * ~3.5 MiB = ~10.5 MiB | RAII (function-scope) |
| `PathTracer::render` (per launch) | nothing - the spp loop reuses the sample / accumulation buffers | 0 | n/a |
| `OptixPipeline::create` | SBT records buffer + launch-params buffer | ~3 * record-size + 1 launch-params bytes (small kB range) | `OptixPipeline` (caller-owned) |
| `OptixGas` | acceleration-structure output | reported by `optixAccelComputeMemoryUsage` (typically ~kB for the demo triangle) | `OptixGas` (caller-owned) |
| `GpuTimer` (per render) | 2 `cudaEvent_t` markers | ~32-64 bytes | RAII (`GpuTimer` ctor/dtor) |

**Future optimisation:** a "render context" could cache the
framebuffer / accumulation / AOV buffers across calls when
dimensions don't change. Today every render allocates and
frees them, costing ~3-10 MiB allocator round-trips per
render. Acceptable for batch CLI use; worth revisiting when
the renderer-server (Stage 15) gains interactive preview.

---

## 6. Leak / double-free analysis

Method: for every `cudaMalloc` / `cudaEventCreate` /
`cudaMemcpy(... cudaMemcpyHostToDevice)` site found by `grep`,
the audit traced every reachable `return` statement below it
and confirmed the matching `cudaFree` /
`cudaEventDestroy` sits between the allocation and the
return. RAII handles were verified by reading the destructor
of every owner and tracing every move-from path.

### 6.1 Findings

- **No leaks.** Every device allocation is freed on every exit
  path. The OptiX render functions (§3.2 / §3.3) have the
  most `return` sites and were audited line-by-line; every
  `return r;` after a `cudaMalloc` is preceded by the matching
  `cudaFree`.
- **No double-frees.** Each owner's `reset()` nulls its
  pointer fields after freeing them; the destructor's
  `reset()` is therefore a no-op if `reset()` was already
  called. Move-from operations also null the source's
  pointers, so the destructor of a moved-from object frees
  nothing.
- **No use-after-free.** All accessor methods on RAII owners
  return `nullptr` / 0 when the buffer is empty or moved-from
  (e.g. `GpuBuffer::device_ptr()` after `reset()` returns
  `nullptr`, never a stale pointer to freed memory). The
  kernel paths gate on `count == 0` / pointer null before
  reading.
- **No orphan allocations.** Every `cudaMalloc` site has a
  documented owner (RAII or function-scope manual free).
- **No raw pointer sharing across owners.** The two cases
  where a raw pointer is shared are intentional and
  non-owning:
  - `OptixGas::device_buffer()` exposes the GAS output buffer
    so the OptiX SBT / launch params can reference it via
    traversable handle. The pointer is non-owning; the GAS
    keeps ownership through its destructor.
  - `CudaSceneView` and `OptixLaunchParams` carry raw device
    pointers as launch arguments; they are never freed
    through the view/params struct. The owners
    (`GpuBuffer<T>` / `OptixGas` / `OptixPipeline`) outlive
    the view.

### 6.2 Cross-check against build artefacts

A full Linux build (`-DRR_ENABLE_CUDA=ON
-DRR_ENABLE_OPTIX=ON`, real CUDA + OptiX SDK)
running every render-* CLI action under `cuda-memcheck
--leak-check full` is the eventual confirmation step.
Documented as a follow-up validation gate; the audit-host
without CUDA cannot run it, so the audit relies on
source-level pairing rather than runtime confirmation today.

The Stage 18A.1 GPU-timing path uses `cudaEvent_t`. The
`GpuTimer` ctor allocates two events; the dtor destroys them.
Move ops null the source. No timer event is ever leaked - the
ctor/dtor pair is the only lifecycle path.

### 6.3 Cross-check against the Stage 18A.1 instrumentation

Stage 18A.1 added a `GpuTimer` instance to:

- `CudaRenderer::run_kernel_render` (every CUDA render path)
- `OptixRenderer::render_test` / `render_triangle` /
  `render_relativistic`
- `PathTracer::render` (one timer for the entire spp loop)

Each of those is a function-scope local; the timer's
destructor frees both events on every exit path. No timer
events accumulate across renders.

---

## 7. Audit-host fallback (no CUDA, no OptiX SDK)

When `RR_HAS_CUDA` is undefined the entire allocation surface
collapses to no-ops:

- `gpu_alloc` returns `nullptr`.
- `gpu_free` is a no-op.
- `GpuBuffer::allocate` / `upload` return `false`.
- `GpuBuffer::reset` is a no-op (no `ptr_` to free).
- `cuda_event_create` is not even compiled in (the rr_cuda
  CUDA-only sources are excluded from the rr_gpu target).
  `GpuTimer`'s no-op fallback methods are used instead.

When `RELATIVITYRENDER_OPTIX_SDK_FOUND` is undefined but
`ENABLE_OPTIX=ON`:

- `OptixBackend::initialize` returns `false` with the
  documented "SDK not found" error; `context_` stays null.
- `OptixGas::reset` is a no-op (the `cudaFree` call is gated
  on `RELATIVITYRENDER_OPTIX_SDK_FOUND` so the audit-host
  build does not even reference the symbol).
- `OptixPipeline::create` returns failure;
  `OptixPipeline::reset` is the audit-host fallback that
  nulls every pointer (none of which are populated).
- `build_mesh_gas` returns failure without touching CUDA.

In either fallback the project compiles, links, and runs - it
just refuses to perform any GPU operation. **No allocations
happen, no frees are needed, the audit trivially passes.**

---

## 8. Allocation tally

A snapshot of every `cudaMalloc` / `cudaEventCreate` /
`cudaFree` / `cudaEventDestroy` call site in the project at
the head of `relativity-core-v1`:

```text
src/cuda/CudaBuffer.cpp:    1 cudaMalloc, 1 cudaFree                (cuda_alloc / cuda_free wrappers)
src/cuda/CudaTiming.cpp:    1 cudaEventCreate, 1 cudaEventDestroy    (cuda_event_* wrappers)

src/optix/OptixBackend.cpp: 1 cudaFree(0)                            (CUDA primary-context priming - NOT a real free)

src/optix/OptixAccel.cpp:   2 cudaMalloc (d_temp, d_output)
                            3 cudaFree   (d_temp x2 success/failure, d_output failure)
                            (d_output transfers to OptixGas on success;
                             OptixGas::reset adds 1 more cudaFree later)
src/optix/OptixGas:         1 cudaFree (in reset, gated SDK-found)

src/optix/OptixPipeline.cpp:2 cudaMalloc (d_records, d_launch_params)
                            5 cudaFree   (d_records on 4 mid-build failure paths;
                                          1 in reset which also handles
                                          launch_params and sbt_record_buf)
                            new/delete pair for OptixShaderBindingTable

src/optix/OptixRenderer.cpp:9 cudaMalloc (3 per render fn x 3 fns)
                            ~40 cudaFree (one per failure path + final
                                          cleanup per render fn)
                            (Detailed inspection: every cudaMalloc has a
                             matching cudaFree on every reachable return path.)
```

Every `cudaMalloc` site has a paired free; every
`cudaEventCreate` site has a paired destroy. No orphan
allocations.

---

## 9. Recommendations (future stages, not this slice)

Stage 18A.2 is documentation-only. The following are tracked
recommendations for later slices:

1. **Reuse per-render scratch buffers** when output dimensions
   are stable (cached `GpuBuffer<float>` on a "render context"
   passed into `run_kernel_render`). Eliminates the ~3-10 MiB
   per-call allocator round-trip; helps interactive preview
   in the renderer server.
2. **Factor out `upload_demo_triangle_gas` helper** to
   collapse the §4.1 duplication once a third OptiX-triangle
   call site appears.
3. **Unify CUDA + OptiX triangle storage** by having
   `optixAccelBuild` consume `GpuMesh::device_vertices()`'s
   position field directly (set the OptiX `vertexStrideInBytes`
   to `sizeof(Vertex)`). Eliminates the §4.2 duplication
   without a host-side copy.
4. **Add a `cuda-memcheck --leak-check full` smoke run** to
   CI (or the manual validation script) for every CLI render
   action, replacing the source-level pairing audit with a
   runtime confirmation.
5. **Add a `GpuMesh::reset()`** that drops the device buffers
   without resetting metadata (today `GpuScene::reset_device`
   replaces the mesh with a default-constructed instance,
   which is fine but slightly wasteful for diagnostics).

None of these are required for correctness; the audit confirms
the current implementation has no leaks, no double-frees, and
no orphan allocations.

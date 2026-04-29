# GPU Memory Audit

Status: audit step 4. Focused only on `GpuBuffer` / CUDA
memory ownership, allocation / free, host-device
transfers, and error handling.

## 1. Surface area

The GPU memory layer is small and well-bounded:

```
   gpu/GpuBuffer.h         template <T> RAII handle       113 lines
   gpu/GpuBuffer.cpp       backend dispatch (RR_HAS_CUDA)   44 lines
   cuda/CudaBuffer.h       backend C-style API             23 lines
   cuda/CudaBuffer.cpp     cudaMalloc / cudaFree / cudaMemcpy x 2  54 lines
   cuda/CudaContext.cpp    cudaGetDeviceCount + props       42 lines
   gpu/GpuScene.cpp        per-section upload paths        344 lines
   cuda/CudaRenderer.cu    per-render alloc + sync + dl    348 lines
```

Three callable layers, top to bottom:

```
   GpuBuffer<T>     typed RAII handle                        (gpu/)
       |
       v
   detail::gpu_alloc / gpu_free / gpu_copy_*                 (gpu/)
       |
       v RR_HAS_CUDA -> cuda::cuda_alloc / cuda_free /       (cuda/)
                       cuda_copy_h2d / cuda_copy_d2h
                            |
                            v
                       cudaMalloc / cudaFree / cudaMemcpy    (CUDA Runtime)
```

No raw `cudaMalloc` call lives outside `cuda_alloc` (one
function, one place). Every device pointer exits the
backend through `GpuBuffer<T>::device_ptr()` which is
typed and owned.

## 2. Confirmed-safe behaviour

### 2.1 RAII ownership end-to-end

`GpuBuffer<T>` (`gpu/GpuBuffer.h:30-111`):

- Default-constructed = empty; no allocation.
- Destructor (line 37) calls `reset()`.
- `reset()` (line 94) calls `detail::gpu_free(ptr_)` then
  zeroes both members. Repeat-safe (the `if (ptr_)`
  guard).
- Move-only: copy ctor + copy assignment deleted
  (lines 39-40). Move ctor + move assignment null out
  the source's `ptr_` / `count_` (lines 42-57).

This is the textbook move-only RAII pattern. There is
no path through which a `GpuBuffer<T>` instance would
free the same `cudaMalloc` pointer twice or leak it
on normal control flow.

### 2.2 cudaMalloc / cudaFree are paired

`cuda/CudaBuffer.cpp`:

- `cuda_alloc(bytes)` (line 17): `cudaMalloc(&ptr,
  bytes)`. On failure, `clear_last_error()` + return
  `nullptr`. On `bytes == 0` returns `nullptr` without
  calling cudaMalloc (no zero-byte alloc on the GPU).
- `cuda_free(ptr)` (line 27): `noexcept`. `if (!ptr)
  return;` guards a free-of-null. On cudaFree failure,
  `clear_last_error()`. Errors are silently absorbed -
  acceptable for `noexcept` cleanup (the free either
  worked or we cannot recover from it).

The pairing is enforced by `GpuBuffer<T>`: `allocate`
calls `gpu_alloc`, the destructor / `reset` calls
`gpu_free`. The user never touches `gpu_alloc` /
`gpu_free` directly.

### 2.3 Memcpy paths are typed-byte-shuffled

`cuda_copy_h2d` / `cuda_copy_d2h` (`CudaBuffer.cpp:34,
44`):

- `if (bytes == 0) return true;` early-out.
- `if (!device_dst || !host_src) return false;` null
  guard.
- `cudaMemcpy(...)` -> on failure, `clear_last_error()`
  + return false.

`GpuBuffer<T>::upload(host_src, count)` (line 75) and
`download(host_dst, count)` (line 86) compute
`count * sizeof(T)` bytes with `count_` as the
authoritative element count. `download` rejects when
`count > count_`; `upload` re-allocates if the
existing size doesn't match.

Type safety: `GpuBuffer<T>` has a
`static_assert(std::is_trivially_copyable_v<T>)` on
line 32. The backend just shuffles bytes; non-trivial
types would silently corrupt because no constructors /
destructors run on the device.

### 2.4 No raw device pointer escapes

`device_ptr()` (lines 105-106) returns the raw `T*`
typed, but the buffer **continues to own** the
underlying allocation. The caller passes that pointer
into:

- `__global__` kernel arguments (e.g.
  `k_render_scene<<<...>>>(dev_pixels, ...)`).
- View structs (`CudaSceneView`,
  `CudaMaterialGraphView`, `TextureView`,
  `CudaAOVPack`) that the kernel reads.

In every case, the pointer is **borrowed**, not
transferred. The owning `GpuBuffer<T>` outlives the
kernel launch (followed by `cudaDeviceSynchronize`)
and the view that points into it. The view PODs go out
of scope after the host returns from `run_kernel_render`;
the `GpuBuffer`s go out of scope right after.

### 2.5 Error draining is consistent

CUDA's "sticky error" (`cudaGetLastError`) is drained
at three points in every render:

1. **Before launch**: `(void)cudaGetLastError();`
   (`CudaRenderer.cu:45` in `run_kernel_render`).
   Clears any leftover error from a previous call so
   a real failure isn't masked.
2. **After launch**: `cudaGetLastError() != cudaSuccess
   -> set message + return` (line 58, 318). Catches
   launch-time failures (block / grid out of range,
   invalid args).
3. **After sync**: `cudaDeviceSynchronize() !=
   cudaSuccess -> message + drain + return` (line 62,
   322). The `(void)cudaGetLastError()` after a sync
   error drain is defensive - a failed sync leaves the
   sticky bit set; clearing it lets the next render
   start clean.

`cuda_alloc` / `cuda_free` / `cuda_copy_*` each call
`clear_last_error()` on failure paths
(`CudaBuffer.cpp:21, 30, 38, 48`) for the same
"don't poison the next call" reason.

### 2.6 GpuScene owns the per-section buffers

`gpu/GpuScene.h:181-225` lists the owned buffers:

| Buffer                            | Type                                                | Purpose                                                |
|-----------------------------------|-----------------------------------------------------|--------------------------------------------------------|
| `spheres_`                        | `GpuBuffer<Sphere>`                                  | Sphere array                                           |
| `mesh_`                           | `GpuMesh` (owns its own `GpuBuffer`s)                | Vertices + indices                                     |
| `materials_`                      | `GpuBuffer<MaterialParams>`                          | Flat material array                                    |
| `graph_ops_`                      | `GpuBuffer<GpuOp>`                                   | M21 IR ops (concatenated across materials)             |
| `graph_terminals_`                | `GpuBuffer<GpuTerminal>`                             | M21 IR terminals (concatenated)                        |
| `material_graph_views_`           | `GpuBuffer<CudaMaterialGraphView>`                   | Per-material view records (one per material id)        |
| `lights_`                         | `GpuBuffer<Light>`                                   | Light array                                            |
| `texture_pixels_`                 | `std::vector<GpuBuffer<float>>`                      | One pixel buffer per texture                           |
| `texture_views_`                  | `GpuBuffer<TextureView>`                             | Packed texture view array                              |

All are direct members of `GpuScene` (no raw pointers).
On destruction the vector + buffer destructors fire in
reverse-construction order, freeing every device
allocation. On `reset()` paths (multiple in
`upload_*`), the buffers are explicitly cleared first
to avoid stale device data.

### 2.7 `texture_pixels_` lifetime invariant is sound

`GpuScene::upload_textures` (`GpuScene.cpp:156-225`)
populates two parallel arrays:

- `texture_pixels_` (vector of pixel `GpuBuffer<float>`).
- `texture_views_` (single `GpuBuffer<TextureView>`).

Each `TextureView` carries `image_data` = the device
pointer of the corresponding `texture_pixels_` entry
(`GpuScene.cpp:208`). The pixel buffers are stored
**before** the views (`push_back(std::move(pixels))`
on line 216), so the `device_ptr()` baked into the
view stays valid as long as the parallel pixel buffer
exists. Both arrays are owned by `*this`; their
destruction order (views first, pixels second per
member declaration order in `.h:202-204`) is fine
because the view records are PODs - they don't
dereference the pointers on destruction, the kernel
does at launch time.

### 2.8 Device buffers never overlap host lifetimes

`run_kernel_render` (`CudaRenderer.cu:36-77`) allocates
a `GpuBuffer<float> dev` on the stack. After the kernel
launch and `cudaDeviceSynchronize`, the device-to-host
download runs. Then the function returns; `dev` goes
out of scope and frees the device memory. **Single-
function lifetime; no leaks possible.**

The AOV variant (`render_aovs`, line 243) uses
`std::array<GpuBuffer<float>, kAOVCount> dev_bufs`
(line 300). Same pattern: stack-allocated, all six
buffers freed together when the array goes out of
scope.

## 3. Minor concerns

These are not bugs in the strict sense; they are
behaviours a reviewer of "production GPU code" would
flag. Listed in order of severity.

### 3.1 `cudaFree` failures swallowed silently

`cuda_free` (`CudaBuffer.cpp:27-32`) calls
`clear_last_error()` on failure but returns `void`.
The caller (`GpuBuffer::reset`) cannot tell a free
failed.

In practice `cudaFree` failures during normal use
are rare and almost always indicate a corrupt CUDA
context that's already unrecoverable. Still, the
rewrite should at minimum **log** a free failure
(so a corrupted context doesn't silently propagate)
and consider deferring to a top-level handler. The
current code's silence is consistent with the
`noexcept` declaration but loses a signal.

### 3.2 `kernel launch error` does not free the buffer

In `run_kernel_render`, the early-return paths after
launch / sync errors:

```cpp
   if (launch_err != cudaSuccess) {
       result.message = "kernel launch failed: " + ...;
       return result;       // <-- dev's destructor runs here
   }
```

The `dev` destructor DOES run (RAII), so the device
buffer is freed. But the **driver state** may be
unrecoverable - subsequent `cudaMalloc` calls in the
next render will likely fail too. The renderer has no
"reset CUDA context after fatal error" path. Right
now this manifests as a sticky-error cascade where
every subsequent render reports the same message. The
existing `(void)cudaGetLastError()` drain is enough
for transient errors but does not recover from a
corrupted context.

### 3.3 Empty allocations vs empty buffers

`cuda_alloc(0)` returns `nullptr` (line 18) - no
device call made. `GpuBuffer::allocate(0)` follows the
same pattern (line 65 returns `true` after `reset()`).
This is consistent + intentional, but it means
`allocate(0)` succeeds with `device_ptr() == nullptr`.
Callers that check `device_ptr() != nullptr` to mean
"buffer is ready" are technically wrong; the
authoritative check is `!empty()`. Code in the
renderer uses neither check directly - it passes the
pointer to the kernel which checks `count` separately
via the view. No bug today; a foot-gun for the
rewrite.

### 3.4 No `cudaSetDevice` discipline

The codebase never calls `cudaSetDevice`. CUDA
defaults to device 0 on first use. Multi-GPU machines
would silently render on whichever device the runtime
picked first. `cudaGetDeviceCount` is called in
`CudaContext::query_devices` for the
`--device-info` listing, but that listing does not
configure rendering.

For v1 single-machine consumer hardware this is
fine; the rewrite should add an explicit
`Config::cuda_device_index` knob and call
`cudaSetDevice` before any renderer-side allocation.

### 3.5 `GpuMesh::upload_from` not audited here

The `upload_mesh` path delegates to `GpuMesh`
(`gpu/GpuMesh.h:84` / `.cpp:56`). Out of scope for
this audit step's focused area; flagged for the
next-pass review. Visual inspection of `GpuMesh.cpp`
shows the same `GpuBuffer<T>::upload` pattern
(vertex buffer + index buffer), so the same
guarantees should apply.

### 3.6 Streams unused

Every `launch_*` host wrapper takes a `cudaStream_t
stream` parameter, defaulted to `nullptr` (the legacy
default stream). Today every render passes
`nullptr`. That serialises every launch + memcpy;
fine for the single-renderer case but blocks the
overlap-compute-with-PCIe optimisation a progressive
preview would want. Future concern, not a bug.

### 3.7 Pinned host memory not used

Host buffers passed to `cudaMemcpy` (the
`rr::image::Image::data()` pointer in
`run_kernel_render`'s download path) are pageable. CUDA
can DMA from pinned host memory without an internal
staging copy; with pageable memory the runtime stages
through pinned memory internally, which costs a
copy. For HD-or-larger frame downloads this matters;
v1's frame sizes are small enough that the cost is
absorbed. Future optimisation.

## 4. Violations / actual bugs

**None found.** The five questions:

| #  | Question                              | Result                                                   |
|----|---------------------------------------|----------------------------------------------------------|
| 1  | Memory leak risk?                     | None on normal control flow. RAII through `GpuBuffer<T>`. |
| 2  | Double free risk?                     | None. `reset()` is `if (ptr_)` guarded; move semantics null out the source.  |
| 3  | Unsafe raw pointers?                  | None. Every `cudaMalloc` pointer exits via `GpuBuffer<T>::device_ptr()`; views borrow it for the kernel call's duration only. |
| 4  | Missing error checks?                 | None on the rendering hot path. Every `cudaMalloc` / `cudaMemcpy` / `cudaDeviceSynchronize` is checked + drained. `cudaFree` failures are silenced (3.1). |
| 5  | Incorrect upload / download logic?    | None. Byte counts derive from `count * sizeof(T)`; download rejects oversize requests; null guards on both copy paths. |

The memory layer is **production-grade today**. The
rewrite can keep the `GpuBuffer<T>` + `cuda_*` backend
shape unchanged. The minor concerns in section 3 are
for the rewrite's polish pass:

- Log `cudaFree` failures (3.1).
- Add a "context recovery" path or at least a clear
  shutdown after fatal CUDA errors (3.2).
- Make `device_ptr() != nullptr` semantically
  equivalent to `!empty()` (3.3).
- Add an explicit `cudaSetDevice` call (3.4).
- Optionally use streams + pinned memory once the
  progressive workflow lands (3.6, 3.7).

None of these are blocking; the rewrite carries
forward the existing memory layer with confidence.

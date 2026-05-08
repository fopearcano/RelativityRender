# Stage 13 Audit B — GPU Upload

Date: 2026-04-30
Branch: `relativity-core-v1`
Last commit on the audited tree: `3fdec9a` ("stage 13 audit A:
files + build")
Scope: master order #18, sub-stages 13A / 13B.1 / 13B.2 / 13B.3.
This is **Audit B** of the Stage 13 audit family: GPU upload
mechanics only - allocation, host-to-device copy, and device
memory cleanup. Per the prompt, the audit lists file + function
names; it does **not** inspect sampling (that is a separate audit
slice) and does not inspect implementation behaviour beyond the
names of the entry points.
Mode: documentation-only. No source code is modified.

The audit answers the three prompt questions in order.

---

## 1. Is there GPU memory allocation for textures?

**PASS - allocation entry points exist and route through the
backend-agnostic byte allocator.**

Texture-specific surface (the type that owns the device memory):

- `src/gpu/GpuTexture.h` - declares `class rr::gpu::GpuTexture`
  with the upload entry points whose successful path performs
  the allocation:
  - `GpuTexture::upload(const std::byte* host_pixels,
    std::size_t pixel_bytes, int width, int height,
    rr::texture::ImageTextureFormat format)`
  - `GpuTexture::upload_from(const rr::texture::ImageTexture&
    src)`
- `src/gpu/GpuTexture.cpp` - definitions for the two entry
  points above.

Per-scene aggregation (the path materials reference into):

- `src/gpu/GpuScene.h` - declares
  `GpuScene::upload_textures(const rr::texture::ImageTexture*
  host, std::size_t count)`.
- `src/gpu/GpuScene.cpp` - definition of `GpuScene::
  upload_textures`. Per the prompt's scope this audit lists the
  function name; the per-entry allocation work delegates into
  `GpuTexture::upload_from`.

Underlying byte-level allocator (where the actual cudaMalloc
happens):

- `src/gpu/GpuBuffer.h` - declares the dispatch
  `rr::gpu::detail::gpu_alloc(std::size_t bytes)` and the
  templated `GpuBuffer<T>::allocate(std::size_t count)` that
  forwards to it.
- `src/gpu/GpuBuffer.cpp` - definition of
  `rr::gpu::detail::gpu_alloc`. Forwards to the CUDA backend
  when `RR_HAS_CUDA` is defined; returns `nullptr` otherwise.
- `src/cuda/CudaBuffer.h` - declares
  `rr::cuda::cuda_alloc(std::size_t bytes)`.
- `src/cuda/CudaBuffer.cpp` - definition of
  `rr::cuda::cuda_alloc` (the call site that issues `cudaMalloc`
  on a CUDA-enabled host).

`GpuTexture` holds a `rr::gpu::GpuBuffer<std::byte>` named
`pixels_`; that is the only field through which device memory
reaches the texture.

---

## 2. Is there host -> device copy for texture data?

**PASS - upload entry points routing the host pixel buffer to
the device exist at every layer.**

- `src/gpu/GpuTexture.h` /
  `src/gpu/GpuTexture.cpp`:
  - `GpuTexture::upload(...)` (raw form; bytes + dims + format)
  - `GpuTexture::upload_from(const rr::texture::ImageTexture&
    src)` (convenience form; forwards to `upload`)
- `src/gpu/GpuScene.h` /
  `src/gpu/GpuScene.cpp`:
  - `GpuScene::upload_textures(host, count)` - aggregates a
    batch of `ImageTexture` entries into the per-scene
    `std::vector<GpuTexture>`.
- `src/gpu/GpuBuffer.h`:
  - Templated method `GpuBuffer<T>::upload(const T* host_src,
    std::size_t count)` (the call `GpuTexture::upload(...)`
    invokes on its `pixels_` member).
  - Free function declaration
    `rr::gpu::detail::gpu_copy_host_to_device(void* device_dst,
    const void* host_src, std::size_t bytes)`.
- `src/gpu/GpuBuffer.cpp`:
  - Definition of
    `rr::gpu::detail::gpu_copy_host_to_device`. Forwards to the
    CUDA backend when `RR_HAS_CUDA` is defined; returns `false`
    otherwise.
- `src/cuda/CudaBuffer.h`:
  - Declaration of
    `rr::cuda::cuda_copy_h2d(void* device_dst, const void*
    host_src, std::size_t bytes)`.
- `src/cuda/CudaBuffer.cpp`:
  - Definition of `rr::cuda::cuda_copy_h2d` (the call site that
    issues `cudaMemcpy(..., cudaMemcpyHostToDevice)` on a
    CUDA-enabled host).

The audit does not inspect the byte counts, layout, or
correctness of the copy; the prompt asks only whether the path
exists, and the entry points above name it.

---

## 3. Is there device memory cleanup?

**PASS - explicit reset surfaces and RAII destructors exist at
every layer.**

Texture-level cleanup:

- `src/gpu/GpuTexture.h` /
  `src/gpu/GpuTexture.cpp`:
  - `GpuTexture::reset() noexcept` - the explicit "free device
    memory" entry point; declared in the header, defined in the
    .cpp.
  - Defaulted destructor `~GpuTexture()` declared in the header
    (line 30) - composes
    `GpuBuffer<std::byte>::~GpuBuffer()` via member RAII so a
    scoped `GpuTexture` cleans up automatically.

Per-scene cleanup that drops the texture set:

- `src/gpu/GpuScene.h` /
  `src/gpu/GpuScene.cpp`:
  - `GpuScene::reset_device() noexcept` - frees every device
    allocation owned by the scene, including the texture set.
  - `GpuScene::clear() noexcept` - calls `reset_device()` plus
    the host-side resets.
  - `GpuScene::upload_textures(host, 0)` - the documented
    "empty upload is a successful clear" path.

Underlying byte-level free:

- `src/gpu/GpuBuffer.h`:
  - `GpuBuffer<T>::reset() noexcept`
  - Destructor `~GpuBuffer() { reset(); }`
  - Free function declaration
    `rr::gpu::detail::gpu_free(void* device_ptr) noexcept`.
- `src/gpu/GpuBuffer.cpp`:
  - Definition of `rr::gpu::detail::gpu_free`. Forwards to the
    CUDA backend when `RR_HAS_CUDA` is defined; no-op otherwise.
- `src/cuda/CudaBuffer.h`:
  - Declaration of `rr::cuda::cuda_free(void* device_ptr)
    noexcept`.
- `src/cuda/CudaBuffer.cpp`:
  - Definition of `rr::cuda::cuda_free` (the call site that
    issues `cudaFree` on a CUDA-enabled host).

---

## Verdict

All three Audit B checks pass on the audited tree. Each upload
mechanic - allocation, host-to-device copy, device memory
cleanup - has named entry points at the texture layer
(`GpuTexture`), the scene layer (`GpuScene`), the byte-level
dispatch layer (`GpuBuffer` + `rr::gpu::detail::gpu_*`), and the
CUDA backend (`rr::cuda::cuda_alloc` / `cuda_copy_h2d` /
`cuda_free`). No layer is missing a function for any of the
three concerns.

What this audit does **not** answer (deferred to follow-up
slices):
- Whether the upload paths produce the correct device byte
  pattern (Audit C / output-image audit).
- Whether sampling reads from the uploaded data correctly
  (separate sampling audit; explicitly out of scope here).
- Whether move semantics, lifetime, or
  upload-then-failure recovery is correct in detail (deeper
  behavioural audit).
- Whether `output/gpu_texture_sample_test.ppm` and
  `output/gpu_textured_material.ppm` exist on a CUDA-enabled
  host (output audit; blocked in this environment).

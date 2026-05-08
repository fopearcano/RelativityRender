# Denoiser Memory Audit — Part B: Free Scan

Date: 2026-05-02
Stage: 19C.2.2
Scope: GPU memory frees on the denoiser execution path only.

This is a list-only enumeration. Pairing analysis (which
free matches which alloc, leak / double-free verification)
lives in subsequent 19C.2.x sub-stages.

---

## 1. Direct `cudaFree` / `cudaFreeAsync`

| # | File | Function | Free |
|---|------|----------|------|
| 1 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaFree(d_state)` (failure path: cudaMalloc(d_scratch) failed) |
| 2 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaFree(d_scratch)` (failure path: optixDenoiserSetup failed) |
| 3 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaFree(d_state)`   (failure path: optixDenoiserSetup failed) |
| 4 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaFree(d_scratch)` (failure path: optixDenoiserInvoke failed) |
| 5 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaFree(d_state)`   (failure path: optixDenoiserInvoke failed) |
| 6 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaFree(d_scratch)` (failure path: cudaDeviceSynchronize failed) |
| 7 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaFree(d_state)`   (failure path: cudaDeviceSynchronize failed) |
| 8 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaFree(d_scratch)` (success path) |
| 9 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaFree(d_state)`   (success path) |

No `cudaFreeAsync` calls on the denoiser path.

---

## 2. Indirect `cudaFree` (via `GpuBuffer<T>::reset` / `~GpuBuffer<T>` → `rr::gpu::detail::gpu_free` → `rr::cuda::cuda_free` → `cudaFree`)

| #  | File | Function | Free |
|----|------|----------|------|
| 10 | `src/main.cpp` | `denoise_aov_buffers_to_ppm` | `~GpuBuffer<float>()` on `denoised_dev` (scope-exit destructor) |
| 11 | `src/main.cpp` | `run_render_denoise`           | `~GpuAOVBuffer()` on `beauty_buf` (scope-exit destructor) |
| 12 | `src/main.cpp` | `run_render_denoise`           | `~GpuAOVBuffer()` on `normal_buf` (scope-exit destructor) |
| 13 | `src/main.cpp` | `run_render_denoise`           | `~GpuAOVBuffer()` on `albedo_buf` (scope-exit destructor) |
| 14 | `src/main.cpp` | `run_render_aovs`              | `~std::vector<GpuAOVBuffer>()` on `aov_set` (scope-exit destructor; calls `~GpuAOVBuffer()` on each of the six entries) |

---

## 3. OptiX buffer / object frees

| #  | File | Function | Free |
|----|------|----------|------|
| 15 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::shutdown` | `optixDenoiserDestroy(denoiser_)` (called by `~OptixDenoiser`, by explicit `shutdown()` calls, and by move-assignment's pre-steal `shutdown()`) |

`optixDenoiserSetup` and `optixDenoiserInvoke` (also in
`OptixDenoiser::invoke`) consume the buffers allocated in
Part A but do not free anything themselves.

---

## 4. Out of scope (excluded from this scan)

The following are not GPU buffer frees and are listed
here only so a future reviewer does not chase them as
omissions:

- `delete[] static_cast<::OptixImage2D*>(input_images_)`
  in `OptixDenoiser::set_inputs` (the rebind path) and
  `OptixDenoiser::shutdown` (host-side descriptor array
  release, not a GPU buffer).
- `cudaEventDestroy` calls in `rr::gpu::GpuTimer`'s
  destructor / move ops used by the denoiser path
  (events, not buffers).
- The host `std::vector<float> host_rgb` in
  `denoise_aov_buffers_to_ppm` (host vector destructor).
- `OptixBackend::shutdown`'s `optixDeviceContextDestroy`
  (device context, not a buffer).

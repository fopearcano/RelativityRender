# Denoiser Memory Audit — Part A: Allocation Scan

Date: 2026-05-02
Stage: 19C.2.1
Scope: GPU memory allocations on the denoiser execution path only.

This is a list-only enumeration. Analysis (ownership / pairing
/ leak / scratch sizing) lives in subsequent 19C.2.x sub-stages.

---

## 1. Direct `cudaMalloc` / `cudaMallocAsync`

| # | File | Function | Allocation |
|---|------|----------|------------|
| 1 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaMalloc(&d_state, sizes.stateSizeInBytes)` |
| 2 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::invoke` | `cudaMalloc(&d_scratch, sizes.withoutOverlapScratchSizeInBytes)` |

No `cudaMallocAsync` calls on the denoiser path.

---

## 2. Indirect `cudaMalloc` (via `GpuBuffer<T>::allocate` → `rr::gpu::detail::gpu_alloc` → `rr::cuda::cuda_alloc` → `cudaMalloc`)

| # | File | Function | Allocation |
|---|------|----------|------------|
| 3 | `src/main.cpp` | `denoise_aov_buffers_to_ppm` | `denoised_dev.allocate(out_float_count)` (denoiser FLOAT3 output buffer) |
| 4 | `src/main.cpp` | `run_render_denoise`           | `beauty_buf.resize(cfg.width, cfg.height)` |
| 5 | `src/main.cpp` | `run_render_denoise`           | `normal_buf.resize(cfg.width, cfg.height)` |
| 6 | `src/main.cpp` | `run_render_denoise`           | `albedo_buf.resize(cfg.width, cfg.height)` |
| 7 | `src/main.cpp` | `run_render_aovs`              | `aov_set[i].resize(cfg.width, cfg.height)` for `i ∈ [0..5]` (six AOV buffers; Beauty / Normal / Albedo are the three consumed by the denoiser when `--denoise` is set) |

---

## 3. OptiX buffer / object allocations

| # | File | Function | Allocation |
|---|------|----------|------------|
| 8 | `src/optix/OptixDenoiser.cpp` | `OptixDenoiser::initialize` | `optixDenoiserCreate(ctx, kModel, &opts, &denoiser)` (creates the `OptixDenoiser` handle plus its SDK-internal device-side resources) |

`optixDenoiserSetup` and `optixDenoiserInvoke` (also in
`OptixDenoiser::invoke`) are launches against the buffers
allocated above; they do not introduce new buffer allocations.

---

## 4. Out of scope (excluded from this scan)

The following are not GPU buffer allocations and are listed
here only so a future reviewer does not chase them as
omissions:

- `new (std::nothrow) ::OptixImage2D[3]` in
  `OptixDenoiser::set_inputs` (host-side descriptor array).
- `cudaEventCreate` calls in `rr::gpu::GpuTimer`'s
  constructors used by the denoiser path (events, not
  buffers).
- `std::vector<float> host_rgb` in
  `denoise_aov_buffers_to_ppm` (host allocation).
- `OptixBackend::initialize`'s `optixDeviceContextCreate`
  (device context, not a buffer).

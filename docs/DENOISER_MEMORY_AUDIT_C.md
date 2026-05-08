# Denoiser Memory Audit — Part C: Mismatch Check

Date: 2026-05-02
Stage: 19C.2.3
Scope: pair Part A (allocations) with Part B (frees).

- **Any allocation without obvious free?** No.
- **Any duplicate allocation?** No.
- A.1–A.2 (cudaMalloc state / scratch) → B.1–B.9 (9 cudaFree
  sites; success + 4 failure paths).
- A.3–A.7 (indirect cudaMalloc via GpuBuffer / GpuAOVBuffer)
  → B.10–B.14 (RAII destructors at the caller's scope exit).
- A.8 (optixDenoiserCreate) → B.15 (optixDenoiserDestroy in
  `OptixDenoiser::shutdown`).

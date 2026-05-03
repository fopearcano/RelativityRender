# Denoiser Plan

## Purpose

- Reduce per-pixel Monte Carlo noise from the GPU path tracer.
- Enable usable images at low samples-per-pixel so the renderer is responsive in interactive workflows.
- Improve the usability of renderer output for previews, server requests, and DCC iteration cycles.
- Preserve relativistic shading cues (aberration, Doppler, searchlight) by consuming auxiliary AOV guides rather than smoothing them away.
- Stay strictly post-process: the denoiser does not replace bounce budget, fix shading bugs, or substitute for tone mapping.

## Backend

- OptiX denoiser is the primary (and only) backend for v1.0.
- Requires the OptiX SDK at build time (`-DRR_ENABLE_OPTIX=ON` plus a discoverable `OPTIX_ROOT`).
- The CUDA renderer remains independent: the denoiser is a sibling pipeline stage, not a CUDA dependency, and the CUDA path keeps building and running with `RR_ENABLE_OPTIX=OFF`.
- No alternative / fallback denoiser is required for v1.0 (no NVIDIA NRD, no Intel Open Image Denoise, no in-house variant).
- When OptiX is unavailable at runtime the renderer surfaces a clear "denoiser requires OptiX" error and continues to produce noisy AOVs.

## Required inputs

- Beauty (noisy linear-RGB radiance) — the path tracer's per-pixel estimate, sourced from the Stage 14 `AOVType::Beauty` buffer.
- Albedo (linear RGB, base colour at hit before lighting) — sourced from the Stage 14 `AOVType::Albedo` buffer.
- Normal (per-pixel shading normal) — sourced from the Stage 14 `AOVType::Normal` buffer.
- All three come from the Stage 14 AOV pipeline (`rr::renderer::GpuAOVBuffer`), populated by the renderer's AOV-aware launch (`CudaRenderer::render_scene_with_aovs` for the CUDA path; the Stage 20N `OptixRenderer::render_aovs` for the OptiX path).
- All three are mandatory: missing any of them is a denoiser configuration error, not a degraded mode.

## Optional inputs

- Depth (per-pixel hit distance) — already produced by the Stage 14 `AOVType::Depth` buffer; reserved for a future denoiser variant that consumes a depth guide.
- Motion vectors (per-pixel screen-space delta to previous frame) — not produced by any current AOV; would require a new `AOVType::Motion` plus per-frame camera/scene state for temporal denoising.
- Neither is required for the v1.0 implementation; the OptiX HDR model used today (`OPTIX_DENOISER_MODEL_KIND_HDR`) consumes only the three mandatory inputs.
- Adding either input is purely additive: new AOV slot, no changes to the Beauty / Albedo / Normal contract.
- Temporal denoising (which would need motion vectors) is explicitly out of scope for v1.0.

## Pipeline

- Stage order: `render → AOV buffers → denoiser → final image`.
- The denoiser runs strictly after GPU rendering: the renderer finishes its launch and `cudaDeviceSynchronize`s its AOV buffers before the denoiser is invoked.
- The denoiser reads the existing AOV device pointers in place; no extra copy or upload between render and denoise.
- Core renderer logic is not modified: no kernel changes, no SBT changes, no path-tracer changes — the denoiser is a separate pipeline stage layered on top.
- The denoiser's output is the final image written to disk; the pre-denoise Beauty AOV remains available as a fallback / debug artifact.

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

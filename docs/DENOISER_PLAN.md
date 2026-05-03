# Denoiser Plan

## Purpose

- Reduce per-pixel Monte Carlo noise from the GPU path tracer.
- Enable usable images at low samples-per-pixel so the renderer is responsive in interactive workflows.
- Improve the usability of renderer output for previews, server requests, and DCC iteration cycles.
- Preserve relativistic shading cues (aberration, Doppler, searchlight) by consuming auxiliary AOV guides rather than smoothing them away.
- Stay strictly post-process: the denoiser does not replace bounce budget, fix shading bugs, or substitute for tone mapping.

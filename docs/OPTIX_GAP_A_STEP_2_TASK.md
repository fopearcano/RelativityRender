# OptiX Gap A — Step 2 Task

Source: `docs/OPTIX_GAP_A_POLISH_PLAN.md` §4 ("Minimal
implementation steps"), Step 2.
Predecessor: Step 1 (types + declaration) shipped at
commit `6287471` ("optix gap A step 1: AovRetainedBuffers
+ render_aovs_retain (types + decl)").
Mode: documentation-only. No source code is modified by
this task file.

---

## 1. Step 2 name

**SDK_FOUND body** — implement the
`#ifdef RELATIVITYRENDER_OPTIX_SDK_FOUND` branch of
`OptixRenderer::render_aovs_retain(scene, lights, width,
height)` so the function actually runs the OptiX launch
and returns the populated `AovRetainedBuffers` struct
instead of the Step-1 "not implemented" stub.

## 2. Short description

Replace the Step-1 stub body of `render_aovs_retain`'s
SDK_FOUND branch with the launch + buffer-retention
sequence per `docs/OPTIX_GAP_A_POLISH_PLAN.md` Step 2:
allocate three `GpuBuffer<float>` instances (Beauty /
Albedo / Normal) instead of raw `cudaMalloc`, run the
same OptiX launch the existing `render_aovs` already
runs, transfer buffer ownership into the
`AovRetainedBuffers` result, and skip the host-side
download (the buffers stay device-resident for the
denoiser). The existing `render_aovs` stays byte-
identical for backward compat (either via the
duplicate-then-refactor path or the refactor-then-share
path documented in the plan).

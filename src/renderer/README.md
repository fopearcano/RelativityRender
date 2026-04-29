# `src/renderer/` — Render passes / AOVs / integrator

**Status (relativity-core-v1, day-1): scaffold only — no code yet.**

The prototype's `AOV` foundation and `Hit` POD come back in their own
slice. The integrator that lived in `cuda/CudaTestKernel.cu` will move
into this module (or `pathtracer/`) per the architecture audit, instead
of being smuggled inside the CUDA backend.

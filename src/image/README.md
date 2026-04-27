# `src/image/` — Image / Framebuffer System

**Layer:** L1. **Milestone:** M4. **Status:** not started.

Host-side image data structures: pixel formats, framebuffers, accumulation
buffers, tile descriptors, tone-mapping primitives. Image IO (EXR, PNG)
lives next door under `src/io/`.

Depends on Math + Core. Must not depend on GPU backends or renderer
algorithms; GPU mirrors live in the CUDA Backend.

See `docs/MODULE_MAP.md` for the authoritative contract.

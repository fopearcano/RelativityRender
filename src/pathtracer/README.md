# `src/pathtracer/` — Path Tracer

**Layer:** L5. **Milestone:** M14. **Status:** not started.

Unidirectional path-tracing integrator: BSDF sampling, NEE + MIS, Russian
roulette, hit handling, BSDF/light interaction loop. Supports both CUDA-only
and OptiX-driven dispatch.

Depends on the GPU backends and the rendering domain (geometry, material,
texture, lighting, camera, relativistic camera, image). Must not depend on
UI, Cinema 4D, the renderer server, or the bridge — the path tracer does
not know who is asking it to render.

See `docs/MODULE_MAP.md` for the authoritative contract.

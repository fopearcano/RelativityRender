# `src/renderer/` — Renderer Glue (Progressive, AOVs, Denoiser)

**Layer:** L5. **Milestone:** M14 onward (progressive in M14, AOVs in M17,
denoiser in M22). **Status:** not started.

The glue layer that drives the path tracer:

- Progressive render sessions (sample budgeting, convergence, refresh hooks).
- Render passes / AOVs registry — including the relativistic AOVs
  (Doppler factor, observed direction, retarded time, frame velocity).
- Denoiser integration (OptiX denoiser / OIDN wrappers, AOV preparation).

Depends on the path tracer, image, and AOV channels. Must not depend on UI
or Cinema 4D — progress is reported through callbacks and buffers, not by
calling UI.

See `docs/MODULE_MAP.md` for the authoritative contract.

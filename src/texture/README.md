# `src/texture/` — Texture System

**Layer:** L4. **Milestone:** M16. **Status:** not started.

2D / 3D texture storage, sampler descriptors (wrap, filter, MIP), UDIM,
texture cache, baked textures, procedural inputs that fan into BSDF
parameters.

Depends on Math, Image, and the GPU Device Layer. Must not depend on the
path tracer or UI.

See `docs/MODULE_MAP.md` for the authoritative contract.

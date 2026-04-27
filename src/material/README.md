# `src/material/` — Material / Shading System

**Layer:** L4. **Milestone:** M11. **Status:** not started.

BSDF interface; concrete BSDFs (Lambert, GGX, dielectric, layered);
parameter binding (constants + textures); shader evaluation callable on
device; `eval / sample / pdf` triple per BSDF.

The path tracer calls into materials, never the other way around. Must not
depend on UI, Cinema 4D, or the node editor.

See `docs/MODULE_MAP.md` for the authoritative contract.

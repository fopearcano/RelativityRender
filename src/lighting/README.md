# `src/lighting/` — Lighting System

**Layer:** L4. **Milestone:** M12. **Status:** not started.

Light types (point, directional, area, environment), emissive-mesh
linkage, importance sampling, light tree / power-based selection.

Depends on Math, Scene Graph, Texture (for env maps), and Geometry (for
emissive triangles). Must not depend on the path tracer or UI.

See `docs/MODULE_MAP.md` for the authoritative contract.

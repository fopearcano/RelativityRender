# `src/geometry/` — Geometry System

**Layer:** L4. **Milestone:** M10. **Status:** not started.

Triangle meshes, instancing data, attribute layouts (position / normal / uv /
tangent), GPU-uploadable forms (interleaved or SoA), AS build inputs for
OptiX. Curves and volumes are placeholders for later milestones.

Depends on Math, Scene Graph, and the GPU Device Layer (for upload
abstractions only). Must not depend on the path tracer or the renderer
server.

See `docs/MODULE_MAP.md` for the authoritative contract.

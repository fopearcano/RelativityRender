# `src/scene/` — Scene Graph

**Layer:** L4. **Milestone:** M10. **Status:** not started.

Scene node hierarchy, transforms (local + world), node references to
geometry / material / light / camera resources, traversal helpers, scene
bounds, scene IDs.

Pure host data. Must not depend on GPU backends, the path tracer, UI, or
Cinema 4D. The Scene File Format under `src/io/` depends on Scene Graph,
not the other way around.

See `docs/MODULE_MAP.md` for the authoritative contract.

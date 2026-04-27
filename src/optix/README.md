# `src/optix/` — OptiX Backend

**Layer:** L3. **Milestone:** M15. **Status:** not started.

OptiX context, modules, program groups, pipelines, SBT management,
acceleration-structure builds (GAS / IAS), ray-scheduling helpers. Built on
top of `src/cuda/`.

OptiX consumes geometry already prepared by `src/geometry/`; it does not poke
at scene-graph internals.

See `docs/MODULE_MAP.md` for the authoritative contract.

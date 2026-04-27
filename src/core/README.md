# `src/core/` — Core Engine

**Layer:** L0. **Milestone:** M2. **Status:** not started.

Foundational services every other module relies on: application lifecycle,
logging, config, error types, file IO primitives, time, threading helpers.

Core is *beneath* the renderer. It must not depend on Math, GPU backends,
Scene Graph, Path Tracer, UI, or Cinema 4D.

See `docs/MODULE_MAP.md` for the authoritative contract.

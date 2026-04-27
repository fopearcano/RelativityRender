# `src/server/` — Renderer Server

**Layer:** L6. **Milestone:** M18. **Status:** not started.

Long-running render service. Listens on IPC / network, accepts scene
payloads (in the format defined under `src/io/`), launches the path
tracer / progressive renderer, streams framebuffers and AOVs back, manages
cancellation and multi-job queuing.

The server has **no idea** what its clients are. It must not depend on
UI or Cinema 4D. The Cinema 4D Bridge under `integrations/c4d/` is a
client of this server, not the other way around.

See `docs/MODULE_MAP.md` for the authoritative contract.

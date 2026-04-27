# `integrations/c4d/` — Cinema 4D integration

**Milestones:** M19 (bridge plugin), M23 (native Cinema 4D renderer
registration). **Status:** not started.

This is the only place in the repository allowed to link the Cinema 4D
SDK.

Two planned components:

- **Bridge plugin (M19):** observes the C4D scene, translates it into
  scene file format, and talks to the renderer server over its
  protocol. Handles preview frames, parameter sync, and progressive
  updates on the C4D side. **Does not** link renderer internals.
- **Native renderer (M23):** registers RelativityRender as a Cinema 4D
  renderer / video post. Drives the renderer in-process via the public
  renderer API and the scene file format — still no internal headers.

Both components depend only on:

- The Cinema 4D SDK.
- `src/io/` (scene file format).
- The renderer server protocol (for the bridge) or the renderer's
  public façade (for the native registration).

Forbidden imports: anything else under `src/`, anything under `tools/`.

See `docs/MODULE_MAP.md` and `docs/DEVELOPMENT_RULES.md` for the
authoritative rules.

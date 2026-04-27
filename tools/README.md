# `tools/` — Standalone tools

UI-side and developer tools that consume the renderer rather than extend
it. Empty at M1.

Planned residents:

- **Preview UI** (M20): a standalone viewer that connects to the renderer
  server and displays progressive frames, AOVs, and relativistic debug
  channels.
- **Material Node Graph editor** (M21): authors materials and emits scene
  file format material blocks. Pure UI; no renderer internals.

Tools belong to layer L7. **Nothing in `src/` may depend on `tools/`.**
Tools depend on the renderer through public formats and protocols only —
never internal headers.

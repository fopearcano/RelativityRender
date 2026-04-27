# `integrations/` — DCC integrations

Plugins that integrate RelativityRender with external Digital Content
Creation applications. Empty at M1.

Planned residents:

- **`c4d/`** — Cinema 4D bridge (M19) and, later, a native Cinema 4D
  renderer registration (M23).

Hard rules:

- Integrations are **clients** of the renderer. They depend on the scene
  file format and the renderer server protocol — never on internal
  renderer code.
- Cinema 4D SDK headers / libs are linked **only** from this directory.
  No other module is allowed to know that Cinema 4D exists.
- Nothing under `src/` may depend on anything under `integrations/`.

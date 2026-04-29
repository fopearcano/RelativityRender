# `integrations/` — DCC integrations

Plugins that integrate RelativityRender with external Digital Content
Creation applications.

**Status (relativity-core-v1, day-1): empty.** No integrations are
built in the current branch. Future residents:

- **`c4d/`** — native Cinema 4D `VideoPostData` plugin. Many slices
  away (after GPU renderer + OptiX + denoiser).

Hard rules (will apply once integrations land):

- Integrations are **clients** of the renderer. They depend on the
  scene file format and the renderer server protocol — never on
  internal renderer code.
- Cinema 4D SDK headers / libs are linked **only** from this directory.
- Nothing under `src/` may depend on anything under `integrations/`.

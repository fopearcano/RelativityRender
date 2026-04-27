# `src/io/` — Image IO and Scene File Format

**Layer:** L1 / L6. **Milestone:** M4 (image IO), M13 (scene format).
**Status:** not started.

Two related responsibilities living side by side:

- **Image IO:** EXR / PNG load / save, multi-channel EXR for AOVs.
- **Scene File Format:** the canonical on-disk and over-the-wire scene
  description, versioned, with a parser and serializer. The contract
  between authoring tools and the renderer.

Depends on Image, Math, Scene Graph, Core. Must not depend on GPU
backends, the path tracer, UI, or Cinema 4D — IO is data only.

See `docs/MODULE_MAP.md` for the authoritative contract.

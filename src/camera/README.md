# `src/camera/` — Camera System

**Layer:** L4. **Milestone:** M7. **Status:** not started.

Classical camera types (perspective, orthographic, thin-lens DOF, motion
blur sampling), screen-space and lens-space sampling, primary-ray
generation in the non-relativistic frame.

The Relativistic Camera Model in `src/relativity/` wraps Camera; Camera
itself does not know about the relativistic layer.

See `docs/MODULE_MAP.md` for the authoritative contract.

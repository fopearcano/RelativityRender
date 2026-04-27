# `src/gpu/` — GPU Device Layer

**Layer:** L2. **Milestone:** M5. **Status:** not started.

Backend-agnostic GPU abstraction: device enumeration, streams, events,
typed buffers, kernel-launch descriptors, error wrapping.

Concrete implementations live in `src/cuda/` and `src/optix/`. This module
defines interfaces only; it ships no kernels.

See `docs/MODULE_MAP.md` for the authoritative contract.

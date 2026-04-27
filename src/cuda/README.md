# `src/cuda/` — CUDA Backend

**Layer:** L3. **Milestone:** M5 / M6. **Status:** not started.

Concrete CUDA implementation of `src/gpu/`: device wrappers, streams, pinned
host memory, device buffers, async copies, error mapping, reusable kernel
utilities (reduction, atomic accumulation), CUDA build glue.

CUDA Backend is **below** OptiX Backend. CUDA may not depend on OptiX.

See `docs/MODULE_MAP.md` for the authoritative contract.

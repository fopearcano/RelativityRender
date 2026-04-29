# `src/material/` — Materials

**Status (relativity-core-v1, day-1): scaffold only — no code yet.**

The prototype's two parallel material implementations
(`material/MaterialGraph.{h,cpp}` and `material/graph/`) are not
copied across day-1. The new `material/graph/` data core + `GpuMaterial`
GPU IR survive into the rewrite per `docs/REUSE_PLAN.md` and come back
in their own slice when there's a renderer that consumes them.

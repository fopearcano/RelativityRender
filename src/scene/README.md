# `src/scene/` — Scene container

**Status (relativity-core-v1, day-1): scaffold only — no code yet.**

The prototype's host-side `Scene` + `SceneObject` PODs come back when
the renderer can consume real geometry. The legacy `Transform.h`
back-compat shim is **not** ported; the rewrite uses `math::Transform`
directly from day one.

# `src/io/` — Scene IO

**Status (relativity-core-v1, day-1): scaffold only — no code yet.**

The prototype's `SceneLoader` / `SceneWriter` were 800+ lines of
hand-rolled JSON. The rewrite reintroduces this module with a real
JSON library (e.g. nlohmann) once the renderer needs to consume
authored scenes. Day-1 has no scene to load.

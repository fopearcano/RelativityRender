# `src/server/` — Render server

**Status (relativity-core-v1, day-1): scaffold only — no code yet.**

The prototype's `RenderServer` v1 (one-client-at-a-time line protocol
on 127.0.0.1:7777) is preserved on the `prototype_v0` tag. The rewrite
brings back the protocol shape in a hardened v2 (multi-client, binary
AOV streaming, EXR, cancellation, progress) once the renderer produces
content worth streaming.

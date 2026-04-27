# `third_party/` — Vendored / fetched dependencies

Empty at M1.

This is where third-party code lives — either vendored (committed) or
fetched at configure time via CMake (`FetchContent` / `ExternalProject`).
Each dependency is added deliberately, in the milestone that needs it.

Anticipated future residents:

- A logging library (M2).
- A small JSON or TOML parser (M2 / M13).
- OpenEXR + Imath, stb-image (M4).
- A test framework, e.g. Catch2 or doctest (M2 / M3).
- A serialization library for the scene file format (M13).
- An IPC / messaging library for the renderer server (M18).

Rules:

- No drive-by additions. Each dependency must be justified in
  `docs/BUILD_PLAN.md`.
- Pinned versions only.
- Third-party code must not leak through public headers of renderer
  modules.

# `src/` — RelativityRender source modules

Each subdirectory is one module from `docs/MODULE_MAP.md`. Modules are
implemented one at a time, in the order defined by
`docs/MILESTONE_ROADMAP.md`. None of these modules contain code yet — only
placeholder READMEs.

| Directory       | Module                          | Layer | Milestone |
|-----------------|---------------------------------|-------|-----------|
| `core/`         | Core Engine                     | L0    | M2        |
| `math/`         | Math Library                    | L1    | M3        |
| `image/`        | Image / Framebuffer System      | L1    | M4        |
| `gpu/`          | GPU Device Layer                | L2    | M5        |
| `cuda/`         | CUDA Backend                    | L3    | M5 / M6   |
| `optix/`        | OptiX Backend                   | L3    | M15       |
| `scene/`        | Scene Graph                     | L4    | M10       |
| `geometry/`     | Geometry System                 | L4    | M10       |
| `material/`     | Material / Shading System       | L4    | M11       |
| `texture/`      | Texture System                  | L4    | M16       |
| `lighting/`     | Lighting System                 | L4    | M12       |
| `camera/`       | Camera System                   | L4    | M7        |
| `relativity/`   | Relativistic Camera Model       | L4    | M9        |
| `pathtracer/`   | Path Tracer                     | L5    | M14       |
| `renderer/`     | Progressive Renderer / AOVs / Denoiser glue | L5 | M14+ |
| `io/`           | Image IO + Scene File Format    | L1/L6 | M4 / M13  |
| `server/`       | Renderer Server                 | L6    | M18       |

The renderer core (everything in `src/`) MUST NOT depend on UI or Cinema 4D.
See `docs/MODULE_MAP.md` for the per-module forbidden lists.

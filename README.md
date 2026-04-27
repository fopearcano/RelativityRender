# RelativityRender

A serious GPU renderer platform with a unique relativistic camera / perception
model.

RelativityRender is CUDA/OptiX-first, designed around GPU path tracing and
ray-level relativistic perception (aberration, Doppler color shift,
searchlight/headlight beaming, Lorentz-style perception, retarded-time
approximation). It targets standalone use first, with a renderer server, and
a Cinema 4D bridge built on top of that server.

## Status

**Pre-alpha. Repository skeleton only (milestone M1).**

No renderer code has been written yet. The directory layout is in place; each
module ships behind its own milestone in
[`docs/MILESTONE_ROADMAP.md`](docs/MILESTONE_ROADMAP.md).

## Documentation

Read these in order:

1. [`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`](RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt) — top-level rules.
2. [`docs/MASTER_ARCHITECTURE.md`](docs/MASTER_ARCHITECTURE.md) — identity, layered architecture, the 22 long-term modules.
3. [`docs/MODULE_MAP.md`](docs/MODULE_MAP.md) — per-module ownership, dependencies, forbidden imports.
4. [`docs/DEVELOPMENT_RULES.md`](docs/DEVELOPMENT_RULES.md) — engineering, dependency, GPU, and process rules.
5. [`docs/MILESTONE_ROADMAP.md`](docs/MILESTONE_ROADMAP.md) — M0 through M23.
6. [`docs/BUILD_PLAN.md`](docs/BUILD_PLAN.md) — current build state and the next concrete step.

## Repository layout

```
RelativityRender/
  CMakeLists.txt          # Top-level build (host-only at M1)
  README.md               # this file
  docs/                   # architecture, rules, roadmap, build plan
  src/
    core/                 # application lifecycle, logging, config, IO
    math/                 # pure math leaf (vec/mat/quat/ray/aabb/sampling)
    image/                # framebuffers, accumulation buffers, pixel formats
    gpu/                  # backend-agnostic GPU abstraction
    cuda/                 # CUDA backend (concrete GPU impl + kernels)
    optix/                # OptiX backend (AS, SBT, pipelines)
    scene/                # scene graph (host data only)
    geometry/             # triangle meshes, instancing, AS build inputs
    material/             # BSDFs, shading, parameter binding
    texture/              # 2D/3D textures, samplers, UDIM
    lighting/             # lights and importance sampling
    camera/               # classical cameras, primary ray generation
    relativity/           # relativistic camera / perception model
    renderer/             # progressive renderer, AOVs, denoiser glue
    pathtracer/           # path-tracing integrator (GPU)
    io/                   # image IO and scene-file parsing/serialization
    server/               # renderer server (IPC / network protocol)
  tests/                  # unit + integration tests
  tools/                  # standalone tools (preview UI, node editor, ...)
  integrations/
    c4d/                  # Cinema 4D bridge / native renderer
  third_party/            # vendored or fetched dependencies
```

## Building (current state)

The build configures cleanly but does not yet compile any targets. Modules are
added as their milestones land. To configure:

```sh
cmake -S . -B build
cmake --build build
```

Optional toggles:

- `-DRR_ENABLE_CUDA=ON` — enable the CUDA backend (requires CUDA Toolkit).
- `-DRR_ENABLE_OPTIX=ON` — enable the OptiX backend (requires OptiX SDK).
- `-DRR_BUILD_TESTS=ON` — build tests (default on).
- `-DRR_BUILD_TOOLS=ON` — build standalone tools.
- `-DRR_BUILD_INTEGRATIONS=ON` — build DCC integrations (e.g. Cinema 4D).

## License

To be determined.

# RelativityRender

A serious GPU renderer platform with a unique relativistic camera /
perception model.

RelativityRender is CUDA/OptiX-first, designed around GPU path tracing
and ray-level relativistic perception (aberration, Doppler color
shift, searchlight beaming).

## Status

This is **`relativity-core-v1`** — the rewrite branch. Day-1 capability:

- CUDA detection (enumerate visible CUDA devices, or honestly report
  no backend).
- A single GPU diagnostic kernel that renders a UV gradient and saves
  it as PPM.

That is the entire intended capability of day-1. Everything else is
deferred to dedicated slices listed in
[`docs/REWRITE_STATUS.md`](docs/REWRITE_STATUS.md).

The earlier prototype is preserved on the `prototype_v0` git tag and
should be treated as a frozen reference, not as in-progress work.

## Build

Host-only (always works, no CUDA needed):

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
build/bin/RelativityRender --detect
```

CUDA-enabled (requires CUDA Toolkit + a CUDA-capable GPU):

```sh
cmake -S . -B build -DRR_ENABLE_CUDA=ON
cmake --build build -j
build/bin/RelativityRender --detect
build/bin/RelativityRender --render-gradient 256x256 gradient.ppm
```

## Documentation

1. [`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`](RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt) — top-level rules.
2. [`docs/REWRITE_STATUS.md`](docs/REWRITE_STATUS.md) — what is reused, rewritten, discarded; slice roadmap.
3. [`docs/PROTOTYPE_REUSE_AUDIT.md`](docs/PROTOTYPE_REUSE_AUDIT.md) — per-file decision table from the prototype audit.
4. [`docs/REUSE_PLAN.md`](docs/REUSE_PLAN.md) — migration strategy and minimum-safe starting point.
5. [`docs/MASTER_ARCHITECTURE.md`](docs/MASTER_ARCHITECTURE.md) — long-term layered architecture.
6. [`docs/MILESTONE_ROADMAP.md`](docs/MILESTONE_ROADMAP.md) — long-term milestones.
7. [`docs/DEVELOPMENT_RULES.md`](docs/DEVELOPMENT_RULES.md) — engineering, dependency, GPU, process rules.

## Layout

```
RelativityRender/
  CMakeLists.txt            # ~120 lines, per-module helpers
  README.md
  docs/                     # foundational docs + audit + status
  src/
    core/                   # Logger, Version (only)
    math/                   # Vec*, Mat4, Transform, MathUtils (RR_HD)
    image/                  # Image, Color
    gpu/                    # GpuBuffer<T>, GpuDevice
    cuda/                   # CudaContext, CudaBuffer, CudaRenderer (gradient), CudaGradientKernel.cu
    scene/                  # scaffold (empty)
    geometry/               # scaffold (empty)
    material/               # scaffold (empty)
    lighting/               # scaffold (empty)
    camera/                 # scaffold (empty)
    relativity/             # scaffold (empty)
    renderer/               # scaffold (empty)
    io/                     # scaffold (empty)
    server/                 # scaffold (empty)
  tests/                    # math_tests, image_tests, gpu_tests
  integrations/c4d/         # scaffold (empty)
```

The renderer core (everything in `src/`) MUST NOT depend on UI or
Cinema 4D. See `docs/REWRITE_STATUS.md` for the slice roadmap.

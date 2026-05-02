# RelativityRender

A serious GPU renderer platform with a unique relativistic camera /
perception model. CUDA / OptiX-first, designed around GPU path tracing
and ray-level relativistic perception (aberration, Doppler color
shift, searchlight beaming).

## Status

The project is built incrementally per the 25-step development order
in
[`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`](RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt).
The per-slice status table — which is the source of truth for what
is implemented and what is not — lives in
[`docs/BUILD_PLAN.md`](docs/BUILD_PLAN.md).

Implementation has reached the denoising slice (master order #24).
The standalone-renderer prerequisites for Cinema 4D integration —
CUDA device detection, GPU framebuffer output, GPU camera rays, GPU
primitive intersection, GPU relativistic camera model, GPU scene
upload, GPU triangle mesh, basic materials, basic lights, scene
parser, renderer server — are all landed. Cinema 4D integration
itself (master order #21) has not started; see
[`docs/ROADMAP_AUDIT.md`](docs/ROADMAP_AUDIT.md) for the full
audit of how the implemented order relates to the master 25-step
list.

Capabilities currently in the tree (see BUILD_PLAN for the
authoritative per-stage state):

- CUDA backend: device enumeration, GPU framebuffer, primary
  rays, sphere / triangle intersection, multi-sphere + single-mesh
  scene render, relativistic stack (aberration / Doppler /
  searchlight) with shared `rr::relativity::*` math leaf.
- OptiX backend: device-context lifecycle, triangle GAS, raygen +
  miss + closest-hit pipeline, single-triangle render, relativistic
  closest-hit + miss, OptiX denoiser with Albedo + Normal guides.
- Path tracer: minimum-viable diffuse path tracer with
  progressive accumulation.
- Materials / lights / textures: diffuse + emissive materials,
  point / directional / environment lights, nearest-neighbor
  texture sampling.
- Scene parser: `.rrscene` v1 schema with full-scene fixtures.
- AOVs: Beauty, Normal, Depth, Albedo, DopplerFactor,
  SearchlightFactor.
- Renderer server: TCP server with `ping` / `load_scene` /
  `set_beta` / `render` / `shutdown` verbs.
- Denoiser: OptiX denoiser end-to-end via `--render-denoise`
  and `--denoise` modifier on `--render-aovs`.
- Observability: GPU timing (`cudaEvent_t`-based, per-kernel +
  per-pass `[GPU]` log lines) and a project-wide GPU memory
  audit.

Empirical visual verification of the OptiX-side outputs (denoiser,
OptiX-relativity render, OptiX triangle render) is gated on a CUDA
+ OptiX-SDK host run; see
[`docs/STAGE_19_DENOISER_AUDIT.md`](docs/STAGE_19_DENOISER_AUDIT.md)
for the documented procedure. The audit-host build (no CUDA, no
OptiX SDK) compiles cleanly and returns the documented
"requires CUDA / OptiX SDK" errors on the gated CLI actions.

## Build

```sh
cmake -S . -B build
cmake --build build -j
build/bin/RelativityRender --help
```

Optional flags:

- `-DRR_ENABLE_CUDA=ON` — compile the CUDA renderer (requires the
  CUDA Toolkit and a CUDA-capable GPU at runtime; otherwise the
  CUDA-required CLI actions exit with the documented error).
- `-DRELATIVITYRENDER_ENABLE_OPTIX=ON` — compile the OptiX backend
  + denoiser scaffolding. Without `-DOPTIX_ROOT=/path/to/optix-sdk`
  the build still succeeds via an audit-host fallback that returns
  the documented "requires OptiX SDK" error from the OptiX-gated
  actions.

## Documentation

1. [`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`](RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt) — top-level rules and the 25-step development order.
2. [`docs/BUILD_PLAN.md`](docs/BUILD_PLAN.md) — **source of truth** for per-stage implementation status.
3. [`docs/MASTER_ARCHITECTURE.md`](docs/MASTER_ARCHITECTURE.md) — long-term layered architecture.
4. [`docs/MILESTONE_ROADMAP.md`](docs/MILESTONE_ROADMAP.md) — long-term M0–M23 milestones (mapped to master order in `docs/ROADMAP_AUDIT.md`).
5. [`docs/DEVELOPMENT_RULES.md`](docs/DEVELOPMENT_RULES.md) — engineering, dependency, GPU, process rules.
6. [`docs/ROADMAP_AUDIT.md`](docs/ROADMAP_AUDIT.md) — consistency audit of master order vs BUILD_PLAN vs README.
7. [`docs/ROADMAP_PROPOSED_ALIGNMENT.md`](docs/ROADMAP_PROPOSED_ALIGNMENT.md) — proposal for renumbering / aligning future stages.
8. Subsystem-specific design docs: [`docs/OPTIX_BACKEND_PLAN.md`](docs/OPTIX_BACKEND_PLAN.md), [`docs/DENOISER_PLAN.md`](docs/DENOISER_PLAN.md), [`docs/RRSCENE_FORMAT.md`](docs/RRSCENE_FORMAT.md).
9. Audits: [`docs/GPU_MEMORY_AUDIT.md`](docs/GPU_MEMORY_AUDIT.md), [`docs/STAGE_19_DENOISER_AUDIT.md`](docs/STAGE_19_DENOISER_AUDIT.md), [`docs/DENOISER_MEMORY_AUDIT_A.md`](docs/DENOISER_MEMORY_AUDIT_A.md) / [`B`](docs/DENOISER_MEMORY_AUDIT_B.md) / [`C`](docs/DENOISER_MEMORY_AUDIT_C.md), and the historical `docs/STAGE_*_AUDIT*.md` set.

## Layout

```
RelativityRender/
  CMakeLists.txt
  README.md
  RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt
  cmake/                                  # PTX-embed helper
  docs/                                   # rules, roadmap, architecture, audits
  scenes/                                 # .rrscene fixtures
  src/
    main.cpp                              # CLI dispatch
    core/                                 # Logger, Config, CommandLine, Version
    math/                                 # Vec / Mat / Transform / sampling
    image/                                # Image + PPM IO
    camera/                               # Camera + GpuCamera POD + ray gen
    geometry/                             # Sphere, Triangle, Mesh, Vertex
    material/                             # Material + MaterialParams
    lighting/                             # Light POD union
    relativity/                           # RelativityMath leaf (host + device)
    texture/                              # ImageTexture + format enum
    pathtracer/                           # PathTracer host orchestration + RNG
    renderer/                             # AccumulationBuffer, AOV, GpuAOVBuffer
    scene/                                # Scene aggregate
    io/                                   # SceneLoader / SceneWriter
    gpu/                                  # GpuBuffer, GpuDevice, GpuMesh, GpuScene,
                                          # GpuTexture, GpuTiming
    cuda/                                 # CUDA backend (kernels + launchers)
    optix/                                # OptiX backend + denoiser
    server/                               # rr_server (TCP)
  tests/                                  # ctest 4/4
```

The on-disk layout and the per-library CMake split are documented
in `docs/MASTER_ARCHITECTURE.md`. The exact module-by-module
implementation status lives in `docs/BUILD_PLAN.md`.

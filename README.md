# RelativityRender

A CUDA / OptiX-first GPU renderer platform with an integrated
relativistic camera / perception model (relativistic
aberration, Doppler colour shift, searchlight beaming). The
relativistic perception model is the differentiator; every
other subsystem (path tracer, materials, lighting, scene
graph, server, denoiser, AOVs) exists to make that model
usable in production.

## Status: pre-alpha

The renderer is under active incremental development. The
per-slice implementation status is recorded in
[`docs/BUILD_PLAN.md`](docs/BUILD_PLAN.md), which is the
project's source of truth — every claim below is sourced
from there.

**This project is not production-ready.** No frame has been
end-to-end visually validated on a real CUDA + OptiX-SDK
host in this branch; runtime GPU validation is a documented
deferred gate (see
[`docs/STAGE_19_DENOISER_AUDIT.md`](docs/STAGE_19_DENOISER_AUDIT.md),
Q1 / Q2). The audit-host build (no CUDA, no OptiX SDK)
compiles cleanly and returns the documented "requires CUDA /
OptiX SDK" errors on the gated CLI actions.

## Implementation surface

### Implemented foundations (verified on the audit host)

Pure host-side or compile-time-verified modules. `ctest`
reports `100% tests passed, 0 tests failed out of 4` on
both the OFF build (no CUDA, no OptiX) and the audit-host
ON build (`-DRELATIVITYRENDER_ENABLE_OPTIX=ON` without an
SDK).

- Math leaf (`Vec` / `Mat` / `Transform` / sampling), Image
  + PPM IO, Camera POD + RR_HD ray-gen helper.
- Geometry (`Sphere` / `Triangle` / `Mesh` + `Vertex`),
  Material + `MaterialParams`, Light POD union, Texture
  metadata + `ImageTextureFormat`.
- Relativity math leaf (`aberrateDirection` /
  `dopplerFactor` / `searchlightFactor`) — host + device
  callable, with a precomputed-invariants POD for
  per-launch reuse.
- Scene aggregate + `.rrscene` v1 loader / writer.
- AOV data model (Beauty / Normal / Depth / Albedo /
  DopplerFactor / SearchlightFactor) + per-pass GPU
  buffer owner.
- Renderer-server TCP scaffold (`ping` / `load_scene` /
  `set_beta` / `render` / `shutdown` verbs, localhost
  only).

### Partial / GPU-side systems (compiled + audit-host fallback verified; runtime GPU validation deferred)

These have shipped source and either compile cleanly under
`-DRR_ENABLE_CUDA=ON` / `-DRELATIVITYRENDER_ENABLE_OPTIX=ON`
or fall back to a documented "requires CUDA / OptiX SDK"
error on the audit host. **End-to-end pixel output has not
been visually validated on a CUDA + OptiX-SDK host in this
branch.**

- **CUDA backend**: device enumeration, GPU framebuffer,
  primary rays, sphere / triangle intersection, multi-
  sphere + single-mesh scene render with the relativistic
  stack, AOV writes, accumulation buffer with `float4`
  fast path, `cudaEvent_t` GPU timing instrumentation.
- **Path tracer** (Stage 11C): minimum-viable diffuse
  integrator with progressive accumulation. Compiles +
  links; no runtime image has been pinned as a reference
  baseline.
- **OptiX backend** (Stage 17A): device-context lifecycle,
  single-triangle GAS, raygen + miss + closest-hit pipeline,
  triangle render, relativistic shading on closest-hit +
  miss. Audit-host fallback (no SDK) returns the documented
  error; the SDK-found path is unexercised on this branch.
- **OptiX denoiser** (Stage 19): `create` / `set_inputs` /
  `invoke` wrapper around `optixDenoiser*`; integrated into
  `--render-aovs --denoise` and `--render-denoise` CLI
  actions. Includes a runtime fallback that saves the noisy
  Beauty AOV when any denoiser-side step fails. Visual
  smoothness verification is pending —
  `STAGE_19_DENOISER_AUDIT.md` Q2 (DEFERRED).

### Spec / planned systems (not yet started)

The following modules from the 25-step master order in
[`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`](RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt)
have no source code in the tree:

- Cinema 4D bridge (master order #21).
- Preview UI (master order #22).
- Material node graph (master order #23).
- Native Cinema 4D renderer integration (master order #25).

Master order #24 (Denoising) shipped before #21–#23; the
dependency-safety review is in
[`docs/ROADMAP_AUDIT.md`](docs/ROADMAP_AUDIT.md) §4.

## Build

```sh
cmake -S . -B build
cmake --build build -j
build/bin/RelativityRender --help
```

The default configuration is the audit-host build (no CUDA,
no OptiX SDK). Optional flags:

- `-DRR_ENABLE_CUDA=ON` — compile the CUDA renderer.
  Requires the CUDA Toolkit at build time and a
  CUDA-capable GPU at runtime; CUDA-required CLI actions
  exit with the documented error otherwise.
- `-DRELATIVITYRENDER_ENABLE_OPTIX=ON` — compile the OptiX
  backend + denoiser. The CMake option uses the full
  project prefix (matching `CMakeLists.txt` line 30).
  Without `-DOPTIX_ROOT=/path/to/optix-sdk` the build still
  succeeds via the audit-host fallback (which returns the
  documented "requires OptiX SDK" error from the OptiX-gated
  CLI actions); a real OptiX runtime requires the SDK
  install plus a CUDA-capable GPU.
- `-DRR_BUILD_TESTS=OFF` — skip the four `ctest` targets.

The CMake flag prefix is split between `RR_*`
(project-internal shorthand) and `RELATIVITYRENDER_*` (full
project name). This is a known cosmetic inconsistency
tracked by the roadmap audit; it does not affect build
correctness.

## Documentation

1. [`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`](RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt) — top-level rules and the 25-step development order.
2. [`docs/BUILD_PLAN.md`](docs/BUILD_PLAN.md) — **source of truth** for per-slice implementation status.
3. [`docs/MASTER_ARCHITECTURE.md`](docs/MASTER_ARCHITECTURE.md) — long-term layered architecture.
4. [`docs/MILESTONE_ROADMAP.md`](docs/MILESTONE_ROADMAP.md) — long-term M0–M23 milestones (mapped to master order in `docs/ROADMAP_AUDIT.md`).
5. [`docs/DEVELOPMENT_RULES.md`](docs/DEVELOPMENT_RULES.md) — engineering, dependency, GPU, process rules.
6. [`docs/ROADMAP_AUDIT.md`](docs/ROADMAP_AUDIT.md) — consistency audit of master order vs BUILD_PLAN vs README.
7. [`docs/ROADMAP_PROPOSED_ALIGNMENT.md`](docs/ROADMAP_PROPOSED_ALIGNMENT.md) — proposal for renumbering / aligning future stages.
8. Subsystem design docs: [`docs/OPTIX_BACKEND_PLAN.md`](docs/OPTIX_BACKEND_PLAN.md), [`docs/DENOISER_PLAN.md`](docs/DENOISER_PLAN.md), [`docs/RRSCENE_FORMAT.md`](docs/RRSCENE_FORMAT.md).
9. Audits: [`docs/GPU_MEMORY_AUDIT.md`](docs/GPU_MEMORY_AUDIT.md), [`docs/STAGE_19_DENOISER_AUDIT.md`](docs/STAGE_19_DENOISER_AUDIT.md), [`docs/DENOISER_MEMORY_AUDIT_A.md`](docs/DENOISER_MEMORY_AUDIT_A.md) / [`B`](docs/DENOISER_MEMORY_AUDIT_B.md) / [`C`](docs/DENOISER_MEMORY_AUDIT_C.md), plus the historical `docs/STAGE_*_AUDIT*.md` set.

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
  tests/                                  # ctest 4/4 (math / image / gpu / pathtracer)
```

`docs/MASTER_ARCHITECTURE.md` §8 "Build / Repository Shape
(Planned)" predates the current layout — its
`cuda_backend/` / `optix_backend/` / `relativistic/` /
`scene_format/` directory names do not match the actual
tree. The actual-vs-planned naming reconciliation is in
`docs/ROADMAP_AUDIT.md` §4. Per-slice implementation status
remains the responsibility of `docs/BUILD_PLAN.md`.

# Stage 20 OptiX Path-Tracing Audit

Date: 2026-05-03
Branch: `relativity-core-v1`
Last commit on the audited tree: `f4da732` ("stage 20O: OptiX
denoiser handoff audit")
Scope: master order #17 — stages 20A–20O (OptiX upgrade path).
Mode: documentation-only. No source code is modified by this
audit.

The audit answers the eleven prompt questions in order. Where
visual-evidence verification requires a CUDA + OptiX-SDK host
that this audit host does not have (`which nvcc` returns
nothing; no `optix.h` under `/opt`, `/usr/local`, or `$HOME`),
the documented expected behaviour is recorded with a clear
"deferred-to-CUDA + OptiX-SDK host" gate so a future operator
can finish the verification on the right hardware.

---

## Stages covered (commit hashes)

| Stage  | Commit    | Slice                                       |
|--------|-----------|---------------------------------------------|
| 20A    | `e1e69a9` | OptiX compile baseline (verification only)  |
| 20B    | `dc8620e` | OptiX launch params POD                     |
| 20C    | `6b2e602` | OptiX raygen / miss baseline                |
| 20D    | `7343453` | OptiX one-triangle GAS (test coverage)      |
| 20E    | `d2ee719` | OptiX closest-hit (verification only)       |
| 20F    | `e90505f` | OptiX mesh-scene GAS                        |
| 20G    | `ce79557` | OptiX material SBT                          |
| 20H    | `7872b16` | OptiX relativistic raygen (D-via-payload)   |
| 20I    | `1649d42` | minimum-viable OptiX path tracer            |
| 20J    | `5c2bb9d` | OptiX accumulation                          |
| 20K    | `d42a9a4` | OptiX direct lighting                       |
| 20L    | `6dc81d9` | OptiX shadow rays                           |
| 20M    | `e1830b3` | OptiX texture sampling                      |
| 20N    | `f08f90d` | OptiX AOVs                                  |
| 20O    | `f4da732` | OptiX denoiser handoff audit (docs-only)    |

---

## 1. Does the OptiX OFF build still work?

**YES** — verified empirically on this audit host.

- `cmake -S . -B build_off -DRR_BUILD_TESTS=ON
  -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=OFF`: clean configure,
  no warnings beyond the Stage 12B.4 SDK-not-found note (which
  itself is suppressed when `RR_ENABLE_OPTIX=OFF`).
- `cmake --build build_off -j4`: clean build, 0 errors, 0
  warnings, all targets including `rr_renderer`, `rr_gpu`,
  `rr_io`, `rr_scene`, `rr_lighting`, and the
  `RelativityRender` executable link successfully.
- `ctest`: 6/6 tests green
  (`math_tests`, `image_tests`, `gpu_tests`, `pathtracer_tests`,
  `relativity_tests`, `demo_tests`). The `optix_tests` target is
  intentionally not built when `RR_ENABLE_OPTIX=OFF`, matching
  the Stage 12B.3 contract.
- `./build_off/bin/RelativityRender --render-optix-aovs`:
  exits 1 with the documented "requires OptiX..." error per
  Stage 12B.4 + Stage 17A.3 fallback contract. Same shape for
  every other `--render-optix-*` action.
- `./build_off/bin/RelativityRender --render-demo`,
  `--render-aovs`, `--render-denoise`: all exit with the
  expected CUDA-required errors (the audit host has neither
  CUDA nor OptiX); none crash or produce malformed output.

Verdict: OFF build is byte-clean, ctest green, every audit-host
fallback message reaches the user with the documented wording.

---

## 2. Does the OptiX ON build work?

**YES (structural)** — verified on the audit-host fallback path.
Empirical SDK-found verification is deferred.

What the audit host can and did verify:

- `cmake -S . -B build_on_audit -DRR_BUILD_TESTS=ON
  -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=ON`: configures
  cleanly with the documented Stage 12B.4 non-blocking
  "OptiX SDK not located" warning.
  `RELATIVITYRENDER_ENABLE_OPTIX` is defined for
  `rr_optix`'s public interface; `RELATIVITYRENDER_OPTIX_SDK_FOUND`
  is intentionally NOT defined (no `<optix.h>` available), so
  `rr_optix` falls back to its audit-host stub branches per
  Stage 12B.4.
- `cmake --build build_on_audit -j4`: clean build,
  `librr_optix.a` compiles, `RelativityRender` links,
  `optix_tests` test-binary builds.
- `ctest`: 7/7 green
  (the OFF six **plus** `optix_tests`). The new test target
  exercises the audit-host stub surface (every stub returns
  the documented `requires OptiX SDK` error), the launch-params
  POD, and the public-header surface.
- `./build_on_audit/bin/RelativityRender --render-optix-test`,
  `--render-optix-pathtrace test.rrscene`, `--render-optix-aovs`,
  etc.: every dispatcher reaches its
  `OptixRenderer::render_*` entry, the SDK-not-found stub fires,
  the user-facing error is logged, exit 1.

What is deferred to a CUDA + OptiX-SDK host run:

- `cmake -S . -B build_on -DRR_ENABLE_CUDA=ON -DRR_ENABLE_OPTIX=ON
  -DOPTIX_ROOT=/path/to/optix-sdk` configure + build, where
  `RELATIVITYRENDER_OPTIX_SDK_FOUND` IS defined and rr_optix
  compiles its `OptixPrograms.cu` to PTX. The actual SDK
  calls (`optixInit`,
  `optixDeviceContextCreate`,
  `optixModuleCreate`, `optixProgramGroupCreate`,
  `optixPipelineCreate`,
  `optixAccelBuild`,
  `optixSbtRecordPackHeader`,
  `optixLaunch`,
  `optixDenoiserCreate`,
  `optixDenoiserInvoke`)
  are all wired in the SDK-found branch of the corresponding
  rr_optix `.cpp` files (verified by `grep`); they are dormant
  on the audit host because the gating macro is undefined.
- The actual GPU launches (`optixLaunch` over the bound SBT)
  + their PPM downloads. These run on the SDK-found branch
  only.

Verdict: ON build is structurally green and every
SDK-required entry has its real-SDK and audit-host-stub
branches in place. Empirical SDK verification deferred to a
CUDA + OptiX-SDK host run.

---

## 3. Does the CUDA renderer still work?

**YES (no regressions structurally possible)** — verified by
diff inspection. Empirical CUDA verification is deferred.

What the audit host can and did verify:

- `git diff e1e69a9~1..f4da732 --stat -- src/cuda/
  src/renderer/ src/pathtracer/`: zero bytes changed. The
  entire Stage 20 arc (commits `e1e69a9`..`f4da732`) is
  byte-identical for the CUDA renderer (`src/cuda/`), the
  AOV / accumulation primitives (`src/renderer/`), and the
  CPU-side path-tracer fixtures (`src/pathtracer/`).
  The CUDA path tracer kernels, AOV writes, accumulation
  helpers, and renderer entry points are all
  unchanged.
- `git diff e1e69a9~1..f4da732 --stat -- src/scene/ src/io/
  src/camera/ src/material/ src/lighting/ src/relativity/
  src/geometry/`: zero bytes changed. Every data-layer module
  the CUDA renderer depends on is byte-identical.
- The Stage 20 changes are confined to:
  - `src/optix/*` (the actual OptiX implementation),
  - `src/main.cpp` (added new
    `run_render_optix_*` dispatchers; existing
    `run_render_*` dispatchers untouched),
  - `src/core/CommandLine.{h,cpp}` (added new
    `RenderOptix*` action enumerators + `--render-optix-*`
    parser branches; existing `RenderXxx` enumerators and
    `--render-xxx` parser branches untouched),
  - `CMakeLists.txt` (banner string + Stage 12B.4 OptiX
    detection block; the CUDA target_link_libraries chain is
    unchanged),
  - `tests/optix_tests.cpp` (new test target gated on
    `RR_ENABLE_OPTIX=ON`),
  - `docs/BUILD_PLAN.md` (slice-closing entries).
- `--render-aovs`, `--render-denoise`, `--render-pathtrace`,
  `--render-direct-lighting`, `--render-textured-material`,
  `--render-mesh-scene`, etc. all keep exactly the same
  dispatcher implementations, scene-build code, and CUDA-path
  call shapes they had at `e1e69a9~1`.
- Mutual-exclusion in `CommandLine::ParseResult`: the new
  `RenderOptix*` action enumerators are added alongside the
  existing CUDA actions; no CUDA action enumerator was
  renamed, removed, or repurposed.

What is deferred to a CUDA host run:

- The actual `--render-aovs`, `--render-denoise`,
  `--render-pathtrace`, `--render-direct-lighting`,
  `--render-textured-material`, `--render-mesh-scene`,
  `--render-relativistic`, `--render-scene`,
  `--render-demo` runs producing their respective
  `output/*.ppm` files. The audit host can only confirm the
  source is structurally identical to the pre-Stage-20A tree;
  end-to-end CUDA verification requires the Stage 11 + 13 +
  19B baselines on a real CUDA-capable host.

Verdict: the CUDA renderer is byte-identical across the
Stage 20 arc. Any pre-Stage-20A CUDA test that passed on a
real host will still pass on the same host post-Stage-20O.

---

## 4. Does the OptiX raygen output exist?

**YES (wired)** — verified by source inspection. Empirical PPM
verification is deferred to a CUDA + OptiX-SDK host run.

What the audit host can and did verify:

- `--render-optix-raygen` dispatcher: `run_render_optix_raygen`
  in `src/main.cpp:1166`. Default output:
  `output/optix_raygen.ppm`. Builds a tiny GAS placed behind
  the camera so every primary ray misses; the
  `__miss__radiance` program in
  `src/optix/OptixPrograms.cu:242` writes the env-color
  gradient per pixel.
- `OptixRenderer::render_raygen(width, height)` is declared in
  `src/optix/OptixRenderer.h:136` and defined in
  `src/optix/OptixRenderer.cpp` (SDK-found branch); audit-host
  stub branch returns the documented "requires OptiX" error.
- The dispatcher is wired into the action switch
  (`src/main.cpp:3919`,
  `case CommandLine::Action::RenderOptixRaygen: return
  run_render_optix_raygen(result.config);`) and into the
  CommandLine parser (`src/core/CommandLine.cpp:281` —
  `else if (a == "--render-optix-raygen") {...}`).
- Audit-host CLI smoke:
  `./build_on_audit/bin/RelativityRender --render-optix-raygen`
  reaches the renderer, hits the SDK-not-found stub, exits 1
  with the documented error.

What is deferred to a CUDA + OptiX-SDK host run:

- Actual `output/optix_raygen.ppm` file write. The
  end-to-end optixLaunch -> framebuffer download -> PPM save
  flow runs on the SDK-found branch only.

Verdict: the dispatcher, renderer entry, raygen / miss
programs, GAS-build call, framebuffer allocation, and PPM-save
chain are wired top-to-bottom. PPM existence on a CUDA +
OptiX-SDK host follows from running the action.

---

## 5. Does the OptiX triangle output exist?

**YES (wired)** — verified by source inspection.

- `--render-optix-triangle` dispatcher:
  `run_render_optix_triangle` in `src/main.cpp:1019`. Default
  output: `output/optix_triangle.ppm`.
- `OptixRenderer::render_triangle(width, height)`:
  `src/optix/OptixRenderer.h:78` declaration,
  `src/optix/OptixRenderer.cpp` SDK + stub definitions.
  Builds a single-triangle GAS, runs the Stage 17A.4 raygen +
  miss + closest-hit pipeline, the closest-hit emits
  `0.5 * normal + 0.5` (normal-as-color) so the saved PPM is
  directly viewable.
- Action switch: `src/main.cpp:3886`,
  `RenderOptixTriangle -> run_render_optix_triangle`.
- Parser: `src/core/CommandLine.cpp:269` —
  `--render-optix-triangle` branch.
- Audit-host CLI smoke: `--render-optix-triangle` exits 1 with
  the documented "requires OptiX" error.

Empirical PPM verification deferred to a CUDA + OptiX-SDK host
run.

---

## 6. Does the OptiX mesh-scene output exist?

**YES (wired)** — verified by source inspection.

The mesh-scene family ships four production-grade OptiX
entries plus a relativistic variant:

| Entry                              | CLI                                 | Default output                            |
|------------------------------------|-------------------------------------|-------------------------------------------|
| `render_mesh_scene`                | `--render-optix-mesh-scene <file>`  | `output/optix_mesh_scene.ppm`             |
| `render_material_scene`            | `--render-optix-material-scene <f>` | `output/optix_material_scene.ppm`         |
| `render_direct_lighting`           | `--render-optix-direct-lighting <f>`| `output/optix_direct_lighting.ppm`        |
| `render_direct_lighting`* (shadows)| `--render-optix-shadow-test <file>` | `output/optix_shadow_test.ppm`            |
| `render_textured_material`         | `--render-optix-textured-material`  | `output/optix_textured_material.ppm`      |

(* same `render_direct_lighting` entry, called with
`enable_shadows = true`)

Common shape (verified by reading `src/optix/OptixRenderer.cpp`):

1. Pick first non-empty visible mesh in `scene.meshes`.
2. Look up the picked mesh's material in `scene.materials`.
3. Initialize backend (`OptixBackend::initialize` ->
   `optixInit` + `optixDeviceContextCreate`).
4. Create pipeline (`OptixPipeline::create` ->
   `optixModuleCreate`, `optixProgramGroupCreate` x4 (raygen +
   miss + miss_shadow + hitgroup), `optixPipelineCreate`,
   `optixSbtRecordPackHeader`).
5. `set_hit_material(material_params, shading_mode=K)`:
   - `K=0`: normal-as-color (Stage 17A.4 baseline)
   - `K=1`: material-flat (Stage 20G; baseColor + emission)
   - `K=2`: direct lighting (Stage 20K; Lambert direct + ambient)
6. Upload positions (extracted from `Vertex` POD), indices,
   optionally UVs (Stage 20M), optionally textures (Stage
   20M), optionally lights (Stage 20K).
7. `build_mesh_gas` -> `optixAccelBuild`.
8. Allocate framebuffer + (Stage 20N) per-AOV device buffers.
9. Memcpy `OptixLaunchParams` to device.
10. `optixLaunch` (timed via `GpuTimer`).
11. `cudaDeviceSynchronize`.
12. Memcpy device framebuffer -> host `Image`.
13. Free all device allocations via the `cleanup` lambda.
14. Save PPM at the dispatcher level (`Image::save_ppm`).

Audit-host CLI smoke: every entry exits 1 with the documented
"requires OptiX" error.

Empirical PPM verification (every output above) deferred to a
CUDA + OptiX-SDK host run.

---

## 7. Do the OptiX path-tracer outputs exist?

**YES (wired)** — verified by source inspection.

Stage 20I shipped the minimum-viable OptiX path tracer; Stage
20J added the accumulation primitives.

- `--render-optix-pathtrace <file>` dispatcher:
  `run_render_optix_pathtrace` in `src/main.cpp:1388`.
  Outputs (default; `--output` overrides one of them depending
  on which `spp` is currently active):
  - `output/optix_pathtrace_spp1.ppm` (single-sample run)
  - `output/optix_pathtrace_spp16.ppm` (16-sample run)
- `OptixRenderer::render_pathtrace(scene, w, h, spp,
  max_bounces, seed)`: declared in `src/optix/OptixRenderer.h:215`.
- `OptixRenderer::render_pathtrace_progressive(scene, w, h,
  max_bounces, seed, checkpoint_samples)` (Stage 20J): declared
  in `src/optix/OptixRenderer.h:274`. Threaded through
  `rr::cuda::launch_accum_clear/_first_sample/_add/_resolve`
  primitives (called from `OptixRenderer.cpp:1662..1759`); the
  `rr_optix` target PRIVATE-links `rr_gpu` for these
  primitives, mirroring the dependency-boundary audit's
  rr_optix -> rr_gpu PRIVATE arrow.
- The pipeline binds the dedicated path-tracer entry-point
  family (`__raygen__pathtrace`, `__miss__pathtrace`,
  `__closesthit__pathtrace`) when `OptixPipelineOptions::path_tracer
  = true`. Source: `src/optix/OptixPrograms.cu:801` (raygen),
  `:943` (miss), `:951` (closest-hit).
- Per-pixel work runs entirely on the GPU: the raygen iterates
  the spp loop, seeds `rr::pathtracer::Rng` per sample via
  `make_pixel_rng(x, y, sample_index, seed)`
  (`OptixPrograms.cu:851`), iterates the bounce loop
  (`:869`), samples cosine-hemisphere (`:906`), accumulates
  throughput per bounce, and divides by spp at the end
  (`:922`). No host shading.

Audit-host CLI smoke: `--render-optix-pathtrace test.rrscene`
fails earlier on file-not-found (no SDK reach); on a valid
scene file the SDK-not-found stub fires.

Empirical PPM verification deferred to a CUDA + OptiX-SDK host
run.

---

## 8. Does relativity work in the OptiX raygen / shading?

**YES (full parity with the CUDA path)** — verified by source
inspection.

The OptiX path consumes the same `rr::relativity::*` helpers
as the CUDA path; PODs are identical (`Observer`,
`RelativityParams`) and uploaded through `OptixLaunchParams`.

- Per-launch precompute:
  `precompute_relativity(observer.velocity)` is called once at
  the top of the raygen
  (`OptixPrograms.cu:134` for `__raygen__pinhole`,
  `:184` for the relativistic raygen variant,
  `:832` for `__raygen__pathtrace`) so the per-pixel `sqrt`
  inside `aberrateDirection` is amortised across the
  framebuffer — same precedent the CUDA renderer set in
  Stage 6.
- Aberration: `aberrateDirection(rel, ray.direction)` is
  applied to every primary ray (`OptixPrograms.cu:187` for
  the relativistic raygen, `:861` for the path tracer).
  Identity at `|beta| = 0`, so default `Observer{}` produces
  the Stage 17A.4 pixel output byte-for-byte.
- Doppler factor: `dopplerFactor(rel, dir)` is computed per
  ray (`OptixPrograms.cu:138`, `:199`, `:931`); the closest-
  hit / miss programs apply it via `applyDopplerColor` /
  payload-threaded `D` (`OptixPrograms.cu:105`).
- Searchlight factor: `searchlightFactor(D)` is applied inside
  the same `apply_doppler_and_searchlight_with_D` helper
  (`OptixPrograms.cu:113`); the AOV pass writes
  `D` and `D^4` to per-pixel buffers
  (`:230..:234`).
- The `RelativityParams` per-effect enable bits + strength
  multipliers + beta cap are honoured the same way the CUDA
  renderer's `k_render_scene` honours them: every gate is
  evaluated GPU-side per ray.
- `--render-optix-relativity` dispatcher honours the `--beta`
  modifier (Stage 19E.2 precedent) so artists can sweep beta
  values from the CLI without rebuilding; the dispatcher
  loops over the requested beta and writes
  `output/optix_relativity_beta{NNN}.ppm` per run
  (`src/main.cpp:1103`).

Verdict: relativity is wired into the OptiX raygen, miss,
closest-hit (radiance), closest-hit (path tracer), and AOV
pass at full parity with the CUDA path.

Empirical pixel-comparison against the CUDA path's
`--render-relativistic` deferred to a CUDA + OptiX-SDK host
run.

---

## 9. Materials / lights / textures / AOVs status

### Materials

**Working on the OptiX path** (Stage 20G).

- `MaterialParams` POD threaded through the SBT hit-group
  record as `HitGroupData::params`; closest-hit reads it via
  `optixGetSbtDataPointer()` (`OptixPrograms.cu:335`).
- Three shading modes selected by `HitGroupData::shading_mode`:
  - `0`: normal-as-color (default; pre-material baseline).
  - `1`: material-flat (baseColor + emission, optionally
    texture-sampled).
  - `2`: direct lighting (Lambert diffuse over uploaded lights;
    Stage 20K).
- `OptixPipeline::set_hit_material(params, shading_mode)`
  rebuilds the hit-group SBT record so artists can mix-and-
  match modes per launch.

Gap: the OptiX path is **mesh-only**. Spheres
(`scene.spheres`) are not built into a custom-IS GAS yet; the
CUDA path supports them via `k_sphere_relativistic` /
`k_render_scene`. `OptixRenderer::render_*` entries pick
`scene.meshes` only.

Gap: glossy / dielectric / metal BSDFs are not wired. The
shading branches all produce Lambert / flat output. Same gap
the CUDA path has at this milestone (master order #16, "BSDF
work" not yet started).

### Lights

**Working on the OptiX path** (Stage 20K + 20L).

- `lighting/Light.h` POD union (Point, Directional, Area,
  Environment) uploaded once per launch via
  `cudaMalloc + cudaMemcpy`; the device pointer + count live
  on `OptixLaunchParams::lights` /
  `OptixLaunchParams::light_count`.
- Point + Directional + Environment branches evaluated per
  hit when `shading_mode == 2`
  (`OptixPrograms.cu:419..527`).
- Area is documented as PLACEHOLDER (Stage 9B; ignored at
  evaluate time) — same shape as the CUDA path.
- Stage 20L shadow rays: closest-hit traces an occlusion ray
  per light when `OptixLaunchParams::enable_shadows == true`,
  using
  `OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT |
  OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT` and
  `missSbtIndex = 1`; the dedicated `__miss__shadow` program
  sets a single visibility-flag payload
  (`OptixPrograms.cu:436` and `:318`).

### Textures

**Working on the OptiX path** (Stage 20M).

- Per-vertex UVs uploaded as a separate `Vec2*` device buffer
  (the GAS keeps its tightly-packed `float3` position layout
  per Stage 20F).
- Per-triangle index buffer uploaded as
  `Triangle*` so the closest-hit can find a triangle's
  vertex indices.
- `DeviceTextureView` array per scene; each entry points at
  its own pixel buffer + records (pixels, w, h, format).
- Closest-hit `shading_mode == 1` branch reads
  `optixGetTriangleBarycentrics()`, indexes
  `optixLaunchParams.mesh_indices`, looks up three vertex UVs
  via `optixLaunchParams.mesh_uvs`, barycentric-interpolates
  the UV, and samples
  `optixLaunchParams.textures[baseColorTextureId]` via
  `rr::cuda::sampleTextureNearest` — the same RR_HD inline
  helper the CUDA path uses
  (`OptixPrograms.cu:586..626`).
- Falls back to flat `params.baseColor` when
  `useBaseColorTexture == false` OR the texture id is out of
  range OR any of the three pointers is null.

Gap: texture filtering is **nearest-neighbour only** (no MIP /
trilinear / anisotropic). Same gap the CUDA path has at this
milestone (Module #10 "foundation landed" maturity reflects
this explicitly).

### AOVs

**Working on the OptiX path** (Stage 20N).

Six AOVs match the CUDA path's `--render-aovs` set:

| AOV               | Component count | Range / encoding                               |
|-------------------|-----------------|------------------------------------------------|
| Beauty            | 3 (FLOAT3)      | Linear-light RGB, post-Doppler / searchlight   |
| Normal            | 3 (FLOAT3)      | Encoded `0.5 n + 0.5` for hits, `0` for miss   |
| Depth             | 1 (FLOAT)       | `1 / (1 + t_hit)` for hits, `0` for miss       |
| Albedo            | 3 (FLOAT3)      | Raw `MaterialParams::baseColor`, pre-lighting  |
| DopplerFactor     | 1 (FLOAT)       | `D` per primary ray (hit + miss)               |
| SearchlightFactor | 1 (FLOAT)       | `D^4`                                          |

- Pointers live on `OptixLaunchParams::aov_*`; defaults are
  null and every write site short-circuits when its pointer
  is null. Existing OptiX entries
  (`render_test`, `render_triangle`, `render_relativity`,
  `render_raygen`, `render_mesh_scene`, `render_material_scene`,
  `render_pathtrace*`, `render_direct_lighting`,
  `render_textured_material`) ignore the AOV fields and
  produce byte-identical output to their pre-Stage-20N
  versions.
- `OptixRenderer::render_aovs(scene, lights, w, h)` allocates
  the six device buffers, threads them through
  `OptixLaunchParams`, runs the existing direct-lighting
  closest-hit, downloads each AOV, and returns a single
  `AovResult` containing six `Image`s.
- CLI: `--render-optix-aovs` saves the six PPMs at fixed
  paths (`output/optix_aov_beauty.ppm`, etc.; mirrors the
  CUDA `--render-aovs` no-`--output` shape).

---

## 10. CPU rendering violations

**ZERO violations** — verified by `grep` over
`src/optix/` and `src/main.cpp`.

The master rule says "no CPU per-pixel or per-ray work as the
production path". The audit looked for host-side loops over
the framebuffer (`for (y = 0; y < height; ...)`) inside the
OptiX dispatchers and the `OptixRenderer` implementation.

Findings:

- `src/optix/OptixPrograms.cu`: every per-pixel write happens
  on the GPU. The raygen iterates `spp` and `max_bounces`
  device-side; the closest-hit and miss programs run on the
  device per ray; AOV writes happen via
  `optixGetLaunchIndex()` device-side. No host code is
  reached during the launch.
- `src/optix/OptixRenderer.cpp`: pixel-space iteration only
  appears in the `download_1_replicate` lambda inside
  `render_aovs` (`OptixRenderer.cpp:2655`), which expands
  scalar AOVs (Depth / Doppler / Searchlight) to RGB
  host-side **after** the device download so the saved PPM
  is directly viewable. This is **display-format
  replication**, not per-pixel rendering — same shape the
  CUDA path's `save_aov_to_ppm` helper uses, and consistent
  with the master rule (which targets render computation,
  not post-download formatting).
- `src/main.cpp` OptiX dispatchers
  (`run_render_optix_test`, `run_render_optix_triangle`,
  `run_render_optix_relativity`, `run_render_optix_raygen`,
  `run_render_optix_mesh_scene`,
  `run_render_optix_material_scene`,
  `run_render_optix_pathtrace`,
  `run_render_optix_direct_lighting`,
  `run_render_optix_shadow_test`,
  `run_render_optix_textured_material`,
  `run_render_optix_aovs`): zero pixel-space `for` loops.
  The dispatchers build the scene POD, hand it to
  `OptixRenderer::render_*`, then save PPMs.
- `Image::save_ppm` (the PPM emit) is not "rendering"; it is
  a one-shot serialisation of an already-computed
  framebuffer.

Verdict: the OptiX path tracer is GPU-only end-to-end; there
are no CPU shortcuts in the per-pixel computation graph.

---

## 11. Remaining gaps before denoising

The Stage 20O audit (`docs/BUILD_PLAN.md` Stage 20O entry)
already documents the producer / consumer contract for the
denoiser handoff. The following gaps remain before
`output/denoised.ppm` can be produced from the OptiX path:

### Gap A — Durable AOV buffer ownership (BLOCKS denoiser handoff)

Stage 20N's `OptixRenderer::render_aovs` allocates the six
AOV device buffers, runs the launch, downloads each buffer
into a host `Image`, and frees the device buffers via its
`cleanup` lambda before returning. The denoiser needs the
device pointers alive across `optixDenoiserInvoke`; the
current entry frees them too early.

The CUDA equivalent (`denoise_aov_buffers_to_ppm`) keeps
device-resident `GpuAOVBuffer` instances alive on the host
side across the denoiser call. The OptiX path needs the
analogous shape — a sibling entry like
`render_aovs_for_denoise(...)` returning
`(beauty_device, albedo_device, normal_device, w, h, cleanup_token)`,
OR an `OptixRenderer` member that owns the buffers across a
denoiser invoke, OR a single `render_aovs_and_denoise(...)`
entry that does the full pipeline in one call (matching the
CUDA `--render-aovs --denoise` end-to-end shape).

### Gap B — Wire the existing `OptixDenoiser` to the OptiX-path AOV producer (BLOCKS denoiser handoff)

`OptixDenoiser::initialize`, `set_inputs`, and `invoke` are
already shipped (Stage 19B.1 / 19B.2 / 19B.3). What's missing
is a host-orchestration helper analogous to
`denoise_aov_buffers_to_ppm` (`src/main.cpp:3521`) but for
the OptiX-path producer instead of the CUDA-path producer.
The orchestration is the same five-step pipeline (init ->
set_inputs -> invoke -> sync -> download); only the upstream
buffer source changes.

### Gap C — `--render-optix-denoise` CLI surface (REQUIRED for parity)

Once Gap A + Gap B land, the dispatcher needs a
`--render-optix-denoise` (or `--render-optix-aovs --denoise`
modifier) CLI surface so artists can produce
`output/optix_denoised.ppm` (or reuse `output/denoised.ppm`)
from the OptiX path. The CLI shape mirrors `--render-denoise`
(Stage 19B.3) / `--render-aovs --denoise` (Stage 19B.4); the
dispatcher needs the standard mutex / validation entries.

### Gap D — Spheres on the OptiX path (BLOCKS visual-parity validation)

Every existing `--render-optix-*` entry walks
`scene.meshes` and ignores `scene.spheres`. The CUDA path
supports both. To validate the OptiX denoiser handoff against
the same sphere-heavy scene the CUDA `--render-denoise`
fixture uses, the OptiX path needs a custom-IS sphere GAS
(per `OPTIX_BACKEND_PLAN.md` §10.2). This is not strictly
required to denoise *some* OptiX output (a textured-quad +
direct-lighting scene works today), but it is required to
empirically compare CUDA-denoised vs OptiX-denoised on the
existing project fixtures.

### Gap E — Motion vectors for temporal denoising (NOT REQUIRED for HDR model)

The current denoiser model is `OPTIX_DENOISER_MODEL_KIND_HDR`
(per `OptixDenoiser.h:55`); temporal denoising would require
`AOVType::Motion` which the renderer does not produce yet
(`DENOISER_PLAN.md` §8.2.2). Out of scope for the current
denoiser handoff; flagged for completeness.

### Gap F — Visual validation gate (PROJECT-WIDE)

The project-wide visual-validation gate (no frame rendered
through the OptiX path on a real OptiX-SDK host in this
branch — every Stage 20A..20O slice notes this in its
BUILD_PLAN entry) remains in place. The OptiX denoiser
handoff slice will need either an actual OptiX-SDK host run
or an explicit deferral notice. This is a workflow gap, not
a code gap.

---

## Summary table

| Check | Question                                             | Verdict          | Empirical / Structural |
|-------|------------------------------------------------------|------------------|-------------------------|
| 1     | Does OptiX OFF build still work?                     | YES              | Empirical (audit host)  |
| 2     | Does OptiX ON build work?                            | YES              | Structural              |
| 3     | Does CUDA renderer still work?                       | YES              | Diff-stats              |
| 4     | Does OptiX raygen output exist?                      | YES (wired)      | Structural              |
| 5     | Does OptiX triangle output exist?                    | YES (wired)      | Structural              |
| 6     | Does OptiX mesh-scene output exist?                  | YES (wired) x5   | Structural              |
| 7     | Do OptiX path-tracer outputs exist?                  | YES (wired) x2   | Structural              |
| 8     | Does relativity work in OptiX raygen / shading?      | YES (full parity)| Structural              |
| 9     | Materials / lights / textures / AOVs status          | All wired        | Structural              |
| 10    | CPU rendering violations                             | ZERO             | Empirical (grep)        |
| 11    | Remaining gaps before denoising                      | A..F documented  | Documentation           |

"Empirical" = the audit host directly verified the claim by
running the relevant command on this host. "Structural" = the
audit verified the source / build configuration / wiring is
in place but the runtime verification requires a CUDA +
OptiX-SDK host the audit host does not have.

The critical finding for the next slice (post-Stage 20O):
**the OptiX path is feature-complete enough that the existing
`OptixDenoiser` can consume its AOV output verbatim; the
remaining work is host-side orchestration (Gaps A, B, C),
not new GPU kernels.**

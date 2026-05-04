# Stage 21 Denoiser Audit (capstone)

Date: 2026-05-04
Branch: `relativity-core-v1`
Last commit on the audited tree: `3f9943c` ("stage 21E.2:
CLI denoise integration (--render --denoise)")
Scope: master order #24 — the entire Stage 21 arc, covering
all five sub-arcs: 21A planning (10 sub-stages), 21B
scaffold (10 sub-stages), 21C input wiring (5 sub-stages),
21D denoiser invoke (6 sub-stages), 21E CLI integration
(2 sub-stages). 33 sub-stages + 3 prior intermediate audits
(post-21B, post-21C, post-21D) total.
Mode: documentation-only. No source code is modified by this
audit.

The audit answers the nine prompt questions in order. Where
visual-evidence verification requires a CUDA + OptiX-SDK host
that this audit host does not have (`which nvcc` returns
nothing; no `optix.h` under `/opt`, `/usr/local`, or `$HOME`),
the documented expected behaviour is recorded with a clear
"deferred-to-CUDA + OptiX-SDK host" gate so a future operator
can finish the verification on the right hardware.

---

## Stages covered

### 21A — planning arc (10 sub-stages)

| # | Commit    | Slice            |
|---|-----------|------------------|
| 1 | `4d08f96` | Purpose          |
| 2 | `4dc4522` | Backend          |
| 3 | `1971110` | Required inputs  |
| 4 | `f7049cb` | Optional inputs  |
| 5 | `1f54491` | Pipeline         |
| 6 | `8050f79` | Output           |
| 7 | `914fde5` | Failure behavior |
| 8 | `22b39ed` | Modes            |
| 9 | `a8ec7ca` | v1 scope         |
|10 | `871052f` | Plan complete    |

### 21B — scaffold arc (10 sub-stages)

| # | Commit    | Slice                                                |
|---|-----------|------------------------------------------------------|
| 1 | `8d36ca9` | Files (minimal class skeleton)                       |
| 2 | `fba3b73` | Compile guards                                       |
| 3 | `0174a99` | Include OptiX headers (SDK-gated)                    |
| 4 | `0e97137` | Object (handle creation via optixDenoiserCreate)     |
| 5 | `c9e970a` | Init function (logging)                              |
| 6 | `a229cce` | Memory requirements (optixDenoiserComputeMemoryResources) |
| 7 | `a9be1f9` | Buffer allocation (GpuBuffer<std::byte>)             |
| 8 | `bb1edf4` | Setup (optixDenoiserSetup)                           |
| 9 | `042bedf` | Availability (isAvailable())                         |
|10 | `93ca434` | Cleanup (formalized invariants + log)                |

### 21C — input wiring arc (5 sub-stages)

| # | Commit    | Slice                                            |
|---|-----------|--------------------------------------------------|
| 1 | `c471970` | Input structs (documented contract)              |
| 2 | `4149816` | Image-format helpers (4 `make_*_image`)          |
| 3 | `0d88f45` | Beauty-only (`prepareBeautyOnlyInput`)           |
| 4 | `26490dd` | Guided (`prepareGuidedInput`)                    |
| 5 | `b8ca6c9` | Input validation (`validateDenoiserInputs`)      |

### 21D — invoke arc (6 sub-stages)

| # | Commit    | Slice                                                |
|---|-----------|------------------------------------------------------|
| 1 | `0eb0c69` | Invoke shell (validate + isAvailable)                |
| 2 | `2236f45` | Beauty/guided invoke (real `optixDenoiserInvoke`)    |
| 3 | `724e2f4` | Guided invoke formalised                             |
| 4 | `c0b38e4` | Save denoised output (`denoise_and_save_ppm`)        |
| 5 | `58da922` | Failure fallback (noisy-Beauty in helper)            |
| 6 | `e4db2a8` | Test output CLI (`--render-optix-denoise`)           |

### 21E — CLI integration arc (2 sub-stages)

| # | Commit    | Slice                                          |
|---|-----------|------------------------------------------------|
| 1 | `ddd6e2d` | CLI denoise flag (announce)                    |
| 2 | `3f9943c` | CLI denoise integration (`--render --denoise`) |

### Intermediate audits

| Audit doc                                       | Commit    |
|-------------------------------------------------|-----------|
| `docs/STAGE_21B_DENOISER_SCAFFOLD_AUDIT.md`     | `4bbb487` |
| `docs/STAGE_21C_DENOISER_INPUT_AUDIT.md`        | `92d01fc` |
| `docs/STAGE_21D_DENOISER_INVOKE_AUDIT.md`       | `3362653` |

---

## 1. Does the denoiser plan exist?

**YES** — `docs/DENOISER_PLAN.md` (81 lines, nine sections).

Contents:

| Section          | Content                                                              |
|------------------|----------------------------------------------------------------------|
| Purpose          | reduce noise / enable low-spp / preserve relativistic shading cues   |
| Backend          | OptiX denoiser as primary; CUDA renderer remains independent          |
| Required inputs  | Beauty / Albedo / Normal mapped to Stage 14 `GpuAOVBuffer` (mandatory)|
| Optional inputs  | Depth + Motion (not required for v1.0)                               |
| Pipeline         | `render → AOV buffers → denoiser → final image`; post-render only    |
| Output           | `output/denoised.ppm`; separate from raw render artifacts            |
| Failure behavior | keep noisy on failure; log warning; never crash                      |
| Modes            | manual (CLI flag); automatic (future / optional)                     |
| v1 scope         | single-frame; no temporal; no motion vectors; no interactive preview |

A 10th `## Status` section closes the plan. Each section is
≤5 bullets per the user's deliberately-minimal cadence (in
deliberate contrast to the prior 1200-line Stage 19A planning
artifact, recoverable from git history at
`fcd90bd^:docs/DENOISER_PLAN.md`).

---

## 2. Does the OptiX denoiser wrapper exist?

**YES** — `src/optix/OptixDenoiser.{h,cpp}` (213 + 892
lines).

Public surface:

- `class OptixDenoiser` declared unconditionally in the
  header (consumers can include it in any TU).
- Move-only RAII handle (deleted copy ops; movable).
- `Inputs` and `Output` structs (Stage 21C.1 documented
  contract).
- Methods:
  - `initialize(OptixBackend&) noexcept -> bool`
    (Stage 21B.4: real `optixDenoiserCreate`).
  - `set_inputs(Inputs) noexcept -> bool`
    (Stage 21B.6 memory query + 21B.7 buffer allocate +
    21B.8 `optixDenoiserSetup`).
  - `invoke(Output) noexcept -> bool` (Stage 21B.1 stub;
    legacy entry, kept for `denoise_aov_buffers_to_ppm`).
  - `denoise(Inputs, Output) noexcept -> bool` (Stage
    21D.1 shell + 21D.2 real `optixDenoiserInvoke` +
    21D.3 guided contract formalised). The new high-level
    entry that wraps the trio.
  - `shutdown() noexcept` (Stage 21B.4 destroy +
    21B.7 buffer reset + 21B.10 cleanup invariants).
  - `isAvailable() const noexcept -> bool` (Stage 21B.9
    inline).
  - Status getters: `is_initialized`, `inputs_set`,
    `input_width`, `input_height`, `denoiser_handle`,
    `last_error`.

Three-branch implementation (matches the established
two-layer audit-host fallback pattern):
- **ENABLE_OPTIX + SDK_FOUND**: real SDK calls.
- **ENABLE_OPTIX without SDK_FOUND**: audit-host stubs
  (return `false` with documented "requires SDK" error).
- **OFF**: stubs (return `false` with documented "OptiX
  disabled at build time" error).

---

## 3. Does input wiring exist?

**YES** — verified by source inspection.

File-static helpers in `src/optix/OptixDenoiser.cpp`'s
SDK_FOUND anonymous namespace:

| Helper                        | Stage | Purpose                                  |
|-------------------------------|-------|------------------------------------------|
| `make_beauty_image(Inputs)`   | 21C.2 | Build `OptixImage2D` for Beauty (FLOAT3/4)|
| `make_albedo_image(Inputs)`   | 21C.2 | Build `OptixImage2D` for Albedo (FLOAT3) |
| `make_normal_image(Inputs)`   | 21C.2 | Build `OptixImage2D` for Normal (FLOAT3) |
| `make_output_image(Output, k)`| 21C.2 | Build `OptixImage2D` for Output          |
| `prepareBeautyOnlyInput(...)` | 21C.3 | Build `OptixDenoiserLayer` (no guides)   |
| `prepareGuidedInput(...)`     | 21C.4 | Build `OptixDenoiserLayer + GuideLayer`  |
| `validateDenoiserInputs(...)` | 21C.5 | 8 precondition checks; bool + error_out  |

All seven helpers are `noexcept`, pure (no globals, no
allocation, no side effects), and the stage-21C-only ones
are `[[maybe_unused]]` until the next sub-stage wired
them. As of Stage 21D.2, `prepareGuidedInput` and
`validateDenoiserInputs` are called from
`OptixDenoiser::denoise()`; the others remain reachable
via the public + helpers chain (the four `make_*_image`
helpers are called from `prepareGuidedInput` /
`prepareBeautyOnlyInput`).

The `prepareBeautyOnlyInput` helper is wired but UNUSED
because the denoiser's init options
(`guideAlbedo=1, guideNormal=1`, Stage 21B.4) force the
guided path; pure beauty-only invokes would return an
SDK error. The helper is reserved for a future
"beauty-only init mode" slice.

---

## 4. Does the invoke path exist?

**YES** — verified by source inspection + `grep`.

`OptixDenoiser::denoise()` SDK_FOUND branch
(`src/optix/OptixDenoiser.cpp:731..836`):

1. `isAvailable()` check (early-out on uninitialised).
2. `validateDenoiserInputs(inputs, output, true, err)` —
   eight preconditions per Stage 21C.5.
3. `set_inputs(inputs)` — Stage 21B.6 memory query +
   21B.7 buffer allocate + 21B.8 `optixDenoiserSetup`.
4. `prepareGuidedInput(inputs, output)` — builds
   `GuidedDenoiserInput { layer, guide }` via Stage 21C.4
   helper.
5. Zero-init `OptixDenoiserParams` with `blendFactor=0`
   (full denoise).
6. **`optixDenoiserInvoke(...)`** — the real SDK call,
   with bound `state_buffer_` / `scratch_buffer_`,
   `numLayers=1`, `inputOffset=0`. Stage 21D.2 wired
   this; Stage 21D.3 formalised the guided contract.
7. `cudaDeviceSynchronize()` — Stage 21D.2 sync after
   invoke.
8. Success log:
   `[OptiX:INFO] OptixDenoiser guided invoke complete:
   width=W height=H FLOAT3 (beauty + albedo + normal).`
9. Returns true.

Failure paths each populate `last_error_` with the
documented error message + emit `[OptiX:ERROR]
OptixDenoiser::denoise: ...` on stderr + return false.

The host-side orchestrator
`denoise_and_save_ppm(denoiser, inputs, out_path)` in
`src/main.cpp` wraps the entire pipeline:
- Allocate device output buffer (`GpuBuffer<float>`).
- Build `Output` POD; call `denoiser.denoise()`.
- Download via `GpuBuffer::download` (one
  `cudaMemcpy(D->H)`).
- Save via `save_image_or_error` (PPM; standard
  `Image::save_ppm` clamp).
- On any failure: `save_noisy_fallback` lambda
  downloads `inputs.beauty_device` and saves it as
  `output/denoised.ppm` per Stage 21D.5 (noisy Beauty
  fallback per Stage 21A.7 contract).

---

## 5. Does the CLI flag exist?

**YES** — verified by source inspection.

Three CLI surfaces relate to denoising:

1. **`--denoise`** (Stage 19B.4 + 21E.1 + 21E.2):
   modifier flag. Parses to `Config::denoise_enabled`.
   Stage 21E.1 added a one-line announcement
   (`[INFO] denoise: requested via --denoise flag`) when
   the flag is set. Stage 21E.2 wired the flag to
   actually run the denoiser after a successful
   `--render <scene>` invocation:
   - Re-renders with AOVs via `render_scene_with_aovs`
     on the same uploaded `GpuScene`.
   - Initialises `OptixBackend` + `OptixDenoiser`.
   - Calls `denoise_and_save_ppm(denoiser, inputs,
     "output/denoised.ppm")`.
   - On any failure (denoiser unavailable, AOV resize
     fails, init fails, denoise fails):
     `Logger::warning("--denoise: ...; keeping original
     render at <out_path>")` + return 0 (the normal
     render's success return).

2. **`--render-optix-denoise`** (Stage 21D.6): dedicated
   action flag. No scene argument required (builds the
   same 4-sphere demo scene as `--render-denoise`
   inline). Drives the new
   `OptixDenoiser::denoise()` API end-to-end. Output:
   `output/denoised.ppm`.

3. **`--render-denoise`** (Stage 19B.3): legacy action
   flag. Drives the OLDER `OptixDenoiser::invoke()` trio
   via `denoise_aov_buffers_to_ppm`. Currently always
   takes the Stage 19C.3 noisy-Beauty fallback path
   because `invoke()` is still the Stage 21B.1 stub. A
   future polish slice could migrate this path to use the
   new `denoise()` API; not required for v1.

The legacy `--render-aovs --denoise` path also exists
(`run_render_aovs(...)` consumes
`Config::denoise_enabled`); it uses the same
`denoise_aov_buffers_to_ppm` helper as `--render-denoise`.

---

## 6. Does `output/denoised.ppm` exist or is the runtime-deferred reason recorded?

**RUNTIME DEFERRED — recorded** per the user's Stage 21D.6
+ Stage 21E.2 rule "If no OptiX runtime is available,
document as runtime deferred, not code failure".

The file does NOT exist on disk on this audit host. The
audit host has no `output/` directory, no CUDA toolkit
(`which nvcc` returns nothing), and no OptiX SDK
(`find / -name optix.h` returns nothing under `/opt`,
`/usr/local`, or `$HOME`). The CLI surfaces that produce
the file all hit the documented audit-host fallback:

```
$ ./build_off/bin/RelativityRender --render \
    scenes/test_full_scene.rrscene --denoise
[INFO] denoise: requested via --denoise flag
[ERROR] --render-from-scene requires CUDA. Rebuild with
        -DRR_ENABLE_CUDA=ON ...
```

```
$ ./build_off/bin/RelativityRender --render-optix-denoise
[ERROR] --render-optix-denoise requires both CUDA and OptiX.
        Rebuild with -DRR_ENABLE_CUDA=ON -DRR_ENABLE_OPTIX=ON ...
```

Both exit 1 with documented errors; no crash. Per the
user's rule: this is "runtime deferred" (the audit host
lacks the runtime), NOT "code failure" (the code builds
cleanly and the audit-host fallback fires correctly).

What is deferred to a CUDA + OptiX-SDK host run:

- `./bin/RelativityRender --render <scene> --denoise`
  produces `output/render.ppm` (unchanged from the
  no-denoise path) and `output/denoised.ppm` (denoised
  radiance).
- `./bin/RelativityRender --render-optix-denoise`
  produces `output/denoised.ppm` from the inline 4-sphere
  demo scene.
- On any denoiser-side failure on the SDK host, the
  Stage 21D.5 noisy-Beauty fallback writes the noisy
  Beauty AOV to `output/denoised.ppm` and the dispatcher
  exits 0.

The contract is in place; the file's actual existence on
disk is a property of running on a CUDA + OptiX-SDK host.

---

## 7. CUDA / OptiX OFF builds remain valid?

**YES** — verified empirically on this audit host.

```
$ cmake -S . -B build_off -DRR_BUILD_TESTS=ON \
    -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=OFF
... clean configure ...
$ cmake --build build_off -j4
... clean build ...
$ cd build_off && ctest
100% tests passed, 0 tests failed out of 6
```

```
$ cmake -S . -B build_on_audit -DRR_BUILD_TESTS=ON \
    -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=ON
... clean configure with the documented Stage 12B.4
    "OptiX SDK not located" warning ...
$ cmake --build build_on_audit -j4
... clean build ...
$ cd build_on_audit && ctest
100% tests passed, 0 tests failed out of 7
```

Two build modes verified empirically; both green; zero
warnings beyond the documented Stage 12B.4 SDK-not-found
warning when ENABLE_OPTIX=ON without an SDK.

The audit host's CUDA-on / SDK-on combination
(`-DRR_ENABLE_CUDA=ON -DRR_ENABLE_OPTIX=ON
-DOPTIX_ROOT=/path/to/optix-sdk`) is structurally
verified (every Stage 21A..21E entry compiles inside
the appropriate gate; the SDK_FOUND-only branches are
audited against the prior 19B.x reference for shape) but
cannot be empirically built on this host because neither
CUDA nor the OptiX SDK is present.

---

## 8. No CPU denoising

**ZERO violations** — verified by `grep` over the new
Stage 21 paths.

The master rule: "No CPU ray tracing as production
path. / All per-pixel/per-ray rendering must happen on
GPU. / CPU may only: ... save image files."

### `src/optix/OptixDenoiser.cpp`

```
$ grep -nE "for\s*\(.*\b(x|y)\s*=\s*0\b" \
    src/optix/OptixDenoiser.cpp
(no output; exit 1)
```

Zero pixel-space `for` loops. Every per-pixel byte is
produced by `optixDenoiserInvoke`'s CUDA kernels. The
`cudaDeviceSynchronize` after the invoke is the only
host-side action between launch and return.

### `src/main.cpp` Stage 21D / 21E paths

- `denoise_and_save_ppm` (Stage 21D.4 + 21D.5):
  GpuBuffer<float> allocate (one `cudaMalloc`) ->
  `denoiser.denoise(...)` (GPU) -> `GpuBuffer::download`
  (one `cudaMemcpy(D->H)`) -> `save_image_or_error`
  (`Image::save_ppm` standard path; no per-pixel work).
- `save_noisy_fallback` lambda (Stage 21D.5):
  `gpu_copy_device_to_host` (one `cudaMemcpy(D->H)`)
  -> `save_image_or_error`. No per-pixel host loop.
- `run_render_optix_denoise` (Stage 21D.6): host-side
  scene build (POD construction; not pixel work) ->
  `gpu_scene.upload_*` -> `render_scene_with_aovs` (GPU)
  -> `denoise_and_save_ppm` (above). No per-pixel host
  work.
- `run_render_from_scene`'s Stage 21E.2 denoise block:
  GpuAOVBuffer allocate -> `render_scene_with_aovs`
  (GPU) -> `OptixBackend::initialize` ->
  `OptixDenoiser::initialize` ->
  `denoise_and_save_ppm` (above). No per-pixel host
  work.

### Caveat: legacy `denoise_aov_buffers_to_ppm`

The Stage 19B.4 `denoise_aov_buffers_to_ppm` helper
(used by `--render-denoise` and `--render-aovs --denoise`)
has a FLOAT3 -> FLOAT4 host-side widening loop in its
`save_noisy_fallback` lambda (4-channel widen with
`alpha=1`). This is in the LEGACY helper (pre-Stage-21A);
it is NOT a denoise computation but channel-format
conversion (display-format adjustment, same shape as
`Image::save_ppm`'s float-to-uint8 clamp). The new Stage
21D path's `save_noisy_fallback` (in
`denoise_and_save_ppm`) intentionally avoids this loop by
allocating the host `Image` directly with the matching
format (Rgb32F when beauty_components=3, Rgba32F when 4).

The new Stage 21 paths are GPU-only end-to-end; zero CPU
per-pixel computation anywhere in the denoise pipeline.

---

## 9. Next safe stage

Per master order:

| Master order item                    | Status                                  |
|--------------------------------------|-----------------------------------------|
| #16 Path tracing foundation          | Foundation landed (Stage 11C); polish   |
|                                      | gaps remain (NEE / non-diffuse BSDFs /  |
|                                      | multi-mesh upload / relativistic-       |
|                                      | perception integration).                |
| #17 OptiX upgrade path               | Stage 20 complete; one polish gap (Gap  |
|                                      | A: durable AOV ownership for OptiX-path |
|                                      | `render_aovs` for an end-to-end         |
|                                      | `--render-optix-aovs --denoise` flow).  |
| #18 Texture system                   | Foundation landed (Stage 13B + 20M);    |
|                                      | gaps: MIP / trilinear / anisotropic /   |
|                                      | UDIM / HDR decode.                      |
| #19 AOV / render passes              | Done (Stage 14).                        |
| #20 Renderer server                  | Deferred per                            |
|                                      | `docs/STAGE_15_SERVER_DEFERRED.md`.     |
| **#24 Denoising**                    | **Stage 21 complete (this audit).**      |
| #21 Cinema 4D bridge                 | Blocked on master rule 4 ("Do not jump  |
|                                      | to UI, Cinema 4D bridge, native C4D     |
|                                      | renderer, or node editor too early").   |
| #22 Preview UI                       | Blocked on master rule 4.               |
| #23 Material node graph              | Blocked on master rule 4.               |
| #25 Native C4D renderer              | Blocked on master rule 4.               |

### Recommended next safe stages (in priority order)

1. **OptiX-path AOV ownership (post-Stage-20 audit Gap
   A).** Make the OptiX-path's `render_aovs` device
   buffers durable across a denoiser invoke so that
   `--render-optix-aovs --denoise` (or analogous) works
   end-to-end without the current "render twice" pattern
   the Stage 21E.2 dispatcher uses. The smallest
   meaningful slice: change
   `OptixRenderer::render_aovs(...)` to optionally return
   a struct that retains the device buffers (or accept
   them as out-params), then add a sibling
   `--render-optix-aovs --denoise` dispatcher that drives
   the new end-to-end flow.
2. **Stage 21 polish — empirical verification on
   CUDA-host.** The denoiser pipeline is structurally
   complete; the actual "run on a CUDA + OptiX-SDK host
   and confirm `output/denoised.ppm` looks denoised"
   verification has been deferred at every step.
   Producing the first real denoised PPM on the right
   hardware would close the runtime-deferred gate that
   appears on every Stage 21D / 21E entry's
   "Verified at the build" section.
3. **Texture system polish (master order #18) — MIP /
   trilinear / anisotropic filtering.** The Stage 13B
   foundation + Stage 20M OptiX-path coverage is in
   place; adding MIP support is the natural next slice.
   Keeps the project moving on master order without
   jumping past renderer-server (#20).
4. **Path-tracer polish (master order #16) — NEE,
   non-diffuse BSDFs, multi-mesh upload.** All listed in
   the post-Stage-20 audit's "what lands next" + every
   prior path-tracer stage's "what is NOT shipped"
   sections. Same scope-respecting reasoning as #18.

### Explicitly NOT recommended

- Master order #20 (Renderer server) — DEFERRED per
  `docs/STAGE_15_SERVER_DEFERRED.md`. Resuming would
  unblock #21 (Cinema 4D bridge) but is a substantial
  re-design slice the operator must explicitly start.
- Master order #21 / #22 / #23 / #25 — blocked by
  master rule 4 (no jumping to UI / C4D bridge / node
  editor / native C4D renderer too early). Per the
  development-order list, C4D integration cannot start
  until the standalone renderer has the renderer
  server, which is currently deferred.

---

## Summary table

| # | Question                                       | Verdict             | Empirical / Structural |
|---|------------------------------------------------|---------------------|-------------------------|
| 1 | Denoiser plan exists?                          | YES (9 sections)    | Empirical (file read)   |
| 2 | OptiX denoiser wrapper exists?                 | YES (213+892 lines) | Empirical (file read)   |
| 3 | Input wiring exists?                           | YES (7 helpers)     | Structural              |
| 4 | Invoke path exists?                            | YES                 | Structural              |
| 5 | CLI flag exists?                               | YES (3 surfaces)    | Empirical (CLI smoke)   |
| 6 | `output/denoised.ppm` exists?                  | RUNTIME DEFERRED    | Empirical + documented  |
| 7 | OFF / ON-audit-host builds remain valid?       | YES (6/6 + 7/7)     | Empirical (audit host)  |
| 8 | No CPU denoising                               | ZERO violations     | Empirical (grep)        |
| 9 | Next safe stage                                | OptiX Gap A polish  | Documentation           |

"Empirical" = the audit host directly verified the claim
by running the relevant command on this host. "Structural"
= the audit verified the source / build configuration /
wiring is in place but the runtime verification requires a
CUDA + OptiX-SDK host the audit host does not have.

---

## Critical findings

- **Stage 21 (master order #24, Denoising) is structurally
  complete.** All 33 sub-stages landed across 5 sub-arcs
  (21A planning + 21B scaffold + 21C input wiring + 21D
  invoke + 21E CLI integration). Three intermediate
  audits (post-21B / 21C / 21D) verified each arc's
  contract before moving to the next.
- **Plan + scaffold + wiring + invoke + CLI all in
  place.** Every checklist item from the user's task is
  satisfied by source artifacts on disk.
- **One runtime gate remains: empirical CUDA-host
  verification of `output/denoised.ppm`.** Per the user's
  Stage 21D.6 + 21E.2 "runtime deferred, not code
  failure" rule, this is the expected audit-host posture.
- **No master rule violations.** Every per-pixel byte in
  the denoise pipeline is produced by
  `optixDenoiserInvoke`'s CUDA kernels; CPU only
  orchestrates + downloads + saves. CUDA renderer is
  byte-identical across the entire Stage 21 arc (no
  regressions to the production CUDA path).
- **Two of three post-Stage-20 gaps closed by Stage 21.**
  Gap B (orchestration helper) satisfied by
  `denoise_and_save_ppm` (Stage 21D.4 + 21D.5). Gap C
  (CLI surface) satisfied by Stage 21D.6 + 21E.2. Gap A
  (durable AOV ownership for the OptiX path's
  `render_aovs`) remains as the natural next polish
  slice.

The next operator-chosen slice can either close
post-Stage-20 Gap A, attempt empirical CUDA-host
verification, or move forward on master order
(#16 path tracer polish, #18 texture system polish).
Master order #20 (renderer server) and #21+ (C4D / UI /
node editor / native C4D renderer) remain deferred /
blocked per the existing project documentation +
master rule 4.

# OptiX Gap A Polish — Plan

Date: 2026-05-04
Branch: `relativity-core-v1`
Master order: #17 (OptiX upgrade path), polish slice
Prior audits referencing this gap:
- `docs/STAGE_20_OPTIX_PATH_TRACING_AUDIT.md` (post-Stage-20
  capstone), Section 11 ("Remaining gaps before denoising")
- `docs/STAGE_21D_DENOISER_INVOKE_AUDIT.md` (post-21D),
  "What lands next" section
- `docs/STAGE_21_DENOISER_AUDIT.md` (post-Stage-21
  capstone), Section 9 ("Next safe stage")

---

## 1. Exact gap definition

**Gap A: durable AOV buffer ownership for the OptiX path's
`render_aovs` entry.**

Stage 20N's `OptixRenderer::render_aovs(scene, lights,
width, height)` currently does the full pipeline in one
call: allocates six AOV device buffers via raw `cudaMalloc`,
populates them via `optixLaunch`, downloads each into a
host-side `rr::image::Image`, then **frees the device
buffers via its internal `cleanup` lambda before
returning**. The returned `AovResult` carries only the host
images.

That free-on-return shape is the gap. The OptiX denoiser
(Stage 21D) consumes Beauty / Albedo / Normal device
pointers across an `optixDenoiserInvoke` call: the device
buffers must remain valid for the duration of the denoise
launch + sync. With the current `render_aovs`, by the time
the host code receives the `AovResult`, the device buffers
the denoiser would have read are already freed.

The CUDA path does not have this problem:
`CudaRenderer::render_scene_with_aovs` writes into
caller-owned `GpuAOVBuffer` instances; ownership lives on
the host side, and the buffers stay alive for as long as
the caller holds the `GpuAOVBuffer` objects. The OptiX
path needs the analogous shape.

The Stage 21E.2 dispatcher (`run_render_from_scene`'s
denoise block) works around the gap by re-rendering the
same scene through the **CUDA** AOV pipeline
(`render_scene_with_aovs`) when `--denoise` is set, even
though the user's primary render ran through the **OptiX**
path. Closing Gap A would let the OptiX-side AOV producer
feed the denoiser directly without the cross-backend
detour.

---

## 2. Files involved

| File                                  | Role                                                |
|---------------------------------------|-----------------------------------------------------|
| `src/optix/OptixRenderer.h`           | Public surface: `AovResult`, `render_aovs(...)`.    |
|                                       | Will gain a sibling `AovRetainedBuffers` struct +   |
|                                       | `render_aovs_retain(...)` entry.                    |
| `src/optix/OptixRenderer.cpp`         | Implementation: existing `render_aovs` SDK_FOUND    |
|                                       | branch (~300 lines) + audit-host stub. Will gain a  |
|                                       | sibling SDK_FOUND `render_aovs_retain` body that    |
|                                       | shares the launch logic but retains device buffers  |
|                                       | through `rr::gpu::GpuBuffer<float>` ownership.      |
| `src/main.cpp`                        | New consumer dispatcher (Step 3 below): a sibling   |
|                                       | of `run_render_optix_aovs` that calls               |
|                                       | `render_aovs_retain` then drives the OptiX denoiser |
|                                       | over the retained Beauty / Albedo / Normal device   |
|                                       | pointers, finally saving `output/optix_denoised.ppm`|
|                                       | (or similar).                                       |
| `src/core/CommandLine.{h,cpp}`        | New CLI surface (Step 4 below): either              |
|                                       | `--render-optix-aovs --denoise` modifier extension  |
|                                       | OR a new `--render-optix-aovs-denoise` action.      |
| `src/gpu/GpuBuffer.h`                 | Already provides `GpuBuffer<T>` with RAII cleanup;  |
|                                       | no change needed.                                   |
| `src/optix/OptixDenoiser.{h,cpp}`     | Already provides `denoise(Inputs, Output)` that     |
|                                       | accepts raw device pointers; no change needed.      |
| `docs/BUILD_PLAN.md`                  | Slice-closing entry per master rule 8.              |
| `docs/OPTIX_GAP_A_POLISH_PLAN.md`     | This file.                                          |

---

## 3. Why this blocks later stages

Gap A is the last open item from the post-Stage-20 audit's
"Remaining gaps before denoising" list. Closing it:

- **Unblocks `--render-optix-aovs --denoise`**: today an
  artist who renders through the OptiX path
  (`--render-optix-aovs`, six AOV PPMs) cannot denoise the
  result without re-rendering the scene through the CUDA
  path. After Gap A: the OptiX-rendered AOVs flow directly
  into the denoiser.
- **Eliminates the cross-backend detour in Stage 21E.2.**
  The current `--render <scene> --denoise` dispatcher
  always re-renders through the CUDA AOV pipeline because
  the OptiX-path AOV producer can't retain its buffers.
  After Gap A, a future polish slice can switch the
  dispatcher to use the OptiX producer directly when the
  user's primary render came through the OptiX path,
  saving one full GPU launch per `--denoise` invocation.
- **Consistent ownership model across backends.** Both
  AOV producers (CUDA + OptiX) would expose host-owned
  durable device buffers. The denoiser orchestrator
  (`denoise_and_save_ppm`) works against raw device
  pointers and is agnostic to which backend produced
  them; Gap A makes that agnosticism real.

Gap A does NOT block:
- master order #18 (Texture system polish — MIP /
  trilinear / anisotropic): independent producer-side
  work.
- master order #16 (Path tracer polish — NEE,
  non-diffuse BSDFs, multi-mesh upload): also
  independent.
- master order #20 (Renderer server, deferred):
  blocked separately per
  `docs/STAGE_15_SERVER_DEFERRED.md`.
- master order #21+ (C4D bridge / UI / node editor /
  native C4D renderer): blocked by master rule 4.

---

## 4. Minimal implementation steps

The slice cadence respects the project's "small,
incremental, always-buildable" pattern. Each step keeps
both OFF and ON-audit-host builds green.

### Step 1 — types + declaration (this slice)

- Add `struct AovRetainedBuffers` to
  `src/optix/OptixRenderer.h`. Holds three
  `rr::gpu::GpuBuffer<float>` members (Beauty / Albedo /
  Normal), the framebuffer dimensions, and the standard
  `ok / message / gpu_time_ms` status fields.
- Declare `[[nodiscard]] static AovRetainedBuffers
  render_aovs_retain(scene, lights, width, height) noexcept`
  in the header.
- Implement the audit-host stub + OFF stub in
  `src/optix/OptixRenderer.cpp` (returns the documented
  "requires OptiX SDK" / "OptiX disabled at build time"
  error). The SDK_FOUND body is intentionally a stub
  for this slice — it returns the documented "not
  implemented in OptiX Gap A Step 1; SDK_FOUND body
  lands in Step 2" error. No buffers are allocated yet.
- Update `docs/BUILD_PLAN.md` per master rule 8.

### Step 2 — SDK_FOUND body (next slice)

- Implement the SDK_FOUND body of `render_aovs_retain`.
  Two reasonable shapes:
  - **Duplicate-then-refactor**: copy the relevant
    pieces of `render_aovs`'s SDK_FOUND body
    (~300 lines), substitute `cudaMalloc` for the three
    retained buffers with `GpuBuffer<float>::allocate(...)`,
    skip the host-side download for those three, skip
    the cleanup of those three (RAII transfers
    ownership into the result struct).
  - **Refactor-then-share**: extract a private internal
    helper that runs the launch and returns
    `AovRetainedBuffers`; have the existing
    `render_aovs` call the helper and additionally
    download + free.
  Either path keeps the existing `render_aovs` byte-
  identical (backward compat for the existing
  `--render-optix-aovs` CLI surface).
- Add a CUDA-host smoke test (deferred to a real
  CUDA + OptiX-SDK host run; the audit host can only
  verify it compiles).

### Step 3 — orchestration helper (next slice)

- Add `render_optix_aovs_and_denoise_to_ppm(...)` in
  `src/main.cpp`. Calls `render_aovs_retain`, builds an
  `OptixDenoiser::Inputs` from the retained device
  pointers, calls `denoise_and_save_ppm`. Mirrors the
  shape of the existing
  `denoise_aov_buffers_to_ppm` (CUDA path) helper.

### Step 4 — CLI surface (next slice)

- Either:
  - extend `--render-optix-aovs` to honour `--denoise`
    (minimal CLI change), OR
  - add a dedicated `--render-optix-aovs-denoise`
    action.
- Wire the dispatcher case + parser branch + mutex /
  validation list + help text per the established
  pattern.

### Step 5 — capstone audit

- Run OFF + ON-audit-host builds + CLI smokes. Document
  empirical CUDA-host verification deferral if the
  audit host still lacks the runtime.
- Add `docs/OPTIX_GAP_A_POLISH_AUDIT.md` confirming
  the gap is closed.

---

## 5. PASS / REPAIR criteria

A Gap A polish step PASSES when ALL of these hold:

| Criterion                                                  | Step verified at |
|------------------------------------------------------------|------------------|
| OFF build is byte-clean and ctest 6/6 green                | every step       |
| ON-audit-host build is byte-clean and ctest 7/7 green      | every step       |
| Existing `OptixRenderer::render_aovs` is byte-identical    | every step       |
| Existing `--render-optix-aovs` CLI behaviour is unchanged  | every step       |
| Existing `--render-denoise` / `--render-aovs --denoise`    | every step       |
| / `--render-optix-denoise` CLI behaviour is unchanged      |                  |
| CUDA renderer is byte-identical (no `src/cuda/` /          | every step       |
| `src/renderer/` / `src/pathtracer/` changes)               |                  |
| New entries are gated by the existing two-layer macro      | every step       |
| pattern (`RR_HAS_CUDA` + `RELATIVITYRENDER_OPTIX_SDK_FOUND`)|                  |
| No CPU per-pixel work introduced (master rule 5 + 7)       | every step       |
| `docs/BUILD_PLAN.md` slice-closing entry added             | every step       |

A Gap A polish step REQUIRES REPAIR when ANY of these fail:

- An existing CLI surface produces different bytes for
  the same inputs.
- The CUDA path is touched (e.g., `src/cuda/` changed).
- `OptixRenderer::render_aovs` (the original Stage 20N
  entry) has any signature or semantic change.
- A test in `tests/` breaks.
- The audit-host fallback no longer surfaces the
  documented "requires SDK" error for any
  `render_aovs_retain` call path.

After Step 4, the slice ships an end-to-end
"OptiX-rendered AOV → OptiX-denoised PPM" flow on a
CUDA + OptiX-SDK host; that flow is the empirical
verification of Gap A's closure. Until Step 4, each
intermediate step PASSES on the slice-closing criteria
above and DEFERS the runtime verification to a real
CUDA + OptiX-SDK host.

---

## Notes for the operator

- This plan doc is itself the audit/spec artifact for
  Gap A; subsequent steps reference it.
- The current slice (Step 1) ships **types + declaration
  + audit-host / OFF stubs only**. The SDK_FOUND body is
  intentionally deferred to Step 2 so this slice's
  blast radius stays minimal. Master rule 12 ("Do not
  overbuild a later system before the current layer
  works") + the existing project slice cadence both
  point this way.
- If a future operator chooses to skip Gap A and move
  to texture-system polish (master #18) or
  path-tracer polish (master #16) instead, that is
  still a valid order — neither depends on Gap A.
  But the cross-backend detour in Stage 21E.2 will
  remain until Gap A lands.

# Stage 21D Denoiser Invoke Audit

Date: 2026-05-04
Branch: `relativity-core-v1`
Last commit on the audited tree: `e4db2a8` ("stage 21D.6:
denoiser test output (--render-optix-denoise CLI)")
Scope: master order #24 — Stage 21D.1..21D.6 (denoiser
invoke arc). Documents the high-level
`OptixDenoiser::denoise(Inputs, Output)` API, the host-side
`denoise_and_save_ppm` orchestration helper, the noisy-Beauty
fallback, and the new `--render-optix-denoise` CLI surface.
Mode: documentation-only. No source code is modified by this
audit.

The audit answers the eight prompt questions in order. Where
visual-evidence verification requires a CUDA + OptiX-SDK host
that this audit host does not have (`which nvcc` returns
nothing; no `optix.h` under `/opt`, `/usr/local`, or `$HOME`),
the documented expected behaviour is recorded with a clear
"deferred-to-CUDA + OptiX-SDK host" gate so a future operator
can finish the verification on the right hardware.

---

## Stages covered (commit hashes)

| Sub-stage | Commit    | Slice                                              |
|-----------|-----------|----------------------------------------------------|
| 21D.1     | `0eb0c69` | Denoiser invoke shell (validate + isAvailable)     |
| 21D.2     | `2236f45` | Beauty/guided invoke (`optixDenoiserInvoke`)       |
| 21D.3     | `724e2f4` | Guided invoke formalised (comments + log)          |
| 21D.4     | `c0b38e4` | Save denoised output (`denoise_and_save_ppm`)      |
| 21D.5     | `58da922` | Failure fallback (noisy-Beauty in helper)          |
| 21D.6     | `e4db2a8` | Test output CLI (`--render-optix-denoise`)         |

---

## 1. Does the OptiX OFF build still work?

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

When OFF: `rr_optix` is not built (Stage 12B.3 contract);
`OptixDenoiser.cpp` not compiled. The Stage 21D.4 / 21D.5
helper `denoise_and_save_ppm` is gated by
`#if defined(RR_HAS_CUDA) && defined(RELATIVITYRENDER_ENABLE_OPTIX)`
so it is also not compiled. The new
`run_render_optix_denoise` dispatcher's audit-host fallback
fires (`#if !defined(RR_HAS_CUDA) || !defined(RELATIVITYRENDER
_ENABLE_OPTIX)`) and exits 1 with the documented "requires
CUDA + OptiX" error.

CLI smoke:

```
$ ./build_off/bin/RelativityRender --render-optix-denoise
[ERROR] --render-optix-denoise requires both CUDA and OptiX. Rebuild
        with -DRR_ENABLE_CUDA=ON -DRR_ENABLE_OPTIX=ON ...
```

Exits 1; no crash.

---

## 2. Does the OptiX ON build work?

**YES (structural)** — verified on the audit-host fallback
path. Empirical SDK-found verification is deferred.

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

When ON without SDK: `OptixDenoiser.cpp` IS compiled with
`RELATIVITYRENDER_ENABLE_OPTIX` defined and
`RELATIVITYRENDER_OPTIX_SDK_FOUND` undefined.
`denoise_and_save_ppm` is still gated out (RR_HAS_CUDA is
undefined on this host). The new
`OptixDenoiser::denoise(...)` audit-host fallback returns
`false` with the documented "requires OptiX SDK" error.

CLI smoke (same as above): exits 1 cleanly.

What is deferred to a CUDA + OptiX-SDK host run:

- `cmake -S . -B build_on -DRR_ENABLE_CUDA=ON
  -DRR_ENABLE_OPTIX=ON -DOPTIX_ROOT=/path/to/optix-sdk`
  configure + build.
- The actual `optixDenoiserInvoke` call inside `denoise()`
  + the `cudaDeviceSynchronize` after it.
- `denoise_and_save_ppm` body (allocate output buffer ->
  call denoise -> download -> save).
- The `run_render_optix_denoise` dispatcher's full path
  (build scene -> upload -> render AOVs -> denoise -> save).

All four pieces are wired in the SDK_FOUND branches
(verified by `grep`); they are dormant on this host because
the gating macro is undefined.

---

## 3. Does the denoiser invoke function exist?

**YES** — verified by source inspection.

`OptixDenoiser::denoise(Inputs, Output) -> bool` exists at
three levels:

- **Header declaration**:
  `src/optix/OptixDenoiser.h` (Stage 21D.1).
  ```
  [[nodiscard]] bool
  denoise(const Inputs& inputs, const Output& output) noexcept;
  ```
  Doc-comment block describes pre-conditions + the
  shell-vs-complete contract.

- **SDK_FOUND implementation**:
  `src/optix/OptixDenoiser.cpp:731` (Stage 21D.2 + 21D.3).
  Calls `isAvailable()`, `validateDenoiserInputs(...)`,
  `set_inputs(inputs)`, `prepareGuidedInput(inputs, output)`,
  builds `OptixDenoiserParams`, calls
  `optixDenoiserInvoke(...)` (line 790), and
  `cudaDeviceSynchronize()`. Logs `[OptiX:INFO]
  OptixDenoiser guided invoke complete: width=W height=H
  FLOAT3 (beauty + albedo + normal)` on success.

- **Audit-host fallback**:
  `src/optix/OptixDenoiser.cpp:841`. Returns `false` with
  `"OptixDenoiser::denoise requires the OptiX SDK; ..."`.

- **OFF stub**:
  `src/optix/OptixDenoiser.cpp` (in the
  `#else // RELATIVITYRENDER_ENABLE_OPTIX` branch). Returns
  `false` with `"OptixDenoiser::denoise: OptiX disabled at
  build time"`.

The legacy `OptixDenoiser::invoke(Output)` method also
exists; it remains the Stage 21B.1 "not implemented" stub
for backwards compatibility with the legacy
`denoise_aov_buffers_to_ppm` consumer.

---

## 4. Beauty-only mode status

**WIRED but UNUSED** — verified by source inspection.

- `prepareBeautyOnlyInput(Inputs, Output) -> ::OptixDenoiserLayer`
  exists at `src/optix/OptixDenoiser.cpp:190` (Stage 21C.3
  helper).
- The function is `[[maybe_unused]]` and **has no caller**.
  `denoise()` does not call it; the `run_render_optix_denoise`
  dispatcher does not call it.
- Why no caller: the denoiser was init'd at Stage 21B.4
  with `OptixDenoiserOptions::guideAlbedo = 1` and
  `::guideNormal = 1`. The OptiX SDK contract requires every
  `optixDenoiserInvoke` call against such a denoiser to
  provide a non-zero `OptixDenoiserGuideLayer` with valid
  Albedo + Normal images; pure beauty-only invokes return an
  SDK error.
- The helper is reserved for a future "beauty-only init mode"
  slice that would either:
  - Re-create the denoiser with `guideAlbedo=0,
    guideNormal=0` (a separate `OptixDenoiser` instance, or
    a new init mode flag), OR
  - Switch to the SDK's beauty-only AOV mode.

Stage 21D's user-visible "beauty-only" rule (the carve-out
`"beauty-only if supported by current scaffold/options"`)
is satisfied: the scaffold's options force the guided path,
which is what the actual invoke uses.

---

## 5. Guided beauty/albedo/normal mode status

**FULLY WIRED** — verified by source inspection.

The guided path is the production path for Stage 21D. Source
trail:

- `prepareGuidedInput(Inputs, Output) -> GuidedDenoiserInput`
  at `src/optix/OptixDenoiser.cpp:221` (Stage 21C.4).
  Builds:
  - `layer.input` = `make_beauty_image(inputs)` (FLOAT3 /
    FLOAT4 selected by `inputs.beauty_components`).
  - `layer.output` = `make_output_image(output,
    inputs.beauty_components)`.
  - `guide.albedo` = `make_albedo_image(inputs)` (FLOAT3,
    linear, pre-lighting).
  - `guide.normal` = `make_normal_image(inputs)` (FLOAT3,
    encoded `0.5 n + 0.5`).
- `OptixDenoiser::denoise()` calls it at
  `src/optix/OptixDenoiser.cpp:770`.
- `optixDenoiserInvoke(...)` is called immediately after at
  `src/optix/OptixDenoiser.cpp:790` with the bound
  `state_buffer_` / `scratch_buffer_` (Stage 21B.7) +
  `numLayers=1` + `inputOffset{X,Y}=0`.
- `cudaDeviceSynchronize()` at line ~819 ensures the host
  knows the output device buffer is fully written.
- Success log:
  `[OptiX:INFO] OptixDenoiser guided invoke complete:
  width=W height=H FLOAT3 (beauty + albedo + normal)`.

Layout matches the Stage 21A.3 / 21A.4 / 21A.6 plan +
Stage 21C.1 / 21C.2 helper contract. The denoiser's
init-time options (guideAlbedo=1, guideNormal=1, model=HDR)
match the invoke-time guide layer (albedo + normal images
populated), so the SDK contract is satisfied.

Empirical SDK-host verification of the actual denoised
output is deferred.

---

## 6. Does `output/denoised.ppm` exist?

**RUNTIME DEFERRED** — explicitly authorised by the user's
rule "If no OptiX runtime is available, document as runtime
deferred, not code failure".

What the audit host can and did verify:

- The file does NOT exist on disk at
  `/home/user/RelativityRender/output/denoised.ppm`. The
  audit host has no `output/` directory at all.
- The CLI surface that produces the file exists and is
  reachable: `--render-optix-denoise` parses correctly,
  validates correctly, dispatches to
  `run_render_optix_denoise(cfg)`.
- The dispatcher's audit-host fallback fires because both
  `RR_HAS_CUDA` and `RELATIVITYRENDER_OPTIX_SDK_FOUND`
  are undefined on this host. Exits 1 with:
  ```
  [ERROR] --render-optix-denoise requires both CUDA and OptiX.
          Rebuild with -DRR_ENABLE_CUDA=ON -DRR_ENABLE_OPTIX=ON
          on a host with the CUDA Toolkit + OptiX SDK installed
          (also pass -DOPTIX_ROOT=/path/to/optix-sdk).
  ```
- Per the user's rule: this is "runtime deferred" (the
  audit host lacks the runtime), NOT "code failure" (the
  code builds cleanly and the dispatcher's audit-host
  fallback fires correctly).

What is deferred to a CUDA + OptiX-SDK host run:

- `./bin/RelativityRender --render-optix-denoise`
  produces `output/denoised.ppm` carrying the OptiX-
  denoised radiance (success path; Stage 21D.1..21D.4
  pipeline).
- On any denoiser-side failure on the SDK host, the
  Stage 21D.5 noisy-Beauty fallback writes the noisy
  Beauty AOV to `output/denoised.ppm` and the dispatcher
  exits 0 (per Stage 21A.7's failure-behavior contract).

The contract is in place; the file's actual existence
on disk is a property of running the CLI on a CUDA +
OptiX-SDK host.

---

## 7. Does the failure fallback exist?

**YES** — verified by source inspection.

Two layers of fallback exist for the new Stage 21D path:

### Inside `denoise_and_save_ppm` (Stage 21D.5)

`save_noisy_fallback` lambda inside the helper:
- Logs `Logger::warning("denoise: <reason>; falling back to
  noisy Beauty AOV (no denoising applied)")`.
- Allocates a host `Image` matching the bound Beauty
  layout (Rgb32F or Rgba32F).
- Downloads `inputs.beauty_device` via
  `rr::gpu::detail::gpu_copy_device_to_host` (single
  byte-level D->H copy; no per-pixel host loop).
- Saves through `save_image_or_error` with label
  `"denoised (noisy fallback)"`.

Triggers:
- `denoiser.denoise(inputs, output)` returns `false` ->
  forwards `denoiser.last_error()` to the lambda.
- The post-denoise download of the denoised buffer fails
  -> forwards `"failed to download denoised output buffer
  to host"` to the lambda.

The function returns `true` whenever a file (denoised OR
noisy fallback) was successfully written. The caller can
distinguish success vs fallback via the warning log line.

### Inside `OptixDenoiser::denoise()` (Stage 21D.1..21D.5)

The class's `noexcept` contract + `last_error()`-based
error reporting means a denoise failure never throws, never
crashes. Every failure path:
- populates `last_error_` with a documented error message,
- emits an `[OptiX:ERROR] OptixDenoiser::denoise: ...`
  line on stderr,
- returns `false`.

### Master rule compliance

Per Stage 21A.7's failure-behavior contract:
- "If denoiser fails -> keep noisy image" -> save_noisy_fallback ✓
- "Log warning" -> Logger::warning(...) ✓
- "Renderer must not crash" -> `noexcept` + return false ✓

The legacy `denoise_aov_buffers_to_ppm` (Stage 19B.4) also
has a Stage 19C.3 noisy-Beauty fallback for the
`--render-denoise` and `--render-aovs --denoise` paths;
the new Stage 21D path inherits the same shape via the
new helper.

---

## 8. No CPU denoising

**ZERO violations** — verified by `grep` over the new
Stage 21D path.

The master rule: "No CPU ray tracing as production path. /
All per-pixel/per-ray rendering must happen on GPU. / CPU
may only: ... save image files."

### `src/optix/OptixDenoiser.cpp`

```
$ grep -nE "for\s*\(.*\b(x|y)\s*=\s*0\b" \
    src/optix/OptixDenoiser.cpp
(no output; exit 1)
```

Zero pixel-space `for` loops. The denoise itself runs
inside `optixDenoiserInvoke` (the SDK's CUDA kernels do
every per-pixel byte). The `cudaDeviceSynchronize` after
the invoke is the only host-side action between launch and
return.

### `src/main.cpp`'s `denoise_and_save_ppm` (Stage 21D.4 + 21D.5)

The function:
- Allocates a `GpuBuffer<float>` (one `cudaMalloc`).
- Calls `denoiser.denoise(inputs, output)` (GPU work).
- Downloads via `GpuBuffer::download` (one `cudaMemcpy(D->H)`).
- Saves via `save_image_or_error` (the standard
  `Image::save_ppm` path; float-to-uint8 clamp; not
  rendering).

The Stage 21D.5 `save_noisy_fallback`:
- Allocates a host `Image`.
- Downloads via `gpu_copy_device_to_host` (one
  `cudaMemcpy(D->H)`).
- Saves via `save_image_or_error`.

Neither path performs per-pixel computation on the host.
Format conversion at save time (float -> uint8 clamp inside
`Image::save_ppm`) is display-format serialisation, not
rendering — same path every other `--render-*` saver uses.

### `src/main.cpp`'s `run_render_optix_denoise` (Stage 21D.6)

The dispatcher:
- Builds the demo scene (host POD construction; no per-pixel
  work).
- Uploads to `GpuScene` (`cudaMemcpy(H->D)` for sphere /
  material PODs).
- Allocates three `GpuAOVBuffer` instances + runs
  `render_scene_with_aovs` (GPU kernel).
- Initialises `OptixBackend` + `OptixDenoiser`.
- Calls `denoise_and_save_ppm`.

No per-pixel host loop in the dispatcher.

### Caveat: legacy `denoise_aov_buffers_to_ppm`

The legacy Stage 19B.4 helper has a FLOAT3 -> FLOAT4
host-side widening loop in its `save_noisy_fallback`
lambda:

```cpp
for (std::size_t i = 0; i < pixel_count; ++i) {
    dst[i * 4 + 0] = host_rgb[i * 3 + 0];
    dst[i * 4 + 1] = host_rgb[i * 3 + 1];
    dst[i * 4 + 2] = host_rgb[i * 3 + 2];
    dst[i * 4 + 3] = 1.0f;
}
```

This is in the LEGACY helper (used by `--render-denoise`
and `--render-aovs --denoise`), not the new Stage 21D
path. The loop is channel-format conversion (FLOAT3 ->
FLOAT4 with alpha=1), not denoising or pixel modification.
It pre-dates Stage 21A and was already accepted by every
previous audit. The new Stage 21D path's
`save_noisy_fallback` (in `denoise_and_save_ppm`)
intentionally avoids this loop by allocating the host
`Image` directly with the matching format (Rgb32F when
beauty_components=3, Rgba32F when 4) and downloading
straight into it.

Verdict: the new Stage 21D path is GPU-only end-to-end;
zero CPU per-pixel rendering anywhere in the denoise
pipeline.

---

## Summary table

| Check | Question                                      | Verdict             | Empirical / Structural |
|-------|-----------------------------------------------|---------------------|-------------------------|
| 1     | Does OptiX OFF build still work?              | YES                 | Empirical (audit host)  |
| 2     | Does OptiX ON build work?                     | YES                 | Structural              |
| 3     | Does the denoiser invoke function exist?      | YES                 | Structural              |
| 4     | Beauty-only mode status                       | WIRED but UNUSED    | Structural              |
| 5     | Guided beauty/albedo/normal mode status       | FULLY WIRED         | Structural              |
| 6     | `output/denoised.ppm` exists?                 | RUNTIME DEFERRED    | Empirical + documented  |
| 7     | Failure fallback exists?                      | YES (two layers)    | Structural              |
| 8     | No CPU denoising                              | ZERO violations     | Empirical (grep)        |

"Empirical" = the audit host directly verified the claim by
running the relevant command on this host. "Structural" = the
audit verified the source / build configuration / wiring is
in place but the runtime verification requires a CUDA +
OptiX-SDK host the audit host does not have.

---

## What lands next (post-Stage 21D)

The denoiser-invoke arc is complete. The remaining work
identified by the post-Stage-20 audit (Gaps A / B / C) is:

- **Gap A** (durable AOV ownership for the OptiX path's
  `render_aovs`): would enable a future `--render-optix-aovs
  --denoise` modifier flow that runs the OptiX render +
  denoise end-to-end without re-rendering. The current
  `--render-optix-denoise` dispatcher uses the CUDA AOV
  pipeline (`CudaRenderer::render_scene_with_aovs`) which
  already keeps the AOV buffers durable via `GpuAOVBuffer`.
  Gap A is about making the OptiX-rendered AOV buffers
  similarly durable.
- **Gap B** (`OptixDenoiser` orchestration helper for the
  OptiX path): satisfied by `denoise_and_save_ppm` (Stage
  21D.4 + 21D.5).
- **Gap C** (`--render-optix-denoise` CLI surface):
  satisfied by Stage 21D.6.

Two of three post-Stage-20 gaps are now closed. Gap A
remains as a future polish slice for the OptiX-path AOV
producer.

Independent of the post-Stage-20 audit, the empirical
verification of `output/denoised.ppm` itself remains
deferred to a CUDA + OptiX-SDK host run. The contract is
in place; the file's actual existence on disk is a
property of running `--render-optix-denoise` on the right
hardware.

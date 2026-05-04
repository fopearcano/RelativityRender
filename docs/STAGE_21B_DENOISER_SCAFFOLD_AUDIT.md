# Stage 21B Denoiser Scaffold Audit

Date: 2026-05-04
Branch: `relativity-core-v1`
Last commit on the audited tree: `93ca434` ("stage 21B.10:
denoiser cleanup")
Scope: master order #24 — Stage 21A.1..21A.10 (planning) +
Stage 21B.1..21B.10 (scaffold). The implementation arc that
wired `optixDenoiserCreate` / `Destroy` /
`ComputeMemoryResources` / `Setup` and the supporting
`GpuBuffer` machinery, but stops short of the actual
`optixDenoiserInvoke` call.
Mode: documentation-only. No source code is modified by this
audit.

The audit answers the six prompt questions in order. Where
visual-evidence verification requires a CUDA + OptiX-SDK host
that this audit host does not have (`which nvcc` returns
nothing; no `optix.h` under `/opt`, `/usr/local`, or `$HOME`),
the documented expected behaviour is recorded with a clear
"deferred-to-CUDA + OptiX-SDK host" gate so a future operator
can finish the verification on the right hardware.

---

## Stages covered (commit hashes)

### Planning arc (Stage 21A)

| Sub-stage | Commit    | Slice                    |
|-----------|-----------|--------------------------|
| 21A.1     | `4d08f96` | Purpose                  |
| 21A.2     | `4dc4522` | Backend                  |
| 21A.3     | `1971110` | Required inputs          |
| 21A.4     | `f7049cb` | Optional inputs          |
| 21A.5     | `1f54491` | Pipeline position        |
| 21A.6     | `8050f79` | Output                   |
| 21A.7     | `914fde5` | Failure behavior         |
| 21A.8     | `22b39ed` | Modes                    |
| 21A.9     | `a8ec7ca` | v1 scope                 |
| 21A.10    | `871052f` | Plan complete (Status)   |

### Scaffold arc (Stage 21B)

| Sub-stage | Commit    | Slice                                         |
|-----------|-----------|-----------------------------------------------|
| 21B.1     | `8d36ca9` | Files (minimal class skeleton)                |
| 21B.2     | `fba3b73` | Compile guards                                |
| 21B.3     | `0174a99` | Include OptiX headers (SDK-gated)             |
| 21B.4     | `0e97137` | Object (handle creation via optixDenoiserCreate) |
| 21B.5     | `c9e970a` | Init function (logging)                       |
| 21B.6     | `a229cce` | Memory requirements (optixDenoiserComputeMemoryResources) |
| 21B.7     | `a9be1f9` | Buffer allocation (GpuBuffer<std::byte>)      |
| 21B.8     | `bb1edf4` | Setup (optixDenoiserSetup)                    |
| 21B.9     | `042bedf` | Availability (isAvailable())                  |
| 21B.10    | `93ca434` | Cleanup (formalized invariants + log)         |

---

## 1. Files exist?

**YES** — verified empirically on this audit host.

```
$ ls -la src/optix/OptixDenoiser.{h,cpp}
-rw-r--r-- src/optix/OptixDenoiser.cpp   18 632 bytes (480 lines)
-rw-r--r-- src/optix/OptixDenoiser.h      4 689 bytes (122 lines)
```

Both files compile cleanly; `git log` confirms the Stage
21B.1..21B.10 commit chain landed every sub-stage on
`relativity-core-v1`. The header carries the public surface
the existing consumers (`denoise_aov_buffers_to_ppm` in
`src/main.cpp`) call through; the .cpp carries the
implementation (audit-host fallback + SDK_FOUND branches).

The prior 1200-line Stage 19A planning artifact + 596+300 line
Stage 19B.1..19C.3 implementation are preserved in git history
at `fcd90bd^:docs/DENOISER_PLAN.md` and
`0445c47:src/optix/OptixDenoiser.{h,cpp}` respectively.

---

## 2. Does the OptiX OFF build work?

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

When OFF, `rr_optix` is not built (Stage 12B.3 contract);
`OptixDenoiser.cpp` is therefore not compiled. Consumer
references inside `denoise_aov_buffers_to_ppm` are gated by
`#ifdef RELATIVITYRENDER_ENABLE_OPTIX`, so main.cpp builds
without depending on the (not-built) `rr_optix` symbols.
`isAvailable()` (Stage 21B.9) is defined inline in the header
and reports `false` constant in OFF mode.

CLI smoke on the OFF audit host:

```
$ ./build_off/bin/RelativityRender --render-denoise
[ERROR] --render-denoise requires both CUDA and OptiX. Rebuild with
        -DRR_ENABLE_CUDA=ON -DRR_ENABLE_OPTIX=ON ...
```

Exits 1 with the documented error; no crash.

---

## 3. Does the OptiX ON build work?

**YES (structural)** — verified on the audit-host fallback
path. Empirical SDK-found verification is deferred.

What the audit host can and did verify:

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

When ON without SDK, `rr_optix` is built;
`OptixDenoiser.cpp` is compiled with
`RELATIVITYRENDER_ENABLE_OPTIX` defined and
`RELATIVITYRENDER_OPTIX_SDK_FOUND` undefined. The audit-host
fallback branch fires for every method:
- `initialize(backend)` returns `false` with the
  `"OptixDenoiser::initialize requires the OptiX SDK; ..."`
  error.
- `set_inputs(inputs)` returns `false` with the matching
  "requires SDK" error.
- `invoke(output)` returns `false` with the Stage 21B.1
  "not implemented" stub.
- `shutdown()` is a no-op (no allocated resources to free).
- `isAvailable()` returns `false` (because `initialized_`
  stays false).

What is deferred to a CUDA + OptiX-SDK host run:

- `cmake -S . -B build_on -DRR_ENABLE_CUDA=ON
  -DRR_ENABLE_OPTIX=ON -DOPTIX_ROOT=/path/to/optix-sdk`
  configure + build, where
  `RELATIVITYRENDER_OPTIX_SDK_FOUND` IS defined and the
  SDK_FOUND branches of every method get exercised.
- The actual `optixDenoiserCreate`,
  `optixDenoiserComputeMemoryResources`,
  `optixDenoiserSetup`, and `optixDenoiserDestroy`
  calls. All four are wired into the SDK_FOUND branches
  (verified by `grep`); they are dormant on this host
  because the gating macro is undefined.

---

## 4. Does the denoiser initialize without crash?

**YES (no-crash invariants in place)** — verified by source
inspection + audit-host CLI smoke.

Audit-host CLI smoke (the only path the audit host can run):

```
$ ./build_on_audit/bin/RelativityRender --render-denoise
[ERROR] --render-denoise requires both CUDA and OptiX. ...
```

Exits 1 with the documented error; no crash. The check
fires before reaching the OptiX path, but it confirms the
binary itself runs without crashing.

Source inspection (the SDK_FOUND branch's no-crash
invariants):

- `OptixDenoiser::initialize(backend)`
  (`src/optix/OptixDenoiser.cpp:222..262`):
    - Idempotent early-out if already initialized
      (`if (initialized_) return true`).
    - Validates `backend.isInitialized()` and the device-
      context accessor; on either failure populates
      `last_error_` + logs `[OptiX:ERROR] denoiser init
      failed: ...` + returns false. No SDK call is made.
    - On `optixDenoiserCreate` non-OK return: populates
      `last_error_` with `optixGetErrorName(res)`, logs the
      error, returns false. No half-allocated state is
      kept.
- `OptixDenoiser::~OptixDenoiser()` and `shutdown()`
  (`src/optix/OptixDenoiser.cpp:114..195`): the Stage
  21B.10 cleanup contract documents the no-leak / no-crash
  invariants explicitly:
    - `optixDenoiserDestroy` is null-guarded.
    - `GpuBuffer.reset()` is documented as safe on empty /
      moved-from / never-allocated buffers.
    - Every member reset is a trivial scalar / pointer
      store.
    - `shutdown()` is `noexcept`; the destructor is a single
      delegating call.
    - Repeated `shutdown` calls are idempotent (members are
      already empty after the first call).
- `set_inputs(inputs)` and the buffer allocation in Stage
  21B.7 follow the same pattern: any allocation failure
  rolls back both `state_buffer_` and `scratch_buffer_`
  before returning false, so the class never holds a
  partial allocation.

Empirical SDK-found "no crash" verification deferred to a
CUDA + OptiX-SDK host run.

---

## 5. No image processing yet?

**YES** — confirmed by source inspection.

The class is a scaffold. The image-processing path
(`optixDenoiserInvoke`) and the input-descriptor binding
(`OptixImage2D` triplet) are explicitly NOT implemented.

What is wired (Stage 21B.1..21B.10):

| OptiX SDK call                          | Status                                          |
|-----------------------------------------|-------------------------------------------------|
| `optixDenoiserCreate`                   | Wired in `initialize()` (Stage 21B.4)            |
| `optixDenoiserComputeMemoryResources`   | Wired in `set_inputs()` (Stage 21B.6)            |
| `optixDenoiserSetup`                    | Wired in `set_inputs()` (Stage 21B.8)            |
| `optixDenoiserDestroy`                  | Wired in `shutdown()` (Stage 21B.4 + 21B.10 log) |
| `optixDenoiserInvoke`                   | **NOT WIRED** — `invoke()` returns the Stage     |
|                                         | 21B.1 "not implemented" stub.                    |

What is NOT wired:

- `OptixImage2D` descriptor triplet construction. The
  `input_images_` member is declared (`void*`) but stays
  `nullptr` throughout the class lifetime. Stage 21B.6 +
  21B.8 set `inputs_set_ = true` after the memory query +
  setup but the descriptors themselves are still empty.
- `optixDenoiserInvoke`. The `invoke(output)` method's ON
  branch currently returns
  `"OptixDenoiser::invoke: not implemented in Stage 21B.1."`
  unchanged across Stage 21B.1..21B.10.
- Any output-buffer allocation. The denoiser writes into a
  caller-supplied `Output::device` pointer; even when
  `invoke` is wired, the class will not own the output
  buffer.

Observable behaviour:

- On the audit host, `denoise_aov_buffers_to_ppm` reaches
  `denoiser.set_inputs(inputs)` which returns `false` with
  the audit-host "requires SDK" error → consumer takes the
  Stage 19C.3 noisy-Beauty fallback path → `output/denoised.ppm`
  contains the noisy Beauty AOV.
- On a hypothetical SDK-found host, `set_inputs` would now
  succeed (memory query + buffer allocation + setup all
  ready) but `invoke` still returns false with "not
  implemented" → consumer still takes the noisy-Beauty
  fallback path → user-visible image is unchanged from the
  audit-host case.

The user's Stage 21B contract is satisfied: the scaffold is
in place; no image processing happens in this scope.

---

## 6. Any CPU rendering violations?

**ZERO violations** — verified by `grep` over
`src/optix/OptixDenoiser.{h,cpp}`.

The master rule says "no CPU per-pixel or per-ray work as the
production path". The audit looked for host-side loops over
the framebuffer (`for (y = 0; y < height; ...)` patterns)
inside the denoiser scaffold.

```
$ grep -nE "for\s*\(.*\b(x|y)\s*=\s*0\b" \
    src/optix/OptixDenoiser.{h,cpp}
(no output; exit 1)
```

Zero pixel-space `for` loops in either file.

The denoiser scaffold by its nature does not contain pixel-
level code: every per-pixel byte in the eventual output
buffer is produced by the OptiX denoiser kernels on the
device. The host orchestrates the launch (will, at
`optixDenoiserInvoke` time) and synchronises; downloading the
denoised output and saving it to PPM are the caller's
responsibility, with display-format conversion done in
`Image::save_ppm` (a one-shot serialisation, not rendering).

`GpuBuffer<std::byte>` allocations (Stage 21B.7) forward
directly to `cudaMalloc`; no host iteration over the device
memory happens at any point in the scaffold.

The CUDA path is byte-identical to its pre-Stage-21A state:
`git diff 4d08f96~1..93ca434 --stat -- src/cuda/ src/renderer/
src/pathtracer/` shows zero changes across the entire Stage
21A + Stage 21B arc.

---

## Summary table

| Check | Question                              | Verdict          | Empirical / Structural |
|-------|---------------------------------------|------------------|-------------------------|
| 1     | Files exist?                          | YES              | Empirical (audit host)  |
| 2     | Does OptiX OFF build work?            | YES              | Empirical (audit host)  |
| 3     | Does OptiX ON build work?             | YES              | Structural              |
| 4     | Does denoiser init without crash?     | YES (invariants) | Structural + smoke      |
| 5     | No image processing yet?              | YES              | Structural (grep)       |
| 6     | Any CPU rendering violations?         | ZERO             | Empirical (grep)        |

"Empirical" = the audit host directly verified the claim by
running the relevant command on this host. "Structural" = the
audit verified the source / build configuration / wiring is
in place but the runtime verification requires a CUDA +
OptiX-SDK host the audit host does not have.

---

## What lands next (post-Stage 21B)

The scaffold's only missing wiring is `optixDenoiserInvoke`
+ the `OptixImage2D` descriptor triplet that feeds it. The
implementation slice ("Stage 21C" or whichever sub-stage the
operator chooses next) needs to:

1. Build three `OptixImage2D` descriptors (Beauty / Albedo /
   Normal) from the `Inputs::*_device` pointers + dimensions
   + `beauty_components`. Store them in `input_images_`
   inside `set_inputs()`.
2. Build the `OptixDenoiserGuideLayer` (Albedo + Normal) and
   `OptixDenoiserLayer` (Beauty input + caller's Output).
3. Build the `OptixDenoiserParams` (e.g. `hdrIntensity`
   pointer or `denoiseAlpha` per-frame override).
4. Call `optixDenoiserInvoke(denoiser, stream, &params,
   stateBuffer, stateSize, &guide, &layers, 1u, 0u, 0u,
   scratchBuffer, scratchSize)`.
5. `cudaDeviceSynchronize()` so the host knows the write to
   `output.device` is complete before the caller proceeds
   to download.

All five steps are pure `OptixDenoiser::invoke` body work; no
new private members are required (the scaffold already owns
the handle, the buffers, and the dimensions). The Stage 21A
plan's Output / Failure-behavior / Pipeline contracts are
already in place — implementation just needs to satisfy them.

The post-Stage-20 audit's Gaps A + B + C
(`docs/STAGE_20_OPTIX_PATH_TRACING_AUDIT.md`) — durable AOV
ownership for the OptiX path's denoiser handoff,
`OptixDenoiser` orchestration helper for the OptiX path,
`--render-optix-denoise` CLI surface — are still the
follow-up scope after Stage 21B.x lands the actual
`optixDenoiserInvoke`.

# Stage 21C Denoiser Input Wiring Audit

Date: 2026-05-04
Branch: `relativity-core-v1`
Last commit on the audited tree: `b8ca6c9` ("stage 21C.5:
denoiser input validation")
Scope: master order #24 — Stage 21C.1..21C.5 (input wiring
arc). Documents the AOV → `OptixImage2D` mapping, the
beauty-only and guided layer builders, and the input
validator that the eventual `optixDenoiserInvoke` call will
consume.
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

| Sub-stage | Commit    | Slice                                              |
|-----------|-----------|----------------------------------------------------|
| 21C.1     | `c471970` | Input structs (documented contract)                |
| 21C.2     | `4149816` | Image-format helpers (4 `make_*_image`)            |
| 21C.3     | `0d88f45` | Beauty-only denoiser input (`prepareBeautyOnlyInput`)|
| 21C.4     | `26490dd` | Beauty + Albedo + Normal input (`prepareGuidedInput`)|
| 21C.5     | `b8ca6c9` | Input validation (`validateDenoiserInputs`)        |

---

## 1. Beauty-only input can be prepared?

**YES** — verified by source inspection.

`prepareBeautyOnlyInput(Inputs, Output) -> ::OptixDenoiserLayer`
exists in `src/optix/OptixDenoiser.cpp` (Stage 21C.3 anonymous
namespace, SDK_FOUND-gated). Returns a value-typed
`::OptixDenoiserLayer` with:

- `layer.input` = `make_beauty_image(inputs)` (Stage 21C.2),
  picking FLOAT3 or FLOAT4 based on
  `inputs.beauty_components`.
- `layer.previousOutput` = zero-initialised
  `::OptixImage2D{}` (unused outside the temporal denoiser
  models the project does not target per the Stage 21A.9
  v1-scope decision).
- `layer.output` = `make_output_image(output,
  inputs.beauty_components)` (Stage 21C.2).

The function is `noexcept`, pure (no allocation, no global
state, no side effects), and tagged `[[maybe_unused]]` so
the unused-function warning is silenced until the next
sub-stage wires it into the `invoke()` body.

Caveat: the denoiser was init'd with `guideAlbedo=1,
guideNormal=1` at Stage 21B.4, so a real
`optixDenoiserInvoke` call will still expect guide images.
The beauty-only layer alone is not sufficient for the
current init configuration; it is reserved for a future
"beauty-only init options" path.

---

## 2. Beauty + Albedo + Normal input can be prepared?

**YES** — verified by source inspection.

`prepareGuidedInput(Inputs, Output) -> GuidedDenoiserInput`
exists in `src/optix/OptixDenoiser.cpp` (Stage 21C.4
anonymous namespace, SDK_FOUND-gated). Returns a small
local struct:

```
struct GuidedDenoiserInput {
    ::OptixDenoiserLayer       layer;
    ::OptixDenoiserGuideLayer  guide;
};
```

Builds:

- `layer.input` = `make_beauty_image(inputs)`.
- `layer.previousOutput` = zero-initialised
  `::OptixImage2D{}` (no temporal model).
- `layer.output` = `make_output_image(output,
  inputs.beauty_components)`.
- `guide.albedo` = `make_albedo_image(inputs)` (FLOAT3,
  linear, pre-lighting).
- `guide.normal` = `make_normal_image(inputs)` (FLOAT3,
  encoded `0.5 n + 0.5`).
- Other guide-layer fields (`flow`,
  `previousOutputInternalGuideLayer`,
  `outputInternalGuideLayer`, newer-SDK
  flow-trustworthiness) stay zero-initialised — temporal-
  denoiser territory ignored by the HDR model.

This layout matches the denoiser's init-time options pinned
at Stage 21B.4 (`guideAlbedo = 1`, `guideNormal = 1`), so
the returned struct is exactly what the eventual
`optixDenoiserInvoke` call will hand to the SDK.

`noexcept`, pure, `[[maybe_unused]]`.

---

## 3. Output buffer metadata exists?

**YES** — verified by source inspection.

Two complementary artifacts cover output metadata:

### Stage 21C.1: documented `Output` struct

`OptixDenoiser::Output` is declared in the public header
(`src/optix/OptixDenoiser.h`) with three fields:

```
struct Output {
    float* device = nullptr;
    int    width  = 0;
    int    height = 0;
};
```

Stage 21C.1 added the doc-comment block formalizing the
contract: caller-owned device buffer; sized to `width *
height * beauty_components` floats (matching the bound
Beauty input's layout); carries the denoised linear-RGB
radiance ready for download + save to
`output/denoised.ppm` per the Stage 21A.6 output contract.

### Stage 21C.2: `make_output_image` helper

`make_output_image(const Output&, int beauty_components)`
in the SDK_FOUND anonymous namespace converts the POD into
the SDK-typed `::OptixImage2D` descriptor:

```
img.data               = reinterpret_cast<::CUdeviceptr>(output.device);
img.width              = output.width;
img.height             = output.height;
img.rowStrideInBytes   = output.width * pixel_bytes;
img.pixelStrideInBytes = pixel_bytes;
img.format             = (beauty_components == 4)
                            ? OPTIX_PIXEL_FORMAT_FLOAT4
                            : OPTIX_PIXEL_FORMAT_FLOAT3;
```

Used by both `prepareBeautyOnlyInput` and
`prepareGuidedInput` to populate the layer's `output`
field.

---

## 4. Input validation exists?

**YES** — verified by source inspection.

`validateDenoiserInputs(Inputs, Output, bool require_guides,
std::string& error_out)` exists in
`src/optix/OptixDenoiser.cpp` (Stage 21C.5 anonymous
namespace, SDK_FOUND-gated). Returns `bool`; on failure
populates `error_out` with the documented error message.

Eight precondition checks, in order:

| Step | Check                                                            |
|------|------------------------------------------------------------------|
| 1    | `inputs.width > 0`                                               |
| 2    | `inputs.height > 0`                                              |
| 3    | `inputs.beauty_device != nullptr`                                |
| 4    | `output.device != nullptr`                                       |
| 5    | `output.width == inputs.width` (output dims match Beauty input)  |
| 6    | `output.height == inputs.height`                                 |
| 7    | `inputs.beauty_components` in `{3, 4}`                           |
| 8    | (when `require_guides`) `inputs.albedo_device != nullptr` AND    |
|      | `inputs.normal_device != nullptr`                                |

Mapping to the user's required checks:

| User's required check                  | Validator step(s)            |
|----------------------------------------|------------------------------|
| `width > 0`                            | step 1                       |
| `height > 0`                           | step 2                       |
| beauty buffer exists                   | step 3                       |
| output buffer exists                   | step 4                       |
| albedo / normal dims match when used   | steps 5 + 6 + 8              |

The `Inputs` struct carries a single `width` / `height`
shared across beauty + albedo + normal per the Stage 21C.1
documented contract; the renderer's AOV pipeline
guarantees this when populating the struct, so explicit
per-buffer dimension checks are not required. Steps 5 + 6
ensure the output matches; step 8 ensures the guides are
populated when needed.

`noexcept`, never touches the GPU, never throws.
`[[maybe_unused]]` until the next sub-stage wires it into
`invoke()`.

---

## 5. No denoiser invocation yet?

**YES (confirmed)** — verified by `grep` over
`src/optix/OptixDenoiser.cpp`.

The string `optixDenoiserInvoke` appears in the file only
inside doc-comments referencing the eventual call (forward
references in the Stage 21C.2 / 21C.3 / 21C.4 / 21C.5
helper docstrings; backward references in the Stage 21B.8
`optixDenoiserSetup` log line and the Stage 21B.7 buffer
allocation comment). It does NOT appear as an actual
function call anywhere.

`OptixDenoiser::invoke(output)` body remains the Stage
21B.1 stub:

```
bool OptixDenoiser::invoke(const Output& /*output*/) noexcept {
    last_error_ =
        "OptixDenoiser::invoke: not implemented in Stage 21B.1.";
    return false;
}
```

(SDK_FOUND branch + the `RELATIVITYRENDER_ENABLE_OPTIX`
disabled branch each have their own copy; both return
false with their respective documented error.)

All five Stage 21C.x helpers (`make_beauty_image`,
`make_albedo_image`, `make_normal_image`,
`make_output_image`, `prepareBeautyOnlyInput`,
`prepareGuidedInput`, `validateDenoiserInputs`) are tagged
`[[maybe_unused]]` precisely because they have no caller
yet. The user-visible behaviour through
`denoise_aov_buffers_to_ppm` and the
`--render-denoise` / `--render-aovs --denoise` CLI surfaces
is unchanged: the consumer reaches `invoke()`, gets false,
falls into the Stage 19C.3 noisy-Beauty fallback path
documented at Stage 21A.7.

---

## 6. Builds with OptiX OFF and ON?

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

When OFF: `rr_optix` is not built (Stage 12B.3 contract);
`OptixDenoiser.cpp` not compiled. The Stage 21C helpers
have no impact.

When ON without SDK: `OptixDenoiser.cpp` IS compiled with
`RELATIVITYRENDER_ENABLE_OPTIX` defined and
`RELATIVITYRENDER_OPTIX_SDK_FOUND` undefined. The Stage
21C helper block (gated by SDK_FOUND) is skipped entirely;
only the audit-host fallback methods run.

When ON with SDK (deferred to a CUDA + OptiX-SDK host):
all seven Stage 21C helpers compile inside the SDK_FOUND
gate. They are still unreached from any caller until the
next sub-stage wires them, so their compile-time existence
is verifiable but their runtime behaviour cannot be
exercised yet.

---

## Summary table

| Check | Question                                       | Verdict           | Empirical / Structural |
|-------|------------------------------------------------|-------------------|-------------------------|
| 1     | Beauty-only input can be prepared?             | YES               | Structural              |
| 2     | Beauty/albedo/normal input can be prepared?    | YES               | Structural              |
| 3     | Output buffer metadata exists?                 | YES               | Structural              |
| 4     | Input validation exists?                       | YES (8 checks)    | Structural              |
| 5     | No denoiser invocation yet?                    | YES (confirmed)   | Empirical (grep)        |
| 6     | Builds with OptiX OFF and ON?                  | YES (6/6 + 7/7)   | Empirical (audit host)  |

"Empirical" = the audit host directly verified the claim by
running the relevant command on this host. "Structural" = the
audit verified the source / build configuration / wiring is
in place but the runtime verification requires a CUDA +
OptiX-SDK host the audit host does not have.

---

## What lands next (post-Stage 21C)

The input-wiring scaffold is complete. The next slice
("Stage 21D" or whichever the operator chooses next) needs
to:

1. Wire `prepareGuidedInput` (and optionally
   `prepareBeautyOnlyInput`) + `validateDenoiserInputs`
   into the `OptixDenoiser::invoke(output)` SDK_FOUND
   branch:
   - Validate inputs first (return false on error).
   - Build the layer + guide via `prepareGuidedInput`.
   - Build the `OptixDenoiserParams` (e.g. `hdrIntensity`
     pointer, `denoiseAlpha` per-frame override).
   - Call `optixDenoiserInvoke(denoiser, stream, &params,
     stateBuffer, stateSize, &guide, &layers, 1u, 0u, 0u,
     scratchBuffer, scratchSize)`.
   - `cudaDeviceSynchronize()` so the host knows the write
     to `output.device` is complete before the caller
     proceeds to download.
2. Add the `--render-optix-denoise` CLI surface (Gap C
   from the post-Stage-20 audit + Stage 21A.8 modes).
3. Address Gap A (durable AOV ownership for the OptiX
   path's `render_aovs` so the device pointers survive
   across the denoiser invoke) per the post-Stage-20 audit.

The Stage 21C wiring artifacts (helpers + struct + validator)
are everything the next slice needs to produce the actual
denoised image; no further input-side scaffolding is
required.

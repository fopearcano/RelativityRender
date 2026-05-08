# OptiX Gap A — Step 3 Task

Source: `docs/OPTIX_GAP_A_POLISH_PLAN.md` §4 ("Minimal
implementation steps"), Step 3.
Predecessors: Step 1 (`6287471`, types + decl) + Step 2
(`9218b18`, SDK_FOUND body) per
`docs/OPTIX_GAP_A_STEP_2_AUDIT.md` (verdict: PASS).
Mode: documentation-only. No source code is modified by
this task file.

---

## 1. Helper name

`render_optix_aovs_and_denoise_to_ppm`

A thin host-side orchestration helper that wires the
Step-2 `OptixRenderer::render_aovs_retain` entry into
the existing Stage 21D.5 `denoise_and_save_ppm` helper.
The name follows the
`denoise_aov_buffers_to_ppm` precedent (Stage 19B.4
CUDA-path equivalent) so the OptiX-side and CUDA-side
orchestrators are visually parallel in `src/main.cpp`.

## 2. File

`src/main.cpp` — same TU as the existing
`denoise_aov_buffers_to_ppm` (Stage 19B.4) and
`denoise_and_save_ppm` (Stage 21D.4 + 21D.5)
orchestrators. The new helper is gated by the same
preprocessor envelope as the others:

```
#if defined(RR_HAS_CUDA) && defined(RELATIVITYRENDER_ENABLE_OPTIX)
// ... helper body ...
#endif
```

No new file, no new header. The function is a free
function (no class membership) at file scope, just like
its CUDA-path sibling.

## 3. Calls

The helper sequences three calls:

| Order | Call                                       | Purpose                              |
|-------|--------------------------------------------|--------------------------------------|
| 1     | `OptixRenderer::render_aovs_retain(        | Run the OptiX AOV launch and        |
|       | scene, lights, width, height)`             | retain the Beauty / Albedo / Normal |
|       |                                            | device buffers in an                |
|       |                                            | `AovRetainedBuffers` struct (Step   |
|       |                                            | 2 surface).                          |
| 2     | (host-side) build an                       | Pull the three device pointers +    |
|       | `OptixDenoiser::Inputs` POD from the       | dimensions out of the retained      |
|       | retained struct's                          | struct; the helper does NOT call    |
|       | `beauty_device.device_ptr()` /             | `OptixDenoiser::denoise` directly — |
|       | `albedo_device.device_ptr()` /             | that happens inside                 |
|       | `normal_device.device_ptr()` plus          | `denoise_and_save_ppm` below.       |
|       | `width`, `height`, `beauty_components=3`.  |                                     |
| 3     | `denoise_and_save_ppm(                     | Existing Stage 21D.4 + 21D.5        |
|       | denoiser, inputs, out_path)`               | helper. Allocates the device-side   |
|       |                                            | output buffer, calls                |
|       |                                            | `OptixDenoiser::denoise(inputs,     |
|       |                                            | output)` internally, downloads      |
|       |                                            | the result, saves the PPM. On any   |
|       |                                            | denoiser failure the Stage 21D.5    |
|       |                                            | noisy-Beauty fallback fires inside  |
|       |                                            | the helper (using                   |
|       |                                            | `inputs.beauty_device` directly).   |

The call chain is intentionally shallow: the new helper
is glue between two pre-existing functions; it does NOT
reimplement any of their work. The retained
`AovRetainedBuffers` struct stays in scope across the
`denoise_and_save_ppm` call so the device buffers
remain alive for the entire denoise invocation
(Gap A's whole point).

## 4. Inputs needed

The helper's signature draft:

```
bool render_optix_aovs_and_denoise_to_ppm(
    rr::optix::OptixDenoiser&                denoiser,
    const rr::scene::Scene&                  scene,
    const std::vector<rr::lighting::Light>&  lights,
    int                                       width,
    int                                       height,
    const std::string&                       out_path
        = std::string("output/optix_aovs_denoised.ppm"));
```

Per-input contract:

| Input          | Source                                  | Notes                            |
|----------------|------------------------------------------|----------------------------------|
| `denoiser`     | Caller (already-initialized via          | Pre-condition:                   |
|                | `OptixDenoiser::initialize(backend)`).   | `denoiser.isAvailable() == true`.|
|                |                                          | The helper does NOT initialise   |
|                |                                          | the denoiser; that is the        |
|                |                                          | caller's responsibility (same    |
|                |                                          | shape as                         |
|                |                                          | `denoise_aov_buffers_to_ppm`).   |
| `scene`        | Caller (built inline by the              | Forwarded verbatim to            |
|                | dispatcher; same shape as                | `render_aovs_retain`. Must       |
|                | `run_render_optix_aovs` builds its       | contain at least one visible    |
|                | scene).                                   | non-empty mesh (the              |
|                |                                          | first-non-empty-mesh-pick rule   |
|                |                                          | from Stage 20F applies).         |
| `lights`       | Caller. Forwarded verbatim to            | Empty vector is acceptable      |
|                | `render_aovs_retain`. Same shape as      | (the OptiX direct-lighting      |
|                | `OptixRenderer::render_aovs`'s           | closest-hit's "no lights"       |
|                | `lights` parameter.                      | facing-ratio fallback per Stage |
|                |                                          | 20K applies).                    |
| `width`,       | Caller. Forwarded verbatim to            | Both must be > 0.                |
| `height`       | `render_aovs_retain`.                    |                                  |
| `out_path`     | Caller. Default                          | A `--output` override at the     |
|                | `output/optix_aovs_denoised.ppm` so      | CLI level (Step 4) would         |
|                | the new helper does not collide with     | propagate via this argument.     |
|                | the CUDA-path `output/denoised.ppm`      |                                  |
|                | from `denoise_aov_buffers_to_ppm`.       |                                  |

The helper does NOT take an `OptixBackend&`; the
denoiser's existing initialize-with-backend contract
keeps the backend reference inside the denoiser.

## 5. Outputs expected

### 5.1 Return value

`bool` — `true` on success (a denoised PPM was written
to `out_path`), `false` on failure (no PPM written,
diagnostic logged to stderr via `Logger::error(...)`).

### 5.2 File output

| Scenario                                    | Outcome                                    |
|---------------------------------------------|--------------------------------------------|
| `render_aovs_retain` succeeds AND           | `out_path` carries the OptiX-denoised      |
| `denoise_and_save_ppm` denoise succeeds     | radiance (RGB or RGBA per                  |
|                                             | `beauty_components`).                      |
| `render_aovs_retain` succeeds BUT           | `out_path` carries the noisy Beauty AOV    |
| `denoise_and_save_ppm` denoise fails        | (Stage 21D.5 noisy-Beauty fallback fires   |
| (init / set_inputs / invoke / sync /        | inside `denoise_and_save_ppm`); a single   |
| download error)                             | `[WARN] denoise: ...; falling back to      |
|                                             | noisy Beauty AOV` line appears on stderr.  |
|                                             | Helper returns `true`.                     |
| `render_aovs_retain` fails                  | No PPM written. Helper logs                |
| (invalid dims / no mesh / backend init      | `Logger::error("optix-aovs-denoise: " +    |
| fail / launch fail / etc.)                  | retained.message)` and returns `false`.    |
| `denoiser.isAvailable() == false`           | Helper short-circuits: logs the documented |
| (denoiser was not initialised)              | "denoiser not available" error and         |
|                                             | returns `false`. No PPM written.           |

### 5.3 Log lines

The helper itself emits at most three log lines per
invocation:

- On entry (optional / informational): none required;
  the helper can be silent.
- On success: a single
  `Logger::info("optix-aovs-denoise: wrote " + out_path)`
  line (or similar; matches the CUDA-side
  `denoise_aov_buffers_to_ppm` pattern).
- On failure: a single `Logger::error(...)` line
  describing the cause; the underlying
  `last_error()` from the failing call propagates.

The Stage 21D.5 noisy-Beauty fallback's own warning
line (emitted INSIDE `denoise_and_save_ppm`) appears
ALSO when the fallback fires — that is the existing
helper's contract, not new behaviour.

## 6. PASS criteria

A Step 3 commit PASSES when ALL of these hold:

| Criterion                                                  | Verifier         |
|------------------------------------------------------------|------------------|
| OFF build is clean and ctest 6/6 green                     | audit host       |
| ON-audit-host build is clean and ctest 7/7 green           | audit host       |
| Existing `OptixRenderer::render_aovs_retain` is            | `git diff` over  |
| byte-identical (Step 2 SDK_FOUND body untouched)           | src/optix/       |
| Existing `denoise_and_save_ppm` is byte-identical          | `git diff` over  |
| (Stage 21D.4 + 21D.5 helper untouched)                     | src/main.cpp     |
| Existing `denoise_aov_buffers_to_ppm` is byte-identical    | `git diff`       |
| (Stage 19B.4 CUDA-path helper untouched)                   |                  |
| Existing CLI surfaces (`--render-optix-aovs`,              | `git diff` +     |
| `--render-optix-denoise`, `--render-aovs --denoise`,       | audit-host CLI   |
| `--render-denoise`, `--render --denoise`) are byte-        | smokes           |
| identical (Step 3 does NOT wire the new helper to a CLI;   |                  |
| Step 4 owns that)                                          |                  |
| CUDA renderer is byte-identical (zero bytes changed in     | `git diff` over  |
| `src/cuda/`, `src/renderer/`, `src/pathtracer/`)           | src/             |
| The new helper is callable in principle (compiles + links  | build success    |
| under both ON-audit-host and CUDA + OptiX-SDK builds)      |                  |
| `docs/BUILD_PLAN.md` slice-closing entry added             | inspection       |
| (per master rule 8)                                        |                  |
| Master rule 5 + 7 ("no CPU per-pixel work as production    | `grep` over the  |
| path"): the new helper does NOT iterate per-pixel on the   | new code         |
| host. Allowed CPU work: build the `Inputs` POD, log,       |                  |
| forward to `denoise_and_save_ppm` (which itself only does  |                  |
| host-side `cudaMemcpy(D->H)` + `Image::save_ppm`).         |                  |

A Step 3 commit REQUIRES REPAIR when ANY of these fail:

- An existing CLI surface produces different bytes for
  the same inputs.
- The CUDA path is touched.
- `OptixRenderer::render_aovs_retain` (Step 2 body) has
  any signature or semantic change.
- A test in `tests/` breaks.
- The CUDA-H.9 verification report's functional content
  (test rows / counts / overall verdict) drifts on the
  audit host. Only the `Tree state` hash line may
  legitimately change.

The audit host can verify everything except the
runtime-visible "denoised PPM with denoiser-grade
smoothing" check; that one stays deferred to a CUDA +
OptiX-SDK host run per the established
`docs/CUDA_HOST_VERIFICATION_PLAN.md` posture.

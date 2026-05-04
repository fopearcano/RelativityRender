# OptiX Gap A — Step 3 Audit

Date: 2026-05-04
Branch: `relativity-core-v1`
Last commit on the audited tree: `2fabb54` ("optix gap A
step 3.5: wire helper into --render-optix-aovs --denoise")
Scope: post-Step-3.5 capstone audit covering the entire
Gap-A Step 3 arc — task spec (`e8523d8`), helper shell
(`c5d6968`), retained-AOV render call (`f8df9b9`),
denoiser handoff (`ccb40c8`), minimal CLI hook
(`2fabb54`).
Mode: documentation-only. No source code is modified by
this audit.

The audit answers the seven prompt questions in order.
Each verification is empirical (audit host actually ran
the relevant command/grep/diff) where possible;
runtime-visible behaviour stays deferred to a CUDA +
OptiX-SDK host run per the established
`docs/CUDA_HOST_VERIFICATION_PLAN.md` posture.

---

## Step 3 commit chain

| Slice    | Commit    | Artifact                                              |
|----------|-----------|-------------------------------------------------------|
| 3.1 task | `e8523d8` | `docs/OPTIX_GAP_A_STEP_3_TASK.md` (helper spec)       |
| 3.2      | `c5d6968` | `src/main.cpp`: helper SHELL (validation + log +      |
|          |           | "not ready" return)                                    |
| 3.3      | `f8df9b9` | `src/main.cpp`: wire `render_aovs_retain` call +      |
|          |           | retained-buffer validation                             |
| 3.4      | `ccb40c8` | `src/main.cpp`: wire `denoise_and_save_ppm` call;     |
|          |           | helper now returns true on PPM written                 |
| 3.5      | `2fabb54` | `src/main.cpp`: `run_render_optix_aovs` calls the     |
|          |           | helper when `--denoise` is set                         |

---

## 1. Helper exists in src/main.cpp?

**YES** — verified by `grep`.

`src/main.cpp:4000` defines
`render_optix_aovs_and_denoise_to_ppm(...)` with the
signature documented in
`docs/OPTIX_GAP_A_STEP_3_TASK.md` §1:

```
bool render_optix_aovs_and_denoise_to_ppm(
        rr::optix::OptixDenoiser&                denoiser,
        const rr::scene::Scene&                  scene,
        const std::vector<rr::lighting::Light>&  lights,
        int                                       width,
        int                                       height,
        const std::string&                       out_path)
```

Plus a forward declaration at `src/main.cpp:180` (added
in Step 3.5 because `run_render_optix_aovs` calls the
helper from above its definition site). Both the forward
decl and the definition are gated by
`#if defined(RR_HAS_CUDA) && defined(RELATIVITYRENDER_ENABLE_OPTIX)`,
matching the surrounding helpers
`denoise_aov_buffers_to_ppm` (Stage 19B.4) and
`denoise_and_save_ppm` (Stage 21D.4 + 21D.5).

The default argument
(`out_path = "output/optix_aovs_denoised.ppm"`) lives on
the forward declaration; the definition omits it per
the C++ "default argument may be specified only once"
rule.

---

## 2. Helper calls render_aovs_retain?

**YES** — verified by `grep` over the helper body.

`src/main.cpp:4062`:

```
auto retained = rr::optix::OptixRenderer::render_aovs_retain(
    scene, lights, width, height);
if (!retained.ok) {
    Logger::error(
        "optix-aovs-denoise: render_aovs_retain failed: "
      + retained.message);
    return false;
}
```

Plus a defensive post-call validation block (added in
Step 3.3) that catches any future Step-2 regression
returning `ok=true` with half-populated buffers:

- Verifies each `GpuBuffer<float>::size() == width *
  height * 3u`.
- Verifies each `device_ptr() != nullptr`.

The retained struct stays in scope through the
denoise call below (per Step 3.4's lifetime contract;
this is Gap A's whole point).

---

## 3. Helper hands retained device pointers to denoiser?

**YES** — verified by `grep`.

`src/main.cpp:4129-4134`:

```
rr::optix::OptixDenoiser::Inputs inputs;
inputs.beauty_device     = retained.beauty_device.device_ptr();
inputs.beauty_components = 3;
inputs.albedo_device     = retained.albedo_device.device_ptr();
inputs.normal_device     = retained.normal_device.device_ptr();
inputs.width             = retained.width;
inputs.height            = retained.height;
```

The three retained `GpuBuffer<float>` instances (live
inside `retained`, the `AovRetainedBuffers` struct
returned from Step 2) supply their `device_ptr()`s to
the `OptixDenoiser::Inputs` POD. The struct's
`width`/`height` are forwarded too. `beauty_components`
is hard-coded to 3 (the Step 2 body always allocates
the Beauty buffer as `width*height*3` floats).

The retained struct's lifetime spans the next call
(`denoise_and_save_ppm` below) so the device pointers
remain valid through the full
`OptixDenoiser::denoise` invocation. This is the
lifetime contract Gap A was designed to provide;
without Step 2's `GpuBuffer<float>` retention, the
existing Stage 20N `render_aovs` would have freed
these device buffers before the helper saw them.

---

## 4. Helper saves denoised PPM?

**YES** — verified by `grep`.

`src/main.cpp:4136`:

```
if (!denoise_and_save_ppm(denoiser, inputs, out_path)) {
    Logger::error(
        "optix-aovs-denoise: denoise_and_save_ppm "
        "failed; no image saved at " + out_path);
    return false;
}

Logger::info(
    "optix-aovs-denoise: complete; wrote " + out_path);
return true;
```

The Stage 21D.4 `denoise_and_save_ppm` helper allocates
the device-side output buffer via `GpuBuffer<float>`,
calls `OptixDenoiser::denoise(inputs, output)`,
downloads the result via `GpuBuffer::download`
(single `cudaMemcpy(D->H)`), and saves the PPM via
`save_image_or_error` (the standard `Image::save_ppm`
clamp).

On any denoiser failure inside `denoise_and_save_ppm`,
the Stage 21D.5 noisy-Beauty fallback fires
**internally** (using `inputs.beauty_device` directly
to download the noisy AOV). When that happens, the
PPM at `out_path` carries the noisy Beauty AOV and
`denoise_and_save_ppm` still returns `true`. The
helper's `return false` branch is reached ONLY when
even the noisy fallback's download or save itself
failed (a rare runner-side failure mode).

The full success path: `out_path` (default
`output/optix_aovs_denoised.ppm`) carries the OptiX-
denoised radiance. The helper's `Logger::info("...
complete; wrote ...")` line reports the path on
success.

---

## 5. OptiX OFF build still works?

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

The new helper + forward decl + dispatcher hook are all
gated by
`#if defined(RR_HAS_CUDA) && defined(RELATIVITYRENDER_ENABLE_OPTIX)`,
so the OFF build skips the entire Gap A Step 3 surface;
ctest baseline (6/6) is byte-identical with the
pre-Step-3 state.

The corresponding ON-audit-host build (`-DRR_ENABLE_CUDA=OFF
-DRR_ENABLE_OPTIX=ON`, no SDK on disk) is also clean +
ctest 7/7 green; the helper is gated out there too
because `RR_HAS_CUDA` is undefined.

---

## 6. CUDA path unchanged?

**YES** — verified by diff-stats over the full Step 3
arc:

```
$ git diff e8523d8..2fabb54 --stat -- \
      src/cuda/ src/renderer/ src/pathtracer/
$ echo "exit=$?"
exit=0
```

(zero output, zero exit code — no files touched).

Every Stage 17-21 + Gap A slice has held this same
rule (the CUDA path stays byte-identical). Step 3
continues that streak: `src/main.cpp` is the ONLY
source file touched in any of the five Step 3
sub-stages.

The Stage 21E.2 `--render --denoise` dispatcher
(which uses the CUDA AOV pipeline `render_scene_with_aovs`
and the existing `denoise_and_save_ppm` helper) is
unaffected. The Stage 21D.6 `--render-optix-denoise`
dispatcher (which uses the CUDA AOV pipeline + the
OptiX denoiser) is also unaffected. The Stage 19B.4
`--render-aovs --denoise` dispatcher (which uses
`denoise_aov_buffers_to_ppm`) is unaffected.

The new code path (`--render-optix-aovs --denoise`)
is purely additive: when `--denoise` is NOT set, the
Stage 20N six-PPM behaviour is byte-identical with
its pre-Step-3 form.

---

## 7. Runtime status

**Verdict per build mode:**

| Build mode               | Static / runtime        | Status     |
|--------------------------|-------------------------|------------|
| OFF                      | helper not compiled     | PASS       |
|                          | (gated out); CUDA-host  | (n/a       |
|                          | smoke deferred          | runtime)   |
| ON, no SDK (audit host)  | helper not compiled     | PASS       |
|                          | (gated by `RR_HAS_CUDA` | (n/a       |
|                          | which is undefined on   | runtime)   |
|                          | this host)              |            |
| ON + CUDA, no OptiX SDK  | helper compiles +       | DEFERRED   |
|                          | links; the new          | to a CUDA  |
|                          | dispatcher block emits  | host       |
|                          | the documented          |            |
|                          | "denoiser unavailable   |            |
|                          | (requires SDK)" warning |            |
|                          | + keeps the noisy AOV   |            |
|                          | PPMs                    |            |
| ON + CUDA + OptiX SDK,   | full SDK-found path:    | DEFERRED   |
| denoise succeeds         | six AOV PPMs +          | to a CUDA  |
|                          | `output/optix_aovs_     | + OptiX-   |
|                          | denoised.ppm` written;  | SDK host   |
|                          | exit 0                   |            |
| ON + CUDA + OptiX SDK,   | six AOV PPMs +          | DEFERRED   |
| denoise fails (e.g.,     | `output/optix_aovs_     | to a CUDA  |
| backend-init failure)    | denoised.ppm` may       | + OptiX-   |
|                          | carry the Stage 21D.5   | SDK host   |
|                          | noisy-Beauty fallback;  |            |
|                          | warning logged; exit 0  |            |
|                          | (denoise failure is     |            |
|                          | non-fatal per the user's|            |
|                          | rule)                    |            |

**Overall verdict: PASS for the audit host's available
checks; DEFERRED for the runtime-visible end-to-end
denoise behaviour.**

The CUDA-H.9 verification report on the audit host
remains byte-identical pre- vs post-Step-3 (only the
documented `Tree state` hash line varies). All test
rows / counts / overall verdict stable; the runner
catches every pre-existing CLI surface's behaviour
unchanged.

A future operator running the verification plan on a
CUDA + OptiX-SDK host with `--render-optix-aovs --denoise`
will produce eight files
(`output/optix_aov_{beauty,normal,depth,albedo,doppler,
searchlight}.ppm` + `output/optix_aovs_denoised.ppm`)
in a single CLI invocation — the empirical proof that
Gap A's lifetime contract works end-to-end.

---

## Summary table

| # | Question                                       | Verdict           | Verifier        |
|---|------------------------------------------------|-------------------|-----------------|
| 1 | Helper exists in `src/main.cpp`?               | YES               | grep            |
| 2 | Helper calls `render_aovs_retain`?             | YES               | grep            |
| 3 | Helper hands retained pointers to denoiser?    | YES               | grep            |
| 4 | Helper saves denoised PPM?                     | YES               | grep + spec     |
| 5 | OptiX OFF build still works?                   | YES (ctest 6/6)   | empirical       |
| 6 | CUDA path unchanged?                           | YES (0 bytes)     | git diff stat   |
| 7 | Runtime status                                 | PASS / DEFERRED   | per build mode  |

---

## Critical findings

- The Step 3 arc closed in five small slices (3.1
  task, 3.2 shell, 3.3 retained-render call, 3.4
  denoiser handoff, 3.5 CLI hook). Each slice
  preserved the OFF + ON-audit-host ctest baselines
  (6/6 + 7/7 green throughout).
- The full orchestration helper exists, calls all
  three of `render_aovs_retain`,
  `OptixDenoiser::denoise` (via
  `denoise_and_save_ppm`), and `Image::save_ppm`
  (via the same), and is wired into the existing
  `--render-optix-aovs --denoise` CLI surface as a
  purely additive optional pass.
- Master rules respected at every slice: no CPU
  per-pixel work, no `--server` invocation, no
  C4D/UI/node-editor surface added, CUDA path zero
  bytes changed, `OFF` + audit-host builds always
  green.
- One open follow-up remains in the OptiX Gap A
  polish plan: **Step 4** (CLI surface) per
  `docs/OPTIX_GAP_A_POLISH_PLAN.md`. Step 3.5
  arguably already SHIPPED Step 4's minimal form
  (the `--render-optix-aovs --denoise` modifier
  flow), but the plan's Step 4 also mentions an
  alternative dedicated `--render-optix-aovs-denoise`
  action. The operator's choice; both shapes are
  acceptable.

---

## Next safe step (one line)

**Either close OptiX Gap A by writing the Step 5
capstone audit (`docs/OPTIX_GAP_A_POLISH_AUDIT.md`)
declaring the gap closed since Step 3.5's
`--render-optix-aovs --denoise` hook satisfies the
plan's Step 4 minimal-CLI form, OR continue with
the alternative dedicated
`--render-optix-aovs-denoise` action as Step 4
proper per `docs/OPTIX_GAP_A_POLISH_PLAN.md` §4.**

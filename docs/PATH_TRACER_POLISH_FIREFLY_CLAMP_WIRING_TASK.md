# Path-Tracer Polish — Firefly Clamp Backend Wiring Task

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_POLISH_PLAN.md` §4.7
(kernel-wiring sub-arc deferred from PT-P.21).
Selected via:
`docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_AUDIT.md` §7's
"Recommended next step" verdict (the PT-P.x §4 polish arc
closed at PT-P.22; PT-P.23 reopens the firefly-clamp
sub-arc with the kernel wiring deferred from PT-P.21).
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next
slice (PT-P.24 implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's
reasoning.

---

## 1. Exact goal

**Title.** PT-P.x — Wire `cfg.firefly_clamp` through both
backends symmetrically.

**Source.** `PATH_TRACER_POLISH_PLAN.md` §4.7
(kernel-wiring portion deferred from PT-P.21);
`PATH_TRACER_POLISH_FIREFLY_CLAMP_TASK.md` §1.2 + §3
(deferral rationale + negative-reference enumeration of
the files this slice will need).

**One-paragraph summary.** PT-P.21 shipped a field-only
placeholder (`PathTraceConfig::firefly_clamp = 0.0f`)
with a doc-comment promising that "a future slice will
wire the value through both backends' path-trace
raygens so each per-sample `radiance.x|y|z` is
`fminf(radiance.x|y|z, firefly_clamp)` before being
added to the accumulator." THIS is that future slice.
The wiring is symmetric across the CUDA path-tracer
kernel (`k_pathtrace_sample` in `CudaPathTracer.cu`)
and the OptiX path-tracer raygen
(`__raygen__pathtrace` in `OptixPrograms.cu`); both
backends MUST land in the SAME commit so the symmetric-
output invariant is preserved (per the PT-P.20 task
§1.2 — "landing one without the other would silently
diverge them at non-zero clamp"). The default-off
behaviour is preserved verbatim: when `firefly_clamp ==
0.0f`, both backends produce byte-identical output with
the pre-PT-P.24 baseline.

**Property of this slice.** PT-P.24 is the LARGEST
PT-P.x source-file footprint to date (~8 files) and
the FIRST PT-P.x slice to deliberately exceed the
"max 2 source files" rule. The exceedance is
explicitly authorised by THIS task brief because the
symmetric-output invariant (the PT-P.20 task §1.2 +
the PT-P.22 audit §6.2 forward-compatible
checks-list) makes piecewise wiring unsafe; the
symmetry constraint forces the cross-backend update
to land atomically.

**The single concrete change** (broken into per-file
sub-changes; see §3 for the full file list).

### 1.1 Validation (`src/pathtracer/PathTracer.cpp`)

Add a lower-bound rejection to the existing validation
prelude in `PathTracer::render`, immediately after the
`cfg.environment_intensity < 0.0f` check at line 54-57:

```cpp
if (cfg.firefly_clamp < 0.0f) {
    result.message = "firefly_clamp must be >= 0";
    return result;
}
```

The convention matches the existing
`environment_intensity` rejection's shape verbatim. No
upper-bound clamp is added (the §4.7 plan recommends
common values around 8.0 but does not impose a cap;
high-variance scenes with strong fireflies may want
larger values; an upper bound is a separate future
slice if needed).

The `cfg.firefly_clamp` value then needs to be threaded
through to the launcher call. The single use of
`launch_pathtrace_sample` at lines 98-105 gains a new
trailing argument:

```cpp
if (!rr::cuda::launch_pathtrace_sample(
        sample.device_ptr(), width, height,
        scene,
        effective_max_bounces,
        cfg.seed,
        static_cast<unsigned int>(s),
        cfg.environment_color,
        cfg.environment_intensity,
        cfg.firefly_clamp)) {  // PT-P.24: per-sample clamp
```

### 1.2 Launcher signature (`src/cuda/CudaPathTracer.cuh`)

Append a new parameter to the
`launch_pathtrace_sample` declaration:

```cpp
[[nodiscard]] bool launch_pathtrace_sample(
    float*                   device_sample_pixels,
    int                      width,
    int                      height,
    const rr::gpu::GpuScene& scene,
    int                      max_bounces,
    unsigned int             seed,
    unsigned int             sample_index,
    rr::math::Vec3           env_color,
    float                    env_intensity,
    float                    firefly_clamp);  // PT-P.24
```

Update the doc-comment block above the declaration to
name the new parameter. Add a `firefly_clamp` row to
the existing parameter table (the file documents each
parameter's contract):

```
//   `firefly_clamp`    per-channel firefly clamp on
//                      the per-sample radiance. 0.0f
//                      disables the clamp (default;
//                      every PathTraceConfig{} value
//                      passes 0.0f); > 0 produces a
//                      `fminf(radiance.x|y|z,
//                      firefly_clamp)` per channel
//                      before the per-pixel write.
//                      See `PathTraceConfig::firefly_clamp`
//                      for the authoring contract.
```

### 1.3 Kernel (`src/cuda/CudaPathTracer.cu`)

Three sub-changes:

(a) The kernel's signature gains `float firefly_clamp`
as its last parameter:

```cpp
__global__ void k_pathtrace_sample(
    float*           pixels,
    int              width,
    int              height,
    CudaSceneView    scene,
    int              max_bounces,
    unsigned int     seed,
    unsigned int     sample_index,
    Vec3             env_color,
    float            env_intensity,
    float            firefly_clamp) {
```

(b) The kernel applies the clamp BEFORE the final per-
pixel write at lines 237-241. Insert the guard
immediately above:

```cpp
// PT-P.24: per-channel firefly clamp on the per-sample
// radiance. Strict `>` gating: when `firefly_clamp ==
// 0.0f` (the PathTraceConfig default), the branch is
// not taken and `radiance` is unchanged; the resulting
// per-pixel write is byte-identical with the pre-PT-P.24
// arithmetic. When `firefly_clamp > 0.0f`, each channel
// is clamped via `fminf` before being written. The
// branch is uniform per-warp (every pixel hitting the
// same launch reads the same `firefly_clamp`), so no
// warp divergence is introduced.
if (firefly_clamp > 0.0f) {
    radiance.x = fminf(radiance.x, firefly_clamp);
    radiance.y = fminf(radiance.y, firefly_clamp);
    radiance.z = fminf(radiance.z, firefly_clamp);
}
const int idx = (y * width + x) * 4;
pixels[idx + 0] = radiance.x;
...
```

(c) The launcher function definition matches the
launcher signature in §1.2; the new arg is forwarded
to the kernel:

```cpp
bool launch_pathtrace_sample(... float env_intensity,
                             float firefly_clamp) {
    if (device_sample_pixels == nullptr || width <= 0 || height <= 0
     || max_bounces < 0
     || firefly_clamp < 0.0f) {  // PT-P.24
        return false;
    }
    ...
    k_pathtrace_sample<<<grid, block>>>(
        ..., env_intensity, firefly_clamp);
    ...
}
```

The lower-bound check on `firefly_clamp` is defence in
depth — the host-side validator in §1.1 already
rejects negative values, but the launcher's pre-launch
guard catches a caller that bypasses the host validator.

### 1.4 OptiX launch params (`src/optix/OptixLaunchParams.h`)

Append a new field to the launch-params POD,
immediately after the existing `seed` field at line
146:

```cpp
std::int32_t  spp          = 1;
std::int32_t  max_bounces  = 1;
std::uint32_t seed         = 0;
float         firefly_clamp = 0.0f;  // PT-P.24
```

The default `0.0f` matches the
`PathTraceConfig::firefly_clamp` default exactly.

### 1.5 OptiX renderer signatures (`src/optix/OptixRenderer.h`)

Both `render_pathtrace` (line 216) and
`render_pathtrace_progressive` (line 274) gain a
trailing `float firefly_clamp = 0.0f` parameter:

```cpp
[[nodiscard]] static Result render_pathtrace(
    const rr::scene::Scene& scene,
    int width, int height,
    int spp, int max_bounces,
    unsigned int seed = 0u,
    float firefly_clamp = 0.0f) noexcept;  // PT-P.24

[[nodiscard]] static PathtraceProgressiveResult
render_pathtrace_progressive(
    const rr::scene::Scene& scene,
    int width, int height,
    int max_bounces,
    unsigned int seed,
    const std::vector<int>& checkpoint_samples,
    float firefly_clamp = 0.0f) noexcept;  // PT-P.24
```

The default-arg `= 0.0f` ensures every existing caller
keeps its behaviour unchanged (zero-arg-update for
existing dispatchers).

### 1.6 OptiX renderer body (`src/optix/OptixRenderer.cpp`)

Two sub-changes — one per dispatcher entry:

(a) `render_pathtrace` (line 1221 onward): set
`params.firefly_clamp` at line 1390 alongside the
existing `params.spp`, `params.max_bounces`,
`params.seed`:

```cpp
params.spp          = spp;
params.max_bounces  = max_bounces;
params.seed         = seed;
params.firefly_clamp = firefly_clamp;  // PT-P.24
```

Add a lower-bound rejection before the launch:

```cpp
if (firefly_clamp < 0.0f) {
    r.message = "OptixRenderer::render_pathtrace: "
                "firefly_clamp must be >= 0";
    return r;
}
```

Place it alongside the existing `spp < 1 || max_bounces
< 1` check at lines 1231-1234.

(b) `render_pathtrace_progressive` (line 1471 onward):
identical treatment. Set
`params.firefly_clamp = firefly_clamp` at the
launch-params upload site (line 1684 onward); add the
lower-bound rejection alongside the existing
`max_bounces < 1` check at lines 1481-1483.

### 1.7 OptiX raygen (`src/optix/OptixPrograms.cu`)

The `__raygen__pathtrace` raygen at line 817 onward
applies the clamp before the per-sample
`rgb_sum +=` accumulation at lines 933-935. Insert the
guard immediately above:

```cpp
// PT-P.24: per-channel firefly clamp on the per-sample
// radiance. Strict `>` gating; default-off behaviour
// matches the CUDA path-tracer kernel verbatim. Both
// backends apply the SAME clamp expression at the
// SAME point in the integrator (per-sample radiance,
// pre-accumulation), so their outputs remain
// convergent at non-zero clamp.
if (optixLaunchParams.firefly_clamp > 0.0f) {
    radiance.x = fminf(radiance.x, optixLaunchParams.firefly_clamp);
    radiance.y = fminf(radiance.y, optixLaunchParams.firefly_clamp);
    radiance.z = fminf(radiance.z, optixLaunchParams.firefly_clamp);
}
rgb_sum.x += radiance.x;
rgb_sum.y += radiance.y;
rgb_sum.z += radiance.z;
```

The `optixLaunchParams` global symbol is already
referenced throughout the file (e.g. line 138
`precompute_relativity(observer.velocity)` reads from
it); the new field is read with the same idiom.

### 1.8 Dispatcher pass-through (`src/main.cpp`)

`run_render_pathtrace` (line 2291) needs no changes —
it already constructs `pcfg` (a
`rr::pathtracer::PathTraceConfig`) which now includes
the `firefly_clamp = 0.0f` default; the existing code
path passes `pcfg` to `pt.render(...)` which forwards
to the CUDA launcher. Default-off is automatic.

`run_render_optix_pathtrace` (line ~1500-1600,
calling `render_pathtrace_progressive` at line 1569):
the OptiX dispatcher's `render_pathtrace_progressive`
call gains `firefly_clamp = 0.0f` as the trailing
argument (or relies on the default-arg the §1.5
signature change provides):

```cpp
auto pr = rr::optix::OptixRenderer::render_pathtrace_progressive(
    scene, width, height,
    max_bounces, seed, checkpoint_samples,
    /*firefly_clamp=*/0.0f);  // PT-P.24 (default; explicit for clarity)
```

The implementer may rely on the default-arg and skip
the explicit `0.0f`; the brief recommends naming it
explicitly so a future reader sees the field is
intentionally defaulted at the dispatcher.

---

## 2. Required invariant — symmetric default-off behaviour

**The two backends MUST behave identically when
`firefly_clamp == 0.0f`.** This is the load-bearing
contract that PT-P.21's BUILD_PLAN entry + PT-P.22's
audit §6.2 + the PT-P.20 task §1.2 all repeated:
"landing one backend's clamp without the other would
silently diverge their outputs at non-zero clamp." The
inverse — "both backends must clamp identically when
the clamp fires" — is equally important.

### 2.1 The strict-`>`-gating clause

Both kernels use `if (firefly_clamp > 0.0f)` (strict
greater-than). When `firefly_clamp == 0.0f`:

- The branch is NOT entered.
- `radiance.x|y|z` is unchanged.
- The per-pixel write (CUDA) / per-sample
  accumulation (OptiX) sees byte-identical input.

The default-off contract is therefore exact: every
existing render with `cfg.firefly_clamp == 0.0f` (i.e.
every render that does not explicitly opt in) produces
PPM byte-identical output with the pre-PT-P.24
baseline.

### 2.2 The non-zero-clamp invariance clause

When `firefly_clamp > 0.0f`, both kernels:

- Apply `fminf(radiance.x|y|z, firefly_clamp)` to the
  same Vec3 (the per-sample radiance just before it's
  added to the accumulator).
- Operate on per-sample radiance (NOT per-bounce, NOT
  post-accumulation).
- Use `fminf` (the IEEE-754 single-precision min that
  treats NaN inputs as the non-NaN argument) — both
  the CUDA kernel and the OptiX raygen are CUDA TUs
  with identical `fminf` ABI semantics.

These three properties together ensure that running
the same scene through `--render-pathtrace
scenes/X.rrscene --firefly_clamp 8.0` and
`--render-optix-pathtrace scenes/X.rrscene
--firefly_clamp 8.0` produces convergent (statistically
identical) output. Bit-for-bit identity across the two
backends is NOT guaranteed (the integrators have
different but-equivalent code paths; FMA fusion, RNG
draws across kernel-launch boundaries, etc., already
diverge the outputs at the bit level), but the
expected-value identity holds.

### 2.3 Future cross-backend smoke test

A future operator-side check (described in §7 below)
compares the CUDA + OptiX output at
`firefly_clamp = 8.0f` on a scene with intentional
fireflies. Both PPMs should show visible reduction in
firefly intensity vs `firefly_clamp = 0.0f` runs;
their statistical means should agree within sampling
noise.

### 2.4 No backend-asymmetric behaviour

The implementation slice MUST NOT introduce any
clause that is "CUDA-only" or "OptiX-only". For
example:

- The CUDA kernel applies `fminf` per channel; the
  OptiX raygen MUST do the same (not a vector clamp,
  not a luminance-based clamp, not a per-radiance-
  magnitude clamp).
- The CUDA path applies the clamp BEFORE the per-
  pixel write; the OptiX path MUST apply it BEFORE
  the per-sample `rgb_sum +=` accumulation. (These
  are the same point in their respective integrators
  — per-sample radiance, pre-accumulation.)
- The CUDA path uses strict `>` gating; the OptiX
  path MUST use the same gating (NOT `>=`, NOT a
  finite-only check, NOT a custom epsilon).

### 2.5 The lower-bound validation

Both `PathTracer::render` (CUDA path) and
`OptixRenderer::render_pathtrace*` (OptiX path)
reject `firefly_clamp < 0.0f` with their respective
"firefly_clamp must be >= 0" diagnostic. The two
rejection messages should be IDENTICAL in their
string content (modulo the prefix that names the
function) so a future operator searching the
codebase for "firefly_clamp must be >= 0" finds both
sites.

---

## 3. Files likely involved

The implementation slice will touch this file set —
EIGHT source files. This is the LARGEST PT-P.x
source-file footprint to date and the FIRST PT-P.x
slice to deliberately exceed the established "max 2
source files" rule. The exceedance is explicitly
authorised by THIS task brief (the symmetric-output
invariant in §2 forces the cross-backend update to
land atomically).

| File                                     | Purpose                                                  |
|------------------------------------------|----------------------------------------------------------|
| `src/pathtracer/PathTracer.cpp`          | Add `firefly_clamp >= 0.0f` lower-bound validation.     |
|                                          | Pass `cfg.firefly_clamp` to `launch_pathtrace_sample`.  |
|                                          | ~5 added.                                                |
| `src/cuda/CudaPathTracer.cuh`            | Add `float firefly_clamp` param to launcher signature.  |
|                                          | Update doc-comment table. ~5 added.                      |
| `src/cuda/CudaPathTracer.cu`             | Accept new arg; thread to kernel. Apply per-channel     |
|                                          | `fminf` clamp before the final per-pixel write.         |
|                                          | ~12 added (including doc-comment).                       |
| `src/optix/OptixLaunchParams.h`          | Add `float firefly_clamp = 0.0f` to launch-params POD.  |
|                                          | ~1 added + doc-comment update.                           |
| `src/optix/OptixRenderer.h`              | Add `float firefly_clamp = 0.0f` default-arg to BOTH    |
|                                          | `render_pathtrace` and `render_pathtrace_progressive`   |
|                                          | signatures. ~2 added.                                     |
| `src/optix/OptixRenderer.cpp`            | Accept new arg in BOTH dispatcher entries. Add          |
|                                          | lower-bound rejection alongside the existing            |
|                                          | `spp < 1 || max_bounces < 1` checks. Set                |
|                                          | `params.firefly_clamp = firefly_clamp` at BOTH          |
|                                          | launch-params upload sites. ~10 added.                   |
| `src/optix/OptixPrograms.cu`             | `__raygen__pathtrace` reads                              |
|                                          | `optixLaunchParams.firefly_clamp` and applies the       |
|                                          | per-channel `fminf` clamp before                         |
|                                          | `rgb_sum +=`. ~12 added (including doc-comment).         |
| `src/main.cpp`                           | `run_render_optix_pathtrace`'s call to                  |
|                                          | `render_pathtrace_progressive` passes                    |
|                                          | `firefly_clamp = 0.0f` (or relies on default-arg).      |
|                                          | `run_render_pathtrace` needs no edit (the default        |
|                                          | flows through `PathTraceConfig`). ~2 added.              |
| `docs/BUILD_PLAN.md`                     | Slice-closing entry following the established           |
|                                          | TEX-P.x / PT-P.x format. The entry MUST include a       |
|                                          | "Backend symmetry note" subsection citing §2 of         |
|                                          | this task to record why the slice exceeded the          |
|                                          | max-2-source-files rule.                                  |

**TOTAL: 8 source files, ~50 added lines across the
slice (LOGIC), plus ~10-15 lines of doc-comments per
non-trivial file.**

### 3.1 Why the max-2-source-files rule is exceeded

Three reasons the brief explicitly authorises the
larger surface:

1. **Cross-backend symmetry invariant.** The PT-P.20
   task §1.2 + the PT-P.22 audit §6.2 both said the
   kernel-wiring slice MUST land both backends in the
   same commit. Splitting across multiple smaller
   slices would silently diverge the two backends'
   outputs at non-zero clamp until the second slice
   landed.

2. **API surface area.** The launcher signature
   (`CudaPathTracer.cuh` + `CudaPathTracer.cu`),
   the launch-params POD
   (`OptixLaunchParams.h`), and the renderer entry
   signatures (`OptixRenderer.h` +
   `OptixRenderer.cpp`) all expose the
   `firefly_clamp` parameter at backend boundaries.
   Each boundary needs the parameter visible on its
   declaration side — there is no "internal helper"
   that hides the cross-cutting concern.

3. **Default-arg discipline.** Both
   `render_pathtrace` and
   `render_pathtrace_progressive` use `= 0.0f`
   default-args so existing callers do not need
   updates. This keeps `src/main.cpp`'s
   `run_render_optix_pathtrace` change to ONE line
   (or zero, if the implementer relies on the
   default-arg).

### 3.2 Test placement

The slice MAY add a host-only smoke test to
`tests/pathtracer_tests.cpp` exercising the
`firefly_clamp` validation — e.g. a placeholder test
that constructs a `PathTraceConfig` with
`firefly_clamp = 5.0f` and confirms the field is
preserved across copy / move. The test is OPTIONAL
because:

- The clamp's RUNTIME behaviour (the per-channel
  `fminf` against radiance) is not exercisable on
  the audit host without CUDA.
- The host-side validation
  (`firefly_clamp < 0.0f` rejection) IS exercisable
  via `PathTracer::render` returning an error
  message — but that requires constructing a
  `GpuScene`, which is awkward in a unit test.

The implementer's call. If a test is added, it
counts as the 9th file edit; the slice's BUILD_PLAN
entry must flag the further deviation.

---

## 4. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 4.1 Per-pixel arithmetic at `firefly_clamp == 0.0f`

For every existing caller (every `PathTraceConfig{}`
default-construction; every dispatcher today
defaults `firefly_clamp == 0.0f`), the per-pixel /
per-sample arithmetic is byte-identical. The new
strict-`>` gate ensures `firefly_clamp == 0.0f` is
not a special case in the `fminf` sense — it's a
no-op at the CONTROL-FLOW level (the branch is not
entered).

This means:

- `output/pathtrace_spp_1.ppm`,
  `output/pathtrace_spp_16.ppm`: byte-identical pixel
  data on a CUDA host pre-/post-PT-P.24.
- `output/optix_pathtrace_spp1.ppm`,
  `output/optix_pathtrace_spp16.ppm`: byte-identical.
- `output/gpu_rng_test.ppm`,
  `output/gpu_accumulation_test.ppm`: byte-identical
  (these dispatchers don't consume `firefly_clamp`).

### 4.2 The validation prelude in `PathTracer::render`

The PT-P.6 / PT-P.9 / PT-P.18 / PT-P.20 / PT-P.21
predecessors in `PathTracer::render`'s validation
prelude are byte-identical:

```cpp
if (cfg.samples_per_pixel <= 0) ...     // PT-P.9
if (effective_samples_per_pixel > kSamplesPerPixelCap) ...
if (cfg.max_bounces < 0) ...            // PT-P.6
if (effective_max_bounces > kMaxBouncesCap) ...
if (cfg.environment_intensity < 0.0f) ... // PT-P.21
```

The new `cfg.firefly_clamp < 0.0f` rejection is
APPENDED after the env-intensity check. The
existing rejections + clamps remain in place.

### 4.3 The OptiX raygen body (modulo the new clamp)

`__raygen__pathtrace` is byte-identical EXCEPT for
the new 5-line `if (firefly_clamp > 0.0f) ...` block
inserted before the `rgb_sum +=` accumulation. The
spp loop, the bounce loop, the relativity-mix
helpers, the post-accumulation Doppler / searchlight
stack: all preserved verbatim.

### 4.4 The CUDA path-tracer kernel body (modulo the new clamp)

`k_pathtrace_sample` is byte-identical EXCEPT for
the new 5-line `if (firefly_clamp > 0.0f) ...` block
inserted before the per-pixel write. The bounce
loop, the emission accumulation (PT-P.15
`is_emissive` short-circuit), the cosine-hemisphere
sampling, the closest-hit walk: all preserved
verbatim.

### 4.5 Other CUDA TUs

`src/cuda/CudaAccumulation.cu`,
`src/cuda/CudaAccumulation.cuh`,
`src/cuda/CudaRngTestKernel.cu`,
`src/cuda/CudaTestKernel.cu`,
`src/cuda/CudaTextureSampleTestKernel.cu`,
every other `.cu` and `.cuh` in `src/cuda/`:
byte-identical.

### 4.6 Other OptiX programs

`src/optix/OptixPipeline.{h,cpp}`,
`src/optix/OptixSBT.h`,
`src/optix/OptixDenoiser.{h,cpp}`,
the non-pathtrace raygen / miss / closest-hit programs
(`__raygen__pinhole`, `__miss__pathtrace`,
`__closesthit__pathtrace`, the AOV programs, etc.):
byte-identical.

The OptiX pipeline binding (the
`OptixPipelineOptions::path_tracer = true` block at
line ~340 in `OptixPipeline.cpp` per Stage 17A) is
byte-identical; the new `firefly_clamp` field flows
through `OptixLaunchParams` only, not through the
pipeline state.

### 4.7 Other path-tracer host code

- `src/pathtracer/PathTracer.h` — byte-identical. The
  `firefly_clamp = 0.0f` field shipped by PT-P.21 is
  preserved verbatim (PT-P.24 does not modify the
  `PathTraceConfig` struct; only its consumers).
- `src/pathtracer/RNG.{h,cuh}` — byte-identical.
  PT-P.18's per-input SplitMix64 mix is unchanged.
- `src/pathtracer/Sampling.{h,cuh}` — byte-identical.

### 4.8 Renderer + accumulation

- `src/renderer/AccumulationBuffer.{h,cpp}` —
  byte-identical. The per-sample buffer is filled by
  the kernel; the accumulator does not see the clamp
  (the clamp fires at per-sample, before the
  accumulator-add).
- `src/renderer/AOV.{h,cpp}`,
  `src/renderer/GpuAOVBuffer.{h,cpp}`,
  `src/renderer/Hit.h`: byte-identical.

### 4.9 Scene / material / texture / lighting / IO

All byte-identical:

- `src/scene/`, `src/material/`, `src/lighting/`,
  `src/texture/`, `src/io/`: zero bytes changed.
- `src/main.cpp`'s OTHER dispatchers
  (`run_render_aovs`,
  `run_render_optix_aovs`,
  `run_render_optix_textured_material`,
  `run_render_pathtrace`'s sphere/material/light/mesh
  setup, etc.): byte-identical. ONLY
  `run_render_optix_pathtrace`'s
  `render_pathtrace_progressive` call gains a
  trailing arg (or relies on default).

### 4.10 Scene fixtures

- All `*.rrscene` files under `scenes/`:
  byte-identical. The TEX-P.6 fixture's three-case
  validator output remains the regression baseline.

### 4.11 Tests

- All `tests/*.cpp` files: byte-identical (or +1
  optional placeholder test per §3.2). The
  `pathtracer_tests` binary's internal count grows
  9 → 10 IF the optional test is added; otherwise
  remains at 9.

### 4.12 CLI surface

- No new `--*` flag for PT-P.24. The clamp value is
  only settable programmatically via
  `cfg.firefly_clamp = N`; no
  `--firefly-clamp <value>` modifier is added.
  Adding a CLI flag is its own follow-up slice if
  desired.

### 4.13 Tools / build

- `tools/verify_cuda_host.py`: byte-identical (the
  runner exercises the existing `--render-pathtrace`
  + `--render-optix-pathtrace` commands; the new
  field is unused by default).
- `CMakeLists.txt`: byte-identical (no new
  dependencies, no new test targets unless §3.2's
  optional test is added).

### 4.14 Other audits / plans

- `docs/PATH_TRACER_POLISH_PLAN.md`: optionally add
  a one-line "PT-P.24 shipped (kernel wiring)" note
  at the top of §4.7. NOT required.
- `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_TASK.md`,
  `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_AUDIT.md`,
  every other PT-P.x task / audit doc: NO edits.
- The TEX-P.x arc + the CUDA-H.x arc: NO edits.

---

## 5. PASS criteria

The implementation slice passes when ALL of the
following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF): clean
  build, zero new warnings. The `is_emissive` /
  `make_pixel_rng` / `firefly_clamp`-using code is
  type-checked through every host-side
  `PathTraceConfig` consumer.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON): clean
  build, zero new warnings.

### 5.2 Tests

- `ctest --output-on-failure` from `build`: 7/7
  PASS.
- `ctest --output-on-failure` from `build-ON`: 8/8
  PASS.
- Test counts unchanged from PT-P.21 / PT-P.18
  (unless §3.2's optional placeholder test is
  added; in that case the `pathtracer_tests`
  internal count grows 9 → 10 but the ctest binary
  count stays at 7 / 8).

### 5.3 Source diff size

This slice exceeds the established "max 2 source
files" rule (see §3.1). The expected size:

- `src/pathtracer/PathTracer.cpp`: ~5 added.
- `src/cuda/CudaPathTracer.cuh`: ~5-10 added.
- `src/cuda/CudaPathTracer.cu`: ~12-18 added.
- `src/optix/OptixLaunchParams.h`: ~1-3 added.
- `src/optix/OptixRenderer.h`: ~2 added.
- `src/optix/OptixRenderer.cpp`: ~10-15 added.
- `src/optix/OptixPrograms.cu`: ~12-18 added.
- `src/main.cpp`: ~1-3 added.

TOTAL across all 8 source files: ~50-75 added /
~0 deleted. Anything LARGER than 100 added flagged
in the BUILD_PLAN entry as a deviation (note: the
50-line baseline is itself a flagged deviation
from the max-2-source-files rule; see §3.1).

### 5.4 No-touch invariants

`git diff` after the slice MUST show zero bytes
changed in:

- `src/cuda/` — every file EXCEPT
  `CudaPathTracer.cu`, `CudaPathTracer.cuh`.
- `src/optix/` — every file EXCEPT
  `OptixLaunchParams.h`, `OptixRenderer.{h,cpp}`,
  `OptixPrograms.cu`.
- `src/pathtracer/` — every file EXCEPT
  `PathTracer.cpp`.
- `src/renderer/`
- `src/core/`
- `src/io/`
- `src/scene/`
- `src/material/`
- `src/lighting/`
- `src/texture/`
- every `*.rrscene` file under `scenes/`
- every `tests/*.cpp` file (unless §3.2's optional
  test is added; in which case `pathtracer_tests.cpp`
  is the only allowed test edit).
- `tools/verify_cuda_host.py`
- `CMakeLists.txt`

Verifiable by:

```
git diff -- \
  src/renderer/ src/core/ src/io/ src/scene/ \
  src/material/ src/lighting/ src/texture/ \
  scenes/ tools/verify_cuda_host.py CMakeLists.txt \
  | wc -l
=> 0
```

(Plus more granular checks for each of the 8
authorised files.)

### 5.5 Behavioural smoke (audit host)

- `./build/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene` continues to emit
  the documented "requires CUDA" audit-host
  fallback byte-identically with the pre-PT-P.24
  baseline. The new clamp is unreachable on the
  audit host (the dispatcher returns from the
  `requires CUDA` branch before any kernel launch);
  the smoke confirms the fallback path is
  unchanged.
- `./build-ON/bin/RelativityRender
  --render-pathtrace
  scenes/test_full_scene.rrscene`: same.
- `./build-ON/bin/RelativityRender
  --render-optix-pathtrace
  scenes/test_full_scene.rrscene`: emits the
  documented OptiX-SDK-required audit-host
  fallback byte-identically with the pre-PT-P.24
  baseline.
- `./build/bin/RelativityRender --scene-info
  scenes/test_textured_material.rrscene`: emits the
  TEX-P.6 fixture's expected three-case log
  sequence byte-identically (one Case 1 info + two
  Case 3 warnings; `fixups applied: 2`). Confirms
  zero PT-P.24 ripple onto the texture validator.

### 5.6 Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing
  entry matching the established PT-P.x format
  (Scope / What ships / What does NOT change /
  Behaviour matrix / Master rule compliance /
  Verified at the build).
- The entry MUST include a "Backend symmetry note"
  subsection citing §2 of this task. The note
  records that the slice exceeded the max-2-source-
  files rule because the cross-backend symmetric-
  output invariant required atomic landing.
- The entry MUST include a "Default-off pixel-
  identity proof" subsection demonstrating (by
  inspection) that `firefly_clamp == 0.0f`
  produces byte-identical output:
    - The new `if (firefly_clamp > 0.0f)` branches
      in both backends do not enter at the default.
    - No other code change in either kernel.
    - Therefore the per-pixel write / per-sample
      accumulation is bit-identical with the
      pre-PT-P.24 arithmetic.
- The entry references
  `docs/PATH_TRACER_POLISH_PLAN.md` §4.7 +
  `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_TASK.md` +
  this task file as the source of the
  specification.

### 5.7 Master rule compliance

- Build incrementally (rule 1) + every step
  compilable (rule 2): both audit-host configs
  green.
- No fake stubs (rule 3): the kernel guards are
  real `fminf` calls applied at the per-sample
  radiance write site; the lower-bound rejections
  are real validation paths.
- No CPU per-pixel work (rule 5/7): the clamp runs
  device-side per kernel thread per sample. ZERO
  new host-side per-pixel work.
- Update BUILD_PLAN (rule 8): the slice-closing
  entry.

---

## 6. Out-of-scope (deferred to future PT-P.x slices)

The following items are explicitly NOT part of this
task:

- **Upper-bound clamp on `firefly_clamp` itself**.
  No `kFireflyClampCap` constant is added. High-
  variance scenes may want clamp values like
  `100.0f` or larger; an upper-bound cap would
  reject them. If a future slice wants one, it ships
  separately.
- **CLI flag for `firefly_clamp`** (e.g.
  `--firefly-clamp <value>`). The clamp is settable
  programmatically only. Adding a CLI flag is its
  own follow-up slice with its own task brief.
- **Adaptive clamp** (auto-detection of
  high-variance pixels, per-pixel clamp tuning).
  Out of scope for v1 firefly support; the constant-
  per-launch clamp is the baseline.
- **Other firefly-management techniques** (Russian
  roulette, splatting, weighted MIS): still out of
  scope for the §4.7 polish.

After PT-P.24 + PT-P.25 (the audit) land, the entire
PT-P.x §4.7 polish has shipped (placeholder + kernel
wiring). The next §4 polish item is — there is no
"next" — §4.7 is the last item; the §4 arc closed at
PT-P.22, and this kernel-wiring sub-arc closes at
PT-P.25.

---

## 7. Runtime CUDA-host checks needed

PT-P.24 is the SECOND PT-P.x slice with mandatory
runtime CUDA-host verification (PT-P.18 was the
first). Six checks the operator MUST perform on a
CUDA host before declaring PT-P.25 (the audit) PASS:

### 7.1 Default-off byte-IDENTITY (CUDA path)

Run `--render-pathtrace
scenes/test_full_scene.rrscene` BEFORE and AFTER the
PT-P.24 commit. The `cfg.firefly_clamp == 0.0f`
default means the kernel's `if (firefly_clamp >
0.0f)` branch is not entered; the per-pixel write
is byte-identical with the pre-PT-P.24 arithmetic.
Procedure:

```
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm  /tmp/post_p24_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/post_p24_spp16.ppm

$ git checkout 47ed5cb -- \
    src/cuda/CudaPathTracer.cuh \
    src/cuda/CudaPathTracer.cu \
    src/pathtracer/PathTracer.cpp
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm  /tmp/pre_p24_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/pre_p24_spp16.ppm
$ git checkout HEAD -- src/

$ cmp /tmp/post_p24_spp1.ppm  /tmp/pre_p24_spp1.ppm  ; echo $?  # 0
$ cmp /tmp/post_p24_spp16.ppm /tmp/pre_p24_spp16.ppm ; echo $?  # 0
```

Expected: `cmp` reports the files are identical
(exit code 0). If they differ, the CUDA-side wiring
introduced an unintended pixel-diff and the PT-P.25
audit's verdict for §1 should flip to REPAIR.

### 7.2 Default-off byte-IDENTITY (OptiX path)

Same procedure with
`--render-optix-pathtrace`. The OptiX raygen's
`if (firefly_clamp > 0.0f)` branch is not entered
at the default; the per-sample accumulation is
byte-identical with the pre-PT-P.24 arithmetic.

```
$ cmp /tmp/post_p24_optix_spp1.ppm  /tmp/pre_p24_optix_spp1.ppm  ; echo $?  # 0
$ cmp /tmp/post_p24_optix_spp16.ppm /tmp/pre_p24_optix_spp16.ppm ; echo $?  # 0
```

### 7.3 Non-zero clamp produces visible reduction (CUDA path)

The operator constructs a high-variance scene
(e.g. a small bright emitter near the camera) and
renders it twice:

```
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace high_variance_scene.rrscene  # firefly_clamp == 0
# Inspect output/pathtrace_spp_*.ppm — bright fireflies visible.
```

Then a future CLI flag (or a manual harness using
`PathTraceConfig{ firefly_clamp = 8.0f }` directly)
runs the same scene with the clamp on; the
resulting PPMs should show visibly less firefly
intensity. PT-P.24 does NOT add the CLI flag, so
this check requires either a one-off binary
modification OR a follow-up slice that adds the
flag. The check is OPTIONAL for PT-P.25's audit;
the structural guarantees in §1.2 + §1.7 +
§5.6's "Default-off pixel-identity proof" cover the
correctness of the clamp expression.

### 7.4 Cross-backend convergence at non-zero clamp

The operator renders the same high-variance scene
with `firefly_clamp = 8.0f` on BOTH backends and
confirms the resulting PPMs are statistically
similar (means within sampling noise; visible
fireflies similarly suppressed). Bit-for-bit
identity is NOT expected (the integrators have
divergent code paths in their bounce loops; see
§2.2). This check is OPTIONAL but recommended; it
is the first cross-backend smoke for a kernel-side
feature.

### 7.5 ctest cycle on CUDA host

`ctest --output-on-failure` from a CUDA-built
`build-cuda` directory must pass — every existing
test continues to pass; no new failure introduced.
Mandatory.

### 7.6 Refresh CUDA-H.x verification report

Re-run `tools/verify_cuda_host.py` after the
PT-P.24 commit. The runner's per-test status
remains PASS (or BLOCKED for items already BLOCKED).
The report's "Tree state" hash line changes because
the source tree changed. Commit the refreshed
`docs/CUDA_HOST_VERIFICATION_REPORT.md` along with
the PT-P.25 audit slice (mirrors PT-P.18's §6.4
contract).

---

## 8. Why this slice is the next viable item

Three reasons:

### 8.1 PT-P.22 audit verdict was clean

`docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_AUDIT.md`
§7 records overall PASS, zero REPAIR items, ZERO
DEFERRED rows. The placeholder is in a known-good
baseline; PT-P.24 starts from there.

### 8.2 The change is contained to known sites

§3 enumerates exactly 8 source files. Every site
is named (file:line); the implementer does not
need to discover new call sites. The
`make_pixel_rng` / `is_emissive` / RNG salt
constants from PT-P.{15,18} all preserved; no
ripple onto unrelated path-tracer features.

### 8.3 The arc closes naturally

After PT-P.24 + PT-P.25 land, the entire
PATH_TRACER_POLISH_PLAN.md §4.7 polish has shipped
(placeholder + kernel wiring). The seven-item §4
arc is fully closed; the next polish cycle starts
fresh on a different `master order` item.

### What's NOT yet safe (and why §4.7-wiring is preferred over alternatives)

The PT-P.22 audit's "Recommended next step" listed
three directions:

- (a) PT-P.23 "Wire firefly clamp" (THIS task);
- (b) trigger CUDA-host verification run that flips
  the accumulated DEFERRED rows from PT-P.{4,7,10,
  13,16,19} to PASS;
- (c) pivot to master order #16 path-tracing
  feature work (NEE / non-diffuse BSDFs / multi-mesh
  upload).

(a) — this task — is preferred over (b) because the
existing CUDA-host verification run is not gated on
PT-P.x kernel changes; the operator can run it at
any time after PT-P.x source-touching slices have
landed. PT-P.24 happens to be the LAST PT-P.x source-
touching slice; running the verification suite
post-PT-P.24 catches any accumulated regressions
across the entire PT-P.x arc, not just PT-P.24's.

(a) is preferred over (c) because (c) is its own
multi-slice arc (NEE alone is ~10 source files +
new tests + cross-backend integration); pivoting to
(c) before closing the §4.7 sub-arc would leave the
firefly-clamp placeholder dangling indefinitely.
PT-P.24 + PT-P.25 deliver the §4.7 promise in two
slices; (c) can start in a clean tree afterwards.

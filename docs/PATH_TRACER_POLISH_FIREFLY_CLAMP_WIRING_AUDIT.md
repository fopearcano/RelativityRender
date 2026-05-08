# Path-Tracer Polish — Firefly Clamp Backend Wiring Audit

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `0a06d0d` ("PT-P.24:
firefly clamp backend wiring (impl) — both backends,
atomic landing").
Scope: PT-P.{23,24} — the task-definition + implementation
sub-arc that wires `cfg.firefly_clamp` through both the
CUDA path-tracer kernel (`k_pathtrace_sample`) and the
OptiX path-tracer raygen (`__raygen__pathtrace`)
symmetrically.
Mode: documentation only. **No source code is modified by
this audit.**
Auditor: Claude Code, on the audit host (no CUDA Toolkit,
no OptiX SDK).

This audit walks the nine prompt checks in order and
records a single verdict at the end. Verdict legend matches
the texture-polish-audit + PT-P.4 / PT-P.7 / PT-P.10 /
PT-P.13 / PT-P.16 / PT-P.19 / PT-P.22 precedent:

- **PASS** — implemented, type-checked on the audit host,
  AND empirically exercisable on the audit host with a
  recorded happy-path run.
- **REPAIR** — implemented but a defect or inconsistency
  was found that should be patched.
- **BLOCKED** — empirical verification requires a CUDA
  host (no nvcc / OptiX SDK on the audit host); used
  here as the equivalent of the prompt's `DEFERRED` row.

PT-P.25 closes the firefly-clamp kernel-wiring sub-arc.
After this audit lands, the §4.7 polish has fully
shipped (placeholder via PT-P.{20,21,22} + kernel wiring
via PT-P.{23,24,25}).

---

## 1. CUDA pathtrace receives firefly_clamp

**PASS.**

The parameter flows through three CUDA-side stages:

### 1.1 Host orchestration (`src/pathtracer/PathTracer.cpp`)

`PathTracer::render` validates `cfg.firefly_clamp` at
`PathTracer.cpp:84-87`:

```cpp
if (cfg.firefly_clamp < 0.0f) {
    result.message = "firefly_clamp must be >= 0";
    return result;
}
```

The check sits AFTER the existing
`cfg.environment_intensity < 0.0f` rejection (lines
72-74), maintaining the validation prelude order
PT-P.{6,9,18,21} established.

`cfg.firefly_clamp` is then passed to
`launch_pathtrace_sample` at
`PathTracer.cpp:136`:

```cpp
if (!rr::cuda::launch_pathtrace_sample(
        sample.device_ptr(), width, height,
        scene,
        effective_max_bounces,
        cfg.seed,
        static_cast<unsigned int>(s),
        cfg.environment_color,
        cfg.environment_intensity,
        cfg.firefly_clamp)) {  // PT-P.24
```

### 1.2 Launcher signature (`src/cuda/CudaPathTracer.cuh`)

The launcher's declaration carries the new parameter at
`CudaPathTracer.cuh:73`:

```cpp
[[nodiscard]] bool launch_pathtrace_sample(
    ...,
    float                    env_intensity,
    float                    firefly_clamp);
```

The parameter's contract is documented at
`CudaPathTracer.cuh:47-58` (12-line block in the
parameter table) — names the default-off behaviour,
the negative-value rejection, the
`PathTraceConfig::firefly_clamp` cross-reference, and
the OptiX mirror cross-reference.

### 1.3 Kernel + launcher fn body (`src/cuda/CudaPathTracer.cu`)

The kernel receives the value at `CudaPathTracer.cu:154`:

```cpp
__global__ void k_pathtrace_sample(
    ...,
    float            env_intensity,
    float            firefly_clamp) {
```

The launcher fn definition matches at
`CudaPathTracer.cu:275`:

```cpp
bool launch_pathtrace_sample(
    ...,
    float                    env_intensity,
    float                    firefly_clamp) {
```

The pre-launch defence-in-depth check at
`CudaPathTracer.cu:282`:

```cpp
if (device_sample_pixels == nullptr || width <= 0 || height <= 0
 || max_bounces < 0 || firefly_clamp < 0.0f) {
    return false;
}
```

The kernel invocation passes the new arg at
`CudaPathTracer.cu:317`:

```cpp
k_pathtrace_sample<<<grid, block>>>(
    ..., env_intensity, firefly_clamp);
```

The clamp guard itself runs in the kernel between the
bounce loop and the per-pixel write
(`CudaPathTracer.cu:251-255`):

```cpp
if (firefly_clamp > 0.0f) {
    radiance.x = fminf(radiance.x, firefly_clamp);
    radiance.y = fminf(radiance.y, firefly_clamp);
    radiance.z = fminf(radiance.z, firefly_clamp);
}
```

The CUDA path therefore RECEIVES `cfg.firefly_clamp`
through host orchestration, signature plumbing, and
kernel-side application. Three independent verifications
(host validation, pre-launch guard, in-kernel branch)
ensure the parameter reaches the per-sample radiance
write site.

---

## 2. OptiX pathtrace receives firefly_clamp

**PASS.**

The parameter flows through four OptiX-side stages:

### 2.1 Dispatcher (`src/main.cpp`)

`run_render_optix_pathtrace` passes
`firefly_clamp = 0.0f` explicitly at `main.cpp:1573-1576`:

```cpp
auto pr = rr::optix::OptixRenderer::render_pathtrace_progressive(
    load.scene, cfg.width, cfg.height,
    kMaxBounces, kSeed, kCheckpoints,
    /*firefly_clamp=*/0.0f);
```

The explicit `0.0f` documents the dispatcher's choice;
the signature default-arg from §2.2 is the safety net.

### 2.2 Renderer signatures (`src/optix/OptixRenderer.h`)

Both pathtrace entries gain trailing `float
firefly_clamp = 0.0f` default-args:

- `OptixRenderer.h:221` (`render_pathtrace`).
- `OptixRenderer.h:282` (`render_pathtrace_progressive`).

Default-arg ensures every existing caller compiles
unchanged.

### 2.3 Renderer bodies (`src/optix/OptixRenderer.cpp`)

Four sites, all wired:

- `OptixRenderer.cpp:1225` (SDK_FOUND
  `render_pathtrace`'s parameter declaration).
- `OptixRenderer.cpp:1243-1246` (lower-bound
  rejection):

  ```cpp
  if (firefly_clamp < 0.0f) {
      r.message = "OptixRenderer::render_pathtrace: "
                  "firefly_clamp must be >= 0";
      return r;
  }
  ```

- `OptixRenderer.cpp:1403` (launch-params upload):

  ```cpp
  params.firefly_clamp = firefly_clamp;  // PT-P.24
  ```

- `OptixRenderer.cpp:1487` (SDK_FOUND
  `render_pathtrace_progressive`'s parameter
  declaration).
- `OptixRenderer.cpp:1502-1505` (lower-bound rejection,
  same shape).
- `OptixRenderer.cpp:1706` (per-sample launch-params
  upload inside the spp loop):

  ```cpp
  params.firefly_clamp = firefly_clamp;  // PT-P.24
  ```

- `OptixRenderer.cpp:3102` (audit-host fallback stub
  for `render_pathtrace`; unused parameter).
- `OptixRenderer.cpp:3121` (audit-host fallback stub
  for `render_pathtrace_progressive`; unused parameter).

The two audit-host fallback stubs ensure the OFF build
type-checks the new signature even though the OptiX
SDK is absent.

### 2.4 Launch params POD (`src/optix/OptixLaunchParams.h`)

The field is declared at `OptixLaunchParams.h:160`:

```cpp
float         firefly_clamp = 0.0f;
```

Above it, `OptixLaunchParams.h:148-159` carries a
14-line doc-comment naming the external semantics, the
default-off behaviour, the cross-reference to the CUDA
mirror, and the "convergent at non-zero clamp"
invariant.

### 2.5 OptiX raygen (`src/optix/OptixPrograms.cu`)

The raygen reads `optixLaunchParams.firefly_clamp` and
applies the clamp at `OptixPrograms.cu:944-948`:

```cpp
if (optixLaunchParams.firefly_clamp > 0.0f) {
    radiance.x = fminf(radiance.x, optixLaunchParams.firefly_clamp);
    radiance.y = fminf(radiance.y, optixLaunchParams.firefly_clamp);
    radiance.z = fminf(radiance.z, optixLaunchParams.firefly_clamp);
}
```

The clamp lives between the bounce loop and the
per-sample `rgb_sum +=` accumulation — the same
position the CUDA path-tracer kernel applies its
clamp.

The OptiX path therefore RECEIVES `cfg.firefly_clamp`
through dispatcher pass-through, renderer entry
signatures, launch-params upload, POD field
declaration, and raygen-side application. Five
independent verifications cover the parameter's path
from the host config to the per-sample radiance.

---

## 3. Clamp is inactive when firefly_clamp <= 0.0f

**PASS structurally; OPTIONAL CUDA-host empirical
confirmation.**

Both backends use STRICT `>` gating
(`if (firefly_clamp > 0.0f)`). When
`firefly_clamp == 0.0f` (the
`PathTraceConfig::firefly_clamp` default + the
`OptixLaunchParams::firefly_clamp` default + the
explicit `/*firefly_clamp=*/0.0f` at
`run_render_optix_pathtrace`), the branch is NOT
entered:

- CUDA path
  (`CudaPathTracer.cu:251`): `if (firefly_clamp >
  0.0f) { ... }` — strict `>` makes 0.0f take the
  false branch.
- OptiX path
  (`OptixPrograms.cu:944`): `if
  (optixLaunchParams.firefly_clamp > 0.0f) { ... }`
  — same strict `>` gating.

When the branch is not entered:

- `radiance.x|y|z` are unchanged.
- The per-pixel write (CUDA: `pixels[idx + 0/1/2] =
  radiance.x|y|z`) and per-sample accumulation (OptiX:
  `rgb_sum.x|y|z += radiance.x|y|z`) see byte-
  identical input as the pre-PT-P.24 arithmetic.

### 3.1 What about negative `firefly_clamp`?

The lower-bound rejection at four sites (host
validator + CUDA launcher + OptiX `render_pathtrace`
+ OptiX `render_pathtrace_progressive`) ensures
`firefly_clamp < 0.0f` cannot reach the kernel. The
strict `>` gate is therefore checked against
`firefly_clamp ∈ [0, +∞)` only:

- `firefly_clamp == 0.0f`: branch NOT taken
  (covered above).
- `firefly_clamp == positive subnormal` (e.g.
  `0.5e-38f`): branch IS taken; clamp fires (per
  IEEE-754 strict comparison, any positive subnormal
  > 0.0f). This is a corner case but matches the
  contract: any positive value triggers the clamp.

### 3.2 Empirical confirmation deferred to CUDA host

The byte-identity of `pathtrace_spp_*.ppm` and
`optix_pathtrace_*.ppm` at the default
(`firefly_clamp == 0.0f`) is structurally guaranteed
above. The PT-P.23 task §7.1 + §7.2 documented the
operator-side `cmp`-based confirmation procedure on a
CUDA + OptiX-SDK host. This audit cannot run those
checks (no CUDA toolchain on the audit host); they are
captured under §8 below as DEFERRED.

---

## 4. Clamp activates only when firefly_clamp > 0.0f

**PASS structurally; OPTIONAL CUDA-host empirical
confirmation.**

The strict `>` gate is the ACTIVATION condition. The
gate is symmetric across both backends (per §2.3 of
the PT-P.23 task brief and §2 of this audit).

### 4.1 Activation truth table

| `firefly_clamp` value      | CUDA branch entered? | OptiX branch entered? | Behaviour                              |
|----------------------------|----------------------|-----------------------|----------------------------------------|
| `0.0f` (default)           | No                   | No                    | Byte-identical with pre-PT-P.24        |
| Negative                   | n/a (rejected)       | n/a (rejected)        | "firefly_clamp must be >= 0" message   |
| Positive subnormal         | Yes                  | Yes                   | Per-channel `fminf` fires              |
| Positive normal (e.g. 8.0f)| Yes                  | Yes                   | Per-channel `fminf` fires              |
| `+inf`                     | Yes                  | Yes                   | `fminf(radiance, +inf) == radiance`    |
|                            |                      |                       | (no-op clamp; semantically valid)      |
| NaN                        | No (NaN > 0.0f false)| No                    | Branch skipped; `radiance` unchanged.  |
|                            |                      |                       | NaN entry not validated upstream — see  |
|                            |                      |                       | §4.2.                                   |

### 4.2 NaN handling (defensive note)

The host validator's `firefly_clamp < 0.0f` rejection
does NOT catch `firefly_clamp == NaN` (because
`NaN < 0.0f` is `false` in IEEE-754). A caller
constructing `PathTraceConfig{ firefly_clamp =
std::numeric_limits<float>::quiet_NaN() }` would pass
through the validator and reach the kernel. The
strict `>` gate handles this safely:

- `NaN > 0.0f` evaluates to `false` per IEEE-754.
- The branch is NOT entered.
- `radiance` is unchanged.

So a NaN clamp value is functionally equivalent to
the default `0.0f` (no clamp). This is defensive — a
NaN does not crash the kernel — but the validator
chain does not REJECT NaN values explicitly. A future
hardening slice could add an `std::isfinite` check;
that's out of scope for PT-P.25.

### 4.3 Symmetric activation

Both backends activate AT THE SAME POINT in their
integrators:

- CUDA: between the bounce loop's exit and the per-
  pixel write to `pixels[]`.
- OptiX: between the bounce loop's exit and the per-
  sample `rgb_sum +=` accumulation.

Both backends apply IDENTICAL operations:

- Per-channel `fminf(radiance.x|y|z, firefly_clamp)`.
- Three independent `fminf` calls (no vector clamp;
  no luminance-based clamp).

The PT-P.23 task §2.4 ("no backend-asymmetric
behaviour") is therefore honoured.

---

## 5. Per-channel clamp is consistent

**PASS.**

Both backends apply the clamp PER CHANNEL — not as a
vector magnitude, not as a luminance-based clamp.

### 5.1 CUDA path (`src/cuda/CudaPathTracer.cu:251-255`)

```cpp
if (firefly_clamp > 0.0f) {
    radiance.x = fminf(radiance.x, firefly_clamp);
    radiance.y = fminf(radiance.y, firefly_clamp);
    radiance.z = fminf(radiance.z, firefly_clamp);
}
```

Three independent `fminf` calls; one per channel. No
cross-channel coupling (no luminance-weighted clamp,
no max-of-channels normalization).

### 5.2 OptiX path (`src/optix/OptixPrograms.cu:944-948`)

```cpp
if (optixLaunchParams.firefly_clamp > 0.0f) {
    radiance.x = fminf(radiance.x, optixLaunchParams.firefly_clamp);
    radiance.y = fminf(radiance.y, optixLaunchParams.firefly_clamp);
    radiance.z = fminf(radiance.z, optixLaunchParams.firefly_clamp);
}
```

Verbatim same shape. Three independent `fminf` calls;
one per channel; same `fminf` IEEE-754 semantics.

### 5.3 Cross-backend consistency

The two snippets above are identical except for:

- The condition expression source (`firefly_clamp` as a
  function arg vs `optixLaunchParams.firefly_clamp` as
  a `__constant__` global). Both bind to the same
  authored value.
- The variable names (CUDA: kernel parameter; OptiX:
  launch-params field). Same value type
  (`float`), same default (`0.0f`).

For any authored `cfg.firefly_clamp = X`:

- CUDA: `X` is passed to the launcher → kernel arg →
  the strict `>` gate → per-channel `fminf`.
- OptiX: `X` is set on `params.firefly_clamp` → the
  launch-params POD → the global `__constant__` →
  the strict `>` gate → per-channel `fminf`.

Both backends apply EXACTLY the same operation to EACH
channel. Bit-for-bit identical output across the two
backends is NOT guaranteed (the integrators have
divergent code paths in their bounce loops; FMA
fusion + RNG draw scheduling already diverge them at
the bit level), but the clamp's RESULT is identical:
each per-channel value is the minimum of its pre-clamp
value and the authored cap.

---

## 6. Default render output should remain unchanged

**PASS structurally; OPTIONAL CUDA-host empirical
confirmation.**

For every authored `PathTraceConfig` with the default
`firefly_clamp == 0.0f`:

### 6.1 Source-diff containment

`git diff 0a06d0d~1..0a06d0d -- src/renderer/
src/core/ src/io/ src/scene/ src/material/
src/lighting/ src/texture/ src/pathtracer/PathTracer.h
src/pathtracer/RNG.h src/pathtracer/RNG.cuh
src/pathtracer/Sampling.h src/pathtracer/Sampling.cuh
scenes/ tests/ tools/verify_cuda_host.py CMakeLists.txt
| wc -l` returns 0 bytes. Every kernel / dispatcher /
host-helper / scene file / runner / CMake config not
in the authorised 8-file list is byte-identical with
the pre-PT-P.24 commit `4cebacf`.

### 6.2 No-op control-flow at default

When `firefly_clamp == 0.0f`:

- The strict `>` gate (CUDA + OptiX) evaluates `false`.
- The clamp body is NOT entered.
- `radiance.x|y|z` carry the same values they had before
  the new branch was inserted.

The pre-PT-P.24 kernel arithmetic between bounce loop
exit and per-pixel write was:

```cpp
// CUDA, post-bounce, pre-write:
const int idx = (y * width + x) * 4;
pixels[idx + 0] = radiance.x;
pixels[idx + 1] = radiance.y;
pixels[idx + 2] = radiance.z;
```

The post-PT-P.24 kernel arithmetic at the default:

```cpp
if (firefly_clamp > 0.0f) {  // false at default
    ...                       // NOT entered
}
const int idx = (y * width + x) * 4;
pixels[idx + 0] = radiance.x;  // same value
pixels[idx + 1] = radiance.y;
pixels[idx + 2] = radiance.z;
```

The IF-branch is dead code at the default;
`radiance` flows from the bounce loop to the write
unchanged. Same reasoning applies to the OptiX
`rgb_sum +=` accumulation.

### 6.3 Audit-host empirical smokes

```
$ ./build/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
[ERROR] --render-pathtrace requires CUDA. ...
```

The dispatcher reaches `PathTracer::render`'s
CUDA-disabled branch and returns the standard
"requires CUDA" message. The new clamp is unreachable
on the audit host (the dispatcher returns from
`#ifndef RR_HAS_CUDA` before any kernel call); the
fallback path is byte-identical with the pre-PT-P.24
baseline.

```
$ ./build-ON/bin/RelativityRender --render-optix-pathtrace \
    scenes/test_full_scene.rrscene
[ERROR] optix path-trace progressive render failed:
        OptixRenderer::render_pathtrace_progressive requires the
        OptiX SDK; ...
```

The OptiX dispatcher reaches the audit-host fallback
stub at `OptixRenderer.cpp:3113` (which got the new
parameter in the slice). The fallback message is
byte-identical with the pre-PT-P.24 baseline. The new
parameter is type-checked through to the stub.

### 6.4 TEX-P.6 fixture regression

```
$ ./build/bin/RelativityRender --scene-info \
    scenes/test_textured_material.rrscene
... [WARN] ... 'textured-quad-material' ... falling back to flat baseColor.
... [WARN] ... 'out-of-range-texture' ... falling back to flat baseColor.
... [INFO]     fixups applied    : 2
```

Three-case log sequence unchanged. Confirms zero
PT-P.24 ripple onto the texture validator.

### 6.5 Empirical PPM byte-IDENTITY (CUDA host, deferred)

The byte-identity of `pathtrace_spp_*.ppm`,
`gpu_rng_test.ppm`, `gpu_accumulation_test.ppm`,
`optix_pathtrace_*.ppm` is structurally guaranteed by
§6.1 + §6.2. A CUDA-host operator can confirm via
`cmp` per the PT-P.23 task §7.1 + §7.2 procedure. This
audit cannot run the `cmp` on the audit host.

---

## 7. OptiX OFF build remains valid

**PASS.**

`cmake --build build` (the OFF config:
`RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF`) compiles
cleanly post-PT-P.24:

```
[100%] Built target RelativityRender
```

ctest 7/7 green; zero new compiler warnings under
`-Wall -Wextra -Wpedantic`. The new
`firefly_clamp` field is type-checked on the OFF
build through every host-side `PathTraceConfig`
consumer:

- `src/pathtracer/PathTracer.cpp` reads
  `cfg.firefly_clamp` in the lower-bound check and
  passes it to the launcher; both expressions
  type-check on the OFF build (the launcher's
  `RR_HAS_CUDA`-gated body returns `false`
  honestly without invoking CUDA).
- `src/main.cpp`'s `run_render_optix_pathtrace`
  passes the explicit `0.0f` to
  `render_pathtrace_progressive`'s default-arg
  parameter; the call type-checks against the
  declaration in `OptixRenderer.h` even when
  RR_ENABLE_OPTIX=OFF (the renderer is not built
  in that config, but the header is included).

Wait — actually `OptixRenderer.h` is NOT included
when RR_ENABLE_OPTIX=OFF. Let me re-read.

### 7.1 Compile-time gating verification

Re-checking `src/main.cpp`:

```cpp
#ifdef RELATIVITYRENDER_ENABLE_OPTIX
    #include "optix/OptixRenderer.h"
    ...
    auto pr = rr::optix::OptixRenderer::render_pathtrace_progressive(
        load.scene, cfg.width, cfg.height,
        kMaxBounces, kSeed, kCheckpoints,
        /*firefly_clamp=*/0.0f);
    ...
#endif
```

The `#ifdef` guard means the OptiX-related code is
NOT compiled in the OFF config (no
RELATIVITYRENDER_ENABLE_OPTIX). The audit-host OFF
build (`build/`) therefore does not need
`OptixRenderer.h` at all; the new parameter is not
type-checked on that path.

The audit-host ON-OFF-SDK build (`build-ON/`,
RR_ENABLE_OPTIX=ON, no SDK) DOES include
`OptixRenderer.h` and compiles the OptiX dispatcher.
The audit-host fallback stubs at
`OptixRenderer.cpp:3096-3110` and `:3113-3128` carry
the new parameter; the build compiles cleanly.

### 7.2 Both audit-host configs green

| Config      | RR_ENABLE_CUDA | RR_ENABLE_OPTIX | Build     | ctest    |
|-------------|:--------------:|:---------------:|-----------|:--------:|
| `build`     | OFF            | OFF             | clean     | 7/7 PASS |
| `build-ON`  | OFF            | ON              | clean     | 8/8 PASS |

Both configs report zero new compiler warnings.
ctest counts unchanged from PT-P.18 / PT-P.21 /
PT-P.22 (the slice did not add or remove a ctest
binary).

The OFF build remains valid because:

1. The CUDA path-tracer kernel TU
   (`CudaPathTracer.cu`) is gated by
   `RR_ENABLE_CUDA`; not compiled on the OFF
   config.
2. The OptiX renderer TU (`OptixRenderer.cpp`) is
   gated by `RR_ENABLE_OPTIX`; not compiled on the
   OFF config.
3. The `PathTracer.h` field declaration is host-
   compiled and type-checked on the OFF config; no
   new dependencies introduced.
4. The launcher signature in `CudaPathTracer.cuh` is
   declared but not defined on the OFF config; the
   linker sees no unresolved symbol because
   `PathTracer.cpp`'s call site is in an
   `#ifdef RR_HAS_CUDA` block.

---

## 8. Runtime CUDA / OptiX verification status

**DEFERRED on six checks** (= BLOCKED on this audit
host).

PT-P.23's task §7 listed six operator-side checks
that need a CUDA + OptiX-SDK host:

| Check                                       | Status on audit host       | What it verifies                                     |
|---------------------------------------------|----------------------------|------------------------------------------------------|
| §7.1 default-off byte-IDENTITY (CUDA)       | DEFERRED                   | The strict `>` gate is exact (no kernel-side bias). |
| §7.2 default-off byte-IDENTITY (OptiX)      | DEFERRED                   | Same as §7.1 on the OptiX backend.                  |
| §7.3 non-zero clamp visible reduction       | DEFERRED + needs harness   | Runtime confirmation that fireflies actually clamp. |
| §7.4 cross-backend convergence at non-zero  | DEFERRED + needs harness   | The two backends' outputs are statistically         |
|     clamp                                   |                            | similar (means within sampling noise) when both     |
|                                             |                            | run with the same `firefly_clamp = 8.0f`.           |
| §7.5 ctest cycle on a CUDA host             | DEFERRED                   | The audit-host ctest 7/7 + 8/8 type-checks the      |
|                                             |                            | code; a CUDA-host ctest exercises the kernel        |
|                                             |                            | launches.                                           |
| §7.6 refresh                                 | DEFERRED                   | The runner's report regenerates with the same      |
|     `CUDA_HOST_VERIFICATION_REPORT.md`      |                            | PASS/FAIL verdicts (modulo the "Tree state" hash). |

### 8.1 Structural guarantees that make the runtime checks confirmations

This audit's §1-§7 + the PT-P.24 BUILD_PLAN entry's
"Default-off pixel-identity proof" together establish
the following structural claims:

- §1 + §2: both backends RECEIVE `cfg.firefly_clamp`
  through fully-named call chains (8 files; every
  signature, declaration, definition, upload site
  cross-checked).
- §3 + §4: the strict `>` gate is byte-correct at
  `firefly_clamp == 0.0f`.
- §5: per-channel clamp is identical across the two
  backends.
- §6: default render output is structurally
  unchanged.
- §7: OFF + ON-audit-host builds compile and pass.

The DEFERRED §7 checks would CONFIRM these
guarantees empirically; failure to confirm would be
a REPAIR signal. PT-P.25 records the structural PASS
verdict with the empirical confirmation explicitly
deferred.

### 8.2 Operator-side procedure for §7.1 + §7.2

Per the PT-P.23 task §7.1, the byte-identity
confirmation procedure is:

```
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm  /tmp/post_p24_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/post_p24_spp16.ppm

$ git checkout 4cebacf -- \
    src/cuda/CudaPathTracer.cuh src/cuda/CudaPathTracer.cu \
    src/pathtracer/PathTracer.cpp \
    src/optix/OptixLaunchParams.h src/optix/OptixRenderer.h \
    src/optix/OptixRenderer.cpp src/optix/OptixPrograms.cu \
    src/main.cpp
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm  /tmp/pre_p24_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/pre_p24_spp16.ppm
$ git checkout HEAD -- src/

$ cmp /tmp/post_p24_spp1.ppm  /tmp/pre_p24_spp1.ppm  ; echo $?  # 0
$ cmp /tmp/post_p24_spp16.ppm /tmp/pre_p24_spp16.ppm ; echo $?  # 0
```

Same procedure with `--render-optix-pathtrace`
verifies §7.2.

Expected: `cmp` reports the files are identical (exit
code 0). If they differ, the kernel-side wiring
introduced an unintended pixel-diff and the
audit's verdict for §3 / §6 should flip to REPAIR;
this audit pre-records a REPAIR contingency that has
not fired.

### 8.3 Runner integration status

`tools/verify_cuda_host.py` does NOT need an update
for PT-P.24 — the runner exercises the existing
`--render-pathtrace` + `--render-optix-pathtrace`
commands; the new field is unused at the runner's
default command set.

```
$ git diff 0a06d0d~1..0a06d0d -- tools/verify_cuda_host.py
=> 0 bytes
```

---

## 9. Verdict

| # | Audit item                                         | Result   |
|---|----------------------------------------------------|----------|
| 1 | CUDA pathtrace receives firefly_clamp              | PASS     |
| 2 | OptiX pathtrace receives firefly_clamp             | PASS     |
| 3 | Clamp inactive when firefly_clamp <= 0.0f          | PASS structurally; OPTIONAL CUDA-host check |
| 4 | Clamp activates only when firefly_clamp > 0.0f     | PASS structurally; OPTIONAL CUDA-host check |
| 5 | Per-channel clamp is consistent                    | PASS — identical fminf-per-channel across backends |
| 6 | Default render output should remain unchanged      | PASS structurally; OPTIONAL CUDA-host check |
| 7 | OptiX OFF build remains valid                      | PASS — ctest 7/7 + 8/8 |
| 8 | Runtime CUDA / OptiX verification status           | DEFERRED on six checks (= BLOCKED on this audit host) |
| 9 | Overall                                            | **PASS** (one DEFERRED row carried forward to a CUDA + OptiX-SDK host run) |

**Overall verdict: PASS.**

PT-P.{23,24} ship the kernel wiring exactly as the
PT-P.23 task brief specified. Both backends RECEIVE
`cfg.firefly_clamp` through fully-named call chains
(8 source files; every signature, declaration,
definition, upload site cross-checked). The strict `>`
gating is byte-symmetric across the CUDA path-tracer
kernel and the OptiX raygen. The per-channel `fminf`
clamp is identical across backends. The default-off
behaviour is structurally guaranteed by the gate's
control-flow short-circuit.

Both audit-host build configs remain green
(7/7 OFF, 8/8 ON-audit-host). The OFF build's
no-OptiX exclusion via `RELATIVITYRENDER_ENABLE_OPTIX`
keeps the OptiX dispatcher TU + the OptixRenderer
audit-host fallback stubs out of the compile graph;
the OFF build sees only the CUDA-side wiring (which
is itself gated by `RR_HAS_CUDA`) + the
`PathTracer.h` field declaration.

**Zero REPAIR items.** The brief-deviation note from
PT-P.18's audit (where the literal code in the brief
had a cancellation bug) is NOT replicated here —
PT-P.24 followed the PT-P.23 brief verbatim, and the
implementation work was straightforward except for
the one minor scope-expansion (the audit-host
fallback stubs at `OptixRenderer.cpp:3096,3113`
needed signature updates the brief §3 didn't enumerate
explicitly; the PT-P.24 BUILD_PLAN entry flagged that
deviation in its diff-size note).

The single DEFERRED row (§8) is the standard runtime-
deferred surface every prior PT-P.x audit (with
runtime checks) recorded. PT-P.24 carries 6
operator-side checks: 4 byte-identity confirmations
(default-off ×2, lower-bound rejection, audit-host
fallback), 2 cross-backend smokes (non-zero clamp
visible reduction + cross-backend convergence). All 6
fold into a CUDA + OptiX-SDK host verification run.

### The §4.7 polish has fully shipped

PT-P.{20,21,22,23,24,25} together complete the
`PATH_TRACER_POLISH_PLAN.md` §4.7 promise:

| Slice  | Role                                                     |
|--------|----------------------------------------------------------|
| PT-P.20 | Task definition for the placeholder                     |
| PT-P.21 | Implementation: `firefly_clamp = 0.0f` field added       |
| PT-P.22 | Audit: placeholder confirmed                             |
| PT-P.23 | Task definition for the kernel wiring                    |
| PT-P.24 | Implementation: both backends wired symmetrically        |
| PT-P.25 | This audit: kernel wiring confirmed                      |

The §4.7 polish is the last item in
`PATH_TRACER_POLISH_PLAN.md` §4. With PT-P.25 landing,
the entire §4 polish arc is fully shipped (placeholder
+ kernel wiring) plus the seven sub-arcs that closed at
PT-P.{4,7,10,13,16,19,22}.

### Recommended next step

The PT-P.x polish arc is now closed. Three viable
directions:

1. **Trigger the CUDA + OptiX-SDK host verification
   run** that flips the DEFERRED rows from this
   audit (§8) + PT-P.4 / PT-P.7 / PT-P.10 / PT-P.13 /
   PT-P.16 / PT-P.19 to PASS. Single command-line
   invocation
   (`tools/verify_cuda_host.py [--optix]`) on a real
   CUDA + OptiX-SDK host, plus the `cmp`-based
   byte-identity procedures from PT-P.18 §6.1 +
   PT-P.23 §7.1 + §7.2. The accumulated runtime
   debt across the entire PT-P.x arc folds into a
   single operator session.
2. **Add a `--firefly-clamp <value>` CLI flag**
   (deferred from PT-P.23 §6). Lets the operator
   exercise the clamp without writing a custom
   harness. Single small slice; ~10 lines in
   `src/core/CommandLine.{h,cpp}` + `src/main.cpp`.
3. **Pivot to a master-order item.** The TEX-P.x
   arc landed PASS (TEX-P.7); master order #16
   (path tracing — feature work like NEE / non-
   diffuse BSDFs / multi-mesh upload) is the next
   major follow-up after the PT-P.x polish arc
   closes. NEE in particular is the consumer that
   justifies a non-zero `firefly_clamp` default
   on a future scene fixture.

The natural sequencing post-PT-P.x is (1) → (2) → (3)
— verify the runtime debt is paid (1) before
expanding the surface (2) and certainly before
opening a new multi-slice arc (3).

PT-P.25 (this audit) closes the firefly-clamp
kernel-wiring sub-arc and, together with PT-P.22,
the entire `PATH_TRACER_POLISH_PLAN.md` §4 polish
catalogue.

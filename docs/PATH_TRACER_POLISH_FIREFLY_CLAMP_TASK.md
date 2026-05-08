# Path-Tracer Polish — Firefly Clamp Placeholder Task

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_POLISH_PLAN.md` §4.7.
Selected via:
`docs/PATH_TRACER_POLISH_RNG_STABILITY_AUDIT.md` §9's
"Recommended next step" verdict (the §4.1 RNG stability
polish shipped PASS via PT-P.18 + PT-P.19; §4.7 is the
last remaining `PATH_TRACER_POLISH_PLAN.md` §4 item).
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next
slice (PT-P.21 implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's reasoning.

---

## 1. Exact issue

**Title.** PT-P.x — Firefly clamp placeholder
(`PathTraceConfig` field only; kernel guards deferred).

**Source.** `PATH_TRACER_POLISH_PLAN.md` §4.7.

**One-paragraph summary.** The path tracer accumulates raw
radiance per sample. Today's diffuse-Lambert + emissive-
only model produces few "fireflies" (runaway samples), but
the next NEE / area-light slice will introduce them.
Adding a clamp prematurely would change the integrator's
bias unsolicited; adding the FIELD now (default-off) lets
the future slice flip the flag without touching the
integrator's hot path or the public API. PT-P.21 is
deliberately scoped to add ONLY the
`PathTraceConfig::firefly_clamp = 0.0f` config field +
its doc-comment. The kernel guards (CUDA path-tracer +
OptiX path-tracer raygens) are EXPLICITLY DEFERRED to a
future slice that can land them together (the plan §4.7
"Risk" note warns the OptiX-side mirror is required so
the two backends' outputs stay convergent at non-zero
clamp; landing one without the other would diverge them).

**Property of this slice.** Every existing render is
byte-identical pre-/post-PT-P.21. The new field is
declared, defaults to `0.0f`, and is NOT read by any
caller. PT-P.21 is the smallest viable footprint for a
"forward-compatible placeholder": one struct field + one
doc-comment, in one source file.

**The single concrete change.**

### 1.1 Add the field to `src/pathtracer/PathTracer.h`

Insert the new field at the end of the existing
`PathTraceConfig` struct, after the `environment_intensity`
field at line 67. Suggested shape:

```cpp
struct PathTraceConfig {
    int          max_bounces = 4;
    int          samples_per_pixel = 16;
    unsigned int seed = 0u;
    rr::math::Vec3 environment_color     = {0.55f, 0.70f, 1.00f};
    float          environment_intensity = 0.30f;

    // PT-P.21 placeholder: per-channel firefly clamp on the
    // per-sample radiance. Default 0.0f disables the clamp
    // (the integrator stays unbiased; the field is currently
    // NOT read by any kernel). When > 0, a future slice will
    // wire the value through both backends' path-trace
    // raygens so each per-sample `radiance.x|y|z` is
    // `fminf(radiance.x|y|z, firefly_clamp)` before being
    // added to the accumulator.
    //
    // Default-off rationale: clamping introduces a small
    // downward bias in scenes with high-variance light paths
    // (e.g. small bright emitters); making it opt-in keeps
    // the unbiased integrator the canonical baseline.
    //
    // Wiring is deferred per PT-P.20 task (this file): the
    // CUDA path-trace kernel + the OptiX `__raygen__pathtrace`
    // need a paired update so the two backends' outputs
    // remain convergent at non-zero clamp; landing one
    // without the other would diverge them. The PT-P.21
    // implementation slice ships ONLY this field + its
    // doc-comment.
    float firefly_clamp = 0.0f;
};
```

The exact wording is the implementer's choice; the
contract is that the doc-comment must:

- Name the field's external semantics (per-channel clamp
  on per-sample radiance).
- Name the default-off behaviour (`0.0f` disables; the
  integrator is unbiased).
- Note the field is currently NOT read (the placeholder
  is forward-compatible).
- Cross-reference PT-P.20 (this task) so a future
  contributor finds the deferral rationale.

### 1.2 No kernel guards in this slice

The §4.7 plan includes a kernel-guard sketch:

```cpp
if (firefly_clamp > 0.0f) {
    radiance.x = fminf(radiance.x, firefly_clamp);
    radiance.y = fminf(radiance.y, firefly_clamp);
    radiance.z = fminf(radiance.z, firefly_clamp);
}
```

PT-P.21 does NOT ship this. The reasons:

1. **Max-2-source-files rule.** Threading
   `firefly_clamp` through both backends requires
   editing at least 7 source files
   (`PathTracer.h`, `PathTracer.cpp`,
   `CudaPathTracer.cuh`, `CudaPathTracer.cu`,
   `OptixLaunchParams.h`, `OptixRenderer.{h,cpp}`,
   `OptixPrograms.cu`). Each of those would need
   signature / launch-params / kernel-arg
   modifications. PT-P.{3,6,9,12,15,18} all stayed
   within 2 source files; PT-P.21 should too.

2. **CUDA-OptiX symmetry.** The §4.7 plan's "Risk"
   note explicitly says the OptiX-side mirror is
   required so the two backends' outputs stay
   convergent at non-zero clamp. Adding the kernel
   guard to one backend without the other (or
   "CUDA only, OptiX TODO") would silently diverge
   the two `pathtrace_spp_*.ppm` outputs whenever a
   caller flipped the field on. The PT-P.x cadence
   has been careful to keep the two backends'
   contracts symmetric (PT-P.6's max_bounces clamp
   was CUDA-host-orchestration only because OptiX
   already had `OPTIX_PIPELINE_MAX_TRACE_DEPTH`;
   PT-P.9's spp clamp same; PT-P.12's env-fallback
   doc-comment was CUDA-dispatcher only because the
   OptiX dispatcher's info-log shape diverges; the
   ONE PT-P.x slice that touched the kernel was
   PT-P.15, and it was CUDA-path-tracer-only because
   the OptiX path tracer is emission-blind anyway).
   §4.7 is fundamentally cross-backend; landing it
   piecewise risks subtle divergence.

3. **NEE timing.** §4.7's whole motivation is "the
   next NEE / area-light slice will introduce
   fireflies". Until that slice exists, there's no
   visible firefly to clamp; the field is decorative
   today. Landing the placeholder now lets the future
   slice flip the flag in ONE place (a caller
   passing `cfg.firefly_clamp = 8.0f`) once both
   backends honour it.

4. **Forward compatibility.** A future slice that
   threads `firefly_clamp` through both backends
   doesn't need a `PathTraceConfig` change — the
   field is already there. The future slice's diff
   becomes "wire an existing field through the
   kernel" rather than "add a new field + wire it
   through the kernel". Cleaner separation.

If the operator decides to ship the full
firefly-clamping implementation — both backends'
kernel guards in one slice — that's a SEPARATE TASK
DEFINITION (PT-P.x for some `x > 22`) that needs its
own brief; this task's PT-P.21 stays scoped to the
field-only placeholder.

### 1.3 No new test required

The placeholder field has no behaviour to verify
beyond "the struct compiles + default-constructs to
0.0f". The existing
`tests/pathtracer_tests.cpp`'s post-condition checks
on `PathTraceConfig` already cover the
default-construction; reading the new field (which
returns `0.0f`) requires no new assertion.

A future test slice that exercises the kernel
guards (post-PT-P.21) will need a
`render(.., firefly_clamp = N)` smoke that compares
clamped vs unclamped output; that's deferred along
with the kernel guards.

---

## 2. Expected behavior

The four contractual properties the polish must
honour (matching the prompt's spec sub-bullets):

### 2.1 Add `firefly_clamp = 0.0f` to `PathTraceConfig`

A `float firefly_clamp = 0.0f` field exists at the
end of the `PathTraceConfig` struct in
`src/pathtracer/PathTracer.h`. Its name + type +
default match the §4.7 plan's specification verbatim.

### 2.2 Document as placeholder / default-off

The doc-comment block above the field names:

- The clamp's external semantics ("per-channel firefly
  clamp on the per-sample radiance").
- The default-off rationale ("clamping introduces a
  small downward bias … making it opt-in keeps the
  unbiased integrator the canonical baseline").
- The deferral ("the field is currently NOT read by
  any kernel … the PT-P.21 implementation slice ships
  ONLY this field + its doc-comment").
- The forward-compatibility plan ("when > 0, a
  future slice will wire the value through both
  backends' path-trace raygens").

### 2.3 Default output must remain unchanged

`pathtrace_spp_1.ppm`, `pathtrace_spp_16.ppm`,
`optix_pathtrace_spp1.ppm`,
`optix_pathtrace_spp16.ppm`,
`gpu_rng_test.ppm`, `gpu_accumulation_test.ppm` are
all byte-identical pre-/post-PT-P.21. The new field
is declared but not read by any caller; the kernel
arithmetic is unchanged. Verifiable by:

```
$ cmp output/pathtrace_spp_1.ppm  pre_p21_pathtrace_spp_1.ppm  ; echo $?  # 0
$ cmp output/pathtrace_spp_16.ppm pre_p21_pathtrace_spp_16.ppm ; echo $?  # 0
... etc.
```

(On a CUDA host; structurally guaranteed by §3 of
this task — PT-P.21 changes only `PathTracer.h`'s
struct, which is consumed by host code that does NOT
read the new field.)

### 2.4 Optional kernel guards may be added only if default-off and no-op when `firefly_clamp <= 0.0f`

The prompt's spec bullet 4 says "optional kernel
guards may be added". This task RECOMMENDS NOT
shipping them — see §1.2 for the four-reason
rationale. If the implementer disagrees and chooses
to ship the kernel guards anyway, the guards MUST:

- Be gated on `firefly_clamp > 0.0f` (strict
  greater-than; `<=` triggers the no-op).
- Apply the same per-channel `fminf(.., clamp)` to
  every `radiance.x|y|z` write site BEFORE
  accumulation.
- Land in BOTH the CUDA `k_pathtrace_sample` kernel
  AND the OptiX `__raygen__pathtrace` raygen in the
  SAME COMMIT (otherwise the two backends'
  byte-output diverges when a caller sets
  `firefly_clamp > 0.0f`).
- Be byte-identical with pre-PT-P.21 when
  `firefly_clamp == 0.0f` (the default; see §2.3).

If those four constraints feel onerous, PT-P.21
should stop at the field-only placeholder and a
future slice can wire the guards separately.

---

## 3. Files likely involved

The implementation slice will touch this minimal set:

| File                                     | Change                                                  |
|------------------------------------------|---------------------------------------------------------|
| `src/pathtracer/PathTracer.h`            | Add `float firefly_clamp = 0.0f` field at the end      |
|                                          | of `PathTraceConfig` + a doc-comment block. ~12-18     |
|                                          | added, 0 deleted.                                       |
| `docs/BUILD_PLAN.md`                     | Slice-closing entry following the established          |
|                                          | TEX-P.x / PT-P.x format. The entry MUST flag the       |
|                                          | "field-only placeholder; kernel guards deferred"       |
|                                          | scoping decision so a future contributor finds         |
|                                          | the rationale.                                          |

ONE source file (the `.h`); honours the PT-P.x
master rule of "max 2 source files" with room to
spare.

`src/pathtracer/PathTracer.cpp`,
`src/cuda/`, `src/optix/`, `src/main.cpp`,
`src/core/`, `src/io/`, `src/scene/`,
`src/material/`, `src/lighting/`, `src/renderer/`,
every `*.rrscene` file, every `tests/*.cpp` file,
and `tools/verify_cuda_host.py`,
`CMakeLists.txt` MUST be byte-identical post-slice.

### 3.1 If the implementer ALSO ships kernel guards (NOT recommended)

If PT-P.21 expands to wire the full kernel
guard through both backends:

| File                                     | Additional change                                       |
|------------------------------------------|---------------------------------------------------------|
| `src/pathtracer/PathTracer.cpp`          | Add lower-bound validation                             |
|                                          | (`firefly_clamp >= 0.0f` rejection); pass               |
|                                          | `cfg.firefly_clamp` to `launch_pathtrace_sample`.       |
| `src/cuda/CudaPathTracer.cuh`            | Add `float firefly_clamp` to launcher signature.       |
| `src/cuda/CudaPathTracer.cu`             | Accept the new arg; thread to the kernel; apply        |
|                                          | the clamp before the final write at line 238.           |
| `src/optix/OptixLaunchParams.h`          | Add `float firefly_clamp = 0.0f` to the launch         |
|                                          | params POD.                                             |
| `src/optix/OptixRenderer.h`              | `render_pathtrace*` signatures gain `float`            |
|                                          | `firefly_clamp` param.                                  |
| `src/optix/OptixRenderer.cpp`            | Thread the param into `OptixLaunchParams` upload.      |
| `src/optix/OptixPrograms.cu`             | `__raygen__pathtrace` reads the new field; applies     |
|                                          | the clamp before `rgb_sum +=`.                          |
| `src/main.cpp`                           | Both `--render-pathtrace` and                          |
|                                          | `--render-optix-pathtrace` dispatchers keep the         |
|                                          | default `0.0f`; no behaviour change.                    |

That's 8+ source files. PT-P.21 with this scope
would VIOLATE the max-2-source-files rule and
should be flagged in the BUILD_PLAN entry as a
deviation. The brief STRONGLY recommends the
field-only placeholder (§1.2 above).

---

## 4. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 4.1 The kernel + launcher code

- `src/cuda/CudaPathTracer.cu` — every byte. The
  per-bounce + per-sample arithmetic is preserved.
- `src/cuda/CudaPathTracer.cuh` — every byte. The
  `launch_pathtrace_sample` signature is preserved
  (the caller passes the existing 8 args; `firefly_clamp`
  is NOT a new arg in the field-only placeholder).
- `src/cuda/CudaAccumulation.cu`,
  `src/cuda/CudaAccumulation.cuh`,
  `src/cuda/CudaRngTestKernel.cu`,
  `src/cuda/CudaTestKernel.cu`,
  every other `.cu` and `.cuh` in `src/cuda/`:
  byte-identical.
- `src/optix/OptixPrograms.cu` — every byte. The
  `__raygen__pathtrace` body is unchanged.
- `src/optix/OptixRenderer.{h,cpp}`,
  `src/optix/OptixPipeline.{h,cpp}`,
  `src/optix/OptixSBT.h`,
  `src/optix/OptixDenoiser.{h,cpp}`,
  `src/optix/OptixLaunchParams.h`: byte-identical.

### 4.2 The path-tracer host orchestration

- `src/pathtracer/PathTracer.cpp` — every byte. The
  validation prelude (PT-P.6 / PT-P.9 clamps + the
  lower-bound rejections) is preserved; the spp
  loop body reads the existing `cfg` fields only.
  No `cfg.firefly_clamp` read; no new validation.
- `src/pathtracer/RNG.{h,cuh}`,
  `src/pathtracer/Sampling.{h,cuh}`: byte-identical.
- `src/renderer/AccumulationBuffer.{h,cpp}`,
  `src/renderer/AOV.{h,cpp}`,
  `src/renderer/GpuAOVBuffer.{h,cpp}`,
  `src/renderer/Hit.h`: byte-identical.

### 4.3 PathTraceConfig field set (existing fields)

The five existing fields keep their declarations,
defaults, types, and field order:

```cpp
int          max_bounces = 4;             // PT-P.6 / PT-P.9 cap honoured
int          samples_per_pixel = 16;       // PT-P.9 cap honoured
unsigned int seed = 0u;
rr::math::Vec3 environment_color     = {0.55f, 0.70f, 1.00f};
float          environment_intensity = 0.30f;
```

The `kMaxBouncesCap` (PT-P.6) and
`kSamplesPerPixelCap` (PT-P.9) constants in the same
header file remain byte-identical. The new
`firefly_clamp` field is APPENDED; no existing
declaration is reordered or deleted.

### 4.4 Path-tracer output

For every authored `PathTraceConfig`:

- `output/pathtrace_spp_1.ppm`,
  `output/pathtrace_spp_16.ppm`: byte-identical
  pixel data on a CUDA host. The new field is
  declared but not read; kernel arithmetic
  unchanged.
- `output/optix_pathtrace_spp1.ppm`,
  `output/optix_pathtrace_spp16.ppm`: byte-
  identical (the OptiX path is also unaffected).
- `output/gpu_accumulation_test.ppm`,
  `output/gpu_rng_test.ppm`: byte-identical (these
  dispatchers do not consume `firefly_clamp` even
  in a future fully-wired implementation).

### 4.5 CLI surface

- No new `--*` flag.
- No change to any dispatcher's info-log format.
- The existing `--render-pathtrace` /
  `--render-optix-pathtrace` /
  `--render-rng-test` /
  `--render-accumulation-test` argument parsers are
  byte-identical.

### 4.6 Other audits / plans

- `docs/PATH_TRACER_POLISH_PLAN.md`: optionally
  add a one-line "PT-P.21 shipped (field-only
  placeholder)" note at the top of §4.7. NOT
  required.
- The nine earlier PT-P.x task / audit docs: NO
  edits.
- The TEX-P.x arc + the CUDA-H.x arc: NO edits.
- `tools/verify_cuda_host.py`: NO changes (the
  runner exercises the existing
  `--render-pathtrace` + `--render-optix-pathtrace`
  commands; the new field is unused).
- `CMakeLists.txt`: NO changes.

---

## 5. PASS criteria

The implementation slice passes when ALL of the
following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF): clean
  build, zero new warnings.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON): clean
  build, zero new warnings.

### 5.2 Tests

- `ctest --output-on-failure` from `build`: 7/7
  PASS.
- `ctest --output-on-failure` from `build-ON`: 8/8
  PASS.
- The `pathtracer_tests` binary's internal count
  remains at 9 (the new placeholder field has no
  test; `pathtracer_tests: 20034/20034 passed`).
- Test counts unchanged from PT-P.18.

### 5.3 Source diff size

- `src/pathtracer/PathTracer.h` diff: ~12-22 added,
  0 deleted (the field declaration + a doc-comment
  block of ~10-18 lines).
- TOTAL across all source files: ≤ 25 added.
  Anything LARGER flagged in the BUILD_PLAN entry
  as a deviation.

### 5.4 No-touch invariants

`git diff` after the slice MUST show zero bytes
changed in:

- `src/cuda/`
- `src/optix/`
- `src/pathtracer/PathTracer.cpp`
- `src/pathtracer/RNG.{h,cuh}`
- `src/pathtracer/Sampling.{h,cuh}`
- `src/main.cpp`
- `src/core/`
- `src/io/`
- `src/scene/`
- `src/material/`
- `src/lighting/`
- `src/renderer/`
- every `*.rrscene` file under `scenes/`
- every `tests/*.cpp` file
- `tools/verify_cuda_host.py`
- `CMakeLists.txt`

Verifiable by:

```
git diff -- \
  src/cuda/ src/optix/ \
  src/pathtracer/PathTracer.cpp \
  src/pathtracer/RNG.h src/pathtracer/RNG.cuh \
  src/pathtracer/Sampling.h src/pathtracer/Sampling.cuh \
  src/main.cpp src/core/ src/io/ src/scene/ \
  src/material/ src/lighting/ src/renderer/ \
  scenes/ tests/ tools/verify_cuda_host.py CMakeLists.txt \
  | wc -l
=> 0
```

### 5.5 Behavioural smoke (audit host)

- `./build/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene` continues to emit
  the documented "requires CUDA" audit-host
  fallback byte-identically with the pre-PT-P.21
  baseline. The new field is unreachable on the
  audit host (the dispatcher returns from the
  `requires CUDA` branch before reaching any
  `cfg`-reading kernel call).
- `./build-ON/bin/RelativityRender
  --render-pathtrace
  scenes/test_full_scene.rrscene`: same.
- `./build/bin/RelativityRender --scene-info
  scenes/test_textured_material.rrscene`: emits the
  TEX-P.6 fixture's expected three-case log
  sequence byte-identically (one Case 1 info + two
  Case 3 warnings; `fixups applied: 2`). Confirms
  zero PT-P.21 ripple onto the texture validator.

### 5.6 Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing
  entry matching the established PT-P.x format
  (Scope / What ships / What does NOT change /
  Behaviour matrix / Master rule compliance /
  Verified at the build).
- The entry MUST include a "Field-only placeholder
  scoping note" subsection citing §1.2 + §3 of
  this task: PT-P.21 ships ONLY the
  `PathTraceConfig` field; the kernel guards in
  both backends are explicitly deferred to a
  future slice.
- The entry references
  `docs/PATH_TRACER_POLISH_PLAN.md` §4.7 + this
  task file as the source of the specification.

### 5.7 Master rule compliance

- Build incrementally (rule 1) + every step
  compilable (rule 2): both audit-host configs
  green.
- No fake stubs (rule 3): the placeholder field is
  a real `float` with a documented contract; not
  a fake.
- No CPU per-pixel work (rule 5/7): the placeholder
  introduces zero new code paths anywhere; the
  field is declared but not read.
- Update BUILD_PLAN (rule 8): the slice-closing
  entry.

---

## 6. Runtime-deferred checks

PT-P.21's field-only placeholder has no runtime
behaviour to verify on a CUDA host. The §4 / §5
no-touch invariants are byte-precise and verifiable
on the audit host alone.

The following CUDA-host checks are NOT mandatory for
PT-P.22's audit verdict to PASS, but the operator
MAY want to confirm them on a future CUDA-host
verification run:

### 6.1 Default render byte-identity (CUDA host, optional)

Render `pathtrace_spp_*.ppm` pre-/post-PT-P.21 and
confirm `cmp` reports them identical. The
field-only placeholder makes this trivially true (no
kernel arg / launch params change), but a CUDA-host
operator may want the empirical confirmation. This
is the inverse of PT-P.18's mandatory byte-DIFFERENCE
check: PT-P.21 expects byte-IDENTITY.

```
$ cmp /tmp/post_p21_spp1.ppm  /tmp/pre_p21_spp1.ppm  ; echo $?  # 0
$ cmp /tmp/post_p21_spp16.ppm /tmp/pre_p21_spp16.ppm ; echo $?  # 0
```

If `cmp` reports a difference, the slice has an
unintended kernel-side ripple and the PT-P.22
audit's verdict for §4 must flip to REPAIR.

### 6.2 ctest cycle on a CUDA host (optional)

`ctest --output-on-failure` from a CUDA-built
`build-cuda` directory exercises the same
`pathtracer_tests` binary the audit host runs. The
new placeholder field has no test, so the binary's
internal count remains 9 of 9; the ctest binary
count remains 7 / 8.

### 6.3 No CUDA-H.x runner update

`tools/verify_cuda_host.py`'s
`render-pathtrace` command exercises the existing
`--render-pathtrace` dispatcher; the placeholder
field doesn't change the dispatcher's behaviour.
The runner's report regenerates with the same
PASS / FAIL verdicts (modulo the "Tree state" hash
line per the CUDA-H.9 determinism contract).

### 6.4 PT-P.22 audit's runtime-deferred posture

PT-P.22's audit (the next slice in the cadence)
will record §6 as "no runtime checks needed; the
placeholder is fully verifiable on the audit host
via §5.4 + §5.5". This is the simplest runtime
posture in the PT-P.x cadence — PT-P.21 has the
LEAST runtime surface of any §4 polish item.

---

## 7. Out-of-scope (deferred to future PT-P.x slices)

This task is the LAST `PATH_TRACER_POLISH_PLAN.md`
§4 item. After PT-P.21 + PT-P.22 land, the §4 polish
arc closes. The kernel-guard wiring deferred here
is its own future task definition (PT-P.23 / "Wire
firefly clamp through both backends"); when that
slice opens, its task brief will:

- Add `cfg.firefly_clamp >= 0.0f` lower-bound
  validation in `PathTracer::render`.
- Thread `firefly_clamp` through
  `launch_pathtrace_sample`'s signature and the
  CUDA `k_pathtrace_sample` kernel.
- Thread `firefly_clamp` through
  `OptixLaunchParams` and the OptiX
  `__raygen__pathtrace` raygen.
- Add a CUDA-host verification step that compares
  clamped vs unclamped render output on a scene
  with intentional fireflies.
- Land BOTH backends in the SAME commit so the
  symmetric-output invariant is preserved.

That future slice would touch ~7-8 source files +
expand the no-touch enumeration. PT-P.21 keeps the
slice minimal (one source file edit) so the
placeholder-vs-implementation boundary is sharp and
auditable.

Other items NOT in scope for this task or the
follow-up:

- Direct light sampling (NEE), area lights with
  high variance — master order #16 follow-up;
  these are the consumers that justify a non-zero
  clamp. The clamp's existence as a placeholder is
  forward-compatible with whichever NEE / BSDF
  slice ships next.
- Other firefly-management techniques (Russian
  roulette, splatting, weighted MIS): out of scope
  for the §4 arc.

---

## 8. Why §4.7 is the safest viable next slice

Five reasons (mirroring the PT-P.5 / PT-P.8 /
PT-P.11 / PT-P.14 / PT-P.17 structure):

### 8.1 PT-P.19 audit verdict was clean

`docs/PATH_TRACER_POLISH_RNG_STABILITY_AUDIT.md`
§9 records overall PASS, zero REPAIR items,
DEFERRED rows carried forward to a CUDA-host run.
The path tracer is in a known-good baseline
post-PT-P.18.

### 8.2 The change is the smallest possible

ONE source file edit, ~15 lines added, zero
deleted. The smallest single-source-file PT-P.x
slice to date. PT-P.{3,12} were ~6-12 lines;
PT-P.21 is comparable.

### 8.3 No new pattern is required

Adding a `float = 0.0f` field to an existing POD
struct + writing a doc-comment is the most
trivial possible change. No new helpers, no new
kernels, no new validators, no new tests. The
implementer's diff is essentially copy-paste from
this task's §1.1.

### 8.4 The default-off contract is provably safe

The new field is declared but not read. C++'s
trivial-aggregate-construction rules guarantee
that:

- `PathTraceConfig{}` produces a struct with
  `firefly_clamp == 0.0f`.
- Every existing caller's `PathTraceConfig` value
  has `firefly_clamp == 0.0f` (because no caller
  sets it; default-construction fills it).
- Every existing caller's behaviour is byte-
  identical with pre-PT-P.21 because no code
  reads the new field.

This is a stronger guarantee than PT-P.{6,9,12,15,18}
provided — those slices either added a clamp that
fired only at extreme values OR added a kernel-side
guard whose IEEE-754 properties made it bit-
identical at the default. PT-P.21's safety is "no
code reads the new field"; trivially proven by
absence.

### 8.5 The PT-P.x arc closes cleanly

§4.7 is the LAST `PATH_TRACER_POLISH_PLAN.md` §4
item. After PT-P.21 + PT-P.22 ship, the polish arc
has shipped six of seven items (§4.{1..6} +
§4.7-placeholder; §4.7-full-kernel-wiring is its
own future arc). The cadence — task → impl → audit
× 6 — gives the reviewer six clean slices each
with a PASS verdict + a clear next-step pointer.

A future operator deciding "what do I work on
next" sees:

- The PT-P.x arc closed at PT-P.22.
- ONE deferred sub-arc: "wire firefly_clamp
  through both backends" (its task is referenced
  in PT-P.21's BUILD_PLAN entry).
- The CUDA-host verification run that flips all
  the DEFERRED rows from PT-P.4 / PT-P.7 / PT-P.10
  / PT-P.13 / PT-P.16 / PT-P.19 / PT-P.22 to
  PASS.
- The next master-order item (e.g. master #16
  feature work like NEE / non-diffuse BSDFs /
  multi-mesh upload).

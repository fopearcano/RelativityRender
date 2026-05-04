# Path Tracer Polish — Audit

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `dfaa199` ("PT-P.6: max-bounce
validation (impl)").
Scope: PT-P.1 plan + PT-P.2/PT-P.3 + PT-P.4/PT-P.5/PT-P.6 +
this audit (PT-P.7), covering
`PATH_TRACER_POLISH_PLAN.md` §4.2 and §4.3 from the seven-item
catalogue.
Mode: documentation only. **No source code is modified by this
audit.**
Auditor: Claude Code, on the audit host (no CUDA Toolkit, no
OptiX SDK).
Sources read: `docs/PATH_TRACER_POLISH_PLAN.md`,
`docs/PATH_TRACER_POLISH_STEP_1_TASK.md`,
`docs/PATH_TRACER_POLISH_STEP_1_AUDIT.md`,
`docs/PATH_TRACER_POLISH_STEP_2_TASK.md`,
`src/pathtracer/PathTracer.{h,cpp}`,
`src/renderer/AccumulationBuffer.{h,cpp}`,
`tests/renderer_tests.cpp`,
`docs/BUILD_PLAN.md`'s PT-P.{1..6} entries.
Recent commits inspected: `bb12904..dfaa199` on
`relativity-core-v1`.

This audit walks the seven prompt checks in order and records a
single verdict at the end. Verdict legend matches the
texture-polish-audit + step-1-audit precedent:

- **PASS** — implemented, type-checked on the audit host, AND
  empirically exercisable on the audit host with a recorded
  happy-path run.
- **REPAIR** — implemented but a defect or inconsistency was
  found that should be patched.
- **BLOCKED** — empirical verification requires a CUDA host
  (no nvcc / OptiX SDK on the audit host). Used here as the
  path-tracer-polish equivalent of the texture audit's
  `DEFERRED`.

---

## 1. Which polish items were implemented

The PT-P.x arc shipped two of the seven items in
`PATH_TRACER_POLISH_PLAN.md` §4. The plan's recommended
sequencing was followed exactly.

| Plan §  | Title                              | Slice  | Commit     | Status   |
|---------|------------------------------------|--------|------------|----------|
| §4.2    | Accumulation reset correctness     | PT-P.3 | `b4d73eb`  | shipped  |
| §4.3    | Max-bounce validation              | PT-P.6 | `dfaa199`  | shipped  |
| §4.1    | RNG stability                      | —      | —          | DEFERRED |
| §4.4    | Environment fallback clarity       | —      | —          | DEFERRED |
| §4.5    | Emission handling                  | —      | —          | DEFERRED |
| §4.6    | Sample count validation            | —      | —          | DEFERRED |
| §4.7    | Firefly clamp placeholder          | —      | —          | DEFERRED |

### 1.1 §4.2 / PT-P.3 (accumulation reset correctness)

- **Source change.**
  `src/renderer/AccumulationBuffer.cpp` (+20 lines):
    - **§1.1** doc-only ordering note above the existing
      `samples_ = 0;` assignment in `reset()`.
    - **§1.2** no-op fast path in `resize(w, h)`. When called
      with `w == width_ && h == height_ && device_.size()
      == float_count`, sets `samples_ = 0` and forwards to
      `reset()`, skipping the `device_.allocate` reallocate.
- **Test change.**
  `tests/renderer_tests.cpp` (NEW, +104 lines): three
  host-only test cases linked PRIVATE against `rr_renderer`
  (`test_default_state`,
  `test_resize_zero_dimensions_returns_to_default`,
  `test_resize_same_dimensions_twice_keeps_zero_samples`).
- **Build-system change.**
  `CMakeLists.txt` (+15 lines) registered the
  `renderer_tests` ctest binary.
- **Audit.**
  `docs/PATH_TRACER_POLISH_STEP_1_AUDIT.md` (PT-P.4)
  recorded overall PASS, zero REPAIR items, one BLOCKED
  row carried forward to a CUDA-host run.

### 1.2 §4.3 / PT-P.6 (max-bounce validation)

- **Header change.**
  `src/pathtracer/PathTracer.h` (+11 lines): new
  `inline constexpr int kMaxBouncesCap = 32` above
  `PathTraceConfig`. Doc-comment notes the cap is a
  SUGGESTION (not a hard ABI limit) + references OptiX's
  analogous `OPTIX_PIPELINE_MAX_TRACE_DEPTH` constraint.
- **Implementation change.**
  `src/pathtracer/PathTracer.cpp` (+20 added, -1 deleted):
    - `#include "core/Logger.h"`.
    - PT-P.6 warn-and-clamp branch in `PathTracer::render`
      between the existing `cfg.max_bounces < 0` check and
      the `cfg.environment_intensity < 0.0f` check.
      Pattern mirrors `validate_material_texture_ids`
      (TEX-P.2 / TEX-P.5).
    - Use-site replacement of `cfg.max_bounces` with
      `effective_max_bounces` in the
      `cuda::launch_pathtrace_sample` arg.
- **No new test.** The §4.3 plan entry called this out:
  "verifiable by code inspection". A CUDA-host slice
  could later thread an end-to-end test through
  `tools/verify_cuda_host.py` if desired.

### 1.3 Documentation artefacts

| File                                          | Slice  | Purpose |
|-----------------------------------------------|--------|---------|
| `docs/PATH_TRACER_POLISH_PLAN.md`             | PT-P.1 | Seven-item polish catalogue + recommended first item (§4.2). |
| `docs/PATH_TRACER_POLISH_STEP_1_TASK.md`      | PT-P.2 | Brief for §4.2 implementation slice. |
| `docs/PATH_TRACER_POLISH_STEP_1_AUDIT.md`     | PT-P.4 | Step-1 verification (PASS, one BLOCKED row). |
| `docs/PATH_TRACER_POLISH_STEP_2_TASK.md`      | PT-P.5 | Brief for §4.3 implementation slice. |
| `docs/PATH_TRACER_POLISH_AUDIT.md` (this file)| PT-P.7 | Arc-end verification. |
| `docs/BUILD_PLAN.md` PT-P.{1..6} entries      | each   | Slice-closing entries with behaviour matrices. |

### 1.4 Total source delta

`git diff bb12904~1..dfaa199 --stat` (the entire PT-P.x arc):

```
 CMakeLists.txt                          |   15 +
 docs/BUILD_PLAN.md                      | 1978 +++++++++++++++++++++++
 docs/PATH_TRACER_POLISH_PLAN.md         |  644 ++++++++++
 docs/PATH_TRACER_POLISH_STEP_1_AUDIT.md |  395 ++++++
 docs/PATH_TRACER_POLISH_STEP_1_TASK.md  |  416 +++++++
 docs/PATH_TRACER_POLISH_STEP_2_TASK.md  |  362 ++++++
 src/pathtracer/PathTracer.cpp           |   21 +-
 src/pathtracer/PathTracer.h             |   11 +
 src/renderer/AccumulationBuffer.cpp     |   20 +
 tests/renderer_tests.cpp                |  104 ++
 10 files changed, 3965 insertions(+), 1 deletion(-)
```

Source-only (excluding docs + CMakeLists): 156 added / 1
deleted across 4 files. Two of those four are tests +
documentation (`tests/renderer_tests.cpp` and
`CMakeLists.txt`'s test-target block); the actual
production-code delta is **3 files, 52 added lines, 1 deleted
line**.

---

## 2. Build status

**PASS.**

Re-ran during this audit:

| Config      | RR_ENABLE_CUDA | RR_ENABLE_OPTIX | Build     | ctest    |
|-------------|:--------------:|:---------------:|-----------|:--------:|
| `build`     | OFF            | OFF             | clean     | 7/7 PASS |
| `build-ON`  | OFF            | ON              | clean     | 8/8 PASS |

ctest count grew from 6/6 OFF (pre-arc) to 7/7 OFF after
PT-P.3's new `renderer_tests` binary; PT-P.6 added no new
test (the §4.3 polish is verifiable by code inspection per
the plan). Both audit-host configs report zero new compiler
warnings under the `rr_apply_warnings`-enforced
`-Wall -Wextra -Wpedantic` triple.

The new test binary (`renderer_tests`) reports
`renderer_tests: 6 / 6 passed` internally
(three test cases × two `RR_CHECK` invocations per case
on average).

---

## 3. CUDA path status

**STRUCTURALLY PASS; EMPIRICALLY BLOCKED.**

### 3.1 Hot-path bytes unchanged

`git diff bb12904~1..dfaa199 -- src/cuda/` returns zero
output. Every CUDA source file (`CudaPathTracer.cu`,
`CudaAccumulation.cu`, `CudaTestKernel.cu`,
`CudaRngTestKernel.cu`, `CudaPathTracer.cuh`,
`CudaAccumulation.cuh`, the rest of `src/cuda/`) is
byte-identical with the pre-PT-P.1 commit.

The same holds for the per-pixel host orchestration:

```
git diff bb12904~1..dfaa199 -- src/main.cpp \
  src/cuda/ src/optix/ src/renderer/AccumulationBuffer.h
=> 0 bytes
```

### 3.2 Effective host-side semantics on a CUDA host

- **PT-P.3 §1.2 fast path** (`AccumulationBuffer::resize`):
  on a CUDA host that re-renders the same scene, the
  fast path skips a `cudaFree + cudaMalloc + cudaMemset`
  round trip and forwards to `reset()` (`launch_accum_clear`).
  The accumulator's value across the spp loop is bit-
  identical with the slow path's end state. PPM output
  byte-identical.
- **PT-P.6 max-bounces clamp**: only fires for callers
  passing `max_bounces > 32`. No dispatcher, scene file,
  or CLI surface today produces such a value (every
  caller uses the default `4`); every existing CUDA-host
  render is therefore byte-identical. The clamp's
  behaviour is per-call host-side — the kernel never sees
  a different bounce budget than what
  `effective_max_bounces` carries.

### 3.3 Empirical verification

Audit-host smoke (recorded during this audit):

```
$ ./build-ON/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
[ERROR] --render-pathtrace requires CUDA. Rebuild with
        -DRR_ENABLE_CUDA=ON on a host with the CUDA Toolkit
        and a CUDA-capable GPU.
```

Documented audit-host fallback fires byte-identically with
the pre-PT-P.1 baseline. The default `max_bounces=4` is
well within the cap; no PT-P.6 warn line appears.

The CUDA-host equivalent — `pathtrace_spp_*.ppm` byte-
identical pre/post-arc — is structurally guaranteed by
§3.1 (zero bytes changed in the per-pixel code path) but
cannot be empirically verified here. See §5.

### 3.4 No regression in other CUDA dispatchers

`--render-rng-test`, `--render-accumulation-test`,
`--render-aovs`, `--render`, `--render-textured-material`,
`--render-texture-sample-test`, `--render-camera-rays`,
`--render-sphere`, `--render-relativistic`, `--render-gradient`:
all dispatched through the same orchestration (parse ->
upload -> launch -> save) used by the path tracer; PT-P.x
does not edit any of them. The CUDA-H.x runner's
`base_commands()` catalogue is untouched (verified by
`git diff bb12904~1..dfaa199 -- tools/verify_cuda_host.py
| wc -l => 0`).

---

## 4. OptiX path status

**STRUCTURALLY PASS; EMPIRICALLY BLOCKED.**

### 4.1 OptiX hot-path bytes unchanged

`git diff bb12904~1..dfaa199 -- src/optix/` returns zero
output. `src/optix/OptixPrograms.cu`,
`src/optix/OptixRenderer.{h,cpp}`,
`src/optix/OptixPipeline.{h,cpp}`,
`src/optix/OptixDenoiser.{h,cpp}`,
`src/optix/OptixLaunchParams.h`, the OptiX-side
`render_pathtrace*` paths, and the OptiX-side accumulation
primitives — all byte-identical with the pre-PT-P.1 commit.

### 4.2 PT-P.6 cap is host-side only

The §4.3 polish lives in `PathTracer::render`, which is
the CUDA-path orchestration class. The OptiX-side
`OptixRenderer::render_pathtrace` reads its own bounce
budget directly from the dispatcher
(`run_render_optix_pathtrace` in `src/main.cpp`); the
PT-P.6 host clamp does NOT reach the OptiX raygen.

This is consistent with the PT-P.5 task §3.5: OptiX
already enforces `OPTIX_PIPELINE_MAX_TRACE_DEPTH` at
pipeline-build time. The host-side PT-P.6 clamp is
analogous defence in depth on the CUDA path; the OptiX
path has its own analogue.

### 4.3 PT-P.3 fast path benefits the OptiX path too

`AccumulationBuffer` is shared between the CUDA and
OptiX path tracers. The §1.2 no-op fast path therefore
benefits the OptiX-side
`OptixRenderer::render_pathtrace_progressive` re-render
case (no observable PPM change; cycle savings only).

### 4.4 Empirical verification

`./build-ON/bin/RelativityRender --render-optix-pathtrace
scenes/test_full_scene.rrscene` continues to hit the
documented "OptiX SDK required" audit-host fallback
(SDK not present). PPM verification is BLOCKED on a
CUDA + OptiX-SDK host run.

---

## 5. Runtime-deferred status

**BLOCKED on the same six artefacts the
`PATH_TRACER_POLISH_PLAN.md` §2 + the Stage 11 audit's
§§ 2-5 already enumerate.** PT-P.x does NOT alter the
per-pixel computation graph (§3.1 + §4.1 confirmed), so
the empirical verification surface is unchanged.

| Artefact                                | CUDA-host expectation                       |
|-----------------------------------------|---------------------------------------------|
| `output/gpu_rng_test.ppm`               | byte-identical with pre-PT-P.1              |
| `output/gpu_accumulation_test.ppm`      | byte-identical                              |
| `output/pathtrace_spp_1.ppm`            | byte-identical                              |
| `output/pathtrace_spp_16.ppm`           | byte-identical                              |
| `output/optix_pathtrace_spp1.ppm`       | byte-identical                              |
| `output/optix_pathtrace_spp16.ppm`      | byte-identical                              |

The byte-identical claim is structurally guaranteed by:

- §3.1 / §4.1's zero-bytes-changed finding in the
  per-pixel code path.
- The §1.2 fast path's external contract (the no-op
  resize fast path collapses to the same
  `launch_accum_clear` `cudaMemset` the slow path issues
  at the end of allocate / memset).
- The PT-P.6 clamp's no-fire-on-default-config behaviour
  (every dispatcher today uses `max_bounces = 4`, well
  within the `kMaxBouncesCap = 32` cap).

### 5.1 Additional CUDA-host operator checks

Two small follow-ups the operator may want to verify on
a CUDA host once one is available:

- **PT-P.3 fast-path timing.** Run
  `--render-pathtrace scenes/test_full_scene.rrscene`
  twice in a single warm process. The §1.2 fast path
  predicts the second invocation skips a
  `cudaFree + cudaMalloc + cudaMemset` round trip on the
  `AccumulationBuffer::resize` call. Speedup is small
  (saved cycles amortised across the spp loop's many
  launches) but measurable.
- **PT-P.6 warn-line emission.** Construct a
  `PathTraceConfig` with `max_bounces = 100` (e.g. via
  a future `--bounces` CLI flag, or a one-off harness
  in a test) and confirm the Logger emits exactly one
  line:
  `PathTraceConfig::max_bounces=100 exceeds the
  recommended cap of 32; clamping. ...`. The CUDA
  kernel then runs with effectively 32 bounces.

### 5.2 Runner integration status

`tools/verify_cuda_host.py` does NOT need an update for
the PT-P.x arc — the runner exercises the existing
`--render-pathtrace` + `--render-optix-pathtrace`
commands; the new `renderer_tests` binary is picked up
by the existing `cmake_build_command` -> ctest step
automatically when a CUDA host invokes the runner.
Verified by `git diff bb12904~1..dfaa199 --
tools/verify_cuda_host.py | wc -l => 0`.

---

## 6. CPU path-tracing violations

**ZERO violations** — verified by re-running the
Stage-11-audit grep sweeps on the post-PT-P.6 tree.

### 6.1 Per-pixel for-loops on the host

```
$ grep -rnE "for.*<.*width|for.*<.*height" \
    src/renderer/ src/pathtracer/*.cpp src/main.cpp
=> (no matches)
```

Stage 11's identical sweep returned zero matches; the
PT-P.x arc did not introduce any.

### 6.2 spp launcher loops

```
$ grep -rn "for.*samples_per_pixel" \
    src/pathtracer/*.cpp src/main.cpp
src/pathtracer/PathTracer.cpp:97:
    for (int s = 0; s < cfg.samples_per_pixel; ++s) {
```

One match at line 97 (the line moved from 78 because of
the PT-P.6 warn-and-clamp insertion above it). This is
the **spp launcher loop** (one kernel launch per
sample), not a per-pixel loop. PT-P.x did not introduce
any new per-sample host iteration; the launcher loop is
the same one the Stage 11 audit's §6 / §8 already
classified as the only host iteration in the path
tracer + at sample-frame granularity.

### 6.3 Host-side intersection / closest-hit code

```
$ grep -rn "intersect_sphere|intersect_triangle|closest_hit" \
    src/renderer/ src/pathtracer/*.cpp
src/renderer/Hit.h:30:
    // `intersect_triangle`. The third coord is `1 - bary_u - bary_v`.
```

One match — a doc-comment in `renderer/Hit.h`. No
host-side intersection code exists. Identical to the
Stage 11 audit + the PT-P.4 step-1-audit findings.

### 6.4 PT-P.x-specific scope

PT-P.x edited four files in `src/`:

| File                                       | New per-pixel code? |
|--------------------------------------------|---------------------|
| `src/renderer/AccumulationBuffer.cpp`      | NO. Only host-side `resize()` / `reset()` state-machine edits. |
| `src/pathtracer/PathTracer.h`              | NO. New `inline constexpr int` + doc-comment. |
| `src/pathtracer/PathTracer.cpp`            | NO. Validation-prelude addition + one use-site replacement. |
| `tests/renderer_tests.cpp` (NEW)           | NO. Public-API post-condition checks. |

None of the four files contains:
- a per-pixel `for` loop,
- a call to any `intersect_*` / `closest_hit` /
  `sample_*hemisphere*` / `next_float` / `next_vec2`
  primitive,
- any code that reads or writes a per-pixel value.

The PT-P.x arc operates at host-side state-machine
granularity (`AccumulationBuffer` sees the device
pointer + the pixel count + the float-stride; it never
iterates pixels itself) and at config-validation
granularity (`PathTracer::render` reads `cfg` fields
once per render).

Master rule 5/7 ("All per-pixel/per-ray rendering must
happen on GPU") therefore remains upheld post-PT-P.6.

---

## 7. Recommended next safe stage

The PT-P.x arc landed two of seven plan items
(`PATH_TRACER_POLISH_PLAN.md` §4.2 + §4.3). Five remain.
Per the PT-P.5 task's §5 sequencing guidance + the
`PATH_TRACER_POLISH_PLAN.md` §5 recommendation, the
preferred order from here is:

1. **§4.6 — Sample count validation (soft cap).** Same
   warn-and-clamp shape PT-P.6 just established; trivially
   adapts the clamp pattern from `max_bounces` to
   `samples_per_pixel`. Same single-file edit
   (`src/pathtracer/PathTracer.cpp`); same safety
   profile (host-side only; default 16 is well within
   any sensible cap). Estimated diff: ~10 source lines.
2. **§4.4 — Environment fallback clarity.** Doc-comment
   + dispatcher info-log addition. ~5 lines. Smallest
   remaining item; pure documentation polish.
3. **§4.5 — Emission handling.**
   `is_emissive(MaterialParams)` inline helper short-
   circuits the per-hit emission add. Touches the CUDA
   path-tracer kernel + `MaterialTypes.h`. Slightly
   larger surface than the others (kernel + header);
   sequence after §4.6 + §4.4 land.
4. **§4.1 — RNG stability.** Changes every
   `pathtrace_spp_*.ppm` byte-exactly. Sequence after
   the smaller items so a single CUDA-host run can
   pixel-diff exactly the RNG-stability change.
5. **§4.7 — Firefly clamp placeholder.** Adds a default-
   off field to `PathTraceConfig` + matching kernel
   guards on BOTH CUDA and OptiX path-trace raygens.
   Largest surface in the §4 catalogue; sequence last
   so the kernel guards are added against a stable RNG.

### Alternative paths

The operator may prefer either:

- **Trigger the CUDA-host verification run** that flips
  the §5 BLOCKED rows of this audit (and the §5 BLOCKED
  rows of the PT-P.4 step-1 audit) to PASS. Single
  command-line invocation
  (`tools/verify_cuda_host.py [--optix]`) on a real
  CUDA + (optional) OptiX-SDK host. Per
  `docs/CUDA_HOST_VERIFICATION_AUDIT.md` §3, the result
  byte-replaces the currently-committed audit-host
  REPAIR report.
- **Pivot to a different polish arc.** Master order
  items #16 (path tracing — feature work like NEE / non-
  diffuse BSDFs / multi-mesh upload) and #18 (texture
  filtering — MIP / anisotropic) are the next major
  follow-ups after the polish arcs close. Each is its
  own multi-slice arc and OUT OF SCOPE for the PT-P.x
  cadence.

The PT-P.x cadence (PT-P.{N+1} task definition followed
by PT-P.{N+2} implementation followed by PT-P.{N+3}
audit) is the established slice rhythm; opening a new
PT-P.8 slice for §4.6 is the lowest-friction
continuation.

---

## 8. Verdict summary

| # | Audit item                                          | Result    |
|---|-----------------------------------------------------|-----------|
| 1 | Polish items implemented (§4.2 + §4.3 of seven)     | (see §1)  |
| 2 | Build status (both audit-host configs)              | PASS      |
| 3 | CUDA path status                                    | PASS structurally; BLOCKED empirically |
| 4 | OptiX path status                                   | PASS structurally; BLOCKED empirically |
| 5 | Runtime-deferred status (six PPMs)                  | BLOCKED   |
| 6 | CPU path-tracing violations                         | PASS — zero violations |
| 7 | Recommended next safe stage                         | (see §7)  |

**Overall verdict: PASS.**

The PT-P.x arc shipped two clean implementation slices
(PT-P.3 + PT-P.6), each with its own task definition and
its own audit. Both audit-host build configs remain green
(7/7 OFF, 8/8 ON-audit-host). The per-pixel code path is
byte-identical pre/post-arc, so every existing CUDA-host
PPM is structurally guaranteed to be byte-identical (the
single empirical CUDA-host-deferred row carried forward
from PT-P.4 still applies). Master rule 5/7 (no CPU ray
tracing) remains enforced; the PT-P.x arc added zero
violations.

**Zero REPAIR items** were found across the entire arc
(PT-P.4 verified PT-P.3; this audit verified PT-P.6 +
the arc as a whole).

The polish arc is **suspended** (not closed): five of
seven plan items remain available, sequenced as in §7.
The next concrete slice — when the operator chooses to
continue — is PT-P.8 task definition for
`PATH_TRACER_POLISH_PLAN.md` §4.6 (sample count
validation), mirroring the PT-P.{2,5} task-definition
cadence.

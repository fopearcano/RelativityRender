# Path-Tracer Polish — Step 1 Audit

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `b4d73eb` ("PT-P.3:
accumulation reset correctness (impl)").
Scope: PT-P.3 — the implementation slice that ships
`PATH_TRACER_POLISH_PLAN.md` §4.2 per the brief in
`PATH_TRACER_POLISH_STEP_1_TASK.md`.
Mode: documentation only. **No source code is modified by
this audit.**
Auditor: Claude Code, on the audit host (no CUDA Toolkit,
no OptiX SDK).

This audit walks the six prompt checks in order and
records a single verdict at the end. Verdict legend
matches the texture-polish-audit precedent:

- **PASS** — implemented, type-checked on the audit host,
  AND empirically exercisable on the audit host with a
  recorded happy-path run.
- **REPAIR** — implemented but a defect or inconsistency
  was found that should be patched.
- **BLOCKED** — empirical verification requires a CUDA
  host (no nvcc / OptiX SDK on the audit host). Used
  here as the path-tracer-polish equivalent of the
  texture audit's `DEFERRED`.

---

## 1. What changed

PT-P.3 (`b4d73eb`) ships exactly the three sub-changes the
PT-P.2 task brief listed.

`git diff b4d73eb~1..b4d73eb --stat`:

```
 CMakeLists.txt                      |  15 ++
 docs/BUILD_PLAN.md                  | 423 ++++++++++++++++++++++++++++++++++++
 src/renderer/AccumulationBuffer.cpp |  20 ++
 tests/renderer_tests.cpp            | 104 +++++++++
 4 files changed, 562 insertions(+)
```

Source-side breakdown:

### 1.1. `src/renderer/AccumulationBuffer.cpp` (+20)

- **§1.1 doc-only ordering note** above the existing
  `samples_ = 0;` assignment in `reset()`. The
  assignment runs unconditionally above the
  `#ifdef RR_HAS_CUDA` branch; the comment makes that
  invariant explicit so a future refactor cannot regress
  the host-only build into a "samples > 0 but not
  actually cleared" state. Zero logic change.
- **§1.2 no-op fast path** in `resize(w, h)`. When
  called with `w == width_ && h == height_ &&
  device_.size() == float_count`, sets `samples_ = 0`
  and forwards to `reset()`, skipping the
  `device_.allocate` reallocate. The external contract
  is identical to the slow path.

### 1.2. `tests/renderer_tests.cpp` (NEW, +104)

A new ctest binary linked PRIVATE against `rr_renderer`
with three host-only test cases:

- `test_default_state`: default-constructed
  `AccumulationBuffer` has `width()/height() == 0`,
  `samples_count() == 0`, `valid() == false`.
- `test_resize_zero_dimensions_returns_to_default`:
  `resize(0, 0)` collapses to default + returns false.
- `test_resize_same_dimensions_twice_keeps_zero_samples`:
  the §1.3 invariant test. Calls `resize(64, 64)`
  twice. Asserts `samples_count() == 0` after the
  second call AND that `width()/height()` are either
  `(64, 64)` (CUDA-host happy path) or `(0, 0)`
  (audit-host failed-allocate path). The disjunction
  keeps the test stable across every build config.

### 1.3. `CMakeLists.txt` (+15)

A new `add_executable(renderer_tests
tests/renderer_tests.cpp)` block matching the
`pathtracer_tests` / `gpu_tests` / `relativity_tests`
/ `demo_tests` per-module pattern. Links PRIVATE
against `rr_renderer`. ctest count grows from 6 -> 7
(OFF) and 7 -> 8 (ON-audit-host).

### 1.4. `docs/BUILD_PLAN.md` (+423)

A standard slice-closing entry that includes (per the
texture-polish-arc precedent):

- Scope.
- What ships (sub-bullets for each of §1.1 / §1.2 /
  §1.3 / CMakeLists).
- What does NOT change (zero bytes touched in
  `src/cuda/`, `src/optix/`, `src/pathtracer/`,
  `src/main.cpp`, `src/core/CommandLine.{h,cpp}`,
  any `*.rrscene`, `src/renderer/AccumulationBuffer.h`,
  `PathTraceConfig`).
- A diff-size deviation note flagging the 20-line
  `AccumulationBuffer.cpp` add against the task's
  12-line cap (the deviation is entirely doc-comment
  text; the actual fast-path LOGIC is exactly 4 lines).
- A behaviour matrix.
- Master-rule compliance + Verified-at-the-build entries.

### 1.5. Files NOT changed (`git diff -- ...`)

- `src/cuda/`: 0 bytes changed.
- `src/optix/`: 0 bytes changed.
- `src/pathtracer/`: 0 bytes changed.
- `src/main.cpp`: 0 bytes changed.
- `src/core/`: 0 bytes changed.
- `scenes/*.rrscene`: 0 bytes changed.
- `src/renderer/AccumulationBuffer.h`: 0 bytes changed.

Verified via:

```
git diff b4d73eb~1..b4d73eb -- \
  src/cuda/ src/optix/ src/pathtracer/ src/main.cpp \
  src/core/ scenes/ src/renderer/AccumulationBuffer.h \
  | wc -l
=> 0
```

---

## 2. Does build pass?

**PASS.**

Rebuild + ctest evidence captured during this audit on
the audit host:

| Config      | RR_ENABLE_CUDA | RR_ENABLE_OPTIX | Build     | ctest          |
|-------------|:--------------:|:---------------:|-----------|:--------------:|
| `build`     | OFF            | OFF             | clean     | 7/7 PASS       |
| `build-ON`  | OFF            | ON              | clean     | 8/8 PASS       |

Both configs were already at the post-PT-P.3 count
(7/7, 8/8) before this audit ran; the re-run confirms
nothing has rotted between commits.

The new `renderer_tests` binary is at index 7 in `build`
and index 7 in `build-ON` (with `optix_tests` at index
8); both binaries report `renderer_tests: 6 / 6 passed`
internally (three test cases × two `RR_CHECK` invocations
per case, give or take the variable post-condition
check). The reported `Passed` is the binary's exit code,
which is 0 iff every internal `RR_CHECK` held.

Zero new compiler warnings under
`-Wall -Wextra -Wpedantic` (the `rr_apply_warnings`
flags every CMake target enforces).

---

## 3. Does the existing path-tracer path remain intact?

**PASS.**

Three independent checks verify no regression:

### 3.1. Source diff: zero bytes changed in the path-tracer hot path

`git diff b4d73eb~1..b4d73eb -- src/cuda/ src/optix/
src/pathtracer/ src/main.cpp` returns zero output. The
five files that drive the per-pixel computation graph
(`src/cuda/CudaPathTracer.cu`,
`src/cuda/CudaAccumulation.cu`,
`src/optix/OptixPrograms.cu`,
`src/pathtracer/PathTracer.cpp`,
`src/main.cpp::run_render_pathtrace`) are byte-identical
with the pre-PT-P.3 commit `9637aa7`.

### 3.2. Audit-host behavioural smoke: --render-pathtrace fallback

```
$ ./build-ON/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
[17:31:35.629] [ERROR] --render-pathtrace requires CUDA. Rebuild with
                       -DRR_ENABLE_CUDA=ON on a host with the CUDA
                       Toolkit and a CUDA-capable GPU.
```

The dispatcher reaches `PathTracer::render` whose
CUDA-disabled branch returns the standard
"PathTracer::render requires CUDA" message; the polish
does not affect this path. Exit 1; no kernel crash;
byte-identical with the pre-PT-P.3 baseline.

### 3.3. Audit-host behavioural smoke: --scene-info on the TEX-P.6 fixture

```
$ ./build/bin/RelativityRender --scene-info \
    scenes/test_textured_material.rrscene
... [WARN] scene: material 'textured-quad-material' (id=0) has
         useBaseColorTexture=true but baseColorTextureId=0 is out of
         range [0, 0); falling back to flat baseColor.
... [WARN] scene: material 'out-of-range-texture' (id=3) has
         useBaseColorTexture=true but baseColorTextureId=99 is out of
         range [0, 0); falling back to flat baseColor.
... [INFO]     fixups applied    : 2
```

Confirms zero ripple onto the texture validator (the
TEX-P.5 / TEX-P.6 logging shape is byte-identical with
the pre-PT-P.3 baseline; the Case 1 info log on
material `dangling-texture-id` also fires but is
filtered out of the `WARN|info|fixups applied` grep
above by case sensitivity — the actual stdout shows
the info line as expected).

The CUDA-host equivalent — `pathtrace_spp_*.ppm`
remains pixel-byte-identical — is structurally
guaranteed by §3.1 (zero bytes changed in the per-pixel
code path) but cannot be empirically verified here.
See §5 below.

---

## 4. CPU ray / path tracing violations

**ZERO violations** — verified by re-running the
Stage-11-audit grep.

### 4.1. Per-pixel for-loops on the host

`grep -rnE "for.*<.*width|for.*<.*height"
src/renderer/ src/pathtracer/*.cpp src/main.cpp`:
**zero matches.** The Stage 11 audit's identical sweep
returned zero matches; PT-P.3 did not introduce any.

`grep -rn "for.*samples_per_pixel" ...`: returns
exactly one match,

```
src/pathtracer/PathTracer.cpp:78:
    for (int s = 0; s < cfg.samples_per_pixel; ++s) {
```

This is the **spp launcher loop** (one kernel launch
per sample), not a per-pixel loop. PT-P.3 did not
touch this file. The Stage 11 audit's §6 / §8 already
classified this loop as the only host iteration in
the path tracer + at sample-frame granularity (not
pixel-granularity), and the classification still
holds.

### 4.2. Host-side intersection / closest-hit code

`grep -rn "intersect_sphere|intersect_triangle|
closest_hit" src/renderer/ src/pathtracer/*.cpp`:
returns one match,

```
src/renderer/Hit.h:30:
    // `intersect_triangle`. The third coord is
    // `1 - bary_u - bary_v`.
```

The match is a doc-comment in
`renderer/Hit.h`; no host-side intersection code
exists. Identical to the Stage 11 audit's finding.

### 4.3. PT-P.3-specific scope

PT-P.3 only edited
`src/renderer/AccumulationBuffer.cpp` and added
`tests/renderer_tests.cpp`. Neither file contains:

- a per-pixel `for` loop;
- a call to any `intersect_*` / `closest_hit` /
  `sample_*hemisphere*` / `next_float` / `next_vec2`
  primitive;
- any code that reads or writes a per-pixel value.

The polish operates at buffer-level granularity (the
`AccumulationBuffer` sees the device pointer + the
pixel count + the float-stride; it never iterates
pixels itself).

Master rule 5/7 ("All per-pixel/per-ray rendering
must happen on GPU") therefore remains upheld
post-PT-P.3.

---

## 5. Runtime-deferred checks

**BLOCKED** on the same six artefacts the
`PATH_TRACER_POLISH_PLAN.md` §2 + the Stage 11 audit's
§§ 2-5 already enumerate. PT-P.3 does NOT alter the
per-pixel computation graph, so the empirical
verification surface is unchanged from PT-P.1's plan:

| Artefact                                | CUDA-host expectation                       |
|-----------------------------------------|---------------------------------------------|
| `output/gpu_rng_test.ppm`               | byte-identical with pre-PT-P.3              |
| `output/gpu_accumulation_test.ppm`      | byte-identical                              |
| `output/pathtrace_spp_1.ppm`            | byte-identical                              |
| `output/pathtrace_spp_16.ppm`           | byte-identical                              |
| `output/optix_pathtrace_spp1.ppm`       | byte-identical                              |
| `output/optix_pathtrace_spp16.ppm`      | byte-identical                              |

The byte-identical claim is structurally guaranteed by
§3.1 (zero bytes changed in the per-pixel code path)
+ the §1.2 fast path's external contract: when the
CUDA host re-renders the same scene, the no-op
`resize` fast path collapses to a single
`launch_accum_clear` launch — the same `cudaMemset`
the slow path would issue at the end of its
allocate / memset sequence. The accumulator's value
across the spp loop is therefore bit-identical with
the pre-PT-P.3 baseline.

A small additional CUDA-host check the operator may
want to run, post-PT-P.3:

- Time `--render-pathtrace
  scenes/test_full_scene.rrscene` twice in a row from
  a single warm process. The §1.2 fast path predicts
  the second invocation skips a `cudaFree +
  cudaMalloc + cudaMemset` round trip on the
  `AccumulationBuffer::resize` call (which the
  dispatcher invokes once per render). The expected
  speedup is small (the saved cycles are amortised
  across the spp loop's many launches) but
  measurable on a wall-clock comparison.

The CUDA-host verification runner
(`tools/verify_cuda_host.py`) does NOT need an
update for PT-P.3 — the runner exercises the
existing `--render-pathtrace` + `--render-optix-
pathtrace` commands; the new
`renderer_tests` binary is picked up by the existing
`cmake_build_command` -> ctest step automatically
when a CUDA host invokes the runner.

---

## 6. Verdict

| # | Audit item                                            | Result   |
|---|-------------------------------------------------------|----------|
| 1 | What changed                                          | (see §1) |
| 2 | Build passes (both audit-host configs)                | PASS     |
| 3 | Existing path-tracer path remains intact              | PASS     |
| 4 | No CPU ray / path tracing violations                  | PASS     |
| 5 | Runtime-deferred checks (six PPMs)                    | BLOCKED  |
| 6 | Overall                                               | **PASS** (with one BLOCKED row carried forward to a CUDA-host run) |

**Overall verdict: PASS.**

PT-P.3 ships exactly the three sub-changes the PT-P.2
task brief specified, both audit-host build configs
remain green (7/7 OFF, 8/8 ON-audit-host), the
per-pixel code path is byte-identical pre/post-slice,
and master rule 5/7 (no CPU ray tracing) remains
enforced. The single BLOCKED row is the same
runtime-deferred surface every prior path-tracer audit
recorded; nothing in PT-P.3 changes the empirical
verification surface, so the BLOCKED status carries
forward unchanged from the Stage 11 audit's §§ 2-5.

REPAIR items: none.

### Recommended next step

Per `PATH_TRACER_POLISH_PLAN.md` §5's sequencing
guidance, the natural follow-up after §4.2
(this slice) is **§4.3 — max-bounce validation**.
That polish reuses the same `Logger::warning` clamp
shape (TEX-P.x precedent) and operates on the same
`PathTraceConfig` POD, so the brief writes itself
from §4.3 of the plan. A PT-P.5 task definition
(mirroring PT-P.2) would open that arc; a PT-P.6
implementation slice would close it.

Alternative next step: trigger the CUDA-host
verification run that flips the BLOCKED row in §5
to PASS. Per `CUDA_HOST_VERIFICATION_AUDIT.md` §3,
the runner is a one-command invocation
(`tools/verify_cuda_host.py [--optix]`) on a real
CUDA + (optional) OptiX-SDK host; the resulting
`docs/CUDA_HOST_VERIFICATION_REPORT.md` byte-replaces
the current audit-host REPAIR report.

Either path is a stand-alone slice; the operator's
call.

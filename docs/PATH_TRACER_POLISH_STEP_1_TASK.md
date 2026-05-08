# Path-Tracer Polish — Step 1 Task

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_POLISH_PLAN.md` §4.2 +
`§5 Recommended first polish item`.
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next slice
(PT-P.3 implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's reasoning.

---

## 1. Exact first polish item

**Title.** PT-P.x — Accumulation reset correctness.

**Source.** `PATH_TRACER_POLISH_PLAN.md` §4.2 (the smallest
viable diff identified by §5 of the plan).

**One-paragraph summary.** Tighten
`rr::renderer::AccumulationBuffer`'s host-side state machine
on two cold paths: (a) the host-only-build `reset()` call
returns false without zeroing `samples_`, leaving the buffer
in a "samples > 0 but not actually cleared" state that no
caller can directly observe today (every existing caller
checks the return) but a future caller might miss; and (b)
`resize(w, h)` always re-allocates, even when called with
the existing `(width_, height_)`, wasting a `cudaFree +
cudaMalloc + cudaMemset` round trip on the common "render
the same scene twice" path. This task ships the two small
cleanups + ONE new test that exercises the no-op resize.
Zero kernel touches, zero behaviour change for callers in
the hot path, byte-identical rendered output for every
existing render.

**Two specific changes.**

### 1.1. `reset()`: zero `samples_` BEFORE the launcher call

`src/renderer/AccumulationBuffer.cpp` `reset()` body today
(verbatim):

```cpp
bool AccumulationBuffer::reset() {
    if (!valid()) return false;
    samples_ = 0;
#ifdef RR_HAS_CUDA
    return rr::cuda::launch_accum_clear(device_.device_ptr(),
                                        device_.size());
#else
    return false;
#endif
}
```

Required outcome: `samples_ = 0;` must execute on EVERY
return-true path AND on the host-only return-false path.
The current code already runs the assignment before the
ifdef branch, so the change is one of THREE shapes,
implementer's choice:

(a) **No code change; doc-comment only.** Add a one-line
note above the `samples_ = 0` assignment explaining the
ordering invariant: "Zero the counter BEFORE the launcher
call so the host-only path still leaves the buffer in a
consistent zero-samples state." This makes the existing
correct ordering explicit and resilient against a future
refactor that moves the assignment to "after a successful
launch".

(b) **Restructure for clarity.** Hoist the `samples_ = 0`
line above the `valid()` early-return:

```cpp
bool AccumulationBuffer::reset() {
    samples_ = 0;
    if (!valid()) return false;
#ifdef RR_HAS_CUDA
    return rr::cuda::launch_accum_clear(device_.device_ptr(),
                                        device_.size());
#else
    return false;
#endif
}
```

This means even an invalid buffer's counter ends up at
zero after `reset()`. Slightly stronger invariant than (a);
slightly more dangerous because callers that rely on
`reset()` not touching state on an invalid buffer would
break. None do today (the only callers route through
`valid()` themselves).

(c) **Document and DO NOT change ordering.** Same as (a).

The recommended choice is **(a)** — doc-only — because
the existing ordering is already correct and changing it
risks the same class of subtle regression the task is
trying to prevent. (b) is acceptable if the implementer
prefers the stronger invariant and adds a test asserting
`samples_count() == 0` after a `reset()` on an invalid
buffer.

### 1.2. `resize(w, h)`: no-op fast path

`src/renderer/AccumulationBuffer.cpp` `resize()` body today
(verbatim, abbreviated):

```cpp
bool AccumulationBuffer::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        device_.reset();
        width_ = 0; height_ = 0; samples_ = 0;
        return false;
    }
    const std::size_t float_count =
        static_cast<std::size_t>(width) * height * kFloatsPerPixel;
    if (!device_.allocate(float_count)) {
        width_ = 0; height_ = 0; samples_ = 0;
        return false;
    }
    width_ = width; height_ = height; samples_ = 0;
    return reset();
}
```

Required outcome: when called with the existing dimensions
AND a buffer of the matching size, skip the
`device_.allocate` call and forward straight to `reset()`.
Concretely, insert a fast-path branch immediately after the
`width <= 0 || height <= 0` early-return:

```cpp
const std::size_t float_count =
    static_cast<std::size_t>(width) * height * kFloatsPerPixel;

// PT-P.x fast path: when called with the existing
// dimensions AND a buffer of the matching size, skip the
// reallocate. Saves a cudaFree + cudaMalloc + cudaMemset
// round trip on the "render the same scene twice" path.
if (width == width_ && height == height_
 && device_.size() == float_count) {
    samples_ = 0;
    return reset();
}

if (!device_.allocate(float_count)) { ... }
```

The sequence `samples_ = 0; return reset();` keeps the
external contract identical to the slow path (the resized
buffer is zero-sample + zero-byte either way).

### 1.3. New test: idempotent `resize(64, 64)` twice

Add ONE new test under `tests/`:

```cpp
// PT-P.x: re-resizing to the same dimensions must produce
// a 0-sample buffer (the no-op fast path's external
// behaviour matches the slow path).
{
    rr::renderer::AccumulationBuffer accum;
    bool first = accum.resize(64, 64);
    // Note: on a host-only audit-host build `first` is
    // false (no CUDA backend). Guard the assertion.
    if (first) {
        // Drive samples > 0 (host-only build cannot;
        // CUDA-host build can via accumulate_sample).
        // The test's purpose is the no-op fast path,
        // so we test the dimensions-only path.
    }
    bool second = accum.resize(64, 64);
    // The fast path returns whatever `reset()` returned
    // (true on CUDA, false on host-only). Either way,
    // samples_count() must read 0.
    (void)first;
    (void)second;
    REQUIRE(accum.samples_count() == 0);
    REQUIRE(accum.width()         == 64);
    REQUIRE(accum.height()        == 64);
}
```

The test placement is the implementer's choice:

- Add to `tests/gpu_tests.cpp` if a renderer-test fixture
  already lives there (today: no — the file covers GpuBuffer
  / GpuScene only).
- Add a new `tests/renderer_tests.cpp` ctest binary that
  links against `rr_renderer` and exercises
  `AccumulationBuffer`. This is the cleaner home; matches
  the per-module ctest split (`pathtracer_tests`,
  `image_tests`, etc.).

The new ctest binary would expand the audit-host count
from 6 -> 7 (OFF) and 7 -> 8 (ON-audit-host).

If a new ctest binary is too much for the slice, the test
can land inside a `#if 0` guard with a TODO (not the
preferred shape; flagged here as a fallback).

---

## 2. Files likely involved

The implementation slice will touch this minimal set:

| File                                       | Change                                                |
|--------------------------------------------|-------------------------------------------------------|
| `src/renderer/AccumulationBuffer.cpp`      | One doc-comment line above `samples_ = 0;` in        |
|                                            | `reset()`. One fast-path branch in `resize()`. ~6    |
|                                            | new lines total.                                      |
| `tests/renderer_tests.cpp` (NEW)           | One test fixture exercising `resize(64,64)` twice +  |
|   *or*                                     | the post-condition assertions. ~15 new lines.        |
| `tests/gpu_tests.cpp`                      | Same test, appended (alternative placement).          |
| `CMakeLists.txt`                           | Register the new ctest binary IF the implementer     |
|                                            | picks the new-file path. ~5 lines (target + link +   |
|                                            | `add_test`).                                          |
| `docs/BUILD_PLAN.md`                       | Slice-closing entry for the implementation slice.    |

`src/renderer/AccumulationBuffer.h` does NOT need to change
(both edits are in the implementation file).

---

## 3. What must not be touched

The implementation slice MUST keep the following byte-
identical:

### 3.1. Kernel and launcher code

- `src/cuda/CudaPathTracer.cu` — every byte.
- `src/cuda/CudaAccumulation.cu` — every byte.
- `src/cuda/CudaAccumulation.cuh` — every byte.
- `src/optix/OptixPrograms.cu` (path-trace raygen / miss /
  closest-hit) — every byte.
- `src/optix/OptixRenderer.cpp` `render_pathtrace*` paths —
  every byte.
- `src/cuda/CudaRngTestKernel.cu` — every byte.
- `src/pathtracer/RNG.h`, `Sampling.h` — every byte (the
  RNG-stability item is §4.1, NOT this slice).

### 3.2. Path-tracer output

- `output/pathtrace_spp_1.ppm`, `output/pathtrace_spp_16.ppm`
  must produce byte-identical pixel data when re-rendered
  on a CUDA host (the renderer's hot path is unchanged;
  the polish only touches the cold-path resize fast-path
  + an existing-line doc-comment).
- `output/optix_pathtrace_spp1.ppm`,
  `output/optix_pathtrace_spp16.ppm`: same.
- `output/gpu_accumulation_test.ppm`: same.
- `output/gpu_rng_test.ppm`: same.

### 3.3. CLI surface

- No new `--*` flag.
- No change to the existing dispatcher info-log lines for
  `--render-pathtrace`, `--render-optix-pathtrace`,
  `--render-rng-test`, `--render-accumulation-test`.

### 3.4. `PathTraceConfig`

- Byte-identical (zero new fields, zero default changes).
  The §4.7 firefly-clamp placeholder is a separate slice
  with its own task definition.

### 3.5. Public API

- `AccumulationBuffer`'s public methods (`resize`,
  `reset`, `accumulate_sample`, `resolve_to_image`,
  `width`, `height`, `samples_count`, `valid`,
  `device_ptr`): byte-identical signatures + return types.
- The existing return-value contracts (e.g.
  "returns `false` when allocation fails") remain
  honoured; the fast path returns whatever `reset()`
  returns.

### 3.6. Other audits / plans

- `docs/STAGE_11_AUDIT.md`: no edits.
- `docs/STAGE_20_OPTIX_PATH_TRACING_AUDIT.md`: no edits.
- `docs/CUDA_HOST_VERIFICATION_AUDIT.md`: no edits.
- `docs/PATH_TRACER_POLISH_PLAN.md`: no edits to §1-§4-§5;
  the implementation slice may add a one-line "PT-P.x
  shipped" note at the top of §4.2 if the format remains
  consistent with the texture-polish-plan precedent.
- `tools/verify_cuda_host.py`: no changes (the runner's
  command catalogue does not include
  `accumulation-buffer-tests`; the new ctest binary is
  picked up by the existing `cmake_build_command` -> ctest
  step automatically when a CUDA host invokes the
  runner).

---

## 4. PASS criteria

The implementation slice passes when ALL of the following
hold:

### 4.1. Build

- `cmake --build build` (audit host, RR_ENABLE_CUDA=OFF,
  RR_ENABLE_OPTIX=OFF): clean build, zero new warnings.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON): clean build,
  zero new warnings.

### 4.2. Tests

- `ctest --output-on-failure` from `build`: every test
  passes. The count grows from 6/6 to 7/7 if a new
  `renderer_tests` binary lands; otherwise it remains
  6/6 with the new test appended to an existing binary.
- `ctest --output-on-failure` from `build-ON`: every test
  passes (count 7/7 -> 8/8 in the new-binary case;
  otherwise 7/7).
- The new `AccumulationBuffer::resize(64, 64)` twice test
  (a) calls `resize(64, 64)`, (b) calls `resize(64, 64)`
  again, (c) asserts `samples_count() == 0 &&
  width() == 64 && height() == 64`. The test body must
  compile + pass on a host-only build (where `valid()`
  may return false; the post-conditions are tested via
  the public accessors which are valid regardless).

### 4.3. Source diff size

- `src/renderer/AccumulationBuffer.cpp` diff: <= 12 lines
  added, <= 4 lines deleted (~6 net new). Anything
  larger should be flagged in the slice's BUILD_PLAN
  entry as a deviation from the plan.
- `tests/`: ~15 new lines for the new test. If a new
  ctest binary is added, ~15 + 5 = ~20 net new across
  test + CMakeLists.

### 4.4. No-touch invariants

- `git diff` after the slice MUST show zero bytes changed
  in:
  - `src/cuda/`
  - `src/optix/`
  - `src/pathtracer/`
  - `src/main.cpp`
  - `src/core/CommandLine.{h,cpp}`
  - any `*.rrscene` file under `scenes/`

### 4.5. Behavioural smoke (audit host)

- `./build-ON/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene` continues to emit the
  documented "requires CUDA" audit-host fallback (exit 1;
  no kernel crash). The dispatcher reaches
  `PathTracer::render` whose CUDA-disabled branch returns
  the standard "PathTracer::render requires CUDA"
  message; the polish does not affect this path.
- `./build/bin/RelativityRender --scene-info
  scenes/test_textured_material.rrscene` continues to
  print the TEX-P.6 fixture's three flag/id case logs +
  `fixups applied: 2` (zero PT-P.x ripple onto the
  texture validator).

### 4.6. Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing entry
  matching the established TEX-P.x format (Scope / What
  ships / What does NOT change / Master rule compliance /
  Verified at the build).
- The entry references `docs/PATH_TRACER_POLISH_PLAN.md`
  §4.2 + this task file as the source of the
  specification.

### 4.7. Master rule compliance

- Build incrementally (rule 1) + every step compilable
  (rule 2): both audit-host configs green.
- No fake stubs (rule 3): the new test exercises real
  state transitions on a real `AccumulationBuffer`.
- No CPU per-pixel work (rule 5/7): the polish touches
  cold-path host-side state only; zero changes in the
  per-pixel computation graph.
- Update BUILD_PLAN (rule 8): the slice-closing entry.

---

## 5. Out-of-scope (deferred to later PT-P.x slices)

The following items from
`docs/PATH_TRACER_POLISH_PLAN.md` are explicitly NOT part
of this task; they have their own task definitions when
sequenced:

- §4.1 RNG stability (key-mix collision audit): touches
  `src/pathtracer/RNG.h` and changes every
  `pathtrace_spp_*.ppm` byte-exactly.
- §4.3 Max-bounce validation: needs a soft cap +
  `Logger::warning` in `src/pathtracer/PathTracer.cpp`.
- §4.4 Environment fallback clarity: doc-comment +
  dispatcher info log.
- §4.5 Emission handling: `is_emissive` helper in
  `src/material/MaterialTypes.h` + kernel branch.
- §4.6 Sample count validation: same shape as §4.3.
- §4.7 Firefly clamp placeholder: adds a default-off
  field to `PathTraceConfig` + matching kernel guard
  on BOTH the CUDA and OptiX path-trace raygens.

PT-P.2 (this task definition) and PT-P.3 (the
implementation slice) are the only PT-P.x slices
currently scheduled. After PT-P.3 lands, the operator
chooses the next polish item from the plan's §4 list.

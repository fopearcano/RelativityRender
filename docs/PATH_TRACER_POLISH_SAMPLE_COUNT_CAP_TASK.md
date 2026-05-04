# Path-Tracer Polish — Sample-Count Cap Task

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_POLISH_PLAN.md` §4.6.
Selected via:
`docs/PATH_TRACER_POLISH_AUDIT.md` §7's "Recommended next
safe stage" verdict (the §4.2 + §4.3 polishes shipped PASS;
§4.6 is the natural follow-up because it trivially adapts
the PT-P.6 warn-and-clamp pattern from `max_bounces` to
`samples_per_pixel`).
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next
slice (PT-P.9 implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's
reasoning.

---

## 1. Exact issue: sample-count cap / validation

**Title.** PT-P.x — Sample-count validation (soft cap).

**Source.** `PATH_TRACER_POLISH_PLAN.md` §4.6.

**One-sentence summary.** Add a soft upper bound to
`PathTraceConfig::samples_per_pixel` so callers asking for
absurdly large sample budgets (e.g. `samples_per_pixel =
100000`) get a single `Logger::warning` line + their spp
clamped down, instead of a host launcher loop that issues
that many kernel launches and dominates wall-clock time.

**The current state.**
`src/pathtracer/PathTracer.cpp::PathTracer::render` already
rejects `cfg.samples_per_pixel <= 0` with a documented
diagnostic at lines 27-30:

```cpp
if (cfg.samples_per_pixel <= 0) {
    result.message = "samples_per_pixel must be > 0";
    return result;
}
```

There is no UPPER bound. The only use of
`cfg.samples_per_pixel` after the validation prelude is the
host launcher loop's bound at line 97:

```cpp
for (int s = 0; s < cfg.samples_per_pixel; ++s) {
    if (!rr::cuda::launch_pathtrace_sample(...)) ...
}
```

The kernel itself is fast; it's the kernel-launch overhead
per sample that becomes pathological at very large spp.
The §4.6 plan called this out: "the operator might want a
warning" before the dispatcher commits to N kernel
launches.

**The single concrete change.**

This polish slice is structurally identical to PT-P.6
(`PATH_TRACER_POLISH_STEP_2_TASK.md`) but for spp instead
of max-bounces. The implementation pattern was established
in PT-P.6 and recorded as PASS by the PT-P.7 audit; this
slice adapts it to the second `PathTraceConfig` field.

Required outcome:

- Add a header-public constant
  `inline constexpr int kSamplesPerPixelCap = 4096`
  alongside the existing `kMaxBouncesCap = 32` in
  `src/pathtracer/PathTracer.h`. The value 4096 is the
  suggestion from §4.6 of the plan; the implementer may
  pick a different number with a recorded rationale. The
  doc-comment should:
    - Note the cap is a SUGGESTION (4096 samples produce
      a substantially deeper integration than the default
      16; the cap is not a hard ABI limit).
    - Mention that the cap exists primarily to catch
      typos / fat-finger errors at scene-authoring time
      (e.g. `samples_per_pixel = 1000` accidentally typed
      as `10000` is caught by a 4096 cap).
    - Reference the PT-P.6 precedent so a future reader
      sees the two cap constants live as a pair.

- Insert a soft-cap branch immediately AFTER the existing
  `cfg.samples_per_pixel <= 0` check and BEFORE the
  `cfg.max_bounces < 0` check:

  ```cpp
  // PT-P.x soft upper cap. Callers asking for absurdly
  // large spp budgets get a single warning + clamp to
  // `kSamplesPerPixelCap` rather than a launcher loop
  // that issues that many kernel launches. Mirrors the
  // PT-P.6 max-bounces clamp shape.
  int effective_samples_per_pixel = cfg.samples_per_pixel;
  if (effective_samples_per_pixel > kSamplesPerPixelCap) {
      rr::core::Logger::warning(
          "PathTraceConfig::samples_per_pixel=" +
          std::to_string(cfg.samples_per_pixel) +
          " exceeds the recommended cap of " +
          std::to_string(kSamplesPerPixelCap) +
          "; clamping. Set explicitly via the dispatcher "
          "CLI when very long sample budgets are needed.");
      effective_samples_per_pixel = kSamplesPerPixelCap;
  }
  ```

- Replace the SINGLE use of `cfg.samples_per_pixel` in the
  host launcher loop's bound with
  `effective_samples_per_pixel`:

  ```cpp
  for (int s = 0; s < effective_samples_per_pixel; ++s) {
      ...
  }
  ```

- The original `cfg.samples_per_pixel` field is PRESERVED
  (not mutated); the clamped local shadows it for the loop
  bound, mirroring PT-P.6's `effective_max_bounces`
  pattern. Any downstream info-log line that intentionally
  echoes the AUTHORED spp count keeps reading
  `cfg.samples_per_pixel`.

- The `<string>` and `core/Logger.h` includes were already
  added by PT-P.6 (commit `dfaa199`), so no new include
  directive is required in `PathTracer.cpp`.

## 2. Expected behavior

The four contractual properties the polish must honour
(matching the prompt's spec sub-bullets exactly):

### 2.1. Invalid sample counts are rejected or clamped

- **`samples_per_pixel == 0` or negative** is REJECTED
  with the existing diagnostic at lines 27-30. PT-P.x
  does NOT change this branch; the existing
  "samples_per_pixel must be > 0" message remains
  byte-identical.

### 2.2. Excessive sample counts are capped

- **`samples_per_pixel > kSamplesPerPixelCap`** is
  CLAMPED to `kSamplesPerPixelCap`. The host launcher
  loop runs exactly `kSamplesPerPixelCap` iterations;
  the resulting PPM is identical to a render with
  `samples_per_pixel = kSamplesPerPixelCap` from the
  caller's perspective.

### 2.3. Warning is logged

- Exactly ONE `Logger::warning` line is emitted per
  excessive call. The line names the authored value,
  the cap, and the clamp action, mirroring the
  PT-P.6 `max_bounces` warning's structure for
  consistency.

### 2.4. Existing valid sample counts remain unchanged

- For `samples_per_pixel ∈ [1, kSamplesPerPixelCap]`
  the function is BYTE-IDENTICAL with the pre-PT-P.x
  path tracer:
    - No new log line.
    - `effective_samples_per_pixel == cfg.samples_per_pixel`
      so the host loop runs the same number of times.
    - The kernel launch arguments (seed, env_color,
      etc.) are unchanged.
    - The accumulator's value across the spp loop is
      bit-identical with the pre-slice baseline (every
      sample consumed the same RNG seed; every kernel
      launch ran the same code path).
- The default `samples_per_pixel = 16` (and the
  `--render-pathtrace` dispatcher's hard-coded `kRuns`
  array values `1` + `16`) are well below the cap; no
  PT-P.x warning fires under any current dispatcher.

---

## 3. Files likely involved

The implementation slice will touch this minimal set:

| File                              | Change                                                  |
|-----------------------------------|---------------------------------------------------------|
| `src/pathtracer/PathTracer.h`     | Add `inline constexpr int kSamplesPerPixelCap = 4096`  |
|                                   | alongside the existing `kMaxBouncesCap`.               |
|                                   | ~5-10 lines (constant + doc-comment).                   |
| `src/pathtracer/PathTracer.cpp`   | Insert the warn-and-clamp branch after the existing    |
|                                   | `samples_per_pixel <= 0` check; replace the single use  |
|                                   | of `cfg.samples_per_pixel` in the host loop bound       |
|                                   | with `effective_samples_per_pixel`.                     |
|                                   | ~10-12 net new lines, 0-1 deleted.                      |
| `docs/BUILD_PLAN.md`              | Slice-closing entry following the established TEX-P.x  |
|                                   | / PT-P.x format.                                        |

`src/cuda/CudaPathTracer.cu`,
`src/optix/OptixPrograms.cu`,
`src/main.cpp`, `src/core/CommandLine.{h,cpp}`,
`src/renderer/AccumulationBuffer.{h,cpp}`,
every `tests/*.cpp` file, every `*.rrscene` file, and
every other `src/` file MUST be byte-identical
post-slice.

The two-source-file cap from PT-P.3 / PT-P.6's master
rules holds: this slice modifies exactly one `.cpp` + one
`.h` (both in `src/pathtracer/`).

### 3.1. Test placement (no new test recommended)

The §4.6 plan entry suggested "Add ONE test that calls
`render(.., spp = cap + 1)` and asserts the warning
fires + the clamped run produces the same image as
`render(.., spp = cap)`". The PT-P.7 audit reviewed the
PT-P.6 polish's identical shape (max-bounces) and
recorded zero REPAIR items WITHOUT a corresponding test
— the polish was deemed "verifiable by code inspection".
The PT-P.6 precedent stands.

This task brief therefore RECOMMENDS no new test, on
three grounds:

1. **Consistency with PT-P.6.** PT-P.6 shipped without a
   test for the analogous `max_bounces` clamp; PT-P.9
   should follow the same pattern so the polish arc has
   a single verification posture across both clamps.
2. **Test infrastructure friction.** Asserting that a
   `Logger::warning` line was emitted requires either a
   stderr-capture harness or a Logger interceptor;
   neither exists today and adding one for an 8-line
   polish is disproportionate. Asserting that
   `render(spp = cap + 1)` produces the SAME image as
   `render(spp = cap)` requires a CUDA host (host-only
   builds return an empty image with the documented
   "requires CUDA" message; the comparison is
   trivially true and useless).
3. **Code-inspection coverage.** The clamp body is three
   lines (init + branch + clamp); reading it confirms
   the contract for §2.1-§2.4 above.

If the implementer disagrees and prefers to add a test,
the cleanest placement is `tests/renderer_tests.cpp`
(extending the existing PT-P.3 ctest binary). The test
would have to be tolerant of the host-only-build branch
returning early — see PT-P.3's
`test_resize_same_dimensions_twice_keeps_zero_samples`
for the disjunction-on-build-config pattern.

The PT-P.9 implementation slice should make the
"test or no test" choice explicit in its BUILD_PLAN
entry, citing this section.

---

## 4. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 4.1. Kernel and launcher code

- `src/cuda/CudaPathTracer.cu` — every byte.
- `src/cuda/CudaAccumulation.cu` — every byte.
- `src/cuda/CudaAccumulation.cuh` — every byte.
- `src/optix/OptixPrograms.cu` (path-trace raygen / miss /
  closest-hit) — every byte. The OptiX-side path tracer
  reads its own `samples_per_pixel` from the dispatcher
  (`run_render_optix_pathtrace` in `src/main.cpp`); the
  PT-P.9 host clamp does NOT reach the OptiX raygen.
  This is consistent with PT-P.6's max-bounces clamp,
  which is also CUDA-path only.
- `src/optix/OptixRenderer.cpp` `render_pathtrace*` paths
  — every byte.

### 4.2. Renderer-level code

- `src/renderer/AccumulationBuffer.{h,cpp}` — every byte.
  The PT-P.3 polish to this file is SETTLED; PT-P.9 does
  not touch it.

### 4.3. Path-tracer output

For valid `samples_per_pixel ∈ [1, kSamplesPerPixelCap]`:

- `output/pathtrace_spp_1.ppm`,
  `output/pathtrace_spp_16.ppm`: byte-identical pixel
  data.
- `output/optix_pathtrace_spp1.ppm`,
  `output/optix_pathtrace_spp16.ppm`: byte-identical
  (the OptiX path is unaffected anyway).
- `output/gpu_accumulation_test.ppm`,
  `output/gpu_rng_test.ppm`: byte-identical (these
  dispatchers do not use `PathTraceConfig`).

For `samples_per_pixel > kSamplesPerPixelCap`: the PPM
matches a render with `samples_per_pixel =
kSamplesPerPixelCap`. No dispatcher today produces such
a value; this row of the matrix is not reachable
without a manual `PathTraceConfig` construction.

### 4.4. CLI surface

- No new `--*` flag.
- No change to the existing dispatcher info-log lines for
  `--render-pathtrace`, `--render-optix-pathtrace`,
  `--render-rng-test`, `--render-accumulation-test`.

### 4.5. `PathTraceConfig` field set

- Byte-identical (zero new fields, zero default changes).
  Only the new `kSamplesPerPixelCap` free constant is
  added — analogous to PT-P.6's `kMaxBouncesCap`.

### 4.6. Existing validation prelude semantics

- The `samples_per_pixel <= 0` rejection at lines 27-30
  must stay. The PT-P.9 clamp inserts AFTER this check;
  it does not replace the lower-bound rejection.
- The `max_bounces < 0` rejection + the PT-P.6
  `effective_max_bounces` clamp at lines 32-53: every
  byte preserved. PT-P.9 inserts BEFORE this block, so
  its diff line numbers move down but its content is
  unchanged.
- The `environment_intensity < 0.0f` rejection: every
  byte preserved.

### 4.7. Other audits / plans

- `docs/PATH_TRACER_POLISH_PLAN.md`: optionally add a
  one-line "PT-P.9 shipped" note at the top of §4.6 if
  the format remains consistent with the
  texture-polish-plan precedent. NOT required.
- `docs/PATH_TRACER_POLISH_AUDIT.md`,
  `docs/PATH_TRACER_POLISH_STEP_1_AUDIT.md`,
  `docs/PATH_TRACER_POLISH_STEP_1_TASK.md`,
  `docs/PATH_TRACER_POLISH_STEP_2_TASK.md`: NO edits.
- `tools/verify_cuda_host.py`: NO changes (the runner
  exercises the existing `--render-pathtrace` +
  `--render-optix-pathtrace` commands; no new artefact).

---

## 5. PASS criteria

The implementation slice passes when ALL of the following
hold:

### 5.1. Build

- `cmake --build build` (audit host, RR_ENABLE_CUDA=OFF,
  RR_ENABLE_OPTIX=OFF): clean build, zero new warnings.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON): clean build,
  zero new warnings.

### 5.2. Tests

- `ctest --output-on-failure` from `build`: 7/7 PASS.
- `ctest --output-on-failure` from `build-ON`: 8/8 PASS.
- Test counts are unchanged from PT-P.6 (the slice does
  not add a ctest binary per §3.1).

### 5.3. Source diff size

- `src/pathtracer/PathTracer.cpp` diff: ~10-12 added,
  ~0-1 deleted. Mirrors PT-P.6's diff (which was 21
  added / 1 deleted; the 21 was a flagged deviation
  due to doc-comment text). Anything LARGER than 25
  added flagged in the BUILD_PLAN entry as a deviation.
- `src/pathtracer/PathTracer.h` diff: ~5-10 added.
  Mirrors PT-P.6's `kMaxBouncesCap` addition (which
  was 11 added).

### 5.4. No-touch invariants

`git diff` after the slice MUST show zero bytes changed
in:

- `src/cuda/`
- `src/optix/`
- `src/renderer/`
- `src/main.cpp`
- `src/core/CommandLine.{h,cpp}`
- `src/io/`
- every `*.rrscene` file under `scenes/`
- every `tests/*.cpp` file (the slice ships no new test;
  see §3.1).
- `tools/verify_cuda_host.py`

This is verifiable by:

```
git diff -- \
  src/cuda/ src/optix/ src/renderer/ \
  src/main.cpp src/core/ src/io/ \
  scenes/ tests/ tools/verify_cuda_host.py \
  | wc -l
=> 0
```

### 5.5. Behavioural smoke (audit host)

- `./build/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene` continues to emit the
  documented "requires CUDA" audit-host fallback
  byte-identically with the pre-PT-P.9 baseline. The
  authored scene's `samples_per_pixel` (default 16)
  is well within the cap; no `Logger::warning` fires
  from the PT-P.9 clamp.
- `./build-ON/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene`: same.
- `./build/bin/RelativityRender --scene-info
  scenes/test_textured_material.rrscene`: emits the
  TEX-P.6 fixture's expected three-case log sequence
  byte-identically (one Case 1 info + two Case 3
  warnings; `fixups applied: 2`). Confirms zero
  PT-P.9 ripple onto the texture validator.

### 5.6. Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing
  entry matching the established PT-P.x format
  (Scope / What ships / What does NOT change /
  Behaviour matrix / Master rule compliance /
  Verified at the build).
- The entry references
  `docs/PATH_TRACER_POLISH_PLAN.md` §4.6 + this task
  file as the source of the specification.
- The PT-P.6 entry's behaviour matrix is the
  template; the PT-P.9 entry should produce a
  similarly-shaped table for spp.

### 5.7. Master rule compliance

- Build incrementally (rule 1) + every step compilable
  (rule 2): both audit-host configs green.
- No fake stubs (rule 3): the clamp is real defence
  against a real failure mode (very-large-spp
  launchers dominating wall-clock time).
- No CPU per-pixel work (rule 5/7): the polish touches
  cold-path host-side validation only; zero changes in
  the per-pixel computation graph.
- Update BUILD_PLAN (rule 8): the slice-closing entry.

---

## 6. Out-of-scope (deferred to later PT-P.x slices)

The following items from
`docs/PATH_TRACER_POLISH_PLAN.md` are explicitly NOT part
of this task; they have their own task definitions when
sequenced:

- §4.1 RNG stability (key-mix collision audit): touches
  `src/pathtracer/RNG.h` and changes every
  `pathtrace_spp_*.ppm` byte-exactly.
- §4.4 Environment fallback clarity: doc-comment +
  dispatcher info-log addition.
- §4.5 Emission handling: `is_emissive` helper in
  `src/material/MaterialTypes.h` + kernel branch.
- §4.7 Firefly clamp placeholder: adds a default-off
  field to `PathTraceConfig` + matching kernel guards
  on BOTH the CUDA and OptiX path-trace raygens.

Per `PATH_TRACER_POLISH_PLAN.md` §5 +
`PATH_TRACER_POLISH_AUDIT.md` §7's sequencing
guidance, the recommended order after §4.6 is:

1. §4.4 (env-fallback clarity; doc-comment + log,
   ~5 lines).
2. §4.5 (emission handling kernel branch).
3. §4.1 (RNG stability; changes every PPM byte-
   exactly).
4. §4.7 (firefly clamp; touches both backends).

PT-P.8 (this task definition) and PT-P.9 (the
implementation slice) are the only PT-P.x slices
currently scheduled. After PT-P.9 lands, the operator
chooses the next polish item from the plan's §4 list,
or pivots to a different polish arc / triggers the
CUDA-host verification run that flips the BLOCKED
rows from the PT-P.7 audit's §5 to PASS.

---

## 7. Why §4.6 is the safest viable next slice

Five reasons (mirroring the PT-P.5 structure for §4.3):

### 7.1. PT-P.7 audit verdict was clean

`docs/PATH_TRACER_POLISH_AUDIT.md` §8 records overall
PASS, zero REPAIR items, BLOCKED rows carried forward
to a CUDA-host run. The path tracer is in a
known-good baseline post-PT-P.6.

### 7.2. The change is host-side only

The clamp body is three statements (assign, branch +
log, clamp) in `PathTracer::render`'s validation
prelude. ZERO bytes of CUDA / OptiX / per-pixel
kernel code change. The PT-P.7 audit's §3.1 / §4.1 /
§6 grep sweeps remain valid post-slice without
re-running.

### 7.3. The pattern is established

`Logger::warning` + clamp + use-effective-local is
exactly the pattern PT-P.6 shipped for `max_bounces`.
PT-P.7 recorded zero REPAIR items for that pattern;
the PT-P.9 slice is structurally identical at a
different config field.

### 7.4. The default + every common value is unaffected

`PathTraceConfig::samples_per_pixel` defaults to
`16`. The dispatchers that override the default
(`run_render_pathtrace` in `src/main.cpp` and the
OptiX equivalent) hard-code spp values of `1` and
`16` only. No CLI flag exists today to override
`samples_per_pixel`. The clamp therefore fires only
when a caller manually constructs a
`PathTraceConfig` with
`samples_per_pixel > kSamplesPerPixelCap` — which no
caller does today and no scene authors. Every
existing render is byte-identical post-slice.

### 7.5. The clamp pattern's host-only nature scales

PT-P.6 + PT-P.9 together establish a
"`PathTraceConfig` validation prelude" idiom: every
field gets its lower-bound check, optionally followed
by an upper-bound clamp, before the function commits
to the CUDA or OptiX dispatch path. Future fields
(e.g. PT-P.7's planned `firefly_clamp` placeholder
in §4.7) can drop into the same prelude shape
without touching any kernel code.

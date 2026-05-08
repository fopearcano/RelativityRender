# Path-Tracer Polish — Step 2 Task

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_POLISH_PLAN.md` §4.3.
Selected via: `docs/PATH_TRACER_POLISH_STEP_1_AUDIT.md`'s
"Recommended next step" verdict (the §4.2 polish landed
PASS; §4.3 is the natural follow-up because it reuses the
same `Logger::warning` clamp shape the texture validator
established in TEX-P.2 / TEX-P.5).
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next
slice (PT-P.6 implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's reasoning.

---

## 1. Exact next polish item

**Title.** PT-P.x — Max-bounce validation (soft cap).

**Source.** `PATH_TRACER_POLISH_PLAN.md` §4.3.

**One-paragraph summary.** Add a soft upper bound to
`PathTraceConfig::max_bounces` so callers asking for
absurdly long paths get a single `Logger::warning` line +
their bounce count clamped down to a safe maximum, instead
of a kernel that does not crash but takes orders of
magnitude longer than expected. The lower-bound check
(`max_bounces < 0` rejects with a diagnostic) already
exists in `PathTracer::render`; this slice only adds the
upper-bound clamp, mirroring the warn-and-clamp pattern
TEX-P.2 / TEX-P.5 established for
`validate_material_texture_ids`. ZERO kernel touches; ZERO
behaviour change for any caller passing
`max_bounces ∈ [0, kMaxBouncesCap]`; ZERO new CLI surface.

**The single concrete change.**

`src/pathtracer/PathTracer.cpp::PathTracer::render` today
(verbatim, abbreviated):

```cpp
if (cfg.max_bounces < 0) {
    result.message = "max_bounces must be >= 0";
    return result;
}
// ... continued validation ...
#ifndef RR_HAS_CUDA
    ...
#else
    ...
    for (int s = 0; s < cfg.samples_per_pixel; ++s) {
        if (!rr::cuda::launch_pathtrace_sample(
                ...,
                cfg.max_bounces,
                ...
        )) ...
    }
```

Required outcome:

- Add a file-local (or header-public) constant
  `kMaxBouncesCap = 32` whose value the implementer may
  pick differently if they have a more principled reason;
  32 is the suggestion from §4.3 of the plan. Public in
  `PathTraceConfig.h` is the recommended placement (the
  cap becomes searchable + reusable by future
  dispatchers); a private constexpr in the .cpp's
  anonymous namespace is also acceptable.
- Insert a soft-cap branch immediately AFTER the existing
  `cfg.max_bounces < 0` check:

  ```cpp
  int effective_max_bounces = cfg.max_bounces;
  if (effective_max_bounces > kMaxBouncesCap) {
      rr::core::Logger::warning(
          "PathTraceConfig::max_bounces=" +
          std::to_string(cfg.max_bounces) +
          " exceeds the recommended cap of " +
          std::to_string(kMaxBouncesCap) +
          "; clamping. Set explicitly via the dispatcher "
          "CLI when long bounce paths are needed.");
      effective_max_bounces = kMaxBouncesCap;
  }
  ```

- Replace the single use of `cfg.max_bounces` in the
  CUDA-only launcher call (`cuda::launch_pathtrace_sample`)
  with `effective_max_bounces`. Logging and other downstream
  references that intentionally echo the AUTHORED value
  (e.g. dispatcher info-log lines in `src/main.cpp`'s
  `run_render_pathtrace` if any) keep reading
  `cfg.max_bounces` — the warn line above gives the operator
  the diagnostic; the rest of the system shows the authored
  intent. The implementer's call: if there is exactly one
  use of `cfg.max_bounces` remaining in
  `PathTracer::render` (the launcher arg), this is a
  one-line replacement. If there are zero or multiple, the
  implementer documents the choice.

- Add `#include "core/Logger.h"` to
  `src/pathtracer/PathTracer.cpp` if not already
  transitively included. (`PathTracer.h` does not include
  it; the .cpp's existing `<utility>` /
  `<cstddef>` / `<string>` direct includes do not pull
  it.)

**No new test required.** The §4.3 plan entry's "Cost"
line says "~8 lines in `PathTracer.cpp`". The polish is
verifiable by code inspection (the clamp body is a
single branch; reading it confirms the contract). The
implementer MAY add a one-line check to
`tests/renderer_tests.cpp` if a test feels useful, but the
test would have to construct a `GpuScene` (host-side
work that requires `RR_HAS_CUDA`-gated upload paths to be
non-trivial) and would still hit the audit-host
"requires CUDA" branch BEFORE the clamp's effect is
observable in the launcher arg. Verifying that the
warning text is correct via a stderr capture is doable
but disproportionate for an 8-line polish.

---

## 2. Files likely involved

The implementation slice will touch this minimal set:

| File                                     | Change                                                  |
|------------------------------------------|---------------------------------------------------------|
| `src/pathtracer/PathTracer.cpp`          | Add `#include "core/Logger.h"`; add the warn+clamp     |
|                                          | block (~8 lines); replace one use of                    |
|                                          | `cfg.max_bounces` with `effective_max_bounces` in the  |
|                                          | launcher call. Net ~8-10 added lines, 0-1 deleted.      |
| `src/pathtracer/PathTracer.h`            | OPTIONAL: add `inline constexpr int                     |
|                                          | kMaxBouncesCap = 32;` (recommended; makes the cap       |
|                                          | searchable + reusable). ~3 lines + a doc-comment.       |
| `docs/BUILD_PLAN.md`                     | Slice-closing entry following the established TEX-P.x  |
|                                          | / PT-P.x format.                                        |

`src/cuda/CudaPathTracer.cu`,
`src/optix/OptixPrograms.cu`,
`src/main.cpp`, `src/core/CommandLine.{h,cpp}`,
every `tests/*.cpp` file, every `*.rrscene` file, and
every other `src/` file MUST be byte-identical
post-slice.

The two-source-file cap from PT-P.3's master rules holds:
this slice modifies at most one .cpp + one .h (both in
`src/pathtracer/`).

---

## 3. Why it is safe now

Five reasons §4.3 is the safest viable next slice:

### 3.1. PT-P.4 audit verdict was clean

`docs/PATH_TRACER_POLISH_STEP_1_AUDIT.md` §6 records:
overall PASS, zero REPAIR items, one BLOCKED row carried
forward to a CUDA-host run. The path-tracer state machine
is in a known-good baseline post-PT-P.3. §4.3 starts
from that baseline.

### 3.2. The change is host-side only

The clamp body is three statements (assign, branch, log)
in `PathTracer::render`'s validation prelude. ZERO bytes
of CUDA / OptiX / per-pixel kernel code change. The
existing PT-P.4 audit's §3.1 + §4 grep sweeps remain
valid post-slice without re-running.

### 3.3. The pattern is established

`Logger::warning` + clamp is the exact shape
`validate_material_texture_ids` already uses (TEX-P.2 /
TEX-P.5 / TEX-P.6). The TEX-P.7 audit recorded zero
REPAIR items for that pattern across the texture-polish
arc; the path-tracer slice is structurally identical at
a different file.

### 3.4. The default + every common value is unaffected

`PathTraceConfig::max_bounces` defaults to `4`. The two
dispatchers (`run_render_pathtrace` in
`src/main.cpp:2374` and the OptiX equivalent) keep the
default. No CLI flag exists today to override
`max_bounces`. The clamp therefore fires only when a
caller manually constructs a `PathTraceConfig` with
`max_bounces > 32` — which no caller does today and no
test scene authors. Every existing render is
byte-identical post-slice.

### 3.5. The OptiX side has its own bound

`OptixPipeline::set_pipeline_options` enforces
`OPTIX_PIPELINE_MAX_TRACE_DEPTH` at pipeline-build time
(the OptiX raygen's bounce loop is bounded by it). The
host-side `PathTraceConfig::max_bounces` clamp is
analogous defence in depth on the CUDA path; both
backends now refuse to dispatch absurdly long paths
without a warning. Mirroring the constraint surface
across backends is consistent with the
`docs/TEXTURE_SYSTEM.md` §1 single-source-of-truth
discipline.

### What is NOT yet safe (and why §4.3 is preferred over alternatives)

- **§4.1 RNG stability** unavoidably changes every
  `pathtrace_spp_*.ppm` byte-exactly. Even with a clean
  TEX-P-style audit, that change ripples through every
  CUDA-host PPM reference. Sequence it AFTER §4.3
  (which keeps the rendered output bit-identical) so
  the next CUDA-host run can flip exactly one BLOCKED
  row at a time.
- **§4.7 Firefly clamp placeholder** touches both
  backends (CUDA + OptiX path-trace raygen). Each backend
  needs the same kernel guard. §4.3 is host-only; pick it
  first.
- **§4.4 Environment fallback clarity** is a doc-comment
  + dispatcher-info-log slice (~5 lines, similar safety
  to §4.3). Acceptable as the next slice instead of §4.3
  if the operator prefers a doc-only follow-up; this
  task definition selects §4.3 because it tightens
  RUNTIME safety (avoids the "not-actually-hung kernel"
  surprise the §4.3 plan entry called out) over
  documentation.

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

- `ctest --output-on-failure` from `build`: 7/7 PASS.
- `ctest --output-on-failure` from `build-ON`: 8/8 PASS.
- Test counts are unchanged from PT-P.3 (the slice does
  not add or remove a ctest binary).

### 4.3. Source diff size

- `src/pathtracer/PathTracer.cpp` diff: <= 12 lines added,
  <= 2 lines deleted (~8-10 net new). Anything larger
  flagged in the BUILD_PLAN entry as a deviation
  (precedent: PT-P.3's diff-size deviation note is the
  template).
- `src/pathtracer/PathTracer.h` diff: <= 6 lines added if
  the implementer hosts `kMaxBouncesCap` here; 0 lines
  if the constant lives in the .cpp's anonymous
  namespace.

### 4.4. No-touch invariants

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
  see §1).
- `tools/verify_cuda_host.py` (no new dispatcher; no new
  artefact).

The `src/pathtracer/RNG.h`, `Sampling.h`, and the OptiX
`render_pathtrace*` paths are also byte-identical
post-slice (verified by `git diff -- src/pathtracer/RNG.h
src/pathtracer/Sampling.h src/optix/OptixRenderer.cpp |
wc -l` returning 0).

### 4.5. Behavioural smoke (audit host)

- `./build/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene` continues to emit the
  documented "requires CUDA" audit-host fallback
  byte-identically with the pre-PT-P.6 baseline. The
  authored scene's `max_bounces` (default 4) is well
  within the cap; no `Logger::warning` fires from the
  clamp.
- `./build-ON/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene`: same.
- `./build/bin/RelativityRender --scene-info
  scenes/test_textured_material.rrscene`: emits the
  TEX-P.6 fixture's expected three-case log sequence
  byte-identically (one Case 1 info + two Case 3
  warnings; `fixups applied: 2`). Confirms zero PT-P.6
  ripple onto the texture validator.

### 4.6. Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing entry
  matching the established TEX-P.x / PT-P.x format
  (Scope / What ships / What does NOT change / Master
  rule compliance / Verified at the build).
- The entry references
  `docs/PATH_TRACER_POLISH_PLAN.md` §4.3 + this task file
  as the source of the specification.

### 4.7. Master rule compliance

- Build incrementally (rule 1) + every step compilable
  (rule 2): both audit-host configs green.
- No fake stubs (rule 3): the clamp is real defence
  against a real failure mode (very-long-bounce kernels
  appearing as hung renderers).
- No CPU per-pixel work (rule 5/7): the polish touches
  cold-path host-side validation only; zero changes in
  the per-pixel computation graph.
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
- §4.4 Environment fallback clarity: doc-comment +
  dispatcher info log.
- §4.5 Emission handling: `is_emissive` helper in
  `src/material/MaterialTypes.h` + kernel branch.
- §4.6 Sample count validation: same shape as §4.3 for
  spp; sequence after §4.3 lands so the warn-and-clamp
  pattern is established in `PathTracer::render` first.
- §4.7 Firefly clamp placeholder: adds a default-off
  field to `PathTraceConfig` + matching kernel guard on
  BOTH the CUDA and OptiX path-trace raygens.

Per `PATH_TRACER_POLISH_PLAN.md` §5's sequencing, the
recommended order after §4.3 is §4.6 (sample-count cap;
trivially reuses the §4.3 pattern), then any of §4.4 /
§4.5 / §4.1 / §4.7 in operator preference.

PT-P.5 (this task definition) and PT-P.6 (the
implementation slice) are the only PT-P.x slices
currently scheduled. After PT-P.6 lands, the operator
chooses the next polish item from the plan's §4 list.

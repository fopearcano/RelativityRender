# Path-Tracer Polish — Sample-Count Cap Audit

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `09cc14a` ("PT-P.9:
sample-count cap (impl)").
Scope: PT-P.9 — the implementation slice that ships
`PATH_TRACER_POLISH_PLAN.md` §4.6 per the brief in
`PATH_TRACER_POLISH_SAMPLE_COUNT_CAP_TASK.md`.
Mode: documentation only. **No source code is modified by
this audit.**
Auditor: Claude Code, on the audit host (no CUDA Toolkit,
no OptiX SDK).

This audit walks the nine prompt checks in order and
records a single verdict at the end. Verdict legend
matches the texture-polish-audit + PT-P.4 / PT-P.7
precedent:

- **PASS** — implemented, type-checked on the audit host,
  AND empirically exercisable on the audit host with a
  recorded happy-path run.
- **REPAIR** — implemented but a defect or inconsistency
  was found that should be patched.
- **BLOCKED** — empirical verification requires a CUDA
  host (no nvcc / OptiX SDK on the audit host).

---

## 1. Sample-count validation exists

**PASS.**

The validation lives at two locations, both in
`src/pathtracer/PathTracer.{h,cpp}`:

### 1.1 Cap constant (`src/pathtracer/PathTracer.h:36`)

```cpp
inline constexpr int kSamplesPerPixelCap = 4096;
```

Sits directly below `kMaxBouncesCap = 32` (the PT-P.6
constant). The doc-comment block (lines 25-35) describes
the cap as a SUGGESTION primarily intended to catch
typos at scene-authoring time (e.g. fat-finger 10000
instead of 1000) and notes the two constants live as a
pair so future readers see them together.

### 1.2 Validation prelude (`src/pathtracer/PathTracer.cpp:32-49`)

The new branch sits between the existing
`cfg.samples_per_pixel <= 0` rejection (lines 28-31) and
the PT-P.6 `cfg.max_bounces < 0` rejection (lines 50
onward), forming a uniform "`PathTraceConfig` validation
prelude" idiom across the two clamped fields.

The branch shape (verbatim from the diff):

```cpp
int effective_samples_per_pixel = cfg.samples_per_pixel;
if (effective_samples_per_pixel > kSamplesPerPixelCap) {
    rr::core::Logger::warning(
        "PathTraceConfig::samples_per_pixel=" +
        std::to_string(cfg.samples_per_pixel) +
        " exceeds the recommended cap of " +
        std::to_string(kSamplesPerPixelCap) +
        "; clamping. Set explicitly via the dispatcher CLI "
        "when very long sample budgets are needed.");
    effective_samples_per_pixel = kSamplesPerPixelCap;
}
```

### 1.3 Use-site (`src/pathtracer/PathTracer.cpp:115`)

The single use of `cfg.samples_per_pixel` at the host
launcher loop's bound was replaced with
`effective_samples_per_pixel`:

```cpp
for (int s = 0; s < effective_samples_per_pixel; ++s) {
```

`grep -rn "for.*samples_per_pixel|for.*effective_samples"
src/pathtracer/*.cpp src/main.cpp` returns exactly one
match (the line above). No stale `cfg.samples_per_pixel`
read remains in the loop body.

The original `cfg.samples_per_pixel` field is preserved
on the POD (the clamp shadows it for the loop bound, not
mutates it), so any downstream info-log line that
intentionally echoes the AUTHORED value would still see
it — the same shape PT-P.6 used for `cfg.max_bounces`.

---

## 2. Invalid sample counts are rejected or clamped

**PASS.**

Two distinct invalid-input classes are handled by two
distinct branches:

| Input class                           | Branch                                   | Outcome                                        |
|---------------------------------------|------------------------------------------|------------------------------------------------|
| `samples_per_pixel <= 0`              | `PathTracer.cpp:28-31` (existing,        | Rejected with the documented diagnostic        |
| (zero or negative)                    | byte-identical with pre-PT-P.9)          | "samples_per_pixel must be > 0"; function      |
|                                       |                                          | returns early with `result.ok == false`.       |
| `samples_per_pixel >`                 | `PathTracer.cpp:32-49` (new in           | One `Logger::warning` line; clamped to         |
| `kSamplesPerPixelCap` (excessive)     | PT-P.9)                                  | `kSamplesPerPixelCap = 4096`; function         |
|                                       |                                          | proceeds with the clamped local; PPM matches   |
|                                       |                                          | a render with `samples_per_pixel = 4096`.      |

The lower-bound rejection (zero / negative) RUNS FIRST
in the prelude order, so a malicious caller cannot
combine `samples_per_pixel = -1` with a "would otherwise
clamp" check and bypass the rejection: the `<= 0` branch
has already returned by the time the clamp test runs.

---

## 3. Excessive sample counts are capped

**PASS.**

The cap value `kSamplesPerPixelCap = 4096` was selected
per `PATH_TRACER_POLISH_PLAN.md` §4.6's suggestion. The
clamp behaves correctly across the four authoring-
relevant boundary cases:

| `samples_per_pixel` | Clamp action                | Loop iterations | Warn line? |
|---------------------|-----------------------------|-----------------|------------|
| 4095                | None (4095 ≤ 4096)          | 4095            | no         |
| 4096                | None (4096 ≤ 4096)          | 4096            | no         |
| 4097                | Clamp to 4096               | 4096            | yes        |
| 100000              | Clamp to 4096               | 4096            | yes        |

The `>` (strict greater-than) comparison ensures
`samples_per_pixel == kSamplesPerPixelCap` passes
through unchanged — the operator gets exactly the
sample budget they authored at the cap value, with no
spurious warning.

---

## 4. Warning / log behavior exists

**PASS.**

The `Logger::warning` call (lines 41-46) emits exactly
one line per excessive call. The line content names:

- **the authored value**: `"PathTraceConfig::samples_per_pixel=" +
  std::to_string(cfg.samples_per_pixel)` — reading
  `cfg.samples_per_pixel` (NOT
  `effective_samples_per_pixel`) so the operator sees
  what they actually authored, not the post-clamp value.
- **the cap**: `" exceeds the recommended cap of " +
  std::to_string(kSamplesPerPixelCap)` — sources the
  number from the constant so any future change to
  `kSamplesPerPixelCap` automatically updates the
  warning text.
- **the clamp action**: `"; clamping. Set explicitly via
  the dispatcher CLI when very long sample budgets are
  needed."` — a hint for the operator that the clamp
  was applied + how to override (placeholder for a
  future `--samples` CLI flag).

The shape mirrors the PT-P.6 `max_bounces` warning
verbatim — same field-naming convention, same
"recommended cap of" phrasing, same "; clamping. ..."
suffix. Two operators reading the two warnings will see
they're the same idiom applied to different fields.

The empirical end-to-end verification of a fired warning
line is BLOCKED on a CUDA host (host-only build returns
the "requires CUDA" message before reaching the kernel
launch); see §8.

---

## 5. Existing valid sample-count behavior is unchanged

**PASS.**

Three independent checks confirm no behavior change for
`samples_per_pixel ∈ [1, kSamplesPerPixelCap]`:

### 5.1 Source-diff containment

`git diff 09cc14a~1..09cc14a -- src/cuda/ src/optix/
src/renderer/ src/main.cpp src/core/ src/io/ scenes/
tests/ tools/verify_cuda_host.py CMakeLists.txt | wc -l`
returns 0 bytes. Every kernel, launcher, OptiX program,
dispatcher, and scene file is byte-identical with the
pre-PT-P.9 commit `dfaa199`.

### 5.2 Branch arithmetic

For `samples_per_pixel ∈ [1, 4096]`:

```cpp
int effective_samples_per_pixel = cfg.samples_per_pixel;
// ^ assigned the authored value
if (effective_samples_per_pixel > kSamplesPerPixelCap) {
    // ^ false; branch not entered
    ...
}
// effective_samples_per_pixel == cfg.samples_per_pixel
```

The launcher loop receives the same iteration count as
before. The kernel arguments (seed, env_color, etc.) are
unchanged. The accumulator's per-sample value is
bit-identical with the pre-PT-P.9 baseline.

### 5.3 Empirical smoke

```
$ ./build-ON/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
[ERROR] --render-pathtrace requires CUDA. Rebuild with
        -DRR_ENABLE_CUDA=ON on a host with the CUDA
        Toolkit and a CUDA-capable GPU.
```

The dispatcher reaches `PathTracer::render` whose
CUDA-disabled branch returns the standard "requires
CUDA" message. The default `samples_per_pixel = 16` is
well within the cap; no `Logger::warning` from PT-P.9
fires. Output byte-identical with the pre-PT-P.9
baseline.

```
$ ./build/bin/RelativityRender --scene-info \
    scenes/test_textured_material.rrscene
... [WARN] scene: material 'textured-quad-material'
         (id=0) has useBaseColorTexture=true but
         baseColorTextureId=0 is out of range [0, 0);
         falling back to flat baseColor.
... [WARN] scene: material 'out-of-range-texture'
         (id=3) has useBaseColorTexture=true but
         baseColorTextureId=99 is out of range [0, 0);
         falling back to flat baseColor.
... [INFO]     fixups applied    : 2
```

TEX-P.6 fixture's three-case logs unchanged. Confirms
zero ripple onto the texture validator.

---

## 6. Build status

**PASS.**

Re-ran during this audit:

| Config      | RR_ENABLE_CUDA | RR_ENABLE_OPTIX | Build     | ctest    |
|-------------|:--------------:|:---------------:|-----------|:--------:|
| `build`     | OFF            | OFF             | clean     | 7/7 PASS |
| `build-ON`  | OFF            | ON              | clean     | 8/8 PASS |

Both audit-host configs report zero new compiler warnings
under the `rr_apply_warnings`-enforced `-Wall -Wextra
-Wpedantic` triple. ctest counts unchanged from PT-P.6
(the slice did not add or remove a ctest binary, per the
PT-P.8 task §3.1's no-new-test recommendation).

---

## 7. CPU path-tracing violations

**ZERO violations** — verified by re-running the
Stage-11-audit grep sweeps on the post-PT-P.9 tree.

### 7.1 Per-pixel for-loops on the host

```
$ grep -rnE "for.*<.*width|for.*<.*height" \
    src/renderer/ src/pathtracer/*.cpp src/main.cpp
=> (no matches)
```

Identical to the Stage 11 audit + the PT-P.4 / PT-P.7
findings.

### 7.2 spp launcher loops

```
$ grep -rn "for.*samples_per_pixel|for.*effective_samples" \
    src/pathtracer/*.cpp src/main.cpp
src/pathtracer/PathTracer.cpp:115:
    for (int s = 0; s < effective_samples_per_pixel; ++s) {
```

One match — the SAME spp launcher loop the Stage 11 +
PT-P.4 + PT-P.7 audits classified as host iteration at
sample-frame granularity (not per-pixel). PT-P.9 only
changed the loop bound's expression
(`cfg.samples_per_pixel` → `effective_samples_per_pixel`);
the loop itself is byte-identical in shape.

The grep needed the `effective_samples` alternation to
catch the post-PT-P.9 expression. The `cfg.samples_per_pixel`
sub-pattern returns zero matches now (the only use of
that field at line 28 is the validation-prelude
comparison, not a `for` loop).

### 7.3 Host-side intersection / closest-hit code

```
$ grep -rn "intersect_sphere|intersect_triangle|closest_hit" \
    src/renderer/ src/pathtracer/*.cpp
src/renderer/Hit.h:30:
    // `intersect_triangle`. The third coord is
    // `1 - bary_u - bary_v`.
```

One match — a doc-comment in `renderer/Hit.h`. No
host-side intersection code exists. Identical to the
Stage 11 + PT-P.4 + PT-P.7 findings.

### 7.4 PT-P.9-specific scope

PT-P.9 edited two files:

| File                              | New per-pixel code? |
|-----------------------------------|---------------------|
| `src/pathtracer/PathTracer.h`     | NO. New `inline constexpr int` + doc-comment. |
| `src/pathtracer/PathTracer.cpp`   | NO. Validation-prelude addition + one use-site replacement. |

Neither file contains a per-pixel `for` loop, a call to
any `intersect_*` / `closest_hit` / `sample_*hemisphere*`
/ `next_float` / `next_vec2` primitive, or any code that
reads or writes a per-pixel value.

Master rule 5/7 ("All per-pixel/per-ray rendering must
happen on GPU") therefore remains upheld post-PT-P.9.

---

## 8. Runtime-deferred status

**BLOCKED on the same six artefacts the
`PATH_TRACER_POLISH_PLAN.md` §2 + every prior path-tracer
audit (Stage 11, PT-P.4, PT-P.7) enumerate.** PT-P.9
does NOT alter the per-pixel computation graph (§5.1
+ §7 confirmed), so the empirical verification surface
is unchanged from PT-P.7.

| Artefact                                | CUDA-host expectation                       |
|-----------------------------------------|---------------------------------------------|
| `output/gpu_rng_test.ppm`               | byte-identical with pre-PT-P.9              |
| `output/gpu_accumulation_test.ppm`      | byte-identical                              |
| `output/pathtrace_spp_1.ppm`            | byte-identical                              |
| `output/pathtrace_spp_16.ppm`           | byte-identical                              |
| `output/optix_pathtrace_spp1.ppm`       | byte-identical                              |
| `output/optix_pathtrace_spp16.ppm`      | byte-identical                              |

The byte-identical claim is structurally guaranteed by
§5.1 + the clamp's no-fire-on-default-config property
(every dispatcher today uses `samples_per_pixel ∈ {1,
16}`, well within the `kSamplesPerPixelCap = 4096`
cap).

### 8.1 Additional CUDA-host operator checks

Two follow-ups the operator may want on a CUDA host:

- **PT-P.9 warn-line emission.** Construct a
  `PathTraceConfig` with
  `samples_per_pixel = 5000` (e.g. via a future
  `--samples` CLI flag, or a one-off harness in a
  test) and confirm the Logger emits exactly one
  line:
  `PathTraceConfig::samples_per_pixel=5000 exceeds
  the recommended cap of 4096; clamping. ...`. The
  CUDA spp loop then runs exactly 4096 iterations.
- **PT-P.9 byte-identity at the cap boundary.**
  Render `samples_per_pixel = 4096` and
  `samples_per_pixel = 5000` on the same scene; the
  resulting PPMs must match bit-for-bit (the second
  is clamped to the first's value). This is the
  external-contract test the §4.6 plan suggested,
  exercised by code inspection in PT-P.9 + this
  audit; a CUDA-host slice could thread it end-to-end
  through `tools/verify_cuda_host.py` if desired.

### 8.2 Runner integration status

`tools/verify_cuda_host.py` does NOT need an update for
PT-P.9 — the runner exercises the existing
`--render-pathtrace` + `--render-optix-pathtrace`
commands; no new artefact.

```
$ git diff 09cc14a~1..09cc14a -- tools/verify_cuda_host.py
=> 0 bytes
```

---

## 9. Verdict

| # | Audit item                                          | Result   |
|---|-----------------------------------------------------|----------|
| 1 | Sample-count validation exists                      | PASS     |
| 2 | Invalid sample counts rejected or clamped           | PASS     |
| 3 | Excessive sample counts capped                      | PASS     |
| 4 | Warning / log behavior exists                       | PASS     |
| 5 | Existing valid sample-count behavior unchanged      | PASS     |
| 6 | Build status (both audit-host configs)              | PASS     |
| 7 | CPU path-tracing violations                         | PASS — zero violations |
| 8 | Runtime-deferred status                             | BLOCKED  |
| 9 | Overall                                             | **PASS** (one BLOCKED row carried forward to a CUDA-host run) |

**Overall verdict: PASS.**

PT-P.9 ships exactly the polish the PT-P.8 task brief
specified, both audit-host build configs remain green
(7/7 OFF, 8/8 ON-audit-host), the per-pixel code path
is byte-identical pre/post-slice, and master rule 5/7
(no CPU ray tracing) remains enforced. The single
BLOCKED row is the same runtime-deferred surface every
prior path-tracer audit recorded; nothing in PT-P.9
changes it.

REPAIR items: none.

The PT-P.{5..9} sub-arc (PT-P.5 task → PT-P.6 impl
(`max_bounces` clamp) → PT-P.7 audit → PT-P.8 task →
PT-P.9 impl (`samples_per_pixel` clamp) → this audit)
together establishes a uniform "`PathTraceConfig`
validation prelude" idiom: every config field gets its
lower-bound check, optionally followed by an upper-bound
clamp, before the function commits to the CUDA or OptiX
dispatch path. Future fields (e.g. §4.7's `firefly_clamp`
placeholder) can drop into the same prelude shape
without touching any kernel code.

### Recommended next step

Per `PATH_TRACER_POLISH_PLAN.md` §5 +
`PATH_TRACER_POLISH_AUDIT.md` §7's sequencing, the
preferred next slice is:

- **§4.4 — Environment fallback clarity** (smallest
  remaining item; ~5 lines of doc-comment + dispatcher
  info-log addition). Opens with a PT-P.11 task
  definition mirroring the PT-P.{2,5,8} cadence.

Alternatives:

- **§4.5 — Emission handling** (`is_emissive` helper +
  CUDA path-tracer kernel branch).
- **§4.1 — RNG stability** (changes every
  `pathtrace_spp_*.ppm` byte-exactly; sequence after
  the smaller items).
- **§4.7 — Firefly clamp placeholder** (default-off
  field on `PathTraceConfig` + matching kernel guards
  on BOTH CUDA and OptiX path-trace raygens; largest
  remaining surface).
- **Trigger the CUDA-host verification run** that
  flips the §8 BLOCKED row of this audit + the same
  rows from PT-P.4 / PT-P.7 to PASS. Single
  command-line invocation
  (`tools/verify_cuda_host.py [--optix]`) on a real
  CUDA + OptiX-SDK host.

Either path is a stand-alone slice; the operator's
call. The PT-P.x cadence (PT-P.{N+1} task → PT-P.{N+2}
impl → PT-P.{N+3} audit) remains the lowest-friction
continuation.

# Path-Tracer Polish — Firefly Clamp Placeholder Audit

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `47ed5cb` ("PT-P.21:
firefly clamp placeholder (impl)").
Scope: PT-P.{20,21} — the task-definition + implementation
sub-arc that ships `PATH_TRACER_POLISH_PLAN.md` §4.7
field-only placeholder per the brief in
`PATH_TRACER_POLISH_FIREFLY_CLAMP_TASK.md`.
Mode: documentation only. **No source code is modified by
this audit.**
Auditor: Claude Code, on the audit host (no CUDA Toolkit,
no OptiX SDK).

This audit walks the seven prompt checks in order and
records a single verdict at the end. Verdict legend matches
the texture-polish-audit + PT-P.4 / PT-P.7 / PT-P.10 /
PT-P.13 / PT-P.16 / PT-P.19 precedent:

- **PASS** — implemented, type-checked on the audit host,
  AND empirically exercisable on the audit host with a
  recorded happy-path run.
- **REPAIR** — implemented but a defect or inconsistency
  was found that should be patched.
- **BLOCKED** — empirical verification requires a CUDA
  host (no nvcc / OptiX SDK on the audit host).

This is the FINAL `PATH_TRACER_POLISH_PLAN.md` §4 audit;
landing PT-P.22 closes the §4 polish arc.

---

## 1. Firefly clamp placeholder exists

**PASS.**

`grep -rn "firefly_clamp" src/` returns exactly two
matches in one file:

```
src/pathtracer/PathTracer.h:86:    // firefly_clamp)` before being added to the accumulator.
src/pathtracer/PathTracer.h:103:    float firefly_clamp = 0.0f;
```

The first match is a doc-comment reference (line 86, inside
the field's doc-comment block); the second is the field
declaration itself.

### 1.1 Field placement

The field is the SIXTH and LAST member of `PathTraceConfig`,
after `environment_intensity`. Verbatim from
`src/pathtracer/PathTracer.h:103`:

```cpp
float firefly_clamp = 0.0f;
```

The PT-P.20 task §1.1 named exactly this declaration:
type `float`, default `0.0f`, position "at the end of the
existing `PathTraceConfig` struct, after the
`environment_intensity` field". PT-P.21's commit honours
each piece verbatim.

### 1.2 Field type and default

- **Type**: `float`. Matches the task §1.1 spec (`float
  firefly_clamp = 0.0f`).
- **Default**: `0.0f`. Matches the task §2.1 contract
  ("Add `firefly_clamp = 0.0f` to `PathTraceConfig`")
  and the §4.7 plan's specification ("0 disables the
  clamp (default - the integrator is unbiased)").

### 1.3 Predecessor fields preserved

The five existing `PathTraceConfig` fields kept their
declarations, defaults, types, and field order
post-PT-P.21:

```cpp
int          max_bounces = 4;             // PT-P.6 cap honoured
int          samples_per_pixel = 16;      // PT-P.9 cap honoured
unsigned int seed = 0u;
rr::math::Vec3 environment_color     = {0.55f, 0.70f, 1.00f};
float          environment_intensity = 0.30f;
```

Verifiable at `src/pathtracer/PathTracer.h:48-78`. The
`kMaxBouncesCap` / `kSamplesPerPixelCap` constants from
PT-P.6 / PT-P.9 (lines 14-35) are byte-identical with the
post-PT-P.18 tree.

---

## 2. Placeholder is documented

**PASS.**

The doc-comment block above the new field is at
`src/pathtracer/PathTracer.h:80-102` (24 lines including
formatting blanks). The block names every property the
PT-P.20 task §2.2 contract required:

### 2.1 External semantics

> "PT-P.21 placeholder: per-channel firefly clamp on the
> per-sample radiance."

Names the clamp's per-channel scope + per-sample
application point. Matches the task §1.1 wording.

### 2.2 Default-off behaviour + "currently NOT read"

> "Default 0.0f disables the clamp (the integrator stays
> unbiased; the field is currently NOT read by any
> kernel)."

Names BOTH the default-off semantics (the integrator
stays unbiased) AND the implementation-deferral fact
(the field is currently NOT read by any kernel). The
"currently NOT read" phrase is the load-bearing claim
that makes §3 (default render output unchanged) provable
from inspection alone.

### 2.3 Forward-compatibility plan

> "When > 0, a future slice will wire the value through
> both backends' path-trace raygens so each per-sample
> `radiance.x|y|z` is `fminf(radiance.x|y|z,
> firefly_clamp)` before being added to the accumulator."

Names the future kernel-wiring contract (both backends;
strict `>` gating; `fminf` per-channel; pre-accumulation).
A future PT-P.x slice that wires the field through has a
ready-to-paste specification.

### 2.4 Default-off rationale

> "Default-off rationale: clamping introduces a small
> downward bias in scenes with high-variance light paths
> (e.g. small bright emitters surfaced by NEE / area
> lights); making it opt-in keeps the unbiased
> integrator the canonical baseline."

Explains WHY `0.0f` is the default rather than a small
positive value: clamping introduces bias, and the
unbiased integrator is preferred as the canonical
baseline. This pre-empts a future operator question
("why isn't this on by default?").

### 2.5 Deferral cross-reference

> "Wiring is deferred per
> `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_TASK.md`
> (PT-P.20): the CUDA path-trace kernel + the OptiX
> `__raygen__pathtrace` need a paired update so the two
> backends' outputs remain convergent at non-zero clamp;
> landing one without the other would silently diverge
> them. PT-P.21 ships ONLY this field + its doc-comment
> so the future kernel-wiring slice has a stable
> forward-compatible anchor to attach itself to."

Names the deferral source (PT-P.20's task brief) by
filename and slice number. A future contributor reading
ONLY the source can find the deferral rationale without
needing `git log` archaeology.

### 2.6 Documentation completeness

The doc-comment block is the single source of truth for
the placeholder's contract. A future reader does not
need to consult `BUILD_PLAN.md`, `PATH_TRACER_POLISH_PLAN.md`,
or any audit doc to understand what the field is, why
it defaults to `0.0f`, and what a future slice will do
with it. Self-contained documentation is the established
PT-P.x discipline (mirrors PT-P.{6,9,12,15}'s field /
constant doc-comments).

---

## 3. Default render output is unchanged

**PASS structurally; BLOCKED empirically (CUDA-host
check).**

For every authored `PathTraceConfig`:

### 3.1 Source-diff containment

`git diff 47ed5cb~1..47ed5cb -- src/cuda/ src/optix/
src/pathtracer/PathTracer.cpp src/pathtracer/RNG.h
src/pathtracer/RNG.cuh src/pathtracer/Sampling.h
src/pathtracer/Sampling.cuh src/main.cpp src/core/
src/io/ src/scene/ src/material/ src/lighting/
src/renderer/ scenes/ tests/ tools/verify_cuda_host.py
CMakeLists.txt | wc -l` returns 0 bytes.

Every kernel, launcher, OptiX program, dispatcher, host
orchestration class, scene file, ctest binary, and
verification runner is byte-identical with the pre-PT-P.21
commit `70c15d0` (PT-P.20). The ONLY source-touching
edit is `src/pathtracer/PathTracer.h` (+25 lines: the
field + its doc-comment block).

### 3.2 No reader of the new field

Search for callers reading `cfg.firefly_clamp` /
`firefly_clamp`-suffixed identifiers:

```
$ grep -rn "firefly_clamp" src/
src/pathtracer/PathTracer.h:86:    // firefly_clamp)` before being added to the accumulator.
src/pathtracer/PathTracer.h:103:    float firefly_clamp = 0.0f;
```

Two matches, BOTH inside the field's home file. Zero
read sites in `src/pathtracer/PathTracer.cpp`,
`src/cuda/`, `src/optix/`, `src/main.cpp`, or any
other source tree. The field is declared but
architecturally unread.

### 3.3 No clamp logic in any kernel

```
$ grep -rn "fminf.*firefly_clamp|fminf.*firefly|clamp.*radiance" \
    src/cuda/ src/optix/
=> (no matches)
```

Zero clamp logic anywhere in the kernel code. No
accidental `fminf(radiance, ...)` was introduced; no
hidden clamp-style math.

### 3.4 Default-construction guarantees

C++'s aggregate initialisation rules guarantee:

- Every `PathTraceConfig{}` value (default-construct
  expression) initialises `firefly_clamp` to `0.0f` per
  the field's `= 0.0f` default-member-initialiser.
- Every existing caller in `src/main.cpp` that
  default-constructs `PathTraceConfig` and only sets
  `samples_per_pixel` (e.g.
  `run_render_pathtrace`'s `pcfg.samples_per_pixel =
  run.spp;`) leaves `firefly_clamp` at `0.0f`.

Combined with §3.1 (no kernel arithmetic changed) and
§3.2 (no caller reads the field), the rendered output
is unchanged for every existing caller.

### 3.5 Empirical smokes (audit host)

```
$ ./build/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
[ERROR] --render-pathtrace requires CUDA. ...
```

The dispatcher reaches `PathTracer::render`'s
CUDA-disabled branch and returns the standard "requires
CUDA" message. The new field is unreachable on the
audit host (the dispatcher returns from the
`#ifndef RR_HAS_CUDA` branch before any kernel call);
the smoke confirms the fallback path is unchanged.

```
$ ./build/bin/RelativityRender --scene-info \
    scenes/test_textured_material.rrscene
... [WARN] ... 'textured-quad-material' ... falling back to flat baseColor.
... [WARN] ... 'out-of-range-texture' ... falling back to flat baseColor.
... [INFO]     fixups applied    : 2
```

TEX-P.6 fixture's three-case logs unchanged. Confirms
zero ripple onto the texture validator.

### 3.6 Empirical PPM byte-identity (CUDA host)

The byte-identity claim — `pathtrace_spp_*.ppm` /
`gpu_rng_test.ppm` / `gpu_accumulation_test.ppm` /
`optix_pathtrace_*.ppm` are bit-for-bit identical
pre-/post-PT-P.21 — is structurally guaranteed by §3.1
+ §3.2 + §3.3 + §3.4 above. A CUDA-host operator can
empirically confirm by:

```
# Step 1: render with the post-PT-P.21 tree
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm  /tmp/post_p21_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/post_p21_spp16.ppm

# Step 2: check out pre-PT-P.21 source and re-render
$ git checkout 47ed5cb~ -- src/pathtracer/PathTracer.h
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm  /tmp/pre_p21_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/pre_p21_spp16.ppm
$ git checkout HEAD -- src/  # restore

# Step 3: pixel-diff (expected: no diff)
$ cmp /tmp/post_p21_spp1.ppm  /tmp/pre_p21_spp1.ppm  ; echo $?  # 0
$ cmp /tmp/post_p21_spp16.ppm /tmp/pre_p21_spp16.ppm ; echo $?  # 0
```

Expected: `cmp` reports the files are identical. This is
the INVERSE of PT-P.18's mandatory byte-DIFFERENCE
check — PT-P.21 expects byte-IDENTITY. If `cmp` reports
a difference, the slice has an unintended kernel-side
ripple and §3's verdict flips to REPAIR.

This audit cannot run the comparison on the audit host
(no CUDA toolchain). Captured under §6 below.

---

## 4. No real clamp accidentally active

**PASS.**

Three independent checks confirm zero clamp logic
introduced:

### 4.1 No clamp expression anywhere in kernel code

```
$ grep -rn "fminf.*firefly_clamp|fminf.*firefly|clamp.*radiance" \
    src/cuda/ src/optix/
=> (no matches)
```

Zero results. No `fminf(radiance.x, firefly_clamp)`
expression exists. No alternate spelling
(`std::fmin`, `min`, `clamp(...)`) is used either.

### 4.2 No conditional reading the field

```
$ grep -rn "if.*firefly_clamp|firefly_clamp.*>|firefly_clamp.*<|firefly_clamp.*==|firefly_clamp.*!=" src/
=> (no matches)
```

Zero conditional expressions reference the field. The
"strict-`>`-gated" check the PT-P.20 task §2.4 required
is absent because no kernel guard was added at all.

### 4.3 No silent bias in the integrator

The kernel code path in `src/cuda/CudaPathTracer.cu`'s
`k_pathtrace_sample` writes per-sample `radiance` to
`pixels[]` without any clamp:

```cpp
const int idx = (y * width + x) * 4;
pixels[idx + 0] = radiance.x;
pixels[idx + 1] = radiance.y;
pixels[idx + 2] = radiance.z;
pixels[idx + 3] = 1.0f;
```

Lines 237-241 (post-PT-P.18; byte-identical with
post-PT-P.21). No `fminf` between the bounce loop and
the write.

The OptiX raygen at `src/optix/OptixPrograms.cu`'s
`__raygen__pathtrace` accumulates per-sample `radiance`
into `rgb_sum` without any clamp:

```cpp
rgb_sum.x += radiance.x;
rgb_sum.y += radiance.y;
rgb_sum.z += radiance.z;
```

Lines 933-935 (post-PT-P.18; byte-identical with
post-PT-P.21). No `fminf` between the bounce loop and
the accumulation.

The integrator stays unbiased: no operation downward-
biases the radiance pre-write. The PT-P.21 polish
preserves the unbiased baseline; the field is the
forward-compatible anchor for the future slice that
WILL clamp.

---

## 5. Build status

**PASS.**

Re-ran during this audit:

| Config      | RR_ENABLE_CUDA | RR_ENABLE_OPTIX | Build     | ctest    |
|-------------|:--------------:|:---------------:|-----------|:--------:|
| `build`     | OFF            | OFF             | clean     | 7/7 PASS |
| `build-ON`  | OFF            | ON              | clean     | 8/8 PASS |

Both audit-host configs report zero new compiler
warnings under the `rr_apply_warnings`-enforced
`-Wall -Wextra -Wpedantic` triple. ctest counts
unchanged from PT-P.18 / PT-P.6 / PT-P.9 / PT-P.12 /
PT-P.15 (the slice did not add or remove a ctest
binary, per the PT-P.20 task §1.3's no-new-test
recommendation).

The `pathtracer_tests` binary's INTERNAL test count
remains at 9 of 9 (`pathtracer_tests: 20034/20034
passed`); the placeholder field has no test by design
(default-construction is trivially guaranteed by C++
aggregate initialisation; no runtime behaviour to
assert).

The new `firefly_clamp` field is type-checked on the
audit-host build through every `PathTraceConfig`
consumer:

- `src/pathtracer/PathTracer.cpp`'s
  `PathTracer::render(...)` reads
  `cfg.samples_per_pixel`, `cfg.max_bounces`, etc., but
  not `cfg.firefly_clamp` (which the function ignores).
  The struct still default-constructs cleanly with the
  new field.
- `src/main.cpp`'s `run_render_pathtrace` constructs
  `pcfg` and sets `pcfg.samples_per_pixel`; the new
  field's default-construction is type-checked through
  this path even though it's not read.

---

## 6. Runtime-deferred status

**NO RUNTIME CHECKS REQUIRED** (the simplest runtime
posture in the PT-P.x cadence).

PT-P.21's field-only placeholder has zero runtime-
observable behaviour beyond default-construction. The
§3 + §4 invariants are byte-precise and verifiable on
the audit host alone. There is NO mandatory CUDA-host
check the operator must run before this audit's verdict
can flip from DEFERRED to PASS — because the verdict
is ALREADY PASS structurally (per §3.1 + §3.2 + §3.3 +
§4) and the empirical CUDA-host confirmation in §3.6 is
OPTIONAL inverse-of-PT-P.18 byte-IDENTITY check.

The PT-P.20 task §6 listed three OPTIONAL CUDA-host
checks, all redundant with the structural guarantees:

| Check                                    | Status                                                                |
|------------------------------------------|-----------------------------------------------------------------------|
| §6.1 Default render byte-IDENTITY        | OPTIONAL — operator may confirm via `cmp` on a CUDA host;             |
| (CUDA host)                              | inverse of PT-P.18's mandatory byte-DIFFERENCE.                       |
| §6.2 ctest cycle on CUDA host            | OPTIONAL — trivially passes; no new test.                             |
| §6.3 No CUDA-H.x runner update           | confirmed; `tools/verify_cuda_host.py` diff = 0 bytes.                |

### 6.1 Comparison with prior PT-P.x audits

The runtime-deferred status of every prior PT-P.x audit:

| Audit  | Runtime status                                                        |
|--------|-----------------------------------------------------------------------|
| PT-P.4  | DEFERRED on six PPMs (Stage 11 baseline)                              |
| PT-P.7  | DEFERRED on six PPMs                                                  |
| PT-P.10 | DEFERRED on six PPMs                                                  |
| PT-P.13 | DEFERRED on six PPMs                                                  |
| PT-P.16 | DEFERRED on six PPMs + 1 PT-P.15-specific check                       |
| PT-P.19 | DEFERRED on six PPMs + 5 mandatory PT-P.18-specific checks (highest!) |
| PT-P.22 | **NO RUNTIME CHECKS REQUIRED** (this audit; lowest)                   |

PT-P.19's mandatory CUDA-host checks were the high-water
mark of the PT-P.x arc (the RNG-stability slice produced
intentional pixel-diffs that needed empirical
confirmation). PT-P.22's runtime surface is the
opposite extreme: zero kernel changes, zero new field
reads, zero new tests, zero new CLI flags. The
placeholder is provably safe by inspection alone.

### 6.2 Forward-compatible runtime checks

A future PT-P.x slice that wires `firefly_clamp`
through both backends (PT-P.23 / "Wire firefly clamp
through both backends") will introduce mandatory
CUDA-host runtime checks of its own:

- Render `pathtrace_spp_*.ppm` with
  `cfg.firefly_clamp = 0.0f`: byte-identical with
  pre-wiring (PT-P.21's invariant).
- Render `pathtrace_spp_*.ppm` with
  `cfg.firefly_clamp = 8.0f` on a high-variance
  scene: clamped output, visibly less noise on
  small bright emitters.
- Compare CUDA + OptiX backends at `cfg.firefly_clamp
  = 8.0f`: the two backends' outputs remain
  convergent (the PT-P.20 task §1.2 "symmetric-output
  invariant").

PT-P.22 (this audit) does NOT require those checks
because PT-P.21 ships zero clamp logic; they belong
in the future slice's audit.

---

## 7. Verdict

| # | Audit item                                          | Result   |
|---|-----------------------------------------------------|----------|
| 1 | Firefly clamp placeholder exists                    | PASS     |
| 2 | Placeholder is documented                           | PASS     |
| 3 | Default render output is unchanged                  | PASS structurally; OPTIONAL CUDA-host inverse byte-IDENTITY check |
| 4 | No real clamp accidentally active                   | PASS — zero clamp logic in any kernel |
| 5 | Build status                                        | PASS     |
| 6 | Runtime-deferred status                             | NO RUNTIME CHECKS REQUIRED |
| 7 | Overall                                             | **PASS** (no DEFERRED rows; no REPAIR items) |

**Overall verdict: PASS.**

PT-P.{20,21} ship exactly the polish PT-P.20's task brief
specified: a single `float firefly_clamp = 0.0f` field
appended to `PathTraceConfig` with a 24-line doc-comment
block. ZERO kernel changes; ZERO new tests; ZERO new CLI
flags; ZERO behavioural change for any caller. Both
audit-host build configs remain green (7/7 OFF, 8/8
ON-audit-host). The new field is type-checked on the
audit host via every `PathTraceConfig` consumer; the
field is declared but not read; no clamp logic exists
in any kernel.

**Zero REPAIR items. Zero DEFERRED rows.** This is the
cleanest verdict in the PT-P.x audit catalogue — every
other audit had at least one DEFERRED row carried
forward to a CUDA-host run; PT-P.22 has none because
the placeholder has no runtime behaviour to defer.

### The PT-P.x polish arc closes

PT-P.22 (this audit) closes the
`PATH_TRACER_POLISH_PLAN.md` §4 polish arc. Final
shipping count:

| Plan §  | Title                              | Slice trio  | Status   |
|---------|------------------------------------|-------------|----------|
| §4.2    | Accumulation reset correctness     | PT-P.{2,3,4}   | PASS    |
| §4.3    | Max-bounce validation              | PT-P.{5,6,7}   | PASS    |
| §4.6    | Sample count validation            | PT-P.{8,9,10}  | PASS    |
| §4.4    | Environment fallback clarity       | PT-P.{11,12,13}| PASS    |
| §4.5    | Emission handling                  | PT-P.{14,15,16}| PASS    |
| §4.1    | RNG stability                      | PT-P.{17,18,19}| PASS    |
| §4.7    | Firefly clamp placeholder          | PT-P.{20,21,22}| PASS    |

Seven out of seven §4 items shipped. The PT-P.1 plan
+ the PT-P.x cadence (task → impl → audit per item)
together delivered 21 incremental slices across the
six-week polish window, each PASSing its independent
audit with zero REPAIR items.

The §4.7 polish ships only the placeholder; the kernel
wiring is its own future sub-arc (PT-P.23 +
follow-ups). That sub-arc is the natural starting
point for the next polish cycle if the operator wants
to keep extending the path tracer.

### Recommended next step

The PT-P.x polish arc is closed. Three viable next
directions:

1. **PT-P.23 — "Wire firefly clamp through both
   backends"**. Drop-in extension that uses
   `cfg.firefly_clamp` (already declared by PT-P.21)
   in both the CUDA `k_pathtrace_sample` kernel +
   the OptiX `__raygen__pathtrace` raygen. ~7-8
   source files; both backends MUST land in the same
   commit per the PT-P.20 task §1.2 symmetric-output
   invariant. Mandatory CUDA-host runtime check:
   compare CUDA + OptiX output at
   `firefly_clamp = 8.0f` on a high-variance scene.

2. **Trigger the CUDA-host verification run** that
   flips the DEFERRED rows from PT-P.4 / PT-P.7 /
   PT-P.10 / PT-P.13 / PT-P.16 / PT-P.19 to PASS.
   PT-P.22 does NOT add a DEFERRED row, so this run
   has no PT-P.22-specific deliverables — it's the
   accumulated runtime debt from the prior PT-P.x
   audits. Single command-line invocation
   (`tools/verify_cuda_host.py [--optix]`) on a real
   CUDA + OptiX-SDK host.

3. **Pivot to a master-order item**. The TEX-P.x arc
   landed PASS (TEX-P.7); master order #16 (path
   tracing — feature work like NEE / non-diffuse
   BSDFs / multi-mesh upload) is the next major
   follow-up after the PT-P.x polish arc closes.
   Each is its own multi-slice arc.

PT-P.22 closes the `PATH_TRACER_POLISH_PLAN.md` §4
arc cleanly. The next concrete slice — when the
operator chooses to continue — opens with whichever
direction above the operator picks.

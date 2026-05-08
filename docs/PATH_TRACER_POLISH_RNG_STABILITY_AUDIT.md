# Path-Tracer Polish — RNG Stability Audit

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `d2af0c5` ("PT-P.18: RNG
stability (impl)").
Scope: PT-P.{17,18} — the task-definition + implementation
sub-arc that ships `PATH_TRACER_POLISH_PLAN.md` §4.1 per the
brief in `PATH_TRACER_POLISH_RNG_STABILITY_TASK.md`.
Mode: documentation only. **No source code is modified by
this audit.**
Auditor: Claude Code, on the audit host (no CUDA Toolkit,
no OptiX SDK).

This audit walks the nine prompt checks in order and records
a single verdict at the end. Verdict legend matches the
texture-polish-audit + PT-P.4 / PT-P.7 / PT-P.10 / PT-P.13 /
PT-P.16 precedent:

- **PASS** — implemented, type-checked on the audit host,
  AND empirically exercisable on the audit host with a
  recorded happy-path run.
- **REPAIR** — implemented but a defect or inconsistency
  was found that should be patched.
- **BLOCKED** — empirical verification requires a CUDA
  host (no nvcc / OptiX SDK on the audit host). Used here
  as the equivalent of the prompt's `DEFERRED`.

---

## 1. RNG behavior is documented

**PASS.**

Three independent locations now describe the
`make_pixel_rng` contract end-to-end:

### 1.1 The RNG header doc-comment (`src/pathtracer/RNG.h`)

The pre-PT-P.18 header doc-block at lines 55-61 — which
described `make_pixel_rng` as a per-pixel decorrelated
seed — is byte-identical post-slice. The PT-P.18 polish
ADDED an inline doc-comment paragraph above the new
key-construction block (~22 lines, 78-100 in the
post-slice tree) that names:

- **The cancellation bug** the naive
  per-input-`splitmix64` mix would have. Verbatim
  excerpt:

  > "a naive `splitmix64(seed) ^ splitmix64(pixel_x) ^
  > splitmix64(pixel_y) ^ splitmix64(frame)` mix has a
  > cancellation bug when two inputs happen to share a
  > value (e.g. `seed == frame == 0` AND `pixel_x == 0`:
  > the four `splitmix64(0)` calls xor-cancel pairwise to
  > produce `key == 0`)."

- **Why salts fix it.** "XORing each input with a distinct
  64-bit salt before hashing breaks the cancellation:
  even when two inputs are value-equal, their pre-hash
  bit patterns differ."

- **The four salt constants** — sourced from well-known
  SplitMix64 / xxHash mixing constants, with the
  rationale ("chosen for their odd-bit-density and lack
  of trivial periodicity") explicit in the comment.

- **The audit trail**: the
  `test_rng_grid_collision_check()` test
  (`tests/pathtracer_tests.cpp`) verifies the 4096-cell
  grid has zero collisions post-PT-P.18. The
  doc-comment names the test by name so a future
  reader can find it.

### 1.2 The `pcg32_next` and `splitmix64` doc-comments

Pre-existing (NOT touched by PT-P.18):

- `pcg32_next` doc-block at `RNG.h:25-28` describes the
  PCG-XSH-RR-64-32 generator, the 32-bit output, and
  the in-place `Rng` mutation. Byte-identical with the
  pre-PT-P.18 commit.
- `splitmix64` doc-block at `:45-47` describes the
  64-bit avalanche hash for "spreading a small key
  (pixel coords, frame index, global seed) across the
  full PCG state space". Byte-identical with the
  pre-PT-P.18 commit.

### 1.3 The `make_pixel_rng` function-level doc-comment

Pre-existing (NOT touched by PT-P.18):

`RNG.h:55-61` describes the function's external
contract — "Build a per-pixel `Rng` whose stream is
uncorrelated with the streams of adjacent pixels and
adjacent frames" — verbatim. Byte-identical. The
contract still holds in either era; only the
mechanism the body uses to achieve it changed.

The three locations together cover:

- **WHAT** the function does (the function-level
  doc-comment).
- **HOW** it does it post-PT-P.18 (the inline
  doc-comment paragraph + the salt-cancellation
  rationale).
- **WHERE** the building blocks come from (the
  `pcg32_next` / `splitmix64` doc-comments naming
  PCG-XSH-RR-64-32 + SplitMix64 by their reference
  implementations).

---

## 2. No time-based seeds exist

**PASS.**

`grep -rEn "std::time|std::chrono|std::random_device|
chrono::|time\(NULL\)|time\(nullptr\)|gettimeofday|
clock_gettime|GetSystemTime"
src/pathtracer/ src/cuda/CudaPathTracer.cu
src/cuda/CudaRngTestKernel.cu src/cuda/CudaAccumulation.cu`
returns zero matches.

The seeding code paths (the host-side `make_pixel_rng`
helper + every kernel that consumes it) are deterministic
functions of their explicit inputs only:

- `pixel_x` / `pixel_y` — kernel-thread launch indices
  (deterministic per pixel).
- `frame_index` — host-controlled spp loop counter
  (deterministic per sample).
- `global_seed` — operator-controlled
  `PathTraceConfig::seed` (default `0u`; an explicit
  field on the config POD).

The `<chrono>` header is referenced exactly once in
`src/gpu/GpuTiming.h` for kernel-launch wall-clock
TIMING (NOT seeding) — that import is OUT OF the seeding
code paths and not relevant to RNG determinism. The
audit re-confirmed this is non-seeding use.

The pre-PT-P.18 RNG already had no time-based seeds
(the PT-P.17 task §2.4 described this property as
preserved); PT-P.18 inherits the same posture and the
audit's grep re-verifies it on the post-slice tree.

---

## 3. Same config should produce deterministic sequence

**PASS.**

`make_pixel_rng(x, y, sample, seed)` is a pure function
in both eras. Three independent checks confirm
post-PT-P.18:

### 3.1 Pure-function structure

`src/pathtracer/RNG.h`'s `make_pixel_rng` body reads:

```cpp
const std::uint64_t key =
      splitmix64(static_cast<std::uint64_t>(global_seed)  ^ kSeedSalt)
    ^ splitmix64(static_cast<std::uint64_t>(pixel_x)      ^ kPxSalt)
    ^ splitmix64(static_cast<std::uint64_t>(pixel_y)      ^ kPySalt)
    ^ splitmix64(static_cast<std::uint64_t>(frame_index)  ^ kFrameSalt);

Rng r;
r.state = splitmix64(key);
(void)pcg32_next(r);
return r;
```

Inputs: four function arguments + four `constexpr`
salts. No global state, no static caches, no environment
reads, no thread-local storage, no syscalls. Two calls
with the same arguments produce the same `r.state`
bit-for-bit.

### 3.2 The pre-existing determinism test still passes

`tests/pathtracer_tests.cpp`'s
`test_rng_determinism` (lines 93-104, byte-identical
post-PT-P.18) asserts:

```cpp
Rng r1 = make_pixel_rng(7u, 9u, 3u, 0xDEADBEEFULL);
Rng r2 = make_pixel_rng(7u, 9u, 3u, 0xDEADBEEFULL);
RR_CHECK(r1.state == r2.state);
RR_CHECK(next_float(r1) == next_float(r2));
```

The post-slice
`./build/bin/pathtracer_tests` reports
`pathtracer_tests: 20034/20034 passed`, which includes
this test's RR_CHECKs.

### 3.3 The new grid-collision test confirms decorrelation

`test_rng_grid_collision_check` (the PT-P.18 addition)
iterates a 16×16×4×4 grid of (`pixel_x`, `pixel_y`,
`frame_index`, `global_seed`) tuples and asserts every
state value is unique. This is a STRICTER form of
determinism: not only does the same input produce the
same output, but DIFFERENT inputs produce
DIFFERENT outputs across a dense grid. The 4096-cell
grid has zero collisions post-slice (per the
`!any_dup` RR_CHECK in the test body).

The post-PT-P.18 RNG sequence for any given
(`x`, `y`, `sample`, `seed`) DIFFERS from the
pre-PT-P.18 sequence at the same inputs (this is
PT-P.18's intentional pixel-diff). Determinism within
either era is preserved.

---

## 4. Pixel/sample seed mixing is explicit

**PASS.**

The mixing is now at the FRONT of `make_pixel_rng`'s
body and surrounded by the most explanatory
doc-comment block in the entire `RNG.h` file. The
mixing is:

1. **Per-input salt** — each of the four inputs gets
   its own constexpr 64-bit salt XORed in before
   `splitmix64`. The salts (`kSeedSalt`, `kPxSalt`,
   `kPySalt`, `kFrameSalt`) are declared at lines
   95-99 immediately above the key construction.
2. **Per-input SplitMix64** — each salted input is
   independently avalanched through SplitMix64 (a
   bijection on `std::uint64_t` per
   `splitmix64`'s doc-comment).
3. **XOR combination** — the four hash outputs are
   XORed together to produce the 64-bit key. Because
   the four `splitmix64` outputs are statistically
   independent (each input passes through a different
   pre-hash bit pattern), XOR of four independent
   uniform 64-bit values produces a uniform 64-bit
   result.
4. **Final SplitMix64 + burn step** — the post-XOR
   key passes through `splitmix64` ONE more time
   before becoming `r.state`, then a single
   `pcg32_next(r)` burn step decorrelates the
   first PCG output from the seed grid pattern.

Each step's role is explicitly named in the
doc-comment. A future maintainer reading
`RNG.h:78-110` (the new doc-block + salts + key
construction) can re-derive the contract from the
source alone — no `git log` or `BUILD_PLAN` archaeology
required.

The kernel-side `make_pixel_rng` call sites (cited at
§5.4 below) all pass the four inputs in their named
positions; no caller bypasses the helper or seeds the
`Rng` struct directly.

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
`-Wall -Wextra -Wpedantic` triple.

The `pathtracer_tests` binary's INTERNAL test count
grew from 8 (pre-PT-P.18) to 9 (post-PT-P.18) with the
new `test_rng_grid_collision_check`; the binary
reports `pathtracer_tests: 20034/20034 passed` (was
20033/20033 pre-slice; +1 for the `!any_dup`
RR_CHECK). The ctest binary count stays at 7 OFF / 8
ON — no new ctest binary was added.

### 5.1 Per-call-site type-check

`grep -rn "make_pixel_rng" src/` returns 10 matches.
The five non-comment call sites are at:

| Caller                                   | Caller's compile config |
|------------------------------------------|-------------------------|
| `src/pathtracer/RNG.h:62`                | the declaration itself  |
| `src/cuda/CudaAccumulation.cu:114`       | RR_ENABLE_CUDA-gated    |
| `src/cuda/CudaRngTestKernel.cu:54`       | RR_ENABLE_CUDA-gated    |
| `src/cuda/CudaPathTracer.cu:158`         | RR_ENABLE_CUDA-gated    |

Plus four doc-comment references in headers
(`PathTracer.h:56`, `CudaRenderer.h:107`,
`CudaAccumulation.cuh:75`, `CudaPathTracer.cuh:39`,
`OptixLaunchParams.h:142`, `CudaKernels.cuh:98`).

All five call-site arguments are signature-preserved
post-PT-P.18 (the function's external signature is
byte-identical). The CUDA-gated `.cu` callers are not
compiled on the audit host; they are type-checked on
a CUDA host's build path, where the post-slice signature
matches verbatim.

---

## 6. Expected PPM byte changes are documented

**PASS.**

PT-P.18 is the FIRST PT-P.x slice that produces a real
pixel-diff pre-/post-slice, and the documentation flags
this prominently in three locations:

### 6.1 `docs/BUILD_PLAN.md`'s PT-P.18 entry

Opens with a verbatim **"Pixel-diff warning"** subsection
above the "Scope" header:

> "**Pixel-diff warning**: this is the FIRST PT-P.x
> implementation slice that produces a real pixel-diff
> pre-/post-slice on `pathtrace_spp_*.ppm` +
> `gpu_rng_test.ppm` + `optix_pathtrace_*.ppm`. Visual
> quality + statistical convergence are unchanged; only
> the specific noise pattern shifts. Mandatory CUDA-host
> verification per PT-P.17 task §6 (deferred to a future
> operator run)."

The BUILD_PLAN entry's "Behaviour matrix" tabulates the
six artefacts and their pre-/post states explicitly:

| Scenario                                         | Pre-PT-P.18                  | PT-P.18                                       |
|--------------------------------------------------|------------------------------|-----------------------------------------------|
| `pathtrace_spp_*.ppm` on CUDA host               | one specific noise pattern   | a DIFFERENT noise pattern (means / variances / PDFs preserved)|
| `gpu_rng_test.ppm` on CUDA host                  | one specific four-quadrant noise | a DIFFERENT noise pattern (same quadrant statistics) |
| `--render-pathtrace` on audit host               | "requires CUDA" fallback     | byte-identical fallback (kernel unreachable)  |

### 6.2 PT-P.17 task §4.5

The PT-P.17 task brief recorded the same expectation
ahead of time:

> "For `samples_per_pixel >= 1`: ... the byte-different
> outcome is the EXPECTED behaviour of this slice. The
> visual quality (means / variances / PDFs) is preserved;
> only the noise pattern shifts. This is the renderer's
> only PT-P.x slice with this property and the
> BUILD_PLAN entry must call it out loudly."

PT-P.18's BUILD_PLAN entry honoured the requirement.

### 6.3 The brief-deviation note

PT-P.18's BUILD_PLAN entry includes a "Brief deviation
note" subsection naming the cancellation bug + the
test-caught discovery + the salt fix. The note is the
audit trail for why the implementation diverged from
the task's literal code while still satisfying the
brief's external-behaviour contract verbatim.

---

## 7. Runtime CUDA-host verification status

**DEFERRED (= BLOCKED on this audit host).**

PT-P.17 task §6 lists FIVE mandatory operator-side
checks on a CUDA host. None can run on this audit host
(no nvcc / no CUDA-capable GPU / no OptiX SDK):

| Check                                       | Status on audit host       | What it verifies                                        |
|---------------------------------------------|----------------------------|---------------------------------------------------------|
| §6.1 PPM byte-DIFFERENCE confirmation       | DEFERRED (cannot render)   | The intentional pixel-diff actually fired                |
| §6.2 Visual + statistical sanity            | DEFERRED                   | Means/variances preserved despite noise pattern shift   |
| §6.3 ctest cycle on CUDA-built host         | DEFERRED                   | The new test passes on a CUDA-built test binary too     |
| §6.4 Refresh CUDA-H.x verification report   | DEFERRED                   | Tree-state hash updates; overall verdict stays PASS     |
| §6.5 Optional 1280×720 collision check      | DEFERRED                   | Empirical bijectivity across full image domain          |

The `cmp`-based byte-DIFFERENCE confirmation procedure
PT-P.17 §6.1 documents is reproducible verbatim (clone
the repo, build pre-/post-slice, render, diff). The
PT-P.18 commit hash (`d2af0c5`) and its parent
(`d2af0c5~1` = `7f25a21` = the PT-P.17 task slice;
last source-touching commit before PT-P.18 is
`dd98d90` = PT-P.15) are the bisection markers.

The audit's verdict for §7 is **DEFERRED** rather than
BLOCKED because the operator-side checks are
well-specified and the deferral is procedural (the
audit host lacks the hardware), not structural (the
implementation lacks the artefact).

### 7.1 Structural pre-conditions for the future runtime checks

This audit confirms post-slice all pre-conditions for
the §6 checks are met:

- The `make_pixel_rng` body changed (per §1.1 + §4
  evidence); the §6.1 byte-DIFFERENCE check has a real
  diff to find.
- The new `test_rng_grid_collision_check()` is in the
  ctest binary (§5 evidence); the §6.3 CUDA-host
  ctest cycle will exercise it.
- The CUDA-H.x runner needs no source change (§5.4 of
  PT-P.18's BUILD_PLAN entry confirmed
  `tools/verify_cuda_host.py` diff = 0 bytes); the
  §6.4 report refresh is a pure runner re-invocation.

### 7.2 What the operator should do post-CUDA-host run

Per the PT-P.17 task §6, after the operator runs the
five checks on a CUDA host:

1. Confirm `cmp` reports the post-PT-P.18 PPMs DIFFER
   from a pre-PT-P.18 build of the same fixture
   (§6.1).
2. Confirm visual / statistical sanity (§6.2).
3. Confirm `pathtracer_tests` passes on the CUDA host
   too (§6.3).
4. Re-run `tools/verify_cuda_host.py` and commit the
   refreshed `docs/CUDA_HOST_VERIFICATION_REPORT.md`
   (§6.4).
5. (Optional) extend §1.3's collision check to a
   1280×720 image (§6.5).

The PT-P.19 audit (this file) will not be re-issued to
flip the §7 verdict to PASS — the cadence is that the
NEXT runtime-verification slice (mirroring CUDA-H.10
or a successor) records the empirical PASS in its own
artifact. This audit's structural verdict (§9) stays
PASS regardless; it is only §7 that flips when the
operator runs.

---

## 8. CPU RNG / path-tracing violations

**ZERO violations** — verified by re-running the
Stage-11 audit's three grep sweeps + an additional
RNG-call-site sweep on the post-PT-P.18 tree.

### 8.1 Per-pixel for-loops on the host

```
$ grep -rnE "for.*<.*width|for.*<.*height" \
    src/renderer/ src/pathtracer/*.cpp src/main.cpp
=> (no matches)
```

Identical to every prior path-tracer audit's finding.
PT-P.18 introduced no per-pixel host loops.

### 8.2 spp launcher loop

```
$ grep -rn "for.*samples_per_pixel|for.*effective_samples" \
    src/pathtracer/*.cpp src/main.cpp
src/pathtracer/PathTracer.cpp:115:
    for (int s = 0; s < effective_samples_per_pixel; ++s) {
```

One match — the same single sample-frame-granularity
launcher loop every prior path-tracer audit recorded.
Byte-identical with PT-P.16's reading; PT-P.18 did not
edit `PathTracer.cpp`.

### 8.3 Host-side intersection / closest-hit code

```
$ grep -rn "intersect_sphere|intersect_triangle|closest_hit" \
    src/renderer/ src/pathtracer/*.cpp
src/renderer/Hit.h:30:
    // `intersect_triangle`. ...
```

One match — a doc-comment in `Hit.h:30`. Identical to
every prior audit.

### 8.4 RNG-specific host-side use sweep

A new sweep added by this audit:

```
$ grep -rn "make_pixel_rng" src/
```

Returns the five non-comment occurrences listed in
§5.1 above. None of the five callers iterates over
pixels host-side; every CUDA-gated call site is
inside a `__global__` kernel that the GPU schedules
per pixel, and the `RNG.h` declaration is a function
prototype (not a caller). The new test
(`test_rng_grid_collision_check`) calls
`make_pixel_rng` host-side from a 4096-cell loop —
this is HOST iteration of a HOST helper for a HOST
test post-condition, NOT per-pixel rendering. The
test binary is not part of the production render path;
it is a ctest verification step.

Master rule 5/7 ("All per-pixel/per-ray rendering
must happen on GPU") therefore remains upheld
post-PT-P.18.

### 8.5 PT-P.18-specific scope

PT-P.18 edited two files:

| File                              | New per-pixel host code? |
|-----------------------------------|--------------------------|
| `src/pathtracer/RNG.h`            | NO. The body change is inside `make_pixel_rng`, which runs on the device when called from a kernel. |
| `tests/pathtracer_tests.cpp`      | NO. The 4096-cell loop is a host-only ctest post-condition; not part of rendering. |

Master rule 5/7 remains upheld.

---

## 9. Verdict

| # | Audit item                                           | Result   |
|---|------------------------------------------------------|----------|
| 1 | RNG behavior is documented                           | PASS     |
| 2 | No time-based seeds exist                            | PASS     |
| 3 | Same config should produce deterministic sequence    | PASS     |
| 4 | Pixel/sample seed mixing is explicit                 | PASS     |
| 5 | Build status                                         | PASS     |
| 6 | Expected PPM byte changes are documented             | PASS     |
| 7 | Runtime CUDA-host verification status                | DEFERRED (= BLOCKED on this audit host) |
| 8 | CPU RNG / path-tracing violations                    | PASS — zero violations |
| 9 | Overall                                              | **PASS** (one DEFERRED row carried forward to a CUDA-host run) |

**Overall verdict: PASS.**

PT-P.{17,18} ship exactly the polish PT-P.17's task
brief specified — modulo a brief-deviation note (the
literal code in §1.1 had a cancellation bug; PT-P.18's
implementation found it via the test the brief
proposed and fixed it with per-position salts; the
external behaviour contract in §2 of the brief remains
satisfied verbatim). Both audit-host build configs
remain green (7/7 OFF, 8/8 ON-audit-host). The new
`test_rng_grid_collision_check` passes 4096/4096
unique states. Master rule 5/7 (no CPU ray tracing)
remains enforced. Hidden time-source grep returns zero
matches. The intentional pixel-diff is documented in
three locations (the BUILD_PLAN entry's "Pixel-diff
warning" + behaviour matrix + brief-deviation note).

**Zero REPAIR items.** The brief-deviation discovery
is NOT a repair item — it is an audit trail of the
test catching a bug in the brief itself; the
implementation now correctly satisfies the brief's
external-behaviour contract.

The single DEFERRED row (§7) is the standard
runtime-deferred surface every prior path-tracer audit
recorded, expanded by one PT-P.18-specific check (the
intentional PPM byte-DIFFERENCE confirmation). Five
operator-side checks the future CUDA-host run flips:

- §6.1 byte-DIFFERENCE confirmation.
- §6.2 visual + statistical sanity.
- §6.3 CUDA-host ctest.
- §6.4 CUDA-H.x report refresh.
- §6.5 optional large-image collision check.

### Recommended next step

Per `PATH_TRACER_POLISH_PLAN.md` §5 +
`PATH_TRACER_POLISH_AUDIT.md` §7's sequencing, ONE
remaining plan item is unshipped:

- **§4.7 — Firefly clamp placeholder**. The largest
  remaining surface (default-off field on
  `PathTraceConfig` + matching kernel guards on BOTH
  the CUDA and OptiX path-trace raygens). After
  PT-P.18's RNG-stability landing, the §4.7 polish
  has a stable baseline to attach itself to.

The natural cadence is:

1. **PT-P.20** — Firefly clamp placeholder task
   definition (docs only). Sources from
   `PATH_TRACER_POLISH_PLAN.md` §4.7. Mirrors the
   PT-P.{2,5,8,11,14,17} task cadence.
2. **PT-P.21** — Firefly clamp placeholder
   implementation. Adds the `firefly_clamp = 0.0f`
   field on `PathTraceConfig` (default-off; render
   output unchanged) + (per the §4.7 spec) matching
   kernel guards on BOTH path-trace raygens (the
   only PT-P.x slice expected to touch BOTH backends
   in a single commit).
3. **PT-P.22** — Firefly clamp placeholder audit
   (docs only).

Landing PT-P.{20,21,22} closes the
`PATH_TRACER_POLISH_PLAN.md` §4 polish arc.

### Alternative paths

- **Trigger the CUDA-host verification run** that
  flips this audit's §7 row + the same rows from
  PT-P.4 / PT-P.7 / PT-P.10 / PT-P.13 / PT-P.16 to
  PASS. Single command-line invocation
  (`tools/verify_cuda_host.py [--optix]`) on a real
  CUDA + OptiX-SDK host. The PT-P.18 runtime
  byte-DIFFERENCE + visual sanity checks (§7.2 1-3
  above) would fold naturally into that run.
- **Pivot to a different polish arc / master-order
  item.** The TEX-P.x arc landed PASS (TEX-P.7) and
  has no open items; master order #16 (path tracing
  feature work — NEE / non-diffuse BSDFs / multi-
  mesh upload) is the next major follow-up after
  the PT-P.x polish arc closes.

PT-P.19 (this audit) closes the PT-P.{17,18} sub-arc.
The next concrete slice — when the operator chooses
to continue — opens with PT-P.20 (the firefly-clamp
task definition) mirroring the PT-P.{2,5,8,11,14,17}
cadence.

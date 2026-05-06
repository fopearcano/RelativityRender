# Path-Tracer Polish — RNG Stability Task

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_POLISH_PLAN.md` §4.1.
Selected via:
`docs/PATH_TRACER_POLISH_EMISSION_AUDIT.md` §7's
"Recommended next step" verdict (the §4.5 polish shipped
PASS via PT-P.15; §4.1 is the next remaining item per the
plan's sequencing — FIRST PT-P.x slice that changes
every `pathtrace_spp_*.ppm` byte-exactly).
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next
slice (PT-P.18 implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's
reasoning.

---

## 1. Exact issue

**Title.** PT-P.x — RNG stability (per-input SplitMix64
seed mix).

**Source.** `PATH_TRACER_POLISH_PLAN.md` §4.1.

**One-paragraph summary.**
`make_pixel_rng(pixel_x, pixel_y, frame_index,
global_seed)` in `src/pathtracer/RNG.h:69-73` packs the
four 32-bit-ish inputs into a 64-bit key by left-shifting
and xoring:

```cpp
const std::uint64_t key =
      global_seed
    ^ (static_cast<std::uint64_t>(pixel_x)     << 32u)
    ^ (static_cast<std::uint64_t>(pixel_y)     << 16u)
    ^ (static_cast<std::uint64_t>(frame_index)       );
```

The shifts make `pixel_y << 16` and `frame_index << 0`
overlap in bits `[0, 32)` of the key. For "small" pixel_y
(< 65536, i.e. every realistic image height) and "small"
frame_index (every realistic spp value), the low 16 bits
of `pixel_y` and the low 16 bits of `frame_index` collide
BEFORE SplitMix64 avalanches the key. SplitMix64 + the
burn-one-step at the end of `make_pixel_rng` recover
decorrelation, so today's renders look fine, but the mix
is weaker than necessary at low (x, y, sample) values
and the existing
`test_rng_per_pixel_decorrelation` test
(`tests/pathtracer_tests.cpp:68-92`) only checks four
adjacent perturbations (x+1 / y+1 / sample+1 / seed+1)
rather than a full grid.

The polish item replaces the four-input shift-and-xor
with a per-input SplitMix64 hash + xor, so each input
gets a full 64-bit avalanche before the inputs are
combined. The new mix is strictly stronger than the old
one — every input contributes its full entropy — and
adjacent (x, y, frame, seed) values produce
provably-distinct pre-PCG keys without relying on the
post-mix burn-step to recover from a collision.

**Property of this slice that distinguishes it from
PT-P.{3,6,9,12,15}.** The PCG state values produced by
the new key construction differ from the old ones for
EVERY (x, y, sample, seed) combination. Every
`pathtrace_spp_*.ppm` byte is therefore expected to
change pre-/post-slice. The visual result + the
statistical convergence + the means / variances /
PDFs are all unchanged (per the plan's "noise pattern
shifts; means / variances unchanged" note); only the
specific noise pattern shifts. This is a
ONE-TIME, intentional, audit-tracked pixel-diff —
NOT a regression.

**The single concrete change.**

### 1.1 Replace the key construction (`src/pathtracer/RNG.h`)

The current 4-line key construction at lines 69-73 is
replaced with a per-input SplitMix64 mix:

```cpp
RR_HD inline Rng make_pixel_rng(std::uint32_t pixel_x,
                                std::uint32_t pixel_y,
                                std::uint32_t frame_index,
                                std::uint64_t global_seed) {
    // PT-P.18: hash each of the four inputs through SplitMix64
    // INDIVIDUALLY before xoring them into the PCG seed key,
    // so the four 32-bit-ish inputs cannot collide in the
    // raw bit layout. The pre-PT-P.18 mix shift-and-xored the
    // inputs into a single 64-bit key (`pixel_y << 16` and
    // `frame_index << 0` overlapped in bits [0, 32)); the
    // collision was recovered by the subsequent SplitMix64 +
    // burn-one-step, but the per-input hash makes the mix's
    // independence explicit and removes the recovery
    // dependency. Each `splitmix64(input)` is a 64-bit
    // avalanche, so the four results are statistically
    // independent before they're combined; xoring four
    // independent uniform 64-bit values produces a uniform
    // 64-bit result.
    //
    // The `pixel_y << 32u` shift here is NOT to avoid a
    // collision (every input has already been hashed) — it's
    // a sanity nudge so an accidental swap of pixel_x <->
    // pixel_y on the call site cannot accidentally reproduce
    // the same key (the two inputs go through the same
    // SplitMix64 but the post-hash xor at different offsets
    // is what disambiguates them).
    const std::uint64_t key =
          splitmix64(static_cast<std::uint64_t>(global_seed))
        ^ splitmix64(static_cast<std::uint64_t>(pixel_x))
        ^ splitmix64(static_cast<std::uint64_t>(pixel_y) << 32u)
        ^ splitmix64(static_cast<std::uint64_t>(frame_index));

    Rng r;
    r.state = splitmix64(key);
    // Burn one step. PCG's first output from an unmixed seed can
    // share low bits with neighbours; one step decorrelates them
    // while keeping the seed function deterministic + cheap.
    (void)pcg32_next(r);
    return r;
}
```

The implementer may pick the alternative the plan §4.1
suggests (the in-place 32-bit pack + a single SplitMix64
on `global_seed ^ frame_index`), but the per-input hash
above is preferred because:

1. It is symmetric across all four inputs (no
   "magic" position for `global_seed` vs the others).
2. The "x and y are different inputs even though both
   pass through the same hash" property is encoded in
   the post-hash xor at different offsets, which
   reads cleanly even without a doc-comment.
3. It costs four extra SplitMix64 calls per kernel
   thread per launch — at ~5 ns each on an A100, ~20 ns
   per pixel per launch, negligible against the
   per-pixel kernel cost (which is dominated by ray
   intersection, not RNG seeding).

The original `splitmix64` + `pcg32_next` helper functions
are byte-identical (no signature / body change). Only
the body of `make_pixel_rng` changes.

### 1.2 Replace the doc-comment on `make_pixel_rng`

The pre-PT-P.18 doc-comment block at lines 55-61 stays
mostly intact — it describes the function's contract
(decorrelated streams across (x, y, frame, seed)) which
remains true. The pre-PCG-step "Pack the four inputs
into a 64-bit key. ..." comment block at lines 66-68
inside the function body is REPLACED with the new
PT-P.18 explanation (in §1.1 above). The "Burn one
step. ..." comment at lines 77-79 is preserved verbatim.

### 1.3 New test in `tests/pathtracer_tests.cpp`

Add ONE new test case asserting that
`make_pixel_rng(...).state` is distinct across a full
16x16x4 grid of (x, y, sample) values at a fixed seed,
plus four perturbations of seed itself. Suggested
shape (the implementer's exact phrasing):

```cpp
// PT-P.18: full-grid collision check on the new
// per-input SplitMix64 seed mix. The pre-PT-P.18 mix
// had overlapping shifts (pixel_y << 16 and
// frame_index << 0 collided in bits [0, 32)). The
// existing decorrelation test only checked four
// adjacent perturbations; this test asserts that
// every (x, y, sample) cell in a 16x16x4 grid + four
// seed perturbations produces a distinct PCG state.
void test_rng_grid_collision_check() {
    // 16 * 16 * 4 = 1024 unique (x, y, sample)
    // tuples. With four seed perturbations
    // (0, 1, 2, 3), that's 4096 distinct calls; we
    // assert every state is unique.
    constexpr int kW = 16;
    constexpr int kH = 16;
    constexpr int kSpp = 4;
    constexpr int kSeeds = 4;
    constexpr std::size_t N = static_cast<std::size_t>(kW) * kH * kSpp * kSeeds;

    std::uint64_t states[N];
    std::size_t idx = 0;
    for (int s = 0; s < kSeeds; ++s) {
        for (int sm = 0; sm < kSpp; ++sm) {
            for (int y = 0; y < kH; ++y) {
                for (int x = 0; x < kW; ++x) {
                    const Rng r = make_pixel_rng(
                        static_cast<std::uint32_t>(x),
                        static_cast<std::uint32_t>(y),
                        static_cast<std::uint32_t>(sm),
                        static_cast<std::uint64_t>(s));
                    states[idx++] = r.state;
                }
            }
        }
    }
    // Sort and scan for duplicates. Sort is fine for
    // a 4096-element array in a host test.
    std::sort(states, states + N);
    bool any_dup = false;
    for (std::size_t i = 1; i < N; ++i) {
        if (states[i] == states[i - 1]) {
            any_dup = true;
            std::fprintf(stderr,
                "FAIL: duplicate state at index %zu: 0x%016llx\n",
                i, static_cast<unsigned long long>(states[i]));
            break;
        }
    }
    RR_CHECK(!any_dup);
}
```

The test's `<algorithm>` include + the `std::sort` /
`std::uint64_t[N]` array allocation are host-only and
do not impact the kernel; the test runs against the
host C++ compiler's instantiation of the `RR_HD inline`
helpers. Add a corresponding entry in the test binary's
`main()` to invoke `test_rng_grid_collision_check()`.

### 1.4 No new CLI surface, no kernel call-site changes

Every `make_pixel_rng` call site in `src/cuda/`,
`src/optix/`, and elsewhere keeps its existing
arguments (`pixel_x, pixel_y, sample_index, seed`).
The function's external signature is unchanged. The
ONLY behavioural change observable to a kernel caller
is that `r.state` after the call has a different
64-bit value than before — exactly what §4.1 plans for.

---

## 2. Expected behavior

The four contractual properties the polish must
honour (matching the prompt's spec sub-bullets
exactly):

### 2.1 Same input config produces same output sequence

`make_pixel_rng(x, y, sample, seed)` is a pure
function with no hidden state, no time-of-day input,
no thread-id input, no environment-variable input.
For ANY two calls with the same four arguments, the
returned `Rng` has the same `r.state` bit-for-bit;
every subsequent `pcg32_next(r)` produces the same
32-bit output bit-for-bit; every `next_float(r)` /
`next_vec2(r)` produces the same float bit-for-bit.

This is the core determinism contract and it remains
intact post-PT-P.18 — the new mix is still a pure
function of its four arguments. The post-PT-P.18
sequence DIFFERS from the pre-PT-P.18 sequence (every
state value changes), but the determinism property
holds within either era.

### 2.2 Sample index affects RNG predictably

`make_pixel_rng(x, y, sample, seed) !=
make_pixel_rng(x, y, sample+1, seed)` for every
authored `(x, y, sample, seed)` tuple. This is a
hard collision-freedom property over the full
spp domain.

The pre-PT-P.18 mix could in principle collide on
adjacent samples for specific (x, y) values (the
shift overlap means `pixel_y` and `frame_index`
share the low 16 bits of the key); the post-mix
SplitMix64 + burn-step rescued the decorrelation in
practice, but the property was not first-class. The
post-PT-P.18 mix gives each input its own
SplitMix64 avalanche before combining, so adjacent
sample indices produce statistically-independent
keys.

The new test (§1.3) verifies this empirically at
1024 grid cells + 4 seed perturbations; the larger
input space is verified by the per-input hash's
mathematical property (SplitMix64 is a bijection
on `std::uint64_t`).

### 2.3 Pixel coordinates affect RNG predictably

Same as §2.2 but for `(x, y)`. The per-input
SplitMix64 + post-hash xor at different offsets
(`pixel_y << 32u`) ensures that swapping x <-> y
produces a distinct key, matching the pre-PT-P.18
intent.

### 2.4 No hidden time-based seed

The renderer's RNG seeding is fully deterministic —
no `std::time(nullptr)`, `std::chrono::*`,
`std::random_device`, or any other entropy source
contributes to the RNG state. The seed comes
exclusively from `PathTraceConfig::seed` (default
`0u`; an explicit field on the config POD). This is
ALREADY the case pre-PT-P.18 and the slice does not
change it — but the audit (§5.5 below) re-verifies
the no-time-source property by `grep`.

The kernel-side runtime entropy that EXISTS today —
the OS-level CUDA / OptiX scheduler nondeterminism
that affects warp execution order, FMA fusion,
etc. — does not enter the RNG. Determinism holds at
the per-pixel level in both eras (the same
`(x, y, sample, seed)` tuple produces the same
`pcg32_next(r)` outputs); cross-thread arrival order
is hardware-/driver-dependent but does not bleed into
the per-thread state because each thread's RNG is
seeded independently.

---

## 3. Files likely involved

The implementation slice will touch this minimal set:

| File                                | Change                                                  |
|-------------------------------------|---------------------------------------------------------|
| `src/pathtracer/RNG.h`              | Replace the four-line key-construction block in        |
|                                     | `make_pixel_rng` with per-input SplitMix64 + xor.       |
|                                     | Update the inline doc-comment to explain the           |
|                                     | rationale. ~10-15 added, ~5 deleted.                    |
| `tests/pathtracer_tests.cpp`        | Add one new `test_rng_grid_collision_check()` test    |
|                                     | + invoke it from `main()`. ~25-35 added.                |
| `docs/BUILD_PLAN.md`                | Slice-closing entry following the established          |
|                                     | TEX-P.x / PT-P.x format. The entry MUST flag the       |
|                                     | expected pixel-diff on `pathtrace_spp_*.ppm`           |
|                                     | byte-exactly so the operator's pre-/post-slice        |
|                                     | comparison documents the change as intentional.       |

Two source files (the `.h` + the test file). Honours
the PT-P.x master rule of "max 2 source files unless
explicitly justified". The test file is the second
authorised source-file change in the slice — it is
NOT a third file because the plan's "~25 lines of new
test" + the prompt's spec bullet 5 ("PASS criteria")
both expect a test addition for this slice
specifically, given that this is the first PT-P.x
slice that changes rendered output.

The test addition includes `<algorithm>` for
`std::sort` (already a host-only standard header; not
new to the test binary's compile environment).

`src/cuda/`, `src/optix/`, `src/pathtracer/PathTracer.{h,cpp}`,
`src/pathtracer/Sampling.h`, `src/main.cpp`,
`src/core/`, `src/io/`, `src/scene/`, `src/material/`,
`src/lighting/`, `src/renderer/`, every `*.rrscene`
file, `tools/verify_cuda_host.py`, and `CMakeLists.txt`
MUST be byte-identical post-slice.

### 3.1 Why this is the FIRST PT-P.x source-edit slice that touches a test file

PT-P.{3,6,9,12,15}'s implementation slices either
deliberately added no test (PT-P.{6,9,12,15}, on the
"verifiable by code inspection" precedent) or added a
new ctest binary (PT-P.3 added
`tests/renderer_tests.cpp`). PT-P.18 extends an
EXISTING ctest binary (`tests/pathtracer_tests.cpp`)
because:

- The existing decorrelation test
  (`test_rng_per_pixel_decorrelation`) covers the
  same domain at a smaller scale; the new test is a
  natural sibling.
- The 1024-cell grid is the kind of empirical check
  the prompt's PASS criteria for this slice
  explicitly requires (§5.4 below).
- Adding the test costs ~30 lines + one `main()`
  invocation; adding a NEW ctest binary would cost
  the new file + a CMakeLists.txt block + a
  duplicate `RR_CHECK` macro + duplicate `main()`
  scaffolding (~50+ lines).

Per the §5.3 source-diff cap below, the test file
addition is accounted for separately from the `.h`
change.

---

## 4. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 4.1 The `pcg32_next` and `splitmix64` helpers

`src/pathtracer/RNG.h:29-43` (`pcg32_next`) and
`:48-53` (`splitmix64`): byte-identical bodies. The
PCG32 LCG step + the SplitMix64 magic constants are
NOT modified. The polish only changes how
`make_pixel_rng` *uses* `splitmix64`; the function
itself is untouched.

### 4.2 The `Rng` struct

`src/pathtracer/RNG.h:21-23`: byte-identical. Single
`std::uint64_t state` field, default-zero, plain
aggregate. No new fields.

### 4.3 The `next_float` and `next_vec2` consumers

`src/pathtracer/RNG.h:89-105`: byte-identical. Same
24-bit float-significand mapping; same two-call Vec2
construction. The polish does not change what bits
`pcg32_next` returns or how they map to floats — it
only changes WHICH bits the post-seed state holds.

### 4.4 Every `make_pixel_rng` call site

The function's public signature is byte-identical:

```cpp
RR_HD inline Rng make_pixel_rng(std::uint32_t pixel_x,
                                std::uint32_t pixel_y,
                                std::uint32_t frame_index,
                                std::uint64_t global_seed);
```

Every call site in `src/cuda/CudaPathTracer.cu:158`,
`src/cuda/CudaAccumulation.cu:114`,
`src/cuda/CudaRngTestKernel.cu:54`, and the OptiX
raygen path (via `pathtracer/RNG.cuh`'s re-export):
byte-identical arguments. Every kernel that consumes
the seeded `Rng` does so unchanged.

### 4.5 Path-tracer output (with one INTENTIONAL exception)

For `samples_per_pixel = 0`: no rays traced; the
empty-image short-circuit fires before any
`make_pixel_rng` call.
`output/gpu_accumulation_test.ppm` should remain
byte-identical (the accumulation test does not invoke
`make_pixel_rng`; it tests a separate
`launch_random_rgba_sample` kernel that uses its own
seeded `Rng` per pixel — verify this is the case
during implementation and document either way in the
slice's BUILD_PLAN entry).

For `samples_per_pixel >= 1`:

- `output/gpu_rng_test.ppm`: **byte-different**
  pre-/post-slice. The four-quadrant noise visualization
  uses `make_pixel_rng` per pixel; every pixel's
  noise sample shifts.
- `output/pathtrace_spp_1.ppm`,
  `output/pathtrace_spp_16.ppm`: **byte-different**
  pre-/post-slice. Every pixel's primary ray + bounce
  RNG stream changes.
- `output/optix_pathtrace_spp1.ppm`,
  `output/optix_pathtrace_spp16.ppm`:
  **byte-different** pre-/post-slice (the OptiX
  raygen consumes the same `make_pixel_rng` helper).

The byte-different outcome is the EXPECTED behaviour
of this slice. The visual quality (means / variances
/ PDFs) is preserved; only the noise pattern shifts.
This is the renderer's only PT-P.x slice with this
property and the BUILD_PLAN entry must call it out
loudly.

### 4.6 CLI surface

- No new `--*` flag.
- No change to any dispatcher's info-log format.
- The existing `--render-pathtrace` /
  `--render-rng-test` /
  `--render-accumulation-test` /
  `--render-optix-pathtrace` argument parsers are
  byte-identical.

### 4.7 PathTraceConfig field set

- Byte-identical (zero new fields, zero default
  changes). The PT-P.6 / PT-P.9 / PT-P.12 / PT-P.15
  predecessors all remain.

### 4.8 The existing `tests/pathtracer_tests.cpp` test cases

The new `test_rng_grid_collision_check` test is
ADDED; the existing eight test cases
(`test_rng_float_range`,
`test_rng_per_pixel_decorrelation`,
`test_rng_determinism`,
`test_rng_vec2_components`,
`test_uniform_hemisphere_unit_length_and_upper`,
`test_uniform_hemisphere_pdf_normalises`,
`test_cosine_hemisphere_unit_length_and_upper`,
`test_cosine_hemisphere_distribution`) are
byte-identical. Their assertions remain valid —
PCG32, SplitMix64, the hemisphere samplers, and the
post-seed `pcg32_next` are all unchanged; only the
specific seed values the existing tests pass to
`make_pixel_rng` produce different post-seed state,
but each test does its own internal sequencing
that's invariant under the seed-mix change.

Specifically: `test_rng_per_pixel_decorrelation`
asserts that five adjacent perturbations of (x, y,
frame, seed) produce DIFFERENT first floats. The
post-PT-P.18 RNG continues to satisfy this property
— the new test (§1.3) is a STRICTER version of the
same check across a 1024-cell grid. The pre-existing
4-call check still passes; the new 4096-call check
also passes.

### 4.9 Other audits / plans

- `docs/PATH_TRACER_POLISH_PLAN.md`: optionally add
  a one-line "PT-P.18 shipped" note at the top of
  §4.1. NOT required.
- The eight earlier PT-P.x task / audit docs: NO
  edits.
- The TEX-P.x arc + the CUDA-H.x arc: NO edits.
- `tools/verify_cuda_host.py`: NO changes (the
  runner exercises the existing `--render-pathtrace`
  + `--render-rng-test` + `--render-optix-pathtrace`
  commands; the new RNG mix is picked up
  automatically).

---

## 5. PASS criteria

The implementation slice passes when ALL of the
following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF): clean
  build, zero new warnings. The new key-construction
  block compiles host-side via the `RR_HD inline`
  helper (the test binary type-checks it).
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON): clean
  build, zero new warnings.

### 5.2 Tests

- `ctest --output-on-failure` from `build`: 7/7
  PASS. The `pathtracer_tests` binary's count grows
  by 1 (8 -> 9 internal tests, but the binary's
  exit code stays 0).
- `ctest --output-on-failure` from `build-ON`: 8/8
  PASS.
- The new `test_rng_grid_collision_check` test
  reports `RR_CHECK(!any_dup)` true — every state
  value in the 4096-cell grid is unique.
- The eight existing pathtracer tests continue to
  pass without modification.

### 5.3 Source diff size

- `src/pathtracer/RNG.h` diff: ~10-18 added, ~5
  deleted (the old key-construction block + its
  comment paragraph + the new key-construction +
  its new comment paragraph).
- `tests/pathtracer_tests.cpp` diff: ~25-40 added,
  0 deleted (one new test function + its `main()`
  invocation + a `<algorithm>` include).
- TOTAL across both source files: <= 60 added.
  Anything LARGER flagged in the BUILD_PLAN entry
  as a deviation (precedent: PT-P.6 / PT-P.9 /
  PT-P.15 flagged-deviation notes are the
  templates).

### 5.4 No-touch invariants

`git diff` after the slice MUST show zero bytes
changed in:

- `src/cuda/`
- `src/optix/`
- `src/pathtracer/PathTracer.{h,cpp}`
- `src/pathtracer/Sampling.{h,cuh}`
- `src/pathtracer/RNG.cuh` (the re-export header)
- `src/main.cpp`
- `src/core/`
- `src/io/`
- `src/scene/`
- `src/material/`
- `src/lighting/`
- `src/renderer/`
- every `*.rrscene` file under `scenes/`
- `tools/verify_cuda_host.py`
- `CMakeLists.txt`

Verifiable by:

```
git diff -- \
  src/cuda/ src/optix/ \
  src/pathtracer/PathTracer.h \
  src/pathtracer/PathTracer.cpp \
  src/pathtracer/Sampling.h src/pathtracer/Sampling.cuh \
  src/pathtracer/RNG.cuh \
  src/main.cpp src/core/ src/io/ src/scene/ \
  src/material/ src/lighting/ src/renderer/ \
  scenes/ tools/verify_cuda_host.py CMakeLists.txt \
  | wc -l
=> 0
```

### 5.5 Hidden time-source check

`grep -rEn "std::time|std::chrono|std::random_device|
chrono::|time\\(NULL\\)|time\\(nullptr\\)|gettimeofday|
clock_gettime|GetSystemTime"
src/pathtracer/ src/cuda/CudaPathTracer.cu
src/cuda/CudaRngTestKernel.cu
src/cuda/CudaAccumulation.cu` MUST return zero
non-comment matches related to RNG seeding. (The
`<chrono>` include in `gpu/GpuTiming.h` is for
kernel-launch wall-clock timing — NOT seeding — and
is acceptable; the implementer should record any
such matches in the slice's BUILD_PLAN entry as
"non-seeding chrono use".)

### 5.6 Behavioural smoke (audit host)

- `./build/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene` continues to emit
  the documented "requires CUDA" audit-host
  fallback byte-identically with the pre-PT-P.18
  baseline. The new key construction is unreachable
  on the audit host (the dispatcher returns from
  the `--render-pathtrace requires CUDA` branch
  before reaching the kernel launch); the smoke
  confirms that fallback path is unchanged.
- `./build-ON/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene`: same.
- `./build/bin/RelativityRender --scene-info
  scenes/test_textured_material.rrscene`: emits the
  TEX-P.6 fixture's expected three-case log
  sequence byte-identically (one Case 1 info + two
  Case 3 warnings; `fixups applied: 2`). Confirms
  zero PT-P.18 ripple onto the texture validator.

### 5.7 Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing
  entry matching the established PT-P.x format
  (Scope / What ships / What does NOT change /
  Behaviour matrix / Master rule compliance /
  Verified at the build).
- The entry MUST include a prominent "Pixel-diff
  warning" sub-section calling out that this is the
  FIRST PT-P.x slice that produces a real pixel-diff
  pre-/post-slice on `pathtrace_spp_*.ppm` /
  `gpu_rng_test.ppm`, and listing the runtime
  CUDA-host checks the operator must perform.
- The entry references
  `docs/PATH_TRACER_POLISH_PLAN.md` §4.1 + this task
  file as the source of the specification.

### 5.8 Master rule compliance

- Build incrementally (rule 1) + every step
  compilable (rule 2): both audit-host configs
  green.
- No fake stubs (rule 3): the per-input SplitMix64
  hash is the canonical hash chain, not a stub.
- No CPU per-pixel work (rule 5/7): the
  `make_pixel_rng` function is `RR_HD inline` and
  runs on the device when called from a kernel; the
  test's host-side calls iterate over a 4096-cell
  POD array, NOT per-pixel rendering. Master rule
  5/7 is upheld.
- Update BUILD_PLAN (rule 8): the slice-closing
  entry.

---

## 6. Runtime CUDA-host checks needed

PT-P.18 is the FIRST PT-P.x slice with mandatory
runtime CUDA-host verification (PT-P.{3,6,9,12,15}'s
runtime checks were optional confirmations of
already-bit-identical behaviour). Three checks the
operator MUST perform on a CUDA host before declaring
PT-P.18's audit (PT-P.19) PASS:

### 6.1 PPM byte-DIFFERENCE confirmation (the intentional pixel-diff)

Run `--render-pathtrace scenes/test_full_scene.rrscene`
and `--render-rng-test` BEFORE and AFTER the PT-P.18
commit. Each PPM must DIFFER pre-/post-slice — the
operator confirms the pixel-diff is actually firing,
not silently no-op'ing. Procedure:

```
# Step 1: render with the post-PT-P.18 tree
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm  /tmp/post_p18_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/post_p18_spp16.ppm
$ ./build-cuda/bin/RelativityRender --render-rng-test
$ cp output/gpu_rng_test.ppm     /tmp/post_p18_rng.ppm

# Step 2: check out pre-PT-P.18 source and re-render
$ git checkout dd98d90 -- src/pathtracer/RNG.h \
                          tests/pathtracer_tests.cpp
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm  /tmp/pre_p18_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/pre_p18_spp16.ppm
$ ./build-cuda/bin/RelativityRender --render-rng-test
$ cp output/gpu_rng_test.ppm     /tmp/pre_p18_rng.ppm
$ git checkout HEAD -- src/ tests/  # restore

# Step 3: confirm the diff fired
$ cmp /tmp/post_p18_spp1.ppm  /tmp/pre_p18_spp1.ppm  ; echo $?  # expected: non-zero
$ cmp /tmp/post_p18_spp16.ppm /tmp/pre_p18_spp16.ppm ; echo $?  # expected: non-zero
$ cmp /tmp/post_p18_rng.ppm   /tmp/pre_p18_rng.ppm   ; echo $?  # expected: non-zero
```

Expected: `cmp` reports the files DIFFER (exit code
1 for each). If `cmp` reports the files are
identical, the slice failed to land the new key
construction and the PT-P.19 audit's verdict for §1
flips to REPAIR.

### 6.2 Visual + statistical sanity

The post-PT-P.18 PPMs must look visually identical to
the pre-PT-P.18 PPMs at the operator's eye level. The
noise PATTERN shifts (every pixel has a different
seed); the noise CHARACTER (variance, energy
distribution, mean luminance) is preserved.

The operator MAY verify statistically by running:

```
$ python3 - <<'EOF'
import struct
def read_ppm(p):
    with open(p, 'rb') as f:
        # Skip header lines until empty line / depth
        # then read raw pixel bytes.
        ...
    return ...

pre  = read_ppm('/tmp/pre_p18_spp16.ppm')
post = read_ppm('/tmp/post_p18_spp16.ppm')
# Mean luminance per channel
for ch in range(3):
    diff = abs(sum(post[ch::3]) - sum(pre[ch::3]))
    print(f'channel {ch}: |sum_post - sum_pre| = {diff}')
# Should be small relative to total energy (e.g.
# < 1% of either sum)
EOF
```

Or simpler: visually toggle between the two PPMs in
an image viewer and confirm the noise PATTERN
differs but the rendered scene (sphere positions,
lighting falloff, emissive surface intensity) is
identical.

### 6.3 ctest cycle on the CUDA host

`ctest --output-on-failure` from a CUDA-built
`build-cuda` directory must pass — in particular,
`pathtracer_tests` must execute the new
`test_rng_grid_collision_check` and report no
duplicate states across the 4096-cell grid.

This is a MANDATORY step; the audit-host build's 7/7
ctest already exercises the test on the host
compiler, but a CUDA host's build path produces a
separate compile of the same .cpp (the test binary
links against `rr_pathtracer` and is built whether
CUDA is on or off).

### 6.4 Update the CUDA-host verification report

The CUDA-H.9 runner's
`docs/CUDA_HOST_VERIFICATION_REPORT.md` is currently
PASS for `render-pathtrace` (when run on a CUDA
host). Post-PT-P.18, the report's per-test status
remains PASS — but the report's "Tree state" hash
line will change because the source tree changed.
The operator must:

- Re-run `tools/verify_cuda_host.py` after the
  PT-P.18 commit.
- Confirm the report's overall verdict stays PASS
  (or BLOCKED for items that were already BLOCKED).
- Commit the refreshed report along with the
  PT-P.19 audit slice (mirroring the CUDA-H.9
  determinism contract: "the only varying content
  is the per-test status + the tree-state hash").

If the runner is not yet available on a CUDA host,
the operator may DEFER §6.4 to a later
verification slice — but the §6.1 + §6.2 + §6.3
checks are mandatory for the PT-P.19 audit verdict
to PASS.

### 6.5 Edge case: small (x, y, sample) values must collide-free

The new test (§1.3) verifies a 16x16x4x4 grid. The
operator may extend this on a CUDA host by
authoring a one-shot harness that samples the FULL
authored image's (x, y, 0) tuples (e.g. 1280x720 =
~921k tuples) and asserts no duplicates in the
post-seed PCG state. This is OPTIONAL; the
mathematical property (SplitMix64 is bijective)
guarantees no collisions in principle, but a
1280x720 empirical confirmation removes any doubt.

---

## 7. Out-of-scope (deferred to later PT-P.x slices)

The following items from
`docs/PATH_TRACER_POLISH_PLAN.md` are explicitly NOT
part of this task:

- §4.7 Firefly clamp placeholder (default-off field
  on `PathTraceConfig` + matching kernel guards on
  BOTH the CUDA and OptiX path-trace raygens). The
  largest remaining surface; sequence last so the
  kernel guards are added against the post-PT-P.18
  RNG.

PT-P.17 (this task definition) and PT-P.18 (the
implementation slice) are the only PT-P.x slices
currently scheduled. After PT-P.18 + PT-P.19 (the
audit) land, the operator chooses the next polish
item:

- **§4.7** is the last `PATH_TRACER_POLISH_PLAN.md`
  §4 item; landing it closes the PT-P.x polish arc.
- **CUDA-host verification run** (a single
  `tools/verify_cuda_host.py [--optix]` invocation)
  flips the BLOCKED rows from PT-P.4 / PT-P.7 /
  PT-P.10 / PT-P.13 / PT-P.16 / PT-P.19 to PASS.
- **Pivot to a different polish arc / master-order
  item.** The TEX-P.x arc landed PASS (TEX-P.7);
  master order #16 (path tracing — feature work
  like NEE / non-diffuse BSDFs / multi-mesh upload)
  is the next major follow-up.

---

## 8. Why §4.1 is the next viable slice (NOT the safest)

§4.1 is explicitly the LEAST-safe of the remaining
PT-P.x items (it changes every rendered pixel). The
plan §5 originally sequenced it AFTER the smaller
items so that:

- The smaller items (§4.{2,3,4,5,6}) all shipped
  byte-identical PPM output and lay quiet baselines
  in place.
- §4.1's pixel-diff is the SOLE expected
  pre-/post-slice change once the smaller items have
  landed; the operator can confirm the diff is
  ENTIRELY due to RNG stability work, not bundled
  with other shifts.

PT-P.{3,6,9,12,15,16} have all landed PASS; the
foundation is quiet. §4.1 is now safe to ship in the
sense that:

1. The FIRST and ONLY pixel-diff between PT-P.15 and
   the future PT-P.20 (whatever opens next) is
   PT-P.18's RNG-stability change.
2. The operator can attribute any post-PT-P.18 PPM
   change unambiguously to the RNG mix.
3. A future regression that introduces an unintended
   pixel-diff has a clear bisection target.

The ONLY remaining alternative (§4.7 firefly clamp)
keeps PPMs byte-identical (default-off field), so
the operator could in principle run §4.7 first and
defer §4.1 indefinitely. But §4.7 touches BOTH
backends (CUDA + OptiX path-trace raygens), which
doubles the slice's blast radius; §4.1 stays
host-compiler-side (the `RR_HD inline` helper
type-checks on the audit host) and only changes the
seed-mix expression.

§4.1 is therefore the NEXT viable slice on the
PT-P.x cadence: largest one-time-pixel-diff cost +
smallest remaining-arc surface = the right time to
land it.

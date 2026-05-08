# Power Heuristic Helper — Task Definition (MIS.4)

Date: 2026-05-08.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_MIS_PLAN.md` §3.3
+ §5.3 (power heuristic concept; "MIS.4 — Power
heuristic helper" stage definition).
Prior slice:
`docs/PATH_TRACER_MIS_LIGHT_PDF_AUDIT.md` (commit
`960c523`) closed MIS.3; recommended MIS.4 as the
last independent leaf before MIS.5 + MIS.6 wire
the integrators.
Mode: documentation only. **No source code is
modified by this task definition.** The task is
the spec; the next slice (the implementation)
ships the diff.

This file is a fully-self-contained brief for the
MIS.4 implementation slice. Anyone picking it up
should be able to ship MIS.4 without re-deriving
the design reasoning. Pattern mirrors the
canonical MIS.x task-brief shape established at
`docs/PATH_TRACER_MIS_BSDF_PDF_TASK.md` and
`docs/PATH_TRACER_MIS_LIGHT_PDF_TASK.md`.

The MIS arc cadence (post-MIS.3 audit):

| Slice                        | Role                                              | Commit       |
|------------------------------|---------------------------------------------------|--------------|
| MIS.1                        | Multiple Importance Sampling plan                 | `67dd03c`    |
| MIS.2 task                   | BSDF PDF data model task brief                    | `659155e`    |
| MIS.2 structure-only         | `BsdfSample` POD                                  | `d9fa6e3`    |
| MIS.2 helpers                | Lambert sampler + 10 host-only tests              | `5a1c772`    |
| MIS.3 task                   | Light PDF data model task brief                   | `da86554`    |
| MIS.3 impl                   | `DirectLightSample` extension                     | `0dd7d46`    |
| MIS.3 audit                  | Per-stage MIS.3 audit                             | `960c523`    |
| **MIS.4 task**               | **This brief** — MIS.4 task definition            | (THIS slice) |
| MIS.4 impl                   | `power_heuristic` helper + tests                  | (next)       |
| MIS.4 audit                  | Per-stage MIS.4 audit (recommended)               | (after impl) |
| MIS.5                        | CUDA integrator                                    | (pending)    |
| MIS.6                        | OptiX integrator                                   | (pending)    |
| MIS.7                        | Arc-level audit                                    | (pending)    |

---

## 1. Exact goal

**Add `rr::pathtracer::power_heuristic(float
p_a, float p_b)` — a pure-math RR_HD inline helper
returning the Veach β=2 power-heuristic MIS
weight for the first estimator. Ship the helper +
host-only test binary. No integrator changes; no
caller in this slice.**

The helper closes the last independent leaf of
the MIS arc (per
`docs/PATH_TRACER_MIS_PLAN.md` §5.3 + §8). After
MIS.4 lands, all three leaves (MIS.{2,3,4}) are
shipped:

- **MIS.2** (commits `d9fa6e3` + `5a1c772`):
  `BsdfSample` POD + Lambert helpers
  (`sample_bsdf` / `bsdf_pdf` / `bsdf_eval`).
- **MIS.3** (commit `0dd7d46`):
  `DirectLightSample` extended with
  `pdf_solid_angle` + `is_delta`.
- **MIS.4** (this brief): `power_heuristic`
  helper combining the two estimators' PDFs.

MIS.5 (CUDA integrator) wires these three leaves
into `k_pathtrace_sample`'s NEE branch + cosine-
bounce-as-light path; MIS.6 mirrors on OptiX;
MIS.7 audits the entire arc.

### 1.1 What the helper computes

The Veach 1995 §9.2.4 power heuristic with β=2
combines N estimators with their per-sample PDFs
into MIS weights that minimise variance under
mild assumptions. With one sample each (`n_i = 1`)
across two estimators (A = NEE-side, B = BSDF-
side), the formula collapses to:

```
                  p_a²
w_a(p_a, p_b) = ─────────
                p_a² + p_b²
```

where:
- `p_a` is the PDF of the A-estimator at the
  sampled direction (in the SAME UNITS as
  `p_b` — per steradian for direct lighting).
- `p_b` is the PDF of the B-estimator at the
  same direction.
- The result `w_a` is a scalar in `[0, 1]` that
  the integrator multiplies into the A-
  estimator's contribution.

The B-estimator's MIS weight is symmetric:
`w_b = power_heuristic(p_b, p_a) = p_b² / (p_a²
+ p_b²)`. The integrator computes both weights
by calling the helper twice with swapped
arguments.

**Sum-to-one invariant**: `w_a + w_b == 1.0`
exactly when at least one PDF is non-zero (the
shared denominator drops out).

### 1.2 Why this slice is small

The power heuristic is a single scalar
expression. The plan §5.3 estimated ~30 lines
for the helper file + ~80 lines for the tests.
This brief targets the same scale; MIS.4 is
the SMALLEST MIS leaf by line count.

The helper is also the most foundational — every
MIS-aware integrator slice (MIS.5, MIS.6, every
future MIS-aware arc like area-light NEE)
calls it at the per-bounce level. Getting the
math + edge-case behaviour right at MIS.4 means
all downstream integrators inherit a tested,
robust foundation.

### 1.3 Module split decision

The plan §5.3 mentioned `Mis.{h,cuh}` (split
across two files). This brief recommends a
**single `Mis.h`** matching the `Sampling.h`
sibling (also a pure-math helper module; no
`.cuh`). Reasoning:

- The `.h` / `.cuh` split exists when the `.h`
  carries a host-friendly POD and the `.cuh`
  carries device-specific code (e.g.
  `DirectLight.h` defines `DirectLightSample`,
  `DirectLight.cuh` carries the helper). MIS
  has no POD — `power_heuristic` is a pure
  scalar function.
- `Sampling.h` (the closest sibling — pure
  scalar helpers like `pdf_cosine_hemisphere`)
  is single-file. Following its pattern keeps
  the module count parsimonious.
- The implementer MAY ship a trivial `Mis.cuh`
  re-exporting `Mis.h` if naming consistency
  with `Bsdf.{h,cuh}` and `DirectLight.{h,cuh}`
  is preferred — both options pass the brief.

The PASS criteria (§5) accept either approach.

---

## 2. Expected behavior

The four contractual properties the helper must
honour, matching the user's enumerated bullets:

### 2.1 Stable for zero PDFs

`power_heuristic(p_a, p_b)` returns a finite,
non-NaN, non-inf value for ALL non-negative
finite-float inputs, including the degenerate
zero cases:

| Input              | Output | Reasoning                                           |
|--------------------|-------:|-----------------------------------------------------|
| `(0, 0)`           | `0.0f` | denominator is zero; explicit zero return          |
| `(0, p_b > 0)`     | `0.0f` | numerator is zero; A-estimator can't sample here   |
| `(p_a > 0, 0)`     | `1.0f` | denominator collapses to `p_a²`; ratio is 1        |

The `(0, 0)` case is the only one requiring a
guard. The natural arithmetic (`p_a² /
(p_a² + p_b²)`) produces `0/0 = NaN` in IEEE-
754. The helper MUST guard against this:

```cpp
RR_HD inline float power_heuristic(float p_a, float p_b) {
    const float pa2 = p_a * p_a;
    const float pb2 = p_b * p_b;
    const float denom = pa2 + pb2;
    return denom > 0.0f ? pa2 / denom : 0.0f;
}
```

The `denom > 0.0f` test catches both finite-
zero (both inputs zero) and the rare denormal-
underflow case where `pa2 + pb2` rounds to
zero. The `0.0f` return is the conventional
"no contribution from this estimator" signal.

### 2.2 Returns sane weights

For any `(p_a, p_b)` with at least one positive:
- Result is in `[0.0f, 1.0f]` inclusive (the
  Veach weight is a probability).
- Sum-to-one invariant:
  `power_heuristic(p_a, p_b) +
  power_heuristic(p_b, p_a) == 1.0f` exactly.
- Equal-PDFs case: `power_heuristic(x, x) ==
  0.5f` for any positive `x` (the two
  estimators are equally informative).
- Symmetry: `power_heuristic(a, b) == 1.0f -
  power_heuristic(b, a)` for any non-zero
  inputs.
- Monotonicity: `p_a > p_a' && p_b == p_b'`
  ⇒ `power_heuristic(p_a, p_b) >=
  power_heuristic(p_a', p_b')` (more weight
  to a more-informative estimator).
- One-dominates limit: as `p_a / p_b → ∞`,
  `power_heuristic(p_a, p_b) → 1.0f` (the
  high-PDF estimator dominates; the low-PDF
  estimator's contribution is downweighted).

These properties are the formal correctness
contract. The helper-host tests (§5.5) anchor
each one.

### 2.3 Host/device usable (RR_HD inline)

The helper is `RR_HD inline` per the
established
`pathtracer/{RNG,Sampling,DirectLight,Bsdf}`
convention. The same code compiles for:

- Host C++ (the test binary; future runtime
  diagnostics).
- CUDA device code (MIS.5
  `k_pathtrace_sample` consumes it per-
  bounce).
- OptiX device code (MIS.6
  `__raygen__pathtrace` consumes it per-
  bounce).

`RR_HD` is defined in `src/math/MathUtils.h`
to expand to `__host__ __device__` under
nvcc and to nothing under host compilers.
The helper MUST not include any CUDA-
specific intrinsics; the formula uses only
multiplication, addition, comparison, and
conditional return — all RR_HD-safe.

### 2.4 Pure function (deterministic)

`power_heuristic(p_a, p_b)` consumes only its
arguments; no global / TLS state; no I/O. The
same arguments produce bit-equal output across
calls. This is the same purity invariant the
existing MIS-arc helpers (`sample_bsdf`,
`bsdf_pdf`, `sample_direct_light_uniform`)
established at MIS.{2,3}.

The host-only test (§5.5) anchors purity via
`memcmp` on the float bit pattern across two
calls with identical inputs.

### 2.5 Dirac-sentinel handling lives at the caller

The MIS plan §3.3 + the MIS.3 task brief §2.2
established the convention that delta lights
(Point, Directional) carry `is_delta == true`
on `DirectLightSample`. The MIS-aware integrator
checks `is_delta` FIRST:

- `is_delta == true`: skip the `power_heuristic`
  call entirely; the NEE-side weight is `1.0`
  (Veach 1995 §10.3 delta-light convention).
- `is_delta == false`: call
  `power_heuristic(p_light, p_bsdf)` to compute
  the weight from the actual PDFs.

**The helper itself does NOT know about delta
lights.** It treats every input as a finite
PDF. The integrator's job is to short-circuit
delta cases before the call. This separation
keeps the helper pure-math + sub-100-line.

If a future caller passes the v1 delta sentinel
`p_light = 0.0f` to the helper (e.g., misuse),
the helper returns `0.0f` for `w_NEE` — silently
corrupting the integrator's MIS-aware
estimator. The test suite catches the symptom
(zero-PDF input → zero output for that
estimator) but does NOT enforce correct usage.
Documentation in the helper's doc-comment
block MUST flag the caller-responsibility
contract.

---

## 3. Files likely involved

The implementation slice will touch this file
set:

| File                                   | Change                                                              |
|----------------------------------------|---------------------------------------------------------------------|
| `src/pathtracer/Mis.h`                 | NEW (~30-50 lines). RR_HD inline `power_heuristic(float p_a, float`|
|                                        | `p_b)` with a doc-comment block walking the §2 contract +          |
|                                        | cross-references to the plan + this brief. Implementation per      |
|                                        | §2.1 (3 lines of body + the guard).                                |
| `src/pathtracer/Mis.cuh` (OPTIONAL)    | OPTIONAL +~10 lines. Trivial re-export of `Mis.h` if the           |
|                                        | implementer prefers naming consistency with `Bsdf.{h,cuh}` /       |
|                                        | `DirectLight.{h,cuh}`. Brief recommends single-file `Mis.h` per   |
|                                        | §1.3; either choice passes.                                        |
| `tests/pathtracer_mis_tests.cpp`       | NEW (~100-150 lines). Host-only RR_CHECK-based test framework     |
|                                        | (mirrors `tests/pathtracer_bsdf_tests.cpp` shape) covering the    |
|                                        | invariants enumerated in §5.5.                                     |
| `CMakeLists.txt`                       | +~5-10 lines. Wires the new test binary alongside the existing    |
|                                        | `pathtracer_bsdf_tests` block. Linkage: standard                   |
|                                        | `target_link_libraries(... PRIVATE rr_pathtracer)`.                |
| `docs/BUILD_PLAN.md`                   | Slice-closing entry per the established narrow-column format.     |

### 3.1 Helper signature (target shape)

```cpp
// src/pathtracer/Mis.h
#pragma once

#include "math/MathUtils.h"  // RR_HD

namespace rr::pathtracer {

// Veach 1995 §9.2.4 power heuristic with β = 2 for
// two estimators with one sample each. Returns the
// MIS weight on the first estimator's contribution.
//
// (signature; implementation per §2.1 with the
// `denom > 0.0f` guard.)
RR_HD inline float power_heuristic(float p_a, float p_b);

}  // namespace rr::pathtracer
```

The doc-comment block above the helper covers:
- The Veach formula + β=2 choice rationale.
- Per-input contract (non-negative finite
  floats).
- The §2.1 stable-zero behaviour.
- The §2.5 caller-responsibility note (Dirac
  short-circuit happens before the call).
- Cross-references to the plan + this brief.
- A short usage example showing the
  symmetric `w_a + w_b` pattern.

### 3.2 Test file shape

`tests/pathtracer_mis_tests.cpp` mirrors
`tests/pathtracer_bsdf_tests.cpp` byte-for-
byte in framework idiom:

- `#include "pathtracer/Mis.h"` plus the
  standard `<cstdio>`, `<cstring>`, `<cmath>`
  headers.
- Hand-rolled RR_CHECK macro + per-case
  counters + `int main()` registry.
- `cli_tests` / `pathtracer_nee_tests` /
  `pathtracer_bsdf_tests`-style fail-message
  format.
- Final `pathtracer_mis_tests: N/N passed`
  line on stderr.

CMake wiring mirrors the existing
`pathtracer_bsdf_tests` block at
`CMakeLists.txt:680-697`:

```cmake
add_executable(pathtracer_mis_tests tests/pathtracer_mis_tests.cpp)
target_link_libraries(pathtracer_mis_tests PRIVATE rr_pathtracer)
rr_apply_warnings(pathtracer_mis_tests)
add_test(NAME pathtracer_mis_tests COMMAND pathtracer_mis_tests)
```

The test binary needs no extra link-time
dependencies beyond `rr_pathtracer`.

---

## 4. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 4.1 The integrators

- `src/cuda/CudaPathTracer.{cu,cuh}` — every
  byte. The MIS-aware integrator change lands
  at MIS.5; not this slice.
- `src/optix/OptixPrograms.cu` — every byte.
  Same as above for the OptiX raygen.
- `src/optix/OptixRenderer.{h,cpp}`,
  `src/optix/OptixLaunchParams.h`,
  `src/optix/OptixPipeline.{h,cpp}`,
  `src/optix/OptixSBT.h`,
  `src/optix/OptixDenoiser.{h,cpp}`,
  `src/optix/OptixBackend.{h,cpp}`,
  `src/optix/OptixAccel.{h,cpp}` — every byte.

### 4.2 The pathtracer module's existing surfaces

- `src/pathtracer/RNG.{h,cuh}` — every byte.
  The MIS helper consumes no random numbers;
  it is purely deterministic from its
  arguments.
- `src/pathtracer/Sampling.{h,cuh}` — every
  byte.
- `src/pathtracer/Bsdf.{h,cuh}` (MIS.2) —
  every byte.
- `src/pathtracer/DirectLight.{h,cuh}` (MIS.3)
  — every byte. The helper does NOT add a
  `power_heuristic` call site to either
  module; it ships standalone in `Mis.h`.
- `src/pathtracer/PathTracer.{h,cpp}` — every
  byte.

### 4.3 The CLI / Config / main.cpp surfaces

- `src/core/Config.h`, `src/core/Config.cpp`,
  `src/core/CommandLine.h`,
  `src/core/CommandLine.cpp`,
  `src/core/Logger.{h,cpp}` — every byte. No
  new CLI flag.
- `src/main.cpp` — every byte.

### 4.4 The renderer / scene / material modules

- `src/renderer/`, `src/io/`, `src/scene/`,
  `src/material/`, `src/lighting/`,
  `src/texture/`, `src/gpu/`, `src/server/`
  — every byte. The MIS helper has no
  upstream consumer.

### 4.5 Tests + scenes + tooling

- `tests/cli_tests.cpp`,
  `tests/pathtracer_tests.cpp`,
  `tests/math_tests.cpp`,
  `tests/image_tests.cpp`,
  `tests/gpu_tests.cpp`,
  `tests/relativity_tests.cpp`,
  `tests/demo_tests.cpp`,
  `tests/renderer_tests.cpp`,
  `tests/optix_tests.cpp`,
  `tests/pathtracer_nee_tests.cpp`,
  `tests/pathtracer_bsdf_tests.cpp` — every
  byte. ONLY `tests/pathtracer_mis_tests.cpp`
  is NEW.
- `scenes/*.rrscene` — every byte.
- `tools/verify_cuda_host.py` — every byte.

### 4.6 Default behaviour

For an operator running ANY action with ANY
combination of existing flags
(`--enable-nee`, `--firefly-clamp`, etc.),
the rendered PPM is bit-identical with the
post-MIS.3-audit baseline at commit
`960c523`. The structural argument: no
caller invokes `power_heuristic` in this
slice; every existing kernel / dispatcher /
helper is byte-identical.

---

## 5. PASS criteria

The implementation slice passes when ALL of
the following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF):
  clean build, zero new compiler warnings.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON
  with SDK fallback): clean build, zero
  new warnings.

### 5.2 Tests

- `ctest --output-on-failure` from
  `build`: 100% green. Count grows
  from 10 to **11** (+1
  `pathtracer_mis_tests` binary).
- `ctest --output-on-failure` from
  `build-ON`: 100% green. Count grows
  from 11 to **12**.
- `pathtracer_mis_tests` per-case count:
  **at least 8** RR_CHECK case functions
  per §5.5 below.
- All other test binaries' per-case
  counts unchanged: `cli_tests` 31/31;
  `pathtracer_nee_tests` 53/53;
  `pathtracer_bsdf_tests` 41/41; etc.

### 5.3 Source diff size

Per `docs/PATH_TRACER_MIS_PLAN.md` §7.3
(MIS.4 budget): ≤ 200 lines added across
the four authorised files (`Mis.h`,
optionally `Mis.cuh`,
`pathtracer_mis_tests.cpp`,
`CMakeLists.txt`).

Suggested per-file budget:
- `Mis.h`: ~30-60 lines (helper + doc-
  comment block).
- `Mis.cuh` (optional): ~10-15 lines.
- `tests/pathtracer_mis_tests.cpp`:
  ~150-250 lines (8+ cases + framework
  scaffold).
- `CMakeLists.txt`: ~5-10 lines.
- TOTAL: ~200-330 lines.

If the slice overshoots ≤200 (likely due
to doc-comment density per the
established PT-P.x / NEE.x / MIS.{2,3}
deviation pattern), the deviation is
flagged in the BUILD_PLAN entry per the
established narrative.

### 5.4 No-touch invariants

`git diff` after the slice MUST show
zero bytes changed in:

```
src/cuda/  src/optix/  src/renderer/  src/io/
src/scene/  src/material/  src/lighting/  src/texture/
src/gpu/  src/server/  src/main.cpp
src/core/  src/pathtracer/RNG.{h,cuh}
src/pathtracer/Sampling.{h,cuh}
src/pathtracer/Bsdf.{h,cuh}
src/pathtracer/DirectLight.{h,cuh}
src/pathtracer/PathTracer.{h,cpp}
tests/cli_tests.cpp  tests/pathtracer_tests.cpp
tests/math_tests.cpp  tests/image_tests.cpp
tests/gpu_tests.cpp  tests/relativity_tests.cpp
tests/demo_tests.cpp  tests/renderer_tests.cpp
tests/optix_tests.cpp
tests/pathtracer_nee_tests.cpp
tests/pathtracer_bsdf_tests.cpp
scenes/  tools/verify_cuda_host.py
```

Verifiable via the standard
`git diff -- <paths> | wc -l` ⇒ 0
invariant.

### 5.5 Helper-host test coverage

`tests/pathtracer_mis_tests.cpp` MUST
contain at least the following eight
case functions:

1. **`test_power_heuristic_both_zero_returns_zero`**:
   `power_heuristic(0.0f, 0.0f) ==
   0.0f`. Anchors the §2.1
   denominator-zero guard. The natural
   `0/0` would be NaN; the guard MUST
   catch this.

2. **`test_power_heuristic_p_a_zero`**:
   `power_heuristic(0.0f, x) == 0.0f`
   for any `x > 0`. Anchors the "A
   estimator can't sample here" case.
   Test against several `x` values
   (e.g. `0.1`, `1.0`, `100.0`).

3. **`test_power_heuristic_p_b_zero`**:
   `power_heuristic(x, 0.0f) == 1.0f`
   for any `x > 0`. Anchors the "all
   weight to A" case. Test against
   several `x` values.

4. **`test_power_heuristic_equal_pdfs`**:
   `power_heuristic(x, x) == 0.5f` for
   any positive `x` (using a small
   epsilon for FP equality). Test
   against several `x` values
   (e.g. `0.1`, `1.0`, `100.0`).

5. **`test_power_heuristic_squares_pdfs`**:
   verify the β=2 behaviour explicitly.
   E.g. `power_heuristic(2.0, 1.0) ==
   4.0 / 5.0 == 0.8f` (within FP eps).
   This case MUST fail if the implementer
   accidentally writes the β=1 (balance)
   heuristic, which would return
   `2.0 / 3.0 ≈ 0.667`.

6. **`test_power_heuristic_one_dominates`**:
   as `p_a / p_b → ∞`,
   `power_heuristic(p_a, p_b) → 1.0f`.
   Test with `(1e6, 1e-3)` and similar
   ratios; assert `> 0.999f`.

7. **`test_power_heuristic_sum_to_one`**:
   for non-zero inputs,
   `power_heuristic(p_a, p_b) +
   power_heuristic(p_b, p_a)` is exactly
   `1.0f` via `std::memcmp` on the float
   bit pattern. Test across several
   `(p_a, p_b)` pairs. Anchors the
   §2.2 sum-to-one invariant which the
   future MIS integrator relies on for
   unbiased estimator combination.

8. **`test_power_heuristic_purity`**:
   calling the helper twice with
   identical inputs returns bit-equal
   floats via `std::memcmp`. Anchors
   §2.4 (no hidden state). Mirrors
   `pathtracer_nee_tests::test_helper_determinism`
   and
   `pathtracer_bsdf_tests::test_helper_determinism`.

The implementer MAY add more cases (e.g.
monotonicity over a sweep; explicit
Veach §9.2.4 reference values from PBRT
or Mitsuba's test suites; comparison
against a reference Python implementation
of the formula). Minimum 8 cases per the
PASS criterion above.

### 5.6 Documentation

- `docs/BUILD_PLAN.md` carries a new
  slice-closing entry matching the
  established narrow-column format
  (Scope / What ships / What does NOT
  change / Master rule compliance /
  Verified at the build).
- The entry references this task brief
  + `docs/PATH_TRACER_MIS_PLAN.md` §3.3
  + §5.3 as the source of the spec.
- The helper's doc-comment block in
  `Mis.h` cross-references both for the
  arc-level + slice-level context.

### 5.7 Master rule compliance

- Build incrementally (rule 1) + every
  step compilable (rule 2): both audit-
  host configs green; ctest green.
- No fake stubs (rule 3): the helper is
  a real Veach β=2 power-heuristic
  implementation; no TODO branches.
- No CPU per-pixel work (rule 5/7): the
  helper is RR_HD inline; per-pixel
  consumption happens device-side at
  MIS.5 / MIS.6 (future slices). Host
  code in this slice is the test
  framework only.
- Module boundaries (rule 9): the new
  module sits cleanly alongside
  `pathtracer/{DirectLight,RNG,Sampling,Bsdf}.h`.
  No cross-module ripple.
- Avoid monolithic files (rule 10): the
  new file is small (~30-60 lines).
- Explicit testable interfaces (rule
  11): the helper is host-callable +
  host-tested.
- Update BUILD_PLAN (rule 8): the
  slice-closing entry.

---

## 6. Out-of-scope (deferred to future slices)

The following items are explicitly NOT
part of MIS.4:

1. **Balance heuristic (β=1).** The
   plan §3.3 chose β=2 (the production-
   standard choice; PBRT, Mitsuba,
   Cycles all default to β=2). A
   future slice could expose β as a
   template parameter or an additional
   helper if needed; out of scope here.

2. **Multi-estimator power heuristic
   (N > 2).** The Veach formula
   generalises to `Σ_j n_j · p_j^β` in
   the denominator. The v1 use case is
   exactly two estimators (NEE + BSDF)
   per bounce, so the brief specs the
   two-argument form. A future slice
   adding env-IBL NEE alongside the
   existing NEE branch might need the
   N-argument form; out of scope here.

3. **Variable-sample-count weighting.**
   The Veach formula's `n_i` weights
   become non-trivial when estimators
   take different sample counts. The
   v1 path tracer takes one sample
   per estimator per bounce, so `n_i =
   1`; out of scope here.

4. **NaN / inf input handling.** The
   helper's contract (§2.1, §2.3) is
   "non-negative finite floats". The
   caller is responsible for ensuring
   inputs satisfy the contract. NaN /
   inf inputs are out-of-contract; the
   helper's behaviour on them is
   undefined (likely returns NaN /
   propagates inf). Out of scope to
   guard against caller misuse.

5. **Overflow protection.** `p_a²`
   could overflow for `p_a > ~1e19`
   (single-precision float maxes at
   ~3.4e38 so `(1e19)² ≈ 1e38` is
   borderline). The v1 PDFs are
   bounded (Lambert: ≤ 1/π ≈ 0.318;
   delta sentinel: 0.0; future area-
   light Jacobian: bounded by scene
   geometry). No production renderer
   produces PDFs > 1e10 for typical
   scenes. Out of scope here; can be
   added later as a clamp + rescale
   step if needed.

6. **Integrator wiring.** The MIS-
   aware integrator changes
   (MIS.5 CUDA, MIS.6 OptiX) are out
   of scope here. The new helper is
   inert until those slices land.

7. **CLI flag.** No `--mis` /
   `--no-mis` flag (per
   `docs/PATH_TRACER_MIS_PLAN.md` §6
   #8). MIS at v1 is a no-op (the
   delta-light short-circuit makes
   the BSDF-side weight 0 with
   probability 1); operator-facing
   surface is unnecessary.

8. **AOV exposure of the MIS weight.**
   Future slice if useful for
   debugging, but out of scope here.

---

## 7. Sub-arc context

### 7.1 Position in the MIS arc

Per `docs/PATH_TRACER_MIS_PLAN.md` §5 +
§8, the MIS arc cadence is:

```
MIS.1 (plan, docs only)           ─── shipped (commit 67dd03c)
MIS.2 (BSDF data model)           ─── shipped (commits d9fa6e3 + 5a1c772)
MIS.3 (Light data model)          ─── shipped (commit 0dd7d46) + audited (commit 960c523)
MIS.4 (MIS helper)                ─── this brief (THIS slice ships impl)
MIS.5 (CUDA integrator)           ─── depends on {2,3,4}; future slice
MIS.6 (OptiX integrator)          ─── depends on {5}; future slice
MIS.7 (audit)                     ─── depends on {2..6}; future slice
```

MIS.{2,3,4} are independent leaves. MIS.4
is the LAST leaf — once it lands, all
three foundational pieces (BSDF data
model, Light data model, MIS helper) are
shipped. MIS.5 (CUDA integrator) gates
on all three.

### 7.2 What this slice unblocks

- MIS.5 (CUDA integrator) gains the
  `power_heuristic(p_a, p_b)` it needs to
  weight the NEE + BSDF estimators in
  `k_pathtrace_sample`'s NEE branch.
- MIS.6 (OptiX integrator) gains the
  same.
- MIS.7 (arc-level audit) gains the
  final per-stage artefact to walk
  before the integrators land.
- Future MIS-consuming arcs (area-light
  NEE, env-IBL, etc.) inherit the
  battle-tested helper without
  reinventing the math.

### 7.3 What this slice does NOT unblock

- Area-light NEE (depends on a
  separate arc adding `Light::Area`
  plumbing).
- Cross-backend MIS convergence
  verification (needs MIS.5 + MIS.6 +
  CUDA / OptiX-SDK host).
- Specular delta MIS (needs the BSDF
  `is_delta` field, which MIS.2
  deferred per the user's narrow
  prompt; a future BSDF arc adds
  it).

### 7.4 Recommended audit cadence

The MIS.3 audit (commit `960c523`)
established a per-stage MIS audit
cadence that this slice should likely
follow: ship MIS.4 impl, then ship a
short MIS.4 audit before MIS.5 lands.
The audit's check list mirrors the
parallel MIS.3 audit:

1. `power_heuristic` helper exists.
2. Helper handles zero PDFs stably.
3. No render behavior changed (no
   integrator caller).
4. Build status (counts grow by +1
   binary).
5. PASS / REPAIR / BLOCKED verdict.

The MIS.4 audit is a NICE-TO-HAVE, not
a hard PASS criterion of THIS task
brief. The implementer / operator may
defer it to bundle with MIS.5 / MIS.6 /
MIS.7 if preferred.

---

## 8. Verdict

The brief is complete. The implementer
can ship MIS.4 end-to-end without re-
deriving any of the design reasoning.
The plan-level context (Veach β=2
choice; sum-to-one invariant) is in
`docs/PATH_TRACER_MIS_PLAN.md`; the
slice-level contract is in this file.

**Mode reminder: documentation only.**
This file is the spec. The next slice
(MIS.4 impl) ships the source diff +
the eight test cases + the BUILD_PLAN
entry.

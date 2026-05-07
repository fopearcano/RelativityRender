# BSDF Sampling + PDF Data Model — Task Definition (MIS.2)

Date: 2026-05-07.
Branch: `relativity-core-v1`.
Plan source:
`docs/PATH_TRACER_MIS_PLAN.md` §3.1, §3.5, §5.1
(BSDF PDF concept; `BsdfSample` POD shape;
"MIS.2 — BSDF PDF data model" stage definition).
Mode: documentation only. **No source code is
modified by this task definition.** The task
is the spec; the next slice (the implementation)
ships the diff.

This file is a fully-self-contained brief for
the next implementation slice. Anyone picking it
up should be able to ship MIS.2 without re-
deriving the design reasoning. Pattern mirrors
`docs/PATH_TRACER_NEE_TASK.md` /
`docs/PATH_TRACER_ENABLE_NEE_CLI_TASK.md` (the
canonical task-brief shape this repository
established for the NEE arc).

---

## 1. Exact goal

**Define the data needed for BSDF sampling and
PDF evaluation, ship the host/device-shared
helper module that produces it, and add host-
only tests verifying the helper's contract.**

The MIS arc (per
`docs/PATH_TRACER_MIS_PLAN.md`) needs FOUR
helpers exposed at the path-tracer integrator
boundary:

1. `sample_bsdf(material, wi, normal, u)` —
   produce one Monte Carlo sample of a
   bounce direction with its PDF.
2. `bsdf_pdf(material, wo, normal)` — evaluate
   the PDF of choosing direction `wo` given
   the surface; needed by the MIS helper to
   weight the NEE estimator's contribution
   against the BSDF estimator's.
3. `bsdf_eval(material, wi, wo, normal)` —
   evaluate the BRDF value at `(wi, wo)`;
   needed by the NEE branch (Lambert evaluated
   at the toward-light direction) and by the
   MIS-aware integrator's BSDF-bounce-as-
   light contribution.
4. `BsdfSample` POD that bundles the
   sampler's output: direction + PDF + value
   + classification flags.

MIS.2 ships ONLY the data model + helpers +
host-only tests for Lambert (the v1 BSDF).
**No integrator changes; no kernel changes.**
The CUDA `k_pathtrace_sample` and OptiX
`__raygen__pathtrace` continue to use their
existing inline cosine-bounce arithmetic.
MIS.5 (CUDA integrator) and MIS.6 (OptiX
integrator) replace the inline arithmetic
with calls to these helpers.

The helpers are scoped to Lambert because:

- Lambert is the only BSDF the v1 path tracer
  evaluates today.
- The MIS arc's apparatus is BSDF-agnostic
  (the integrator dispatches on whichever
  helpers fire); future BSDF arcs (GGX metal,
  dielectric, glass) extend `Bsdf.{h,cuh}`
  without integrator-level change.
- Shipping Lambert alone keeps MIS.2's diff
  budget tight (≤250 lines per
  `docs/PATH_TRACER_MIS_PLAN.md` §7.3).

The "data model" framing matters:
**MIS.2 is a no-op at runtime.** No caller
invokes the new helpers in this slice; the
integrator is unchanged; PPM bytes are
identical with the post-NEE-arc baseline at
`827f5de`. The new module sits in
`src/pathtracer/` waiting to be consumed at
MIS.5 + MIS.6.

---

## 2. Required concepts

The four concepts the user enumerated, each
with a concrete `BsdfSample` field + helper
contract.

### 2.1 Sampled direction (`BsdfSample::wo`)

The outgoing world-space direction the BSDF
sampler chose for the bounce. Unit vector;
lies in the upper hemisphere with respect
to the surface normal at the hit point.

For Lambert (v1): `sample_cosine_hemisphere`
produces a tangent-space direction with
`z >= 0` (cos-weighted). The helper aligns
it to the world-space normal via the same
`align_to_normal` function the existing
integrator uses (CUDA: `CudaPathTracer.cu`'s
inline lambda, OptiX:
`OptixPrograms.cu::pt_align_to_normal` at
line 790-799). The helper exposes the
aligned world-space `wo` directly so callers
do not re-implement the alignment.

For future BSDFs (out of scope per §4):
- **Specular delta**: `wo = reflect(wi,
  normal)` — the unique mirror-reflection
  direction.
- **GGX metal / dielectric**: `wo =
  reflect_or_refract(wi, sampled_microfacet,
  normal)` per the chosen importance-sample
  scheme.

### 2.2 BSDF value / throughput contribution (`BsdfSample::value`)

The BRDF evaluated at `(wi, wo)` — the
spectral-energy-per-solid-angle that scatters
from the incoming direction to the outgoing
direction at the surface. Vec3 (RGB).

For Lambert (v1): `value = baseColor / π`.
This is independent of `wi` and `wo` (the
classic Lambert isotropic-diffuse property);
the helper still takes the directions because
future BSDFs need them.

The integrator's per-bounce throughput
update is:

```
throughput *= (value · cos_theta_o) / pdf
```

where `cos_theta_o = max(0, dot(normal, wo))`.

For cosine-weighted Lambert sampling, this
simplifies analytically:

```
(baseColor/π · cos_theta_o) / (cos_theta_o/π)
   = baseColor
```

— which is exactly the existing integrator's
inline `throughput *= m.baseColor`. **The MIS
arc preserves this inline simplification's
result at v1**; the new helpers carry the
cancellation factors explicitly so the
integrator can compute the MIS weight (which
needs the un-cancelled PDF), and the post-
cancellation throughput multiplier remains
`baseColor` byte-for-byte.

### 2.3 BSDF PDF (`BsdfSample::pdf` + `bsdf_pdf` helper)

The probability density (per steradian) of
the sampler choosing direction `wo`. Used by
MIS to weight the BSDF-sampler estimator's
contribution against the NEE estimator's.

For Lambert (v1):

```
pdf = pdf_cosine_hemisphere(cos_theta_o)
    = max(0, cos_theta_o) / π
```

The `pdf_cosine_hemisphere` helper at
`src/pathtracer/Sampling.h:96-99` already
exists and is reused verbatim.

The helper has two surfaces:

- `BsdfSample::pdf` — the PDF the sampler
  produced for the chosen `wo`. Set when
  `sample_bsdf` returns the sample; the
  caller does not recompute it.
- `bsdf_pdf(material, wo, normal)` — a
  standalone helper evaluating the PDF for
  any `wo`, regardless of how it was
  produced. Needed by the MIS integrator to
  evaluate `p_bsdf` at the NEE-chosen
  direction (so the MIS weight on the NEE
  estimator can be computed).

The two surfaces must be consistent: for any
`(material, normal, u)` and the resulting
`wo = sample_bsdf(material, wi, normal, u).wo`,
the equality `BsdfSample::pdf ==
bsdf_pdf(material, wo, normal)` must hold
exactly. The helper-host test (§5.5) anchors
this invariant.

For future BSDFs (out of scope per §4):
- **Specular delta**: PDF is a Dirac delta;
  `pdf` is set to a positive sentinel (e.g.
  `1.0f`) and `is_delta` (§2.4) is set
  `true`. The MIS helper short-circuits on
  `is_delta` and never evaluates the
  sentinel.
- **GGX**: PDF is the microfacet
  distribution's sampling PDF (per the
  selected importance-sample scheme).

### 2.4 Valid sample flag + delta classification

Two boolean fields on `BsdfSample`:

#### 2.4.1 `BsdfSample::valid`

`true` iff the sampler produced a usable
sample with non-zero contribution. The
integrator checks this flag before using
the sample. `valid == false` cases (for any
BSDF):

- The sampled direction lies below the
  surface horizon (`cos_theta_o <= 0`).
  Cosine-hemisphere sampling never produces
  this for a healthy normal, but defence-
  in-depth guards against numerical edge
  cases (degenerate normal, near-grazing
  incident).
- The BSDF value is identically zero (e.g.
  a future opaque material's transmission
  lobe at zero transmission).
- The sampler's PDF is zero (degenerate
  case).

For Lambert (v1): `valid` is `true` whenever
`cos_theta_o > 0`, which is always after
cosine-hemisphere sampling + alignment.
Edge cases:
- `normal == (0,0,0)`: `align_to_normal`
  is undefined; `valid` set `false`.
- `cos_theta_o == 0` (numerical tie at the
  equator): `valid` set `false` to avoid
  divide-by-zero in the throughput update.

The integrator's contract:

```cpp
const auto s = sample_bsdf(material, wi, normal, u);
if (!s.valid) {
    // Skip this bounce; no contribution.
    break;
}
throughput *= (s.value * cos_theta_o) / s.pdf;
ray.direction = s.wo;
```

When `valid == false`, the integrator MUST
NOT consume `pdf` / `value` / `wo` (they
may carry sentinel values). The flag is
the single gate.

#### 2.4.2 `BsdfSample::is_delta`

`true` iff the BSDF lobe is a Dirac delta
(specular) — meaning the PDF is a delta
distribution, not a finite density. The MIS
helper short-circuits on this flag (delta
samples are MIS-weight `1.0` by convention
per Veach 1995 §10.3) and never tries to
evaluate `power_heuristic` on a delta PDF.

For Lambert (v1): `is_delta` is always
`false`. The field is structural — a
forward-looking placeholder for the future
specular / dielectric / glass arcs.

The two flags are independent:

| `valid` | `is_delta` | Meaning                                                  |
|--------:|----------:|----------------------------------------------------------|
| `true`  | `false`   | Diffuse / glossy sample; use PDF normally               |
| `true`  | `true`    | Specular sample; MIS-skip; throughput uses `value` only |
| `false` | (any)     | Skip this bounce entirely                                |
| `false` | `true`    | (degenerate; specular sampler produced no valid hit)    |

For v1 Lambert the only states reachable
are `(true, false)` (the normal case) and
`(false, false)` (the numerical-edge-case
fallback).

---

## 3. Files likely involved

The implementation slice will touch this
file set — three NEW files + one
`CMakeLists.txt` line addition. The new
module mirrors
`src/pathtracer/DirectLight.{h,cuh}`'s
`.h / .cuh` split exactly:

| File                                     | Change                                                           |
|------------------------------------------|------------------------------------------------------------------|
| `src/pathtracer/Bsdf.h`                  | NEW (~50 lines). Defines the `BsdfSample` POD with the four     |
|                                          | fields enumerated in §2 + a doc-comment block walking the       |
|                                          | contract per §2.1-§2.4.                                          |
| `src/pathtracer/Bsdf.cuh`                | NEW (~80 lines). RR_HD inline `sample_bsdf`, `bsdf_pdf`,        |
|                                          | `bsdf_eval` for Lambert. Re-exports the data type from `.h`.   |
| `tests/pathtracer_bsdf_tests.cpp`        | NEW (~120 lines). Host-only RR_CHECK-based test framework      |
|                                          | (mirrors `tests/pathtracer_nee_tests.cpp` shape) covering the   |
|                                          | invariants enumerated in §5 PASS criteria.                      |
| `CMakeLists.txt`                         | +5-10 lines. Wires the new test binary alongside the existing   |
|                                          | `pathtracer_nee_tests` block. Linkage: standard                  |
|                                          | `target_link_libraries(... PRIVATE rr_pathtracer)` (the helper  |
|                                          | is RR_HD inline + part of the existing pathtracer module).      |
| `docs/BUILD_PLAN.md`                     | Slice-closing entry following the established TEX-P.x /         |
|                                          | PT-P.x / NEE.x / MIS.1 narrow-column format.                    |

The `Bsdf.{h,cuh}` split pattern was
established by `DirectLight.{h,cuh}` at
NEE.2: the `.h` declares the POD (host-
friendly include surface; no CUDA
dependencies), the `.cuh` provides the
RR_HD inline helpers (callable from CUDA
device code, OptiX device code, or host
C++ test code). The MIS.2 module follows
this convention verbatim.

### 3.1 Helper signatures (target shapes)

The implementer fills the doc-comment text
+ implementation body; the brief specifies
only the signatures.

```cpp
// src/pathtracer/Bsdf.h
namespace rr::pathtracer {

struct BsdfSample {
    rr::math::Vec3 wo              = {0.0f, 0.0f, 0.0f};  // §2.1
    float          pdf             = 0.0f;                // §2.3
    rr::math::Vec3 value           = {0.0f, 0.0f, 0.0f};  // §2.2 (BRDF eval)
    bool           valid           = false;               // §2.4.1
    bool           is_delta        = false;               // §2.4.2
};

}  // namespace rr::pathtracer
```

```cpp
// src/pathtracer/Bsdf.cuh
#pragma once
#include "material/MaterialTypes.h"
#include "math/MathUtils.h"     // RR_HD + kInvPi
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "pathtracer/Bsdf.h"
#include "pathtracer/Sampling.h" // sample_cosine_hemisphere + pdf_cosine_hemisphere

namespace rr::pathtracer {

// Lambert BSDF sample. wi unused (Lambert is rotation-
// invariant); kept in the signature for forward-
// compatibility with future BSDFs.
RR_HD inline BsdfSample sample_bsdf(
        const rr::material::MaterialParams& m,
        rr::math::Vec3 /*wi*/,
        rr::math::Vec3 normal,
        rr::math::Vec2 u);

// Lambert BSDF PDF at an arbitrary outgoing direction.
// Returns 0 when wo is below the surface horizon.
RR_HD inline float bsdf_pdf(
        const rr::material::MaterialParams& m,
        rr::math::Vec3 wo,
        rr::math::Vec3 normal);

// Lambert BSDF value (rotation-invariant, so wi / wo
// only matter through the cos_theta gate). The integrator
// uses this for the NEE branch's BRDF eval at the
// toward-light direction.
RR_HD inline rr::math::Vec3 bsdf_eval(
        const rr::material::MaterialParams& m,
        rr::math::Vec3 /*wi*/,
        rr::math::Vec3 wo,
        rr::math::Vec3 normal);

}  // namespace rr::pathtracer
```

### 3.2 Tangent-space alignment

`sample_bsdf` returns `wo` in **world
space**. The implementation aligns the
tangent-space cosine-hemisphere sample to
the world-space normal via the same
algorithm the existing integrators use:

```cpp
// (target shape; final wording at impl.)
const Vec3 local = sample_cosine_hemisphere(u);
// Build a tangent frame from the normal:
const Vec3 helper = (fabsf(normal.z) < 0.999f)
                  ? Vec3{0.0f, 0.0f, 1.0f}
                  : Vec3{1.0f, 0.0f, 0.0f};
const Vec3 t = normalize(cross(helper, normal));
const Vec3 b = cross(normal, t);
const Vec3 wo_world = t * local.x + b * local.y + normal * local.z;
```

This is the same `align_to_normal` shape
both backends already implement inline:
- CUDA: `CudaPathTracer.cu`'s inline lambda
  at the bounce site.
- OptiX: `OptixPrograms.cu::pt_align_to_normal`
  at line 790-799.

The MIS.2 helper centralises this so the
MIS.5 / MIS.6 integrators do not duplicate
it. The two existing inline copies remain
untouched at MIS.2 (per §4 must-not-touch);
they are replaced at MIS.5 / MIS.6.

### 3.3 Test file shape

`tests/pathtracer_bsdf_tests.cpp` mirrors
`tests/pathtracer_nee_tests.cpp` byte-for-
byte in framework idiom:

- `#include "pathtracer/Bsdf.cuh"` + the
  same `math/`, `material/MaterialTypes.h`
  includes.
- Hand-rolled RR_CHECK macro + per-case
  counters + `int main()` registry.
- `cli_tests` / `pathtracer_nee_tests`-
  style fail message format ("FAIL: %s
  (%s:%d)").
- Final `cli_tests: N/N passed` line on
  stderr.

CMake wiring mirrors the existing
`pathtracer_nee_tests` block at
`CMakeLists.txt:676-679`:

```cmake
add_executable(pathtracer_bsdf_tests tests/pathtracer_bsdf_tests.cpp)
target_link_libraries(pathtracer_bsdf_tests PRIVATE rr_pathtracer)
rr_apply_warnings(pathtracer_bsdf_tests)
add_test(NAME pathtracer_bsdf_tests COMMAND pathtracer_bsdf_tests)
```

The test binary needs no extra link-time
dependencies beyond `rr_pathtracer`. The
`MaterialParams` POD lives in
`src/material/MaterialTypes.h`, which is
header-only and on the existing INTERFACE
include path of `rr_pathtracer`.

---

## 4. What must not be touched

The implementation slice MUST keep the
following byte-identical:

### 4.1 The integrators

- `src/cuda/CudaPathTracer.{cu,cuh}` —
  every byte. The existing inline cosine-
  bounce arithmetic continues to fire;
  MIS.5 (a future slice) replaces it.
  This MIS.2 slice does not.
- `src/optix/OptixPrograms.cu` — every
  byte. Same as above for the OptiX
  raygen.
- `src/optix/OptixRenderer.{h,cpp}`,
  `src/optix/OptixLaunchParams.h`,
  `src/optix/OptixPipeline.{h,cpp}`,
  `src/optix/OptixSBT.h`,
  `src/optix/OptixDenoiser.{h,cpp}`,
  `src/optix/OptixBackend.{h,cpp}`,
  `src/optix/OptixAccel.{h,cpp}` — every
  byte. No POD layout / dispatcher /
  pipeline change.

### 4.2 The pathtracer module's existing surfaces

- `src/pathtracer/RNG.{h,cuh}` — every
  byte. The MIS.2 helper consumes a
  pre-computed `Vec2 u` from the caller;
  it does not invoke `next_float` /
  `next_vec2` itself.
- `src/pathtracer/Sampling.{h,cuh}` — every
  byte. The MIS.2 helper REUSES
  `sample_cosine_hemisphere` and
  `pdf_cosine_hemisphere`; it does not
  modify them.
- `src/pathtracer/DirectLight.{h,cuh}` —
  every byte. The light-side data model
  (`DirectLightSample`,
  `sample_direct_light_uniform`) is
  extended at MIS.3 (a future slice),
  not this one.
- `src/pathtracer/PathTracer.{h,cpp}` —
  every byte. `PathTraceConfig` is
  unchanged. The orchestration's spp loop
  is unchanged.

### 4.3 The CLI / Config / main.cpp surfaces

- `src/core/Config.h`, `src/core/Config.cpp`,
  `src/core/CommandLine.h`,
  `src/core/CommandLine.cpp`,
  `src/core/Logger.{h,cpp}` — every byte.
  No new CLI flag in this slice. (No CLI
  flag is planned for the entire MIS arc
  per `PATH_TRACER_MIS_PLAN.md` §6.)
- `src/main.cpp` — every byte. No
  dispatcher changes.

### 4.4 The renderer / scene / material modules

- `src/renderer/`, `src/io/`, `src/scene/`,
  `src/material/`, `src/lighting/`,
  `src/texture/`, `src/gpu/`, `src/server/`
  — every byte. The MIS.2 helper consumes
  `MaterialParams` by const-reference; it
  does NOT modify the POD. No upload
  contract changes.

### 4.5 Tests + scenes + tooling

- `tests/cli_tests.cpp`,
  `tests/pathtracer_nee_tests.cpp`,
  `tests/pathtracer_tests.cpp`,
  `tests/math_tests.cpp`,
  `tests/image_tests.cpp`,
  `tests/gpu_tests.cpp`,
  `tests/relativity_tests.cpp`,
  `tests/demo_tests.cpp`,
  `tests/renderer_tests.cpp`,
  `tests/optix_tests.cpp` — every byte.
  Only `tests/pathtracer_bsdf_tests.cpp`
  is NEW.
- `scenes/*.rrscene` — every byte. The
  helper does not author fixtures.
- `tools/verify_cuda_host.py` — every
  byte. The runner exercises rendering
  actions; the new helper has no caller
  in the runner's command catalogue.

### 4.6 Documentation

- Every prior PT-P.x / TEX-P.x / NEE.x /
  CUDA-H.x / firefly-clamp / MIS.1
  doc — every byte. Only
  `docs/BUILD_PLAN.md` grows with the
  slice-closing entry.

### 4.7 Build configs

- `CMakeLists.txt`'s `add_executable(RelativityRender
  ...)` source list — every byte. The MIS.2
  module is RR_HD inline; the executable
  does not need to compile any new `.cpp`
  file. Only the new test-binary
  `add_executable(pathtracer_bsdf_tests
  ...)` block is added.

### 4.8 Default behaviour

- For an operator running ANY action with
  ANY combination of existing flags
  (`--enable-nee`, `--firefly-clamp`,
  `--render-pathtrace`,
  `--render-optix-pathtrace`, etc.), the
  rendered PPM is bit-identical with the
  post-NEE-arc baseline at commit
  `827f5de`. The structural argument:
  no caller invokes the MIS.2 helpers;
  the integrators use their pre-existing
  inline cosine-bounce arithmetic; no
  per-pixel arithmetic changes.

---

## 5. PASS criteria

The implementation slice passes when ALL
of the following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF):
  clean build, zero new compiler warnings.
  The new test binary builds without
  pulling extra dependencies.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON
  with SDK fallback): clean build, zero
  new warnings.

### 5.2 Tests

- `ctest --output-on-failure` from `build`:
  100% green, **count grows from 9 to 10**
  (+1 `pathtracer_bsdf_tests` binary).
- `ctest --output-on-failure` from
  `build-ON`: 100% green, **count grows
  from 10 to 11**.
- The new `pathtracer_bsdf_tests` binary's
  per-case count: **at least 10** RR_CHECK
  cases per §5.5 below (the implementer
  may add more; minimum 10).
- All existing test binaries' per-case
  counts unchanged: `cli_tests` 31/31;
  `pathtracer_nee_tests` 34/34; etc.

### 5.3 Source diff size

Per `PATH_TRACER_MIS_PLAN.md` §7.3: ≤ 250
lines added across the four authorised
files (no `src/cuda/`, `src/optix/`,
`tests/pathtracer_*_tests.cpp` other than
the new file, etc.).

Suggested per-file budget:
- `src/pathtracer/Bsdf.h`: ~30-60 lines
  (POD + doc-comment).
- `src/pathtracer/Bsdf.cuh`: ~60-100 lines
  (helper + doc-comment).
- `tests/pathtracer_bsdf_tests.cpp`:
  ~100-150 lines (10+ cases + framework
  scaffold).
- `CMakeLists.txt`: ~5-10 lines (test
  binary block).

Anything LARGER flagged in the BUILD_PLAN
entry as a deviation, per the established
PT-P.x / NEE.x / MIS.1 deviation-note
pattern.

### 5.4 No-touch invariants

`git diff` after the slice MUST show zero
bytes changed in:

```
src/cuda/  src/optix/  src/renderer/  src/io/
src/scene/  src/material/  src/lighting/  src/texture/
src/gpu/  src/server/  src/main.cpp
src/core/  src/pathtracer/RNG.{h,cuh}
src/pathtracer/Sampling.{h,cuh}
src/pathtracer/DirectLight.{h,cuh}
src/pathtracer/PathTracer.{h,cpp}
tests/cli_tests.cpp  tests/pathtracer_nee_tests.cpp
tests/pathtracer_tests.cpp  tests/math_tests.cpp
tests/image_tests.cpp  tests/gpu_tests.cpp
tests/relativity_tests.cpp  tests/demo_tests.cpp
tests/renderer_tests.cpp  tests/optix_tests.cpp
scenes/  tools/verify_cuda_host.py
```

Verifiable via the standard
`git diff -- <paths> | wc -l` ⇒ 0
invariant the prior PT-P.x / NEE.x /
MIS.1 audits use.

### 5.5 Helper-host test coverage

`tests/pathtracer_bsdf_tests.cpp` MUST
contain at least the following cases:

1. **`test_default_constructed_sample_is_invalid`**:
   `BsdfSample{}` (default-constructed) has
   `valid == false`, `pdf == 0.0f`,
   `is_delta == false`, and bit-zero
   `wo`/`value` (the NEE.5 byte-identity
   anchor pattern — `std::memcmp` against
   a default-constructed instance returns
   0).
2. **`test_lambert_sample_in_upper_hemisphere`**:
   for a typical material (`baseColor =
   (0.8, 0.8, 0.8)`) + a unit normal
   `(0, 1, 0)` + sampled `u = (0.5, 0.5)`,
   the returned `wo` has `dot(wo, normal)
   > 0` (upper-hemisphere) and `valid ==
   true` and `is_delta == false`.
3. **`test_lambert_pdf_matches_sampler`**:
   for a representative `(material,
   normal, u)`, the equality
   `BsdfSample::pdf == bsdf_pdf(material,
   sample.wo, normal)` holds via
   `std::memcmp` on the float bits (the
   §2.3 consistency invariant).
4. **`test_lambert_pdf_below_horizon_is_zero`**:
   `bsdf_pdf(material, wo_below_normal,
   normal) == 0.0f` for any `wo` with
   `dot(wo, normal) < 0`. Anchors the
   §2.3 below-horizon contract.
5. **`test_lambert_eval_matches_inverse_pi_albedo`**:
   `bsdf_eval(material, wi, wo, normal) ==
   material.baseColor * kInvPi` for any
   non-degenerate inputs. Anchors the
   §2.2 BRDF formula.
6. **`test_lambert_throughput_simplification`**:
   for a Lambert sample, the integrator's
   throughput-update product
   `(value * cos_theta_o) / pdf` equals
   `material.baseColor` (up to a small
   epsilon). Anchors the analytical
   simplification §2.2 documented; this
   is what guarantees byte-identity at
   the integrator level when MIS.5 lands.
7. **`test_lambert_sample_pdf_normalises_via_monte_carlo`**:
   `Σ_i (1 / pdf_i)` over `N` samples
   approximates the unit-hemisphere area
   `2π` within sampling noise (e.g. 10%
   relative error at `N = 10^4`). Anchors
   that the PDF integrates to 1 over the
   upper hemisphere (the formal
   normalisation requirement; statistical
   test).
8. **`test_lambert_cos_weighted_mean_dz`**:
   the mean `dot(sample.wo, normal)` over
   `N` samples approximates `2/3` (the
   cos-weighted hemisphere mean). Mirrors
   `tests/pathtracer_tests.cpp::test_cosine_hemisphere_distribution`'s
   shape.
9. **`test_degenerate_normal_returns_invalid`**:
   for `normal == (0, 0, 0)` (degenerate
   input), the sample has `valid ==
   false`. Anchors the §2.4.1 defence-
   in-depth contract.
10. **`test_helper_determinism`**:
    `sample_bsdf(m, wi, n, u)` called
    twice with the same inputs returns
    bit-equal samples (`std::memcmp`).
    Mirrors the
    `pathtracer_nee_tests::test_helper_determinism`
    pattern.

The implementer may add more cases (e.g.
roughness / metallic field independence;
the `wi`-independence invariant for
Lambert). Minimum 10 cases per the PASS
criterion above.

### 5.6 Documentation

- `docs/BUILD_PLAN.md` carries a new
  slice-closing entry matching the
  established TEX-P.x / PT-P.x / NEE.x /
  MIS.1 narrow-column format (Scope /
  What ships / What does NOT change /
  Master rule compliance / Verified at
  the build).
- The entry references this task brief +
  `docs/PATH_TRACER_MIS_PLAN.md` §3.5 +
  §5.1 as the source of the spec.
- The BSDF helper file's doc-comment
  block cross-references
  `docs/PATH_TRACER_MIS_PLAN.md` for the
  arc-level context + the `BsdfSample`
  field-level semantics from §3.5 of the
  plan (and §2 of this brief).

### 5.7 Master rule compliance

- Build incrementally (rule 1) + every
  step compilable (rule 2): both audit-
  host configs green; ctest green.
- No fake stubs (rule 3): the new
  `Bsdf.{h,cuh}` module's helpers are
  real Lambert implementations (PDF
  evaluator + sampler + BRDF eval); no
  TODO / unimplemented branches.
- No CPU per-pixel work (rules 5/7): the
  helpers are RR_HD inline; per-pixel
  consumption happens device-side at
  MIS.5 / MIS.6 (future slices). Host
  code in this slice is the test
  framework (one-shot per RR_CHECK).
- Module boundaries (rule 9): the new
  module sits cleanly alongside
  `pathtracer/{DirectLight,RNG,Sampling}.{h,cuh}`.
  No cross-module ripple.
- Avoid monolithic files (rule 10): the
  new files are small (~30-100 lines
  each).
- Explicit testable interfaces (rule
  11): the `sample_bsdf` / `bsdf_pdf` /
  `bsdf_eval` helpers + the `BsdfSample`
  POD are host-callable and host-tested.
- Update BUILD_PLAN (rule 8): the slice-
  closing entry.

---

## 6. Runtime-deferred checks

Per the established PT-P.x / NEE.x
audit-host-fingerprint pattern, runtime
checks that require a CUDA / OptiX-SDK
host are recorded for a future operator
session. **MIS.2 has none**.

The full reasoning:

- The MIS.2 helpers are RR_HD inline +
  consumed only by host-level tests in
  this slice. The kernels are unchanged.
- No PPM bytes change; no
  cross-backend convergence question
  arises; no observability question
  arises.
- The host-only tests in §5.5 fully
  exercise the helper's contract on
  the audit host. The Monte Carlo test
  (case 7) consumes ~10^4 samples; runs
  in milliseconds; deterministic given
  a fixed RNG seed (§5.5 case 10's
  determinism anchor). No need to
  defer to a CUDA host.
- The runtime confirmation that MIS at
  v1 is byte-identical with the post-
  NEE-arc baseline is recorded for the
  CUDA + OptiX-SDK host operator
  session — but that runtime check
  belongs to MIS.5 / MIS.6, not MIS.2.

The MIS.7 audit (per
`PATH_TRACER_MIS_PLAN.md` §5.6) walks
the deferred runtime checks for the
entire MIS arc; MIS.2 contributes no
new deferred row beyond the existing
NEE arc's accumulated debt.

---

## 7. Out-of-scope (deferred to future slices)

The following items are explicitly NOT
part of MIS.2:

1. **Non-Lambert BSDFs.** GGX metal,
   dielectric, glass, transmission lobes —
   all reserved for future BSDF arcs that
   extend `Bsdf.{h,cuh}` without
   integrator-level change. The MIS.2
   data model + helper contract is
   forward-compatible.
2. **Specular delta lobes.** The
   `is_delta` flag is shipped, but no
   sampler in this slice produces a
   delta sample (Lambert is purely
   diffuse). Future specular arc sets
   `is_delta = true` + uses a positive
   sentinel for `pdf`.
3. **`MaterialParams` extension.** The
   POD is read-only; no new fields. If
   future BSDFs need additional
   parameters (anisotropy, IOR), those
   land in the future BSDF arc, not
   here.
4. **Integrator changes.** Both
   integrators (`k_pathtrace_sample`,
   `__raygen__pathtrace`) continue to
   use their inline cosine-bounce
   arithmetic. MIS.5 / MIS.6 replace
   them.
5. **NEE branch consumption.** The NEE
   branch in both integrators uses an
   inline `material.baseColor *
   kInvPi` for the Lambert BRDF eval;
   MIS.2 ships `bsdf_eval` as the
   replacement, but no caller invokes
   it yet. MIS.5 / MIS.6 wire it in
   alongside the MIS weight.
6. **Tangent-frame helper module.** The
   MIS.2 helper duplicates the
   `align_to_normal` shape inline rather
   than extracting a shared helper. A
   future cleanup slice could extract
   `pathtracer/TangentFrame.{h,cuh}`;
   out of scope here.
7. **CLI flag.** No `--mis` /
   `--enable-mis` flag. Per
   `PATH_TRACER_MIS_PLAN.md` §6 #8, MIS
   has no operator-facing surface — it
   is structurally on at v2 (area
   lights), trivially a no-op at v1.
8. **AOV exposure.** The BSDF PDF +
   value are not exposed as AOVs.
   Future slice if there's an
   operator-facing need.

---

## 8. Sub-arc context

### 8.1 Position in the MIS arc

Per `PATH_TRACER_MIS_PLAN.md` §5 + §8,
the MIS arc cadence is:

```
MIS.1 (plan, docs only)              ─── shipped (commit 67dd03c)
MIS.2 (BSDF data model)              ─── this brief (THIS slice ships impl)
MIS.3 (Light data model)             ─── independent leaf; future slice
MIS.4 (MIS helper)                   ─── independent leaf; future slice
MIS.5 (CUDA integrator)              ─── depends on {2,3,4}; future slice
MIS.6 (OptiX integrator)             ─── depends on {5}; future slice
MIS.7 (audit)                        ─── depends on {2..6}; future slice
```

MIS.2 / MIS.3 / MIS.4 are independent
leaves. They can be shipped in any order
or interleaved with unrelated arcs. The
recommended cadence (per
`PATH_TRACER_MIS_PLAN.md` §8) starts
with MIS.2 because the BSDF data model
is the most concrete leaf — it informs
MIS.5's integrator shape.

### 8.2 What this slice unblocks

- MIS.5 (CUDA integrator) gains the
  `BsdfSample` + `sample_bsdf` /
  `bsdf_pdf` / `bsdf_eval` it needs to
  thread MIS through `k_pathtrace_sample`.
- MIS.6 (OptiX integrator) gains the
  same.
- MIS.4 (MIS helper) becomes more
  natural to test (the
  `power_heuristic(p_a, p_b)` helper
  can be tested against representative
  Lambert-PDF values produced by
  MIS.2's helpers).
- Future BSDF arcs (GGX, etc.) gain a
  template for new helper additions.

### 8.3 What this slice does NOT unblock

- Area-light NEE (depends on a separate
  arc adding `Light::Area` plumbing).
- Cross-backend MIS convergence
  verification (needs MIS.5 + MIS.6
  + CUDA / OptiX-SDK host).

---

## 9. Verdict

The brief is complete. The implementer
can ship MIS.2 end-to-end without re-
deriving any of the design reasoning.
The plan-level context (problem, scope,
power heuristic, etc.) is in
`docs/PATH_TRACER_MIS_PLAN.md`; the
slice-level contract is in this file.

**Mode reminder: documentation only.**
This file is the spec. The next slice
(MIS.2 impl) ships the source diff +
`tests/pathtracer_bsdf_tests.cpp` + the
BUILD_PLAN entry.

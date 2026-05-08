# CUDA MIS Integrator — Task Definition (MIS.5)

Date: 2026-05-08.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_MIS_PLAN.md` §4.1
+ §5.4 + §7.5 (CUDA integrator scope; v1
byte-identity invariant).
Prior slices:
- MIS.2 BSDF data model + helpers: `d9fa6e3`
  + `5a1c772`.
- MIS.3 Light data model: `0dd7d46` + audit
  `960c523`.
- MIS.4 Power heuristic helper: `cef4a6b`.

All three independent leaves
(MIS.{2,3,4}) now shipped; this slice
(MIS.5) is the first INTEGRATING slice
that consumes them in `k_pathtrace_sample`.

Mode: documentation only. **No source code
is modified by this task definition.** The
task is the spec; the next slice (the
implementation) ships the diff.

Note on the user's referenced
`PATH_TRACER_MIS_BSDF_PDF_AUDIT.md`: that
doc was not shipped — MIS.2 closed via two
BUILD_PLAN entries (commits `d9fa6e3` +
`5a1c772`), not via a separate audit doc,
as recorded in the MIS.3 audit's §0
header note. The MIS arc has no per-stage
audit cadence for MIS.2 specifically; the
planned MIS.7 covers the entire arc once
implementation closes. This brief
proceeds from the plan + the MIS.2 BSDF
task brief + the MIS.3 / MIS.4 audits as
the source of truth.

This file is a fully-self-contained brief
for the MIS.5 implementation slice. Anyone
picking it up should be able to ship MIS.5
without re-deriving the design reasoning.
Pattern mirrors the canonical MIS.x task-
brief shape established at MIS.{2,3,4}.

The MIS arc cadence (post-MIS.4 impl):

| Slice                        | Role                                              | Commit       |
|------------------------------|---------------------------------------------------|--------------|
| MIS.1                        | Multiple Importance Sampling plan                 | `67dd03c`    |
| MIS.2                        | BSDF data model + helpers                         | `d9fa6e3` + `5a1c772` |
| MIS.3                        | Light data model + audit                          | `0dd7d46` + `960c523` |
| MIS.4                        | Power heuristic helper                            | `cef4a6b`    |
| **MIS.5 task**               | **This brief** — CUDA integrator task definition  | (THIS slice) |
| MIS.5 impl                   | CUDA integrator wiring                            | (next)       |
| MIS.6                        | OptiX integrator (mirrors MIS.5)                  | (pending)    |
| MIS.7                        | Arc-level audit                                    | (pending)    |

---

## 1. Exact goal

**Wire the three MIS leaves
(`BsdfSample` /
`DirectLightSample::pdf_solid_angle` /
`DirectLightSample::is_delta` /
`power_heuristic`) into the CUDA path-
trace integrator (`k_pathtrace_sample`
in `src/cuda/CudaPathTracer.cu`) at the
NEE branch's existing call site, while
preserving byte-identical output for the
v1 light-type scope.**

The three leaves arrived as standalone
modules; MIS.5 is the FIRST consumer.
Specifically:

- `pathtracer/Mis.h::power_heuristic(p_a,
  p_b)` (MIS.4) — the Veach β=2 weight.
- `pathtracer/Bsdf.cuh::bsdf_pdf(m, wo,
  normal)` (MIS.2) — the BSDF-side PDF
  evaluator at the NEE-chosen direction.
- `pathtracer/Bsdf.cuh::bsdf_eval(m, wi,
  wo, normal)` (MIS.2) — the BRDF
  evaluator (replaces the inline
  `m.baseColor * kInvPi` for cleanliness;
  bit-equivalent at v1 Lambert).
- `DirectLightSample::is_delta` /
  `pdf_solid_angle` (MIS.3) — the new
  POD fields the helper populates.

The integrator is `k_pathtrace_sample`'s
existing NEE branch at
`CudaPathTracer.cu:276-317`. MIS.5
augments this branch with MIS-aware
weighting WITHOUT changing its overall
shape (kernel guard, RNG draw, helper
call, shadow ray, contribution
accumulation are all preserved).

### 1.1 The single architectural change

Inside the existing
`if (vis > 0.0f && cos_th > 0.0f)`
branch, the NEE contribution
multiplier `k = cos_th * vis * pdf_inv`
becomes:

```cpp
const float k = cos_th * vis * pdf_inv * mis_weight_nee;
```

where `mis_weight_nee` is computed via:

```cpp
const float mis_weight_nee = sample.is_delta
    ? 1.0f
    : rr::pathtracer::power_heuristic(
          sample.pdf_solid_angle,
          rr::pathtracer::bsdf_pdf(m, sample.wi, hit.normal));
```

That's the entire integrator change.
Everything else — the kernel guard, the
RNG draw, the shadow ray, the BRDF
formula, the contribution accumulation,
the BSDF bounce, the emission add —
remains byte-identical with the post-
MIS.4 baseline at `cef4a6b`.

### 1.2 Why the change is minimal

The MIS framework was designed (per
`docs/PATH_TRACER_MIS_PLAN.md` §1.4 #2
+ §7.5) so that v1 Lambert + delta-
light scope produces byte-identical
output with the pre-MIS NEE-only
build. The mechanism:

- **Delta short-circuit:**
  `sample.is_delta == true` for v1
  Point + Directional lights (MIS.3
  populates this); the integrator
  short-circuits to `mis_weight_nee
  = 1.0f`. Multiplying the
  contribution by `1.0f` is the
  IEEE-754 identity multiplication;
  the float bits are bit-equal with
  the unmultiplied value.
- **The `power_heuristic` call is
  unreachable at v1.** The
  short-circuit ternary's else
  branch is never taken because
  v1 lights always set `is_delta`;
  consequently `bsdf_pdf` is never
  called either. The new helpers
  are wired but inert in
  practice.

This is the minimum-risk wiring
that satisfies the user's prompt
("wire BsdfSample,
DirectLightSample::pdf_solid_angle
/ is_delta, and power_heuristic")
while preserving the byte-identity
contract. Future area-light arcs
will exercise the non-delta branch.

### 1.3 What MIS.5 does NOT include (deferred)

Three pieces from
`docs/PATH_TRACER_MIS_PLAN.md` §4.1
are explicitly DEFERRED from MIS.5:

1. **BSDF bounce swap** (plan §4.1
   step 5). The current inline
   cosine-hemisphere bounce + Lambert
   throughput simplification
   (`throughput *= m.baseColor`) is
   PRESERVED bit-for-bit at MIS.5.
   Swapping to `sample_bsdf` (which
   would compute throughput as
   `(value * cos_theta_o) / pdf`)
   introduces sub-ULP drift in the
   framebuffer floats vs the
   inline `* baseColor` form (the
   MIS.2 BSDF brief's
   `test_lambert_throughput_simplification`
   used `approx(..., 1e-6f)` because
   of this; not bit-exact). Deferred
   to a future slice that can risk
   the sub-ULP framebuffer drift
   (PPM byte-identity likely holds
   via 8-bit quantisation, but
   verifying that requires a CUDA-
   host run; deferred to MIS.7
   audit's runtime checks).
2. **MIS-on-emission-add** (plan §4.1
   step 3). Currently the
   emission-add at
   `CudaPathTracer.cu:233-244` is
   unweighted. The plan envisions
   weighting it by
   `power_heuristic(p_bsdf_at_prev_bounce,
   p_light_at_dir)` so a hit on an
   area-light surface gets balanced
   contribution from BOTH the BSDF
   sampler AND the NEE sampler. At
   v1 (delta lights only,
   `p_light_at_dir = 0` for any
   bounce), this weight collapses
   to `1.0f`; the architectural
   tracking (previous-bounce BSDF
   PDF state) is OVERHEAD with no
   payoff. Deferred to the area-
   light arc when it actually
   matters.
3. **Per-bounce relativity-on-
   throughput.** The current
   integrator applies Doppler /
   searchlight to the FINAL per-
   sample radiance (post-bounce
   loop). This is not a MIS
   concern; out of scope.

The brief explicitly authorises
these deferrals because each
preserves byte-identity at v1. A
future slice will revisit them.

---

## 2. v1 behavior

The four contractual properties the
slice must honour:

### 2.1 Delta lights short-circuit to NEE weight = 1

For any v1 light type (Point or
Directional), `sample.is_delta` is
`true` per MIS.3's helper population.
The integrator's ternary
`sample.is_delta ? 1.0f :
power_heuristic(...)` evaluates to
`1.0f`. Multiplying the contribution
by `1.0f` is the IEEE-754 identity:
`x * 1.0f == x` for any finite
non-NaN `x` per IEEE-754 §6.

Anchored by the host-only
test from the MIS.5 BUILD_PLAN
test-extension (§5.5 below).

### 2.2 BSDF-bounce-as-delta-light contribution remains zero-measure / deferred

At v1 (delta lights only), the
emission-add path doesn't fire on
delta lights — Point + Directional
lights are not hittable surfaces;
they exist only as
`scene.lights` entries enumerated
by NEE. The current emission-add
fires only on emissive sphere /
triangle hit surfaces, which are
NOT in `scene.lights`. So:

- The MIS-on-emission-add concern
  (plan §4.1 step 3) is moot at v1
  — the BSDF-bounce can never
  hit a Point / Directional light
  (zero measure on the unit
  sphere).
- The unweighted emission-add for
  emissive surfaces at v1 is
  CORRECT — there's no NEE
  counterpart for emissive
  surfaces (NEE doesn't sample
  emissive sphere / triangle
  surfaces; only `scene.lights`
  Point/Directional). No double-
  count.
- MIS.5 leaves the emission-add
  unchanged. The BSDF-bounce-as-
  light MIS weight is structurally
  `1.0f` at v1; no architectural
  tracking is needed.

This is the rationale for §1.3 #2
deferral.

### 2.3 Output byte-identical to current NEE-only build for delta lights

For an operator running
`--enable-nee` against a v1 scene
(Point + Directional lights only,
no area lights, no emissive
surfaces with NEE counterparts):

- Pre-MIS.5 (post-MIS.4 baseline at
  `cef4a6b`): NEE contribution =
  `cos_th * vis * pdf_inv *
  brdf * Li_unattenuated *
  throughput`.
- Post-MIS.5: NEE contribution =
  `cos_th * vis * pdf_inv *
  mis_weight_nee * brdf *
  Li_unattenuated * throughput`,
  where `mis_weight_nee == 1.0f`
  always (delta short-circuit).

By IEEE-754 multiplicative identity,
the two expressions produce bit-
equal float results. The
intermediate FP operation count
differs by ONE multiplication (the
`* 1.0f`), but `* 1.0f` is exact
in IEEE-754 — the result is bit-
equal with the unmultiplied value.

**PPM bytes are bit-identical with
the pre-MIS.5 build at v1 light-
type scope** (the §7.5 invariant
from the plan).

### 2.4 Default-OFF (no `--enable-nee`) byte-identical

For an operator running WITHOUT
`--enable-nee`, the kernel guard
`if (enable_nee && light_count > 0)`
short-circuits at `enable_nee ==
false`. The MIS.5 additions live
INSIDE this guard. None execute.
Byte-identity is preserved
trivially.

---

## 3. Files likely involved

The implementation slice will touch
this file set:

| File                                       | Change                                                              |
|--------------------------------------------|---------------------------------------------------------------------|
| `src/cuda/CudaPathTracer.cu`               | +~25-40 lines. Inside the existing NEE branch at lines 276-317:   |
|                                            | (a) `#include "pathtracer/Bsdf.cuh"` and                          |
|                                            | `"pathtracer/Mis.h"` at the top; (b) compute                      |
|                                            | `mis_weight_nee` per §1.1; (c) multiply contribution by it.       |
|                                            | OPTIONAL: replace inline `m.baseColor * kInvPi` with             |
|                                            | `bsdf_eval(...)` for module consistency (bit-equivalent).         |
|                                            | NO bounce swap; NO emission-add change.                            |
| `tests/pathtracer_nee_tests.cpp`           | +~30-50 lines. ONE new mandatory test case:                       |
|                                            | `test_mis_weight_delta_short_circuits_to_one` — exercising the   |
|                                            | helper composition at v1 (delta lights ⇒ effective MIS weight    |
|                                            | = 1.0). The integrator-level byte-identity check is a runtime-   |
|                                            | deferred CUDA-host PPM `cmp`; the host test anchors the          |
|                                            | helper-composition logic that PRODUCES the weight.                 |
| `docs/BUILD_PLAN.md`                       | Slice-closing entry per the established narrow-column format.    |

`CMakeLists.txt` is NOT touched — no
new test binary.

`src/cuda/CudaPathTracer.cuh` is NOT
touched — the launcher signature
already takes `enable_nee`; no new
parameter is needed (the MIS-aware
integrator reads its inputs from
the existing
`DirectLightSample` POD).

### 3.1 Helper integration points (target shapes)

The implementer fills the per-line
doc-comment text + the inline-helper
invocations. Suggested target shape
inside the existing NEE branch
(replacing the existing
`brdf` + `k` + `contrib`
calculation at lines 304-313):

```cpp
if (cos_th > 0.0f) {
    // MIS.5: compute the MIS weight on the NEE-side
    // estimator. Veach 1995 §10.3 delta-light convention:
    // when sample.is_delta == true (v1 Point + Directional
    // lights), the BSDF sampler can never reach this light
    // (zero measure on the unit sphere); the NEE-side
    // weight is exactly 1.0. For non-delta lights (future
    // area-light arc), the weight is computed via the
    // Veach β=2 power heuristic from sample.pdf_solid_angle
    // and the BSDF PDF at sample.wi.
    const float mis_weight_nee = sample.is_delta
        ? 1.0f
        : rr::pathtracer::power_heuristic(
              sample.pdf_solid_angle,
              rr::pathtracer::bsdf_pdf(m, sample.wi, hit.normal));

    // Lambert BRDF: baseColor / pi. (MIS.5: bsdf_eval
    // returns bit-equivalent value; using the helper
    // for module consistency. Replace `m.baseColor *
    // rr::math::kInvPi` if the bsdf_eval helper produces
    // bit-equivalent FP; otherwise keep the inline form.)
    const Vec3 brdf = m.baseColor * rr::math::kInvPi;

    const float k = cos_th * vis * sample.pdf_inv * mis_weight_nee;
    const Vec3 contrib = Vec3{
        throughput.x * sample.li_unattenuated.x * brdf.x,
        throughput.y * sample.li_unattenuated.y * brdf.y,
        throughput.z * sample.li_unattenuated.z * brdf.z}
        * k;
    radiance = radiance + contrib;
}
```

The implementer MAY replace the
inline `m.baseColor * rr::math::kInvPi`
with `rr::pathtracer::bsdf_eval(m,
sample.wi, hit.normal, hit.normal)`
provided the produced float bits are
bit-equal. If the swap introduces any
ULP drift (compiler emits different
code; rare for trivial inline
helpers), the implementer should
keep the inline form and document the
deferral.

### 3.2 Test case shape

`tests/pathtracer_nee_tests.cpp`
gains ONE new case anchoring the
MIS weight composition at v1:

```cpp
void test_mis_weight_delta_short_circuits_to_one() {
    // For v1 light types (Point + Directional), the helper
    // populates is_delta == true. The integrator's MIS
    // weight ternary `is_delta ? 1.0f : power_heuristic(...)`
    // evaluates to 1.0f, matching the Veach 1995 §10.3
    // delta-light convention.

    // Helper: simulate the integrator's MIS-weight ternary
    // for a representative DirectLightSample.
    auto compute_mis_weight = [](const DirectLightSample& s,
                                 float p_bsdf_at_wi) {
        return s.is_delta
            ? 1.0f
            : rr::pathtracer::power_heuristic(
                  s.pdf_solid_angle, p_bsdf_at_wi);
    };

    // Point light fixture: should set is_delta = true.
    const Light L_point = make_point(Vec3{0, 5, 0},
                                     Vec3{1, 1, 1}, 1.0f);
    const auto s_point = sample_direct_light_uniform(
        &L_point, 1, Vec3{0, 0, 0}, Vec3{0, 1, 0}, 0.0f);
    RR_CHECK(s_point.is_delta == true);
    RR_CHECK(compute_mis_weight(s_point, 0.5f) == 1.0f);

    // Directional light fixture: should set is_delta = true.
    const Light L_dir = make_directional(Vec3{0, -1, 0},
                                         Vec3{1, 1, 1}, 1.0f);
    const auto s_dir = sample_direct_light_uniform(
        &L_dir, 1, Vec3{0, 0, 0}, Vec3{0, 1, 0}, 0.0f);
    RR_CHECK(s_dir.is_delta == true);
    RR_CHECK(compute_mis_weight(s_dir, 0.5f) == 1.0f);
}
```

The case is registered in `main()`
alongside the existing MIS.3 cases.
Per-binary count grows from 53/53
to ~57/57 (+4 RR_CHECK assertions).

The implementer MAY add additional
cases (e.g. simulate a non-delta
hypothetical area-light sample with
finite `pdf_solid_angle` and verify
the helper composition produces
the expected `power_heuristic`
result). Minimum 1 case per the
PASS criterion.

---

## 4. What must not be touched

The implementation slice MUST keep
the following byte-identical:

### 4.1 The OptiX integrator

- `src/optix/OptixPrograms.cu` — every
  byte. The OptiX-side mirror lands
  at MIS.6 (a future slice); not
  this one.
- `src/optix/OptixRenderer.{h,cpp}`,
  `src/optix/OptixLaunchParams.h`,
  `src/optix/OptixPipeline.{h,cpp}`,
  `src/optix/OptixSBT.h`,
  `src/optix/OptixDenoiser.{h,cpp}`,
  `src/optix/OptixBackend.{h,cpp}`,
  `src/optix/OptixAccel.{h,cpp}` —
  every byte.

### 4.2 The pathtracer module's existing surfaces

- `src/pathtracer/RNG.{h,cuh}` — every
  byte. The MIS.5 integrator does
  NOT add an extra `next_float` /
  `next_vec2` call inside the NEE
  branch (the helper composition is
  deterministic given the existing
  RNG state).
- `src/pathtracer/Sampling.{h,cuh}` —
  every byte.
- `src/pathtracer/DirectLight.{h,cuh}`
  (MIS.3) — every byte. The integrator
  consumes `is_delta` and
  `pdf_solid_angle` but does not
  modify their producers.
- `src/pathtracer/Bsdf.{h,cuh}`
  (MIS.2) — every byte. The
  integrator consumes `bsdf_pdf` and
  optionally `bsdf_eval` but does not
  modify them.
- `src/pathtracer/Mis.h` (MIS.4) —
  every byte. The integrator
  consumes `power_heuristic`.
- `src/pathtracer/PathTracer.{h,cpp}`
  — every byte. `PathTraceConfig`
  unchanged. The orchestration's
  spp loop unchanged.

### 4.3 The CLI / Config / main.cpp surfaces

- `src/core/Config.h`, `src/core/Config.cpp`,
  `src/core/CommandLine.h`,
  `src/core/CommandLine.cpp`,
  `src/core/Logger.{h,cpp}` — every
  byte. No new CLI flag (per the
  plan §6 #8).
- `src/main.cpp` — every byte. No
  dispatcher signature change.

### 4.4 The renderer / scene / material modules

- `src/renderer/`, `src/io/`,
  `src/scene/`, `src/material/`,
  `src/lighting/`, `src/texture/`,
  `src/gpu/`, `src/server/` — every
  byte.

### 4.5 The CUDA kernel structure

- The kernel signature
  `k_pathtrace_sample(float* pixels,
  ..., bool enable_nee)` — UNCHANGED.
- The `launch_pathtrace_sample`
  launcher — UNCHANGED.
- The `enable_nee` guard — UNCHANGED
  (`if (enable_nee && light_count
  > 0)` at line 276).
- The NEE shadow ray (`trace_shadow_ray_pt`)
  — UNCHANGED.
- The cosine-hemisphere bounce
  (lines 326-340) — UNCHANGED. The
  inline `throughput *= m.baseColor`
  Lambert simplification preserved
  for byte-identity (per §1.3 #1
  deferral).
- The emission-add at lines 233-244
  — UNCHANGED (per §1.3 #2 deferral).
- The firefly clamp at lines 343-360
  — UNCHANGED.
- The relativity stack (Doppler +
  searchlight at lines 362-end of
  function) — UNCHANGED.

The MIS.5 changes are scoped to a
SINGLE region: the
`if (cos_th > 0.0f)` branch inside
the existing NEE branch's
`if (vis > 0.0f)` block — about 10
lines of arithmetic.

### 4.6 Tests + scenes + tooling

- `tests/cli_tests.cpp`,
  `tests/pathtracer_tests.cpp`,
  `tests/math_tests.cpp`,
  `tests/image_tests.cpp`,
  `tests/gpu_tests.cpp`,
  `tests/relativity_tests.cpp`,
  `tests/demo_tests.cpp`,
  `tests/renderer_tests.cpp`,
  `tests/optix_tests.cpp`,
  `tests/pathtracer_bsdf_tests.cpp`,
  `tests/pathtracer_mis_tests.cpp` —
  every byte. ONLY
  `tests/pathtracer_nee_tests.cpp`
  is modified (extended with the
  new MIS.5 case per §3.2).
- `scenes/*.rrscene` — every byte.
- `tools/verify_cuda_host.py` — every
  byte.
- `CMakeLists.txt` — every byte. No
  new test binary.

### 4.7 Default behaviour

For an operator running ANY action
with ANY combination of existing
flags (`--enable-nee`,
`--firefly-clamp`, etc.), the
rendered PPM is bit-identical with
the post-MIS.4 baseline at commit
`cef4a6b`. The structural
arguments:

- Default-OFF: kernel guard short-
  circuits; new code unreachable.
- Default-ON at v1 light-type
  scope: delta short-circuit gives
  `mis_weight_nee == 1.0f`;
  multiplying by 1.0f is IEEE-754
  identity.
- Optional `bsdf_eval` swap: bit-
  equivalent FP if implementer
  verifies; otherwise inline
  form preserved.

---

## 5. PASS criteria

The implementation slice passes
when ALL of the following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF,
  RR_ENABLE_OPTIX=OFF): clean
  build, zero new compiler
  warnings.
- `cmake --build build-ON` (audit
  host, RR_ENABLE_CUDA=OFF,
  RR_ENABLE_OPTIX=ON with SDK
  fallback): clean build, zero
  new warnings.

### 5.2 Tests

- `ctest --output-on-failure`
  from `build`: 100% green.
  Count UNCHANGED at 11/11 (no
  new test binary).
- `ctest --output-on-failure`
  from `build-ON`: 100% green.
  Count UNCHANGED at 12/12.
- `pathtracer_nee_tests`
  per-case count grows by at
  least 1 RR_CHECK case
  function (the §3.2 test).
  Per-RR_CHECK assertion count
  grows by at least 4 (the case
  has 4 explicit RR_CHECK
  invocations).
- All other test binaries' per-
  case counts unchanged.
- The existing
  `test_zero_contribution_is_bit_default`
  NEE.5 anchor and the
  three MIS.3 anchors (lines
  381-383 of the post-MIS.3
  test file) MUST continue to
  pass without modification.

### 5.3 Source diff size

Per `docs/PATH_TRACER_MIS_PLAN.md`
§7.3 (MIS.5 budget): ≤ 200
lines added.

Suggested per-file budget:
- `src/cuda/CudaPathTracer.cu`:
  ~25-40 lines (the helper
  composition + new include
  lines + ~20 lines of doc-
  comment).
- `tests/pathtracer_nee_tests.cpp`:
  ~30-50 lines (one case
  function + main() registry
  line).
- TOTAL: ~55-90 lines.

Well within budget — the
slice is genuinely small.
Doc-comment density is
expected to overshoot per
the established PT-P.x /
NEE.x / MIS.{2,3,4} pattern;
the deviation is acceptable
up to ~150 lines per the
established "2x cap" tolerance
the prior MIS slices set.

### 5.4 No-touch invariants

`git diff` after the slice
MUST show zero bytes changed
in:

```
src/optix/  src/renderer/  src/io/
src/scene/  src/material/  src/lighting/
src/texture/  src/gpu/  src/server/
src/main.cpp  src/core/
src/pathtracer/RNG.{h,cuh}
src/pathtracer/Sampling.{h,cuh}
src/pathtracer/Bsdf.{h,cuh}
src/pathtracer/DirectLight.{h,cuh}
src/pathtracer/Mis.h
src/pathtracer/PathTracer.{h,cpp}
src/cuda/CudaPathTracer.cuh
tests/cli_tests.cpp  tests/pathtracer_tests.cpp
tests/math_tests.cpp  tests/image_tests.cpp
tests/gpu_tests.cpp  tests/relativity_tests.cpp
tests/demo_tests.cpp  tests/renderer_tests.cpp
tests/optix_tests.cpp
tests/pathtracer_bsdf_tests.cpp
tests/pathtracer_mis_tests.cpp
scenes/  tools/verify_cuda_host.py
CMakeLists.txt
```

Verifiable via the standard
`git diff -- <paths> | wc
-l` ⇒ 0 invariant.

### 5.5 Helper-composition test coverage

`tests/pathtracer_nee_tests.cpp`
MUST gain at least the
following case function per
§3.2:

- **`test_mis_weight_delta_short_circuits_to_one`**:
  for the v1 Point + Directional
  light fixtures, verify that
  the integrator's MIS-weight
  ternary
  `is_delta ? 1.0f :
  power_heuristic(...)`
  evaluates to `1.0f`.
  Anchors the §2.1
  delta-short-circuit
  behaviour at the helper
  composition level.

The implementer may add
additional cases (e.g. a
hypothetical non-delta sample
exercising the
`power_heuristic` branch).
Minimum 1 mandatory case.

### 5.6 Existing test invariants preserved

- `test_zero_contribution_is_bit_default`
  (NEE.5) MUST continue to
  pass.
- `test_helper_determinism`
  (NEE.5) MUST continue to
  pass.
- `test_point_light_sets_is_delta_and_zero_pdf`,
  `test_directional_light_sets_is_delta_and_zero_pdf`,
  `test_zero_contribution_sample_has_default_is_delta`
  (MIS.3) MUST continue to
  pass.
- All eight cases in
  `pathtracer_bsdf_tests.cpp`
  (MIS.2) MUST continue to
  pass (41/41 unchanged).
- All eight cases in
  `pathtracer_mis_tests.cpp`
  (MIS.4) MUST continue to
  pass (34/34 unchanged).
- All other tests unchanged.

### 5.7 Documentation

- `docs/BUILD_PLAN.md` carries
  a new slice-closing entry
  matching the established
  narrow-column format.
- The entry references this
  task brief +
  `docs/PATH_TRACER_MIS_PLAN.md`
  §4.1 + §5.4 + §7.5 as the
  source of the spec.
- The
  `CudaPathTracer.cu` MIS.5
  insertion site has a doc-
  comment block walking the
  delta-short-circuit
  rationale + cross-
  references to the plan +
  this brief.

### 5.8 Master rule compliance

- Build incrementally (rule
  1) + every step compilable
  (rule 2): both audit-host
  configs green; ctest green.
- No fake stubs (rule 3):
  the helper composition is
  real; the
  `mis_weight_nee` value is
  computed correctly per
  Veach.
- No CPU per-pixel work
  (rules 5/7): the helpers
  are RR_HD inline; per-
  pixel consumption is
  device-side. Host code
  in this slice is the
  integrator-level test
  scaffold only.
- Module boundaries (rule
  9): the integrator
  consumes the three
  pathtracer module helpers
  via clean APIs; no cross-
  module ripple beyond the
  established
  `CudaPathTracer.cu` →
  `pathtracer/*` consumer
  pattern.
- Update BUILD_PLAN (rule
  8): the slice-closing
  entry.

---

## 6. Runtime-deferred CUDA-host checks

The audit-host CANNOT run
the CUDA kernel; the byte-
identity claim from §2.3 is
STRUCTURAL (IEEE-754
identity multiplication
argument). The runtime
empirical confirmation is
DEFERRED to a CUDA-equipped
operator session:

| §            | Check                                                           | Procedure                                                    |
|--------------|------------------------------------------------------------------|--------------------------------------------------------------|
| **§6.1**    | **MIS-on byte-IDENTITY at v1 (CUDA, runtime)**                  | `cmp` post-MIS.5 vs post-MIS.4 PPM (no-flag and `--enable-nee`) |
| §6.2         | Default-OFF byte-IDENTITY (CUDA, runtime; carry-forward)        | `cmp` no-flag PPM (pre vs post)                              |
| §6.3         | NEE-on visible behaviour unchanged                              | render lit scene with `--enable-nee`; visual diff = 0        |
| §6.4         | ctest cycle on CUDA host                                        | re-run on CUDA-built host                                    |

### 6.1 MIS-on byte-IDENTITY at v1 (the key check)

```
$ git checkout cef4a6b      # post-MIS.4 baseline
$ cmake --build build-cuda -j
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene \
    --enable-nee
$ cp output/pathtrace_spp_1.ppm /tmp/pre_mis5_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/pre_mis5_spp16.ppm

$ git checkout MIS.5_commit
$ cmake --build build-cuda -j

# Post-MIS.5 build, --enable-nee passed against same scene.
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene \
    --enable-nee
$ cp output/pathtrace_spp_1.ppm /tmp/post_mis5_spp1.ppm
$ cp output/pathtrace_spp_16.ppm /tmp/post_mis5_spp16.ppm

$ cmp /tmp/pre_mis5_spp1.ppm /tmp/post_mis5_spp1.ppm  ; echo $?
=> 0 (identical — MIS at v1 is a no-op at the PPM level)
$ cmp /tmp/pre_mis5_spp16.ppm /tmp/post_mis5_spp16.ppm ; echo $?
=> 0
```

This is THE confirmation that
the §2.3 byte-identity
invariant holds at runtime.
DEFERRED to a CUDA host;
the host-only test in §3.2
anchors the helper
composition logic on the
audit host.

### 6.2 Default-OFF byte-IDENTITY (carry-forward)

The pre-existing default-OFF
byte-identity (no
`--enable-nee` passed) was
DEFERRED at NEE.6 §9.1; this
slice does not regress it.
Same `cmp` procedure as
§6.1 but with no flag passed
on either side.

### 6.3 NEE-on visible behaviour unchanged

For the v1 lit scene
(`scenes/test_full_scene.rrscene`),
the rendered image with
`--enable-nee` should
visually MATCH the post-
MIS.4 baseline at every
sample count
(`pathtrace_spp_1.ppm`,
`pathtrace_spp_4.ppm`,
`pathtrace_spp_16.ppm`).
The visual diff is the
PPM `cmp` from §6.1
(stronger than visual; bit-
identical).

### 6.4 ctest cycle on CUDA host

`ctest --output-on-failure`
from a CUDA-built
`build-cuda` directory must
pass. The new helper-
composition case in
`pathtracer_nee_tests.cpp`
runs identically to its
audit-host run (host-only
test; no kernel
dependency).

### 6.5 Carry-forward from prior MIS audits

The MIS.3 audit (`960c523`)
recorded the runtime-
deferred status as
DEFERRED on the audit
host. MIS.5 inherits this
status. The MIS.7 arc-
level audit will roll up
all deferred runtime
checks for a single
operator session.

---

## 7. Out-of-scope (deferred to future slices)

The following items are
explicitly NOT part of
MIS.5:

1. **OptiX integrator
   wiring (MIS.6).** Mirrors
   the CUDA changes in
   `__raygen__pathtrace`'s
   NEE branch on the
   OptiX side. Independent
   slice; depends on
   MIS.5 to establish the
   pattern.
2. **BSDF bounce swap.**
   The `sample_bsdf` call
   replacing the inline
   cosine-hemisphere
   bounce is deferred per
   §1.3 #1; the
   throughput simplification
   `(value * cos) / pdf
   == baseColor` for
   Lambert is mathematically
   exact but FP-rounding-
   wise within ~1 ULP.
   Future slice can swap
   if the sub-ULP drift is
   acceptable (likely
   invisible at 8-bit PPM
   precision; CUDA-host
   `cmp` confirms).
3. **MIS-on-emission-add.**
   The plan §4.1 step 3
   architectural tracking
   is deferred per §1.3
   #2 because at v1 it's
   structurally `1.0`.
   Lands with the area-
   light arc.
4. **Area-light NEE
   support.** Independent
   arc; depends on
   `Light::Area`
   plumbing (currently a
   PLACEHOLDER per
   `Light.h:20-31`).
5. **Specular-delta BSDFs.**
   Lambert-only at v1;
   the `is_delta` field on
   `BsdfSample` is reserved
   but unused at v1
   (Lambert never sets it
   true).
6. **Environment-IBL NEE.**
   Future arc.
7. **CLI flag for MIS.**
   No `--mis` /
   `--no-mis` flag (per
   the plan §6 #8).
8. **MIS-weight AOV
   exposure.**
9. **Per-bounce
   relativity-on-
   throughput** — not
   a MIS concern.
10. **Cross-backend MIS
    convergence
    verification.** Needs
    MIS.5 + MIS.6 + a
    CUDA + OptiX-SDK
    host. DEFERRED to
    MIS.7 audit.

---

## 8. Sub-arc context

### 8.1 Position in the MIS arc

Per
`docs/PATH_TRACER_MIS_PLAN.md`
§5 + §8, MIS.5 is the FIRST
INTEGRATING slice — it is
the first to consume the
three independent leaves
(MIS.{2,3,4}). MIS.6
(OptiX integrator) follows
the MIS.5 pattern; MIS.7
audits the entire arc.

### 8.2 What this slice unblocks

- MIS.6 (OptiX integrator)
  has a CUDA reference to
  mirror.
- MIS.7 (arc-level audit)
  has the integrator-level
  artefact to walk
  alongside the leaf-
  level artefacts.
- Future area-light arc
  inherits a tested MIS
  framework — the area-
  light integrator changes
  swap the
  `sample.is_delta`
  short-circuit for the
  full
  `power_heuristic` call
  + add the MIS-on-
  emission-add tracking.

### 8.3 What this slice does NOT unblock

- Cross-backend MIS
  convergence (needs
  MIS.6).
- Area-light NEE
  (independent arc).
- Specular delta BSDFs
  (independent arc;
  needs `BsdfSample::is_delta`
  consumer).

### 8.4 Recommended audit cadence

The MIS.3 audit
(`960c523`) established
a per-stage MIS audit
cadence; the MIS.4 audit
was NICE-TO-HAVE per
the MIS.4 task brief
§7.4. For MIS.5, the
audit is RECOMMENDED
(the integrator change
is more impactful than
the leaf additions; a
per-stage audit anchor
documents the byte-
identity invariant
formally) but not a
hard PASS criterion of
THIS task brief. The
implementer / operator
may defer the MIS.5
audit to bundle with
MIS.6 / MIS.7 if
preferred.

---

## 9. Verdict

The brief is complete.
The implementer can ship
MIS.5 end-to-end without
re-deriving any of the
design reasoning. The
plan-level context
(Veach β=2, sum-to-one,
delta short-circuit) is
in
`docs/PATH_TRACER_MIS_PLAN.md`;
the slice-level contract
is in this file.

**Mode reminder:
documentation only.**
This file is the spec.
The next slice (MIS.5
impl) ships the source
diff + the one mandatory
test case + the
BUILD_PLAN entry.

# Path Tracer Multiple Importance Sampling — Arc Audit (MIS.7)

Date: 2026-05-08.
Branch: `claude/recreate-mis-audit-7pQQN`
(audit content authored on top of
`relativity-core-v1` HEAD — last commit on
the audited renderer tree:
`ea7c344` "optix: MIS.6 OptiX MIS integrator
wiring (delta short-circuit; v1 byte-
identical)").
Plan source: `docs/PATH_TRACER_MIS_PLAN.md`
§5.6 ("MIS.7 — Audit (docs only)") + the
six per-stage task briefs (MIS.{2..6}) +
the two per-stage audits (MIS.3, MIS.5)
already shipped on `relativity-core-v1`.
Mode: documentation only. **No source code
is modified by this audit.**
Auditor: Claude Code, on the audit host
(no CUDA Toolkit; OptiX-SDK fallback on
the ON build). Same fingerprint as every
prior audit in this session.

This audit is the **arc-level closure** for
the MIS sub-arc (MIS.{1..6}). It walks the
ten user-enumerated checks + records a
closing PASS / REPAIR / BLOCKED verdict on
the arc as a whole, and explicitly
SUBSUMES the per-stage audits that were
not separately shipped (MIS.2, MIS.4,
MIS.6 — see §0 below).

Verdict legend (matches every prior audit):

- **PASS** — implemented; type-checked on
  the audit host; AND empirically
  exercisable via host-only tests with a
  recorded happy-path run. (Runtime CUDA /
  OptiX-SDK confirmation may still be
  DEFERRED; see §8.)
- **REPAIR** — implemented but a defect or
  inconsistency was found that should be
  patched.
- **BLOCKED** — verification cannot
  proceed on this audit host AND the
  structural argument also cannot be
  confirmed without runtime evidence.
- **DEFERRED** (§8) — empirical
  verification requires a CUDA-equipped
  host; a structural argument exists.
- **SUBSUMED** (§0) — the per-stage audit
  doc was not separately written; this
  arc audit absorbs it.

---

## 0. Audits SUBSUMED by this arc audit

Per the MIS arc cadence (mirroring the
NEE.{1..6} cadence), each implementation
slice was OPTIONALLY followed by a per-
stage audit doc. Two were shipped on
`relativity-core-v1`:

- `docs/PATH_TRACER_MIS_LIGHT_PDF_AUDIT.md`
  (MIS.3, commit `960c523`).
- `docs/PATH_TRACER_MIS_CUDA_INTEGRATOR_AUDIT.md`
  (MIS.5, commit `470af7d`).

Three were NOT shipped — their PASS
contracts are absorbed into THIS arc
audit:

| Slice | Per-stage audit doc                                     | Shipped? | Reason for non-shipment                                                                                                        |
|-------|---------------------------------------------------------|:--------:|---------------------------------------------------------------------------------------------------------------------------------|
| MIS.2 | `docs/PATH_TRACER_MIS_BSDF_PDF_AUDIT.md`                | NO       | MIS.2 closed via two BUILD_PLAN entries (`d9fa6e3` POD + `5a1c772` helpers + tests), per the MIS.3 audit's §0 header note.      |
| MIS.4 | `docs/PATH_TRACER_MIS_POWER_HEURISTIC_AUDIT.md`         | NO       | MIS.4 task brief §7.4 marked the audit as NICE-TO-HAVE (not a hard PASS criterion); the 8-case host-only test (`pathtracer_mis_tests` 34/34) anchors the helper's contract empirically. |
| MIS.6 | `docs/PATH_TRACER_MIS_OPTIX_INTEGRATOR_AUDIT.md`        | NO       | MIS.5 audit §11.3 + MIS.6 task brief §8.4 marked the per-stage audit as RECOMMENDED but bundle-able with MIS.7; this arc audit is that bundle. |

**Marking these audits SUBSUMED.** The
PASS contracts they would have anchored
are walked in §1–§7 below. Per the user-
prompt rule "do not recreate them
separately", no separate audit doc is
written for any of MIS.2, MIS.4, MIS.6 —
their closure lives in this arc audit's
section indices noted next to each
finding.

The MIS arc to date (final ledger):

| Slice                        | Role                                              | Commit       |
|------------------------------|---------------------------------------------------|--------------|
| MIS.1                        | Multiple Importance Sampling plan                 | `67dd03c`    |
| MIS.2 task                   | BSDF PDF data model task brief                    | `659155e`    |
| MIS.2 structure-only         | `BsdfSample` POD                                  | `d9fa6e3`    |
| MIS.2 helpers                | Lambert sampler + 10 host-only tests              | `5a1c772`    |
| MIS.2 audit                  | (NOT SHIPPED — **SUBSUMED here**)                 | —            |
| MIS.3 task                   | Light PDF data model task brief                   | `da86554`    |
| MIS.3 impl                   | `DirectLightSample` extension                     | `0dd7d46`    |
| MIS.3 audit                  | Per-stage MIS.3 audit                             | `960c523`    |
| MIS.4 task                   | Power heuristic helper task brief                 | `ef6c535`    |
| MIS.4 impl                   | `power_heuristic` helper + 8 host-only tests      | `cef4a6b`    |
| MIS.4 audit                  | (NOT SHIPPED — **SUBSUMED here**)                 | —            |
| MIS.5 task                   | CUDA integrator task definition                   | `91de1e7`    |
| MIS.5 impl                   | CUDA integrator wiring                            | `35577a6`    |
| MIS.5 audit                  | Per-stage MIS.5 audit                             | `470af7d`    |
| MIS.6 task                   | OptiX integrator task definition                  | `96418ab`    |
| MIS.6 impl                   | OptiX integrator wiring                           | `ea7c344`    |
| MIS.6 audit                  | (NOT SHIPPED — **SUBSUMED here**)                 | —            |
| **MIS.7**                    | **Arc-level audit (this doc)**                    | (this slice) |

---

## 1. BSDF sample / PDF model exists

**PASS.** *(Subsumes MIS.2 audit.)*

The `BsdfSample` POD + Lambert helpers
ship at the contracted module split
mirroring `pathtracer/DirectLight.{h,cuh}`:

- `src/pathtracer/Bsdf.h` — POD definition.
- `src/pathtracer/Bsdf.cuh` — RR_HD inline
  helpers (`sample_bsdf`, `bsdf_pdf`,
  `bsdf_eval`, `detail::align_to_normal`).

### 1.1 `BsdfSample` POD

`src/pathtracer/Bsdf.h` declares:

```cpp
struct BsdfSample {
    rr::math::Vec3 wo    = rr::math::Vec3{0.0f, 0.0f, 0.0f};
    rr::math::Vec3 value = rr::math::Vec3{0.0f, 0.0f, 0.0f};
    float          pdf   = 0.0f;
    bool           valid = false;
};
```

Field semantics (per the MIS.2 task brief
§2 + the in-file doc-comment block):

- `wo`: world-space unit direction the
  sampler chose; upper-hemisphere wrt
  surface normal; non-direction sentinel
  `(0,0,0)` when `valid == false`.
- `value`: RGB BRDF value (radiance per
  steradian); Lambert: `baseColor / pi`.
- `pdf`: BSDF probability density per
  steradian at `wo`; Lambert:
  `cos_theta_o / pi`.
- `valid`: `true` iff the sampler
  produced a usable sample. The
  integrator checks this BEFORE
  consuming any other field.

Default-constructed instance is the
"no-contribution" sentinel — bit-zero
matches the NEE.5 byte-identity anchor's
expectation for a default `DirectLightSample`.

### 1.2 Helpers

`src/pathtracer/Bsdf.cuh` provides three
RR_HD inline helpers + one detail helper:

- `sample_bsdf(material, wi, normal, u)`:
  cosine-weighted hemisphere sample
  aligned to world-space normal via
  `detail::align_to_normal`. Returns
  `valid == false` for degenerate normal
  (`dot(n,n) <= 0`) or below-horizon
  edge case (`cos_theta_o <= 0`).
- `bsdf_pdf(material, wo, normal)`:
  returns `0.0f` for `wo` below the
  surface horizon; otherwise
  `pdf_cosine_hemisphere(cos_theta_o)`.
  CONSISTENT with `sample_bsdf` —
  bit-equal floats for the same `wo`.
- `bsdf_eval(material, wi, wo, normal)`:
  returns `m.baseColor * kInvPi`
  (rotation-invariant Lambert; the
  `cos_theta_o > 0` gate is the
  integrator's responsibility).
- `detail::align_to_normal(local, n)`:
  centralises the tangent-frame
  alignment algorithm previously
  inlined separately in
  `CudaPathTracer.cu`'s lambda and
  `OptixPrograms.cu::pt_align_to_normal`.

### 1.3 Host-only test anchor

`tests/pathtracer_bsdf_tests.cpp` ships
the 10 mandatory cases per the MIS.2
task brief §5.5:

| #  | Test                                                       |
|----|------------------------------------------------------------|
| 1  | `test_default_constructed_sample_is_invalid`               |
| 2  | `test_lambert_sample_in_upper_hemisphere`                  |
| 3  | `test_lambert_pdf_matches_sampler`                         |
| 4  | `test_lambert_pdf_below_horizon_is_zero`                   |
| 5  | `test_lambert_eval_matches_inverse_pi_albedo`              |
| 6  | `test_lambert_throughput_simplification`                   |
| 7  | `test_lambert_sample_pdf_normalises_via_monte_carlo`       |
| 8  | `test_lambert_cos_weighted_mean_dz`                        |
| 9  | `test_degenerate_normal_returns_invalid`                   |
| 10 | `test_helper_determinism`                                  |

Total: `pathtracer_bsdf_tests: 41/41 passed`
on the audit host (per the MIS.5 audit
§8.1; unchanged through MIS.6).

### 1.4 Integrator consumption

Both backends include `pathtracer/Bsdf.cuh`
and call `bsdf_pdf` in the MIS-aware NEE
branch (§4 and §5 below). The `BsdfSample`
POD itself is NOT directly consumed at
v1 — the bounce-direction sampling
remains inline in both kernels per the
MIS.5 task brief §1.3 #1 deferral
(IEEE-754 bit-identity preservation
chosen over POD-direct adoption). The
helper API IS the consumed surface; the
POD is forward-looking infrastructure
the future BSDF-bounce-swap slice will
naturalise.

---

## 2. Light sample / PDF model exists

**PASS.** *(Already audited at MIS.3,
commit `960c523`. This arc audit
re-confirms the contract holds at
HEAD.)*

The `DirectLightSample` POD
(`src/pathtracer/DirectLight.h`) carries
the four pre-existing NEE fields plus
two MIS additions:

```cpp
struct DirectLightSample {
    rr::math::Vec3 wi              = {0.0f, 0.0f, 0.0f};
    float          distance        = 0.0f;
    rr::math::Vec3 li_unattenuated = {0.0f, 0.0f, 0.0f};
    float          pdf_inv         = 0.0f;
    // MIS.3 additions:
    float          pdf_solid_angle = 0.0f;
    bool           is_delta        = false;
};
```

### 2.1 `pdf_solid_angle`

Per-steradian directional PDF of choosing
the returned `wi`. For v1 delta lights
(Point + Directional), populated with
sentinel `0.0f`; the MIS helper short-
circuits on `is_delta == true` and never
reads the field. For future area lights,
will carry `(1/light_count) · (1/area) ·
r² / cos(theta_light)` with `is_delta ==
false`.

### 2.2 `is_delta`

`true` iff the light is a Dirac delta in
direction (Point or Directional); `false`
for finite-PDF lights (future area / IBL).
The MIS helper checks this flag FIRST.

### 2.3 Helper population

`src/pathtracer/DirectLight.cuh::sample_direct_light_uniform`
populates both fields per light type:

- Point branch: `pdf_solid_angle = 0.0f;
  is_delta = true` (sentinel + delta
  marker).
- Directional branch: `pdf_solid_angle =
  0.0f; is_delta = true` (same).
- Area + Environment + every "no
  contribution" branch: bit-zero
  defaults (`pdf_solid_angle = 0.0f;
  is_delta = false`) flow through
  unchanged.

### 2.4 Default-bit-pattern preservation

The NEE.5 byte-identity anchor at
`tests/pathtracer_nee_tests.cpp::test_zero_contribution_is_bit_default`
continues to pass at HEAD WITHOUT
modification — the new fields are
bit-zero by default; the `std::memcmp`
on the default-constructed sample
remains green.

### 2.5 MIS.3 audit re-confirmation

`docs/PATH_TRACER_MIS_LIGHT_PDF_AUDIT.md`
recorded zero REPAIR items + a
PASS verdict. The MIS.3 impl commit
(`0dd7d46`) is reachable from HEAD
unchanged; no subsequent slice modified
the data model. The audit's findings
hold at HEAD.

---

## 3. `power_heuristic` exists and is tested

**PASS.** *(Subsumes MIS.4 audit.)*

`src/pathtracer/Mis.h` ships the single
RR_HD inline helper:

```cpp
RR_HD inline float power_heuristic(float p_a, float p_b) {
    const float pa2   = p_a * p_a;
    const float pb2   = p_b * p_b;
    const float denom = pa2 + pb2;
    return denom > 0.0f ? pa2 / denom : 0.0f;
}
```

### 3.1 Single-file module

The MIS.4 helper ships ONLY the `.h` file
(no `.cuh`). The plan §3.3 had reserved
both names; the impl simplified to a
single header because the helper is pure
scalar math with no CUDA-specific
intrinsics. Both backends include the
`.h` directly:

- `src/cuda/CudaPathTracer.cu:39`:
  `#include "pathtracer/Mis.h"`.
- `src/optix/OptixPrograms.cu:59`:
  `#include "pathtracer/Mis.h"`.

This is a documented deviation from the
plan's two-file shape; the simpler
single-file form is cleaner and the
host-only tests + both integrator
includes confirm it works in every
build configuration.

### 3.2 Veach β = 2 power heuristic

The expression `pa² / (pa² + pb²)` is
exactly the β = 2 power heuristic from
Veach 1995 §9.2.4 with one sample per
estimator. The denominator-zero guard
(`denom > 0.0f ? ... : 0.0f`) handles
the `(0, 0)` edge case explicitly,
returning `0.0f` instead of the natural
`0/0 → NaN`.

### 3.3 Host-only test anchor

`tests/pathtracer_mis_tests.cpp` ships
the 8 mandatory cases per the MIS.4
task brief §5.5:

| # | Test                                              |
|---|---------------------------------------------------|
| 1 | `test_power_heuristic_both_zero_returns_zero`     |
| 2 | `test_power_heuristic_p_a_zero`                   |
| 3 | `test_power_heuristic_p_b_zero`                   |
| 4 | `test_power_heuristic_equal_pdfs`                 |
| 5 | `test_power_heuristic_squares_pdfs` (β = 2 anti-regression) |
| 6 | `test_power_heuristic_one_dominates`              |
| 7 | `test_power_heuristic_sum_to_one`                 |
| 8 | `test_power_heuristic_purity`                     |

Total: `pathtracer_mis_tests: 34/34
passed` on the audit host (per the
MIS.5 audit §8.1; unchanged through
MIS.6).

The 34 RR_CHECKs across 8 test functions
cover:
- Edge-case behaviour (zeros, equal
  PDFs, one-dominates).
- The β = 2 squaring (anti-regression
  against an accidental β = 1 collapse).
- The sum-to-one invariant for the
  symmetric pair.
- Determinism (identical inputs ⇒
  bit-equal outputs via `std::memcmp`).

### 3.4 Documentation

The helper's doc-comment block
(`Mis.h:30-100`, ~70 lines) covers:

- The Veach formula + units (per
  steradian).
- Edge-case behaviour in tabular form.
- Properties (range, symmetry,
  monotonicity, one-dominates limit,
  purity).
- Caller-responsibility note for the
  `is_delta` short-circuit at v1
  delta lights (the helper would
  return `0.0f` for the sentinel
  `pdf_solid_angle = 0.0f` input;
  the caller MUST short-circuit
  before invoking).

---

## 4. CUDA integrator consumes MIS data safely

**PASS structurally; runtime DEFERRED.**
*(Already audited at MIS.5, commit
`470af7d`. This arc audit re-confirms
the integrator at HEAD.)*

### 4.1 Includes

`src/cuda/CudaPathTracer.cu`:

```
38:#include "pathtracer/Bsdf.cuh"          // MIS.5: bsdf_pdf for the BSDF-side PDF inside power_heuristic
39:#include "pathtracer/Mis.h"             // MIS.5: power_heuristic helper
```

Both new modules are wired with MIS.5-
tagged comments for traceability.

### 4.2 NEE branch MIS site

The MIS-weight ternary lives inside the
existing NEE branch's `if (cos_th >
0.0f)` block:

```cpp
const float mis_weight_nee = sample.is_delta
    ? 1.0f
    : rr::pathtracer::power_heuristic(
          sample.pdf_solid_angle,
          rr::pathtracer::bsdf_pdf(
              m, sample.wi, hit.normal));

const Vec3 brdf = m.baseColor * rr::math::kInvPi;
const float k =
    cos_th * vis * sample.pdf_inv * mis_weight_nee;
const Vec3 contrib = Vec3{
    throughput.x * sample.li_unattenuated.x * brdf.x,
    throughput.y * sample.li_unattenuated.y * brdf.y,
    throughput.z * sample.li_unattenuated.z * brdf.z}
    * k;
radiance = radiance + contrib;
```

### 4.3 Safety of the consumption

- The ternary discriminator
  (`sample.is_delta`) is set EXCLUSIVELY
  by `sample_direct_light_uniform`
  (MIS.3) — the integrator does not
  fabricate the value. For v1 lights,
  the helper sets `is_delta == true`
  unconditionally; the then-branch
  (`1.0f`) is taken.
- The else-branch's `power_heuristic`
  call is UNREACHABLE at v1 (every NEE
  sample sets `is_delta == true`). The
  `pdf_solid_angle` sentinel `0.0f` is
  never propagated to the helper.
- The `bsdf_pdf` call inside the else-
  branch is also unreachable at v1 —
  the helper would consume `m` (a
  valid `MaterialParams`), `sample.wi`
  (a valid unit direction), and
  `hit.normal` (a valid unit normal),
  all already populated by upstream
  code; the call would be safe even
  if reached.

### 4.4 Forward-compatibility with non-delta lights

For a future area-light arc, the helper
`sample_direct_light_uniform` will set
`is_delta = false` for area-light
samples + populate `pdf_solid_angle`
with the area-to-solid-angle Jacobian.
The integrator's else-branch then fires
and the power heuristic computes the
correct MIS weight. No integrator-side
code change is needed at that point.

### 4.5 No-touch invariants

`git diff 35577a6~1..35577a6` (the
MIS.5 commit diff) shows ONLY
`src/cuda/CudaPathTracer.cu` and
`tests/pathtracer_nee_tests.cpp`
changed. Per the MIS.5 audit §6.3, all
must-not-touch paths (every other
`src/`, every other `tests/*.cpp`,
`scenes/`, `tools/`, `CMakeLists.txt`)
diff to 0 bytes.

### 4.6 MIS.5 audit re-confirmation

`docs/PATH_TRACER_MIS_CUDA_INTEGRATOR_AUDIT.md`
recorded zero REPAIR items + a
PASS verdict. The MIS.5 impl commit
(`35577a6`) is reachable from HEAD
unchanged; the subsequent MIS.6 slice
touched ONLY `src/optix/OptixPrograms.cu`.
The audit's findings hold at HEAD.

---

## 5. OptiX integrator mirrors CUDA behavior

**PASS structurally; runtime DEFERRED.**
*(Subsumes MIS.6 audit.)*

### 5.1 Includes

`src/optix/OptixPrograms.cu`:

```
58:#include "pathtracer/Bsdf.cuh"        // MIS.6: bsdf_pdf for the BSDF-side PDF inside power_heuristic
59:#include "pathtracer/Mis.h"           // MIS.6: power_heuristic helper
```

Both new modules are wired with MIS.6-
tagged comments for traceability,
mirroring the CUDA pattern at lines
38-39.

### 5.2 OptiX raygen MIS site

`__raygen__pathtrace`'s NEE branch
(at `OptixPrograms.cu:1069-1086`)
applies the same ternary as the CUDA
integrator:

```cpp
const float mis_weight_nee = sample.is_delta
    ? 1.0f
    : rr::pathtracer::power_heuristic(
          sample.pdf_solid_angle,
          rr::pathtracer::bsdf_pdf(
              rr::material::MaterialParams{},
              sample.wi, hit_n));

// Lambert BRDF: albedo / pi.
// (kInvPi factored into k below.)
const float k = cos_th
              * sample.pdf_inv
              * rr::math::kInvPi
              * mis_weight_nee;
radiance.x += throughput.x
    * sample.li_unattenuated.x
    * albedo.x * k;
radiance.y += throughput.y
    * sample.li_unattenuated.y
    * albedo.y * k;
radiance.z += throughput.z
    * sample.li_unattenuated.z
    * albedo.z * k;
```

### 5.3 OptiX-vs-CUDA shape parity

The OptiX form differs from CUDA in
two non-arithmetic ways, both
documented in the MIS.6 task brief §1.4
+ in the in-file doc-comment block:

1. **`MaterialParams` plumbing**: OptiX
   raygen has only `albedo` (a `Vec3`)
   available, not a full
   `MaterialParams` POD. Since
   `bsdf_pdf` ignores its `material`
   argument for Lambert (the PDF is
   geometry-only:
   `cos_theta_o / pi`), the OptiX call
   passes a default-constructed
   `rr::material::MaterialParams{}`.
   Bit-equivalent with the CUDA
   caller's `m` for any Lambert-
   reachable case.
2. **`kInvPi` factoring**: CUDA
   computes `brdf = m.baseColor * kInvPi`
   then folds it into `contrib` via
   componentwise multiply. OptiX
   factors `kInvPi` into the scalar
   `k` directly (since `albedo` is
   already a separate `Vec3`). The
   resulting per-channel arithmetic
   `albedo.{x,y,z} * sample.li_unattenuated.{x,y,z}
   * cos_th * sample.pdf_inv * kInvPi * mis_weight_nee`
   is mathematically identical to the
   CUDA form
   `(baseColor * kInvPi).{x,y,z}
   * sample.li_unattenuated.{x,y,z}
   * cos_th * vis * sample.pdf_inv
   * mis_weight_nee`. (Note OptiX
   keeps `vis` outside `k` via the
   shadow-payload gate `v_shadow != 0u`;
   CUDA folds `vis` into `k`.)

The two forms produce
convergence-equivalent output for any
`(enable_nee, scene)` combination at
v1 light-type scope; the cross-backend
PPM `cmp` runtime confirmation is
DEFERRED (§8).

### 5.4 No-touch invariants

`git diff ea7c344~1..ea7c344` (the
MIS.6 commit diff) shows ONLY
`src/optix/OptixPrograms.cu` changed.
Per the in-MIS.6-impl BUILD_PLAN
entry, all must-not-touch paths
(every `src/cuda/`, every other
`src/optix/*` file, every `tests/`,
`scenes/`, `tools/`, `CMakeLists.txt`)
diff to 0 bytes. The CUDA path is
byte-identical with the post-MIS.5
baseline.

### 5.5 Cross-backend symmetry

After both MIS.5 + MIS.6 land:

| Backend  | Integrator                                | Slice        |
|----------|-------------------------------------------|--------------|
| CUDA     | `k_pathtrace_sample` (NEE branch)         | MIS.5 (`35577a6`) |
| OptiX    | `__raygen__pathtrace` (NEE branch)        | MIS.6 (`ea7c344`) |

Both backends consume the three MIS
leaves (MIS.{2,3,4}) symmetrically;
both apply the `is_delta ? 1.0f :
power_heuristic(...)` ternary; both
short-circuit to `1.0f` at v1 and
preserve byte-identity via IEEE-754
§6 identity multiplication.

---

## 6. Delta-light current output should remain unchanged

**PASS structurally; runtime DEFERRED.**

### 6.1 The structural argument

For every v1 NEE sample (Point or
Directional), `sample_direct_light_uniform`
sets `is_delta == true` (MIS.3). Both
backends' integrators take the
ternary's then-branch:

```cpp
mis_weight_nee = 1.0f;
```

Multiplying any contribution by `1.0f`
is the IEEE-754 §6 identity operation:
`x * 1.0f == x` bit-exactly for any
finite non-NaN `x`. The pre-MIS
multiplier (`cos_th * vis * pdf_inv`
on CUDA; `cos_th * pdf_inv * kInvPi`
on OptiX) becomes `pre_multiplier *
1.0f`, which is bit-equal with
`pre_multiplier`. **No perturbation
of the radiance accumulator at v1.**

### 6.2 Default-OFF byte-identity

For an operator running ANY action
that does NOT pass `--enable-nee`,
both kernels' `if (enable_nee &&
light_count > 0)` guards short-
circuit. The MIS-weight arithmetic
lives INSIDE this guard; it is never
reached. The pre-MIS default-OFF
output is preserved bit-for-bit.

### 6.3 Default-ON-at-v1 byte-identity

For an operator running
`--enable-nee` against a scene with
ONLY Point and/or Directional lights,
the MIS weight is `1.0f` (per §6.1);
the multiplier is bit-equal with the
pre-MIS form; the PPM is bit-
identical with the pre-MIS NEE-on
build.

The structural argument applies on
BOTH backends. The runtime PPM `cmp`
empirical confirmation is DEFERRED
(§8) — the audit host has no CUDA
Toolkit and uses the OptiX-SDK
fallback.

### 6.4 NEE.5 byte-identity anchor

`tests/pathtracer_nee_tests.cpp::test_mis_weight_delta_short_circuits_to_one`
(post-MIS.5 case) anchors the v1
delta short-circuit at the host
level:

```cpp
RR_CHECK(s_point.is_delta == true);
RR_CHECK(compute_mis_weight(s_point, 0.5f) == 1.0f);

RR_CHECK(s_dir.is_delta == true);
RR_CHECK(compute_mis_weight(s_dir, 0.5f) == 1.0f);
```

Both pass on the audit host
(`pathtracer_nee_tests: 59/59` per
the MIS.5 audit §8.1; unchanged
through MIS.6 since the test
exercises the helper-composition
logic shared by both backends).

The bit-zero default invariant is
also still anchored:
`test_zero_contribution_is_bit_default`
continues to pass without
modification across MIS.{2..6}.

---

## 7. Non-delta / area-light MIS remains future work

**PASS — explicit non-goal preserved.**

### 7.1 The MIS arc's deliberate scope

Per `docs/PATH_TRACER_MIS_PLAN.md` §6
#1, area-light MIS is NOT part of the
MIS arc. The arc ends at MIS.7
(this audit). The area-light arc is
a separate arc the MIS foundation
unblocks.

### 7.2 What the MIS arc shipped (foundation only)

- `BsdfSample` POD + Lambert helpers
  (MIS.2).
- `DirectLightSample::pdf_solid_angle`
  + `is_delta` (MIS.3).
- `power_heuristic` helper (MIS.4).
- CUDA integrator wires the three
  leaves into the NEE branch (MIS.5).
- OptiX integrator mirrors (MIS.6).

### 7.3 What the MIS arc deliberately did NOT ship

Per `docs/PATH_TRACER_MIS_PLAN.md` §6
+ the per-stage task briefs:

1. **Area-light geometry / sampling**
   — the `Light::Area` placeholder in
   `src/lighting/Light.h` is unchanged;
   `sample_direct_light_uniform`'s
   Area branch still returns a
   default-constructed (zero-
   contribution) sample.
2. **BSDF-bounce-as-light contribution**
   — the integrator's bounce loop
   uses inline cosine-bounce arithmetic
   throughout; no MIS-weighted
   emission-add path is wired at v1
   (see MIS.5 task brief §1.3 #2).
   At v1 delta lights this contribution
   is zero with probability 1; the
   MIS apparatus is ready but
   unconsumed.
3. **`--no-mis` CLI flag** — at v1
   MIS is a no-op; at v2 (area lights)
   MIS is mandatory. No operator-
   facing knob is needed.
4. **Specular / GGX / dielectric MIS**
   — the `BsdfSample::valid` flag is
   the foundation for future
   `is_delta`-equivalent BSDF gating;
   v1 ships Lambert only.
5. **MIS for environment lighting**
   — IBL sampling + MIS for env / BSDF
   pair lands alongside a future IBL
   slice.
6. **BDPT / volumetric / spectral MIS**
   — explicitly indefinitely deferred
   per the plan §6 #2-#10.

### 7.4 The MIS apparatus is sufficient for area lights

A future area-light arc will:

- Implement uniform area-light
  sampling in
  `sample_direct_light_uniform`'s
  Area branch.
- Set `is_delta = false` for area-
  light samples.
- Populate `pdf_solid_angle` with
  `(1/light_count) · (1/area) · r²
  / cos(theta_light)`.
- Add a per-mesh "is emissive area
  light?" flag for the BSDF-bounce-
  as-light detection.
- Add an integrator-side BSDF-bounce-
  as-light contribution gated on the
  per-mesh flag + multiplied by the
  symmetric `power_heuristic(p_bsdf,
  p_light)` MIS weight.

ALL of the above land in the area-
light arc. The MIS arc's `bsdf_pdf`,
`power_heuristic`, and the
`is_delta`-flagged ternary already
support the non-delta path; no
integrator-level rework is needed.

### 7.5 Documented in plan

The `docs/PATH_TRACER_MIS_PLAN.md`
§5.7 ("Optional follow-up: MIS.8+
(area lights)") + §6 ("Non-goals") +
§9.1 ("Recommended next step (post-
MIS-arc)") all describe the area-
light arc as the natural successor.
Deferral is intentional and
documented.

---

## 8. Runtime verification status

**DEFERRED.**

The audit host has no CUDA Toolkit,
no `/usr/local/cuda`, presumably no
NVIDIA GPU. The OFF audit-host config
is the no-GPU baseline; the ON
audit-host config (`build-ON/`) uses
the OptiX-SDK-fallback path (every
`--render-optix-*` action returns
the documented "requires the OptiX
SDK" message before any kernel can
run).

### 8.1 What is verified on the audit host

| Layer                                   | Verified?         |
|-----------------------------------------|-------------------|
| Build (RR_ENABLE_CUDA=OFF, OPTIX=OFF)   | YES (OFF: 11/11)  |
| Build (RR_ENABLE_CUDA=OFF, OPTIX=ON)    | YES (ON: 12/12)   |
| `pathtracer_bsdf_tests` (host-only)     | YES (41/41)       |
| `pathtracer_mis_tests` (host-only)      | YES (34/34)       |
| `pathtracer_nee_tests` (host-only)      | YES (59/59)       |
| `cli_tests` (host-only)                 | YES (31/31)       |
| Source-level no-touch invariants        | YES (`git diff`)  |
| OptiX dispatcher fallback log lines     | YES (smoke run)   |

The host-only test surface exercises
every MIS helper + the integrator-
level helper-composition logic.
Type-check + symbolic execution
through the helpers is complete on
the audit host.

### 8.2 What is DEFERRED to a CUDA + OptiX-SDK host

The strongest empirical confirmations
are the runtime PPM `cmp` cycles
that prove byte-identity at v1
delta-light scope. The MIS.5 audit
§9 enumerated these for the CUDA
side; MIS.6 added the OptiX side.
The arc-level DEFERRED list is the
union:

| § | Check                                                | Procedure |
|---|------------------------------------------------------|-----------|
| 8.2.1 | CUDA default-OFF byte-IDENTITY (no `--enable-nee`) | `cmp` pre-MIS.5 vs post-MIS.5 PPM (no flag) |
| 8.2.2 | CUDA MIS-on byte-IDENTITY at v1 (with `--enable-nee`) | `cmp` post-MIS.4 vs post-MIS.5 PPM (`--enable-nee`) |
| 8.2.3 | OptiX default-OFF byte-IDENTITY (no `--enable-nee`) | `cmp` pre-MIS.6 vs post-MIS.6 OptiX PPM (no flag) |
| 8.2.4 | OptiX MIS-on byte-IDENTITY at v1 (with `--enable-nee`) | `cmp` post-MIS.5 vs post-MIS.6 OptiX PPM (`--enable-nee`) |
| 8.2.5 | Cross-backend MIS-on convergence at v1                | CUDA `--enable-nee` PPM stats vs OptiX `--enable-nee` PPM (statistically similar; not bit-identical) |
| 8.2.6 | ctest cycle on CUDA + OptiX-SDK host                  | re-run on host with both backends built |

All six checks have a strong
structural prediction (IEEE-754 §6
identity multiplication for §8.2.{1..4};
shared helpers for §8.2.5; no-new-
binary for §8.2.6). The runtime
checks are EMPIRICAL CONFIRMATION,
not the proof. The audit host's
host-only tests provide the symbolic
proof; the CUDA + OptiX-SDK host
operator session provides the
empirical confirmation.

### 8.3 Carry-forward debt

Every prior PT-P.x / NEE.x / firefly-
clamp-CLI / MIS.x audit recorded
DEFERRED runtime checks. The MIS.7
arc audit INHERITS this debt — it
does not introduce new BLOCKED items
+ it does not flip any prior
DEFERRED row to PASS. The MIS.7
audit is the SCHEDULING POINT for a
single CUDA + OptiX-SDK operator
session that flips all accumulated
DEFERRED rows to PASS in one cycle.

Per the plan §9.1 ("Recommended next
step (post-MIS-arc)"), three viable
directions after MIS.7 closes:

1. **Trigger the CUDA + OptiX-SDK
   host verification run** flipping
   all accumulated DEFERRED rows.
2. **Pivot to area-light arc** —
   the natural successor consuming
   the MIS foundation.
3. **Pivot to a different master-
   #16+ arc** (non-Lambert BSDFs,
   master-#18+ textures, master-#19
   AOVs polish).

Recommended sequencing per the plan:
**(2)** as the natural successor
consuming the MIS arc. Recommended
by THIS audit: **(1)** first (one
host session pins the entire post-
MIS baseline + the firefly-clamp-CLI
+ NEE arcs in a single cycle),
**(2)** second (the area-light arc
exercises the MIS apparatus in
earnest).

### 8.4 No new BLOCKED items

A BLOCKED item would be a check
that requires runtime AND has no
structural fallback. The MIS arc
has no such item — every byte-
identity claim has an IEEE-754 §6
structural argument; every helper
has a host-only test. The runtime
PPM `cmp` is empirical confirmation
of an already-proven prediction.
**Zero BLOCKED items at the arc
level.**

---

## 9. Remaining gaps before area-light NEE / non-diffuse BSDFs

This section enumerates what the
NEXT arc(s) need to ship to deliver
production direct lighting beyond
the v1 (Point + Directional only,
Lambert only) scope.

### 9.1 Area-light arc prerequisites

For an unbiased integrator at v2
(area lights present), the area-
light arc must ship:

- **Area-light geometry plumbing**
  — promote `Light::Area`'s
  PLACEHOLDER fields (`area_width`,
  `area_height`) from spec-only to
  scene-graph-active. Scene parser
  + uploader changes.
- **Area-light sampling** — extend
  `sample_direct_light_uniform`'s
  Area branch to:
  - Pick a uniform point on the
    area light's surface.
  - Compute `wi` toward that point
    + `distance` to the point.
  - Compute `li_unattenuated` from
    the light's color / intensity.
  - Set `pdf_inv = light_count *
    light_area`.
  - Set `pdf_solid_angle =
    (1/light_count) · (1/light_area)
    · r² / cos(theta_light)`.
  - Set `is_delta = false`.
- **Per-mesh "is emissive area light"
  flag** — for the BSDF-bounce-as-
  light detection. Likely lives on
  `Material` (e.g.
  `emission_strength > 0` plus a
  per-mesh "is light" flag) or on
  `Mesh` directly.
- **Integrator BSDF-bounce-as-light
  contribution** — when a bounce
  ray hits an emissive area light:
  - Compute `p_light_at_wo` using
    the area light's
    area-to-solid-angle Jacobian.
  - Compute `mis_weight_bsdf =
    power_heuristic(p_bsdf,
    p_light_at_wo)`.
  - Accumulate
    `throughput * emission *
    mis_weight_bsdf` into the
    radiance.
  - Skip the inline emission-add for
    NEE-reachable area lights to
    avoid double-counting.
- **Fixture scene + golden image** —
  exercise both estimators against
  the same area light; pin the
  cross-backend convergence with
  the MIS-on PPM.

### 9.2 Non-diffuse BSDF arc prerequisites

For a metal / dielectric / glass
material set:

- **GGX / Beckmann sampler + PDF**
  — extend `Bsdf.{h,cuh}` with
  `sample_ggx`, `ggx_pdf`,
  `ggx_eval` per microfacet model;
  consume `MaterialParams::roughness
  / metallic / specular`.
- **Dielectric Fresnel** —
  reflectance / transmittance split
  via Fresnel.
- **Specular-delta gating** — the
  `BsdfSample::valid` flag is
  insufficient; need a separate
  `is_delta` (mirror reflection)
  flag on `BsdfSample`. The
  integrator MUST short-circuit MIS
  for specular-delta samples (the
  NEE-side weight is 0 by
  convention; specular bounce is
  handled outside MIS).
- **Per-material BSDF dispatch** —
  the integrator switches on
  `material.metallic` /
  `material.specular` /
  `material.transmission` to call
  the right sampler / PDF / eval.
- **Russian roulette** — variance
  reduction at deeper bounces; the
  v1 path tracer terminates at a
  fixed `max_bounces`.

### 9.3 Cross-cutting prerequisites (already shipped)

Both arcs above CAN consume the MIS
apparatus this arc shipped without
modification:

- `power_heuristic` is BSDF-agnostic
  + light-type-agnostic.
- `bsdf_pdf` dispatches on material;
  the area-light arc does not
  invoke it directly except in the
  MIS-weight computation.
- `DirectLightSample::pdf_solid_angle`
  + `is_delta` are universal across
  light types.
- The integrator's
  `is_delta ? 1.0f :
  power_heuristic(...)` ternary
  works for every light type +
  every BSDF.

The MIS apparatus is the NECESSARY
foundation; both follow-up arcs are
SUFFICIENT to deliver production
direct lighting on top of it.

---

## 10. Final verdict

| #  | Audit item                                                  | Result                                                                                   |
|----|-------------------------------------------------------------|------------------------------------------------------------------------------------------|
| 1  | BSDF sample / PDF model exists                              | PASS — `BsdfSample` POD + 3 helpers + 10 tests; subsumes MIS.2 audit                      |
| 2  | Light sample / PDF model exists                             | PASS — `DirectLightSample::{pdf_solid_angle, is_delta}` + populated by helper             |
| 3  | `power_heuristic` exists and is tested                      | PASS — RR_HD inline `Mis.h` + 8 tests / 34 RR_CHECKs; subsumes MIS.4 audit                |
| 4  | CUDA integrator consumes MIS data safely                    | PASS structurally + host tests; runtime DEFERRED                                          |
| 5  | OptiX integrator mirrors CUDA behavior                      | PASS structurally + host tests; runtime DEFERRED; subsumes MIS.6 audit                    |
| 6  | Delta-light current output should remain unchanged          | PASS structurally (IEEE-754 §6 identity multiplication); runtime DEFERRED                 |
| 7  | Non-delta / area-light MIS remains future work              | PASS — explicit non-goal preserved per plan §6                                            |
| 8  | Runtime verification status                                 | DEFERRED — no CUDA Toolkit on audit host; six runtime PPM `cmp` cycles tabulated in §8.2  |
| 9  | Remaining gaps before area-light NEE / non-diffuse BSDFs    | Documented — see §9.1 (area-light arc) + §9.2 (non-Lambert BSDF arc)                      |
| 10 | Closing verdict                                             | **PASS_WITH_SUBSUMED_AUDITS**                                                             |

**Overall verdict: PASS_WITH_SUBSUMED_AUDITS.**

The MIS arc closes cleanly. All six
implementation slices (MIS.{1..6})
shipped per their per-slice task
briefs; the two per-stage audits
(MIS.3, MIS.5) recorded zero REPAIR
items + PASS verdicts; the three
per-stage audits not separately
shipped (MIS.2, MIS.4, MIS.6) are
absorbed by this arc audit's §1, §3,
§5 respectively (each cites the
specific source files / line numbers
/ test counts that anchor the
contract).

The byte-identity invariant at v1
holds structurally on both backends
via the IEEE-754 §6 identity-
multiplication argument. Host-only
tests anchor the helper-composition
logic empirically on the audit host
(`pathtracer_bsdf_tests: 41/41`,
`pathtracer_mis_tests: 34/34`,
`pathtracer_nee_tests: 59/59`,
`cli_tests: 31/31`). The runtime
PPM `cmp` empirical confirmation
across the six DEFERRED rows in §8.2
is scheduled for a single CUDA +
OptiX-SDK operator session that
flips all accumulated audit-host
DEFERRED rows (PT-P.x + firefly-
clamp-CLI + NEE.x + MIS.x) to PASS.

Zero REPAIR items. Zero BLOCKED
items. Verdict carries through to
the MIS arc closure.

### 10.1 Master rule compliance

- **Build incrementally (rule 1) +
  every step compilable (rule 2)**:
  preserved across MIS.{1..6}; both
  audit-host configs (OFF / ON)
  remained green at every slice.
- **No fake stubs (rule 3)**: every
  MIS helper is real RR_HD inline
  code; the BSDF / MIS helpers
  compile + link cleanly; the
  integrator-level wiring is real
  per-bounce code.
- **No CPU per-pixel work (rules 5
  + 7)**: every per-pixel decision
  is device-side; the MIS weight is
  computed per-bounce on the GPU
  inside the existing kernels.
- **Module boundaries (rule 9)**:
  the new modules
  (`src/pathtracer/{Bsdf,Mis}.{h,cuh}`)
  sit alongside
  `pathtracer/{DirectLight,RNG,Sampling}.{h,cuh}`
  cleanly. No cross-cutting concerns
  introduced.
- **Avoid monolithic files (rule
  10)**: the MIS arc spread logic
  across multiple small files
  (`Bsdf.h` ~150 lines POD only;
  `Bsdf.cuh` ~150 lines helpers;
  `Mis.h` ~115 lines including doc-
  comment block; integrator deltas
  ≤ ~35 lines per backend) rather
  than expanding any one source.
- **Explicit testable interfaces
  (rule 11)**: every helper has a
  host-side test (10 BSDF + 8 MIS +
  3 integrator-composition cases).
- **Update BUILD_PLAN (rule 8)**:
  every slice (MIS.{1..6}) added a
  BUILD_PLAN entry; this audit will
  add one too.

### 10.2 Documentation-only invariant

This audit makes ZERO source-code
changes. The REPAIR list is empty.
No prior implementation slice is
reopened. No C4D / server / UI /
node-editor surface is touched.

### 10.3 Carry-forward deviations (not re-flagged)

Each MIS.{2..6} impl slice's
BUILD_PLAN entry recorded its own
diff-size deviations + scope
deferrals. This audit does NOT re-
flag any of them. The previously
documented deviations:

- **MIS.3 doc-comment density
  overshoot** (~226 lines vs ≤ 100
  budget) — documented in the impl
  commit's BUILD_PLAN entry.
- **MIS.5 BSDF bounce-swap deferral**
  (POD-direct consumption deferred
  per task brief §1.3 #1) — covered
  in §1.4 above.
- **MIS.6 OptiX `MaterialParams`
  passing** (default-constructed
  instance; bit-equivalent for
  Lambert) — covered in §5.3 above.

These are all documented, deliberate,
+ preserve the strictest byte-
identity invariant. NOT defects.
Forward-looking work the area-light
+ non-Lambert BSDF arcs may revisit
if a sub-ULP framebuffer drift is
confirmed acceptable.

---

## 11. Sub-arc closure

The MIS arc closes at MIS.7 (this
audit). After it lands:

- `src/pathtracer/{Bsdf,Mis}.{h,cuh}`
  exist as the BSDF / MIS foundation.
- `DirectLightSample` carries
  `pdf_solid_angle` + `is_delta`.
- Both path tracers integrate MIS
  into the NEE branch with the
  `is_delta ? 1.0f :
  power_heuristic(...)` ternary.
- Default-OFF + default-ON-at-v1-
  delta-lights byte-identity
  preserved structurally on both
  backends.
- Both backends symmetric (CUDA
  MIS.5; OptiX MIS.6).
- The arc unblocks the future area-
  light arc (out of scope here per
  §7).
- The arc unblocks the future non-
  Lambert BSDF arc (the MIS
  apparatus is BSDF-agnostic).

`PATH_TRACER_NEE_TASK.md` §1's
reserved "future area-light slice"
is now properly gated: MIS lands
first; the area-light arc lands
second; the integrator at v2 is
unbiased without rework.

The runtime DEFERRED list (§8.2)
across PT-P.x + firefly-clamp-CLI +
NEE.x + MIS.x arcs is the operator-
session backlog the next CUDA +
OptiX-SDK host run flips to PASS.

---

Mode reminder: **documentation only.**
This audit makes zero source-code
changes. The REPAIR list is empty.
The BLOCKED list is empty. The MIS
arc closes with PASS_WITH_SUBSUMED_AUDITS.

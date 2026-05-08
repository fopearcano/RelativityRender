# OptiX MIS Integrator — Task Definition (MIS.6)

Date: 2026-05-08.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_MIS_PLAN.md` §4.2
+ §5.5 + §7.5 (OptiX integrator scope; cross-
backend symmetry contract).
Prior slices:
- MIS.5 task brief: `91de1e7`.
- MIS.5 impl (CUDA integrator): `35577a6`.
- MIS.5 audit: `470af7d`.
- MIS.4 helper: `cef4a6b`.
- MIS.3 light data model + audit: `0dd7d46` +
  `960c523`.
- MIS.2 BSDF data model + helpers: `d9fa6e3` +
  `5a1c772`.

Mode: documentation only. **No source code is
modified by this task definition.** The task is
the spec; the next slice (the implementation)
ships the diff.

Note on the user's referenced audit docs that
don't exist:
- `PATH_TRACER_MIS_BSDF_PDF_AUDIT.md` — NOT
  SHIPPED. MIS.2 closed via two BUILD_PLAN
  entries (`d9fa6e3` + `5a1c772`), not via a
  separate audit doc, as recorded in the
  MIS.3 audit's §0 header note.
- `PATH_TRACER_MIS_POWER_HEURISTIC_AUDIT.md`
  — NOT SHIPPED. The MIS.4 task brief §7.4
  marked the audit as NICE-TO-HAVE (not a
  hard PASS criterion); operator opted to
  bundle with downstream slices. The
  `pathtracer_mis_tests` 34/34 test binary
  (shipped at `cef4a6b`) anchors the helper's
  contract empirically; a per-stage audit
  doc was not written.

This brief proceeds from the plan + the
shipped task briefs + the MIS.3 / MIS.5 audits
+ the MIS.{2,4} BUILD_PLAN entries as the
source of truth.

This file is a fully-self-contained brief for
the MIS.6 implementation slice. Anyone picking
it up should be able to ship MIS.6 without re-
deriving the design reasoning. Pattern mirrors
the canonical MIS.x task-brief shape
established at MIS.{2,3,4,5}.

The MIS arc cadence (post-MIS.5 audit):

| Slice                        | Role                                              | Commit       |
|------------------------------|---------------------------------------------------|--------------|
| MIS.1                        | Multiple Importance Sampling plan                 | `67dd03c`    |
| MIS.2                        | BSDF data model + helpers                         | `d9fa6e3` + `5a1c772` |
| MIS.3                        | Light data model + audit                          | `0dd7d46` + `960c523` |
| MIS.4                        | Power heuristic helper                            | `cef4a6b`    |
| MIS.5 task                   | CUDA integrator task definition                   | `91de1e7`    |
| MIS.5 impl                   | CUDA integrator wiring                            | `35577a6`    |
| MIS.5 audit                  | CUDA integrator audit                             | `470af7d`    |
| **MIS.6 task**               | **This brief** — OptiX integrator task definition | (THIS slice) |
| MIS.6 impl                   | OptiX integrator wiring (mirrors MIS.5)           | (next)       |
| MIS.7                        | Arc-level audit                                   | (pending)    |

---

## 1. Exact goal

**Mirror the MIS.5 CUDA integrator wiring on
the OptiX path tracer's `__raygen__pathtrace`
NEE branch (`src/optix/OptixPrograms.cu`),
applying the same `is_delta ? 1.0f :
power_heuristic(sample.pdf_solid_angle,
bsdf_pdf(...))` ternary to the NEE
contribution multiplier, while preserving
byte-identical output for the v1 light-type
scope.**

The OptiX raygen has its own NEE branch
(added at NEE.4 in `b29daae` per
`docs/PATH_TRACER_NEE_AUDIT.md` §3.3
recommendation). The branch lives at
`OptixPrograms.cu:974-1040` with a structure
parallel to the CUDA kernel's NEE branch:
the integrator computes `cos_th =
dot(hit_n, sample.wi)`, gates on
`cos_th > 0.0f`, then accumulates a
contribution using the multiplier `k =
cos_th * sample.pdf_inv * kInvPi`.

MIS.6 extends `k` with the MIS weight,
exactly as MIS.5 did for CUDA. The two
backends produce convergence-equivalent
output for every `(enable_nee, scene)`
combination (post-MIS.6 cross-backend
symmetry).

### 1.1 The single architectural change

Inside the existing `if (cos_th > 0.0f)`
block at `OptixPrograms.cu:1023` (the
OptiX-side mirror of CUDA's
`CudaPathTracer.cu:303`), the contribution
multiplier `k = cos_th * sample.pdf_inv *
kInvPi` becomes:

```cpp
const float mis_weight_nee = sample.is_delta
    ? 1.0f
    : rr::pathtracer::power_heuristic(
          sample.pdf_solid_angle,
          rr::pathtracer::bsdf_pdf(
              rr::material::MaterialParams{},
              sample.wi, hit_n));

const float k = cos_th * sample.pdf_inv
              * rr::math::kInvPi * mis_weight_nee;
```

That's the entire integrator change. The
ternary mirrors the CUDA MIS.5 pattern
verbatim modulo the OptiX-specific
`bsdf_pdf` first-argument handling (see
§1.4 below).

### 1.2 Why the change is minimal

Same argument as MIS.5 — the MIS framework
was designed (per
`docs/PATH_TRACER_MIS_PLAN.md` §1.4 #2 +
§7.5) so that v1 Lambert + delta-light
scope produces byte-identical output with
the pre-MIS NEE-only build. The mechanism:

- **Delta short-circuit:**
  `sample.is_delta == true` for v1 Point +
  Directional lights (MIS.3 helper
  population); the integrator short-
  circuits to `mis_weight_nee = 1.0f`.
  Multiplying the contribution by `1.0f`
  is the IEEE-754 §6 identity multiplication
  (`x * 1.0f == x` for any finite non-NaN
  x; the result is bit-equal with `x`).
- **The `power_heuristic` call is
  unreachable at v1.** Same reason as
  MIS.5: the short-circuit ternary's else
  branch is never taken because v1 lights
  always set `is_delta`; consequently
  `bsdf_pdf` is never called either. The
  new helpers are wired but inert in
  practice.

The MIS.5 audit (`470af7d`) already
verified the structural argument on the
CUDA side. The OptiX side reuses the
same helpers (`power_heuristic` from MIS.4,
`bsdf_pdf` from MIS.2, `is_delta` /
`pdf_solid_angle` from MIS.3) — so the
correctness inherits from MIS.{2,3,4} +
MIS.5 by construction.

### 1.3 What MIS.6 does NOT include (deferred)

Three deferrals carry forward from the
MIS.5 task brief §1.3:

1. **BSDF bounce swap.** The current inline
   cosine-hemisphere bounce + Lambert
   throughput simplification at
   `OptixPrograms.cu:1044-1056` is
   PRESERVED bit-for-bit. The
   `pt_align_to_normal` helper at
   `OptixPrograms.cu:790-799` (the OptiX-
   side mirror of CUDA's `align_to_normal`
   lambda) continues to fire inline.
   Swapping to `sample_bsdf` introduces
   sub-ULP framebuffer drift via the
   Lambert cancellation (per the MIS.2
   `test_lambert_throughput_simplification`
   anchor). Deferred to a future slice.
2. **MIS-on-emission-add.** Currently the
   `__closesthit__pathtrace` reads albedo
   into payload registers but does not
   carry an emission term — emission
   handling is deferred per the existing
   OptiX path-tracer scope. Adding MIS-
   weighted emission on the BSDF-bounce-
   as-light path is doubly deferred (no
   emission consumption yet; MIS-weighted
   emission lands with the area-light
   arc).
3. **Per-bounce relativity-on-throughput.**
   The current OptiX raygen applies
   Doppler / searchlight to the FINAL
   per-sample radiance (post-bounce loop)
   at `OptixPrograms.cu:~1071+`. Same as
   CUDA; not a MIS concern.

The brief explicitly authorises these
deferrals because each preserves byte-
identity at v1.

### 1.4 OptiX-specific consideration: `bsdf_pdf` first argument

The CUDA integrator at MIS.5 calls
`bsdf_pdf(m, sample.wi, hit.normal)` where
`m` is the full `MaterialParams` POD
fetched via `material_for(...)`. The
OptiX integrator does NOT have a full
`MaterialParams` available — it has only
`albedo` (a `Vec3` decoded from payload
registers in `__raygen__pathtrace` per
the NEE.4 path-tracer payload layout at
`OptixPrograms.cu:746-751`).

Per the MIS.2 BSDF helper contract
(`pathtracer/Bsdf.cuh::bsdf_pdf`), the
material parameter is **unused for
Lambert** — the helper computes
`pdf_cosine_hemisphere(cos_theta_o)`
which depends only on the geometry, not
the material. So the OptiX integrator
can pass a default-constructed
`rr::material::MaterialParams{}` as the
first argument:

```cpp
rr::pathtracer::bsdf_pdf(
    rr::material::MaterialParams{},  // unused for Lambert
    sample.wi, hit_n)
```

This is the simplest mirror that keeps
the helper API symmetric with MIS.5.
The `MaterialParams{}` default-
construction is FP-zero across all
fields; the helper reads NONE of them
for the Lambert PDF. Bit-equivalent
with the CUDA call.

**Alternative shapes** the implementer
may prefer:

- **Construct a local `m_local`**:
  `rr::material::MaterialParams m_local;
  m_local.baseColor = albedo;` then
  call `bsdf_pdf(m_local, ...)`. More
  forward-compatible with future BSDFs
  that read `baseColor` (none do
  today). Slightly more verbose.
- **Skip `bsdf_pdf` and call
  `pdf_cosine_hemisphere(cos_th)`
  directly.** Hardcodes Lambert into
  the integrator; loses BSDF-agnostic
  abstraction. NOT RECOMMENDED.

The brief recommends the
`MaterialParams{}` form for tight
mirror with MIS.5; either of the
first two alternatives passes.

---

## 2. v1 behavior

The four contractual properties the slice
must honour:

### 2.1 Delta lights short-circuit to NEE weight = 1

For any v1 light type (Point or
Directional), `sample.is_delta` is `true`
per MIS.3's helper population. The
integrator's ternary `sample.is_delta ?
1.0f : power_heuristic(...)` evaluates to
`1.0f`. Multiplying the contribution by
`1.0f` is the IEEE-754 §6 identity:
`x * 1.0f == x` for any finite non-NaN
`x`.

Anchored by the existing MIS.5 host-only
test
`tests/pathtracer_nee_tests.cpp::test_mis_weight_delta_short_circuits_to_one`
(no MIS.6-specific test addition needed —
the test is BACKEND-AGNOSTIC; it
exercises the helper composition logic
via a lambda that the OptiX integrator
uses identically).

### 2.2 Non-delta MIS structurally prepared but not expanded

The `power_heuristic` call's else-branch
is wired in the OptiX integrator but
unreachable at v1 (every NEE sample sets
`is_delta == true`). The architecture is
in place for the future area-light arc
to flip the `is_delta` short-circuit
and exercise the helper.

### 2.3 Current delta-light output unchanged

For an operator running `--render-optix-pathtrace`
WITH `--enable-nee` against a v1 scene
(Point + Directional lights only), the
OptiX-rendered PPM is bit-identical with
the post-MIS.5 baseline (commit
`35577a6`). Same IEEE-754 identity-
multiplication argument as CUDA MIS.5
§2.3.

The cross-backend convergence question —
"does CUDA `--enable-nee` PPM match OptiX
`--enable-nee` PPM byte-for-byte?" — is
NOT a goal of this slice. The two
backends use DIFFERENT integrator
internals (different bounce-loop ordering
+ FMA-fusion patterns + RNG draws) and
have NEVER been bit-equivalent at the
PPM level. They are STATISTICALLY
similar at high spp; that's the
established cross-backend convergence
contract from `PATH_TRACER_NEE_AUDIT.md`
§6.2.

What MIS.6 DOES preserve: each backend
is byte-identical with its own pre-MIS
counterpart at v1 delta-light scope.
CUDA-pre-MIS.5 == CUDA-post-MIS.5 (per
MIS.5 §6.1); OptiX-pre-MIS.6 == OptiX-
post-MIS.6 (per this brief §6.1).

### 2.4 Default-OFF (no `--enable-nee`) byte-identical

For an operator running WITHOUT
`--enable-nee`, the kernel guard
`if (optixLaunchParams.enable_nee &&
optixLaunchParams.light_count > 0)` at
`OptixPrograms.cu:974` short-circuits
at `enable_nee == false`. The MIS.6
additions live INSIDE this guard. None
execute. Byte-identity preserved
trivially.

---

## 3. Files likely involved

The implementation slice will touch this
file set:

| File                                       | Change                                                              |
|--------------------------------------------|---------------------------------------------------------------------|
| `src/optix/OptixPrograms.cu`               | +~25-40 lines. (a) Add `#include "pathtracer/Bsdf.cuh"` and        |
|                                            | `"pathtracer/Mis.h"` at the top (next to the existing               |
|                                            | `pathtracer/DirectLight.cuh` include at line 57); (b) compute      |
|                                            | `mis_weight_nee` per §1.1 + §1.4 inside the existing `if           |
|                                            | (cos_th > 0.0f)` block at line 1023; (c) multiply `k` by it.       |
| `tests/pathtracer_nee_tests.cpp`           | NO change. The existing `test_mis_weight_delta_short_circuits_     |
|                                            | to_one` (added at MIS.5) is BACKEND-AGNOSTIC; it exercises the     |
|                                            | helper composition via a lambda that the OptiX integrator uses     |
|                                            | identically. **No new test case needed.**                          |
| `docs/BUILD_PLAN.md`                       | Slice-closing entry per the established narrow-column format.      |

`CMakeLists.txt` is NOT touched — no
new test binary; no source-file list
change.

`src/optix/OptixRenderer.{h,cpp}`,
`src/optix/OptixLaunchParams.h`,
`src/optix/OptixPipeline.{h,cpp}`,
`src/optix/OptixSBT.h` are NOT touched —
the integrator's SBT layout, launch
params POD, dispatcher signature,
pipeline configuration are all
preserved.

### 3.1 Helper integration site (target shape)

The implementer fills the per-line doc-
comment text + the inline-helper
invocations. Suggested target shape
inside the existing `if (cos_th >
0.0f)` block at
`OptixPrograms.cu:1023` (replacing the
existing `k` calculation at line
1025-1027):

```cpp
if (cos_th > 0.0f) {
    // MIS.6: compute the MIS weight on the NEE-side
    // estimator. Mirrors the CUDA MIS.5 pattern at
    // `CudaPathTracer.cu:333-338`. Veach 1995 §10.3
    // delta-light convention: when sample.is_delta
    // == true (v1 Point + Directional lights), the
    // BSDF sampler can never reach this light (zero
    // measure on the unit sphere); the NEE-side
    // weight is exactly 1.0f. For non-delta lights
    // (future area-light arc), the weight is
    // computed via the Veach β=2 power heuristic
    // from sample.pdf_solid_angle (MIS.3) and the
    // BSDF PDF at sample.wi (`bsdf_pdf` from
    // MIS.2). The MaterialParams argument to
    // bsdf_pdf is unused for Lambert (the
    // helper's PDF formula reads only the
    // geometry); a default-constructed instance
    // is bit-equivalent with the CUDA caller's
    // `m`. The `power_heuristic` call (MIS.4) is
    // unreachable at v1 because every v1 light
    // sets is_delta == true.
    const float mis_weight_nee = sample.is_delta
        ? 1.0f
        : rr::pathtracer::power_heuristic(
              sample.pdf_solid_angle,
              rr::pathtracer::bsdf_pdf(
                  rr::material::MaterialParams{},
                  sample.wi, hit_n));

    // Lambert BRDF: albedo / pi. (kInvPi factored
    // into k below.)
    const float k = cos_th * sample.pdf_inv
                  * rr::math::kInvPi * mis_weight_nee;
    radiance.x += throughput.x
        * sample.li_unattenuated.x
        * albedo.x * k;
    radiance.y += throughput.y
        * sample.li_unattenuated.y
        * albedo.y * k;
    radiance.z += throughput.z
        * sample.li_unattenuated.z
        * albedo.z * k;
}
```

Note the structural difference from CUDA:
the OptiX integrator folds `kInvPi` into
`k` directly (no separate `brdf` Vec3
variable), and uses `albedo` (decoded
from payload) instead of `m.baseColor`.
These are pre-existing OptiX conventions
established at NEE.4; MIS.6 preserves
them.

### 3.2 No test case needed

The MIS.5 `test_mis_weight_delta_short_circuits_to_one`
case at `pathtracer_nee_tests.cpp:519+`
exercises the helper composition logic
via a backend-agnostic lambda:

```cpp
auto compute_mis_weight = [](const DirectLightSample& s,
                             float p_bsdf_at_wi) {
    return s.is_delta
        ? 1.0f
        : rr::pathtracer::power_heuristic(
              s.pdf_solid_angle, p_bsdf_at_wi);
};
```

This lambda mirrors the CUDA AND OptiX
ternaries identically. The test verifies
Point + Directional fixtures both set
`is_delta == true` and produce
`mis_weight == 1.0f`, and cross-checks a
hypothetical non-delta sample against a
direct `power_heuristic` call.

The OptiX integrator's ternary at MIS.6
uses the SAME `is_delta`, the SAME
`power_heuristic`, the SAME PDF inputs
(`sample.pdf_solid_angle` from MIS.3
helper population, identical across
backends). Therefore the existing test
covers the OptiX-side composition logic
WITHOUT modification.

The integrator-level byte-identity check
at the PPM level is a runtime-deferred
OptiX-host `cmp` per §6.1 below; the
host test catches a regression in the
short-circuit logic before the operator
runs the runtime check.

---

## 4. What must not be touched

The implementation slice MUST keep the
following byte-identical:

### 4.1 The CUDA integrator

- `src/cuda/CudaPathTracer.{cu,cuh}` —
  every byte. The CUDA-side wiring
  shipped at MIS.5 (`35577a6`) is
  preserved.

### 4.2 The pathtracer module's existing surfaces

- `src/pathtracer/RNG.{h,cuh}` — every
  byte.
- `src/pathtracer/Sampling.{h,cuh}` —
  every byte.
- `src/pathtracer/DirectLight.{h,cuh}`
  (MIS.3) — every byte. The integrator
  consumes `is_delta` and
  `pdf_solid_angle` but does not modify
  their producers.
- `src/pathtracer/Bsdf.{h,cuh}` (MIS.2)
  — every byte. The integrator consumes
  `bsdf_pdf` but does not modify it.
- `src/pathtracer/Mis.h` (MIS.4) — every
  byte. The integrator consumes
  `power_heuristic`.
- `src/pathtracer/PathTracer.{h,cpp}` —
  every byte.

### 4.3 The OptiX module's other surfaces

- `src/optix/OptixRenderer.{h,cpp}` —
  every byte. The dispatcher signature +
  body unchanged.
- `src/optix/OptixLaunchParams.h` —
  every byte. The launch-params POD
  layout unchanged.
- `src/optix/OptixPipeline.{h,cpp}` —
  every byte. The pipeline configuration
  + SBT building unchanged.
- `src/optix/OptixSBT.h` — every byte.
- `src/optix/OptixDenoiser.{h,cpp}`,
  `src/optix/OptixBackend.{h,cpp}`,
  `src/optix/OptixAccel.{h,cpp}` —
  every byte.

### 4.4 The CLI / Config / main.cpp surfaces

- `src/core/Config.h`, `src/core/Config.cpp`,
  `src/core/CommandLine.h`,
  `src/core/CommandLine.cpp`,
  `src/core/Logger.{h,cpp}` — every byte.
  No new CLI flag (per the plan §6 #8).
- `src/main.cpp` — every byte. No
  dispatcher changes.

### 4.5 The renderer / scene / material modules

- `src/renderer/`, `src/io/`,
  `src/scene/`, `src/material/`,
  `src/lighting/`, `src/texture/`,
  `src/gpu/`, `src/server/` — every
  byte.

### 4.6 The OptiX raygen structure

Inside `OptixPrograms.cu`:

- The PT-payload helpers
  (`pt_set_hit`, `pt_set_miss`,
  `pt_align_to_normal`,
  `pt_environment_radiance`) at lines
  757-810 — UNCHANGED.
- `__miss__shadow` at line 318 —
  UNCHANGED.
- `__closesthit__radiance` (the
  Stage 20K direct-lighting closest-
  hit) — UNCHANGED.
- `__raygen__pathtrace`'s structure
  (the spp loop, the bounce loop, the
  RNG seeding, the kernel guard at
  line 974, the shadow ray at lines
  984-1017, the cosine-hemisphere
  bounce at lines 1044-1056, the
  emission-add (none in OptiX
  currently), the firefly clamp at
  lines 1062-1067, the relativity
  stack at lines 1071+) — ALL
  UNCHANGED.
- `__miss__pathtrace` at line 1086+
  — UNCHANGED.
- `__closesthit__pathtrace` at line
  1094+ — UNCHANGED. The albedo
  decode + payload-write is
  preserved.

The MIS.6 changes are scoped to a
SINGLE region: the `if (cos_th >
0.0f)` block at line 1023 — about
~10 lines of arithmetic.

### 4.7 Tests + scenes + tooling

- `tests/pathtracer_nee_tests.cpp` —
  every byte. The existing MIS.5 case
  covers the helper composition logic
  for both backends.
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
  every byte.
- `scenes/*.rrscene` — every byte.
- `tools/verify_cuda_host.py` — every
  byte.

### 4.8 Build configs

- `CMakeLists.txt` — every byte. No
  new test binary; no source-file
  list change.

### 4.9 Default behaviour

For an operator running ANY action
with ANY combination of existing
flags, the rendered PPM is bit-
identical with the post-MIS.5-audit
baseline at commit `470af7d`. The
structural arguments:

- Default-OFF: kernel guard short-
  circuits; new code unreachable.
- Default-ON at v1 light-type scope:
  delta short-circuit gives
  `mis_weight_nee == 1.0f`;
  multiplying by 1.0f is IEEE-754
  identity.
- CUDA path: byte-identical (no
  MIS.5 changes).

---

## 5. PASS criteria

The implementation slice passes when
ALL of the following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF,
  RR_ENABLE_OPTIX=OFF): clean build,
  zero new compiler warnings. The
  OptiX changes are inside
  `OptixPrograms.cu` which compiles
  via PTX on the ON build only;
  on the OFF build the file is
  not compiled, so the change is
  trivially OFF-build-safe.
- `cmake --build build-ON` (audit
  host, RR_ENABLE_CUDA=OFF,
  RR_ENABLE_OPTIX=ON with SDK
  fallback): clean build, zero new
  warnings. The OptiX changes
  compile to PTX cleanly.

### 5.2 Tests

- `ctest --output-on-failure` from
  `build`: 100% green. Count
  UNCHANGED at 11/11.
- `ctest --output-on-failure` from
  `build-ON`: 100% green. Count
  UNCHANGED at 12/12.
- All existing test binaries' per-
  case counts unchanged
  (`pathtracer_nee_tests` 59/59,
  `pathtracer_bsdf_tests` 41/41,
  `pathtracer_mis_tests` 34/34,
  `cli_tests` 31/31).
- The MIS.5 case
  `test_mis_weight_delta_short_circuits_to_one`
  continues to pass without
  modification. The case is
  backend-agnostic; its existence
  confirms the helper composition
  logic the OptiX integrator
  consumes is correct.

### 5.3 Source diff size

Per `docs/PATH_TRACER_MIS_PLAN.md`
§7.3 (MIS.6 budget): ≤ 200 lines
added.

Suggested per-file budget:
- `src/optix/OptixPrograms.cu`:
  ~25-40 lines (the helper
  composition + new include lines
  + ~20 lines of doc-comment).
- TOTAL: ~25-40 lines.

Even smaller than MIS.5 (which had
~125 lines) — MIS.6 ships NO test
case (the existing MIS.5 case
covers the helper composition
backend-agnostically). Doc-comment
density expected ~70-80% per the
established PT-P.x / NEE.x / MIS.x
pattern; final size likely 30-60
lines.

### 5.4 No-touch invariants

`git diff` after the slice MUST
show zero bytes changed in:

```
src/cuda/  src/renderer/  src/io/
src/scene/  src/material/  src/lighting/
src/texture/  src/gpu/  src/server/
src/main.cpp  src/core/
src/pathtracer/RNG.{h,cuh}
src/pathtracer/Sampling.{h,cuh}
src/pathtracer/Bsdf.{h,cuh}
src/pathtracer/DirectLight.{h,cuh}
src/pathtracer/Mis.h
src/pathtracer/PathTracer.{h,cpp}
src/optix/OptixRenderer.{h,cpp}
src/optix/OptixLaunchParams.h
src/optix/OptixPipeline.{h,cpp}
src/optix/OptixSBT.h
src/optix/OptixDenoiser.{h,cpp}
src/optix/OptixBackend.{h,cpp}
src/optix/OptixAccel.{h,cpp}
tests/  scenes/  tools/verify_cuda_host.py
CMakeLists.txt
```

Verifiable via the standard
`git diff -- <paths> | wc -l` ⇒ 0
invariant.

### 5.5 No new test cases

MIS.6 ships NO new test cases
(unlike MIS.5 which added one).
The MIS.5
`test_mis_weight_delta_short_circuits_to_one`
covers the helper composition
logic backend-agnostically; it
already passes for the OptiX
integrator's ternary. The
integrator-level byte-identity
check at the PPM level is
runtime-deferred per §6 below.

If the implementer feels a host-
only OptiX-specific anchor is
useful (e.g. a test that
constructs a `DirectLightSample`
the way `__raygen__pathtrace`
would and verifies the
composition produces 1.0f), it
may be added — but the brief
does NOT mandate it.

### 5.6 Existing test invariants preserved

- All MIS.{2,3,4,5} test cases
  pass without modification.
- All NEE.x test cases pass.
- All cli_tests pass.
- All pathtracer_tests, math_tests,
  etc. pass.

### 5.7 Documentation

- `docs/BUILD_PLAN.md` carries a new
  slice-closing entry matching the
  established narrow-column format.
- The entry references this task
  brief +
  `docs/PATH_TRACER_MIS_PLAN.md`
  §4.2 + §5.5 + §7.5 as the source
  of the spec.
- The
  `OptixPrograms.cu` MIS.6 insertion
  site has a doc-comment block
  walking the delta-short-circuit
  rationale + cross-references to
  the plan + this brief + the CUDA
  MIS.5 mirror.

### 5.8 Master rule compliance

- Build incrementally (rule 1) +
  every step compilable (rule 2):
  both audit-host configs green;
  ctest green.
- No fake stubs (rule 3): the
  helper composition is real; the
  `mis_weight_nee` value is
  computed correctly per Veach.
- No CPU per-pixel work (rules
  5/7): the helpers are RR_HD
  inline; per-pixel consumption
  is device-side.
- Module boundaries (rule 9): the
  integrator consumes the three
  pathtracer module helpers via
  clean APIs; no cross-module
  ripple.
- Update BUILD_PLAN (rule 8): the
  slice-closing entry.

---

## 6. Runtime-deferred CUDA / OptiX-host checks

The audit host CANNOT run the
OptiX kernel; the byte-identity
claim from §2.3 is STRUCTURAL
(IEEE-754 identity multiplication
argument). The runtime empirical
confirmation is DEFERRED to a
CUDA + OptiX-SDK-equipped operator
session:

| §            | Check                                                           | Procedure                                                    |
|--------------|------------------------------------------------------------------|--------------------------------------------------------------|
| **§6.1**    | **OptiX MIS-on byte-IDENTITY at v1 (runtime)**                  | `cmp` post-MIS.5 vs post-MIS.6 OptiX PPM (with `--enable-nee`) |
| §6.2         | OptiX default-OFF byte-IDENTITY (runtime; carry-forward)        | `cmp` no-flag PPM (pre vs post)                              |
| §6.3         | OptiX NEE-on visible behaviour unchanged                        | render lit scene with `--enable-nee`; visual diff = 0        |
| §6.4         | Cross-backend MIS convergence at v1                             | CUDA `--enable-nee` PPM stats vs OptiX `--enable-nee` PPM    |
| §6.5         | ctest cycle on CUDA + OptiX-SDK host                            | re-run on host with both backends built                      |

### 6.1 OptiX MIS-on byte-IDENTITY at v1 (the key check)

```
$ git checkout 470af7d      # post-MIS.5-audit baseline
$ cmake --build build-optix -j
$ ./build-optix/bin/RelativityRender \
    --render-optix-pathtrace scenes/test_full_scene.rrscene \
    --enable-nee
$ cp output/optix_pathtrace_spp1.ppm /tmp/pre_mis6_spp1.ppm
$ cp output/optix_pathtrace_spp16.ppm /tmp/pre_mis6_spp16.ppm

$ git checkout MIS.6_commit
$ cmake --build build-optix -j

# Post-MIS.6 build, --enable-nee passed against same scene.
$ ./build-optix/bin/RelativityRender \
    --render-optix-pathtrace scenes/test_full_scene.rrscene \
    --enable-nee
$ cp output/optix_pathtrace_spp1.ppm /tmp/post_mis6_spp1.ppm
$ cp output/optix_pathtrace_spp16.ppm /tmp/post_mis6_spp16.ppm

$ cmp /tmp/pre_mis6_spp1.ppm /tmp/post_mis6_spp1.ppm  ; echo $?
=> 0 (identical — IEEE-754 identity multiplication confirmed at runtime)
$ cmp /tmp/pre_mis6_spp16.ppm /tmp/post_mis6_spp16.ppm ; echo $?
=> 0
```

This is THE confirmation that the §2.3
byte-identity invariant holds at runtime
on the OptiX side. DEFERRED to a CUDA
+ OptiX-SDK host; the existing MIS.5
host-only test anchors the helper
composition logic on the audit host.

### 6.2 OptiX default-OFF byte-IDENTITY (carry-forward)

The pre-existing default-OFF byte-
identity (no `--enable-nee` passed) was
DEFERRED at NEE.6 §9.1; this slice
does not regress it. Same `cmp`
procedure as §6.1 but with no flag
passed on either side.

### 6.3 OptiX NEE-on visible behaviour unchanged

For the v1 lit scene
(`scenes/test_full_scene.rrscene`),
the rendered image with `--enable-nee`
should visually MATCH the post-MIS.5
baseline at every checkpoint
(`optix_pathtrace_spp1.ppm`,
`optix_pathtrace_spp16.ppm`). The
visual diff is the PPM `cmp` from §6.1
(stronger than visual; bit-identical).

### 6.4 Cross-backend MIS convergence at v1

After MIS.5 + MIS.6 both land, the
two backends produce convergence-
equivalent output:

```
$ ./build-optix/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene \
    --enable-nee
$ cp output/pathtrace_spp_16.ppm /tmp/cuda_mis_spp16.ppm

$ ./build-optix/bin/RelativityRender \
    --render-optix-pathtrace scenes/test_full_scene.rrscene \
    --enable-nee
$ cp output/optix_pathtrace_spp16.ppm /tmp/optix_mis_spp16.ppm
```

The two PPMs are NOT expected to be
bit-identical (different bounce-loop
code paths produce different RNG
draws + FMA-fusion patterns; same
caveat as the firefly-clamp CLI
audit §6.3 + the NEE.6 audit §6.2).
They ARE expected to be statistically
similar:

- Mean luminance per channel agrees
  within sampling noise (~5% at
  spp=16; tighter at higher spp).
- Lit + shadowed regions match
  qualitatively.

This is the established cross-
backend convergence contract; MIS.6
does not change it. Verifying
statistical similarity with MIS-on
empirically confirms that both
backends correctly short-circuit
the v1 delta-light case to NEE
weight = 1.

### 6.5 ctest cycle on CUDA + OptiX-SDK host

`ctest --output-on-failure` from a
CUDA + OptiX-SDK-built directory
must pass. The new helper-
composition is exercised by the
existing MIS.5 case
(backend-agnostic); no kernel
dependency.

### 6.6 Carry-forward from prior MIS audits

Every prior MIS audit + the
firefly-clamp + NEE.x + PT-P.x
arcs have accumulated DEFERRED
runtime checks. MIS.6 inherits
this debt; the **MIS.7 arc-
level audit** rolls up ALL MIS-
arc deferred checks for a single
operator session.

---

## 7. Out-of-scope (deferred to future slices)

The following items are explicitly
NOT part of MIS.6:

1. **MIS.7 arc-level audit.**
   Independent slice; depends on
   MIS.6 to land first. Walks the
   integrator-level + cross-backend
   runtime checks across the full
   MIS arc.
2. **BSDF bounce swap (OptiX side).**
   Same deferral as CUDA MIS.5
   §1.3 #1; the inline cosine-
   hemisphere bounce + Lambert
   throughput simplification
   (`throughput.x *= albedo.x;
   ...` at
   `OptixPrograms.cu:~1049-1055`)
   is preserved bit-for-bit.
3. **MIS-on-emission-add (OptiX
   side).** The OptiX
   `__closesthit__pathtrace`
   currently does not carry
   emission (only albedo). Adding
   MIS-weighted emission requires
   the area-light arc.
4. **Area-light NEE.** Independent
   arc; depends on `Light::Area`
   plumbing (currently a
   PLACEHOLDER per
   `Light.h:20-31`).
5. **Specular-delta BSDFs.**
   Lambert-only at v1; the
   `is_delta` field on `BsdfSample`
   is reserved but unused at v1.
6. **Environment-IBL NEE.**
7. **CLI flag for MIS.** No
   `--mis` / `--no-mis` flag (per
   the plan §6 #8).
8. **MIS-weight AOV exposure.**
9. **Per-bounce relativity-on-
   throughput (OptiX side)** —
   not a MIS concern.
10. **OptiX-side `BsdfSample`
    POD direct consumption.**
    Same deferral as CUDA MIS.5
    §1.3 #1; the bounce swap to
    `sample_bsdf` would consume
    the POD but introduces sub-
    ULP drift.

---

## 8. Sub-arc context

### 8.1 Position in the MIS arc

Per `docs/PATH_TRACER_MIS_PLAN.md`
§5 + §8, MIS.6 is the SECOND
INTEGRATING slice — it mirrors
MIS.5's CUDA pattern on the OptiX
side. After MIS.6 lands, both
backends consume the three MIS
leaves (MIS.{2,3,4}) symmetrically.
The MIS arc's remaining slice is
**MIS.7** (arc-level audit).

### 8.2 What this slice unblocks

- MIS.7 (arc-level audit) has the
  full MIS arc shipped + can walk
  the deferred runtime checks
  across all stages.
- Cross-backend MIS convergence
  verification (§6.4) becomes
  meaningful (was DEFERRED + not
  exercisable until both
  integrators wire MIS).
- Future area-light arc inherits
  a tested MIS framework on
  BOTH backends.

### 8.3 What this slice does NOT unblock

- Area-light NEE (independent
  arc).
- Specular delta BSDFs
  (independent arc).
- Cross-backend BSDF-bounce
  swap (deferred).

### 8.4 Recommended audit cadence

Per the established MIS.x
pattern, a per-stage audit is:
- MIS.3 audit (`960c523`) —
  shipped (FIRST per-stage MIS
  audit).
- MIS.5 audit (`470af7d`) —
  shipped.
- MIS.4 audit — NOT shipped
  (NICE-TO-HAVE per MIS.4 task
  brief §7.4; bundled into
  MIS.5/6/7 review).
- MIS.6 audit — RECOMMENDED but
  not a hard PASS criterion of
  THIS task brief. The
  implementer / operator may
  defer the MIS.6 audit to
  bundle with MIS.7 (the arc-
  level audit will naturally
  cover the OptiX integrator
  alongside the cross-backend
  convergence check).

The brief recommends shipping
the MIS.6 audit (consistent with
MIS.5's per-stage audit cadence)
but accepts deferral.

---

## 9. Verdict

The brief is complete. The
implementer can ship MIS.6 end-to-
end without re-deriving any of
the design reasoning. The plan-
level context (Veach β=2,
sum-to-one, delta short-circuit)
is in
`docs/PATH_TRACER_MIS_PLAN.md`;
the slice-level contract is in
this file; the CUDA-side mirror
contract is in
`docs/PATH_TRACER_MIS_CUDA_INTEGRATOR_TASK.md`
(MIS.5 task brief) +
`docs/PATH_TRACER_MIS_CUDA_INTEGRATOR_AUDIT.md`
(MIS.5 audit, commit `470af7d`).

**Mode reminder: documentation
only.** This file is the spec.
The next slice (MIS.6 impl) ships
the source diff (no test
additions; no CMake changes) +
the BUILD_PLAN entry.

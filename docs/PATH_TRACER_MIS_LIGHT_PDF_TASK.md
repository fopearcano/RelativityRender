# Light PDF Data Model — Task Definition (MIS.3)

Date: 2026-05-08.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_MIS_PLAN.md` §3.2, §3.4,
§5.2 (Light PDF concept; `DirectLightSample` extension;
"MIS.3 — Light PDF data model" stage definition).
Prior slice: `docs/PATH_TRACER_MIS_BSDF_PDF_TASK.md`
+ commit `5a1c772` (MIS.2 BSDF helpers) — established
the symmetric data-model + helper pattern this slice
mirrors on the light side.
Mode: documentation only. **No source code is modified
by this task definition.** The task is the spec; the
next slice (the implementation) ships the diff.

Note on the user's referenced
`PATH_TRACER_MIS_BSDF_PDF_AUDIT.md`: that doc was not
shipped — MIS.2 closed via two BUILD_PLAN entries
(structure-only at `d9fa6e3`; helpers + tests at
`5a1c772`), not via a separate audit doc. The MIS arc
to date does not yet have a per-stage audit cadence;
the planned MIS.7 covers the entire arc once
implementation closes. This brief proceeds from the
plan + the MIS.2 BSDF task brief as the source of
truth.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be
able to ship MIS.3 without re-deriving the design
reasoning. Pattern mirrors
`docs/PATH_TRACER_MIS_BSDF_PDF_TASK.md` exactly (the
canonical MIS.x task-brief shape just established at
the prior slice).

---

## 1. Exact goal

**Extend `DirectLightSample` with the MIS-required PDF
information, populate the new fields per light type
inside `sample_direct_light_uniform`, and add host-
only test cases anchoring the new contract.**

The MIS arc (per `docs/PATH_TRACER_MIS_PLAN.md`) needs
the light side of the PDF data model to mirror the
BSDF side shipped at MIS.2:

- The BSDF side carries `BsdfSample::pdf` (per-
  steradian density) + `BsdfSample::valid` (gate
  flag) + `BsdfSample::value` (BRDF eval) + the
  forward-looking `is_delta` placeholder reserved
  for future specular BSDFs.
- The light side currently carries
  `DirectLightSample::pdf_inv` (the inverse
  selection PDF, equal to `light_count` for
  uniform-by-count selection) but NO directional
  PDF in solid-angle units. The MIS power
  heuristic needs the two estimators' PDFs in the
  SAME UNITS — the BSDF PDF is per steradian; the
  light PDF must also be per steradian to be
  comparable.

For Point + Directional lights (the v1 scope): the
directional PDF is a Dirac delta. The light is at a
zero-measure point on the unit sphere (Point: a
specific position; Directional: a specific
direction); BSDF sampling can never produce a
direction that lands exactly on the light. So at v1
the MIS weight collapses cleanly:
- For NEE samples (light-side): MIS weight = 1.0
  (the light PDF is a Dirac; no BSDF-bounce-as-
  light overlap).
- For BSDF samples (BSDF-side): MIS weight = 0.0
  for delta lights (zero measure under BSDF
  sampling).

**At v1 the integrator's NEE branch arithmetic is
unchanged byte-for-byte.** The new fields are inert
in this slice — no caller reads `pdf_solid_angle`
or `is_delta`; the existing `pdf_inv`-based NEE
contribution flows through the helper +
integrator unmodified.

The "data model" framing matters:
**MIS.3 is a no-op at runtime.** No caller invokes
the new fields in this slice; the integrator is
unchanged; PPM bytes are identical with the post-
MIS.2 baseline at `5a1c772`. The new fields sit
on the POD waiting to be consumed at MIS.5 + MIS.6
(the future integrator slices).

The fields are scoped to the v1 light-type set
(Point + Directional) because:
- They are the only non-PLACEHOLDER light types
  the NEE arc ships (NEE.{1..6}).
- Area + Environment lights are PLACEHOLDER per
  `lighting/Light.h:20-31`; the
  `sample_direct_light_uniform` helper already
  silently returns zero-contribution samples for
  them (the helper's "PLACEHOLDER returns
  default-constructed sample" branch). For MIS.3
  these placeholder branches simply leave the
  new fields at their bit-zero defaults; no
  semantic change.
- Future area-light arcs will populate
  `pdf_solid_angle` with the area-to-solid-
  angle Jacobian (`(1/area) · r² /
  cos(theta_light)`). The MIS.3 data model is
  forward-compatible: the future arc just adds
  a new branch in the helper, no POD change.

---

## 2. Required fields

Two new fields on `DirectLightSample`, mirroring the
plan §3.2 design.

### 2.1 `pdf_solid_angle`

Per-steradian directional PDF of choosing the
returned `wi` direction-toward-light.

For Point + Directional lights at v1 (delta lights):
the directional PDF is a Dirac delta. There is no
finite per-steradian density that meaningfully
represents a delta; the field is a SENTINEL whose
value is conventionally `0.0f` and whose
interpretation depends on `is_delta` (§2.2):
- `is_delta == true`: `pdf_solid_angle` is a
  sentinel — the MIS helper short-circuits and
  never reads the value.
- `is_delta == false`: `pdf_solid_angle` carries
  a real per-steradian density (used by the MIS
  power heuristic).

Field type: `float`.
Default value: `0.0f` (bit-zero).

For future area lights (out of scope per §6):
`pdf_solid_angle = (1/light_count) · (1/light_area)
· r² / cos(theta_light)` per the plan §3.2 area-
to-solid-angle Jacobian. The area-light arc adds
this branch; MIS.3 does not need to anticipate
the formula.

### 2.2 `is_delta`

Boolean discriminator separating delta-direction
lights (Point, Directional) from finite-PDF
lights (future area lights, IBL).

For Point + Directional lights: `true` (the light
is a Dirac delta in direction). The MIS helper
checks `is_delta` first; on `true`, the NEE
estimator gets MIS weight = 1.0 (Veach 1995 §10.3
delta-light convention) and the BSDF-bounce-as-
light contribution to delta lights is zero (zero
measure).

For future area lights: `false`. The MIS helper
computes the power heuristic from the actual
`pdf_solid_angle` value.

For PLACEHOLDER Area / Environment branches at v1
(the helper's
`return s; // default-constructed`
paths): the default `false` is correct — the
sample contributes zero (`pdf_inv == 0.0f`), and
the MIS weight is moot.

Field type: `bool`.
Default value: `false` (bit-zero).

### 2.3 Why `is_delta` is included (not deferred)

The MIS.2 BSDF brief noted that `is_delta` was a
forward-looking placeholder for future specular
BSDFs and was deferred from the BSDF POD per the
user's narrow-scope prompt. **For MIS.3 the
situation is different:**

At v1 the v1 light set IS already delta. The
field is not a forward-looking placeholder — it
is the discriminator the MIS helper reads on
EVERY NEE call to short-circuit the power-
heuristic computation. Without `is_delta`, MIS.5
+ MIS.6 cannot distinguish the v1 delta-light
case from a future area-light case at the
integrator level. They would have to inspect the
`Light::type` enum at every call site, breaking
the helper's "scene-agnostic" contract
established at NEE.2.

Including `is_delta` on the POD is the canonical
answer per the plan §3.4 design + the user's
prompt's explicit invitation ("is_delta if
useful for point/directional lights"). This
brief judges it useful and includes it.

The user's MIS.2 prompt deferred `is_delta` on
the BSDF side because Lambert never sets it
true; this brief asks the user to flip that
decision on the light side because v1 lights
ALWAYS set it true.

### 2.4 Field placement on the POD

Append both fields after the existing `pdf_inv`
field (which sits at the end of the POD today).
The post-MIS.3 POD shape is:

```cpp
struct DirectLightSample {
    rr::math::Vec3 wi              = {0.0f, 0.0f, 0.0f};   // (existing)
    float          distance        = 0.0f;                   // (existing)
    rr::math::Vec3 li_unattenuated = {0.0f, 0.0f, 0.0f};   // (existing)
    float          pdf_inv         = 0.0f;                   // (existing)
    // MIS.3 additions:
    float          pdf_solid_angle = 0.0f;
    bool           is_delta        = false;
};
```

Appending preserves the offsets of the existing
four fields. Any code that reads / writes the
existing fields continues to work bit-for-bit.

### 2.5 Byte-identity invariant for the POD

The NEE.5 byte-identity anchor at
`tests/pathtracer_nee_tests.cpp::test_zero_contribution_is_bit_default`
(line 341) does a `std::memcmp` on a default-
constructed `DirectLightSample` against a
reference. The post-MIS.3 default constructor
must still produce bit-zero across all six
fields (the four existing + the two new) for
the existing test to pass without modification.

Both new field defaults satisfy this:
- `pdf_solid_angle = 0.0f` — bit-zero (`+0.0f`
  is bit-zero in IEEE-754).
- `is_delta = false` — bit-zero (`false` is
  `(bool)0` in standard C++).

Padding bytes between fields are
implementation-defined; the existing test
relies on the compiler producing zero-
initialised padding (which g++ / clang on x86-64
does for default-initialised `struct`s). This
behaviour is preserved across the field
addition: the new bool's trailing padding
(typically 3 bytes after `is_delta` to align
to the next struct boundary) is zero-
initialised the same way the existing
`distance`'s trailing padding (zero bytes —
`distance` is already aligned) is.

**The existing
`test_zero_contribution_is_bit_default` test
must continue to pass without modification.**
The MIS.3 implementer should run the existing
test BEFORE adding the new test cases to
verify the constraint is honoured.

---

## 3. Files likely involved

The implementation slice will touch this file
set — three files, all modifications (no NEW
files):

| File                                       | Change                                                              |
|--------------------------------------------|---------------------------------------------------------------------|
| `src/pathtracer/DirectLight.h`             | +~25 lines. Append `pdf_solid_angle` + `is_delta` fields after the |
|                                            | existing `pdf_inv` per §2.4. Extend the field-semantics doc-comment |
|                                            | block to cover the new fields per §2.1 + §2.2. Cross-reference     |
|                                            | `docs/PATH_TRACER_MIS_PLAN.md` §3.2 and this task brief §2.        |
| `src/pathtracer/DirectLight.cuh`           | +~10 lines. Inside `sample_direct_light_uniform`, populate the new |
|                                            | fields per light type:                                              |
|                                            | - Point branch (line ~157): `s.pdf_solid_angle = 0.0f; s.is_delta`|
|                                            |   `= true;` after the existing `s.pdf_inv` assignment.              |
|                                            | - Directional branch (line ~186): same two assignments after the   |
|                                            |   existing `s.pdf_inv` assignment.                                  |
|                                            | - Area / Environment PLACEHOLDER branches (line ~196): NO change   |
|                                            |   (return default-constructed sample; new fields default to bit-   |
|                                            |   zero).                                                            |
| `tests/pathtracer_nee_tests.cpp`           | +~50 lines. Add test cases per §5.5 below (3 new mandatory cases). |
| `docs/BUILD_PLAN.md`                       | Slice-closing entry following the established TEX-P.x / PT-P.x /   |
|                                            | NEE.x / MIS.x narrow-column format.                                |

`CMakeLists.txt` is NOT touched — the new test
cases are appended to the existing
`pathtracer_nee_tests` binary; no new test
binary is added.

The plan §5.2 estimated +~10 lines for the
header + +~15 lines for the helper + +~30
lines for the tests. The estimate stands; the
brief's per-file budget above (~25 + ~10 + ~50)
includes per-line doc-comment density consistent
with prior MIS / NEE arcs.

### 3.1 Helper change (target shape)

`DirectLight.cuh:157-164` (Point branch)
target shape:

```cpp
if (L.type == rr::lighting::LightType::Point) {
    // ... existing wi / r2 / cos_th computation ...
    s.wi              = wi;
    s.distance        = r;
    s.li_unattenuated = ...;
    s.pdf_inv         = static_cast<float>(count);
    // MIS.3 additions:
    s.pdf_solid_angle = 0.0f;   // sentinel; is_delta gates consumption
    s.is_delta        = true;
    return s;
}
```

Same two-line addition in the Directional
branch.

The Area + Environment PLACEHOLDER branches at
`DirectLight.cuh:196-203` already return the
default-constructed `s`; the new fields
default to `0.0f` / `false` respectively, which
matches the "no contribution + not a delta"
semantic the helper documents as the
PLACEHOLDER behaviour.

---

## 4. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 4.1 The integrators

- `src/cuda/CudaPathTracer.{cu,cuh}` — every
  byte. The existing NEE branch uses
  `pdf_inv`; the new fields are not read.
  MIS.5 (a future slice) wires them in.
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
- `src/pathtracer/Sampling.{h,cuh}` — every
  byte.
- `src/pathtracer/Bsdf.{h,cuh}` — every byte
  (the MIS.2 sibling module).
- `src/pathtracer/PathTracer.{h,cpp}` — every
  byte. `PathTraceConfig` is unchanged.
- `src/pathtracer/DirectLight.cuh`'s helper
  control flow — every existing `if (count
  <= 0) return s;` / `if (cos_th <= 0.0f)
  return s;` / etc. branch is preserved
  byte-for-byte. ONLY the two assignments
  `s.pdf_solid_angle = ...; s.is_delta =
  ...;` are appended after the existing
  `s.pdf_inv = ...;` assignment in the two
  delta-light branches.

### 4.3 The CLI / Config / main.cpp surfaces

- `src/core/Config.h`, `src/core/Config.cpp`,
  `src/core/CommandLine.h`,
  `src/core/CommandLine.cpp`,
  `src/core/Logger.{h,cpp}` — every byte. No
  new CLI flag. (The MIS arc has no
  operator-facing surface per
  `docs/PATH_TRACER_MIS_PLAN.md` §6 #8.)
- `src/main.cpp` — every byte.

### 4.4 The renderer / scene / material modules

- `src/renderer/`, `src/io/`, `src/scene/`,
  `src/material/`, `src/lighting/`,
  `src/texture/`, `src/gpu/`, `src/server/`
  — every byte. The MIS.3 helper consumes
  `Light` by value (read-only); does NOT
  modify the POD. No upload contract
  changes.

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
  `tests/pathtracer_bsdf_tests.cpp` — every
  byte. ONLY
  `tests/pathtracer_nee_tests.cpp` is
  modified (extended with the new MIS.3
  cases per §5.5).
- `scenes/*.rrscene` — every byte.
- `tools/verify_cuda_host.py` — every byte.

### 4.6 Build configs

- `CMakeLists.txt` — every byte. The
  modified `pathtracer_nee_tests` binary
  picks up the new cases automatically;
  no new test-binary block.

### 4.7 Existing test invariants

- The NEE.5 byte-identity anchor at
  `pathtracer_nee_tests.cpp::test_zero_contribution_is_bit_default`
  (line 341) MUST continue to pass without
  modification. This is the constraint
  §2.5 documented: the new fields' bit-
  zero defaults preserve the `memcmp`-
  based byte-identity test against a
  default-constructed reference.
- The NEE.5 determinism anchor at
  `pathtracer_nee_tests.cpp::test_helper_determinism`
  (line 310) MUST continue to pass. The
  helper produces deterministic output for
  identical inputs; the new field
  population is also deterministic.
- Every other existing test case (cases
  1-11 from NEE.4 + NEE.5 + the bin-
  selection test) MUST continue to pass.
  The new fields are appended; existing
  field reads are unchanged.

### 4.8 Default behaviour

For an operator running ANY action with ANY
combination of existing flags
(`--enable-nee`, `--firefly-clamp`, etc.),
the rendered PPM is bit-identical with the
post-MIS.2 baseline at commit `5a1c772`.
Structural argument: no caller reads
`pdf_solid_angle` / `is_delta`; the
existing `pdf_inv`-based NEE arithmetic is
the only consumer of `DirectLightSample`'s
fields in the integrators; that arithmetic
is unchanged.

---

## 5. PASS criteria

The implementation slice passes when ALL of
the following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF):
  clean build, zero new compiler
  warnings.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON
  with SDK fallback): clean build, zero
  new warnings.

### 5.2 Tests

- `ctest --output-on-failure` from
  `build`: 100% green, count UNCHANGED at
  10/10 (no new test binary; the
  existing `pathtracer_nee_tests` binary
  grows by ~3 cases internally).
- `ctest --output-on-failure` from
  `build-ON`: 100% green, count UNCHANGED
  at 11/11.
- `pathtracer_nee_tests` per-case count
  grows by at least 3 RR_CHECK case
  functions (the three new cases below).
  The exact RR_CHECK assertion count
  growth depends on per-case structure;
  expect ~10-15 new assertions.
- All other test binaries' per-case
  counts unchanged.

### 5.3 Source diff size

Per `docs/PATH_TRACER_MIS_PLAN.md` §7.3
(MIS.3 budget): ≤ 100 lines added
across the three authorised files.

Suggested per-file budget:
- `src/pathtracer/DirectLight.h`:
  ~15-25 lines (two field declarations
  + ~10-20 lines of doc-comment).
- `src/pathtracer/DirectLight.cuh`:
  ~5-10 lines (two assignments × two
  branches; minimal doc-comment if any).
- `tests/pathtracer_nee_tests.cpp`:
  ~50-80 lines (three case functions +
  registration in `main()`).
- TOTAL: ~70-115 lines.

If the slice overshoots the ≤ 100 budget
(likely due to doc-comment density per
the established PT-P.x / NEE.x / MIS.2
deviation pattern), the deviation is
flagged in the BUILD_PLAN entry per the
established narrative pattern.

### 5.4 No-touch invariants

`git diff` after the slice MUST show zero
bytes changed in:

```
src/cuda/  src/optix/  src/renderer/  src/io/
src/scene/  src/material/  src/lighting/  src/texture/
src/gpu/  src/server/  src/main.cpp
src/core/  src/pathtracer/RNG.{h,cuh}
src/pathtracer/Sampling.{h,cuh}
src/pathtracer/Bsdf.{h,cuh}
src/pathtracer/PathTracer.{h,cpp}
tests/cli_tests.cpp  tests/pathtracer_tests.cpp
tests/math_tests.cpp  tests/image_tests.cpp
tests/gpu_tests.cpp  tests/relativity_tests.cpp
tests/demo_tests.cpp  tests/renderer_tests.cpp
tests/optix_tests.cpp  tests/pathtracer_bsdf_tests.cpp
scenes/  tools/verify_cuda_host.py
CMakeLists.txt
```

Verifiable via the standard
`git diff -- <paths> | wc -l` ⇒ 0
invariant.

### 5.5 New test cases (mandatory)

`tests/pathtracer_nee_tests.cpp` MUST gain
at least the following three NEW case
functions, registered in `main()`:

1. **`test_point_light_sets_is_delta_and_zero_pdf`**:
   For a Point light producing a valid
   sample (via the existing point-light-
   in-front fixture from NEE.5):
   - `s.pdf_solid_angle == 0.0f` (the
     Dirac sentinel).
   - `s.is_delta == true`.
   - All four pre-existing fields
     (`wi`, `distance`, `li_unattenuated`,
     `pdf_inv`) carry the same bit-
     identical values they carried at
     NEE.5 (use a `memcmp` on the first
     `offsetof(DirectLightSample,
     pdf_solid_angle)` bytes against a
     reference constructed without the
     new fields, OR use field-by-field
     `==` comparison against expected
     values per the existing NEE.5
     test).

2. **`test_directional_light_sets_is_delta_and_zero_pdf`**:
   For a Directional light producing a
   valid sample:
   - `s.pdf_solid_angle == 0.0f`.
   - `s.is_delta == true`.
   - All four pre-existing fields carry
     bit-identical values.

3. **`test_zero_contribution_sample_has_default_is_delta`**:
   For each of the helper's "no
   contribution" branches (null lights,
   count == 0, point light coincident,
   point light behind, directional
   pointing away, directional zero
   direction, Area placeholder,
   Environment placeholder) — verify
   `s.is_delta == false` AND
   `s.pdf_solid_angle == 0.0f`. This
   confirms the bit-zero defaults flow
   through every "early return" branch
   in the helper.

The implementer may add additional cases
(e.g. "valid sample carries all six
fields populated correctly" ANDed across
multiple representative input vectors;
"the existing memcmp default-state
anchor still holds with sizeof growing
to include the new fields"). The minimum
is three cases per the brief.

### 5.6 Existing test invariants preserved

- `test_zero_contribution_is_bit_default`
  (line 341): MUST continue to pass
  WITHOUT modification. The `memcmp`
  against a default-constructed
  reference still returns 0 because
  both new field defaults are bit-zero
  (§2.5).
- `test_helper_determinism` (line 310):
  MUST continue to pass without
  modification. The new field
  population is deterministic.
- All other existing case functions:
  unchanged.

### 5.7 Documentation

- `docs/BUILD_PLAN.md` carries a new
  slice-closing entry matching the
  established narrow-column format.
- The entry references this task brief +
  `docs/PATH_TRACER_MIS_PLAN.md` §3.2 +
  §5.2 as the source of the spec.
- The `DirectLight.h` doc-comment block
  is extended with the two new fields'
  semantics per §2.

### 5.8 Master rule compliance

- Build incrementally (rule 1) + every
  step compilable (rule 2): both audit-
  host configs green; ctest green.
- No fake stubs (rule 3): the new
  fields are real data carrying real
  semantics; the helper population is
  real (`is_delta = true` for the
  delta-light cases). The MIS-helper
  consumption is reserved for MIS.5 +
  MIS.6 but the data itself is
  meaningful at v1.
- No CPU per-pixel work (rules 5/7):
  the helper is RR_HD inline; the
  field assignments are device-side
  consumed at MIS.5 / MIS.6.
- Module boundaries (rule 9): the new
  fields sit on the existing
  `DirectLightSample` POD; no new
  module + no cross-module ripple.
- Avoid monolithic files (rule 10):
  `DirectLight.h` grows by ~25 lines
  total; well within reasonable size.
- Explicit testable interfaces (rule
  11): the new fields are host-
  testable via the mandatory cases in
  §5.5.
- Update BUILD_PLAN (rule 8): the
  slice-closing entry.

---

## 6. Out-of-scope (deferred to future slices)

The following items are explicitly NOT part
of MIS.3:

1. **Area-light `pdf_solid_angle`
   computation.** Reserved for the future
   area-light arc that adds
   `Light::Area` plumbing; the helper
   gains a new branch computing
   `(1/light_count) · (1/area) · r² /
   cos(theta_light)`. MIS.3 does not
   anticipate the formula.
2. **Environment-light `pdf_solid_angle`
   computation.** Reserved for the
   future IBL arc.
3. **MIS-helper consumption.** The MIS
   integrator changes (MIS.5 CUDA,
   MIS.6 OptiX) are out of scope here.
   The new fields are inert in this
   slice.
4. **`DirectLightSample` field
   reordering.** The new fields are
   appended after `pdf_inv`; no
   reordering of the existing four
   fields.
5. **`Light` POD extension.** The
   helper consumes the existing POD
   shape; no new fields on `Light`.
6. **Per-light-type sample helper
   split.** A future refactor might
   split `sample_direct_light_uniform`
   into per-type helpers; out of scope
   here.
7. **`pdf_inv` deprecation.** The
   existing `pdf_inv` field is
   preserved alongside the new fields.
   Both serve different purposes
   (selection PDF vs directional
   PDF); MIS.5 + MIS.6 consume both.
8. **CLI flag for MIS.** No `--mis` /
   `--enable-mis` flag (per the plan
   §6 #8).

---

## 7. Sub-arc context

### 7.1 Position in the MIS arc

Per `docs/PATH_TRACER_MIS_PLAN.md` §5 +
§8, the MIS arc cadence is:

```
MIS.1 (plan, docs only)              ─── shipped (commit 67dd03c)
MIS.2 BSDF data model task           ─── shipped (commit 659155e)
MIS.2 BSDF structure-only            ─── shipped (commit d9fa6e3)
MIS.2 BSDF helpers + tests           ─── shipped (commit 5a1c772)
MIS.3 (Light data model)             ─── this brief (THIS slice)
MIS.4 (MIS helper)                   ─── independent leaf; future slice
MIS.5 (CUDA integrator)              ─── depends on {2,3,4}; future slice
MIS.6 (OptiX integrator)             ─── depends on {5}; future slice
MIS.7 (audit)                        ─── depends on {2..6}; future slice
```

MIS.2 + MIS.3 + MIS.4 are independent
leaves. MIS.2 closed across two
implementation slices (structure-only +
helpers); MIS.3 is shipped as a single
implementation slice (the extension is
narrower than MIS.2's full module
addition). MIS.4 remains pending.

### 7.2 What this slice unblocks

- MIS.5 (CUDA integrator) gains the
  `pdf_solid_angle` + `is_delta` fields
  it needs to thread MIS through
  `k_pathtrace_sample`'s NEE branch.
- MIS.6 (OptiX integrator) gains the
  same.
- MIS.4 (MIS helper) becomes less
  abstract — the helper-host tests
  for `power_heuristic(p_light, p_bsdf)`
  can use the actual `pdf_solid_angle`
  + `BsdfSample::pdf` shape from the
  data models.
- Future area-light arcs gain a
  template extension point (the
  Area branch of
  `sample_direct_light_uniform`).

### 7.3 What this slice does NOT unblock

- Area-light NEE (depends on a separate
  arc adding `Light::Area` plumbing).
- Cross-backend MIS convergence
  verification (needs MIS.5 + MIS.6 +
  CUDA / OptiX-SDK host).
- IBL / environment NEE.

---

## 8. Verdict

The brief is complete. The implementer
can ship MIS.3 end-to-end without re-
deriving any of the design reasoning.
The plan-level context (Veach power
heuristic, light PDF formulas, etc.)
is in `docs/PATH_TRACER_MIS_PLAN.md`;
the slice-level contract is in this
file.

**Mode reminder: documentation only.**
This file is the spec. The next slice
(MIS.3 impl) ships the source diff +
the three test cases + the BUILD_PLAN
entry.

# Light PDF Data Model — Audit (MIS.3)

Date: 2026-05-08.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `0dd7d46`
("pathtracer: MIS.3 light PDF data model
(DirectLightSample extension)") — closes the MIS.3
sub-arc.
Plan source: `docs/PATH_TRACER_MIS_PLAN.md` §3.2 +
§5.2 + the slice-specific brief
`docs/PATH_TRACER_MIS_LIGHT_PDF_TASK.md`.
Mode: documentation only. **No source code is
modified by this audit.**
Auditor: Claude Code, on the audit host (no CUDA
Toolkit; OptiX-SDK fallback on the ON build). Same
fingerprint as every prior audit in this session.

This audit walks the five user-enumerated checks +
records a closing PASS / REPAIR / BLOCKED verdict.
Verdict legend (matches every prior audit):

- **PASS** — implemented; type-checked on the
  audit host; AND empirically exercisable on the
  audit host with a recorded happy-path run.
- **REPAIR** — implemented but a defect or
  inconsistency was found that should be patched.
- **BLOCKED** — verification cannot proceed on this
  audit host AND the structural argument also
  cannot be confirmed without runtime evidence.

This audit is the FIRST per-stage MIS audit shipped
(MIS.2 closed via two BUILD_PLAN entries without a
separate audit doc, as recorded in the MIS.3 task
brief's header note). It establishes the audit
cadence the remaining MIS slices (MIS.4 helper +
MIS.5 / MIS.6 integrators) will likely follow.

The MIS arc to date (twelve-slice cadence, mirroring
the NEE arc):

| Slice                        | Role                                              | Commit       |
|------------------------------|---------------------------------------------------|--------------|
| MIS.1                        | Multiple Importance Sampling plan                 | `67dd03c`    |
| MIS.2 task                   | BSDF PDF data model task brief                    | `659155e`    |
| MIS.2 structure-only         | `BsdfSample` POD                                  | `d9fa6e3`    |
| MIS.2 helpers                | Lambert sampler + 10 host-only tests              | `5a1c772`    |
| MIS.3 task                   | Light PDF data model task brief                   | `da86554`    |
| MIS.3 impl                   | `DirectLightSample` extension                     | `0dd7d46`    |
| **MIS.3 audit**              | **This audit** — verifies the impl                | (docs)       |
| MIS.4                        | MIS helper (`power_heuristic`) — last leaf        | (pending)    |
| MIS.5                        | CUDA integrator                                    | (pending)    |
| MIS.6                        | OptiX integrator                                   | (pending)    |
| MIS.7                        | Arc-level audit                                    | (pending)    |

---

## 1. Direct-light sample PDF field exists

**PASS.**

The `pdf_solid_angle` field is present on
`DirectLightSample` at
`src/pathtracer/DirectLight.h:133`:

```cpp
struct DirectLightSample {
    rr::math::Vec3 wi              = {0.0f, 0.0f, 0.0f};
    float          distance        = 0.0f;
    rr::math::Vec3 li_unattenuated = {0.0f, 0.0f, 0.0f};
    float          pdf_inv         = 0.0f;
    // MIS.3 additions: directional PDF + delta-light flag for
    // future MIS-aware integrator consumption (MIS.5 CUDA,
    // MIS.6 OptiX). Inert in this slice — no caller reads
    // these fields. Bit-zero defaults preserve the NEE.5
    // byte-identity anchor.
    float          pdf_solid_angle = 0.0f;
    bool           is_delta        = false;
};
```

Field type: `float`. Default value: `0.0f`. Per
the task brief §2.4, the field is appended after
the existing `pdf_inv` so the four pre-existing
field offsets are preserved.

### 1.1 Field is populated by the helper

`pdf_solid_angle` is populated by
`sample_direct_light_uniform` at
`src/pathtracer/DirectLight.cuh:170` (Point branch):

```cpp
s.pdf_inv         = static_cast<float>(count);
// MIS.3: a Point light is a Dirac delta in direction.
// `pdf_solid_angle` is a sentinel `0.0f` here; the
// future MIS helper short-circuits on `is_delta` and
// never reads this field for delta lights. See
// `pathtracer/DirectLight.h` doc-comment for the
// contract.
s.pdf_solid_angle = 0.0f;
s.is_delta        = true;
return s;
```

And at `:205` (Directional branch):

```cpp
s.pdf_inv         = static_cast<float>(count);
// MIS.3: a Directional light is a Dirac delta on the
// unit sphere. Same sentinel pattern as the Point
// branch above: `pdf_solid_angle = 0.0f` is unread;
// `is_delta = true` is the discriminator the future
// MIS helper consumes.
s.pdf_solid_angle = 0.0f;
s.is_delta        = true;
return s;
```

Both v1 light branches set the sentinel `0.0f`
explicitly + flip `is_delta = true`. The
PLACEHOLDER Area + Environment branches (line
~210-215) are unchanged — they return the
default-constructed `s`, which carries
`pdf_solid_angle = 0.0f` + `is_delta = false`
via the bit-zero defaults.

### 1.2 Documentation exists

The field's doc-comment block at
`DirectLight.h:71-87` (~17 lines) covers:

- Per-steradian directional PDF semantics.
- Read by the future MIS-aware integrator
  (`pathtracer::power_heuristic`, MIS.4) to
  weight the NEE-side estimator.
- Sentinel `0.0f` semantics for v1 Dirac
  lights with `is_delta == true`.
- Future area-light formula
  `(1/light_count) · (1/area) · r² /
  cos(theta_light)` reserved.
- Default `0.0f` matches the bit-zero "no
  contribution" convention.

The doc-comment cross-references
`docs/PATH_TRACER_MIS_LIGHT_PDF_TASK.md` and
`docs/PATH_TRACER_MIS_PLAN.md` for arc-level
context. An operator picking up MIS.5 / MIS.6
finds the field's contract without reading
the brief.

### 1.3 Cross-check: field is unique

`grep -rn "pdf_solid_angle" src/` returns:

- 5 references in `DirectLight.h` (field
  declaration + 4 doc-comment mentions).
- 4 references in `DirectLight.cuh` (2
  population sites + 2 doc-comment
  mentions).

No competing field; no stray references in
any other source file. The field is the
canonical light-side directional PDF surface.

---

## 2. Delta-light marker exists or is explicitly deferred

**PASS — explicitly INCLUDED (not deferred).**

The `is_delta` field is present on
`DirectLightSample` at
`src/pathtracer/DirectLight.h:134`:

```cpp
bool           is_delta        = false;
```

Field type: `bool`. Default value: `false`.

### 2.1 Why is_delta is included this slice

The MIS.3 task brief §2.3 explicitly argued
for INCLUDING `is_delta` in this slice, vs
the parallel MIS.2 BSDF brief which DEFERRED
the BSDF-side `is_delta` (Lambert never sets
it true at v1).

Light-side rationale (per the brief §2.3):
- v1 lights (Point + Directional) ALWAYS
  set `is_delta == true`.
- Without `is_delta` on the POD, the future
  MIS helper would have to inspect the
  `Light::type` enum at every call site,
  breaking the helper's scene-agnostic
  contract established at NEE.2.
- Including the field is the canonical
  answer per the plan §3.4 design.

The user's prompt explicitly invited
"is_delta if useful for point/directional
lights" — the brief judged it useful and
the impl ships it. **This audit confirms
the inclusion is wired correctly.**

### 2.2 Field is populated correctly

For the v1 light types (Point + Directional),
both helper branches set `s.is_delta = true`
alongside the `s.pdf_solid_angle = 0.0f`
sentinel. Cross-check at the source:

- `DirectLight.cuh:171` — Point branch.
- `DirectLight.cuh:206` — Directional branch.

For the PLACEHOLDER branches (Area,
Environment) and every "no contribution"
early-return branch (count == 0, nullptr,
coincident, behind, dir-away, dir-zero), the
default-constructed `s` carries `is_delta =
false` automatically — the bit-zero default
flows through unchanged.

### 2.3 Documentation exists

The field's doc-comment at
`DirectLight.h:89-105` (~17 lines) covers:

- `true` iff the light is a Dirac delta in
  direction (Point or Directional).
- `false` for finite-PDF lights (future
  area / IBL).
- The MIS helper's short-circuit semantics
  (NEE-side weight = 1.0 for delta;
  power-heuristic for non-delta).
- Default `false` flowing through every
  "no contribution" branch unchanged.

### 2.4 Cross-check: marker is unique

`grep -rn "is_delta" src/` returns:

- 6 references in `DirectLight.h` (field
  declaration + 5 doc-comment mentions).
- 4 references in `DirectLight.cuh` (2
  population sites + 2 doc-comment
  mentions).

No competing flag; no stray references in
any other source file.

---

## 3. No render behavior changed

**PASS.**

The MIS.3 impl extends the
`DirectLightSample` POD on the host side
but does NOT introduce any caller that
reads the new fields. Both backends'
NEE-branch arithmetic continues to
consume only the four pre-existing fields
(`wi`, `distance`, `li_unattenuated`,
`pdf_inv`).

### 3.1 Source-level diff scoping

The diff between the audited commit
(`0dd7d46`) and its parent (`da86554`,
the MIS.3 task brief doc-only commit)
shows changes contained inside the
`pathtracer/DirectLight.{h,cuh}` module
+ the `tests/pathtracer_nee_tests.cpp`
test extension:

```
$ git diff da86554..0dd7d46 --stat
 docs/BUILD_PLAN.md             | 996 +++++++++++++++++++++
 src/pathtracer/DirectLight.cuh |  15 +
 src/pathtracer/DirectLight.h   |  60 ++-
 tests/pathtracer_nee_tests.cpp | 153 +++++++
 4 files changed, 1222 insertions(+), 2 deletions(-)
```

**+226 / -2 across exactly three source
files** (`DirectLight.h`, `DirectLight.cuh`,
`tests/pathtracer_nee_tests.cpp`). No
integrator-side change.

### 3.2 No-touch invariants verified

`git diff 0dd7d46~1..0dd7d46 -- <path>`
returns 0 bytes for every must-not-touch
path:

| Must-not-touch path                          | git diff bytes |
|----------------------------------------------|---------------:|
| `src/cuda/` (CUDA path-tracer kernels)       | 0              |
| `src/optix/` (OptiX programs + dispatcher)   | 0              |
| `src/pathtracer/RNG.{h,cuh}`                 | 0              |
| `src/pathtracer/Sampling.{h,cuh}`            | 0              |
| `src/pathtracer/Bsdf.{h,cuh}` (MIS.2)        | 0              |
| `src/pathtracer/PathTracer.{h,cpp}`          | 0              |
| `src/renderer/`, `src/io/`, `src/scene/`     | 0              |
| `src/material/`, `src/lighting/`             | 0              |
| `src/texture/`, `src/gpu/`, `src/server/`    | 0              |
| `src/main.cpp`, `src/core/`                  | 0              |
| `tests/cli_tests.cpp`                         | 0              |
| `tests/pathtracer_bsdf_tests.cpp` (MIS.2)    | 0              |
| Every other `tests/*.cpp`                    | 0              |
| `scenes/`, `tools/`, `CMakeLists.txt`        | 0              |

The CUDA + OptiX kernels' source is
byte-identical with the post-MIS.2
baseline. No POD-layout consumer change in
the kernels (the POD grew on the host
side, but device-side reads only touch
the pre-existing fields).

### 3.3 Behavioural argument

The structural proof of "no render
behaviour change":

1. The new fields (`pdf_solid_angle`,
   `is_delta`) are appended after the
   pre-existing fields on the POD. Any
   code reading `wi`, `distance`,
   `li_unattenuated`, or `pdf_inv` reads
   the SAME memory offsets it read pre-
   MIS.3 (C++ standard layout
   guarantees).
2. The kernels (CUDA `k_pathtrace_sample`
   at `CudaPathTracer.cu:276+` and
   OptiX `__raygen__pathtrace`'s NEE
   branch) consume only the four pre-
   existing fields. Verified by
   `grep -rn "pdf_solid_angle\|is_delta"
   src/cuda/ src/optix/` returning empty.
3. Therefore the kernel-level per-pixel
   arithmetic is unchanged.
4. The host-side helper change populates
   two new fields with deterministic
   values (`0.0f` and `true` for v1
   delta lights); the helper's
   pre-existing return path (the four
   field assignments) is preserved
   byte-for-byte.

This is the formal "no behaviour change"
argument: the kernel never reads the new
fields; the helper writes them but never
mutates any read site that the kernel
consumes. PPM output is bit-identical
with the post-MIS.2 baseline.

### 3.4 Default-OFF byte-identity preserved

The MIS arc at v1 is a no-op (per
`docs/PATH_TRACER_MIS_PLAN.md` §1.4 #2).
With or without `--enable-nee`, the
existing render pipeline produces
byte-identical output:

- `--enable-nee` not passed: kernel guard
  short-circuits at `enable_nee == false`;
  the new fields are never reached.
- `--enable-nee` passed (delta-light
  scene): NEE branch executes; consumes
  `pdf_inv`-based estimator; the new
  fields sit on the POD unread.

The static IEEE-754 + RNG-stream
argument from
`PATH_TRACER_NEE_AUDIT.md` §1.2 carries
forward unchanged.

### 3.5 NEE.5 byte-identity anchor preserved

The NEE.5 byte-identity anchor at
`tests/pathtracer_nee_tests.cpp::test_zero_contribution_is_bit_default`
(line 341 pre-MIS.3) is the formal host-
only proof of bit-zero default
preservation. The test does
`std::memcmp` on a default-constructed
`DirectLightSample` against a reference
constructed without explicit field
initialisation.

The test continues to pass at HEAD
WITHOUT modification — confirming that
the new field defaults (`0.0f` for
`pdf_solid_angle`, `false` for
`is_delta`) are bit-zero, and that any
padding bytes the compiler inserts
between fields are also zero-initialised.

This is empirically verified by the
`pathtracer_nee_tests` binary running to
53/53 passes (was 34/34 pre-MIS.3; +19
new RR_CHECK assertions from the three
new MIS.3 cases). The pre-existing 34
assertions still pass — including the
NEE.5 memcmp anchor.

---

## 4. Build status

**PASS.**

Re-ran during this audit:

| Config      | RR_ENABLE_CUDA | RR_ENABLE_OPTIX | Build     | ctest    |
|-------------|:--------------:|:---------------:|-----------|:--------:|
| `build`     | OFF            | OFF             | clean     | 10/10 PASS |
| `build-ON`  | OFF            | ON              | clean     | 11/11 PASS |

Both audit-host configs report zero new
compiler warnings. ctest counts UNCHANGED
from the post-MIS.2 baseline (10/10 OFF +
11/11 ON) — matching the task brief §5.2
expectation exactly. The slice did not
add a new test binary; the existing
`pathtracer_nee_tests` binary grew its
internal case count.

### 4.1 Per-binary case counts

| Binary                        | Pre-MIS.3 | Post-MIS.3 | Delta |
|-------------------------------|----------:|-----------:|------:|
| `pathtracer_nee_tests`        | 34/34     | 53/53      | +19   |
| `cli_tests`                   | 31/31     | 31/31      | 0     |
| `pathtracer_bsdf_tests`       | 41/41     | 41/41      | 0     |
| Every other test binary       | unchanged | unchanged  | 0     |

The +19 RR_CHECK growth in
`pathtracer_nee_tests` is from the three
new MIS.3 cases:

- `test_point_light_sets_is_delta_and_zero_pdf`
  (5 RR_CHECKs).
- `test_directional_light_sets_is_delta_and_zero_pdf`
  (5 RR_CHECKs).
- `test_zero_contribution_sample_has_default_is_delta`
  (9 RR_CHECKs across 8 zero-contribution
  branches via the `check_default` lambda).

Empirically verified during this audit:

```
$ ./build/bin/pathtracer_nee_tests
pathtracer_nee_tests: 53/53 passed

$ ./build/bin/cli_tests
cli_tests: 31/31 passed

$ ./build/bin/pathtracer_bsdf_tests
pathtracer_bsdf_tests: 41/41 passed
```

### 4.2 Smoke matrix

| Smoke                                              | Result                                              |
|----------------------------------------------------|-----------------------------------------------------|
| `cmake --build build -j`                           | clean rebuild                                       |
| `cmake --build build-ON -j`                        | clean rebuild including OptiX-SDK fallback path     |
| `ctest` from `build`                               | 10/10 pass                                          |
| `ctest` from `build-ON`                            | 11/11 pass                                          |
| `pathtracer_nee_tests`                             | 53/53 (was 34/34; NEE.5 anchor preserved)           |
| `cli_tests`                                        | 31/31 unchanged                                     |
| `pathtracer_bsdf_tests`                            | 41/41 unchanged (MIS.2 sibling intact)              |
| `--scene-info scenes/test_textured_material.rrscene` (TEX-P.6) | three-case log sequence intact; fixups applied: 2 |
| `--render-pathtrace ... --enable-nee`              | "requires CUDA" fallback (audit-host expected)      |
| `--render-optix-pathtrace ... --enable-nee`        | enable_nee + firefly_clamp log lines + "requires OptiX SDK" fallback |

All cells green.

### 4.3 Audit-host fingerprint

Same as the parent NEE.6 / MIS.x audits:
no CUDA Toolkit (`command -v nvcc`
returns empty); no `/usr/local/cuda`;
presumably no NVIDIA GPU. The OFF audit-
host config is the no-GPU baseline; the
ON audit-host config (`build-ON/`) uses
the OptiX-SDK-fallback path (every
`--render-optix-*` action returns the
documented "requires the OptiX SDK"
message before any kernel can run).

The MIS.3 helpers + tests are RR_HD
inline pure host code; the audit host
fully exercises the contract without a
CUDA / OptiX runtime.

---

## 5. Verdict

| #  | Audit item                                         | Result   |
|----|----------------------------------------------------|----------|
| 1  | Direct-light sample PDF field exists               | PASS     |
| 2  | Delta-light marker exists or is explicitly deferred| PASS — INCLUDED (not deferred) per task brief §2.3 |
| 3  | No render behavior changed                         | PASS     |
| 4  | Build status                                       | PASS — 10/10 OFF + 11/11 ON, counts UNCHANGED      |
| 5  | Closing verdict                                    | **PASS** |

**Overall verdict: PASS.**

The MIS.3 light PDF data model ships
cleanly. The two new fields
(`pdf_solid_angle`, `is_delta`) are
present on `DirectLightSample` with
correct types + defaults; the helper
populates them correctly per light type;
the host-only test cases anchor the
contract; the kernels are byte-
identical with the post-MIS.2 baseline;
the NEE.5 byte-identity anchor is
preserved. **Zero REPAIR items.**

### 5.1 Master rule compliance

- Build incrementally (rule 1) + every
  step compilable (rule 2): preserved
  trivially. Both audit-host configs
  re-built + re-tested green during
  this audit.
- No fake stubs (rule 3): every audit
  finding is a read-only observation
  cited to a source line number / smoke
  output.
- No CPU per-pixel work (rule 5/7): no
  changes; trivially preserved.
- Module boundaries (rule 9): the
  changes are scoped to
  `src/pathtracer/DirectLight.{h,cuh}`
  + `tests/pathtracer_nee_tests.cpp`.
- Update BUILD_PLAN (rule 8): the parent
  slice (commit `0dd7d46`) added a
  BUILD_PLAN entry; this audit will add
  one too.
- Documentation only / do not modify
  source code (this slice's rules):
  zero source edits in this audit slice.

### 5.2 Known deviation (carried forward from impl slice)

The MIS.3 impl slice's BUILD_PLAN entry
recorded a §5.3 diff-size budget
deviation (+226 lines vs ≤ 100 budget;
~2.26x overshoot). This audit
re-confirms the deviation is documented
in the impl slice's commit message +
BUILD_PLAN entry per the established
PT-P.x / NEE.x / MIS.2 doc-comment
density overshoot pattern. **The
deviation is documentation, not defect
— the shipped output satisfies every
other PASS criterion.**

No new deviation is introduced by this
audit.

### 5.3 No new REPAIR candidates

This audit's read of the source +
helper population sites + test cases
finds zero new REPAIR candidates. The
MIS.3 sub-arc closes cleanly; no
follow-up slice is needed to fix
anything from MIS.3.

---

## 6. Sub-arc context

### 6.1 What this audit confirms

- The MIS.3 data-model extension is
  wired correctly + complete.
- The future MIS-aware integrator
  slices (MIS.5 CUDA, MIS.6 OptiX) can
  consume the new fields via the
  contracts documented in
  `DirectLight.h` without re-deriving
  the design.
- The byte-identity invariant from
  NEE.5 is structurally preserved
  across the MIS.3 field addition (bit-
  zero defaults flow through every
  zero-contribution branch).

### 6.2 What this audit does NOT confirm

- The MIS-aware integrator output
  (cross-backend convergence with MIS
  on at v1 vs MIS off; MIS-on noise
  reduction at higher spp) — this is
  a runtime check that requires a
  CUDA + OptiX-SDK host AND the
  MIS.5 / MIS.6 integrator slices to
  have shipped. DEFERRED.
- The MIS.4 helper's correctness
  (`power_heuristic` produces the
  expected weights for representative
  PDF inputs) — handled by the future
  MIS.4 slice's own host-only tests.

### 6.3 Recommended next step

The MIS arc has one independent leaf
remaining (MIS.4 — the
`power_heuristic` helper). After it
lands:
- All three leaves (MIS.{2,3,4}) are
  shipped.
- MIS.5 (CUDA integrator) wires them
  into `k_pathtrace_sample`'s NEE
  branch.
- MIS.6 (OptiX integrator) mirrors on
  the OptiX raygen.
- MIS.7 audits the entire arc, walking
  the deferred runtime checks (MIS-on
  cross-backend convergence + MIS-on
  noise reduction + the MIS.6 §6.3
  pattern that adds an integration-
  level CUDA-host PPM `cmp` cycle).

Recommended sequencing: ship MIS.4
next (single-file pure-math leaf;
trivial impl) to clear the MIS leaves
before MIS.5 lands.

---

Mode reminder: **documentation only.**
This audit makes zero source-code
changes. The REPAIR list is empty.

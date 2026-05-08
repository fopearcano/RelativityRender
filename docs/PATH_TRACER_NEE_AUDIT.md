# Path Tracer Next Event Estimation — Audit (NEE.3)

Date: 2026-05-07.
Branch: `relativity-core-v1`.
Commit under audit: `6f49c55` (NEE.2 — CUDA NEE skeleton).
Audit-host fingerprint: no NVIDIA GPU; no CUDA Toolkit;
`build/` configured `RR_ENABLE_CUDA=OFF
RR_ENABLE_OPTIX=OFF`; `build-ON/` configured
`RR_ENABLE_CUDA=OFF RR_ENABLE_OPTIX=ON` with the OptiX SDK
fallback (per
`docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md`).
Mode: documentation only. **No source code is modified by
this audit.** Audit findings are read-only observations of
the NEE.2 commit's artefacts; any REPAIR items would be
recorded here for a follow-up implementation slice, not
fixed in this audit.

This file walks each PASS criterion from
`docs/PATH_TRACER_NEE_TASK.md` §5, marking each
**PASS** / **REPAIR** / **DEFERRED** / **DEVIATION** with
evidence. Runtime checks per §6 are recorded as DEFERRED
runtime-host concerns per the established
PT-P.{15,18,21,24,25} +
`FIREFLY_CLAMP_CLI_AUDIT.md` precedent.

---

## 0. Slice scope reminder

NEE.2 deliberately ships ONLY the CUDA half of the NEE
arc — helper module + shadow-ray any-hit traversal +
guarded kernel integration point + config field. The
brief's §5.5 atomicity criterion ("both backends same
commit") was authored against the original NEE.1
proposal, where the author's intent was a single atomic
landing of CUDA + OptiX + tests + CLI. The user
subsequently sub-divided that scope into four smaller
slices (skeleton → audit → OptiX mirror → CLI / tests),
and NEE.2 covers only the first. This audit interprets
each PASS criterion against the NEE.2 actual scope; the
§5.5 atomicity item is recorded as a **deliberate
DEVIATION** with the byte-identity invariant
substituting for the divergence safeguard (no caller
flips the flag, so both backends produce identical
output today; the divergence concern only materialises
when a future caller turns it on, at which point the
OptiX-side mirror slice must already have landed).

---

## 1. PASS criteria walk

### 1.1 §5.1 Build green on both audit-host configs — **PASS**

- **OFF / OFF.** `cmake --build build -j` rebuilt cleanly
  during this audit; `ctest` reports **7/7 green**:

      math_tests, image_tests, gpu_tests, pathtracer_tests,
      relativity_tests, demo_tests, renderer_tests

- **OFF / ON.** `cmake --build build-ON -j` rebuilt
  cleanly during this audit; `ctest` reports
  **8/8 green** (adds `optix_tests`).

- **CUDA-configure on a CUDA host (`RR_ENABLE_CUDA=ON`).**
  DEFERRED on the audit host — `find_package(CUDAToolkit)`
  fails on a host without the CUDA Toolkit. The §6 runtime
  checks (below) require this configure to land on a
  CUDA-equipped host before they can run; the BUILD_PLAN
  entry for any subsequent slice that exercises this path
  must document the reconfigure (per the firefly-clamp
  CLI impl precedent).

### 1.2 §5.2 Default-off byte-identity proof — **PASS** (static); **DEFERRED** (runtime)

- **Static IEEE-754 argument.** The new
  `PathTraceConfig::enable_nee` field defaults to
  `false`. The kernel guard at
  `src/cuda/CudaPathTracer.cu:213` reads:

      if (enable_nee && scene.light_count > 0) { ... }

  Both operands are zero / false at default
  (`enable_nee = false`; `scene.light_count` is the count
  of `Light*` entries `GpuScene::upload_lights` placed in
  the device buffer — typically zero for fixtures
  without an explicit `lights:` array). The branch is
  therefore *never executed*, and the kernel-side
  arithmetic — `radiance` accumulator, `throughput`
  multiply chain, RNG state advancement, firefly-clamp
  guard — proceeds bit-for-bit identical to the pre-
  NEE.2 build. No FP add ever reaches `radiance` from
  the NEE branch at default. This is the same IEEE-754
  zero-add-exactness argument PT-P.21 and PT-P.24
  established for the firefly-clamp default-off path.

- **Static RNG-stream identity.** The single
  `next_float(rng)` draw for the light-index selection
  lives **inside** the `enable_nee` guard at
  `CudaPathTracer.cu:214`. At default the draw is never
  performed, so the cosine-bounce sampler at
  `CudaPathTracer.cu:294-296` (`next_vec2(rng)`) pulls
  from a bit-identical RNG state. This satisfies the
  brief §4.3 requirement explicitly:

  > The kernel must place the NEE `rng.next_uint()`
  > calls *inside* the `enable_nee` guard (not outside)
  > so the default-off RNG sequence is preserved
  > exactly.

  Verified by reading the kernel source.

- **Dynamic in-build identity test.** DEFERRED. The
  brief proposed
  `tests/pathtracer_tests.cpp::test_nee_default_off_byte_identity`
  (§5.2 dynamic). NEE.2 ships no tests — the slice's
  current task explicitly scoped to "NEE helper /
  device function shell + direct-light sample data
  structure if needed + compile-time integration
  points", which does not include test expansion. A
  future slice can add the dynamic identity test
  alongside the CLI flag (so the test has a way to
  exercise the `enable_nee = true` path); until then,
  the static argument above is the byte-identity
  guarantee.

- **Runtime bit-identity verification.** DEFERRED to a
  CUDA-equipped host (§6.3 below).

### 1.3 §5.3 CLI flag end-to-end — **DEFERRED**

The CLI flag (`--enable-nee`) is reserved for a
follow-up slice in the new sub-arc cadence (skeleton →
audit → OptiX mirror → CLI). NEE.2's task scope
deliberately excludes the CLI; the brief's §5.3
acceptance gates (parse, validate, --help listing,
silent ignore on non-pathtrace actions) cannot apply
to a slice that has no CLI surface. Recorded as
DEFERRED, not REPAIR — the deferral is intentional
and consistent with the user-driven sub-arc
sub-division in §0.

### 1.4 §5.4 Light-array consumption invariants — **PASS**

- **Bounded read.** The helper at
  `src/pathtracer/DirectLight.cuh:124-129` clamps
  `li` to `[0, count)` defensively before the array
  read at `:131`, and returns the default zero-
  contribution sample when `lights == nullptr` or
  `count <= 0` at `:118-120`. No out-of-bounds read,
  no past-the-end stride. The selection PDF
  multiplier (`pdf_inv = static_cast<float>(count)`
  at `:159`, `:184`) is also gated on the picked
  light not being a PLACEHOLDER type or degenerate;
  invalid samples carry `pdf_inv = 0.0f`, which the
  kernel naturally treats as a zero-contribution
  sample.

- **Placeholder light handling.** `LightType::Area`
  and `LightType::Environment` (per `Light.h:20-31`'s
  PLACEHOLDER comment) silently return the default
  zero-contribution sample at `DirectLight.cuh:188-198`.
  This matches the brief §5.4 alternative ("zero-
  contribution-when-picked"), explicitly chosen over
  the rejected alternatives (filter-at-upload-time
  would touch `GpuScene::upload_lights`, which is on
  the must-not-touch list per §4.5).

- **Same upload contract.** No new fields, strides,
  or alignment were added to `Light`,
  `OptixLaunchParams`, or `CudaSceneView::lights`.
  Verified via `git diff --stat 6f49c55~1..6f49c55
  -- src/lighting/ src/gpu/GpuScene.cpp
  src/gpu/GpuScene.h src/cuda/CudaScene.cuh`:
  **0 bytes** (all four paths are on the
  must-not-touch list).

### 1.5 §5.5 Atomicity (both backends in same commit) — **DEVIATION (deliberate)**

The brief's §5.5 atomicity criterion was authored
against the original NEE.1 proposal of a single
atomic CUDA+OptiX landing. The user subsequently
sub-divided NEE into four slices (skeleton → audit
→ OptiX mirror → CLI / tests). NEE.2 ships only the
CUDA skeleton.

The atomicity criterion exists to prevent silent
cross-backend divergence (the §4.7 concern). NEE.2
does **not** introduce divergence today because:

- No caller flips `PathTraceConfig::enable_nee` to
  `true` in this slice or any prior commit (verified
  by `grep -rn "enable_nee" src/` — the only
  reference is the field definition, the field's
  doc-comment, the kernel guard, and the dispatcher
  passthrough; no `cfg.enable_nee = true`
  assignment anywhere).
- The OptiX dispatcher
  (`OptixRenderer::render_pathtrace`) takes
  individual arguments rather than a
  `PathTraceConfig` reference, so adding
  `enable_nee` to `PathTraceConfig` does not reach
  the OptiX signature; the OptiX backend is
  signature-equivalent to the pre-NEE.2 version.

The divergence risk §4.7 worries about therefore
materialises only at the moment a future caller
flips the flag. The new sub-arc cadence ensures
that future caller (the CLI flag slice or any other)
lands AFTER the OptiX-side mirror slice; until
then, the NEE.2 commit produces *identical* CUDA +
OptiX output on every fixture, just like the pre-
NEE.2 commit. The atomicity invariant is upheld
"in spirit" even though the two backends do not
share a single commit.

This is recorded as a **deliberate deviation**
documented in:

- The `PathTraceConfig::enable_nee` field's doc-
  comment block (`PathTracer.h:104-156`) — the
  doc-comment explicitly warns: "the OptiX path-
  trace raygen has no NEE wiring yet [...] mixing
  `enable_nee = true` with the OptiX backend
  silently produces an emission-only render from
  OptiX while the CUDA backend produces a NEE
  render; the two backends DO NOT converge to the
  same image until the OptiX-side NEE slice lands".
- The NEE.2 BUILD_PLAN entry's "What does NOT
  change" → "The OptiX backend" subsection.

The follow-up slice that flips a caller (the CLI
slice) **must** land AFTER the OptiX-side mirror
slice. Recorded as a sequencing constraint in §3
below.

### 1.6 §5.6 Diff-size budget — **DEVIATION (within precedent)**

Brief target: ~120 lines of source diff across all
files. Actual diff at `6f49c55~1..6f49c55`:

      src/cuda/CudaPathTracer.cu    | 113 +++++++++++--
      src/cuda/CudaPathTracer.cuh   |  23 ++-
      src/pathtracer/DirectLight.cuh| 207 +++++++++++++++++++++++
      src/pathtracer/DirectLight.h  |  81 +++++++++++
      src/pathtracer/PathTracer.cpp |   3 +-
      src/pathtracer/PathTracer.h   |  57 ++++
      6 files changed, 479 insertions(+), 5 deletions(-)

- **Pure logic.** The non-doc-comment diff is
  approximately:
    - `DirectLight.h`: `DirectLightSample` POD = 7
      lines of struct.
    - `DirectLight.cuh`: helper body (Point branch
      ~25 lines + Directional branch ~25 lines +
      defence-in-depth ~15 lines + namespace
      constants 2 lines) ≈ 70 lines.
    - `CudaPathTracer.cu`: shadow-ray helper ~22
      lines + guarded NEE branch ~30 lines +
      kernel signature ~1 line + launch site ~1
      line ≈ 55 lines.
    - `CudaPathTracer.cuh`: signature change ~1
      line.
    - `PathTracer.h`: field declaration ~1 line.
    - `PathTracer.cpp`: single argument
      pass-through ~1 line.
  - Total pure-logic diff ≈ **~155 lines**, ~30%
    above the brief's ~120-line target.
- **Doc-comment text.** ~324 lines of doc-comment
  blocks across the six files. This is the
  established PT-P.x precedent on inline
  documentation density (PT-P.6, PT-P.9, PT-P.15,
  PT-P.18, PT-P.24, firefly-clamp CLI all overshot
  their briefs' line targets by similar ratios on
  doc-comments).

Recorded as a **deviation within precedent**, not
REPAIR. The deviation flag was added to the NEE.2
BUILD_PLAN entry's "Diff size" subsection per the
established PT-P.x deviation-note pattern.

### 1.7 §5.7 Test expansion — **DEFERRED**

The brief proposed three tests:

- `tests/pathtracer_tests.cpp::test_nee_default_off_byte_identity`
- `tests/pathtracer_nee_tests.cpp` (host-side
  helper-stub linkage)
- `tests/cli_tests.cpp::test_cli_enable_nee_flag_sets_config_bool`

NEE.2 ships **none** of these. The slice's current
task explicitly scoped to "NEE helper / device
function shell + direct-light sample data structure
+ compile-time integration points", and excluded
test expansion. The reasons each test is deferrable:

- The byte-identity test would need an existing
  fixture with `light_count > 0` and a way to flip
  `enable_nee` from a host-only test (no CLI surface
  yet); both prerequisites are easier after the
  CLI slice lands.
- The host-side helper-stub test is the most
  tractable today — `sample_direct_light_uniform`
  is `RR_HD inline` and host-callable. A minimal
  test of count-zero / point-light-forward / point-
  light-behind / directional-default-distance could
  land in this slice without additional
  infrastructure.
- The CLI test requires the CLI flag to exist.

Recorded as DEFERRED. **Recommended next-slice
action**: include at least the host-side helper
test alongside the OptiX mirror slice (so NEE.4 has
test coverage for the CUDA helper before the OptiX
side lands; cross-backend invariants then have a
host-side anchor).

### 1.8 §5.8 No-touch invariants verified — **PASS**

`git diff --stat 6f49c55~1..6f49c55 -- <path>`
returns **0 bytes** for every must-not-touch path:

| Must-not-touch path (brief §)             | git diff bytes |
| ----------------------------------------- | -------------- |
| `src/optix/` (§4.1, §4.7)                 | 0              |
| `src/lighting/` (§4.5)                    | 0              |
| `src/gpu/GpuScene.cpp`, `.h` (§4.5)       | 0              |
| `src/pathtracer/RNG.h`, `.cuh` (§4.3)     | 0              |
| `src/pathtracer/Sampling.h`, `.cuh`       | 0              |
| `src/cuda/CudaIntersection.cuh`           | 0              |
| `src/cuda/CudaScene.cuh` (light fields)   | 0              |
| `src/cuda/CudaRenderer.{cu,cuh,cpp}`      | 0              |
| `tests/`                                  | 0              |
| `scenes/`                                 | 0              |
| `tools/verify_cuda_host.py`               | 0              |
| `CMakeLists.txt`                          | 0              |

The PT-P.15 emission handling (§4.2) and the
PT-P.24 firefly-clamp wiring (§4.4) are *adjacent*
to the new code (the NEE branch is inserted
between the emission add and the bounce-budget
check; the firefly-clamp guard remains the kernel's
last operation before the per-pixel write). Neither
was modified — the NEE branch is purely additive,
inserted at lines 213-261 of `CudaPathTracer.cu`,
and the surrounding emission / firefly-clamp
arithmetic at lines 201-212 + 251-255 (post-NEE.2:
lines 281-285) is character-identical with the
pre-NEE.2 source.

The TEX-P.6 fixture regression (`--scene-info
scenes/test_textured_material.rrscene`) was
re-verified during this audit and emits the
expected three-message sequence (Case 1 INFO + two
Case 3 WARNs) with `fixups applied: 2`. No drift.

---

## 2. Runtime-deferred §6 checks

All five §6 runtime checks DEFERRED to a CUDA-equipped
host per the established
`docs/CUDA_HOST_VERIFICATION_PLAN.md` fallback pattern.
Each is recorded with its operator-instructions stub for
when a CUDA host becomes available; the audit-host
fingerprint section above documents why no check is
runnable here.

| §6 check                              | Status   | When unblocked                    |
| ------------------------------------- | -------- | --------------------------------- |
| §6.1 visible noise reduction at low spp | DEFERRED | needs `--enable-nee` CLI + CUDA host |
| §6.2 cross-backend convergence         | DEFERRED | needs OptiX-side mirror + CUDA + OptiX hosts |
| §6.3 default-off bit-identity (runtime) | DEFERRED | needs CUDA host; trivially provable from §1.2 |
| §6.4 no-light scene smoke              | DEFERRED | needs `--enable-nee` CLI + CUDA host |
| §6.5 directional-light smoke           | DEFERRED | needs `--enable-nee` CLI + CUDA host |

**Recommended sequencing.** §6.3 is the cheapest
first runtime check (it does not need the CLI flag —
it just renders the same fixture with the pre-NEE.2
binary and the post-NEE.2 binary at default and asserts
bit-identical output). §6.4 + §6.5 + §6.1 each require
the CLI flag. §6.2 requires both the CLI flag AND the
OptiX-side mirror slice.

---

## 3. Sub-arc status & next steps

### 3.1 Sub-arc status

NEE.2 lands the CUDA skeleton. NEE.3 (this audit)
closes the skeleton with **PASS** verdict, modulo the
two deliberate DEVIATIONs (§5.5 atomicity, §5.6 size)
and the four DEFERRALs (§5.3 CLI, §5.7 tests, runtime
§6.x). No REPAIR items.

### 3.2 Sequencing constraints for follow-up slices

- **The OptiX-side mirror slice MUST land before any
  caller flips `PathTraceConfig::enable_nee = true`.**
  This is the §5.5 atomicity-equivalent invariant: as
  long as the flag is never `true`, both backends
  produce identical output; flipping it before the
  OptiX side has the matching kernel branch produces
  silent cross-backend divergence (the brief §4.7
  failure mode the original atomicity criterion was
  designed to prevent).
- **The CLI flag slice (`--enable-nee`) is the most
  natural caller-flip lever**, so by the constraint
  above the CLI slice should land AFTER the OptiX
  mirror.
- **The host-side helper test (§5.7) is the most
  tractable test to ship today**, because
  `sample_direct_light_uniform` is `RR_HD inline` and
  host-callable. Recommended to include it in the
  OptiX mirror slice (so the new OptiX code can lean
  on a tested CUDA helper baseline).

### 3.3 Recommended next slice

The user has three clean options for the next slice in
the NEE arc, in increasing scope:

1. **NEE.4: OptiX-side mirror.** Add the
   `sample_direct_light_uniform` call site +
   any-hit shadow-ray reuse (the existing
   `__miss__shadow` / `__closesthit__shadow` SBT
   programs at `OptixPrograms.cu:436` + `:489` from
   the `shading_mode == 2` branch are reusable as-is)
   + `OptixLaunchParams::enable_nee` field +
   dispatcher signature update. Mirrors the NEE.2
   shape on the OptiX side. Includes the host-side
   helper test from §5.7. Closes the §5.5 atomicity-
   equivalent invariant.

2. **NEE.5: CLI flag + tests.** Add `--enable-nee`
   modifier flag mirroring the `--firefly-clamp` CLI
   pattern, plus the byte-identity dynamic test +
   helper-host test + CLI parser test. Requires
   NEE.4 to have already landed (per §3.2 sequencing).

3. **Pivot.** Pause the NEE arc here and pick up a
   different master-#16 follow-up: non-diffuse
   materials, multi-mesh upload, or relativistic-
   perception integration into the path tracer. The
   NEE skeleton stays latent (no caller flips the
   flag) until the user decides to resume the arc.

---

## 4. Verdict

**PASS** for the NEE.2 — CUDA NEE Skeleton slice.

- All §5 PASS criteria that apply to the NEE.2 scope
  pass. The two §5 criteria that do not apply
  (§5.5 atomicity, §5.6 diff size) are recorded as
  deliberate / within-precedent DEVIATIONs with
  written justifications. The two §5 criteria that
  are explicitly out of scope (§5.3 CLI, §5.7 tests)
  are DEFERRED to follow-up slices with sequencing
  constraints recorded in §3.2.
- All §4 must-not-touch invariants pass at 0 bytes
  diff per §1.8.
- All §6 runtime checks DEFERRED per the audit-host
  fingerprint per the established PT-P.x pattern.

No REPAIR items. Both audit-host build configs
re-verified green during this audit (7/7 OFF; 8/8
ON-with-OptiX-SDK-fallback). The TEX-P.6 fixture
regression is intact.

The NEE.2 skeleton is ready for follow-up; the
recommended next slice is the OptiX-side mirror
(NEE.4) per §3.3.

---

## 5. Sub-arc closure note

This audit closes the NEE skeleton sub-arc cleanly.
The PATH_TRACER_NEE_TASK.md brief remains the
canonical specification for the full NEE arc; the
follow-up slices (NEE.4 OptiX mirror, NEE.5 CLI +
tests) remain open. The skeleton's default-off byte-
identity invariant means the project can pause the
arc here at zero risk to existing fixtures or
goldens.

Mode reminder: **documentation only.** This file is
the audit; no source code is modified by the NEE.3
slice. The REPAIR list is empty so no fix-up edits
were needed.

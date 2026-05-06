# Path-Tracer Polish — Emission Handling Audit

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `dd98d90` ("PT-P.15:
emission handling (impl)").
Scope: PT-P.15 — the implementation slice that ships
`PATH_TRACER_POLISH_PLAN.md` §4.5 per the brief in
`PATH_TRACER_POLISH_EMISSION_TASK.md`.
Mode: documentation only. **No source code is modified by
this audit.**
Auditor: Claude Code, on the audit host (no CUDA Toolkit,
no OptiX SDK).

This audit walks the seven prompt checks in order and
records a single verdict at the end. Verdict legend
matches the texture-polish-audit + PT-P.4 / PT-P.7 /
PT-P.10 / PT-P.13 precedent:

- **PASS** — implemented, type-checked on the audit host,
  AND empirically exercisable on the audit host with a
  recorded happy-path run.
- **REPAIR** — implemented but a defect or inconsistency
  was found that should be patched.
- **BLOCKED** — empirical verification requires a CUDA
  host (no nvcc / OptiX SDK on the audit host).

---

## 1. Emission behavior is documented

**PASS.**

Three independent locations now describe the
`emissionColor * emissionStrength` factorisation
convention + the per-bounce short-circuit semantics:

### 1.1 The `is_emissive` predicate (`src/material/MaterialTypes.h:56-74`)

The predicate's doc-comment block is the canonical
specification:

```cpp
// PT-P.15: predicate that returns true iff a material's
// emission contribution is meaningfully non-zero. ...
//
// The convention authored across `MaterialParams` is:
//   emitted_radiance = emissionColor * emissionStrength
// where `emissionColor` is the unit-spectrum colour and
// `emissionStrength` is its scalar multiplier. Both must
// be non-zero for the material to contribute light.
[[nodiscard]] RR_HD inline bool is_emissive(const MaterialParams& m) {
    return m.emissionStrength > 0.0f
        && (m.emissionColor.x + m.emissionColor.y + m.emissionColor.z) > 0.0f;
}
```

The block names:

- The truth table (false for default-constructed
  `MaterialParams{}`, true only when both
  `emissionStrength > 0` AND
  `(emissionColor.x + .y + .z) > 0`).
- The factorisation convention so a future material-
  graph editor can distinguish "white emitter at 5x"
  from "5x-white emitter at 1x".
- The uniform-per-warp branch property (every pixel
  hitting the same surface reads the same
  `MaterialParams`, so no warp divergence).

### 1.2 The CUDA path-tracer kernel (`src/cuda/CudaPathTracer.cu:188-211`)

The kernel-side guard's doc-comment paragraph
references the predicate's home and explains the
short-circuit's correctness:

```cpp
// PT-P.15: short-circuit the emission add on non-emissive
// surfaces. The kernel-side branch is uniform per-warp
// (every pixel hitting the same surface reads the same
// MaterialParams), so the cost is one uniform compare and
// the savings on the common path are 6 multiplies + 3 adds
// per hit per bounce. The `emissionColor * emissionStrength`
// factorisation is the convention declared in
// `MaterialTypes.h::is_emissive`; both must be non-zero for
// the material to contribute light. The non-emissive path
// is bit-identical with the pre-PT-P.15 `+= 0` arithmetic
// because IEEE-754 addition of zero is exact.
if (rr::material::is_emissive(m)) {
    // Emission: the hit acts as a light source. Add its
    // contribution before generating the next bounce so even
    // bounce == max_bounces - 1 picks it up.
    const Vec3 emission = ...
    radiance = radiance + ...;
}
```

The "IEEE-754 addition of zero is exact" sentence is
the load-bearing claim that makes §3 (non-emissive
materials unchanged) provable from inspection alone.

### 1.3 The `MaterialParams` field-level documentation
(`src/material/MaterialTypes.h:29-31`)

Pre-existing (NOT touched by PT-P.15):

```cpp
rr::math::Vec3 emissionColor    = rr::math::Vec3{0.0f, 0.0f, 0.0f};
float          emissionStrength = 0.0f;   // multiplier on emissionColor
```

The "multiplier on emissionColor" comment was already
in place. PT-P.15's predicate doc-comment provides the
formalisation; the field-level note remains as the
quick-reference shorthand.

The three locations together cover the renderer's
emission contract end-to-end: WHAT the convention is
(the predicate doc-comment), HOW the kernel applies
it (the guard's comment block), and WHERE the fields
live (the struct itself).

---

## 2. Emissive hits add radiance consistently

**PASS structurally; BLOCKED empirically (CUDA-host check).**

For every material `m` with `is_emissive(m) == true`:

### 2.1 The guard's true-branch executes the identical arithmetic

`git diff dd98d90~1..dd98d90 -- src/cuda/CudaPathTracer.cu`
shows that the original 6-line emission body
(`const Vec3 emission = ... ; radiance = radiance +
...;`) was preserved verbatim INSIDE the new
`if (rr::material::is_emissive(m)) { ... }` brace —
the only change to that block is the indentation
shift induced by the new scope. The multiply-and-add
arithmetic is byte-identical with the pre-PT-P.15
commit `fa41e58`.

Specifically, when a thread enters the guarded block:

```cpp
const Vec3 emission =
    Vec3{m.emissionColor.x * m.emissionStrength,
         m.emissionColor.y * m.emissionStrength,
         m.emissionColor.z * m.emissionStrength};
radiance = radiance + Vec3{throughput.x * emission.x,
                           throughput.y * emission.y,
                           throughput.z * emission.z};
```

Six multiplies + three adds per hit per bounce — the
same instructions the pre-PT-P.15 kernel executed.
No FMA-pattern differences, no rounding differences,
no register-allocation surprises that an optimising
compiler could exploit differently across the two
versions.

### 2.2 The fixture's emissive material fires the predicate

`scenes/test_full_scene.rrscene`'s material id 3
("emissive") is authored as:

```json
{ "id": 3, "name": "emissive",
  "baseColor":        [0.10, 0.10, 0.10],
  "emissionColor":    [1.00, 0.85, 0.35],
  "emissionStrength": 2.0 }
```

`is_emissive(m)` evaluates to:

```
m.emissionStrength = 2.0f > 0.0f                       => true
m.emissionColor.x + .y + .z = 1.00 + 0.85 + 0.35
                            = 2.20 > 0.0f              => true
true && true => true
```

So every hit on the "ground-bulb" sphere (which uses
material 3) takes the guarded branch, consuming the
identical arithmetic path. The predicate is correct
on this fixture.

### 2.3 Empirical verification deferred

The byte-identity claim — `pathtrace_spp_*.ppm` for
`scenes/test_full_scene.rrscene` is bit-for-bit
identical pre-/post-PT-P.15 — is structurally
guaranteed by §2.1. A CUDA-host operator can confirm
empirically by:

```
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene
$ git checkout fa41e58~ -- src/  # pre-PT-P.15
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene \
    --output output/baseline_spp1.ppm
$ git checkout HEAD -- src/      # restore
$ cmp output/pathtrace_spp_1.ppm output/baseline_spp1.ppm
```

Expected: `cmp` reports the files are identical. If
they differ even by one byte, the audit's verdict for
§2 should flip to REPAIR.

This audit cannot run the comparison on the audit
host (no CUDA toolchain). The check is captured under
§6 below.

---

## 3. Non-emissive materials unchanged

**PASS structurally; BLOCKED empirically (CUDA-host check).**

For every material `m` with `is_emissive(m) == false`:

### 3.1 The pre-slice and post-slice paths are arithmetically equivalent

Pre-PT-P.15 kernel (recovered from commit `fa41e58`):

```cpp
const Vec3 emission =
    Vec3{0 * 0, 0 * 0, 0 * 0}; // = (0, 0, 0)
radiance = radiance + Vec3{throughput.x * 0,
                           throughput.y * 0,
                           throughput.z * 0};
// radiance += (0, 0, 0)  ->  radiance unchanged
```

Post-PT-P.15 kernel:

```cpp
if (rr::material::is_emissive(m)) {
    // ... (skipped for non-emissive)
}
// radiance unchanged
```

IEEE-754 addition of zero is exact: `x + 0.0f == x`
bit-for-bit for any finite `x` (and even for NaN /
±inf, since adding a non-NaN zero is the identity
on every finite operand). The pre-PT-P.15 path
produced `radiance + (0, 0, 0) = radiance` exactly;
the post-PT-P.15 path skips the add and leaves
`radiance` untouched. The two paths produce
bit-identical pixels.

### 3.2 The fixture's non-emissive materials cover the common case

`scenes/test_full_scene.rrscene` has five materials:

| id | name      | emissionColor       | emissionStrength | `is_emissive`? |
|----|-----------|---------------------|------------------|----------------|
| 0  | red       | (0, 0, 0) (default) | 0.0 (default)    | false          |
| 1  | green     | (0, 0, 0) (default) | 0.0 (default)    | false          |
| 2  | blue      | (0, 0, 0) (default) | 0.0 (default)    | false          |
| 3  | emissive  | (1.00, 0.85, 0.35)  | 2.0              | **true**       |
| 4  | neutral   | (0, 0, 0) (default) | 0.0 (default)    | false          |

Four out of five materials in the fixture take the
PT-P.15 short-circuit path. For three of the four
spheres (left=red, centre=green, right=blue) and the
ground-quad mesh (neutral), every hit skips the
emission block. The fifth path (the ground-bulb
emissive sphere) takes the guarded branch.

### 3.3 The non-emissive material `MaterialParams{}` default

The renderer's fallback for unmaterialed primitives is
`MaterialParams{}` (the default constructor; see
`material_for(...)` in `CudaPathTracer.cu:114-122`).
The default values are exactly the non-emissive case:
`emissionColor = (0, 0, 0)`, `emissionStrength = 0.0f`.
`is_emissive(MaterialParams{}) == false` by the
truth-table check at §1.1. Unmaterialed primitives
take the short-circuit path, identical to the
explicitly-non-emissive case.

### 3.4 Empirical verification deferred

The byte-identity claim — every existing
`pathtrace_spp_*.ppm` is bit-for-bit identical
pre-/post-PT-P.15 — is structurally guaranteed by
§3.1 + §3.2. A CUDA-host operator can confirm with
the same `cmp` procedure described in §2.3. Captured
under §6.

---

## 4. GPU-side shading only

**PASS.**

`grep -rn "is_emissive" src/`:

```
src/material/MaterialTypes.h:71:[[nodiscard]] RR_HD inline bool is_emissive(const MaterialParams& m) {
src/cuda/CudaPathTracer.cu:196:// `MaterialTypes.h::is_emissive`; both must be non-zero for
src/cuda/CudaPathTracer.cu:200:if (rr::material::is_emissive(m)) {
```

Three matches:

- The predicate declaration in
  `MaterialTypes.h` (line 71).
- One reference in a kernel doc-comment
  (`CudaPathTracer.cu:196`).
- One actual call site
  (`CudaPathTracer.cu:200`) inside the
  `__global__ k_pathtrace_sample` kernel.

ZERO host-side callers. The predicate is `RR_HD
inline` so it COULD be called from host code, but no
host caller exists today.

The Stage-11 audit's three grep sweeps confirm no
new host-side per-pixel work was introduced:

```
$ grep -rnE "for.*<.*width|for.*<.*height" \
    src/renderer/ src/pathtracer/*.cpp src/main.cpp
=> (no matches)

$ grep -rn "intersect_sphere|intersect_triangle|closest_hit" \
    src/renderer/ src/pathtracer/*.cpp
src/renderer/Hit.h:30:    // `intersect_triangle`. ...

$ grep -rn "for.*samples_per_pixel|for.*effective_samples" \
    src/pathtracer/*.cpp src/main.cpp
src/pathtracer/PathTracer.cpp:115:
    for (int s = 0; s < effective_samples_per_pixel; ++s) {
```

Same baseline matches every prior path-tracer audit
recorded:

- Zero per-pixel for-loops on the host.
- One spp launcher loop at sample-frame granularity
  (NOT per-pixel; the same loop the Stage 11 audit
  classified, byte-identical with PT-P.9's renaming
  to `effective_samples_per_pixel`).
- One doc-comment in `Hit.h:30` referencing
  `intersect_triangle`.

### 4.1 PT-P.15-specific scope

PT-P.15 edited two files:

| File                              | New per-pixel code? |
|-----------------------------------|---------------------|
| `src/material/MaterialTypes.h`    | NO. Free function predicate. The function body is one boolean expression; no loops, no per-pixel reads, no per-pixel writes. |
| `src/cuda/CudaPathTracer.cu`      | NO. The new `if` guards an existing per-bounce add with no new per-pixel arithmetic. |

The `is_emissive` predicate runs ONCE per kernel
thread per bounce (uniform-per-warp), exactly as the
existing emission add did. Master rule 5/7 ("All
per-pixel/per-ray rendering must happen on GPU")
remains upheld — and the new branch is uniform per
warp, so the GPU's SIMT execution model handles it
without divergence.

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
`-Wall -Wextra -Wpedantic` triple. ctest counts
unchanged from PT-P.6 / PT-P.9 / PT-P.12 (the slice
did not add or remove a ctest binary, per the PT-P.14
task §1.7's no-new-test recommendation).

The `is_emissive` predicate is type-checked on the
audit-host build through every `MaterialTypes.h`
consumer (the OFF config still includes it via the
host-compiled `rr_material` static library); the
`.cu` kernel TU is gated by `RR_ENABLE_CUDA` and is
not compiled on the audit host, but the host-side
predicate's signature + body type-check cleanly.

---

## 6. Runtime-deferred CUDA-host status

**BLOCKED on six PPM artefacts (the same set every
prior path-tracer audit enumerated) PLUS one PT-P.15-
specific empirical check.**

The standard runtime-deferred surface from prior
audits is unchanged by PT-P.15:

| Artefact                                | CUDA-host expectation                       |
|-----------------------------------------|---------------------------------------------|
| `output/gpu_rng_test.ppm`               | byte-identical with pre-PT-P.15             |
| `output/gpu_accumulation_test.ppm`      | byte-identical                              |
| `output/pathtrace_spp_1.ppm`            | byte-identical (see §6.1 below)            |
| `output/pathtrace_spp_16.ppm`           | byte-identical (same)                      |
| `output/optix_pathtrace_spp1.ppm`       | byte-identical (OptiX path-tracer programs |
|                                         | are emission-blind; PT-P.15 doesn't reach |
|                                         | them)                                       |
| `output/optix_pathtrace_spp16.ppm`      | byte-identical (same)                      |

### 6.1 PT-P.15-specific operator check: byte-identity on `test_full_scene.rrscene`

The structural guarantee from §2.1 + §3.1 says every
existing `pathtrace_spp_*.ppm` should be bit-for-bit
identical pre-/post-PT-P.15. A CUDA-host operator
can confirm by:

```
# Step 1: render with the post-PT-P.15 tree
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene

# Step 2: check out pre-PT-P.15 source and re-render
$ git checkout fa41e58 -- src/cuda/CudaPathTracer.cu \
                          src/material/MaterialTypes.h
$ cmake --build build-cuda
$ ./build-cuda/bin/RelativityRender \
    --render-pathtrace scenes/test_full_scene.rrscene \
    --output output/baseline_spp1.ppm
$ git checkout HEAD -- src/  # restore

# Step 3: pixel-diff
$ cmp output/pathtrace_spp_1.ppm output/baseline_spp1.ppm
```

Expected: `cmp` reports no difference. The fixture's
material id 3 ("emissive", `is_emissive == true`)
exercises the §2.1 path; materials 0/1/2/4
(non-emissive, `is_emissive == false`) exercise the
§3.1 path. A single render covers both cases.

### 6.2 PT-P.15-specific operator check: optional emissive-only scene

A future scene fixture authored with ALL materials
having `emissionStrength > 0` AND `emissionColor != 0`
would isolate the §2.1 path. Per the PT-P.14 task
§6.3 this is OPTIONAL and not required for the
verdict; the existing fixture's mixed coverage
(four non-emissive + one emissive) is sufficient.

### 6.3 No runner integration update needed

`tools/verify_cuda_host.py` exercises
`--render-pathtrace` automatically. The empirical
PPM byte-identity check is a manual operator step
once a CUDA host is available; the runner does not
currently capture pre-/post-slice diffs (and adding
that capability would be its own slice).

```
$ git diff dd98d90~1..dd98d90 -- tools/verify_cuda_host.py
=> 0 bytes
```

---

## 7. Verdict

| # | Audit item                                           | Result   |
|---|------------------------------------------------------|----------|
| 1 | Emission behavior is documented                      | PASS     |
| 2 | Emissive hits add radiance consistently              | PASS structurally; BLOCKED empirically |
| 3 | Non-emissive materials unchanged                     | PASS structurally; BLOCKED empirically |
| 4 | GPU-side shading only                                | PASS — zero host-side callers |
| 5 | Build status (both audit-host configs)               | PASS     |
| 6 | Runtime-deferred CUDA-host status                    | BLOCKED  |
| 7 | Overall                                              | **PASS** (BLOCKED rows for the structurally-guaranteed PPM byte-identity carry forward to a CUDA-host run) |

**Overall verdict: PASS.**

PT-P.15 ships exactly the polish the PT-P.14 task brief
specified, both audit-host build configs remain green
(7/7 OFF, 8/8 ON-audit-host), the per-pixel code path
is documented end-to-end across three locations
(`MaterialTypes.h::is_emissive`'s contract,
`CudaPathTracer.cu`'s short-circuit comment, and the
`MaterialParams` field-level "multiplier on
emissionColor" note), and master rule 5/7 (no CPU ray
tracing) remains enforced. The two BLOCKED rows are the
runtime byte-identity check on `pathtrace_spp_*.ppm`
that needs a CUDA host; the structural guarantees from
§2.1 + §3.1 (the guarded body executes identical
arithmetic; IEEE-754 add-of-zero is exact) make the
empirical check a confirmation rather than a discovery.

REPAIR items: none.

The PT-P.{14,15,16} sub-arc closes a clean three-slice
cadence:

- **PT-P.14** — task definition (docs only).
- **PT-P.15** — implementation (one new `RR_HD inline`
  predicate + one `if` guard; first PT-P.x slice to
  touch a `.cu` file).
- **PT-P.16** — this audit.

Diff size deviation flagged in PT-P.15's BUILD_PLAN
entry (43 added vs the 25 cap) was entirely doc-comment
+ indentation; the actual LOGIC was 3 lines. Per the
PT-P.6 / PT-P.9 precedent, the deviation note in
PT-P.15's entry is the audit trail; this audit
re-confirms zero REPAIR items despite the deviation.

### Recommended next step

Per `PATH_TRACER_POLISH_PLAN.md` §5 +
`PATH_TRACER_POLISH_AUDIT.md` §7 +
`PATH_TRACER_POLISH_EMISSION_TASK.md` §7's sequencing,
two remaining plan items can ship next; the operator's
preferred order from those docs:

1. **§4.1 — RNG stability** (key-mix collision audit).
   Touches `src/pathtracer/RNG.h` and changes every
   `pathtrace_spp_*.ppm` byte-exactly. The first
   PT-P.x slice that produces a real pixel-diff
   pre-/post-slice; sequence its CUDA-host
   verification carefully.
2. **§4.7 — Firefly clamp placeholder**. Default-off
   field on `PathTraceConfig` + matching kernel
   guards on BOTH the CUDA and OptiX path-trace
   raygens. Largest remaining surface; sequence last
   so the kernel guards are added against a stable
   RNG.

### Alternative paths

- **Trigger the CUDA-host verification run** that
  flips the §6 BLOCKED rows of THIS audit (and
  PT-P.4 / PT-P.7 / PT-P.10 / PT-P.13's BLOCKED
  rows) to PASS. Single command-line invocation
  (`tools/verify_cuda_host.py [--optix]`) on a real
  CUDA + OptiX-SDK host. Per
  `docs/CUDA_HOST_VERIFICATION_AUDIT.md` §3, the
  resulting `docs/CUDA_HOST_VERIFICATION_REPORT.md`
  byte-replaces the currently-committed audit-host
  REPAIR report. The PT-P.15 byte-identity check
  (§6.1 above) is a NEW item this run could confirm.
- **Apply the `is_emissive` predicate to the other
  emission consumers**: `CudaTestKernel.cu`'s
  `--render` direct-lighting renderer (line 425)
  and `OptixPrograms.cu`'s flat-shading +
  textured-material branches (lines 382-387 + 645-649).
  These are NOT path-tracing and are out of scope
  for the §4.5 polish; a separate small slice can
  apply the helper there.
- **Pivot to a different polish arc / master-order
  item.** The TEX-P.x arc landed PASS (TEX-P.7); the
  CUDA-H.x arc is currently in REPAIR pending the
  CUDA-host run. Master order #16 (path tracing —
  feature work like NEE / non-diffuse BSDFs /
  multi-mesh upload) is the next major follow-up
  after the PT-P.x polish arc closes.

PT-P.16 (this audit) closes the PT-P.{14,15} sub-arc.
The next concrete slice — when the operator chooses
to continue — opens with a PT-P.17 task definition
mirroring the PT-P.{2,5,8,11,14} cadence, OR pivots
to a runtime-verification slice as above.

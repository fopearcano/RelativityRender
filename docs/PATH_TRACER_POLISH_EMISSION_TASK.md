# Path-Tracer Polish — Emission Handling Task

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_POLISH_PLAN.md` §4.5.
Selected via:
`docs/PATH_TRACER_POLISH_ENV_FALLBACK_AUDIT.md` §7's
"Recommended next step" verdict (the §4.4 polish shipped
PASS via PT-P.12; §4.5 is the next remaining item per the
plan's sequencing — first non-host-only PT-P.x slice).
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next
slice (PT-P.15 implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's
reasoning.

---

## 1. Exact issue

**Title.** PT-P.x — Emission handling clarity + per-hit
short-circuit (CUDA path tracer only).

**Source.** `PATH_TRACER_POLISH_PLAN.md` §4.5.

**One-paragraph summary.** The CUDA path tracer's
per-bounce kernel adds `material.emissionColor *
emissionStrength` modulated by throughput on EVERY hit,
regardless of whether the surface is meaningfully
emissive. For non-emissive materials (the common case)
the multiplication produces `(0, 0, 0)` and the add is
`+= 0` — correct but wasteful. There's also a clarity
gap: nothing in `MaterialTypes.h` or the kernel comment
explicitly factors the `emissionColor * emissionStrength`
convention, so a future material-graph editor can't
distinguish "white emitter at 5x" from "5x-white emitter
at 1x" without reverse-engineering the kernel. This
slice adds (a) a `RR_HD inline bool is_emissive(const
MaterialParams&)` helper in `MaterialTypes.h` that
guards the add, and (b) a brief kernel comment
documenting the factorisation convention. ZERO new
rendering modes; ZERO behavioural change for any
correctly-authored material.

**The current state.** Three source artefacts are
relevant.

### 1.1 Material POD (`src/material/MaterialTypes.h`)

`MaterialParams` carries the two emission fields as
declared at lines 29-31:

```cpp
rr::math::Vec3 emissionColor    = rr::math::Vec3{0.0f, 0.0f, 0.0f};
float          emissionStrength = 0.0f;   // multiplier on emissionColor
```

The defaults are non-emissive. The header has a long
file-level doc-comment but no per-field doc explaining
the factorisation convention.

### 1.2 CUDA path tracer (`src/cuda/CudaPathTracer.cu`)

The per-bounce emission add lives at lines 185-198:

```cpp
const auto m = material_for(hit.material_index,
                            scene.materials,
                            scene.material_count);

// Emission: the hit acts as a light source. Add its
// contribution before generating the next bounce so even
// bounce == max_bounces - 1 picks it up.
const Vec3 emission =
    Vec3{m.emissionColor.x * m.emissionStrength,
         m.emissionColor.y * m.emissionStrength,
         m.emissionColor.z * m.emissionStrength};
radiance = radiance + Vec3{throughput.x * emission.x,
                           throughput.y * emission.y,
                           throughput.z * emission.z};
```

The `emission` Vec3 is unconditionally constructed,
multiplied by throughput, and added to `radiance`. For
a non-emissive material this is six floating-point ops
producing zero contribution.

### 1.3 Other emission consumers (NOT in scope)

`grep -rln "emissionColor|emissionStrength" src/cuda/
src/optix/` returns three actual call sites:

- `src/cuda/CudaPathTracer.cu` (lines 185-198) — the
  §4.5 target.
- `src/cuda/CudaTestKernel.cu` (line 425) — the
  `--render` direct-lighting renderer (NOT a path
  tracer).
- `src/optix/OptixPrograms.cu` (lines 382-387 +
  645-649) — the OptiX `shading_mode == 1` flat-
  shading branch + the textured-material branch (also
  NOT path-tracing).

The OptiX path-tracer programs
(`__raygen__pathtrace`, `__miss__pathtrace`,
`__closesthit__pathtrace`) do NOT consume emission
today — they are Lambert-only. No symmetric edit is
needed on the OptiX side for this slice; emission
support there is its own future task.

**The single concrete change.**

Required outcome:

### 1.4 New helper in `src/material/MaterialTypes.h`

Add an inline `RR_HD` predicate immediately after the
`MaterialParams` struct closing brace (line 54):

```cpp
// PT-P.15: predicate that returns true iff a material's
// emission contribution is meaningfully non-zero. Used
// by the CUDA path tracer to short-circuit the per-bounce
// emission add on the common (non-emissive) path; the
// branch is uniform per-warp because `MaterialParams` is
// shared by every pixel hitting the same surface, so no
// warp divergence is introduced. Returns false for the
// default-constructed `MaterialParams{}` (the renderer's
// fallback for unmaterialed primitives).
//
// The convention authored across `MaterialParams` is:
//   emitted_radiance = emissionColor * emissionStrength
// where `emissionColor` is the unit-spectrum colour and
// `emissionStrength` is its scalar multiplier. Both
// must be non-zero for the material to contribute light.
[[nodiscard]] RR_HD inline bool is_emissive(const MaterialParams& m) {
    return m.emissionStrength > 0.0f
        && (m.emissionColor.x + m.emissionColor.y + m.emissionColor.z) > 0.0f;
}
```

The exact wording is the implementer's choice; the
contract is that the predicate must:

- Return `false` for a default-constructed
  `MaterialParams{}` (`emissionStrength == 0.0f` AND
  `emissionColor == (0, 0, 0)`).
- Return `true` only when BOTH `emissionStrength >
  0.0f` AND at least one component of `emissionColor`
  is `> 0.0f`. The OR-guard via component sum
  (rather than per-component max) is cheaper on
  the device (one float-add + one compare) and
  semantically equivalent for non-negative
  components (the loader / validator already enforces
  `emissionColor.x|y|z >= 0.0f`).
- Be `RR_HD inline` so the same code runs host
  (validator + future host tests) and device (kernel).

### 1.5 Use the helper in `src/cuda/CudaPathTracer.cu`

Replace the unconditional `emission` add with a guarded
one:

```cpp
const auto m = material_for(hit.material_index,
                            scene.materials,
                            scene.material_count);

// PT-P.15: short-circuit the emission add on non-
// emissive surfaces. The kernel-side branch is uniform
// per-warp (every pixel hitting the same surface
// reads the same MaterialParams), so the cost is one
// uniform compare and the savings on the common path
// are 6 multiplies + 3 adds per hit per bounce. The
// `emissionColor * emissionStrength` convention is
// declared in `MaterialTypes.h::is_emissive`.
if (rr::material::is_emissive(m)) {
    const Vec3 emission =
        Vec3{m.emissionColor.x * m.emissionStrength,
             m.emissionColor.y * m.emissionStrength,
             m.emissionColor.z * m.emissionStrength};
    radiance = radiance + Vec3{throughput.x * emission.x,
                               throughput.y * emission.y,
                               throughput.z * emission.z};
}
```

The kernel keeps the existing `// Emission: the hit
acts as a light source. ...` comment; the new comment
above the guard explains the short-circuit + the
factorisation reference.

### 1.6 No `MaterialParams` field changes

`emissionColor` / `emissionStrength` keep their existing
types, defaults, and field order. The slice only adds a
free function alongside the struct; the struct itself is
byte-identical.

### 1.7 No new test (consistent with PT-P.6 / PT-P.9 / PT-P.12)

The polish is verifiable by code inspection (the
`is_emissive` predicate body is one boolean expression;
reading it confirms the truth table) plus a CUDA-host
behavioural smoke (per §6 below) on a future operator
run. The PT-P.6 / PT-P.9 / PT-P.12 "no new test"
precedent applies; PT-P.13 cleared the same posture
with zero REPAIR items.

The implementer MAY optionally extend
`tests/renderer_tests.cpp` with a host-only check that
`is_emissive(MaterialParams{}) == false` and that a
constructed emissive material returns `true` (the
helper is `RR_HD inline` and compiles host-side too,
so the test runs even on the audit host without CUDA).
This adds ~10 lines to the existing ctest binary; if
chosen, the BUILD_PLAN entry must justify the added
scope.

---

## 2. Expected behavior

The three contractual properties the polish must
honour (matching the prompt's spec sub-bullets
exactly):

### 2.1 Emissive hits add radiance consistently

For every material `m` with `is_emissive(m) == true`:

- The kernel computes `emission = m.emissionColor *
  m.emissionStrength` exactly as before.
- The kernel adds `throughput * emission` to
  `radiance` exactly as before.
- The byte-level CUDA arithmetic is identical with
  the pre-PT-P.15 baseline (same multiply
  instructions, same add instructions, same input
  registers).

In other words: for a scene whose emissive surfaces
include `MaterialParams{ emissionColor = (1, 0.85,
0.35), emissionStrength = 2.0f }` (the existing
"warm emitter" / "emissive" material in
`scenes/test_full_scene.rrscene`), the rendered
PPM is byte-identical with the pre-PT-P.15 baseline.

### 2.2 Non-emissive materials remain unchanged

For every material `m` with `is_emissive(m) == false`:

- The pre-PT-P.15 kernel computed `emission = (0,
  0, 0)`, multiplied by throughput, added zero to
  `radiance`. Net contribution: zero radiance.
- The post-PT-P.15 kernel skips the emission block
  entirely. Net contribution: zero radiance.

The two paths are arithmetically equivalent: floating-
point addition of zero is exact (no rounding), so
`radiance + (0, 0, 0)` is byte-identical with
`radiance` itself. Every existing scene whose materials
all have `emissionStrength == 0` (the default;
non-emissive) renders byte-identically.

### 2.3 No CPU shading

The `is_emissive` predicate runs on the device (the
guard is inside `k_pathtrace_sample`, a `__global__`
kernel). The host-side `PathTracer::render` and the
dispatcher (`run_render_pathtrace`) do not call
`is_emissive` and do not touch per-pixel emission
data. The predicate is `RR_HD inline` so it COULD be
called from host code — but no such caller is added by
this slice. Master rule 5/7 ("All per-pixel/per-ray
rendering must happen on GPU") remains upheld.

The host-side validator (`validate_material_texture_ids`)
and the loader do not call `is_emissive`. A future
slice may use the predicate for an authoring-time
warning ("you set `emissionStrength` but
`emissionColor` is black; this material won't
emit"), but that's out of scope for §4.5.

---

## 3. Files likely involved

The implementation slice will touch this minimal set:

| File                                     | Change                                                  |
|------------------------------------------|---------------------------------------------------------|
| `src/material/MaterialTypes.h`           | Append `RR_HD inline bool is_emissive(...)` after      |
|                                          | the `MaterialParams` struct. ~8-12 lines (predicate    |
|                                          | + doc-comment block).                                   |
| `src/cuda/CudaPathTracer.cu`             | Wrap the existing emission add (lines 192-198) in      |
|                                          | `if (rr::material::is_emissive(m)) { ... }`. Add a     |
|                                          | brief comment above the guard. ~6-8 lines (1 if        |
|                                          | + 1 brace + 4-line comment).                            |
| `docs/BUILD_PLAN.md`                     | Slice-closing entry following the established          |
|                                          | TEX-P.x / PT-P.x format.                                |

Two source files; matches the PT-P.x master rule of
"max 2 source files unless explicitly justified".

`src/optix/`, `src/cuda/CudaTestKernel.cu`,
`src/cuda/CudaPathTracer.cuh`,
`src/renderer/AccumulationBuffer.{h,cpp}`,
`src/pathtracer/PathTracer.{h,cpp}`,
`src/main.cpp`, `src/core/CommandLine.{h,cpp}`,
every `tests/*.cpp` file, every `*.rrscene` file, and
every other `src/` file MUST be byte-identical
post-slice (unless the implementer takes the optional
test-extension path described in §1.7; in that case
`tests/renderer_tests.cpp` may add ~10 lines exercising
the host-side `is_emissive` predicate — flagged in the
BUILD_PLAN entry).

### 3.1 Why CudaPathTracer.cu counts as one source file (not the kernel)

The PT-P.x cap of "max 2 source files" counts files
edited, not files compiled. PT-P.15 edits two files
(`MaterialTypes.h` + `CudaPathTracer.cu`). The
`__global__` / `__device__` annotations on the kernel
functions in `CudaPathTracer.cu` make it a CUDA
translation unit, but it is still one file in the
`max 2 source files` sense. This is the first PT-P.x
slice that touches the CUDA TU; PT-P.{3,6,9,12} all
stayed host-side.

---

## 4. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 4.1 OptiX programs

- `src/optix/OptixPrograms.cu` — every byte. The
  OptiX path-tracer raygen / miss / closest-hit
  programs do not consume emission today; the
  flat-shading + textured-material branches that DO
  read `emissionColor * emissionStrength` are NOT
  path-tracing and are out of scope for §4.5.
- `src/optix/OptixRenderer.{h,cpp}`,
  `src/optix/OptixPipeline.{h,cpp}`,
  `src/optix/OptixSBT.h`,
  `src/optix/OptixDenoiser.{h,cpp}`,
  `src/optix/OptixLaunchParams.h` — every byte.

### 4.2 Other CUDA kernels

- `src/cuda/CudaTestKernel.cu` — every byte. The
  `--render` direct-lighting renderer's emission add
  (line 425) is NOT path-tracing; out of scope.
  Adding `is_emissive` there is a separate (smaller)
  slice — DO NOT bundle it into PT-P.15.
- `src/cuda/CudaTextureSampleTestKernel.cu`,
  `src/cuda/CudaRngTestKernel.cu`,
  `src/cuda/CudaAccumulation.cu`,
  `src/cuda/CudaIntersection.cuh`,
  `src/cuda/CudaTexture.cuh`,
  `src/cuda/CudaScene.cuh`,
  `src/cuda/CudaKernels.cuh`,
  `src/cuda/CudaSceneSpheresKernel.cu`,
  `src/cuda/CudaSphereKernel.cu`,
  `src/cuda/CudaCameraRaysKernel.cu`,
  `src/cuda/CudaGradientKernel.cu`,
  `src/cuda/CudaRelativisticKernel.cu`,
  `src/cuda/CudaPathTracer.cuh` (the launcher decl):
  every byte.

### 4.3 Path-tracer host code

- `src/pathtracer/PathTracer.h` — every byte. The
  PT-P.6 `kMaxBouncesCap`, the PT-P.9
  `kSamplesPerPixelCap`, and the PT-P.12 environment-
  fallback doc-comment all remain.
- `src/pathtracer/PathTracer.cpp` — every byte. The
  validation prelude (PT-P.6 / PT-P.9 clamps + the
  lower-bound rejections) is byte-identical.
- `src/pathtracer/RNG.{h,cuh}`,
  `src/pathtracer/Sampling.{h,cuh}`: every byte.
- `src/renderer/AccumulationBuffer.{h,cpp}`,
  `src/renderer/AOV.{h,cpp}`,
  `src/renderer/GpuAOVBuffer.{h,cpp}`,
  `src/renderer/Hit.h`: every byte.

### 4.4 Dispatcher / CLI

- `src/main.cpp` — every byte. No new info-log
  line; no change to `run_render_pathtrace` or any
  other dispatcher.
- `src/core/CommandLine.{h,cpp}` — every byte.
  No new CLI flag.

### 4.5 Path-tracer output

For every authored `MaterialParams`:

- `output/pathtrace_spp_1.ppm`,
  `output/pathtrace_spp_16.ppm`: byte-identical
  pixel data on a CUDA host. The branch arithmetic
  (§2.1 + §2.2) is exact-equivalent to the pre-
  PT-P.15 path; FMA instruction sequencing and
  rounding are unchanged because the emissive case
  retains identical multiply / add operations and
  the non-emissive case adds exactly zero (which
  IEEE-754 addition handles bit-exactly).
- `output/optix_pathtrace_spp1.ppm`,
  `output/optix_pathtrace_spp16.ppm`: byte-identical
  (the OptiX path-trace programs are unchanged).
- `output/gpu_accumulation_test.ppm`,
  `output/gpu_rng_test.ppm`,
  `output/test_full_scene.ppm` (`--render`),
  `output/optix_*.ppm` (every existing OptiX
  dispatcher output): byte-identical.

### 4.6 PathTraceConfig field set

- Byte-identical (zero new fields, zero default
  changes). The PT-P.6 / PT-P.9 / PT-P.12
  predecessors remain.

### 4.7 Scene fixtures

- All `*.rrscene` files under `scenes/` —
  byte-identical. The TEX-P.6 fixture's three-case
  validator output remains the regression baseline.

### 4.8 Other audits / plans

- `docs/PATH_TRACER_POLISH_PLAN.md`: optionally add
  a one-line "PT-P.15 shipped" note at the top of
  §4.5. NOT required.
- The seven earlier PT-P.x task / audit docs: NO
  edits.
- The TEX-P.x arc: NO edits.
- The CUDA-H.x arc + `tools/verify_cuda_host.py`:
  NO changes (the runner exercises the existing
  `--render-pathtrace` command; the new helper is
  picked up by the existing build automatically).

---

## 5. PASS criteria

The implementation slice passes when ALL of the
following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF): clean
  build, zero new warnings. The new
  `is_emissive` helper compiles host-side too
  (it's `RR_HD inline`), so the audit-host build
  still type-checks the predicate even though
  `CudaPathTracer.cu` is not compiled in this
  config (the `.cu` file is gated by
  `RR_ENABLE_CUDA` in CMake).
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON): clean
  build, zero new warnings.

### 5.2 Tests

- `ctest --output-on-failure` from `build`: 7/7
  PASS (or 7/7 + 1 if the implementer takes the
  optional test-extension path; the binary count
  stays at 7).
- `ctest --output-on-failure` from `build-ON`:
  8/8 PASS.

### 5.3 Source diff size

- `src/material/MaterialTypes.h` diff: ~8-15
  added, 0 deleted (predicate + doc-comment).
- `src/cuda/CudaPathTracer.cu` diff: ~6-12 added,
  ~0-2 deleted (one `if` wrapper + indentation
  changes on the existing emission lines + a new
  comment above the guard).
- TOTAL across both source files: ≤ 25 added.
  Anything LARGER flagged in the BUILD_PLAN entry
  as a deviation (precedent: PT-P.6 / PT-P.9
  flagged-deviation notes are the templates).

### 5.4 No-touch invariants

`git diff` after the slice MUST show zero bytes
changed in:

- `src/optix/`
- `src/cuda/` (every file EXCEPT
  `CudaPathTracer.cu`)
- `src/pathtracer/`
- `src/renderer/`
- `src/main.cpp`
- `src/core/`
- `src/io/`
- `src/scene/`
- `src/lighting/`
- `src/material/` (every file EXCEPT
  `MaterialTypes.h`)
- every `*.rrscene` file under `scenes/`
- every `tests/*.cpp` file (unless the implementer
  chose the optional test extension; the
  BUILD_PLAN entry must flag the choice).
- `tools/verify_cuda_host.py`
- `CMakeLists.txt`

Verifiable by:

```
git diff -- \
  src/optix/ src/pathtracer/ src/renderer/ \
  src/main.cpp src/core/ src/io/ src/scene/ \
  src/lighting/ scenes/ tools/verify_cuda_host.py \
  CMakeLists.txt \
  | wc -l
=> 0
```

(Plus a more granular check excluding
`src/material/MaterialTypes.h` and
`src/cuda/CudaPathTracer.cu`.)

### 5.5 Behavioural smoke (audit host)

- `./build/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene` continues to emit
  the documented "requires CUDA" audit-host
  fallback byte-identically with the pre-PT-P.15
  baseline. The new guard is unreachable on the
  audit host (the dispatcher returns from the
  `--render-pathtrace requires CUDA` branch
  before reaching the kernel launch); the smoke
  confirms that fallback path is unchanged.
- `./build-ON/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene`: same.
- `./build/bin/RelativityRender --scene-info
  scenes/test_textured_material.rrscene`: emits
  the TEX-P.6 fixture's expected three-case log
  sequence byte-identically (one Case 1 info +
  two Case 3 warnings; `fixups applied: 2`).
  Confirms zero PT-P.15 ripple onto the texture
  validator.

### 5.6 Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing
  entry matching the established PT-P.x format
  (Scope / What ships / What does NOT change /
  Behaviour matrix / Master rule compliance /
  Verified at the build).
- The entry references
  `docs/PATH_TRACER_POLISH_PLAN.md` §4.5 + this
  task file as the source of the specification.
- The entry calls out that PT-P.15 is the FIRST
  PT-P.x slice to touch a `.cu` file (the
  predecessors PT-P.{3,6,9,12} all stayed
  host-side).

### 5.7 Master rule compliance

- Build incrementally (rule 1) + every step
  compilable (rule 2): both audit-host configs
  green.
- No fake stubs (rule 3): the predicate names a
  real, verifiable property (the renderer's
  emission contract).
- No CPU per-pixel work (rule 5/7): the `is_emissive`
  branch runs on the device per kernel thread per
  bounce; zero new host-side per-pixel work.
- Update BUILD_PLAN (rule 8): the slice-closing
  entry.

---

## 6. Runtime-deferred CUDA-host checks

PT-P.15 is the first PT-P.x slice whose runtime
verification has a SLICE-SPECIFIC empirical hook —
not just "the `requires CUDA` fallback fires the
same as before". The kernel-side guard's correctness
needs a CUDA-host run on a scene with at least one
emissive material to confirm:

### 6.1 Byte-identity on a scene with emissive surfaces

Run `./build-cuda/bin/RelativityRender --render-pathtrace
scenes/test_full_scene.rrscene` BEFORE and AFTER the
PT-P.15 commit. The "emissive" material (id 3 in
`test_full_scene.rrscene`, `emissionColor = (1.00,
0.85, 0.35)`, `emissionStrength = 2.0f`) is meaningfully
emissive — the new `is_emissive` predicate returns
`true` for it — so its kernel branch runs both pre-
and post-slice. The resulting `pathtrace_spp_1.ppm`
+ `pathtrace_spp_16.ppm` must be bit-for-bit
identical pre/post-PT-P.15. This is the §2.1 contract
empirically verified.

If the PPMs differ even by one byte, the slice
introduced a bug and the audit's verdict for §2.1
should flip to REPAIR.

### 6.2 Byte-identity on a scene without emissive surfaces

Run the same command on a scene where every
`emissionStrength == 0` (none of the existing
`scenes/*.rrscene` fixtures are purely non-emissive
with a path-traceable layout, but a one-off harness
scene can be authored: spheres + meshes + materials,
all with `emissionStrength = 0.0f`). The new branch
SKIPS every emission add; the resulting PPM should
match a pre-PT-P.15 render that ALSO produces zero
emission contribution (the +=0 path in the old
kernel).

This is the §2.2 contract empirically verified. The
comparison is "post-PT-P.15 with new branch" vs
"pre-PT-P.15 unconditional add of zero" — both
should produce bit-identical pixels.

### 6.3 Optional: emissive-only scene

A scene with ONLY emissive surfaces (every material's
`emissionStrength > 0`, no light sources, no
non-emissive geometry) verifies that the predicate
correctly fires `true` on every hit. The §2.1 byte-
identity guarantee covers this case via §6.1's
fixture, but a dedicated emissive-only scene would
isolate the path more cleanly. NOT required.

### 6.4 Runner integration

`tools/verify_cuda_host.py`'s
`render-pathtrace` command exercises §6.1
automatically. No runner update needed; the empirical
PPM byte-identity check is a manual operator step
once a CUDA host is available.

---

## 7. Out-of-scope (deferred to later PT-P.x slices)

The following items from
`docs/PATH_TRACER_POLISH_PLAN.md` are explicitly NOT
part of this task:

- §4.1 RNG stability (key-mix collision audit;
  changes every `pathtrace_spp_*.ppm`
  byte-exactly).
- §4.7 Firefly clamp placeholder (default-off
  field on `PathTraceConfig` + matching kernel
  guards on BOTH the CUDA and OptiX path-trace
  raygens).

The OptiX path-tracer's emission support (the
programs at `OptixPrograms.cu:817..` do NOT consume
`MaterialParams::emissionColor` /
`emissionStrength`) is also explicitly deferred — a
future symmetric polish can wire emission into the
OptiX path tracer using the same `is_emissive`
predicate. Doing it here doubles the slice's blast
radius.

The `is_emissive` predicate's other potential
consumers (`CudaTestKernel.cu`'s `--render` direct-
lighting branch, `OptixPrograms.cu`'s flat-shading
+ textured-material branches) are similarly
deferred — applying the predicate there is a
smaller follow-up slice once §4.5 lands.

PT-P.14 (this task definition) and PT-P.15 (the
implementation slice) are the only PT-P.x slices
currently scheduled. After PT-P.15 lands, the
operator chooses the next polish item from the
plan's §4 list (§4.1 RNG stability or §4.7
firefly clamp), or pivots to a different polish
arc / triggers the CUDA-host verification run that
flips the BLOCKED rows from PT-P.4 / PT-P.7 /
PT-P.10 / PT-P.13 to PASS.

---

## 8. Why §4.5 is the safest viable next slice

Five reasons (mirroring the PT-P.5 / PT-P.8 / PT-P.11
structure):

### 8.1 PT-P.13 audit verdict was clean

`docs/PATH_TRACER_POLISH_ENV_FALLBACK_AUDIT.md` §7
records overall PASS, zero REPAIR items, BLOCKED
rows carried forward to a CUDA-host run. The path
tracer is in a known-good baseline post-PT-P.12.

### 8.2 The predicate is provably equivalent on the common path

For non-emissive materials, the pre-slice arithmetic
adds `(0, 0, 0) * throughput + radiance`, which is
exactly `radiance` (IEEE-754 addition of zero is
exact). The post-slice arithmetic SKIPS the entire
block, leaving `radiance` unchanged. The two paths
produce bit-identical pixels for any non-emissive
hit. No floating-point reasoning is required beyond
"add zero is exact".

For emissive materials, the post-slice path enters
the guarded block and executes the IDENTICAL
multiply-add sequence as pre-slice. Same registers,
same FMA opportunities, same rounding. Bit-identical
pixels.

### 8.3 No new pattern is required

The `RR_HD inline bool` predicate shape is already
established by `device_texture_view_valid` in
`src/cuda/CudaTexture.cuh:64`. The `is_emissive`
helper follows the same style — same return type,
same `RR_HD inline` qualifier, same `[[nodiscard]]`
attribute, same single-expression body. No operator
confusion about the convention.

### 8.4 The CUDA path tracer is the only path-tracer-side consumer

The OptiX path tracer programs do not consume emission
today. The `--render` and OptiX flat-shading branches
DO consume emission, but they are direct-lighting
shaders, not path tracers. PT-P.15's blast radius is
exactly one kernel function (`k_pathtrace_sample`'s
emission block), which is the smallest viable
kernel-side slice in the PT-P.x cadence.

### 8.5 The branch is uniform per-warp

`MaterialParams` is shared by every pixel hitting
the same surface (the `material_for(...)` lookup is
indexed by `hit.material_index`, which is constant
per primitive). Every thread in a warp that hits the
same primitive reads the same `m`, evaluates
`is_emissive(m)` to the same boolean, and takes the
same branch. There is no warp divergence; the
predicate is essentially free on the device after
constant-folding.

---

## 9. Reference: the planned PT-P.15 emission-block diff

For an operator reading the PT-P.15 commit, the
expected `CudaPathTracer.cu` patch is:

```diff
         const auto m = material_for(hit.material_index,
                                     scene.materials,
                                     scene.material_count);

-        // Emission: the hit acts as a light source. Add its
-        // contribution before generating the next bounce so even
-        // bounce == max_bounces - 1 picks it up.
-        const Vec3 emission =
-            Vec3{m.emissionColor.x * m.emissionStrength,
-                 m.emissionColor.y * m.emissionStrength,
-                 m.emissionColor.z * m.emissionStrength};
-        radiance = radiance + Vec3{throughput.x * emission.x,
-                                   throughput.y * emission.y,
-                                   throughput.z * emission.z};
+        // PT-P.15: short-circuit the emission add on non-
+        // emissive surfaces. ...
+        if (rr::material::is_emissive(m)) {
+            // Emission: the hit acts as a light source. Add its
+            // contribution before generating the next bounce so even
+            // bounce == max_bounces - 1 picks it up.
+            const Vec3 emission =
+                Vec3{m.emissionColor.x * m.emissionStrength,
+                     m.emissionColor.y * m.emissionStrength,
+                     m.emissionColor.z * m.emissionStrength};
+            radiance = radiance + Vec3{throughput.x * emission.x,
+                                       throughput.y * emission.y,
+                                       throughput.z * emission.z};
+        }
```

The existing six-line emission block becomes a six-
line guarded block, with one new comment paragraph +
one `if` wrapper + matching brace. Net diff size in
the kernel: ~10 added, ~7 deleted, depending on
exactly how the implementer indents the block body.

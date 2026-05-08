# Path Tracer Next Event Estimation — Task Definition (NEE.1)

Date: 2026-05-07.
Branch: `relativity-core-v1`.

Plan source:
- `docs/PATH_TRACER_POLISH_PLAN.md` §"Out of scope" item
  ("No direct-light sampling (NEE)") + §"Master #16
  follow-up" row;
- `docs/PATH_TRACER_POLISH_AUDIT.md` arc-end audit
  (closes the §4 polish arc; the next natural step
  is a feature-breadth slice);
- `docs/FIREFLY_CLAMP_CLI_AUDIT.md` §"Sub-arc closure"
  (firefly-clamp polish closed; the next slice is
  free to start a new sub-arc);
- `docs/BUILD_PLAN.md` "Next stage" §"feature breadth"
  bullet ("direct-light sampling (NEE)").

Mode: **documentation only.** No source code is modified
by this task definition. The task is the spec; the next
slice (the implementation) ships the diff.

This file is a fully self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's
reasoning.

---

## 0. One-paragraph summary

The path tracer (`src/cuda/CudaPathTracer.cu`'s
`k_pathtrace_sample` and `src/optix/OptixPrograms.cu`'s
`__raygen__pathtrace`) is emission-only today: it adds a
Lambert bounce's contribution **only** when a ray happens
to land on an emissive surface or the environment.
Explicit point + directional lights uploaded through
`GpuScene::upload_lights` are passed through `LaunchParams`
but **never sampled** by the path-trace raygens (see the
explicit no-MIS-yet comment at `CudaPathTracer.cu:26` and
`PathTracer.h:140-146`). This is correct but slow: small
bright point lights converge with very high variance because
the only way they enter the integral is through random hits.

NEE.1 adds **explicit direct-light sampling at each bounce
vertex** so the path tracer evaluates a deterministic
contribution from every uploaded `Light` instead of waiting
for emission to randomly land. Scope is the smallest viable
v1: **Point + Directional** lights only (the two non-
PLACEHOLDER `LightType` values per `src/lighting/Light.h`),
**one direct-light sample per bounce vertex**, **no MIS**
(the existing emission term and the new NEE term are summed
naively for v1 — double-counting at very short distances is
accepted as a known v1 limitation; MIS is a future slice).
The whole feature is gated on a default-off
`enable_nee = false` config field so the byte-identity
invariant holds for all existing scenes / fixtures /
golden images.

---

## 1. Purpose

**Improve direct-lighting convergence for point and
directional lights at low spp.**

Today's emission-only integrator produces visibly noisy
output at low samples-per-pixel for any scene with
non-extended (point) or far-field (directional) light
sources, because the random-bounce loop has near-zero
probability of landing on an infinitesimal point light
and zero probability of "hitting" a directional source
(directional sources have no surface to hit). The only
illumination the integrator currently sees from those
lights is whatever bleeds in through the environment-
fallback term — which is none, because point /
directional lights aren't part of the environment lookup.

Adding NEE lets the integrator evaluate
`light_contribution(vertex, light)` deterministically at
every bounce vertex: for a point light, that's
`color * intensity / r²` modulated by the BRDF and a
shadow-ray visibility term; for a directional light,
it's `color * intensity` modulated by the BRDF and a
shadow-ray to the light's direction at "infinity"
(typically a large `t_max`, not literally infinity).
The expected outcome on a point-light-driven Cornell-
box-style fixture at 16 spp is a **substantial visible
noise reduction in the directly-lit regions**; indirect
illumination noise is unchanged (NEE does not touch the
diffuse-bounce term, only adds a parallel direct term
at each vertex).

**Non-goal — bias.** NEE is unbiased on its own when
combined with a careful "do not also count direct
emission via the random bounce" rule (the canonical
trick is to *skip* the emissive add at any vertex
reached by a bounce that happened from a vertex that
already did NEE, to avoid double-counting). NEE.1
deliberately does **not** add this skip: the existing
emission add stays exactly as it is. The result is
**slightly biased upward** in scenes where a point /
directional light is also represented as an emissive
surface (it's not — point + directional lights have no
mesh), so in practice for v1 the double-count window is
empty: point + directional lights produce contributions
*only* through the new NEE term, never through the
emission term, so summing them gives the correct
estimator. The v1 brief is written so this invariant
holds without code changes; MIS is reserved for a future
slice that adds area-light support, where the double-
count concern becomes real.

---

## 2. Initial scope

### 2.1 Light types

- **`LightType::Point` (== 0).** Sample a shadow ray from
  the bounce vertex toward `light.position`. On the
  CUDA path:

      const float3 to_light  = light.position - hit_position;
      const float  r2        = dot(to_light, to_light);
      const float  r         = sqrtf(r2);
      const float3 wi        = to_light * (1.0f / r);
      const float  cos_theta = fmaxf(0.0f, dot(normal, wi));
      const float  vis       = trace_shadow_ray(hit_position, wi, /*tmax=*/r - kShadowEps);
      const float3 li        = light.color * light.intensity / r2;
      direct_radiance       += throughput * brdf_albedo * cos_theta * li * vis;

  (`brdf_albedo == material.diffuse / pi`; the `1/pi`
  is the Lambert BRDF normalisation; `kShadowEps`
  prevents self-intersection at the receiver and at the
  light position — typically `1e-3f` along `wi`).

- **`LightType::Directional` (== 1).** The "to_light"
  vector is `-normalize(light.direction)` (mirrors the
  existing `__closesthit__ shading_mode == 2` convention
  at `OptixPrograms.cu:419-435`); shadow ray `tmax`
  is a large finite value (e.g. `1e30f`); no `1/r²`
  attenuation. Otherwise the structure mirrors the
  point-light case.

- **`LightType::Area` (== 2 PLACEHOLDER) — DEFERRED.**
  `Light.h` lines 28-31 mark Area as a placeholder
  (the geometry payload is not yet wired through to
  the BLAS). NEE.1 explicitly skips any light whose
  `type == Area`; a future slice introduces uniform
  area-sampling + a real area-light Jacobian +
  optional MIS-with-emission.

- **`LightType::Environment` (== 3 PLACEHOLDER) —
  DEFERRED.** Environment-light sampling needs an
  importance map (or at minimum a uniform-sphere
  sampler); without one the contribution is identical
  to the existing miss-side environment add and adding
  it as a "NEE term" would just double-count it.
  Deferred to the same future slice that introduces
  importance sampling for environment IBLs.

### 2.2 Sample budget per vertex

- **One direct-light sample per bounce vertex** in v1.
  When `light_count == 0` the kernel skips the NEE
  branch entirely (no work done; matches the
  default-off byte-identity guarantee). When
  `light_count >= 1` the kernel picks one light by
  uniform random index `li = rand_int_uniform(rng, 0,
  light_count)` and *divides the contribution by the
  selection PDF `1.0f / light_count`* (i.e. multiplies
  by `light_count`) so the estimator stays unbiased
  with respect to the count.

  The "first-bounce-only" simplification proposed in
  the user's NEE.1 brief is **rejected** for v1: it
  costs a single additional shadow ray per vertex (a
  rounding error compared to the closest-hit traversal)
  and the no-double-count argument in §1 only holds
  if NEE runs at every vertex. Sampling at every
  vertex is also what the existing
  `__closesthit__ shading_mode == 2` direct-lighting
  branch does, so the two paths stay structurally
  parallel.

### 2.3 Shadow rays

- Reuse the existing shadow-ray infrastructure:
  - **CUDA backend.** `CudaPathTracer.cu` does **not**
    have a shadow-ray helper today (the path tracer is
    emission-only; the only shadow rays in the CUDA
    tree live in `k_render_direct_lighting`). NEE.1
    introduces a `__device__ float trace_shadow_ray(
    float3 ro, float3 rd, float tmax, ...)` that reuses
    the existing closest-hit traversal (`intersect_*`
    helpers) but returns early on the first hit
    (any-hit semantics — we only need 0/1 visibility,
    not the actual `t`). The helper lives next to
    `k_pathtrace_sample` in the same TU.
  - **OptiX backend.** `OptixPrograms.cu`'s
    `__raygen__pathtrace` reuses the existing
    `__miss__shadow` + `__closesthit__shadow` programs
    (already used by `shading_mode == 2` at
    `OptixPrograms.cu:436` and `:489`). The shadow-ray
    payload is a single `unsigned int` flag; no new
    SBT records, no new pipeline configuration.

### 2.4 No MIS

The brief explicitly DEFERS Multiple Importance Sampling.
v1 sums the existing emission term + the new NEE term
naively. The bias argument in §1 hinges on point +
directional lights having no mesh representation, so
the double-count window is empty for v1's two supported
light types. When area lights land, MIS is a co-
requisite (their geometry can also be hit by random
bounces), and the area-light slice owns introducing it.

### 2.5 Default-off gate

A new config field `bool enable_nee = false` on
`PathTraceConfig` (mirrors `firefly_clamp = 0.0f` from
PT-P.21+24). The kernel guard is the simplest possible
shape:

    if (enable_nee && light_count > 0) {
        direct_radiance += sample_direct_lights(...);
    }

When `enable_nee == false` the NEE branch is **never
entered**, the existing per-pixel arithmetic is
**byte-identical** with the pre-NEE build, and the
existing fixture / golden-image goldens hold without
re-baking. (See §6 for the IEEE-754 byte-identity
argument.)

### 2.6 CLI exposure

A new modifier flag `--enable-nee` (no value; presence
sets the bool to true). Mirrors the
`--firefly-clamp <value>` shape from
`docs/FIREFLY_CLAMP_CLI_TASK.md` §1, but boolean
(no value parsing). Wired through `Config::enable_nee`
to both `run_render_pathtrace` (CUDA dispatcher) and
`run_render_optix_pathtrace` (OptiX dispatcher).
**Other actions ignore it** (same modifier-flag
discipline as `--beta` and `--firefly-clamp`).

---

## 3. Files likely involved

### 3.1 Headers (signatures touched)

- **`src/pathtracer/PathTracer.h`** — append
  `bool enable_nee = false;` to `PathTraceConfig`
  with a doc-comment block matching the
  `firefly_clamp` precedent (lines 80-103). Note
  that the field is read by both backends'
  path-trace raygens; default `false` matches the
  pre-NEE behaviour exactly.
- **`src/cuda/CudaPathTracer.cu`** — `render`
  signature acquires an `enable_nee` parameter
  (the kernel itself takes a `bool` launch arg).
- **`src/optix/OptixRenderer.h`** — analogous
  addition to the path-trace dispatcher signature
  (mirrors the firefly-clamp wiring at PT-P.24).
- **`src/optix/OptixLaunchParams.h`** — append
  `bool enable_nee = false;` to the launch-params
  struct. The `lights` + `light_count` +
  `enable_shadows` fields are **already present**
  (lines 166-198) — no new upload plumbing.

### 3.2 Kernels (logic touched)

- **`src/cuda/CudaPathTracer.cu`** —
  - Add `__device__ float trace_shadow_ray(...)`
    helper next to the existing `intersect_*`
    helpers.
  - Add `__device__ float3 sample_direct_lights(
    const Light* lights, int light_count,
    float3 hit_position, float3 normal,
    float3 brdf_albedo, Rng& rng)` helper that
    iterates one uniform-random light, evaluates
    its contribution per §2.1, multiplies by
    `light_count` for the PDF, and returns the
    sum.
  - Inside the bounce loop, *between* the
    emission add and the cosine-bounce sampling,
    insert:

        if (params.enable_nee && light_count > 0) {
            radiance += throughput * sample_direct_lights(
                lights, light_count, hit_position, normal,
                brdf_albedo, rng);
        }

    The placement is the canonical NEE-at-vertex
    shape: emission is whatever the surface itself
    contributes; NEE is what the lights *push to*
    the surface; the next bounce carries the
    indirect term.
- **`src/optix/OptixPrograms.cu`** —
  - `__raygen__pathtrace`: insert the same
    `enable_nee` branch in the same position. The
    helper code is **structurally identical** to
    the existing `__closesthit__ shading_mode == 2`
    direct-lighting branch at lines 393-527 — it
    can in fact be factored to a shared
    `__device__` helper, **but NEE.1 deliberately
    does not refactor**. The two branches stay
    independent; the existing `shading_mode == 2`
    branch is on the **must-not-touch** list (§4).
  - The shadow-ray `__miss__shadow` /
    `__closesthit__shadow` programs are reused
    as-is — already in the SBT for `shading_mode
    == 2`.

### 3.3 CLI / config (modifier flag)

- **`src/core/Config.h`** — append
  `bool enable_nee = false;` to `Config` with a
  doc-comment matching the `firefly_clamp` field
  precedent (lines 47-59).
- **`src/core/CommandLine.cpp`** — add an
  `else if (a == "--enable-nee")` arm in the
  parser loop next to the existing
  `--firefly-clamp` arm. No value parsing
  (boolean flag). Add the flag to the `--help`
  text alongside `--firefly-clamp`.
- **`src/main.cpp`** — wire `cfg.enable_nee` into
  the two pathtrace dispatchers (CUDA +
  OptiX). Other actions silently ignore it.

### 3.4 Tests

- **`tests/pathtracer_tests.cpp`** (existing) —
  add a `test_nee_default_off_byte_identity`
  fixture that renders the existing point-light
  scene fixture twice (once with the pre-NEE
  build via direct config-field elision, once
  with the post-NEE build at default
  `enable_nee == false`) and asserts byte-
  identical pixels. (The *cross-build* identity
  is what we cannot test in-tree; the *in-build*
  identity test asserts that the NEE branch is
  truly inert at default — the two cases that
  stay unchanged are: `enable_nee == false`
  (skipped entirely) and `enable_nee == true`
  with `light_count == 0` (skipped due to the
  `light_count > 0` guard). The test renders
  both and compares to the
  `enable_nee == false` baseline.)
- **`tests/pathtracer_nee_tests.cpp`** (new, if
  the implementor prefers a separate TU) — host-
  side unit tests for the new helpers'
  signatures (the math itself is GPU-side and
  audit-host DEFERRED; what we *can* test
  host-side is the helper-stub linkage on the
  audit host's `RR_ENABLE_CUDA=OFF` build).
- **`tests/cli_tests.cpp`** (existing) — add
  `test_cli_enable_nee_flag_sets_config_bool`
  asserting `Config::enable_nee == true` after
  parsing `--enable-nee` and `false` otherwise.

### 3.5 Docs

- **`docs/BUILD_PLAN.md`** — append the closing
  entry per the per-slice cadence. The shape
  matches the firefly-clamp CLI's BUILD_PLAN
  entry (commit `b5da850` task / `a3b43b4`
  impl / `7386418` audit pattern).

---

## 4. What must not be touched

The following code paths are **structurally adjacent**
to NEE but explicitly **out of scope** for NEE.1.
Touching any of them in the NEE.1 implementation slice
is grounds for an audit REPAIR.

### 4.1 The existing `shading_mode == 2` direct-lighting branch

`src/optix/OptixPrograms.cu` lines 393-527 already
contain a Point + Directional + Environment direct-
lighting implementation, used by the
`--render-optix-direct-lighting` action. NEE.1's
helper code is structurally identical, but the brief
**deliberately does not refactor** the two branches
into a shared helper. The reasons:

- The `shading_mode == 2` branch is a one-shot
  primary-ray-only direct-lighting renderer (no
  bounce loop); the NEE branch is a *per-vertex*
  helper called inside the bounce loop. The two
  contracts differ on payload state, on `radiance`
  accumulator semantics, and on how shadow misses
  are interpreted (the primary-ray version returns
  black; the NEE version simply doesn't add the
  light's contribution).
- Refactoring requires touching 134 lines of
  existing PASS-tested OptiX code (lines 393-527)
  whose audit trail (`STAGE_11_AUDIT.md`,
  `OPTIX_GAP_A_*`) would need to be re-run. That
  ratio (134-line refactor : ~30-line NEE add) is
  the wrong shape for an incremental slice.
- A future "direct-lighting helper consolidation"
  slice can do the refactor cleanly once both call
  sites are stable and tested.

NEE.1 implements its helper **independently**, in the
same TU, without modifying the `shading_mode == 2`
path.

### 4.2 The emission handling (PT-P.15)

`src/cuda/CudaPathTracer.cu` and
`src/optix/OptixPrograms.cu` both have an
`is_emissive(material)` check + an emission-add at
the bounce vertex (PT-P.15's contribution; commit
`dd98d90`). The emission add stays **exactly as it
is**. NEE.1 adds a *parallel* term, not a
*replacement* term. The "no double-count" argument in
§1 hinges on this: point + directional lights have no
emissive mesh, so the emission term and the NEE term
sample disjoint contributions.

When (in a future slice) area lights land, *that*
slice will need to either skip the emission add at
NEE-active vertices or introduce MIS. NEE.1 does
neither — that's an area-light concern.

### 4.3 The RNG (PT-P.18)

`src/pathtracer/Rng.h`'s `make_pixel_rng(x, y,
sample_index, seed)` SplitMix64 mix (PT-P.18; commit
`d2af0c5`) is **not modified**. The NEE branch
consumes the same per-pixel RNG used by the bounce
loop — when `enable_nee == true` it draws **two
extra `Rng::next_uint`s** (one for the light index,
one for the offset packed into the shadow ray's
`tmin`/`tmax` if needed). When `enable_nee ==
false` it draws **zero** extra calls and the RNG
sequence is bit-identical to the pre-NEE build.
The kernel must place the NEE `rng.next_uint()` calls
**inside the `enable_nee` guard** (not outside) so
the default-off RNG sequence is preserved exactly.

### 4.4 The firefly-clamp wiring (PT-P.24)

`src/cuda/CudaPathTracer.cu` lines 251-255 and the
matching OptiX guard apply the per-channel `fminf`
clamp on the *per-sample* radiance just before the
accumulator add (PT-P.24; commit `0a06d0d`). The
NEE contribution is added to `radiance` *before*
the firefly-clamp, so the clamp applies to the sum
of the emission + NEE + indirect terms. This is the
correct ordering — clamping after the sum bounds
the worst-case per-sample variance, which is what
the firefly-clamp is for. **The clamp position is
not moved**; the NEE add is inserted *upstream* of
it.

### 4.5 Light upload / `GpuScene::upload_lights`

`src/gpu/GpuScene.cpp::upload_lights` already
allocates a device-resident `Light*` array and
writes its pointer + count into the launch params
(`OptixLaunchParams.h:179-180`). NEE.1 **reuses this
upload exactly as-is** — no new fields, no new
strides, no new alignment requirements. The same
`Light` POD layout used by `--render-direct-lighting`
and `--render-optix-direct-lighting` is consumed
by the NEE branch.

### 4.6 Existing fixtures / golden images

Every existing path-tracer fixture (`tests/`,
`scenes/`) was authored with the pre-NEE
emission-only integrator. With `enable_nee = false`
the new code path is inert; with `enable_nee =
true` *no existing fixture is rendered with that
flag*, so all goldens hold without re-baking. The
NEE.1 implementation slice **does not re-bake any
golden image**.

### 4.7 Cross-backend convergence

The NEE math (§2.1) is **identical** between the
two backends. Backend divergence — for example
choosing different shadow-ray epsilons, or one
backend dividing by `light_count` and the other not
— is the failure mode that the audit
(`PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_AUDIT.md`
established as a structural concern) is most
sensitive to. The implementor **must** land both
backends in the same commit (the PT-P.24 atomic-
landing pattern); a half-landed NEE that ships
CUDA-only or OptiX-only is grounds for audit
REPAIR.

---

## 5. PASS criteria

A NEE.1 implementation slice ships PASS when **all**
of the following hold. Items marked DEFERRED are
runtime-host concerns audited but not enforced on
the audit host (`docs/CUDA_HOST_VERIFICATION_PLAN.md`
fallback pattern).

### 5.1 Build green on both audit-host configs

- `cmake -S . -B build -DRR_ENABLE_CUDA=OFF
  -DRR_ENABLE_OPTIX=OFF && cmake --build build
  --target rr_renderer rr_tests` → exit 0, no
  warnings beyond the pre-NEE baseline.
- `cmake -S . -B build_cuda -DRR_ENABLE_CUDA=ON
  -DRR_ENABLE_OPTIX=OFF` configures successfully on
  a CUDA host (DEFERRED on the audit host —
  expected `find_package(CUDAToolkit)` BLOCKED
  fallback per the firefly-clamp impl precedent;
  the slice's BUILD_PLAN entry must document the
  reconfigure).

### 5.2 Default-off byte-identity proof

`enable_nee == false` (the default for *all* existing
callers, since no fixture passes the new flag)
produces **byte-identical** per-pixel output with
the pre-NEE build. The proof has two halves:

- **Static.** The new field
  `PathTraceConfig::enable_nee` defaults to `false`
  and is the *only* way the NEE branch is entered.
  The kernel guard is `if (enable_nee && light_count
  > 0)`; both operands are zero / false at default,
  so the branch is *never executed*. No kernel-side
  state mutates: `radiance`, `throughput`, the RNG
  (§4.3), and the firefly-clamp (§4.4) all read /
  write the same values they did pre-NEE. The
  IEEE-754 byte-identity argument is the same as
  PT-P.21 / PT-P.24's: with the branch un-executed,
  no FP add ever touches the accumulator at default.
- **Dynamic.** `tests/pathtracer_tests.cpp::
  test_nee_default_off_byte_identity` renders the
  existing emissive-sphere fixture and the existing
  point-light fixture (the latter has `light_count
  > 0` so it exercises the second guard half).
  Both must match the pre-NEE golden bit-exact.

### 5.3 CLI flag end-to-end

- `--enable-nee` parses successfully, sets
  `Config::enable_nee = true`. Absent flag leaves
  it `false`.
- `Config::validate()` accepts both states
  (boolean — nothing to validate).
- `--help` lists the new flag in the modifier-flags
  section alongside `--beta` and `--firefly-clamp`.
- `--enable-nee` on a non-pathtrace action is
  *silently ignored* (the dispatcher for that
  action does not read `cfg.enable_nee`); **no
  error message** for unused modifier flags
  (matches the `--firefly-clamp` and `--beta`
  precedents).

### 5.4 Light-array consumption invariants

- The NEE branch reads `lights` and `light_count`
  from the launch params (`OptixLaunchParams.h:179-
  180` for OptiX; the CUDA equivalent is the
  kernel's existing launch-arg passthrough).
  `lights == nullptr` AND `light_count == 0` is
  the no-lights-uploaded contract; the
  `light_count > 0` guard is the canonical check.
- Reading `lights[li]` for `0 <= li < light_count`
  is the only access pattern; no out-of-bounds
  reads, no past-the-end stride.
- `light.type == Area` AND `light.type ==
  Environment` are silently skipped (they
  contribute zero in the v1 scope per §2.1). The
  skip is a `continue` in the per-light loop *if*
  the implementor opts for "iterate all lights"
  rather than "uniform pick one"; per §2.2 v1
  picks one uniformly, so the skip path is
  "if the picked light is Area / Environment, the
  NEE term is zero this sample" — that's a
  correct estimator (zero-contribution samples
  are valid in Monte Carlo) but adds variance.
  An alternative is "filter the lights array to
  Point + Directional only at upload time"; this
  is **out of scope** for NEE.1 (it touches
  `GpuScene::upload_lights`, which is on the
  must-not-touch list per §4.5). NEE.1 takes the
  zero-contribution-when-picked approach.

### 5.5 Atomicity invariant

- Both backends' kernel changes (§3.2) land in
  the **same commit**. This is the PT-P.24
  atomic-landing pattern: a half-landed NEE
  that ships CUDA-only or OptiX-only would
  produce silently divergent outputs and is
  grounds for audit REPAIR (§4.7).

### 5.6 Diff-size budget

- Source diff (excluding doc-comments / brief /
  audit) target: **~120 lines** across all files.
  Breakdown:
  - `Config.h` + `CommandLine.cpp` + `main.cpp`:
    ~30 lines (mirrors firefly-clamp CLI's diff).
  - `PathTracer.h`: ~5 lines (one field).
  - `OptixLaunchParams.h`: ~3 lines (one field).
  - `CudaPathTracer.cu`: ~40 lines (helper +
    integration).
  - `OptixPrograms.cu`: ~40 lines (helper +
    integration).
- Diff-size deviations are acceptable when they
  are entirely doc-comment text matching the
  PT-P.x precedent on inline documentation
  density; flag in the BUILD_PLAN entry's
  "Diff size deviation note" subsection per
  the established pattern.

### 5.7 Test expansion

- `tests/pathtracer_tests.cpp` adds the byte-
  identity test (§5.2 dynamic).
- `tests/cli_tests.cpp` adds the CLI parsing
  test (§5.3).
- All existing tests pass without modification
  (no golden re-bake; §4.6).

### 5.8 No-touch invariants verified

- `git diff --stat` for the implementation slice
  shows **zero** changes to:
  - `src/optix/OptixPrograms.cu` lines 393-527
    (the `shading_mode == 2` branch — §4.1).
  - `src/pathtracer/Rng.h` and the RNG mix
    (§4.3).
  - `src/gpu/GpuScene.cpp::upload_lights` (§4.5).
  - Any file under `tests/goldens/` or
    `scenes/` (§4.6).
- The implementation slice's BUILD_PLAN entry
  documents this verification with the same
  `git diff --stat` ✶ output filtering shape
  used by the firefly-clamp CLI impl entry.

---

## 6. Runtime-deferred CUDA / OptiX checks

The audit host has no NVIDIA GPU and no CUDA Toolkit
(per `docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md`).
The following checks are **DEFERRED to a CUDA-equipped
host** and recorded in the slice's audit document
under a "Runtime-deferred" section per the established
pattern (PT-P.{15,18,21,24,25} audits and
`FIREFLY_CLAMP_CLI_AUDIT.md` §"Runtime-deferred").

### 6.1 Visible noise reduction at low spp

**Setup.** A scene with one small bright point light
(e.g. `light.intensity = 50.0f`, `light.position` 1.5
units above a Lambert plane), at 16 spp.

**Procedure.**
- Render with `--enable-nee` OFF; record
  per-pixel variance estimate (mean of squared
  residuals to the converged 4096 spp ground
  truth) over the directly-lit region.
- Render with `--enable-nee` ON, otherwise
  identical config; record the same variance
  estimate.

**Pass.** The directly-lit-region variance with
NEE ON is **at least 4× lower** than with NEE OFF
(equivalent to a 2× reduction in std-dev — the
expected magnitude for a single direct sample per
vertex on a single dominant light). The exact
factor is informational; the gate is "substantial
visible reduction" — a side-by-side at 16 spp
should be clearly less noisy in the directly-lit
region.

### 6.2 Cross-backend convergence

**Setup.** The same point-light fixture as §6.1.

**Procedure.** Render with `--render-pathtrace
--enable-nee` (CUDA backend) and
`--render-optix-pathtrace --enable-nee` (OptiX
backend) at 4096 spp.

**Pass.** Per-pixel mean-squared-error between the
two backends' outputs is below the existing
cross-backend convergence tolerance used by the
PT-P.24 firefly-clamp wiring runtime check
(typically `< 1e-4` MSE on linear-space RGB; the
exact tolerance lives in `tools/verify_cuda_host.py`
or a future cross-backend harness — the slice's
audit may need to introduce one if it does not yet
exist).

### 6.3 Default-off bit-identity (runtime)

**Setup.** Any existing path-tracer fixture (§4.6).

**Procedure.** Render with the pre-NEE build (`git
checkout` the commit before NEE.1) and with the
post-NEE build at default `enable_nee == false`.

**Pass.** Bit-identical PPM output. This is the
runtime-host equivalent of §5.2's static / in-build
byte-identity proof — the runtime check rules out
any toolchain-level non-determinism that the static
argument could in principle miss (it shouldn't, given
PT-P.24's precedent, but the check is cheap and
recorded for completeness).

### 6.4 No-light scene smoke test

**Setup.** An existing scene with `light_count == 0`
(e.g. `scenes/tex_arc_smoke.rrscene` if it has no
light array; otherwise a fresh scene).

**Procedure.** Render with `--enable-nee`. The
`light_count > 0` guard fires; the NEE branch is
never entered.

**Pass.** Output is bit-identical to the same scene
rendered without `--enable-nee`. (This catches a
guard-inversion bug; cheap and worth running.)

### 6.5 Directional-light smoke test

**Setup.** A scene with a single
`LightType::Directional` light pointing straight
down (`direction = (0, -1, 0)`) over a Lambert
plane.

**Procedure.** Render with `--enable-nee` at 16
spp and at 4096 spp.

**Pass.** The 16 spp render is visibly smoother than
the same scene with `--enable-nee` OFF (the OFF
case will be very dark — directional lights have no
mesh and emit nothing through random bounces, so
the emission-only integrator sees zero light from
them). The 4096 spp render converges to a uniform
shaded plane (cosine-modulated by the surface
normal).

---

## 7. Out-of-scope / sequencing rationale

The following items are **explicitly deferred** to
future slices. Each has a one-line "why later" so the
brief is self-contained on the question of why NEE.1
isn't bigger.

| Deferred item                          | Why later                                                              |
| -------------------------------------- | ---------------------------------------------------------------------- |
| MIS (Multiple Importance Sampling)     | Co-requisite with area lights (§2.4); v1 has no double-count window.   |
| `LightType::Area` support              | `Light.h:28-31` PLACEHOLDER — geometry payload not wired through BLAS. |
| `LightType::Environment` IS sampling   | `Light.h:30-31` PLACEHOLDER — needs an importance map.                 |
| Per-bounce *multiple* light samples    | One sample per vertex is the smallest viable v1; revisit at MIS time.  |
| Russian-roulette path termination      | Independent feature; orthogonal to NEE; see PT-polish-plan §"future".  |
| First-bounce-only NEE simplification   | Rejected per §2.2; cost difference is rounding error.                  |
| Shadow-helper consolidation across backends | Refactors `shading_mode == 2` (must-not-touch §4.1).               |
| Light-array filtering at upload time   | Touches `GpuScene::upload_lights` (must-not-touch §4.5).               |
| Re-baking goldens for NEE-on configs   | No existing fixture passes `--enable-nee` (§4.6).                      |

The order between NEE.1's implementation slice and the
deferred items is the operator's call. The natural next
slice after NEE.1 ships + audits is either (a) MIS +
area-light support (the largest single feature on the
deferred list, and the one that closes the bias /
double-count concern raised in §1), or (b) a different
sub-arc entirely (relativistic-perception integration
into the path tracer, non-diffuse materials,
multi-mesh upload). NEE.1 does not constrain that
choice.

---

## 8. Slice cadence

Per the established `task → impl → audit` cadence:

- **NEE.1 (this file).** Documentation only. No
  source code modified. Commit message: `docs:
  next event estimation task definition (docs only)`.
- **NEE.2.** Implementation. Adds the new field +
  CLI flag + kernel branches + tests + BUILD_PLAN
  entry per §3 / §5. Atomic landing of CUDA + OptiX
  per §5.5. Commit message: `feat: --enable-nee
  next event estimation (impl)`.
- **NEE.3.** Audit. Walks the §5 PASS criteria,
  records DEFERRED runtime checks per §6, sets
  arc verdict (PASS / REPAIR / BLOCKED). Commit
  message: `docs: next event estimation audit
  (docs only)`.

Each commit is independently reviewable; each
landing point in the cadence is a stable rollback
target.

---

## 9. Open questions for the implementor

The following are **not** blockers — they are
choices the implementor makes during NEE.2 and
records in the BUILD_PLAN entry. The brief is
opinionated on each but doesn't enforce; deviations
are acceptable when justified.

1. **`kShadowEps` value.** §2.1 suggests `1e-3f`
   along `wi` for both ends of the shadow ray.
   The existing `__closesthit__ shading_mode == 2`
   branch uses a specific value (cross-reference
   `OptixPrograms.cu` lines 436 and 489). NEE.2
   should *match that value exactly* unless there
   is a measurable failure case — the cross-backend
   convergence check (§6.2) is sensitive to
   epsilon mismatches.
2. **RNG-call placement (§4.3).** The brief mandates
   that the NEE `rng.next_uint()` calls live
   *inside* the `enable_nee` guard so the default-
   off RNG sequence is bit-identical. NEE.2's
   implementation must verify this with a focused
   unit test or a kernel-side `static_assert` on
   the call sequence (the latter is impractical;
   the former is the recommended path).
3. **Helper TU placement.** The brief proposes the
   shadow-ray + direct-light helpers live next to
   `k_pathtrace_sample` in `CudaPathTracer.cu` and
   in `__raygen__pathtrace`'s TU for OptiX. An
   alternative is a shared `src/pathtracer/
   DirectLight.h` with `RR_HD inline` helpers
   (matches the host/device-shared helper pattern
   established in PT-P.{15,18,24}). Either is
   acceptable; the brief recommends the latter
   for parity with the existing shared-helper
   discipline, *but* the `intersect_*` helpers it
   would call are not (yet) factored to
   host/device-shared form, so a clean shared
   helper requires a small additional refactor
   that is **explicitly DEFERRED** here. NEE.2
   takes the per-TU duplicate-helper approach
   for v1.
4. **`--enable-nee` flag spelling.** Alternative
   spellings considered: `--nee` (terse;
   abbreviation-heavy and may not be obvious to a
   first-time reader); `--direct-lighting`
   (collides with the `--render-direct-lighting`
   action name and would mislead operators);
   `--explicit-light-sampling` (verbose). NEE.2
   ships `--enable-nee` unless the implementor has
   a strong preference; deviation requires a one-
   sentence rationale in the BUILD_PLAN entry.

---

## 10. Sign-off

This task definition is complete and ready for the
NEE.2 implementation slice. The implementor should
not need to consult any document outside this brief
+ the four `must-not-touch` referents (§4.1 →
`OptixPrograms.cu:393-527`; §4.2 → PT-P.15 commit
`dd98d90`; §4.3 → PT-P.18 commit `d2af0c5`; §4.4 →
PT-P.24 commit `0a06d0d`) + the existing precedents
named inline (firefly-clamp CLI's
`docs/FIREFLY_CLAMP_CLI_TASK.md` §1, etc.) to ship
the change.

Mode reminder: **documentation only.** This file is
the spec; no source code is modified by the NEE.1
slice.

# Path Tracer Multiple Importance Sampling — Plan (MIS.1)

Date: 2026-05-07.
Branch: `relativity-core-v1`.
Plan source: post-NEE arc closure (the
twelve-slice cadence ending at commit
`827f5de`); the NEE arc's deferred design
question — how to handle the double-count
window when a future area-light slice lands —
is the trigger for this MIS plan.
Mode: documentation only. **No source code is
modified by this plan.** The plan is the
contract for MIS.{2..7}; the next slices ship
the diff.
Reference docs read for this plan:
- `docs/PATH_TRACER_NEE_AUDIT.md` (NEE.3
  audit — closes the CUDA NEE skeleton).
- `docs/PATH_TRACER_ENABLE_NEE_CLI_AUDIT.md`
  (NEE.6 audit — closes the CLI sub-arc).
- `docs/PATH_TRACER_ENABLE_NEE_HELP_REPAIR_AUDIT.md`
  (NEE.6 §11.1 REPAIR audit — closes the
  REPAIR list).
- `docs/BUILD_PLAN.md` (NEE arc entries).
- Source: `src/pathtracer/{DirectLight,Sampling,
  PathTracer,RNG}.{h,cuh,cpp}`,
  `src/cuda/CudaPathTracer.{cu,cuh}`,
  `src/optix/{OptixPrograms.cu,OptixRenderer.{cpp,h},
  OptixLaunchParams.h}`,
  `src/material/MaterialTypes.h`.

This file is a fully-self-contained design
brief for the MIS arc. Anyone picking up
MIS.{2..7} should be able to ship the arc
without re-deriving its reasoning. Pattern
mirrors `docs/PATH_TRACER_NEE_TASK.md` (the
canonical multi-slice design brief in this
repository).

---

## 1. Problem

### 1.1 Two estimators for direct lighting

The path tracer has two natural ways to find
light arriving at a surface point:

- **NEE** (Next Event Estimation; shipped
  NEE.{1..6}): at every bounce vertex, pick
  one light and compute its direct contribution
  to the surface (visibility-modulated, BRDF-
  modulated). Variance is low for small / hard
  light sources (delta lights, point lights);
  high for diffuse / large light sources where
  the chosen light direction is rarely the
  "important" direction at the surface.
- **BSDF sampling** (the existing diffuse
  bounce at `CudaPathTracer.cu:326-329` and
  `OptixPrograms.cu:1044-1046`): sample a
  direction proportional to the BSDF's
  importance (cosine-weighted for Lambert),
  trace a ray, and accumulate emission /
  environment if the ray hits a light or
  escapes. Variance is low for diffuse / large
  light sources where the BSDF distribution
  matches the surface's importance; high for
  small / hard sources that the random bounce
  rarely finds.

Both estimators are unbiased on their own.
**Summing them naively double-counts the
overlap region.** A path that NEE samples
explicitly AND that BSDF sampling produces by
chance contributes twice to the radiance
estimator.

### 1.2 The current v1 light-type scope avoids the problem

Per `PATH_TRACER_NEE_TASK.md` §1's deliberate
choice, NEE.{1..6} ships ONLY Point and
Directional lights. Both are delta lights:

- A Point light has no spatial extent (zero
  measure in the scene); BSDF sampling can
  never produce a direction that lands
  exactly on a point.
- A Directional light has no position (zero
  measure on the unit sphere); BSDF sampling
  can never produce a direction that exactly
  matches a Dirac-delta light direction.

So the BSDF sampler's contribution to delta
lights is zero with probability 1 — there is
no overlap to double-count. The NEE-only
estimator is unbiased; the BSDF-bounce
estimator finds zero light energy that NEE
also samples; the naive sum is correct.

### 1.3 The double-count window opens at area lights

The moment a future slice introduces area
lights (and the operator can author them in
`.rrscene` files), the situation changes:

- An area light HAS a mesh. The NEE branch
  samples a point on the area light's
  surface; the BSDF branch can also bounce a
  ray that happens to hit the area light's
  geometry.
- BOTH estimators contribute non-zero energy
  to the same light. Without MIS, the
  estimator double-counts every path the
  scene admits via both routes — a
  factor-of-two bias on direct lighting from
  area sources.
- This is the bias `PATH_TRACER_NEE_TASK.md`
  §1 explicitly reserved for "the future
  area-light slice", which becomes the area-
  light arc once MIS lands.

### 1.4 Why ship MIS BEFORE area lights

Three reasons, mirroring the firefly-clamp
arc's "field placeholder before kernel
wiring" cadence (PT-P.{20..22} shipped
`PathTraceConfig::firefly_clamp` as a
placeholder; PT-P.{23..25} wired the kernel):

1. **MIS is the architectural prerequisite.**
   The area-light arc cannot ship a correct
   integrator without it. Landing area-light
   geometry without MIS produces a known-
   biased renderer. Shipping MIS as a
   separate arc lets the area-light arc
   focus exclusively on geometry / sampling
   rather than splitting attention between
   two cross-cutting concerns.
2. **MIS at v1 (Point + Directional only) is
   a no-op at the math level**, so the
   default-off byte-identity contract is
   trivially preserved (§7.4). The arc can
   land the data model + helpers + integrator
   shape without ANY change to the existing
   renders.
3. **The existing helpers + PODs need
   semantic-only growth, not signature
   churn.** `DirectLightSample` already
   carries `pdf_inv`; the BSDF sampler
   already lives in `Sampling.h` next to
   `pdf_cosine_hemisphere`. Adding the
   solid-angle PDF semantics + the power
   heuristic + the MIS weight multiply
   touches a tight surface area.

### 1.5 What "MIS" means in this plan

Veach's Multiple Importance Sampling
combines N estimators, each with its own
sampling distribution, into a single
weighted estimator. The combined estimator
is unbiased iff the weights sum to 1 for
every reachable sample point. The **power
heuristic** (Veach 1995, §9.2.4) is the
standard weight choice with `β = 2`:

```
        n_i^β · p_i^β
w_i = -----------------
       Σ_j n_j^β · p_j^β
```

For the v1 case (one NEE sample + one BSDF
sample per bounce, both with equal sample
counts `n_i = n_j = 1`), this collapses to:

```
            p_i^2
w_i = -------------
       p_i^2 + p_j^2
```

where `p_i` and `p_j` are the two estimators'
PDFs evaluated at the same direction (in the
same units — solid angle from the surface
point).

The MIS arc ships:
- The data model to carry both PDFs at the
  point of estimator combination.
- The helper that computes the power
  heuristic.
- The integrator wiring that multiplies the
  MIS weight into the existing NEE +
  BSDF-bounce contributions.

---

## 2. Scope

### 2.1 In-scope

- **Direct lighting MIS.** Combines the NEE
  estimator (NEE.{1..6}) with the BSDF
  sampler estimator (the existing diffuse
  bounce). Applied at every bounce vertex.
- **Point + Directional lights first.** The
  current v1 light-type scope. MIS at this
  scope is a degenerate no-op (the BSDF-
  bounce estimator contributes zero to delta
  lights with probability 1; the NEE-only
  estimator is unbiased; the MIS weight
  collapses to 1.0 for NEE and 0.0 for BSDF-
  bounce-as-light). Shipping MIS at v1
  preserves byte-identity by construction.
- **Lambert BSDF only.** The current diffuse-
  only path tracer. The BSDF PDF helper
  ships `pdf_cosine_hemisphere`-equivalent
  semantics for Lambert; future BSDFs (GGX
  metal, dielectric, glass) extend the
  helper without touching the MIS
  integrator.
- **Both backends symmetric.** CUDA and
  OptiX path tracers ship MIS in the same
  slice cadence (CUDA in MIS.5, OptiX in
  MIS.6). Same byte-identity contract as the
  NEE.4 / NEE.5b cross-backend symmetry.

### 2.2 Out-of-scope

- **Area-light MIS in this slice.** The MIS
  arc ships the architectural foundation;
  the area-light arc (a future slice or
  parallel arc) ships the geometry +
  sampling that exercises the MIS payoff.
  See §6 for the explicit non-goal list.
- **Full spectral MIS.** PDFs are evaluated
  per-sample (one scalar PDF per direction),
  not per-wavelength. A future spectral
  renderer would need wavelength-resolved
  PDFs; out of scope here. The v1 RGB
  pipeline carries the same PDF for all
  three channels.
- **Bidirectional path tracing (BDPT) MIS.**
  BDPT extends MIS to combine N paths from
  the camera with M paths from the lights;
  the v1 path tracer is unidirectional
  (camera-only). BDPT is orthogonal future
  work.
- **Volumetric MIS.** No participating media
  in the v1 path tracer; volumetric
  estimators are deferred indefinitely.
- **MIS for environment lighting.** The
  existing environment-radiance miss path
  is implicitly the BSDF-sampler-as-light
  estimator for the env. NEE for env (IBL
  sampling) is itself a future slice; MIS
  for the env / BSDF combination lands
  alongside the IBL slice.
- **MIS-aware denoising.** The OptiX
  denoiser consumes the existing
  Beauty / Albedo / Normal AOVs; MIS does
  not change those AOVs' shape.

### 2.3 Implicit scope (handled by structural argument)

- **Default-off byte-identity preservation.**
  The MIS weight defaults to a no-op at the
  v1 light-type scope. No CLI flag is needed
  in this arc; the next caller (the area-
  light arc) will gate MIS on a separate
  per-light-type flag if needed.
- **No CUDA-host requirement for tests.**
  Every helper is RR_HD inline; the host
  test framework exercises the same code
  the kernels run.

---

## 3. Required concepts

### 3.1 BSDF PDF

The probability density of the BSDF
sampler's chosen outgoing direction `wo`,
given an incoming direction `wi` and the
surface normal `n`. Units: per steradian
(solid angle).

For Lambert (the v1 BSDF):

```
p_bsdf(wo) = max(0, dot(n, wo)) / pi
           = pdf_cosine_hemisphere(cos(theta_o))
```

The `pdf_cosine_hemisphere` helper at
`Sampling.h:96-99` already exists.

For specular (a future BSDF): the PDF is a
Dirac delta at the mirror reflection
direction. MIS weight degenerates to 1.0
(specular lobes are handled outside MIS by
convention).

For metal / dielectric (future BSDFs): the
PDF is a per-microfacet expression
(GGX / Beckmann). The MIS arc ships only
the data model + helper; the future BSDF
arcs ship the per-material PDF
implementations.

**The BSDF PDF helper signature** (target
shape; final wording at MIS.2 impl):

```cpp
// pathtracer/Bsdf.{h,cuh}
RR_HD inline float bsdf_pdf(
    const rr::material::MaterialParams& m,
    rr::math::Vec3 wo,         // outgoing dir (world or tangent)
    rr::math::Vec3 normal);    // surface normal
```

### 3.2 Light PDF

The probability density of the NEE sampler's
chosen direction-toward-light, in the same
units as the BSDF PDF (per steradian, from
the receiver point).

For Point lights: the light is at a single
position. The NEE sampler returns a delta:
the only direction that hits this light is
`normalize(L.position - hit_position)`. The
PDF is a Dirac delta scaled by the inverse
selection probability:

```
p_light(wo) = (1/light_count) · δ(wo - wi_to_light)
```

For Directional lights: similar delta but
on the unit sphere (no position term).

For Area lights (future): the NEE sampler
picks a point on the light's surface
uniformly; the area-to-solid-angle Jacobian
converts:

```
p_light(wo) = (1/light_count) · (1/light_area) · r² / cos(theta_light)
```

where `r` is the distance to the sampled
point and `theta_light` is the angle
between the light's surface normal and
`-wo`.

**At v1 (Point + Directional only), the
delta nature of the light PDF means MIS
collapses cleanly:**

- For NEE samples: `p_bsdf` is finite (the
  BSDF can sample any direction); `p_light`
  is a Dirac delta (infinite at the light's
  direction, zero elsewhere). The power
  heuristic gives `w_NEE = 1` and the NEE
  contribution flows through unchanged.
- For BSDF samples: `p_light` is zero at any
  direction except the delta points; the
  BSDF sampler's chosen direction misses the
  delta with probability 1 (zero measure).
  So the BSDF-bounce-as-light contribution
  to delta lights is zero — no double-count
  to weight.

The plan ships the full MIS apparatus, but
at v1 the math collapses to "MIS weight = 1
for NEE, 0 for BSDF-bounce-as-delta-light"
— preserving the existing NEE.{1..6}
behaviour byte-identically.

**`DirectLightSample` extension** (target
shape; final wording at MIS.3 impl):

The existing field `pdf_inv` (inverse
selection probability; equals `light_count`
for uniform-by-count) stays. A new field
`pdf_solid_angle` carries the directional
PDF in steradians:

```cpp
struct DirectLightSample {
    rr::math::Vec3 wi;
    float          distance;
    rr::math::Vec3 li_unattenuated;
    float          pdf_inv;        // inverse selection PDF (existing)
    // NEW:
    float          pdf_solid_angle;  // directional PDF (sr⁻¹);
                                     // Dirac sentinel for delta lights
};
```

The Dirac sentinel is a special value (e.g.
`-1.0f`, or a separate `is_delta` flag) that
the MIS helper inspects to short-circuit
the power-heuristic computation. Final
choice is at MIS.3 impl; both options
preserve byte-identity at v1.

### 3.3 Power heuristic

The Veach power heuristic with `β = 2` is
the standard MIS weight in production
renderers (PBRT, Mitsuba, Cycles). With one
sample per estimator (`n_i = n_j = 1`):

```
              p_i²
w_i = -------------------
         p_i² + p_j²
```

Properties:
- `0 ≤ w_i ≤ 1`.
- `w_i + w_j = 1` (combined estimator
  unbiased).
- When `p_i ≫ p_j`: `w_i → 1`, `w_j → 0`
  (the high-PDF estimator dominates;
  variance reduction is the gain).
- Smoothly degenerate when one PDF is zero:
  `w_i = 0` when `p_i = 0` (regardless of
  `p_j`).

**Power heuristic helper signature** (target
shape; final wording at MIS.4 impl):

```cpp
// pathtracer/Mis.{h,cuh}
RR_HD inline float power_heuristic(float p_a, float p_b) {
    const float pa2 = p_a * p_a;
    const float pb2 = p_b * p_b;
    const float denom = pa2 + pb2;
    return denom > 0.0f ? pa2 / denom : 0.0f;
}
```

Pure host/device function. Unit-tested at
the host level (no CUDA dependency).

### 3.4 Direct light sample

The existing `DirectLightSample` POD
(`pathtracer/DirectLight.h`) extended per
§3.2 with `pdf_solid_angle`. The
`sample_direct_light_uniform` helper
(`pathtracer/DirectLight.cuh`) populates the
new field per light type:

- Point: Dirac sentinel.
- Directional: Dirac sentinel.
- Area (future): `(1/light_area) · r² /
  cos(theta_light)` per §3.2.

### 3.5 BSDF sample

A new POD capturing the bounce direction +
its BSDF PDF + its evaluated radiance
contribution. Used by the integrator to
combine the BSDF-bounce-as-light estimator
with the NEE estimator.

**`BsdfSample` POD** (target shape; final
wording at MIS.2 impl):

```cpp
// pathtracer/Bsdf.h
struct BsdfSample {
    rr::math::Vec3 wo;             // outgoing direction (world space)
    float          pdf;            // BSDF PDF at wo, sr⁻¹
    rr::math::Vec3 brdf_value;     // BRDF evaluated at (wi, wo)
    bool           is_delta;       // true for specular (future);
                                   // false for diffuse / glossy
};
```

For Lambert (v1):
- `wo`: cosine-weighted hemisphere sample
  via existing `sample_cosine_hemisphere`.
- `pdf`: `pdf_cosine_hemisphere(cos_theta_o)`.
- `brdf_value`: `baseColor / pi` (the
  existing Lambert eval).
- `is_delta`: `false`.

**`BsdfSample` helper** (target shape; final
wording at MIS.2 impl):

```cpp
RR_HD inline BsdfSample sample_bsdf(
    const rr::material::MaterialParams& m,
    rr::math::Vec3 wi,             // incoming dir (toward camera)
    rr::math::Vec3 normal,
    rr::math::Vec2 u);             // 2D random sample
```

---

## 4. Integration points

### 4.1 CUDA path tracer

**Site**: `src/cuda/CudaPathTracer.cu`,
inside `k_pathtrace_sample` (the per-pixel
kernel).

The MIS-aware integrator replaces the
existing NEE branch at lines 276-317 + the
existing BSDF bounce at lines 326-340 with
a unified shape:

```
for each bounce:
    1. Trace primary / bounce ray; compute hit.
    2. If miss: add env (BSDF-sampler-as-env contribution); break.
    3. If emissive hit: add emission * MIS_weight(BSDF-as-light)
       — ONLY when MIS is enabled and the previous bounce was sampled
       via the BSDF (not NEE).
    4. NEE branch (when enable_nee):
        a. sample_direct_light_uniform → DirectLightSample
        b. trace shadow ray → visibility
        c. compute p_light (sample.pdf_solid_angle)
        d. compute p_bsdf at sample.wi (bsdf_pdf)
        e. compute MIS weight: power_heuristic(p_light, p_bsdf)
        f. accumulate radiance += w_NEE · (BRDF · cos · Li · vis · pdf_inv)
    5. BSDF bounce:
        a. sample_bsdf → BsdfSample
        b. throughput *= BsdfSample.brdf_value · cos / BsdfSample.pdf
        c. step ray.origin/direction
```

**At v1 (delta lights only)**:
- Step 3 fires only on NEE-disabled path
  (back-compat; the existing emission-add).
- Step 4e: `p_light` is the Dirac sentinel;
  `power_heuristic` returns `1.0` for the
  delta case (special-cased on the
  `is_delta` flag). The NEE contribution
  flows through unchanged. **Byte-identical
  with NEE.{1..6}.**
- Step 5: same Lambert bounce; throughput
  update uses `BsdfSample.brdf_value · cos
  / pdf` which simplifies to `baseColor`
  for Lambert (`(baseColor/π) · cos /
  (cos/π) = baseColor`). **Byte-identical
  with NEE.{1..6}.**

The MIS arc therefore ships ONLY the data
model + helpers + integrator shape. The
v1 light-type scope's runtime arithmetic
is untouched. **No PPM byte-identity
regression.**

### 4.2 OptiX path tracer

**Site**: `src/optix/OptixPrograms.cu`,
inside `__raygen__pathtrace`.

Same shape as §4.1. The OptiX raygen reads
the same `pathtracer/Mis.cuh` +
`pathtracer/Bsdf.cuh` helpers (they are
RR_HD inline; OptiX device code can include
them directly, same as
`pathtracer/DirectLight.cuh` per NEE.4).

The shadow ray reuses the existing
`__miss__shadow` SBT record (Stage 20L).

### 4.3 Material / BSDF evaluation

**Site**: a new
`src/pathtracer/Bsdf.{h,cuh}` module
mirroring `pathtracer/DirectLight.{h,cuh}`'s
shape:

- `Bsdf.h`: `BsdfSample` POD (host-friendly
  data type).
- `Bsdf.cuh`: `RR_HD inline` `sample_bsdf`,
  `bsdf_pdf`, `bsdf_eval` helpers.

The new module reads `MaterialParams` (read-
only, per the existing convention). It does
NOT modify `MaterialParams` itself; the
v1 Lambert helpers consume `baseColor`
only.

For future BSDFs (GGX metal, dielectric,
glass — out of scope here per §6):
- `Bsdf.{h,cuh}` grows with per-BSDF
  `sample_*` / `pdf_*` / `eval_*` helpers.
- The integrator switches on
  `material.metallic` / `material.specular`
  / etc. to dispatch.
- The MIS apparatus consumes whichever
  helpers fire, no integrator-level change
  needed.

### 4.4 NEE branch

**Site**: `src/pathtracer/DirectLight.cuh`
(extends NEE.4's
`sample_direct_light_uniform` to populate
`pdf_solid_angle`). Plus the integrator
sites in §4.1 + §4.2 that consume the new
field via the power heuristic.

The NEE branch's Cosmetic / log-line
surface is unchanged: the operator-facing
CLI (`--enable-nee`) does NOT change. MIS
is turned ON automatically when NEE is on
AND the scene contains at least one non-
delta light (a future area-light arc adds
this gate). At v1 (no area lights), MIS is
on but produces the same arithmetic as
no-MIS — see §4.1's "byte-identical with
NEE.{1..6}" note.

A future operator-facing knob `--no-mis`
is conceivable but NOT proposed by this
plan; the default-on MIS contract is
preferable because MIS at v1 is a no-op
and at v2 (area lights) it's mandatory.

---

## 5. Proposed stage order

Six implementation slices + one audit,
mirroring the NEE.{1..6} cadence:

### 5.1 MIS.2 — BSDF PDF data model (impl)

**Scope**: ship `src/pathtracer/Bsdf.{h,cuh}`
with `BsdfSample` POD + `sample_bsdf` +
`bsdf_pdf` + `bsdf_eval` for Lambert. Pure
data model + helper module; no integrator
changes; no kernel changes.

**Files touched**:
- `src/pathtracer/Bsdf.h` (NEW; ~50 lines).
- `src/pathtracer/Bsdf.cuh` (NEW; ~80 lines
  for Lambert).
- `tests/pathtracer_bsdf_tests.cpp` (NEW;
  ~100 lines covering PDF normalization +
  cos-theta correctness + degenerate
  cases).
- `CMakeLists.txt` (+~5 lines wiring the
  new test binary).

**Default-off byte-identity**: trivially
preserved — no caller invokes the new
helpers in this slice.

**ctest count change**: +1 test binary
(`pathtracer_bsdf_tests`); 9/9 → 10/10
OFF and 10/10 → 11/11 ON.

### 5.2 MIS.3 — Light PDF data model (impl)

**Scope**: extend
`src/pathtracer/DirectLight.{h,cuh}` with
the `pdf_solid_angle` field + populate it
in `sample_direct_light_uniform` for Point
and Directional lights (Dirac sentinel).
The new field is inert at v1 — no caller
reads it yet.

**Files touched**:
- `src/pathtracer/DirectLight.h` (+~10
  lines: add `pdf_solid_angle` field +
  doc-comment).
- `src/pathtracer/DirectLight.cuh` (+~15
  lines: populate the new field per light
  type; sentinel for Point / Directional).
- `tests/pathtracer_nee_tests.cpp` (+~30
  lines: anchor that the new field
  carries the expected Dirac sentinel for
  Point / Directional; default-constructed
  sample carries the bit-zero default).

**Default-off byte-identity**: trivially
preserved — the `bit_default` test from
NEE.5 final-test slice gets the new field
included via `sizeof(DirectLightSample)`
in the memcmp; the new field's default
must be bit-zero so the existing test
case still passes. (This is a minor design
constraint; the sentinel choice in MIS.3
must allow `0.0f` as a valid default-
constructed value. A separate `is_delta`
boolean field defaulting to `false` would
satisfy this, with the kernel checking
`is_delta` before consuming `pdf_solid_angle`.)

**ctest count change**: 0 (no new binary;
existing `pathtracer_nee_tests` grows).

### 5.3 MIS.4 — Power heuristic helper (impl)

**Scope**: ship
`src/pathtracer/Mis.{h,cuh}` with the
`power_heuristic(p_a, p_b)` helper. Pure
host/device function; no integrator
changes.

**Files touched**:
- `src/pathtracer/Mis.h` (NEW; ~30 lines).
- `src/pathtracer/Mis.cuh` (NEW; ~30 lines
  re-exporting the helper RR_HD inline).
- `tests/pathtracer_mis_tests.cpp` (NEW;
  ~80 lines covering: zero PDFs
  (`w == 0`), equal PDFs (`w == 0.5`),
  one-dominates (`w → 1`), Dirac sentinel
  passthrough, sum-to-one invariant for
  the (a, b) and (b, a) calls).
- `CMakeLists.txt` (+~5 lines wiring the
  new test binary).

**Default-off byte-identity**: trivially
preserved — no integrator calls the
helper in this slice.

**ctest count change**: +1 test binary
(`pathtracer_mis_tests`); 10/10 → 11/11
OFF and 11/11 → 12/12 ON.

### 5.4 MIS.5 — CUDA direct-light MIS (impl)

**Scope**: integrate the helpers into
`k_pathtrace_sample`'s NEE branch +
BSDF-bounce-as-light path. Inside the
guard `if (enable_nee && light_count >
0)`, multiply the NEE contribution by
`power_heuristic(p_light, p_bsdf)`. At v1
(delta lights), the helper short-circuits
to `1.0`; the existing arithmetic flows
through unchanged. The BSDF-bounce-as-
light path is gated on `material.is_emissive`
+ MIS_weight from the BSDF side; at v1
delta lights there's no emissive surface
to bounce into, so it's a no-op too.

**Files touched**:
- `src/cuda/CudaPathTracer.cu` (~30-50
  lines: integrate MIS into the existing
  NEE branch; add the BSDF-bounce-as-
  light contribution gated on
  `is_emissive` + the BSDF-side MIS
  weight; the integrator's overall shape
  is the same).
- `tests/pathtracer_nee_tests.cpp` (+~30
  lines: anchor that the integrator's
  v1 byte-identity holds — same `memcmp`-
  level checks the NEE.5 byte-identity
  cases established).

**Default-off byte-identity**: PRESERVED
structurally per §4.1's "byte-identical
with NEE.{1..6}" argument. Verified at
two layers:
- Static IEEE-754 + RNG-stream argument
  carries forward (the MIS weight at v1 is
  exactly `1.0`; the multiplier is inert).
- Host-only test extension to
  `pathtracer_nee_tests.cpp` confirms the
  helper output is bit-equal across the
  v1 light-type scope.

The CUDA-host runtime PPM `cmp` for
default-OFF byte-identity (DEFERRED at
NEE.6 §9.1) extends to also cover MIS.5
no-flag invocations.

**ctest count change**: 0 (no new binary).

### 5.5 MIS.6 — OptiX direct-light MIS (impl)

**Scope**: mirror MIS.5 in
`__raygen__pathtrace`. Same insertion
points as the NEE.4 OptiX-side mirror;
same default-OFF byte-identity argument;
same MIS.5 byte-identity test extensions
apply (the host-only helper anchors are
backend-agnostic).

**Files touched**:
- `src/optix/OptixPrograms.cu` (~30-50
  lines mirroring MIS.5).
- `tests/pathtracer_nee_tests.cpp` (no
  additional changes; the host-only
  anchor exercises the same RR_HD inline
  helpers both backends use).

**Default-off byte-identity**: PRESERVED
per §4.2's "same shape as §4.1" note +
the static argument from MIS.5.

**ctest count change**: 0.

### 5.6 MIS.7 — Audit (docs only)

**Scope**: walk the PASS criteria from §7
+ verify the structural invariants. Same
shape as the NEE.3 / NEE.6 audits.

**Files touched**:
- `docs/PATH_TRACER_MIS_AUDIT.md` (NEW;
  ~500-800 lines).
- `docs/BUILD_PLAN.md` (slice-closing
  entry).

**Default-off byte-identity**: trivially
preserved — docs only.

**ctest count change**: 0.

### 5.7 Optional follow-up: MIS.8+ (area lights)

The MIS arc ends at MIS.7. The area-light
arc (a separate arc — likely numbered
A-LIGHT.1+ or similar) consumes the MIS
foundation. That arc will:

- Add Area light type plumbing
  (`Light::Area` is currently a
  PLACEHOLDER per `lighting/Light.h`).
- Implement uniform area-light sampling.
- Implement BSDF-bounce-hit-light detection
  (the integrator gates "is this hit on
  an emissive area-light mesh?" on a
  per-mesh flag).
- Implements `pdf_solid_angle` for Area
  lights (the area-to-solid-angle
  Jacobian).
- Adds a fixture scene exercising both
  estimators on the same area light.
- The MIS weight (already shipped at
  MIS.4) automatically combines them.

That arc is NOT planned here; this plan
ships the MIS prerequisite only.

---

## 6. Non-goals

The following items are explicitly NOT part
of the MIS arc:

1. **Area-light MIS in this slice.** The MIS
   arc ends at MIS.7. Area-light geometry +
   sampling is its own arc (out of scope per
   §5.7).
2. **Bidirectional path tracing (BDPT).**
   BDPT adds light-side path generation; the
   v1 path tracer is camera-only. BDPT is
   orthogonal future work.
3. **Spectral renderer.** PDFs are scalar
   per-sample, not per-wavelength. A future
   spectral arc would extend the helpers;
   out of scope here.
4. **Volumetric MIS.** No participating
   media. Volumetric estimators (in-
   scattering, single-scattering) are
   indefinitely deferred.
5. **MIS for environment lighting.** The
   environment-radiance miss path is
   implicitly the BSDF-sampler-as-env
   estimator. NEE for env (IBL sampling) +
   its MIS pair is a future IBL arc.
6. **MIS-aware denoising.** The OptiX
   denoiser's input AOVs are unchanged.
7. **MIS for non-Lambert BSDFs.** The MIS
   apparatus is BSDF-agnostic, but the v1
   `Bsdf.{h,cuh}` helpers ship Lambert
   only. GGX / dielectric / glass land in
   future BSDF arcs that consume the
   existing MIS apparatus without changing
   it.
8. **`--no-mis` CLI flag.** MIS at v1 is a
   no-op (default-on is byte-identical with
   pre-MIS). At v2 (area lights), MIS is
   mandatory for an unbiased integrator.
   No operator-facing knob is proposed.
9. **Specular delta MIS.** Specular lobes
   bypass MIS by convention (Veach 1995
   §10.3); the MIS arc's `is_delta` flag
   on `BsdfSample` is the foundation for
   future specular-delta handling, but the
   v1 Lambert-only BSDF never sets it true.
10. **MIS for transient / time-resolved
    light transport.** Transient rendering
    is deferred indefinitely.

---

## 7. PASS criteria for future implementation

Each MIS.{2..6} impl slice + the MIS.7
audit must satisfy:

### 7.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF):
  clean build, zero new warnings.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON
  with SDK fallback): clean build, zero
  new warnings.

### 7.2 Tests

- `ctest --output-on-failure` from `build`:
  100% green.
- `ctest --output-on-failure` from
  `build-ON`: 100% green.
- New test binaries (`pathtracer_bsdf_tests`
  at MIS.2; `pathtracer_mis_tests` at
  MIS.4) added to ctest.
- Existing `pathtracer_nee_tests` extended
  with the MIS-specific byte-identity
  cases at MIS.3 + MIS.5.
- `cli_tests` unchanged (no CLI surface
  in the MIS arc).

### 7.3 Source diff size

Per-slice budget:

| Slice | Diff target | Notes                                               |
|-------|-------------|-----------------------------------------------------|
| MIS.2 | ≤ 250       | Bsdf.{h,cuh} + tests + CMake.                       |
| MIS.3 | ≤ 100       | DirectLight.{h,cuh} extension + test cases.         |
| MIS.4 | ≤ 200       | Mis.{h,cuh} + tests + CMake.                       |
| MIS.5 | ≤ 200       | CudaPathTracer.cu integration + tests.              |
| MIS.6 | ≤ 200       | OptixPrograms.cu integration.                       |
| MIS.7 | docs only   | ~500-800 lines audit doc + BUILD_PLAN entry.        |

Anything LARGER per slice is flagged as a
deviation in that slice's BUILD_PLAN
entry, per the established PT-P.x / NEE.x
deviation-note pattern.

### 7.4 Default-OFF byte-identity invariant

For an operator running ANY action that
does NOT pass `--enable-nee`, the rendered
PPM is bit-identical with the pre-MIS
build. The argument is the same as the
NEE.{1..6} default-off argument:

- The NEE branch is never entered (kernel
  guard short-circuits).
- The new MIS apparatus is never invoked
  (no caller path reaches it at default-
  off).
- The BSDF bounce uses the same `Sampling.h`
  helpers; throughput update is bit-
  equivalent.

### 7.5 Default-ON byte-identity at v1 light-type scope

For an operator running `--enable-nee`
against a scene with ONLY Point and/or
Directional lights, the rendered PPM is
bit-identical with the pre-MIS NEE-on
build. The argument is the §4.1 "MIS
weight collapses to 1.0 for delta lights":

- `power_heuristic(p_light_dirac,
  p_bsdf_finite)` short-circuits to `1.0`
  via the `is_delta` flag.
- The NEE contribution flows through with
  the same multiplier (1.0).
- The BSDF-bounce-as-light contribution
  is zero (delta lights have zero solid-
  angle measure).

This is the structural argument. The
host-only test (MIS.5 extension to
`pathtracer_nee_tests.cpp`) anchors it
empirically on the audit host.

The CUDA-host runtime PPM `cmp` (DEFERRED
on the audit host) extends to:

```
$ git checkout 827f5de    # post-NEE-arc baseline
$ cmake --build build-cuda -j
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --enable-nee
$ cp output/pathtrace_spp_16.ppm /tmp/pre_mis.ppm
$ git checkout MIS.5_commit
$ cmake --build build-cuda -j
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --enable-nee
$ cp output/pathtrace_spp_16.ppm /tmp/post_mis5.ppm
$ cmp /tmp/pre_mis.ppm /tmp/post_mis5.ppm  ; echo $?
=> 0 (identical — MIS at v1 is a no-op)
```

### 7.6 No-touch invariants

Each impl slice MUST keep zero bytes of
diff in:

- `src/pathtracer/RNG.{h,cuh}` (RNG
  primitives unchanged).
- `src/pathtracer/Sampling.{h,cuh}` (the
  cosine-hemisphere sampler is reused;
  the MIS arc may add a parallel
  `Bsdf.{h,cuh}` module but does not
  modify Sampling).
- `src/pathtracer/PathTracer.{h,cpp}`
  (config + dispatcher unchanged; no new
  CLI flag in this arc).
- `src/core/{Config,CommandLine}.{h,cpp}`
  (no CLI surface).
- `src/main.cpp` (no dispatcher changes).
- `src/lighting/Light.h`, `src/scene/`,
  `src/material/MaterialTypes.h` (no
  scene-format changes; no material POD
  changes).
- `src/cuda/CudaScene.cuh`, every
  `src/optix/Optix*.h` POD layout
  (no upload contract changes).

### 7.7 Cross-backend symmetry

After MIS.5 + MIS.6 both land, both
backends produce convergence-equivalent
output for any operator-passable flag
combination (default-off, NEE-on, future
MIS-on). The cross-backend convergence
check from `PATH_TRACER_NEE_AUDIT.md`
§6.2 (DEFERRED on the audit host)
extends to MIS-on cases.

### 7.8 Documentation

- Each impl slice adds a BUILD_PLAN
  entry following the established TEX-
  P.x / PT-P.x / NEE.x format.
- The MIS.7 audit ships
  `docs/PATH_TRACER_MIS_AUDIT.md`
  walking these PASS criteria.
- This plan
  (`docs/PATH_TRACER_MIS_PLAN.md`) is
  the canonical MIS arc reference;
  individual slices reference it.

### 7.9 Master rule compliance

- Build incrementally (rule 1): each
  slice ships ≤200 lines + builds
  cleanly.
- Every step compilable (rule 2): both
  audit-host configs green after each
  slice.
- No fake stubs (rule 3): every helper
  is real RR_HD inline code; the BSDF /
  MIS helpers compile + link cleanly.
- No CPU per-pixel work (rules 5 + 7):
  every per-pixel decision is device-
  side; the MIS weight is computed
  per-bounce on the GPU.
- Module boundaries (rule 9): the new
  modules
  (`src/pathtracer/{Bsdf,Mis}.{h,cuh}`)
  sit alongside the existing
  `pathtracer/{DirectLight,RNG,Sampling}.{h,cuh}`
  cleanly.
- Avoid monolithic files (rule 10): the
  MIS arc spreads logic across multiple
  small files rather than expanding any
  one source.
- Explicit testable interfaces (rule
  11): every helper has a host-side
  test.
- Update BUILD_PLAN (rule 8): every
  slice + the audit add an entry.

---

## 8. Implementation cadence + sequencing

The six impl slices have a strict
sequencing constraint:

```
MIS.2 (BSDF data model) ──┐
MIS.3 (Light data model) ─┼──> MIS.5 (CUDA integrator) ──> MIS.6 (OptiX integrator) ──> MIS.7 (audit)
MIS.4 (MIS helper)        ─┘
```

MIS.{2,3,4} are independent leaves. They
can be shipped in any order or in
parallel slices; each is self-contained
data-model + helper code with its own
host-only tests.

MIS.5 depends on all three leaves. MIS.6
depends on MIS.5 (for the cross-backend
symmetry argument; the OptiX side
mirrors the CUDA shape verbatim).

MIS.7 depends on all six prior slices.

The recommended cadence:

1. MIS.2 first (BSDF data model is the
   most concrete; informs MIS.5's
   integrator shape).
2. MIS.3 second (Light data model
   extends an existing POD; trivial
   ripple).
3. MIS.4 third (the MIS helper is
   pure-math; tests fully exercise it
   without integrator).
4. MIS.5 fourth (CUDA integration; lands
   the v1 byte-identity contract).
5. MIS.6 fifth (OptiX mirror; closes
   cross-backend symmetry).
6. MIS.7 sixth (audit; closes the arc).

Any deviation from this order is fine
PROVIDED the dependencies above hold.
The user may choose to interleave with
unrelated arcs (e.g. land MIS.2 + MIS.3,
pivot to a TEX-P.x slice, return to
MIS.4) — the leaves are independent.

---

## 9. Sub-arc closure

The MIS arc closes at MIS.7. After it
lands:

- `src/pathtracer/{Bsdf,Mis}.{h,cuh}`
  exist as the BSDF / MIS foundation.
- `DirectLightSample` carries
  `pdf_solid_angle`.
- Both path tracers integrate MIS into
  the NEE branch.
- Default-off + default-on-at-v1 byte-
  identity preserved.
- Both backends symmetric.
- The arc unblocks the future area-
  light arc (out of scope here per §5.7).

`PATH_TRACER_NEE_TASK.md` §1's reserved
"future area-light slice" is now properly
gated: MIS lands first; the area-light
arc lands second; the integrator at v2
is unbiased without rework.

### 9.1 Recommended next step (post-MIS-arc)

After MIS.7 closes, three viable
directions:

1. **Trigger the CUDA + OptiX-SDK host
   verification run** flipping the
   accumulated DEFERRED rows (PT-P.x +
   firefly-clamp-CLI + NEE.x + MIS.x)
   to PASS in a single operator
   session.

2. **Pivot to area-light arc** (the
   natural successor consuming the MIS
   foundation; addresses the v1 NEE
   "double-count window" in earnest).

3. **Pivot to a different master-#16+
   arc** (non-Lambert BSDFs; or
   master-#18+ textures; or master-#19
   AOVs polish).

Recommended sequencing: **(2)** as the
natural successor consuming the MIS arc.

---

Mode reminder: **documentation only.**
This file is the spec. The next slices
(MIS.{2..6} impl + MIS.7 audit) ship the
diff. No source code is modified by this
plan.

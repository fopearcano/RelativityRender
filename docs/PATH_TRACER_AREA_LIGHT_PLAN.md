# Path Tracer Area-Light Support — Plan (AREA arc)

Date: 2026-05-09.
Branch: `area-light-arc`
(branched from `relativity-core-v1` HEAD
`cee451e` "docs: add MIS.7 arc audit to
relativity-core-v1").
Mode: documentation only. **No source code is
modified by this slice.** This is the AREA arc
opener, mirroring the cadence of
`docs/PATH_TRACER_NEE_TASK.md` (NEE.1) and
`docs/PATH_TRACER_MIS_PLAN.md` (MIS.1) — a
plan-only slice that establishes the arc's
scope, design, slice order, and PASS criteria
before any implementation lands.

This plan begins the next major path-tracer
feature arc after MIS.7 closure. It consumes
the MIS apparatus (
`pathtracer/{Bsdf,Mis,DirectLight}.{h,cuh}`)
already shipped on `relativity-core-v1` to
deliver area-light support — the natural
successor identified by `PATH_TRACER_MIS_
ARC_AUDIT.md` §8.3 ("Pivot to area-light arc
— the natural successor consuming the MIS
foundation") and §9.1 ("Area-light arc
prerequisites").

---

## 0. Pre-flight context

**MIS arc closure (PASS_WITH_SUBSUMED_AUDITS).**
`docs/PATH_TRACER_MIS_ARC_AUDIT.md` shipped at
`a4d802f` (forward-ported to `relativity-core-v1`
at `cee451e`). The seven MIS slices delivered:

- `BsdfSample` POD + `sample_bsdf` /
  `bsdf_pdf` / `bsdf_eval` Lambert helpers
  (`pathtracer/Bsdf.{h,cuh}`).
- `DirectLightSample::pdf_solid_angle` +
  `is_delta` fields
  (`pathtracer/DirectLight.h`).
- `power_heuristic` helper
  (`pathtracer/Mis.h`).
- CUDA + OptiX integrator wirings —
  `is_delta ? 1.0f : power_heuristic(...)`
  ternary in both backends' NEE branches.
- 18 host-only tests (10 BSDF + 8 MIS).

Default-OFF + default-ON-at-v1-delta-lights
byte-identity is preserved structurally on
both backends. Runtime PPM `cmp` confirmation
across §8.2.{1..6} of the MIS audit remains
DEFERRED to a CUDA + OptiX-SDK operator
session.

**Where the MIS arc was deliberately silent.**
MIS.7 audit §7 ("Non-delta / area-light MIS
remains future work") explicitly carved out
the area-light arc as a separate deliverable.
The MIS apparatus is the *necessary* foundation;
the area-light arc is *sufficient* to deliver
production direct lighting on top of it.

This plan is that arc.

---

## A. Current renderer state

This section anchors the AREA arc against the
`cee451e` HEAD it builds on.

### A.1 Delta-light-only MIS

Both path tracers' NEE branches are wired with
the MIS ternary
(`pathtracer/Mis.h::power_heuristic` consumed
in `src/cuda/CudaPathTracer.cu` line ~333 and
`src/optix/OptixPrograms.cu` line ~1026):

```cuda
const float mis_weight_nee = sample.is_delta
    ? 1.0f
    : power_heuristic(sample.pdf_solid_angle,
                      bsdf_pdf(m, sample.wi, hit_n));
```

At `cee451e`, every NEE sample sets
`is_delta == true`:

- **Point lights** — `DirectLight.cuh:171`
  sets `s.is_delta = true`.
- **Directional lights** — `DirectLight.cuh:206`
  sets `s.is_delta = true`.
- **Area / Environment lights** —
  `DirectLight.cuh:210-219` returns the
  default-constructed `DirectLightSample`
  (which has `is_delta == false` AND
  `pdf_inv == 0.0f`). The integrator's
  `if (sample.pdf_inv > 0.0f)` gate at
  `CudaPathTracer.cu:283` skips the entire
  NEE contribution for these light types,
  so the `is_delta` flag is unread.

The else-branch of the MIS ternary
(`power_heuristic(p_light, p_bsdf)`) is
**structurally unreachable** at v1. It is
real CUDA + OptiX code that compiles +
links + is invoked by zero pixels. The
AREA arc is the slice that flips that
predicate's reachability — for the first
time in the renderer's history, an NEE
sample will return `is_delta == false` and
the `power_heuristic` call will execute.

### A.2 Lambert-only BSDF state

`pathtracer/Bsdf.{h,cuh}` ships:

- `BsdfSample` POD (`wo`, `value`, `pdf`,
  `valid`).
- `sample_bsdf(material, wi, normal, u)`
  — cosine-weighted hemisphere sample.
- `bsdf_pdf(material, wo, normal)` —
  `cos_theta_o / pi` (per steradian).
- `bsdf_eval(material, wi, wo, normal)` —
  `baseColor / pi` (Lambert BRDF).

The Lambert sampler IS consumed by the MIS
ternary's `bsdf_pdf` call — it produces the
BSDF-side PDF for the NEE-side MIS weight.
The actual `sample_bsdf` is NOT yet
consumed by the integrator; the existing
inline cosine-bounce arithmetic
(`CudaPathTracer.cu:362-376`) remains in
place. Both implementations produce
bit-equivalent results for Lambert
(by IEEE-754 §6 identity multiplication
through `align_to_normal` + `kInvPi`).

The AREA arc does **not** introduce new
BSDFs. Lambert is the only BSDF the
integrator consumes; non-Lambert BSDFs
(GGX, dielectric, glass) are deferred to
a separate post-AREA arc per MIS.7
audit §9.2.

### A.3 Current `Light::Area` placeholder status

The `Light` POD (`src/lighting/Light.h:56-67`)
is forward-compatible with area lights:

```cpp
struct Light {
    LightType      type        = LightType::Point;
    rr::math::Vec3 color       = {1, 1, 1};
    float          intensity   = 1.0f;
    rr::math::Vec3 position    = {0, 0, 0};
    rr::math::Vec3 direction   = {0, -1, 0};
    float          area_width  = 0.0f;   // PLACEHOLDER
    float          area_height = 0.0f;   // PLACEHOLDER
};
```

Status by subsystem:

| Subsystem                       | Area-light support today                                                                           |
|---------------------------------|----------------------------------------------------------------------------------------------------|
| `Light::Area` enumerator        | Defined (`Light.h:25`)                                                                             |
| `make_area_light` factory       | Defined (`Light.cpp:44-60`); normalizes normal, clamps `width`/`height` to `>= 0`                  |
| Scene-format spec               | Documented (`docs/RRSCENE_FORMAT.md` §609-655); `area_width`/`area_height` listed as PLACEHOLDER   |
| Scene parser                    | Reads `type == "area"` (`SceneLoader.cpp:1197`); reads `position` (1240); does **NOT** read `direction` (only directional triggers `needs_direction`); does **NOT** read `area_width` / `area_height` |
| Scene→GPU upload                | `GpuScene::upload_lights` uploads the full `Light` POD including `area_width`/`area_height`; the kernel just doesn't consume them |
| CUDA NEE helper                 | Returns default-zero `DirectLightSample` for `LightType::Area` (`DirectLight.cuh:210-219`)         |
| OptiX NEE helper                | Same fall-through (consumes the same RR_HD `sample_direct_light_uniform`)                          |
| BSDF-bounce-as-light            | No detection — the closest-hit walk has no concept of "this hit is an emissive area-light surface" |

Net effect: an area light declared in the
scene file is **uploaded to GPU and
silently ignored** at every per-pixel
arithmetic site. The user-visible result
is identical to declaring zero lights of
that type. The AREA arc closes this gap
end-to-end.

### A.4 Default test fixtures (pre-AREA baseline)

The fixtures the v1 byte-identity invariant
binds against (per MIS.7 audit §8.2):

- Default scene (no scene file) — Point
  lights only.
- Cornell-style fixture (when authored) —
  Point + Directional only.
- Any user scene with `type == "area"` —
  parses, but the area-light contribution
  is zero.

The AREA arc preserves byte-identity for
the first two and **deliberately changes**
the third (the area-light contribution
becomes non-zero for the first time).
This is the arc's PASS criterion §E.

---

## B. Required architecture changes

This section enumerates every architectural
delta needed to ship area-light support. Each
change is scoped to the slice that delivers
it (cross-referenced to §G).

### B.1 Scene / light data model (AREA.1)

**Current state (`Light.h:56-67`).** The POD
already has `area_width` + `area_height`. No
schema additions needed.

**Required additions.** None to the POD
itself. The `LightType` enumerator already
has `Area = 2`. The factory (`make_area_light`)
already populates the right fields.

**`DirectLightSample` reuse.** The MIS arc
(MIS.3) added `pdf_solid_angle` and
`is_delta` to `DirectLightSample`. The AREA
arc consumes both for the first time. No
new fields needed:

- `pdf_solid_angle` carries the
  area-to-solid-angle Jacobian
  (per §C.2 below).
- `is_delta` is `false` for area lights —
  the MIS ternary's else-branch executes.

The MIS.3 doc-comment block on
`DirectLightSample::pdf_solid_angle` already
describes the area-light Jacobian
(`(1/light_count) · (1/area) · r² /
cos(theta_light)`) — the AREA arc fulfils
that contract.

**Reserved decision: the "is emissive area
light" mesh flag.** MIS.7 audit §9.1 calls
for a per-mesh "is emissive area light"
flag for BSDF-bounce-as-light detection.
**The AREA arc does NOT adopt that
strategy.** Rationale: rectangular area
lights have an *implicit* geometric form
(width × height, anchored at `position`
with surface normal `direction`) that does
not require a `Mesh` representation. The
BSDF-bounce-as-light intersection is
performed against the area-light rectangle
directly (per §C.4). Adding an
"emissive-mesh" lighting surface (arbitrary
mesh acts as an area light) is a separate
post-AREA arc deferred from this scope.
This decision keeps AREA.1 a zero-byte
data-model change and pushes the geometric
work into AREA.4 / AREA.5.

### B.2 Parser changes (AREA.2)

**Current parser gaps (`SceneLoader.cpp`).**

| Gap                                                        | Line ref                          | Required change                                                       |
|------------------------------------------------------------|-----------------------------------|-----------------------------------------------------------------------|
| `direction` not read for area lights                       | 1241 (`needs_direction` predicate)| Promote to `(directional || area)`                                    |
| `area_width` not read                                      | (no read site)                    | Read required-when-area float; reject if `<= 0`                       |
| `area_height` not read                                     | (no read site)                    | Read required-when-area float; reject if `<= 0`                       |
| Spec doc disagreement                                      | `RRSCENE_FORMAT.md` §612-615      | Already documents `direction` + `area_*` as required-for-area; parser is the lagging side |

**Required parser additions.**

- `needs_direction = (Directional || Area)`.
- For `Area`, also require `area_width`
  and `area_height` as positive finite
  floats. The `make_area_light` factory
  clamps non-positive inputs to `0.0f` —
  the parser must reject *before* the
  factory normalises, so the user sees a
  schema error instead of a silent
  no-emission area light.
- Schema-error message format follows the
  existing pattern: `lights[N].area_width
  must be > 0 (got X)`.

**Backwards compatibility.** Scenes that
declared `type == "area"` BEFORE the AREA
arc parsed but produced zero contribution.
After AREA.2 lands they still parse IF
they include `direction` + `area_*` (the
spec already required them per
`RRSCENE_FORMAT.md` §639); they FAIL TO
PARSE if those fields are missing. This is
the schema's documented contract, not a
breakage — the `RRSCENE_FORMAT.md` table
already lists them as required for `area`
type.

### B.3 GPU upload path (AREA.3)

**Current state (`gpu/GpuScene.{h,cpp}`).**
`GpuScene::upload_lights` uploads the full
`Light` POD via `GpuBuffer<Light>`. The
`area_width`/`area_height` fields are
already in flight to GPU; the integrator
just doesn't read them.

**Required additions.** None. The upload
path is byte-correct for area lights
already; AREA.3 verifies by reading the
fields on the device side rather than by
adding new bytes to the upload.

The AREA.3 slice is therefore mostly a
**verification + cross-cutting concerns**
slice rather than a code-add slice:

- A host-side test that constructs a
  `Light` with non-zero `area_width` /
  `area_height`, uploads, downloads, and
  confirms the round-trip (`memcmp` on
  the POD).
- A scene-loader → upload integration
  smoke test (host-only) that confirms
  the parser-populated area-light fields
  reach `CudaSceneView::lights[i]` /
  `OptixLaunchParams::lights[i]`.
- No CUDA dispatch needed — every check
  is host-side because the upload buffer
  is allocated on the audit-host without
  CUDA when `RR_ENABLE_CUDA = OFF`.

### B.4 CUDA integrator (AREA.4)

**Two integration sites.**

**Site 1: NEE branch — area-light sampling.**

Today: `sample_direct_light_uniform` returns
default-zero for `LightType::Area`
(`DirectLight.cuh:210-219`).

Required change: extend the helper with an
`Area` branch that produces a real
`DirectLightSample` (per §C.1–§C.3 design).
The existing `Point` and `Directional`
branches are not touched — every byte of
their PDFs / radiance / shadow-ray contract
is preserved.

The integrator-side change is **zero
bytes**. The existing `if (enable_nee &&
scene.light_count > 0)` branch consumes
the new sample exactly as it consumes
existing samples — the MIS ternary
(`is_delta ? 1.0f : power_heuristic(...)`)
auto-routes area lights through the
non-delta branch.

**Site 2: BSDF-bounce-as-light contribution.**

Today: when a BSDF bounce ray walks the
scene, the closest-hit walk
(`closest_hit_pt` in `CudaPathTracer.cu`)
considers spheres and the single-mesh
slot. It does NOT test against area-light
rectangles. There is no "BSDF hit an
area light" branch.

Required change: extend the closest-hit
walk with a loop over area lights
(`scene.lights[0..count) where
type == LightType::Area`), testing each as
a one-sided rectangle. If a BSDF bounce
hits an area light:

- Compute the area-light's solid-angle
  PDF at the hit point (per §C.2,
  evaluated from the BSDF-side hit's
  geometry).
- Compute the BSDF sampler's PDF at the
  same direction (already known —
  `pdf_cosine_hemisphere(local.z)` from
  the cosine sampler).
- Compute the MIS weight on the BSDF
  side: `power_heuristic(p_bsdf,
  p_light_at_wo)`.
- Add `throughput * emission *
  mis_weight_bsdf` to `radiance`.
- **Skip** the bounce's normal "miss" /
  "next bounce" path — the BSDF bounce
  terminated on a light, by convention.

This is a structurally new code path —
the AREA.4 slice's principal delta. See
§C.4 for the full sequence.

**Cross-cutting: per-bounce flag.** When
NEE samples an area light AND the BSDF
bounce *also* lands on the same area
light, the integrator must NOT add the
"emission from the surface" contribution
twice. The MIS apparatus handles this via
the BSDF-side weight: if NEE already
accumulated `mis_weight_nee * Li`, the
BSDF-side adds `mis_weight_bsdf * Li`,
and the two weights sum to ~1.0f
(power-heuristic invariant). Net
contribution is unbiased.

The legacy "inline emission add" at
`CudaPathTracer.cu:239-246` (`emission =
m.emissionColor * m.emissionStrength`)
still fires when the BSDF bounce hits an
emissive surface (e.g., an emissive mesh,
*not* an area light). Area lights are
NOT emissive meshes — their rectangle is
never represented as a `Sphere` or
`Mesh`, so the inline emission-add never
sees them. No double-count in the
NEE / BSDF / inline-emission triangle.

### B.5 OptiX integrator (AREA.5)

**Mirror of AREA.4.** The OptiX NEE branch
(`OptixPrograms.cu` line ~921+) consumes
the same RR_HD `sample_direct_light_uniform`
helper as the CUDA path; AREA.4's
`DirectLight.cuh` change is therefore
shared between backends with zero
duplication.

**Backend-specific delta: BSDF-bounce-as-
light intersection.** OptiX does NOT use
the inline closest-hit loop the CUDA path
uses; it uses the SBT + GAS for primitive
intersection. Area-light rectangles must be
either:

- **Strategy A** — added as an additional
  GAS (`OptixAccel`) holding the
  rectangle as two triangles per area
  light. The closest-hit program for that
  GAS sets a "this hit is an area light"
  flag on the `PerRayData`. The raygen
  reads the flag after `optixTrace` and
  branches into the BSDF-bounce-as-light
  path.
- **Strategy B** — kept implicit; the
  raygen, after `optixTrace` returns
  (regardless of hit / miss), iterates
  over `optixLaunchParams.lights` and
  performs a host-style ray-rectangle
  intersection in CUDA against each area
  light, picking the nearest among the
  GAS hit and the implicit area-light
  hits.

**Recommendation: Strategy B (implicit).**
Rationale:

- Smaller cross-cutting surface — no
  `OptixAccel` rebuild for area lights;
  no SBT records to manage.
- Symmetric with the CUDA backend's
  closest-hit loop (which already does an
  inline list walk).
- Performance is acceptable for v2 —
  area-light counts are small (≤ 10
  typical scenes); the per-pixel cost is
  `O(area_light_count)` extra ray-
  rectangle intersections beyond the GAS
  closest-hit, which is negligible
  relative to the GAS cost.
- The eventual upgrade path to Strategy
  A is a closed slice in a future arc
  ("real emissive-mesh area lights")
  rather than a refactor of AREA.5.

The AREA.5 slice ships Strategy B. The
CUDA closest-hit loop in AREA.4 mirrors
the same shape; cross-backend symmetry is
preserved by construction (same RR_HD
ray-rectangle intersection helper invoked
from both backends).

The `__raygen__pathtrace` program's
existing structure
(`OptixPrograms.cu:850-1100`) remains the
host of the integrator logic; the only
delta is the post-`optixTrace`
area-light-test loop and the resulting
"closest of GAS + implicit" disambiguation.

---

## C. Area-light sampling design

This section is the per-direction analytic
reference the implementation slices
(AREA.4 / AREA.5) follow.

### C.1 Surface sampling

**Geometry of the rectangle.** An area light is
a one-sided rectangle in world space:

- Anchor: `light.position` (the rectangle's
  centre — convention chosen to match the
  user-facing `position` field of Point
  lights and the existing
  `make_area_light` semantics).
- Normal: `light.direction` (unit vector;
  factory normalizes — emits radiance into
  the half-space `dot(world_dir, normal) >
  0`).
- Half-extents: `light.area_width / 2` and
  `light.area_height / 2` along two
  tangent axes constructed from the
  normal (same `align_to_normal` /
  tangent-frame algorithm as
  `pathtracer/Bsdf.cuh:detail::align_to_
  normal` to maximise code reuse).

**Surface-point sampler.** Two uniform
[0, 1) samples `(u_x, u_y)` map to a
world-space point on the rectangle:

```
local_x = (u_x - 0.5) * area_width
local_y = (u_y - 0.5) * area_height
local   = Vec3{local_x, local_y, 0}
p_light = light.position + align_to_normal(local, light.direction)
```

The PDF on the surface (in m⁻²) is:

```
p_surface = 1 / (area_width * area_height)
```

(uniform over the rectangle).

### C.2 Solid-angle PDF

The NEE estimator is in solid-angle
units: `pdf_solid_angle` carries the
per-steradian PDF density of the
sampled `wi` direction.

Conversion from surface PDF to
solid-angle PDF (the standard
area-to-solid-angle Jacobian, Veach
1997 §8.2.2.2):

```
p_solid_angle = p_surface * r² / |cos(theta_light)|
              = (1 / area) * r² / cos(theta_light)
```

where:

- `r = |p_light - hit_position|` (distance
  from the receiver vertex to the sampled
  point on the light).
- `cos(theta_light) = max(0, dot(-wi,
  light.direction))` (cosine between the
  outgoing direction at the light surface
  and the light's normal — the rectangle
  emits into `+normal`, so a sample
  reaching the receiver from the front
  has `dot(-wi, normal) > 0`).

The selection PDF is uniform-by-count:

```
p_select = 1 / light_count
```

The total PDF of the NEE sample is the
product:

```
p_nee_total = p_select * p_solid_angle
            = (1 / light_count) *
              (1 / area) *
              r² / cos(theta_light)
```

Stored on `DirectLightSample` per the
MIS.3 contract:

| Field             | Value at AREA arc                                                                  |
|-------------------|------------------------------------------------------------------------------------|
| `pdf_inv`         | `light_count * area * cos(theta_light) / r²`  (1/p_nee_total)                      |
| `pdf_solid_angle` | `(1/light_count) * (1/area) * r² / cos(theta_light)`  (per steradian, MIS unit)    |
| `is_delta`        | `false`                                                                            |

The integrator's existing arithmetic
(`throughput * brdf * li * cos_th * vis *
pdf_inv * mis_weight`) consumes `pdf_inv`
unchanged. The MIS weight uses
`pdf_solid_angle` only.

### C.3 Geometric-term handling

**Edge cases the sampler must reject.**

The sampler returns the bit-zero
default-constructed `DirectLightSample`
(matching the Point / Directional
contract for "no contribution") when:

- `light.area_width <= 0` or
  `light.area_height <= 0` — degenerate
  rectangle, zero area. The parser
  rejects this case ahead of GPU
  upload; the device-side check is
  defence-in-depth.
- `r² <= 0` — receiver and light
  coincident. Mirrors the existing
  Point-light branch's check
  (`DirectLight.cuh:143`).
- `dot(-wi, light.direction) <= 0` — the
  light's back face is toward the
  receiver. Area lights are one-sided.
- `cos(theta_receiver) = dot(normal,
  wi) <= 0` — the receiver's BRDF gates
  on cosine; below-horizon samples
  contribute zero. Mirrors the existing
  Point / Directional branches'
  `cos_th <= 0` checks.

In each case `pdf_inv = 0.0f` and the
integrator's `if (sample.pdf_inv > 0.0f)`
gate at `CudaPathTracer.cu:283` skips the
contribution. The bit-zero default
preserves the
`tests/pathtracer_nee_tests.cpp::test_
zero_contribution_is_bit_default` invariant
(the same anchor MIS.3 preserved).

**Radiance.** The light's outgoing
radiance is:

```
li_unattenuated = light.color * light.intensity
```

In Formulation A (which the renderer uses),
the geometric falloff is encoded into
`pdf_inv` (the kernel multiplies the
radiance by `pdf_inv`). For Point lights
the existing helper folds `1/r²` into
`li_unattenuated` (`DirectLight.cuh:160-
162`); for area lights the Jacobian goes
into `pdf_inv` instead. Net per-pixel
arithmetic is equivalent at the IEEE-754
level for the non-r²-bearing terms.

### C.4 BSDF-bounce-as-light: emissive-rectangle strategy

**Why the BSDF side matters.** With area
lights present, the BSDF sampler can
*reach* the light surface by random
bounce. The contribution from a BSDF
bounce that lands on an area light is
the SAME random variable as the NEE
contribution to that light — both
estimators cover the same surface point
(modulo the difference between
solid-angle sampling and area sampling
of the same PDF). MIS combines them
with weights summing to 1.

**Closest-hit walk extension.** The
CUDA `closest_hit_pt`
(`CudaPathTracer.cu:78-130`) currently
walks spheres and triangles. The AREA.4
slice extends it with:

```cpp
// Iterate area lights as one-sided rectangles.
for (int i = 0; i < scene.light_count; ++i) {
    const Light& L = scene.lights[i];
    if (L.type != LightType::Area) continue;
    if (L.area_width <= 0 || L.area_height <= 0) continue;

    float t_hit;
    if (intersect_area_light_rectangle(
            ray, L, t_hit) && t_hit < hit.t) {
        hit.t              = t_hit;
        hit.position       = ray.origin + ray.direction * t_hit;
        hit.normal         = L.direction;
        hit.is_area_light  = true;
        hit.area_light_idx = i;
        // emission and material are read at the integrator
        // post-walk, not stored on Hit (matches existing pattern).
    }
}
```

The integrator post-walk adds:

```cpp
if (hit.is_area_light) {
    // emission = light.color * light.intensity
    // p_bsdf  = bsdf_pdf at the bounce direction (already known)
    // p_light = (1/count) * (1/area) * r² / cos(theta_light)
    // mis_weight_bsdf = power_heuristic(p_bsdf, p_light)
    radiance += throughput * emission * mis_weight_bsdf;
    break;  // path terminates — the bounce hit a light
}
```

The `intersect_area_light_rectangle`
helper is a new RR_HD inline function in
`pathtracer/AreaLight.h` (per §B.1's
"no new POD" decision; the helper lives
alongside the existing
`pathtracer/DirectLight.{h,cuh}` and
`pathtracer/Mis.h`).

**Why this prevents double-counting.**
Three estimators contribute to the same
area-light surface:

| Path                             | Contribution                                                            |
|----------------------------------|-------------------------------------------------------------------------|
| NEE direct sample                | `throughput * brdf * Li * cos_th * pdf_inv * mis_weight_nee * vis`      |
| BSDF bounce reaches the light    | `throughput * Li * mis_weight_bsdf`                                     |
| Inline emission-add              | (does NOT fire — area-light geometry has no `Material::emissionColor`)  |

`mis_weight_nee + mis_weight_bsdf ≈
1.0f` (power heuristic with β=2 sums to
1 within ~1 ULP). The combined
estimator is unbiased.

The inline emission-add (`m.emissionColor
* m.emissionStrength`) at
`CudaPathTracer.cu:239-246` fires when a
hit lands on a `Sphere` or `Mesh` whose
material has non-zero emission. Area
lights are NEITHER spheres NOR meshes —
they have no `material_index` — so the
inline path silently never sees them.
This is the "explicit-rectangle, not
emissive-mesh" architectural choice
from §B.1: it makes double-count
prevention trivial because the three
estimators are disjoint by construction.

### C.5 Cross-backend symmetry

Both backends share:

- The `Area` branch in
  `pathtracer/DirectLight.cuh` (RR_HD
  inline; one impl, two consumers).
- The new `intersect_area_light_rectangle`
  helper in `pathtracer/AreaLight.h`
  (RR_HD inline).
- The new `area_light_solid_angle_pdf`
  helper in `pathtracer/AreaLight.h`
  (RR_HD inline; reusable from the BSDF-
  bounce-as-light contribution
  computation).

CUDA and OptiX call sites pass the same
arguments to the same helpers. The MIS.5
/ MIS.6 audit pattern is repeated:
backends are byte-equivalent for the
overlapping Lambert+rectangle scope by
construction (same RR_HD code).

---

## D. MIS interaction

This section documents how the AREA arc
consumes the MIS apparatus shipped at
MIS.{2..6}.

### D.1 BSDF PDF

Today (`Bsdf.cuh:159-168`):

```cpp
RR_HD inline float bsdf_pdf(
        const MaterialParams& /*m*/,
        Vec3 wo, Vec3 normal) {
    const float cos_theta_o = dot(normal, wo);
    if (cos_theta_o <= 0.0f) return 0.0f;
    return pdf_cosine_hemisphere(cos_theta_o);
}
```

The AREA arc consumes `bsdf_pdf` at two
sites:

- **NEE-side MIS weight (already wired,
  reachable for the first time at AREA.4).**
  `bsdf_pdf(m, sample.wi, hit_n)` —
  evaluates the BSDF's directional PDF at
  the NEE-chosen direction. Same call
  shape as today; the AREA arc just
  changes which inputs it receives
  (formerly the unread else-branch; now
  the live area-light path).
- **BSDF-bounce-as-light MIS weight (new
  at AREA.4).** When the BSDF bounce hits
  an area light, the BSDF sampler's PDF
  at the bounce direction is needed for
  the symmetric MIS weight. The cosine
  sampler's PDF is `cos_theta_o / pi`;
  this is reusable as
  `pdf_cosine_hemisphere(cos_theta_o)` or
  equivalently `bsdf_pdf(m, wo, normal)`.

No new BSDF helper is added by AREA.

### D.2 Light PDF

The AREA arc populates
`DirectLightSample::pdf_solid_angle` for
the first time. The MIS.3 doc-comment
(`DirectLight.h:71-87`) already specified
the expected formula:

> `(1/light_count) · (1/area) · r² /
> cos(theta_light)` and `is_delta == false`.

AREA.4 implements that formula. No
field addition or contract change.

### D.3 Power heuristic

Today (`Mis.h:102-107`):

```cpp
RR_HD inline float power_heuristic(float p_a, float p_b) {
    const float pa2   = p_a * p_a;
    const float pb2   = p_b * p_b;
    const float denom = pa2 + pb2;
    return denom > 0.0f ? pa2 / denom : 0.0f;
}
```

The AREA arc invokes `power_heuristic`
for the first time (the call exists in
both backends today but is unreachable).
Both NEE-side and BSDF-side weights call
the same helper:

- `mis_weight_nee  = power_heuristic(
   sample.pdf_solid_angle,
   bsdf_pdf(m, sample.wi, hit_n))` —
   already wired in both backends; AREA.4
   makes it reachable.
- `mis_weight_bsdf = power_heuristic(
   bsdf_pdf_at_wo, area_light_pdf_at_wo)`
   — new at AREA.4 / AREA.5.

The helper is BSDF-agnostic and
light-type-agnostic (per MIS.7 §9.3) — it
does not change for AREA. The AREA arc is
its first real consumer.

### D.4 Delta vs non-delta behavior

The integrator-level discriminator is
`DirectLightSample::is_delta` (MIS.3).
Per-light-type result post-AREA:

| Light type     | `is_delta` | NEE-side weight                             | BSDF-side reachability                                          |
|----------------|:----------:|---------------------------------------------|-----------------------------------------------------------------|
| Point          | `true`     | `1.0f` (Veach §10.3)                        | unreachable — point lights have no surface; BSDF can't hit one  |
| Directional    | `true`     | `1.0f`                                      | unreachable — directional sources at infinity                   |
| **Area**       | **`false`**| `power_heuristic(p_light, p_bsdf)`          | **reachable** — BSDF bounce can hit the rectangle                |
| Environment    | `false`    | (deferred — Environment is a separate arc)  | (deferred)                                                      |

The integrator's `is_delta ? 1.0f :
power_heuristic(...)` ternary at both
backends' NEE sites is unchanged. The
AREA arc just changes which lights flow
through which branch.

### D.5 Double-count prevention

Three contribution paths to the same
area-light surface (per §C.4) are
disjoint by construction:

- NEE direct sample → uses the area-
  light POD's geometry; weighted by
  `mis_weight_nee`.
- BSDF bounce → uses
  `intersect_area_light_rectangle`;
  weighted by `mis_weight_bsdf`.
- Inline emission-add → never fires for
  area lights (§C.4 — area lights have
  no `Material::emissionColor`).

The MIS weights satisfy
`mis_weight_nee + mis_weight_bsdf ≈ 1.0f`
within ~1 ULP for any non-zero `(p_a,
p_b)` pair (per the `power_heuristic`
sum-to-one invariant). Combined
estimator is unbiased.

Edge case: BSDF bounce visibility blocked
by occluder. The standard rendering
equation handles this — the BSDF bounce
that *should* have hit the light hits
the occluder instead, so that path
contributes zero. The NEE side carries
the full `mis_weight_nee * Li` for that
direction (instead of the
`mis_weight_nee + mis_weight_bsdf`
"correct" combined weight), which
introduces variance but NOT bias.
Standard MIS behaviour; no AREA-arc
mitigation needed.

---

## E. Expected invariants

The AREA arc preserves the following
byte-identity invariants that prior arcs
established. These are the PASS criteria
the impl slices must not regress.

### E.1 Default scenes remain byte-identical

A scene with no area lights renders
*identically* (byte-for-byte) before and
after the AREA arc. Specifically:

| Scene class                             | Pre-AREA contribution            | Post-AREA contribution           | Identity argument                                                                                                                                |
|-----------------------------------------|----------------------------------|----------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------|
| No lights                               | facing-ratio fallback            | facing-ratio fallback            | The integrator's `if (scene.light_count > 0)` gate is unchanged; no light-iteration occurs.                                                      |
| Point + Directional only                | NEE delta-light path             | NEE delta-light path             | The `LightType::Area` branch in `sample_direct_light_uniform` only fires for `type == Area`; Point + Directional execute the existing branches. |
| Point + Directional + Area (parsed but ignored pre-AREA) | NEE delta-light only (Area returns zero) | NEE delta + new Area contribution | The user *intended* the area-light contribution to be non-zero. This is the deliberate behavioural change.                                       |

The first two rows are the **byte-identity invariants**. A `cmp` of the rendered PPM output across the AREA-arc transition must produce zero output for any scene in those two classes.

### E.2 Delta-light paths unchanged

Existing Point + Directional NEE paths
flow through the `is_delta == true`
branch of the MIS ternary, which IS the
IEEE-754 §6 identity multiplication
(`x * 1.0f == x`). The AREA arc:

- Does not modify the `Point` or
  `Directional` branches in
  `sample_direct_light_uniform`.
- Does not modify the MIS ternary's
  shape.
- Does not modify the BSDF helpers
  (`Bsdf.{h,cuh}`).

Cross-arc invariant: the MIS.7 audit's
§8.2.{1..4} byte-identity claims at the
v1 scope (Point + Directional only) are
inherited unchanged by the AREA arc.

### E.3 v1 compatibility constraints

`make_area_light` factory: unchanged
public API. Existing callers see the
factory accept the same arguments and
return a `Light` POD with the same
field layout. The factory's behaviour
on degenerate inputs (zero-length
direction, non-positive width / height)
is unchanged — the AREA arc does NOT
tighten the factory contract; it
tightens the *parser* contract instead
(per §B.2). Direct factory callers
(none in renderer source today; only
test code) are unaffected.

`Light` POD layout: unchanged. No fields
added; no fields reordered; no fields
removed. `sizeof(Light)` is byte-equal
across the transition.
`std::is_trivially_copyable<Light>`
remains true. Upload buffers
(`GpuBuffer<Light>`) bit-identical.

`DirectLightSample` POD layout:
unchanged. The MIS.3 fields are already
present on the struct; the AREA arc just
populates them for `LightType::Area`.

`MaterialParams` POD layout: unchanged.
The AREA arc does NOT add an "is
emissive area light" flag (per §B.1's
deferred-decision argument).
`MaterialParams::emissionColor` is
unread when the surface is an area
light (the inline emission-add never
sees them).

CLI surface: no new flags. The existing
`--enable-nee` flag continues to gate
the NEE branch; area lights only
contribute via NEE / BSDF-bounce when
that flag is on. With `--enable-nee`
off the area-light contribution is
zero, mirroring the pre-AREA behaviour.

### E.4 No-touch invariants

The AREA arc does **NOT** modify:

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`.
- `docs/MASTER_ARCHITECTURE.md`.
- `docs/MILESTONE_ROADMAP.md`.
- Any C4D / server / UI / node-editor
  source.
- Any non-Lambert BSDF code paths
  (deferred to a separate post-AREA
  arc).
- `pathtracer/RNG.{h,cuh}` /
  `Sampling.{h,cuh}` / `Mis.h` (the
  AREA arc consumes them; it does
  not modify them).
- `material/Material.{h,cpp}` /
  `MaterialTypes.h` (the deferred
  "emissive-mesh" arc owns those).

---

## F. Runtime-deferred checks

This section enumerates the empirical
confirmations that are NOT part of any
AREA implementation slice — they live in
the operator-session backlog.

### F.1 CUDA host validation

| § | Check                                                                                            |
|---|--------------------------------------------------------------------------------------------------|
| F.1.1 | CUDA pre-AREA vs post-AREA byte-IDENTITY for default scene (no `--enable-nee`) — PPM `cmp`     |
| F.1.2 | CUDA pre-AREA vs post-AREA byte-IDENTITY for Point-only `--enable-nee` scene                    |
| F.1.3 | CUDA pre-AREA vs post-AREA byte-IDENTITY for Point + Directional `--enable-nee` scene           |
| F.1.4 | CUDA AREA: convergence statistics (variance / mean) of the area-light fixture vs analytic       |

F.1.1–F.1.3 are the byte-identity
invariants from §E.1. F.1.4 is the
correctness check for the new
contribution path (the analytic
expectation is `Li * cos(theta) *
solid_angle / pi` for a Lambert
receiver under a uniformly-emitting
rectangle area light at moderate
distance).

### F.2 OptiX host validation

| § | Check                                                                                            |
|---|--------------------------------------------------------------------------------------------------|
| F.2.1 | OptiX pre-AREA vs post-AREA byte-IDENTITY for default scene (no `--enable-nee`) — PPM `cmp`    |
| F.2.2 | OptiX pre-AREA vs post-AREA byte-IDENTITY for Point + Directional `--enable-nee` scene          |
| F.2.3 | OptiX-vs-CUDA convergence parity at the area-light fixture (statistically similar; not bit)     |
| F.2.4 | OptiX raygen + AREA-arc: regression on Stage 19 / 20 OptiX denoiser fixtures                    |

### F.3 Expected future fixture scenes

The AREA.6 slice (per §G) ships the
fixture scenes the operator session
runs against. Anticipated fixtures:

- `area_light_smoke.rrscene` — single
  Lambert plane below a single
  rectangular area light. Analytic
  expected radiance (uniform). Smallest
  possible regression anchor.
- `area_light_cornell.rrscene` —
  Cornell-style box with a ceiling
  light authored as a rectangular area
  light. Cross-tests against the
  legacy emissive-mesh ceiling
  (after the post-AREA "emissive-mesh"
  arc lands; for AREA, the fixture
  exercises the rectangle path
  exclusively).
- `area_light_mixed.rrscene` —
  Point + Directional + Area lights
  in the same scene. Verifies the
  delta / non-delta MIS coexistence.
- `area_light_oblique.rrscene` —
  Receiver and area light at high
  oblique angle. Stresses the
  `cos(theta_light)` → 0 edge of the
  Jacobian denominator.

Each fixture pairs with a golden PPM
captured during the AREA-arc operator
session.

### F.4 Carry-forward debt

The AREA arc inherits the MIS arc's
DEFERRED runtime ledger (per
`PATH_TRACER_MIS_ARC_AUDIT.md` §8.3) —
six rows still open from MIS / NEE /
PT-P / firefly-clamp arcs. AREA adds
F.1 + F.2 (8 new rows). The single
CUDA + OptiX-SDK operator session
recommended at MIS.7 §8.3 can flip ALL
of them in one cycle if scheduled
after AREA.7 closes. Recommended
sequencing:

1. AREA.{1..6} ship as commits on
   `area-light-arc`.
2. AREA.7 audit closes the arc with
   PASS (mirroring MIS.7's
   PASS_WITH_SUBSUMED_AUDITS shape).
3. The post-AREA operator session
   flips MIS.7 §8.2.{1..6} + AREA
   §F.1.{1..3} + §F.2.{1..2} (9
   structural-byte-identity rows) to
   PASS in lockstep.
4. The same session runs §F.1.4 +
   §F.2.3 + §F.2.4 + the AREA fixture
   scenes for convergence /
   correctness anchors.

---

## G. Proposed implementation slices

This section enumerates the slice
order. Each slice is a single
self-contained commit (or a small
number of commits — task-brief +
impl + audit per the NEE / MIS
cadence). Each lands in topological
dependency order; no slice
implements anything its predecessors
have not yet shipped.

### G.1 AREA.1 — Data model

**Scope.** Verify the existing data
model's forward-compatibility with
the AREA arc and ship a
`pathtracer/AreaLight.h` module
holding the new RR_HD helpers (
`intersect_area_light_rectangle`,
`area_light_solid_angle_pdf`,
`sample_area_light_surface`).

**Key principle.** Per §B.1 the AREA
arc adds **zero new POD fields**.
AREA.1 ships only the helper module;
no `Light` / `MaterialParams` /
`DirectLightSample` field
additions.

**Deliverables.**

- `src/pathtracer/AreaLight.h` (RR_HD
  inline helpers).
- Host-only tests in
  `tests/pathtracer_area_light_tests.cpp`:
  - rectangle intersection (front/back
    face, oblique angle, miss).
  - solid-angle PDF (Jacobian
    correctness across r, area, and
    cos_theta_light).
  - surface sampler (uniformity over
    the rectangle).
- `BUILD_PLAN.md` entry.

**Byte-identity invariant.** No
runtime impact — the new helpers are
not invoked by any caller in this
slice. **Default-OFF and default-ON
PPM bytes unchanged from `cee451e`.**

**Slice size budget.** ≤ ~250 lines
across helper + tests; no source-
under-`src/cuda` or `src/optix`
modification.

### G.2 AREA.2 — Parser

**Scope.** Promote `direction` +
`area_width` + `area_height` from
PLACEHOLDER to required-when-area in
the scene parser. Reject malformed
area-light declarations with the
existing schema-error message
pattern.

**Deliverables.**

- `src/io/SceneLoader.cpp` —
  `apply_light` extension
  (lines around 1241).
- Tests in
  `tests/scene_loader_area_light_tests.cpp`:
  - happy path (well-formed area
    light parses).
  - missing `area_width` rejects.
  - non-positive `area_height`
    rejects.
  - missing `direction` rejects.
- `docs/RRSCENE_FORMAT.md` —
  remove "PLACEHOLDER" annotations
  from `area_width` / `area_height`
  rows in the schema table; promote
  to "required for `area`" only.
- `BUILD_PLAN.md` entry.

**Byte-identity invariant.** Existing
*well-formed* scenes (Point +
Directional + Area-with-all-fields)
parse identically. The CHANGE is
that ill-formed area-light
declarations (missing required
fields, previously silently parsed
to zero-contribution) now reject
the file. This is a **deliberate
strictening**, not a regression —
the schema spec already required
those fields.

**Slice size budget.** ≤ ~150 lines
across parser + tests + spec doc.

### G.3 AREA.3 — GPU upload

**Scope.** Verify (no new code) that
the existing `GpuScene::upload_lights`
path correctly transports the
parser-populated area-light fields to
device memory. Add a host-side round-
trip test anchoring the invariant.

**Deliverables.**

- `tests/gpu_scene_area_light_upload_
  tests.cpp` — host-only memcmp on
  the uploaded `Light` array's POD
  contents. Uses the
  `RR_ENABLE_CUDA = OFF` audit-host
  build's host-resident `GpuBuffer`
  fallback (no CUDA device needed).
- `BUILD_PLAN.md` entry.

**Byte-identity invariant.** None
broken — this slice adds tests, no
source.

**Slice size budget.** ≤ ~100 lines
test code only.

### G.4 AREA.4 — CUDA sampling + integrator

**Scope.** This is the principal
slice. Three deltas:

1. Add the `Area` branch to
   `pathtracer/DirectLight.cuh::sample
   _direct_light_uniform` (replace
   the bit-zero default-return for
   `LightType::Area` with the real
   sampler from §C.1–§C.3).
2. Extend the CUDA closest-hit walk
   (`closest_hit_pt` in
   `CudaPathTracer.cu`) with the
   area-light rectangle loop from
   §C.4. Set a `Hit::is_area_light`
   flag + `Hit::area_light_idx`.
3. Extend the CUDA integrator
   (`k_pathtrace_sample`) post-
   walk with the BSDF-bounce-as-
   light contribution from §C.4.

**Deliverables.**

- `pathtracer/DirectLight.cuh` —
  `Area` branch (~60 lines).
- `cuda/CudaPathTracer.cu` —
  closest-hit loop extension
  (~25 lines) + integrator
  post-walk (~30 lines).
- `renderer/Hit.h` — add
  `is_area_light` + `area_light_idx`
  fields. (Bit-zero defaults; matches
  the existing pattern.)
- Host-only tests:
  - extended
    `tests/pathtracer_nee_tests.cpp`
    with an Area-branch case
    (already-zero-contribution
    edge cases preserve
    bit-zero defaults; non-zero
    Jacobian computed correctly).
  - new
    `tests/pathtracer_area_light_
    integrator_tests.cpp` —
    closed-form expected radiance
    against a known-geometry
    fixture.
- `BUILD_PLAN.md` entry.

**Byte-identity invariant.** The
**non-area** subset of every scene's
NEE branch is preserved. Specifically,
the helper's `Point` + `Directional`
branches are not touched; the
integrator's NEE path for non-area
lights is byte-identical with the
post-MIS.6 baseline.

The **area** subset of every scene
WILL produce different output — this
is the deliberate behavioural change.

**Slice size budget.** ≤ ~400 lines
of source code + ~250 lines of test
code (the largest slice in the
arc).

### G.5 AREA.5 — OptiX sampling + integrator

**Scope.** Mirror of AREA.4 on the
OptiX side. The
`pathtracer/DirectLight.cuh` change
from AREA.4 is consumed unchanged
(same RR_HD helper). The OptiX-side
deltas are:

1. The OptiX raygen
   (`__raygen__pathtrace` in
   `OptixPrograms.cu`) gains the
   post-`optixTrace` area-light
   intersection loop per §B.5
   Strategy B.
2. Closest-of-{GAS-hit,
   implicit-area-light-hit}
   disambiguation.
3. BSDF-bounce-as-light contribution
   (matching CUDA).

**Deliverables.**

- `optix/OptixPrograms.cu` —
  raygen extension (~100 lines).
- (no `OptixAccel` / SBT changes;
  Strategy B per §B.5).
- Host-only / device-only test
  parity matrix
  (`tests/optix_pathtracer_area_
  light_tests.cpp`).
- `BUILD_PLAN.md` entry.

**Byte-identity invariant.** Same as
AREA.4 — non-area scenes are byte-
identical with the post-MIS.6
OptiX baseline. Cross-backend
convergence parity (CUDA-vs-OptiX
PPM) for area-light scenes is
DEFERRED to the operator session
(§F.2.3).

**Slice size budget.** ≤ ~250 lines
of source + ~200 lines of test
code.

### G.6 AREA.6 — Fixture scenes + golden images

**Scope.** Ship the four fixture
scenes from §F.3 with their golden
PPMs, capture-able from the operator
session.

**Deliverables.**

- `scenes/area_light_smoke.rrscene`.
- `scenes/area_light_cornell.rrscene`.
- `scenes/area_light_mixed.rrscene`.
- `scenes/area_light_oblique.rrscene`.
- (golden PPMs DEFERRED — captured
  by the operator session and
  committed in a follow-up slice
  outside the AREA arc).
- `BUILD_PLAN.md` entry.

**Byte-identity invariant.** None
applicable — these are new fixtures.

**Slice size budget.** ≤ ~150 lines
of scene-file content.

### G.7 AREA.7 — Audit

**Scope.** Closing arc-level audit,
mirroring MIS.7's
`PATH_TRACER_MIS_ARC_AUDIT.md` shape.
Walks the user-enumerated checks
across the AREA.{1..6} deliverables
+ records a closing PASS / REPAIR /
BLOCKED verdict.

**Deliverables.**

- `docs/PATH_TRACER_AREA_LIGHT_ARC_
  AUDIT.md`.
- `BUILD_PLAN.md` entry recording
  the arc-level audit landing.

**Byte-identity invariant.** None —
documentation only.

**Slice size budget.** ≤ ~1200 lines
(comparable to MIS.7's 1276-line
audit).

**Sub-arc closure.** After AREA.7
lands:

- Area lights are first-class. The
  scene format, parser, GPU upload,
  CUDA + OptiX integrators, and
  test fixtures all exercise them.
- The MIS apparatus is fully
  utilised — both `is_delta` and
  `pdf_solid_angle` populated and
  consumed.
- The "area-light arc" item from
  MIS.7 §9.1 is closed; the
  remaining MIS.7 §9.2 ("non-
  Lambert BSDFs") item is the
  natural successor arc.
- The runtime DEFERRED ledger
  carries 9 rows ready for a
  CUDA + OptiX-SDK operator
  session.

---

## H. Non-goals (out of arc scope)

The AREA arc does NOT ship:

- **Non-Lambert BSDFs.** GGX / Beckmann
  microfacet samplers, dielectric
  Fresnel, specular-delta gating —
  reserved for a separate post-AREA
  arc per MIS.7 §9.2.
- **General emissive-mesh lighting.**
  Arbitrary mesh acts as an area
  light. Per §B.1 this is deferred —
  the AREA arc handles rectangular
  area lights *only*. A future
  "emissive-mesh" arc can promote any
  mesh to a light source via a
  per-mesh `is_emissive_area_light`
  flag and a CDF-by-area sampler over
  the mesh's triangles.
- **Environment-light sampling.**
  Image-based lighting + IBL
  sampling — joins with the texture
  system (M16) per
  `MILESTONE_ROADMAP.md`. Today's
  flat sky tint is preserved
  unchanged.
- **Russian roulette.** Variance
  reduction at deeper bounces;
  reserved for a separate post-AREA
  arc per MIS.7 §9.2 #5.
- **Texture-sampled emission.**
  Spatially-varying emission across
  the area light's surface — joins
  with the texture system.
- **Multi-mesh / arbitrary primitive
  area lights.** Disks, spheres,
  tubes — all rectangle-only at
  AREA scope.
- **CLI flag changes.** No new
  flags. Existing `--enable-nee`
  continues to gate the NEE branch.
- **Cinema 4D bridge / preview UI /
  node editor / server.** Master
  rule 4 + the dependency rules of
  `RELATIVITYRENDER_CLAUDE_MASTER_
  INSTRUCTIONS.txt`. No work past
  master order #16 in this arc.

---

## I. PASS criteria (for future implementation)

This section is the canonical PASS
contract every AREA.{1..6} slice
must satisfy. The AREA.7 audit
walks against it.

### I.1 Build

- Audit-host build (`RR_ENABLE_CUDA =
  OFF`, `RR_ENABLE_OPTIX = OFF`):
  passes after each slice.
- CUDA-host build
  (`RR_ENABLE_CUDA = ON`,
  `RR_ENABLE_OPTIX = OFF`): passes
  after each slice that touches
  CUDA source (AREA.4).
- OptiX-SDK-host build (both ON):
  passes after each slice that
  touches OptiX source (AREA.5).
- Zero new compiler warnings.

### I.2 Tests

- Every existing `ctest` test
  continues to pass after each
  slice.
- Each AREA slice that ships test
  code adds those tests to the
  ctest registry.
- AREA arc total: at least 12
  new host-only tests
  (4 intersection + 4 PDF + 4
  integrator).

### I.3 Source diff size

Per-slice diff budgets enumerated
in §G. Total AREA arc diff:
≤ ~1500 source lines + ~700 test
lines (excluding AREA.7 audit
text).

### I.4 Default-OFF byte-identity

For any scene **without
LightType::Area** lights:

- CUDA `--enable-nee` + AREA-OFF
  PPM: bit-identical with `cee451e`
  baseline.
- CUDA no-`--enable-nee` PPM:
  bit-identical with `cee451e`.
- OptiX same as CUDA.

### I.5 Default-ON byte-identity (at v1 light scope)

This invariant is *more nuanced*
than MIS.7's because the AREA arc
*is* the slice that introduces a
new behaviour for `LightType::Area`:

- For scenes with no
  `LightType::Area` lights: the
  byte-identity invariant from §I.4
  applies to the full
  `--enable-nee` ON case. The PPM
  is byte-identical with the
  `cee451e` baseline.
- For scenes with
  `LightType::Area` lights: the
  PPM **changes** by design. The
  pre-AREA result was zero
  contribution from area lights;
  the post-AREA result is the
  unbiased Monte Carlo estimate.

The byte-identity invariant is
maintained by the *non-area*
branch of the integrator; the
behavioural change is scoped to
the *area* branch.

### I.6 No-touch invariants

Per §E.4. The AREA arc audit must
verify zero diffs against:

- `RELATIVITYRENDER_CLAUDE_MASTER_
  INSTRUCTIONS.txt`.
- Any source under
  `bridges/`, `tools/`,
  non-renderer subsystems.
- `MasterParams` /
  `DirectLightSample` /
  `BsdfSample` /
  `Light` PODs (no new fields).

### I.7 Cross-backend symmetry

CUDA + OptiX integrators consume
the same `pathtracer/{DirectLight,
AreaLight,Bsdf,Mis}` modules. Any
divergence in per-pixel arithmetic
between the two backends (beyond
the existing OptiX-vs-CUDA RNG
differences documented in the
NEE / MIS audits) is a REPAIR
flag.

### I.8 Documentation

- `docs/RRSCENE_FORMAT.md` updated
  to remove `area_width` /
  `area_height` PLACEHOLDER
  annotations (AREA.2).
- `docs/PATH_TRACER_AREA_LIGHT_
  PLAN.md` (this doc) updated only
  if the plan's design changes
  during implementation.
- `docs/PATH_TRACER_AREA_LIGHT_
  ARC_AUDIT.md` (AREA.7) shipped
  at arc closure.
- `docs/MILESTONE_ROADMAP.md`:
  updated to reflect M12 (Lighting
  System) status promotion from
  "foundation landed" to "partial
  implementation" (AREA.7's
  responsibility).

### I.9 Master rule compliance

- **Rule 1 (build incrementally):**
  Each AREA.{1..7} slice is a
  self-contained delta.
- **Rule 2 (compilable):** Every
  slice keeps the project
  buildable.
- **Rule 3 (no fake stubs):** No
  AREA slice implements a
  placeholder that appears
  complete but isn't. The
  rectangle-only area-light scope
  is documented as such; it is
  not advertised as general
  emissive-mesh support.
- **Rule 5 + 7 (no CPU per-pixel
  / GPU-only ray tracing):** The
  rectangle intersection helpers
  are RR_HD inline; the kernels
  invoke them on the device. Host
  tests exercise the same code
  paths but only for unit
  validation.
- **Rule 8 (BUILD_PLAN.md):**
  Each AREA slice adds an entry.
- **Rule 9 (clean module
  boundaries):** New helpers live
  in `pathtracer/AreaLight.h`
  alongside the existing
  pathtracer modules.
- **Rule 10 (avoid monolithic
  files):** AREA.4's
  `CudaPathTracer.cu` extension
  is ≤ ~55 new lines (well below
  the threshold that would
  warrant splitting the file).
  The same applies to AREA.5's
  OptiX raygen.
- **Rule 11 (testable interfaces):**
  Every new helper has at least
  one host-only test.
- **Rule 12 (no overbuilding):**
  Non-Lambert BSDFs, emissive
  meshes, environment maps, and
  Russian roulette are all
  deferred to separate arcs.

---

## J. Recommended next step (post-AREA-arc)

After AREA.7 closes, the path-tracer
foundation (M14) advances from
"partial implementation" to "in
progress" with the lighting system
(M12) likewise promoted. Three
viable directions, in plan-recommended
order:

1. **Trigger the CUDA + OptiX-SDK
   operator session** flipping
   AREA's §F.1 + §F.2 + the
   accumulated MIS / NEE / PT-P
   DEFERRED rows. One session
   pins the post-AREA byte-identity
   baseline + correctness for
   every arc shipped to date.
2. **Pivot to the non-Lambert
   BSDF arc** (per MIS.7 §9.2).
   The MIS apparatus + AREA
   apparatus together cover every
   light type the arc needs;
   GGX / dielectric / Fresnel are
   the missing pieces.
3. **Pivot to the emissive-mesh
   arc** — generalise area lights
   from rectangles to arbitrary
   meshes. Adds a per-mesh
   `is_emissive_area_light` flag
   per MIS.7 §9.1 and a
   CDF-by-area sampler over mesh
   triangles. Less expressive than
   path (2) but keeps the arc
   unblocked while a future BSDF
   arc lands.

The plan-recommended sequence is
**(1) first, (2) second, (3)
third** — operator validation
before further feature growth.

---

## K. Verdict (planning slice)

This document is the AREA arc
opener. No source code is modified.
No implementation begins. The plan
records the design + slice order +
PASS criteria + invariants the
future AREA.{1..7} slices must
satisfy.

The plan supersedes nothing. It
extends the MIS arc's
`PATH_TRACER_MIS_PLAN.md` by
delivering the area-light arc
that MIS deliberately deferred. It
inherits the runtime DEFERRED
ledger from prior arcs and adds
its own §F rows.

Approval to proceed implies
authorization to ship AREA.1 (the
data-model verification + helper
module) as the first impl slice.
Each subsequent slice is its own
approval gate.

**Mode reminder: documentation only.**
This planning slice makes zero
source-code changes. The REPAIR
list is empty. The BLOCKED list is
empty. The AREA arc opens with
PLAN_READY.

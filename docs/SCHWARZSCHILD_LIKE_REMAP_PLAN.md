# Schwarzschild-Like Coordinate Remap Plan (MANI-I.10)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this design document; the implementation lands
        in subsequent SCHW.* sub-slices that consume this
        doc as their canonical brief.

This document is the design for **MANI-I.10 —
Schwarzschild-like artistic coordinate remap** under the
twelve-slice integration plan
(`docs/MANIFOLD_INTEGRATION_PLAN.md` §8). It is the
operator-facing brief the SCHW.* implementation slices
will read to decide the exact math, the safety
constraints, and the integration strategy. The plan does
not modify code; it names the sub-slices that follow.

Prerequisite slices already green:

- **MANI-I.1 / .2** — CLI config + audit
  (`1bb1fb4` / `799a9ac`).
- **MANI-I.3 / .4** — Render config bridge + audit
  (`d677b2c` / `4c73d1b`).
- **MANI-I.5 / .6** — Euclidean identity GPU path +
  audit (`a34e265` / `fef3e50`).
- **MANI-I.7 / .8 / .9** — Manifold Debug AOV task
  definition + implementation + audit
  (`f6d16b9` / `094306f` / `b4ed22e`).

---

## 1. Scope

**MANI-I.10 ships an artistic, Schwarzschild-inspired
coordinate-remap layer that selects the first
non-Euclidean entry of the `CoordinateChartType` enum
(`SchwarzschildLike`).** It is the first slice that
introduces real curved-chart math after the eight
prerequisite slices established the manifold POD
surface, the CLI / config / GPU-side plumbing, and the
debug AOV.

The slice is:

- **Artistic, not physical.** The remap is a closed-
  form coordinate transform inspired by the
  Schwarzschild solution's radial coordinate
  compression near the horizon, NOT a ray-traced
  null-geodesic integration of the Schwarzschild
  metric. Architecture-doc §8 non-goals "physically
  exact Kerr ray tracing" and "full GR solver"
  remain in force; the SchwarzschildLike chart is
  named `*Like` precisely to flag this honesty
  (per the master-rule-#3 convention
  MANIFOLD.1 established).
- **A coordinate-remap layer.** The transform
  changes how chart-space coordinates relate to
  world-space coordinates; it does NOT change the
  path tracer's BSDF / NEE / MIS / RR machinery,
  it does NOT change the OptiX denoiser pipeline,
  and it does NOT introduce new BVH or
  acceleration-structure machinery.
- **Off by default.** The chart engages only when the
  operator passes both `--manifold-enable` AND
  `--manifold-chart schwarzschild-like` (per the
  CLI surface MANI-I.1 shipped). The Euclidean
  default remains the documented "no output change"
  anchor and every existing CLI action's beauty
  output is byte-identical to the pre-MANI-I.10
  baseline when those flags are absent.

What this slice deliberately is NOT:

- **Not physical Schwarzschild ray tracing.** No
  Christoffel symbol evaluation; no geodesic ODE
  integrator; no event-horizon-crossing physics
  beyond a simple clamp.
- **Not the kernel-side path-tracer integrator
  rewrite.** The path tracer's bounce loop, BSDF
  sampling, NEE / MIS, firefly clamp, and Russian
  roulette continue to operate on world-space rays.
  Only the primary-ray direction (optionally) and
  the chart-space hit position (always) get
  remapped.
- **Not the FIELD.x curvature-AOV programme.**
  Kretschmann-scalar and other curvature
  invariants belong to the Field Interpretation
  Layer (`docs/FIELD_INTERPRETATION_LAYER.md` §6);
  SCHW.9 (debug visualization) reuses the existing
  `ManifoldCoordinates` AOV slot, not a new
  curvature slot.
- **Not a `.rrscene` schema bump.** The mass
  position, radius, falloff, and clamp parameters
  ride on the existing `CoordinateChart` + 
  `CoordinateChartParameters` PODs the MANIFOLD.1
  slice already shipped (with documented per-chart
  reinterpretation of the parameter slots, §3
  below).

---

## 2. Core idea

The chart's `world_to_chart(p_world)` map is a closed-
form coordinate deformation centred on a configured
mass origin. Far from the mass, the map is the
identity; close to the configured `r_s` (Schwarzschild-
like radius), points are radially displaced in a way
that mimics the coordinate compression Schwarzschild
charts exhibit near the horizon. The deformation is
bounded by a `clamp_radius` so the map stays finite at
the origin.

```
   r = max(|p_world - mass_origin|, clamp_radius)
   f = warp_strength * r_s / r^falloff
   chart_pos = p_world + f * (p_world - mass_origin)
```

Properties (verifiable analytically):

- `warp_strength = 0` ⇒ `f = 0` ⇒ `chart_pos = p_world`
  (the **Euclidean fallback**, identical to the
  Euclidean chart's identity map).
- `r → ∞` ⇒ `f → 0` ⇒ `chart_pos → p_world`
  (the **far-field identity** — far away from the
  mass the chart is indistinguishable from
  Euclidean, so a render with `--manifold-chart
  schwarzschild-like` shows the configured mass-
  centric region as warped while preserving the
  rest of the image).
- `r = clamp_radius` ⇒ `f = warp_strength * r_s /
  clamp_radius^falloff` (a documented finite
  maximum displacement; no division by zero, no
  NaN, no infinity).
- `p_world = mass_origin` ⇒ the formula reduces to
  `chart_pos = mass_origin` (with the `r = max(0,
  clamp_radius) = clamp_radius` substitution
  preventing the singularity; the displacement
  vector `p_world - mass_origin` is zero so no
  amount of `f` produces a NaN).

The chart preserves the **light transport pipeline**:

- The path tracer's BSDF / NEE / MIS machinery
  continues to operate on world-space rays and
  world-space hit positions. The Schwarzschild-like
  chart only modifies (a) the chart-space hit
  position written to the `ManifoldCoordinates`
  AOV via `world_to_chart`, and (b) optionally the
  primary-ray direction via a separate
  ray-direction warp (see §6.2 below). It does NOT
  modify bounce rays, shadow rays, or any
  secondary ray's direction.

The chart preserves the **renderer's existing
substrate**:

- No new GPU resource. No new acceleration
  structure. No new SBT record type. No new
  denoiser input. The new math runs entirely in the
  device-side helper that the kernel calls per
  pixel; the launch surface gains zero new fields
  beyond what MANI-I.5 / MANI-I.8 already wired.

---

## 3. Required parameters

The slice's six artist-facing parameters map onto the
manifold module's existing POD surface — no schema
bump on `CoordinateChart` / `CoordinateChartParameters`
/ `ManifoldMode` is required. The mapping is
**documented per chart family** (master rule #3:
honest reinterpretation, not silent reuse).

| Plan parameter            | Source field                                          | Range / default                                                                                                          | Notes                                                                                                                                            |
|---------------------------|-------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------|
| `massOrigin`              | `CoordinateChart::origin` (`Vec3`)                    | Default `(0, 0, 0)` (scene origin).                                                                                      | Reused as-is. For the Euclidean chart this is the chart's spatial origin; for SchwarzschildLike it is the location of the configured mass.       |
| `schwarzschildRadiusLike` | `CoordinateChartParameters::mass` (`float`)           | Range `[0, ∞)`; default `0.0` (Euclidean fallback regardless of `warp_strength`).                                       | Reinterpreted as the "event horizon proxy" radius. The MANIFOLD.1 doc-comment of `mass` already documents this reinterpretation per chart type.   |
| `warpStrength`            | `ManifoldMode::strength` (`float`)                    | Range nominally `[0, 1]`; out-of-range values pass through unchanged per the existing `ManifoldMode::strength` contract. | Reused as-is. `0` is the Euclidean fallback; `1` is the maximum artistic warp the formula allows under the clamp.                                |
| `falloff`                 | `CoordinateChartParameters::spin` (`float`)           | Range `[0.5, 4.0]`; default `1.0`.                                                                                       | Reinterpreted for SchwarzschildLike. Exponent in `1 / r^falloff`. `1.0` matches Newtonian gravity's far-field; `2.0` is more localised.          |
| `clampRadius`             | `CoordinateChartParameters::compactification_scale`   | Range `(0, ∞)`; default `1.0` (matches the existing field's default).                                                   | Reinterpreted for SchwarzschildLike. Minimum `r` the formula uses; prevents the `1/r` singularity at the mass origin.                            |
| `debugMode`               | `ManifoldMode::debug_visualization` (`bool`)          | Default `false`.                                                                                                         | Reused as-is. Drives whether the `--render-aovs` action emits `output/aov_manifold_coordinates.ppm` per the MANI-I.8 wiring.                     |

Notes on reinterpretation:

- `CoordinateChartParameters::spin` is *named* spin
  because Kerr-like usage will eventually want it
  as the dimensionless spin parameter `a`. For
  SchwarzschildLike, the field carries the falloff
  exponent because Schwarzschild-like has no spin
  and the slot is otherwise unused. Future Kerr-
  like slices will need a more disciplined per-
  chart parameter struct; today, the documented
  per-chart reinterpretation is honest.
- `CoordinateChartParameters::compactification_scale`
  is *named* compactification because PenroseLike
  usage will use it as the conformal-compactification
  parameter. For SchwarzschildLike, the field
  carries the clamp radius because it would
  otherwise be unused.
- `CoordinateChartParameters::reserved` stays
  unused by this chart. Future expansion (an
  artist-facing "horizon thickness" or "tidal-
  stretching" knob) can claim it without an ABI
  bump.

---

## 4. Expected visual effects

The artist-facing effects this remap is designed to
produce, with the documented analytical limit each
parameter dial reaches:

### 4.1 Radial compression / stretching

- **`warpStrength > 0` + `falloff = 1.0`**: points
  near `r_s` get pushed outward by a factor `~r_s /
  r`, which on the `manifold_coordinates` AOV
  visibly inflates the region around the mass.
- **`warpStrength > 0` + `falloff = 2.0`**: more
  localized effect — only points within ~`r_s` of
  the mass are visibly affected; points further out
  are nearly identity.
- **`warpStrength > 0` + `falloff < 1`**: a
  longer-range effect ("warm" lensing); points far
  from the mass also bend a little.

### 4.2 Pseudo-lensing

When SCHW.7 (OptiX integration) or SCHW.5 (CUDA
integration) opt into the **primary-ray direction
warp** (§6.2 below), primary rays passing near the
mass bend toward it, producing a gravitational-
lensing-like visual signature on the beauty pass.
The effect is bounded by a **hard cap on the bending
factor** (`min(bend_factor, 0.5)`) so the primary
ray never flips direction or terminates inside the
mass; an unaffected silhouette is visible at the
edge of the bending region.

### 4.3 Coordinate crowding near radius

Points whose `r` approaches `clamp_radius` show
visible *crowding* on the `manifold_coordinates`
AOV: the AOV pixel values converge toward the
mass-origin's surroundings, producing a visually
recognizable "compressed" region. This is the
Schwarzschild-like analog of how Schwarzschild
charts compress radial coordinates near the
horizon (mathematically distinct, but visually
analogous).

### 4.4 Horizon-like visual boundary

The `clampRadius` parameter doubles as a soft
**horizon proxy**: pixels with `r ≤ clamp_radius`
are clamped to the same `r = clamp_radius` for
deformation purposes, so the inner region appears
as a *uniform-warp shell* on the AOV. This is
**artistic, not physical**: a real Schwarzschild
event horizon traps light; the SchwarzschildLike
chart's clamp does not. The visual cue is the
clamp shell's edge.

### 4.5 What is NOT produced

- **No photon-sphere ring**. The SchwarzschildLike
  chart does not integrate null geodesics; the
  photon-sphere visual signature requires a real
  geodesic solver.
- **No frame-dragging**. SchwarzschildLike has
  zero spin by construction (the `spin` field is
  reinterpreted as `falloff` for this chart).
- **No back-side-of-horizon emergent rays**. The
  clamp shell hides the inner region; rays that
  would mathematically escape from "behind" the
  horizon in a real Schwarzschild spacetime do
  not appear here.

---

## 5. Safety constraints

The slice must satisfy six safety invariants. Each
maps to a concrete implementation requirement that
SCHW.* sub-slices verify.

### 5.1 No singularities / NaN / Inf

- The formula's `r` denominator uses `max(r,
  clampRadius)` with `clampRadius > 0` documented
  as a positive lower bound. The implementation
  must `static_assert` (or runtime-clamp) on
  `clampRadius > 0` at the host-side validator.
- `pow(r, falloff)` for negative or zero `r` is
  prevented by the `max(r, clampRadius)` guard;
  `falloff` itself is range-clamped to `[0.5,
  4.0]` at the host validator so the exponent
  cannot drive `r^falloff` to underflow.
- The displacement scalar `f = warp_strength * r_s
  / r^falloff` is bounded above by `warp_strength
  * r_s / clamp_radius^falloff`. The host validator
  surfaces this maximum to the operator as part of
  the chart configuration log line.

### 5.2 Preserve Euclidean fallback

- `warp_strength = 0` ⇒ `f = 0` ⇒ identity
  transform (verifiable analytically).
- `ManifoldMode.enabled = false` ⇒ `is_active(m)`
  returns `false` ⇒ kernel skips the
  Schwarzschild-like arm entirely and writes the
  world-space hit position to the AOV (the same
  output the Euclidean default produces today per
  MANI-I.8).
- The Euclidean fallback is verified by SCHW.3 (CPU
  integration) with a bit-identity test: at
  `warp_strength = 0`, `world_to_chart` returns
  its input byte-for-byte.

### 5.3 Bounded transforms only

- The chart's `world_to_chart` displacement is
  bounded by `warp_strength * r_s /
  clamp_radius^falloff` (proven analytically; no
  empirical clamp needed).
- The optional primary-ray direction warp
  (§6.2) hard-caps the bending factor at
  `0.5` so the ray's direction cannot flip.
- No iterative integration; the formula is
  closed-form so there is no "step count" to
  bound.

### 5.4 Reversible (chartToWorld inverse)

- The Schwarzschild-like remap is **not strictly
  invertible** for arbitrary parameter choices:
  the formula
  `chart_pos = p_world * (1 + warp_strength * r_s
  / r^falloff)` is a self-map of R³ whose inverse
  requires solving a transcendental equation in
  the general case.
- For SCHW.1 (math helper), the plan ships:
  (a) the exact forward map `world_to_chart`;
  (b) an **approximate inverse**
  `chart_to_world` that runs ~3 Newton-Raphson
  iterations on the radial equation, bounded by a
  hard iteration cap (so the kernel always
  terminates). The approximation's accuracy is
  documented (residual error ≤ `1e-4` at typical
  parameter ranges).
- The approximate inverse's accuracy gates SCHW.11
  (audit): the audit runs a forward+inverse
  round-trip on representative input points and
  verifies the residual is bounded.

### 5.5 Bit-identity on the Euclidean off-path

- Every existing CLI action without
  `--manifold-enable` continues to use the
  pre-MANI-I.10 code path. The
  `is_active(manifold_mode)` guard is the entry
  point to the Schwarzschild-like arm; the guard
  short-circuits at `false` on the Euclidean
  default.
- The MANI-I.5 / MANI-I.8 audit findings (no
  kernel arm reads `manifold_mode` on the
  default; the kernel skips the AOV write when
  the pointer is null) extend naturally: SCHW.5
  / SCHW.7 implementations gate their new code
  behind `is_active(...)`, preserving the
  bit-identity invariant.

### 5.6 Defence-in-depth on the parameter validator

- The host-side validator rejects:
  - `r_s <= 0` (would make the chart Euclidean
    silently; better to flag);
  - `clamp_radius <= 0` (NaN risk);
  - `falloff` outside `[0.5, 4.0]` (out-of-range
    artistic risk);
  - `|warp_strength|` not finite (NaN propagation
    risk).
- The validator logs the rejection with a clear
  message and falls back to the documented
  Euclidean default (the same posture
  `--manifold-strength` takes for non-parseable
  inputs in MANI-I.1).

---

## 6. Integration strategy

The chart's runtime surface plugs into the existing
manifold-module helpers MANI-I.1–MANI-I.9 built.
Three integration seams, each with its own SCHW.*
sub-slice.

### 6.1 `world_to_chart(...)` deformation

- The existing `world_to_chart(ManifoldTransform&,
  Vec3)` helper in `src/manifold/ManifoldTransform.h`
  already branches on
  `t.chart.type == CoordinateChartType::Euclidean`
  (passthrough for non-Euclidean today). SCHW.3
  extends the helper with a
  `t.chart.type == CoordinateChartType::SchwarzschildLike`
  arm that calls the new math helper from SCHW.1.
- The `Vec4` overload (spacetime) gets the same
  arm, treating the time component as invariant
  (the SchwarzschildLike chart is static in
  coordinate time).
- The new arm uses the artist parameters per §3's
  reinterpretation table.

### 6.2 `chart_to_world(...)` inverse approximation

- The existing inverse helper in
  `ManifoldTransform.h` gains a SchwarzschildLike
  arm that runs the approximate Newton-Raphson
  inverse from SCHW.1.
- The inverse helper's accuracy is documented;
  callers that need exact inversion (none today)
  must check the residual or bound the parameter
  range.

### 6.3 Optional primary-ray direction warp

- An optional helper `warp_primary_ray_direction(
  origin, world_dir, manifold_mode,
  chart_params)` produces a bending of the primary-
  ray direction toward the mass origin. The
  helper is **only** called by the kernel raygen
  for primary rays — bounce rays and shadow rays
  continue to use world-space directions.
- The bending factor is hard-capped at `0.5` so
  the ray cannot flip; the cap is bypassable only
  by a chart with `warpStrength > 1`, and even
  then the cap holds.
- SCHW.5 (CUDA) and SCHW.7 (OptiX) opt into the
  ray warp at the raygen entry point. The
  primary-ray-only scope is what makes the slice
  tractable: the BSDF / NEE / MIS pipeline
  continues to operate on world-space directions
  end-to-end.

### 6.4 Debug AOV interaction

- The existing `ManifoldCoordinates` AOV
  (MANI-I.8) consumes the `world_to_chart`
  result on every hit. With the SchwarzschildLike
  chart engaged, the AOV's pixel values **diverge
  from the world-space hit positions** in a way
  that visualises the chart's coordinate
  deformation per pixel — the documented purpose
  of the debug AOV per the MANI-I.8 task
  definition.
- SCHW.9 (debug visualization) refines the AOV's
  encoding for SchwarzschildLike: rather than
  raw chart-space hit position, the slice may
  emit the *displacement vector* `chart_pos -
  world_pos` for a clearer visual signal. The
  AOV's component count stays at 3 floats per
  pixel; the encoding choice is documented in
  the slice's audit.

---

## 7. Runtime-deferred CUDA / OptiX checks

The audit-host build (no CUDA, no OptiX SDK) cannot
directly verify the Schwarzschild-like remap's
visual output. The runtime checks below are
DEFERRED behind the audit-host's existing
no-CUDA / no-OptiX-SDK fallback, matching the
`MANI-I.6` / `MANI-I.9` per-slice audit posture
(`docs/STAGE_19_DENOISER_AUDIT.md` Q1 / Q2 rubric).

Each deferred check must be exercised on a CUDA +
OptiX-SDK host before SCHW.11 (audit) closes the
chart's per-slice gate:

### 7.1 Euclidean fallback bit-identity (CUDA)

Run every pre-MANI-I.10 reference render with
`--manifold-enable --manifold-chart euclidean`:
- `--render-pathtrace scenes/test_relativity.rrscene`
- `--render-scene`
- `--render-mesh-scene`
- `--render-material-scene`
- `--render-direct-lighting`
- `--render-aovs scenes/test_full_scene.rrscene`
- `--render-aovs --manifold-debug scenes/test_full_scene.rrscene`

Verify every output PPM is byte-identical to the
pre-MANI-I.10 reference. The Schwarzschild-like arm
is structurally guarded by
`is_active(manifold_mode)`, which returns `false`
for `chart == Euclidean`; the new arm is not
reached.

### 7.2 Euclidean fallback bit-identity (OptiX)

Same scope as §7.1 but with `--render-optix-*`
actions. The OptiX kernel arms for the
Schwarzschild-like remap gate on
`is_active(launch_params.manifold_mode)`; on the
default they short-circuit and the existing
program output is unchanged.

### 7.3 `warp_strength = 0` byte-identity

Run with `--manifold-enable --manifold-chart
schwarzschild-like --manifold-strength 0`. The
formula `f = warp_strength * r_s / r^falloff` is
zero everywhere, so `chart_pos == world_pos` and
the output is byte-identical to the
`--manifold-chart euclidean` baseline.

### 7.4 Visual signature on the AOV

Run with `--render-aovs --manifold-enable
--manifold-chart schwarzschild-like
--manifold-strength 1.0 --manifold-debug`. The
`output/aov_manifold_coordinates.ppm` should
visibly diverge from world-space hit positions in
a documented signature:
- Radial compression near the mass origin.
- Far-field identity (the corner pixels'
  AOV values match world-space positions).
- A documented clamp shell at `r = clampRadius`.

The reference AOV PPM is pinned by SCHW.9 + SCHW.11
on a CUDA + OptiX-SDK host.

### 7.5 Beauty-pass lensing signature

Run with the primary-ray-direction warp enabled
(see §6.2). The beauty pass should show a
documented lensing signature: a sphere at known
position behind the mass should appear "stretched"
toward the lensing edge. The reference PPM is
pinned by SCHW.11 audit.

### 7.6 Off-chart non-regression

Run a non-Schwarzschild render with
`--manifold-enable --manifold-chart kerr-like`
(reserved-but-inert per MANIFOLD.1). The
SchwarzschildLike arm is gated on
`chart_type == SchwarzschildLike`; for
`KerrLikePlaceholder` the helper falls through to
the documented passthrough. Output is
byte-identical to the Euclidean default.

---

## 8. Proposed slices (SCHW.* sub-slice ladder)

Six sub-slices, each one its own commit with its
own audit gate. The chain is strict-prefix; each
slice ships only after its predecessor is green.

### SCHW.1 — Math helper (impl, math-leaf)

- **Scope:** add a new header
  `src/manifold/SchwarzschildLikeMath.h` (or
  similar) containing the closed-form math
  helpers:
    - `RR_HD inline Vec3 schwarzschild_like_world_to_chart(
       p_world, mass_origin, r_s, warp_strength,
       falloff, clamp_radius);`
    - `RR_HD inline Vec3 schwarzschild_like_chart_to_world(
       chart_pos, ...);` (Newton-Raphson inverse,
      bounded iteration count).
    - `RR_HD inline Vec3 schwarzschild_like_warp_ray_direction(
       origin, world_dir, mass_origin, r_s,
       warp_strength);` (optional primary-ray
      warp; capped at `0.5` bending factor).
    - `RR_HD inline bool schwarzschild_like_validate_params(
       r_s, warp_strength, falloff,
       clamp_radius);`
- **Acceptance:**
  - Audit-host `g++ -std=c++20 -Isrc -Wall -Wextra
    -Werror` build of an
    `SchwarzschildLikeMath.h`-only TU compiles
    cleanly.
  - Analytic checks: `warp_strength = 0` ⇒
    `world_to_chart` returns input exactly;
    `r → ∞` ⇒ displacement ≤ `1e-6`;
    `world_to_chart ∘ chart_to_world` residual
    ≤ `1e-4` for representative parameter
    sweeps.
- **What does NOT ship:** no
  `ManifoldTransform.h` change yet; no kernel
  code change; no test binary other than the
  standalone compile check.

### SCHW.2 — Audit (docs only)

- **Scope:** per-slice gate for SCHW.1. Writes
  `docs/SCHWARZSCHILD_LIKE_WARP_AUDIT.md` verifying
  the seven structural items: Euclidean fallback
  exists; transforms are bounded; no singularity
  generation; clamping behavior documented;
  build / test green; no renderer behavior
  changed yet; verdict.
- **Acceptance:** all seven checks PASS; the
  audit-host build remains at the post-SCHW.1
  baseline (`100% tests passed, 0 tests failed
  out of 12`; `manifold_identity_tests: 140 /
  140 checks passed`).
- **What does NOT ship:** no source code; no
  test binary changes; no CMake change. The
  audit shifts the SCHW.* sub-slice numbering
  by `+1` from the original plan
  (CPU integration moves SCHW.2 → SCHW.3, etc.;
  the final audit slot moves SCHW.9 → SCHW.11).

### SCHW.3 — CPU integration (impl, host-only)

- **Scope:** extend
  `src/manifold/ManifoldTransform.h`'s
  `world_to_chart` / `chart_to_world` /
  `transform_ray_like_direction` helpers with
  the `SchwarzschildLike` arm. The Vec3 and Vec4
  overloads both gain the new arm. The chart's
  per-pixel evaluation happens through the SCHW.1
  math helpers.
- **Acceptance:**
  - Audit-host build green.
  - `manifold_identity_tests` (12-binary ctest)
    gains assertions that
    `world_to_chart(schwarzschild_chart,
    p_world)` returns `p_world` when
    `warp_strength = 0`, and produces
    documented non-identity outputs at
    `warp_strength > 0` on a representative
    input.
- **What does NOT ship:** no CUDA/OptiX kernel
  consumption; no AOV change; no beauty-pass
  change.

### SCHW.4 — Audit (docs only)

- **Scope:** per-slice gate for SCHW.3. Writes
  `docs/SCHWARZSCHILD_LIKE_CPU_INTEGRATION_AUDIT.md`
  verifying the eight structural items: ManifoldTransform
  supports SchwarzschildLike chart; disabled/default
  mode remains identity; Euclidean chart remains
  identity; Schwarzschild-like transform is bounded;
  near-clamp behavior avoids NaN/Inf; no CUDA/OptiX
  behavior changed; build/test status; verdict.
- **Acceptance:** all eight checks PASS; the
  audit-host build remains at the post-SCHW.3
  baseline (`100% tests passed, 0 tests failed
  out of 12`; `manifold_identity_tests: 198 /
  198 checks passed`).
- **What does NOT ship:** no source code; no
  test binary changes; no CMake change. The
  audit shifts the SCHW.* sub-slice numbering
  by `+1` from the post-SCHW.2 plan
  (CUDA integration moves SCHW.4 → SCHW.5;
  OptiX integration moves SCHW.5 → SCHW.7;
  debug visualization moves SCHW.7 → SCHW.9;
  the final audit slot moves SCHW.9 → SCHW.11).

### SCHW.5 — CUDA integration (impl, GPU-side)

- **Scope:** wire the SchwarzschildLike arm into
  the CUDA kernels:
  - `k_render_scene`'s
    `ManifoldCoordinates` AOV write arm: when
    `is_active(launch_params.manifold_mode)` and
    `chart_type == SchwarzschildLike`, call
    `world_to_chart_schwarzschild(...)` on the
    hit position before writing.
  - Optional: the primary-ray direction warp in
    raygen, gated behind a separate `warp_-
    primary_rays` toggle.
- **Acceptance:**
  - Audit-host build green.
  - CUDA + OptiX-SDK host runtime check: the
    AOV's pixel values for a known scene
    fixture diverge from world-space hit
    positions in the documented signature.
  - Beauty-pass byte-identity preserved when
    the chart is Euclidean or
    `warp_strength = 0` (structurally
    guaranteed by the `is_active(...)` guard).
- **What does NOT ship:** OptiX-side
  integration (deferred to SCHW.7); test
  binary additions beyond the existing
  `renderer_tests` (the kernel arm is
  exercised end-to-end at runtime, not at
  unit-test level).

### SCHW.6 — Audit (docs only)

- **Scope:** per-slice gate for SCHW.5. Writes
  `docs/SCHWARZSCHILD_LIKE_CUDA_WARP_AUDIT.md`
  verifying the nine structural items: CUDA-safe
  warp helper exists; warp activates only on the
  intended gates (`enabled + SchwarzschildLike +
  strength > 0`); disabled/default mode remains
  no-op; Euclidean mode remains identity;
  bounded / no-NaN behavior; OptiX path was not
  modified; build / test status; runtime CUDA-host
  status (PASS / DEFERRED / BLOCKED); verdict.
- **Forward-looking variant:** the audit may be
  *forward-looking* and land before its predecessor
  SCHW.5 if the operator wants the CUDA-safety
  guarantees verified before the kernel-side
  activation slice lands. In that case, check #8
  (runtime CUDA-host status) is DEFERRED on two
  grounds: audit-host has no CUDA SDK; no kernel
  call site invokes the chart-aware arm yet. The
  other eight checks PASS on the existing
  structural CUDA-safety properties (RR_HD inline
  math leaf; RR_HD inline seam; kernel signatures
  accept `ManifoldMode`; OptiX untouched).
- **Acceptance:** all eight structural checks PASS;
  check #8 PASS or DEFERRED as appropriate; the
  audit-host build remains at the post-SCHW.5
  (or post-SCHW.4, in the forward-looking case)
  baseline.
- **What does NOT ship:** no source code; no test
  binary changes; no CMake change. The audit
  shifts the SCHW.* sub-slice numbering by `+1`
  from the post-SCHW.4 plan (OptiX integration
  moves SCHW.6 → SCHW.7; debug visualization
  moves SCHW.7 → SCHW.9; the final audit slot
  moves SCHW.9 → SCHW.11).

### SCHW.7 — OptiX integration (impl, GPU-side)

- **Scope:** mirror SCHW.5 in the OptiX
  closest-hit / miss programs. The
  `OptixLaunchParams::manifold_mode` field
  (MANI-I.5) and the
  `aov_manifold_coordinates` pointer field
  (MANI-I.8) are already in place. SCHW.7
  wires the SchwarzschildLike math into the
  programs' chart-aware arms AND lands the
  OptiX host-side allocation in
  `OptixRenderer::render_aovs` that MANI-I.9
  flagged as deferred.
- **Acceptance:**
  - Audit-host build green.
  - CUDA + OptiX-SDK host: AOV output of
    `--render-optix-aovs --manifold-enable
    --manifold-chart schwarzschild-like
    --manifold-debug` is byte-identical to
    the CUDA path's output for the same
    parameters.
  - OptiX host-side allocation for
    `aov_manifold_coordinates` lands in
    `render_aovs` (clears the MANI-I.9
    deferred item).
- **What does NOT ship:** denoiser integration
  for the new AOV (the denoiser still consumes
  Beauty/Albedo/Normal only).

### SCHW.8 — Audit (docs only)

- **Scope:** per-slice gate for SCHW.7. Writes
  `docs/SCHWARZSCHILD_LIKE_OPTIX_WARP_AUDIT.md`
  verifying the nine structural items: OptiX launch
  params receive the manifold payload; OptiX warp
  activates only on the intended triple-gate
  (`enabled + SchwarzschildLike + strength > 0`);
  disabled/default mode remains no-op; Euclidean
  mode remains identity; CUDA/OptiX warp math
  equivalence; bounded / no-NaN behavior; OptiX OFF
  build remains valid; runtime CUDA/OptiX-host
  status (PASS / DEFERRED / BLOCKED); verdict.
- **Acceptance:** all eight structural checks PASS;
  check #8 (runtime status) PASS or DEFERRED as
  appropriate; the audit-host build remains at the
  post-SCHW.7 baseline (`100% tests passed, 0 tests
  failed out of 12`; `manifold_identity_tests: 198
  / 198 checks passed`).
- **What does NOT ship:** no source code; no test
  binary changes; no CMake change. The audit shifts
  the SCHW.* sub-slice numbering by `+1` from the
  post-SCHW.6 plan (debug visualization moves
  SCHW.8 → SCHW.9; the final audit slot moves
  SCHW.9 → SCHW.11).

### SCHW.9 — Debug visualization + fixture (impl, scene-loader + fixture)

- **Scope:** consolidate the operator's "debug
  visualization" goal with the SCHW.* ladder's
  canonical fixture-scene need. Adds (a) a
  controlled fixture scene
  (`scenes/test_schwarzschild_like_manifold.rrscene`)
  with six visible marker spheres + ground-plane
  mesh + a `manifold` block authoring all four
  parser-supported `ManifoldMode` fields
  (`enabled=true`, `chart="schwarzschild-like"`,
  `strength=0.5`, `debug_visualization=true`);
  (b) minimal scene-parser support for the
  `manifold` block (`apply_manifold` +
  `parse_chart_type` helpers in
  `src/io/SceneLoader.cpp`); (c) a `Scene::manifold`
  POD slot; (d) dispatcher merge logic in
  `src/main.cpp::run_render_optix_aovs` /
  `run_render_aovs` resolving `effective_manifold =
  cfg.manifold.enabled ? cfg.manifold :
  scene.manifold`; (e) a fixture-companion doc
  (`docs/SCHWARZSCHILD_LIKE_FIXTURE.md`). The
  `ManifoldCoordinates` AOV encoding decision the
  original SCHW.9 brief enumerated is deferred to a
  future encoding-refinement slice if the operator
  requests it — SCHW.9 (this slice) reuses the
  existing raw-chart-space-position encoding the
  MANI-I.8 / SCHW.7 wiring already produces.
- **Acceptance:**
  - Audit-host build green; ctest 12/12 PASS;
    `manifold_identity_tests: 198/198 checks
    passed`.
  - Fixture loads cleanly via
    `--scene-info scenes/test_schwarzschild_like_manifold.rrscene`
    on the audit host.
  - Default scenes byte-identical to the
    pre-SCHW.9 baseline (verified by
    `git diff --name-only -- scenes/`
    returning exactly the new fixture).
- **What does NOT ship:** no new warp math; no
  CUDA-side kernel wiring (SCHW.5 still
  deferred); no chart-parameter scene authoring;
  no new CLI action that loads the fixture
  through `--render-*` (the dispatcher merge
  logic is in place but `--render-optix-aovs` /
  `--render-aovs` continue to build scenes
  inline). The runtime CUDA + OptiX-SDK
  end-to-end render is deferred to a future
  slice OR to operator-side validation on an
  SDK-equipped host.

### SCHW.10 — Audit (docs only)

- **Scope:** per-slice gate for SCHW.9. Writes
  `docs/SCHWARZSCHILD_LIKE_FIXTURE_AUDIT.md`
  verifying the seven structural items: fixture
  scene exists; fixture uses SchwarzschildLike
  manifold mode; values are bounded/safe; default
  scenes remain unchanged; parser changes are
  minimal; CUDA/OptiX runtime status (PASS /
  DEFERRED / BLOCKED); verdict.
- **Acceptance:** all six structural checks PASS;
  check #6 (runtime status) DEFERRED on
  documented audit-host limitations; the
  audit-host build remains at the post-SCHW.9
  baseline.
- **What does NOT ship:** no source code; no test
  binary changes; no CMake change. The audit
  shifts the SCHW.* sub-slice numbering by `+1`
  from the post-SCHW.8 plan (the final audit slot
  moves SCHW.10 → SCHW.11).

### SCHW.11 — Arc capstone audit (docs only) — LANDED

- **Scope:** writes
  `docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md`, the
  per-arc capstone verdict that closes the SCHW.1
  → SCHW.10 ladder and the MANI-I.10 slot.
  Synthesises the eight prior per-slice audit
  verdicts (SCHW.2 / SCHW.4 / SCHW.6 / SCHW.8 /
  SCHW.10 + MANIFOLD_CORE_FOUNDATION_AUDIT.md)
  into a single arc-level verdict, catalogues the
  remaining risks, and recommends the next safe
  stage. Ten audit items per the operator's
  capstone brief:
  1. Architecture scope stayed artistic / bounded,
     not full GR.
  2. CPU manifold transform supports
     SchwarzschildLike safely (SCHW.3 / SCHW.4).
  3. CUDA warp bridge exists and is default-no-op
     (SCHW.5 PARTIAL — kernel wiring still
     deferred; infrastructure verified by
     SCHW.6 forward-looking audit).
  4. OptiX warp bridge mirrors CUDA behavior
     structurally (SCHW.7 / SCHW.8; today AHEAD
     of CUDA because SCHW.5 unlanded).
  5. Fixture scene exists and is isolated
     (SCHW.9 / SCHW.10).
  6. Default Euclidean / disabled output remains
     byte-identical (six-layer safety analysis).
  7. Bounded / no-NaN safety status (four
     math-leaf guards inherited from SCHW.1 /
     SCHW.2).
  8. Runtime CUDA / OptiX validation status:
     DEFERRED on documented audit-host
     limitations.
  9. Remaining risks: five gaps catalogued
     (SCHW.5 unlanded; consumption-gap CLI
     extension; no primary-ray warp; no
     chart-parameter scene authoring; no
     runtime PPM regression suite).
  10. Recommended next safe stage: SCHW.5
      (CUDA-side kernel wiring) as the
      highest-priority tractable continuation.
- **Verdict:** **PASS_WITH_RUNTIME_DEFERRED**.
  All landed slices' per-slice audits returned
  PASS; the SCHW.5 PARTIAL is an acknowledged
  deferred gap, not a regression; the runtime
  fixture-render suite is DEFERRED to a CUDA +
  OptiX-SDK host. No REPAIR required; no BLOCKED
  item outstanding.
- **Acceptance:** audit-host build green; ctest
  12/12; `manifold_identity_tests: 198 / 198
  checks passed`. The verdict closes the
  MANI-I.10 slot for the audit-host portion;
  deferred items become PASS-able when SCHW.5
  lands + a CUDA + OptiX-SDK host runs the
  plan §7 fixture renders + the consumption-gap
  CLI extension lands.

---

## 9. Non-goals (this whole MANI-I.10 ladder)

Until a future SCHW.* addendum lifts them
explicitly, the slice does **not** introduce,
claim, or plan:

- A physically exact Schwarzschild solver. The
  remap is artistic; no Christoffel symbols, no
  geodesic ODE.
- A Kerr-spin component. SchwarzschildLike has
  zero spin by construction; the
  `CoordinateChartParameters::spin` field is
  reinterpreted as `falloff` for this chart.
- A photon-sphere ring or back-side-emergent
  ray. The chart is a coordinate remap, not a
  light-bending integrator.
- A new AOV slot. The existing
  `ManifoldCoordinates` AOV is reused; SCHW.9
  refines its encoding.
- An OptiX denoiser change. The denoiser
  continues to consume Beauty / Albedo / Normal
  only.
- A `.rrscene` schema bump. Parameters ride on
  the existing `CoordinateChart` + 
  `CoordinateChartParameters` PODs.
- A multi-chart scene (e.g. two Schwarzschild
  masses simultaneously). The chart's parameter
  surface carries a single `mass_origin`; a
  multi-chart slice is future work.
- A scene-file authoring surface (e.g. a
  `chart_block` in `.rrscene`). The CLI flags
  shipped at MANI-I.1 are the only entry
  point; scene-file authoring is a separate
  later slice.
- A Cinema 4D bridge / preview-UI integration.
  Architecture-doc §8 non-goals stand.

---

## 10. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules.
- `docs/MANIFOLD_INTEGRATION_PLAN.md` §8 (the
  slice section this design implements; the §3
  chain diagram shows MANI-I.10's position).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §4.2
  (SchwarzschildLike chart-family reserved slot)
  and §8 (non-goals).
- `docs/MANIFOLD_DEBUG_AOV_TASK.md` (the
  preceding task definition for
  `ManifoldCoordinates` — SCHW.9 reuses the
  slot).
- `docs/MANIFOLD_DEBUG_AOV_AUDIT.md` §4 (the
  immediately preceding per-slice audit that
  authorised proceeding to MANI-I.10).
- `src/manifold/CoordinateChart.h` (parameter
  surface SCHW.* reuses with documented
  per-chart reinterpretation).
- `src/manifold/ManifoldTransform.h`
  (`world_to_chart` / `chart_to_world` /
  `transform_ray_like_direction` helpers SCHW.3
  extends).
- `src/manifold/ManifoldMode.h` (`is_active`
  helper SCHW.5 / SCHW.7 guards on).
- `src/cuda/CudaTestKernel.cu` (the
  `ManifoldCoordinates` AOV arm SCHW.5
  extends).
- `src/optix/OptixPrograms.cu` (the
  `ManifoldCoordinates` AOV arm SCHW.7
  extends).
- `src/optix/OptixRenderer.cpp`
  (`render_aovs` host-side allocation SCHW.7
  completes per the MANI-I.9 deferred item).

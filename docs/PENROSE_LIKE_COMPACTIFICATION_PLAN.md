# Penrose-Like Compactification Plan (PENROSE.1)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this design document; the implementation
        lands in subsequent PENROSE.* sub-slices that
        consume this doc as their canonical brief.

This document is the design for the **Penrose-like
artistic coordinate-compactification chart** under
the integration plan's MANI-I.11 slot
(`docs/MANIFOLD_INTEGRATION_PLAN.md` §9). It mirrors
the structure of the predecessor design doc
(`docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md`) used for the
SCHW.* arc that just closed (capstone verdict
`PASS_WITH_RUNTIME_DEFERRED` at
`docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md`; CUDA-side
completion at `docs/SCHWARZSCHILD_LIKE_CUDA_COMPLETION_AUDIT.md`).

The arc this document plans is the **second non-trivial
chart family** in the renderer. Prerequisite work
already landed:

- **MANIFOLD.1 / .2 / .3 / .4 / .5 / .6 / .7** —
  Manifold Core data model and CoordinateChart POD
  surface (`docs/MANIFOLD_CORE_FOUNDATION_AUDIT.md`).
- **MANI-I.1 .. MANI-I.10** — CLI / render config /
  GPU plumb / debug AOV / Schwarzschild-like chart
  capstone (`docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md`,
  verdict `PASS_WITH_RUNTIME_DEFERRED`).
- **SCHW.5 completion** — CUDA kernel arm wired and
  verified (`docs/SCHWARZSCHILD_LIKE_CUDA_COMPLETION_AUDIT.md`,
  verdict `PASS_WITH_RUNTIME_DEFERRED`; SCHW.11
  capstone check #3 PARTIAL → PASS).

The SCHW.11 capstone audit explicitly named Penrose /
Kerr / Kruskal work as **NOT authorised** at the time
of its writing, conditional on operator approval AND
SCHW.5 closure. Both conditions are now met:

- **Operator approval:** the prior task brief
  ("PENROSE.1 — Penrose-Like Compactification
  Planning") explicitly authorised this design work
  (the planning slice landed at commit `a84f8b2`).
- **SCHW.5 closure:** confirmed at commit `73e9591` +
  completion audit `e4345ed`.

The Penrose-like arc may therefore commence with this
design doc as its operator-facing brief.

---

## 1. Scope

**PENROSE.* ships an artistic, conformally-inspired
coordinate-compactification chart that promotes the
existing `CoordinateChartType::PenroseLikePlaceholder`
enumerator (renamed to `PenroseLike` at PENROSE.2) to
a concrete implementation.** It is the second
non-trivial chart family after the SchwarzschildLike
arc; it shares the same architectural seams (the SCHW.*
ladder built the chart-aware infrastructure
end-to-end) and reuses the same activation gating,
debug AOV, fixture-scene authoring, and dispatcher
merge logic the SCHW.* arc established.

The slice is:

- **Artistic, not mathematically exact Penrose
  diagrams.** The compactification is a closed-form
  radial coordinate compression inspired by the
  visual signature of Penrose diagrams' asymptotic
  infinity boundary, NOT a conformal compactification
  of a Lorentzian metric. Architecture-doc §8
  non-goals "physically exact Kerr ray tracing" and
  "full GR solver" remain in force; the `*Like`
  naming convention from MANIFOLD.1 flags this
  honesty at every call site. **There is no
  conformal factor; the spacetime metric is not
  rescaled; light-like geodesics are NOT
  preserved as 45° lines in the chart.**
- **An observer-manifold visualization layer.** The
  chart's purpose is **diagrammatic** — letting an
  artist render the whole asymptotic structure of a
  scene in a single frame, with extreme distances
  (which would otherwise render as a single distant
  pixel) compressed into a visible portion of the
  framebuffer. It is NOT intended for production
  beauty passes.
- **Off by default.** The chart engages only when
  the operator passes both `--manifold-enable` AND
  `--manifold-chart penrose-like` (per the CLI
  surface MANI-I.1 shipped). The Euclidean default
  remains the documented "no output change" anchor.
- **Composable with — and currently mutually
  exclusive with — the SchwarzschildLike chart.**
  Today only one `CoordinateChart::type` is active
  per render; a future "manifold stack" slice (§7
  below) may allow composition, but PENROSE.* itself
  ships as a single-chart selection.

What this slice deliberately is NOT:

- **Not a real conformal Penrose diagram.** No
  conformal factor on the metric; no preservation of
  null-geodesic angle; no Penrose-Carter coordinate
  derivation; no event-horizon-as-finite-boundary
  computation. The compactification is a **pure
  coordinate transform**, not a metric operation.
- **Not the kernel-side path-tracer integrator
  rewrite.** The path tracer's BSDF / NEE / MIS,
  firefly clamp, and Russian roulette continue to
  operate on world-space rays. Only the
  `ManifoldCoordinates` AOV write site invokes the
  compactification; the beauty pass is unchanged.
- **Not the FIELD.* curvature-AOV programme.**
  Curvature scalars (Ricci, Kretschmann) belong to
  the Field Interpretation Layer
  (`docs/FIELD_INTERPRETATION_LAYER.md` §6);
  PENROSE.9 (fixture / debug viz) reuses the existing
  `ManifoldCoordinates` AOV slot, not a new
  curvature slot.
- **Not a `.rrscene` schema bump.** The mass-origin /
  compactification-scale / falloff parameters ride on
  the existing `CoordinateChart` +
  `CoordinateChartParameters` PODs (with documented
  per-chart reinterpretation, §3 below).

---

## 2. Core idea

The chart's `world_to_chart(p_world)` map is a
closed-form radial compactification centred on a
configured observation origin. Far from the origin
(`r → ∞`), points are compressed asymptotically
toward a bounded chart-radius `R_max`. Near the
origin (`r → 0`), the map is the identity. Two
candidate compactification families:

**Family A — `tanh`-based (preferred for bounded
output):**

```
   r        = |p_world - origin|
   r_chart  = R_max * tanh(strength * r / scale)
   chart_pos = origin + r_chart * normalize(p_world - origin)
```

Properties:
- `r → 0` ⇒ `r_chart → strength * r / scale * R_max`
  (linear regime; near-identity when `strength /
  scale ≈ 1 / R_max`).
- `r → ∞` ⇒ `r_chart → R_max` (asymptotic boundary).
- Smooth + monotonic + bounded by `[0, R_max]`.
- Closed-form inverse: `r = (scale / strength) *
  atanh(r_chart / R_max)`, well-defined for `r_chart
  < R_max`.

**Family B — rational (alternative):**

```
   r        = |p_world - origin|
   r_chart  = R_max * r / (r + scale)
   chart_pos = origin + r_chart * normalize(p_world - origin)
```

Properties:
- `r → 0` ⇒ `r_chart → r * R_max / scale` (linear
  regime).
- `r → ∞` ⇒ `r_chart → R_max` (asymptotic boundary).
- Bounded by `[0, R_max)`; never reaches the
  boundary exactly.
- Closed-form inverse: `r = scale * r_chart /
  (R_max - r_chart)`, diverges at `r_chart = R_max`
  but stable strictly inside the chart.

**PENROSE.2 ships Family A (`tanh`).** It is
preferred because (a) the integration plan's MANI-I.11
sketch (§9 of `docs/MANIFOLD_INTEGRATION_PLAN.md`)
already names "`tanh`-style coordinate compression";
(b) the bounded asymptote is **strict** (`r_chart`
reaches `R_max` only in the analytical limit, but
the IEEE-754 representation saturates at `R_max`
when `tanh` saturates, which is well-defined and
NaN-free); (c) the inverse has a documented stable
domain.

Family B is **deferred to a future addendum** if the
operator wants a rational-family alternative for
aesthetic reasons; PENROSE.2 ships only one helper
family to keep the math leaf small.

The chart preserves the **light transport pipeline**:

- The path tracer's BSDF / NEE / MIS continues to
  operate on world-space rays and world-space hit
  positions. The PenroseLike chart only modifies (a)
  the chart-space hit position written to the
  `ManifoldCoordinates` AOV via `world_to_chart`,
  and (b) optionally the primary-ray direction (NOT
  shipped at PENROSE.2; deferred to a future slice
  if the operator wants a "compactified
  primary-ray" mode). The beauty pass renders the
  scene as Euclidean.

The chart preserves the **renderer's existing
substrate**:

- No new GPU resource. No new acceleration
  structure. No new SBT record type. No new
  denoiser input. The new math runs entirely in the
  device-side helper that the kernel calls per
  pixel; the launch surface gains zero new fields
  beyond what MANI-I.5 / MANI-I.8 / SCHW.7 / SCHW.5
  already wired (the same `manifold_mode` +
  `coordinate_chart` per-launch payload supports
  PenroseLike).

---

## 3. Required parameters

The slice's artist-facing parameters map onto the
manifold module's existing POD surface — no schema
bump on `CoordinateChart` / `CoordinateChartParameters`
/ `ManifoldMode`. The mapping is **documented per
chart family** (master rule #3: honest
reinterpretation, not silent reuse).

| Plan parameter         | Source field                                            | Range / default                                                                                        | Notes                                                                                                                                                  |
|------------------------|---------------------------------------------------------|--------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------|
| `compactification_origin` | `CoordinateChart::origin` (`Vec3`)                   | Default `(0, 0, 0)` (scene origin).                                                                    | Reused as-is. For the Euclidean chart this is the chart's spatial origin; for SchwarzschildLike it is the mass origin; for PenroseLike it is the observer / compactification centre. |
| `compactification_scale` | `CoordinateChartParameters::compactification_scale` (`float`) | Range `(0, ∞)`; default `1.0`.                                                                | **Reinterpreted CANONICAL** for PenroseLike (the field's *original* documented purpose per MANIFOLD.1). `scale` in the `tanh` formula. Smaller values produce more aggressive compactification (sharper boundary); larger values produce gentler far-field compression. |
| `R_max`                | `CoordinateChartParameters::mass` (`float`)            | Range `(0, ∞)`; default `0.0` (PenroseLike falls back to Euclidean when `mass = 0`).                  | **Reinterpreted** for PenroseLike. The bounded chart-radius; pixel positions with `r → ∞` saturate at this radius. Default `0` means "Euclidean fallback" (matches the math leaf's documented short-circuit). |
| `strength`             | `ManifoldMode::strength` (`float`)                     | Range nominally `[0, 1]`; out-of-range values pass through unchanged per the existing `ManifoldMode::strength` contract. | Reused as-is. `0` is the Euclidean fallback; `1` is the full compactification. Interpolation between identity-rendering (`0`) and full PenroseLike compression (`1`). |
| `falloff`              | `CoordinateChartParameters::spin` (`float`)            | Range `[0.5, 4.0]`; default `1.0`.                                                                     | **Reinterpreted** for PenroseLike. Exponent applied to `r / scale` *before* the `tanh` to control the compactification curvature: `tanh((r / scale)^falloff)`. `1.0` matches the plain `tanh`-of-linear formula; higher values produce a sharper transition; lower values produce a softer one. |
| `debugMode`            | `ManifoldMode::debug_visualization` (`bool`)           | Default `false`.                                                                                       | Reused as-is. Drives whether the `--render-aovs` action emits `output/aov_manifold_coordinates.ppm` per the MANI-I.8 / SCHW.7 wiring.                  |

Notes on reinterpretation:

- `CoordinateChartParameters::compactification_scale`
  is *named* compactification because PenroseLike was
  always the canonical consumer per MANIFOLD.1's
  doc-comment. The SchwarzschildLike chart
  reinterpreted this slot as `clamp_radius` (per
  `SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §3); PenroseLike
  uses the field for its named purpose.
- `CoordinateChartParameters::mass` is reinterpreted
  as `R_max` for PenroseLike. The SchwarzschildLike
  chart used it as the Schwarzschild radius
  proxy (`r_s`); PenroseLike uses it as the bounded
  chart radius. The default `mass = 0.0` triggers
  the Euclidean fallback in both charts (matches the
  SCHW.1 math leaf's documented behaviour).
- `CoordinateChartParameters::spin` is reinterpreted
  as `falloff` for PenroseLike (parallel to the
  SchwarzschildLike `spin → falloff` reinterpretation).
  Kerr-like usage will eventually reclaim the field
  as the dimensionless spin parameter `a`.
- `CoordinateChartParameters::reserved` stays unused
  by PenroseLike. Future expansion (an artist-facing
  "boundary thickness" or "asymmetric
  compactification" knob) can claim it without an
  ABI bump.

The per-chart reinterpretation table this slice
contributes is parallel to the SchwarzschildLike
table (`SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §3); a
future Kerr-like chart would extend the pattern
again. The honesty of the design is that each chart
documents its reinterpretation explicitly at the
math-leaf header AND in the relevant per-chart
design doc.

---

## 4. Visual goals

The chart is designed to produce five operator-visible
visual signatures on the `ManifoldCoordinates` AOV
when engaged on a CUDA + OptiX-SDK host:

### 4.1 Asymptotic compactification

- **`strength > 0` + `R_max = 5.0` + `scale = 1.0` +
  `falloff = 1.0`:** points at world-space radial
  distance `r = 1.0` from the origin map to
  `r_chart ≈ 0.76 * R_max = 3.8`; points at `r =
  5.0` map to `r_chart ≈ 0.999 * R_max ≈ 5.0`;
  points at `r → ∞` saturate at `R_max = 5.0`.
  Visualises **unreachable / extreme distances**
  inside a bounded framebuffer region.

### 4.2 Horizon-like compression

- Pixels whose world-space radius `r` approaches
  `R_max * scale` (the "knee" of the `tanh`
  saturation) show visible **crowding** on the
  `manifold_coordinates` AOV: the AOV pixel values
  converge toward `R_max`, producing a visually
  recognizable compressed boundary region. This is
  the Penrose-like analog of how Penrose diagrams
  compress asymptotic infinity onto a finite
  boundary (mathematically distinct, but visually
  analogous).

### 4.3 Causal-boundary visualization

- The `R_max` parameter doubles as a soft
  **causal-boundary proxy**: pixels with `r → ∞`
  (which would normally render as a single distant
  pixel in Euclidean rendering) are compressed onto
  a finite ring at chart-radius `R_max`. This is
  **artistic, not physical**: a real Penrose
  diagram's boundary is a true conformal boundary at
  infinity; the PenroseLike chart's boundary is a
  visual compression of large finite radii. The
  visual cue is the "edge" of the compactified
  region on the AOV.

### 4.4 Manifold folding / compression

- With moderate `strength` (e.g. `0.5`), the
  near-field is mildly compressed and the far-field
  is strongly compressed — producing a **folded**
  visual signature where geometry transitions
  smoothly from near-identity to highly-compressed.
  This is the dial that makes the chart artistically
  useful: the operator can tune `strength` to
  expose the asymptotic structure without losing
  near-field detail.

### 4.5 Observer-centric infinity mapping

- The `compactification_origin` parameter defaults
  to the scene origin but can be set to the camera
  position (or any other observer location) to
  centre the compactification on a chosen observer.
  This is the "observer-manifold visualization"
  goal: each observer's perception of asymptotic
  infinity is mapped onto a finite boundary
  centred on that observer.

### 4.6 What is NOT produced

- **No 45° light-cone preservation.** Real Penrose
  diagrams preserve the angle of null geodesics
  through the conformal factor; the PenroseLike
  chart does NOT (no conformal factor; the
  spacetime metric is unchanged). A future
  `PenroseConformalChart` (separate from
  `PenroseLike`) could add this for a more
  physically meaningful diagrammatic mode; it is
  not part of PENROSE.*.
- **No time-axis compactification.** PenroseLike
  compactifies only the spatial radial coordinate.
  Real Penrose diagrams compactify BOTH time and
  space (compactifying the t-axis onto a finite
  segment). The Vec4 overload preserves the time
  component as invariant; a future time-axis
  compactification slice could add it.
- **No event-horizon visualisation.** The chart's
  boundary at `R_max` is a coordinate boundary, not
  a horizon. A horizon would require a real GR
  solver (architecture-doc §8 non-goal).
- **No back-side-of-boundary emergent rays.** The
  asymptotic compactification compresses infinity
  onto a finite region; rays that would
  mathematically emerge from "behind" the boundary
  in a real Penrose diagram do not appear here.

---

## 5. Proposed transform behavior

The chart's behavior is fully specified by the
formula in §2 (Family A, `tanh`-based) plus the §3
parameter mapping. The transform's properties:

### 5.1 Radial compactification

- Direction-preserving: the unit vector
  `normalize(p_world - origin)` is invariant under
  the transform. Only the radial scalar changes.
- Sign-preserving: positive `r` always maps to
  positive `r_chart`. There is no sign flip.

### 5.2 Asymptotic compression

- Monotonic: `r_chart` is a strictly-increasing
  function of `r` for any positive `(strength,
  R_max, scale, falloff)`. The chart is
  order-preserving in radial distance.
- Bounded above: `r_chart < R_max` for all finite
  `r`; `r_chart → R_max` as `r → ∞`.
- Bounded below: `r_chart > 0` for `r > 0`;
  `r_chart = 0` iff `r = 0` (the origin is fixed).

### 5.3 Bounded output coordinates

- The chart's output `chart_pos` is bounded in
  every direction:
  `|chart_pos - origin| ≤ R_max`. The chart-space
  representation of the entire scene fits inside a
  bounding sphere of radius `R_max` centred on the
  compactification origin.
- IEEE-754 floats represent the asymptotic limit
  cleanly: `tanh(x)` saturates to exactly `1.0` for
  `x > ~16` (single precision), so `r_chart` is
  exactly `R_max` (not a NaN, not an Inf) for
  sufficiently far points.

### 5.4 Configurable compactification strength

- The `strength` parameter (sourced from
  `ManifoldMode::strength`) interpolates between
  identity (`strength = 0`, the Euclidean fallback)
  and full compactification (`strength = 1`).
  Out-of-range values pass through per the
  `ManifoldMode::strength` documented contract; the
  math leaf does not clamp.
- The `scale` parameter shifts the "knee" of the
  `tanh` saturation: smaller `scale` means the knee
  is at a smaller `r`, so even nearby points start
  to compactify visibly.
- The `falloff` parameter shapes the transition:
  `falloff > 1` produces a sharper knee; `falloff <
  1` produces a softer knee.

---

## 6. Safety constraints

The slice must satisfy six safety invariants, each
mapping to a concrete implementation requirement that
PENROSE.* sub-slices verify.

### 6.1 Bounded transforms

- The formula `r_chart = R_max * tanh(strength *
  (r / scale)^falloff)` is bounded by construction
  in `[0, R_max]` for any finite `(r, strength,
  R_max, scale, falloff)`.
- The `tanh` function is naturally NaN/Inf-free for
  any finite input; IEEE-754 saturates to `±1`
  cleanly without producing NaN.
- The `(r / scale)^falloff` evaluation requires
  `r / scale ≥ 0` (always true for `r ≥ 0` and
  `scale > 0`) and `falloff ≥ 0.5` (validator-
  enforced per §6.5 below). No `pow(0, 0)` ambiguity.

### 6.2 No NaN/Inf

- `scale > 0` enforced at the host-side validator
  (rejects `scale ≤ 0`).
- `R_max > 0` enforced at the host-side validator
  (rejects `R_max ≤ 0`; `R_max = 0` triggers the
  documented Euclidean fallback via the math leaf's
  short-circuit).
- `falloff` clamped to `[0.5, 4.0]` at the
  host-side validator (matches the SchwarzschildLike
  validator's range).
- `strength` finite (NaN / Inf rejected at the
  validator).
- The Newton-Raphson inverse (§6.4) bounded by a
  hard iteration cap matching SCHW.1.

### 6.3 Euclidean fallback

- **`strength = 0` ⇒ identity transform** (verifiable
  analytically: `tanh(0) = 0` ⇒ `r_chart = 0` ⇒
  uniform shrinkage to origin… wait, that's not
  identity).

  **Correction:** the math at `strength = 0`
  evaluates to `r_chart = R_max * tanh(0) = 0` for
  all `r > 0`. That's NOT identity. To preserve the
  Euclidean fallback contract at `strength = 0`,
  the math leaf MUST short-circuit: `if (strength
  == 0) return p_world`. Similarly `R_max = 0` ⇒
  return `p_world`. This is the SCHW.1 short-circuit
  pattern applied to PenroseLike.

- **`ManifoldMode.enabled = false` ⇒
  `is_active(...)` returns `false` ⇒ kernel skips
  the PenroseLike arm entirely** and writes the
  world-space hit position to the AOV (the same
  output the Euclidean default produces today per
  MANI-I.8).
- **Chart = Euclidean ⇒ `is_active(...)` returns
  `false`** (the Euclidean chart is "intentionally
  not active" per `ManifoldMode.h:143-145`).

### 6.4 Reversible (chart_to_world inverse)

- The PenroseLike formula `r_chart = R_max *
  tanh(strength * (r / scale)^falloff)` has a
  **closed-form inverse for `falloff = 1.0`**:
  `r = scale * atanh(r_chart / R_max) / strength`,
  defined for `r_chart < R_max`.
- For general `falloff ≠ 1`, the inverse requires
  solving `(r / scale)^falloff = atanh(r_chart /
  R_max) / strength`, which has the closed-form
  solution `r = scale * (atanh(r_chart / R_max) /
  strength)^(1 / falloff)` provided
  `atanh(r_chart / R_max) / strength ≥ 0` (always
  true for `r_chart ∈ [0, R_max)` and `strength >
  0`).
- **Boundary handling:** `r_chart = R_max` is a
  singularity of `atanh`; the inverse returns `+∞`
  in exact arithmetic. The implementation clamps
  `r_chart` to `R_max * (1 - epsilon)` before
  evaluating `atanh`, returning a large but finite
  `r`. The clamp-epsilon is configurable but
  defaults to `1e-6`.
- The inverse is **analytical** (not iterative
  like SCHW.1's Newton-Raphson). Iteration count
  is zero; convergence is guaranteed.

### 6.5 Defence-in-depth on the parameter validator

- The host-side validator rejects:
  - `R_max <= 0` and non-finite;
  - `scale <= 0` and non-finite;
  - `falloff` outside `[0.5, 4.0]` and non-finite;
  - `strength` non-finite (out-of-`[0, 1]` allowed
    per the `ManifoldMode::strength` contract;
    only NaN/Inf rejected).
- The validator logs the rejection with a clear
  message and falls back to the documented
  Euclidean default (the same posture
  `--manifold-strength` takes for non-parseable
  inputs in MANI-I.1 and that SchwarzschildLike
  takes for invalid chart parameters in SCHW.1).

### 6.6 Bit-identity on the Euclidean off-path

- Every existing CLI action without
  `--manifold-enable` continues to use the
  pre-PENROSE.* code path. The
  `is_active(manifold_mode)` guard is the entry
  point to the PenroseLike arm; the guard
  short-circuits at `false` on the Euclidean
  default.
- The SCHW.7 / SCHW.5 triple-gate (`is_active(...)
  && chart == SchwarzschildLike && strength > 0`)
  is preserved verbatim; the PenroseLike arm uses a
  parallel triple-gate (`is_active(...) && chart
  == PenroseLike && strength > 0`).
- Other non-PenroseLike non-Euclidean chart
  families (`SchwarzschildLike` /
  `KruskalLikePlaceholder` /
  `KerrLikePlaceholder`) structurally bypass the
  PenroseLike arm via the explicit `chart ==
  PenroseLike` check, mirroring the SCHW.7
  pattern.

---

## 7. Relationship to Schwarzschild-like warp

The SchwarzschildLike (SCHW.*) and PenroseLike
(PENROSE.*) charts are **conceptually
complementary**:

- **SchwarzschildLike inflates near-mass regions.**
  Points near the mass origin are pushed outward
  by a factor `1 + warp_strength * r_s / r^falloff`.
  Far-field is identity.
- **PenroseLike compresses far-field regions.**
  Points far from the compactification origin are
  pulled inward toward a finite chart radius
  `R_max`. Near-field is near-identity.

The two are **mutually exclusive** at the
`CoordinateChart::type` level today (one chart per
render). A future addendum could allow composition:

### 7.1 Composability (deferred to future slice)

- **Mathematically composable:** the two transforms
  are bounded continuous maps `R³ → R³`; their
  composition `Penrose(Schwarzschild(p))` is
  well-defined and bounded.
- **Visually meaningful:** the composition would
  produce a render where (a) the mass-vicinity is
  inflated (SchwarzschildLike near-field signature)
  AND (b) the far-field is compressed onto a
  finite boundary (PenroseLike far-field signature).
  This is the "Schwarzschild + Penrose stack"
  visualization a future slice could ship.
- **Ordering:** the natural order is
  `Penrose(Schwarzschild(p))` — apply the local
  mass warp first (in world coordinates), then the
  global compactification (which compresses
  whatever world it sees). The reverse order
  (`Schwarzschild(Penrose(p))`) would mass-warp in
  the compactified chart space, which is visually
  meaningless because the Schwarzschild radius
  `r_s` is defined in world units.
- **Future manifold-stack concept:** the
  `ManifoldMode` POD could grow a `chart_stack`
  field (a list of `CoordinateChart` values) that
  the renderer applies in order. PENROSE.* itself
  ships only the single-chart selection; the stack
  concept is a separate future arc.

### 7.2 Transform ordering (this arc)

- PENROSE.* ships with single-chart selection: the
  active chart is `ManifoldMode::chart`. The
  renderer applies the chart's transform on the
  hit position, writes to the AOV, and is done.
- The dispatcher's chart selection is **last writer
  wins** between CLI and scene-file (per SCHW.9
  precedent): `effective_manifold = cfg.manifold.enabled
  ? cfg.manifold : scene.manifold`. The chart
  selected by `effective_manifold.chart`
  exclusively decides which arm runs.

### 7.3 Future manifold-stack concept (out of scope)

A `ManifoldStack` concept would let the operator
chain multiple charts. The architecture has the
necessary shape:

- `ManifoldMode::chart` becomes
  `ManifoldMode::chart_stack`, a fixed-size array
  of `CoordinateChart` values.
- The kernel iterates the stack, applying each
  chart's transform in order.
- The `is_active(...)` gate extends to "any chart
  in the stack is non-Euclidean".

This is **explicitly NOT shipped by PENROSE.***;
mentioned here as the natural future direction the
arc's design accommodates without an ABI bump (the
`ManifoldMode` POD could grow a stack field with a
documented bounded length later).

---

## 8. Integration strategy

The chart's runtime surface plugs into the existing
manifold-module helpers MANI-I.1–MANI-I.10 +
SCHW.1–SCHW.10 built. Three integration seams, each
with its own PENROSE.* sub-slice.

### 8.1 `world_to_chart(...)` compactification

- The existing `world_to_chart(ManifoldTransform&,
  Vec3)` helper in `src/manifold/ManifoldTransform.h`
  already branches on `t.chart.type == Euclidean`
  AND `t.chart.type == SchwarzschildLike`
  (SCHW.3). PENROSE.4 extends the helper with a
  `t.chart.type == CoordinateChartType::PenroseLike`
  arm that calls the new math helper from PENROSE.2.
- The `Vec4` overload (spacetime) gets the same arm,
  treating the time component as invariant (the
  PenroseLike chart is static in coordinate time at
  this slice; a future time-axis-compactification
  slice could change this).
- The new arm uses the artist parameters per §3's
  reinterpretation table.

### 8.2 `chart_to_world(...)` inverse (analytical)

- The existing inverse helper in `ManifoldTransform.h`
  gains a PenroseLike arm that runs the closed-form
  analytical inverse from PENROSE.2 (no Newton-
  Raphson required, unlike SCHW.1; the `atanh`
  formula gives a direct inverse).
- The inverse helper's accuracy is documented;
  callers near the boundary `r_chart = R_max`
  experience the documented clamp (clamp-epsilon
  default `1e-6`) and receive a large but finite
  output `r`.

### 8.3 Optional primary-ray direction warp (deferred)

- PENROSE.* deliberately does NOT ship a primary-ray
  direction warp. The PenroseLike chart is a
  diagnostic / diagrammatic mode; the beauty pass
  is unaffected. A future slice may add a
  `penrose_like_warp_ray_direction(...)` helper if
  the operator wants a "compactified primary-ray"
  visualization mode, but the SchwarzschildLike
  precedent (the helper exists at SCHW.1 but is
  never invoked at raygen) suggests primary-ray
  warp is optional.

### 8.4 Debug AOV interaction

- The existing `ManifoldCoordinates` AOV (MANI-I.8 /
  SCHW.7 / SCHW.5) consumes the `world_to_chart`
  result on every hit. With the PenroseLike chart
  engaged, the AOV's pixel values **compactify the
  world-space hit positions onto a bounded chart
  radius `R_max`** — the documented purpose of the
  debug AOV per the MANI-I.8 task definition. The
  AOV's component count stays at 3 floats per
  pixel.

### 8.5 Kernel-arm gating

Mirrors the SCHW.7 / SCHW.5 triple-gate pattern:

```
const bool active =
    is_active(launch_params.manifold_mode)
 && launch_params.manifold_mode.chart
        == CoordinateChartType::PenroseLike
 && launch_params.manifold_mode.strength > 0.0f;
```

The first two gates ensure the chart is the active
chart family (the `is_active(...)` helper requires
`enabled && chart != Euclidean`; the explicit
`chart == PenroseLike` check structurally bypasses
the other `*Like` / `*LikePlaceholder` families per
master rule #3). The third gate enforces the
`strength > 0` precondition; the math leaf's
`strength == 0` short-circuit is a fourth defensive
layer.

### 8.6 Dispatcher merge logic

The SCHW.9 dispatcher merge logic (`effective_manifold
= cfg.manifold.enabled ? cfg.manifold : scene.manifold`)
applies verbatim to PenroseLike. No new merge
behaviour required.

The fixture scene PENROSE.9 ships authors `chart:
"penrose-like"` in its `manifold` block; the
existing SCHW.9 scene parser (`apply_manifold` +
`parse_chart_type`) already accepts this kebab-case
chart name and maps it to
`CoordinateChartType::PenroseLikePlaceholder`. The
PENROSE.2 slice renames the enum value to
`PenroseLike` (dropping the `Placeholder` suffix);
the parser is updated in the same slice to consume
the renamed enum (the kebab-case name
`penrose-like` stays the same; only the C++
enumerator identifier changes).

---

## 9. Runtime-deferred CUDA / OptiX checks

The audit-host build (no CUDA, no OptiX SDK) cannot
directly verify the PenroseLike compactification's
visual output. The runtime checks below are
**DEFERRED** behind the audit-host's existing
no-CUDA / no-OptiX-SDK fallback, matching the
SCHW.2 / SCHW.4 / SCHW.6 / SCHW.8 / SCHW.10 / SCHW.11
per-slice audit posture.

Each deferred check must be exercised on a CUDA +
OptiX-SDK host before PENROSE.10 (audit) closes the
chart's per-slice gate:

### 9.1 Euclidean fallback bit-identity (CUDA + OptiX)

Run every pre-PENROSE.* reference render with
`--manifold-enable --manifold-chart euclidean`:
- `--render-pathtrace scenes/test_relativity.rrscene`
- `--render-scene`
- `--render-mesh-scene`
- `--render-material-scene`
- `--render-direct-lighting`
- `--render-aovs scenes/test_full_scene.rrscene`
- `--render-aovs --manifold-debug scenes/test_full_scene.rrscene`

Verify every output PPM is byte-identical to the
pre-PENROSE.* reference. The PenroseLike arm is
structurally guarded by `is_active(manifold_mode)
&& chart == PenroseLike`, which returns `false`
for `chart == Euclidean` AND for `chart ==
SchwarzschildLike`; the new arm is not reached.

### 9.2 SchwarzschildLike non-regression

Run every existing SCHW.* fixture / CLI invocation
with `--manifold-chart schwarzschild-like` — the
SCHW.5 / SCHW.7 wiring must be preserved
byte-for-byte by PENROSE.*. Specifically:

- `--render-aovs --manifold-enable --manifold-chart
  schwarzschild-like --manifold-strength 1.0
  --manifold-debug` → `output/aov_manifold_coordinates.ppm`
  byte-identical to the pre-PENROSE.* SchwarzschildLike
  reference.
- The PenroseLike arm's `chart == PenroseLike` check
  structurally bypasses the SchwarzschildLike chart
  configuration; the SCHW.5 arm continues to
  execute.

### 9.3 `strength = 0` byte-identity

Run with `--manifold-enable --manifold-chart
penrose-like --manifold-strength 0`. The triple-gate
short-circuits on `strength > 0`; the kernel writes
the raw `best.position` (the MANI-I.8 baseline);
the output is byte-identical to the
`--manifold-chart euclidean` baseline.

### 9.4 Visual signature on the AOV

Run with `--render-aovs --manifold-enable
--manifold-chart penrose-like --manifold-strength 1.0
--manifold-debug`. The
`output/aov_manifold_coordinates.ppm` should
visibly diverge from world-space hit positions in
a documented signature:
- Asymptotic compactification: far-field pixels
  saturate at `R_max`.
- Near-identity: nearby pixels remain close to
  their world-space values.
- Documented boundary at `r_chart = R_max`.

The reference AOV PPM is pinned by PENROSE.9 +
PENROSE.10 on a CUDA + OptiX-SDK host.

### 9.5 CUDA / OptiX byte-equivalence

Run identical fixtures through both backends
(`--render-aovs` for CUDA and `--render-optix-aovs`
for OptiX, both with a future `<scene-path>`
extension or via inline scene configuration). The
CUDA `output/aov_manifold_coordinates.ppm` and the
OptiX `output/optix_aov_manifold_coordinates.ppm`
should be **byte-identical** for the same fixture
and same `--manifold-*` parameters. This is the
single-source-of-truth invariant the SCHW.5
completion audit established for SchwarzschildLike;
PENROSE.* inherits the invariant by reusing the
same shared math-leaf pattern.

### 9.6 Off-chart non-regression

Run a non-PenroseLike non-Euclidean render with
`--manifold-enable --manifold-chart kerr-like`
(reserved-but-inert per MANIFOLD.1). The
PenroseLike arm is gated on `chart_type ==
PenroseLike`; for `KerrLikePlaceholder` the helper
falls through to the documented passthrough. Output
is byte-identical to the Euclidean default.

---

## 10. Proposed slices (PENROSE.* sub-slice ladder)

Six sub-slices, each one its own commit with its own
audit gate. The chain is strict-prefix; each slice
ships only after its predecessor is green (mirroring
the SCHW.1 → SCHW.11 ladder cadence).

### PENROSE.2 — Math helper (impl, math-leaf)

- **Scope:** add a new header
  `src/manifold/PenroseLikeCompactification.h` (or
  similar) containing the closed-form math helpers:
    - `struct PenroseLikeCompactificationParams { float
       r_max, strength, scale, falloff; };`
    - `RR_HD inline bool penrose_like_validate_params(p);`
    - `RR_HD inline Vec3 penrose_like_world_to_chart(
       p_world, origin, p);` (closed-form `tanh`-based
      compactification).
    - `RR_HD inline Vec3 penrose_like_chart_to_world(
       chart_pos, origin, p);` (analytical inverse;
      `atanh` with clamp-epsilon).
- **Optionally:** rename `CoordinateChartType::PenroseLikePlaceholder`
  to `CoordinateChartType::PenroseLike` (parallel to
  the SchwarzschildLike enum naming convention). All
  call sites + the CLI's `parse_chart_type` + the
  scene parser's `parse_chart_type` updated in this
  slice; the kebab-case CLI / scene name
  `penrose-like` is unchanged.
- **Acceptance:**
  - Audit-host `g++ -std=c++20 -Isrc -Wall -Wextra
    -Werror` build of a
    `PenroseLikeCompactification.h`-only TU compiles
    cleanly.
  - Analytic checks: `strength = 0` ⇒
    `world_to_chart` returns input exactly;
    `r → ∞` ⇒ output approaches `R_max` (verified
    at `r = 1e6` with `R_max = 5.0`); `world_to_chart
    ∘ chart_to_world` residual ≤ `1e-6` for
    representative parameter sweeps (better than
    SCHW.1's `1e-4` because the inverse is
    analytical, not iterative).
- **What does NOT ship:** no `ManifoldTransform.h`
  change yet (PENROSE.4); no kernel code change
  (PENROSE.6 / PENROSE.8); no test binary other
  than appending to `manifold_identity_tests`.

### PENROSE.3 — Audit (docs only)

- **Scope:** per-slice gate for PENROSE.2. Writes
  `docs/PENROSE_LIKE_COMPACTIFICATION_MATH_AUDIT.md`
  verifying the eight structural items the
  operator's PENROSE.3 task brief enumerates:
  math helper exists; strength 0 is identity;
  output is bounded; no NaN/Inf behavior exists;
  radial compression is monotonic; no renderer
  behavior changed; build/test status; verdict.
- **Acceptance:** all seven structural checks
  PASS; the audit-host build remains at the
  post-PENROSE.2 baseline (`100% tests passed, 0
  tests failed out of 12`;
  `manifold_identity_tests: 250 / 250 checks
  passed`).
- **What does NOT ship:** no source code; no test
  binary changes; no CMake change. The audit
  shifts the PENROSE.* sub-slice numbering by `+1`
  from the post-PENROSE.2 plan (CPU integration
  moves PENROSE.3 → PENROSE.4; CUDA integration
  moves PENROSE.4 → PENROSE.6; OptiX integration
  moves PENROSE.6 → PENROSE.8; fixture / debug
  viz moves PENROSE.8 → PENROSE.9; the arc
  capstone audit moves PENROSE.9 → PENROSE.10).

### PENROSE.4 — CPU integration (impl, host-only)

- **Scope:** extend `src/manifold/ManifoldTransform.h`'s
  `world_to_chart` / `chart_to_world` helpers
  (Vec3 + Vec4 overloads) with the `PenroseLike` arm.
  Mirrors the SCHW.3 CPU integration shape.
- **Acceptance:**
  - Audit-host build green.
  - `manifold_identity_tests` gains assertions that
    `world_to_chart(penrose_chart, p_world)` returns
    `p_world` when `strength = 0`, and produces
    documented non-identity outputs at `strength >
    0` on a representative input.
- **What does NOT ship:** no CUDA / OptiX kernel
  consumption; no AOV change; no beauty-pass
  change.

### PENROSE.5 — Audit (docs only)

- **Scope:** per-slice gate for PENROSE.4. Writes
  `docs/PENROSE_LIKE_CPU_INTEGRATION_AUDIT.md`
  verifying the nine structural items the operator's
  PENROSE.5 task brief enumerates: ManifoldTransform
  supports PenroseLike chart; disabled/default mode
  remains identity; Euclidean chart remains identity;
  strength 0 remains identity; PenroseLike transform
  is bounded; large coordinates avoid NaN/Inf; no
  CUDA/OptiX behavior changed; build/test status;
  verdict.
- **Acceptance:** all eight structural checks PASS;
  the audit-host build remains at the
  post-PENROSE.4 baseline (`100% tests passed, 0
  tests failed out of 12`;
  `manifold_identity_tests: 312 / 312 checks
  passed`).
- **What does NOT ship:** no source code; no test
  binary changes; no CMake change. The audit shifts
  the PENROSE.* sub-slice numbering by `+1` from the
  post-PENROSE.4 plan (CUDA integration moves
  PENROSE.5 → PENROSE.6; OptiX integration moves
  PENROSE.6 → PENROSE.8; fixture / debug viz moves
  PENROSE.8 → PENROSE.9; the arc capstone audit
  moves PENROSE.9 → PENROSE.10).

### PENROSE.6 — CUDA integration (impl, GPU-side)

- **Scope:** wire the PenroseLike arm into the
  CUDA kernel:
  - `k_render_scene`'s `ManifoldCoordinates` AOV
    write arm: when `is_active(scene.manifold_mode)
    && chart == PenroseLike && strength > 0.0f`,
    call `penrose_like_world_to_chart(...)` on the
    hit position before writing.
  - Parallel to SCHW.5's CUDA kernel wiring;
    reuses the same `scene.manifold_mode` +
    `scene.coordinate_chart` payload threading.
- **Acceptance:**
  - Audit-host build green.
  - CUDA + OptiX-SDK host runtime check: the AOV's
    pixel values for a known scene fixture
    compactify toward `R_max` in the far field.
  - Default / disabled / Euclidean modes remain
    byte-identical (mirroring SCHW.5's
    triple-gate + four-layer safety).
- **What does NOT ship:** OptiX-side integration
  (deferred to PENROSE.8); test binary additions
  beyond the existing `manifold_identity_tests`.

### PENROSE.7 — Audit (docs only)

- **Scope:** per-slice gate for PENROSE.6. Writes
  `docs/PENROSE_LIKE_CUDA_INTEGRATION_AUDIT.md`
  verifying the ten structural items the operator's
  PENROSE.7 task brief enumerates: CUDA-safe
  Penrose-like helper exists or shared helper is
  GPU-compatible; GPU payload supports PenroseLike
  chart; activation conditions are correct;
  disabled/default mode remains no-op; Euclidean
  mode remains identity; Schwarzschild behavior
  unchanged; OptiX path was not modified;
  build/test status; runtime CUDA-host status
  (PASS / DEFERRED / BLOCKED); verdict.
- **Acceptance:** all nine structural checks PASS;
  check #9 (runtime CUDA-host status) DEFERRED on
  documented audit-host limitations; the audit-host
  build remains at the post-PENROSE.6 baseline
  (`100% tests passed, 0 tests failed out of 12`;
  `manifold_identity_tests: 312 / 312 checks
  passed`).
- **What does NOT ship:** no source code; no test
  binary changes; no CMake change. The audit
  shifts the PENROSE.* sub-slice numbering by `+1`
  from the post-PENROSE.6 plan (OptiX integration
  moves PENROSE.7 → PENROSE.8; fixture / debug viz
  moves PENROSE.8 → PENROSE.9; the arc capstone
  audit moves PENROSE.9 → PENROSE.10).

### PENROSE.8 — OptiX integration (impl, GPU-side)

- **Scope:** mirror PENROSE.6 in the OptiX
  closest-hit program. The
  `OptixLaunchParams::manifold_mode` +
  `coordinate_chart` fields (MANI-I.5 + SCHW.7) are
  already in place; PENROSE.8 wires the PenroseLike
  math into the kernel arm at
  `OptixPrograms.cu:773-795` (where the SCHW.7
  SchwarzschildLike arm sits).
- **Acceptance:**
  - Audit-host build green.
  - CUDA + OptiX-SDK host: AOV output of
    `--render-optix-aovs --manifold-enable
    --manifold-chart penrose-like --manifold-debug`
    is byte-identical to the CUDA path's output for
    the same parameters (single-source-of-truth
    math).
  - SCHW.5 / SCHW.7 SchwarzschildLike rendering
    continues to work byte-identically (the new
    arm coexists; the explicit `chart ==
    SchwarzschildLike` / `chart == PenroseLike`
    checks structurally separate the two).
- **What does NOT ship:** denoiser integration for
  the new AOV (the denoiser still consumes Beauty /
  Albedo / Normal only).

### PENROSE.9 — Fixture / debug visualization (impl, scene + dispatcher)

- **Scope:** parallel to SCHW.9's fixture work. Add
  a controlled diagnostic scene:
  `scenes/test_penrose_like_manifold.rrscene` with
  visible geometry spanning a wide radial range
  (sphere field extending out to `r = 100` or so)
  + a `manifold` block authoring `enabled=true,
  chart="penrose-like", strength=0.5, debug_visualization=true`.
  No new parser surface required (the SCHW.9
  `apply_manifold` already accepts kebab-case
  `penrose-like`; the PENROSE.2 enum rename keeps
  the kebab-case name unchanged).
- **Acceptance:**
  - Audit-host build green; ctest 12/12.
  - Fixture loads cleanly via `--scene-info` on
    the audit host.
  - Default scenes (the existing nine `.rrscene`
    files) are byte-identical to the pre-PENROSE.9
    state.
- **What does NOT ship:** new CLI action; chart-
  parameter scene authoring (artistic defaults
  baked into main.cpp's PenroseLike-specific
  helper, mirroring SCHW.7's pattern); golden-PPM
  pinning (deferred to CUDA + OptiX-SDK host).

### PENROSE.10 — Audit (docs only)

- **Scope:** per-arc capstone verdict. Writes
  `docs/PENROSE_LIKE_COMPACTIFICATION_AUDIT.md`
  mirroring the SCHW.11 capstone shape. Ten audit
  items:
  1. Architecture scope stayed artistic / bounded,
     not full conformal compactification.
  2. CPU integration via `ManifoldTransform`
     extension (PENROSE.4).
  3. CUDA kernel arm wired (PENROSE.6).
  4. OptiX kernel arm wired + cross-backend AOV
     byte-equivalence by single-source-of-truth
     math (PENROSE.8).
  5. Fixture scene exists and is isolated
     (PENROSE.9).
  6. Default Euclidean / disabled / SchwarzschildLike
     output remains byte-identical.
  7. Bounded / no-NaN safety status.
  8. Runtime CUDA / OptiX validation status
     (PASS / DEFERRED / BLOCKED).
  9. Remaining risks (per-slice catalogue).
  10. Recommended next safe stage.
- **Verdict:** PASS / PASS_WITH_RUNTIME_DEFERRED /
  REPAIR / BLOCKED.
- **Acceptance:** audit-host build green; ctest
  12/12. The verdict authorises proceeding to
  the next manifold-chart family (Kerr-like?
  Kruskal-like?) per operator authorisation, OR
  to MANI-I.12 (final cross-host audit) per the
  integration plan §11.

---

## 11. Non-goals (this whole PENROSE.* arc)

Until a future PENROSE.* addendum lifts them
explicitly, the slice does **NOT** introduce, claim,
or plan:

- **A physically exact Penrose / conformal
  compactification.** The compactification is
  artistic; no conformal factor on the metric; no
  preservation of null-geodesic angles.
  Architecture-doc §8 non-goals stand.
- **A time-axis compactification.** The chart
  compactifies only the spatial radial coordinate.
  Real Penrose diagrams compactify BOTH time and
  space; PENROSE.* does not.
- **Composability with the SchwarzschildLike
  chart.** §7 above describes the future manifold-
  stack concept; PENROSE.* itself ships only
  single-chart selection.
- **A new AOV slot.** The existing
  `ManifoldCoordinates` AOV (MANI-I.8 / SCHW.7 /
  SCHW.5) is reused.
- **A denoiser change.** The denoiser continues to
  consume Beauty / Albedo / Normal only.
- **A `.rrscene` schema bump.** Parameters ride on
  the existing `CoordinateChart` +
  `CoordinateChartParameters` PODs (with documented
  per-chart reinterpretation, §3 above).
- **Primary-ray direction warp.** The PenroseLike
  beauty pass is unaffected; a future slice may
  ship a `penrose_like_warp_ray_direction(...)`
  helper if the operator wants a "compactified
  primary-ray" mode.
- **Multi-chart scene authoring.** The fixture
  scene PENROSE.9 ships authors a single
  `manifold` block; multi-chart scenes are a
  future-arc concern.
- **A Cinema 4D bridge / preview-UI integration.**
  Architecture-doc §8 non-goals stand.
- **Kerr-like or Kruskal-like chart work.**
  PENROSE.* is one chart family; the next family
  (Kerr / Kruskal / extension) is a separate
  future arc.

---

## 12. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` —
  top-level rules; master rule #3 ("no fake
  stubs") is the load-bearing invariant.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3
  ontology (Coordinate Chart / Metric Tensor /
  Observer Frame / Geodesic State); §4.2 reserved
  PenroseLikePlaceholder slot; §8 non-goals.
- `docs/MANIFOLD_INTEGRATION_PLAN.md` §9
  (MANI-I.11 — Penrose-like compactification
  visualization, the integration slot this arc
  consumes).
- `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` — the
  predecessor design doc PENROSE.* mirrors in
  structure.
- `docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md` — SCHW.11
  capstone verdict (`PASS_WITH_RUNTIME_DEFERRED`)
  that the operator's task brief satisfies as
  prerequisite.
- `docs/SCHWARZSCHILD_LIKE_CUDA_COMPLETION_AUDIT.md`
  — SCHW.5 completion audit (closes the SCHW.11
  capstone's check #3 PARTIAL → PASS); confirms
  the SCHW.5 gap closure prerequisite.
- `src/manifold/CoordinateChart.h` —
  `PenroseLikePlaceholder` enum + the
  `CoordinateChartParameters::compactification_scale`
  slot PENROSE.2 promotes.
- `src/manifold/SchwarzschildLikeWarp.h` — the
  precedent math leaf PENROSE.2 mirrors in shape.
- `src/manifold/ManifoldTransform.h` — the seam
  PENROSE.4 extends.
- `src/manifold/ManifoldMode.h` — the activation
  gate (`is_active(...)`) PENROSE.6 / PENROSE.8
  reuse verbatim.
- `src/cuda/CudaTestKernel.cu:615-655` — the
  SCHW.5 CUDA kernel arm PENROSE.6 mirrors.
- `src/optix/OptixPrograms.cu:773-795` — the
  SCHW.7 OptiX kernel arm PENROSE.8 mirrors.
- `src/io/SceneLoader.cpp::apply_manifold` — the
  SCHW.9 scene-parser surface PENROSE.9 reuses
  verbatim (kebab-case `penrose-like` already
  parseable; only the C++ enumerator identifier
  changes at PENROSE.2).
- `scenes/test_schwarzschild_like_manifold.rrscene`
  — the SCHW.9 fixture PENROSE.9 mirrors in
  shape.
- `docs/SCHWARZSCHILD_LIKE_FIXTURE.md` — the
  SCHW.9 fixture companion doc PENROSE.9 will
  parallel.
- `tests/manifold_identity_tests.cpp` — the 198
  RR_CHECK test suite PENROSE.* extends; PENROSE.2
  + PENROSE.4 add new test functions.

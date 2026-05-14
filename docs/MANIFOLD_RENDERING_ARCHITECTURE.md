# RelativityRender — Manifold Rendering Architecture

Status: **Architecture document only. No source code lands with this
artifact.** Per the master instructions (rules #2, #3, #12) the only
deliverable of the *Manifold Core Pivot — Architecture Doc* stage is this
file. Concrete modules, headers, kernels, and tests land in their own
incremental commits, each with a BUILD_PLAN.md update and reference output.

This document is read alongside:

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — top-level rules and
  the 25-step development order.
- `docs/MASTER_ARCHITECTURE.md` — current layered architecture; this
  document extends §3's layer diagram and §6's forbidden-dependency matrix
  without superseding either.
- `docs/MODULE_MAP.md`, `docs/DEVELOPMENT_RULES.md`,
  `docs/BUILD_PLAN.md` — module ownership, engineering rules, per-slice
  implementation status.

---

## 1. Purpose & scope

This document records the *Manifold Core Pivot*: a reframing of
RelativityRender from a light-field-only relativistic renderer (special
relativity applied on a flat Minkowski background) into a metric-space /
coordinate-system rendering engine.

The pivot is **structural**. It introduces two new module slots in the
existing Rendering Domain layer (L4):

1. a **Manifold Core** (§3), which becomes the operational core of the
   renderer's relativistic behaviour, and
2. an **optional** Perceptual Field Interpretation sibling (§6), which
   exposes non-light fields as additive image layers.

The pivot does not delete code. It does not rewrite the path tracer.
It does not invalidate `docs/MASTER_ARCHITECTURE.md`. It does not
deprecate `src/relativity/`. Each of those is subsumed or extended in
the manner enumerated below; none is replaced. The current CUDA / OptiX
renderer remains the photon/render substrate (§7).

---

## 2. Why the pivot

The current renderer treats *the light field on Minkowski spacetime* as
the only physical primitive. Special-relativistic perception (aberration,
Doppler shift, searchlight beaming) is applied to that field via a single
Lorentz boost of the observer (see `src/relativity/RelativityMath.h`,
the `aberrateDirection` / `dopplerFactor` / `searchlightFactor` leaf).
Two limits motivate the pivot:

1. **Geometric.** Physical light propagation in curved spacetime cannot
   be expressed as a Lorentz boost of straight rays. The light
   *direction* and the *coordinate system the direction is measured in*
   are not separable — straight Cartesian rays are only well-defined on
   a flat chart. Lensing, frame-dragging, horizon crossing, and Penrose
   compactification all live in the chart/metric, not in the ray.
2. **Interpretive.** The renderer's expressive surface is restricted to
   the EM field. Other fields the user might want to surface (a
   spacetime-metric perturbation, a scalar Klein-Gordon-like analogue, a
   curvature invariant) have no abstraction in the current core.

The pivot's response is to make the *coordinate chart and the metric on
it* the new core primitive, with light propagation defined relative to
that metric, and to expose an optional sibling that maps non-light fields
onto the chromatic output. The pivot's **non-response** is enumerated in
§8 and is equally important — in particular it is *not* a full
general-relativity solver, *not* a quantum field simulator, and *not* a
replacement for the existing path tracer.

---

## 3. New core ontology

The Manifold Core introduces five concepts. Each is described here as a
*contract* — the shape of the abstraction — not an implementation. This
document does not name files, classes, function signatures, or kernel
layouts beyond what is needed to fix the contract. Concrete code lands in
later milestones, each as its own commit with tests.

### 3.1 Coordinate Chart

A **coordinate chart** is a map from an open region of the rendered
manifold to an open subset of R^n (n = 4 for spacetime, n = 3 for
spatial-only modes). A chart provides:

- a **domain predicate** — is a manifold point covered by this chart?
- a **forward map** — manifold point → chart coordinates,
- an **inverse map** — chart coordinates → manifold point,
- a **chart identity tag** — used by the renderer for caching, AOV
  labelling, and observation-mode selection.

A scene may carry **multiple charts** covering overlapping regions of the
same physical manifold (e.g. a Schwarzschild chart plus a
Kruskal-Szekeres chart on the same black-hole spacetime). Charts are
*data*, not code generators: the renderer must be able to switch the
active chart between frames without recompiling kernels.

### 3.2 Metric Tensor

A **metric tensor** is a rank-(0,2) tensor field defined on a chart's
coordinate domain. It provides:

- pointwise **components** `g_{μν}(x)` at a chart coordinate `x`,
- the **inverse** `g^{μν}(x)`,
- (optionally) the **Christoffel symbols** `Γ^λ_{μν}(x)` for use by the
  geodesic integrator.

The metric is the carrier of *spacetime deformation*. The flat
(Minkowski) metric on the identity chart is the degenerate case that
reproduces today's renderer exactly. A Schwarzschild metric on a
Schwarzschild chart is the first non-trivial case the pivot targets
(§5.3 milestone 3).

Christoffel symbols may be:

- **analytic** — evaluated in closed form from `g_{μν}` (e.g.
  Schwarzschild, Kerr in Boyer-Lindquist coordinates), or
- **numerical** — finite-differenced from `g_{μν}` when no analytic form
  is convenient.

Both satisfy the same contract; the geodesic integrator does not branch
on which is in use.

### 3.3 Observer Frame

An **observer frame** is a tetrad (orthonormal frame) attached to a
worldline in the chart, together with the four-velocity along that
worldline. It provides:

- the **tetrad** `{e_(a)^μ(τ)}` expressed in chart coordinates,
- the **four-velocity** `u^μ(τ)`,
- helpers to convert between **tetrad-local** quantities (what the
  observer physically measures) and **chart** quantities (what the
  geodesic integrator advances).

The existing special-relativistic camera (constant Lorentz boost relative
to scene-rest) is exactly the special case `MinkowskiChart +
ConstantVelocityFrame`. The new ontology *subsumes* today's
`src/relativity/` without invalidating it — `aberrateDirection`,
`dopplerFactor`, `searchlightFactor`, and `PrecomputedRelativity` are
reinterpreted as the Minkowski + constant-velocity specialisation of the
observer-frame contract (see §7.2).

### 3.4 Geodesic / ray propagation

A **geodesic integrator** advances a null geodesic (light ray) by one
step in chart coordinates, given the active metric and a state
`(x^μ, p^μ)`. It exposes:

- a **single-step advance** with a chart-local step length and an
  integrator-chosen scheme (RK4 by default; the contract does not fix the
  scheme),
- a **chart-boundary signal** so the renderer can hand off to a different
  chart when the geodesic leaves the current chart's domain,
- a **termination signal** for hitting geometry, leaving the scene
  bounds, or crossing a horizon proxy.

Straight-line propagation on the Minkowski chart is the degenerate case:
the integrator returns `(x + Δλ · p, p)` and the existing path-tracer
intersection layer may short-circuit the per-step call. The contract
*permits* the short-circuit; it does not *require* it.

### 3.5 Spacetime deformation layer

The **spacetime deformation layer** is the orchestrator that holds:

- the set of charts on the scene,
- the metric associated with each chart,
- the chart-transition rules (how a geodesic crosses a chart boundary),
- the **active observation chart** — the chart the camera is currently
  parameterised in.

It is the public surface the rendering algorithm sees. The path tracer
asks the spacetime deformation layer for "advance one step of a null
geodesic" and "what is the observer frame at this worldline parameter?"
— it does not see chart internals or call into individual metrics.

This is where Phase 2 (§5) lives operationally. It is also the surface
that Phase 1 (§6) reads when a perceptual-field interpretation needs
the metric, the geodesic history, or the active tetrad.

---

## 4. Revised layer diagram

The Manifold Core slots into L4 of the existing master diagram
(`docs/MASTER_ARCHITECTURE.md` §3). L0–L2 and L3 are unchanged. Only
L4 grows; L5 acquires the chart-aware seam described in §7.1.

```
+-------------------------------------------------------------+
| L5  Rendering Algorithms                                    |
|     - Path Tracer  (now chart-aware via Manifold Core)      |
|     - Progressive Renderer                                  |
|     - Render Passes / AOVs                                  |
|     - Denoiser Integration                                  |
+-------------------------------------------------------------+
| L4  Rendering Domain                                        |
|     - Scene Graph                                           |
|     - Geometry / Material / Texture / Lighting              |
|     - Camera System                                         |
|     - Manifold Core           <-- NEW                       |
|         - Coordinate Charts                                 |
|         - Metric Tensors                                    |
|         - Observer Frames                                   |
|         - Geodesic Integrator                               |
|         - Spacetime Deformation Layer                       |
|     - Perceptual Field Interpretation   <-- NEW, OPTIONAL   |
|         - Field Samplers                                    |
|         - Field -> light remap                              |
|     - Relativistic Camera Model                             |
|         (subsumed by Manifold Core's Minkowski +            |
|          constant-velocity-frame specialisation;            |
|          kept as a legacy alias, not deleted)               |
+-------------------------------------------------------------+
| L3  GPU Backends (unchanged)                                |
|     - CUDA Backend                                          |
|     - OptiX Backend                                         |
+-------------------------------------------------------------+
```

The diagram is additive. Nothing in MASTER_ARCHITECTURE.md §3 is
removed; the existing entries in L4 are preserved verbatim and joined
by the two new module slots.

---

## 5. Phase 2 — Manifold deformation as the CORE

Phase 2 is the operational core of the pivot. The path tracer stops
thinking of rays as `origin + t·direction` in world space and instead
asks the spacetime deformation layer to advance them.

### 5.1 Principle — light is "normal", the coordinate space bends

In the Manifold Core, a photon's worldline is a null geodesic of the
*active metric*. The renderer does **not** deform light directly. The
renderer chooses a coordinate chart whose metric encodes the
deformation; the integrator advances the photon's chart coordinates
according to that metric. The visual effect — light bending around a
Schwarzschild mass, frame-dragging in Kerr, conformal compression near
Penrose-diagram infinity — is the by-product of how the chart
represents the underlying physical spacetime, not of any modification
applied to the photon itself.

In other words: in the chart-local frame the photon obeys
`g_{μν} p^μ p^ν = 0` and `dp^μ/dλ = -Γ^μ_{αβ} p^α p^β`, which on a flat
metric reduces to straight-line propagation and on a curved metric
naturally produces the lensing / dragging / compactification the
classical diagrams illustrate.

### 5.2 Light travels through transformed coordinate space

The path-tracer step that today reads (pseudocode)

```
x_next = x + t * d
```

becomes

```
(x_next, p_next, status) = spacetime.advance(chart_id, x, p, dλ)
```

where `spacetime` is the spacetime deformation layer (§3.5), `chart_id`
is the active chart, `(x, p)` is the chart-space ray state, and `dλ` is
the integrator's affine-parameter step. The path tracer does not see the
metric; it sees `advance(...)`. This is the *only* seam the pivot adds
to the integrator (§7.1).

For the identity chart (`MinkowskiChart`) the implementation of
`advance` is literally `x + dλ · p_spatial` plus a passthrough of `p`,
and the existing intersection layer remains the natural per-step
consumer. For Schwarzschild and richer charts, `advance` integrates the
geodesic equation in chart coordinates.

### 5.3 Visualisation-mode slots (future, *named only*)

The following slots are reserved in the chart registry. **None is
implemented as part of this document.** Each lands in its own milestone
with tests and a reference image; this document fixes only the slot
names and their intent.

- **Identity / Minkowski** — flat metric, straight rays. Always
  present. Default observation mode. Reproduces today's renderer
  bit-for-bit on Minkowski-only scenes. *(First implementation
  milestone after this doc; see §10.)*
- **Schwarzschild** — static spherically symmetric black hole, single
  mass parameter. First curved-metric milestone. Validates the geodesic
  integrator against the classical light-ring / shadow geometry.
- **Kruskal-Szekeres** — maximal analytic extension of the Schwarzschild
  spacetime. Same *physical* spacetime as the Schwarzschild slot, but
  a different chart that removes the coordinate singularity at the
  horizon. Exercises the chart-transition contract in §3.5.
- **Penrose (conformal)** — conformally compactified chart that maps
  asymptotic infinities onto a finite boundary. Used for diagrammatic
  / pedagogical visualisation modes; not intended for primary-ray
  tracing of detailed production scenes.
- **Kerr (Boyer-Lindquist)** — rotating black hole. Adds the
  ergosphere and frame-dragging; exercises non-trivial tetrad
  transport in §3.3. First Kerr milestone is approximate and
  ergosphere-aware, not certified against a reference geodesic
  integrator (§8 non-goal).

The list is intentionally open. Other charts (Eddington-Finkelstein,
Painlevé-Gullstrand, isotropic Schwarzschild, FLRW for cosmological
tests, weak-field linearised metrics) may be added later without
changing the contracts in §3.

---

## 6. Phase 1 — Perceptual field interpretation (OPTIONAL)

Phase 1 is the broader *interpretation* layer. It is **explicitly
optional**: the core renderer must remain useful with Phase 1 absent,
and Phase 1 must not appear in the dependency closure of L3 or below
(§9).

The full design — field types (scalar / vector / tensor / curvature /
probability-amplitude placeholder), output channels (color / emission /
distortion / density / chromatic shift / diagnostic AOV), composition
semantics, non-goals, additive dependency rules, and the first
implementation slice (FIELD.1 Kretschmann-scalar diagnostic AOV) — is
specified in [`docs/FIELD_INTERPRETATION_LAYER.md`](FIELD_INTERPRETATION_LAYER.md).
The sketches in §6.1 / §6.2 / §6.3 below are the high-level summary;
the design doc is the authoritative source.

### 6.1 Perceptual field interpretation

The intent of Phase 1 is to surface fields *other than* the EM field
through the renderer's only output (an RGB image). A **field sampler**
evaluates a non-EM field along the observer's worldline or across a
chart region. A **field interpretation** then maps that sampled value
into the chromatic / luminous output via an interpretation kernel.

The mapping is **explicitly artistic / interpretive**, not physical. It
exists so that field content the EM field cannot carry (e.g. a scalar
amplitude, a curvature invariant, a metric perturbation) becomes
visible. Phase 1 is *how* the renderer expresses a quantum-field or
spacetime-field hypothesis; it is *not* a simulation of that field's
dynamics (§8 non-goal).

### 6.2 Quantum / spacetime / scalar / tensor fields → visible light / colour / distortion

A field sampler may operate on:

- a **scalar field** — e.g. an artist-supplied scalar texture in chart
  coordinates, or a Klein-Gordon-like wavefunction analogue `φ(x)`
  treated as input data (not evolved by the renderer);
- a **vector / tensor field** — e.g. a metric perturbation `h_{μν}`,
  an electromagnetic tensor `F_{μν}`, a fluid four-velocity, an
  artist-supplied tensor texture;
- a **derived scalar from a tensor field** — e.g. the Ricci scalar
  `R`, the Kretschmann scalar `K = R_{μνρσ} R^{μνρσ}`, the
  electromagnetic invariants `(E^2 − B^2)` and `(E·B)`.

The interpretation kernel composes the sampled value into one or more
of the following output channels:

- **luminous** — additive emission contribution along a ray (e.g.
  `K → emission` produces visible brightness near a curvature
  singularity);
- **chromatic** — hue / saturation modulation of the beauty pass
  (e.g. `arg(φ) → hue`, `|φ|² → saturation`);
- **geometric distortion** — a *small* additive perturbation to the
  primary-ray direction in tetrad-local coordinates, *clamped* and
  declared as artistic, not as a metric modification.

The third form is deliberately constrained. Any large geometric
deformation belongs in a coordinate chart and a metric (§3.1–§3.2), not
in a Phase 1 interpretation kernel. This keeps the line between
"physical" (Manifold Core) and "interpretive" (Phase 1) crisp.

### 6.3 Implemented as interpretation modules, not renderer foundation

A field-interpretation module:

- **consumes** the Manifold Core's read-only surface (chart, metric,
  observer frame, geodesic samples) and any user-provided field
  samplers,
- **emits** an additive contribution to a named AOV or to the beauty
  pass,
- **is composable** with other field-interpretation modules in any
  order within a single AOV (the renderer's AOV accumulation buffer
  guarantees commutativity).

A field-interpretation module is **not** a path-tracer integrator, not
a coordinate chart, not a metric, and not an observer frame. It does
not advance geodesics. It does not modify the metric. If a Phase 1
module needs any of those, the work belongs in the Manifold Core
instead, and the module's design must be revisited before
implementation.

This is why Phase 1 is *optional*: the Manifold Core contracts in §3
make no reference to any field-interpretation module. A scene with no
Phase 1 modules renders exactly as the Minkowski- or Schwarzschild-chart
light field; Phase 1 contributes additive layers during shading or as
separate AOVs. Production renders that do not need field interpretation
pay zero overhead, and Phase 1's mapping is permitted to evolve faster
than the physical core because it never appears in a core contract.

---

## 7. Relationship to the existing renderer

The existing CUDA / OptiX path tracer remains the **photon / render
substrate** of the platform. The Manifold Core sits *above* the camera
system and *below* the integrator's geometry-intersection stage, and it
feeds transformed ray state into the existing substrate. Nothing in
the current `src/cuda/`, `src/optix/`, `src/pathtracer/`,
`src/renderer/`, `src/geometry/`, `src/material/`, `src/texture/`,
`src/lighting/`, `src/scene/`, `src/io/`, `src/gpu/`, or `src/image/`
trees needs to be redesigned for the pivot.

### 7.1 The seam — chart-aware primary ray + step

The seam is the **primary-ray generation and per-step advance**. Today:

- the camera produces `(origin, direction)` pairs in flat Cartesian
  world space (`generate_camera_ray` in `src/camera/CameraRay.h`);
- the path tracer steps them as `origin + t · direction`.

After the pivot:

- the camera produces a primary state in the **observer's tetrad**
  (§3.3),
- the Manifold Core maps that tetrad state into the **active chart**
  via the spacetime deformation layer (§3.5),
- the path tracer advances the state via the **geodesic integrator**
  (§3.4) instead of straight-line arithmetic,
- per-step samples are handed to the existing intersection / shading
  pipeline in the chart's local frame at that step.

For the identity / Minkowski chart this seam is a no-op: the tetrad-to-
chart map is the identity, the geodesic integrator is the identity
straight-line step, and the path tracer behaves exactly as it does
today. **No visual regression is acceptable** on the Minkowski path;
this constraint is what makes the pivot tractable as an incremental
migration (master rule #2: every step compilable, every step working)
rather than a rewrite (master rules forbid #4: "do not jump to a
later system before the current layer works").

### 7.2 The current `src/relativity/` module

The current special-relativistic helpers — `aberrateDirection`,
`dopplerFactor`, `searchlightFactor`, `applyDopplerColor`,
`PrecomputedRelativity` — become the Minkowski + constant-velocity-frame
specialisation of:

- **observer-frame Lorentz boost of the tetrad** → aberration of the
  primary direction in tetrad-local coordinates;
- **frequency ratio of the photon four-momentum along a constant-
  velocity worldline** → `dopplerFactor`;
- **bolometric I/ν^3 invariant along that worldline** →
  `searchlightFactor`.

They are *not* deleted by this pivot. They are reinterpreted: the
Manifold Core's Minkowski chart pins down the specialisation that today's
helpers implement, and the helpers continue to be used unchanged in
that specialisation. When the Manifold Core lands its Minkowski
implementation (§10 step 1), the existing tests on these helpers must
continue to pass bit-for-bit.

### 7.3 What this means for the path tracer

The path tracer becomes **chart-aware** but not **chart-implementing**.
It calls into the Manifold Core for ray advancement; it does not
inspect charts or metrics. Its BSDF / NEE / MIS machinery (see
`docs/PATH_TRACER_MIS_PLAN.md`, `src/pathtracer/Bsdf.h`,
`src/pathtracer/DirectLight.h`, `src/pathtracer/Mis.h`) is unchanged:
per-step shading happens in the local chart frame at the intersection
point, which on the Minkowski chart is identically the existing world
frame.

The renderer's existing AOVs (Beauty, Normal, Depth, Albedo,
DopplerFactor, SearchlightFactor) are unchanged. New AOVs introduced
later — e.g. a chart-identity AOV, a Kretschmann-scalar AOV — slot in
through the existing AOV machinery in `src/renderer/`.

---

## 8. Non-goals for now

This pivot is deliberately narrow at the architecture level. Until the
master instructions, BUILD_PLAN.md, and a subsequent architecture
addendum say otherwise, the pivot does **not** introduce, claim, or
plan:

- **A full general-relativity solver.** No Einstein-equation evolution,
  no matter back-reaction, no numerical-relativity machinery, no
  constraint-preserving integrators. The metric is *input* to the
  renderer, not solved by it. Schwarzschild and Kerr arrive as
  closed-form metric data; perturbed or numerical-relativity metrics
  may arrive later as imported data but are not a goal of this pivot.
- **Real quantum field simulation.** Phase 1's scalar / vector / tensor
  fields are *artistically mapped* data, not physically evolved
  quantum fields. There is no Klein-Gordon evolution on a curved
  background, no QFT-in-curved-spacetime sampler, no path-integral
  machinery. A `φ(x)` field is an input texture; what the renderer does
  with it is interpretive shading, not physics.
- **Physically exact Kerr ray tracing.** The Kerr chart slot exists as
  an ontology placeholder. The first Kerr milestone will be
  approximate and ergosphere-aware but **not** certified against a
  reference geodesic integrator. A physically-exact Kerr renderer is
  a separate, future programme that may or may not be pursued.
- **Replacing the current path tracer.** The existing CUDA / OptiX
  integrator stays. The Manifold Core slots in above it via the §7.1
  seam. BSDF / NEE / MIS / RR / firefly clamps / progressive
  accumulation / denoising are not on the rewrite path.
- **Removing or deprecating `src/relativity/`.** It is subsumed
  (§7.2), not retired. The Minkowski + constant-velocity-frame
  specialisation continues to consume those helpers unchanged.
- **An empty scaffold for the Manifold Core.** Master rule #3 forbids
  fake stubs. No `src/manifold/`, `src/spacetime/`, `src/field/`, or
  similar directories are created by this document. They land when
  the §10 milestones land, each with a real, tested implementation.

These boundaries hold until a future architecture addendum lifts them
*explicitly*.

---

## 9. Dependency rules (additions only)

The Manifold Core (§3) is a new L4 module sibling to the existing
Rendering Domain modules. Its dependency surface:

| Direction                                          | Allowed                                                                       |
|----------------------------------------------------|-------------------------------------------------------------------------------|
| Manifold Core MAY depend on                        | Math Library, Image System, Scene Graph (chart metadata only), Relativistic Camera Model leaf (`src/relativity/`, header-only; for the §7.2 Minkowski + constant-velocity-frame subsumption bridge) |
| Manifold Core MUST NOT depend on                   | GPU Backends, Path Tracer, Progressive Renderer, Renderer Server, Bridge, UI  |
| Path Tracer MAY depend on                          | Manifold Core (chart-aware advance), as it already depends on Camera/Geometry |
| Camera System MAY depend on                        | Manifold Core (observer-frame helper); free to ignore it for Minkowski        |
| Relativistic Camera Model (existing `src/relativity/`) MAY depend on | Math Library only — unchanged                                       |

The Perceptual Field Interpretation (§6) is a new L4 module sibling to
the Manifold Core:

| Direction                                          | Allowed                                                                                 |
|----------------------------------------------------|-----------------------------------------------------------------------------------------|
| FieldInterpretation MAY depend on                  | Math, Image, Scene Graph, Manifold Core (read-only)                                     |
| FieldInterpretation MUST NOT depend on             | GPU Backends, Path Tracer internals, Progressive Renderer internals, Server, Bridge, UI |
| Anything below L4 MUST NOT depend on FieldInterpretation | Enforced by absence of include paths and link edges                               |
| Manifold Core MUST NOT depend on FieldInterpretation     | The optional sibling must never appear in the core's closure                      |

The existing forbidden-dependency matrix in `docs/MASTER_ARCHITECTURE.md`
§6 remains in force. This pivot adds rows; it removes none.

---

## 10. What lands next

This document is the **only** deliverable of the *Manifold Core Pivot —
Architecture Doc* stage. The operational sequence of slices that
threads the Manifold Core's POD surface into the existing CUDA / OptiX
renderer is specified in
[`docs/MANIFOLD_INTEGRATION_PLAN.md`](MANIFOLD_INTEGRATION_PLAN.md)
(MANI-I.1 through MANI-I.8); the high-level list below is the source-
side milestone view, the integration-plan doc is the renderer-side
operational view, and the two are read together.

The next concrete commits, in order, each as
its own incremental slice with tests and a BUILD_PLAN.md entry:

1. **Manifold Core — Minkowski chart.** Implements the identity chart,
   the Minkowski metric, the constant-velocity observer frame, and the
   straight-line geodesic integrator. Wraps today's renderer behaviour
   as the Manifold Core's degenerate case. Acceptance: every existing
   Minkowski test image reproduces bit-for-bit; the existing
   relativity-leaf tests pass unchanged. No code is deleted; the
   `src/relativity/` helpers are consumed unchanged.
2. **Manifold Core — chart-aware seam in the path tracer.** Replaces
   the path tracer's straight-line step with a call into the geodesic
   integrator, behind the Minkowski chart. Visual output unchanged.
   Acceptance: ctest still 100% green on both audit-host and
   CUDA / OptiX hosts.
3. **Schwarzschild chart.** First non-trivial metric. Reference image:
   a single Schwarzschild black hole at a fixed mass with the camera
   at a known geodesic distance, validated against a published
   ray-traced reference figure. Acceptance criteria for the reference
   match are written in the milestone's own audit doc.
4. **Kruskal-Szekeres chart and the chart-transition contract.** Same
   physical spacetime as milestone 3, different chart; exercises the
   chart-boundary signal of §3.4.
5. **Penrose (conformal) chart.** Diagrammatic mode; not intended for
   production scenes. Lands with its own AOV.
6. **Kerr (Boyer-Lindquist) chart.** First rotating-black-hole
   visualisation, approximate per §8.

After milestone 6 a separate architecture addendum opens the optional
**Perceptual Field Interpretation** programme:

7. **First field-interpretation module.** Likely a curvature-invariant
   AOV (e.g. Kretschmann scalar `K → emission`) on the Schwarzschild
   chart. Exercises the §6 contracts with the lightest possible
   field-sampler / interpretation kernel.

Each milestone is its own commit, its own BUILD_PLAN.md entry, and its
own optional audit doc under `docs/`. None of milestones 1–7 is started
by this commit.

---

## 11. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — top-level rules
  and the 25-step development order.
- `docs/MASTER_ARCHITECTURE.md` — the layered architecture this
  document extends. §3 layer diagram and §6 forbidden-dependency
  matrix remain authoritative; this document adds entries.
- `docs/MODULE_MAP.md` — per-module ownership rules; will receive a
  Manifold Core entry when §10 step 1 lands.
- `docs/DEVELOPMENT_RULES.md` — engineering, dependency, GPU, and
  process rules; unchanged.
- `docs/BUILD_PLAN.md` — per-slice implementation status; will receive
  a Manifold Core entry when §10 step 1 lands.
- `docs/MILESTONE_ROADMAP.md`, `docs/ROADMAP_AUDIT.md`,
  `docs/ROADMAP_PROPOSED_ALIGNMENT.md` — long-term milestone tracking;
  to be cross-referenced when the Manifold Core milestones acquire
  their own M-numbers.
- `src/relativity/RelativityMath.h`, `src/relativity/RelativityParams.h`
  — the existing special-relativistic helpers reinterpreted in §7.2 as
  the Minkowski + constant-velocity-frame specialisation of the new
  ontology.
- `src/camera/CameraRay.h`, `src/camera/Camera.h` — the existing
  primary-ray generation that the §7.1 seam will sit alongside.
- `src/pathtracer/PathTracer.h`, `src/pathtracer/Bsdf.h`,
  `src/pathtracer/DirectLight.h`, `src/pathtracer/Mis.h` — the path
  tracer surfaces that the §7.1 seam will call into and that §7.3
  describes as chart-aware-but-not-chart-implementing.

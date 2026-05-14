# RelativityRender — Field Interpretation Layer

Status: **Architecture document only. No source code lands with this
artifact.** Per the master instructions (rules #2, #3, #12) the only
deliverable of this stage is this file. Concrete modules, headers,
kernels, and tests land in their own incremental commits, each with a
BUILD_PLAN.md update and reference output.

This document is read alongside:

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — top-level rules
  and the 25-step development order.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` — the layered architecture
  this document extends. §3 names the Manifold Core's read-only
  surface; §6 sketches Phase 1; §9 fixes the additive dep rules. This
  document fills in §6 in detail and elaborates §9 for the
  field-interpretation layer.
- `docs/MASTER_ARCHITECTURE.md` — long-term project architecture
  outside the Manifold Core Pivot.
- `docs/MODULE_MAP.md`, `docs/DEVELOPMENT_RULES.md`,
  `docs/BUILD_PLAN.md` — module ownership, engineering rules,
  per-slice implementation status.

---

## 1. Purpose & scope

The **Field Interpretation Layer** is the optional sibling slot to the
Manifold Core named in `MANIFOLD_RENDERING_ARCHITECTURE.md` §6. It is
the *Phase 1* of the Manifold Core Pivot: a mechanism for surfacing
fields *other than* the electromagnetic / light field through the
renderer's only output (an RGB image), by mapping non-light field
samples into the chromatic / luminous / volumetric / distortion
channels of the existing rendering pipeline.

This document fixes the layer's:

- **Purpose** (§2) — what the layer is for and why it is optional;
- **Field types** (§3) — what a Phase 1 module is allowed to consume:
  scalar, vector, tensor, curvature, and a probability / amplitude
  placeholder;
- **Mapping outputs** (§4) — what a Phase 1 module is allowed to emit:
  color, emission, distortion, density, chromatic shift, diagnostic
  AOV;
- **Relationship to the Manifold Core** (§5) — how the two layers
  compose, what each is forbidden to touch in the other, and why the
  Field Interpretation Layer never replaces the metric / coordinate
  engine;
- **Composition semantics** (§6) — the order-independence /
  commutativity contract Phase 1 modules must respect;
- **Non-goals** (§7) — what this layer deliberately is *not*;
- **Dependency rules** (§8) — additions only;
- **First implementation slice** (§9) — what lands when the
  prerequisites are ready.

The document is **structural**. It introduces no code and no test
binary; nothing in `src/manifold/`, `src/relativity/`, `src/cuda/`,
`src/optix/`, `src/pathtracer/`, `src/renderer/`, or any other source
tree is touched by the stage that lands this file. The Field
Interpretation Layer's first concrete commit is scheduled in §9.

---

## 2. Purpose

The renderer's only output is an RGB image; the renderer's only
physical primitive (today and after the Manifold Core lands) is the
electromagnetic / light field. The universe carries information in
many fields the EM field cannot represent directly: a quantum
wavefunction's amplitude, the curvature of spacetime around a mass, a
gauge-field potential, an artist-supplied scalar texture standing in
for a non-EM "probe". The Field Interpretation Layer exists so those
fields become visible — by mapping their sampled values into the
chromatic, luminous, volumetric, or geometric channels the renderer
already emits.

The layer is **explicitly artistic / interpretive**, not physical:

- It does **not** simulate the field's own dynamics. A scalar field
  `φ(x)` consumed by a Phase 1 module is *input data*, not an
  evolving Klein-Gordon wavefunction the renderer integrates.
- It does **not** modify the metric, the coordinate chart, the
  observer frame, or the geodesic equation. Any physical /
  geometric effect that bends light belongs in the Manifold Core
  (`docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3); this layer
  reads the Manifold Core's surface read-only.
- It does **not** appear in the renderer's foundation. A
  production render that does not need field interpretation pays
  zero overhead, and the pixel pipeline is byte-identical with
  zero Phase 1 modules active.

The phrase "perception transcoding" captures the layer's intent:
it transcodes the *perception* of a non-light field (computed by
some sampler the artist or scene author supplies) into a form the
renderer's eye-side output stage can consume.

---

## 3. Field types

A Phase 1 module consumes one or more **field samplers**. A field
sampler is a read-only callable: given a chart-coordinate event
(and optionally an active `ManifoldTransform`), it returns a value
of one of the field types below. Field samplers are *data*, not
integrators; they do not evolve, they evaluate.

Each field type carries a value-shape (scalar / vector / tensor),
a default interpretation hint (what kind of output it most
naturally feeds), and a list of canonical worked examples. The
hints are non-binding — a Phase 1 module is free to map any field
type into any output channel — they exist to document the
expected use case so artist tooling can default sensibly.

### 3.1 Scalar field

A scalar field assigns a real (or, for the amplitude placeholder
in §3.5, complex) number `φ(x)` at every chart-coordinate event.

Examples the layer is built to support:

- An **artist-supplied scalar texture** in chart coordinates,
  loaded from disk or generated procedurally. The texture is
  evaluated at the chart event during shading.
- A **Klein-Gordon-like wavefunction analogue** `φ(x)` carried
  by the scene as data. The Phase 1 module treats it as static
  input — the renderer does **not** evolve it in time.
- A **density scalar** representing a non-EM "stuff" the artist
  wants visible (e.g. a fluid concentration, a charge density,
  a temperature field).

Default interpretation hints (non-binding):

- `|φ|` → emission magnitude;
- `sign(φ)` → bipolar colour (positive / negative palette);
- `φ - φ_ref` → chromatic shift relative to a reference value.

### 3.2 Vector field

A vector field assigns a 3-vector `V(x)` (or, for some
spacetime contexts, a 4-vector `V^μ(x)`) at every chart-
coordinate event.

Examples:

- A **fluid velocity field** — the local flow direction and
  speed of some non-EM medium.
- A **gauge-field potential** `A^μ` — the electromagnetic
  four-potential or a non-Abelian analogue, treated as input
  data rather than as the source of an EM evolution.
- A **gradient field** `∇φ` derived from a scalar field
  sampler — the layer permits chaining samplers, with the
  sampler's output serving as another sampler's input.

Default interpretation hints:

- `V / |V|` → direction → hue (mapping `S^2 → hue circle`);
- `|V|` → emission or density;
- `V` projected onto the view direction → chromatic shift
  proportional to the projected magnitude (Doppler-like
  visual analogy).

### 3.3 Tensor field

A tensor field assigns a rank-`(p, q)` tensor at every chart-
coordinate event. Phase 1 ships support for at minimum:

- a **rank-(0, 2) symmetric tensor** (e.g. a metric
  perturbation `h_{μν}` or an artist-supplied "tensor texture");
- the **electromagnetic field tensor** `F_{μν}` (treated as
  input data when sourced externally, or evaluated from a
  vector-field potential `A^μ`);
- a **stress-energy-like tensor** `T_{μν}` (input data;
  Phase 1 does not solve the Einstein field equations from it
  — that is a non-goal per §7).

Phase 1 modules typically reduce a tensor to a scalar before
mapping (via a trace, contraction, or invariant) and then feed
that scalar through the §3.1 hints. The reductions are
documented per module; common ones are:

- `tr(h)` → scalar magnitude;
- `F_{μν} F^{μν}` → scalar EM invariant;
- `T_{μν} u^μ u^ν` (energy density observed by the active
  observer frame) → emission.

### 3.4 Curvature field

A curvature field is a **specialisation of §3.3** in which the
tensor is the chart's *intrinsic curvature*: the Riemann tensor
`R^ρ_{σμν}(x)`, its contractions (the Ricci tensor `R_{μν}(x)`,
the Ricci scalar `R(x)`), or the Kretschmann scalar
`K(x) = R_{μνρσ} R^{μνρσ}`. The distinguishing property of a
curvature field is that it is **derivable from the active
`MetricTensor`** rather than supplied as artist data — though
Phase 1 is permitted to consume an artist-supplied curvature
texture too, when the chart has no concrete metric yet.

Examples:

- **Kretschmann scalar `K` near a Schwarzschild mass** —
  diverges at the singularity, finite at the event horizon;
  the visual cue makes "where curvature is large" legible.
- **Ricci scalar `R`** — vanishes everywhere in vacuum
  Schwarzschild; a Phase 1 module on a Schwarzschild chart
  using `R` is therefore expected to be (numerically) zero
  everywhere unless the chart carries non-vacuum stress-
  energy.
- **Sectional curvature** in some 2-plane defined by the
  observer's tetrad — useful for stylised "how curved is
  this direction" overlays.

Default interpretation hints:

- `K` → emission (large `K` → bright; bounded by a clamp the
  artist controls);
- `R` → density (large `|R|` → opaque);
- a curvature-invariant gradient → distortion (small additive
  perturbation, clamped per §4.3).

**Important:** Phase 1's curvature field is *evaluated* from
the metric; it does **not** modify the metric. The Manifold
Core's `MetricTensor` (architecture-doc §3.2) remains the
single source of truth for the chart's geometry.

### 3.5 Probability / amplitude placeholder

A **placeholder slot** for fields whose natural representation
is a complex amplitude — the wavefunctions of quantum mechanics
and quantum field theory. The slot ships as a contract surface
only; no concrete sampler is implemented this stage (master
rule #3, "no fake stubs pretending to be complete systems").

The intent is to support, in a later slice:

- A **single-particle wavefunction** `ψ(x)` in some chart-
  coordinate region, treated as input data. The renderer does
  **not** evolve `ψ` via the Schrödinger or Dirac equation —
  that simulation belongs outside the renderer's process.
- A **field operator expectation value** `⟨φ̂(x)⟩` from a
  Quantum-Field-Theory-in-curved-spacetime simulation,
  supplied as a complex scalar / spinor sampler.
- A **two-state coherence** ρ between mode labels, presented as
  a per-pixel hue/saturation modulator.

Default interpretation hints (non-binding):

- `|ψ|²` → density (probability density → opacity);
- `arg(ψ)` → hue (phase → colour wheel);
- `Re(ψ)` / `Im(ψ)` → bipolar palette overlays.

The placeholder records the *shape* of the sampler the layer
will eventually accept — a `(chart_event) → complex_value`
read-only callable — without committing to a runtime
implementation today. Until a real sampler ships, scenes
referencing the slot get the documented "no Phase 1 output"
behaviour.

---

## 4. Mapping outputs

A Phase 1 module emits its contribution into one of the
output channels below. Every channel composes additively with
the renderer's existing output for that channel (architecture-
doc §6.3 commutativity), so the order in which Phase 1 modules
fire within a single channel is artist-controlled but does
not affect correctness.

### 4.1 Color

Direct modulation of the beauty pass's chromatic values —
`hue`, `saturation`, or `value` (in the renderer's chosen
colour space). Used for "what is this region" / "what is the
sign of this field" overlays.

Constraints:

- Operates in the renderer's working colour space (currently
  linear sRGB); a future slice may add Phase 1 colour-space
  conversions when the renderer's colour pipeline gains a
  spectral / wide-gamut stage.
- Output is clamped to `[0, 1]` per-channel after composition.
  Phase 1 modules may emit out-of-gamut intermediates; the
  AOV accumulator clamps at the resolve step.

Example: a sign-of-`φ` overlay tints +`φ` red and `-φ` blue
on top of the underlying beauty pass.

### 4.2 Emission

Additive luminous contribution along a ray. The Phase 1
module evaluates the field along the geodesic (using the
Manifold Core's read-only geodesic samples — architecture-doc
§3.4) and emits luminance per-step:

```
    L_field(x_step) = κ_emission · f(field_sampler(x_step))
```

where `κ_emission` is the module's strength scalar (artist-
facing) and `f` is the module's per-sample interpretation
kernel.

Used for: "where curvature is large the image is bright"
(`K → emission`), "where the wavefunction's amplitude is
peaked the image glows".

### 4.3 Distortion

A *small*, *clamped* additive perturbation to the primary-ray
direction in tetrad-local coordinates (architecture-doc §6.2's
third channel). Phase 1 distortion is **explicitly limited**
to perturbations that:

- have a documented per-module magnitude cap (e.g.
  `|δd| ≤ 0.1`),
- are applied in tetrad-local coordinates (not chart-
  coordinate or world-coordinate space, so the perturbation
  composes with the Manifold Core's chart-aware ray seam
  rather than competing with it),
- are declared as artistic, not as a metric modification.

Any large or unbounded geometric deformation belongs in a
coordinate chart and a metric (architecture-doc §3.1–§3.2).
This constraint is **load-bearing** — without it Phase 1
would silently compete with the Manifold Core for the same
visual effect, and the line between "physical" (Manifold
Core) and "interpretive" (Phase 1) would dissolve.

### 4.4 Density

Volumetric absorption / scattering coefficient `σ(x)` along
the ray. Composes with the renderer's future volumetric
machinery via the Beer-Lambert attenuation
`I = I_0 · exp(-∫σ ds)`. Per-step `σ` is sampled from the
field, scaled by the module's strength, and accumulated into
the `accumulated_optical_depth` slot reserved on
`GeodesicState` (architecture-doc §3.4 / `GeodesicState.h`
MANIFOLD.4).

Used for: "high-`|R|` regions are opaque", "high-`|ψ|²`
regions act as a fog of probability".

Note: this slice does not specify a volumetric integrator;
the channel is reserved so that, when the volumetric pipeline
lands (future master-order item beyond the Manifold Core
Pivot), Phase 1 modules slot into it without re-architecting.

### 4.5 Chromatic shift

A per-channel spectral shift applied to the beauty pass, of
the same shape as a Doppler shift but driven by an arbitrary
field rather than a relativistic velocity. The shift `Δλ/λ`
is sampled from the field and applied through the
artist-controlled colour-shift kernel
(`rr::relativity::applyDopplerColor`-style — initially a
warm/cool tanh of a log-ratio, replaced by a real spectral
remap when the renderer's colour pipeline becomes spectral).

Used for: "near a strong-curvature region the colour shifts
toward the violet" (`K → blueshift`), "the wavefunction's
phase induces a hue rotation".

Notes:

- A **physical** Doppler colour shift (driven by a
  relativistic observer velocity through the Manifold Core's
  observer frame) is *not* this channel — that lives in the
  Manifold Core's relativity-subsumption path (architecture-
  doc §7.2). Phase 1's chromatic-shift channel is the
  *artistic* analog: a chromatic shift driven by some
  non-EM field the renderer otherwise could not surface.

### 4.6 Diagnostic AOV

A dedicated arbitrary-output-variable pass that records the
field sampler's per-pixel value directly, without compositing
into the beauty pass. Used for debugging, scientific
visualisation, and post-process pipelines.

Each Phase 1 module that emits a diagnostic AOV declares the
AOV's name, value-shape (scalar / vector / RGB / RGBA), and
clamp policy. The renderer's existing AOV machinery
(`src/renderer/AOV.h`, `GpuAOVBuffer`) is the natural target;
no Phase-1-specific AOV machinery is required.

Diagnostic AOVs are the **safest** Phase 1 output channel —
they do not modify the beauty pass at all and so cannot
introduce regressions in the renderer's pixel pipeline. The
first Phase 1 implementation slice (§9) is expected to
target this channel only.

---

## 5. Relationship to the Manifold Core

The Field Interpretation Layer sits **above** the Manifold
Core in the L4 Rendering Domain layer, as a sibling slot to
the existing Manifold Core module (architecture-doc §4
revised layer diagram). The relationship is strictly
one-way:

```
+-------------------------------------------------------------+
| L4  Rendering Domain                                        |
|     - Manifold Core                                         |
|         - Coordinate Charts                                 |
|         - Metric Tensors                                    |
|         - Observer Frames                                   |
|         - Geodesic Integrator                               |
|         - Spacetime Deformation Layer                       |
|     - Field Interpretation Layer  (Phase 1, optional)       |
|         - Field Samplers (scalar / vector / tensor /        |
|             curvature / probability-amplitude)              |
|         - Mapping kernels (color / emission / distortion /  |
|             density / chromatic-shift / diagnostic-AOV)     |
|         - Reads Manifold Core surface read-only             |
|                                                             |
|         field-interpretation -----> manifold-core           |
|                            (read-only)                      |
+-------------------------------------------------------------+
```

### 5.1 Field interpretation sits *above* the Manifold Core

The Field Interpretation Layer is allowed to **consume** the
Manifold Core's published read-only surface (architecture-doc
§3):

- the **active chart** (read its identity / scale / origin /
  units / params; never mutate);
- the **metric tensor** at the current chart event (read
  components, the inverse, validation helpers; never
  mutate);
- the **observer frame** (read position4, velocity4, beta,
  tetrad, time placeholders; never mutate);
- **geodesic samples** along an active ray (read the
  per-step `GeodesicState` from the integrator; never
  drive the integrator).

The Manifold Core has **no** dependency on the Field
Interpretation Layer. The layer never appears in the
Manifold Core's link line, include set, or closure;
removing the layer at compile time leaves the Manifold
Core's behaviour byte-identical (architecture-doc §6
"explicitly optional").

### 5.2 The Field Interpretation Layer does **not** replace the metric / coordinate engine

This is the single most important invariant of the layer:

- **Phase 1 modules MUST NOT modify the metric.** Any
  "geometric" effect a Phase 1 module wants to express
  through the `Distortion` channel (§4.3) is constrained
  to a small, clamped additive perturbation to the
  primary-ray direction in tetrad-local coordinates.
  Anything larger is a curved-chart concern (architecture-
  doc §3.1–§3.2), not a Phase 1 concern.
- **Phase 1 modules MUST NOT advance geodesics.** The
  geodesic integrator (architecture-doc §3.4) is the
  Manifold Core's surface; Phase 1 modules consume its
  samples, they do not drive it.
- **Phase 1 modules MUST NOT mutate the observer frame.**
  The observer frame is the renderer's authoritative
  description of "what the camera sees"; Phase 1 modules
  read it and produce derived outputs but never replace
  it.
- **Phase 1 modules MUST NOT replace the renderer's path
  tracer or BSDF stack.** Phase 1 emits additive
  contributions into named AOVs / beauty channels via the
  existing AOV accumulation surface; it does not become
  an integrator.

A Phase 1 module that needs to do any of the above is
architecturally misclassified — the work belongs in the
Manifold Core, and the module's design must be revisited
before implementation (architecture-doc §6.3).

### 5.3 Why this separation matters

The separation lets the two layers evolve at different
speeds and with different correctness criteria:

- The **Manifold Core** is a physical / geometric engine.
  Its correctness is anchored to closed-form references
  (Schwarzschild light-ring shadow, Kerr ergosphere,
  Penrose conformal compression). It evolves slowly and
  conservatively, because a regression there is a
  visual-physics bug that propagates everywhere.
- The **Field Interpretation Layer** is an artistic /
  perceptual surface. Its correctness is anchored to
  artist intent and to numerical-stability invariants
  (no NaN, no AOV blow-up, clamped outputs). It evolves
  quickly, because a Phase 1 module's interpretation
  kernel is allowed to change between versions without
  invalidating any physical baseline.

Mixing the two — by, say, letting Phase 1 modify the
metric — would force the Manifold Core to chase Phase 1's
faster evolution rate, and would lose the clean line
between "physical" and "interpretive" that the architecture
relies on.

---

## 6. Composition semantics

A Phase 1 module:

- **consumes** zero or more field samplers and the Manifold
  Core's read-only surface (§5.1);
- **emits** an additive contribution to one or more named
  AOVs or beauty channels (§4);
- **is composable** with other Phase 1 modules in any order
  within a single channel — the renderer's AOV accumulation
  buffer guarantees commutativity for `Color`, `Emission`,
  `Density` (within the Beer-Lambert linear regime),
  `Chromatic shift` (within the linear superposition regime
  of the colour-shift kernel), and `Diagnostic AOV` channels;
- **may be partially commutative** for the `Distortion`
  channel: small distortions compose linearly; large
  distortions are forbidden (§4.3), so the load-bearing
  regime is always linear-and-thus-commutative;
- **is artist-strength-controlled** via a documented scalar
  parameter the module exposes (the `κ` in §4.2's
  expression); strength = 0 disables the module exactly,
  strength = 1 is the module's reference contribution.

### 6.1 Module catalog

Each Phase 1 module is identified by:

- a **name** the artist sees in scene-file authoring;
- a **declared field-sampler shape** (which §3 type does it
  consume);
- a **declared output channel** (which §4 channel does it
  emit into);
- an **interpretation kernel** (the per-sample `f` that
  maps field value → output contribution);
- a **strength scalar** (`κ`);
- optional **chart-region predicates** (e.g. "only fire
  inside `r < 10`" — to keep a Phase 1 module from
  dominating the entire scene).

The catalog is open. Future modules are added without
modifying the layer's contracts.

### 6.2 Disabling Phase 1

Phase 1 is opted into per-scene by attaching at least one
field-interpretation module. A scene with no Phase 1
modules — and `ManifoldMode::enabled = false` — renders
exactly as the pre-pivot renderer. A scene with
`ManifoldMode::enabled = true` but no Phase 1 modules
renders as the Manifold Core's per-chart output with no
field-interpretation layers.

A future CLI / scene-file slice will expose:

- a global "disable all Phase 1 modules" master switch
  (mirroring `ManifoldMode::enabled` for the chart side);
- per-module strength clamps and bypass toggles, so an
  artist can dial down a module without removing it from
  the scene.

This slice does **not** ship the CLI / scene-file surface.

---

## 7. Non-goals

Until a future Field-Interpretation addendum lifts them
explicitly, the layer does **not**:

- **Simulate field dynamics.** A scalar field `φ(x)` is
  input data, never an evolving Klein-Gordon wavefunction.
  An EM tensor `F_{μν}` is input data, never the solution
  of Maxwell's equations the renderer integrates. A
  wavefunction `ψ(x)` is input data, never an integration
  of the Schrödinger or Dirac equation. Field dynamics
  belongs outside the renderer's process; the renderer
  consumes the simulation's output, it does not produce
  it.
- **Solve the Einstein field equations from a
  stress-energy field.** A `T_{μν}` field consumed by a
  Phase 1 module is interpretive data; it does not drive
  the chart's `MetricTensor`. The Manifold Core's metric
  is *input* to the renderer (architecture-doc §8 non-
  goal: "full GR solver").
- **Implement a spectral colour pipeline.** The chromatic-
  shift channel (§4.5) is initially driven by the same
  artistically-mapped tanh-of-log-ratio kernel
  `applyDopplerColor` uses; a real spectral remap arrives
  with the renderer's wider spectral / colour-pipeline
  programme, not within the Field Interpretation Layer's
  own roadmap.
- **Replace the Manifold Core or the path tracer.** Phase 1
  is additive on top of both. A scene with zero Phase 1
  modules is byte-identical to a scene that simply elides
  this layer.
- **Define a new AOV machinery.** The diagnostic-AOV
  channel (§4.6) consumes the existing
  `src/renderer/AOV.h` / `GpuAOVBuffer` surface. If the
  future Phase 1 implementation needs to widen that
  surface (e.g. for non-scalar AOV types), the widening
  goes into the AOV machinery itself, not into a Phase
  1-specific parallel path.

These boundaries hold until a subsequent design doc lifts
them explicitly.

---

## 8. Dependency rules (additions only)

The Field Interpretation Layer is a new L4 module sibling
to the Manifold Core. Its dependency surface:

| Direction                                          | Allowed                                                                                              |
|----------------------------------------------------|------------------------------------------------------------------------------------------------------|
| Field Interpretation Layer MAY depend on           | Math Library, Image System, Scene Graph, Manifold Core (read-only surface only)                      |
| Field Interpretation Layer MUST NOT depend on      | GPU Backends, Path Tracer internals, Progressive Renderer internals, Renderer Server, Bridge, UI     |
| Anything below L4 MUST NOT depend on the Layer     | Enforced by absence of include paths and link edges                                                  |
| Manifold Core MUST NOT depend on the Layer         | The optional sibling must never appear in the Manifold Core's closure                                |
| Path Tracer MAY depend on the Layer's AOV outputs  | Via the existing AOV accumulation surface only; never via direct field-sampler invocation            |

The forbidden-dependency matrix in
`docs/MASTER_ARCHITECTURE.md` §6 and the additive matrix
in `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §9 remain in
force. This document adds rows for the Field Interpretation
Layer; it removes none.

A future module-map (`docs/MODULE_MAP.md`) entry will
formalise the Phase 1 link-line constraints when the
layer's first concrete module lands.

---

## 9. First implementation slice (deferred)

The Field Interpretation Layer **does not start
implementation in this stage**. The architecture-doc §10
milestone order places the layer after the Manifold Core's
Schwarzschild chart slice (§10 step 3) — Phase 1 needs at
least one curved chart to exercise the curvature-field /
emission pipeline meaningfully.

The first Phase 1 implementation slice, scheduled to land
after architecture-doc §10 step 3 (Schwarzschild chart) is
green, will be:

**FIELD.1 — Kretschmann-scalar Diagnostic AOV.**

- **Field type:** curvature field (§3.4); specifically
  the Kretschmann scalar
  `K(x) = R_{μνρσ} R^{μνρσ}` evaluated from the active
  metric.
- **Output channel:** diagnostic AOV (§4.6).
- **Why this combination:** the safest pair. The
  diagnostic-AOV channel does not touch the beauty pass,
  so no regression in the renderer's pixel pipeline is
  possible. The curvature field is evaluated from the
  Manifold Core's metric, so the slice exercises the
  read-only Manifold Core surface end-to-end.
- **Acceptance:** the Kretschmann AOV on a
  Schwarzschild chart at fixed mass / fixed observer
  position matches the closed-form
  `48 M² / r⁶` along a radial line to within
  single-precision tolerance.
- **Out of scope for the first slice:** beauty-pass
  modulation, distortion, density, chromatic shift,
  artist-supplied field samplers. Subsequent slices
  (FIELD.2+) add those channels one at a time, each
  with its own reference image and its own
  `BUILD_PLAN.md` entry.

A later addendum will detail FIELD.2 (artist-supplied
scalar texture → emission) and FIELD.3 (probability-
amplitude sampler → density-and-hue). Both wait on
FIELD.1's contract surface to land.

---

## 10. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — top-
  level rules and the 25-step development order.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` — the layered
  architecture this document extends. §3 names the
  Manifold Core's read-only surface; §6 sketches Phase
  1; §9 fixes the additive dep rules; §10 places the
  Phase 1 milestone after the Schwarzschild chart slice.
- `docs/MASTER_ARCHITECTURE.md` — the long-term layered
  architecture; §6 forbidden-dependency matrix this
  document extends additively in §8.
- `docs/MODULE_MAP.md` — per-module ownership rules;
  will receive a Field Interpretation Layer entry when
  FIELD.1 lands.
- `docs/DEVELOPMENT_RULES.md` — engineering, dependency,
  GPU, and process rules; unchanged.
- `docs/BUILD_PLAN.md` — per-slice implementation
  status; receives a "Field Interpretation Layer —
  Design Doc" stage entry alongside this artifact.
- `src/manifold/*` — the Manifold Core POD surface this
  layer reads in §5.1 (`CoordinateChart.h`,
  `MetricTensor.h`, `ObserverFrame.h`,
  `GeodesicState.h`, `ManifoldTransform.h`,
  `ManifoldMode.h`).
- `src/renderer/AOV.h`, `src/renderer/GpuAOVBuffer.h` —
  the existing AOV accumulation surface the diagnostic-
  AOV channel (§4.6) consumes when FIELD.1 lands.
- `src/relativity/RelativityMath.h` — the existing
  `applyDopplerColor` artistic colour-shift kernel
  that §4.5 inherits the initial form of.

# Field Interpretation Phase 1 Plan (FIELD-I.1)

Date:   2026-05-17
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched
        by this design document; the implementation
        lands in subsequent FIELD-I.* sub-slices that
        consume this doc as their canonical brief.

This document is the design for **Phase 1 of the
Field Interpretation Layer** — the optional
perceptual transcoding layer that sits above the
Manifold Core (chart / metric / observer frame) and
maps non-light fields into the renderer's visible
output channels. The layer is documented at
`docs/FIELD_INTERPRETATION_LAYER.md` (~737 lines)
and skeletoned at `src/field/` (FIELD.1 / .2 / .3
header-only POD surface; ~465 lines across 4
headers + README). This plan defines the
**FIELD-I.* implementation arc** that lifts the
skeleton into a working Phase 1: a real interpretation
pipeline that ships a CUDA + OptiX bridge, a
diagnostic AOV, a fixture scene, and an arc-level
audit.

The FIELD-I.* arc is the natural next architectural
arc after the **OBSERVER.*-family closure**
(OBSERVER.* foundation + OBS-P.* perception
migration + OBS-F.* fixture, all audited
PASS / PASS_WITH_RUNTIME_DEFERRED). The OBSERVER.15
arc capstone identified Phase 1 of the Field
Interpretation Layer as one of three "manifold-
orthogonal work" candidate next slots
(`OBSERVER_FRAME_ARC_AUDIT.md` §9); this plan
authorises that choice.

Per the architecture doc
(`docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §6),
the Field Interpretation Layer is an **OPTIONAL**
extension above the Manifold Core. The renderer
remains a fully-functional spacetime / perception
renderer without it; the layer's role is to give
artists the ability to visualise non-light
quantities (energy densities, temperatures,
probability amplitudes, sentiment overlays, etc.)
as chromatic / luminous / volumetric /
distortion contributions to the standard
rendering pipeline.

Per the operator's task brief, this is
**documentation only**. No source code, no
implementation, no test binary changes. The
implementation lands in the FIELD-I.2 →
FIELD-I.8 sub-slice ladder defined in §5
below.

---

## Naming convention: FIELD.* vs FIELD-I.*

The `src/field/` skeleton was previously
landed under the **FIELD.* slice naming**
(FIELD.1 = skeleton; FIELD.2 = ScalarField
promotion; FIELD.3 = FieldMapping
promotion). The PRESENT arc uses the
**FIELD-I.* naming** (with the `-I` infix,
mirroring MANI-I.* "Manifold Integration"):
the `-I` stands for **Interpretation** —
the act of mapping field data into renderer
output channels.

The distinction is intentional:

- **FIELD.* (legacy)** = data-model
  skeleton. Header-only PODs +
  interpretation-API contracts. Already
  landed.
- **FIELD-I.* (this arc)** = working
  interpretation pipeline. CUDA + OptiX
  bridges + diagnostic AOV + fixture +
  arc capstone. To be landed.

The FIELD-I.* arc consumes the FIELD.*
skeleton verbatim; it does NOT replace it.
Future field-type promotions (Vector,
Tensor, Curvature, ProbabilityAmplitude)
may use the FIELD.* naming again for their
data-model promotions; their interpretation
pipelines would land under their own
FIELD-I-V.* / FIELD-I-T.* etc. sub-arcs.

---

## 1. Purpose

**The Field Interpretation Layer maps
non-light fields into visible render
channels as an OPTIONAL perceptual
transcoding step.**

This is **not** a core physics substrate
addition. The Manifold Core (Coordinate
Chart + Metric Tensor + Observer Frame +
Geodesic Integrator) is what defines the
physical scene; the Field Interpretation
Layer is what makes **non-light quantities
visible** to the operator + viewer when the
artist authors them.

The motivating use cases:

- **Energy / temperature visualisation.**
  A scalar field representing a fluid's
  temperature or a star's surface
  luminance gets mapped into the
  renderer's color or emission channel
  so the artist can SEE the field's
  distribution without authoring
  matching light sources for every value
  bucket.
- **Probability-amplitude visualisation
  (placeholder slot).** A future
  ProbabilityAmplitudePlaceholder field
  type (reserved in `FieldType.h:23`)
  could map a quantum-state amplitude
  into a chromatic shift or alpha
  density without committing to a
  full Schrödinger / Klein-Gordon /
  Dirac evolver (the existing
  `docs/FIELD_INTERPRETATION_LAYER.md`
  §7 non-goal stands).
- **Diagnostic AOVs for non-rendered
  scene state.** A scalar field
  representing a metric component or a
  manifold-warp magnitude can write to a
  dedicated AOV pass so the operator can
  verify the manifold's behaviour
  per-pixel without reading source
  (already partially served by the
  MANI-I.8 `ManifoldCoordinates` AOV;
  the FIELD-I.* path generalises this to
  arbitrary scalar inputs).
- **Artist-authored sentiment / data-
  visualisation overlays.** Out of
  scope for Phase 1 but enabled by the
  layer's design: future arcs could
  consume scalar fields from
  externally-authored data sources
  (CSV / NetCDF / HDF5) for scientific-
  visualisation use cases that benefit
  from the renderer's relativistic +
  manifold-aware perception.

The Phase 1 implementation:

- Consumes the EXISTING `src/field/`
  skeleton's data model
  (`ConstantScalarField`,
  `SampledScalarField`, `FieldMapping`,
  `FieldInterpreter`) without
  modification.
- Adds CUDA + OptiX kernel bridges that
  read a per-launch `FieldInterpreter`
  payload (one or more interpretation
  modules per launch).
- Adds a diagnostic AOV that
  visualises a scalar field's per-pixel
  sample.
- Adds CLI / config / fixture surfaces
  parallel to the OBSERVER.* arc's
  proven pattern.
- Is **OPTIONAL** at every layer: every
  default invocation produces zero
  Phase 1 contribution; the operator
  engages Phase 1 via explicit CLI
  flags + scene-file blocks.

---

## 2. Phase 1 scope

The FIELD-I.* arc scope is deliberately
narrow per the operator's brief:

### 2.1 Scalar fields only

The arc implements ONLY the
`FieldType::Scalar` slot. The other four
reserved slots (`Vector`, `Tensor`,
`Curvature`, `ProbabilityAmplitudePlaceholder`)
remain reserved-but-inert per the
existing `FieldType.h:18-23` enum
comment. Selecting a non-scalar field
type produces a "no-output-this-slice"
warning + zero contribution (master rule
#3 satisfied).

Future arcs (FIELD-I-V.* / FIELD-I-T.*
/ etc.) lift this scope when authorised.

### 2.2 Three concrete scalar-field kinds

Phase 1 ships three concrete scalar-field
backends:

- **Constant.** The existing
  `ConstantScalarField` POD
  (`ScalarField.h:87-96`) already
  provides this; FIELD-I.* exposes it
  to scene authors + the kernel.
- **Radial (procedural).** A new
  `RadialScalarField` POD carrying a
  `Vec3 origin` + `float min_radius`
  + `float max_radius` + `float
  min_value` + `float max_value` +
  optional `falloff` exponent.
  Evaluates `value(r) = lerp(min_value,
  max_value, smoothstep(min_radius,
  max_radius, |position - origin|))`
  on the spatial position. Mirrors the
  manifold `SchwarzschildLikeWarp`'s
  radial-symmetric authoring (a single
  origin + radial falloff with a
  smoothstep envelope) but evaluates a
  scalar field rather than a coordinate
  warp.
- **Procedural (function-pointer-based).**
  A new `ProceduralScalarField` POD
  carrying an `RR_HD` function pointer
  (or a small fixed-size enumerator
  dispatching to a built-in procedural
  bank — TBD at FIELD-I.2). Phase 1
  ships at least one built-in
  procedural: a 3D sinusoidal lattice
  (`f(x, y, z) = a * sin(b*x) *
  sin(b*y) * sin(b*z)`) for diagnostic
  visualisation + future authoring
  expansion.

The `SampledScalarField` POD
(`ScalarField.h:103-118`) is preserved
but **not implemented this Phase 1
arc** — the texture / grid backend
remains deferred per the existing
`ScalarField.h:36-44` doc. Future
FIELD-I-S.* arc lifts the sampled
backend when an externally-authored
texture-grid pipeline is needed.

### 2.3 Map to color / emission / AOV

Phase 1 implements three of the six
`FieldOutputChannel` enumerators
(`FieldMapping.h:70-77`):

- **Color (§4.1)** — multiplicatively
  modulates the beauty pass's per-pixel
  color by a scalar derived from the
  field sample. Default neutral
  multiplier `1.0`.
- **Emission (§4.2)** — additively
  contributes to the beauty pass's
  per-pixel emission. Default `0`.
- **DiagnosticAOV (§4.6)** — writes
  the field sample directly to a
  dedicated AOV pass. Default not
  allocated.

The other three channels (`Distortion`,
`Density`, `ChromaticShift`) remain
reserved per the existing
`FieldMapping.h` comment but are NOT
implemented this Phase 1 arc. Future
arcs may extend Phase 1 with these
channels.

### 2.4 No quantum simulation

The arc does **not** introduce any
quantum-state evolver (Klein-Gordon /
Schrödinger / Dirac). This is the
existing
`docs/FIELD_INTERPRETATION_LAYER.md` §7
non-goal carried forward verbatim; the
`ProbabilityAmplitudePlaceholder` enum
slot remains reserved-but-inert. Phase 1
is a visualisation / transcoding layer;
field DYNAMICS are out of scope at every
slice in the FIELD-I.* arc.

### 2.5 No tensor solver

The arc does **not** introduce any
tensor-field math (Einstein field
equations, Ricci tensor evolution,
parallel transport of arbitrary
tensors). This is the existing §7
non-goal carried forward. The
`Tensor` / `Curvature` enum slots
remain reserved-but-inert.

### 2.6 No new perception model

The arc does **not** introduce a fourth
`PerceptionMode` enumerator beyond
`Identity` / `ConstantVelocityMinkowski`
/ `CurvedChartGeodesicPlaceholder`. The
field-sample's per-pixel evaluation may
read the observer frame (per §4 below)
but does not alter the perception-mode
contract; the OBSERVER.* + OBS-P.* arc
family's verdicts carry forward
unchanged.

---

## 3. Relationship to manifold core

The Phase 1 field-evaluation pipeline
interacts with the Manifold Core at two
optional sampling points per the existing
`docs/FIELD_INTERPRETATION_LAYER.md` §5:

### 3.1 Sampling coordinate space

A scalar field is evaluated at a **chart-
event** — a position in the active chart's
coordinate space. The chart event may be:

- **The world-space hit position** (the
  default; chart is `Euclidean` per the
  manifold-default fallback).
- **The chart-space hit position** (the
  manifold's `world_to_chart(...)` of
  the world-space hit; engaged when the
  operator has a non-Euclidean chart
  active OR when the field interpreter
  explicitly opts into chart-space
  sampling).
- **The observer-frame-relative
  position** (the position minus the
  observer's `position4` spatial part;
  engaged when the field interpreter
  opts into observer-relative
  sampling — see §4 below).

The Phase 1 sampling space is selected
per-interpreter via a new
`FieldSamplingSpace` enum on the
`FieldInterpreter` POD (added at
FIELD-I.3). Three values:

- `WorldSpace` (default; preserves the
  existing skeleton's behaviour
  verbatim).
- `ChartSpace` (engages the manifold's
  `world_to_chart(...)` per the active
  `ManifoldTransform`).
- `ObserverRelative` (subtracts the
  observer's `position4` spatial part
  before sampling).

The default `WorldSpace` makes a
default `FieldInterpreter{}` produce
zero behaviour change (the field reads
the world-space hit position, then
`evaluate(field, position)` returns the
default-zero scalar value, then the
multi-target strengths multiply to
zero, then the beauty / emission / AOV
write paths short-circuit).

### 3.2 Manifold-transform composition

The chart-space sampling path (§3.1) is
the **load-bearing manifold integration
point**. When the operator has both:

- A non-Euclidean chart active (e.g.
  SchwarzschildLike with non-zero
  `manifold.strength`), AND
- A field interpreter with
  `sampling_space == ChartSpace`,

the field is evaluated at the
chart-warped position, NOT the
world-space position. A radial scalar
field centred at `(0, 0, 0)` will
therefore show **different per-pixel
output** depending on whether the
SchwarzschildLike chart is engaged:

- Without the chart: radial isolines
  appear as concentric spheres in
  world space.
- With the chart: the same isolines
  appear at chart-warped radial
  distances — closer to the centre
  in world space (because the
  SchwarzschildLike chart inflates
  the near-mass region).

This is the documented
"non-light fields can use the
manifold's coordinate deformation"
contract from the architecture doc §6.
Phase 1 implements it via the
per-interpreter `sampling_space`
selector; future arcs may add
per-field-instance overrides.

### 3.3 No new manifold math

The arc does **not** introduce new
chart families, new metric helpers, or
new geodesic-integrator surfaces. The
existing MANI-I.* surface
(SchwarzschildLike + PenroseLike +
the EuclideanChart default + the
reserved Kerr / Kruskal slots)
provides everything Phase 1 needs.
The chart-space sampling path at
§3.2 invokes the existing
`world_to_chart(...)` helper from
`ManifoldTransform.h`; no new math
leaf required.

---

## 4. Relationship to observer frame

The Phase 1 field-evaluation pipeline
interacts with the Observer Frame at two
optional sampling-mode points per the
architecture doc §6:

### 4.1 Observer-relative sampling

The `FieldSamplingSpace::ObserverRelative`
value from §3.1 subtracts the observer's
`position4` spatial part from the hit
position before passing it to
`evaluate(field, ...)`. This makes a
**field centred on the observer** —
useful for visualisations where the
field's reference frame should travel
with the observer rather than the
world origin.

When the OBSERVER.* + OBS-P.* arc
family's `perception_mode == Identity`
(the default), the observer is at the
chart origin and the
`ObserverRelative` mode produces the
same output as `WorldSpace` (the
subtraction is by zero). On
`ConstantVelocityMinkowski` the
observer's `position4` is wherever
the dispatcher placed the camera +
observer-frame combination; the
field's per-pixel sample reads
relative to that anchor.

### 4.2 Observer-mediated chromatic shift (FUTURE)

The architecture doc §6 mentions an
optional observer-mediated chromatic
shift: a field interpreter could
modulate its output through the
observer's beta velocity (e.g. a
field's emission gets Doppler-shifted
along with the rest of the beauty
pass). The Phase 1 arc **does NOT
ship this** per the §2.6 "no new
perception model" non-goal; the
field's per-pixel contribution is
written BEFORE the SR pipeline's
Doppler / aberration helpers fire on
the bounce-loop primary ray
direction. Future FIELD-I-O.* arc
may add observer-mediated
chromatic-shift if the operator
authorises.

### 4.3 Default observer = neutral mapping

The OBSERVER.* foundation arc's
`Identity` perception mode + the
default `ObserverFrame{}` produce a
**neutral mapping** for every Phase 1
output: the observer-relative sampling
path collapses to world-space sampling
(because the observer is at the world
origin), and the SR Doppler /
aberration helpers (which the field
contribution flows through on the
beauty pass) all collapse to identity
because the legacy `Observer.velocity`
is zero by default.

When the operator engages
`--observer-perception-mode
relativistic` with a non-zero beta,
the Phase 1 field contribution still
flows through the standard beauty
pass, so it inherits the existing
SR effects on the photon's
frequency / direction. The field's
per-pixel VALUE doesn't change with
the observer; the renderer's
PRESENTATION of that value (color
shift + aberration of the carrying
photon) is the existing OBS-P.2
pipeline's job.

---

## 5. Proposed slices

Seven sub-slices in the FIELD-I.*
sub-slice ladder. Per-slice gate audits
may be inserted between impl slices as
operator cadence permits (mirroring the
OBSERVER.* / OBS-P.* / OBS-F.* arc
pattern where audit slots were inserted
in-band at OBSERVER.3 / .5 / .7 / .9 /
.11 / .14 + OBS-P.3 + OBS-F.3). The
ladder below numbers the
**implementation** slices only; audit
slots are added in-band as the
operator requests.

### FIELD-I.2 — Scalar field model audit/update (impl, POD-leaf + tests)

- **Scope:** verify the existing
  `src/field/ScalarField.h` POD set
  (`ConstantScalarField`,
  `SampledScalarField`) is sufficient
  for Phase 1, then add two new POD
  types:
  - `RadialScalarField` (per §2.2):
    `origin` + `min_radius` +
    `max_radius` + `min_value` +
    `max_value` + `falloff`. Default
    constructed = zero-value field at
    origin with `[0, 1]` radius +
    `[0, 0]` value range = no-op.
  - `ProceduralScalarField` (per §2.2):
    enumerator-dispatched (e.g.
    `ProceduralScalarFieldKind::Sinusoidal3D`)
    + per-kind parameter struct. The
    enum + dispatch approach is simpler
    than a function pointer for Phase 1
    (function pointers complicate
    GPU upload + cross-backend
    consistency).
  - Add `evaluate(field, Vec3)` /
    `evaluate(field, Vec4)` overloads
    for both new types.
  - Add a new
    `FieldSamplingSpace` enum (per
    §3.1) and extend the
    `FieldInterpreter` POD with a
    `sampling_space` field. Default
    `WorldSpace` preserves byte-
    identity.
- **Acceptance:**
  - Audit-host build green; ctest
    12/12 PASS.
  - A new `tests/field_tests.cpp`
    binary added with ~30 RR_CHECK
    assertions covering: default-no-op
    on every new POD; radial-field
    smoothstep correctness at known
    sample points; sinusoidal
    procedural correctness at known
    sample points; sampling-space
    enum dispatch correctness.
  - No renderer code path consumes
    the new types yet (kernel reads
    happen at FIELD-I.5 / FIELD-I.6).
- **What does NOT ship:** kernel-
  side consumption; CLI surface
  (deferred to FIELD-I.3); GPU
  payload (deferred to FIELD-I.5 /
  FIELD-I.6); chart-aware sampling
  (uses the new `sampling_space`
  enum but kernels don't yet
  invoke `world_to_chart` —
  deferred until FIELD-I.5).

### FIELD-I.3 — Field mapping config (impl, host-only)

- **Scope:** add a
  `rr::field::FieldInterpreter
  field_interpreter` field (or a
  vector of them — TBD) to
  `rr::core::Config` next to the
  existing `manifold` / `observer`
  fields. Add CLI flags parsed by
  `src/core/CommandLine.cpp`:
  - `--field-enable` (presence-only;
    flips `enabled = true`).
  - `--field-name <string>` (sets
    the interpreter's `name`; used in
    log output + AOV labelling).
  - `--field-kind <name>` (selects
    the field kind: `constant` /
    `radial` / `sinusoidal3d`).
  - `--field-value <float>` (the
    constant value, or per-kind
    parameter dispatch table).
  - `--field-origin <x,y,z>` (radial
    origin).
  - `--field-min-radius` /
    `--field-max-radius` /
    `--field-min-value` /
    `--field-max-value` /
    `--field-falloff` (radial
    parameters).
  - `--field-color-strength` /
    `--field-emission-strength` /
    `--field-aov-strength`
    (per-channel mapping strengths
    matching the FIELD.3 POD's
    five-target slots; only 3 are
    exposed at Phase 1 per §2.3).
  - `--field-strength <float>`
    (overall module strength override).
  - `--field-sampling-space
    <world|chart|observer>` (the
    new sampling-space enum from
    FIELD-I.2).
  - Plus a scene-loader extension at
    `apply_field_interpreter` (parallel
    to `apply_manifold` from SCHW.9)
    that parses an optional
    `fieldInterpreter` block on the
    `.rrscene` schema.
- **Acceptance:**
  - Audit-host build green; ctest
    12/12 PASS.
  - `cli_tests` grows by ~25 new
    assertions covering the new
    flags' parser surface + default-
    off byte-identity + composability
    with the existing
    `--observer-*` / `--manifold-*`
    flag families.
  - `--scene-info <fixture>`
    displays the parsed
    `fieldInterpreter` block (or
    `disabled` by default).
- **What does NOT ship:** kernel
  consumption (deferred to
  FIELD-I.5 / FIELD-I.6); the AOV
  enum (deferred to FIELD-I.4).

### FIELD-I.4 — Scalar diagnostic AOV (impl, AOV data model)

- **Scope:** add a new
  `AOVType::FieldScalarDiagnostic`
  enumerator at `src/renderer/AOV.h`
  (value `= 8`, appended after
  `ObserverBeta = 7`). Add the
  `make_field_scalar_diagnostic(...)`
  factory + `aov_component_count`
  returning `1` (single-float per-
  pixel) + `aov_type_name` returning
  `"field_scalar_diagnostic"`. Add
  the corresponding
  `DeviceAOVView::field_scalar_diagnostic`
  slot + `AOVTargets::field_scalar_diagnostic`
  field + `OptixLaunchParams::aov_field_scalar_diagnostic`
  field (mirroring the OBSERVER.13
  `observer_beta` AOV plumbing
  shape verbatim).
- **Acceptance:**
  - Audit-host build green; ctest
    12/12 PASS.
  - `renderer_tests` grows by ~4
    new RR_CHECK assertions
    (enumerator value + name +
    component count + factory
    output; mirrors
    `test_observer_13_observer_beta_aov_type`).
- **What does NOT ship:** kernel-
  side write (deferred to FIELD-I.5
  / FIELD-I.6). The AOV slot exists;
  the kernel arm + dispatcher gate
  arrive with the bridges.

### FIELD-I.5 — CUDA bridge (impl, CUDA-side)

- **Scope:** wire the
  `FieldInterpreter` payload into
  the CUDA kernel.
  - Add
    `rr::field::FieldInterpreter
    field_interpreter{}` field to
    `CudaSceneView` (sibling of
    `manifold_mode` +
    `observer_frame`).
  - Add matching
    `AOVTargets::field_interpreter{}`
    field on `CudaRenderer.h`.
  - `CudaRenderer::render_scene_with_aovs`
    threads the field into the view.
  - `main.cpp::run_render_aovs`
    populates
    `targets.field_interpreter`
    from `cfg.field_interpreter`.
  - Kernel-side: add a per-pixel
    field-evaluation step gated on
    `is_enabled(scene.field_interpreter)`
    (a new helper analogous to
    `is_active(manifold_mode)`):
    sample the configured field at
    the per-pixel chart event;
    multiply by per-channel
    strengths; contribute to color /
    emission / AOV per the mapping.
    Default-off path produces
    byte-identical output to the
    pre-FIELD-I.5 baseline.
  - The kernel arm respects the
    new `sampling_space` selector
    (FIELD-I.2): on `ChartSpace` it
    invokes
    `world_to_chart(scene.manifold_mode,
    scene.coordinate_chart,
    world_hit_pos)` (single-
    source-of-truth math leaf
    reused from SCHW.5 /
    PENROSE.6); on
    `ObserverRelative` it
    subtracts
    `scene.observer_frame.position4.{x,y,z}`
    from the hit position.
- **Acceptance:**
  - Audit-host build green; ctest
    12/12 PASS.
  - Default-off byte-identity
    preserved (no flag passed →
    field contribution short-
    circuits → existing AOV PPMs
    byte-unchanged).
  - Cross-backend AOV byte-
    equivalence structurally
    guaranteed by single-source-
    of-truth math (the kernel-
    side field evaluation uses
    the same `RR_HD inline`
    `evaluate(...)` helpers from
    `ScalarField.h`).
- **What does NOT ship:** OptiX
  wiring (deferred to FIELD-I.6);
  the path-tracer's bounce-loop
  field contribution remains on
  the legacy unconfigured path
  (the path-tracer migration is
  a separate future arc).

### FIELD-I.6 — OptiX bridge (impl, OptiX-side)

- **Scope:** mirror FIELD-I.5 on
  the OptiX side.
  - Add
    `rr::field::FieldInterpreter
    field_interpreter{}` field to
    `OptixLaunchParams`.
  - `OptixRenderer::render_aovs`
    gains a `field_interpreter`
    trailing defaulted parameter
    (mirroring SCHW.7 / OBSERVER.10
    / OBSERVER.13 pattern).
  - `main.cpp::run_render_optix_aovs`
    passes
    `cfg.field_interpreter` as
    the new trailing argument.
  - Kernel-side: same as FIELD-I.5
    — the existing closest-hit /
    miss arms in
    `OptixPrograms.cu` get a new
    field-evaluation step gated
    on `is_enabled(...)`.
- **Acceptance:**
  - Audit-host build green; ctest
    12/12 PASS.
  - Default-off byte-identity
    preserved on both backends.
  - CUDA ↔ OptiX byte-equivalence
    for the same field-interpreter
    input (structurally
    guaranteed; pixel-level
    verification deferred to SDK
    host).

### FIELD-I.7 — Fixture scene (impl, scene + companion doc)

- **Scope:** add
  `scenes/test_field_interpreter.rrscene`
  + `docs/FIELD_INTERPRETER_FIXTURE.md`
  (mirroring the OBS-F.2 +
  OBSERVER_FRAME_FIXTURE.md
  precedent shape). Fixture
  authors:
  - A simple scene (5-6 spheres +
    ground plane + 2 lights,
    matching SCHW.9 / PENROSE.10 /
    OBS-F.2 conventions).
  - A `fieldInterpreter` scene
    block authoring:
    - `kind = "radial"` field
      centred at the origin with
      visible smoothstep falloff
      (min_radius = 1.0,
      max_radius = 5.0, value
      range `[0.0, 1.0]`).
    - `mapping.colorMultiplier =
      0.5`, `mapping.diagnosticAOV
      = 1.0`, others zero.
    - `strength = 1.0`,
      `enabled = true`,
      `samplingSpace = "world"`.
  - Optional secondary fixture
    variant with `samplingSpace
    = "chart"` + a non-Euclidean
    chart engaged via the
    existing `manifold` block —
    demonstrates the chart-space
    sampling path's deformed
    isolines.
  - Companion doc with the
    standard 7-section structure
    (Purpose / Composition /
    Expected visual signature
    per invocation variant /
    Cross-backend equivalence /
    Audit-host smoke transcript /
    Runtime SDK-host validation
    checks / References).
- **Acceptance:**
  - Audit-host build green;
    ctest 12/12 PASS.
  - `--scene-info` loads the
    fixture cleanly.
  - Default-off byte-identity
    of existing scenes preserved.

### FIELD-I.8 — Arc capstone audit (docs only)

- **Scope:** per-arc capstone
  verdict mirroring the SCHW.11
  + PENROSE.12 + OBSERVER.15
  capstone shapes. Synthesises
  the prior per-slice audits
  (FIELD-I.2 → FIELD-I.7) into a
  single arc-level verdict.
- **Verdict options:** PASS /
  PASS_WITH_RUNTIME_DEFERRED /
  REPAIR / BLOCKED.
- **Acceptance:** audit-host
  build green; ctest 12/12;
  verdict authorises proceeding
  to one of: the deferred
  SDK-host runtime pass (which
  exercises every FIELD-I.* +
  OBSERVER.*-family +
  OBS-P.* + OBS-F.* fixture in
  one cross-arc audit); the
  Vector / Tensor / Curvature
  field-type promotions
  (FIELD-I-V.* / FIELD-I-T.*
  arcs); manifold-orthogonal
  work (MANI-I.12, path-tracer
  feature breadth, denoiser
  integration).

---

## 6. Non-goals

Per the operator's brief + the existing
`docs/FIELD_INTERPRETATION_LAYER.md` §7
non-goals, the FIELD-I.* arc does NOT:

- **Solve field dynamics.** No Klein-
  Gordon / Schrödinger / Dirac evolver.
  Phase 1 consumes static field data;
  field DYNAMICS are reserved for a
  separate future arc that has not yet
  been authorised.
- **Implement tensor / curvature
  fields.** The `Tensor` / `Curvature`
  enum slots remain reserved-but-inert.
- **Implement the SampledScalarField
  backend.** The texture / grid
  backend is deferred to FIELD-I-S.*
  (a future arc); Phase 1 ships
  Constant + Radial + Procedural
  scalar fields only.
- **Implement chromatic shift /
  distortion / density channels.**
  Phase 1 implements 3 of the 6
  `FieldOutputChannel` enumerators
  (Color + Emission + DiagnosticAOV);
  the other 3 are reserved for future
  arcs.
- **Modify the Manifold Core.** No new
  chart family; no new metric helper;
  no new geodesic-integrator surface.
  Phase 1 consumes the existing
  MANI-I.* surface verbatim.
- **Modify the OBSERVER.* arc family.**
  No new `PerceptionMode` enumerator;
  no observer-mediated chromatic
  shift; no new ObserverFrame field;
  no kernel-side OBSERVER.* migration.
  The OBSERVER.15 + OBS-P.3 + OBS-F.3
  verdicts carry forward unchanged.
- **Modify the path tracer's bounce
  loop.** The path-tracer's per-
  bounce shading is preserved
  verbatim; Phase 1's field
  contribution flows through the
  primary-ray beauty / AOV write
  arms only (matching the
  OBSERVER.13 `observer_beta` AOV
  scope decision).
- **Modify the denoiser.** Stage
  19B.4 / 21D OptiX denoiser
  continues to consume Beauty /
  Albedo / Normal only.
- **Modify the camera ABI.** The
  existing `rr::camera::Camera`
  class is unchanged.
- **Touch C4D / server / UI /
  node-editor.**
- **Add a new `RelativityParams`
  flag.**
- **Add per-pixel field state.**
  The field is per-launch
  configured (one `FieldInterpreter`
  per launch); per-pixel field
  variation comes from the
  field's own evaluator
  (radial / procedural), not from
  per-pixel ABI extension.
- **Implement Phase 2** (the
  multi-module field interpreter
  composition + per-region gating
  the design doc §6.1 sketches).
  Phase 1 implements ONE field
  interpreter per launch; Phase 2
  is a separate arc.

---

## 7. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule
  #3 ("no fake stubs") + #12
  ("Do not overbuild a later
  system before the current
  layer works") both load-bearing
  for this arc's narrow-scope
  discipline.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §6 — defines the Field
  Interpretation Layer as
  Phase 1 of the architecture
  pivot; the FIELD-I.* arc is
  the implementation of §6's
  optional perception-
  transcoding contract.
- `docs/FIELD_INTERPRETATION_LAYER.md`
  — the existing 737-line design
  doc; §3 (field types) + §4
  (mapping outputs) + §5
  (manifold-core relationship)
  + §6 (composition semantics)
  + §7 (non-goals) + §9
  (milestone order). The
  FIELD-I.* arc consumes this
  doc as its canonical design
  reference.
- `src/field/README.md` — the
  existing FIELD.* skeleton's
  status document; documents
  the FIELD.1 / .2 / .3
  promotions that already
  landed.
- `src/field/FieldType.h` —
  the five-slot field-type
  enum; FIELD-I.* arc
  implements ONLY the
  `Scalar` slot.
- `src/field/ScalarField.h` —
  the existing
  `ConstantScalarField` +
  `SampledScalarField` PODs;
  FIELD-I.2 adds
  `RadialScalarField` +
  `ProceduralScalarField`.
- `src/field/FieldMapping.h` —
  the existing 6-channel
  `FieldOutputChannel` enum +
  5-target `FieldMapping`
  POD; FIELD-I.* implements
  only 3 of the 6 channels
  this Phase 1 arc.
- `src/field/FieldInterpreter.h`
  — the existing per-render
  metadata POD; FIELD-I.2
  extends it with the new
  `sampling_space` field.
- `docs/OBSERVER_FRAME_RENDERING_PLAN.md`
  (OBSERVER.1) — the
  precedent arc-planning doc
  this FIELD-I.1 plan
  mirrors in structure (5
  sections covering scope /
  relationships /
  sub-slices).
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15) §9 — the
  capstone identifying
  Phase 1 of the Field
  Interpretation Layer as
  one of three "manifold-
  orthogonal work"
  candidates.
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3) — established
  the OBS-P.2 ternary's
  cross-source equivalence
  property the FIELD-I.5 /
  FIELD-I.6 bridges
  consume.
- `docs/OBSERVER_DEBUG_AOV_TASK.md`
  (OBSERVER.12) +
  `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14) — precedent
  AOV-implementation pattern
  the FIELD-I.4 +
  FIELD-I.5 + FIELD-I.6
  AOV plumbing mirrors.
- `docs/OBSERVER_FRAME_FIXTURE_TASK.md`
  (OBS-F.1) +
  `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md`
  (OBS-F.3) — precedent
  fixture-arc pattern the
  FIELD-I.7 fixture
  mirrors.
- `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md`
  (SCHW.1 plan) — the
  precedent chart-arc
  planning doc; the FIELD-I.*
  arc's "implementation
  ladder + per-slice audit
  discipline" pattern
  inherits from the SCHW.* /
  PENROSE.* + OBSERVER.* +
  OBS-P.* + OBS-F.* arc
  precedents.
- `docs/BUILD_PLAN.md` —
  every prior arc's per-
  slice entries; the
  FIELD-I.* arc's BUILD_PLAN
  entries land alongside
  each impl + audit slice.
- `src/manifold/ManifoldTransform.h`
  + `src/manifold/SchwarzschildLikeWarp.h`
  + `src/manifold/PenroseLikeCompactification.h`
  — the existing chart
  surface FIELD-I.5's
  ChartSpace sampling-space
  invokes via
  `world_to_chart(...)`.
- `src/manifold/ObserverFrame.h`
  — the existing observer-
  frame POD FIELD-I.5's
  ObserverRelative
  sampling-space reads from.
- `src/renderer/AOV.h` +
  `src/renderer/AOV.cpp`
  — the existing AOV data
  model FIELD-I.4 extends
  with `FieldScalarDiagnostic`.
- `src/cuda/CudaScene.cuh`
  + `src/cuda/CudaRenderer.h/.cu`
  + `src/cuda/CudaTestKernel.cu`
  + `src/cuda/CudaAOV.cuh`
  — the existing CUDA
  surface FIELD-I.5
  extends.
- `src/optix/OptixLaunchParams.h`
  + `src/optix/OptixRenderer.h/.cpp`
  + `src/optix/OptixPrograms.cu`
  — the existing OptiX
  surface FIELD-I.6
  extends.
- `src/core/Config.h` +
  `src/core/CommandLine.cpp`
  — the existing CLI / config
  surface FIELD-I.3
  extends.

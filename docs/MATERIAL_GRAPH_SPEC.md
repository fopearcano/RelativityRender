# Material Node Graph — Specification

Status: **specification only**, introduction slice. No graph
code exists yet. This document is the contract that future
implementation slices will deliver against; the rest of the
spec (node types, sockets, evaluation model, GPU compilation
strategy, scene-format integration) lands as separate doc
slices before any code is written.

Module reference: `tools/node_editor/` and the
graph-evaluation glue in `src/material/` (modules 9 + 22 in
`docs/MODULE_MAP.md`, milestone M21 in
`docs/MILESTONE_ROADMAP.md`).

## 1. Purpose

RelativityRender is a path tracer with a relativistic camera.
Authoring real materials in that renderer means describing,
per surface, how light interacts with it: which fraction is
diffusely reflected, which fraction is emitted, how the
albedo varies with surface UVs, how a per-pixel mask drives
roughness, and so on.

Today the renderer's authoring surface is a flat struct,
`rr::material::MaterialParams` (`src/material/MaterialTypes.h`),
that pins one constant value per material parameter. The
struct is enough to ship the renderer's first usable
materials but is not enough to describe production
materials, where the same parameter is typically driven by
some combination of constants, textures, math operations,
and (eventually) procedural noise.

The **material node graph** is the data model the project
uses to describe those compositions. A graph is a directed
acyclic collection of small evaluation nodes, each producing
a typed value that other nodes (or the renderer) consume.
At the boundary the renderer still sees a per-hit-point set
of shading parameters - the graph is the *recipe* for those
parameters, not a replacement for them.

This document explains:

- What the graph is, in the abstract.
- Why RelativityRender needs one above and beyond the flat
  struct.
- How the graph differs from `MaterialParams`.
- The constraints any graph implementation must respect:
  GPU-first execution, real-time-preview compatibility, and
  compatibility with path tracing.

It deliberately stops there. Node types, socket types,
connection rules, the evaluation model, and the GPU
compilation strategy each get their own document slice with
the same level of detail. No implementation is committed to
in this document.

## 2. What is a material node graph?

A material node graph is an ordered collection of
**nodes** wired together by **connections**. Each node is a
small unit of computation (a constant, a texture lookup, a
math operation, a BSDF) that consumes some inputs and
produces some outputs. Connections route a node's output
into another node's input.

A graph has at least one **terminal node** the renderer
recognises - typically a BSDF or emission node - whose
inputs determine how the surface shades. The renderer
evaluates the graph per shading sample to recover the
shading parameters it needs (albedo, emission, roughness,
...) and then runs its standard path-tracing logic with
those parameters.

The graph itself is a **declarative description** of how to
compute those parameters. It is not a script: there is no
control flow, no side effects, no allocations during
evaluation, and no reference to renderer internals. The
graph form lets authoring tools (the future standalone node
editor, the Cinema 4D bridge) construct the same description
without depending on the renderer's implementation.

## 3. Why RelativityRender needs one

The current flat-struct material is a temporary contract.
It has carried the renderer through M11 (foundations), M16
(textures), and M19 (Cinema 4D bridge), but it shows three
limits as soon as scenes get non-trivial:

- **Per-parameter compositions are impossible.** With a
  flat struct, `baseColor` is either a constant or sampled
  from a single texture (M16's `base_color_texture_id`).
  There is no way to express "tint a texture by a constant",
  "mix two textures by a mask", or "drive roughness from
  the green channel of a third texture" without inventing a
  one-off field per case. Each new artistic axis grows the
  struct.
- **The authoring surface cannot evolve independently of
  the renderer.** When the standalone node editor (M21) and
  the Cinema 4D bridge (M19) want to add a new authoring
  primitive, they have to wait for the renderer to grow a
  new struct field. With a graph the authoring tools add
  new node types whose evaluation reduces to existing
  primitives; the renderer's shading parameters do not
  need to change.
- **The path tracer cannot exploit local detail.** Real
  materials vary across a surface (per-UV albedo, per-
  texel masks). The current material struct is one value
  per material; the renderer cannot legally call a
  per-hit-point albedo function. The graph is the place
  that function lives, evaluated once per hit.

A graph addresses all three: it lets a parameter be the
composition of constants, samples, and math, lets authoring
tools ship new compositions without touching the renderer's
shading parameter set, and gives the renderer a per-hit
evaluation hook that does not push complexity into
`MaterialParams` itself.

## 4. How the graph differs from `MaterialParams`

`MaterialParams` (`src/material/MaterialTypes.h`) is the
**flattened result** of a graph evaluation, not a competitor
to the graph. The two coexist at different layers:

| Concern                 | `MaterialParams` (today)              | Material graph (future)                 |
|-------------------------|---------------------------------------|-----------------------------------------|
| Shape                   | One struct per material               | A DAG of small nodes per material        |
| Storage                 | Flat fields (`baseColor`, `roughness`, `emissionColor`, ...) | Typed nodes + connections                |
| Variation across a hit  | Constant per material; one optional texture binding for `baseColor` | Any parameter can vary per hit via the graph |
| Authoring               | Direct field edit; one slot per parameter | Compose constants / textures / math / BSDFs |
| Renderer contract       | Kernel reads the struct directly      | Kernel reads the per-hit evaluation result |
| Where it lives in the scene file | `materials[].base_color`, etc. | Optional `materials[].graph` (later schema slice) |

The two contracts coexist by design:

- The graph **always** evaluates down to the same shading
  parameters `MaterialParams` already exposes. The renderer's
  shading code keeps consuming that parameter set; it does
  not need to know whether they came from a struct or a
  graph evaluation.
- A material with no graph is equivalent to a graph that
  consists of one terminal node fed by constants. That
  equivalence keeps every material the bridge has exported
  to date valid under the graph contract.
- A scene file that carries both a flat snapshot and a
  graph treats the snapshot as a **bake** of the graph
  (the value the graph would produce for a default shading
  context). Loaders that do not implement graph evaluation
  still get a usable material from the snapshot.

The graph is therefore an **extension** of the existing
material model, not a rewrite. Code paths that read
`MaterialParams` today keep working; new code paths that
want per-hit composition opt in by evaluating the graph.

## 5. Constraints

Any implementation of the graph - both the renderer's
evaluation path and the authoring tools that emit it -
MUST respect three hard constraints. They follow directly
from RelativityRender's identity as a CUDA / OptiX-first
GPU path tracer.

### 5.1 GPU-first execution

The renderer's per-pixel and per-bounce work runs on the
GPU; the CPU only orchestrates (`docs/DEVELOPMENT_RULES.md`,
"engineering rules"). The graph evaluation that runs at
shading time MUST run on the GPU.

Concretely:

- The graph's run-time form MUST be representable as
  device-resident data (constant memory, global memory,
  or kernel arguments). Anything reachable from a kernel
  must be addressable by a device pointer or a flat index.
- The graph's evaluation MUST NOT require host callbacks,
  dynamic allocation, or unbounded recursion at shading
  time. Each evaluation MUST complete in bounded work
  decided at compile / upload time.
- Graph authoring (in the standalone editor, in the
  Cinema 4D bridge) runs on the host. The split is the
  same one the rest of the renderer uses: hosts compose
  data, kernels consume it.

This rules out, in v1:

- JIT-compiled host-side shader fragments (no LLVM, no
  NVRTC at first - those are an optimisation later).
- Per-shading-sample interpreter calls into Python or
  similar.
- Unbounded loops in node operations.

### 5.2 Real-time compatibility

The renderer's preview UX (the C4D bridge dialog, the
future standalone preview UI) issues frequent re-renders
as the user scrubs sliders or edits a graph. Every render
re-uploads the affected material data and re-launches the
shading kernel.

The graph contract MUST therefore:

- Produce a **deterministic, cheap, host-side** "compile to
  device-friendly form" step. Editing a node SHOULD NOT
  require recompiling the renderer or relaunching the
  application.
- Bound the per-evaluation cost. A path tracer evaluates
  the graph at every surface hit on every bounce of every
  sample of every pixel; per-evaluation cost MUST stay in
  the same order of magnitude as the existing flat-struct
  shading does today.
- Support partial re-uploads. Editing one material's graph
  SHOULD NOT force a reupload of the full scene. The exact
  granularity will be settled in the GPU compilation slice;
  the contract is "no full-scene churn for one slider tick".

This rules out, in v1:

- Graph forms that require a full scene re-parse on edit.
- Graph forms whose evaluation cost grows worse than O(N)
  in node count for a single per-hit evaluation.
- Authoring representations that require a server round-trip
  for every node-graph mouse drag.

### 5.3 Path-tracing compatibility

The renderer is a path tracer (M14). The graph runs INSIDE
the integrator's hit-shading loop. That imposes its own
constraints:

- **Each evaluation is independent.** The graph MUST NOT
  rely on state shared across hits, samples, or threads.
  The integrator parallelises over rays; any cross-thread
  dependency would serialise that.
- **The graph evaluates with a SHADING CONTEXT.** Inputs
  the renderer can give the graph at evaluation time
  (surface UV, surface normal, view direction, the bound
  texture array, ...) form a fixed contract. The contract
  is open - new inputs can join in later schema versions -
  but a node implementation MUST consume only what the
  context exposes. No globals.
- **Outputs feed `MaterialParams`-shaped consumers.** The
  graph's terminal nodes describe contributions to the
  renderer's existing shading model (diffuse, emission,
  ...). A v1 graph MUST decompose into the same parameter
  set the M11 / M16 kernels already shade with.
- **Differentiability is NOT required in v1.** The path
  tracer (M14) does not yet do gradient-based work
  (denoising, neural materials). Graph nodes are NOT
  required to be analytically differentiable. A future
  slice can revisit when the denoiser (M22) lands.

This rules out, in v1:

- Light-network nodes (light shaders), volume networks,
  layered BSDFs - these introduce shading models the path
  tracer does not implement yet.
- Recursive node references (a node whose evaluation
  re-enters the integrator).
- Nodes that read from "the previous frame" or any
  history buffer.

## 6. What this slice covers

This slice introduces the graph at the conceptual level:
its purpose, its relationship to the existing material
struct, and the three constraints any implementation MUST
respect.

It deliberately does NOT pin:

- The set of node types (constants, samples, math, BSDFs,
  ...). That is the next doc slice.
- The socket type system or connection rules. Slice after
  next.
- The evaluation model (lazy / eager, caching strategy).
- The GPU compilation strategy (interpreted bytecode,
  emitted CUDA source, lookup tables, ...).
- The scene-file integration (the optional
  `materials[].graph` block).
- The standalone editor's UX.
- The Cinema 4D bridge's graph emission.

Each of those lands as its own doc slice. Implementation
work begins only after the slices that constrain it have
landed - the same incremental rule the rest of the project
follows.

## 7. Out of scope for v1 of the spec

The graph contract documented across this and the upcoming
slices is a v1 contract. The following are explicitly **not**
v1 concerns and are listed here to keep the scope honest:

- **Light-network graphs.** v1 covers SURFACE materials.
  Light shaders / IES profile networks / spectral lights
  arrive only after the lighting system grows past its
  M12 foundations.
- **Volume and SSS networks.** The renderer has no volume
  pipeline today; volume graphs follow that pipeline.
- **Layered BSDFs / BSDF mixing.** v1 terminates a graph at
  individual BSDF nodes (when those land in the next
  slice). Mixing two BSDFs by a mask is a later contract.
- **Procedural noise nodes** (Worley, Perlin, simplex,
  worley-cell-id). The first node-type slice does NOT
  include these; they slot in once the math + texture
  primitives are stable.
- **Time-varying inputs.** The graph evaluates in a single
  "now"; per-frame animation lives in the scene file's
  animation track (a future-version concern), not inside
  the graph.
- **Graph-driven AOVs.** AOVs (M17) are still a fixed set
  picked by the renderer. Custom-output AOVs through the
  graph are a future slice.
- **Differentiable graphs.** As stated in 5.3, v1 does
  not require analytic differentiation. The denoiser (M22)
  may revisit.
- **GPU compilation, scene-format integration, C4D bridge
  emission, and the standalone editor's UX.** Each of
  those is its own future slice and is intentionally
  excluded from THIS slice.

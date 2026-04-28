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

## 6. Node catalogue (v1)

The v1 catalogue is intentionally minimal. It covers the
four node categories the rest of the renderer needs to
express the materials it can already shade
(`MaterialParams`-shape parameters from the M11 / M14 / M16
work) plus the placeholder BSDF nodes that pin the contract
for the BSDFs the renderer will grow into. Every other
authoring primitive (procedural noise, vector math, custom
attribute reads, layered shaders, ...) is a future slice.

The catalogue is **open**. Future slices add nodes by
describing them in the same shape as the entries below;
they do NOT modify or remove existing entries. A graph
written against this catalogue MUST keep parsing and
evaluating identically when later slices land.

### 6.1 Naming conventions

The catalogue, the future scene-format schema, and the
authoring tools all share a single naming discipline so
authors and parsers cannot drift:

- **Node type names** are `PascalCase` and match the
  catalogue entry verbatim (`ConstantColor`,
  `TextureSample`, `Multiply`, `Diffuse`, ...).
- **Input names** on a node are `snake_case`
  (`albedo`, `color`, `strength`, `roughness`, `uv`,
  `factor`, ...).
- **Output names** are `snake_case`. A node with a single
  output names that output `value` unless the catalogue
  entry pins a more specific name.
- **Categories** documented here (`Input`, `Math`,
  `Utility`, `BSDF`) are the only ones recognised by v1.
  A new node introduced by a later slice MUST either
  belong to one of these categories or introduce its own
  category in the same slice.

Each entry below records:

- **Category** - one of Input / Math / Utility / BSDF.
- **Purpose** - what the node computes.
- **Inputs** - the named inputs the node consumes. Inputs
  marked "(node parameter)" are stored ON the node (like a
  constant value) and cannot be wired from another node;
  every other input MAY be either connected to another
  node's output or left unconnected to take its default.
- **Outputs** - the named outputs the node produces.
  Terminal nodes (BSDFs) have no outputs.
- **Status** - `core` (the renderer evaluates this node's
  contribution to `MaterialParams` today) or
  `placeholder` (the parser accepts the node and binds its
  parameters, but the renderer's BSDF code does not yet
  implement the corresponding shading; future renderer
  slices light it up).

The "kinds" referred to under inputs / outputs (scalar,
vector, colour, uv, ...) are deliberately informal at this
slice. The formal socket type system - what actually
connects to what, what conversions are implicit, and what
the cycle / arity rules are - is the next doc slice.

### 6.2 Input nodes

Input nodes have no incoming connections. They produce a
value at evaluation time, either from data stored on the
node itself or by sampling some part of the renderer's
shading context.

#### `ConstantFloat`

- **Category:** Input.
- **Purpose:** emit a fixed scalar constant.
- **Inputs:**
  - `value` (node parameter, scalar) - the constant.
- **Outputs:**
  - `value` (scalar).
- **Status:** core.

#### `ConstantColor`

- **Category:** Input.
- **Purpose:** emit a fixed RGB constant.
- **Inputs:**
  - `value` (node parameter, colour) - the RGB triple.
- **Outputs:**
  - `value` (colour).
- **Status:** core.

#### `TextureSample`

- **Category:** Input.
- **Purpose:** sample a texture bound to the scene at a
  given UV. Mirrors `MaterialParams::base_color_texture_id`'s
  binding model (M16): the texture is referenced by a
  scene-level integer id, not embedded inline.
- **Inputs:**
  - `texture_id` (node parameter, integer) - index into
    the scene's texture array.
  - `uv` (kind: 2D coordinate; default: surface UV from
    the shading context) - the lookup coordinate.
- **Outputs:**
  - `value` (colour) - the sampled texel.
- **Status:** placeholder. The v1 contract is that the node
  is recognised and its `texture_id` round-trips through the
  scene format; the renderer's existing nearest-neighbour
  sampler (`src/cuda/CudaTexture.cuh`) is the implementation
  it lowers to. Filtering / wrap modes / sRGB handling are
  future-slice extensions.

### 6.3 Math nodes

Math nodes consume one or more values of the same kind and
produce a value of that kind. They have no shading-context
dependencies; their behaviour is purely a function of their
inputs.

The output kind matches the inputs' kind: an `Add` of two
scalars produces a scalar; an `Add` of two colours produces
a colour. The exact rules for mixing kinds (e.g. scalar +
colour broadcasting) are pinned in the socket-type slice.

#### `Add`

- **Category:** Math.
- **Purpose:** per-component sum of two operands.
- **Inputs:**
  - `a` (kind: scalar / vector / colour; default: zero).
  - `b` (kind: matches `a`; default: zero).
- **Outputs:**
  - `value` (matches the inputs).
- **Status:** core.

#### `Multiply`

- **Category:** Math.
- **Purpose:** per-component product of two operands.
- **Inputs:**
  - `a` (kind: scalar / vector / colour; default: one).
  - `b` (kind: matches `a`; default: one).
- **Outputs:**
  - `value` (matches the inputs).
- **Status:** core.

#### `Mix`

- **Category:** Math.
- **Purpose:** linear blend of two operands by a scalar
  factor (`a * (1 - factor) + b * factor`).
- **Inputs:**
  - `a` (kind: scalar / vector / colour; default: zero).
  - `b` (kind: matches `a`; default: one).
  - `factor` (scalar in `[0, 1]`; default: `0.5`).
- **Outputs:**
  - `value` (matches `a` / `b`).
- **Status:** core.

### 6.4 Utility nodes

Utility nodes expose pieces of the renderer's per-hit
shading context (geometric attributes the kernel already
computes) or apply small geometric / coordinate transforms
to other nodes' outputs. They are the connective tissue
between input nodes and BSDFs.

#### `Normal`

- **Category:** Utility.
- **Purpose:** emit the surface normal at the current
  shading sample, in world space, matching the convention
  the renderer's `Hit::normal` already uses
  (`src/renderer/Hit.h`).
- **Inputs:** none.
- **Outputs:**
  - `value` (vector).
- **Status:** core.

#### `UV`

- **Category:** Utility.
- **Purpose:** emit the surface UV at the current shading
  sample, matching `Hit::uv` (M16: spherical UV on spheres,
  barycentric-interpolated UV on triangles).
- **Inputs:** none.
- **Outputs:**
  - `value` (2D coordinate).
- **Status:** core.

#### `UVTransform`

- **Category:** Utility.
- **Purpose:** apply a 2D affine transform to a UV input
  (scale + offset). Lets a graph reuse one texture across
  multiple surface regions without duplicating the
  texture binding.
- **Inputs:**
  - `uv` (2D coordinate; default: surface UV).
  - `scale` (2D coordinate; default: `(1, 1)`).
  - `offset` (2D coordinate; default: `(0, 0)`).
- **Outputs:**
  - `value` (2D coordinate).
- **Status:** core.

### 6.5 BSDF nodes

BSDF nodes are **terminal**: they have no outputs. A graph
contributes to surface shading by terminating at one or
more BSDF nodes; each BSDF node's inputs feed a piece of
the renderer's existing `MaterialParams` consumer set.

A v1 graph SHOULD include at most one node of each terminal
type. The exact rules for combining multiple terminals
(BSDF mixing, layered shaders) are explicitly out of scope
for v1 (see section 8).

#### `Diffuse`

- **Category:** BSDF.
- **Purpose:** contribute a Lambertian diffuse term. Maps
  to `MaterialParams::baseColor` in the renderer's existing
  shading model.
- **Inputs:**
  - `albedo` (colour; default: mid-grey
    `(0.8, 0.8, 0.8)` matching `MaterialParams::baseColor`'s
    default).
- **Outputs:** none (terminal).
- **Status:** core. The renderer's M11 / M14 shading
  evaluates Lambertian diffuse from this term.

#### `Emission`

- **Category:** BSDF.
- **Purpose:** contribute emissive radiance. Maps to
  `MaterialParams::emissionColor` and
  `MaterialParams::emissionStrength`.
- **Inputs:**
  - `color` (colour; default: black `(0, 0, 0)`).
  - `strength` (scalar; default: `1.0`).
- **Outputs:** none (terminal).
- **Status:** core. The renderer's M11 path picks up the
  emission contribution at hit time; the path tracer
  (M14) accumulates emissive radiance through bounces.

#### `Metallic`

- **Category:** BSDF.
- **Purpose:** contribute a metallic specular term. Maps
  to `MaterialParams::baseColor` (interpreted as F0 tint
  when `metallic` is `1`), `MaterialParams::metallic`,
  and `MaterialParams::roughness`.
- **Inputs:**
  - `albedo` (colour; default: mid-grey `(0.8, 0.8, 0.8)`).
  - `roughness` (scalar in `[0, 1]`; default: `0.5`).
- **Outputs:** none (terminal).
- **Status:** placeholder. `MaterialParams` already
  carries the `metallic` and `roughness` fields, but the
  v1 path tracer evaluates Lambertian only. Graphs that
  terminate at `Metallic` MUST parse and round-trip; the
  renderer's shading reduces them to a Lambertian
  approximation until the GGX BSDF lands in a future
  slice.

#### `Glass`

- **Category:** BSDF.
- **Purpose:** contribute a transmissive dielectric term
  (refractive transparent surface). Maps to
  `MaterialParams::transmission` plus the colour /
  roughness inputs.
- **Inputs:**
  - `tint` (colour; default: white `(1, 1, 1)`).
  - `ior` (scalar > 1; default: `1.5`, typical for
    common glass).
  - `roughness` (scalar in `[0, 1]`; default: `0.0`).
- **Outputs:** none (terminal).
- **Status:** placeholder. `MaterialParams::transmission`
  is reserved (`src/material/MaterialTypes.h` flags it
  PLACEHOLDER) and the path tracer does not implement
  refraction yet. Graphs that terminate at `Glass` MUST
  parse and round-trip; the renderer's shading reduces
  them to the diffuse fallback until the dielectric BSDF
  lands.

### 6.6 Catalogue summary

| Category | Node             | Status      |
|----------|------------------|-------------|
| Input    | `ConstantFloat`  | core        |
| Input    | `ConstantColor`  | core        |
| Input    | `TextureSample`  | placeholder |
| Math     | `Add`            | core        |
| Math     | `Multiply`       | core        |
| Math     | `Mix`            | core        |
| Utility  | `Normal`         | core        |
| Utility  | `UV`             | core        |
| Utility  | `UVTransform`    | core        |
| BSDF     | `Diffuse`        | core        |
| BSDF     | `Emission`       | core        |
| BSDF     | `Metallic`       | placeholder |
| BSDF     | `Glass`          | placeholder |

Twelve nodes - the smallest set that covers every
parameter `MaterialParams` exposes today plus the
placeholder BSDFs that pin the contract for tomorrow.

## 7. Sockets and graph structure

This section formalises the wiring between nodes: what a
socket is, what kinds of values flow through them, when a
connection is legal, and what shape the resulting graph
must have. It pins the structural contract; the
evaluation model (when each node fires, how outputs are
cached, what the host-side compile step does) is the next
doc slice.

### 7.1 Sockets

A **socket** is a named, typed connection point on a node.
Each socket sits on exactly one node and is identified by
its name within that node. Names are pinned by the node
catalogue (section 6) and follow the `snake_case`
convention from 6.1.

Sockets come in two kinds, distinguished by direction:

- An **input socket** receives a value the node consumes
  during evaluation. Inputs may be wired (connected to an
  output socket on another node) or unwired (the node
  uses the input's catalogue-defined default value, e.g.
  `Mix.factor` defaults to `0.5`). An input socket has at
  most ONE incoming connection.
- An **output socket** emits a value the node produces.
  Outputs may fan out: a single output MAY drive multiple
  input sockets on different nodes. An output's value is
  the same for every consumer in a single evaluation
  (the evaluation model slice pins the caching contract).

A few sockets in the catalogue are flagged "node parameter"
(e.g. `ConstantFloat.value`, `TextureSample.texture_id`).
These are NOT sockets in the wiring sense: they store a
literal value on the node itself and cannot be wired from
another node's output. They are listed in the catalogue
under "Inputs" only because they share the node-bound
input shape; future tooling treats them as fields, not
sockets.

### 7.2 Data types

Every socket carries one of the following data types. The
type list is intentionally narrow: each entry maps directly
to a primitive the rest of the renderer already uses, and
the connection rules below depend on the list staying
small enough to enumerate.

| Type     | Width | Storage notes                              |
|----------|-------|--------------------------------------------|
| `float`  | 1     | 32-bit IEEE float scalar.                  |
| `vec3`   | 3     | Three 32-bit floats, generic geometry.     |
| `color`  | 3     | Three 32-bit floats, linear RGB.           |
| `normal` | 3     | Three 32-bit floats, unit-length contract. |
| `vec2`   | 2     | Two 32-bit floats, generic 2D coordinate.  |

Notes on each type:

- **`float`** - scalar values: blend factors, roughness,
  emission strength, IOR, ...
- **`vec3`** - geometric vectors that do NOT carry a
  brightness or a unit-length contract. Useful for raw
  direction outputs that have not been normalised.
- **`color`** - three components in **linear RGB**. Values
  SHOULD be non-negative; the renderer's writers
  (`make_material_section`, `make_light_section`)
  non-negative-clamp the destination, and the host parser
  clamps to zero on load. HDR / above-1.0 values are
  permitted.
- **`normal`** - a `vec3` with the additional contract that
  the value is **unit-length** at evaluation time. Surface
  shading paths (Lambertian, future GGX) rely on this.
  The `Normal` utility node produces a value that already
  satisfies the contract; `vec3` -> `normal` requires an
  explicit normalisation step (see 7.3 - the conversion
  is NOT implicit). This type is documented as "optional"
  in the original prompt; v1 keeps it because the
  catalogue's `Normal` node already produces one and the
  shading path consumes one - making the type explicit
  pins that contract instead of pretending the renderer
  treats raw vectors and unit normals identically.
- **`vec2`** - the smallest extension to the prompt's
  four-type list. The catalogue's `UV` / `UVTransform` /
  `TextureSample.uv` sockets carry 2D coordinates;
  formalising `vec2` is the most honest way to type them.
  No `vec2` constant nodes ship in v1 (the
  `ConstantColor` / `ConstantFloat` pair is enough);
  `vec2` exists only to type UV-flavoured connections.

The list is closed for v1. New types (a hypothetical
`vec4`, an explicit `bsdf` handle, a procedural-mask type)
land only through their own doc slice with the matching
node-catalogue updates.

### 7.3 Connection rules

A connection wires a source output socket on one node to a
sink input socket on another node. v1 admits a connection
when:

1. **The source type matches the sink type, OR an implicit
   conversion is permitted between them.** The legal
   implicit conversions are pinned in the table below;
   anything not listed is a parse-time error.
2. **The sink is not already wired.** An input socket
   accepts at most ONE incoming connection.
3. **The connection does not introduce a cycle.** The
   resulting graph MUST stay a DAG (see 7.4).

The full implicit-conversion table:

| From    | To      | Behaviour                                       |
|---------|---------|-------------------------------------------------|
| `float` | `float` | Identity.                                       |
| `float` | `vec2`  | Broadcast: `x` -> `(x, x)`.                     |
| `float` | `vec3`  | Broadcast: `x` -> `(x, x, x)`.                  |
| `float` | `color` | Broadcast: `x` -> `(x, x, x)`.                  |
| `vec2`  | `vec2`  | Identity.                                       |
| `vec3`  | `vec3`  | Identity.                                       |
| `vec3`  | `color` | Reinterpret: same three floats, no rescale.     |
| `color` | `color` | Identity.                                       |
| `color` | `vec3`  | Reinterpret: same three floats, no rescale.     |
| `normal`| `normal`| Identity.                                       |
| `normal`| `vec3`  | Identity (a normal IS a vec3, drops the         |
|         |         | unit-length contract for downstream use).       |

Conversions NOT in the table (and therefore rejected at
parse time) include:

- `vec3` -> `normal` (no implicit normalisation; a future
  `Normalize` math node will perform it explicitly).
- `vec3` -> `vec2` or `vec2` -> `vec3` (no truncation /
  zero-padding of components; future slices may add
  explicit `Swizzle` / `Combine` nodes).
- `color` -> `float` (no implicit luminance reduction; a
  future `Luminance` math node will perform it
  explicitly).
- Any conversion involving a type not yet in the table.

The fan-out direction has no conversion: the same source
output can drive multiple sink inputs, and each sink
applies its own implicit conversion (or none) at the
point of use.

### 7.4 Graph topology

A material graph is a **directed acyclic graph** of nodes:

- Nodes are the entries listed in section 6's catalogue
  (or future-slice extensions to it).
- Edges run from output sockets to input sockets,
  obeying the connection rules in 7.3.
- The graph MUST be acyclic: there MUST NOT be a sequence
  of edges that returns to a node it has already
  visited. A graph that contains a cycle is rejected at
  parse time.

Two role labels follow from the topology and are useful
when reasoning about a graph:

- A **leaf** node has no incoming connections (every
  input is either a node parameter or unwired). The v1
  leaves are `ConstantFloat`, `ConstantColor`,
  `Normal`, `UV` (the utility shading-context emitters
  count as leaves; they pull from the renderer's
  shading context, not from another node).
- A **terminal** node has no outgoing connections (it
  exposes no output sockets). The v1 terminals are
  `Diffuse`, `Emission`, `Metallic`, `Glass` -
  exactly the BSDF category from 6.5.

Internal nodes (`TextureSample`, `Add`, `Multiply`,
`Mix`, `UVTransform`) have both inputs and outputs and
sit in the middle of the DAG.

A graph MAY contain isolated subgraphs - paths that do
not reach any terminal. Such subgraphs are **dead code**:
they parse, they pass the DAG check, but they contribute
nothing to shading. The parser MAY warn about them; the
evaluator MUST NOT spend work evaluating them. The exact
warning policy is the evaluation-model slice's call.

### 7.5 Root / terminal nodes

A graph contributes to surface shading **through its
terminal nodes**. v1's terminals are the four BSDF nodes
in 6.5; each one produces a piece of the renderer's
existing `MaterialParams`-shape consumer set:

| Terminal   | Produces                                                |
|------------|---------------------------------------------------------|
| `Diffuse`  | The Lambertian albedo term (`MaterialParams::baseColor`). |
| `Emission` | Emissive radiance (`emissionColor` + `emissionStrength`). |
| `Metallic` | (Placeholder) metallic specular contribution.           |
| `Glass`    | (Placeholder) transmissive dielectric contribution.     |

A graph MUST contain **at least one** terminal node. A
graph with zero terminals does not describe any shading
and MUST be rejected at parse time.

A v1 graph SHOULD contain **at most one** node of each
terminal type. The motivating case is the existing
flat-struct material's coexisting baseColor + emission:
a graph with one `Diffuse` and one `Emission` terminal
matches that material exactly. Multiple terminals of the
same type (two `Diffuse` nodes, two `Glass` nodes) are
NOT defined for v1: the rules for blending their
contributions are the BSDF-mixing concern that section
8 explicitly punts. Parsers MAY accept such graphs and
warn; renderers MAY pick one and ignore the rest, or
sum them, or reject; the spec does not pin a winner
until the mixing slice lands.

The set of terminals therefore plays the same role as a
single conventional "root" or "output" node: it is the
point at which the graph hands its computed shading
contributions back to the renderer. v1 keeps the set
explicit (multiple distinct terminals) instead of
introducing a single "Output" node that aggregates them,
because the renderer's existing shading model already
processes the contributions independently.

## 8. Evaluation model

This section pins WHEN the graph fires, WHAT it consumes,
and WHAT it produces. It deliberately stops short of byte
layouts and kernel snippets - those are the GPU compilation
strategy's job (section 9) and the eventual implementation's
job. The contract here is conceptual: every implementation
the project ships MUST honour it; the implementations are
free to differ in mechanics.

### 8.1 Per-hit evaluation contract

A material graph evaluates **once per surface hit** during
shading. Each evaluation:

- **Reads** a *shading context* (the per-hit values the
  renderer's path tracer already computes: surface
  position, surface UV, surface normal, view direction,
  the bound texture array, the per-hit material id, ...)
  plus the material's compiled graph.
- **Produces** a *snapshot* shaped exactly like the
  existing `rr::material::MaterialParams` (a
  `baseColor`, an `emissionColor`, an `emissionStrength`,
  a `roughness`, a `metallic`, ...).
- **Returns** that snapshot to the renderer's existing
  shading code, which integrates it the same way it
  integrates flat-struct materials today.

Three properties follow:

- **Determinism.** Same shading context + same compiled
  graph -> same snapshot, bit-for-bit. No randomness
  inside the graph (sampling is the integrator's
  concern, see 5.3).
- **Statelessness.** Each evaluation is independent of
  every other - across pixels, samples, bounces,
  threads, frames. The graph reads the context, writes
  the snapshot, returns. No history buffers, no shared
  scratch, no cross-thread synchronisation.
- **Boundedness.** The cost of one evaluation is decided
  at host-side compile time; the runtime does not
  allocate, recurse unbounded, or branch on input values
  in a way that changes the operation count. (See 9.4 -
  the strategy enforces this by construction.)

### 8.2 Two-stage model

The renderer evaluates the graph through two distinct
stages:

| Stage | Where | Frequency | Inputs | Outputs |
|-------|-------|-----------|--------|---------|
| Compile | Host (CPU) | Per material, per author edit | A `Graph` host object (the section-6/7 contract) | A backend-agnostic IR (section 9) + an upload-ready device buffer |
| Execute | Device (GPU) | Per surface hit, per bounce, per sample, per pixel | Compiled IR + shading context | A `MaterialParams` snapshot |

Compile is **infrequent and host-side**. It runs once
when the scene loads, or once when an author edits a
node (in the standalone editor or the C4D bridge -
those are the M19 / M21 surfaces, not the renderer's).
The per-edit path is the constraint that motivates
section 5.2: editing one node MUST NOT recompile the
renderer or rebuild the whole scene.

Execute is **frequent and device-side**. It is the only
stage the path tracer's shading kernel cares about.
Every per-hit work item runs the same Execute path with
the per-material IR; warp divergence between materials
is unavoidable but warp divergence INSIDE the
evaluation of a single material is what 9.4 designs
out.

The split is the same one the rest of the renderer
already uses: the host composes data, the kernel
consumes it. The graph is one more piece of data the
host composes.

### 8.3 Evaluation order

Compile traverses the graph in **topological order**:
each node fires only after every node that feeds one of
its connected inputs has fired. The ordering is fixed
at compile time; the device-side Execute path traverses
the same order linearly with no branching on graph
structure.

Three rules pin the order's exact behaviour:

- **Terminal-driven reachability.** Only nodes
  reachable backwards from at least one terminal (the
  BSDFs from 6.5) are present in the IR. Dead-code
  subgraphs (per 7.4) are dropped at compile time and
  never reach the device.
- **Default-value semantics.** An unwired input takes
  the catalogue default for that input (e.g.
  `Mix.factor` defaults to `0.5`). Defaults are folded
  into the IR at compile time; the runtime does NOT
  branch on "is this input wired?".
- **Fan-out caching.** When a node's output drives
  multiple consumers, the output is computed once per
  evaluation and parked in a *slot* (see 9.2);
  consumers read the slot. This keeps cost linear in
  node count rather than exponential in fan-out depth.

### 8.4 What the contract does NOT include

The evaluation model's contract is the WHAT, not the HOW.
v1 deliberately does not pin:

- The Compile stage's exact algorithm. Any topological
  traversal that respects 8.3 is admissible.
- The Execute stage's exact instruction format. The
  IR is the strategy's job (section 9); the contract
  here only requires that the IR exists and is
  device-evaluable.
- Whether multiple materials share an IR layout (a
  per-scene optimisation) or each gets its own (a
  per-material clarity choice). Both are admissible
  because both produce the same snapshot.

## 9. GPU compilation strategy

This section pins HOW the host turns a `Graph` into
something the device can execute. It is conceptual but
concrete: it commits to a pipeline shape, an IR shape,
a per-node mapping, and a branching policy. The exact
byte layouts, the maximum-op-count limits, and the
choice between an interpreter and an NVRTC-emitted CUDA
function are explicit non-decisions, deferred to a
future implementation slice.

### 9.1 Compilation pipeline

The host turns a graph into device-resident state through
a fixed sequence of steps:

```
                  +---------+
   author edits   |  Graph  |  in-memory host object;
                  +----+----+  the section-6/7 contract.
                       |
                       v
                  +---------+   type + DAG + reachability
                  |Validate |   per section 7. Reject on
                  +----+----+   any rule break.
                       |
                       v
                  +---------+   topological order; drop
                  | Lower   |   nodes not reachable from
                  +----+----+   any terminal; fold
                       |        catalogue defaults into
                       v        the IR.
                  +---------+
                  |   IR    |   flat per-material plan;
                  +----+----+   plain old data.
                       |
                       v
                  +---------+
                  | Upload  |   IR -> GpuBuffer (CUDA) or
                  +----+----+   per-material SBT record
                       |        (OptiX). Section 9.5.
                       v
                +-------------+
                | Device IR   |  device-resident; the
                | + slot pool |  shading kernel iterates
                +-------------+  this on every hit.
```

The pipeline is **strictly host-side** through the Upload
step; nothing in the kernel code path runs the Validate
or Lower steps. That is the same split the rest of the
renderer follows (`GpuScene::upload_from(scene)`
flattens host scene data; the kernel never sees the host
objects).

### 9.2 Intermediate representation

The IR is a small, flat per-material plan:

- An **operation list**: a sequence of records, each
  describing one node's evaluation. Records are
  fixed-shape and self-contained: an opcode (which
  node type), references to input slots, a destination
  slot, and any immediate values (the constants the
  node carries on itself, like `ConstantColor.value`
  or `TextureSample.texture_id`).
- A **slot pool**: a flat array of typed scratch
  values, one slot per output of every non-dead-code
  node. Slots are addressed by integer index; each
  slot's type (per 7.2) is decided at compile time.
- A **terminal table**: the small mapping from each
  terminal (Diffuse / Emission / Metallic / Glass) to
  the slot or slots it consumes. The Execute stage
  reads this table to assemble the `MaterialParams`
  snapshot at the end of evaluation.

Two properties make the IR cheap to upload and cheap to
execute:

- **Plain old data.** No pointers into the host scene
  graph, no string keys, no dynamic dispatch tables.
  Every reference is an integer index into the IR's
  own arrays.
- **Self-contained.** Beyond the shading context the
  renderer already provides, the IR depends on
  nothing. Editing one material's IR does not affect
  another's; uploading one IR does not require
  re-uploading another.

### 9.3 Per-node mapping

Each node from section 6 lowers to a small piece of
device-side logic. The lowering reuses primitives the
renderer already has - notably the texture sampler from
M16 - so the IR's repertoire matches the renderer's
existing capabilities.

| Node             | Device-side lowering                                             |
|------------------|------------------------------------------------------------------|
| `ConstantFloat`  | A literal slot value baked at compile time; no runtime op.       |
| `ConstantColor`  | Same - a literal slot value, no runtime op.                      |
| `TextureSample`  | A call into `sample_texture(scene.textures[texture_id], uv)` from `src/cuda/CudaTexture.cuh`. |
| `Add`            | Per-component float / float3 add.                                |
| `Multiply`       | Per-component float / float3 multiply.                           |
| `Mix`            | `a * (1 - factor) + b * factor` per component.                   |
| `Normal`         | Read `Hit::normal` from the shading context; write the slot.     |
| `UV`             | Read `Hit::uv` from the shading context; write the slot.         |
| `UVTransform`    | Two fused multiply-adds per component (`uv * scale + offset`).   |
| `Diffuse`        | The terminal table records this slot as `MaterialParams::baseColor`. |
| `Emission`       | Terminal table records colour + strength slots into emission.    |
| `Metallic`       | (Placeholder) Terminal table stages the slots; renderer's shading reduces to Lambertian until the GGX BSDF lands. |
| `Glass`          | (Placeholder) Terminal table stages the slots; renderer's shading reduces to diffuse until the dielectric BSDF lands. |

The constants (`ConstantFloat`, `ConstantColor`) are
called out as having no runtime op because of the
folding policy in 9.4 - they live in the slot pool's
initial state and never produce a per-hit instruction.

### 9.4 Avoiding dynamic branching

Per the GPU-first constraint (5.1) and the path-tracing
constraint (5.3), the Execute path MUST NOT diverge
inside a single material's evaluation. The compiler
enforces that with three policies:

- **Constant folding at compile time.** Any subgraph
  whose inputs are all constants collapses into a
  single literal in the slot pool. A graph that wires
  `ConstantColor + ConstantColor -> Diffuse.albedo`
  emits ONE slot literal, not three operations + two
  intermediate slots.
- **Default folding at compile time.** Unwired inputs
  resolve to their catalogue defaults at lowering. The
  runtime does NOT have an "input wired?" branch.
- **Linear opcode stream.** The operation list is a
  flat sequence; the Execute path is `for each op:
  dispatch(op)` with no early-exit, no skip, no
  conditional jump. Every op fires every time the IR
  runs. A node that contributes nothing to the chosen
  terminals never appears in the list (dead-code drop,
  per 8.3).

A few things the strategy explicitly does NOT promise:

- **Cross-material sharing.** Different materials may
  have different IRs of different lengths; warp
  threads that hit different materials diverge across
  IRs. That divergence is the integrator's concern
  (path-trace material sorting, M16 / M22 work), not
  the graph's.
- **Vector packing.** v1 does not commit to packing
  multiple op records per cache line or coalescing
  slot reads. Either is an implementation slice's
  call.

### 9.5 Backend mapping (CUDA / OptiX)

The IR is backend-agnostic. The two GPU backends differ
in WHERE the IR lives and HOW the kernel reaches it; the
contents are identical.

- **CUDA backend** (`src/cuda/`). The IR per material
  becomes one entry in a per-scene array of compiled
  graphs, uploaded as `GpuBuffer`s alongside the
  existing material array (`GpuScene::upload_materials`).
  The closest-hit kernel reads the IR for the hit's
  material id and runs the linear opcode loop inline.
  Visual diff against today's kernel: between "look up
  `MaterialParams`" and "shade", a small "evaluate
  graph IR -> overwrite `MaterialParams` snapshot" step
  appears.
- **OptiX backend** (`src/optix/`, M15 plan). The IR
  becomes part of each material's Shader Binding Table
  record. The closest-hit program runs the same linear
  opcode loop, fed by the per-material SBT data. The
  IR's bytewise contents are the same as in the CUDA
  case; only the address it lives at differs.

In either backend the per-hit evaluation cost is bounded
by the operation count of that material's IR, the IR is
read-only once uploaded, and the shading kernel never
walks back into host memory.

### 9.6 What this strategy does NOT pin

The strategy is concrete enough to drive an
implementation slice without committing to choices that
deserve their own slice. v1 does NOT pin:

- The exact byte layout of an operation record (opcode
  width, slot-index width, immediate-value packing).
- The maximum operations per material. The constraint
  is "small enough to live alongside the existing
  material data on every supported GPU"; the number is
  set by the implementation.
- Whether the device-side Execute is an interpreter or
  emitted CUDA / OptiX source via NVRTC. The
  interpreter is the simplest first target; an NVRTC
  emitter is a clear later optimisation. Either honours
  9.1-9.5 above.
- The progressive update protocol: when one material's
  graph is edited, what subset of the device buffers
  is replaced. Section 5.2's "no full-scene churn for
  one slider tick" rule is the constraint; the
  granularity is the implementation slice's call.

## 10. What this slice covers

This and the previous slices together establish:

- The graph at the conceptual level: purpose, relationship
  to `MaterialParams`, GPU / real-time / path-tracing
  constraints (sections 1-5).
- The v1 node catalogue with naming conventions and a
  per-node entry shape (section 6).
- The socket / connection / topology contract: what
  sockets are, the closed v1 type list, the legal
  implicit conversions, the DAG requirement, and the
  terminal-node contract (section 7).
- The evaluation model: per-hit contract, two-stage
  host-compile / device-execute split, evaluation
  order, statelessness (section 8).
- The GPU compilation strategy: pipeline, IR shape,
  per-node mapping, branching policy, CUDA / OptiX
  backend mapping (section 9).

It deliberately does NOT pin:

- The scene-file integration (the optional
  `materials[].graph` block in `.rrscene`).
- The standalone editor's UX.
- The Cinema 4D bridge's graph emission.

Each of those lands as its own doc slice. Implementation
work begins only after the slices that constrain it have
landed - the same incremental rule the rest of the project
follows.

## 11. Out of scope for v1 of the spec

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

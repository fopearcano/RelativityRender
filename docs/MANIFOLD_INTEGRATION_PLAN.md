# RelativityRender — Manifold Integration Plan

Status: **Planning document only. No source code lands with
this artifact.** Per the master instructions (rules #2, #3,
#12) the only deliverable of this stage is this file.
Concrete kernels, CLI parsing, RenderSettings extensions,
and AOV plumbing land in their own incremental commits
(`MANI-I.1` through `MANI-I.10`), each with a
`BUILD_PLAN.md` entry and its own reference / acceptance
output.

This document is read alongside:

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — top-
  level rules and the 25-step development order.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` — the layered
  architecture this plan implements. §3 names the Manifold
  Core's read-only surface; §5 fixes Phase 2 as the core;
  §7 fixes the chart-aware seam into the existing
  CUDA / OptiX path tracer; §10 enumerates the milestone
  order this plan threads through.
- `docs/FIELD_INTERPRETATION_LAYER.md` — Phase 1's
  perception-transcoding sibling. Its §9 kernel slices
  (FIELD.3 Kretschmann-scalar diagnostic AOV onward) wait
  on this plan's MANI-I.8 / MANI-I.9 to ship a curved
  chart they can read.
- `docs/MASTER_ARCHITECTURE.md` — long-term project
  architecture outside the Manifold Core Pivot.
- `docs/MODULE_MAP.md`, `docs/DEVELOPMENT_RULES.md`,
  `docs/BUILD_PLAN.md` — module ownership, engineering
  rules, per-slice implementation status.

---

## 1. Purpose & scope

The Manifold Core's data layer is already in place:
`src/manifold/` ships `CoordinateChart`, `MetricTensor`,
`ObserverFrame`, `GeodesicState`, `ManifoldTransform`, and
`ManifoldMode` as header-only POD types (MANIFOLD.1 through
MANIFOLD.7), exercised by 112 assertions in
`tests/manifold_identity_tests.cpp` and 12/12 ctest
binaries on the audit host.

What is *not* in place is the integration of those POD
types into the renderer's actual code path. Today the
`rr_manifold` library has zero consumers outside its own
headers; the CUDA / OptiX renderer pipeline produces
pre-pivot pixels exactly as it did before the Manifold
Core landed. Three years of accumulated tests and reference
images (the `--render-relativistic`, `--render-scene`,
`--render-mesh-scene`, `--render-material-scene`,
`--render-direct-lighting`, `--render-aovs`,
`--render-denoise`, etc. surfaces) must keep producing
bit-identical output through every step of this plan.

This document plans the **integration**: seven incremental
slices that, together, take the Manifold Core from
"shaped data POD types with no consumer" to "first
production-style Schwarzschild-like artistic chart and
Penrose-like compactification visualisation", with every
intermediate state keeping the existing renderer green.

The plan is **structural and operational**. It does not
introduce any source artifact today; each named slice will
land in its own commit with its own `BUILD_PLAN.md` entry
and its own audit doc when audit is required.

---

## 2. Integration principle: never break the current renderer

The single load-bearing invariant of this plan is:

> Every slice (MANI-I.1 through MANI-I.10) must preserve
> the renderer's pre-pivot output bit-for-bit on every
> existing CLI action when the user does not opt in to a
> non-Euclidean manifold mode.

This is the architectural reading of architecture-doc
§7.1's "no regression on the Minkowski path" rule. It is
also the operational reading of master rule #2 ("keep
every step compilable") extended to runtime behaviour:
every step must not only compile but also produce the
same pixel output it produced before.

The integration mechanism is **opt-in**. Each slice that
touches the renderer adds new state to the CLI / config /
GPU path, but the new state is exercised only when the
user explicitly turns it on. The default state
(`ManifoldMode{}` = `{enabled = false, chart = Euclidean,
strength = 0, ...}`) makes the renderer bypass the
manifold layer entirely, and the GPU kernel falls through
to its pre-pivot code path.

Three layers carry this invariant:

1. **CLI default.** The CLI flags introduced in MANI-I.1
   default to "manifold disabled". Scene fixtures and the
   existing `--render-*` actions that do not name a
   manifold flag get the pre-pivot interpretation.
2. **Config default.** The `RenderSettings`
   (or sibling struct) the CLI populates carries
   `ManifoldMode{}` by default — the disabled state.
3. **GPU default.** The path-tracer kernel branches on
   `ManifoldMode::enabled` at the top of ray-gen. When
   the flag is `false` the kernel takes the existing
   pre-pivot path with no Manifold Core lookups. When
   the flag is `true` the kernel calls into the
   chart-aware seam (architecture-doc §7.1).

A bit-identity ctest covering at least the
`--render-scene`, `--render-mesh-scene`,
`--render-material-scene`, `--render-direct-lighting`, and
`--render-aovs` actions on a representative
`.rrscene` fixture is the **acceptance gate** every slice
crosses before merge.

---

## 3. Slice ordering & dependencies

The ten slices form a strict prefix chain — each slice
ships only after its predecessor is green. MANI-I.2,
MANI-I.4, and MANI-I.6 are per-slice audit slots
(doc-only) inserted once MANI-I.1 / MANI-I.3 / MANI-I.5
landed (see `docs/MANIFOLD_CLI_CONFIG_AUDIT.md`,
`docs/MANIFOLD_RENDER_CONFIG_BRIDGE_AUDIT.md`, and
`docs/MANIFOLD_EUCLIDEAN_GPU_IDENTITY_AUDIT.md`); the
original plan had a single-audit endpoint, but
per-slice audits slot in alongside the implementation
slices when an operator-prompted gate is needed before
the next slice starts.

```
+--------------------------------------------------------------+
| MANI-I.1: CLI config only                                    |
|   - Parses --manifold-enable / --manifold-chart /            |
|     --manifold-strength / --manifold-debug into              |
|     rr::core::Config::manifold (a ManifoldMode value         |
|     shipped at MANIFOLD.6); no RenderSettings touch.         |
+--------------------------------------------------------------+
                              |
                              v
+--------------------------------------------------------------+
| MANI-I.2: CLI Config Audit (docs-only)                       |
|   - Per-slice gate for MANI-I.1. Verifies the four flags     |
|     exist, defaults are no-op, invalid input is handled      |
|     safely on both chart-name and strength axes, no          |
|     renderer behaviour changed, build/test green.            |
|   - Documented in docs/MANIFOLD_CLI_CONFIG_AUDIT.md; not     |
|     a slice-specific section in this plan.                   |
+--------------------------------------------------------------+
                              |
                              v
+--------------------------------------------------------------+
| MANI-I.3: Render Config Bridge                               |
|   - rr::pathtracer::PathTraceConfig gains a ManifoldMode     |
|     member; CLI dispatcher copies cfg.manifold into          |
|     pcfg.manifold; both pathtrace dispatchers (CUDA and      |
|     OptiX) emit a single render-start log line. No GPU       |
|     touch.                                                   |
+--------------------------------------------------------------+
                              |
                              v
+--------------------------------------------------------------+
| MANI-I.4: Render Config Bridge Audit (docs-only)             |
|   - Per-slice gate for MANI-I.3. Verifies ManifoldMode       |
|     reaches PathTraceConfig, defaults stay disabled /        |
|     Euclidean, render logs the mode at both pathtrace        |
|     dispatch sites, no CUDA/OptiX behaviour changed, no      |
|     visual output change by default, build/test green.       |
|   - Documented in docs/MANIFOLD_RENDER_CONFIG_BRIDGE_AUDIT.md;|
|     not a slice-specific section in this plan.               |
+--------------------------------------------------------------+
                              |
                              v
+--------------------------------------------------------------+
| MANI-I.5: Euclidean identity GPU path                        |
|   - First real GPU touch. The CUDA launcher and OptiX        |
|     OptixLaunchParams carry ManifoldMode; on the Euclidean   |
|     default the kernel arms still ignore it (no FP-drift     |
|     risk on bit-identity reference images). is_active()      |
|     helper added for MANI-I.7+ to guard on.                  |
+--------------------------------------------------------------+
                              |
                              v
+--------------------------------------------------------------+
| MANI-I.6: Euclidean Identity GPU Path Audit (docs-only)      |
|   - Per-slice gate for MANI-I.5. Verifies GPU-side payload   |
|     landed, disabled mode is no-op, Euclidean is identity,   |
|     both CUDA and OptiX kernel arms remain visually          |
|     unchanged (structurally guaranteed: no kernel reads      |
|     manifold_mode), build/test green. Runtime CUDA/OptiX     |
|     byte-identity verification DEFERRED behind the           |
|     audit-host gate.                                         |
|   - Documented in docs/MANIFOLD_EUCLIDEAN_GPU_IDENTITY_AUDIT.md;|
|     not a slice-specific section in this plan.               |
+--------------------------------------------------------------+
                              |
                              v
+--------------------------------------------------------------+
| MANI-I.7: debug coordinate-warp AOV                          |
|   - New AOV slot `ManifoldWarp` writes per-pixel chart-      |
|     space hit position. Sanity check for the future          |
|     curved-chart slices.                                     |
+--------------------------------------------------------------+
                              |
                              v
+--------------------------------------------------------------+
| MANI-I.8: Schwarzschild-like artistic coordinate remap       |
|   - First non-trivial chart. Artistic, not physical.         |
|     Lights bend around a configured "mass" centre via a      |
|     closed-form coordinate remap (NOT a geodesic             |
|     integrator).                                             |
+--------------------------------------------------------------+
                              |
                              v
+--------------------------------------------------------------+
| MANI-I.9: Penrose-like compactification visualisation        |
|   - Second non-trivial chart. Maps asymptotic infinity       |
|     onto a finite boundary for diagrammatic visualisation.   |
+--------------------------------------------------------------+
                              |
                              v
+--------------------------------------------------------------+
| MANI-I.10: cross-host final audit                             |
|   - Cross-host runtime audit (CUDA + OptiX-SDK host)         |
|     pinning every bit-identity invariant + every opt-in      |
|     surface + every new AOV layout + every CLI combo.        |
|     Merge gate for the whole MANI-I.* programme.             |
+--------------------------------------------------------------+
```

**Why this ordering?**

- MANI-I.1 (CLI config only) and MANI-I.3 (renderer
  config plumb) are host-only; they introduce the config
  surface the rest of the chain reads. Landing them
  early lets MANI-I.5+ start from a stable config shape.
- MANI-I.2 (CLI Config Audit) is a doc-only gate
  between MANI-I.1 (which shipped a non-trivial parser
  surface and a new struct field) and MANI-I.3 (which
  begins modifying the renderer's config struct).
  Inserting an audit slot here pins the bit-identity
  invariant on the CLI surface before any downstream
  slice begins touching the renderer's data flow.
- MANI-I.4 (Render Config Bridge Audit) is the
  doc-only gate between MANI-I.3 (which extended
  PathTraceConfig and added the render-start log line)
  and MANI-I.5 (the first GPU touch). The audit pins
  the structural guarantee that no kernel reads
  pcfg.manifold before MANI-I.5 deliberately makes it
  the consumer.
- MANI-I.5 is the **first GPU touch** but is
  deliberately a no-op — its acceptance is "bit-
  identical output to the pre-MANI-I.5 baseline".
  Landing the GPU plumbing while it is a no-op is what
  makes MANI-I.8 / MANI-I.9 tractable: only the
  *chart* changes, not the kernel scaffolding.
- MANI-I.6 (Euclidean Identity GPU Path Audit) is
  the doc-only gate between MANI-I.5 (which widened
  the launch-params POD and the CUDA launcher
  signature) and MANI-I.7 (the first kernel-side
  read of manifold state, through the debug AOV
  slot). The audit pins the structural guarantee
  that no kernel arm reads manifold_mode at
  MANI-I.5; the runtime CUDA / OptiX byte-identity
  re-verification is deferred behind the
  audit-host gate, matching the existing
  firefly_clamp / enable_nee posture.
- MANI-I.7 (debug AOV) lands before any curved chart
  so the visual sanity check is available *when* the
  first curved chart breaks something.
- MANI-I.8 (Schwarzschild-like) before MANI-I.9
  (Penrose-like) because the Schwarzschild-like remap
  is the simpler coordinate transform (radial-only,
  closed-form); Penrose-like compactification needs
  `tanh`-style coordinate compression on top of an
  already-working curved-chart seam.
- MANI-I.10 (final audit) is last; the audit covers
  every prior slice including the per-slice MANI-I.2 /
  MANI-I.4 / MANI-I.6 audits and absorbs the runtime
  CUDA / OptiX byte-identity gate that MANI-I.6
  defers.

---

## 4. MANI-I.1 — CLI config only

### Goal

Add four new CLI flags that populate an
`rr::manifold::ManifoldMode` value on `rr::core::Config`
the rest of the chain will consume. Host-only this slice;
no `RenderSettings`, no renderer, no GPU code touched.

### Prerequisites

- MANIFOLD.6 (`src/manifold/ManifoldMode.h` config struct
  shipped) — already green.

### What ships (LANDED; this section reflects what actually shipped)

- **`src/core/Config.h`** gains a
  `rr::manifold::ManifoldMode manifold` field. The
  default value is the documented
  `disabled_manifold_mode()` anchor (enabled=false,
  chart=Euclidean, strength=0, debug=off) — every
  existing CLI invocation without a `--manifold-*` flag
  produces pixel-bit-identical output to the pre-pivot
  renderer.
- **`src/core/CommandLine.cpp`** parses four flags:
  - `--manifold-enable` — presence-only switch. Sets
    `manifold.enabled = true`. Mirrors the
    `--denoise` / `--enable-nee` shape.
  - `--manifold-chart <name>` — takes one value naming
    the chart family. Legal names (case-sensitive,
    kebab-case): `euclidean`, `schwarzschild-like`,
    `kruskal-like`, `penrose-like`, `kerr-like`. Maps
    to `CoordinateChartType` per MANIFOLD.1. Unknown
    values are a parse error with a message listing
    every legal name.
  - `--manifold-strength <float>` — sets
    `manifold.strength`. Nominal `[0, 1]`; out-of-range
    values pass through to the renderer (per the
    `ManifoldMode::strength` contract). Only a non-
    parseable string is rejected at parse time.
  - `--manifold-debug` — presence-only switch. Sets
    `manifold.debug_visualization = true`. Reserved
    for the MANI-I.7 debug coordinate-warp AOV; no
    observable behaviour change this slice.

  (Naming refinement vs the original plan: the original
  draft of this section proposed `--manifold-mode` as a
  single flag combining the chart selector and an
  implicit enable bit, plus `--manifold-debug-warp` for
  the debug toggle. MANI-I.1 split the chart selector
  from the enable bit — `--manifold-enable` is now a
  dedicated presence-only flag — and renamed
  `--manifold-debug-warp` to `--manifold-debug` to reflect
  that the toggle flips a general `debug_visualization`
  field, not a warp-AOV-specific one. The four-flag
  surface lets an artist tweak `chart` / `strength` /
  `debug` without flipping the master switch and vice
  versa, matching the FIELD.1-era idiom for
  enabled-vs-strength separation.)
- **`tests/cli_tests.cpp`** (existing binary) gains
  13 MANI-I.1 test cases (M1–M13) covering: default-off
  anchors, each-value chart dispatch, unknown-chart
  rejection, case-sensitivity, in/out-of-range strength
  values, non-parseable strength rejection, debug flag
  presence, four-flag combination, order-independence,
  missing-value rejection, and default-off byte-identity
  with other modifier flags
  (`--denoise` / `--enable-nee` / `--firefly-clamp` /
  `--beta` / `--width` / `--height`).
- **`docs/BUILD_PLAN.md`** gets a MANI-I.1 entry.

### Acceptance

- Audit-host build green; ctest 12/12 (no new binary).
- The four new flags appear in `--help` output, each
  with a dedicated help block citing the MANI-I.1 slice
  and the future consumer slice (MANI-I.3 for the
  config plumb, MANI-I.7 for `--manifold-debug`).
- Running every existing CLI action **without** any
  `--manifold-*` flag produces bit-identical output to
  the pre-MANI-I.1 baseline (the renderer ignores the
  populated `ManifoldMode` because nothing reads it yet).
- A pixel-cmp on at least one representative scene
  (`scenes/test_full_scene.rrscene` via `--render-scene`)
  confirms 0-byte diff.

### Risks & mitigations

- **CLI namespace collision** with a future
  `--manifold-foo` flag. Mitigated by reserving the
  `--manifold-*` prefix exclusively for this layer and
  flagging it in the `--help` text.
- **Silent parsing failure** on a malformed
  `--manifold-mode` value. Mitigated by mandatory exact-
  match parsing with a clean error message listing the
  legal enum values.

### What does NOT ship

- No `RenderSettings` field for the manifold (MANI-I.3).
  The field lives only on `Config` (the CLI parser's
  struct) this slice; the renderer's render-time config
  shape is unchanged.
- No renderer call site that reads the populated value.
  Every CLI action's dispatcher ignores `Config::manifold`
  this slice.
- No GPU change.
- No new AOV.
- No `.rrscene` serialisation of the manifold mode (lands
  later, alongside MANI-I.8 when artists need to author
  curved-chart scenes through the file format).
- No `--manifold-*` modifier flag is wired into the
  action-mutual-exclusion list (the four flags are
  modifiers, not actions).

---

## 5. MANI-I.3 — pass ManifoldMode into renderer config

### Goal

Plumb the parsed `ManifoldMode` from the CLI through the
renderer's config surface so the future GPU slices can
read it. Host-side wiring only; no GPU code touched; no
behaviour change.

### Prerequisites

- MANI-I.1 — CLI parsing in place.

### What ships (LANDED; reflects what actually shipped)

- **`src/pathtracer/PathTracer.h`** gains a
  `rr::manifold::ManifoldMode manifold` member on
  `PathTraceConfig`. Default value is the
  `disabled_manifold_mode()` "no output change" anchor.
  (Implementation choice: `PathTraceConfig` is the
  per-render runtime config the path tracer actually
  consumes, so the field lives there rather than on
  `rr::scene::RenderSettings`. The original plan offered
  either as the home; `PathTraceConfig` is the cleaner
  choice because it avoids touching the `.rrscene`
  scene-file surface, deferring the file-format change
  to a later slice where it has a real artist-facing
  reason to land.)
- **`CMakeLists.txt`**
  `target_link_libraries(rr_pathtracer INTERFACE rr_math
  rr_manifold)` — `rr_pathtracer` now propagates the
  manifold header to its consumers (header-only
  INTERFACE link; `rr_renderer` and the main
  executable both pick this up transitively).
- **`src/main.cpp`**:
    - The CUDA pathtrace dispatcher
      (`run_render_pathtrace`) copies
      `cfg.manifold` into `pcfg.manifold` at every
      `PathTraceConfig` construction site (alongside
      the existing `pcfg.firefly_clamp` / `pcfg.enable_nee`
      assignments).
    - A small `format_manifold_mode(ManifoldMode)`
      helper in the anonymous namespace formats the
      mode into a single human-readable line
      (`"<enabled|disabled> (chart=<kebab>, strength=<f>,
       debug=<on|off>)"`) — the chart-name mapping
      mirrors the kebab-case strings the
      `--manifold-chart` CLI flag accepts.
    - Both the OptiX pathtrace dispatcher
      (`run_render_optix_pathtrace`) and the CUDA
      pathtrace dispatcher emit a single
      `Logger::info("manifold         : ...")` line at
      render-launch time, alongside the existing
      `firefly_clamp` / `enable_nee` log lines.
- **`docs/BUILD_PLAN.md`** gets a MANI-I.3 entry.

### What does NOT ship

- **No `RenderSettings` change.** The scene-file POD
  (`rr::scene::RenderSettings`) is byte-identical. The
  `.rrscene` parser / writer is untouched. This defers
  the file-format change to a later slice (per the
  integration plan's existing "no file-format bump in
  MANI-I.3" non-goal).
- **No GPU consumption of the field.** The CUDA
  `k_pathtrace_sample` kernel and the OptiX
  `__raygen__pathtrace` program both ignore
  `pcfg.manifold`. MANI-I.5 is the first slice that
  wires the field into either backend's GPU code path.
- **No new CLI flag.** Covered by MANI-I.1.
- **No new `renderer_tests` assertions.** The MANI-I.3
  surface is host-only plumb + log line; the cli_tests
  binary already verifies the parser shape end-to-end
  (123/123 assertions across 20 test cases including
  the 13 MANI-I.1 cases). A new `renderer_tests` case
  would have to mock `gpu_scene.upload_*` to exercise
  the plumb path, which is scope creep for this slice.

### Acceptance

- Audit-host build green; ctest 12/12. `cli_tests`
  reports `123/123 passed` (unchanged from MANI-I.1).
- A `--render-pathtrace` or `--render-optix-pathtrace`
  invocation on a CUDA + OptiX-SDK host emits a single
  `manifold         : <enabled|disabled> (chart=...,
  strength=..., debug=...)` log line at render-launch
  time, alongside the existing `firefly_clamp` /
  `enable_nee` lines.
- Running every existing CLI action without any
  `--manifold-*` flag produces bit-identical pixel
  output to the pre-MANI-I.3 baseline (structurally
  guaranteed: no kernel reads `pcfg.manifold`).
- Audit-host observation: both dispatchers'
  audit-host-fallback paths short-circuit *before*
  the log line (no CUDA / no OptiX SDK), matching the
  existing `firefly_clamp` / `enable_nee` log lines'
  visibility on the same audit host. Runtime
  observation of the new log line is therefore
  audit-host-deferred, identical to the deferral on
  the prior log lines.

### Risks & mitigations

- **`rr_pathtracer` consumer rebuild fan-out** from the
  new `rr_manifold` INTERFACE link. Mitigated by
  rr_manifold's header-only nature — the rebuild is
  limited to TUs that already included
  `pathtracer/PathTracer.h`, and the new include is
  a small POD header.
- **`PathTraceConfig` ABI break** for any code that
  initialises the struct field-by-field. Mitigated by
  using member-default initialisation so the new field
  picks up the disabled default without forcing
  call-site changes; the only call site in the tree
  (main.cpp's `pcfg.samples_per_pixel = ...` block)
  is updated as part of this slice.
- **Cinema 4D bridge** (not yet shipped, but planned
  per master order #21): the bridge will eventually
  need to populate this field. Mitigated by
  documenting the field's role in the
  `PathTraceConfig` header so the bridge implementor
  sees it; when the file-format slice lands, the
  bridge plumb extends naturally.

---

## 6. MANI-I.5 — Euclidean identity GPU path

### Goal

First real GPU touch: the CUDA / OptiX path tracer reads
the `ManifoldMode` and `ManifoldTransform` and runs the
chart-aware ray seam **on the Euclidean chart**. The
seam's Euclidean specialisation is the identity map
(architecture-doc §3.1 / §7.1), so the rendered output
must be bit-identical to the pre-MANI-I.5 baseline.

This slice lands the GPU plumbing while it is still a
no-op. After this slice, every curved-chart slice
(MANI-I.8 / MANI-I.9) only needs to add a chart
specialisation; the kernel scaffolding is already in place.

### Prerequisites

- MANI-I.3 — `RenderSettings::manifold` carries the
  config.

### What ships (LANDED; reflects what actually shipped)

- **`src/manifold/ManifoldMode.h`** gains a
  `RR_HD inline bool is_active(const ManifoldMode&)`
  helper returning `m.enabled && m.chart !=
  CoordinateChartType::Euclidean`. The helper is the
  single guard MANI-I.7+ slices flip when they wire
  per-chart logic; today it is reachable from kernel
  code but no kernel calls it.
- **`src/optix/OptixLaunchParams.h`** gains a
  `rr::manifold::ManifoldMode manifold_mode{}` field
  appended at the end of the POD (preserves offsets of
  all pre-existing fields). The default-constructed
  value is the documented "disabled, Euclidean,
  strength 0, debug off" anchor.
- **`src/optix/OptixRenderer.h`** —
  `render_pathtrace_progressive(...)` gains a trailing
  `rr::manifold::ManifoldMode manifold_mode = {}`
  parameter (default = disabled-mode). The
  implementation in `OptixRenderer.cpp` populates
  `params.manifold_mode = manifold_mode` immediately
  after the existing `params.firefly_clamp` /
  `params.enable_nee` populates.
- **`src/cuda/CudaPathTracer.cuh`** —
  `launch_pathtrace_sample(...)` gains a trailing
  `rr::manifold::ManifoldMode manifold_mode = {}`
  parameter (default = disabled-mode). The
  implementation in `CudaPathTracer.cu` accepts the
  parameter as `[[maybe_unused]]` — the kernel does
  not consume the field this slice; the launcher's
  signature change is the GPU-side plumbing.
- **`src/pathtracer/PathTracer.cpp`** threads
  `cfg.manifold` through to the CUDA launcher's new
  trailing argument.
- **`src/main.cpp`** threads `cfg.manifold` through to
  the OptiX dispatcher's new trailing argument inside
  `run_render_optix_pathtrace`.
- **`CMakeLists.txt`** —
  `target_link_libraries(rr_optix PUBLIC rr_manifold)`
  so consumers of `rr_optix` see the manifold header
  transitively (the `OptixLaunchParams::manifold_mode`
  field is in a public header). Header-only
  INTERFACE link; no .cpp / link-order impact.
- **`docs/BUILD_PLAN.md`** gets a MANI-I.5 entry.

### Implementation choice notes

- **No `ManifoldTransform` field on the launch params
  this slice.** The earlier draft of this plan named
  both `ManifoldMode mode` AND
  `ManifoldTransform transform` as fields. Only
  `manifold_mode` ships at MANI-I.5 because:
  (a) the kernel does not need the full transform
  while it is gated to identity behaviour;
  (b) `ManifoldTransform`'s `CoordinateChart` member
  carries a `const char* name` which is a host-only
  pointer (the GPU memcpy would copy the host
  address, and dereferencing it on device would
  fault). Adding `ManifoldTransform` to the launch
  params requires a GPU-friendly POD strip — that
  surface lands with MANI-I.8's first curved-chart
  slice that actually needs to read the transform on
  device.
- **No kernel-side ray-gen seam this slice.** The
  earlier plan draft wrote a per-pixel
  `transform_ray_like_direction(transform, dir)` call
  into the kernel. Inserting that helper on the
  Euclidean / disabled default would compute
  `normalize(world_dir * (1 / scale))`; on a
  unit-length input with `scale = 1.0f` the resulting
  vector is mathematically equal to `world_dir` but
  the IEEE-754 `sqrt + multiply` chain can drift by
  one ULP per component, breaking the bit-identity
  invariant on the existing reference images. To
  preserve byte-identity at MANI-I.5, no kernel arm
  reads the new fields. MANI-I.7+ slices that
  introduce a curved-chart guard place the call
  *inside* the `is_active(mode)` branch so the
  Euclidean / disabled fast path stays bit-exact.
- **No hit-shading seam this slice.** Same FP-drift
  reasoning. The existing kernels continue to
  consume `Observer` / `RelativityParams` directly
  from the launch params (OptiX) or kernel args
  (CUDA); the manifold module's
  `to_relativity_observer(...)` bridge stays
  available for MANI-I.7+ to use when it is the
  active code path.

### Acceptance

- Audit-host build green; CUDA + OptiX-SDK host
  (when available) build green.
- ctest 12/12 on the audit host (`100% tests passed,
  0 tests failed out of 12`); `cli_tests: 123/123
  passed`.
- The `--render-pathtrace` and
  `--render-optix-pathtrace` actions accept
  `--manifold-*` flags without error and the
  manifold log line MANI-I.3 introduced still emits
  exactly once per render (verified at the parser
  layer by `cli_tests` cases M1-M13).
- **Bit-identity invariant**: structurally
  guaranteed because no kernel reads the new field
  this slice. The CUDA kernel accepts
  `manifold_mode` as `[[maybe_unused]]`; the OptiX
  device-side `__raygen__pathtrace` /
  `__closesthit__pathtrace` / `__miss__pathtrace`
  programs do not reference `optixLaunchParams.manifold_mode`.
  A future cross-host audit on a CUDA + OptiX-SDK
  host re-verifies by `cmp`-ing every pre-MANI-I.5
  reference PPM (`scenes/test_full_scene.rrscene`
  via `--render-scene`, `--render-mesh-scene`,
  `--render-material-scene`,
  `--render-direct-lighting`, `--render-aovs`,
  `--render-relativistic`, `--render-pathtrace`,
  `--render-aovs --denoise`).

### Risks & mitigations (LANDED slice)

- **Launch-params ABI break** for the existing
  OptiX pipeline. Mitigated by appending the
  `manifold_mode` field at the END of
  `OptixLaunchParams` (preserving offsets of every
  pre-MANI-I.5 field).
- **Floating-point non-bit-identity** from
  inserting a normalising ray-direction transform.
  Mitigated by NOT inserting the transform this
  slice (see "Implementation choice notes" above);
  MANI-I.7+ inserts it inside an `is_active(mode)`
  guard so the default-disabled / Euclidean fast
  path remains the existing pre-pivot code path.
- **`rr_optix` header consumer rebuild fan-out**
  from the new include of `manifold/ManifoldMode.h`
  in `OptixLaunchParams.h`. Mitigated by the
  manifold header's small POD-only surface (16
  bytes); the rebuild fan-out is limited to TUs
  that already included `OptixLaunchParams.h` (the
  three `rr_optix` `.cpp` files plus the device
  `OptixPrograms.cu` PTX TU).
- **`launch_pathtrace_sample` parameter
  proliferation** from the new
  `manifold_mode` argument. Mitigated by the
  default-value-equals-disabled trailing-argument
  pattern (same shape as the existing
  `firefly_clamp` / `enable_nee` defaults); no
  caller has to change unless it wants to opt in.

### Acceptance

- Audit-host build green; CUDA + OptiX-SDK host (when
  available) build green.
- ctest 12/12 on both build modes.
- **Bit-identity pixel cmp on at least these CLI actions**
  (the canonical "no regression" gate), each run with
  default `ManifoldMode{}` and compared against the
  pre-MANI-I.5 reference PPMs:
  - `--render-scene scenes/test_full_scene.rrscene`
  - `--render-mesh-scene`
  - `--render-material-scene`
  - `--render-direct-lighting`
  - `--render-aovs scenes/test_full_scene.rrscene`
  - `--render-relativistic` (4-beta sweep)
  - `--render-pathtrace` (one fixed-seed render)
- The OptiX denoiser path
  (`--render-aovs --denoise`) is also bit-identical on
  the default manifold mode.

### Risks & mitigations

- **Launch-params ABI break** for the existing OptiX
  pipeline. Mitigated by appending the new fields at the
  end of the params struct (preserving offsets of all
  pre-existing fields) and by version-stamping the
  struct.
- **Floating-point non-bit-identity** from compiler
  reordering after the new helper inserts. Mitigated by
  marking the Euclidean specialisation as
  `RR_HD inline constexpr` so the optimiser folds it to
  the identity at compile time, leaving the existing
  generated PTX unchanged.
- **Observer-frame round-trip drift** if
  `to_relativity_observer(observer_frame_from(obs))`
  isn't a bit-exact identity on the constant-velocity
  case. Mitigated by an explicit assertion in
  `manifold_identity_tests` (already MANIFOLD.7-covered)
  pinning the round-trip exactness at single precision.

### What does NOT ship

- No curved-chart code path. The reserved
  `*Like` / `*LikePlaceholder` chart enumerators
  are accepted at the CLI parser and now ride
  through to the launch-params / launcher arg, but
  no kernel branches on chart type yet.
- No `ManifoldTransform` on the launch params (see
  "Implementation choice notes" above — deferred to
  MANI-I.8's first curved-chart slice).
- No kernel-side ray-gen seam (see "Implementation
  choice notes" — FP byte-identity gate).
- No hit-shading seam.
- No new AOV (MANI-I.7).
- No geodesic integrator. The "ray seam" is the
  straight-line identity for Euclidean; future
  curved charts replace `transform_ray_like_direction`
  with per-chart coordinate remaps **only** —
  geodesic integration is a longer-term programme
  outside this plan.
- No artist-facing strength interpolation yet (the
  `ManifoldMode::strength` field is plumbed but the
  Euclidean specialisation does not depend on it).

---

## 7. MANI-I.7 — debug coordinate-warp AOV

### Goal

A new AOV that visualises the chart-space coordinates of
each primary-ray hit. On the Euclidean chart the AOV is
identically the world-space hit position (no warp). On a
future curved chart (MANI-I.8 / MANI-I.9) the AOV will
make the coordinate deformation legible — the visual
sanity check the curved-chart slices need.

### Prerequisites

- MANI-I.5 — GPU plumbing in place.

### What ships (LANDED; reflects what actually shipped)

The canonical name landed as **`ManifoldCoordinates`** (the
informal "ManifoldWarp" label is replaced with the
PascalCase enumerator matching the existing `DopplerFactor`
/ `SearchlightFactor` convention). The task brief
`docs/MANIFOLD_DEBUG_AOV_TASK.md` discusses the rationale
in §3.4.

- **`src/renderer/AOV.h` + `AOV.cpp`** — new
  `AOVType::ManifoldCoordinates = 6` enumerator appended
  at the end of the enum (preserves every pre-MANI-I.8
  value); `aov_component_count` returns `3`;
  `aov_type_name` returns `"manifold_coordinates"`;
  `AOV::make_manifold_coordinates(...)` factory.
- **`src/cuda/CudaAOV.cuh`** — new
  `DeviceAOVView::manifold_coordinates` device pointer
  (default `nullptr` short-circuits the kernel write
  arm); mirrors the CPU-side `AOVTargets` slot.
- **`src/cuda/CudaRenderer.h`** — new
  `CudaRenderer::AOVTargets::manifold_coordinates`
  pointer field (default `nullptr`).
- **`src/cuda/CudaRenderer.cu`** — wires
  `view.aovs.manifold_coordinates = targets.manifold_coordinates`
  in `render_scene_with_aovs`.
- **`src/cuda/CudaTestKernel.cu`** — closest-hit / miss
  write arm in `k_render_scene` writes
  `(best.position.x, .y, .z)` on hit and `(0, 0, 0)` on
  miss when the pointer is non-null. The arm is the
  *neutral / identity diagnostic* for MANI-I.8: the
  world-space hit position is what the future
  curved-chart code would also output on the Euclidean
  default, so the implementation is honest for every
  future slice that adds chart-specific logic above
  this arm.
- **`src/optix/OptixLaunchParams.h`** — new
  `float* aov_manifold_coordinates = nullptr;` field at
  the end of the launch-params POD (preserves every
  pre-MANI-I.8 field's offset).
- **`src/optix/OptixPrograms.cu`** — closest-hit-side
  write arm (uses `optixGetWorldRayOrigin() +
  optixGetRayTmax() * optixGetWorldRayDirection()` to
  compute the world-space hit position) and miss-side
  write arm (writes `(0, 0, 0)`). Both arms null-gated.
  **The OptiX host-side `OptixRenderer::render_aovs`
  does NOT allocate this slot this slice** — that
  follow-up landing is deferred to a small subsequent
  slice (or rolled into the MANI-I.9 / MANI-I.10
  surface). The kernel arms stay dormant at runtime
  in the OptiX path until that wiring lands; the OptiX
  path's pre-MANI-I.8 output is byte-identical.
- **`src/main.cpp` `run_render_aovs`** — when
  `cfg.manifold.debug_visualization` is `true`,
  allocates a 7th `GpuAOVBuffer` for the
  `ManifoldCoordinates` AOV, threads its
  `device_ptr()` into `AOVTargets.manifold_coordinates`,
  and saves the downloaded buffer to
  `output/aov_manifold_coordinates.ppm` alongside the
  existing six AOV PPMs. When the gate is off the
  buffer is not allocated, `targets.manifold_coordinates
  = nullptr`, and the kernel arm short-circuits — the
  CUDA path's pre-MANI-I.8 output is byte-identical.
- **`tests/renderer_tests.cpp`** — three new test
  functions (six new RR_CHECKs total): enumerator
  value `== 6`, `aov_component_count == 3`,
  `aov_type_name == "manifold_coordinates"`,
  factory default-name behaviour, factory custom-name
  behaviour. The binary reports `19 / 19 passed`
  (was 13 / 13 pre-MANI-I.8).

### Acceptance

- Audit-host build green; ctest 12/12 (`100% tests
  passed, 0 tests failed out of 12`). `renderer_tests`
  reports `19 / 19 passed`. `cli_tests: 123/123 passed`
  (unchanged — parser surface untouched).
- CUDA path:
  `--render-aovs --manifold-debug` emits the existing
  six AOV PPMs PLUS `output/aov_manifold_coordinates.ppm`.
  `--render-aovs` *without* `--manifold-debug` emits
  exactly the existing six PPMs (no new file).
- OptiX path: `--render-optix-aovs` is byte-identical to
  the pre-MANI-I.8 baseline regardless of
  `--manifold-debug` (the kernel arms are wired but
  the host-side allocation is deferred — the field
  stays `nullptr` at runtime; pre-MANI-I.8 pixel
  output is structurally preserved).
- Beauty output byte-identity on every existing CLI
  action: structurally guaranteed; the new AOV write
  arm is gated on a null pointer that the existing
  dispatchers don't populate. The CUDA kernel's
  Beauty pass arithmetic is unchanged (only a new
  null-gated arm appended after the six existing
  AOV write arms).
- Runtime CUDA / OptiX verification of the AOV pixel
  values (e.g. `cmp` of the new PPM against a
  pinned reference) is DEFERRED behind the audit
  host's no-CUDA / no-OptiX-SDK fallback, matching
  the existing per-slice audits' posture (the
  MANI-I.9 audit and MANI-I.10 final cross-host audit
  will pin the reference images).

### Risks & mitigations (LANDED)

- **AOV-buffer layout change** breaks the OptiX
  denoiser hand-off. Mitigated by appending the new
  AOV slot at the END of the enum (preserves every
  pre-MANI-I.8 enumerator value) and by gating the
  new slot's allocation behind `--manifold-debug`
  (the denoiser path's `Beauty` / `Albedo` /
  `Normal` consumption is unaffected — those three
  slots' allocations are unchanged).
- **Disk-write side effect** for users who set
  `--manifold-debug` accidentally. Mitigated by the
  flag being explicit; the `--help` text already
  documents the flag (added at MANI-I.1).
- **OptiX host-side allocation deferred**. The kernel
  arms are in place but the OptiX `--render-optix-aovs`
  action does not yet emit the new PPM. Documented
  as a known follow-up; the OptiX path's
  pre-MANI-I.8 output is byte-identical structurally
  (null pointer → kernel short-circuit). No
  observable regression; the deferral is honest.

### What does NOT ship

- No curved-chart math (MANI-I.9). The AOV writes
  the world-space hit position as the documented
  identity / neutral diagnostic; future MANI-I.9+
  slices add chart-specific logic above this write
  arm.
- No second-tier AOV (e.g. curvature scalar) — that
  lands with the Field Interpretation Layer's
  FIELD.3 once a curved chart exists. The
  `ManifoldCoordinates` AOV is intentionally just
  the chart-coordinate hit position.
- No per-step coordinate trace (geodesic-history
  AOV); deferred to a possible future addendum.
- No OptiX-side host allocation; see "Risks &
  mitigations" above.

---

## 8. MANI-I.8 — Schwarzschild-like artistic coordinate remap

### Goal

First non-trivial chart. Implements a Schwarzschild-LIKE
coordinate remap as a **closed-form artistic transform** —
not a physical Schwarzschild metric, not a geodesic
integrator. The chart bends primary-ray directions around
a configured "mass" centre, producing a visually plausible
gravitational-lensing effect.

Master rule #3 is preserved: the chart is real, complete,
and tested against its own analytic reference (the
artistic remap formula, not a physical-Schwarzschild
geodesic), but it is **explicitly not** claimed to be
physically exact (architecture-doc §8 non-goal).

### Prerequisites

- MANI-I.7 — debug coordinate-warp AOV available for
  visual sanity checks.

### What ships

- **`src/manifold/SchwarzschildLikeChart.h` (new)** with:
  - a configuration struct extending the
    `CoordinateChartParameters` placeholder slots from
    MANIFOLD.1 — specifically a `mass` parameter
    interpreted as a Schwarzschild-like radius in chart
    units;
  - a `transform_ray_like_direction(SchwarzschildLikeChart, Vec3)`
    overload that applies the artistic remap formula
    (a closed-form expression chosen for visual
    plausibility — exact form documented in the slice's
    header preamble);
  - the `Vec4` overload mirroring the Vec3 form;
  - a `domain_predicate` helper that flags rays passing
    through the Schwarzschild radius (these are
    "absorbed" — the chart's analog of an event-horizon
    proxy, again artistic).
- **CUDA / OptiX kernel branches**: the chart-aware seam
  added in MANI-I.5 branches on `transform.chart.type`;
  the `SchwarzschildLike` branch calls the new overload.
- **A new test fixture**: an `--render-scene` with a
  single bright sphere offset from the optical axis,
  rendered with and without the chart engaged. The
  without-flag image is bit-identical to the
  pre-MANI-I.8 baseline; the with-flag image is the new
  reference image pinning the chart's visual signature.
- **A new audit doc** `docs/MANI_I_5_SCHWARZSCHILD_AUDIT.md`
  with the closed-form remap formula, the visual
  acceptance gate, and the bit-identity verification of
  the off-path.
- **`docs/BUILD_PLAN.md`** gets a MANI-I.8 entry.

### Acceptance

- Audit-host build green; ctest 12/12.
- The off-path (default `ManifoldMode{}`, or
  `--manifold-mode Euclidean`) is bit-identical to the
  pre-MANI-I.8 baseline on all the actions enumerated in
  the MANI-I.5 acceptance section.
- The on-path
  (`--manifold-mode SchwarzschildLike`,
  `--manifold-strength 1.0`, configured mass)
  produces an image that:
  - has a documented "deflection angle" at a fixed
    impact-parameter pixel — measured directly from the
    rendered PPM — that matches the remap formula's
    closed-form expectation to within ~1 pixel;
  - exhibits the expected shadow / horizon-proxy region
    at the configured Schwarzschild radius;
  - reduces to the Euclidean image when
    `--manifold-strength 0` is passed (interpolation
    invariant).
- The `--manifold-debug-warp` AOV on the on-path shows
  the expected chart-space warp around the mass centre
  (matches the closed-form remap's signature in the AOV).

### Risks & mitigations

- **Visual plausibility vs physical correctness**
  confusion. Mitigated by the `*Like` naming convention
  (MANIFOLD.1) and by an explicit non-goal note in the
  audit doc — and in the CLI `--help` text — that the
  chart is artistic, not physical.
- **Performance regression** if the curved-chart branch
  fires per-pixel even when off. Mitigated by branching on
  `ManifoldMode::enabled` and `chart_type == Euclidean`
  *before* entering the chart-aware seam; the Euclidean
  fast path is the existing pre-pivot code path.
- **Numerical singularity** at the configured mass centre.
  Mitigated by the horizon-proxy `domain_predicate`
  absorbing rays inside the singularity radius
  (analytical safety net).

### What does NOT ship

- No physical Schwarzschild metric. No geodesic
  integrator. Architecture-doc §8 non-goal "physically
  exact Kerr ray tracing" applies one tier up to this
  one too — the Schwarzschild-like artistic remap is
  inspired by the Schwarzschild diagram but does not
  claim to ray-trace null geodesics of the Schwarzschild
  metric.
- No `KruskalLikePlaceholder` chart. The Kruskal-Szekeres
  chart family in the architecture doc §5.3 is
  *reserved-but-inert*; activating it via
  `--manifold-mode KruskalLikePlaceholder` continues to
  fall through to the Euclidean fast path (and emits a
  warning in the renderer's launch log).
- No path-tracer geometry change. The chart only remaps
  primary-ray directions; bounce rays, BSDFs, NEE, MIS,
  and the firefly clamp remain pre-pivot.

---

## 9. MANI-I.9 — Penrose-like compactification visualization

### Goal

Second non-trivial chart. Implements a Penrose-LIKE
conformal compactification — mapping asymptotic infinity
onto a finite boundary via a `tanh`-style coordinate
compression. The chart is intended for **diagrammatic**
visualisation modes: scientific / pedagogical renders
where the artist wants the whole asymptotic structure
visible in a single frame, not for production beauty
passes.

Same master-rule honesty as MANI-I.8: the chart is real,
complete, and tested against its own closed-form
reference, but not claimed to be physically exact.

### Prerequisites

- MANI-I.8 — the chart-aware seam is already curved-chart-
  capable; MANI-I.9 only adds a new chart branch.

### What ships

- **`src/manifold/PenroseLikeChart.h` (new)** with:
  - a configuration struct using the
    `compactification_scale` parameter from MANIFOLD.1's
    `CoordinateChartParameters`;
  - a `transform_ray_like_direction(PenroseLikeChart, Vec3)`
    overload applying a `tanh`-style conformal
    compactification along the spatial axes;
  - the `Vec4` overload mirroring the Vec3 form;
  - documented behaviour at the compactification
    boundary (rays approaching infinity are mapped to a
    finite boundary set at the chart's edge).
- **CUDA / OptiX kernel branches**: the `PenroseLike`
  branch of the chart-aware seam calls the new overload.
- **A new test fixture**: an `--render-scene` with a
  field of distant spheres extending toward infinity;
  rendered with the Penrose chart engaged at multiple
  `compactification_scale` values to verify the boundary
  compression behaves correctly.
- **A new audit doc**
  `docs/MANI_I_6_PENROSE_AUDIT.md` with the
  compactification formula, the boundary-mapping
  acceptance gate, and the bit-identity verification of
  the off-path.
- **`docs/BUILD_PLAN.md`** gets a MANI-I.9 entry.

### Acceptance

- Audit-host build green; ctest 12/12.
- The off-path (default `ManifoldMode{}`) is
  bit-identical to the pre-MANI-I.9 baseline on every
  CLI action enumerated in the MANI-I.5 acceptance
  section, **plus** the MANI-I.8 reference images
  (i.e. running `--manifold-mode SchwarzschildLike`
  still produces the MANI-I.8 image bit-for-bit).
- The on-path (`--manifold-mode PenroseLikePlaceholder` —
  but reading "PenroseLike", which we may rename in this
  slice; see Risks) produces an image whose
  boundary-coordinate values match the closed-form
  `tanh(...)` mapping to within `1e-5f` along a
  representative radial scan.
- The `--manifold-debug-warp` AOV on the on-path shows
  the expected compactification compression toward the
  edges.

### Risks & mitigations

- **Enum-name evolution**: MANIFOLD.1's enum has
  `PenroseLikePlaceholder` ("placeholder" reflecting
  that no concrete chart was wired). When the chart is
  wired in this slice the "Placeholder" suffix becomes
  inaccurate. Mitigated by renaming the enumerator to
  `PenroseLike` in this slice **and** keeping the old
  name as a typed alias for one slice (with a deprecation
  log) to avoid breaking any in-flight callers.
- **Conformal compactification distortion at the
  boundary** producing visually unreadable images.
  Mitigated by the artist-facing
  `compactification_scale` parameter and by a documented
  default that produces a sensible boundary at
  `tanh(scale * |r|) ≈ ±0.95` for typical scene scales.
- **Performance regression** on the off-path same as
  MANI-I.8; same mitigation.

### What does NOT ship

- No physical Penrose diagram. Architecture-doc §8
  non-goals stand: the Penrose-like chart is a
  visualisation aid, not a calculation of any spacetime's
  actual Penrose diagram.
- No Kerr-like chart (`KerrLikePlaceholder` stays
  reserved-but-inert).
- No combination with `SchwarzschildLike` in a single
  render — only one chart is active per render. Chart
  composition (e.g. Penrose-compactified Schwarzschild)
  is a future addendum if artists request it.

---

## 10. MANI-I.10 — audit

### Goal

Cross-host runtime audit pinning every invariant the
preceding slices introduced. Single audit doc; no source
artifacts; ctest set widens only if the audit finds a
gap. The audit lives in `docs/MANI_I_7_AUDIT.md` and is
the merge gate for the whole MANI-I.* programme.

### Prerequisites

- MANI-I.1 through MANI-I.9 — all six prior slices green
  on the audit host.

### What ships

- **`docs/MANI_I_7_AUDIT.md` (new)** containing:
  - a CUDA + OptiX-SDK host runtime checklist exercising
    every CLI flag combination
    (`--manifold-mode * × --manifold-strength * ×
    --manifold-debug-warp`);
  - per-slice acceptance regression: every reference
    image pinned by MANI-I.8 / MANI-I.9 reproduces
    bit-for-bit; every pre-pivot reference image is
    untouched by the off-path of every slice;
  - launch-params layout audit: the
    `OptixLaunchParams` / CUDA launch-params struct
    grew by exactly the documented offsets; no existing
    field was reordered or resized;
  - AOV-buffer layout audit: the `ManifoldWarp` AOV
    slot is the last entry in the AOV enum; no existing
    slot is reordered;
  - log-output audit: the manifold log line introduced
    in MANI-I.3 fires exactly once per render and only
    once;
  - performance audit: the off-path
    (default `ManifoldMode{}`) per-pixel cost on a
    representative scene is within `5%` of the
    pre-pivot baseline (measured via the existing
    `GpuTiming` instrumentation).
- **`docs/BUILD_PLAN.md`** gets a MANI-I.10 entry.

### Acceptance

- All audit items in the doc check off green on a
  CUDA + OptiX-SDK host.
- Audit-host build green; ctest 12/12 (no new binary
  unless the audit reveals a gap).
- The audit doc is signed off by the operator before
  any further FIELD.x kernel slice (FIELD.3 Kretschmann
  diagnostic AOV onwards) is started — that is the
  audit's "merge gate" role.

### Risks & mitigations

- **Audit-host unavailability**: the audit needs a CUDA
  + OptiX-SDK host. Mitigated by maintaining the
  existing audit-host fallback (no CUDA, no OptiX SDK)
  for the bit-identity portions of the audit; the
  runtime-pinned items are documented as "DEFERRED"
  until the audit-host run.
- **Drift between audit doc and reality**: same
  mitigation the existing `docs/STAGE_*_AUDIT.md` set
  uses — the audit doc is regenerated, not edited
  in-place, when the underlying behaviour changes.

### What does NOT ship

- No source change. The audit is a regression-and-
  invariant document; if it finds a gap, the gap is
  filled by a follow-up slice (`MANI-I.10.N`), not by
  this slice.
- No new test binary unless gap-driven; the existing
  ctest set is expected to cover the audit invariants
  with the assertion expansions added across MANI-I.1
  through MANI-I.9.

---

## 11. Non-goals for the whole plan

The MANI-I.* programme deliberately does **not**:

- Implement a geodesic integrator. The Schwarzschild-like
  and Penrose-like charts ship as closed-form coordinate
  remaps; null-geodesic integration is a later programme
  beyond this plan.
- Replace the existing CUDA / OptiX path tracer. The
  chart-aware seam (architecture-doc §7.1) is the **only**
  hook into the existing renderer; the BSDF / NEE / MIS /
  RR / firefly-clamp / progressive-accumulation /
  denoising machinery is byte-identical at every slice
  boundary.
- Promote the manifold module to "production ready" in
  `docs/MODULE_MAP.md`. That promotion is the operator's
  call; this plan is the operational sequence the
  operator follows before approving the promotion.
- Ship a CLI / scene-file pipeline for the Field
  Interpretation Layer. FIELD.3+ slices land separately,
  reading the curved-chart surface MANI-I.8 / MANI-I.9
  expose.
- Re-architect the relativistic camera model. The
  existing `src/relativity/` helpers continue to feed
  the kernel via the MANIFOLD.3 observer-frame bridge
  (architecture-doc §7.2 subsumption).
- Touch the Cinema 4D bridge (master order #21) or any
  authoring tool. Those modules consume the renderer's
  scene format and server protocol; the MANI-I.* slices
  widen those contracts only through the CLI and
  config-struct extensions documented per slice.

---

## 12. References

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` — top-
  level rules.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` — the layered
  architecture this plan implements (§3 ontology, §5
  Phase 2, §7 chart-aware seam, §8 non-goals, §10
  milestone order, §11 references).
- `docs/FIELD_INTERPRETATION_LAYER.md` — Phase 1 sibling;
  §9 kernel slices wait on MANI-I.8 / MANI-I.9.
- `docs/MASTER_ARCHITECTURE.md` — long-term project
  architecture.
- `docs/BUILD_PLAN.md` — per-slice implementation status;
  receives a "Manifold Integration Plan" stage entry
  alongside this artifact.
- `docs/MODULE_MAP.md` — per-module ownership rules;
  unchanged by this plan but will be updated as MANI-I.*
  slices land.
- `docs/DEVELOPMENT_RULES.md` — engineering, dependency,
  GPU, process rules.
- `src/manifold/*` — the POD surface every MANI-I.*
  slice consumes
  (`CoordinateChart.h`, `MetricTensor.h`,
  `ObserverFrame.h`, `GeodesicState.h`,
  `ManifoldTransform.h`, `ManifoldMode.h`).
- `src/relativity/RelativityMath.h`,
  `src/relativity/RelativityParams.h` — the legacy SR
  helpers the kernel continues to consume via the
  MANIFOLD.3 bridge.
- `src/scene/RenderSettings.h` — the renderer's config
  surface MANI-I.3 extends.
- `src/core/CommandLine.cpp` — the CLI parser MANI-I.1
  extends.
- `src/renderer/AOV.h`, `src/renderer/GpuAOVBuffer.h` —
  the AOV plumbing MANI-I.7 extends.
- `tests/manifold_identity_tests.cpp` — the
  112-assertion default-no-op anchor every MANI-I.*
  slice must continue to satisfy.

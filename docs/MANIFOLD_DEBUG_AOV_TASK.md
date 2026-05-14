# Manifold Debug AOV — Task Definition (MANI-I.7)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Mode:   Documentation only. No source code is touched by
        this task definition; the implementation lands in a
        subsequent slice that consumes this doc as its
        canonical brief.

This document defines the work for **MANI-I.7 — debug
coordinate-warp AOV** under the renumbered integration plan
(`docs/MANIFOLD_INTEGRATION_PLAN.md` §7). It is the
operator-facing brief the implementation slice will read to
decide the exact surface, the acceptance gates, and the
non-goals.

Prerequisite slices already green:

- **MANI-I.1** — CLI config only (`1bb1fb4`).
- **MANI-I.2** — CLI Config Audit (`799a9ac`).
- **MANI-I.3** — Render Config Bridge (`d677b2c`).
- **MANI-I.4** — Render Config Bridge Audit (`4c73d1b`).
- **MANI-I.5** — Euclidean Identity GPU Path (`a34e265`).
- **MANI-I.6** — Euclidean Identity GPU Path Audit
  (`fef3e50`).

---

## 1. Exact goal

**Add a single optional diagnostic AOV that visualises
the per-pixel effect of the active manifold mode on the
primary ray's chart-space hit position.**

The AOV writes a 3-component (Vec3) per-pixel value to a
dedicated render pass. The AOV is *optional*: it is only
allocated and written when the operator requests it. The
beauty pass and every existing AOV (Beauty / Normal /
Depth / Albedo / DopplerFactor / SearchlightFactor) are
byte-identical to the pre-MANI-I.7 baseline regardless of
whether the new AOV is requested.

On the Euclidean chart with `scale = 1.0` and `origin =
(0, 0, 0)` (the disabled / pre-pivot default) the AOV's
per-pixel value equals the world-space hit position
to within single-precision FP tolerance — a documented
identity / neutral visualisation. Future curved-chart
slices (MANI-I.8 Schwarzschild-like, MANI-I.9
Penrose-like) make the AOV visually divergent from
world-space hit positions in a way that lets an operator
*see* the chart's coordinate deformation per pixel.

---

## 2. Expected behaviour

The implementation slice must satisfy three load-bearing
behavioural invariants:

### 2.1 Beauty output unchanged

Every existing CLI action — `--render-pathtrace`,
`--render-optix-pathtrace`, `--render-scene`,
`--render-mesh-scene`, `--render-material-scene`,
`--render-direct-lighting`, `--render-aovs`,
`--render-relativistic`, `--render-aovs --denoise`, and
every diagnostic render — produces pixel-bit-identical
beauty output to the pre-MANI-I.7 baseline regardless of:

- whether the new AOV is requested;
- the operator's choice of `--manifold-chart` value;
- the operator's choice of `--manifold-strength` value;
- the operator's `--manifold-enable` / `--manifold-debug`
  state.

The new AOV writes ONLY to its dedicated per-pass
framebuffer. The Beauty pass kernel arithmetic stays
unchanged.

### 2.2 Debug AOV only active when requested

The new AOV slot is gated on TWO conditions, both of
which must hold for the AOV pass's device buffer to be
allocated and the kernel arm to fire:

1. The operator passes `--render-aovs` (the existing
   AOV-aware action that already enumerates Beauty /
   Normal / Depth / Albedo / DopplerFactor /
   SearchlightFactor outputs).
2. The operator passes `--manifold-debug` (the MANI-I.1
   modifier flag that sets `ManifoldMode::debug_-
   visualization = true`).

Either gate by itself produces no new file. Both
together cause the renderer to allocate the new
per-pass device buffer, fill it from the kernel, and
save the resulting PPM alongside the existing six AOV
PPMs.

A separate dedicated CLI action
(`--render-manifold-debug-aov` or similar) is NOT
shipped at MANI-I.7. The two-flag composition above is
the only entry point.

### 2.3 Disabled / Euclidean mode produces identity / neutral visualisation

When the new AOV pass is requested but `ManifoldMode`
is in its disabled or Euclidean default state, the
per-pixel value the AOV writes is the **identity /
neutral** value for the visualisation channel chosen
(see §3 below). The identity value is documented and
verifiable against a closed-form reference at every
pixel:

- For the `manifoldCoordinates` channel: the
  per-pixel value equals the world-space hit position
  `(hit.x, hit.y, hit.z)`. On miss, the value is
  `(0, 0, 0)` (matches the existing Normal AOV's
  miss-pixel convention).
- The check is verifiable by `cmp`-ing the AOV PPM
  against a reference image pinned at the
  implementation slice; the reference is generated
  once on a CUDA + OptiX-SDK host and lives in the
  ctest `goldens/` set.

---

## 3. AOV naming proposal

The task brief offers three candidate AOV names:

### 3.1 `manifoldCoordinates` (RECOMMENDED for MANI-I.7)

- **Component count:** 3 floats / pixel (Vec3).
- **Encoding:** per-pixel chart-space hit position
  `(chart_pos.x, chart_pos.y, chart_pos.z)` from
  `world_to_chart(manifold_transform, world_hit)`.
- **Euclidean identity:** equals the world-space hit
  position component-wise (the Euclidean chart's
  `world_to_chart` is the identity map at `origin =
  0`, `scale = 1`).
- **Curved-chart visualisation:** on a Schwarzschild-
  like or Penrose-like chart, the AOV's pixel values
  diverge from world-space hit positions in a way
  that makes the chart's coordinate remap visible at
  a glance (e.g. the radial compression near a
  configured Schwarzschild radius).
- **Why recommended:** the most informative single
  channel for "what is the chart doing to this
  pixel". The other two candidate channels are
  derivable from this one post-process (warp
  magnitude = norm of difference vs world-space hit
  position; chart-type info is small and can ride
  in a sidecar log line or a separate AOV slot).

### 3.2 `manifoldWarpMagnitude` (FUTURE)

- **Component count:** 1 float / pixel.
- **Encoding:** per-pixel `|chart_pos - world_pos|`
  (Euclidean norm).
- **Euclidean identity:** `0.0f` everywhere.
- **Trade-off:** smaller payload (1 channel vs 3),
  cleaner visual signal of "where is warp
  happening", but loses the directional information
  the `manifoldCoordinates` channel carries.
- **Recommendation:** defer to a follow-up slice
  (call it MANI-I.7.1 or roll it into MANI-I.9's
  Penrose-like work, where the warp magnitude is
  most visually striking near the conformal
  boundary).

### 3.3 `manifoldChartDiagnostic` (FUTURE)

- **Component count:** 1 float / pixel (or 3 floats;
  TBD at implementation time).
- **Encoding:** chart-specific diagnostic per pixel.
  Examples a future implementation might choose:
  - the active chart's enum index normalised to
    `[0, 1]` (useful only for multi-chart scenes —
    none today);
  - per-pixel curvature scalar (Kretschmann scalar
    `K = R_{μνρσ} R^{μνρσ}`) — this would be the
    Field-Interpretation-Layer §6 surface and
    overlaps with the FIELD.3 milestone, so MANI-I.7
    should NOT pre-empt it;
  - per-pixel "is the ray inside the configured
    horizon proxy" boolean.
- **Trade-off:** the encoding choice is chart-
  specific and best made when the first non-trivial
  chart lands. Premature standardisation would
  either constrain the future curved-chart slices or
  underspecify the channel.
- **Recommendation:** defer to MANI-I.8 / MANI-I.9
  (whichever curved chart needs a chart-specific
  diagnostic first).

### 3.4 Final naming decision

**MANI-I.7 ships `manifoldCoordinates` only.** The
implementation slice adds a single new `AOVType::ManifoldCoordinates`
enumerator + the matching helpers, factory, launch-
params field, kernel arm, and `--render-aovs` PPM
output. The other two candidate channels are named in
this doc as forward-looking placeholders; their
implementation is gated behind future MANI-I.* slices
that have a concrete consumer.

The integration plan §7 chain-diagram box's `ManifoldWarp`
label (preserved across the MANI-I.6 renumbering) is the
informal shorthand for the new AOV; the canonical name
the implementation slice will use is `ManifoldCoordinates`
(matching the existing `DopplerFactor` /
`SearchlightFactor` PascalCase convention).

---

## 4. Files likely involved

The implementation slice is expected to touch the
following files (host + CUDA + OptiX). Numbers in
parentheses are rough net-line estimates from
comparable past slices.

| Layer | File | Why |
|-------|------|-----|
| AOV data model | `src/renderer/AOV.h` (+15) | New `AOVType::ManifoldCoordinates` enumerator + `make_manifold_coordinates(...)` factory. |
| AOV data model | `src/renderer/AOV.cpp` (+15) | `aov_component_count` → 3 for the new type; `aov_type_name` → `"manifold_coordinates"`; factory body. |
| GPU AOV buffer | `src/renderer/GpuAOVBuffer.{h,cpp}` (+5) | New entry in the device-buffer-owner switch if it dispatches by type; otherwise zero. |
| OptiX launch params | `src/optix/OptixLaunchParams.h` (+10) | New trailing `float* aov_manifold_coordinates = nullptr;` field, with the same null-means-skip doc-comment as the existing six AOV slots. |
| OptiX kernel | `src/optix/OptixPrograms.cu` (+20) | Closest-hit + miss arms write to the new pointer when non-null. The closest-hit's per-pixel write is `world_to_chart(manifold_transform_from_launch_params, hit_position)`; miss writes `(0, 0, 0)`. |
| OptiX renderer | `src/optix/OptixRenderer.cpp` (+30) | Allocate the per-pass device buffer when the AOV is requested; pass the pointer through `OptixLaunchParams`; download + save PPM at the end of `--render-aovs`. |
| CUDA kernel | `src/cuda/CudaRenderer.cu` (+20) | Same closest-hit + miss arm changes as OptiX. The host launcher in `CudaPathTracer.cu` is **not** the target — `--render-aovs` goes through `CudaRenderer`, not `CudaPathTracer`. |
| CUDA host | `src/cuda/CudaRenderer.cu` host functions (+20) | Allocate the buffer; thread the pointer through; download + save PPM. |
| CLI | `src/main.cpp` (+15) | `--render-aovs` dispatcher (CUDA + OptiX paths) honours the `--manifold-debug` gate; emits `output/aov_manifold_coordinates.ppm` (CUDA) / `output/optix_aov_manifold_coordinates.ppm` (OptiX) when both conditions hold. |
| Tests | `tests/renderer_tests.cpp` (+30) | Host-side: assertion that `AOVType::ManifoldCoordinates` has `component_count == 3` and `name == "manifold_coordinates"`; assertion that `make_manifold_coordinates` produces a valid `AOV`. |
| Docs | `docs/MANIFOLD_INTEGRATION_PLAN.md` §7 (LANDED-update on slice merge) | Rewrite §7 with the actual landed surface (mirrors what MANI-I.5 did to §6). |
| Docs | `docs/BUILD_PLAN.md` | MANI-I.7 entry. |
| CMake | none expected | The new AOV uses the existing `rr_renderer` library wiring; `rr_optix` / `rr_gpu` already link `rr_manifold` since MANI-I.5. |

---

## 5. What must not be touched

Per master rule #3 and the integration plan §2
bit-identity invariant, the implementation slice MUST
NOT:

- **Modify the Beauty pass kernel arithmetic.** The
  existing closest-hit / miss / raygen programs'
  shading code paths stay unchanged. The new AOV
  write is gated behind a `if (params.aov_manifold_coordinates
  != nullptr)` check that the existing kernel arms
  already use for the six pre-existing AOV slots.
- **Modify the existing six AOV slots' layouts** or
  their `AOVType` enumerator values. The new
  enumerator MUST be appended at the END of the
  `AOVType` enum to preserve every pre-MANI-I.7 value.
  The existing six slots' kernel write paths stay
  bit-identical.
- **Touch the `.rrscene` scene-file format.** No
  parser change, no writer change, no schema bump.
  The new AOV is request-gated by CLI flags, not
  scene-file metadata. The file-format hookup is a
  separate later slice.
- **Touch `src/server/`, `bridges/`, or `tools/`
  (other than the existing `verify_cuda_host.py`).**
  No C4D / server / UI / node-editor surface change.
- **Add a new `--render-*` action.** The two-flag
  composition (`--render-aovs --manifold-debug`)
  is the entry point. A new action would create CLI
  surface duplication.
- **Modify the OptiX denoiser path.** The denoiser
  consumes Beauty / Albedo / Normal only; it must
  continue to do so. The new AOV slot is denoiser-
  ignored.
- **Modify `OptixLaunchParams` field offsets that
  predate MANI-I.5.** The new `aov_manifold_coordinates`
  pointer field is appended at the END of the POD
  (immediately after `aov_searchlight_factor`).
- **Change the existing `--render-aovs` PPM
  filenames or the existing `output/aov_*.ppm`
  set's enumeration.** The new file is *additional*,
  not a replacement.

---

## 6. PASS criteria

The implementation slice's acceptance gate is satisfied
when ALL of the following hold:

### 6.1 Structural

- [ ] `AOVType::ManifoldCoordinates` enumerator exists
      at the end of the `AOVType` enum (value `= 6`).
- [ ] `aov_component_count(AOVType::ManifoldCoordinates)
      == 3`.
- [ ] `aov_type_name(AOVType::ManifoldCoordinates) ==
      "manifold_coordinates"`.
- [ ] `AOV::make_manifold_coordinates(...)` factory
      exists and produces a well-formed `AOV` with
      `type() == ManifoldCoordinates` and
      `name() == "manifold_coordinates"` (or the
      caller-supplied name).
- [ ] `OptixLaunchParams::aov_manifold_coordinates =
      nullptr` field exists at the end of the POD.
- [ ] OptiX device-side programs (`__closesthit__` /
      `__miss__` for the AOV-aware ray types) gate
      writes on `aov_manifold_coordinates != nullptr`.
- [ ] CUDA `CudaRenderer.cu` AOV-aware kernels do
      the same (closest-hit + miss arms gated).
- [ ] `--render-aovs --manifold-debug` emits
      `output/aov_manifold_coordinates.ppm` (CUDA
      path) and `output/optix_aov_manifold_coordinates.ppm`
      (OptiX path) alongside the existing six AOV
      PPMs.

### 6.2 Behavioural

- [ ] `--render-aovs` **without** `--manifold-debug`
      emits exactly the same six AOV PPMs it emitted
      pre-MANI-I.7 (no new file, no missing file, no
      changed file).
- [ ] Beauty output of every existing CLI action is
      pixel-bit-identical to the pre-MANI-I.7
      baseline.
- [ ] The six existing AOV PPMs (Beauty / Normal /
      Depth / Albedo / DopplerFactor /
      SearchlightFactor) are pixel-bit-identical to
      the pre-MANI-I.7 baseline for every existing
      `--render-aovs` invocation.
- [ ] On the Euclidean default chart (`disabled_manifold_mode()`),
      `aov_manifold_coordinates.ppm`'s per-pixel
      value matches the world-space hit position to
      within `1.0e-5f` per channel for at least 95%
      of pixels (the remaining 5% tolerance covers
      hit-misses at edge-of-frame anti-aliasing and
      single-precision FP rounding through
      `world_to_chart`).
- [ ] On miss pixels, `aov_manifold_coordinates`
      writes `(0, 0, 0)` (matches the Normal AOV's
      miss convention).

### 6.3 Test surface

- [ ] `ctest` reports `12/12 passed` on the audit-
      host build.
- [ ] `cli_tests` reports `123/123 passed` (no
      parser change this slice).
- [ ] `renderer_tests` reports its pre-MANI-I.7
      assertion count + at least 4 new MANI-I.7
      assertions covering: enum value,
      `aov_component_count`, `aov_type_name`,
      factory output.
- [ ] A `g++ -std=c++20 -Isrc -Wall -Wextra -Werror`
      standalone build of an
      `AOV::make_manifold_coordinates`-consuming TU
      compiles cleanly.

### 6.4 Documentation

- [ ] `docs/MANIFOLD_INTEGRATION_PLAN.md` §7
      rewritten with the landed-surface description
      (mirrors what MANI-I.5 did to §6).
- [ ] `docs/BUILD_PLAN.md` MANI-I.7 entry added.
- [ ] The integration plan §3 chain diagram's
      MANI-I.7 box is updated to cite the actual
      AOV name (`ManifoldCoordinates` rather than
      the placeholder `ManifoldWarp`).

---

## 7. Runtime-deferred CUDA / OptiX checks

The audit-host build (no CUDA, no OptiX SDK) cannot
directly verify the AOV's pixel content. The runtime
checks below are DEFERRED behind the audit host's
existing no-CUDA / no-OptiX-SDK fallback, matching
the existing `firefly_clamp` / `enable_nee` /
MANI-I.5 byte-identity claims' posture (per
`docs/STAGE_19_DENOISER_AUDIT.md` Q1 / Q2 rubric).

Each deferred check must be exercised on a CUDA +
OptiX-SDK host before the final cross-host audit
(MANI-I.10) closes the MANI-I.* programme:

### 7.1 Identity on the Euclidean chart (CUDA path)

Run:
```
RelativityRender --render-aovs --manifold-debug
                 scenes/test_full_scene.rrscene
```

Verify:
- `output/aov_manifold_coordinates.ppm` exists.
- For at least one hit pixel `(x, y)` with a
  known world-space hit position `p_world`, the
  pixel's RGB value (decoded from the PPM's
  encoding) matches `p_world` component-wise to
  within `1.0e-5f`.
- `output/aov_beauty.ppm` is byte-identical to
  the pre-MANI-I.7 reference (a pinned PPM in
  `tests/goldens/`).

### 7.2 Identity on the Euclidean chart (OptiX path)

Run:
```
RelativityRender --render-aovs --manifold-debug
                 scenes/test_full_scene.rrscene
                 --use-optix
```

(The `--use-optix` switch is hypothetical for
`--render-aovs`; the actual flag may differ. The
implementation slice picks the canonical OptiX
invocation per the existing `--render-optix-aovs`
action's convention.)

Verify:
- `output/optix_aov_manifold_coordinates.ppm`
  exists.
- The CUDA-side and OptiX-side
  `aov_manifold_coordinates.ppm` files are
  pixel-bit-identical (the chart-space hit
  position is computed by the same RR_HD inline
  helper on both backends).
- `output/optix_aov_beauty.ppm` is byte-identical
  to the pre-MANI-I.7 reference.

### 7.3 Off-path bit-identity (both backends)

Run:
```
RelativityRender --render-aovs
                 scenes/test_full_scene.rrscene
```
(WITHOUT `--manifold-debug`)

Verify:
- Exactly six AOV PPMs are produced; no
  `manifold_coordinates.ppm` is emitted.
- All six PPMs are byte-identical to the
  pre-MANI-I.7 reference.

### 7.4 Off-chart bit-identity

Run:
```
RelativityRender --render-pathtrace
                 scenes/test_relativity.rrscene
                 --manifold-enable
                 --manifold-chart schwarzschild-like
                 --manifold-strength 0.5
                 --manifold-debug
```

Verify:
- `output/pathtrace_spp_16.ppm`'s beauty output
  is byte-identical to the pre-MANI-I.7 baseline
  for the same scene + same seed (the kernel
  arithmetic for the beauty pass remains
  unchanged; only the AOV write path consumes
  manifold state, and `--render-pathtrace` does
  not write AOVs at all today).
- No `manifold_coordinates.ppm` is emitted by
  `--render-pathtrace` (that action does not
  write AOVs; `--manifold-debug` has no
  observable AOV effect there).

### 7.5 Denoiser-path no-regression

Run:
```
RelativityRender --render-aovs --denoise
                 scenes/test_full_scene.rrscene
                 --manifold-debug
```

Verify:
- `output/denoised.ppm` is byte-identical to the
  pre-MANI-I.7 denoised reference.
- The denoiser continues to consume Beauty /
  Albedo / Normal only — it does NOT consume the
  new `manifold_coordinates` AOV. No new error
  message, no new denoiser failure.

---

## Cross-references

- `docs/MANIFOLD_INTEGRATION_PLAN.md` §7 — the
  slice section this task definition implements.
- `docs/MANIFOLD_EUCLIDEAN_GPU_IDENTITY_AUDIT.md` —
  the immediately preceding audit verdict that
  authorised proceeding to MANI-I.7.
- `src/renderer/AOV.h` — the existing AOV data
  model the new enumerator extends.
- `src/optix/OptixLaunchParams.h` — the existing
  six `aov_*` slots whose appended-pointer pattern
  the new slot mirrors.
- `docs/STAGE_14_AOV_AUDIT.md` — the original
  AOV-system audit; documents the
  `if (params.aov_X != nullptr)` gate pattern.
- `docs/STAGE_19_DENOISER_AUDIT.md` Q1 / Q2 — the
  audit-host runtime-deferral rubric this task
  inherits.

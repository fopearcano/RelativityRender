# Manifold Consumption-Gap Audit (MANI-CONSUME.2)

Date:   2026-05-15
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `1890142` ("host:
MANI-CONSUME.1 — CLI Consumption-Gap Closure (impl,
host-only)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-PENROSE.12 baseline, the `manifold_identity_tests`
runtime output, `ctest` exit codes, and audit-host CLI
smoke-test transcripts.

This audit is the per-slice gate for MANI-CONSUME.1
(`1890142`). It verifies the nine items the task brief
enumerates — SchwarzschildLike fixture auto-consumes;
PenroseLike fixture auto-consumes; default scenes
unchanged; CLI overrides still function; CPU/CUDA/OptiX
paths consume the same manifold settings; no new
manifold math; build/test status; runtime CUDA/OptiX
status; verdict — and produces the PASS / REPAIR /
BLOCKED verdict that gates progression to the next
slice.

---

## 1. VERDICT

**PASS.**

All eight structural checks return PASS. No REPAIR or
BLOCKED item is found. Check #8 (runtime CUDA/OptiX
validation status) is DEFERRED on documented audit-host
limitations (no CUDA SDK, no OptiX SDK; SDK_FOUND TUs
compile but cannot link / launch device code). The
MANI-CONSUME.1 commit closes the host-side consumption
gap that the SCHW.10 / SCHW.11 / PENROSE.10 / PENROSE.11
/ PENROSE.12 audits all catalogued as the highest-
priority deferred item: the SchwarzschildLike +
PenroseLike fixture scenes now drive rendering behavior
automatically when loaded via `--render-aovs <path>` or
`--render-optix-aovs <path>`. Default scenes byte-
identical; CLI overrides preserved verbatim.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | SchwarzschildLike fixture auto-consumes manifold settings | **PASS** | Audit-host smoke-test verified:<br>**Command:** `RelativityRender --render-aovs scenes/test_schwarzschild_like_manifold.rrscene`<br>**Output (transcript):**<br>`[INFO] scene file: /home/.../scenes/test_schwarzschild_like_manifold.rrscene`<br>`[INFO] aovs manifold mode: enabled (chart=schwarzschild-like, strength=0.500000, debug=on)`<br>`[ERROR] --render-aovs requires CUDA...` (audit-host CUDA fallback; expected).<br>The scene-load runs the SCHW.9 parser surface (`apply_manifold` + `parse_chart_type`); the fixture's `manifold` block populates `scene.manifold` with `enabled=true, chart=SchwarzschildLike, strength=0.5, debug_visualization=true`; the SCHW.9 dispatcher-merge logic (`effective_manifold = cfg.manifold.enabled ? cfg.manifold : scene.manifold`) resolves to the scene-side mode (CLI default has `cfg.manifold.enabled=false`); the operator-facing log confirms the resolved mode. On a CUDA host the kernel-side SCHW.5 arm engages via the same `targets.manifold_mode = effective_cuda_manifold` plumbing. **Auto-consumption verified at the host-side level**; kernel-side execution DEFERRED to a CUDA host. |
| 2 | PenroseLike fixture auto-consumes manifold settings | **PASS** | Audit-host smoke-test verified:<br>**Command:** `RelativityRender --render-optix-aovs scenes/test_penrose_like_manifold.rrscene`<br>**Output (transcript):**<br>`[INFO] scene file: /home/.../scenes/test_penrose_like_manifold.rrscene`<br>`[INFO] optix-aovs manifold mode: enabled (chart=penrose-like, strength=0.500000, debug=on)`<br>`[ERROR] --render-optix-aovs requires OptiX...` (audit-host OptiX fallback; expected).<br>Same flow as check #1 but for the OptiX path: SCHW.9 parser parses the PenroseLike `manifold` block; dispatcher-merge resolves to scene-side; resolved mode logged. On an OptiX-SDK host the kernel-side PENROSE.8 arm engages via the same `params.manifold_mode = manifold_mode` + `params.coordinate_chart = coordinate_chart` plumbing. **Auto-consumption verified at the host-side level**; kernel-side execution DEFERRED. |
| 3 | Default scenes remain unchanged                            | **PASS** | `git diff 1890142~..1890142 -- scenes/` returns **zero bytes**. The ten existing `.rrscene` fixtures (eight default + the SCHW.9 + PENROSE.10 manifold fixtures) are byte-identical to the pre-MANI-CONSUME.1 state. The MANI-CONSUME.1 commit touched exactly three files: `docs/BUILD_PLAN.md`, `src/core/CommandLine.cpp`, `src/main.cpp`. No scene file is modified. The eight default fixtures (`test_camera`, `test_full_scene`, `test_lights`, `test_materials`, `test_mesh`, `test_relativity`, `test_render_settings`, `test_spheres`, `test_textured_material`) do not author a `manifold` block; the SCHW.9 parser's optional-field handling leaves `scene.manifold = ManifoldMode{}` (the disabled / Euclidean / strength-0 default) for those scenes; the dispatcher merge resolves to whichever default the CLI is using — preserving the byte-identity invariant for every existing render action. |
| 4 | CLI overrides still function correctly                     | **PASS** | The SCHW.9 dispatcher-merge policy (`effective_manifold = cfg.manifold.enabled ? cfg.manifold : scene.manifold`) is preserved verbatim by MANI-CONSUME.1. When the operator passes explicit CLI flags AND a scene path, the CLI override wins (matches the SCHW.9 documented policy). Empirically verified on the audit host:<br>**Command:** `RelativityRender --render-aovs scenes/test_schwarzschild_like_manifold.rrscene --manifold-enable --manifold-chart euclidean`<br>**Output:**<br>`[INFO] scene file: /home/.../scenes/test_schwarzschild_like_manifold.rrscene`<br>`[INFO] aovs manifold mode: enabled (chart=euclidean, strength=0.000000, debug=off)`<br>`[ERROR] --render-aovs requires CUDA...`<br>The scene file authors `chart=schwarzschild-like` but the CLI `--manifold-chart euclidean` wins the merge (because `cfg.manifold.enabled = true` from `--manifold-enable`). The resolved `chart=euclidean` confirms the CLI override flows through correctly. Without the CLI flag (`cfg.manifold.enabled = false`), the scene-side `chart=schwarzschild-like` wins (verified by check #1's transcript). |
| 5 | CPU/CUDA/OptiX paths consume the same manifold settings    | **PASS** | The dispatcher-merge logic resolves manifold mode at one site per CLI action (`run_render_aovs` for CUDA path; `run_render_optix_aovs` for OptiX path). Both sites use the **identical** merge expression `effective_*_manifold = cfg.manifold.enabled ? cfg.manifold : scene.manifold`. The resolved mode flows to:<br>**(a) CPU seam:** the `ManifoldTransform.h` SchwarzschildLike + PenroseLike arms (SCHW.3 / PENROSE.4) read `t.chart.type` from the same `CoordinateChart` payload the kernel arms read. Audited at SCHW.4 + PENROSE.5.<br>**(b) CUDA kernel:** `CudaTestKernel.cu:639-680` reads `scene.manifold_mode` + `scene.coordinate_chart` populated by `CudaRenderer::render_scene_with_aovs(...)` from `targets.manifold_mode` + `targets.coordinate_chart` populated by `main.cpp::run_render_aovs` from `effective_cuda_manifold`. Audited at SCHW.6 / SCHW.7 + PENROSE.6 / PENROSE.7.<br>**(c) OptiX kernel:** `OptixPrograms.cu:783-844` reads `optixLaunchParams.manifold_mode` + `optixLaunchParams.coordinate_chart` populated by `OptixRenderer::render_aovs(...)` from the SCHW.7 trailing parameters populated by `main.cpp::run_render_optix_aovs` from `effective_manifold`. Audited at SCHW.7 / SCHW.8 + PENROSE.8 / PENROSE.9.<br>The chart-payload builder helpers in `main.cpp` (`manifold_chart` for OptiX, `cuda_manifold_chart` for CUDA) emit **byte-identical** parameter values for the same chart family (SchwarzschildLike: mass=1.0/spin=1.0/compactification_scale=0.1; PenroseLike: mass=5.0/spin=1.0/compactification_scale=1.0). The math leaves are single-source-of-truth (`SchwarzschildLikeWarp.h` + `PenroseLikeCompactification.h`). Cross-backend AOV byte-equivalence is structurally guaranteed by single-source-of-truth math; the SCHW.11 + PENROSE.12 capstones already verified this for the kernel-side. MANI-CONSUME.1 extends the equivalence claim to the **host-side input** (the same `scene.manifold` + `effective_*_manifold` flows to all three backends). |
| 6 | No new manifold math added                                  | **PASS** | `git diff 1890142~..1890142 -- src/manifold/` returns **zero bytes**. The MANI-CONSUME.1 commit reuses the existing math leaves (`SchwarzschildLikeWarp.h` from SCHW.1, `PenroseLikeCompactification.h` from PENROSE.2) verbatim. No new helper, no new function, no new file in `src/manifold/`. The commit's source-side diff is restricted to two host-only files: `src/core/CommandLine.cpp` (CLI-parser extension) and `src/main.cpp` (dispatcher scene-load + manifold-mode log). |
| 7 | Build / test status                                         | **PASS** | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. Full ctest: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `312 / 312 checks passed` (unchanged from the post-PENROSE.12 baseline — MANI-CONSUME.1 is a CLI / dispatcher slice that reuses the existing math leaves; the unit tests are unaffected). `cli_tests: 123/123 passed`, `renderer_tests: 19/19 passed`, `relativity_tests` unchanged. No new ctest target; no CMake link-line change. |
| 8 | Runtime CUDA/OptiX validation status                       | **DEFERRED** | Standard audit-host posture: no CUDA SDK, no OptiX SDK. The SDK_FOUND TUs compile cleanly but cannot link / launch device code. Deferred runtime checks the operator should exercise on an SDK-equipped host:<br>**(a)** `--render-aovs scenes/test_schwarzschild_like_manifold.rrscene` on a CUDA host produces `output/aov_manifold_coordinates.ppm` with the SchwarzschildLike radial-warp signature (audited at SCHW.11 capstone; deferred runtime exercises the kernel arm with the fixture's parameters).<br>**(b)** `--render-optix-aovs scenes/test_penrose_like_manifold.rrscene` on an OptiX-SDK host produces `output/optix_aov_manifold_coordinates.ppm` with the PenroseLike asymptotic-compactification signature (audited at PENROSE.12 capstone).<br>**(c)** CUDA ↔ OptiX byte-equivalence: `--render-aovs <fixture>` and `--render-optix-aovs <fixture>` on the same SDK-equipped host produce byte-identical `aov_manifold_coordinates.ppm` outputs for the same fixture (structurally guaranteed by single-source-of-truth math at PENROSE.7 / PENROSE.9 audits + identical artistic-default chart payloads in main.cpp).<br>**(d)** Default-scene byte-identity: invocations without a scene-path argument continue to produce the pre-MANI-CONSUME.1 inline-scene AOV output on both backends (the inline-scene path is preserved verbatim in the `else { ... }` block).<br>**(e)** CLI-override correctness on SDK host: `--render-aovs <fixture> --manifold-enable --manifold-chart euclidean` produces an Euclidean-fallback AOV (raw `best.position`) regardless of the scene's `manifold` block. |
| 9 | PASS / REPAIR / BLOCKED verdict                            | **PASS** | All eight structural checks (#1–#7 and #9) return PASS. Check #8 is DEFERRED on documented audit-host limitations. No REPAIR or BLOCKED item is outstanding. The MANI-CONSUME.1 commit closes the host-side consumption gap cleanly: SchwarzschildLike + PenroseLike fixture scenes auto-consume their manifold settings; default scenes byte-identical; CLI overrides preserved; CPU/CUDA/OptiX paths share the single resolved manifold mode; no new manifold math added; build green. The slice is **safe to extend** to the next manifold direction. |

---

## 3. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3 ("Do not implement fake stubs
pretending to be complete systems") the audit is
explicit about its scope boundary:

- **No runtime device-side verification.** The audit
  host cannot execute the CUDA or OptiX kernel arms;
  check #8 enumerates the runtime checks deferred to
  an SDK-equipped host. The structural checks
  (#1–#7 and #9) are exhaustive within the
  audit-host's reach.
- **No PPM-level byte-identity comparison.** The
  default-scene byte-identity invariant (check #3)
  is verified at the source-diff level (`git diff
  -- scenes/` returns 0 bytes; the source files
  driving inline scenes are unchanged). PPM-level
  byte-identity for the inline-scene rendering is
  deferred to the SDK host.
- **No golden-PPM pinning.** The fixture-doc
  predictions (per-sphere `r_chart` for PenroseLike;
  per-sphere `f` factor for SchwarzschildLike)
  enumerate the expected visual signatures
  qualitatively; pinning a golden PPM as a
  regression anchor requires an SDK-equipped host
  AND a stable random seed / sample count.
- **No path-tracer / mesh-scene / direct-lighting
  scene-aware action verification.** MANI-CONSUME.1
  closes the gap for `--render-aovs` and
  `--render-optix-aovs` specifically (the two
  actions that engage the `ManifoldCoordinates`
  AOV). Other scene-aware actions
  (`--render-pathtrace`, `--render-mesh-scene`,
  `--render-optix-pathtrace`, etc.) already load
  scene files but don't write the
  `ManifoldCoordinates` AOV, so the operator-side
  observable behavior of loading a manifold fixture
  through them is unchanged.
- **No CLI surface validation against the broader
  CLI test suite beyond cli_tests's 123/123.** The
  MANI-CONSUME.1 parser change is purely additive
  (peek-then-take optional positional); existing
  parser-tests pass unchanged because they don't
  exercise the new optional-positional path.

---

## 4. REASONING SUMMARY

The MANI-CONSUME.1 commit (`1890142`) ships three
host-side files with a unified consumption-gap
closure:

- **`src/core/CommandLine.cpp`** — two CLI-parser
  arms (`--render-aovs` and `--render-optix-aovs`)
  gain optional positional `<scene-path>` argument.
  The peek-then-take pattern preserves the
  zero-argument invocation byte-identically.
- **`src/main.cpp::run_render_optix_aovs`** —
  scene-file loading branch at the top; inline
  procedural scene wrapped in `else { ... }`;
  manifold-mode resolution + log moved BEFORE the
  `#ifndef RELATIVITYRENDER_ENABLE_OPTIX` guard so
  the operator sees the resolved mode on every
  host.
- **`src/main.cpp::run_render_aovs` (CUDA)** —
  parallel CUDA shape; inline quad mesh promoted
  into `scene.meshes` (the kernel-upload path now
  feeds through the shared "first visible non-empty
  mesh" picker); manifold-mode resolution + log
  moved BEFORE the `#ifndef RR_HAS_CUDA` guard;
  `effective_cuda_manifold` aliased to
  `pre_effective_manifold` via `const&` (single
  resolution).

The auto-consumption invariant (checks #1 + #2) is
**verified by audit-host transcripts** — the
operator-facing logs show the resolved mode
including chart name + strength + debug flag, for
both the SchwarzschildLike and PenroseLike
fixtures, even when the audit host refuses to
launch device code.

The default-scenes-unchanged invariant (check #3)
is **directly verified** by `git diff -- scenes/`
returning 0 bytes.

The CLI-override-correctness invariant (check #4)
is **directly verified** by an audit-host
transcript showing the CLI `--manifold-chart
euclidean` winning the merge over the scene's
`chart=schwarzschild-like`.

The cross-backend-consumption invariant (check #5)
is **structurally guaranteed** by the
single-source-of-truth dispatcher-merge expression
+ single-source-of-truth math leaves + identical
artistic-default chart payloads. The SCHW.11 +
PENROSE.12 capstones already verified the kernel-
side equivalence; MANI-CONSUME.1 extends the
equivalence to the host-side input.

The no-new-manifold-math invariant (check #6) is
**diff-zero verified** (`git diff -- src/manifold/`
returns 0 bytes).

The build/test status (check #7) shows the slice
integrates cleanly: ctest 12/12 PASS;
`manifold_identity_tests 312/312`; `cli_tests
123/123`; `renderer_tests 19/19`. No new test
binary; no regression vs the post-PENROSE.12
baseline.

The runtime CUDA/OptiX status (check #8) is
DEFERRED on documented audit-host limitations;
the standard posture for post-impl per-slice
audits.

The verdict (check #9) is **PASS** structurally;
runtime DEFERRED.

---

## 5. CAPSTONE STATUS UPDATE — DEFERRED ITEMS REVIEW

The MANI-CONSUME.1 commit's primary motivation
was closing the **consumption gap** that both the
SCHW.11 capstone (`docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md`
§10 risk #b) and the PENROSE.12 capstone
(`docs/PENROSE_LIKE_ARC_AUDIT.md` §3 check #10
risk #a) flagged as the **highest-priority
deferred item** for both arcs.

**Status update — SCHW.11 capstone §10 risks:**

| Risk | Pre-MANI-CONSUME.1 | Post-MANI-CONSUME.1 |
|------|---------------------|----------------------|
| (a) SCHW.5 CUDA-side wiring unlanded   | CLOSED at SCHW.5 / `73e9591`                | unchanged (already CLOSED) |
| (b) Consumption-gap CLI extension       | DEFERRED (highest-priority gap)             | **CLOSED at MANI-CONSUME.1 / `1890142`** |
| (c) No primary-ray direction warp      | DEFERRED (cosmetic / artistic)              | unchanged (still DEFERRED)  |
| (d) No chart-parameter scene-authoring | DEFERRED (artistic defaults in main.cpp)    | unchanged (still DEFERRED)  |
| (e) Runtime PPM regression suite       | DEFERRED (requires SDK host + golden PPMs)  | unchanged (still DEFERRED)  |

**Status update — PENROSE.12 capstone §3 check #10 risks:**

| Risk | Pre-MANI-CONSUME.1 | Post-MANI-CONSUME.1 |
|------|---------------------|----------------------|
| (a) Consumption-gap CLI extension       | DEFERRED (highest-priority gap, same as SCHW.11 (b)) | **CLOSED at MANI-CONSUME.1 / `1890142`** |
| (b) No primary-ray direction warp      | DEFERRED                                    | unchanged (still DEFERRED)  |
| (c) No chart-parameter scene-authoring | DEFERRED                                    | unchanged (still DEFERRED)  |
| (d) Runtime PPM regression suite       | DEFERRED                                    | unchanged (still DEFERRED)  |

**Net effect:** Both arcs' largest open item is now
CLOSED. The remaining deferred items are
**cosmetic / artistic** (primary-ray warp) or
**SDK-host-blocked** (PPM golden pinning;
chart-parameter scene-authoring is also tractable
but lower priority since the artistic defaults work
fine for the fixture). The path forward is
MANI-I.12 (final cross-host audit when an SDK
host is available) per the integration plan §11.

---

## 6. NEXT

The slice closes the consumption gap **without
introducing new numbered slices in the SCHW.* /
PENROSE.* per-arc ladders** (MANI-CONSUME.1 is its
own meta-slice that cross-cuts both arcs). The
audit-host build remains at the post-PENROSE.12
baseline. No new ctest target; no CMake link-line
change.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is one of (in strategic priority
order):

- **MANI-I.12 — Final cross-host audit** (highest
  priority when an SDK host is available): synthesise
  both SCHW.* and PENROSE.* arcs' SDK-host runtime
  verdicts into the integration plan §11's final
  manifold-rendering verdict. With MANI-CONSUME.1
  landed, the SDK-host runtime is now exercisable
  via the simple commands the fixture docs
  document (`--render-aovs <SCHW fixture>` /
  `--render-optix-aovs <PENROSE fixture>`); the
  SDK-host capstone need only confirm the visual
  signatures and pin golden PPMs.

- **Primary-ray direction warp** (medium priority;
  cosmetic / artistic): invoke
  `schwarzschild_like_warp_ray_direction(...)` from
  raygen for SchwarzschildLike; add an analogous
  helper for PenroseLike. Adds the pseudo-lensing
  beauty-pass signature for both chart families.

- **Manifold-orthogonal work** (any priority): Field
  Interpretation Layer (Phase 1), other path-tracer
  features (denoiser integration with chart-aware
  AOVs), or the chart-parameter scene-authoring
  surface (lifts the SCHW.* / PENROSE.* deferred
  item (d)/(c) by adding an optional `chart_params`
  sub-block to the `manifold` scene-file block).

**Explicitly NOT authorised by this audit:** Kerr /
Kruskal work (PENROSE.12 capstone explicit
non-authorisation; operator's MANI-CONSUME briefs
also forbid). Cinema 4D / server / UI / node-editor
work (architecture-doc §8 non-goals).

---

## 7. REFERENCES

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  — top-level rules; master rule #3 ("no fake
  stubs") is the load-bearing invariant.
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3
  ontology; §8 non-goals.
- `docs/SCHWARZSCHILD_LIKE_ARC_AUDIT.md` (SCHW.11) —
  predecessor arc capstone; risk (b) closed by
  MANI-CONSUME.1.
- `docs/PENROSE_LIKE_ARC_AUDIT.md` (PENROSE.12) —
  predecessor arc capstone; risk (a) closed by
  MANI-CONSUME.1.
- `docs/MANIFOLD_RENDER_CONFIG_BRIDGE_AUDIT.md` —
  MANI-I.4 audit of the host-side config bridge
  the MANI-CONSUME.1 dispatcher merge logic
  extends.
- `docs/SCHWARZSCHILD_LIKE_FIXTURE.md` (SCHW.10
  companion) — fixture purpose; §5 "Current
  consumption status" called out the gap
  MANI-CONSUME.1 closes.
- `docs/PENROSE_LIKE_FIXTURE.md` (PENROSE.10
  companion) — parallel fixture doc; §5 referenced
  the same gap.
- `docs/MANIFOLD_INTEGRATION_PLAN.md` §11 —
  MANI-I.12 final cross-host audit slot, now
  next on the manifold ladder.
- `src/core/CommandLine.cpp::parse_chart_type` —
  SCHW.9 / PENROSE.4 chart-name parser; consumed
  unchanged by MANI-CONSUME.1.
- `src/io/SceneLoader.cpp::apply_manifold` —
  SCHW.9 scene-file parser; consumed unchanged.
- `src/scene/Scene.h::Scene::manifold` — SCHW.9
  scene POD slot; consumed unchanged.
- `src/main.cpp::run_render_optix_aovs` (lines
  ~1996–2250) — extended at MANI-CONSUME.1 with
  scene-load + manifold-mode log before the
  OptiX-availability guard.
- `src/main.cpp::run_render_aovs` (lines
  ~3792–4100) — extended at MANI-CONSUME.1 with
  parallel CUDA-side scene-load + manifold-mode log.
- `src/manifold/SchwarzschildLikeWarp.h` — math
  leaf from SCHW.1; unchanged.
- `src/manifold/PenroseLikeCompactification.h` —
  math leaf from PENROSE.2; unchanged.
- `scenes/test_schwarzschild_like_manifold.rrscene`
  — SCHW.9 fixture; now auto-consumed.
- `scenes/test_penrose_like_manifold.rrscene` —
  PENROSE.10 fixture; now auto-consumed.

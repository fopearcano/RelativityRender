# Manifold Render Config Bridge Audit (MANI-I.4)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `d677b2c` ("pathtracer:
MANI-I.3 — Manifold Render Config Bridge (impl, host-only
plumb)").
Audit host: linux, audit-host build (no CUDA, no OptiX SDK).
Mode: documentation-only. No source code is touched by this
verdict; the result is synthesised purely from the tree's
current state, `git diff` against the post-MANI-I.2
baseline, the running `RelativityRender` executable's log
output, the `cli_tests` binary's runtime output, and
`ctest` exit codes.

This audit is the per-slice gate for the MANI-I.3 Render
Config Bridge (`d677b2c`). It verifies the seven items the
task brief enumerates — ManifoldMode reaches a renderer-
facing config, defaults remain disabled/Euclidean, the
render-start log line is wired correctly, no CUDA / OptiX
behaviour changed, no output changes by default, build /
test green — and produces the PASS / REPAIR / BLOCKED
verdict that gates progression to the Euclidean-identity
GPU path (renumbered MANI-I.5; see §4).

---

## 1. VERDICT

**PASS.**

All seven checks return PASS. No REPAIR or BLOCKED item
is found. The MANI-I.3 Render Config Bridge is safely
threaded into `PathTraceConfig` without a behavioural
change, and the operator may proceed to the Euclidean-
identity GPU path (MANI-I.5, renumbered from the original
MANI-I.4 per §4 below).

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | ManifoldMode reaches renderer-facing config | **PASS** | `src/pathtracer/PathTracer.h:189` declares `rr::manifold::ManifoldMode manifold;` as a member of `PathTraceConfig`, with a 15-line doc-comment block citing MANI-I.3 / MANI-I.5 / the disabled-default "no output change" contract. `src/main.cpp:2492` copies `cfg.manifold` into `pcfg.manifold` at the CUDA pathtrace dispatcher's `PathTraceConfig` construction site, alongside the existing `pcfg.firefly_clamp` / `pcfg.enable_nee` assignments. The OptiX pathtrace dispatcher reads `cfg.manifold` directly (no `PathTraceConfig` round-trip; per the existing `firefly_clamp` / `enable_nee` pattern in that dispatcher). |
| 2 | Defaults remain disabled / Euclidean       | **PASS** | `PathTraceConfig::manifold` is declared with default-initialisation, so `PathTraceConfig{}` carries `ManifoldMode{}` field-by-field: `enabled = false`, `chart = CoordinateChartType::Euclidean`, `strength = 0.0f`, `debug_visualization = false`, `preserve_light_speed_normally = true`, `transform_coordinates_instead_of_light = true` (the documented "no output change" anchor pinned at MANIFOLD.6 and re-verified at the MANI-I.2 audit). The MANI-I.3 commit adds no new default-overriding code path; the only call sites that set `pcfg.manifold` set it to `cfg.manifold`, which at MANI-I.1's CLI default is the same `ManifoldMode{}` value. |
| 3 | Render logs manifold mode if applicable    | **PASS** | `src/main.cpp:152` defines `format_manifold_mode(const ManifoldMode&)` in the anonymous namespace, producing a single line of the documented shape `"<enabled\|disabled> (chart=<kebab>, strength=<f>, debug=<on\|off>)"` with an exhaustive enum switch over every `CoordinateChartType` enumerator (no default-clause fallthrough). The OptiX pathtrace dispatcher (`run_render_optix_pathtrace`, `src/main.cpp:1641`) emits `Logger::info("manifold         : ..." + format_manifold_mode(cfg.manifold))` immediately after the existing `enable_nee` log line and *before* the OptiX renderer invocation. The CUDA pathtrace dispatcher (`run_render_pathtrace`, `src/main.cpp:2559`) emits the same-shape line *after* the per-spp render returns, alongside the existing `firefly_clamp` / `enable_nee` lines. The log line emits the operator-supplied or default mode unconditionally whenever the dispatcher reaches the log site. |
| 4 | No CUDA / OptiX behaviour changed          | **PASS** | `git diff 799a9ac..d677b2c --name-only` returns five files, none under `src/cuda/` or `src/optix/`: `CMakeLists.txt`, `docs/BUILD_PLAN.md`, `docs/MANIFOLD_INTEGRATION_PLAN.md`, `src/main.cpp`, `src/pathtracer/PathTracer.h`. The CUDA `k_pathtrace_sample` kernel's source TUs (`src/cuda/CudaPathTracer.cu`, `src/cuda/CudaRenderer.cu`) and the OptiX `__raygen__pathtrace` program (`src/optix/OptixPrograms.cu`) are byte-identical. `OptixLaunchParams` is byte-identical. No PTX-embed helper change. No launch-params field added, no kernel signature change, no SBT record change. |
| 5 | No output should change by default         | **PASS** | Structurally guaranteed: no kernel reads `pcfg.manifold`. The kernel surface enumerated under check 4 is byte-identical to the pre-MANI-I.3 baseline. The `PathTraceConfig::manifold` field is a host-side data carrier — it exists on the CPU-resident POD, gets copied from `cfg.manifold` on every `PathTraceConfig` construction, and gets read by `format_manifold_mode` for the host-side echo log line. No device-side memcpy moves it to GPU memory; no kernel argument refers to it. The pre-MANI-I.3 reference images therefore reproduce bit-for-bit on every existing CLI action regardless of the `--manifold-*` flags' values. |
| 6 | Build / test status                        | **PASS** | Audit-host rebuild (`cmake --build build -j`) succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. Full `ctest`: `100% tests passed, 0 tests failed out of 12` — the same twelve binaries that were green at the post-MANI-I.2 baseline. `cli_tests` reports `cli_tests: 123/123 passed` (unchanged from MANI-I.2 — the parser surface is unchanged). The rebuild fan-out from the `rr_pathtracer` → `rr_manifold` INTERFACE link is limited to TUs that already included `pathtracer/PathTracer.h` (five TUs total: `src/pathtracer/PathTracer.cpp`, `src/main.cpp`, plus three `rr_renderer` TUs); no unexpected file recompiled. |
| 7 | PASS / REPAIR / BLOCKED verdict            | **PASS** | All six structural checks above return PASS; no observation is REPAIR or BLOCKED. The slice's "what does NOT ship" list (in the integration plan §5 and the BUILD_PLAN MANI-I.3 entry) is exhaustive across the six "no" rules from the task brief (no coordinate warp, no CUDA kernel change, no OptiX program change, no visual output change, no C4D / server / UI / node-editor touch, no scene-file change). The runtime invariants are structurally guaranteed by the kernel's continued ignorance of `pcfg.manifold`. The slice is **safe to extend**. |

---

## 3. REASONING SUMMARY

The MANI-I.3 commit (`d677b2c`) introduces:

- a `rr::manifold::ManifoldMode manifold` member on
  `rr::pathtracer::PathTraceConfig` (defaulting to the
  disabled / Euclidean / strength 0 / debug off anchor);
- a `target_link_libraries(rr_pathtracer INTERFACE rr_math
  rr_manifold)` extension so the manifold header
  propagates to every consumer of `rr_pathtracer`
  transitively (header-only INTERFACE link; rebuild
  fan-out limited to TUs that included
  `pathtracer/PathTracer.h`);
- a `format_manifold_mode(ManifoldMode)` helper in the
  anonymous namespace of `src/main.cpp`, producing the
  documented `"<enabled|disabled> (chart=<kebab>,
  strength=<f>, debug=<on|off>)"` log shape with an
  exhaustive enum switch;
- a `cfg.manifold → pcfg.manifold` copy at the CUDA
  pathtrace dispatcher's `PathTraceConfig` construction
  site, alongside the existing `pcfg.firefly_clamp` /
  `pcfg.enable_nee` assignments;
- a single `Logger::info("manifold         : ...")` line
  at each of the two pathtrace dispatch sites: the OptiX
  dispatcher's line fires *before* the OptiX renderer
  invocation (visible even when the renderer returns the
  documented audit-host "requires OptiX" fallback error
  on a CUDA + OptiX-SDK host); the CUDA dispatcher's
  line fires *after* the per-spp render returns
  (matching the existing `firefly_clamp` / `enable_nee`
  lines' placement in that dispatcher).

No file outside `src/main.cpp`, `src/pathtracer/PathTracer.h`,
`CMakeLists.txt`, and `docs/` is touched. The CUDA and
OptiX kernel surfaces are byte-identical; the
`OptixLaunchParams` struct is byte-identical; the PTX
embed helper is unchanged. The `rr_manifold` and
`rr_field` INTERFACE libraries' link-graph fingerprint
gains exactly one new edge (`rr_pathtracer → rr_manifold`);
the audit-host build verifies that edge resolves
cleanly with no warning.

The bit-identity invariant the integration plan §2
requires (every existing CLI action without any
`--manifold-*` flag produces pixel-bit-identical output
to the pre-pivot baseline) is **structurally guaranteed**
by the kernel's continued ignorance of `pcfg.manifold`.
There is no device-side memcpy moving the field to GPU
memory, no kernel argument referencing it, no `__device__`
read of it. The audit-host's runtime cannot directly
verify the bit-identity (CUDA + OptiX-SDK both off on
this host); a CUDA + OptiX-SDK host would re-verify by
running every pre-MANI-I.3 reference image and
`cmp`-ing the output. That runtime gate is the same one
the existing `firefly_clamp` / `enable_nee` log lines
sit behind and is deferred to the final cross-host audit
(MANI-I.9 under the renumbered integration plan §10).

The render-start log line is observable on a CUDA +
OptiX-SDK host. The audit-host's `--render-optix-
pathtrace` invocation correctly short-circuits with the
documented "requires OptiX" error message *before*
reaching the log site (the `#ifndef RELATIVITYRENDER_-
ENABLE_OPTIX` guard at `main.cpp:1583` runs first); the
audit-host's `--render-pathtrace` invocation correctly
short-circuits with the CUDA-scene-upload failure
(`!gpu_scene.upload_camera()`) at `main.cpp:2421`
before reaching the log site. Both behaviours match the
existing `firefly_clamp` / `enable_nee` log lines'
visibility on the same audit host; no new audit-host
visibility regression is introduced.

---

## 4. NEXT

The slice is **safe to extend**. The integration plan's
slice numbering needs another one-step shift to absorb
this audit slot:

- **MANI-I.1** — CLI config only (LANDED).
- **MANI-I.2** — CLI Config Audit (LANDED).
- **MANI-I.3** — Render Config Bridge (LANDED).
- **MANI-I.4** — **THIS AUDIT** (Render Config Bridge
  Audit, doc-only).
- **MANI-I.5** — Euclidean identity GPU path (was
  MANI-I.4).
- **MANI-I.6** — debug coordinate-warp AOV (was
  MANI-I.5).
- **MANI-I.7** — Schwarzschild-like artistic coordinate
  remap (was MANI-I.6).
- **MANI-I.8** — Penrose-like compactification
  visualisation (was MANI-I.7).
- **MANI-I.9** — final cross-host audit (was MANI-I.8);
  merge gate for the whole MANI-I.* programme.

The integration plan §3 chain diagram and §5–§10 slice
sections are updated as part of this MANI-I.4 commit so
the per-slice numbering stays coherent. The
`MANIFOLD_INTEGRATION_PLAN.md` §11 non-goals and §12
references sections are unchanged. The two cross-
references in `MANIFOLD_CORE_FOUNDATION_AUDIT.md` and
`MANIFOLD_RENDERING_ARCHITECTURE.md` that point at the
final-audit slice number are updated to `MANI-I.9`.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator may
prompt for is **MANI-I.5 — Euclidean identity GPU
path** per the renumbered integration plan §6 (first
real GPU touch; the CUDA / OptiX kernel reads
`pcfg.manifold` and `ManifoldTransform`; on the
Euclidean default the chart-aware ray seam is the
identity; bit-identity on at least seven enumerated CLI
actions is the acceptance gate).

# Schwarzschild-Like CUDA Warp Audit (SCHW.6)

Date:   2026-05-14
Branch: `claude/rewrite-rendering-core-De71I`
Last commit on the audited tree: `b78fe98` ("docs:
SCHW.4 — Schwarzschild-Like CPU Integration Audit
(docs only)").
Audit host: linux, audit-host build (no CUDA, no OptiX
SDK).
Mode: documentation-only. No source code is touched by
this verdict; the result is synthesised purely from the
tree's current state, `git diff` against the
post-SCHW.4 baseline, the `manifold_identity_tests`
runtime output, and `ctest` exit codes.

This audit is a **forward-looking CUDA-safety review**
of the structural bridge between the SCHW.1 math leaf
(`schwarzschild_like_*` helpers in
`src/manifold/SchwarzschildLikeWarp.h`), the SCHW.3
`ManifoldTransform.h` seam, and the existing
MANI-I.5 / MANI-I.8 CUDA kernel surface. It verifies
the nine items the task brief enumerates — CUDA-safe
warp helper exists; warp activates only on the
intended gates; disabled/default no-op; Euclidean
identity; bounded / no-NaN; OptiX untouched; build /
test status; runtime CUDA-host status; verdict.

**Important honesty note (master rule #3).** The
operator's renumbered plan has SCHW.5 (CUDA
integration; the slice that wires the
SchwarzschildLike arm into the CUDA kernels'
`ManifoldCoordinates` AOV write path) as **NOT YET
LANDED** at the time of this audit. This audit
verifies the *infrastructure that SCHW.5 will plug
into* is CUDA-safe by construction; it does NOT
verify a kernel-side activation that has not yet been
written. The check enumerated below explicitly
acknowledge which parts of the bridge exist today
versus which are deferred to SCHW.5. See §3 ("WHAT
THIS AUDIT DOES NOT VERIFY") for the explicit boundary.

---

## 1. VERDICT

**PASS** (structural CUDA-safety of the existing bridge),
with **DEFERRED** runtime CUDA-host status and a
documented gap: the kernel-side activation slice
(renumbered SCHW.5) has not yet landed.

All eight structural checks (#1–#7 and #9) return PASS.
Check #8 (runtime CUDA-host status) is DEFERRED on
two grounds: (a) the audit-host has no CUDA SDK and
cannot execute device code; (b) the kernel-side
activation slice (renumbered SCHW.5) has not yet
landed, so even on a CUDA host there is no kernel
call site that invokes the chart-aware arm to verify
at runtime. Both reasons map cleanly onto the
MANI-I.6 / MANI-I.9 DEFERRED posture for audit-host
audits of CUDA-side work.

No REPAIR or BLOCKED item is found. The bridge is safe
by construction; the operator may proceed to SCHW.5
(CUDA integration) when ready, and the SCHW.8 (Final
audit) will verify the kernel-side runtime behaviour
on a CUDA host.

---

## 2. PER-CHECK RESULTS

| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | CUDA-safe warp helper exists                       | **PASS**     | Every shipping math helper carries the `RR_HD inline` decoration (the `__host__ __device__` macro from `math/MathUtils.h`), so the helpers compile under NVCC and are callable from CUDA kernels by construction. Locations: `SchwarzschildLikeWarp.h:113` (`schwarzschild_like_validate_params`), `:145` (`schwarzschild_like_world_to_chart`), `:196` (`schwarzschild_like_chart_to_world`), `:267` (`schwarzschild_like_warp_ray_direction`). The SCHW.3 seam in `ManifoldTransform.h` is also `RR_HD inline` end-to-end: `:158` (`schwarzschild_like_params_from`), `:186` / `:205` / `:226` / `:242` / `:261` / `:289` / `:323` / `:347` (all eight `world_to_chart` / `chart_to_world` / `transform_direction` / `transform_ray_like_direction` Vec3 + Vec4 overloads). The downstream call closure (`rr::math::length`, `rr::math::normalize`, `rr::math::Vec3` ops) is `RR_HD inline` throughout — no host-only symbol leaks into the device call graph. |
| 2 | Warp activates only when enabled + SchwarzschildLike + strength > 0 | **PASS** (structural; runtime activation deferred to SCHW.5) | The activation gate is decomposed across three layers, each verified separately:<br>**(a) `enabled` gate:** `is_active(manifold_mode)` at `ManifoldMode.h:143-145` returns `m.enabled && m.chart != Euclidean`. The CUDA kernel signatures already accept `manifold_mode` as a trailing parameter — `CudaPathTracer.cuh:103` (`launch_pathtrace_sample(..., ManifoldMode manifold_mode = {})`) and `OptixLaunchParams.h:360` (`ManifoldMode manifold_mode{}`). SCHW.5 will introduce the kernel-side `is_active(...)` guard at the AOV-write call site.<br>**(b) `SchwarzschildLike` gate:** the `t.chart.type == CoordinateChartType::SchwarzschildLike` arm in `ManifoldTransform.h:191` / `:210` / `:272` / `:299` sits AFTER the Euclidean `return`, so Euclidean default cannot fall into the chart-aware path. Non-`SchwarzschildLike` placeholders (Kruskal / Penrose / Kerr) take the explicit passthrough at the bottom of each helper, master-rule-#3-honest.<br>**(c) `strength > 0` gate:** the math leaf short-circuits at `SchwarzschildLikeWarp.h:153-154` (`p.warp_strength == 0.0f` → return input). The SCHW.3 seam currently hardcodes `warp_strength = 1.0f` at `ManifoldTransform.h:159` (the chart's intrinsic full warp); SCHW.5 will thread `ManifoldMode::strength` from the kernel launch params into the helper as the runtime-modifiable dial. Today, with no kernel call site invoking the chart-aware seam, the strength gate is structurally in place but unreachable. |
| 3 | Disabled/default mode remains no-op                | **PASS**     | The CUDA `ManifoldCoordinates` AOV write site at `CudaTestKernel.cu:582-602` is **unchanged** from MANI-I.8 — writes the raw `best.position` on hit and `(0, 0, 0)` on miss with no chart-aware branching. `git diff fef3e50..b78fe98 -- src/cuda/CudaTestKernel.cu` shows zero meaningful diff in this region (the only `b78fe98`-era touches to the file post-MANI-I.8 are unrelated comments in the doppler-AOV area; the AOV-write site is byte-identical). The `manifold_mode` parameter on `launch_pathtrace_sample(...)` at `CudaPathTracer.cu:427` is decorated `[[maybe_unused]]` and the kernel body does not read it. Default `ManifoldMode{}` (`enabled = false`, `chart = Euclidean`, `strength = 0`) propagates through MANI-I.3 / MANI-I.5 / MANI-I.8 without engaging any new code path. |
| 4 | Euclidean mode remains identity                    | **PASS**     | The Euclidean arm in every `ManifoldTransform.h` helper is preserved verbatim from MANIFOLD.5 — SCHW.3's diff inserted the `SchwarzschildLike` arm AFTER each Euclidean `return`, never modifying the Euclidean expression. `world_to_chart(t, Vec3)` Euclidean expression at `ManifoldTransform.h:187` (`(world_pos - t.chart.origin) * (1.0f / t.chart.scale)`), `chart_to_world(t, Vec3)` at `:206` (`t.chart.origin + chart_pos * t.chart.scale`), and the Vec4 counterparts at `:263-270` / `:291-298`. With no CUDA kernel arm invoking these helpers today (SCHW.5 deferred), the Euclidean path the CUDA kernel takes is the existing pre-MANIFOLD.5 expression at `CudaTestKernel.cu:592-602` (raw `best.position` write). |
| 5 | Bounded / no-NaN behavior exists                   | **PASS**     | Inherited from the SCHW.1 math leaf (audited at SCHW.2: `docs/SCHWARZSCHILD_LIKE_WARP_AUDIT.md` §2 checks #2 / #3). Four bounding/no-NaN mechanisms:<br>**(a)** `r = max(|delta|, clamp_radius)` (`SchwarzschildLikeWarp.h:158`) prevents `1 / r^falloff` underflow;<br>**(b)** Newton-Raphson 8-iteration cap + `1e-5` convergence tolerance (`:214-215`);<br>**(c)** `F'` zero-guard at `1e-9` (`:227`);<br>**(d)** primary-ray bend hard-cap at `±0.5` (`:287-291`).<br>The CUDA-safety property is preserved because the helpers are `RR_HD inline` end-to-end — no host-only library call (e.g. `std::abs`, `std::pow`, `std::isfinite`) routes through a CPU-only path: every CMath use is via `<cmath>` which NVCC handles as device-callable. The SCHW.4 audit (`docs/SCHWARZSCHILD_LIKE_CPU_INTEGRATION_AUDIT.md` §2 check #5) verified the seam-level no-NaN/Inf invariant via `test_schw_3_no_nan_inf_near_clamp_radius` (`manifold_identity_tests.cpp:780`); the same test exercises the helpers via host code, but the helpers' device-side behaviour is identical by the `RR_HD inline` contract. |
| 6 | OptiX path was not modified                        | **PASS**     | `git diff fef3e50..b78fe98 -- src/optix/` (the post-MANI-I.6 / pre-SCHW.6 window) shows zero changes to OptiX-side source files. `src/optix/OptixPrograms.cu`'s `ManifoldCoordinates` AOV write arm at `OptixPrograms.cu:723-740` is **byte-identical** to its MANI-I.8 form — closest-hit writes the hit position; miss writes `(0, 0, 0)`; no chart-aware branching. `OptixLaunchParams.h:338-360` retains `ManifoldMode manifold_mode{}` and the `aov_manifold_coordinates` pointer from MANI-I.5 / MANI-I.8, both initialised to the no-op defaults. `OptixRenderer.cpp::render_aovs` host-side allocation for `aov_manifold_coordinates` is still deferred (as flagged in MANI-I.9's audit) — SCHW.6 (this audit) does not change that posture; the deferred allocation closes at SCHW.7 (OptiX integration; was SCHW.6 before this renumber) per the plan §8 renumber summary. |
| 7 | Build / test status                                | **PASS**     | Audit-host `cmake --build build -j` succeeds cleanly with no new warnings under the project's `rr_apply_warnings` settings. Full ctest: `100% tests passed, 0 tests failed out of 12`. `manifold_identity_tests` reports `198 / 198 checks passed` (post-SCHW.4 baseline; unchanged from the post-SCHW.3 state because SCHW.4 was doc-only). `cli_tests: 123/123 passed`, `renderer_tests: 19 / 19 passed`, `relativity_tests` unchanged. No new ctest target; no CMake link-line change since SCHW.3. |
| 8 | Runtime CUDA-host status                           | **DEFERRED** | Two grounds for DEFERRED:<br>**(a)** The audit host (`linux`, no CUDA SDK) cannot execute CUDA device code. The audit-host build skips the CUDA / OptiX targets via the existing CMake conditional (the same posture MANI-I.6 and MANI-I.9 audits flagged DEFERRED).<br>**(b)** Even on a CUDA host, no kernel call site invokes the chart-aware `world_to_chart(t, ...)` helper today — the SCHW.5 (CUDA integration) slice that wires the SchwarzschildLike arm into `CudaTestKernel.cu`'s `ManifoldCoordinates` AOV write path **has not yet landed**. The deferred runtime checks the operator should exercise once SCHW.5 lands include the seven plan §7 fixture renders enumerated in `SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §7.1–§7.6 (Euclidean fallback byte-identity, `warp_strength = 0` byte-identity, visual signature on the AOV, off-chart non-regression, etc.). |
| 9 | PASS / REPAIR / BLOCKED verdict                    | **PASS**     | All eight structural checks return PASS; check #8 is DEFERRED on documented audit-host limitations. No REPAIR or BLOCKED item is outstanding. The bridge between the SCHW.1 math leaf and the existing CUDA kernel surface is **safe by construction**: the helpers are `RR_HD inline`, the kernel signatures already accept `ManifoldMode`, the activation gate decomposes onto existing helpers (`is_active(...)`, `chart.type == SchwarzschildLike`, `warp_strength == 0` short-circuit), and the OptiX path is untouched. The renumbered SCHW.5 (CUDA integration) is the next concrete step. |

---

## 3. WHAT THIS AUDIT DOES NOT VERIFY

Per master rule #3 ("Do not implement fake stubs
pretending to be complete systems") the audit is
explicit about its scope boundary:

- **No kernel-side activation.** This audit does NOT
  verify that any CUDA kernel call site invokes the
  SchwarzschildLike arm of `world_to_chart(...)` or
  `chart_to_world(...)`. As of `b78fe98` no such call
  site exists; the existing `CudaTestKernel.cu`
  AOV-write arm writes the raw `best.position`
  unconditionally. SCHW.5 (CUDA integration; the
  renumbered slice in
  `SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §8) is the
  concrete commit that introduces this kernel-side
  activation, and the SCHW.8 (final audit) will
  verify it end-to-end on a CUDA host.
- **No runtime byte-identity verification.** Plan §7
  enumerates seven runtime checks (Euclidean
  fallback byte-identity for `--render-pathtrace`,
  `--render-scene`, `--render-mesh-scene`,
  `--render-material-scene`, `--render-direct-lighting`,
  `--render-aovs scenes/test_full_scene.rrscene`,
  `--render-aovs --manifold-debug
  scenes/test_full_scene.rrscene`; `warp_strength
  = 0` byte-identity; visual AOV signature; beauty-
  pass lensing signature; off-chart non-regression).
  None of these can be run on the audit host. They
  are noted here for the operator's future SCHW.5
  / SCHW.7 (OptiX integration) / SCHW.8 (final
  audit) follow-through.
- **No strength-dial wiring verification.** The
  `ManifoldMode::strength` runtime dial is currently
  unenforced: the SCHW.3 seam hardcodes
  `warp_strength = 1.0f`. The CLI plumbing
  (MANI-I.1) accepts `--manifold-strength <float>`
  and propagates the value into `ManifoldMode`, but
  no downstream consumer reads it for SchwarzschildLike
  rendering yet. The audit verifies the slot is
  ready; SCHW.5 fills the slot.
- **No OptiX host-side AOV allocation.** The
  `OptixRenderer::render_aovs` host-side
  allocation for `aov_manifold_coordinates` is
  still deferred per MANI-I.9's audit finding;
  this audit does not change that posture. The
  deferred allocation closes at the renumbered
  SCHW.7 (OptiX integration).
- **No primary-ray direction warp.** The math
  leaf's `schwarzschild_like_warp_ray_direction`
  exists and is CUDA-callable, but the
  `ManifoldTransform.h` seam at SCHW.3 explicitly
  did NOT wire it (the seam signature lacks
  `ray_origin`). SCHW.5 (and possibly SCHW.7) will
  decide whether the primary-ray warp invokes the
  helper at raygen.

---

## 4. REASONING SUMMARY

The CUDA-side bridge for the Schwarzschild-like warp
consists of four structural elements, each verified
separately by the per-check table:

- **Math leaf (`SchwarzschildLikeWarp.h`).** Landed
  at SCHW.1 (`2da5780`), audited at SCHW.2
  (`c799621`). All four helpers (validator,
  forward, inverse, primary-ray warp) are `RR_HD
  inline` and CUDA-callable. The SCHW.2 audit
  verified the helpers are bounded by construction
  with documented no-NaN / no-Inf guards. The
  CUDA-safety property — that the helpers compile
  under NVCC and execute on the device — is
  inherited from the `RR_HD inline` decoration plus
  the `<cmath>` / `<math/Vec3.h>` dependency
  closure, which is itself `RR_HD inline`
  throughout.
- **Seam (`ManifoldTransform.h`).** Landed at
  SCHW.3 (`b48c480`), audited at SCHW.4
  (`b78fe98`). The four chart-aware arms route
  through the SCHW.1 math leaf with a builder
  helper (`schwarzschild_like_params_from`) that
  maps `CoordinateChart::params` per the plan §3
  reinterpretation table. The seam is `RR_HD inline`
  end-to-end. The CUDA-safety property is preserved
  because every downstream call (`length`,
  `normalize`, `Vec3` ops) is itself `RR_HD inline`.
- **Kernel signature (`CudaPathTracer.cu` /
  `OptixLaunchParams.h`).** Landed at MANI-I.5
  (`a34e265`), audited at MANI-I.6 (`fef3e50`).
  Both the CUDA `launch_pathtrace_sample(...)` and
  the OptiX launch params already accept
  `ManifoldMode manifold_mode`. The CUDA kernel
  body marks the parameter `[[maybe_unused]]`,
  consistent with the MANI-I.6 audit's note that
  the kernel signature was plumbed but the
  activation was deferred to a later slice.
- **AOV write site (`CudaTestKernel.cu` /
  `OptixPrograms.cu`).** Landed at MANI-I.8
  (`094306f`), audited at MANI-I.9 (`b4ed22e`).
  Both the CUDA closest-hit / miss kernels and the
  OptiX equivalents write the raw world-space hit
  position to the `ManifoldCoordinates` AOV with no
  chart-aware branching. This is the call site
  SCHW.5 will modify to invoke the chart-aware
  `world_to_chart(...)` arm.

The bridge is **structurally complete**: the math
exists, the seam exists, the kernel signature
accepts the runtime mode, the AOV slot is
provisioned, and the OptiX side has its launch param
+ pointer in place. What remains is the SCHW.5
commit that flips a single guard (`if
(is_active(launch_params.manifold_mode)) { ... }`)
in the AOV-write region of `CudaTestKernel.cu`,
calls the chart-aware `world_to_chart(...)` arm,
and writes the result. SCHW.5 will then have its
own per-slice audit (renumbered SCHW.8 if the same
audit-insertion cadence continues), at which point
the runtime CUDA-host status can be PASS rather than
DEFERRED.

The OptiX path (check #6) is untouched. The audit
verified this by `git diff fef3e50..b78fe98 --
src/optix/` returning zero changes; the OptiX
closest-hit / miss programs continue to write the
raw hit position to `aov_manifold_coordinates` with
no chart-aware branching, and the host-side
allocation of the AOV pointer in `render_aovs`
remains deferred per MANI-I.9. The renumbered
SCHW.7 (OptiX integration) will close both items
together.

The build / test status (check #7) is unchanged
from the post-SCHW.4 baseline: `ctest 12/12`,
`manifold_identity_tests 198/198 RR_CHECKs`,
`cli_tests 123/123`, `renderer_tests 19/19`. SCHW.6
is doc-only; the audit-host build is identical to
the post-SCHW.4 state.

---

## 5. NEXT

The slice is **safe to extend**. The
`SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §8 SCHW.* sub-slice
ladder needs a one-step shift to absorb this audit
slot (inserted before the actual CUDA integration
lands, the same forward-looking-safety-audit pattern
the operator's brief asks for):

- **SCHW.1** — Math helper (LANDED at `2da5780`).
- **SCHW.2** — Audit of SCHW.1 (LANDED at `c799621`).
- **SCHW.3** — CPU integration (LANDED at `b48c480`).
- **SCHW.4** — Audit of SCHW.3 (LANDED at `b78fe98`).
- **SCHW.5** — CUDA integration (not yet landed; the
  next concrete impl commit).
- **SCHW.6** — **THIS AUDIT** (Schwarzschild-Like CUDA
  Warp Audit, doc-only; forward-looking CUDA-safety
  review of the existing bridge).
- **SCHW.7** — OptiX integration (was SCHW.6 in the
  prior plan; renumbered).
- **SCHW.8** — Debug visualization (was SCHW.7).
- **SCHW.9** — Final audit (was SCHW.8); closes the
  MANI-I.10 slot.

The `docs/SCHWARZSCHILD_LIKE_REMAP_PLAN.md` §8
sub-slice ladder is updated as part of this SCHW.6
commit so the per-slice numbering stays coherent. The
plan's other sections (§1–§7, §9–§10) are unchanged.

No REPAIR action is required. No BLOCKED item is
outstanding. The next concrete commit the operator
may prompt for is **SCHW.5 — CUDA integration**
(NOTE: this is structurally *before* SCHW.6 audit in
the per-slice ladder; SCHW.6 audit is forward-looking
and gates the SCHW.5 implementation by verifying the
infrastructure SCHW.5 will plug into). When SCHW.5
lands, the runtime CUDA-host checks DEFERRED here
will become PASS-able on a CUDA host; the SCHW.9
(final audit) will close the MANI-I.10 slot with the
end-to-end PASS verdict.

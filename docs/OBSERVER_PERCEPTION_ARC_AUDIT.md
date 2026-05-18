# Observer Primary-Ray Perception Transform Arc Capstone Audit (OBS-PERCEPT.10)

Date:   2026-05-18
Branch: `claude/rewrite-rendering-core-De71I`
Audited arc: **OBS-PERCEPT.*** — the first true
observer-dependent perception transform layer in the
renderer. Closes the OBSERVER.15 capstone audit's
`PASS_WITH_RUNTIME_DEFERRED` future-kernel-migration
risk #1 on BOTH backends + audits the closure.
Arc commits (OBS-PERCEPT.1 – OBS-PERCEPT.9):

- `8db1f9c` OBS-PERCEPT.1 (arc plan)
- `0bf2bb8` OBS-PERCEPT.2 (primary-ray transform
  task brief)
- `b653e48` OBS-PERCEPT.3 (CUDA impl)
- `40bb476` OBS-PERCEPT.4 (CUDA audit)
- `1dbeb23` OBS-PERCEPT.5 (OptiX impl)
- `3d125ad` OBS-PERCEPT.6 (OptiX audit)
- `66a2046` OBS-PERCEPT.7 (debug AOV task brief)
- `95d56d5` OBS-PERCEPT.8 (debug AOV data-model
  impl)
- `8dc5d28` OBS-PERCEPT.9 (fixture + companion
  doc)

Capstone baseline: `bda382c` (FIELD-BEAUTY.8
capstone audit; the last commit before the
OBS-PERCEPT.* arc opened with OBS-PERCEPT.1).
Audit host: linux, audit-host build (no CUDA SDK,
no OptiX SDK). The OBS-PERCEPT.3 + OBS-PERCEPT.5 +
OBS-PERCEPT.8 OptiX-ON-no-SDK builds were
empirically verified at each landing commit
(ctest 14/14 PASS in `/tmp/rr_build_optix_no_sdk`).
Mode: documentation-only. No source code is touched
by this verdict; the result is synthesised purely
from the tree's current state, the OBS-PERCEPT.3 –
OBS-PERCEPT.9 commits' content, the per-slice
audits (OBS-PERCEPT.4 + OBS-PERCEPT.6), the
OBS-PERCEPT.9 companion doc, the audit-host
`ctest` runtime outputs, and `git diff` filter
inspections at the arc boundary + per-slice
commit boundaries.

This audit is the **arc capstone** for OBS-PERCEPT.*.
It verifies the twelve items the task brief
enumerates — CUDA + OptiX primary-ray aberration
exists; activation gates; default-no-op anchors;
secondary rays + Doppler/searchlight unchanged;
debug AOV status; fixture status; runtime status;
remaining risks; recommended next safe stage — and
produces one of four verdicts (PASS /
PASS_WITH_RUNTIME_DEFERRED / REPAIR / BLOCKED).

The OBS-PERCEPT.* arc opens parallel to the
FIELD-I.* + FIELD-BEAUTY.* arc family (which
closed at the FIELD-BEAUTY.8 capstone). The two
arc families coexist and compose orthogonally:
FIELD-* operates on the per-pixel field/mapping
read-and-write path; OBS-PERCEPT.* operates on the
per-pixel observer-frame Lorentz boost of the
primary ray direction. Both arcs share the
"PASS_WITH_RUNTIME_DEFERRED, closes at the
combined CLI bridge slice's SDK-host audit"
runtime-deferral pattern.

---

## 1. VERDICT

**PASS_WITH_RUNTIME_DEFERRED.**

All eleven structural checks (#1 through #9, #11) +
the runtime status (#10) return their expected
verdicts. Check #12 (recommended next safe stage)
documents the HIGHLY RECOMMENDED combined FIELD-* +
OBS-PERCEPT CLI bridge slice as the canonical
runtime-closure follow-up. The overall verdict is
`PASS_WITH_RUNTIME_DEFERRED` because:

- **Structural completeness on the audit-host
  side.** All OBS-PERCEPT.* arc artifacts the
  operator authorised across OBS-PERCEPT.1 –
  OBS-PERCEPT.9 ship cleanly:
    - Arc plan + task briefs (OBS-PERCEPT.1, .2,
      .7) — 3 documentation slices.
    - CUDA + OptiX kernel arms (OBS-PERCEPT.3 +
      OBS-PERCEPT.5) — 2 impl slices with
      per-slice audit gates (OBS-PERCEPT.4 + .6).
    - Debug AOV data-model entries (OBS-PERCEPT.8)
      — 1 impl slice; kernel-arm bridge deferred
      to renumbered OBS-PERCEPT.11.
    - Fixture + companion doc (OBS-PERCEPT.9) —
      1 impl slice.
  The audit-host build and the OptiX-ON-no-SDK
  build both pass clean ctest at every per-slice
  landing commit (13/13 audit-host PASS; 14/14
  OptiX-ON-no-SDK PASS).

- **Runtime deferral on the SDK-host side.** The
  OBS-PERCEPT.4 + OBS-PERCEPT.6 per-slice audits
  both carry `PASS_WITH_RUNTIME_DEFERRED` runtime-
  status verdicts. The audit-host has neither
  CUDA nor OptiX SDK; the kernel arms' empirical
  per-pixel aberration cannot be exercised here.
  The OBS-PERCEPT.9 fixture is the canonical
  SDK-host validation surface; the deferred
  runtime scenarios (per OBS-PERCEPT.4 §3.11 +
  OBS-PERCEPT.6 §3.11 + OBS-PERCEPT.9 companion
  §6 + OBS-PERCEPT.7 task brief §8) all defer to
  the future combined FIELD-* + OBS-PERCEPT CLI
  bridge slice's audit on an SDK host.

- **No structural risks.** The five-axis cross-
  backend symmetry argument (OBS-PERCEPT.6 §3.7)
  guarantees byte-identity by construction
  between CUDA and OptiX outputs for the same
  fixture input. The three-layer no-op anchor
  (Identity outer gate + zero-beta inner gate +
  legacy else-branch via `aberrateDirection(rel,
  ...)`) preserves byte-identical default-state
  output on both backends. The Doppler /
  searchlight + secondary-ray + diagnostic-AOV
  preservations are structural. The OBSERVER.15
  capstone audit's `PASS_WITH_RUNTIME_DEFERRED`
  future-kernel-migration risk #1 is now closed
  on both backends.

- **Three documented remaining risks** (per §3.10
  below; all scope-deferrals, not bugs): the
  debug AOV kernel-arm bridge is deferred to
  the renumbered OBS-PERCEPT.11; the SDK-host
  runtime validation requires SDK availability +
  the future CLI bridge; the per-bounce Option B
  perception transform is deferred to a future
  arc.

The verdict honestly distinguishes "the arc's
structural surface is complete + verified on
audit-host" from "the kernel arms' empirical PPM
outputs are deferred to SDK host" — matches the
FIELD-BEAUTY.8 capstone framing applied at this
arc's scope.

---

## 2. PER-CHECK RESULTS

| #  | Check                                                  | Evidence                                                                                                                                                                                                                                                                                                                                                              | Verdict |
|----|--------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------|
| 1  | CUDA primary-ray observer aberration exists            | `src/cuda/CudaTestKernel.cu:248-258` (`k_render_scene`) + `:387-397` (`k_sphere_relativistic`) + `src/cuda/CudaPathTracer.cu:209-220` (`k_pathtrace_sample`) consume the shared `rr::manifold::apply_observer_primary_ray_aberration(observer_frame, direction)` helper landed at OBS-PERCEPT.3 in `src/manifold/ObserverFrame.h:553+`. The helper is invoked with the dispatch shape `if (perception_active) { ray.direction = apply_observer_primary_ray_aberration(observer_frame, ray.direction); } else { ray.direction = aberrateDirection(rel, ray.direction); }`. Audited at OBS-PERCEPT.4 check #1 (PASS).                                                                                                                                                                                                                                                                                                                                                                                                                                              | PASS    |
| 2  | OptiX primary-ray observer aberration mirrors CUDA     | `src/optix/OptixPrograms.cu:239-249` (`__raygen__pinhole`) + `:1268-1277` (`__raygen__pathtrace`) consume the same shared helper. **Five-axis symmetry** verified at OBS-PERCEPT.6 §3.7: same POD type (rr::manifold::ObserverFrame); same shared helper (apply_observer_primary_ray_aberration); same dispatch shape (outer perception_active + inner helper + else legacy); same math leaf (rr::relativity::aberrateDirection two-argument form); same gate semantics. Cross-backend bit-identity by construction. Empirical SDK-host PPM cmp deferred per check #10.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | PASS    |
| 3  | Activation requires ConstantVelocityMinkowski          | The shared helper's outer gate at `ObserverFrame.h:559-562`: `if (obs_frame.perception_mode != PerceptionMode::ConstantVelocityMinkowski) return direction;`. Default `Identity` perception_mode closes the gate; `CurvedChartGeodesicPlaceholder` reserved mode also closes (master rule #3 placeholder honesty). Empirically verified by `test_obs_percept_3_identity_mode_returns_input_direction` + `test_obs_percept_3_curved_placeholder_returns_input` (6 RR_CHECKs on `tests/manifold_identity_tests.cpp`, landed at OBS-PERCEPT.3). Audited at OBS-PERCEPT.4 + OBS-PERCEPT.6 check #2.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | PASS    |
| 4  | Activation requires beta > 0                           | The shared helper's inner gate at `ObserverFrame.h:565-571`: `const float beta2 = beta.x*beta.x + beta.y*beta.y + beta.z*beta.z; if (!(beta2 > 0.0f)) return direction;`. Squared-magnitude check avoids sqrt cost + is exact at beta=0; NaN-safe `!(beta2 > 0.0f)` form catches NaN beta. Empirically verified by `test_obs_percept_3_constant_velocity_zero_beta_returns_input` (3 RR_CHECKs). Audited at OBS-PERCEPT.4 + OBS-PERCEPT.6 check #3.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | PASS    |
| 5  | Default observer / beta=0 remains no-op                | Three-layer no-op anchor preserved on both backends (the OBS-PERCEPT.3 → OBS-PERCEPT.5 contract inherits verbatim): (a) **Layer 1 (helper inner gate)** — explicit `!(beta2 > 0.0f)` short-circuit; (b) **Layer 2 (math leaf identity)** — `rr::relativity::aberrateDirection(beta_vec, direction)` short-circuits at `beta_mag <= 1.0e-12f` to identity; (c) **Layer 3 (OBSERVER.6 adapter)** — emits `observer_frame.beta = (0, 0, 0)` exactly on zero-beta inputs. Same anchor on both backends; identical empirical contract via `test_obs_percept_3_constant_velocity_zero_beta_returns_input` + `test_obs_percept_3_identity_mode_returns_input_direction`. Audited at OBS-PERCEPT.4 + OBS-PERCEPT.6 checks #4 + #5.                                                                                                                                                                                                                                                                                                                                                       | PASS    |
| 6  | Secondary rays unchanged                               | OBS-PERCEPT.3 + OBS-PERCEPT.5 kernel-arm modifications target ONLY primary-ray generation sites. `CudaPathTracer.cu`'s bounce loop at lines 207+ + `OptixPrograms.cu`'s `__raygen__pathtrace` per-spp inner loop + closest-hit/miss/shadow programs (`__closesthit__radiance`, `__closesthit__pathtrace`, `__miss__radiance`, `__miss__pathtrace`, `__miss__shadow`) are byte-identical to the pre-OBS-PERCEPT.3 baseline. Per-line `git diff bda382c..8dc5d28 -- src/cuda/CudaPathTracer.cu src/optix/OptixPrograms.cu` confirms zero changes inside the bounce-loop bodies. Option A primary-ray-only per OBS-PERCEPT.1 §5.2 honoured. Audited at OBS-PERCEPT.4 check #6 + OBS-PERCEPT.6 check #7. | PASS    |
| 7  | Doppler / searchlight unchanged                        | The post-OBS-P.2 guarded ternary at the Doppler / searchlight call sites is preserved verbatim across both backends. The `rel` snapshot computation at `CudaTestKernel.cu:229-232` + `OptixPrograms.cu:215-221` + `:1216-1221` is byte-identical to the OBS-PERCEPT.2 baseline; the Doppler factor + Doppler color shift + searchlight scaling blocks consume the unchanged `rel` snapshot. The OBS-PERCEPT.3 + OBS-PERCEPT.5 unified helper consolidates ONLY the aberration site, not the Doppler / searchlight sites. Per-line diff confirms zero changes inside the Doppler / searchlight blocks on both backends. Master rule #12 (do not overbuild) honoured — the Doppler / searchlight migration is deferred to future OBS-PERCEPT.* sub-slices. Audited at OBS-PERCEPT.4 check #7 + OBS-PERCEPT.6 check #8.                                                                                                                                                                                                                                                            | PASS    |
| 8  | Debug AOV status                                       | **PARTIAL — data-model-only landed; kernel-arm bridge deferred.** The OBS-PERCEPT.7 task brief defined three diagnostic AOVs (observerAberrationMagnitude NEW; observerBeta EXISTING from OBSERVER.13; observerDirection NEW LIFTED from OBSERVER.12 §2.2). The OBS-PERCEPT.8 implementation slice landed the data-model entries: `AOVType::ObserverAberrationMagnitude = 9` + `AOVType::ObserverDirection = 10` at `src/renderer/AOV.h`; `make_observer_aberration_magnitude(...)` + `make_observer_direction(...)` factories at `src/renderer/AOV.cpp`; 16 NEW RR_CHECK assertions on `tests/renderer_tests.cpp` (renderer_tests grew 35 → 51). The OBSERVER.13 `ObserverBeta = 7` AOV is preserved verbatim. The kernel-arm bridge (the CUDA + OptiX program arms that compute + write the diagnostic AOV values) is DEFERRED to the renumbered OBS-PERCEPT.11 impl slot per the FIELD-I.7 → FIELD-I.9 → FIELD-I.11 staged-impl precedent.                                                                                                                                                                                                                                                                                                                                                          | PASS (data model) — kernel-arm bridge DEFERRED |
| 9  | Fixture scene exists and is isolated                   | `scenes/test_observer_primary_ray_perception.rrscene` (72 lines) ships at OBS-PERCEPT.9 (`8dc5d28`). Mirrors OBS-F.2 + FIELD-I.13 + FIELD-BEAUTY.7 geometry verbatim (6 spheres + ground + 2 lights); distinguishes via **two variables**: wider 60° camera FOV (vs OBS-F.2's 45°) + oblique beta direction `[0.6, -0.8, 0.0]` (vs OBS-F.2's axis-aligned `[0, 0, -1]`). Companion doc `docs/OBSERVER_PRIMARY_RAY_PERCEPTION_FIXTURE.md` (~470 lines) ships alongside with the 7-section FIELD-I.13 / FIELD-BEAUTY.7-style structure. **Isolation verified**: `git diff bda382c..8dc5d28 --name-only -- 'scenes/' ':(exclude)scenes/test_observer_primary_ray_perception.rrscene'` returns zero hits. Every pre-OBS-PERCEPT.* `.rrscene` fixture is byte-identical to the FIELD-BEAUTY.8 baseline. The OBS-PERCEPT.9 fixture is the canonical runtime-deferred SDK-host validation surface (per OBS-PERCEPT.9 companion doc §6). Audit-host `--scene-info` smoke verified parser cleanness: `observer_velocity = [0.3, -0.4, 0.0]`, `|beta| = 0.5`, camera FOV = 60°. | PASS    |
| 10 | Runtime CUDA / OptiX validation status                 | `PASS_WITH_RUNTIME_DEFERRED`. **Audit-host (OptiX OFF)**: 13/13 ctest PASS at every per-slice landing (OBS-PERCEPT.3 / .4 / .5 / .6 / .8 / .9). **OptiX-ON-no-SDK**: 14/14 ctest PASS at every per-slice landing (when the OptiX-on path was exercised; OBS-PERCEPT.3 / .5 / .8 landings empirically verified). **SDK-host**: DEFERRED across the entire arc — OBS-PERCEPT.4 check #10, OBS-PERCEPT.6 check #10 both carry runtime-deferred verdict. The arc's SDK-host runtime scenarios (from OBS-PERCEPT.4 §3.11 + OBS-PERCEPT.6 §3.11 + OBS-PERCEPT.9 companion doc §6): (i) default-invocation byte identity (both backends); (ii) relativistic-mode byte identity (both backends); (iii) CLI-override beta direction (both backends); (iv) cross-backend byte-identity cmp; (v) diagnostic AOV runtime (double-deferred behind both SDK-host AND the future OBS-PERCEPT.11 kernel-arm bridge); (vi) Doppler / searchlight interaction; (vii) OBS-F.2 + OBS-PERCEPT.9 cross-fixture comparison. The OBS-PERCEPT.9 fixture is the canonical SDK-host validation input; the future combined FIELD-* + OBS-PERCEPT CLI bridge slice's audit (per FIELD-BEAUTY.8 §4.2 (b)) is the canonical converging-leverage closure that converts every OBS-PERCEPT.4 + OBS-PERCEPT.6 + OBS-PERCEPT.10 deferred verdict to PASS. | DEFERRED |
| 11 | Remaining risks                                        | Three documented risks (see §3.10 for full detail): (a) **Debug AOV kernel-arm bridge not landed** — OBS-PERCEPT.8 shipped the data-model entries only; the kernel-arm write sites + payload threading + dispatcher allocation are deferred to the renumbered OBS-PERCEPT.11. (b) **SDK-host runtime validation requires SDK availability + future CLI bridge** — the OBS-PERCEPT.9 fixture is ready but the empirical PPM cmp requires a CUDA + OptiX-SDK host; the combined FIELD-* + OBS-PERCEPT CLI bridge slice would close this. (c) **Per-bounce Option B perception transform deferred** — the path-tracer's secondary bounce rays do NOT apply the perception transform (Option A primary-ray-only per OBS-PERCEPT.1 §5.2); a future FRAME-PROPAGATION.* arc would lift this if authorised. All three risks are scope-deferral (not bugs); each is documented honestly in the per-slice doc-comments + BUILD_PLAN entries.                                                                                                                                                                                                                                                                                                                                                                          | PASS (documented) |
| 12 | Recommended next safe stage                            | **HIGHLY RECOMMENDED — combined FIELD-* + OBS-PERCEPT CLI bridge slice** (per FIELD-BEAUTY.8 §4.2 (b) applied at the OBS-PERCEPT.* arc scope). Single SDK-host audit closes the entire field-and-observer-arc family's runtime-deferred verdict tail: FIELD-I.10 + FIELD-I.12 + FIELD-I.14 + FIELD-BEAUTY.4 + FIELD-BEAUTY.6 + FIELD-BEAUTY.8 + OBS-PERCEPT.4 + OBS-PERCEPT.6 + OBS-PERCEPT.10 deferred verdicts all convert to PASS once both arc families' kernel surfaces are reachable on SDK host. Alternative continuations: (a) OBS-PERCEPT.11 — debug AOV kernel-arm bridge (consumes OBS-PERCEPT.8 data-model entries; mirrors FIELD-I.9 + FIELD-I.11 staged-impl pattern); (b) manifold-orthogonal work (deferred SDK-host runtime pass; MANI-I.12 final cross-host audit; denoiser; path-tracer feature breadth). | PASS    |

---

## 3. REASONING SUMMARY

### 3.1 Arc shape

The OBS-PERCEPT.* arc spans nine per-slice commits
(OBS-PERCEPT.1 – OBS-PERCEPT.9) over the
post-FIELD-BEAUTY.8 baseline (`bda382c`). The
aggregate diff at the arc boundary:

```
$ git diff bda382c..8dc5d28 --stat
docs/BUILD_PLAN.md                                  | 2202 +
docs/OBSERVER_PERCEPTION_DEBUG_AOV_TASK.md          |  975 +
docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md             |  898 +
docs/OBSERVER_PRIMARY_RAY_OPTIX_AUDIT.md            |  920 +
docs/OBSERVER_PRIMARY_RAY_PERCEPTION_FIXTURE.md     |  659 +
docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md         |  713 +
docs/OBSERVER_SPACE_PERCEPTION_PLAN.md              |  984 +
scenes/test_observer_primary_ray_perception.rrscene |   72 +
src/cuda/CudaPathTracer.cu                          |   19 +
src/cuda/CudaTestKernel.cu                          |   42 +-
src/manifold/ObserverFrame.h                        |   84 +
src/optix/OptixPrograms.cu                          |   56 +-
src/renderer/AOV.cpp                                |   22 +
src/renderer/AOV.h                                  |   92 +
tests/manifold_identity_tests.cpp                   |  136 +
tests/renderer_tests.cpp                            |  107 +
```

Source-code surface (~558 net lines): the shared
`apply_observer_primary_ray_aberration(...)`
helper at `ObserverFrame.h:553+` + 2 CUDA files
(`CudaTestKernel.cu` 3 primary-ray sites +
`CudaPathTracer.cu` 1 site) + 1 OptiX file
(`OptixPrograms.cu` 2 raygen sites) + the OBS-PERCEPT.8
AOV data-model additions (`AOV.h` + `AOV.cpp` —
two new enumerators + factories). Test surface
(~243 lines): `manifold_identity_tests.cpp` +
`renderer_tests.cpp` extensions (+13 RR_CHECKs on
helper + 16 RR_CHECKs on AOV data-model entries).
Documentation surface (~6151 lines across 6 new
docs + BUILD_PLAN.md). Scene fixture (72 lines).
Zero CMakeLists.txt modification across the arc.

The narrow scope intentionally excludes every
other file from the operator's brief-by-brief
discipline: no `OptixRenderer.h` / `.cpp`
modification (kernel-arm-internal scope); no
`src/field/` / `src/io/` modifications; no
`src/core/Config.h` extension; no new CLI flag.

### 3.2 Checks #1 + #2 — CUDA + OptiX primary-ray aberration arms

Both backends consume the same shared helper at
`ObserverFrame.h:553+`. **Five-axis symmetry**
(per OBS-PERCEPT.6 §3.7):

| Axis | CUDA | OptiX |
|------|------|-------|
| POD type | `view.observer_frame` | `optixLaunchParams.observer_frame` (both `rr::manifold::ObserverFrame`) |
| Shared helper | `rr::manifold::apply_observer_primary_ray_aberration` | (same) |
| Dispatch shape | `if (perception_active) { unified helper } else { legacy }` | (same) |
| Math leaf | `rr::relativity::aberrateDirection(beta, direction)` two-arg form | (same) |
| Gate semantics | `perception_mode == ConstantVelocityMinkowski` + `|beta|² > 0` | (same) |

Cross-backend bit-identity is **structurally
guaranteed by construction**. Empirical SDK-host
verification (cmp `aov_beauty.ppm`
`optix_aov_beauty.ppm` on the OBS-PERCEPT.9
fixture) deferred per check #10.

### 3.3 Checks #3 + #4 — activation gates

The shared helper's two-gate logic is the load-
bearing structural contract:

- **Outer gate** (perception_mode): closes on
  `Identity` (the default) + on
  `CurvedChartGeodesicPlaceholder` (the
  reserved placeholder mode honored per master
  rule #3). Opens only on
  `ConstantVelocityMinkowski`.
- **Inner gate** (|beta| > 0): closes on
  zero-beta. NaN-safe squared-magnitude form
  (`!(beta2 > 0.0f)`) also closes on NaN
  components (defence-in-depth on top of the
  OBSERVER.6 adapter's pre-clamping).

Empirically pinned by 13 NEW RR_CHECK assertions
on `tests/manifold_identity_tests.cpp` (landed
at OBS-PERCEPT.3; manifold_identity_tests grew
from 408 → 421):

- `test_obs_percept_3_identity_mode_returns_input_direction`
  (3 RR_CHECKs): outer gate closes on Identity +
  non-zero beta → identity direction returned.
- `test_obs_percept_3_constant_velocity_zero_beta_returns_input`
  (3 RR_CHECKs): outer gate opens; inner gate
  closes on zero beta → identity direction
  returned.
- `test_obs_percept_3_constant_velocity_nonzero_beta_aberrates`
  (4 RR_CHECKs): both gates open + non-zero beta
  + ray-direction → boosted-direction is
  unit-length + non-trivially differs from input
  on transverse direction.
- `test_obs_percept_3_curved_placeholder_returns_input`
  (3 RR_CHECKs): outer gate closes on
  CurvedChartGeodesicPlaceholder + non-zero
  beta → identity direction returned (master
  rule #3 placeholder honesty).

### 3.4 Check #5 — three-layer default no-op anchor

The three-layer no-op anchor is the load-bearing
default-state preservation:

1. **Layer 1 — helper inner gate** (the
   OBS-PERCEPT.3 contract): explicit
   `!(beta2 > 0.0f)` short-circuit at
   `ObserverFrame.h:565-571`.
2. **Layer 2 — math leaf identity** (pre-existing):
   `rr::relativity::aberrateDirection(beta_vec,
   direction)` at `RelativityMath.h:118-119`
   short-circuits at `beta_mag <= 1.0e-12f`.
   Defence-in-depth.
3. **Layer 3 — OBSERVER.6 adapter** (host-side):
   emits `observer_frame.beta = (0, 0, 0)`
   exactly on zero-beta inputs (verified at
   OBSERVER.7 audit).

Identical anchor on both backends; both consume
the same shared helper. The composition guarantees
every existing `--render-*` invocation against
any scene WITHOUT
`--observer-perception-mode relativistic`
preserves byte-identical PPM output to the
post-FIELD-BEAUTY.8 baseline.

The default `ObserverFrame{}` carries
`perception_mode = Identity` (OBSERVER.2 audit's
check #2); the outer gate closes → the dispatch
else-branch fires → the legacy
`aberrateDirection(rel, ...)` path runs reading
`observer.velocity` (the post-OBS-P.2 fallback);
byte-identical to the post-OBS-P.2 +
FIELD-BEAUTY.5 baseline.

### 3.5 Check #6 — secondary rays unchanged

The OBS-PERCEPT.* arc's kernel-arm modifications
target ONLY the primary-ray generation block at
each of the five sites (3 CUDA + 2 OptiX). The
secondary-ray surfaces are byte-identical to the
pre-OBS-PERCEPT.3 baseline:

- **CUDA `k_pathtrace_sample`** bounce loop at
  `CudaPathTracer.cu:207+`: byte-identical.
  The helper call at line 220 is BEFORE the
  loop. Per-line diff
  `git diff bda382c..8dc5d28 -- src/cuda/CudaPathTracer.cu`
  shows the only addition is the 19 lines at
  lines 204-222 (helper call + doc-comment).
- **OptiX `__raygen__pathtrace`** per-spp loop
  at `OptixPrograms.cu:1281+`: byte-identical.
  The dispatch at lines 1268-1277 is INSIDE
  the per-spp primary-ray block (after
  `generate_camera_ray(...)`); the bounce
  loop body is unchanged.
- **OptiX closest-hit programs**
  (`__closesthit__radiance`,
  `__closesthit__pathtrace`): byte-identical.
- **OptiX miss programs** (`__miss__radiance`,
  `__miss__pathtrace`, `__miss__shadow`):
  byte-identical.

Master rule #12 + OBS-PERCEPT.1 §5.2 Option A
(primary-ray-only) honored throughout the arc.

### 3.6 Check #7 — Doppler / searchlight unchanged

The post-OBS-P.2 guarded ternary at the Doppler
/ searchlight call sites is preserved verbatim
on both backends:

- **CUDA `k_render_scene`**: the `rel` snapshot
  computation at `CudaTestKernel.cu:226-232`
  uses the OBS-P.2 ternary
  `(perception_active ? observer_frame.beta :
  observer.velocity)`; the Doppler factor
  computation + Doppler color shift +
  searchlight scaling blocks at lines 558-572
  consume the unchanged `rel` snapshot.
- **CUDA `k_sphere_relativistic`**: same shape
  at lines 351-356 + downstream Doppler /
  searchlight blocks.
- **OptiX `__raygen__pinhole`**: same shape at
  `OptixPrograms.cu:215-221` + downstream
  Doppler factor computation at line 257+.
- **OptiX `__raygen__pathtrace`**: same shape at
  lines 1216-1222 + downstream Doppler factor
  computation.

Per-line diff confirms zero changes inside the
Doppler / searchlight blocks. The OBS-PERCEPT.*
arc's Doppler / searchlight migration is
explicitly out-of-scope per the OBS-PERCEPT.2
task brief §4.3 + the OBS-PERCEPT.5 task brief
preserves the post-OBS-P.2 ternary at those
sites. Future OBS-PERCEPT.* sub-slices may
consolidate them into the unified perception
transform; out of scope for the present arc.

### 3.7 Check #8 — debug AOV status

**PARTIAL — data-model-only landed; kernel-arm
bridge deferred.**

The OBS-PERCEPT.7 task brief defined three
diagnostic AOVs:
- `observerAberrationMagnitude` — NEW (per-pixel
  magnitude of aberration delta).
- `observerBeta` — EXISTING (OBSERVER.13;
  preserved verbatim).
- `observerDirection` — NEW (LIFTED from
  OBSERVER.12 §2.2 deferred-FUTURE slot).

The OBS-PERCEPT.8 implementation slice landed
the **data-model entries** only (per the
FIELD-I.7 staged-impl precedent):
- `AOVType::ObserverAberrationMagnitude = 9`
  enumerator + factory + name + component
  count at `src/renderer/AOV.h` + `src/renderer/AOV.cpp`.
- `AOVType::ObserverDirection = 10` enumerator
  + factory + name + component count at the
  same files.
- 16 NEW RR_CHECK assertions on
  `tests/renderer_tests.cpp` (renderer_tests
  grew from 35 → 51).

The **kernel-arm bridge** (the CUDA + OptiX
program arms that compute + write the diagnostic
AOV values + the payload-field threading + the
dispatcher allocation + the PPM save sites) is
DEFERRED to the renumbered OBS-PERCEPT.11 impl
slot. This mirrors the FIELD-I.7 → FIELD-I.9 →
FIELD-I.11 staged-impl pattern:
- FIELD-I.7 shipped just the AOV data-model
  entry for `FieldScalar`.
- FIELD-I.9 added the CUDA kernel-arm bridge.
- FIELD-I.11 added the OptiX kernel-arm bridge.

OBS-PERCEPT.8 ships the same staged-impl Stage 1
for both new AOVs; OBS-PERCEPT.11 will be the
combined Stage 2 (CUDA + OptiX kernel-arm
bridge for both new AOVs).

The OBSERVER.13 `ObserverBeta = 7` AOV is
preserved verbatim — its kernel-arm bridge
landed at OBSERVER.13 already.

### 3.8 Check #9 — fixture isolation

The OBS-PERCEPT.9 fixture preserves the
**one-variable-difference principle** the FIELD-*
arc family established:

| Fixture                                                            | Beta direction        | FOV  | Engaged blocks                              |
|--------------------------------------------------------------------|-----------------------|------|---------------------------------------------|
| `test_observer_frame.rrscene` (OBS-F.2)                            | `[0, 0, -1]` axis     | 45°  | relativity                                  |
| `test_observer_primary_ray_perception.rrscene` (OBS-PERCEPT.9)     | `[0.6, -0.8, 0]` XY   | 60°  | relativity                                  |
| `test_scalar_field_diagnostic.rrscene` (FIELD-I.13)                | (no relativity)       | 45°  | scalar_field                                |
| `test_scalar_field_color_multiplier.rrscene` (FIELD-BEAUTY.7)      | (no relativity)       | 45°  | scalar_field + field_mapping                |
| `test_scalar_field_emission.rrscene` (FIELD-BEAUTY.7)              | (no relativity)       | 45°  | scalar_field + field_mapping                |
| `test_schwarzschild_like_manifold.rrscene` (SCHW.9)                | (no relativity)       | 45°  | manifold                                    |
| `test_penrose_like_manifold.rrscene` (PENROSE.10)                  | (no relativity)       | 45°  | manifold                                    |

OBS-PERCEPT.9 isolates the observer-perception
variable (oblique-direction relativity) from
every other arc family's authoring surface.
Geometry layer is verbatim from OBS-F.2 +
FIELD-I.13 + FIELD-BEAUTY.7 (same 6 spheres +
ground plane + 2 lights).

**Isolation verified** via `git diff
bda382c..8dc5d28 --name-only -- 'scenes/'
':(exclude)scenes/test_observer_primary_ray_perception.rrscene'`
returns zero hits. Every pre-OBS-PERCEPT.* `.rrscene`
fixture is byte-identical to the FIELD-BEAUTY.8
baseline.

Audit-host smoke verified at OBS-PERCEPT.9 landing
(via `--scene-info`): parser correctly extracts
camera FOV = 60°, `observer_velocity = [0.3, -0.4,
0.0]`, `|beta| = 0.5`, all 7 relativity
parameters.

### 3.9 Check #10 — runtime status

The arc's runtime status is
`PASS_WITH_RUNTIME_DEFERRED`. The deferral has
two honest framings:

**Frame A — audit-host SDK absence.** The
audit-host build is `RR_ENABLE_CUDA=OFF +
RR_ENABLE_OPTIX=OFF`; neither kernel can be
launched. Property of the audit environment, not
the arc.

**Frame B — combined CLI bridge unfilled.** Even
on an SDK host today, the OBS-PERCEPT.* arc's
runtime PPM verification would require:
- The OBS-F.2 or OBS-PERCEPT.9 fixture (both
  available).
- The OBSERVER.4 `--observer-perception-mode
  relativistic` CLI flag (available).
- BUT: the SDK-host audit pass that runs the
  PPM cmp scenarios + the diagnostic-AOV pass
  + the cross-fixture comparison requires
  dedicated audit infrastructure. The
  combined FIELD-* + OBS-PERCEPT CLI bridge
  slice (per FIELD-BEAUTY.8 §4.2 (b)) is the
  canonical converging-leverage closure that
  ships the dedicated infrastructure +
  exercises the deferred scenarios in one
  audit pass.

The seven deferred SDK-host scenarios
(synthesized from OBS-PERCEPT.4 §3.11 +
OBS-PERCEPT.6 §3.11 + OBS-PERCEPT.9 companion
doc §6 + OBS-PERCEPT.7 task brief §8):

- (i) default-invocation byte identity (both
  backends; OBS-F.2 + OBS-PERCEPT.9 fixtures).
- (ii) relativistic-mode byte identity (both
  backends).
- (iii) CLI-override beta direction (both
  backends).
- (iv) cross-backend byte-identity cmp.
- (v) diagnostic AOV runtime (double-deferred
  behind both SDK-host AND the future
  OBS-PERCEPT.11 kernel-arm bridge slice).
- (vi) Doppler / searchlight interaction
  (verifies the post-OBS-P.2 ternary preservation
  at runtime).
- (vii) OBS-F.2 + OBS-PERCEPT.9 cross-fixture
  comparison (different visible patterns from
  axis-aligned vs oblique beta directions).

All seven defer to the future combined CLI
bridge slice's audit (or to a dedicated
OBS-PERCEPT-RUNTIME.* sub-arc if the operator
prefers narrower sequencing).

### 3.10 Check #11 — remaining risks

Three documented scope-deferrals (not bugs):

**Risk A: Debug AOV kernel-arm bridge not
landed.** The OBS-PERCEPT.8 slice shipped the
AOV data-model entries
(`AOVType::ObserverAberrationMagnitude = 9` +
`ObserverDirection = 10` + factories +
component counts + names + 16 RR_CHECKs); the
kernel-arm bridge (CUDA + OptiX program arms
that compute + write the diagnostic values +
payload threading + dispatcher allocation) is
DEFERRED to the renumbered OBS-PERCEPT.11
impl slot. **Mitigation**: this is the FIELD-I.7
→ FIELD-I.9 → FIELD-I.11 staged-impl precedent
shape applied to the OBS-PERCEPT.* arc; the
data-model entries are honest scaffolding for
the future bridge slice. The pre-aberration
direction snapshot discipline is documented in
the OBS-PERCEPT.7 task brief §4.1.

**Risk B: SDK-host runtime validation requires
SDK availability + future CLI bridge.** The
OBS-PERCEPT.9 fixture is ready; the OBS-PERCEPT.4
+ OBS-PERCEPT.6 audits' runtime-deferred
scenarios are documented; the OBS-PERCEPT.7
task brief §8 enumerates additional debug-AOV
runtime scenarios. The empirical validation
requires (a) a CUDA + OptiX-SDK host AND (b)
the combined CLI bridge slice infrastructure
(or dedicated runtime audit infrastructure).
**Mitigation**: this matches the
PASS_WITH_RUNTIME_DEFERRED pattern that closes
the FIELD-I.10 / .12 / .14 + FIELD-BEAUTY.4 /
.6 / .8 deferred verdicts at the same combined
CLI bridge slice. The OBS-PERCEPT.10 deferred
verdicts naturally fold into this closure.

**Risk C: Per-bounce Option B perception
transform deferred.** The path-tracer's
secondary bounce rays do NOT apply the
perception transform (Option A primary-ray-
only per the OBS-PERCEPT.1 plan §5.2). For
fully relativistic-perception path tracing, a
future FRAME-PROPAGATION.* arc would lift to
Option B (per-bounce perception application).
**Mitigation**: Option B is explicitly out of
scope per the OBS-PERCEPT.1 plan §5.2; the
primary-ray-only scope is sufficient for the
documented arc semantic (the per-pixel beauty
PPM shows the observer's primary-ray
aberration; per-bounce perception would
amplify the effect but isn't required for
visual demonstration). The kernel-arm
doc-comments at `CudaPathTracer.cu:204+` +
`OptixPrograms.cu:1253+` document Option B as
deferred future work.

### 3.11 Check #12 — recommended next safe stage

**HIGHLY RECOMMENDED — combined FIELD-* +
OBS-PERCEPT CLI bridge slice** (per
FIELD-BEAUTY.8 §4.2 (b) applied at this arc's
scope). Single SDK-host audit closes the
**entire field-and-observer-arc family's
runtime-deferred verdict tail**:

| Audit | Pre-CLI-bridge Verdict | Post-CLI-bridge Verdict |
|-------|------------------------|--------------------------|
| FIELD-I.10 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-I.12 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-I.14 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-BEAUTY.4 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-BEAUTY.6 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| FIELD-BEAUTY.8 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| OBS-PERCEPT.4 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| OBS-PERCEPT.6 | PASS_WITH_RUNTIME_DEFERRED | PASS |
| **OBS-PERCEPT.10 (this audit)** | **PASS_WITH_RUNTIME_DEFERRED** | **PASS** |

Best converging-leverage option if the operator
has SDK-host access. The combined slice's CLI
surface would ship:
- `--observer-perception-debug` (or reuse the
  existing `--observer-debug` per OBS-PERCEPT.7
  task brief §3.3) — engages the OBS-PERCEPT.8
  diagnostic AOVs.
- The OBS-PERCEPT.11 kernel-arm bridge (lands
  the CUDA + OptiX program arms that compute +
  write the diagnostic AOV values).
- Dispatcher allocation + PPM save sites for
  the new AOVs.

Alternative continuations:

**(a) OBS-PERCEPT.11 — debug AOV kernel-arm
bridge** (the renumbered next OBS-PERCEPT.*
impl slot). Consumes the OBS-PERCEPT.8
data-model entries + adds the CUDA + OptiX
program arms + the payload-field threading +
the dispatcher allocation + the PPM save sites.
Mirrors the FIELD-I.9 + FIELD-I.11 staged-impl
pattern.

**(b) Manifold-orthogonal work.** Multiple
options:
- **Deferred SDK-host runtime pass** for the
  entire arc family (OBSERVER.* + OBS-P.* +
  OBS-F.* + FIELD-I.* + FIELD-BEAUTY.* +
  OBS-PERCEPT.*) — highest converging-leverage
  option.
- **MANI-I.12 final cross-host manifold audit**.
- **Denoiser integration with chart-aware
  AOVs**.
- **Path-tracer feature breadth** (NEE
  extension, BSDF expansion, MIS tuning).

**(c) FRAME-PROPAGATION.\* arc** (per OBS-PERCEPT.1
§5.2 Option B). Lifts to per-bounce perception
transform for fully relativistic path tracing.
HIGHER-RISK; defer until OBS-PERCEPT.* arc's
primary-ray-only contract is empirically
verified on SDK host.

### 3.12 Master-rule satisfaction recap

- **Master rule #1 ("Build incrementally"):**
  satisfied. Nine per-slice impl + audit +
  fixture slices (1 plan → 2 task briefs → 2
  impl + 2 audit pairs → 1 AOV data-model
  impl → 1 fixture). Each slice was followed
  by an audit gate (per-slice or arc-level);
  this OBS-PERCEPT.10 capstone closes the
  arc.

- **Master rule #3 ("no fake stubs"):**
  satisfied across every slice. The shared
  helper at `ObserverFrame.h:553+` is fully
  wired with real arithmetic + real branches.
  The `CurvedChartGeodesicPlaceholder` mode's
  no-transform fallback is honest (master rule
  #3: the helper short-circuits to identity;
  future CURVED-CHART arc would lift). The
  OBS-PERCEPT.8 AOV data-model entries are
  fully-formed (real enum + factory + name +
  component count; 16 RR_CHECKs verify); the
  doc-comments document the future kernel-arm
  contract honestly.

- **Master rule #11 ("explicit, testable
  interfaces"):** satisfied. The shared
  helper's three-gate logic is empirically
  pinned by 13 RR_CHECK assertions on
  `manifold_identity_tests.cpp` (OBS-PERCEPT.3
  landed; mirrors the FIELD-I.3 / FIELD-I.5
  test-pinning precedent). The two new AOV
  data-model entries are empirically pinned
  by 16 RR_CHECK assertions on
  `renderer_tests.cpp` (OBS-PERCEPT.8 landed;
  mirrors the FIELD-I.7 + OBSERVER.13
  test-trio precedent verbatim). The
  five-axis cross-backend symmetry argument
  (OBS-PERCEPT.6 §3.7) rests on inspectable
  file/line references.

- **Master rule #12 ("do not overbuild a
  later system before the current layer
  works"):** satisfied. Each per-slice scope
  was deliberately narrow:
    - OBS-PERCEPT.3 = CUDA primary-ray only;
      no OptiX; no Doppler/searchlight
      consolidation.
    - OBS-PERCEPT.5 = OptiX primary-ray only;
      no CUDA changes.
    - OBS-PERCEPT.8 = AOV data-model only;
      no kernel arm.
    - OBS-PERCEPT.9 = fixture only; no
      kernel arm; no parser extension.
  The kernel-arm bridge for the diagnostic
  AOVs + the SDK-host runtime validation +
  the Option B per-bounce transform are all
  deferred (per the documented risks at check
  #11). The OBS-PERCEPT.* arc opens parallel
  to the FIELD-I.* + FIELD-BEAUTY.* arc
  family without overlap.

- **Master rule #16 ("default-off /
  reasoning-traceable defaults"):** satisfied.
  The OBS-PERCEPT.* default state is unchanged
  from the FIELD-BEAUTY.8 baseline on both
  backends:
    - No `--render-*` action's output
      changes by default.
    - No existing PPM filename changes.
    - No new file produced by default.
    - No existing AOV slot's value changes.
  The single observable behaviour change is
  the structural presence of the unified
  helper + the kernel-arm dispatches + the
  new AOV enumerators; their observable
  behaviour from every default CLI invocation
  is zero because gates close + AOV pointers
  remain null.

### 3.13 Honest scope recap

The OBS-PERCEPT.* arc is a **kernel-arm +
helper-leaf + payload-aware + AOV-data-model +
fixture arc on the audit-host side**, with the
**SDK-host runtime validation + debug-AOV
kernel-arm bridge + Option B per-bounce
transform all deferred**. The verdict
`PASS_WITH_RUNTIME_DEFERRED` honestly captures
this:

- The arc's structural content is complete +
  verified on the audit-host.
- The runtime verification of the kernel
  arms' composed Lorentz-boost aberration
  output (the seven SDK-host scenarios from
  §3.9) is reserved for the future combined
  CLI bridge slice's audit.
- The debug AOV kernel-arm bridge is reserved
  for the OBS-PERCEPT.11 impl slot.

The combined CLI bridge slice would close
this audit's runtime-deferred verdict tail
PLUS the entire FIELD-I.* + FIELD-BEAUTY.*
arc family's verdict tails in one converging-
leverage operation.

---

## 4. NEXT

### 4.1 Renumbered OBS-PERCEPT.* sub-slice ladder

The OBS-PERCEPT.10 capstone audit closes the
OBS-PERCEPT.* arc's per-slice gate chain. The
post-OBS-PERCEPT.10 ladder for remaining
deferred work is:

- **OBS-PERCEPT.11** — Debug AOV kernel-arm
  bridge (consumes OBS-PERCEPT.8 data-model
  entries; lands CUDA + OptiX program arms +
  payload threading + dispatcher allocation +
  PPM save sites; mirrors FIELD-I.9 +
  FIELD-I.11 staged-impl precedent shape).
- **OBS-PERCEPT.12** — Debug AOV kernel-arm
  bridge audit.
- **OBS-PERCEPT.13** — Arc-wide SDK-host
  runtime pass (alternative to the combined
  CLI bridge; exercises the OBS-PERCEPT.9
  fixture end-to-end on a CUDA + OptiX-SDK
  host; verifies the seven SDK-host scenarios
  from §3.9 empirically; produces the
  canonical SDK-host audit that converts the
  OBS-PERCEPT.4 + OBS-PERCEPT.6 +
  OBS-PERCEPT.10 deferred verdicts to PASS).

The ladder above is the **operator's choice**;
the combined CLI bridge slice (per FIELD-BEAUTY.8
§4.2 (b)) collapses OBS-PERCEPT.13 into a
single converging-leverage slice that closes
the entire field-and-observer-arc family.

### 4.2 Candidate next slots (prioritised)

**(a) HIGHLY RECOMMENDED — combined FIELD-* +
OBS-PERCEPT CLI bridge slice** (per
FIELD-BEAUTY.8 §4.2 (b)). Closes the entire
field-and-observer-arc family's runtime-
deferred verdict tail in one SDK-host audit.
Best converging-leverage option.

**(b) RECOMMENDED — OBS-PERCEPT.11: debug AOV
kernel-arm bridge** (the renumbered next
OBS-PERCEPT.* impl slot). Consumes the
OBS-PERCEPT.8 data-model entries + lands the
kernel-arm bridge for both new AOVs
(`ObserverAberrationMagnitude` +
`ObserverDirection`). Mirrors the FIELD-I.9 +
FIELD-I.11 staged-impl pattern. Once landed,
the future SDK-host runtime audit can
empirically verify the per-pixel diagnostic
output against the OBS-PERCEPT.9 fixture's
expected signature (per the OBS-PERCEPT.7 task
brief §8.4).

**(c) Manifold-orthogonal work.** Multiple
options available:
- **Deferred SDK-host runtime pass** for the
  entire arc family (highest converging-
  leverage option).
- **MANI-I.12 final cross-host manifold
  audit**.
- **Denoiser integration with chart-aware
  AOVs**.
- **Path-tracer feature breadth**.

**(d) NOT RECOMMENDED — FRAME-PROPAGATION.* arc
(per-bounce Option B perception transform)
before the primary-ray contract is empirically
verified on SDK host.** Higher-risk than the
combined CLI bridge; better to validate the
existing Option A contract empirically first
before expanding to per-bounce.

**(e) DEFERRABLE — RETROACTIVE task brief
authoring.** The operator may choose to
backfill the missing FIELD-BEAUTY.1 +
FIELD-BEAUTY.2 + FIELD_INTERPRETATION_PHASE1_AUDIT.md
task brief / audit slots (carried forward from
FIELD-BEAUTY.8 capstone's §4.2 (f) discretion
note). The honest-framing approach has worked
across both arc families.

---

## 5. REFERENCES

### 5.1 Master references

- `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`
  (core engineering rules; master rule #1 +
  #3 + #11 + #12 + #16 satisfaction recap at
  §3.12).
- `docs/MANIFOLD_RENDERING_ARCHITECTURE.md`
  §7.2 (the observer-frame Lorentz boost
  concept the OBS-PERCEPT.* arc operationalises).

### 5.2 OBS-PERCEPT.* arc references

- `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`
  (OBS-PERCEPT.1).
- `docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`
  (OBS-PERCEPT.2).
- `docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md`
  (OBS-PERCEPT.4 — the precedent CUDA-side
  bridge audit synthesised at this capstone's
  checks #1 + #3 + #4 + #5 + #6 + #7).
- `docs/OBSERVER_PRIMARY_RAY_OPTIX_AUDIT.md`
  (OBS-PERCEPT.6 — the precedent OptiX-side
  bridge audit; the §3.7 five-axis cross-
  backend symmetry argument underpins this
  capstone's check #2).
- `docs/OBSERVER_PERCEPTION_DEBUG_AOV_TASK.md`
  (OBS-PERCEPT.7).
- `docs/OBSERVER_PRIMARY_RAY_PERCEPTION_FIXTURE.md`
  (OBS-PERCEPT.9 — the fixture companion doc;
  this audit references §6 for the
  runtime-deferred SDK-host scenarios).

### 5.3 OBSERVER.* + OBS-P.* + OBS-F.* arc references

- `docs/OBSERVER_FRAME_DATA_MODEL_AUDIT.md`
  (OBSERVER.3 — the default-constructed
  `ObserverFrame{}` underpinning §3.4 Layer 3).
- `docs/OBSERVER_FRAME_CONFIG_AUDIT.md`
  (OBSERVER.5).
- `docs/CAMERA_TO_OBSERVER_ADAPTER_AUDIT.md`
  (OBSERVER.7 — the zero-beta + clamp-safety
  contracts underpinning §3.4 Layer 3).
- `docs/OBSERVER_CUDA_PAYLOAD_AUDIT.md`
  (OBSERVER.9).
- `docs/OBSERVER_OPTIX_PAYLOAD_AUDIT.md`
  (OBSERVER.11).
- `docs/OBSERVER_DEBUG_AOV_AUDIT.md`
  (OBSERVER.14 — the `ObserverBeta = 7` AOV
  audit; this capstone's check #8 preserves
  OBSERVER.13 + .14 verbatim).
- `docs/OBSERVER_FRAME_ARC_AUDIT.md`
  (OBSERVER.15 — the capstone whose runtime-
  deferred risk #1 is now closed on BOTH
  backends + audited).
- `docs/OBSERVER_PERCEPTION_KERNEL_MIGRATION_AUDIT.md`
  (OBS-P.3 — the precedent kernel-migration
  audit; check #5 noted the CUDA path tracer
  had no pre-existing aberration call, which
  OBS-PERCEPT.3 added).
- `docs/OBSERVER_FRAME_FIXTURE_AUDIT.md`
  (OBS-F.3 — the precedent fixture audit
  underpinning this capstone's check #9
  one-variable-difference principle).

### 5.4 Parallel-arc references

- `docs/FIELD_SCALAR_BEAUTY_MAPPING_AUDIT.md`
  (FIELD-BEAUTY.8 — the precedent arc-capstone
  audit shape this audit mirrors; the §4.2
  (b) combined CLI bridge recommendation
  carries forward).
- `docs/FIELD_SCALAR_OPTIX_BRIDGE_AUDIT.md`
  (FIELD-I.12 — the precedent OptiX-bridge
  five-axis symmetry framework).
- `docs/FIELD_SCALAR_BEAUTY_OPTIX_AUDIT.md`
  (FIELD-BEAUTY.6 — the OBS-PERCEPT.6 audit's
  §3.7 inherits this framework).

### 5.5 Source surface audited (arc-wide)

The OBS-PERCEPT.* arc touched the following
source files (relative to the arc baseline
`bda382c`):

| File                                            | Net lines | Slice |
|-------------------------------------------------|-----------|-------|
| `src/manifold/ObserverFrame.h`                  | +84       | OBS-PERCEPT.3 |
| `src/cuda/CudaTestKernel.cu`                    | +42       | OBS-PERCEPT.3 |
| `src/cuda/CudaPathTracer.cu`                    | +19       | OBS-PERCEPT.3 |
| `src/optix/OptixPrograms.cu`                    | +56       | OBS-PERCEPT.5 |
| `src/renderer/AOV.h`                            | +92       | OBS-PERCEPT.8 |
| `src/renderer/AOV.cpp`                          | +22       | OBS-PERCEPT.8 |
| `tests/manifold_identity_tests.cpp`             | +136      | OBS-PERCEPT.3 |
| `tests/renderer_tests.cpp`                      | +107      | OBS-PERCEPT.8 |
| `scenes/test_observer_primary_ray_perception.rrscene` | 72  | OBS-PERCEPT.9 |

Total source-code surface: ~558 net lines
across 6 source files + 2 test files. Total
scene surface: 72 lines across 1 fixture.
Zero CMakeLists.txt change across the arc.

### 5.6 Documentation surface produced (arc-wide)

| File                                              | Lines | Slice |
|---------------------------------------------------|-------|-------|
| `docs/OBSERVER_SPACE_PERCEPTION_PLAN.md`          | 984   | OBS-PERCEPT.1 |
| `docs/OBSERVER_PRIMARY_RAY_TRANSFORM_TASK.md`     | 713   | OBS-PERCEPT.2 |
| `docs/OBSERVER_PRIMARY_RAY_CUDA_AUDIT.md`         | 898   | OBS-PERCEPT.4 |
| `docs/OBSERVER_PRIMARY_RAY_OPTIX_AUDIT.md`        | 920   | OBS-PERCEPT.6 |
| `docs/OBSERVER_PERCEPTION_DEBUG_AOV_TASK.md`      | 975   | OBS-PERCEPT.7 |
| `docs/OBSERVER_PRIMARY_RAY_PERCEPTION_FIXTURE.md` | 659   | OBS-PERCEPT.9 |
| `docs/OBSERVER_PERCEPTION_ARC_AUDIT.md`           | ~1025 | OBS-PERCEPT.10 (this doc) |
| `docs/BUILD_PLAN.md`                              | per-slice entries | OBS-PERCEPT.1 – .10 |

### 5.7 Surrounding commit SHAs

- OBS-PERCEPT.1: `8db1f9c` (arc plan).
- OBS-PERCEPT.2: `0bf2bb8` (primary-ray
  transform task brief).
- OBS-PERCEPT.3: `b653e48` (CUDA impl).
- OBS-PERCEPT.4: `40bb476` (CUDA audit).
- OBS-PERCEPT.5: `1dbeb23` (OptiX impl).
- OBS-PERCEPT.6: `3d125ad` (OptiX audit).
- OBS-PERCEPT.7: `66a2046` (debug AOV task
  brief).
- OBS-PERCEPT.8: `95d56d5` (debug AOV
  data-model impl).
- OBS-PERCEPT.9: `8dc5d28` (fixture +
  companion doc).
- Arc baseline (pre-OBS-PERCEPT.1):
  `bda382c` (FIELD-BEAUTY.8 capstone; last
  commit before the OBS-PERCEPT.* arc opened).

### 5.8 Audit-host empirical state at this capstone

- `ctest`: 13/13 PASS on the audit-host build
  (`RR_ENABLE_OPTIX=OFF`).
- Per-binary:
  - `relativity_tests: 841/841 passed`
    (unchanged from arc baseline).
  - `manifold_identity_tests: 421/421 passed`
    (+13 NEW from OBS-PERCEPT.3 vs 408
    baseline).
  - `cli_tests: 274/274 passed` (unchanged).
  - `renderer_tests: 51/51 passed` (+16 NEW
    from OBS-PERCEPT.8 vs 35 baseline).
  - `field_tests: 135/135 passed` (unchanged
    from FIELD-I.4).
  - Every other suite unchanged.
- OptiX-ON-no-SDK build at each relevant
  per-slice landing (OBS-PERCEPT.3 / .5 / .8):
  14/14 ctest PASS (including `optix_tests`).
- `git diff bda382c..8dc5d28 --name-only --
  'src/manifold/' ':(exclude)src/manifold/ObserverFrame.h'`:
  zero hits (the shared helper is the only
  manifold-side modification; no chart /
  metric / geodesic surface touched).
- `git diff bda382c..8dc5d28 --name-only --
  'scenes/' ':(exclude)scenes/test_observer_primary_ray_perception.rrscene'`:
  zero hits (fixture isolation verified at
  check #9).

### 5.9 Single-source-of-truth math leaves

The OBS-PERCEPT.* arc consumes the existing
`src/relativity/` math leaves verbatim:

- `rr::relativity::aberrateDirection(beta_vec,
  direction)` — RR_HD inline two-argument
  form at `RelativityMath.h:112+` (the math
  leaf the unified helper invokes when both
  gates open).
- `rr::relativity::precompute_relativity(beta_vec)`
  — RR_HD inline at `RelativityMath.h:160+`
  (consumed by the unchanged Doppler /
  searchlight call sites).

The arc adds **one new** helper at
`src/manifold/ObserverFrame.h:553+`:
`rr::manifold::apply_observer_primary_ray_aberration(observer_frame,
direction)` — RR_HD inline; composes the
three-gate logic + the math leaf invocation.

Cross-backend bit-identity for the perception
transform is structurally guaranteed by both
backends consuming the same shared helper +
the same math leaf (same RR_HD inline code on
both CUDA + OptiX).

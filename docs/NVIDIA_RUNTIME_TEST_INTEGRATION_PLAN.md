# NVIDIA CUDA + OptiX SDK Runtime Test — Integration Plan

Planning document for integrating outstanding branch work into a single
temporary runtime-test branch, exercising it against the NVIDIA CUDA Toolkit
and OptiX SDK on a real GPU, and rolling back cleanly afterwards. Derived
from `docs/BRANCH_INVENTORY_FOR_NVIDIA_TEST.md`.

- Date: 2026-05-18
- Authoring branch: `claude/discover-branches-ZrrgE` (diagnostic only)
- Temporary test branch: `nvidia-sdk-runtime-test`
  (created locally at this commit, not pushed)
- Recommended base: `origin/claude/rewrite-rendering-core-De71I` @ `107300e`

This plan does **not** execute any merges, cherry-picks, or source edits.
It defines the exact sequence and conditions under which those operations
would be performed once the user authorizes them.

---

## 1. Decisions Carried Forward from the Inventory

### Safest canonical runtime-test base

`origin/claude/rewrite-rendering-core-De71I` @ `107300e` is the only viable
base. It is a strict superset of `main` (+108 / -0), contains all
manifold / observer / field source modules, and is the only branch that
actually builds the new CUDA and OptiX program arms for that work. Any
other base would lose source code, not just docs.

### Branches by topical area

| Area                                    | Branch                                          | State on base?            |
|-----------------------------------------|-------------------------------------------------|---------------------------|
| (a) Manifold work                       | `claude/rewrite-rendering-core-De71I`           | Native — already present  |
| (b) Observer work                       | `claude/rewrite-rendering-core-De71I`           | Native — already present  |
| (c) Field interpretation                | `claude/rewrite-rendering-core-De71I`           | Native — already present  |
| (d) Area-light work                     | `area-light-arc`                                | Docs-only, not on base    |
| (e) Latest renderer / path tracer       | `claude/rewrite-rendering-core-De71I`           | Native — already present  |

(a), (b), (c), (e) all live exclusively on the base branch. (d) is a single
1763-line planning document with no source impact.

### Merge state

| Branch                                          | Status                                                                  |
|-------------------------------------------------|-------------------------------------------------------------------------|
| `relativity-core-v1-clean`                      | Already merged into `main` via PR #1. No unique content.                |
| `relativity-core-v1`                            | Effectively merged — its source matches `main`; only one duplicate MIS.7 audit doc remains.       |
| `main`                                          | Trunk; fully contained inside the chosen base.                          |
| `claude/recreate-mis-audit-7pQQN`               | Single duplicate MIS.7 audit commit; superseded by `main`.              |
| `claude/create-docs-architecture-T2Dp5`         | Fossil M0–M6 timeline; 267-file delta with 140 687 deletions vs `main`. |
| `area-light-arc`                                | Not merged. One docs commit unique vs the chosen base.                  |
| `claude/discover-branches-ZrrgE`                | Diagnostic only; carries the inventory + this plan.                     |

### Branches requiring cherry-pick

Only `area-light-arc` carries unmerged content that is unique vs the base:

- `447b909` — `docs: AREA arc planning document`
  (adds `docs/PATH_TRACER_AREA_LIGHT_PLAN.md`, 1763 lines, no source).

`git merge-tree` against the base produces a clean tree — no conflicts.

### Conflicting / divergent branches

| Branch                                          | Risk     | Reason                                                                                                  |
|-------------------------------------------------|----------|---------------------------------------------------------------------------------------------------------|
| `claude/create-docs-architecture-T2Dp5`         | HIGH     | Disjoint timeline branched from the very first commit; modern files like `integrations/c4d/*` and `tools/verify_cuda_host.py` were intentionally removed downstream. Merging would resurrect ~140k deleted lines. |
| `relativity-core-v1`                            | Low      | Only adds one duplicate MIS.7 audit doc and an older `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`. No source delta vs `main`.                                                                                              |
| `area-light-arc`                                | None     | Single new doc file, no source overlap with the base.                                                  |

### Branches safe to ignore for this test

- `relativity-core-v1-clean` (merged).
- `claude/recreate-mis-audit-7pQQN` (stale duplicate).
- `claude/create-docs-architecture-T2Dp5` (fossil — explicitly do NOT
  integrate).
- `relativity-core-v1` (superseded by `main`).
- `main` (already a subset of the base).
- `claude/discover-branches-ZrrgE` (diagnostic; only useful for the
  inventory + plan).

---

## 2. Temporary Test Branch

```
git branch nvidia-sdk-runtime-test origin/claude/rewrite-rendering-core-De71I
git branch --unset-upstream nvidia-sdk-runtime-test
```

- Created at `107300e`.
- Upstream tracking deliberately unset so a future push goes to a new
  remote of the same name, not back into `rewrite-rendering-core-De71I`.
- Will accept (in this order) the integration steps in §3 once the user
  authorizes them.

This plan deliberately leaves the new branch as a pristine copy of the
base. No commits, no merges, no cherry-picks have been applied yet.

---

## 3. Integration Order

Execute strictly top-to-bottom. Each step has an explicit gate; do not
proceed past a red gate.

### Step 0 — Pre-flight (read-only)

- Confirm `git status` is clean and HEAD is `107300e` on
  `nvidia-sdk-runtime-test`.
- Confirm CUDA Toolkit version (`nvcc --version`).
- Confirm OptiX SDK headers are reachable (`$OptiX_INSTALL_DIR` or the
  CMake `find_package(OptiX)` path).
- Confirm GPU visible (`nvidia-smi`) and compute capability.

Gate: all four checks pass. If CUDA Toolkit or OptiX SDK is missing,
limit the run to CUDA-OFF / OptiX-OFF configurations and document the
gap; do not attempt to fake the SDK presence.

### Step 1 — Baseline build verification on the bare base

No integration yet. Build `nvidia-sdk-runtime-test` four times (matrix
in §6) before introducing any new content. Establishes a clean reference
for rollback comparison.

Gate: at least the CUDA-OFF / OptiX-OFF configuration must build and
pass `ctest --output-on-failure`. CUDA-ON and OptiX-ON failures are not
yet blocking — the point of the runtime test is to surface them.

### Step 2 — Cherry-pick the AREA planning doc (optional, low-risk)

```
git cherry-pick 447b909         # docs: AREA arc planning document
```

- Adds only `docs/PATH_TRACER_AREA_LIGHT_PLAN.md`.
- Merge-tree probe against the base is clean.
- Risk: none. Build behaviour unchanged.
- Recommend: include only if the AREA plan needs to be visible alongside
  manifold/observer/field docs during the runtime session. Otherwise skip
  — it has no runtime impact.

Gate: `git status` clean post cherry-pick. Re-run Step 1 if anything
unexpected appears.

### Step 3 — Build matrix from §6

Run the four configurations sequentially. Capture each configure log,
build log, and ctest log under `build-logs/<config>/`. Do not commit
logs to the temp branch; treat them as ephemeral artefacts.

Gate: collect results, do not auto-fix. Surface failures back to the
user before any source change.

### Step 4 — Targeted runtime scenes (§5)

Render the fixture scenes listed in §5 with `RR_ENABLE_CUDA=ON` (and
again with `RR_ENABLE_OPTIX=ON` where applicable). Compare AOV outputs
across backends.

Gate: collect outputs, do not auto-fix.

### Step 5 — Wrap-up

- Write a short runtime report (out of scope for this plan).
- Decide whether observed runtime fixes should land back on
  `claude/rewrite-rendering-core-De71I` directly, or on a new feature
  branch off it. Do **not** push fixes from
  `nvidia-sdk-runtime-test` to origin; it is throwaway.
- Apply rollback (§7).

---

## 4. Per-Branch Merge vs Cherry-Pick Recommendation

| Branch                                          | Action                | Mechanism      | Conflict risk | Reason                                                                                                                                              |
|-------------------------------------------------|-----------------------|----------------|---------------|------------------------------------------------------------------------------------------------------------------------------------------------------|
| `claude/rewrite-rendering-core-De71I`           | **Use as base**       | n/a            | n/a           | Already chosen.                                                                                                                                      |
| `area-light-arc`                                | **Cherry-pick** `447b909` (optional) | `git cherry-pick 447b909` | None          | Single self-contained doc; no source overlap. Merging the whole branch would also pull `cee451e`, a duplicate of the MIS.7 audit already on `main`.   |
| `main`                                          | Skip                  | n/a            | n/a           | Strict subset of the base.                                                                                                                           |
| `relativity-core-v1`                            | Skip                  | n/a            | n/a           | Source already on base; only a duplicate audit doc and an older MASTER instructions file remain.                                                     |
| `relativity-core-v1-clean`                      | Skip                  | n/a            | n/a           | Already merged via PR #1.                                                                                                                            |
| `claude/recreate-mis-audit-7pQQN`               | Skip                  | n/a            | n/a           | Stale duplicate.                                                                                                                                     |
| `claude/create-docs-architecture-T2Dp5`         | **Do NOT integrate**  | n/a            | High          | Disjoint history; merging would resurrect ~140k deleted lines (C4D bridge, prototype tools) intentionally removed downstream.                        |
| `claude/discover-branches-ZrrgE`                | Skip                  | n/a            | n/a           | Diagnostic-only.                                                                                                                                     |

No full-branch merge is recommended. The single permitted operation is a
cherry-pick of `447b909`, and only if AREA-related docs need to be
co-located during the test.

---

## 5. Expected CUDA / OptiX Runtime Tests

### 5.1 ctest suite (no scene I/O)

Existing tests on the base branch, all wired via `RR_BUILD_TESTS=ON`:

- `math_tests` — host only.
- `image_tests` — host only; emits `output/image_test.ppm`.
- `gpu_tests` — gated on `RR_HAS_CUDA`; covers `GpuBuffer<T>`.
- `pathtracer_tests`, `pathtracer_nee_tests`, `pathtracer_bsdf_tests`,
  `pathtracer_mis_tests` — host-side MIS / NEE / BSDF math.
- `cli_tests` — host only.
- `relativity_tests` — host only.
- `manifold_identity_tests` — host only (2451 lines, identity-warp
  invariants).
- `field_tests` — host only (787 lines, scalar-field POD invariants).
- `demo_tests` — host only.
- `renderer_tests` — host only.
- `optix_tests` — gated on the OptiX backend being wired in (Stage 20D).

Expected runtime outcomes:

- CUDA-OFF / OptiX-OFF: every test except `optix_tests` (and CUDA-gated
  paths inside `gpu_tests`) must pass.
- CUDA-ON / OptiX-OFF: all host tests pass; `gpu_tests` exercises the
  CUDA path; `optix_tests` not built.
- CUDA-OFF / OptiX-ON: configure-time message acknowledges the OptiX
  request but the SDK wiring is gated through subsequent stages —
  surface any actual link error as a runtime finding, not a fix target.
- CUDA-ON / OptiX-ON: full matrix; `optix_tests` must build and pass.

### 5.2 Scene fixtures (manual `RelativityRender` invocations)

Run each via the built `RelativityRender` binary with both CUDA and OptiX
backends where supported. Compare AOV outputs for cross-backend identity
on the no-op / identity arms.

| Scene                                                | Exercises                                       |
|------------------------------------------------------|-------------------------------------------------|
| `scenes/test_camera.rrscene`                         | Camera basics.                                  |
| `scenes/test_spheres.rrscene`                        | Sphere geometry.                                |
| `scenes/test_mesh.rrscene`                           | Mesh upload + traversal.                        |
| `scenes/test_lights.rrscene`                         | Delta-light path.                               |
| `scenes/test_materials.rrscene`                      | Material data flow.                             |
| `scenes/test_textured_material.rrscene`              | Texture sampling.                               |
| `scenes/test_full_scene.rrscene`                     | Combined smoke.                                 |
| `scenes/test_relativity.rrscene`                     | Relativistic camera transform.                  |
| `scenes/test_render_settings.rrscene`                | Render config plumbing.                         |
| `scenes/test_scalar_field_diagnostic.rrscene`        | Field interpreter diagnostic AOV.               |
| `scenes/test_scalar_field_color_multiplier.rrscene`  | Scalar-field beauty mapping (color path).       |
| `scenes/test_scalar_field_emission.rrscene`          | Scalar-field beauty mapping (emission path).    |
| `scenes/test_observer_frame.rrscene`                 | Observer frame fixture.                         |
| `scenes/test_observer_primary_ray_perception.rrscene`| Observer perception transform (primary rays).   |
| `scenes/test_penrose_like_manifold.rrscene`          | Penrose-like compactification warp.             |
| `scenes/test_schwarzschild_like_manifold.rrscene`    | Schwarzschild-like warp.                        |

Default expectation: identity / default-OFF configurations are
byte-identical to baseline; non-default arms produce the corresponding
AOVs without NaNs, INFs, or driver faults.

---

## 6. Expected Build Configurations

Four matrix points. All built from a clean `build/` directory each time
to surface stale-cache regressions.

| # | `RR_ENABLE_CUDA` | `RR_ENABLE_OPTIX` | Purpose                                                          | Required SDK     |
|---|------------------|-------------------|------------------------------------------------------------------|------------------|
| 1 | OFF              | OFF               | Host-only baseline. Establishes byte-identical reference output. | None.            |
| 2 | ON               | OFF               | CUDA backend without OptiX. Exercises CUDA kernels.              | CUDA Toolkit.    |
| 3 | OFF              | ON                | OptiX flag-only path. Surfaces configure-time messages.          | OptiX SDK (configure only). |
| 4 | ON               | ON                | Full GPU backend. Production target.                             | CUDA Toolkit + OptiX SDK.   |

Per-configuration commands (run from a fresh `build/<n>` directory):

```
cmake -S . -B build/1 -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=OFF
cmake -S . -B build/2 -DRR_ENABLE_CUDA=ON  -DRR_ENABLE_OPTIX=OFF
cmake -S . -B build/3 -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=ON
cmake -S . -B build/4 -DRR_ENABLE_CUDA=ON  -DRR_ENABLE_OPTIX=ON
cmake --build build/<n> -j
ctest --test-dir build/<n> --output-on-failure
```

For each configuration record: configure log, build log, ctest log, and
a quick scene render of `scenes/test_full_scene.rrscene` if the
configuration supports it.

---

## 7. Rollback Strategy

The `nvidia-sdk-runtime-test` branch is explicitly throwaway. Rollback
is a sequence of pure-pointer operations — no force-pushes, no resets on
shared branches.

### 7.1 Normal-case rollback (no findings to keep)

```
git checkout claude/discover-branches-ZrrgE   # or main
git branch -D nvidia-sdk-runtime-test
rm -rf build/                                  # ephemeral
```

No origin state is touched. The branch never had an upstream.

### 7.2 Keep findings, discard the branch

If the runtime test surfaced fixes worth keeping:

1. From `nvidia-sdk-runtime-test`, identify the relevant commits with
   `git log --oneline 107300e..HEAD`.
2. Open a fresh feature branch off
   `claude/rewrite-rendering-core-De71I` (e.g.
   `claude/manifold-runtime-fixes-<short>`).
3. `git cherry-pick <sha>` each finding-bearing commit onto the feature
   branch.
4. Delete `nvidia-sdk-runtime-test` as in §7.1.
5. Push the new feature branch for review. **Do not** push
   `nvidia-sdk-runtime-test` itself.

### 7.3 Mid-flight abort

If any step in §3 fails unexpectedly and leaves a dirty working tree:

```
git reset --merge      # safe; aborts in-progress merge / cherry-pick
git status             # confirm clean
```

`git reset --merge` is the only `git reset` form used here, and it is
non-destructive — it refuses to clobber uncommitted work. If a hard
reset is ever considered, stop and check with the user first (it is
explicitly excluded by the standing rules).

### 7.4 Invariants the rollback must preserve

- `origin/claude/rewrite-rendering-core-De71I` is unchanged.
- `origin/main` is unchanged.
- `origin/area-light-arc` is unchanged.
- No remote refs other than the diagnostic branch
  `claude/discover-branches-ZrrgE` are touched.
- No source files outside `docs/` are modified by anything described in
  this document.

---

## 8. Out of Scope

- Actual SDK runtime execution (this plan only enumerates the matrix).
- Toolchain pinning (CUDA arch flags, OptiX SDK version, driver minimums).
- Performance benchmarking.
- Any modification to `claude/rewrite-rendering-core-De71I` or `main`.
- Any modification of renderer source files.
- Any push of `nvidia-sdk-runtime-test`.

These belong in follow-up task definitions once this plan is approved
and the runtime session is scheduled.

# Branch Inventory for NVIDIA CUDA + OptiX SDK Runtime Test

Read-only diagnostic snapshot of the RelativityRender repository, captured to
choose a canonical base for an upcoming CUDA + OptiX SDK runtime test on
NVIDIA hardware. No source, build, or remote state was modified in producing
this document.

- Date: 2026-05-18
- Working directory: `/home/user/RelativityRender`
- Origin: `fopearcano/RelativityRender`

## Current Branch

- `claude/discover-branches-ZrrgE` @ `f2b9be9`
- Identical to `origin/main` (same commit). This branch was created as a
  read-only diagnostic scratch space for the inventory; it contains no work
  beyond what is on `main`.

## Local Branches

```
* claude/discover-branches-ZrrgE  f2b9be9
  main                            f2b9be9 [origin/main]
```

Only `main` and the diagnostic branch exist locally. All other work lives on
remote-tracking refs.

## Remote Branches (sorted by last commit, newest first)

| Branch                                          | Last commit (UTC)   | Tip      | Ahead/behind main | Author |
|-------------------------------------------------|---------------------|----------|-------------------|--------|
| `origin/claude/rewrite-rendering-core-De71I`    | 2026-05-18 12:23    | `107300e` | **+108 / -0**     | Claude |
| `origin/area-light-arc`                         | 2026-05-09 06:49    | `447b909` | +2 / -4           | Claude |
| `origin/claude/discover-branches-ZrrgE` (HEAD)  | 2026-05-09 01:36    | `f2b9be9` | +0 / -0           | Mic    |
| `origin/main`                                   | 2026-05-09 01:36    | `f2b9be9` | (baseline)        | Mic    |
| `origin/relativity-core-v1`                     | 2026-05-08 23:56    | `cee451e` | +1 / -4           | Claude |
| `origin/relativity-core-v1-clean`               | 2026-05-08 23:05    | `a4d802f` | +0 / -3           | Claude |
| `origin/claude/recreate-mis-audit-7pQQN`        | 2026-05-08 22:01    | `6c4191c` | +1 / -50          | Claude |
| `origin/claude/create-docs-architecture-T2Dp5`  | 2026-04-29 10:43    | `a90f9e1` | +73 / -51         | Claude |

`Ahead/behind` numbers are `git rev-list --left-right --count
origin/main...<branch>` taken at fetch time (i.e. branch ahead / main ahead).

## Per-Branch Summary

### `origin/claude/rewrite-rendering-core-De71I` — active development tip

- 108 commits ahead of `main`, 0 behind. Branched directly off the current
  `main` tip (`f2b9be9`), so it is a strict superset of merged work.
- Latest commit `107300e` (2026-05-18) is today. This is the only branch with
  ongoing activity.
- Contains all renderer source additions absent from `main`:
  - `src/field/` — `FieldInterpreter.h`, `FieldMapping.h`, `FieldType.h`,
    `ScalarField.h`, `README.md` (Field Interpretation Layer).
  - `src/manifold/` — `CameraObserverAdapter.h`, `CoordinateChart.h`,
    `GeodesicState.h`, `ManifoldMode.h`, `ManifoldTransform.h`,
    `MetricTensor.h`, `ObserverFrame.h`, `PenroseLikeCompactification.h`,
    `SchwarzschildLikeWarp.h`, `README.md`.
- Modifies all major backends to consume the new data:
  - CUDA: `CudaAOV.cuh`, `CudaKernels.cuh`, `CudaPathTracer.cu(h)`,
    `CudaRenderer.cu(h)`, `CudaScene.cuh`, `CudaTestKernel.cu`.
  - OptiX: `OptixLaunchParams.h`, `OptixPrograms.cu`, `OptixRenderer.cpp(h)`.
  - Core/scene/path tracer: `Config.h`, `CommandLine.cpp(h)`,
    `PathTracer.cpp(h)`, `Scene.cpp(h)`, `AOV.cpp(h)`, `SceneLoader.cpp`,
    `main.cpp`.
- Adds scenes: `test_observer_frame.rrscene`,
  `test_observer_primary_ray_perception.rrscene`,
  `test_penrose_like_manifold.rrscene`,
  `test_schwarzschild_like_manifold.rrscene`, and three
  `test_scalar_field_*.rrscene` fixtures.
- Adds large host-side test suites: `tests/field_tests.cpp` (787 lines),
  `tests/manifold_identity_tests.cpp` (2451 lines),
  `tests/relativity_tests.cpp`, `tests/renderer_tests.cpp`.
- Expands `CMakeLists.txt` by +262 lines (new modules + test targets).
- Commit arcs present (per `git log` subjects): `OBSERVER.*`, `OBS-F.*`,
  `OBS-P.*`, `OBS-PERCEPT.*`, `OBS-DOP.*`, `FIELD-I.*`, `FIELD-BEAUTY.*`,
  plus pre-existing `MIS.*`, `NEE.*`, `PT-P.*`. CUDA and OptiX program arms
  are implemented for every observer/field arc, not just docs.

This is the latest renderer source work, the manifold/observer/field work,
and the most plausible candidate for runtime testing.

### `origin/area-light-arc` — AREA planning, docs only

- 2 ahead of `main`, 4 behind. Branched from `origin/relativity-core-v1`
  (`cee451e`), not from current `main`.
- Diff vs `main`: only `docs/PATH_TRACER_AREA_LIGHT_PLAN.md` is added and
  `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` differs. No source
  changes, no scene fixtures, no CUDA/OptiX work.
- Holds the AREA arc planning document; no implementation has started here.

### `origin/relativity-core-v1` — superseded development line

- 1 ahead of `main`, 4 behind. Diff vs `main`: only the MASTER instructions
  file differs.
- Tip `cee451e` adds `docs: MIS.7 arc audit` on top of the pre-merge
  history. The post-merge `main` already incorporates the equivalent work
  through PR #1 (`relativity-core-v1-clean` → `main`).
- No source content unique to this branch beyond what was merged.

### `origin/relativity-core-v1-clean` — already merged

- 0 ahead, 3 behind `main`. This is the branch that fed PR #1 into `main`.
- Its content lives on `main` now. Safe to consider archival.

### `origin/main` — current trunk

- Tip `f2b9be9` is the PR #1 merge from `relativity-core-v1-clean`. Includes
  the full MIS arc and NEE work, but no Field, Manifold, or Observer
  modules.
- This is the closed/stable baseline against which `rewrite-rendering-core`
  is +108.

### `origin/claude/recreate-mis-audit-7pQQN` — stale audit redo

- 1 ahead, 50 behind `main`. Single commit `6c4191c` recreating the MIS.7
  audit doc; the same audit is already on `main` through PR #1.
- No source changes. Safe to consider stale.

### `origin/claude/create-docs-architecture-T2Dp5` — old separate timeline

- 73 ahead, 51 behind `main`. Branched from the very first repo commit
  (`8667eb2`), so it shares almost no history with current work.
- Contains an older M0–M6 milestone series (math library, image system, GPU
  device layer, first CUDA kernel) and a 9-step audit. Predates the
  PathTracer/MIS/NEE/Manifold work entirely. Effectively a fossil branch;
  treat as stale for runtime test purposes.

### `origin/claude/discover-branches-ZrrgE` — this diagnostic branch

- Same tip as `origin/main`. Created for this inventory. After the doc is
  pushed it will be +1 over `main` (one docs-only commit). Not a candidate
  for runtime testing; will be discarded after the inventory lands.

## Functional Map of Important Work

| Concern                                  | Branch                                          |
|------------------------------------------|-------------------------------------------------|
| Latest renderer source work              | `claude/rewrite-rendering-core-De71I`           |
| Manifold module (`src/manifold/`)        | `claude/rewrite-rendering-core-De71I`           |
| Observer frame + perception + Doppler    | `claude/rewrite-rendering-core-De71I`           |
| Scalar field interpretation (`src/field/`) | `claude/rewrite-rendering-core-De71I`         |
| Penrose-like / Schwarzschild-like warps  | `claude/rewrite-rendering-core-De71I`           |
| MIS / NEE / firefly-clamp / RNG polish   | `main` (merged) and inherited by all newer tips |
| AREA light arc (planning only)           | `area-light-arc`                                |
| Original M0–M6 audits                    | `claude/create-docs-architecture-T2Dp5` (stale) |

## Stale or Temporary Branches

- `relativity-core-v1-clean` — merged into `main`; no unique content.
- `claude/recreate-mis-audit-7pQQN` — single doc commit duplicated on `main`.
- `claude/create-docs-architecture-T2Dp5` — disjoint old timeline, predates
  current architecture.
- `claude/discover-branches-ZrrgE` — temporary diagnostic branch (this one).
- `relativity-core-v1` — superseded by the PR #1 merge into `main`; only
  carries a duplicate audit doc forward.

## Recommendation for NVIDIA SDK Runtime Test

### Canonical base

**`origin/claude/rewrite-rendering-core-De71I`** (currently `107300e`).

Reasons:

1. It is the only branch with all manifold / observer / field source code
   wired into both CUDA and OptiX backends.
2. It is a strict superset of `main` (+108 / -0), so the stable MIS / NEE
   work merged via PR #1 is included.
3. It has the matching scene fixtures
   (`test_penrose_like_manifold.rrscene`, etc.) and host-side test suites
   needed to exercise the new modules.
4. CMake build wiring for the new modules already lives on this branch
   (`+262` lines); `main` cannot build the manifold/field code at all.

Choosing `relativity-core-v1` or `relativity-core-v1-clean` would mean
losing the manifold/observer/field work entirely. Choosing `area-light-arc`
would add a single planning doc with no runtime impact.

### Suggested temporary test branch

Create a short-lived branch off the chosen base, e.g.:

```
claude/nvidia-sdk-runtime-test-2026-05-18
```

(branched from `origin/claude/rewrite-rendering-core-De71I` @ `107300e`).

Alternative naming, if preferred:

- `nvidia-runtime/rewrite-core-2026-05-18`
- `runtime-test/cuda-optix-2026-05-18`

The branch should be treated as throwaway: it carries build-system
adjustments, runtime smoke configurations, and any non-functional notes
discovered while exercising the SDK on NVIDIA hardware. Real fixes belong
back on `claude/rewrite-rendering-core-De71I` (or a feature branch off
it), not on the throwaway test branch.

### Out of scope for this document

- No merges have been performed.
- No branches have been reset, deleted, or pushed beyond the diagnostic
  branch that carries this doc.
- No renderer source files were modified.
- The actual SDK runtime test plan (toolchain versions, CUDA arch flags,
  OptiX SDK version, scene matrix, success criteria) is intentionally not
  included here — that belongs in a follow-up task definition once the
  canonical base is confirmed.

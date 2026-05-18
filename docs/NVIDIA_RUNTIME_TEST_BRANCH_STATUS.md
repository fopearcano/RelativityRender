# NVIDIA Runtime Test Branch — Status

Status snapshot of the temporary integration branch
`nvidia-sdk-runtime-test`, captured immediately after applying the
integration plan defined in
`docs/NVIDIA_RUNTIME_TEST_INTEGRATION_PLAN.md`.

- Date: 2026-05-18
- Branch: `nvidia-sdk-runtime-test` (local-only, no upstream)
- HEAD: `9b87e25` — `docs: AREA arc planning document`
- Base: `origin/claude/rewrite-rendering-core-De71I` @ `107300e`
- Range applied: `107300e..HEAD`
- Conflicts: none

## Base Branch

- `origin/claude/rewrite-rendering-core-De71I` @
  `107300e35e47446aba957cd062cb6206e00ce543`
- Subject: `docs: OBS-DOP.6 — Observer Doppler/Searchlight Capstone
  Audit (docs only)`
- Reason for choice: only branch carrying `src/manifold/`, `src/field/`,
  observer modules, the matching CUDA + OptiX program arms, and the
  scene fixtures + host tests needed to exercise them. Strict superset
  of `main`.

## Integrated Branches / Commits

Exactly one operation was applied, per
`docs/NVIDIA_RUNTIME_TEST_INTEGRATION_PLAN.md` §3 Step 2 and §4
("Cherry-pick `447b909`"):

| Source branch    | Source commit | Local commit on temp branch | Files affected                              |
|------------------|---------------|------------------------------|---------------------------------------------|
| `area-light-arc` | `447b909`     | `9b87e25`                    | `docs/PATH_TRACER_AREA_LIGHT_PLAN.md` (+1763) |

`git diff --stat 107300e..HEAD`:

```
 docs/PATH_TRACER_AREA_LIGHT_PLAN.md | 1763 +++++++++++++++++++++++++++++++++++
 1 file changed, 1763 insertions(+)
```

No source files were modified. No build-system files were modified.
Only a single docs-only addition lands on this temp branch beyond the
base.

## Skipped Branches

Per the integration plan §4, the following branches were intentionally
not merged or cherry-picked:

| Branch                                          | Reason for skip                                                                  |
|-------------------------------------------------|----------------------------------------------------------------------------------|
| `origin/main`                                   | Strict subset of the base; nothing to add.                                       |
| `origin/relativity-core-v1`                     | Source already on base; only a duplicate audit doc + older MASTER instructions. |
| `origin/relativity-core-v1-clean`               | Already merged into `main` via PR #1.                                            |
| `origin/claude/recreate-mis-audit-7pQQN`        | Stale duplicate of MIS.7 audit already on `main`.                                |
| `origin/claude/create-docs-architecture-T2Dp5`  | High-risk fossil timeline (would resurrect ~140k deleted lines). Do NOT integrate. |
| `origin/claude/discover-branches-ZrrgE`         | Diagnostic-only authoring branch for the inventory + plan + this status doc.    |

`area-light-arc`'s second unique commit `cee451e` (MIS.7 audit
duplicate) was deliberately not cherry-picked — only `447b909` was.

## Conflicts

None. The cherry-pick reported a single new file
(`docs/PATH_TRACER_AREA_LIGHT_PLAN.md`) and applied without prompting.
Working tree is clean after the operation.

## Current HEAD

```
9b87e25 docs: AREA arc planning document
107300e docs: OBS-DOP.6 — Observer Doppler/Searchlight Capstone Audit (docs only)
3256a01 docs: OBS-DOP.5 — OptiX Observer Doppler/Searchlight Audit (docs only)
```

`git status` is clean. The branch has no upstream and has not been
pushed.

## Host Capability Snapshot

Captured to gate the next runtime step. The current environment is the
managed cloud session this work is being authored in — not the target
NVIDIA host.

| Tool / variable         | Present? |
|-------------------------|----------|
| `nvcc`                  | No       |
| `nvidia-smi`            | No       |
| `$OptiX_INSTALL_DIR`    | Unset    |

Consequence: configurations #2 and #4 from
`docs/NVIDIA_RUNTIME_TEST_INTEGRATION_PLAN.md` §6 cannot be
exercised in this environment. Only #1 (host-only) is fully runnable
here; #3 (OptiX flag-only) can be configured but the underlying SDK
would still be absent. Full runtime testing must happen on the actual
NVIDIA target host.

## Next Recommended Runtime Test Commands

Strict execution order. Each step assumes the previous one passed its
gate. Logs should be captured but not committed.

### 1. Confirm branch state (always safe)

```
git checkout nvidia-sdk-runtime-test
git log --oneline 107300e..HEAD
git status
```

### 2. Configuration #1 — CUDA OFF / OptiX OFF (host-only baseline)

```
cmake -S . -B build/1 -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=OFF
cmake --build build/1 -j
ctest --test-dir build/1 --output-on-failure
```

Expected: every test except `optix_tests` and the CUDA-gated paths
inside `gpu_tests` passes. Establishes the byte-identical reference.

### 3. Configuration #2 — CUDA ON / OptiX OFF (requires CUDA Toolkit)

```
cmake -S . -B build/2 -DRR_ENABLE_CUDA=ON -DRR_ENABLE_OPTIX=OFF
cmake --build build/2 -j
ctest --test-dir build/2 --output-on-failure
```

Expected: all host tests pass; `gpu_tests` exercises the CUDA path;
`optix_tests` not built.

### 4. Configuration #3 — CUDA OFF / OptiX ON (OptiX flag-only path)

```
cmake -S . -B build/3 -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=ON
cmake --build build/3 -j
ctest --test-dir build/3 --output-on-failure
```

Expected: configure-time message acknowledges the OptiX request. Any
link error here is a runtime finding, not a fix target on this branch.

### 5. Configuration #4 — CUDA ON / OptiX ON (full GPU backend)

```
cmake -S . -B build/4 -DRR_ENABLE_CUDA=ON -DRR_ENABLE_OPTIX=ON
cmake --build build/4 -j
ctest --test-dir build/4 --output-on-failure
```

Expected: full matrix; `optix_tests` must build and pass.

### 6. Targeted scene renders (configurations #2 and #4)

For each scene listed in
`docs/NVIDIA_RUNTIME_TEST_INTEGRATION_PLAN.md` §5.2, invoke the
built `RelativityRender` binary. Compare AOV outputs between CUDA-only
(#2) and CUDA+OptiX (#4) for cross-backend identity on the no-op /
identity arms. Suggested smoke first:

```
./build/2/bin/RelativityRender scenes/test_full_scene.rrscene
./build/4/bin/RelativityRender scenes/test_full_scene.rrscene
./build/4/bin/RelativityRender scenes/test_penrose_like_manifold.rrscene
./build/4/bin/RelativityRender scenes/test_schwarzschild_like_manifold.rrscene
./build/4/bin/RelativityRender scenes/test_observer_frame.rrscene
./build/4/bin/RelativityRender scenes/test_observer_primary_ray_perception.rrscene
./build/4/bin/RelativityRender scenes/test_scalar_field_diagnostic.rrscene
./build/4/bin/RelativityRender scenes/test_scalar_field_color_multiplier.rrscene
./build/4/bin/RelativityRender scenes/test_scalar_field_emission.rrscene
```

### 7. Rollback

Per integration plan §7. The temp branch never had an upstream; nothing
to revoke remotely.

```
git checkout claude/discover-branches-ZrrgE
git branch -D nvidia-sdk-runtime-test
rm -rf build/
```

If findings emerged, cherry-pick them onto a fresh feature branch off
`claude/rewrite-rendering-core-De71I` instead of pushing this temp
branch.

## Invariants Preserved

- `origin/claude/rewrite-rendering-core-De71I` not touched.
- `origin/main` not touched.
- `origin/area-light-arc` not touched.
- No renderer source files modified.
- No build-system files modified.
- No push performed (temp branch is local-only, no upstream).
- Conflicts were not auto-resolved (none arose).

# OptiX Gap A — Step 2 Audit

Date: 2026-05-04
Branch: `relativity-core-v1`
Last commit on the audited tree: `8377a1c` ("docs: GAP-A2.6
local verification of Step 2")
Scope: post-Step-2 capstone audit covering the entire
GAP-A2.x arc — the four task slices (GAP-A2.1 task ident,
GAP-A2.2 scope+files, GAP-A2.3 PASS criteria, GAP-A2.4
preconditions), the implementation slice (GAP-A2.5 = the
Step-2 commit `9218b18`), and the local-verification slice
(GAP-A2.6 = `8377a1c` after the report-refresh `96d8e1b`).
Mode: documentation-only. No source code is modified by
this audit.

---

## 1. Verdict

**PASS** — on the audit host's available checks.

The runtime-visible PASS criteria (SDK-found success path
producing populated `GpuBuffer<float>` instances) remain
explicitly DEFERRED to a CUDA + OptiX-SDK host run per the
documented runtime-deferral posture. The audit host has
no `nvcc` and no `optix.h`; the verification plan
(`docs/CUDA_HOST_VERIFICATION_PLAN.md`) formalises this
deferral as "runtime deferred, not code failure". The
audit host runs every check it can; nothing on this host
is BLOCKED.

A REPAIR was NOT triggered. A BLOCKED state was NOT
reached. The task spec at
`docs/OPTIX_GAP_A_STEP_2_TASK.md` §9.2 confirmed every
prerequisite was already in the tree before
implementation began.

---

## 2. What worked

Six artifacts were shipped across the GAP-A2.x arc, all
on `relativity-core-v1`:

| Slice    | Commit    | Artifact                                                      |
|----------|-----------|---------------------------------------------------------------|
| GAP-A2.1 | `f4c4d17` | `docs/OPTIX_GAP_A_STEP_2_TASK.md` (initial: name + short      |
|          |           | description per §1-2)                                          |
| GAP-A2.2 | `7c76326` | Task §3-5 appended (files / functions / what-must-not-change)  |
| GAP-A2.3 | `04c7c57` | Task §6-8 appended (PASS criteria: outputs, builds,            |
|          |           | non-regression)                                                |
| GAP-A2.4 | `b2ab9ec` | Task §9 appended (preconditions table; READY status)           |
| GAP-A2.5 | `9218b18` | **Step-2 implementation**: replaced the Step-1 SDK_FOUND       |
|          |           | stub of `OptixRenderer::render_aovs_retain` with the full      |
|          |           | launch + buffer-retention body (~290 lines).                   |
| (refresh)| `96d8e1b` | CUDA-H.9 report regenerated to capture the new tree state.    |
| GAP-A2.6 | `8377a1c` | Task §10 appended (local-verification record).                 |

Specifically, on Step 2's implementation:

- Mirrored the Stage 20N `render_aovs` body
  (duplicate-then-refactor path per the plan §4 Step 2;
  the existing `render_aovs` stays byte-identical).
- Substituted raw `cudaMalloc` for the Beauty / Albedo /
  Normal device buffers with
  `rr::gpu::GpuBuffer<float>::allocate(...)` so RAII
  transfers ownership into the `AovRetainedBuffers`
  result struct on success.
- Skipped the depth / doppler / searchlight AOVs (the
  matching launch-params pointers stay null; the OptiX
  programs short-circuit those writes per the Stage 20N
  null-pointer-skip design).
- Skipped the host-side download (the caller uses the
  retained device pointers directly; no `cudaMemcpy(D->H)`
  in this entry).
- On every failure path: calls `R.{beauty,albedo,
  normal}_device.reset()` so the returned struct reports
  `ok=false` with empty buffers (no half-allocated state).
- Cleanup lambda still frees temporary allocations
  (positions, indices, lights, framebuffer) in all paths;
  the three retained `GpuBuffer<float>` instances move
  out via NRVO.

Local verification (GAP-A2.6) confirmed:

- OFF build: ctest 6/6 green; the SDK_FOUND body is not
  compiled (rr_optix isn't built when `RR_ENABLE_OPTIX=OFF`).
- ON-audit-host build (no SDK on disk): ctest 7/7 green;
  the SDK_FOUND body is gated out by
  `RELATIVITYRENDER_OPTIX_SDK_FOUND`; only the Step-1
  audit-host stub is reachable.
- Runner smokes (with and without `--optix`, on both
  build modes): every command exits cleanly within
  milliseconds with the documented audit-host fallback
  error; no segfault, no abort, no timeout, no infinite
  loop.
- CUDA-H.9 report determinism: `git diff
  docs/CUDA_HOST_VERIFICATION_REPORT.md` shows ONLY
  the `Tree state` hash line changing pre- vs post-
  Step-2 (the only "varying-but-stable identifier" the
  runner emits per CUDA-H.9 spec). All test rows /
  counts / overall verdict byte-identical.
- The CUDA path stayed byte-identical:
  `git diff 6287471..8377a1c --stat -- src/cuda/
  src/renderer/ src/pathtracer/` reports zero bytes
  changed across the entire GAP-A2.x arc. The Stage
  17-21 rule (CUDA path byte-identical across every
  OptiX / denoiser slice) continues to hold.

---

## 3. What still missing

Step 2 closed only one of five total Gap A steps per
`docs/OPTIX_GAP_A_POLISH_PLAN.md` §4. The remaining
work:

| Step | Name                                  | Status   |
|------|---------------------------------------|----------|
| 3    | Orchestration helper                  | PENDING  |
|      | (`render_optix_aovs_and_denoise_to_ppm`|          |
|      | in `src/main.cpp`; calls               |          |
|      | `render_aovs_retain` then              |          |
|      | `denoise_and_save_ppm`).               |          |
| 4    | CLI surface                            | PENDING  |
|      | (extend `--render-optix-aovs` to       |          |
|      | honour `--denoise`, OR add a           |          |
|      | dedicated `--render-optix-aovs-        |          |
|      | denoise` action).                      |          |
| 5    | Capstone audit                         | PENDING  |
|      | (`docs/OPTIX_GAP_A_POLISH_AUDIT.md`    |          |
|      | confirming the gap is closed end-to-   |          |
|      | end + empirical CUDA-host verification).|         |

Plus the long-standing project-wide deferral
(unchanged across this audit):

- Empirical verification of `render_aovs_retain` on a
  real CUDA + OptiX-SDK host. The runner +
  CUDA_HOST_VERIFICATION_PLAN make this a one-command
  operator action when the right hardware becomes
  available.

Master order #20 (renderer server, deferred per
`docs/STAGE_15_SERVER_DEFERRED.md`) and #21+ (C4D
bridge / UI / node editor / native C4D renderer,
blocked by master rule 4) remain at their pre-Step-2
status.

---

## 4. Next safe step (one line)

**OptiX Gap A Step 3 — add the orchestration helper
`render_optix_aovs_and_denoise_to_ppm` in `src/main.cpp`
that calls `OptixRenderer::render_aovs_retain` then
hands the retained device pointers to
`OptixDenoiser::denoise` via `denoise_and_save_ppm`,
per `docs/OPTIX_GAP_A_POLISH_PLAN.md` §4 Step 3.**

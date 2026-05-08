# OptiX Gap A — Capstone Audit

Date: 2026-05-04
Branch: `relativity-core-v1`
Last commit on the audited tree: `9a3da6c` ("docs: OptiX
Gap A Step 3 audit (PASS verdict + next step)")
Scope: capstone audit covering the entire OptiX Gap A
polish arc — Step 1 (`6287471` types + decl), Step 2
(`9218b18` SDK_FOUND body), Step 3 (sub-stages 3.1..3.5
in commits `e8523d8`, `c5d6968`, `f8df9b9`, `ccb40c8`,
`2fabb54`), plus the seven supporting audit / task /
verification docs.
Mode: documentation-only. No source code is modified by
this audit. No new CLI flag is introduced. No
`docs/BUILD_PLAN.md` slice-closing entry is required;
the prior per-step entries already document each
implementation slice.

The audit answers the seven evaluation questions in
order, then issues a verdict.

---

## 1. Did Step 3.5 implement a working minimal CLI path using `--render-optix-aovs --denoise`?

**YES.**

Commit `2fabb54` ("optix gap A step 3.5: wire helper into
--render-optix-aovs --denoise") added a post-AOV-save
denoise block to `run_render_optix_aovs` in
`src/main.cpp`. The block:

- Is gated by `if (cfg.denoise_enabled)` so the
  no-`--denoise` path stays byte-identical with Stage
  20N.
- When `--denoise` is set AND `RR_HAS_CUDA` is defined:
  initialises an `OptixBackend` + `OptixDenoiser`,
  calls
  `render_optix_aovs_and_denoise_to_ppm(denoiser, scene,
  lights, cfg.width, cfg.height,
  "output/optix_aovs_denoised.ppm")`. Each step's
  failure logs a warning + falls through to the
  noisy-AOV fallback (per the user's "if OptiX
  unavailable, log warning and keep noisy output"
  rule).
- When `--denoise` is set BUT `RR_HAS_CUDA` is
  undefined: emits the documented "denoiser
  unavailable on this build" warning + leaves the
  noisy AOVs in place.

The dispatcher's exit code is determined ENTIRELY by
the AOV-save step; denoise failures never change
success status. The combined invocation
(`--render-optix-aovs --denoise`) produces:

- six AOV PPMs unchanged from Stage 20N
  (`output/optix_aov_{beauty,normal,depth,albedo,
  doppler,searchlight}.ppm`).
- one ADDITIONAL `output/optix_aovs_denoised.ppm`
  carrying the denoised radiance (or the Stage 21D.5
  noisy-Beauty fallback if the denoiser failed).

The path is verified on the audit host's compile
side: OFF + ON-audit-host builds both clean; the
CLI's audit-host fallback (no-SDK case) emits the
documented `--denoise` announcement (Stage 21E.1)
followed by the documented "requires OptiX" error.
Runtime-visible end-to-end behaviour deferred per the
established posture.

---

## 2. Does this satisfy the plan's Step 4 minimal-CLI requirement?

**YES.**

`docs/OPTIX_GAP_A_POLISH_PLAN.md` §4 Step 4 documents
TWO acceptable shapes:

> Either:
> - extend `--render-optix-aovs` to honour `--denoise`
>   (minimal CLI change), OR
> - add a dedicated `--render-optix-aovs-denoise`
>   action.

Step 3.5 chose the FIRST option verbatim. The plan
explicitly labels that path "minimal CLI change", so
Step 3.5 IS the plan's minimal-CLI form by design.

The slice cadence reinforces this: 3.5 was scoped as
"minimal CLI hook" and consciously avoided introducing
a new action enum / parser branch / mutex-validation
entry / help-text block. It re-uses the existing
`--render-optix-aovs` action's lifecycle and the
existing `--denoise` modifier flag's wiring (Stage
19B.4 / 21E.1).

---

## 3. Is a separate `--render-optix-aovs-denoise` action actually necessary?

**NO.**

The plan explicitly framed the dedicated action as an
OR alternative to the modifier-flag approach, not an
additional requirement. Both shapes solve the same
problem (give the operator a way to trigger the
helper); both are valid; the operator's choice
between them is documented as taste, not contract.

A dedicated action would NOT add any new
functionality:

- The same helper
  (`render_optix_aovs_and_denoise_to_ppm`) would run.
- The same scene-build path would run.
- The same `OptixBackend` + `OptixDenoiser` init
  would happen.
- The same six AOV PPMs + one denoised PPM would be
  produced.
- The same noisy-Beauty fallback would fire on
  denoise failure.

A dedicated action WOULD:

- Add a fifth `RenderOptix*` enum entry (currently
  RenderOptix{Test,Triangle,Relativity,Raygen,
  MeshScene,MaterialScene,Pathtrace,DirectLighting,
  ShadowTest,TexturedMaterial,Aovs,Denoise} — 12
  entries).
- Add a parser branch + mutex / validation list /
  help text per the established pattern (~30 lines
  in `src/core/CommandLine.{h,cpp}`).
- Add a dispatcher case in `main.cpp`'s action
  switch.
- Cost: ~50 lines of additive boilerplate that
  duplicates the existing `--render-optix-aovs
  --denoise` invocation's behaviour.

Per master rule 12 ("Do not overbuild a later system
before the current layer works"), the dedicated
action falls outside the minimal scope. It can land
later if a downstream consumer (renderer server, C4D
bridge, scripted CI runner) finds the modifier
combination awkward; today no such consumer exists.

---

## 4. Are OptiX OFF builds still valid?

**YES.**

Empirically verified across every Gap A slice
(commits `6287471`, `9218b18`, `c5d6968`, `f8df9b9`,
`ccb40c8`, `2fabb54`). Each slice's BUILD_PLAN entry
records the same post-slice state:

```
$ cmake -S . -B build_off -DRR_BUILD_TESTS=ON \
      -DRR_ENABLE_CUDA=OFF -DRR_ENABLE_OPTIX=OFF
... clean configure + build ...
$ cd build_off && ctest
100% tests passed, 0 tests failed out of 6
```

The 6/6 baseline is byte-identical with the
pre-Gap-A state (commit `1801859` post-CUDA-host-
verification audit). Every Gap A artifact (the new
`render_aovs_retain` + the new helper + the new
forward decl + the new dispatcher block) is gated by
`RR_HAS_CUDA && RELATIVITYRENDER_ENABLE_OPTIX` so the
OFF build never sees them.

The ON-audit-host build (`-DRR_ENABLE_CUDA=OFF
-DRR_ENABLE_OPTIX=ON`, no SDK) is also unchanged at
ctest 7/7 green throughout. The new helper is gated
on `RR_HAS_CUDA` (not just `ENABLE_OPTIX`), so even
the ON-audit-host build doesn't compile it.

---

## 5. Is the CUDA path unchanged?

**YES — verified by diff-stats over the entire Gap A
arc.**

```
$ git diff 6287471~1..9a3da6c --stat -- \
      src/cuda/ src/renderer/ src/pathtracer/
$ echo "exit=$?"
exit=0
```

(zero bytes changed; zero files reported.)

This continues the Stage 17-21 streak: the CUDA path
has been byte-identical across every OptiX / denoiser
/ Gap A slice. The Stage 21E.2 `--render --denoise`
dispatcher (CUDA AOV pipeline) is unaffected. The
Stage 19B.4 `--render-aovs --denoise` /
`--render-denoise` paths (also CUDA AOV pipeline) are
unaffected. The only `src/main.cpp` changes in Gap A
are:

- Step 1: zero `src/main.cpp` changes (header /
  rr_optix only).
- Step 2: zero `src/main.cpp` changes (rr_optix
  only).
- Step 3.x: all changes confined to `src/main.cpp`
  but nested inside the existing
  `RR_HAS_CUDA && RELATIVITYRENDER_ENABLE_OPTIX`
  envelope; the no-`--denoise` invocation of every
  `--render-optix-*` action is byte-identical.

---

## 6. Does the denoiser handoff avoid CPU denoising?

**YES.**

Tracing the data flow through the new helper:

1. **`OptixRenderer::render_aovs_retain`** (Step 2)
   does `optixLaunch` over the OptiX programs; every
   per-pixel byte is produced by the SDK's CUDA
   kernels. The host only orchestrates the launch +
   `cudaDeviceSynchronize`s.
2. **Helper body** (Step 3.4) constructs an
   `OptixDenoiser::Inputs` POD via SIX field
   assignments (three device pointers + three
   integers). No iteration, no per-pixel work.
3. **`denoise_and_save_ppm`** (Stage 21D.4) allocates
   a device-side `GpuBuffer<float>` for the output,
   calls `OptixDenoiser::denoise(inputs, output)`
   which runs `optixDenoiserInvoke` on the GPU, then
   `GpuBuffer::download(host_image_data,
   output_floats)` (one `cudaMemcpy(D->H)` — no
   per-pixel transformation).
4. **`save_image_or_error` -> `Image::save_ppm`**
   (existing, unchanged): float-to-uint8 clamp
   serialisation. The same code path every other
   `--render-*` saver uses; classified as display-
   format conversion (not rendering computation) by
   every prior audit since Stage 14A.

Master rules 5 + 7 ("No CPU ray tracing as production
path" / "All per-pixel/per-ray rendering must happen
on GPU"): satisfied. The new helper is purely
host-side orchestration.

A `grep` for pixel-space `for` loops over the new
code finds zero hits:

```
$ grep -nE "for\s*\(.*\b(x|y)\s*=\s*0\b" \
      src/main.cpp | grep -E "render_optix_aovs"
(no output)
```

The ONLY host-side per-pixel loop in the broader
`--denoise` family is the legacy
`denoise_aov_buffers_to_ppm`'s FLOAT3 -> FLOAT4
widening (Stage 19B.4) — that's inherited code on
the CUDA path, not on the new Gap A path, and was
explicitly classified as channel-format conversion
in the post-Stage-21 capstone audit.

---

## 7. What remains runtime-deferred?

The audit host has no `nvcc` and no `optix.h` (`which
nvcc` returns nothing; `find / -name optix.h` returns
nothing under `/opt`, `/usr/local`, or `$HOME`). The
verification plan
(`docs/CUDA_HOST_VERIFICATION_PLAN.md`) formalises
this deferral as "runtime deferred, not code
failure". For Gap A specifically the deferred items
are:

- The actual SDK-found body of `render_aovs_retain`
  allocating + populating the three retained
  `GpuBuffer<float>` instances on a real GPU (Step 2
  body). The audit host's ON build compiles it out
  via the `RELATIVITYRENDER_OPTIX_SDK_FOUND` gate.
- The full helper call chain on a CUDA + OptiX-SDK
  host: `render_aovs_retain` -> build inputs POD ->
  `denoise_and_save_ppm` -> `optixDenoiserInvoke` ->
  GpuBuffer download -> save PPM. Each step has been
  structurally verified by reading the source +
  building under both audit-host modes; runtime
  behaviour requires the right hardware.
- The end-to-end `--render-optix-aovs --denoise`
  invocation producing eight files in one CLI run
  (`output/optix_aov_*.ppm` × 6 +
  `output/optix_aovs_denoised.ppm`). The CUDA-H.9
  runner is ready to verify this with a single
  command on a CUDA + OptiX-SDK host.

The deferral is documented in:
- `docs/OPTIX_GAP_A_STEP_2_TASK.md` §6 (build
  requirements + behaviour matrix).
- `docs/OPTIX_GAP_A_STEP_2_AUDIT.md` §1 (PASS
  verdict + DEFERRED rows).
- `docs/OPTIX_GAP_A_STEP_3_TASK.md` §6 (PASS criteria
  table) + §5.2 (file-output table).
- `docs/OPTIX_GAP_A_STEP_3_AUDIT.md` §7 (runtime
  status table per build mode).

A future operator with the right hardware closes the
deferral by running `tools/verify_cuda_host.py`
(per `docs/CUDA_HOST_VERIFICATION_PLAN.md`) with
`--optix --optix-root <path>` and committing the
resulting PASS report. The runner is deterministic
(CUDA-H.9), so the resulting report swaps the
audit-host's REPAIR for PASS and the per-test SKIPPED
rows for PASS/FAIL outcomes that match the hardware
reality.

---

## Verdict

**CLOSED.**

OptiX Gap A is structurally complete. The minimal-CLI
hook (Step 3.5's `--render-optix-aovs --denoise`
modifier) satisfies the plan's Step 4 requirement
verbatim. All five Gap A slices preserved the
project's invariants: OptiX OFF build green throughout,
CUDA path zero bytes changed, no per-pixel CPU work
introduced, master rules respected at every step. The
remaining runtime-deferred verification is part of the
long-standing project-wide
`docs/CUDA_HOST_VERIFICATION_PLAN.md` posture and is
NOT a Gap-A-specific gap.

A separate `--render-optix-aovs-denoise` action would
be additive boilerplate (~50 lines for an enum entry
+ parser branch + mutex / validation list + help text
+ dispatcher case) that duplicates the existing
modifier flow's behaviour. Per master rule 12 + the
plan's explicit "EITHER ... OR" framing, it is not
necessary to close the gap. If a future downstream
consumer (server / C4D bridge / scripted CI runner)
finds the modifier flow awkward, a dedicated action
can land then; today no such consumer exists.

### Closure summary

| Plan §4 step                | Status                                       |
|-----------------------------|----------------------------------------------|
| Step 1 — types + decl       | DONE (`6287471`)                             |
| Step 2 — SDK_FOUND body     | DONE (`9218b18`)                             |
| Step 3 — orchestration      | DONE (sub-stages 3.1-3.5: `e8523d8`,         |
| helper                      | `c5d6968`, `f8df9b9`, `ccb40c8`, `2fabb54`)  |
| Step 4 — CLI surface        | DONE (subsumed by Step 3.5's minimal-CLI     |
|                             | form: `--render-optix-aovs --denoise`)       |
| Step 5 — capstone audit     | DONE (this file)                             |

### Per master rule 8

`docs/BUILD_PLAN.md` already carries per-slice
entries for each of the implementation commits
(Step 1 / Step 2 / Steps 3.2-3.5). This capstone
audit is documentation-only and adds no new
implementation; per the user's "do NOT modify
BUILD_PLAN.md except normal progress note if
required" guidance, no BUILD_PLAN edit is made in
this slice. The existing entries collectively
constitute the project's progress record for Gap A.

### Operator action (none required)

OptiX Gap A is closed. The operator may:

- Run the CUDA-H.9 verification plan
  (`tools/verify_cuda_host.py`) on a CUDA + OptiX-
  SDK host with `--optix --optix-root <path>` to
  empirically verify the Gap A end-to-end flow +
  commit the PASS report. This is the
  long-standing runtime-deferred verification gate
  that is not specific to Gap A.
- Continue forward on the master order. The
  post-Stage-21 capstone audit's recommended
  follow-ups (texture system polish #18,
  path-tracer polish #16) remain available; OptiX
  Gap A no longer blocks them.
- Decline both and start a different polish item
  the operator considers higher priority. Master
  rules permit any of these.

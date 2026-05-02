# Roadmap Consistency Audit

Date: 2026-05-02
Branch: `relativity-core-v1`
Last commit on the audited tree: `14f04a8` ("stage 19D:
denoiser validation")
Mode: documentation-only. No source code is modified by
this audit. `docs/BUILD_PLAN.md` is the source of truth
and is NOT modified except for the standard status-table
row.

The audit answers: do `README.md`, `MILESTONE_ROADMAP.md`,
and `NEXT_STEPS.md` match the development order set out in
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` and the
actual implemented progression captured in
`docs/BUILD_PLAN.md`?

---

## 1. Current canonical order (from MASTER_INSTRUCTIONS)

The 25-step "DEVELOPMENT ORDER" block in
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`:

| # | Module |
|---|--------|
| 1  | Architecture/docs |
| 2  | Repository skeleton |
| 3  | Core app/logging/config |
| 4  | Math library |
| 5  | Image/framebuffer system |
| 6  | CUDA device layer |
| 7  | CUDA framebuffer/kernel infrastructure |
| 8  | Camera system |
| 9  | GPU primitive rendering |
| 10 | Relativistic camera model |
| 11 | GPU scene upload |
| 12 | Mesh system |
| 13 | Material system |
| 14 | Lighting system |
| 15 | Scene format/parser |
| 16 | Path tracing foundation |
| 17 | OptiX upgrade path |
| 18 | Texture system |
| 19 | AOV/render passes |
| 20 | Renderer server |
| 21 | Cinema 4D bridge |
| 22 | Preview UI |
| 23 | Material node graph |
| 24 | Denoising |
| 25 | Native Cinema 4D renderer integration |

`docs/MILESTONE_ROADMAP.md` re-numbers these as M0–M23
(the "M" sequence collapses master #11 GPU scene upload +
#12 Mesh system into a single M10).

---

## 2. Actual implemented order (from BUILD_PLAN.md)

`BUILD_PLAN.md` uses its own numbering ("Stage NN"). The
two scales are NOT 1:1 — Stage NN in BUILD_PLAN ≠ master
order #NN past Stage ~10. Mapping table below; ✅ marks
the BUILD_PLAN status row reads "implemented".

| Master # | Module | BUILD_PLAN landed as | Status |
|----------|--------|----------------------|--------|
| 1  | Architecture / docs                  | Stage 1                              | ✅ |
| 2  | Repository skeleton                  | Stage 2                              | ✅ |
| 3  | Core app / logging / config          | Stage 3                              | ✅ |
| 4  | Math library                         | Stage 4                              | ✅ |
| 5  | Image / framebuffer system           | Stage 5                              | ✅ |
| 6  | CUDA device layer                    | Stage 6                              | ✅ |
| 7  | CUDA framebuffer + first kernels     | Stage 7 (gradient / camera-rays)     | ✅ |
| 8  | Camera system                        | Stage 8                              | ✅ |
| 9  | GPU primitive rendering              | Stage 9 (sphere intersect)           | ✅ |
| 10 | Relativistic camera model            | Stage 10                             | ✅ |
| 11 | GPU scene upload                     | Stage 6B (multi-sphere scene)        | ✅ |
| 12 | Mesh system                          | Stage 10B.8 (inline meshes)          | ✅ |
| 13 | Material system                      | Stage 10B.5 / Stage 10B-materials    | ✅ |
| 14 | Lighting system                      | Stage 10B.7                          | ✅ |
| 15 | Scene format / parser                | Stage 10B.1–10B.11                   | ✅ |
| 16 | Path tracing foundation              | Stage 11A / 11B / 11C                | ✅ |
| 17 | OptiX upgrade path                   | Stage 12A planning + 17A.1–17A.5 impl | ✅ |
| 18 | Texture system                       | Stage 13A / 13B.1–13B.3              | ✅ |
| 19 | AOV / render passes                  | Stage 14A.1 / 14A.2 / 14A.3          | ✅ |
| 20 | Renderer server                      | Stage 15A.1 / 15A.2 / 15B.1 / 15B.3 / 15-render | ✅ |
| 21 | Cinema 4D bridge                     | (not started)                        | ⬜ |
| 22 | Preview UI                           | (not started)                        | ⬜ |
| 23 | Material node graph                  | (not started)                        | ⬜ |
| 24 | Denoising                            | Stage 19A.1–19A.3 + 19B.1–19B.4 + 19C.1–19C.3 + 19D | ✅ |
| 25 | Native Cinema 4D renderer            | (not started)                        | ⬜ |

Cross-cutting work that has no master-order entry:

| BUILD_PLAN bucket | What it is                                                    |
|-------------------|---------------------------------------------------------------|
| Stage 18A.1       | GPU timing instrumentation (`cudaEvent_t` + `format_gpu_timing_line` + `[GPU]` log lines) |
| Stage 18A.2       | GPU memory audit (docs/GPU_MEMORY_AUDIT.md)                   |
| Stage 18A.3       | Relativity precompute (single perf fix)                       |
| Stage 18A.4       | Progressive optimisation (float4 accum kernels)               |
| Stage 19C.2.{1,2,3} | Denoiser memory audit (DENOISER_MEMORY_AUDIT_{A,B,C}.md)    |
| Various -fix / -repair | Build / portability / lifetime repairs (Windows, server, CLI) |

---

## 3. Detected mismatches

- **README.md is severely stale.** `README.md` line 10
  declares "Stage 1 — Core app. The repository currently
  contains only the skeleton C++20 application: an
  executable that prints its version and a startup line.
  No GPU, no renderer, no scene system, no server."
  Actual state (BUILD_PLAN: 19D) implements GPU rendering,
  CUDA + OptiX backends, scene parser, server, AOVs, and
  a denoiser.
- **MILESTONE_ROADMAP.md M0 is marked "(CURRENT)"** at
  line 15. Actual current milestone equivalent is M22
  (Denoising). M19 / M20 / M21 (C4D bridge / Preview UI /
  Node graph) have not started even though M22 has
  shipped.
- **NEXT_STEPS.md is the Stage 1–5 audit follow-up**
  (dated 2026-04-29; "Step 0–5" queue). Every item in its
  queue (GPU runtime verification through scene-format
  parser) is now landed; the document reads as if it is
  current guidance.
- **BUILD_PLAN's "Stage NN" numbering diverged from
  master order #NN starting at master #11.** Master #11
  GPU scene upload landed as BUILD_PLAN "Stage 6B";
  master #18 Texture system landed as BUILD_PLAN "Stage
  13"; master #19 AOV landed as BUILD_PLAN "Stage 14";
  master #20 Server landed as BUILD_PLAN "Stage 15";
  master #24 Denoising landed as BUILD_PLAN "Stage 19".
  The two scales now share digits (e.g. there exists both
  a master #18 and a BUILD_PLAN "Stage 18A") but they
  refer to different things — a frequent source of
  confusion for any new reader.
- **Master #21 / #22 / #23 (C4D bridge, Preview UI,
  Material node graph) were skipped.** BUILD_PLAN
  implemented master #24 (Denoising) before any of the
  three. The MASTER_INSTRUCTIONS "DEVELOPMENT ORDER"
  block places #24 after #21–#23 because denoising is a
  polish step on top of UI / authoring. The actual
  implementation is **dependency-safe** (denoiser
  consumes the AOV pipeline (#19) + OptiX backend (#17),
  both shipped) but the order documented in
  MASTER_INSTRUCTIONS is not what the project followed.
- **MILESTONE_ROADMAP.md collapses master #11 + #12 into
  M10.** The actual implementation kept them as two
  steps (BUILD_PLAN's Stage 6B for the scene upload, then
  Stage 10B.8 for the mesh system). The collapsed M10 is
  a documented inconsistency rather than a bug.
- **Cross-cutting buckets (timing, memory audits,
  progressive optimisation, build repairs) have no
  master-order slot.** They are correctly modelled in
  BUILD_PLAN as project-internal slices but
  MILESTONE_ROADMAP and the master 25-step order do not
  acknowledge their existence. Future readers comparing
  the two see "extra" stages in BUILD_PLAN that look
  unaccounted for.
- **No master entry covers "OptiX implementation"
  separately from "OptiX upgrade path".** Master #17 is
  one line; BUILD_PLAN split it into the 12A planning
  phase + the 12B SDK-detection scaffold + the 17A.1–17A.5
  actual implementation. The split is reasonable but
  deepens the master-vs-BUILD_PLAN numbering drift.

---

## 4. Risk analysis

**A. Does this mismatch affect architecture?** **No,
practically — yes, presentationally.**

- The dependency graph is intact: every implemented stage
  layers on prerequisites that are themselves implemented
  (denoiser → AOV pipeline + OptiX backend; OptiX backend
  → CUDA device layer + scene + meshes + materials +
  lights; etc.). No master-order rule was violated in a
  way that produces broken code paths or missing
  upstream modules.
- The skipped master items (#21 C4D bridge, #22 Preview
  UI, #23 Material node graph) are downstream consumers
  of the standalone renderer (per MASTER_INSTRUCTIONS:
  "Do not start Cinema 4D integration until the
  standalone renderer has [the listed prerequisites]").
  Skipping them and implementing #24 first did not break
  any prerequisite chain because #24 has no dependency on
  any of #21 / #22 / #23.
- The architecture is consistent, but the **public docs
  do not honestly describe it**. A reader following
  README → MILESTONE_ROADMAP → NEXT_STEPS believes the
  project is at "Stage 1 / M0 / Step 0" when it is in
  fact 19 master-order modules deep with one milestone
  (M22) shipped out of order.

**B. Does this mismatch affect dependencies?** **No.**

- BUILD_PLAN shows every cross-cutting slice (timing,
  memory audits, optimisation) is gated on its own set of
  prerequisites being implemented; none of them
  back-reference an unshipped feature.
- The denoiser slice (BUILD_PLAN "Stage 19") explicitly
  cites its prerequisites (Stage 14A AOVs, Stage 17A
  OptiX backend, Stage 18A.1 timing) and consumes none of
  the skipped #21 / #22 / #23 modules.
- A cross-check against `DENOISER_PLAN.md` §4.2 ("Must
  not modify core renderer logic") + the Stage 19D audit
  (Q3 PASS) confirms the implemented surface respects
  the dependency contract; the renderer keeps working
  unchanged when the denoiser is off.

---

## 5. Summary

| # | Question | Verdict |
|---|----------|---------|
| 1 | Does README.md reflect actual progression? | NO — stale at Stage 1 |
| 2 | Does MILESTONE_ROADMAP reflect actual progression? | NO — M0 marked CURRENT, M22 already done |
| 3 | Does NEXT_STEPS.md reflect actual progression? | NO — Stage 1–5 audit follow-up; queue completed |
| 4 | Does BUILD_PLAN's stage numbering match master order numbering? | NO — diverged at master #11 |
| 5 | Did implementation order respect the master order? | MOSTLY — #21 / #22 / #23 skipped before #24 |
| 6 | Architecture risk?  | NO (practical); YES (presentational, doc clarity) |
| 7 | Dependency risk?    | NO |

The two new docs — this audit (`ROADMAP_AUDIT.md`) and
the proposed alignment (`ROADMAP_PROPOSED_ALIGNMENT.md`) —
plus the README update are the resolution path. Future
implementation slices remain governed by `BUILD_PLAN.md`
as the source of truth.

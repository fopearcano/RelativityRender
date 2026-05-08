# Roadmap — Proposed Alignment

Date: 2026-05-02
Branch: `relativity-core-v1`
Companion to `docs/ROADMAP_AUDIT.md`.
Mode: **proposal** — describes the alignment but does not
adopt it. The 25-step "DEVELOPMENT ORDER" block in
`RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt` and the
status-table content of `docs/BUILD_PLAN.md` are NOT
modified by this document. A separate slice may adopt
this alignment after review.

The audit (`ROADMAP_AUDIT.md`) found that:

- BUILD_PLAN's "Stage NN" numbering diverged from the
  25-step master-order #NN starting at master #11.
- The master order's #21 / #22 / #23 (C4D bridge,
  Preview UI, Material node graph) were skipped; #24
  (Denoising) shipped first.
- Cross-cutting work (timing, memory audits, perf,
  build repairs) has no master-order slot.

This document proposes a corrected order that:

1. Respects the GPU-first architecture.
2. Reflects the actual implementation order already
   landed (Stages 1–15 in BUILD_PLAN's numbering, plus
   the 16 / 17 / 18 / 19 buckets actually shipped).
3. Keeps future stages logically consistent with the
   skipped master items so they remain pickable.

---

## Constraints

- **Stages 1–15 in BUILD_PLAN are frozen.** They are
  already implemented; renumbering them would break
  every downstream cross-reference (commit messages,
  audit docs, in-source comments). Any "alignment"
  applies only to the description of what stages mean,
  not their numbers.
- **Master 25-step list is the canonical priority
  order.** Where the actual implementation diverged
  (skipping #21–#23 and shipping #24 first), the
  proposal documents the deviation rather than
  retroactively reordering the implemented stages.
- **No new code.** This document defines what future
  slices SHOULD look like; it does not add any.
- **BUILD_PLAN remains the source of truth.** The
  proposal here is a presentation layer over what
  BUILD_PLAN already records.

---

## Proposed alignment

### A. Two-axis numbering, made explicit

Adopt the convention that two numbering scales coexist
and label them clearly:

- **Master order #NN** — the 25-step priority list in
  `RELATIVITYRENDER_CLAUDE_MASTER_INSTRUCTIONS.txt`. Used
  in design docs, BUILD_PLAN entries, and commit
  messages to anchor each slice to its master-order
  module ("master order #17 OptiX upgrade path").
- **BUILD_PLAN Stage NN** — the project-internal slice
  identifier ("Stage 17A.5" / "Stage 19C.3") used in
  status-table rows + commit subject lines. Stages can
  be hierarchical (`19C.2.3` = bucket 19, sub-bucket
  C, sub-sub 2.3) and need not be 1:1 with master order
  numbers.

Existing docs already follow this implicitly (every Stage
17A.x entry in BUILD_PLAN names "master order #17 OptiX
upgrade path"). Making it explicit in README +
MILESTONE_ROADMAP closes the audit's mismatch #4.

### B. Re-state the implemented mapping

Adopt the table from `ROADMAP_AUDIT.md` §2 as canonical
in MILESTONE_ROADMAP — it shows master-order # ↔
BUILD_PLAN Stage in one place. No data is added; the
mapping is already implicit across BUILD_PLAN entries.

### C. Accept the skipped-then-shipped pattern for #24

Master items #21 / #22 / #23 / #24 / #25 are the
"polish + integration" tail of the master order. The
implementation skipped #21–#23 and shipped #24
(Denoising) early. The proposal:

- Document the deviation in MILESTONE_ROADMAP as a
  one-line note ("Denoising shipped before Cinema 4D
  bridge — see docs/STAGE_19_DENOISER_AUDIT.md Q3 for
  the dependency-safety verification").
- Keep the master-order priority list unchanged: future
  picks should still prefer #21 (C4D bridge) before
  #25 (Native C4D renderer), per the master rule "Do
  not start Cinema 4D integration until the standalone
  renderer has [the listed prerequisites]".

### D. Cross-cutting buckets get an "X" prefix

Items that have no master-order slot — GPU timing,
memory audits, perf optimisation, build repairs — get
acknowledged as a cross-cutting axis in
MILESTONE_ROADMAP rather than shoehorning them into the
linear 25-step list. Suggested label: "Xn" (X1 = GPU
timing / 18A.1, X2 = GPU memory audit / 18A.2, etc.).
This is purely presentational; BUILD_PLAN's existing
"Stage 18A.1" labels stay.

---

## Proposed future order

After the implementation already shipped (BUILD_PLAN
Stages 1–19D plus the cross-cutting Xs), the priority
order for the next slices is:

| Priority | Master # | Module | Notes |
|----------|----------|--------|-------|
| 1 | (Stage 19E pending) | Denoiser CUDA-host validation pass | Closes Q1 / Q2 of `STAGE_19_DENOISER_AUDIT.md`. Runtime PPM visual diff on a CUDA + OptiX-SDK host. No code change required. |
| 2 | #21 | Cinema 4D bridge | The next master item that fits the existing standalone-renderer prerequisites (CUDA detection, GPU framebuffer, GPU camera rays, GPU primitive intersection, GPU relativistic camera, GPU scene upload, GPU triangle mesh, materials, lights, scene parser, server — all ✅ in BUILD_PLAN). Per the master rule, it is the earliest C4D-side slice the project may now start. |
| 3 | #22 | Preview UI | Standalone preview viewer outside C4D. Consumes the renderer-server protocol shipped in Stage 15. |
| 4 | #23 | Material node graph | Visual material authoring; emits scene-format material blocks. Pure UI. |
| 5 | #25 | Native Cinema 4D renderer | Last master item; depends on #21 (bridge) being shipped. |
| 6+ | various | Cross-cutting Xn slices as needed | Future audits / perf fixes / portability repairs. |

The denoiser validation pass (Priority 1 above) is a
**doc / runtime-only** slice; it does not add code, and
it is the only "Stage 19E" prefix that should ever be
used. Anything else in 19 should be a 19F+ extension.

---

## What this proposal does NOT change

- **BUILD_PLAN's existing numbering.** Every Stage NN
  entry already in BUILD_PLAN keeps its number.
- **Past audit doc names.**
  `STAGE_19_DENOISER_AUDIT.md`,
  `DENOISER_MEMORY_AUDIT_{A,B,C}.md`, etc. all stay
  exactly where they are.
- **Master-order 25-step list.** Per the prompt's
  "you are NOT allowed to change past implemented
  stages": the master list is canonical and stays.
- **The GPU-first architecture.** Every proposed
  future slice respects the master rule "All per-pixel/
  per-ray rendering must happen on GPU" and the
  CPU-only-orchestrates rule (#5 / #6 / #7 in
  MASTER_INSTRUCTIONS).

---

## Adoption path

This is a proposal. To adopt it, a future slice would:

1. Update `MILESTONE_ROADMAP.md`'s M0 "(CURRENT)"
   marker to point at the actual current milestone +
   add the master-#-to-BUILD_PLAN-Stage mapping table.
2. Mark `NEXT_STEPS.md` as historical (its Step 1–5
   queue is complete) and replace with a forward-
   looking pointer to BUILD_PLAN's status table + the
   priority list above.
3. Continue the README update started in this slice
   (see the standalone README diff in the same commit).

This document, `ROADMAP_AUDIT.md`, and the README diff
together close the consistency mismatch identified by
the audit. None of them changes BUILD_PLAN's
canonical content.

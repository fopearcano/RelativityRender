# `--enable-nee` Help-Text Repair — Audit

Date: 2026-05-07.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `fc4c482` ("cli: fix
stale --enable-nee help text (NEE.6 audit §11.1
REPAIR)").
Plan source:
`docs/PATH_TRACER_ENABLE_NEE_CLI_AUDIT.md` §11.1
("REPAIR candidates — Help text is stale (cosmetic
only)").
Mode: documentation only. **No source code is modified
by this audit.**
Auditor: Claude Code, on the audit host (no CUDA
Toolkit; `command -v nvcc` returns nothing;
`/usr/local/cuda` does not exist; presumably no NVIDIA
GPU). Same fingerprint as the parent NEE.6 audit.

Scope: walk the five user-enumerated checks against
HEAD `fc4c482` + record a closing PASS / REPAIR /
BLOCKED verdict. The NEE.6 audit identified ONE REPAIR
candidate (cosmetic-only stale wording in the
`--enable-nee` help block); this audit verifies the
fix shipped at `fc4c482` resolved it cleanly without
any renderer / kernel / dispatcher side effects.

Verdict legend matches every prior PT-P.x / TEX-P.x /
firefly-clamp-CLI / NEE-skeleton / NEE.6 audit:

- **PASS** — implemented; type-checked on the audit
  host; AND empirically exercisable on the audit host
  with a recorded happy-path run.
- **REPAIR** — implemented but a defect or
  inconsistency was found that should be patched.
- **BLOCKED** — verification cannot proceed on this
  audit host AND the structural argument also cannot
  be confirmed without runtime evidence.

---

## 1. `--help` documents `--enable-nee`

**PASS.**

Empirically verified during this audit:

```
$ ./build/bin/RelativityRender --help | grep -A 9 enable-nee
  --enable-nee          NEE.5 modifier flag (not an action). Enables
                        explicit direct-light sampling (Next Event
                        Estimation) at every bounce vertex of the path
                        tracer. Default off matches the pre-NEE.5
                        emission + environment-only behaviour byte-for-byte.
                        Read by --render-pathtrace and
                        --render-optix-pathtrace; ignored by every other
                        action.
                        Light-type scope: Point + Directional contribute;
                        Area / Environment are placeholder and contribute
```

The corrected 10-line block prints in full. Operator
finds the flag's documentation without reading
source.

### 1.1 Stale phrase confirmed gone

The pre-`fc4c482` block contained the stale phrase
"the OptiX dispatcher consumption is deferred to a
follow-up slice" (authored at NEE.5a when the OptiX
side was still pending). NEE.6 §11.1 flagged this as
the cosmetic-only REPAIR.

Empirically verified during this audit:

```
$ ./build/bin/RelativityRender --help | grep "deferred to a follow-up"
$ echo $?
1   # no match — the stale phrase is gone
```

### 1.2 OptiX dispatcher now correctly listed

The corrected wording explicitly names BOTH
consumers (matching the actual runtime behaviour
verified by the parent NEE.6 audit §5):

```
Read by --render-pathtrace and
--render-optix-pathtrace; ignored by every other
action.
```

Empirically verified:

```
$ ./build/bin/RelativityRender --help | grep -A 9 enable-nee | grep -q render-optix-pathtrace
$ echo $?
0   # match — --render-optix-pathtrace is listed
```

The help text now matches the runtime behaviour: a
caller passing `--enable-nee --render-optix-pathtrace`
will see the flag honoured (NEE.5b commit `f0bf3e9`
shipped the OptiX dispatcher wiring + light upload).

---

## 2. Wording matches existing CLI style

**PASS.**

The replacement wording mirrors the `--firefly-clamp`
help-text idiom verbatim, modulo the line-break shape
needed to fit the shorter `--enable-nee` sentence.

### 2.1 Source-level comparison

`--firefly-clamp` block at `CommandLine.cpp:967-970`:

```
... symmetrically on both CUDA and OptiX backends. Read by
--render-pathtrace and --render-optix-pathtrace; ignored
by every other action. Negative values are rejected at parse
time ("--firefly-clamp must be >= 0").
```

`--enable-nee` block at `CommandLine.cpp:984-989`
(post-`fc4c482`):

```
... emission + environment-only behaviour byte-for-byte.
Read by --render-pathtrace and
--render-optix-pathtrace; ignored by every other
action.
Light-type scope: ...
```

The "Read by --render-pathtrace and / --render-optix-pathtrace; ignored / by every other action."
phrasing is identical word-for-word. The line breaks
differ slightly because `--enable-nee`'s sentence is
shorter (no "Negative values are rejected" tail), so
the three-line break shape is tighter than
firefly-clamp's four-line shape — but the idiom is
the same.

### 2.2 Column-width comparison

Both blocks use the same 24-space leading indent for
continuation lines (`"                        "`).
The `--enable-nee` label column (`  --enable-nee`)
ends at column 22; the description column starts at
column 24 — same as `--firefly-clamp` and every other
modifier-flag block. The `--help` output lays out
visually consistent across every flag.

### 2.3 Sentence shape consistency

Each modifier-flag block in the help text follows the
same shape:

1. Modifier-flag declaration sentence (line 1):
   "Modifier flag (not an action). Enables / Sets / ..."
2. Behavioural description (lines 2-N): what the
   flag does at default + when present.
3. Reader list ("Read by --X and --Y; ignored by
   every other action.").
4. Optional caveats (e.g. firefly-clamp's "Negative
   values are rejected" tail).

`--enable-nee`'s post-fix block matches this shape
exactly:
1. "NEE.5 modifier flag (not an action). Enables..."
   (line 974).
2. "explicit direct-light sampling..." (lines 976-983).
3. "Read by --render-pathtrace and / --render-optix-pathtrace;
   ignored / by every other action." (lines 984-989).
4. "Light-type scope: Point + Directional contribute;
   Area / Environment are placeholder..." (lines
   990-993).

The reader-list (item 3) is the section the audit
fixed; items 1, 2, 4 were unchanged at this slice.

### 2.4 No deviation from idiom

Cross-checked against every other modifier-flag in
the help text (`--denoise`, `--beta`, `--firefly-clamp`,
`--width`, `--height`, `--output`):

- `--denoise` (line 906) — shorter pattern: no
  separate "Read by" clause; the description embeds
  the consumer ("supported by --render-aovs"). Pre-
  dates the `--firefly-clamp` idiom.
- `--beta` (line 936) — uses a similar sentence shape
  but no explicit "Read by" clause (only the
  `--render-demo` action consumes it; documented
  inline).
- `--firefly-clamp` (line 957) — the canonical idiom
  this slice mirrored.
- `--enable-nee` (line 974) — matches `--firefly-clamp`.
- `--width`, `--height`, `--output` — value-bearing
  flags; different shape (no descriptive prose).

The `--firefly-clamp` idiom is the most recently
established pattern (Stage 19+ vintage), and is what
NEE.5 + NEE.5b adopted. The repair preserves that
adoption — the post-fix `--enable-nee` block is now
in the same family as `--firefly-clamp`.

---

## 3. No renderer behavior changed

**PASS.**

The diff between the audited commit (`fc4c482`) and
its parent (`22b9d21`, the NEE.6 audit doc-only
commit) confirms the change is scoped to a single
text-emitter region in `src/core/CommandLine.cpp`:

### 3.1 Source-level diff

```
$ git diff fc4c482~1..fc4c482 -- src/
diff --git a/src/core/CommandLine.cpp b/src/core/CommandLine.cpp
@@ -981,10 +981,10 @@
                                   "NEE.5\n"
        << "                        emission + environment-only behaviour "
                                   "byte-for-byte.\n"
-       << "                        Read by --render-pathtrace; the OptiX "
-                                  "dispatcher\n"
-       << "                        consumption is deferred to a follow-up "
-                                  "slice.\n"
+       << "                        Read by --render-pathtrace and\n"
+       << "                        --render-optix-pathtrace; ignored "
+                                  "by every other\n"
+       << "                        action.\n"
        << "                        Light-type scope: Point + Directional "
                                   "contribute;\n"
```

**+4 / -4 across exactly one file** (`src/core/CommandLine.cpp`).
Scope: lines 984-989 in the post-fix file (lines
984-987 pre-fix). The change is contained inside
`std::string CommandLine::usage(std::string_view
argv0)` — a host-only function whose entire purpose
is to build the help-text string for the `--help`
action. The function returns a `std::string`; the
caller (`main.cpp`'s `--help` handler) prints it to
stdout and exits.

### 3.2 No-touch invariants verified

`git diff fc4c482~1..fc4c482 -- <path>` returns 0
bytes for every must-not-touch path:

| Must-not-touch path                       | git diff bytes |
|-------------------------------------------|---------------:|
| `src/cuda/`                               | 0              |
| `src/optix/`                              | 0              |
| `src/pathtracer/`                         | 0              |
| `src/renderer/`                           | 0              |
| `src/io/`                                 | 0              |
| `src/scene/`                              | 0              |
| `src/material/`                           | 0              |
| `src/lighting/`                           | 0              |
| `src/texture/`                            | 0              |
| `src/gpu/`                                | 0              |
| `src/server/`                             | 0              |
| `src/main.cpp`                            | 0              |
| `src/core/Config.{h,cpp}`                 | 0              |
| `src/core/CommandLine.h`                  | 0              |
| `src/core/Logger.{h,cpp}`                 | 0              |
| `tests/`                                  | 0              |
| `scenes/`                                 | 0              |
| `tools/verify_cuda_host.py`               | 0              |
| `CMakeLists.txt`                          | 0              |

The kernel guards (`CudaPathTracer.cu:276` and
`__raygen__pathtrace`'s NEE branch), the field
declarations (`Config::enable_nee`,
`PathTraceConfig::enable_nee`,
`OptixLaunchParams::enable_nee`), the dispatcher
signatures + bodies (`OptixRenderer::render_pathtrace*`,
`run_render_pathtrace`, `run_render_optix_pathtrace`),
the parser arm itself (`CommandLine.cpp:417-433`),
and the test binaries (`cli_tests`,
`pathtracer_nee_tests`) are all byte-identical
across `fc4c482~1..fc4c482`.

### 3.3 Behavioural argument

The text emitter `CommandLine::usage()` runs ONLY
when `--help` is the active action (or when usage
is appended to a parser-level error message). It
has no kernel / dispatcher / renderer side
effects. Calling it with the new wording produces
a different `std::string`; printing that string
produces different stdout bytes during a `--help`
invocation. No other action's behaviour is
affected — the emitter is not consulted by any
non-`--help` code path.

This is the formal "no behaviour change" argument:
the only function whose output changed is `usage()`;
the only consumer of `usage()`'s output is the
`--help` print path; therefore no kernel / dispatcher /
renderer behaviour can change.

### 3.4 Default-OFF byte-identity preserved

The static IEEE-754 + RNG-stream argument from
`PATH_TRACER_NEE_AUDIT.md` §1.2 is a source-level
argument over the kernel guard + the in-guard
`next_float(rng)` placement + the `radiance`
accumulator arithmetic. This slice did NOT touch any
of those three contributors. The argument carries
forward unchanged; the default-OFF byte-identity
contract is preserved.

---

## 4. Build / test status

**PASS.**

Re-ran during this audit:

| Config      | RR_ENABLE_CUDA | RR_ENABLE_OPTIX | Build     | ctest    |
|-------------|:--------------:|:---------------:|-----------|:--------:|
| `build`     | OFF            | OFF             | clean     | 9/9 PASS |
| `build-ON`  | OFF            | ON (fallback)   | clean     | 10/10 PASS |

Both audit-host configs report zero new compiler
warnings under the `rr_apply_warnings`-enforced
`-Wall -Wextra -Wpedantic` triple. ctest counts
unchanged from the NEE.5 final-test-slice baseline
(commit `8d9e75f`); the cosmetic edit added no test
binary and removed none.

### 4.1 Per-binary case counts unchanged

- `cli_tests`: 31/31 passed (the parser-arm
  recogniser is unchanged; the test exercises
  `parse()` not `usage()`).
- `pathtracer_nee_tests`: 34/34 passed (helper
  determinism + bit-default anchors unchanged).

Empirically verified during this audit:

```
$ ./build/bin/cli_tests 2>&1 | tail -1
cli_tests: 31/31 passed

$ ./build/bin/pathtracer_nee_tests 2>&1 | tail -1
pathtracer_nee_tests: 34/34 passed
```

### 4.2 Smoke matrix

| Smoke                                              | Result                                              |
|----------------------------------------------------|-----------------------------------------------------|
| `--help \| grep -A 9 enable-nee`                   | corrected 10-line block prints                      |
| `--help \| grep "deferred to a follow-up"`         | empty (stale phrase gone)                           |
| `--help \| grep render-optix-pathtrace` (in block) | match (OptiX dispatcher correctly listed)           |
| `--scene-info scenes/test_textured_material.rrscene` (TEX-P.6) | three-case log sequence intact; fixups applied: 2 |
| `cli_tests`                                        | 31/31 pass                                          |
| `pathtracer_nee_tests`                             | 34/34 pass                                          |
| ctest OFF                                          | 9/9 pass                                            |
| ctest ON                                           | 10/10 pass                                          |

### 4.3 Diff size sanity check

The slice's source-diff is +4/-4 across one file.
This is consistent with the audit's §11.1 description
("Three-line wording change at `CommandLine.cpp:980-981`
+ adjacent continuation"). The post-fix block is one
line longer than the pre-fix block (3 output lines
vs 2 output lines for the "Read by..." section), so
the `--help` total line count grew by exactly 1
line. This does not affect any pager / wrapping
contract that a caller might rely on; the help
text is already multi-page on a typical 80x24
terminal.

---

## 5. Verdict

| #  | Audit item                                         | Result   |
|----|----------------------------------------------------|----------|
| 1  | `--help` documents `--enable-nee`                  | PASS     |
| 2  | Wording matches existing CLI style                 | PASS     |
| 3  | No renderer behaviour changed                      | PASS     |
| 4  | Build / test status                                | PASS     |
| 5  | Closing verdict                                    | **PASS** |

**Overall verdict: PASS.**

The cosmetic REPAIR identified by
`PATH_TRACER_ENABLE_NEE_CLI_AUDIT.md` §11.1 is fixed
cleanly. The corrected `--enable-nee` help block
prints exactly per the audit's recommended wording,
mirrors the `--firefly-clamp` "Read by\n--X and --Y;
ignored\nby every other action." idiom, and
introduces zero behavioural change (verified at four
levels: source-level diff scoping, function-purpose
argument, no-touch grep, ctest counts unchanged).

### 5.1 Master rule compliance

- Build incrementally (rule 1) + every step
  compilable (rule 2): preserved trivially. Both
  audit-host configs re-built + re-tested green
  during this audit.
- No fake stubs (rule 3): the corrected text emits
  visibly correct output (verified by §1.1 + §1.2
  smokes); no placeholder or partial wording.
- No CPU per-pixel work (rule 5/7): not applicable
  to a help-text emitter.
- Module boundaries (rule 9): the edit is scoped to
  `usage()` in `CommandLine.cpp`. No cross-module
  ripple.
- Update BUILD_PLAN (rule 8): the parent slice
  (commit `fc4c482`) added a BUILD_PLAN entry; this
  audit will add one too (alongside this audit
  doc).
- Documentation only / do not modify source code
  (this slice's rules): zero source edits in this
  audit slice.

### 5.2 No new REPAIR candidates

This audit's read of the source + the `--help`
output finds zero new REPAIR candidates. The
cosmetic REPAIR list closes fully: NEE.6 §11.1
identified one item; commit `fc4c482` fixed it; this
audit verifies the fix.

---

## 6. Sub-arc closure

The NEE arc's documentation loop now closes
completely:

| Slice                              | Role                                            | Commit       |
|------------------------------------|-------------------------------------------------|--------------|
| NEE.1                              | Task definition for the NEE arc                 | (docs)       |
| NEE.2                              | CUDA NEE skeleton (impl)                        | `6f49c55`    |
| NEE.3                              | CUDA NEE skeleton audit                         | `c857f29`    |
| NEE.4                              | OptiX-side NEE mirror + first host helper test  | `b29daae`    |
| NEE.5 task brief                   | `--enable-nee` CLI flag task definition         | `0e64240`    |
| NEE.5a                             | CLI parse + Config + CUDA mapping               | `122b81c`    |
| NEE.5b                             | OptiX dispatcher + light upload                 | `f0bf3e9`    |
| NEE.5 dispatch verification        | Doc-only verification of dispatch wiring        | `739b332`    |
| NEE.5 tests                        | CLI parser tests + helper byte-identity         | `8d9e75f`    |
| NEE.6                              | `--enable-nee` CLI audit                        | `22b9d21`    |
| NEE.6 §11.1 REPAIR                 | Stale help-text wording fix                     | `fc4c482`    |
| **NEE.6 §11.1 REPAIR audit**       | **This audit** — verifies the fix               | (docs)       |

`PATH_TRACER_NEE_TASK.md` §1's full operator-facing
contract is implemented + documented + audited. The
NEE arc's REPAIR list is empty. The runtime-deferred
checks (NEE.6 §9.1–§9.6: default-off PPM `cmp`,
visible NEE-on noise reduction, cross-backend
convergence, no-light safety net, CUDA-host ctest,
CUDA-OPTIX-VERIFY refresh) remain deferred to a real
CUDA + OptiX-SDK host operator session.

### 6.1 Recommended next step

The NEE arc is at a clean stopping point. Three
viable directions:

1. **Trigger a CUDA + OptiX-SDK host verification
   run.** Single operator session flips ~25
   cumulative DEFERRED rows across the PT-P.x +
   firefly-clamp-CLI + NEE.x arcs to PASS. Most
   immediately satisfying; pays accumulated
   runtime debt in one session.

2. **Pivot to master order #16+ (non-diffuse BSDFs /
   area-light NEE / MIS).** Addresses the v1
   "double-count window" the NEE task brief
   reserved for a future slice. Natural successor
   to NEE.6.

3. **Pivot further to master order #18+ (textures)
   or beyond.** Fewer prerequisites; comparable
   user-visible payoff.

Recommended sequencing: **(1)** when a CUDA +
OptiX-SDK host becomes available, then **(2)** as
the next major arc.

---

Mode reminder: **documentation only.** This audit
makes zero source-code changes. The REPAIR list is
empty; the previous REPAIR (NEE.6 §11.1) was fixed
at commit `fc4c482` and verified here.

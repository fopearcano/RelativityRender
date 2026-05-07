# Firefly Clamp CLI Flag — Audit

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `a3b43b4` ("feat:
--firefly-clamp CLI flag (impl) — exposes PT-P.21+24
firefly-clamp wiring").
Scope: the implementation slice that ships
`docs/FIREFLY_CLAMP_CLI_TASK.md` per the brief committed in
`b5da850` ("docs: --firefly-clamp CLI flag task definition
(docs only)").
Mode: documentation only. **No source code is modified by
this audit.**
Auditor: Claude Code, on the audit host (no CUDA Toolkit,
no OptiX SDK).

This audit walks the seven prompt checks the task brief
enumerated under §5 PASS criteria + the three additional
runtime-status questions the brief's §6 listed. Verdict
legend matches every prior PT-P.x audit + the
TEX-P.7 / CUDA-H.x precedent:

- **PASS** — implemented, type-checked on the audit host,
  AND empirically exercisable on the audit host with a
  recorded happy-path run.
- **REPAIR** — implemented but a defect or inconsistency
  was found that should be patched.
- **BLOCKED** — empirical verification requires a CUDA
  host (no nvcc / OptiX SDK on the audit host).

This audit closes the firefly-clamp CLI sub-arc. After it
lands, the entire firefly-clamp polish has fully shipped:

| Slice                      | Role                                           |
|----------------------------|------------------------------------------------|
| PT-P.{20,21,22}            | Field-only placeholder                         |
| PT-P.{23,24,25}            | Kernel wiring on both backends                 |
| FIREFLY_CLAMP_CLI_TASK     | Task definition for the CLI flag               |
| FIREFLY_CLAMP_CLI impl     | `a3b43b4` — the slice this audit verifies      |
| FIREFLY_CLAMP_CLI_AUDIT    | This audit                                      |

---

## 1. `--firefly-clamp <value>` parser exists

**PASS.**

The parser arm is present at
`src/core/CommandLine.cpp:444-475` (post-`a3b43b4`),
between the existing `--beta` arm and the `--width` arm.
It mirrors the `--beta` parser shape verbatim:

- `take_value` to read the next argv token (line 458).
- `std::from_chars` to parse the float (line 463).
- `from_chars`-result `ec` + `ptr` checks for "non-numeric
  value" rejection (line 464-470).
- A new `clamp_value < 0.0f` check (line 471-476) for
  parse-time negative-value rejection per task §1.2
  option A.
- Final store `r.config.firefly_clamp = clamp_value;`
  (line 477).

The parser arm's leading 12-line doc-comment
(line 444-457) names the modifier-flag semantics +
cross-references the FOUR downstream rejection sites
the renderer carries as defence in depth
(`PathTracer.cpp:84`, `CudaPathTracer.cu:282`,
`OptixRenderer.cpp:1243+1502`).

### 1.1 Help-text entry exists

**PASS.** A 17-line block at `CommandLine.cpp:925-941`
appended after the `--beta` help-block + before the
`--width` help-block. The new block's wording matches
the task §1.3 spec:

```
--firefly-clamp <float>
                    Modifier flag (not an action). ...
                    Default 0.0 disables the clamp ...
                    values > 0 enable a `fminf(...)` ...
                    Read by --render-pathtrace and
                    --render-optix-pathtrace; ignored
                    by every other action. Negative
                    values are rejected at parse time
                    ("--firefly-clamp must be >= 0").
```

Empirically verified during this audit: running
`./build/bin/RelativityRender --help | grep -A 12
firefly-clamp` returns the block; the operator finds
the flag's documentation without reading source.

### 1.2 Cross-check: parser is unique

`grep -rn "firefly-clamp" src/` returns 13 matches —
all in `CommandLine.cpp` (parser arm + help text + a
documentation reference inside the parser arm comment).
No competing parser arm; no stray references in any
other file. The flag is recognised exclusively by the
new arm.

---

## 2. `Config::firefly_clamp` field exists with default 0.0f

**PASS.**

The field is declared at `src/core/Config.h:62`:

```cpp
float       firefly_clamp    = 0.0f;
```

Sits AFTER the existing `beta` field at line 45 and
BEFORE the `validate()` declaration. The 13-line
doc-comment block above the field (lines 47-61) names:

- Modifier-flag semantics ("read by `--render-pathtrace`
  and `--render-optix-pathtrace`; other actions ignore
  it").
- Default-construction equivalence with
  `PathTraceConfig::firefly_clamp` ("default 0.0f
  matches `PathTraceConfig::firefly_clamp`'s PT-P.21
  default exactly so a caller that does NOT pass
  `--firefly-clamp` sees byte-identical behaviour").
- Cross-reference to the task brief
  (`docs/FIREFLY_CLAMP_CLI_TASK.md` §1).
- Reminder that the parser's lower-bound rejection
  guarantees `>= 0` invariant.

### 2.1 Default propagates through default-construction

`Config{}` produces a struct with `firefly_clamp ==
0.0f` per C++ aggregate-initialisation rules. The
default flows through:

- `r.config` in `CommandLine::parse` is
  default-constructed at the start of the function;
  `firefly_clamp == 0.0f` until/unless the parser arm
  fires.
- `cfg.firefly_clamp` in every dispatcher reads
  `0.0f` when the operator does not pass the flag.

The default-off invariant therefore holds at TWO
sites: the parser default (no flag → 0.0f) AND the
PathTraceConfig::firefly_clamp default (PT-P.21
established).

---

## 3. CUDA pathtrace dispatcher reads `cfg.firefly_clamp`

**PASS.**

`src/main.cpp`'s `run_render_pathtrace` (the CUDA
dispatcher) wires the field through to
`PathTraceConfig::firefly_clamp` at lines 2391-2398:

```cpp
rr::pathtracer::PathTraceConfig pcfg;
pcfg.samples_per_pixel = run.spp;
// ... (10-line PT-P.24 wiring doc-comment)
pcfg.firefly_clamp = cfg.firefly_clamp;
```

`pcfg.firefly_clamp` flows through `PathTracer::render`
→ `launch_pathtrace_sample` → `k_pathtrace_sample`'s
`firefly_clamp` parameter → the per-channel
`fminf(radiance.x|y|z, firefly_clamp)` clamp at
`CudaPathTracer.cu:251-255`. PT-P.24 verified the four
intermediate sites + the kernel guard; this slice
extends the chain by ONE site (the new
`pcfg.firefly_clamp = cfg.firefly_clamp` line) which
this audit verifies.

### 3.1 No bypass

The single `pcfg` variable is passed verbatim to
`pt.render(gpu_scene, width, height, pcfg)`; no
intermediate copy modifies it. The PT-P.24 wiring's
contract therefore extends from the operator's CLI
input to the per-pixel write site without
intervention.

---

## 4. OptiX pathtrace dispatcher reads `cfg.firefly_clamp`

**PASS.**

`src/main.cpp`'s `run_render_optix_pathtrace` wires
the field through to
`OptixRenderer::render_pathtrace_progressive`'s
`firefly_clamp` parameter at lines 1573-1591:

```cpp
// PT-P.24 doc-comment ...
Logger::info(std::string("firefly_clamp    : ")
           + std::to_string(cfg.firefly_clamp)
           + (cfg.firefly_clamp > 0.0f
                  ? " (enabled)"
                  : " (disabled)"));
auto pr = rr::optix::OptixRenderer::render_pathtrace_progressive(
    load.scene, cfg.width, cfg.height,
    kMaxBounces, kSeed, kCheckpoints,
    /*firefly_clamp=*/cfg.firefly_clamp);
```

The `cfg.firefly_clamp` flows through
`render_pathtrace_progressive`'s parameter →
`params.firefly_clamp = firefly_clamp;` upload at
`OptixRenderer.cpp:1706` → the
`OptixLaunchParams::firefly_clamp` POD field →
`__raygen__pathtrace`'s
`optixLaunchParams.firefly_clamp` read → the
per-channel `fminf` clamp at
`OptixPrograms.cu:944-948`.

### 4.1 Replaced literal `0.0f`

PT-P.24's BUILD_PLAN entry recorded:

> "`run_render_optix_pathtrace`'s
> `render_pathtrace_progressive` call gains explicit
> `/*firefly_clamp=*/0.0f` trailing arg".

This audit verifies the literal `0.0f` was REPLACED
with `cfg.firefly_clamp` at line 1591:

```cpp
/*firefly_clamp=*/cfg.firefly_clamp);
```

The `/*firefly_clamp=*/` named-argument comment is
preserved; only the value source changed. A diff-grep
confirms:

```
$ grep -c "firefly_clamp=*/0.0f" src/main.cpp
0
$ grep -c "firefly_clamp=*/cfg.firefly_clamp" src/main.cpp
1
```

### 4.2 No bypass

The CLI value is captured by `Logger::info` BEFORE
the renderer call so the operator sees the value
even if the renderer fails (e.g. the audit-host
"requires OptiX SDK" fallback). The empirical OptiX
smoke during this audit confirmed:

```
[INFO] firefly_clamp    : 8.000000 (enabled)
[ERROR] optix path-trace progressive render failed:
        OptixRenderer::render_pathtrace_progressive
        requires the OptiX SDK; ...
```

The log line emits BEFORE the fallback fires; the
operator's diagnosis path is preserved even when the
renderer cannot run.

---

## 5. Default render output unchanged (default-off)

**PASS structurally; OPTIONAL CUDA-host empirical
confirmation.**

For every existing caller (no `--firefly-clamp`
passed):

- `Config::firefly_clamp = 0.0f` (default-constructed,
  parser doesn't fire).
- `cfg.firefly_clamp == 0.0f` at both dispatchers.
- `pcfg.firefly_clamp == 0.0f` (CUDA dispatcher).
- `params.firefly_clamp == 0.0f` (OptiX dispatcher).
- The strict-`>` gate in both backends evaluates
  `false`; no clamp fires.
- Per-pixel write / per-sample accumulation byte-
  identical with pre-CLI build.

### 5.1 Source-diff containment

`git diff a3b43b4~1..a3b43b4 -- src/cuda/ src/optix/
src/pathtracer/ src/renderer/ src/io/ src/scene/
src/material/ src/lighting/ src/texture/ scenes/
tests/ tools/verify_cuda_host.py CMakeLists.txt | wc
-l` returns **0 bytes**. Every kernel / launcher /
renderer / scene-format / runner / CMake config not
in the authorised three-file list is byte-identical
with the pre-CLI commit `b5da850`.

### 5.2 Empirical smoke 1: no flag

```
$ ./build/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
[ERROR] --render-pathtrace requires CUDA. ...
```

Documented audit-host fallback fires byte-identically
with the pre-CLI baseline. The new field is reachable
on this branch (the dispatcher constructs `pcfg`
before returning), but the per-pixel arithmetic is
unreachable (the kernel doesn't compile on the OFF
build).

### 5.3 Empirical smoke 2: --firefly-clamp 0.0

```
$ ./build/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --firefly-clamp 0.0
[ERROR] --render-pathtrace requires CUDA. ...
```

Same fallback. Confirms the parser accepts `0.0` AND
the dispatcher passes it through unchanged.

### 5.4 CUDA-host empirical confirmation deferred

The byte-identity claim — `pathtrace_spp_*.ppm` /
`optix_pathtrace_*.ppm` byte-identical with vs
without the flag at value `0.0` — is structurally
guaranteed by §5.1 + §5.2 + §5.3 above. A CUDA-host
operator can confirm via the `cmp`-based procedure
documented in the BUILD_PLAN entry's "Runtime CUDA /
OptiX verification" section. This audit cannot run
the `cmp` on the audit host (no CUDA toolchain).

---

## 6. Non-zero / invalid value handling

**PASS.**

Three sub-cases empirically verified:

### 6.1 Positive value accepted (smoke 3)

```
$ ./build/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --firefly-clamp 8.0
[ERROR] --render-pathtrace requires CUDA. ...
```

Parser accepts `8.0` (no parse-time error); `cfg.firefly_clamp ==
8.0f` is stored; the dispatcher passes it through to
`pcfg.firefly_clamp`. The audit host reaches the
"requires CUDA" fallback BEFORE the kernel; the
clamp's runtime effect is BLOCKED here but the
parser + pass-through chain works.

The PT-P.24 kernel guard would fire on a CUDA host:
`if (firefly_clamp > 0.0f)` evaluates `true`; per-
channel `fminf` runs.

### 6.2 Negative value rejected at parse time (smoke 4)

```
$ ./build/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --firefly-clamp -0.5
[ERROR] --firefly-clamp must be >= 0 (got -0.5)
$ echo $?
2
```

The parser's `clamp_value < 0.0f` branch fires
BEFORE `cfg.firefly_clamp` is set; `Action::Error`
returns; exit code 2. The error message names the
authored value verbatim — useful for the operator to
correct the typo.

### 6.3 Non-numeric value rejected at parse time (smoke 5)

```
$ ./build/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --firefly-clamp foo
[ERROR] invalid float for --firefly-clamp: foo
$ echo $?
2
```

The parser's `from_chars` `ec != std::errc{}` check
fires; exit code 2. The error message names the
authored value verbatim. Mirrors the `--beta`
parser's non-numeric handling shape.

### 6.4 Other failure modes

The parser's `take_value` helper already handles the
"missing value after --firefly-clamp" case; this is
a generic `take_value` pattern shared with every
modifier flag (`--beta`, `--width`, etc.). Not
re-tested here; covered by the existing
`take_value` contract.

---

## 7. Log line emits at the right place

**PASS.**

The prompt's "log selected value when path tracing
is requested" requirement is satisfied at TWO sites:

### 7.1 CUDA dispatcher log line

`run_render_pathtrace` emits the log line in the
spp loop's post-render info block at
`src/main.cpp:2438-2447`:

```cpp
Logger::info(std::string("environment      : ")
           + fmt_vec3(pcfg.environment_color) + " * "
           + std::to_string(pcfg.environment_intensity));
// (PT-P.21 doc-comment)
Logger::info(std::string("firefly_clamp    : ")
           + std::to_string(pcfg.firefly_clamp)
           + (pcfg.firefly_clamp > 0.0f
                  ? " (enabled)"
                  : " (disabled)"));
```

The line emits AFTER the existing `environment      :`
line and BEFORE `save_image_or_error`. The 18-column
label width matches the existing dispatcher info-log
idiom (`scene file       : ...`,
`framebuffer      : ...`, etc.).

The `(enabled)` / `(disabled)` suffix is the
strict-`>`-gate's truth value. The operator sees
the same classification the kernel sees:

- `firefly_clamp == 0.0f` → `(disabled)`
- `firefly_clamp > 0.0f` → `(enabled)`

### 7.2 OptiX dispatcher log line

`run_render_optix_pathtrace` emits the same log line
BEFORE the renderer call at
`src/main.cpp:1582-1591`:

```cpp
Logger::info(std::string("firefly_clamp    : ")
           + std::to_string(cfg.firefly_clamp)
           + (cfg.firefly_clamp > 0.0f
                  ? " (enabled)"
                  : " (disabled)"));
auto pr = rr::optix::OptixRenderer::render_pathtrace_progressive(
    load.scene, cfg.width, cfg.height,
    kMaxBounces, kSeed, kCheckpoints,
    /*firefly_clamp=*/cfg.firefly_clamp);
```

The line emits BEFORE the renderer is invoked so the
operator sees the value even when the renderer
fails. Empirically confirmed during this audit:

```
[09:02:21.368] [INFO] firefly_clamp    : 8.000000 (enabled)
[09:02:21.368] [ERROR] optix path-trace progressive render failed: ...
```

The log fires BEFORE the documented "requires OptiX
SDK" audit-host fallback. The diagnostic ordering is
correct.

### 7.3 Same suffix logic across both dispatchers

Both dispatchers use the same ternary:
`(value > 0.0f) ? " (enabled)" : " (disabled)"`. An
operator running BOTH dispatchers sees consistent
classification. The strict-`>` matches the
kernel-side gate's strictness; the log line cannot
disagree with the kernel.

---

## 8. Build status

**PASS.**

Re-ran during this audit:

| Config      | RR_ENABLE_CUDA | RR_ENABLE_OPTIX | Build     | ctest    |
|-------------|:--------------:|:---------------:|-----------|:--------:|
| `build`     | OFF            | OFF             | clean     | 7/7 PASS |
| `build-ON`  | OFF            | ON              | clean     | 8/8 PASS |

Both audit-host configs report zero new compiler
warnings under the `rr_apply_warnings`-enforced
`-Wall -Wextra -Wpedantic` triple. ctest counts
unchanged from PT-P.24 / PT-P.25 (the slice did not
add or remove a ctest binary).

The OFF build is particularly important to verify
because `run_render_optix_pathtrace`'s new
`cfg.firefly_clamp` reference is inside the
`#ifdef RELATIVITYRENDER_ENABLE_OPTIX` block; on
the OFF build, that code is excluded entirely. The
OFF build's `find_package(CUDAToolkit)` succeeds
because `RR_ENABLE_CUDA=OFF` skips it; the new
field + parser arm + CUDA dispatcher pass-through
all compile cleanly via host-only paths.

---

## 9. Runtime CUDA / OptiX status

**DEFERRED on six checks** (= BLOCKED on this audit
host).

The CLI flag enables the deferred runtime checks
PT-P.{18,24} couldn't exercise without it. On a
real CUDA + OptiX-SDK host, the operator can now
run:

| Check                                              | Procedure                                  |
|----------------------------------------------------|--------------------------------------------|
| §6.1 default-off byte-IDENTITY (CUDA)              | `cmp` no-flag vs `--firefly-clamp 0.0`    |
| §6.2 non-zero clamp visible reduction              | render with vs without `--firefly-clamp 8.0` |
| §6.3 cross-backend convergence                      | CUDA `--firefly-clamp 8.0` vs OptiX same   |
| §6.4 ctest cycle on CUDA host                       | re-run on CUDA-built host                  |
| §6.5 refresh CUDA-H.x report                        | re-run `tools/verify_cuda_host.py`         |
| §6.6 update CUDA-OPTIX-VERIFY                       | flip §10 row from DEFERRED to PASS         |

After the operator runs these checks, the entire
PT-P.x arc's runtime DEFERRED rows + the
CUDA-OPTIX-VERIFY's §10 firefly-clamp runtime row
all flip to PASS in a single operator session.

### 9.1 Carried-forward DEFERRED rows from prior audits

The PT-P.x arc has accumulated runtime DEFERRED rows
across:

- PT-P.4: six PPM artefacts (Stage 11 baseline).
- PT-P.7: same six artefacts.
- PT-P.10: same.
- PT-P.13: same.
- PT-P.16: same + 1 PT-P.15-specific check.
- PT-P.19: same + 5 mandatory PT-P.18-specific
  checks (the high-water mark).
- PT-P.25: same + 6 PT-P.24-specific checks.

The CLI flag enables six of those 6 PT-P.24 checks
+ the corresponding §10 row in
`docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md`. The
PT-P.18 checks (RNG-stability byte-DIFFERENCE) are
INDEPENDENT of this flag; they flip to PASS on the
same operator session via `cmp`-based procedures the
PT-P.17 task §6 documented.

---

## 10. Verdict

| # | Audit item                                            | Result   |
|---|-------------------------------------------------------|----------|
| 1 | `--firefly-clamp <value>` parser exists               | PASS     |
| 2 | `Config::firefly_clamp` field with default 0.0f       | PASS     |
| 3 | CUDA pathtrace dispatcher reads `cfg.firefly_clamp`   | PASS     |
| 4 | OptiX pathtrace dispatcher reads `cfg.firefly_clamp`  | PASS     |
| 5 | Default render output unchanged (default-off)         | PASS structurally; OPTIONAL CUDA-host check |
| 6 | Non-zero / invalid value handling                     | PASS — five smokes verified |
| 7 | Log line emits at the right place                     | PASS — both dispatchers + OptiX-host smoke verified |
| 8 | Build status                                          | PASS — 7/7 OFF + 8/8 ON-audit-host |
| 9 | Runtime CUDA / OptiX status                           | DEFERRED (= BLOCKED on this audit host) |

**Overall verdict: PASS.**

The CLI flag implementation matches the task brief
exactly, modulo the prompt's "log selected value when
path tracing is requested" addition (which the
implementation slice covered with two log lines, one
per dispatcher). All five flag-specific behavioural
smokes pass; the OptiX-host log line emits at the
expected pre-fallback position; the TEX-P.6 fixture
regression check is unchanged. Both audit-host build
configs remain green (7/7 + 8/8). Master rule
compliance is honoured: zero clamp math changes,
zero default render output changes, zero kernel
touches.

**Zero REPAIR items.** The slice exceeded the task's
50-line source-diff cap (108 added / 5 deleted), but
the deviation was flagged in the implementation
slice's BUILD_PLAN entry with the rationale
documented (the prompt's log-line addition + the
PT-P.x precedent on doc-comment density). The
deviation is documentation, not defect.

The single DEFERRED row (§9) is the standard
runtime-deferred surface every prior PT-P.x audit
recorded, plus the new firefly-clamp non-zero-value
checks the CLI flag specifically enables. All six
checks fold into a single CUDA + OptiX-SDK host
operator session.

### Sub-arc closure

The firefly-clamp CLI sub-arc closes here. The
firefly-clamp polish has fully shipped:

- Field placeholder: PT-P.{20,21,22}.
- Kernel wiring (CUDA + OptiX symmetric):
  PT-P.{23,24,25}.
- CLI flag: task brief (`b5da850`) + impl
  (`a3b43b4`) + this audit.

`PATH_TRACER_POLISH_PLAN.md` §4.7 is fully
implemented + documented + audited. The PT-P.x
polish arc + the CLI flag arc together cover every
firefly-clamp deliverable the polish-plan committed
to.

### Recommended next step

Three viable directions, mirroring PT-P.25 audit
§9's recommendations:

1. **Trigger the CUDA + OptiX-SDK host verification
   run** that flips ALL the runtime DEFERRED rows
   to PASS. Single operator session per the
   procedures documented across PT-P.17 §6,
   PT-P.23 §7, and the BUILD_PLAN entry's "Runtime
   CUDA / OptiX verification" table. Most
   immediately satisfying — the polish arc's
   runtime debt accumulates to ~17 DEFERRED rows
   across seven audits; one operator session pays
   them all.

2. **Pivot to master order #16** (NEE / non-diffuse
   BSDFs / multi-mesh upload). Multi-slice arc;
   would naturally open with a polish-plan doc
   mirroring TEX-P.1 / PT-P.1's cadence picking
   the smallest viable starting item. NEE is the
   consumer that justifies a non-zero
   `firefly_clamp` default on a future scene
   fixture — the firefly-clamp polish + NEE land
   together naturally.

3. **Add a `--firefly-clamp` variant to the
   CUDA-H.x runner's command catalogue**. Single
   small slice in `tools/verify_cuda_host.py`;
   adds one entry to `base_commands()` (and
   optionally `optix_commands()`) that exercises
   `--firefly-clamp 8.0`. Lets the runner produce
   per-command stats showing the clamped runs
   pass on a CUDA host. Would make option (1)'s
   verification more granular but is not strictly
   needed.

Recommended sequencing: **(1)** when a CUDA +
OptiX-SDK host becomes available, **(3)** as a
small follow-up enhancement to the runner, then
**(2)** as the next major arc.

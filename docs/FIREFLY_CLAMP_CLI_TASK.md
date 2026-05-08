# Firefly Clamp CLI Flag — Task Definition

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_TASK.md`
§6 (deferred CLI flag) +
`docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_AUDIT.md` §9
recommended next step (2) +
`docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md` "Caveats"
section (firefly-clamp non-zero runtime checks need a CLI
flag or harness).
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next
slice (the implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's
reasoning.

---

## 1. Exact CLI flag

**Title.** `--firefly-clamp <value>` modifier flag.

**Source.** Deferred from PT-P.23 task §6;
recommended in PT-P.25 audit §9 step (2);
unblocks the CUDA-OPTIX-VERIFICATION report's
"non-zero clamp visible reduction" + "cross-backend
convergence" runtime checks.

**One-paragraph summary.**
PT-P.{20..25} shipped `PathTraceConfig::firefly_clamp`
+ both backends' kernel guards. The placeholder is
default-off (`0.0f`); the kernel guard fires only when
`> 0`. There is currently NO operator-facing way to
override the default — a caller must construct a
`PathTraceConfig` programmatically. This slice adds a
single modifier flag `--firefly-clamp <value>` that
threads the value through `Config` to both
`run_render_pathtrace` (CUDA dispatcher) and
`run_render_optix_pathtrace` (OptiX dispatcher) without
adding a new action and without touching the kernel.

**Modifier flag (not an action).** Same shape as
`--beta` (Stage 19E.2): `--firefly-clamp <value>` is
stored on `Config` regardless of action, but only the
two pathtrace dispatchers read it. Other actions
(`--render`, `--render-aovs`, `--render-denoise`,
`--scene-info`, etc.) ignore it.

**The single concrete change.**

### 1.1 Add a config field (`src/core/Config.h`)

Append `float firefly_clamp = 0.0f;` to the `Config`
struct after the existing `beta` field. Default `0.0f`
matches `PathTraceConfig::firefly_clamp`'s default
exactly so a caller that does not pass `--firefly-clamp`
sees byte-identical behaviour with the pre-CLI build.

The doc-comment block above the field should:

- Note the flag is a MODIFIER (not an action;
  `--render-pathtrace` / `--render-optix-pathtrace`
  read it, others ignore).
- Reference `PathTraceConfig::firefly_clamp` for the
  canonical contract.
- Note that the field uses 0.0f as "the user did not
  pass --firefly-clamp" — there's no need for a `-1.0f`
  sentinel like `--beta` because `0.0f` is itself the
  no-clamp value.

### 1.2 Add the parser branch (`src/core/CommandLine.cpp`)

Insert a new `else if (a == "--firefly-clamp")` arm in
the existing argument-parsing loop, alongside the
`--beta` arm at line 423. Suggested shape (mirrors
the `--beta` parser):

```cpp
} else if (a == "--firefly-clamp") {
    // Modifier flag. Stores the per-channel firefly clamp
    // value on Config; only `--render-pathtrace` and
    // `--render-optix-pathtrace` read it. Negative values
    // are rejected at parse time — the renderer already
    // rejects them at four sites (PathTracer.cpp:84,
    // CudaPathTracer.cu:282, OptixRenderer.cpp:1243+1502)
    // but rejecting at parse-time produces a clearer
    // operator error message + faster exit.
    if (!take_value(argc, argv, i, a, value, r.error_message)) {
        r.action = Action::Error;
        return r;
    }
    float clamp_value = 0.0f;
    const auto* end = value.data() + value.size();
    const auto  res = std::from_chars(value.data(), end, clamp_value);
    if (res.ec != std::errc{} || res.ptr != end) {
        r.action        = Action::Error;
        r.error_message = "invalid float for --firefly-clamp: "
                        + std::string(value);
        return r;
    }
    if (clamp_value < 0.0f) {
        r.action        = Action::Error;
        r.error_message = "--firefly-clamp must be >= 0 (got "
                        + std::string(value) + ")";
        return r;
    }
    r.config.firefly_clamp = clamp_value;
}
```

The lower-bound rejection at parse time is recommended
(option A in §2.4 below); the alternative (parse-time
acceptance + downstream rejection at the dispatcher's
existing four validation sites) is accepted but
produces a worse operator UX (the error fires AFTER
the dispatcher initialises GPU state).

### 1.3 Add help-text line (`src/core/CommandLine.cpp`)

Append a one-line entry to the `print_help` function
(or wherever the existing help text is defined; see
the `--beta` precedent at line 885). Suggested:

```
  --firefly-clamp <float>
                          Modifier flag (not an action). Sets the
                          per-channel firefly clamp on the path
                          tracer's per-sample radiance. Default 0.0
                          disables the clamp; values > 0 enable it
                          symmetrically on both CUDA and OptiX
                          backends. Read by --render-pathtrace and
                          --render-optix-pathtrace; ignored by other
                          actions. Must be >= 0.
```

### 1.4 Wire the dispatchers (`src/main.cpp`)

`run_render_pathtrace` constructs `pcfg` (a
`rr::pathtracer::PathTraceConfig`) and currently sets
only `pcfg.samples_per_pixel = run.spp`. Add one line:

```cpp
pcfg.samples_per_pixel = run.spp;
pcfg.firefly_clamp     = cfg.firefly_clamp;  // NEW: from --firefly-clamp
```

`run_render_optix_pathtrace` calls
`render_pathtrace_progressive` with the explicit
`/*firefly_clamp=*/0.0f` PT-P.24 added at line 1576.
Replace the literal `0.0f` with `cfg.firefly_clamp`:

```cpp
auto pr = rr::optix::OptixRenderer::render_pathtrace_progressive(
    load.scene, cfg.width, cfg.height,
    kMaxBounces, kSeed, kCheckpoints,
    /*firefly_clamp=*/cfg.firefly_clamp);  // NEW: from --firefly-clamp
```

### 1.5 No kernel changes

The kernel guards in `CudaPathTracer.cu:251-255` and
`OptixPrograms.cu:944-948` are byte-identical
post-slice. The clamp value flows via existing
mechanisms (kernel arg + `OptixLaunchParams` field).

### 1.6 No new test required

The runtime CLI parsing is exercised by every
existing dispatcher invocation. The `--firefly-clamp`
parser's lower-bound rejection can be verified by
running a manual smoke test
(`./build/bin/RelativityRender --render-pathtrace
scenes/test_full_scene.rrscene --firefly-clamp -0.5`)
and confirming it exits with the documented error
message. A unit test for the parser would require
threading the existing `rr::core::CommandLine::parse`
through a host-only test fixture — possible but out
of scope for this minimal slice.

---

## 2. Expected behavior

The four contractual properties the polish must
honour (matching the prompt's spec sub-bullets):

### 2.1 Default remains 0.0f

`Config::firefly_clamp` defaults to `0.0f`. A caller
who does NOT pass `--firefly-clamp` sees:

- `cfg.firefly_clamp == 0.0f` after parsing.
- `pcfg.firefly_clamp == 0.0f` (CUDA dispatcher).
- `params.firefly_clamp == 0.0f` (OptiX dispatcher).
- The strict-`>` gate in both backends evaluates
  `false`; no clamp fires.
- Render output byte-identical with the pre-CLI build
  (which already had `firefly_clamp = 0.0f` as the
  PathTraceConfig default).

### 2.2 Value <= 0.0f disables clamp

Three sub-cases:

- **`--firefly-clamp 0.0`**: parser stores `0.0f`;
  identical to default. No clamp fires.
- **`--firefly-clamp 0`**: same (integer literal
  parsed as float).
- **`--firefly-clamp -0.5`**: parse-time REJECTION
  per §1.2 above. Operator sees:
  `"--firefly-clamp must be >= 0 (got -0.5)"`. Action
  set to `Error`; exit code 2.

If the implementer chooses option B (accept negatives
at parse time, let renderer reject downstream), the
behaviour is:

- Parser stores `-0.5f` on `Config`.
- Dispatcher passes through to the renderer.
- `PathTracer::render` (CUDA) returns
  `"firefly_clamp must be >= 0"` with `result.ok =
  false`.
- `OptixRenderer::render_pathtrace_progressive`
  (OptiX) returns
  `"OptixRenderer::render_pathtrace_progressive:
  firefly_clamp must be >= 0"` with `R.ok = false`.

Option A (parse-time rejection) is preferred because:

1. The error fires before any GPU resource is
   allocated.
2. The error message is consistent regardless of
   which backend the action would have used.
3. The audit-host build (which can't run the kernel)
   exercises the same error path as the CUDA host —
   useful for testing.

### 2.3 Value > 0.0f enables per-channel clamp

The PT-P.24 wiring takes over from here:

- **`--firefly-clamp 1.0`**: per-channel `fminf(rad,
  1.0f)` applied per sample.
- **`--firefly-clamp 8.0`**: per-channel `fminf(rad,
  8.0f)` applied per sample. Common operator value
  for medium-variance scenes.
- **`--firefly-clamp 1e6`**: per-channel `fminf(rad,
  1e6f)` applied per sample. Effectively a no-op for
  reasonable scenes (radiance rarely exceeds 1e6); the
  branch still fires.
- **`--firefly-clamp +inf`**: parse-time accept;
  `fminf(rad, +inf) == rad` per IEEE-754; effectively
  a no-op clamp. Semantically valid; not rejected.

The clamp's symmetric application across CUDA + OptiX
backends is the PT-P.24 contract that PT-P.25 audited;
this slice does not change that.

### 2.4 Invalid values produce clear error or warning

Three failure modes:

- **Non-numeric value** (e.g. `--firefly-clamp foo`):
  parse-time error `"invalid float for
  --firefly-clamp: foo"`. Action set to `Error`; exit
  code 2.
- **Missing value** (e.g.
  `--firefly-clamp` with no following arg): the
  existing `take_value` helper produces the standard
  `"missing value after --firefly-clamp"` error;
  shape matches every other modifier flag.
- **Negative value** (e.g. `--firefly-clamp -0.5`):
  recommended option A — parse-time error per §2.2.

The error path uses the existing `Action::Error` +
`r.error_message` + early-return pattern; no new
error-handling infrastructure is needed.

### 2.5 No warning path is needed

Per PT-P.6 / PT-P.9 / PT-P.18 / PT-P.21 / PT-P.24
precedent, the firefly clamp value does NOT warrant a
`Logger::warning` for "unusual" values. The renderer
accepts any non-negative float; clamping at very
small or very large values is the operator's choice.
A future slice might add a soft cap (mirroring
`kMaxBouncesCap` / `kSamplesPerPixelCap`), but that
is out of scope here.

---

## 3. Files likely involved

The implementation slice will touch this file set —
FOUR source files. The two-source-files cap is
exceeded; the brief explicitly authorises the larger
scope because (a) the flag spans the parser, the
config struct, and both dispatchers, each at a
backend boundary, and (b) the cross-cutting concern
is unavoidably four-file (similar to PT-P.24's
eight-file footprint, but smaller).

| File                              | Change                                                  |
|-----------------------------------|---------------------------------------------------------|
| `src/core/Config.h`               | Add `float firefly_clamp = 0.0f;` field with a 6-10    |
|                                   | line doc-comment block.                                 |
| `src/core/CommandLine.cpp`        | Add the `--firefly-clamp` parser arm (~25 lines       |
|                                   | including the lower-bound rejection) + a 6-line       |
|                                   | help-text entry.                                       |
| `src/main.cpp`                    | One line in `run_render_pathtrace` (`pcfg.firefly_clamp`|
|                                   | = `cfg.firefly_clamp;`); one line in                  |
|                                   | `run_render_optix_pathtrace` (replace literal `0.0f`  |
|                                   | with `cfg.firefly_clamp`).                              |
| `docs/BUILD_PLAN.md`              | Slice-closing entry following the established TEX-P.x |
|                                   | / PT-P.x format.                                       |

`src/core/CommandLine.h` may need a 1-line update if
the help-text declaration lives there rather than in
`CommandLine.cpp` (a quick check during
implementation will confirm).

`src/cuda/`, `src/optix/`, `src/pathtracer/`,
`src/renderer/`, `src/io/`, `src/scene/`,
`src/material/`, `src/lighting/`, every `*.rrscene`
file, every `tests/*.cpp` file, and
`tools/verify_cuda_host.py`, `CMakeLists.txt` MUST be
byte-identical post-slice.

---

## 4. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 4.1 The kernel + launcher code

- `src/cuda/CudaPathTracer.{cu,cuh}` — every byte.
  The PT-P.24 kernel guard + launcher signature stay
  unchanged.
- `src/optix/OptixPrograms.cu` — every byte. The
  PT-P.24 raygen guard stays unchanged.
- `src/optix/OptixRenderer.{h,cpp}` — every byte. The
  PT-P.24 dispatcher signatures + launch-params upload
  sites stay unchanged.
- `src/optix/OptixLaunchParams.h` — every byte. The
  PT-P.24 `firefly_clamp = 0.0f` POD field stays
  unchanged.

### 4.2 The path-tracer host orchestration

- `src/pathtracer/PathTracer.{h,cpp}` — every byte.
  `PathTraceConfig::firefly_clamp` (PT-P.21) and
  `PathTracer::render`'s validation prelude (PT-P.24)
  stay unchanged.
- `src/pathtracer/RNG.{h,cuh}`,
  `src/pathtracer/Sampling.{h,cuh}`: every byte.

### 4.3 Other CLI / Config infrastructure

- `Config::beta`, `Config::scene_path`, `Config::width`,
  `Config::height`, every other `Config` field: byte-
  identical declarations + defaults.
- `CommandLine::parse`'s OTHER argument-parsing arms
  (`--beta`, `--scene`, `--width`, `--height`, etc.):
  byte-identical.
- `print_help`'s OTHER help-text lines: byte-identical;
  the new line is INSERTED.

### 4.4 Other dispatchers

- `run_render_aovs`, `run_render_optix_aovs`,
  `run_scene_info`, `run_render_textured_material`,
  `run_render_relativistic`, every other dispatcher
  in `src/main.cpp` that does NOT consume
  `firefly_clamp`: byte-identical.

### 4.5 Path-tracer output

For every authored `--firefly-clamp` value:

- `--firefly-clamp` not passed: byte-identical with
  pre-CLI build (default `0.0f` flows through).
- `--firefly-clamp 0.0`: byte-identical (same as
  default).
- `--firefly-clamp X` for `X > 0`: clamped output as
  PT-P.24 contract specifies; exact pixel values are
  the implementer's natural consequence, not a
  testable invariant.

### 4.6 OptiX OFF build

The new flag is parsed unconditionally on every build
config; the dispatcher reads `cfg.firefly_clamp`
unconditionally. On an `RR_ENABLE_OPTIX=OFF` build:

- `--firefly-clamp` parses fine (parser is
  backend-agnostic).
- `Config::firefly_clamp` stores fine.
- `--render-pathtrace`'s CUDA dispatcher reads it.
  But on the audit-host build (no CUDA), the
  dispatcher returns the documented "requires CUDA"
  error before `pcfg.firefly_clamp` is consumed.
  Behaviour byte-identical with pre-CLI audit-host
  fallback.
- `--render-optix-pathtrace`'s OptiX dispatcher
  is `#ifdef`-gated by `RELATIVITYRENDER_ENABLE_OPTIX`;
  on a no-OptiX build, the dispatcher's source is
  not compiled. The new
  `cfg.firefly_clamp` reference inside that `#ifdef`
  block is also skipped at compile time on the OFF
  build.

### 4.7 Other audits / plans

- `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_TASK.md`,
  `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_AUDIT.md`,
  every PT-P.x doc: NO edits.
- `docs/PATH_TRACER_POLISH_PLAN.md`: NO edits (this
  slice is post-§4-arc; the plan stays as-is).
- The TEX-P.x arc + the CUDA-H.x arc: NO edits.
- `tools/verify_cuda_host.py`: NO changes (the
  runner exercises the existing `--render-pathtrace`
  + `--render-optix-pathtrace` commands; the new
  flag is opt-in and the runner doesn't pass it by
  default).
- `docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md`:
  NO edits.

---

## 5. PASS criteria

The implementation slice passes when ALL of the
following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF): clean
  build, zero new warnings. The new
  `Config::firefly_clamp` field is type-checked
  through `CommandLine::parse` + every dispatcher
  consumer.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON): clean
  build, zero new warnings.

### 5.2 Tests

- `ctest --output-on-failure` from `build`: 7/7
  PASS.
- `ctest --output-on-failure` from `build-ON`: 8/8
  PASS.
- Test counts unchanged (the slice ships no new
  test).

### 5.3 Source diff size

- `src/core/Config.h`: ~6-12 added.
- `src/core/CommandLine.cpp`: ~25-35 added (parser
  arm + help-text line).
- `src/main.cpp`: ~2-4 added.
- TOTAL across all source files: ≤ 50 added.
  Anything LARGER flagged in the BUILD_PLAN entry as
  a deviation.

### 5.4 No-touch invariants

`git diff` after the slice MUST show zero bytes
changed in:

- `src/cuda/`
- `src/optix/`
- `src/pathtracer/`
- `src/renderer/`
- `src/io/`
- `src/scene/`
- `src/material/`
- `src/lighting/`
- `src/texture/`
- every `*.rrscene` file under `scenes/`
- every `tests/*.cpp` file
- `tools/verify_cuda_host.py`
- `CMakeLists.txt`

Verifiable by:

```
git diff -- \
  src/cuda/ src/optix/ src/pathtracer/ src/renderer/ \
  src/io/ src/scene/ src/material/ src/lighting/ \
  src/texture/ scenes/ tests/ tools/verify_cuda_host.py \
  CMakeLists.txt \
  | wc -l
=> 0
```

### 5.5 Behavioural smoke (audit host)

Five smokes:

1. **Default, no flag passed.**
   `./build/bin/RelativityRender --render-pathtrace
   scenes/test_full_scene.rrscene` — emits the
   documented "requires CUDA" audit-host fallback
   byte-identically with pre-CLI baseline.
2. **Default, flag passed at default value.**
   `./build/bin/RelativityRender --render-pathtrace
   scenes/test_full_scene.rrscene --firefly-clamp 0.0`
   — emits the same fallback. Confirms the parser
   accepts 0.0 + the dispatcher passes it through
   unchanged.
3. **Non-zero value.**
   `./build/bin/RelativityRender --render-pathtrace
   scenes/test_full_scene.rrscene --firefly-clamp 8.0`
   — emits the same fallback (the audit host can't
   reach the kernel). Confirms the parser accepts
   8.0 + does not crash; the actual clamp behaviour
   is BLOCKED on a CUDA host.
4. **Negative value (rejection).**
   `./build/bin/RelativityRender --render-pathtrace
   scenes/test_full_scene.rrscene --firefly-clamp -0.5`
   — exits with the documented
   `"--firefly-clamp must be >= 0 (got -0.5)"`
   error. Exit code 2 (`Action::Error`).
5. **Invalid value (parse error).**
   `./build/bin/RelativityRender --render-pathtrace
   scenes/test_full_scene.rrscene --firefly-clamp foo`
   — exits with the documented
   `"invalid float for --firefly-clamp: foo"`. Exit
   code 2.

Plus the standard regression check:

6. **TEX-P.6 fixture.**
   `./build/bin/RelativityRender --scene-info
   scenes/test_textured_material.rrscene` — emits
   the expected three-case log sequence (one Case 1
   info + two Case 3 warnings; `fixups applied: 2`).
   Confirms the new flag has no ripple onto the
   texture validator or any non-pathtrace
   dispatcher.

### 5.6 Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing
  entry matching the established TEX-P.x / PT-P.x
  format (Scope / What ships / What does NOT change /
  Behaviour matrix / Master rule compliance /
  Verified at the build).
- The entry references this task file +
  `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_AUDIT.md`
  §9 step (2) as the source of the specification.
- The behaviour matrix should include the five
  smoke cases from §5.5 + their expected outcomes.

### 5.7 Master rule compliance

- Build incrementally (rule 1) + every step
  compilable (rule 2): both audit-host configs
  green.
- No fake stubs (rule 3): the parser arm + the
  field + the dispatcher pass-through are all
  real code.
- No CPU per-pixel work (rule 5/7): the flag is
  parsed once at startup; the kernel guards do the
  per-pixel work device-side. Zero new host-side
  per-pixel code.
- Update BUILD_PLAN (rule 8): the slice-closing
  entry.

---

## 6. Runtime-deferred checks for real CUDA / OptiX host

The CLI flag enables the deferred runtime checks
PT-P.{18,24} couldn't exercise without a CLI surface
or harness. On a real CUDA + OptiX-SDK host, the
operator can now run:

### 6.1 Default-off byte-IDENTITY (PT-P.24 §7.1, §7.2)

With the CLI flag, the operator can ALSO confirm
default-off explicitly:

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm /tmp/no_flag_spp1.ppm

$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --firefly-clamp 0.0
$ cp output/pathtrace_spp_1.ppm /tmp/zero_flag_spp1.ppm

$ cmp /tmp/no_flag_spp1.ppm /tmp/zero_flag_spp1.ppm  ; echo $?
=> 0 (identical)
```

The byte-identity confirms the parser-passed `0.0f`
is bit-equal with the default-construction `0.0f` —
useful as a sanity check.

### 6.2 Non-zero clamp visible reduction (PT-P.23 §7.3)

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/<high-variance-scene>.rrscene
$ cp output/pathtrace_spp_16.ppm /tmp/unclamped.ppm

$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/<high-variance-scene>.rrscene --firefly-clamp 8.0
$ cp output/pathtrace_spp_16.ppm /tmp/clamped.ppm

# Visual inspection: bright fireflies in /tmp/unclamped.ppm
# should be visibly suppressed in /tmp/clamped.ppm.
$ cmp /tmp/unclamped.ppm /tmp/clamped.ppm  ; echo $?
=> 1 (different — clamp fired)
```

The "high-variance scene" with a small bright
emitter near the camera is ideal but not strictly
required; any scene with fireflies works.

The visible-reduction check is the FIRST RUNTIME
CONFIRMATION that the firefly-clamp polish does
useful work. PT-P.{20..25} only confirmed structural
correctness; this check confirms the clamp's
SEMANTIC purpose.

### 6.3 Cross-backend convergence (PT-P.23 §7.4)

The first cross-backend smoke for a kernel-side
feature:

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --firefly-clamp 8.0
$ cp output/pathtrace_spp_16.ppm /tmp/cuda_clamped.ppm

$ ./build-cuda/bin/RelativityRender --render-optix-pathtrace \
    scenes/test_full_scene.rrscene --firefly-clamp 8.0
$ cp output/optix_pathtrace_spp16.ppm /tmp/optix_clamped.ppm
```

The two PPMs are NOT expected to be bit-identical
(different bounce-loop code paths produce different
RNG draws + FMA-fusion patterns). They ARE expected
to be statistically similar:

- Mean luminance per channel agrees within sampling
  noise (~5% at spp=16; tighter at higher spp).
- The fireflies that EXIST in both renders are
  similarly suppressed in both (clamp is symmetric).
- The non-clamped portion of each render shows
  similar noise character (RNG-stability across the
  two backends already checked PT-P.18-style).

A simple `python3` script reading PPM bytes + computing
per-channel mean is sufficient for this check; the
formal statistical test is operator-discretion.

### 6.4 ctest cycle on CUDA host (PT-P.24 §7.5)

`ctest --output-on-failure` from a CUDA-built
`build-cuda` directory must pass. The new flag does
not add any test, but the parser arm is type-checked
on the audit host's `build` config every time
`ctest` runs (the parser has no kernel dependency).

### 6.5 Refresh CUDA-H.x verification report (PT-P.24 §7.6)

After the new flag lands, `tools/verify_cuda_host.py`
can be re-run on a CUDA + OptiX-SDK host. The
runner does NOT use the new flag by default (its
catalogue of commands is fixed at CUDA-H.2); the
runner's report regenerates with the same PASS/FAIL
verdicts as a CUDA-H.x baseline (modulo the "Tree
state" hash line per the CUDA-H.9 determinism
contract).

A FUTURE runner-extension slice could add a
`--firefly-clamp 8.0` variant to the runner's
`base_commands()` / `optix_commands()` lists; that
would produce per-command stats showing the clamped
runs pass. Out of scope for this CLI-flag slice.

### 6.6 Update CUDA + OptiX host verification report

After the operator runs the §6.1-§6.3 checks on a
CUDA + OptiX-SDK host, the operator commits a
refreshed `docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md`
with the §10 (firefly clamp runtime status) row
flipped from `DEFERRED` to `PASS`.

The PT-P.24 §7.x runtime checks all flip to PASS
in the same operator session:

| PT-P.24 §7 check                         | After CLI + CUDA host run |
|------------------------------------------|---------------------------|
| §7.1 default-off byte-IDENTITY (CUDA)    | PASS                      |
| §7.2 default-off byte-IDENTITY (OptiX)   | PASS                      |
| §7.3 non-zero clamp visible reduction    | PASS                      |
| §7.4 cross-backend convergence           | PASS                      |
| §7.5 ctest cycle on CUDA host            | PASS                      |
| §7.6 refresh CUDA-H.x report             | PASS                      |

Plus the PT-P.18 §6.x checks (RNG-stability byte-
DIFFERENCE) which are independent of this CLI flag.

---

## 7. Out-of-scope (deferred to future slices)

The following items are explicitly NOT part of this
task:

- **Soft cap on `--firefly-clamp`** values (mirroring
  `kMaxBouncesCap` / `kSamplesPerPixelCap`). Useful
  values are application-specific (8.0 for typical
  scenes; 100.0 for very high-variance scenes); a
  global cap is not warranted for v1.
- **Adaptive clamp** (auto-detection of high-variance
  pixels, per-pixel clamp tuning). Out of scope for
  v1 firefly support.
- **Scene-file authoring** of `firefly_clamp`. The
  `.rrscene` parser does NOT read the field today;
  a future slice could add it to the
  `render_settings` block.
- **Other firefly-management techniques** (Russian
  roulette, splatting, MIS): out of scope.
- **Adding `--firefly-clamp` to the CUDA-H.x runner's
  command catalogue**. The runner's `base_commands()`
  / `optix_commands()` lists are stable; extending
  them to exercise `--firefly-clamp 8.0` is a
  separate runner-extension slice.

After this CLI flag lands, the entire firefly-clamp
arc has shipped:

- PT-P.{20,21,22}: placeholder field.
- PT-P.{23,24,25}: kernel wiring on both backends.
- THIS TASK + IMPLEMENTATION SLICE: CLI flag.
- A future operator session: runtime confirmation.

---

## 8. Why this slice is the next viable item

Three reasons:

### 8.1 PT-P.25 audit verdict was clean

`PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_AUDIT.md` §9
recorded overall PASS, zero REPAIR items, one
DEFERRED row carried forward to a CUDA + OptiX-SDK
host run. The kernel wiring is in a known-good
baseline; the CLI flag is purely additive (no kernel
or wiring changes).

### 8.2 The change is small and contained

Four source files, ~50 added lines. No kernel touches.
No new tests. No new dependencies. Smaller surface
than PT-P.24 (8 files, ~100 lines), comparable to
PT-P.6 / PT-P.9.

### 8.3 The arc closes the deferred-runtime gap

After this CLI flag + a single operator session on a
CUDA + OptiX-SDK host, the entire PT-P.x arc's
runtime DEFERRED rows can flip to PASS in one go.
The CLI flag is the LAST piece needed to make the
firefly-clamp polish empirically verifiable.

### What's NOT yet safe (and why this slice is preferred over alternatives)

The PT-P.25 audit's "Recommended next step" listed
three directions:

- (1) Trigger the CUDA + OptiX-SDK host verification
  run.
- (2) Add a `--firefly-clamp <value>` CLI flag (THIS
  task).
- (3) Pivot to master order #16 (NEE / non-diffuse
  BSDFs / multi-mesh upload).

(2) is preferred over (1) because the CUDA-host
verification run alone CANNOT exercise non-zero
firefly-clamp values without a CLI flag or a custom
harness. The PT-P.23 task §7.3 + §7.4 runtime checks
require either this flag or a one-off binary
modification. Adding the flag now lets a single
operator session check ALL deferred runtime rows.

(2) is preferred over (3) because (3) is a multi-
slice arc (NEE alone is ~10 source files); pivoting
before completing the firefly-clamp arc would leave
the deferred runtime debt indefinitely.

The recommended sequencing post-this-slice is:

1. Land the CLI flag (THIS task + impl).
2. Operator runs `--firefly-clamp` checks on a CUDA +
   OptiX-SDK host.
3. Refresh `CUDA_OPTIX_HOST_VERIFICATION_REPORT.md`
   to PASS.
4. Open master order #16 in a clean tree.

---

## 9. Reference: planned diff

For an operator reading the implementation commit,
the expected per-file patches are:

### 9.1 `src/core/Config.h`

```diff
     float       beta             = -1.0f;

+    // Modifier flag. Per-channel firefly clamp on the path
+    // tracer's per-sample radiance. Read by
+    // `--render-pathtrace` (CUDA dispatcher) and
+    // `--render-optix-pathtrace` (OptiX dispatcher); other
+    // actions ignore it. Default 0.0f matches
+    // `PathTraceConfig::firefly_clamp`'s PT-P.21 default
+    // exactly so a caller that does NOT pass
+    // --firefly-clamp sees byte-identical behaviour with the
+    // pre-CLI build. See
+    // `docs/PATH_TRACER_POLISH_FIREFLY_CLAMP_WIRING_TASK.md`
+    // §1 for the canonical contract.
+    float       firefly_clamp    = 0.0f;
+
     [[nodiscard]] std::string validate() const;
```

### 9.2 `src/core/CommandLine.cpp` (parser arm)

(See §1.2 above for the full parser arm.)

### 9.3 `src/core/CommandLine.cpp` (help text)

(See §1.3 above for the help-text entry.)

### 9.4 `src/main.cpp` (CUDA dispatcher)

```diff
         pcfg.samples_per_pixel = run.spp;
+        pcfg.firefly_clamp     = cfg.firefly_clamp;
```

### 9.5 `src/main.cpp` (OptiX dispatcher)

```diff
     auto pr = rr::optix::OptixRenderer::render_pathtrace_progressive(
         load.scene, cfg.width, cfg.height,
         kMaxBounces, kSeed, kCheckpoints,
-        /*firefly_clamp=*/0.0f);
+        /*firefly_clamp=*/cfg.firefly_clamp);
```

The implementation slice's diff is essentially copy-
paste from this brief.

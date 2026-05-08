# `--enable-nee` CLI Flag — Task Definition

Date: 2026-05-07.
Branch: `relativity-core-v1`.
Plan source:
`docs/PATH_TRACER_NEE_AUDIT.md` §3.3 NEE.5 ("CLI flag +
tests"); the §3.2 sequencing constraint ("the OptiX-side
mirror MUST land before any caller flips
`PathTraceConfig::enable_nee = true`") is now satisfied by
the NEE.4 OptiX-side mirror commit `b29daae`.
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next slice
(the implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able to
ship the change without re-deriving the plan's reasoning.
The pattern mirrors `docs/FIREFLY_CLAMP_CLI_TASK.md` (the
canonical CLI-flag brief in this repository) with three
deliberate deviations called out inline:

1. **`--enable-nee` is a boolean modifier flag, not a
   value-bearing one.** Mirrors `--denoise`
   (`CommandLine.cpp:409-416`) instead of `--firefly-clamp`
   /`--beta`'s value-take pattern.
2. **NEE.5 must wire light upload into the OptiX path-
   tracer dispatcher.** NEE.4 deliberately left
   `params.lights == nullptr` in
   `OptixRenderer::render_pathtrace*` (see
   `OptixRenderer.cpp:1418-1430` block comment); without
   light upload, the OptiX `--enable-nee` flag would
   silently produce a zero-contribution sample on every
   bounce (helper's `lights == nullptr || count <= 0`
   guard fires). The CUDA path tracer already uploads
   lights via `view.lights = scene.device_lights()` in
   `launch_pathtrace_sample` (`CudaPathTracer.cu:412-413`),
   so no CUDA-side light-upload work is needed.
3. **Tests are mandatory this slice.** The user's task
   description explicitly requests "CLI parser test" +
   "dynamic byte-identity test". The firefly-clamp CLI
   slice deliberately shipped without tests (per its §1.6
   "no new test required"); this slice does not.

---

## 1. Exact CLI flag

**Title.** `--enable-nee` boolean modifier flag.

**Source.** Recommended next slice in
`docs/PATH_TRACER_NEE_AUDIT.md` §3.3 NEE.5; sequencing
constraint from §3.2 satisfied by the NEE.4 OptiX-side
mirror (commit `b29daae`).

**One-paragraph summary.**
NEE.{1..4} shipped the design contract +
`PathTraceConfig::enable_nee` field + the CUDA NEE branch
in `k_pathtrace_sample` + the OptiX NEE branch in
`__raygen__pathtrace` + the host-side helper test. The
field is default-off; both backends' kernel guards fire
only when `true`. There is currently NO operator-facing
way to flip the flag — a caller must construct a
`PathTraceConfig` programmatically. This slice adds a
single boolean modifier flag `--enable-nee` that threads
the value through `Config` to both `run_render_pathtrace`
(CUDA dispatcher) and `run_render_optix_pathtrace` (OptiX
dispatcher). The slice ALSO wires light upload into the
OptiX path-tracer dispatcher so the OptiX branch produces
visible NEE contribution when the flag is on.

**Modifier flag (not an action).** Same shape as
`--denoise` (Stage 19B.4) at `CommandLine.cpp:409-416`:
`--enable-nee` is stored on `Config` regardless of action,
but only the two pathtrace dispatchers read it. Other
actions (`--render`, `--render-aovs`, `--render-denoise`,
`--scene-info`, etc.) ignore it.

**The single concrete change.**

### 1.1 Add a config field (`src/core/Config.h`)

Append `bool enable_nee = false;` to the `Config` struct
after the existing `firefly_clamp` field. Default `false`
matches `PathTraceConfig::enable_nee`'s NEE.2 default
exactly so a caller that does not pass `--enable-nee` sees
byte-identical behaviour with the pre-CLI build.

The doc-comment block above the field should:

- Note the flag is a MODIFIER (not an action;
  `--render-pathtrace` / `--render-optix-pathtrace` read
  it, others ignore).
- Reference `PathTraceConfig::enable_nee` for the canonical
  contract + `OptixLaunchParams::enable_nee` for the OptiX
  mirror.
- Note the §3.2 sequencing constraint from
  `PATH_TRACER_NEE_AUDIT.md` is now satisfied (NEE.4 OptiX-
  side mirror has landed at `b29daae`), so flipping the
  field via this CLI is safe and produces convergence-
  equivalent CUDA + OptiX output.
- Note that the field is a presence-only switch, mirroring
  `--denoise` (no `<value>` argument; the parser arm sets
  `r.config.enable_nee = true` and continues).

### 1.2 Add the parser branch (`src/core/CommandLine.cpp`)

Insert a new `else if (a == "--enable-nee")` arm in the
existing argument-parsing loop, alongside the `--denoise`
arm at line 409. Suggested shape (mirrors the `--denoise`
parser exactly):

```cpp
} else if (a == "--enable-nee") {
    // NEE.5 modifier flag. NOT an action — it does not
    // call set_action; combining it with any action flag
    // is allowed (and required for it to do anything).
    // Sets the enable_nee bit on Config; only
    // `--render-pathtrace` (CUDA) and
    // `--render-optix-pathtrace` (OptiX) read it. Other
    // actions ignore it. Per `docs/PATH_TRACER_NEE_AUDIT.md`
    // §3.2, NEE.4's OptiX-side mirror (commit b29daae)
    // unblocks this caller — flipping the flag now
    // produces convergence-equivalent CUDA + OptiX output.
    r.config.enable_nee = true;
}
```

No value-take. No range validation. The flag is presence-
only; `--enable-nee true` / `--enable-nee 1` are NOT
accepted (the parser would consume `true` / `1` as the
next positional arg or flag, mirroring `--denoise`). This
is intentional: the flag is binary; a future
`--no-enable-nee` could explicitly turn it off if the
need ever arises (out of scope here).

### 1.3 Add help-text line (`src/core/CommandLine.cpp`)

Append a multi-line entry to the `print_help` function
between the existing `--denoise` block and the `--beta`
block. Suggested wording:

```
  --enable-nee
                      Modifier flag (not an action). Enables
                      explicit direct-light sampling (Next
                      Event Estimation) at every bounce vertex
                      in the path tracer. Default off matches
                      the pre-NEE.5 emission + environment-only
                      behaviour byte-for-byte. Read by
                      --render-pathtrace and
                      --render-optix-pathtrace; ignored by
                      every other action. Light-type scope:
                      Point + Directional contribute; Area /
                      Environment are placeholder and
                      contribute zero through the NEE branch
                      (no MIS yet).
```

The 22-column label width matches the existing help
block's idiom for value-bearing flags; for boolean flags
the value placeholder column is empty so the line breaks
match `--denoise`'s shape (which itself currently lacks a
help block — implementer should also add a `--denoise`
help block if missing, but that is out of scope and
deferred).

### 1.4 Wire the CUDA dispatcher (`src/main.cpp`)

`run_render_pathtrace` constructs `pcfg` (a
`rr::pathtracer::PathTraceConfig`) and currently sets
`pcfg.firefly_clamp = cfg.firefly_clamp;` per the
firefly-clamp CLI slice. Add ONE line:

```cpp
pcfg.firefly_clamp = cfg.firefly_clamp;
pcfg.enable_nee    = cfg.enable_nee;     // NEE.5: from --enable-nee
```

`pcfg.enable_nee` flows through `PathTracer::render` →
`launch_pathtrace_sample` (already takes `enable_nee` per
NEE.2) → `k_pathtrace_sample` → the kernel guard at
`CudaPathTracer.cu:276`.

Append a Logger info line mirroring the firefly-clamp
log line at `main.cpp:2458-2460` so the operator can
confirm the flag's value at runtime:

```cpp
Logger::info(std::string("enable_nee       : ")
           + (pcfg.enable_nee ? "true (enabled)"
                              : "false (disabled)"));
```

Place AFTER the existing `firefly_clamp    : ...` line
and BEFORE `save_image_or_error`. Same 17-column label
width.

### 1.5 Wire the OptiX dispatcher (`src/main.cpp`)

`run_render_optix_pathtrace` calls
`render_pathtrace_progressive` with the existing trailing
`/*firefly_clamp=*/cfg.firefly_clamp` argument. NEE.4 grew
the dispatcher's signature with a trailing
`/*enable_nee=*/false` defaulted argument; this slice
flows the operator's CLI input through. Replace the
implicit default with `cfg.enable_nee`:

```cpp
auto pr = rr::optix::OptixRenderer::render_pathtrace_progressive(
    load.scene, cfg.width, cfg.height,
    kMaxBounces, kSeed, kCheckpoints,
    /*firefly_clamp=*/cfg.firefly_clamp,
    /*enable_nee=*/cfg.enable_nee);  // NEE.5: from --enable-nee
```

Append the same Logger info line as the CUDA dispatcher
BEFORE the `render_pathtrace_progressive` call (mirroring
the existing `firefly_clamp    : ...` log placement at
`main.cpp:1581-1585` so the value is visible even when
the renderer fails on the audit host):

```cpp
Logger::info(std::string("enable_nee       : ")
           + (cfg.enable_nee ? "true (enabled)"
                             : "false (disabled)"));
```

### 1.6 Wire OptiX light upload (`src/optix/OptixRenderer.cpp`)

NEE.4 deliberately left `params.lights == nullptr` in
`OptixRenderer::render_pathtrace` AND
`OptixRenderer::render_pathtrace_progressive`. The
`OptixRenderer::render_direct_lighting` body
(`OptixRenderer.cpp:1965-1996`) already has the canonical
light-upload pattern that this slice MUST mirror in BOTH
path-tracer dispatchers. Suggested shape (paste-mirror
from `render_direct_lighting:1965-1996` with the same
ordering: cudaMalloc → cudaMemcpy → params.lights /
light_count assignment → cudaFree on every error path):

```cpp
// NEE.5: upload scene.lights so the __raygen__pathtrace
// NEE branch has lights to sample when enable_nee == true.
// Mirrors render_direct_lighting:1965-1996 verbatim. At
// enable_nee == false the kernel never reads
// optixLaunchParams.lights, but uploading
// unconditionally preserves byte-identity (the upload is
// host-side; the kernel's per-pixel arithmetic is
// untouched).
void*       d_lights     = nullptr;
const int   light_count  = static_cast<int>(scene.lights.size());
if (light_count > 0) {
    const std::size_t bytes =
        static_cast<std::size_t>(light_count) * sizeof(rr::lighting::Light);
    if (::cudaMalloc(&d_lights, bytes) != cudaSuccess) {
        // ... cudaFree every prior allocation + return error ...
    }
    std::vector<rr::lighting::Light> light_pods;
    light_pods.reserve(scene.lights.size());
    for (const auto& sl : scene.lights) {
        light_pods.push_back(sl.data);
    }
    if (::cudaMemcpy(d_lights, light_pods.data(), bytes,
                     cudaMemcpyHostToDevice) != cudaSuccess) {
        // ... error handling ...
    }
}
params.lights      = static_cast<const rr::lighting::Light*>(d_lights);
params.light_count = light_count;
```

The implementer must:

- Add the upload BEFORE the
  `params.firefly_clamp = firefly_clamp` block in BOTH
  `render_pathtrace` (active body around
  `OptixRenderer.cpp:1394-1407`) AND
  `render_pathtrace_progressive` (per-launch param
  population at `OptixRenderer.cpp:1725-1729`). The
  progressive variant uploads ONCE before the spp loop,
  not per-launch (the lights are constant across the
  loop).
- Add `cudaFree(d_lights)` to every error-return path
  alongside the existing `cudaFree(d_framebuffer)` /
  `cudaFree(d_indices)` / `cudaFree(d_positions)`
  cleanup blocks. Each error path requires audit-host
  re-verification post-impl.
- Update the existing block comment at
  `OptixRenderer.cpp:1411-1430` ("NEE.4: the NEE branch
  reads `optixLaunchParams.lights` ... leaving
  `params.lights == nullptr` is safe") to reflect the
  NEE.5 upload. The comment can be condensed into a
  one-paragraph summary since the upload is now
  unconditional.

The CUDA path-tracer dispatcher does NOT need any
similar work: `launch_pathtrace_sample` at
`CudaPathTracer.cu:412-413` already sets
`view.lights = scene.device_lights()` /
`view.light_count = scene.light_count()` from the
`GpuScene` upload manager, so the CUDA NEE branch already
has lights available.

### 1.7 No kernel changes

The kernel guards in `CudaPathTracer.cu:276-317` and
`OptixPrograms.cu` `__raygen__pathtrace` NEE branch
(post-NEE.4) are byte-identical post-slice. The
`enable_nee` value flows via existing mechanisms (kernel
arg + `OptixLaunchParams` field). The light upload is
host-side; kernel-side arithmetic is unchanged.

---

## 2. Expected behavior

The four contractual properties the slice must honour
(matching the prompt's spec sub-bullets):

### 2.1 Default OFF

`Config::enable_nee` defaults to `false`. A caller who
does NOT pass `--enable-nee` sees:

- `cfg.enable_nee == false` after parsing.
- `pcfg.enable_nee == false` (CUDA dispatcher).
- `params.enable_nee == false` (OptiX dispatcher).
- The `if (enable_nee && light_count > 0)` guard in BOTH
  backends evaluates `false`; the NEE branch is never
  entered; no shadow ray is traced; no extra RNG draw is
  performed.
- Per-pixel write / per-sample accumulation byte-identical
  with the pre-CLI build (the static IEEE-754 + RNG-stream
  argument from `PATH_TRACER_NEE_AUDIT.md` §1.2 still
  holds).

### 2.2 When present, `PathTraceConfig::enable_nee = true`

The presence of `--enable-nee` on the argv vector (with
no value) sets `r.config.enable_nee = true` in the parser
arm. The CUDA dispatcher's
`pcfg.enable_nee = cfg.enable_nee;` propagates it; the
OptiX dispatcher's
`/*enable_nee=*/cfg.enable_nee` argument propagates it.

The CUDA path tracer's `launch_pathtrace_sample` already
takes `enable_nee` per the NEE.2 signature; the kernel
guard at `CudaPathTracer.cu:276` fires; the helper
`sample_direct_light_uniform` is invoked; the shadow ray
walk runs; the direct-light contribution is added to
`radiance`.

### 2.3 OptiX path tracer receives `enable_nee`

NEE.4 grew `OptixRenderer::render_pathtrace*`'s signature
with a trailing `bool enable_nee = false` arg. NEE.5
populates that arg from `cfg.enable_nee` per §1.5 above.
The dispatcher's `params.enable_nee = enable_nee` upload
(NEE.4) puts it on the `OptixLaunchParams` POD; the
`__raygen__pathtrace` NEE branch (NEE.4) reads it; the
`__miss__shadow` SBT record (Stage 20L) is the shadow-
ray path; the helper produces the same Lambert-BRDF +
cosine + throughput-modulated direct contribution as the
CUDA mirror.

The light upload from §1.6 ensures
`optixLaunchParams.lights` is non-null when the scene
has lights. Without this slice's §1.6 upload,
`params.lights` stays `nullptr` and the helper's
`lights == nullptr || count <= 0` guard returns the
default zero-contribution sample on every bounce —
correct (unbiased) but visually identical to the
`enable_nee == false` case. The upload is what makes
the flag actually do useful work on the OptiX path.

### 2.4 Invalid / repeated usage follows existing CLI conventions

Three failure modes:

- **Repeated `--enable-nee` flags.** E.g.
  `--enable-nee --enable-nee` should be silently
  idempotent; the second arm-evaluation re-assigns
  `true` to an already-`true` field. Mirrors `--denoise`'s
  behaviour (no de-duplication; idempotent).
- **`--enable-nee` followed by a value.** E.g.
  `--enable-nee true` — the parser does NOT consume the
  next token; `true` becomes the next argv entry.
  Suggested behaviour: depending on the surrounding
  argv, `true` either becomes a positional argument
  (no positional consumer in this CLI, so it falls
  through to the "unknown flag / unrecognised
  argument" arm) OR is parsed as the next flag
  (failing if it doesn't match any registered flag).
  Mirrors the existing `--denoise true` behaviour
  (manual smoke during impl will record the exact
  exit code + error message).
- **Unrecognised flag.** E.g. `--enable-Nee` (different
  case). The parser hits its existing `else` arm and
  emits the standard "unknown argument" error; exit
  code 2. No new error-handling infrastructure needed.

The error path uses the existing `Action::Error` +
`r.error_message` + early-return pattern. Same idiom as
every other modifier flag.

### 2.5 Log line classification

Both dispatchers emit a Logger info line per §1.4 / §1.5
above. Format:

```
[INFO] enable_nee       : <bool> (<status>)
```

Where `<bool>` is `true` / `false` and `<status>` is
`enabled` / `disabled`. The strict-equality classification
matches the kernel's binary guard. A future operator
running BOTH dispatchers sees consistent classification
across them.

---

## 3. Required OptiX wiring

This is the most slice-specific section vs the
firefly-clamp brief. NEE.4 already grew the dispatcher
signatures + the `OptixLaunchParams` POD field; NEE.5
must add the LIGHT UPLOAD that makes the flag exercise
the branch on the OptiX path.

### 3.1 Dispatcher signature already has `enable_nee`

NEE.4 (`b29daae`) added a trailing
`bool enable_nee = false` argument to:

- `OptixRenderer::render_pathtrace` (active body at
  `OptixRenderer.cpp:1221-1228`).
- `OptixRenderer::render_pathtrace_progressive` (active
  body at `OptixRenderer.cpp:1503-1509`).
- BOTH fallback (no-OptiX-SDK) bodies at
  `OptixRenderer.cpp:3120-3126` and `:3138-3147`.

NEE.5 does NOT touch these signatures; it only wires
the dispatcher's main.cpp call site to populate the
existing trailing argument.

### 3.2 Light upload (the new work)

Mirror the
`OptixRenderer::render_direct_lighting:1965-1996`
pattern in BOTH `render_pathtrace` (single-launch) and
`render_pathtrace_progressive` (multi-launch). Per §1.6
above:

- `cudaMalloc(d_lights, light_count * sizeof(Light))`
  before the launch-params upload.
- `cudaMemcpy(d_lights, light_pods.data(), bytes, H2D)`.
- `params.lights = d_lights;
  params.light_count = light_count;` before the
  `cudaMemcpy` of `params` to device.
- `cudaFree(d_lights)` on every error-return path AND
  at the function's normal return path AFTER the
  download.
- For the progressive variant: upload ONCE before the
  spp loop, not per-launch.

### 3.3 No-light-scene safety net

When `scene.lights.empty()`:

- `light_count == 0`; `cudaMalloc` is skipped;
  `d_lights == nullptr`.
- `params.lights = nullptr; params.light_count = 0;`
- The `__raygen__pathtrace` NEE guard
  `optixLaunchParams.enable_nee &&
  optixLaunchParams.light_count > 0` evaluates `false`
  even when `enable_nee == true`.
- The integrator falls back to the pre-NEE.5
  emission + environment behaviour byte-for-byte.

This mirrors the CUDA path's safety net (the CUDA
`scene.light_count == 0` branch in the same kernel
guard).

### 3.4 Cross-backend convergence

With `--enable-nee` AND a scene carrying Point /
Directional lights, both backends produce a path tracer
render that includes direct-light contributions. The
two backends are NOT expected to be bit-identical
(different bounce-loop code paths produce different RNG
draws + FMA-fusion patterns; same caveat as the
firefly-clamp CLI brief §6.3). They ARE expected to be
statistically similar:

- Mean luminance per channel agrees within sampling
  noise (~5% at spp=16; tighter at higher spp).
- The lit + shadowed regions match qualitatively (a
  surface point behind an occluder shows the same
  shadow on both backends).

A formal cross-backend convergence check is reserved
for a future runtime-host operator session (per
`PATH_TRACER_NEE_AUDIT.md` §6.2 DEFERRED row).

---

## 4. Required tests

The user's task description explicitly requests two
tests. The slice MUST add both.

### 4.1 CLI parser test

**Goal.** Anchor the parser arm's behaviour (flag sets
the field, absence leaves the default, repeated flags
idempotent) so a future regression on the parser is
caught at host-build time.

**Constraint.** `src/core/CommandLine.cpp` is currently
compiled directly into the `RelativityRender` executable
(see `CMakeLists.txt:604-609`); it is NOT part of any
library. The slice has TWO acceptable options:

- **Option A — extract `rr_core_cli` library.** Add a
  new `add_library(rr_core_cli OBJECT ...)` (or
  STATIC) target containing
  `src/core/CommandLine.cpp` + `src/core/Config.cpp`
  + `src/core/Logger.cpp`. Link both `RelativityRender`
  and the new test binary against it. Cleaner; matches
  the master rule "explicit, testable interfaces"
  (rule 11) + "module boundaries clean" (rule 9).
  Out-of-scope side effect: the `Logger.cpp` link
  pulls in stdio dependencies into the test, which
  is fine (the existing `gpu_tests` already links
  stdio-using code).
- **Option B — re-compile the source in the test.**
  Add a new `add_executable(cli_tests
  tests/cli_tests.cpp src/core/CommandLine.cpp
  src/core/Config.cpp src/core/Logger.cpp)` with
  `target_include_directories(... PRIVATE src)`. No
  library extraction needed. Faster to land; slight
  duplication of the source-list between the
  executable and the test.

**Recommendation: Option A.** Aligns with the
project's existing library-extraction pattern
(`rr_pathtracer`, `rr_renderer`, `rr_optix`, etc.) and
makes future CLI-test growth painless. Implementer may
choose Option B if Option A's ripple is judged too
large for this slice; the brief accepts either.

**Test cases (host-only).** New file
`tests/cli_tests.cpp` matching the existing test-
binary shape (`RR_CHECK` + `int main()` + per-case
counters). Five mandatory cases:

1. **Default-off.** `parse({"prog"})` (no flag) →
   `r.config.enable_nee == false` AND `r.action != Error`.
   Anchors the C++ default-init contract.
2. **Flag presence.** `parse({"prog", "--render-pathtrace",
   "scene.rrscene", "--enable-nee"})` →
   `r.config.enable_nee == true` AND
   `r.action == RenderPathtrace`. Anchors the parser
   arm fires.
3. **Flag-then-action ordering.**
   `parse({"prog", "--enable-nee", "--render-pathtrace",
   "scene.rrscene"})` → same outcome as case 2.
   Confirms the flag is order-independent (the
   modifier-flag contract).
4. **Repeated flag idempotent.**
   `parse({"prog", "--render-pathtrace", "scene.rrscene",
   "--enable-nee", "--enable-nee"})` →
   `r.config.enable_nee == true` AND `r.action !=
   Error`. Anchors the idempotency from §2.4.
5. **Other modifier flag still works.**
   `parse({"prog", "--render-pathtrace", "scene.rrscene",
   "--enable-nee", "--firefly-clamp", "8.0"})` →
   `r.config.enable_nee == true` AND
   `r.config.firefly_clamp == 8.0f` AND `r.action !=
   Error`. Anchors no cross-flag interference.

**Optional 6th case** (recommended; not strictly
required): unrecognised case-variant. `parse({"prog",
"--render-pathtrace", "scene.rrscene", "--enable-Nee"})`
→ `r.action == Error` AND
`r.error_message.contains("--enable-Nee")` (or whatever
the existing "unknown argument" handler emits).
Anchors the case-sensitive matching contract.

### 4.2 Dynamic byte-identity test

**Goal.** Prove the default-OFF path-tracer output
remains byte-identical with the pre-NEE.5 build. This
is the runtime confirmation of the static IEEE-754 +
RNG-stream argument the NEE.2 + NEE.4 doc-comments
make.

**Constraint.** The full byte-identity check requires a
CUDA host (the path-tracer kernel has to actually
launch). On the audit host (no CUDA), the dispatcher's
"requires CUDA" fallback fires before the kernel can
reach the `enable_nee` guard. Two options:

- **Option A — host-only deterministic-arithmetic
  test** in the existing `tests/pathtracer_nee_tests.cpp`
  (new in NEE.4): exercise the
  `rr::pathtracer::sample_direct_light_uniform` helper
  with a representative `(hit_position, normal,
  u_select)` tuple AND assert that the resulting
  `DirectLightSample` has bit-equal `wi` / `distance`
  / `li_unattenuated` / `pdf_inv` fields with the
  pre-slice run. Indirect; doesn't actually exercise
  `enable_nee == false` byte-identity at the kernel
  level, but anchors that the helper's output is
  determinism-stable across the slice.
- **Option B — CUDA-host deferred test** marked
  `BLOCKED` on the audit host: a `cmp`-based PPM
  comparison between a pre-NEE.5 reference render
  (no `--enable-nee` flag) and a post-NEE.5 render
  with the same flag absence. Documented in the
  BUILD_PLAN entry as a runtime-deferred check
  (per `PATH_TRACER_NEE_AUDIT.md` §6.3 DEFERRED
  row pattern); NOT runnable on the audit host.

**Recommendation: BOTH.** The host-only deterministic
test (Option A) ships in `tests/pathtracer_nee_tests.cpp`
inside this slice; it adds 1-2 cases to the existing
file (no new test binary). The CUDA-host test (Option
B) is recorded in the BUILD_PLAN entry's runtime-
deferred table; the `cmp` procedure is documented
verbatim so a CUDA-equipped operator can flip the row
to PASS without re-deriving the procedure.

**Optional 3rd case** for the host-only side: byte-
identity of `sample_direct_light_uniform` returned
sample vs a hand-computed expected value (the existing
pathtracer_nee_tests.cpp cases already cover this for
the in-front Point / Directional shapes; a regression
on the helper's float arithmetic would be caught by
re-asserting bit-identical `li_unattenuated.{x,y,z}`
across builds).

### 4.3 Existing test counts

ctest goes from 8/8 OFF + 9/9 ON (the NEE.4 baseline)
to 9/9 OFF + 10/10 ON if Option A is chosen for §4.1
(the new `cli_tests` binary). If the slice extends the
existing `pathtracer_nee_tests.cpp` (per §4.2 Option A)
WITHOUT adding a new binary, the count stays at 9/9
OFF + 10/10 ON (still +1 vs the NEE.4 baseline because
of the new `cli_tests`).

The TEX-P.6 fixture regression (`--scene-info
scenes/test_textured_material.rrscene`) MUST still emit
the expected three-message sequence.

---

## 5. Files likely involved

The implementation slice will touch this file set —
SIX source files plus the BUILD_PLAN entry plus the new
test file. The two-file cap from PT-P.x is exceeded;
the brief explicitly authorises the larger scope
because (a) the flag spans the parser, the config
struct, both dispatchers' main.cpp call sites, AND the
OptiX dispatcher's light-upload wiring, each at a
backend boundary, and (b) the cross-cutting concern is
unavoidably six-file (similar to the firefly-clamp CLI
slice's four-file footprint, plus the OptiX light
upload).

| File                              | Change                                                  |
|-----------------------------------|---------------------------------------------------------|
| `src/core/Config.h`               | Add `bool enable_nee = false;` field + doc-comment.     |
| `src/core/CommandLine.cpp`        | Add `--enable-nee` parser arm + help-text entry.        |
| `src/main.cpp`                    | One line in `run_render_pathtrace` (`pcfg.enable_nee`); |
|                                   | one line in `run_render_optix_pathtrace` (replace the   |
|                                   | implicit `enable_nee = false` default with              |
|                                   | `cfg.enable_nee`); two `Logger::info` lines.            |
| `src/optix/OptixRenderer.cpp`     | Light upload mirroring `render_direct_lighting:1965-`   |
|                                   | `1996` in BOTH `render_pathtrace` and                   |
|                                   | `render_pathtrace_progressive`. Updates the existing    |
|                                   | NEE.4 block-comment at `:1411-1430` to reflect the      |
|                                   | new upload.                                             |
| `tests/pathtracer_nee_tests.cpp`  | Optional 1-2 cases per §4.2 Option A (host-only         |
|                                   | helper-determinism anchor).                             |
| `tests/cli_tests.cpp` (NEW)       | 5-6 cases per §4.1 covering parser arm behaviour.       |
| `CMakeLists.txt`                  | Wire `cli_tests` (and optionally extract `rr_core_cli`  |
|                                   | per §4.1 Option A).                                     |
| `docs/BUILD_PLAN.md`              | Slice-closing entry following the established TEX-P.x  |
|                                   | / PT-P.x / NEE.x format.                                |

`src/core/CommandLine.h` may need a 1-line update if
the help-text declaration lives there rather than in
`CommandLine.cpp` (a quick check during implementation
will confirm).

---

## 6. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 6.1 The kernel + launcher code

- `src/cuda/CudaPathTracer.{cu,cuh}` — every byte. The
  NEE.2 kernel guard + launcher signature stay
  unchanged.
- `src/optix/OptixPrograms.cu` — every byte. The NEE.4
  raygen guard stays unchanged.
- `src/optix/OptixLaunchParams.h` — every byte. The
  NEE.4 `enable_nee = false` POD field stays unchanged.
- `src/optix/OptixRenderer.h` — every byte. The NEE.4
  dispatcher signatures stay unchanged.
- `src/optix/OptixPipeline.{h,cpp}` — every byte. The
  Stage 20L `__miss__shadow` SBT record binding stays
  unchanged.
- `src/optix/OptixSBT.h` — every byte.

### 6.2 The path-tracer host orchestration

- `src/pathtracer/PathTracer.{h,cpp}` — every byte.
  `PathTraceConfig::enable_nee` (NEE.2) stays
  unchanged. `PathTracer::render`'s validation
  prelude unchanged.
- `src/pathtracer/RNG.{h,cuh}`,
  `src/pathtracer/Sampling.{h,cuh}`,
  `src/pathtracer/DirectLight.{h,cuh}`: every byte.

### 6.3 Other CLI / Config infrastructure

- `Config::beta`, `Config::firefly_clamp`,
  `Config::scene_path`, `Config::width`,
  `Config::height`, `Config::denoise_enabled`, every
  other `Config` field: byte-identical declarations +
  defaults.
- `CommandLine::parse`'s OTHER argument-parsing arms
  (`--beta`, `--scene`, `--width`, `--height`,
  `--firefly-clamp`, `--denoise`, etc.):
  byte-identical.
- `print_help`'s OTHER help-text lines: byte-identical;
  the new `--enable-nee` line is INSERTED.

### 6.4 Other dispatchers

- `run_render_aovs`, `run_render_optix_aovs`,
  `run_scene_info`, `run_render_textured_material`,
  `run_render_relativistic`,
  `run_render_direct_lighting`,
  `run_render_optix_direct_lighting`, every other
  dispatcher in `src/main.cpp` that does NOT consume
  `enable_nee`: byte-identical.

### 6.5 Other OptiX dispatchers

- `OptixRenderer::render_test`,
  `OptixRenderer::render_triangle`,
  `OptixRenderer::render_relativity`,
  `OptixRenderer::render_raygen`,
  `OptixRenderer::render_mesh_scene`,
  `OptixRenderer::render_material_scene`,
  `OptixRenderer::render_direct_lighting`,
  `OptixRenderer::render_shadow_test`,
  `OptixRenderer::render_textured_material`,
  `OptixRenderer::render_aovs`,
  `OptixRenderer::render_optix_denoise`: every byte.
  Only `render_pathtrace` and
  `render_pathtrace_progressive` grow with light upload.

### 6.6 Path-tracer output at default-OFF

For a caller passing NO `--enable-nee` flag:

- Pre-NEE.5 PPM bytes vs post-NEE.5 PPM bytes:
  bit-identical for both backends (CUDA + OptiX).
- The static IEEE-754 + RNG-stream argument from
  `PATH_TRACER_NEE_AUDIT.md` §1.2 is the proof.
- The §4.2 dynamic byte-identity test confirms it
  empirically (host-only Option A; CUDA-host Option
  B deferred).

### 6.7 OptiX OFF build

The new flag is parsed unconditionally on every build
config; the dispatcher reads `cfg.enable_nee`
unconditionally. On an `RR_ENABLE_OPTIX=OFF` build:

- `--enable-nee` parses fine (parser is backend-
  agnostic).
- `Config::enable_nee` stores fine.
- `--render-pathtrace`'s CUDA dispatcher reads it.
  But on the audit-host build (no CUDA), the
  dispatcher returns the documented "requires CUDA"
  error before `pcfg.enable_nee` is consumed.
  Behaviour byte-identical with pre-CLI audit-host
  fallback.
- `--render-optix-pathtrace`'s OptiX dispatcher is
  `#ifdef`-gated by `RELATIVITYRENDER_ENABLE_OPTIX`;
  on a no-OptiX build, the dispatcher's source is
  not compiled. The new `cfg.enable_nee` reference
  inside that `#ifdef` block is also skipped at
  compile time on the OFF build.

### 6.8 Other audits / plans

- `docs/PATH_TRACER_NEE_TASK.md`,
  `docs/PATH_TRACER_NEE_AUDIT.md`: NO edits. These
  remain the canonical specifications for the NEE arc.
- `docs/FIREFLY_CLAMP_CLI_TASK.md`,
  `docs/FIREFLY_CLAMP_CLI_AUDIT.md`: NO edits.
- Every PT-P.x doc, the TEX-P.x arc, the CUDA-H.x arc,
  the CUDA-OPTIX-VERIFY report: NO edits.
- `tools/verify_cuda_host.py`: NO changes (the runner
  exercises the existing `--render-pathtrace` +
  `--render-optix-pathtrace` commands; the new flag is
  opt-in and the runner doesn't pass it by default. A
  FUTURE runner-extension slice could add an
  `--enable-nee` variant; out of scope here).

### 6.9 Scenes

- Every `*.rrscene` file under `scenes/`: byte-
  identical. The NEE flag does not author new fixtures.
  The existing `scenes/test_full_scene.rrscene` /
  similar fixtures (if they carry Point / Directional
  lights) become exercisable with `--enable-nee` on
  a CUDA + OptiX-SDK host without modification.

---

## 7. PASS criteria

The implementation slice passes when ALL of the
following hold:

### 7.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF): clean
  build, zero new warnings. The new
  `Config::enable_nee` field is type-checked through
  `CommandLine::parse` + every dispatcher consumer.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON with SDK
  fallback): clean build, zero new warnings. The
  light-upload wiring's `cudaMalloc` / `cudaMemcpy` /
  `cudaFree` calls compile against the existing
  `<cuda_runtime.h>` includes + the
  `RELATIVITYRENDER_ENABLE_OPTIX` `#ifdef` block.

### 7.2 Tests

- `ctest --output-on-failure` from `build`: 9/9 PASS
  (was 8/8 OFF; +1 from the new `cli_tests`).
- `ctest --output-on-failure` from `build-ON`: 10/10
  PASS (was 9/9 ON; +1 from the new `cli_tests`).
- The new `pathtracer_nee_tests` (NEE.4) test counts
  may grow by 1-2 cases per §4.2 Option A (still one
  binary; ctest binary count unchanged).

### 7.3 Source diff size

- `src/core/Config.h`: ~6-12 added.
- `src/core/CommandLine.cpp`: ~10-20 added (parser
  arm + help-text entry; smaller than firefly-clamp's
  25-35 because no value-take + no range check).
- `src/main.cpp`: ~6-10 added (two passthrough lines
  + two Logger lines).
- `src/optix/OptixRenderer.cpp`: ~60-100 added (light
  upload in BOTH dispatchers + cudaFree path on every
  error return + the block-comment update). This is
  the dominant source-diff line-count.
- `tests/cli_tests.cpp`: ~150-250 added (new file).
- `tests/pathtracer_nee_tests.cpp`: 0-10 added (per
  §4.2 Option A; optional).
- `CMakeLists.txt`: ~5-10 added (test wiring +
  optional library extraction per §4.1 Option A).
- TOTAL across all source files: ≤ 350 added (excl.
  doc-comments). Anything LARGER flagged in the
  BUILD_PLAN entry as a deviation.

### 7.4 No-touch invariants

`git diff` after the slice MUST show zero bytes
changed in:

- `src/cuda/`
- `src/optix/OptixPrograms.cu`,
  `src/optix/OptixLaunchParams.h`,
  `src/optix/OptixRenderer.h`,
  `src/optix/OptixPipeline.{h,cpp}`,
  `src/optix/OptixSBT.h`,
  `src/optix/OptixDenoiser.{h,cpp}`,
  `src/optix/OptixAccel.{h,cpp}`,
  `src/optix/OptixBackend.{h,cpp}`. (Only
  `OptixRenderer.cpp` grows.)
- `src/pathtracer/`
- `src/renderer/`
- `src/io/`
- `src/scene/`
- `src/material/`
- `src/lighting/`
- `src/texture/`
- every `*.rrscene` file under `scenes/`
- `tools/verify_cuda_host.py`

Verifiable by:

```
git diff -- \
  src/cuda/ src/optix/OptixPrograms.cu \
  src/optix/OptixLaunchParams.h \
  src/optix/OptixRenderer.h \
  src/optix/OptixPipeline.h src/optix/OptixPipeline.cpp \
  src/optix/OptixSBT.h \
  src/optix/OptixDenoiser.h src/optix/OptixDenoiser.cpp \
  src/optix/OptixAccel.h src/optix/OptixAccel.cpp \
  src/optix/OptixBackend.h src/optix/OptixBackend.cpp \
  src/pathtracer/ src/renderer/ src/io/ src/scene/ \
  src/material/ src/lighting/ src/texture/ \
  scenes/ tools/verify_cuda_host.py \
  | wc -l
=> 0
```

### 7.5 Behavioural smoke (audit host)

Five smokes:

1. **Default, no flag passed.**
   `./build/bin/RelativityRender --render-pathtrace
   scenes/test_full_scene.rrscene` — emits the
   documented "requires CUDA" audit-host fallback
   byte-identically with pre-CLI baseline. The new
   `enable_nee       : false (disabled)` log line MAY
   appear before the fallback (depending on whether
   the dispatcher logs before erroring); the
   implementer records the exact ordering.
2. **`--enable-nee` passed, no value.**
   `./build/bin/RelativityRender --render-pathtrace
   scenes/test_full_scene.rrscene --enable-nee` —
   emits the same "requires CUDA" fallback. Confirms
   the parser accepts `--enable-nee` without a value
   + the dispatcher passes `true` through to
   `pcfg.enable_nee`. The new
   `enable_nee       : true (enabled)` log line MAY
   appear.
3. **OptiX equivalent.**
   `./build-ON/bin/RelativityRender
   --render-optix-pathtrace
   scenes/test_full_scene.rrscene --enable-nee` —
   emits the existing
   `firefly_clamp    : 0.000000 (disabled)` log line
   followed by the new
   `enable_nee       : true (enabled)` log line
   followed by the documented "requires OptiX SDK"
   fallback. Confirms the OptiX dispatcher's log
   ordering + the dispatcher passes `true` through to
   `params.enable_nee` (audit-host fallback fires
   before the kernel can reach the guard).
4. **Repeated flag idempotent.**
   `./build/bin/RelativityRender --render-pathtrace
   scenes/test_full_scene.rrscene --enable-nee
   --enable-nee` — same outcome as smoke 2. Anchors
   the §2.4 idempotency contract.
5. **Combined with `--firefly-clamp`.**
   `./build/bin/RelativityRender --render-pathtrace
   scenes/test_full_scene.rrscene --enable-nee
   --firefly-clamp 8.0` — emits BOTH new log lines
   (`firefly_clamp    : 8.000000 (enabled)` AND
   `enable_nee       : true (enabled)`) before the
   fallback. Confirms the two flags are independent.

Plus the standard regression check:

6. **TEX-P.6 fixture.**
   `./build-ON/bin/RelativityRender --scene-info
   scenes/test_textured_material.rrscene` — emits the
   expected three-case log sequence (one Case 1 info
   + two Case 3 warnings; `fixups applied: 2`).
   Confirms the new flag has no ripple onto the
   texture validator or any non-pathtrace dispatcher.

### 7.6 Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing
  entry matching the established TEX-P.x / PT-P.x /
  NEE.x format (Scope / What ships / What does NOT
  change / Master rule compliance / Verified at the
  build).
- The entry references this task file +
  `docs/PATH_TRACER_NEE_AUDIT.md` §3.3 NEE.5 as the
  source of the specification.
- The entry's "Verified at the build" subsection
  includes the six smoke cases from §7.5 + their
  expected outcomes.

### 7.7 Master rule compliance

- Build incrementally (rule 1) + every step
  compilable (rule 2): both audit-host configs green.
- No fake stubs (rule 3): the parser arm + the field
  + the dispatcher pass-throughs + the OptiX light
  upload are all real code; nothing pretends to work.
- No CPU per-pixel work (rule 5/7): the flag is
  parsed once at startup; the kernel guards do the
  per-pixel work device-side. Zero new host-side
  per-pixel code. The light upload is an O(N_lights)
  H2D copy; not per-pixel.
- Module boundaries (rule 9): if the implementer
  picks §4.1 Option A, the new `rr_core_cli` library
  + `cli_tests` binary cleanly separate the parser
  contract from the executable.
- Avoid monolithic files (rule 10): the new logic
  spreads across six files; no single file grows
  unreasonably.
- Explicit testable interfaces (rule 11): the CLI
  parser becomes host-testable for the first time.
- Update BUILD_PLAN (rule 8): the slice-closing entry.

---

## 8. Runtime-deferred checks for real CUDA / OptiX host

The CLI flag enables runtime checks NEE.{2..4} couldn't
exercise without it. On a real CUDA + OptiX-SDK host,
the operator can now run:

### 8.1 Default-off byte-IDENTITY (`PATH_TRACER_NEE_AUDIT.md` §6.3)

With the CLI flag, the operator can confirm default-OFF
byte-identity explicitly:

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm /tmp/pre_nee5.ppm

# After NEE.5 lands (same flag absence):
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm /tmp/post_nee5.ppm

$ cmp /tmp/pre_nee5.ppm /tmp/post_nee5.ppm  ; echo $?
=> 0 (identical — NEE.5 is byte-identical at default-OFF)
```

This is the runtime confirmation of the static IEEE-754
+ RNG-stream argument the NEE.2 + NEE.4 + NEE.5 doc-
comments make. The host-only test from §4.2 Option A
anchors the helper's determinism on the audit host;
this CUDA-host check anchors the kernel's per-pixel
arithmetic.

### 8.2 Visible NEE-on noise reduction (`PATH_TRACER_NEE_AUDIT.md` §6.1)

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/<lit-scene>.rrscene
$ cp output/pathtrace_spp_4.ppm /tmp/no_nee.ppm

$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/<lit-scene>.rrscene --enable-nee
$ cp output/pathtrace_spp_4.ppm /tmp/with_nee.ppm

# Visual inspection: shadows + lit regions should be
# visibly less noisy in /tmp/with_nee.ppm at low spp.
$ cmp /tmp/no_nee.ppm /tmp/with_nee.ppm  ; echo $?
=> 1 (different — NEE branch fired)
```

The lit scene needs Point and/or Directional lights for
NEE to contribute. The first runtime confirmation that
NEE does useful work.

### 8.3 Cross-backend convergence (`PATH_TRACER_NEE_AUDIT.md` §6.2)

The first cross-backend smoke for NEE:

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/<lit-scene>.rrscene --enable-nee
$ cp output/pathtrace_spp_16.ppm /tmp/cuda_nee.ppm

$ ./build-cuda/bin/RelativityRender --render-optix-pathtrace \
    scenes/<lit-scene>.rrscene --enable-nee
$ cp output/optix_pathtrace_spp16.ppm /tmp/optix_nee.ppm
```

Same caveat as the firefly-clamp CLI brief §6.3: the two
PPMs are NOT expected to be bit-identical (different
bounce-loop code paths produce different RNG draws +
FMA-fusion patterns). They ARE expected to be
statistically similar:

- Mean luminance per channel agrees within sampling
  noise (~5% at spp=16; tighter at higher spp).
- Lit + shadowed regions match qualitatively (a surface
  point behind an occluder shows the same shadow on
  both backends).

### 8.4 No-light-scene safety net

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/<no-lights-scene>.rrscene --enable-nee
$ cp output/pathtrace_spp_16.ppm /tmp/with_nee_nolight.ppm

$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/<no-lights-scene>.rrscene
$ cp output/pathtrace_spp_16.ppm /tmp/no_nee_nolight.ppm

$ cmp /tmp/with_nee_nolight.ppm /tmp/no_nee_nolight.ppm  ; echo $?
=> 0 (identical — light_count == 0 short-circuits NEE)
```

Confirms the §3.3 safety-net contract: when the scene
has no lights, the NEE branch's `light_count > 0` guard
short-circuits regardless of `enable_nee` value.

### 8.5 ctest cycle on CUDA host

`ctest --output-on-failure` from a CUDA-built
`build-cuda` directory must pass. The new `cli_tests`
binary is host-only and runs identically to the
audit-host run.

### 8.6 Refresh CUDA-OPTIX-VERIFY report

After the operator runs §8.1-§8.4 on a CUDA + OptiX-SDK
host, the operator commits a refreshed
`docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md` with the
NEE rows flipped from `DEFERRED` to `PASS`.

---

## 9. Out-of-scope (deferred to future slices)

The following items are explicitly NOT part of this
slice:

1. **MIS (Multiple Importance Sampling).** The v1 NEE
   scope sums the existing emission term and the new
   NEE term naively. The "no double-count window"
   argument from `docs/PATH_TRACER_NEE_TASK.md` §1
   holds because Point + Directional lights have no
   mesh — the emission and NEE terms sample disjoint
   contributions. MIS is reserved for the area-light
   slice.
2. **Area + Environment NEE.** The placeholder light
   types remain. The helper returns zero-contribution
   samples for them; the kernel naturally treats them
   as no-op. A future slice unblocks Area + Environment
   NEE alongside MIS.
3. **`tools/verify_cuda_host.py` extension.** A
   FUTURE runner-extension slice could add an
   `--enable-nee` variant to `base_commands()` /
   `optix_commands()` lists; would produce per-command
   stats showing NEE-on runs pass on a CUDA host. Out
   of scope here.
4. **A `--no-enable-nee` flag.** No operator-facing
   need today; the default-off baseline serves the
   "turn it off" case implicitly. A future slice
   adds this if needed (e.g. for a CLI that defaults
   the flag on for some action).
5. **`enable_nee` on action `--render-direct-lighting`
   / `--render-optix-direct-lighting`.** These actions
   are the Stage 9B / 20K direct-lighting renders,
   NOT path tracers. The NEE flag has no meaning for
   them; they ignore it. Documented in the new
   field's doc-comment per §1.1.
6. **Per-light-type NEE switches.** A more granular
   `--enable-point-nee` / `--enable-directional-nee`
   pair is hypothetical and not requested. A future
   slice adds them if needed.

---

## 10. Sub-arc context

### 10.1 NEE arc cadence

| Slice | Role |
|-------|------|
| NEE.1 | Task definition for the NEE arc (`PATH_TRACER_NEE_TASK.md`) |
| NEE.2 | CUDA NEE skeleton (impl) — `6f49c55` |
| NEE.3 | CUDA NEE skeleton audit (`PATH_TRACER_NEE_AUDIT.md`) — `c857f29` |
| NEE.4 | OptiX-side NEE mirror + first host-side helper test — `b29daae` |
| **NEE.5** | **`--enable-nee` CLI flag + tests** (this brief) |
| NEE.6 | NEE.5 audit (recommended next) |
| NEE.7+ | Area / Environment NEE + MIS (deferred) |

### 10.2 What this slice unblocks

- The `PATH_TRACER_NEE_AUDIT.md` §3.3 NEE.5 row from
  "DEFERRED" to "PASS" (the CLI flag now exists).
- The `PATH_TRACER_NEE_AUDIT.md` §6.1-§6.5 runtime
  DEFERRED rows: §6.1 visible-noise-reduction, §6.2
  cross-backend convergence, §6.3 default-off byte-
  identity (runtime), §6.4 no-light-scene smoke, §6.5
  directional-light smoke. All five become runnable
  on a CUDA + OptiX-SDK host.
- The `PATH_TRACER_NEE_AUDIT.md` §3.2 sequencing
  constraint flips from "the OptiX-side mirror MUST
  land before any caller flips the flag" to
  "satisfied — NEE.4 landed at `b29daae`; NEE.5
  flips the flag safely". The atomicity-equivalent
  invariant is now a closed-loop contract.

### 10.3 What this slice does NOT unblock

- A formal cross-backend convergence test (§8.3 above
  is a runtime-deferred operator session, not an
  in-build automated test). The cross-backend
  byte-difference characterisation is reserved for a
  future slice that adds tooling to compare PPMs +
  produce statistical summaries.
- An automated visible-noise-reduction harness
  (§8.2). The reduction is qualitative; an automated
  metric (e.g. variance reduction across pixels)
  could land in a future slice but is out of scope
  here.

---

## 11. Verdict

The brief is complete. The implementer can ship NEE.5
end-to-end without re-deriving any of the NEE.{1..4}
artefacts; the task description above is a fully-
specified contract. The sequencing constraint from
`PATH_TRACER_NEE_AUDIT.md` §3.2 is satisfied; the OptiX
mirror has landed; the CLI flag implementation is
unblocked.

**Mode reminder: documentation only.** This file is
the spec. The next slice (the impl) ships the source
diff + `tests/cli_tests.cpp` + an updated
`tests/pathtracer_nee_tests.cpp` + the BUILD_PLAN
entry.

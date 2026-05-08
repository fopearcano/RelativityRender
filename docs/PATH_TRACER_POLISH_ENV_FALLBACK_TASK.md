# Path-Tracer Polish — Environment Fallback Clarity Task

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Plan source: `docs/PATH_TRACER_POLISH_PLAN.md` §4.4.
Selected via:
`docs/PATH_TRACER_POLISH_SAMPLE_COUNT_CAP_AUDIT.md` §9's
"Recommended next step" verdict (the §4.6 polish shipped
PASS via PT-P.9; §4.4 is the smallest remaining item per
`PATH_TRACER_POLISH_PLAN.md` §5's sequencing).
Mode: documentation only. **No source code is modified by
this task definition.** The task is the spec; the next
slice (PT-P.12 implementation) ships the diff.

This file is a fully-self-contained brief for the next
implementation slice. Anyone picking it up should be able
to ship the change without re-deriving the plan's
reasoning.

---

## 1. Exact issue

**Title.** PT-P.x — Environment fallback clarity.

**Source.** `PATH_TRACER_POLISH_PLAN.md` §4.4.

**One-sentence summary.** The environment fallback
behaviour exists and is correct (the kernel multiplies
`environment_color * environment_intensity` on every
miss and adds `throughput * env` to the radiance), but
two clarity gaps make it easy to misread: (a) the
`PathTraceConfig::environment_intensity` doc-comment
does not state that `0.0f` produces a fully black
background; (b) the `run_render_pathtrace` dispatcher's
post-render info log does not echo the environment
fields, so an operator wondering why a sphere-only
scene looks blue has to read source to confirm the
default sky tint is firing.

**The current state.** Two source artefacts are
relevant:

### 1.1 The kernel-side fallback

`src/cuda/CudaPathTracer.cu` (lines 144-184) reads
`env_color` + `env_intensity` from launch arguments and,
on a miss, executes:

```cpp
const Vec3 env = env_color * env_intensity;
radiance = radiance + Vec3{throughput.x * env.x,
                           throughput.y * env.y,
                           throughput.z * env.z};
break;
```

When `env_intensity == 0`, `env == (0, 0, 0)`, so the
miss adds zero to `radiance` — the documented black-
background behaviour. The OptiX-side raygen
(`src/optix/OptixPrograms.cu`'s `__raygen__pathtrace` /
`__miss__pathtrace`) honours the same convention.

### 1.2 The host-side config

`src/pathtracer/PathTracer.h:60-67` defines the two
fields with their defaults + a doc-comment that
explains the multiplication semantics:

```cpp
// Environment fallback. When a ray misses every scene primitive
// the path tracer treats this as the radiance arriving from
// infinity. `environment_color * environment_intensity` is the
// emitted spectral colour; both are linear-space RGB. Defaults
// produce a moderate cool sky tint so a scene with no emissive
// surfaces still produces a visible image.
rr::math::Vec3 environment_color     = {0.55f, 0.70f, 1.00f};
float          environment_intensity = 0.30f;
```

The doc-comment names the multiplication but does NOT
flag the `environment_intensity == 0` special case.

### 1.3 The host-side dispatcher

`src/main.cpp:2400-2411` (`run_render_pathtrace`'s
post-render info-log block) prints four lines per
spp run:

```cpp
Logger::info(std::string("scene file       : ") + cfg.scene_path);
Logger::info("framebuffer      : "
           + std::to_string(width) + "x" + std::to_string(height)
           + " (from render_settings)");
Logger::info(std::string("pathtrace        : ")
           + std::to_string(run.spp) + " spp, "
           + std::to_string(pcfg.max_bounces) + " bounces, "
           + std::to_string(sphere_pods.size())   + " sphere(s), "
           + std::to_string(material_pods.size()) + " material(s), "
           + std::to_string(light_pods.size())    + " light(s), "
           + std::to_string(mesh_to_upload != nullptr ? 1 : 0)
           + " mesh(es)");
```

There is no `environment :` line. The operator sees
spp / bounces / object counts but not the environment
contribution.

**The single concrete change.**

This polish slice is a doc-comment extension + a
single new info-log line. NO kernel touches; NO new
CLI flags; NO `PathTraceConfig` field changes. The
implementation is mechanical; the difficulty is purely
in writing concise prose.

Required outcome:

### 1.4 Doc-comment extension (`src/pathtracer/PathTracer.h`)

Append exactly one new sentence to the existing
`environment_intensity` doc-comment block. Suggested
wording:

```cpp
// Environment fallback. When a ray misses every scene primitive
// the path tracer treats this as the radiance arriving from
// infinity. `environment_color * environment_intensity` is the
// emitted spectral colour; both are linear-space RGB. Defaults
// produce a moderate cool sky tint so a scene with no emissive
// surfaces still produces a visible image.
//
// PT-P.12: setting `environment_intensity == 0.0f` produces a
// fully black background for missed rays — the kernel still
// adds `throughput * env` to the radiance on every miss, but
// `env` evaluates to `(0, 0, 0)` so the contribution is zero.
// Use this when authoring scenes whose only light sources
// are emissive surfaces / explicit lights and the operator
// wants no background ambient term.
rr::math::Vec3 environment_color     = {0.55f, 0.70f, 1.00f};
float          environment_intensity = 0.30f;
```

The exact wording is the implementer's choice; the
contract is that the doc-comment must (a) name the
`== 0.0f` special case, (b) describe its visual
outcome ("fully black background"), and (c) note the
authoring rationale (use it when there are explicit
light sources). Roughly 6-8 lines of comment text.

### 1.5 Dispatcher info-log line (`src/main.cpp`)

Insert ONE new `Logger::info` call alongside the
existing four. Suggested wording matches the existing
columnar format (right-padded label, ":", value):

```cpp
Logger::info(std::string("environment      : (")
           + format_float3(pcfg.environment_color) + ") * "
           + format_float(pcfg.environment_intensity));
```

The implementer may either:

- **(a)** introduce two small format helpers (e.g.
  `format_float3` / `format_float`) producing strings
  like `"0.550, 0.700, 1.000"` / `"0.300"`;
- **(b)** inline the formatting using
  `std::to_string` (loses precision: `std::to_string`
  produces `"0.550000"` by default, which clutters
  the line);
- **(c)** reuse an existing helper if one is available
  in `src/main.cpp` (the dispatcher already has
  `fmt_vec3` / `fmt_bool` / etc. — see the
  `--scene-info` block that prints scene-loaded values).

**Choice (c) is recommended.** `src/main.cpp` already
defines a `fmt_vec3` helper that produces
`"[0.550000, 0.700000, 1.000000]"`-style output (see
`run_scene_info`'s materials block printout); reusing
it preserves the dispatcher's existing visual idiom.

If the implementer adopts (c), the new line might be:

```cpp
Logger::info(std::string("environment      : ")
           + fmt_vec3(pcfg.environment_color) + " * "
           + std::to_string(pcfg.environment_intensity));
```

The exact label width / formatting is the implementer's
call; the contract is that ONE new info-log line names
both fields + the multiplication. Place it immediately
after the existing `pathtrace        : ...` line so
the operator sees a logical config-summary block.

### 1.6 No kernel change

The §4.4 plan's "Risk: None. Doc-only + post-render
info log; no kernel change." is the contract. The
prompt's rule "no kernel behavior change unless
strictly necessary" is honoured trivially: the polish
adds zero `__device__` / `__global__` / `RR_HD` code.
The kernel-side miss handler in
`src/cuda/CudaPathTracer.cu` and the OptiX-side
`__miss__pathtrace` in `src/optix/OptixPrograms.cu`
are byte-identical post-slice.

---

## 2. Expected behavior

The three contractual properties the polish must
honour (matching the prompt's spec sub-bullets
exactly):

### 2.1 Path tracer clearly documents what happens when rays miss

After the slice, the
`PathTraceConfig::environment_intensity` doc-comment
in `PathTracer.h` enumerates THREE cases the operator
might author:

- **Default (`environment_intensity == 0.30`,
  `environment_color == (0.55, 0.70, 1.00)`)**: cool
  sky tint; scenes without emissive surfaces still
  produce a visible image.
- **`environment_intensity == 0.0f`**: fully black
  background; suitable for scenes with explicit
  emissive surfaces / lights.
- **`environment_intensity > 0` with custom
  `environment_color`**: artist-chosen ambient
  spectrum.

The doc-comment can name the first case implicitly
(by leaving the existing default-naming sentence as-
is) and explicitly document the second case (per
§1.4 above). The third case is implicit in the
multiplication semantics already documented.

### 2.2 Dispatcher / log output identifies the environment fallback mode

After the slice, `run_render_pathtrace`'s post-render
info-log block emits FIVE lines per spp run:

```
[INFO] scene file       : scenes/test_full_scene.rrscene
[INFO] framebuffer      : 1280x720 (from render_settings)
[INFO] pathtrace        : 1 spp, 4 bounces, 4 sphere(s), 5 material(s), 3 light(s), 1 mesh(es)
[INFO] environment      : [0.550000, 0.700000, 1.000000] * 0.300000
[INFO] wrote pathtrace_spp_1.ppm: ...
```

(Or whatever exact format the implementer chooses; see
§1.5.) The line must:

- Always emit (no special-case suppression for
  default values; the operator wants to confirm the
  default is firing).
- Not require `--render-pathtrace` to know about
  `PathTraceConfig` internals beyond the two fields
  it already passes to `pt.render(...)`.
- Survive the `samples_per_pixel = 0` short-circuit
  (the existing `if (!r.ok)` check handles failures
  before reaching the info-log block; the polish
  inserts AFTER the existing four lines, so any
  failure path still skips it).

The OptiX-side dispatcher
(`run_render_optix_pathtrace` in `src/main.cpp`) is
explicitly OUT OF SCOPE for this slice — it has its
own post-render block whose shape diverges from the
CUDA dispatcher's. A future PT-P.x slice may apply
the same clarity polish to the OptiX dispatcher; this
brief covers the CUDA dispatcher only, mirroring
PT-P.6 / PT-P.9's CUDA-only host-side scope.

### 2.3 No kernel behavior change unless strictly necessary

ZERO kernel touches. The kernel-side miss handler in
`src/cuda/CudaPathTracer.cu` and the OptiX-side
miss + closest-hit programs are byte-identical
post-slice. Verifiable by:

```
git diff -- src/cuda/ src/optix/ | wc -l
=> 0
```

after the implementation slice lands.

---

## 3. Files likely involved

The implementation slice will touch this minimal set:

| File                              | Change                                                  |
|-----------------------------------|---------------------------------------------------------|
| `src/pathtracer/PathTracer.h`     | Append a doc-comment paragraph to the existing         |
|                                   | `environment_intensity` block (~6-8 lines).             |
| `src/main.cpp`                    | Add ONE new `Logger::info` call inside                 |
|                                   | `run_render_pathtrace`'s post-render block,            |
|                                   | reusing the existing `fmt_vec3` helper if it is        |
|                                   | already in scope (~3-5 lines).                          |
| `docs/BUILD_PLAN.md`              | Slice-closing entry following the established          |
|                                   | TEX-P.x / PT-P.x format.                                |

Two source files; matches the PT-P.x master rule of
"max 2 source files unless explicitly justified".

`src/cuda/CudaPathTracer.cu`,
`src/optix/OptixPrograms.cu`,
`src/optix/OptixRenderer.{h,cpp}`,
`src/renderer/AccumulationBuffer.{h,cpp}`,
`src/pathtracer/PathTracer.cpp`,
`src/core/CommandLine.{h,cpp}`,
every `tests/*.cpp` file, every `*.rrscene` file, and
every other `src/` file MUST be byte-identical
post-slice.

### 3.1 No new test required

The polish is a doc-comment + an info-log line. The
PT-P.6 / PT-P.9 "verifiable by code inspection"
precedent applies: reading the doc-comment confirms
§2.1; reading the info-log line confirms §2.2; the
zero-`src/cuda/` / zero-`src/optix/` diff confirms
§2.3. No ctest binary needs to assert log-line
emission.

---

## 4. What must not be touched

The implementation slice MUST keep the following
byte-identical:

### 4.1 Kernel and launcher code

- `src/cuda/CudaPathTracer.cu` — every byte. The
  miss handler's `env_color * env_intensity`
  multiplication is correct as-is; no floating-point
  threshold check (`if (env_intensity > 0) ...`) is
  introduced because the existing math already
  collapses to a no-op when `env_intensity == 0`.
- `src/cuda/CudaAccumulation.cu` — every byte.
- `src/cuda/CudaAccumulation.cuh` — every byte.
- `src/cuda/CudaPathTracer.cuh` — every byte.
  `launch_pathtrace_sample`'s signature is preserved.
- `src/optix/OptixPrograms.cu` — every byte. The OptiX
  raygen / miss / closest-hit programs are
  unchanged.
- `src/optix/OptixRenderer.{h,cpp}` — every byte.

### 4.2 Renderer and path-tracer host code

- `src/renderer/AccumulationBuffer.{h,cpp}` — every
  byte. The PT-P.3 polish to this file is settled.
- `src/pathtracer/PathTracer.cpp` — every byte.
  PathTracer::render's validation prelude (PT-P.6
  / PT-P.9 clamps + the lower-bound / negative
  rejections) is byte-identical. The function body
  reads `cfg.environment_color` /
  `cfg.environment_intensity` once each in the
  existing CUDA-only branch (passed straight to
  `launch_pathtrace_sample`); no new validation or
  transformation is inserted.

### 4.3 PathTraceConfig field set

- Byte-identical (zero new fields, zero default
  changes). `environment_color` /
  `environment_intensity` keep their existing
  defaults. The PT-P.6 `kMaxBouncesCap` and PT-P.9
  `kSamplesPerPixelCap` constants are unchanged.

### 4.4 Path-tracer output

For every authored `PathTraceConfig`:

- `output/pathtrace_spp_1.ppm`,
  `output/pathtrace_spp_16.ppm`: byte-identical
  pixel data.
- `output/optix_pathtrace_spp1.ppm`,
  `output/optix_pathtrace_spp16.ppm`: byte-identical.
- `output/gpu_accumulation_test.ppm`,
  `output/gpu_rng_test.ppm`: byte-identical.

The polish is host-side only; no kernel arg / launch
parameter changes; no PPM byte changes.

### 4.5 CLI surface

- No new `--*` flag.
- No change to the existing `--render-pathtrace` /
  `--render-optix-pathtrace` /
  `--render-rng-test` /
  `--render-accumulation-test` argument parsers.
- The OPTIX dispatcher (`run_render_optix_pathtrace`)
  is OUT OF SCOPE for this slice; if its info-log
  block is touched, the slice's source-diff size
  cap is automatically violated. Stay focused on the
  CUDA dispatcher only.

### 4.6 Existing dispatcher info-log lines

- The four existing lines in `run_render_pathtrace`'s
  post-render block (`scene file       : ...`,
  `framebuffer      : ...`,
  `pathtrace        : ... spp, ... bounces, ...`,
  `wrote pathtrace_spp_1.ppm: ...`) are
  byte-identical. The new `environment      : ...`
  line is INSERTED; nothing is reformatted or
  removed.
- Other dispatcher info-log lines elsewhere in
  `src/main.cpp` (e.g. `run_render_aovs`,
  `run_render_optix_pathtrace`,
  `run_scene_info`'s validator block) are
  byte-identical.

### 4.7 Other audits / plans

- `docs/PATH_TRACER_POLISH_PLAN.md`: optionally add
  a one-line "PT-P.12 shipped" note at the top of
  §4.4. NOT required; the implementer may also add
  the entry to the plan's change log if one is
  introduced.
- `docs/PATH_TRACER_POLISH_AUDIT.md`,
  `docs/PATH_TRACER_POLISH_STEP_1_AUDIT.md`,
  `docs/PATH_TRACER_POLISH_SAMPLE_COUNT_CAP_AUDIT.md`,
  every `PATH_TRACER_POLISH_*_TASK.md`: NO edits.
- `tools/verify_cuda_host.py`: NO changes (the
  runner exercises `--render-pathtrace`; the new
  log line shows up in stderr automatically when a
  CUDA host invokes the runner).

---

## 5. PASS criteria

The implementation slice passes when ALL of the
following hold:

### 5.1 Build

- `cmake --build build` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=OFF): clean
  build, zero new warnings.
- `cmake --build build-ON` (audit host,
  RR_ENABLE_CUDA=OFF, RR_ENABLE_OPTIX=ON): clean
  build, zero new warnings.

### 5.2 Tests

- `ctest --output-on-failure` from `build`: 7/7
  PASS.
- `ctest --output-on-failure` from `build-ON`: 8/8
  PASS.
- Test counts are unchanged from PT-P.6 / PT-P.9
  (the slice does not add a ctest binary per §3.1).

### 5.3 Source diff size

- `src/pathtracer/PathTracer.h` diff: ~6-10 added,
  0 deleted (one paragraph appended to an existing
  doc-comment).
- `src/main.cpp` diff: ~3-6 added, 0 deleted (one
  new `Logger::info` call). If the implementer
  introduces a new format helper, ~10-15 added /
  0 deleted is acceptable; the BUILD_PLAN entry
  flags the choice.
- Anything LARGER than 25 added across both files
  flagged in the BUILD_PLAN entry as a deviation
  (precedent: PT-P.6 / PT-P.9 deviation notes are
  the templates).

### 5.4 No-touch invariants

`git diff` after the slice MUST show zero bytes
changed in:

- `src/cuda/`
- `src/optix/`
- `src/renderer/`
- `src/pathtracer/PathTracer.cpp`
- `src/core/CommandLine.{h,cpp}`
- `src/io/`
- `src/scene/`
- `src/material/`
- `src/lighting/`
- every `*.rrscene` file under `scenes/`
- every `tests/*.cpp` file
- `tools/verify_cuda_host.py`
- `CMakeLists.txt`

Verifiable by:

```
git diff -- \
  src/cuda/ src/optix/ src/renderer/ \
  src/pathtracer/PathTracer.cpp \
  src/core/ src/io/ src/scene/ src/material/ \
  src/lighting/ scenes/ tests/ \
  tools/verify_cuda_host.py CMakeLists.txt \
  | wc -l
=> 0
```

### 5.5 Behavioural smoke (audit host)

- `./build/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene` continues to emit
  the documented "requires CUDA" audit-host
  fallback byte-identically with the pre-PT-P.12
  baseline. The new info-log line is unreachable
  on the audit host (the dispatcher returns from
  the `--render-pathtrace requires CUDA` branch
  before reaching the post-render block); the
  smoke confirms that fallback path is unchanged.
- `./build-ON/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene`: same.
- `./build/bin/RelativityRender --scene-info
  scenes/test_textured_material.rrscene`: emits the
  TEX-P.6 fixture's expected three-case log
  sequence byte-identically (one Case 1 info + two
  Case 3 warnings; `fixups applied: 2`). Confirms
  zero PT-P.12 ripple onto the texture validator.

### 5.6 Documentation

- `docs/BUILD_PLAN.md` carries a new slice-closing
  entry matching the established PT-P.x format
  (Scope / What ships / What does NOT change /
  Behaviour matrix / Master rule compliance /
  Verified at the build).
- The entry references
  `docs/PATH_TRACER_POLISH_PLAN.md` §4.4 + this
  task file as the source of the specification.
- The PT-P.6 / PT-P.9 entries' behaviour matrices
  are the templates.

### 5.7 Master rule compliance

- Build incrementally (rule 1) + every step
  compilable (rule 2): both audit-host configs
  green.
- No fake stubs (rule 3): the doc-comment names a
  real path-tracer behaviour (the
  `environment_intensity == 0` zero-radiance
  fallback) that the kernel honours via existing
  arithmetic.
- No CPU per-pixel work (rule 5/7): the polish
  touches host-side documentation + a single
  info-log call; zero changes in the per-pixel
  computation graph.
- Update BUILD_PLAN (rule 8): the slice-closing
  entry.

---

## 6. Out-of-scope (deferred to later PT-P.x slices)

The following items from
`docs/PATH_TRACER_POLISH_PLAN.md` are explicitly NOT
part of this task; they have their own task
definitions when sequenced:

- §4.1 RNG stability (key-mix collision audit;
  changes every `pathtrace_spp_*.ppm` byte-exactly).
- §4.5 Emission handling (`is_emissive` helper +
  CUDA path-tracer kernel branch). Touches the
  kernel; sequence after §4.4 lands so the
  doc-comment polish is in place first.
- §4.7 Firefly clamp placeholder (default-off field
  on `PathTraceConfig` + matching kernel guards on
  BOTH the CUDA and OptiX path-trace raygens).
  Largest remaining surface.

The OptiX dispatcher's environment-fallback echo
(symmetric to §2.2 but for `run_render_optix_pathtrace`)
is also explicitly DEFERRED — the OptiX info-log
shape diverges from the CUDA dispatcher's, and a
matching slice can land separately once the CUDA
side has the established idiom.

Per `PATH_TRACER_POLISH_PLAN.md` §5 +
`PATH_TRACER_POLISH_AUDIT.md` §7's sequencing, the
recommended order after §4.4 is:

1. §4.5 (emission handling kernel branch).
2. §4.7 (firefly clamp; touches both backends).
3. §4.1 (RNG stability; changes every PPM
   byte-exactly).

PT-P.11 (this task definition) and PT-P.12 (the
implementation slice) are the only PT-P.x slices
currently scheduled. After PT-P.12 lands, the
operator chooses the next polish item from the
plan's §4 list, or pivots to a different polish
arc / triggers the CUDA-host verification run that
flips the BLOCKED rows from PT-P.4 / PT-P.7 /
PT-P.10 to PASS.

---

## 7. Why §4.4 is the safest viable next slice

Five reasons (mirroring the PT-P.5 / PT-P.8
structure):

### 7.1 PT-P.10 audit verdict was clean

`docs/PATH_TRACER_POLISH_SAMPLE_COUNT_CAP_AUDIT.md`
§9 records overall PASS, zero REPAIR items, BLOCKED
rows carried forward to a CUDA-host run. The path
tracer is in a known-good baseline post-PT-P.9.

### 7.2 The change is the smallest remaining viable slice

`PATH_TRACER_POLISH_PLAN.md` §4.4 estimates "~5 lines
(1 doc, 4 logging)". The other remaining items are
larger (§4.5 touches the kernel; §4.1 ripples
through every PPM; §4.7 touches both backends). §4.4
is the path-tracer equivalent of the
texture-polish-arc's TEX-P.5-style doc-comment
extension — pure clarity work with zero behavioural
risk.

### 7.3 No new pattern is required

PT-P.6 / PT-P.9 established the
"`PathTraceConfig` validation prelude" idiom. §4.4
uses neither validation nor a clamp — it is purely
additive (one doc-comment paragraph, one info-log
line). No new pattern needs to be invented; the
existing `Logger::info` shape in
`run_render_pathtrace` is the template.

### 7.4 The dispatcher idiom is well-defined

`run_render_pathtrace`'s post-render block already
has four `Logger::info` lines with a consistent
columnar format ("right-padded label : value").
Adding a fifth line is a one-instruction insertion
that follows the same shape; no operator confusion
about whether the new line is part of a different
log block.

### 7.5 The OptiX side is left untouched

§4.4 is CUDA-dispatcher-only. The OptiX side's
post-render block has a different shape (it iterates
spp counts via a `kRuns`-equivalent and emits
"OptiX pathtrace (spp=N): ..."-style lines). Touching
it is OUT OF SCOPE for the smallest viable §4.4
slice; a future symmetric polish can handle the
OptiX dispatcher independently. This keeps PT-P.12's
diff small and its blast radius CUDA-only — same
discipline PT-P.6 / PT-P.9 followed.

---

## 8. Reference: the planned post-PT-P.12 dispatcher output

For an operator running
`./build/bin/RelativityRender --render-pathtrace
scenes/test_full_scene.rrscene` on a CUDA host, the
expected post-render info log per spp run becomes:

```
[INFO] pathtrace spp=1 : 1280x720 in 12.345 ms (GPU time)
[INFO] scene file       : scenes/test_full_scene.rrscene
[INFO] framebuffer      : 1280x720 (from render_settings)
[INFO] pathtrace        : 1 spp, 4 bounces, 4 sphere(s), 5 material(s), 3 light(s), 1 mesh(es)
[INFO] environment      : [0.550000, 0.700000, 1.000000] * 0.300000
[INFO] wrote pathtrace_spp_1.ppm: ...
```

The new line is the fifth (between `pathtrace` and
`wrote ...`). For the spp=16 second run the same
five-line block is emitted again, with `pathtrace
        : 16 spp, ...`.

For an operator who custom-constructs a
`PathTraceConfig{ environment_intensity = 0.0f }`,
the new line would read:

```
[INFO] environment      : [0.550000, 0.700000, 1.000000] * 0.000000
```

Confirming the doc-comment's claim that the kernel
sees a zero-intensity fallback (and therefore
contributes zero radiance per miss). The operator
sees this immediately rather than having to read
source.

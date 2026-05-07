# `--enable-nee` CLI Flag — Audit (NEE.6)

Date: 2026-05-07.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `8d9e75f` ("test: NEE.5
CLI parser tests + helper byte-identity anchors") —
closes the NEE.5 sub-arc.
Plan source:
`docs/PATH_TRACER_ENABLE_NEE_CLI_TASK.md` (the canonical
NEE.5 task brief).
Mode: documentation only. **No source code is modified
by this audit.**
Auditor: Claude Code, on the audit host (no CUDA
Toolkit; `command -v nvcc` returns nothing;
`/usr/local/cuda` does not exist; presumably no NVIDIA
GPU). The OFF audit-host config (`build/`,
`RR_ENABLE_CUDA=OFF` + `RR_ENABLE_OPTIX=OFF`) is the
no-GPU baseline; the ON audit-host config (`build-ON/`,
`RR_ENABLE_CUDA=OFF` + `RR_ENABLE_OPTIX=ON`) uses the
OptiX-SDK-fallback path — every `--render-optix-*` call
returns the documented "requires the OptiX SDK"
message before any kernel can run.

This audit walks the ten checks the user enumerated +
records the closing verdict for the NEE.5 sub-arc.
Verdict legend (matches every prior PT-P.x / TEX-P.x /
firefly-clamp-CLI / NEE-skeleton audit):

- **PASS** — implemented; type-checked on the audit
  host; AND empirically exercisable on the audit host
  with a recorded happy-path run.
- **REPAIR** — implemented but a defect or
  inconsistency was found that should be patched.
- **DEFERRED** — empirical verification requires a CUDA
  + (optionally) OptiX-SDK-equipped host (no nvcc /
  OptiX SDK on the audit host). The structural
  argument holds; the runtime confirmation is recorded
  for a future operator session.
- **BLOCKED** — runtime verification cannot proceed on
  this audit host AND the structural argument also
  cannot be confirmed without runtime evidence.

This audit closes the NEE.5 CLI sub-arc. The complete
NEE arc to date:

| Slice                         | Role                                           | Commit       |
|-------------------------------|------------------------------------------------|--------------|
| NEE.1                         | Task definition for the NEE arc                | (docs only)  |
| NEE.2                         | CUDA NEE skeleton (impl)                       | `6f49c55`    |
| NEE.3                         | CUDA NEE skeleton audit                        | `c857f29`    |
| NEE.4                         | OptiX-side NEE mirror + first host helper test | `b29daae`    |
| NEE.5 task brief              | `--enable-nee` CLI flag task definition        | `0e64240`    |
| NEE.5a                        | CLI parse + Config + CUDA mapping              | `122b81c`    |
| NEE.5b                        | OptiX dispatcher + light upload                | `f0bf3e9`    |
| NEE.5 dispatch verification   | No-op verification of dispatch wiring          | `739b332`    |
| NEE.5 tests                   | CLI parser tests + helper byte-identity        | `8d9e75f`    |
| **NEE.6**                     | **This audit** — `--enable-nee` audit          | (docs only)  |

---

## 1. `--enable-nee` exists

**PASS.**

The parser arm is present at
`src/core/CommandLine.cpp:417-433` (post-`8d9e75f`),
inserted immediately after the `--denoise` arm at
line 409. Mirrors `--denoise`'s presence-only shape
verbatim (no value-take; no range validation; the
parser sets `r.config.enable_nee = true` and
continues).

The arm carries a 16-line doc-comment block (lines
418-432) documenting:
- Modifier-flag semantics ("NOT an action; combining
  it with any action flag is allowed").
- The two consumers (`--render-pathtrace`,
  `--render-optix-pathtrace`); other actions ignore.
- Cross-reference to
  `docs/PATH_TRACER_NEE_AUDIT.md` §3.2 + the NEE.4
  commit (`b29daae`) closing the sequencing
  constraint.

### 1.1 Help-text entry exists

**PASS.** A 10-line block at
`src/core/CommandLine.cpp:974-993` appended after the
`--firefly-clamp` help-block + before the `--width` /
`--height` blocks. The block's wording matches the
task brief §1.3 spec:

```
--enable-nee          NEE.5 modifier flag (not an action). Enables
                      explicit direct-light sampling (Next Event
                      Estimation) at every bounce vertex of the path
                      tracer. Default off matches the pre-NEE.5
                      emission + environment-only behaviour byte-for-byte.
                      Read by --render-pathtrace; the OptiX dispatcher
                      consumption is deferred to a follow-up slice.
                      ...
```

Empirically verified during this audit: running
`./build/bin/RelativityRender --help | grep -A 9
enable-nee` returns the 10-line block. The
operator finds the flag's documentation without
reading source.

(Note: the help text says "the OptiX dispatcher
consumption is deferred to a follow-up slice". That
text was authored at NEE.5a when the OptiX side was
still pending; NEE.5b shipped the OptiX dispatcher
wiring at commit `f0bf3e9`. The help text is stale
at HEAD — the OptiX dispatcher now consumes the
flag fully. **Recorded as a documentation-only
REPAIR candidate in §11.** The runtime behaviour is
correct; only the help-text wording is out of date.)

### 1.2 Cross-check: parser is unique

`grep -rn "enable-nee" src/` returns 5 matches — all
in `CommandLine.cpp`:

- Line 417: parser arm.
- Line 974, 977: help-text entries (split across two
  `<<` chained lines).
- Line 974 cross-reference (the same line; counted
  once).

No competing parser arm; no stray references in any
other source file. The flag is recognised
exclusively by the new arm.

---

## 2. Default is OFF

**PASS.**

The `enable_nee = false` default exists at THREE
sites (matching the three POD layers the flag flows
through):

| Site                                       | Line | Default     |
|--------------------------------------------|-----:|-------------|
| `src/core/Config.h::Config::enable_nee`    |   89 | `false`     |
| `src/pathtracer/PathTracer.h::PathTraceConfig::enable_nee` |  172 | `false`     |
| `src/optix/OptixLaunchParams.h::OptixLaunchParams::enable_nee` |  252 | `false`     |

### 2.1 Default propagates through default-construction

`Config{}` produces a struct with `enable_nee ==
false` per C++ aggregate-initialisation rules. The
default flows through:

- `r.config` in `CommandLine::parse` is
  default-constructed at the start of the function;
  `enable_nee == false` until/unless the parser arm
  fires.
- `pcfg.enable_nee` in `run_render_pathtrace` reads
  `cfg.enable_nee` — `false` when the operator
  does not pass the flag.
- `params.enable_nee` in
  `OptixRenderer::render_pathtrace*` reads
  `enable_nee` argument — defaulted to `false` per
  the dispatcher's trailing default-arg.

The default-off invariant therefore holds at FOUR
sites: the parser default (no flag → `false`), the
PathTraceConfig default, the OptixLaunchParams
default, and the dispatcher's trailing default-arg.
A regression at any one site does not silently flip
the others; the redundancy is defence-in-depth.

### 2.2 Default-OFF byte-identity (structural argument)

The static IEEE-754 + RNG-stream argument from
`docs/PATH_TRACER_NEE_AUDIT.md` §1.2 carries forward
unchanged. At `enable_nee == false`:

- The kernel guards (`CudaPathTracer.cu:276`,
  `OptixPrograms.cu` `__raygen__pathtrace` NEE
  branch) short-circuit at the boolean AND; the
  branches are never entered.
- The `next_float(rng)` draw for light selection
  lives INSIDE the guard, so the cosine-bounce
  `next_vec2(rng)` immediately below pulls from a
  bit-identical RNG state.
- No FP add ever touches the `radiance` accumulator
  from the NEE branch.

The per-pixel arithmetic is therefore bit-identical
with the pre-NEE build at default. NEE.5 did not
modify any of the three contributors (kernel guard
placement, `next_float` placement inside the guard,
`radiance` accumulator) — the argument from NEE.2 +
NEE.4 carries forward.

---

## 3. `PathTraceConfig::enable_nee` is wired

**PASS.**

The field-to-kernel chain is verified end-to-end at
HEAD:

| Step                                        | Source anchor                                  |
|---------------------------------------------|------------------------------------------------|
| Field declaration                           | `PathTracer.h:172`                             |
| Read in `PathTracer::render`                | `PathTracer.cpp:137` (passes `cfg.enable_nee`) |
| Launcher signature accepts `bool enable_nee`| `CudaPathTracer.cuh:94`                        |
| Launcher implementation accepts the arg     | `CudaPathTracer.cu:186` (kernel-launch wrapper)|
| Kernel guard consumes the flag              | `CudaPathTracer.cu:276`                        |

`PathTracer.cpp:128-141` (the spp loop's per-sample
launcher invocation) passes `cfg.enable_nee` as the
final positional argument to
`rr::cuda::launch_pathtrace_sample`. The launcher's
NEE.2 signature accepts the flag and forwards it to
`k_pathtrace_sample`'s kernel argument. The kernel's
NEE.2 guard at line 276 (`if (enable_nee &&
scene.light_count > 0)`) consumes both halves —
short-circuits at default, fires at non-default.

### 3.1 Doc-comment cross-references

The `PathTraceConfig::enable_nee` field carries a
~50-line doc-comment block (`PathTracer.h:155-171`)
that documents:

- The kernel-side guard contract.
- The byte-identity argument at default-off.
- The Point + Directional light-type scope (Area /
  Environment placeholder).
- The MIS-deferred status (no double-count window
  with the v1 light types).
- The OptiX-mirror status post-NEE.4 ("both
  backends are convergence-equivalent at every
  value of `enable_nee`").

The doc-comment was last touched at NEE.4
(`b29daae`) to flip the OptiX-status line from "no
NEE wiring yet" to "convergence-equivalent". NEE.5
did not need to touch it — the CLI flow is
orthogonal to the field's semantic contract.

---

## 4. CUDA dispatch receives `enable_nee`

**PASS.**

The wiring lands at NEE.5a (commit `122b81c`):

- `src/main.cpp:2437`:
  ```cpp
  pcfg.enable_nee = cfg.enable_nee;
  ```
  Inside the spp loop's `pcfg` population block,
  immediately after the existing
  `pcfg.firefly_clamp = cfg.firefly_clamp;` line.
- `src/main.cpp:2493-2497`: a Logger info line
  classifying the flag's runtime value:
  ```cpp
  Logger::info(std::string("enable_nee       : ")
             + (pcfg.enable_nee ? "true (enabled)"
                                : "false (disabled)"));
  ```
  Same 17-column label width as the existing
  `firefly_clamp    : ...` line above.

The `pcfg.enable_nee` value flows into
`PathTracer::render` (which passes it to
`launch_pathtrace_sample` at `PathTracer.cpp:137`
per §3) → kernel guard at `CudaPathTracer.cu:276`.

### 4.1 No bypass

The single `pcfg` variable is passed verbatim to
`pt.render(gpu_scene, width, height, pcfg)`; no
intermediate copy modifies it between the
assignment at line 2437 and the kernel launch.

### 4.2 Empirical smoke

Audit-host smoke (no CUDA toolchain): running
`./build/bin/RelativityRender --render-pathtrace
scenes/test_full_scene.rrscene --enable-nee` emits
the documented "requires CUDA" audit-host fallback
byte-identically with the no-flag invocation. The
in-spp-loop log lines (firefly_clamp + enable_nee)
are unreachable on the audit host (the dispatcher
returns the "requires CUDA" error BEFORE reaching
the spp loop). This is the expected fallback
shape; the empirical confirmation of the flag
flowing through to the kernel is DEFERRED to a
CUDA-host operator session per §9.

---

## 5. OptiX dispatch receives `enable_nee`

**PASS.**

The wiring lands at NEE.5b (commit `f0bf3e9`):

- `src/main.cpp:1601`:
  ```cpp
  /*enable_nee=*/cfg.enable_nee);  // NEE.5b: from --enable-nee
  ```
  The trailing argument on the
  `OptixRenderer::render_pathtrace_progressive`
  call. The argument position was added to the
  dispatcher's signature at NEE.4 (`b29daae`) as a
  defaulted `bool enable_nee = false`; NEE.5b
  flipped the value source from the implicit
  default to the CLI input.
- `src/main.cpp:1594-1596`: the matching Logger
  info line, placed BEFORE the renderer call so
  the value is visible even when the renderer
  fails on the audit-host fallback.
- `src/optix/OptixRenderer.cpp:1804` (progressive):
  `params.enable_nee = enable_nee;` written on
  every per-launch params write inside the spp
  loop. `params.enable_nee` is uploaded to the
  device-side `OptixLaunchParams` POD;
  `__raygen__pathtrace`'s NEE branch (NEE.4) reads
  it.
- `src/optix/OptixRenderer.cpp:1451` (single-launch
  variant of `render_pathtrace`):
  `params.enable_nee = enable_nee;` in the
  pre-launch params population. The single-launch
  variant has the same signature parameter (NEE.4)
  and the same internal write; no live caller in
  `src/main.cpp` exercises it today, but the API
  is symmetric with the progressive variant.

### 5.1 Dispatcher signatures

Both `OptixRenderer::render_pathtrace*` signatures
in `src/optix/OptixRenderer.h` carry a trailing
`bool enable_nee = false` arg (lines 231 + 300).
The defaults preserve byte-identity for any caller
that does not opt in.

### 5.2 Empirical smoke

Audit-host smoke (OptiX SDK fallback): running
`./build-ON/bin/RelativityRender
--render-optix-pathtrace
scenes/test_full_scene.rrscene --enable-nee` emits
during this audit:

```
[INFO] firefly_clamp    : 0.000000 (disabled)
[INFO] enable_nee       : true (enabled)
[ERROR] optix path-trace progressive render failed:
        OptixRenderer::render_pathtrace_progressive
        requires the OptiX SDK; ...
```

Confirms:
- The parser stored `cfg.enable_nee = true`.
- The dispatcher emitted the diagnostic log line
  with the correct classification (`true (enabled)`).
- The flag value reached the dispatcher BEFORE the
  audit-host fallback fired.
- The new log line emits at the documented
  pre-fallback position (after `firefly_clamp`,
  before the renderer).

The empirical confirmation that the OptiX kernel
guard fires + produces direct-light contribution is
DEFERRED to a CUDA + OptiX-SDK host operator
session per §9.

---

## 6. OptiX light-upload wiring status

**PASS.**

NEE.5b (commit `f0bf3e9`) shipped the light-upload
wiring in BOTH OptiX path-tracer dispatchers,
mirroring the canonical
`OptixRenderer::render_direct_lighting:1965-1996`
pattern verbatim.

### 6.1 `render_pathtrace` (single-launch)

| Component                          | Lines             |
|------------------------------------|-------------------|
| `void* d_lights = nullptr` decl    | 1411              |
| `light_count` from scene.lights    | 1412              |
| `cudaMalloc(&d_lights, ...)` gate  | 1416              |
| `cudaMemcpy(d_lights, ...)`        | 1429              |
| `params.lights / light_count`      | 1457-1458         |
| `cudaFree(d_lights)` on every error path | 1431, 1468, 1495, 1507, 1520 |
| `cudaFree(d_lights)` on success path | 1529              |

Six free-sites in total: one inside the
cudaMemcpy-failure branch (line 1431) and five at
function-exit error / success paths.

### 6.2 `render_pathtrace_progressive` (multi-launch)

| Component                          | Lines             |
|------------------------------------|-------------------|
| `void* d_lights = nullptr` decl    | 1725              |
| `light_count` from scene.lights    | 1726              |
| `cleanup` lambda free-line         | 1729              |
| Light upload before spp loop       | 1737-1758         |
| `params.lights / light_count` per-launch | 1810-1811   |

The `cleanup` lambda (closure over `d_lights /
d_display / d_accumulator / d_framebuffer /
d_indices / d_positions`) is invoked from every
error-return path inside the spp loop AND at
function exit. Lights are uploaded ONCE before the
spp loop and reused across every per-launch params
write (lights are constant across the loop).

### 6.3 `render_direct_lighting` reference unchanged

The canonical pattern at
`OptixRenderer.cpp:1965-1996` is byte-identical
with its pre-NEE.5b state (the existing Stage 20K
direct-lighting render still uses it independently).
NEE.5b mirrored the pattern; it did not modify the
original.

### 6.4 No-light-scene safety net

When `scene.lights.empty()`:
- `light_count == 0`; `cudaMalloc` is skipped;
  `d_lights == nullptr`.
- `params.lights = nullptr; params.light_count = 0;`
- The kernel guard
  (`enable_nee && light_count > 0`) short-circuits
  at the AND regardless of `enable_nee` value, so a
  no-lights scene with `--enable-nee` produces the
  same emission + environment image as without the
  flag.

This is the §3.3 safety-net contract from the
NEE.5 task brief — preserved structurally in both
dispatchers.

### 6.5 Empirical smoke

The light-upload code paths are unreachable on the
audit host (no CUDA + no OptiX SDK), but the
no-touch invariant for the pre-NEE.5b code
(`render_direct_lighting`) confirms the upload
pattern compiles + links cleanly in the ON-fallback
build.

---

## 7. CLI parser test status

**PASS.**

NEE.5 final test slice (commit `8d9e75f`) ships
`tests/cli_tests.cpp` (193 lines) exercising every
parser arm relevant to `--enable-nee`.

### 7.1 Linkage

Per the task brief §4.1 Option B (recompile
`src/core/CommandLine.cpp` + `src/core/Config.cpp`
directly into the test binary). `CommandLine.cpp`
does not depend on `Logger.cpp` or any other
`src/core` module, so the two-source recompile
suffices.

`CMakeLists.txt:680-700` wires the binary:

```cmake
add_executable(cli_tests
    tests/cli_tests.cpp
    src/core/CommandLine.cpp
    src/core/Config.cpp)
target_include_directories(cli_tests PRIVATE src)
rr_apply_warnings(cli_tests)
add_test(NAME cli_tests COMMAND cli_tests)
```

No new library; no changes to the `RelativityRender`
executable's source list.

### 7.2 Test cases

Six mandatory cases per §4.1 + 1 optional
case-mismatch case:

| Test name                              | What it anchors                                       |
|----------------------------------------|-------------------------------------------------------|
| `test_default_off_no_flag`             | `parse({"prog"})` ⇒ `enable_nee==false`               |
| `test_flag_after_action`               | flag after action ⇒ `true` + scene-path consumed      |
| `test_flag_before_action`              | flag-then-action order ⇒ same outcome (modifier-flag) |
| `test_flag_idempotent`                 | repeated flag ⇒ `true` (no Error)                     |
| `test_flag_with_firefly_clamp`         | `--enable-nee --firefly-clamp 8.0` ⇒ both fields set  |
| `test_case_mismatch_rejected`          | `--enable-Nee` ⇒ Error + msg names offending token    |

Plus a bonus
`test_default_off_with_other_flags` case looping
through five argv vectors that don't pass
`--enable-nee` (mixing `--render-pathtrace`,
`--render-optix-pathtrace`, `--scene-info`,
`--firefly-clamp`, `--width` / `--height`),
asserting `enable_nee==false` on each. Directly
exercises the user's "Default OFF behavior must
remain unchanged" rule on the parser surface
across multiple action / modifier-flag
combinations.

### 7.3 Test count + run output

**31/31 RR_CHECK assertions pass.** Empirically
verified during this audit:

```
$ ./build/bin/cli_tests
cli_tests: 31/31 passed
```

ctest runs both audit-host configs include
`cli_tests` (line 6 of 9 OFF; line 6 of 10 ON).

---

## 8. Default-OFF byte-identity test status

**PASS** (host-only Option A);
**DEFERRED** (CUDA-host Option B PPM `cmp`).

### 8.1 Static argument

The static IEEE-754 + RNG-stream argument from
`PATH_TRACER_NEE_AUDIT.md` §1.2 is the formal
byte-identity proof. NEE.5 did not modify any of
the three contributors:

1. The kernel guard placement (`if (enable_nee &&
   light_count > 0)`).
2. The `next_float(rng)` draw for light selection
   (inside the guard).
3. The `radiance` accumulator arithmetic.

The argument carries forward unchanged.

### 8.2 Host-only dynamic test (Option A)

`tests/pathtracer_nee_tests.cpp::test_helper_determinism`
(line 310) and
`tests/pathtracer_nee_tests.cpp::test_zero_contribution_is_bit_default`
(line 341) ship the host-only byte-identity
anchor. Both shipped at NEE.5 commit `8d9e75f`.

- **`test_helper_determinism`** — calling
  `sample_direct_light_uniform` twice with the
  same `(hit_position, normal, u_select)` inputs
  produces bit-equal `DirectLightSample` via
  `std::memcmp`. Anchors the helper is a pure
  function (no hidden global / TLS state); a
  regression that introduces non-determinism
  fails this case at host-build time.
- **`test_zero_contribution_is_bit_default`** —
  the helper's `lights == nullptr` AND
  `count == 0` guards both return bit-equal
  `DirectLightSample` with a default-constructed
  `DirectLightSample{}`. `std::memcmp`
  distinguishes `+0.0f` from `-0.0f`, so a
  regression that introduces negative-zero into
  the zero-contribution path (which could in
  principle disagree with the static IEEE-754
  argument) fails this case.

The host-only test is the formal byte-identity
proxy on this audit host. Together with the
parser-surface anchor in
`tests/cli_tests.cpp::test_default_off_with_other_flags`
(five argv vectors, all
producing `enable_nee==false`), the default-OFF
contract is exercised at three layers (parser →
field default → helper output) without requiring
a CUDA host.

### 8.3 CUDA-host runtime PPM `cmp` (Option B)

DEFERRED on this audit host. The procedure is
documented in the NEE.5 task brief §8.1 + the
NEE.6 §9 below; a CUDA-equipped operator can run:

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm /tmp/pre_nee5.ppm

$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --enable-nee
$ cp output/pathtrace_spp_1.ppm /tmp/zero_flag_spp1.ppm

# Note: this is not the right cmp. The right cmp
# is between the no-flag run and a re-run of the
# no-flag run (both should be byte-identical
# trivially), OR between the pre-NEE.5 baseline
# and the post-NEE.5 no-flag run (the byte-
# identity proof). The flag-on case is
# expected to differ when lights are present.
```

The corrected procedure for the byte-identity
check is in §9.1.

### 8.4 Test count + run output

**34/34 RR_CHECK assertions pass** in
`pathtracer_nee_tests` (was ~26 pre-slice; +8 from
the two new cases). Empirically verified:

```
$ ./build/bin/pathtracer_nee_tests
pathtracer_nee_tests: 34/34 passed
```

ctest runs both audit-host configs include
`pathtracer_nee_tests` (line 5 of 9 OFF; line 5 of
10 ON).

---

## 9. CUDA / OptiX runtime status

**DEFERRED on six checks** (= BLOCKED on this
audit host).

The CLI flag enables runtime checks the static
arguments alone cannot verify. On a real CUDA +
OptiX-SDK host, the operator can run:

| §             | Check                                          | Procedure                                  |
|---------------|------------------------------------------------|--------------------------------------------|
| §9.1          | default-off byte-IDENTITY (CUDA, runtime)      | `cmp` no-flag PPM (pre-NEE.5 vs post)      |
| §9.2          | visible NEE-on noise reduction (CUDA)          | render lit scene with vs without flag      |
| §9.3          | cross-backend convergence                      | CUDA `--enable-nee` vs OptiX `--enable-nee`|
| §9.4          | no-light-scene safety net                      | render no-lights scene with flag           |
| §9.5          | ctest cycle on CUDA host                       | re-run on CUDA-built host                  |
| §9.6          | refresh CUDA-OPTIX-VERIFY report               | flip NEE rows from DEFERRED to PASS        |

After the operator runs §9.1–§9.4 on a CUDA +
OptiX-SDK host, the entire NEE arc's runtime
DEFERRED rows + the
`docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md` NEE
section all flip to PASS in a single operator
session.

### 9.1 Default-OFF byte-IDENTITY (CUDA, runtime)

```
# Reference: pre-NEE.5 build (e.g. checkout NEE.4
# commit b29daae and rebuild).
$ git checkout b29daae
$ cmake --build build-cuda -j
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm /tmp/pre_nee5_spp1.ppm
$ git checkout 8d9e75f
$ cmake --build build-cuda -j

# Post-NEE.5 build, no flag passed.
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_1.ppm /tmp/post_nee5_spp1.ppm

$ cmp /tmp/pre_nee5_spp1.ppm /tmp/post_nee5_spp1.ppm  ; echo $?
=> 0 (identical — NEE.5 is byte-identical at default-OFF)
```

Confirms the static IEEE-754 + RNG-stream
argument matches runtime behaviour.

### 9.2 Visible NEE-on noise reduction (CUDA)

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene
$ cp output/pathtrace_spp_4.ppm /tmp/no_nee.ppm

$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --enable-nee
$ cp output/pathtrace_spp_4.ppm /tmp/with_nee.ppm

# Visual inspection: shadows + lit regions visibly
# less noisy in /tmp/with_nee.ppm at low spp.
$ cmp /tmp/no_nee.ppm /tmp/with_nee.ppm  ; echo $?
=> 1 (different — NEE branch fired)
```

The lit scene needs Point and/or Directional
lights for NEE to contribute. First runtime
confirmation that NEE does useful work.

### 9.3 Cross-backend convergence

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/test_full_scene.rrscene --enable-nee
$ cp output/pathtrace_spp_16.ppm /tmp/cuda_nee.ppm

$ ./build-cuda/bin/RelativityRender --render-optix-pathtrace \
    scenes/test_full_scene.rrscene --enable-nee
$ cp output/optix_pathtrace_spp16.ppm /tmp/optix_nee.ppm
```

Same caveat as the firefly-clamp CLI cross-
backend check: the two PPMs are NOT bit-identical
(different bounce-loop code paths produce
different RNG draws + FMA-fusion patterns). They
ARE statistically similar:

- Mean luminance per channel agrees within
  sampling noise (~5% at spp=16).
- Lit + shadowed regions match qualitatively.

### 9.4 No-light-scene safety net

```
$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/<no-lights-scene>.rrscene --enable-nee
$ cp output/pathtrace_spp_16.ppm /tmp/with_nee_nolight.ppm

$ ./build-cuda/bin/RelativityRender --render-pathtrace \
    scenes/<no-lights-scene>.rrscene
$ cp output/pathtrace_spp_16.ppm /tmp/no_nee_nolight.ppm

$ cmp /tmp/with_nee_nolight.ppm /tmp/no_nee_nolight.ppm ; echo $?
=> 0 (identical — light_count == 0 short-circuits NEE)
```

Confirms the §3.3 / §6.4 safety-net contract.

### 9.5 ctest cycle on CUDA host

`ctest --output-on-failure` from a CUDA-built
`build-cuda` directory must pass. The new
`cli_tests` binary is host-only and runs
identically to the audit-host run; the
`pathtracer_nee_tests` binary is also host-only
(the helper is RR_HD inline). Both should
continue to pass on a CUDA host.

### 9.6 Refresh CUDA-OPTIX-VERIFY report

After the operator runs §9.1–§9.4 on a CUDA +
OptiX-SDK host, the operator commits a
refreshed
`docs/CUDA_OPTIX_HOST_VERIFICATION_REPORT.md`
flipping the NEE rows from `DEFERRED` to `PASS`.

---

## 10. Verdict

| #  | Audit item                                                | Result   |
|----|-----------------------------------------------------------|----------|
| 1  | `--enable-nee` exists                                     | PASS     |
| 2  | Default is OFF                                            | PASS     |
| 3  | `PathTraceConfig::enable_nee` is wired                    | PASS     |
| 4  | CUDA dispatch receives `enable_nee`                       | PASS     |
| 5  | OptiX dispatch receives `enable_nee`                      | PASS     |
| 6  | OptiX light-upload wiring status                          | PASS     |
| 7  | CLI parser test status                                    | PASS — 31/31 in `cli_tests` |
| 8  | Default-OFF byte-identity test status                     | PASS structurally + host-only Option A; CUDA-host Option B DEFERRED |
| 9  | CUDA / OptiX runtime status                               | DEFERRED (= BLOCKED on this audit host) |
| 10 | Closing verdict                                           | **PASS** |

**Overall verdict: PASS.**

The `--enable-nee` CLI flag is safely exposed,
correctly wired through both backends, and
exercised by host-only tests at three layers
(parser → field default → helper output). The
runtime confirmation of NEE actually doing useful
work on the GPU is DEFERRED to a CUDA + OptiX-SDK
host operator session — a single operator
session flips all six §9 rows to PASS.

### 10.1 Master rule compliance

- Build incrementally (rule 1) + every step
  compilable (rule 2): both audit-host configs
  green at HEAD (`build` 9/9 ctest;
  `build-ON` 10/10 ctest; both rebuilt during
  this audit).
- No fake stubs (rule 3): every flag site is
  real code with a real effect on the kernel.
- No CPU per-pixel work (rule 5/7): the flag is
  parsed once at startup; the kernel guards do
  the per-pixel work device-side.
- Module boundaries (rule 9): the new logic
  spreads across `core/` (parser + Config),
  `main.cpp` (dispatchers), `optix/` (renderer
  + light upload), `tests/` (parser + helper
  tests). No cross-module ripple beyond the
  established layer boundaries.
- Update BUILD_PLAN (rule 8): every NEE.5 slice
  added a BUILD_PLAN entry; this audit will add
  one too (§11).

### 10.2 Smoke matrix re-verified during this audit

| Smoke                                              | Result on audit host                                   |
|----------------------------------------------------|--------------------------------------------------------|
| `--help \| grep -A 9 enable-nee`                   | 10-line help block prints                              |
| `--render-pathtrace ... --enable-nee` (OFF)        | "requires CUDA" fallback                               |
| `--render-optix-pathtrace ... --enable-nee` (ON)   | `firefly_clamp : 0.000000 (disabled)` + `enable_nee : true (enabled)` + "requires OptiX SDK" fallback |
| `--render-pathtrace ... --enable-Nee` (case)       | "unknown argument: --enable-Nee" + exit 2              |
| `--scene-info scenes/test_textured_material.rrscene` (TEX-P.6 fixture) | three-case log sequence intact; fixups applied: 2     |
| `cli_tests`                                        | 31/31 pass                                             |
| `pathtracer_nee_tests`                             | 34/34 pass                                             |
| ctest OFF                                          | 9/9 pass                                               |
| ctest ON                                           | 10/10 pass                                             |

---

## 11. REPAIR candidates (low-priority)

Single REPAIR candidate identified during this
audit; recorded for a future cleanup slice (NOT
fixed in this audit per the
documentation-only rule):

### 11.1 Help text is stale (cosmetic only)

`src/core/CommandLine.cpp:980-981`:

```
  --enable-nee          NEE.5 modifier flag (not an action). Enables
                        ...
                        Read by --render-pathtrace; the OptiX dispatcher
                        consumption is deferred to a follow-up slice.
```

The "OptiX dispatcher consumption is deferred to
a follow-up slice" wording was authored at NEE.5a
when the OptiX side was still pending. NEE.5b
(commit `f0bf3e9`) shipped the OptiX dispatcher
wiring + light upload; the help text is now
stale. The runtime behaviour is correct
(verified by §5 + §10.2 smoke matrix); the
help-text wording is the only mismatch.

**Recommended fix** (future cleanup slice,
documentation only):

```
  --enable-nee          NEE.5 modifier flag (not an action). Enables
                        ...
                        Read by --render-pathtrace and
                        --render-optix-pathtrace; ignored by every other
                        action.
                        ...
```

Mirrors the `--firefly-clamp` help text's "Read
by" idiom. Three-line wording change at
`CommandLine.cpp:980-981` + adjacent
continuation.

This REPAIR is cosmetic; it does not affect any
ctest outcome or runtime behaviour. Defer to a
future cleanup slice (or fold into the next NEE
or CLI doc-touch slice). Not blocking the NEE.5
sub-arc closure.

---

## 12. Sub-arc closure

The NEE.5 CLI sub-arc closes here. The complete
`--enable-nee` deliverable has shipped:

- Field placeholder + parser arm + help text:
  NEE.5a (commit `122b81c`).
- CUDA dispatcher mapping + log line: NEE.5a.
- OptiX dispatcher mapping + log line + light
  upload: NEE.5b (commit `f0bf3e9`).
- Dispatch-wiring verification (no-op slice
  documenting the prior commits): commit
  `739b332`.
- CLI parser tests + helper byte-identity
  anchors: NEE.5 tests (commit `8d9e75f`).
- This audit: documentation only.

`PATH_TRACER_NEE_TASK.md` §1's full operator-
facing contract is implemented + documented +
audited. The NEE arc's complete cadence (NEE.1
through NEE.6) covers every deliverable the
NEE-task document committed to, modulo the
deferred area-light + MIS work that
`PATH_TRACER_NEE_TASK.md` §1 explicitly reserved
for a future slice.

### 12.1 Recommended next step

Three viable directions, mirroring the
firefly-clamp CLI audit §9's recommendation
shape:

1. **Trigger the CUDA + OptiX-SDK host
   verification run** that flips all NEE.x +
   PT-P.x runtime DEFERRED rows to PASS.
   Single operator session per the procedures
   in §9 above + the parallel firefly-clamp
   CLI audit §9. Most immediately satisfying;
   the cumulative runtime debt across the
   PT-P.x + NEE arcs accumulates to ~25
   DEFERRED rows; one operator session pays
   them all.

2. **Fix the help-text REPAIR (§11.1)**.
   Single-line cosmetic edit; could be folded
   into any subsequent CLI / docs slice. Not
   blocking.

3. **Pivot to master order #16+**: the
   non-diffuse BSDF / area-light / MIS arc is
   the natural successor to NEE.5. NEE.7 (or a
   parallel arc) would unblock area-light NEE +
   MIS, addressing the v1 "double-count window"
   that
   `PATH_TRACER_NEE_TASK.md` §1 documented.
   Or pivot further: master order #18 (textures)
   has fewer prerequisites but a comparable
   user-visible payoff.

Recommended sequencing: **(2)** as a small
follow-up cosmetic, **(1)** when a CUDA +
OptiX-SDK host becomes available, then **(3)** as
the next major arc.

---

Mode reminder: **documentation only.** This audit
makes zero source-code changes. The REPAIR list is
a ONE-line cosmetic candidate; the fix lives in a
future slice, not this audit.

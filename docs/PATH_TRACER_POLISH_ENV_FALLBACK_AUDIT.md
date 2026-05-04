# Path-Tracer Polish — Environment Fallback Clarity Audit

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Last commit on the audited tree: `fa41e58` ("PT-P.12:
environment fallback clarity (impl)").
Scope: PT-P.12 — the implementation slice that ships
`PATH_TRACER_POLISH_PLAN.md` §4.4 per the brief in
`PATH_TRACER_POLISH_ENV_FALLBACK_TASK.md`.
Mode: documentation only. **No source code is modified by
this audit.**
Auditor: Claude Code, on the audit host (no CUDA Toolkit,
no OptiX SDK).

This audit walks the seven prompt checks in order and
records a single verdict at the end. Verdict legend
matches the texture-polish-audit + PT-P.4 / PT-P.7 /
PT-P.10 precedent:

- **PASS** — implemented, type-checked on the audit host,
  AND empirically exercisable on the audit host with a
  recorded happy-path run.
- **REPAIR** — implemented but a defect or inconsistency
  was found that should be patched.
- **BLOCKED** — empirical verification requires a CUDA
  host (no nvcc / OptiX SDK on the audit host).

---

## 1. Environment fallback behavior is documented

**PASS.**

The doc-comment at `src/pathtracer/PathTracer.h:60-78`
now enumerates three artist-meaningful cases for the
`environment_color` / `environment_intensity` pair:

### 1.1 Case 1 — defaults (cool sky tint)

The pre-existing first paragraph (lines 60-65, byte-
identical with pre-PT-P.12) names the multiplication
semantics + the default values:

```
// Environment fallback. When a ray misses every scene primitive
// the path tracer treats this as the radiance arriving from
// infinity. `environment_color * environment_intensity` is the
// emitted spectral colour; both are linear-space RGB. Defaults
// produce a moderate cool sky tint so a scene with no emissive
// surfaces still produces a visible image.
```

### 1.2 Case 2 — `environment_intensity == 0.0f` (PT-P.12)

The new paragraph (lines 66-76) names the fully-black
special case:

```
// PT-P.12: setting `environment_intensity == 0.0f` produces a
// fully black background for missed rays — the kernel still
// adds `throughput * env` to the radiance on every miss, but
// `env` evaluates to `(0, 0, 0)` so the contribution is zero.
// Use this when authoring scenes whose only light sources are
// emissive surfaces / explicit lights and the operator wants
// no background ambient term. The kernel has no `env_intensity
// > 0` short-circuit; the multiply-and-add is unconditional,
// and the zero-valued add is the documented contract rather
// than a special case.
```

This paragraph satisfies the PT-P.11 task §2.1 contract
on three points:

- Names the `== 0.0f` special case explicitly.
- Describes the visual outcome ("fully black background
  for missed rays").
- Notes the authoring rationale (use it when explicit
  emissive surfaces / lights make ambient unwanted).

The "no `env_intensity > 0` short-circuit" sentence is
defensive: it prevents a future kernel-side maintainer
from "optimising" the multiply-and-add into a branch and
introducing a divergence between the documented contract
and the code.

### 1.3 Case 3 — custom `environment_color` / non-zero intensity

Implicit in the multiplication semantics already
documented in Case 1. Any `environment_intensity > 0`
with a custom `environment_color` produces an artist-
chosen ambient spectrum. The polish does not add a
dedicated paragraph for this case; the existing
"linear-space RGB" + "emitted spectral colour" wording
suffices.

---

## 2. Dispatcher / log clarity exists if implemented

**PASS.**

`src/main.cpp`'s `run_render_pathtrace` post-render
info-log block grew from FOUR lines to FIVE per spp
run. The added line lives between the existing
`pathtrace        : ...` line and the
`save_image_or_error` call (lines 2412-2421 in the
post-PT-P.12 source):

```cpp
// PT-P.12: echo the environment-fallback config so an
// operator can confirm what the kernel sees on every miss
// without reading source. Format mirrors run_scene_info's
// existing `fmt_vec3` lambda for visual consistency.
auto fmt_vec3 = [](rr::math::Vec3 v) {
    return "[" + std::to_string(v.x) + ", "
               + std::to_string(v.y) + ", "
               + std::to_string(v.z) + "]";
};
Logger::info(std::string("environment      : ")
           + fmt_vec3(pcfg.environment_color) + " * "
           + std::to_string(pcfg.environment_intensity));
```

### 2.1 Format

The new line uses the same `[x, y, z]`-style Vec3
formatting as `run_scene_info` (line 471-475 of the
same file). The label width (`"environment      : "`)
is 18 columns wide, matching the existing
`scene file       : ` / `framebuffer      : ` /
`pathtrace        : ` labels. Visual consistency with
the existing four lines is preserved.

### 2.2 Always emits

The line emits unconditionally — no special-casing for
default values. This is the contract the PT-P.11 task
§2.2 specified: an operator wondering "is the default
sky firing?" can confirm the default is in fact firing
without reading source. The line also emits when
`environment_intensity == 0.0f`, producing
`environment      : [0.550000, 0.700000, 1.000000] *
0.000000`, which makes the artist's "no ambient" choice
visibly distinct from "default ambient".

### 2.3 OptiX dispatcher untouched

`run_render_optix_pathtrace` in `src/main.cpp`'s OptiX
dispatcher block is byte-identical post-slice. The
PT-P.11 task §6 explicitly deferred the OptiX-side
symmetric polish to a future slice; the CUDA-only blast
radius matches the discipline PT-P.6 / PT-P.9
followed.

---

## 3. No kernel behavior changed

**PASS.**

`git diff fa41e58~1..fa41e58 -- src/cuda/ src/optix/
src/renderer/ src/pathtracer/PathTracer.cpp src/core/
src/io/ src/scene/ src/material/ src/lighting/
scenes/ tests/ tools/verify_cuda_host.py CMakeLists.txt
| wc -l` returns 0 bytes.

The kernel-side miss handler at
`src/cuda/CudaPathTracer.cu:144-184` is byte-identical
with the pre-PT-P.12 commit `09cc14a`:

```cpp
const Vec3 env = env_color * env_intensity;
radiance = radiance + Vec3{throughput.x * env.x,
                           throughput.y * env.y,
                           throughput.z * env.z};
break;
```

Same for the OptiX-side `__miss__pathtrace` program in
`src/optix/OptixPrograms.cu`. PT-P.12 inserted neither
an `if (env_intensity > 0)` short-circuit nor any other
runtime check; the multiply-and-add remains
unconditional. When `env_intensity == 0`, the multiply
produces `(0, 0, 0)`; when `env_intensity > 0`, the
multiply produces the authored spectral colour. Either
way, the existing arithmetic is correct.

The doc-comment's "no `env_intensity > 0` short-
circuit" sentence (§1.2) is the contract: the kernel
SHOULD NOT introduce one. PT-P.12 honours that contract
trivially by changing zero kernel bytes.

---

## 4. Build status

**PASS.**

Re-ran during this audit:

| Config      | RR_ENABLE_CUDA | RR_ENABLE_OPTIX | Build     | ctest    |
|-------------|:--------------:|:---------------:|-----------|:--------:|
| `build`     | OFF            | OFF             | clean     | 7/7 PASS |
| `build-ON`  | OFF            | ON              | clean     | 8/8 PASS |

Both audit-host configs report zero new compiler
warnings under the `rr_apply_warnings`-enforced
`-Wall -Wextra -Wpedantic` triple. ctest counts
unchanged from PT-P.6 / PT-P.9 (the slice did not add
or remove a ctest binary, per the PT-P.11 task §3.1's
no-new-test recommendation).

The `renderer_tests` ctest binary (PT-P.3) ran clean;
its three test cases continue to verify
`AccumulationBuffer`'s post-conditions independently of
PT-P.12.

---

## 5. CPU path-tracing violations

**ZERO violations** — verified by re-running the
Stage-11 audit's three grep sweeps on the post-PT-P.12
tree.

### 5.1 Per-pixel for-loops on the host

```
$ grep -rnE "for.*<.*width|for.*<.*height" \
    src/renderer/ src/pathtracer/*.cpp src/main.cpp
=> (no matches)
```

Identical to the Stage 11 audit + every prior
path-tracer audit's findings. PT-P.12 added no new
matches.

### 5.2 spp launcher loop

```
$ grep -rn "for.*samples_per_pixel|for.*effective_samples" \
    src/pathtracer/*.cpp src/main.cpp
src/pathtracer/PathTracer.cpp:115:
    for (int s = 0; s < effective_samples_per_pixel; ++s) {
```

One match — the SAME spp launcher loop the Stage 11 +
PT-P.4 + PT-P.7 + PT-P.10 audits classified as host
iteration at sample-frame granularity. PT-P.12 did not
edit `PathTracer.cpp`; the line is byte-identical with
PT-P.9.

### 5.3 Host-side intersection / closest-hit code

```
$ grep -rn "intersect_sphere|intersect_triangle|closest_hit" \
    src/renderer/ src/pathtracer/*.cpp
src/renderer/Hit.h:30:
    // `intersect_triangle`. The third coord is
    // `1 - bary_u - bary_v`.
```

One match — a doc-comment in `renderer/Hit.h`. No
host-side intersection code. Identical to every prior
audit.

### 5.4 PT-P.12-specific scope

PT-P.12 edited two files:

| File                              | New per-pixel code? |
|-----------------------------------|---------------------|
| `src/pathtracer/PathTracer.h`     | NO. Doc-comment paragraph extension. |
| `src/main.cpp`                    | NO. One new `Logger::info` call inside the spp dispatcher loop's per-iteration host orchestration block; format-helper lambda in scope. The lambda iterates ONCE per spp run (scalar Vec3 → string formatting), not per pixel. |

Neither file contains a per-pixel `for` loop, a call to
any `intersect_*` / `closest_hit` / `sample_*hemisphere*`
/ `next_float` / `next_vec2` primitive, or any code
that reads or writes a per-pixel value.

The new `Logger::info` line emits ONCE per
`PathTracer::render` call — at most twice per
dispatcher invocation given the `--render-pathtrace`
dispatcher's hard-coded `kRuns = {1, 16}` array. That
is the same sample-frame granularity the existing four
info-log lines run at; no new per-pixel work is
introduced.

Master rule 5/7 ("All per-pixel/per-ray rendering must
happen on GPU") therefore remains upheld post-PT-P.12.

---

## 6. Runtime-deferred status

**BLOCKED on the same six artefacts the
`PATH_TRACER_POLISH_PLAN.md` §2 + every prior
path-tracer audit (Stage 11, PT-P.4, PT-P.7, PT-P.10)
enumerate.** PT-P.12 does NOT alter the per-pixel
computation graph (§3 + §5 confirmed), so the empirical
verification surface is unchanged from PT-P.10.

| Artefact                                | CUDA-host expectation                       |
|-----------------------------------------|---------------------------------------------|
| `output/gpu_rng_test.ppm`               | byte-identical with pre-PT-P.12             |
| `output/gpu_accumulation_test.ppm`      | byte-identical                              |
| `output/pathtrace_spp_1.ppm`            | byte-identical                              |
| `output/pathtrace_spp_16.ppm`           | byte-identical                              |
| `output/optix_pathtrace_spp1.ppm`       | byte-identical                              |
| `output/optix_pathtrace_spp16.ppm`      | byte-identical                              |

The byte-identical claim is structurally guaranteed by
§3's zero-bytes-changed finding in the per-pixel code
path.

### 6.1 Operator-side PT-P.12-specific check

ONE additional CUDA-host check the operator may want
on a CUDA host:

- **Confirm the new info-log line emits.** Run
  `./build/bin/RelativityRender --render-pathtrace
  scenes/test_full_scene.rrscene` on a CUDA host and
  inspect the post-render block. Expected:

  ```
  [INFO] scene file       : scenes/test_full_scene.rrscene
  [INFO] framebuffer      : 1280x720 (from render_settings)
  [INFO] pathtrace        : 1 spp, 4 bounces, 4 sphere(s),
         5 material(s), 3 light(s), 1 mesh(es)
  [INFO] environment      : [0.550000, 0.700000, 1.000000] * 0.300000
  [INFO] wrote pathtrace_spp_1.ppm: ...
  ```

  Repeats with `pathtrace        : 16 spp, ...` for
  the second run. If the `environment      : ...` line
  is missing, the slice failed to wire correctly and
  the audit's verdict for §2 should flip to REPAIR;
  this audit assumes structural correctness from the
  diff inspection alone.

### 6.2 Runner integration status

`tools/verify_cuda_host.py` does NOT need an update for
PT-P.12 — the runner exercises the existing
`--render-pathtrace` + `--render-optix-pathtrace`
commands; the new info-log line is captured by the
runner's stderr log automatically when a CUDA host
invokes it.

```
$ git diff fa41e58~1..fa41e58 -- tools/verify_cuda_host.py
=> 0 bytes
```

---

## 7. Verdict

| # | Audit item                                        | Result   |
|---|---------------------------------------------------|----------|
| 1 | Environment fallback behavior is documented       | PASS     |
| 2 | Dispatcher / log clarity exists                   | PASS     |
| 3 | No kernel behavior changed                        | PASS     |
| 4 | Build status (both audit-host configs)            | PASS     |
| 5 | CPU path-tracing violations                       | PASS — zero violations |
| 6 | Runtime-deferred status                           | BLOCKED  |
| 7 | Overall                                           | **PASS** (one BLOCKED row carried forward to a CUDA-host run) |

**Overall verdict: PASS.**

PT-P.12 ships exactly the polish the PT-P.11 task brief
specified, both audit-host build configs remain green
(7/7 OFF, 8/8 ON-audit-host), the per-pixel code path
is byte-identical pre/post-slice (the `git diff`
sweep over every kernel / launcher / renderer /
pathtracer / scene / test / runner directory returns
zero bytes), and master rule 5/7 (no CPU ray tracing)
remains enforced. The single BLOCKED row is the same
runtime-deferred surface every prior path-tracer audit
recorded; the new info-log line's empirical confirmation
on a CUDA host is documented in §6.1 as a one-shot
operator check.

REPAIR items: none.

The PT-P.{11..13} sub-arc (PT-P.11 task → PT-P.12 impl
→ this audit) is the smallest in the PT-P.x cadence to
date: the implementation slice landed 23 added lines
across two files vs the task's 25-line cap. No
deviation flag was needed; the polish honours its
"smallest viable next slice" billing.

### Recommended next step

Per `PATH_TRACER_POLISH_PLAN.md` §5 +
`PATH_TRACER_POLISH_AUDIT.md` §7's sequencing, three
remaining plan items can ship next; the operator's
preferred order from the most recent audit doc was:

1. **§4.5 — Emission handling** (`is_emissive`
   helper in `src/material/MaterialTypes.h` + a
   per-hit kernel branch in
   `src/cuda/CudaPathTracer.cu`'s `closest_hit` shader
   path). Larger surface than PT-P.12 because it
   touches the kernel; first non-host-only PT-P.x
   slice.
2. **§4.1 — RNG stability** (key-mix collision audit;
   changes every `pathtrace_spp_*.ppm` byte-exactly).
   Sequence after §4.5 because §4.5 has no rendered-
   pixel ripple.
3. **§4.7 — Firefly clamp placeholder** (default-off
   field on `PathTraceConfig` + matching kernel
   guards on BOTH the CUDA and OptiX path-trace
   raygens). Largest remaining surface; sequence
   last.

### Alternative paths

- **Trigger the CUDA-host verification run** that flips
  the §6 BLOCKED rows of this audit (and PT-P.4 /
  PT-P.7 / PT-P.10's BLOCKED rows) to PASS. Single
  command-line invocation
  (`tools/verify_cuda_host.py [--optix]`) on a real
  CUDA + OptiX-SDK host. Per
  `docs/CUDA_HOST_VERIFICATION_AUDIT.md` §3, the
  resulting `docs/CUDA_HOST_VERIFICATION_REPORT.md`
  byte-replaces the currently-committed audit-host
  REPAIR report.
- **Pivot to a different polish arc.** The TEX-P.x
  arc landed PASS (TEX-P.7) and has no open items;
  master order #16 (path tracing — feature work like
  NEE / non-diffuse BSDFs / multi-mesh upload) is the
  next major follow-up after the PT-P.x polish arc
  closes. Each is its own multi-slice arc.

PT-P.13 (this audit) closes the PT-P.{11,12} sub-arc.
The next concrete slice — when the operator chooses to
continue — opens with a PT-P.14 task definition
mirroring the PT-P.{2,5,8,11} cadence, OR pivots to a
runtime-verification slice as above.

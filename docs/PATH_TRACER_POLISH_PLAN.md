# Path Tracer — Polish Plan

Date: 2026-05-04.
Branch: `relativity-core-v1`.
Master order: #16 (Path tracing foundation).
Sources read: `docs/BUILD_PLAN.md`,
`docs/CUDA_HOST_VERIFICATION_AUDIT.md`,
`docs/TEXTURE_POLISH_AUDIT.md`,
`docs/STAGE_11_AUDIT.md`,
`docs/STAGE_20_OPTIX_PATH_TRACING_AUDIT.md`,
`src/pathtracer/{PathTracer.{h,cpp},RNG.h,Sampling.h}`,
`src/renderer/AccumulationBuffer.{h,cpp}`,
`src/cuda/{CudaPathTracer.cu,CudaAccumulation.cu}`,
`src/optix/OptixPrograms.cu` (path-trace raygen),
`src/main.cpp` (`run_render_pathtrace`).
Mode: documentation-only. **No source code is modified by
this plan.** NO new CLI flags. The plan is the spec; future
slices implement individual items per the project's
established slice cadence.

The plan answers five prompt sections in order; the closing
section recommends the smallest first polish item.

---

## 1. Current path tracer status

The path-tracer foundation landed in Stage 11 (master order
#16) and was extended onto the OptiX backend in Stages
20I/20J. Today's surface:

### Host-side configuration (`PathTraceConfig`)

`src/pathtracer/PathTracer.h:19-44` declares the public
configuration POD:

| Field                  | Type        | Default       | Meaning                                      |
|------------------------|-------------|---------------|----------------------------------------------|
| `max_bounces`          | `int`       | `4`           | Bounce budget. `0` traces no rays; `1`       |
|                        |             |               | traces only the primary ray and accounts     |
|                        |             |               | for emission + environment hits.             |
| `samples_per_pixel`    | `int`       | `16`          | spp count. The host loop launches the per-   |
|                        |             |               | sample kernel this many times and feeds      |
|                        |             |               | each result through `AccumulationBuffer`.    |
| `seed`                 | `unsigned`  | `0u`          | Mixed into `make_pixel_rng(x, y, sample,     |
|                        |             |               | seed)`. Re-running with the same seed        |
|                        |             |               | produces a deterministic image.              |
| `environment_color`    | `Vec3`      | `(0.55,0.70,1.00)` | Linear-RGB radiance "from infinity"     |
|                        |             |               | when a ray misses every scene primitive.     |
| `environment_intensity`| `float`     | `0.30`        | Multiplier on `environment_color`.           |

### Host orchestration (`PathTracer::render`)

`src/pathtracer/PathTracer.cpp` validates dimensions + four
config fields, allocates an `AccumulationBuffer` + a single
device-side sample buffer, then runs:

```
for (s = 0; s < cfg.samples_per_pixel; ++s) {
    launch_pathtrace_sample(... s ...);
    accum.accumulate_sample(sample);
}
img = accum.resolve_to_image();
```

The host loop is the only iteration outside the GPU; per-
ray and per-pixel work lives entirely in
`__device__` / `__global__` code (`STAGE_11_AUDIT.md` §6/§7
recorded zero CPU violations).

### Per-sample kernel (`k_pathtrace_sample`)

`src/cuda/CudaPathTracer.cu:144-229`:

1. Seed `Rng` via `make_pixel_rng(x, y, sample_index, seed)`.
2. Sub-pixel jitter from `next_vec2(rng)` -> primary ray
   (`generate_primary_ray`).
3. `for bounce in [0, max_bounces)`:
    - `closest_hit` against spheres + (single) mesh slot.
    - On miss: add `throughput * env_color * env_intensity`
      and break.
    - Add `throughput * material.emissionColor *
      emissionStrength`.
    - If `bounce + 1 >= max_bounces`: break (no point
      sampling a direction we will not trace).
    - Cosine-weighted hemisphere sample
      (`sample_cosine_hemisphere`); align to hit normal via
      `align_to_normal`; multiply throughput by
      `material.baseColor`; offset ray origin by
      `hit.normal * 1e-4f` to dodge self-intersection.

### Accumulation buffer (`AccumulationBuffer`)

Stage 11B (`src/renderer/AccumulationBuffer.{h,cpp}`):

- `resize(w, h)` allocates a device-side
  `width * height * 4` float sum buffer and resets the
  sample counter.
- `reset()` zeroes the buffer + counter via
  `launch_accum_clear`.
- `accumulate_sample(device_sample)` pumps one frame in;
  Stage 18A.4 routes the FIRST sample through `cudaMemcpy
  D2D` (skips the wasted read-of-zeros) and subsequent
  samples through the float4-vectorised add kernel.
- `resolve_to_image()` divides the running sum by
  `samples_count()` on the device and downloads into a
  fresh host `Image`. Zero samples returns an empty
  (zeroed) image rather than divide-by-zero.

### OptiX parity (Stages 20I / 20J)

`src/optix/OptixPrograms.cu`'s `__raygen__pathtrace`,
`__miss__pathtrace`, `__closesthit__pathtrace` mirror the
CUDA per-sample kernel. `OptixRenderer::render_pathtrace`
and `render_pathtrace_progressive` consume the same
`PathTraceConfig` knobs (spp, max_bounces, seed) and reuse
`launch_accum_*` from `rr_gpu` (audit recorded at
`STAGE_20_OPTIX_PATH_TRACING_AUDIT.md` §7).

### CLI surface

| CLI                                  | Output                                   | Stage  |
|--------------------------------------|------------------------------------------|--------|
| `--render-pathtrace <scene>`         | `output/pathtrace_spp_1.ppm`             | 11C    |
|                                      | `output/pathtrace_spp_16.ppm`            |        |
| `--render-optix-pathtrace <scene>`   | `output/optix_pathtrace_spp1.ppm`        | 20I    |
|                                      | `output/optix_pathtrace_spp16.ppm`       |        |
| `--render-rng-test`                  | `output/gpu_rng_test.ppm`                | 11A    |
| `--render-accumulation-test`         | `output/gpu_accumulation_test.ppm`       | 11B    |

The CUDA dispatcher runs spp=1 + spp=16 from a single
invocation (a hard-coded `kRuns` array in
`run_render_pathtrace` at `src/main.cpp:2374`). Other
config fields keep their defaults — no `--seed`,
`--bounces`, `--env-color` modifier exists today.

### Known caveats (carried over from Stage 11 audit § 10)

- No direct-light sampling (NEE). Only emissive surfaces +
  the environment fallback contribute illumination.
- Diffuse Lambert only. `roughness` / `metallic` /
  `specular` / `transmission` are uploaded but not
  consulted by the path tracer.
- Single-mesh GpuScene slot. Scenes with > 1 visible mesh
  are silently truncated to the first.
- Spheres on the OptiX path: not built into a custom-IS
  GAS yet. The OptiX path tracer is mesh-only
  (`STAGE_20_OPTIX_PATH_TRACING_AUDIT.md` §9 / §11 Gap D).
- No relativistic perception in the CUDA path tracer's
  primary or bounce rays (the OptiX path tracer DOES
  invoke aberration / Doppler / searchlight; the CUDA
  path tracer does not).

---

## 2. Runtime-deferred checks

The audit host carries the same posture every prior audit
recorded: no `nvcc`, no OptiX SDK, no GPU. The path tracer
therefore has six artefacts whose runtime behaviour is
empirically unverifiable in this branch:

| Artefact                                   | Empirical status on audit host        | CUDA-host expectation |
|--------------------------------------------|---------------------------------------|-----------------------|
| `output/gpu_rng_test.ppm`                  | absent (`requires CUDA` fallback)     | four-quadrant noise   |
| `output/gpu_accumulation_test.ppm`         | absent (same)                         | uniform mid-grey      |
| `output/pathtrace_spp_1.ppm`               | absent (same)                         | noisy beauty image    |
| `output/pathtrace_spp_16.ppm`              | absent (same)                         | converged beauty      |
| `output/optix_pathtrace_spp1.ppm`          | absent (no SDK)                       | OptiX-rendered noisy  |
| `output/optix_pathtrace_spp16.ppm`         | absent (same)                         | OptiX-rendered conv   |

The CUDA-host verification runner
(`tools/verify_cuda_host.py`) carries
`render-pathtrace` in `base_commands()` and
`render-optix-pathtrace` in `optix_commands()`; per
`docs/CUDA_HOST_VERIFICATION_REPORT.md` the audit-host
runner records both as `FAIL` (CUDA-required dispatchers
hit their documented `requires CUDA` audit-host fallback);
on a CUDA + (optional) OptiX-SDK host the same rows flip
to `PASS`.

Additional runtime claims that are not empirically checked
on the audit host but are structurally clear from source:

- **Determinism.** `make_pixel_rng(x, y, s, seed)` produces a
  pure function of the four inputs; the spp loop's
  per-sample seed is `s`. Re-running with the same seed
  must produce a bit-identical PPM (modulo floating-point
  associativity in the accumulator's add kernel).
- **First-sample memcpy fast path.** Stage 18A.4 routes
  `accumulate_sample` with `samples_ == 0` through
  `launch_accum_first_sample` (a `cudaMemcpy D2D`),
  bypassing the add kernel. Verified by source inspection;
  not pixel-compared on the audit host.
- **Spp loop convergence.** `pathtrace_spp_1` vs
  `pathtrace_spp_16` should show the standard
  `1 / sqrt(N)` noise reduction. Visual confirmation
  requires a CUDA host.

---

## 3. Known limitations

The path tracer's v1 surface is deliberately minimal. The
following limitations are intentional + documented; this
plan does NOT propose lifting any of them in the next slice
(each is its own master-order item or polish arc).

| Limitation                                  | Status                                          |
|---------------------------------------------|-------------------------------------------------|
| Diffuse Lambert only                        | Master #16 follow-up (BSDF stage not started).  |
| No direct-light sampling (NEE)              | Master #16 follow-up
                                              (Stage 11 audit §10 listed as 11D candidate).    |
| No multi-mesh upload                        | Master #11 follow-up (`GpuScene` single-mesh    |
|                                             | slot; Stage 11 audit §10).                      |
| No spheres on the OptiX path                | Stage 20 §11 Gap D.                             |
| No relativistic perception in the CUDA      | Master #16 follow-up.                           |
| path tracer's primary/bounce rays           |                                                 |
| No texture filtering beyond nearest-        | Master #18 future work (TEXTURE_SYSTEM.md §4).  |
| neighbour                                   |                                                 |
| No firefly clamp / variance control         | This plan §4.7.                                 |
| No Russian roulette                         | Future polish (out of scope for v1).            |
| `samples_per_pixel` and `max_bounces`       | Defaults in `PathTraceConfig`; no CLI knob.     |
| are not surfaced via CLI                    |                                                 |

The plan items in §4 below are intentionally chosen to be
SMALLER than any of the limitations in this table — they
strengthen the existing v1 surface rather than expand it.

---

## 4. Small safe polish candidates

Seven polish items, each scoped small enough for a single
slice. Estimated diff is informational only; the
implementation slice owns the actual line count.

### 4.1 RNG stability

**Status today.**
`make_pixel_rng(pixel_x, pixel_y, frame_index, global_seed)`
mixes the four inputs into a 64-bit key:

```cpp
const std::uint64_t key =
      global_seed
    ^ (static_cast<std::uint64_t>(pixel_x)     << 32u)
    ^ (static_cast<std::uint64_t>(pixel_y)     << 16u)
    ^ (static_cast<std::uint64_t>(frame_index)       );
```

The `pixel_y << 16` and `frame_index << 0` shifts overlap
in bits `[0, 32)` of the key, so for small `pixel_y` (< 65536)
the low 16 bits of `pixel_y` and the low 16 bits of
`frame_index` collide before SplitMix64 avalanches. The
collision is recovered by SplitMix64 + the burn-one-step at
the end of `make_pixel_rng`, but the mix is weaker than
necessary at low (x, y, sample) values. Tests
(`tests/pathtracer_tests.cpp`) currently exercise PCG range,
decorrelation, determinism, hemisphere unit-length / upper,
MC PDF normalisation, and `E[dz] = 2/3` — but NOT a
collision check on adjacent pixels at sample 0.

**Polish item.**
Change the key construction so the four 32-bit inputs do
not overlap before SplitMix64. Options (in order of
preference):

```cpp
// Hash each input through SplitMix64 individually, then xor.
const std::uint64_t key =
      splitmix64(global_seed)
    ^ splitmix64(static_cast<std::uint64_t>(pixel_x))
    ^ splitmix64(static_cast<std::uint64_t>(pixel_y) << 32u)
    ^ splitmix64(static_cast<std::uint64_t>(frame_index));
```

OR pack the four 32-bit inputs into the key with a
non-colliding layout:

```cpp
const std::uint64_t key =
      (static_cast<std::uint64_t>(pixel_x)     << 32u)
    ^ (static_cast<std::uint64_t>(pixel_y))
    ^ splitmix64(global_seed
                 ^ (static_cast<std::uint64_t>(frame_index) << 32u));
```

Add ONE new test in `tests/pathtracer_tests.cpp` that
asserts `make_pixel_rng(x,   y,   0, 0).state !=
make_pixel_rng(x+1, y,   0, 0).state` and similar for
y / sample / seed perturbations across a 16x16 grid + 4
sample indices.

**Cost.** ~10 lines in `RNG.h` + ~25 lines of new test
(no new CLI surface, no kernel change).

**Risk.** The PCG state values change. The
`pathtrace_spp_*.ppm` files become bit-different (same
visual result; same statistical convergence). This is the
expected outcome of any RNG-stability change; an audit
note confirming "noise pattern shifts; means / variances
unchanged" should accompany the slice.

### 4.2 Accumulation reset correctness

**Status today.**
`AccumulationBuffer::resize(w, h)` zeroes the buffer + the
sample counter, and `reset()` does the same on an
already-allocated buffer. `accumulate_sample` increments
`samples_` after a successful kernel launch. `resolve_to_image`
guards against `samples_ == 0` by returning an empty image.

There are two small audit gaps:

1. `reset()` on a host-only build returns `false` without
   touching state. Callers that check the return value are
   safe; callers that don't are exposed to a subtle bug
   where `samples_` keeps growing across resets if CUDA is
   absent. The current call sites (path tracer + OptiX
   path) all check the return; a future caller might not.
2. `resize(w, h)` with `w == width_` AND `h == height_`
   (a no-op resize) re-allocates and re-zeroes. Idempotent
   and correct, but wastes a `cudaMemset` on every
   re-render of the same scene. Not a bug; an optimisation.

**Polish item.**
Two small cleanups:

- `reset()`: zero `samples_` BEFORE the
  `launch_accum_clear` call (instead of after). On the
  host-only path the counter then ends up at 0 even when
  the launcher returns false; the buffer's `valid()` check
  already prevents callers from misreading the (still-
  empty) buffer, so the state is consistently "0 samples
  in a non-CUDA-cleared buffer" rather than "N samples in
  a non-cleared buffer".
- `resize(w, h)`: when `w == width_ && h == height_ &&
  device_.size() == w*h*4`, call `reset()` instead of
  re-allocating. Saves a `cudaFree` + `cudaMalloc` round
  trip on the common "render the same scene twice" path.

Add ONE new test that calls `resize(64, 64)` twice and
asserts `samples_count() == 0` after the second call (the
existing tests cover the alloc path; this would cover the
no-op-resize fast path).

**Cost.** ~6 lines in `AccumulationBuffer.cpp` + ~15 lines
of new test.

**Risk.** Trivial — both changes preserve external
behaviour (callers see a 0-sample buffer either way).

### 4.3 Max-bounce validation

**Status today.**
`PathTracer::render` rejects `max_bounces < 0` with a
diagnostic message. The kernel accepts `max_bounces == 0`
(loop body never executes; output is uniformly black) and
`max_bounces == 1` (only the primary ray; emission +
environment hits accounted for, no diffuse bounces).

There is no UPPER bound. A caller passing
`max_bounces = 1000` produces a kernel that does not
crash but takes ~125x longer than the default `4`; on a
poorly-budgeted timeline this looks like a hung renderer.

**Polish item.**
Document an upper bound in `PathTraceConfig` (e.g.
`max_bounces` <= 32) and reject values above it with the
same `Logger::warning` shape the texture validator uses:

```cpp
if (cfg.max_bounces > 32) {
    rr::core::Logger::warning(
        "PathTraceConfig::max_bounces=" +
        std::to_string(cfg.max_bounces) +
        " exceeds the recommended cap of 32; clamping. "
        "Set explicitly via the dispatcher CLI when long "
        "bounce paths are needed.");
    // Clamp rather than reject — the artist asked for a
    // long path; 32 produces a deeper integration than the
    // default 4 without hung-kernel risk.
    cfg = cfg with { max_bounces = 32 };  // pseudocode
}
```

The cap is a SOFT warning — clamp + log rather than reject
— so existing scripts that pass `max_bounces = 100` keep
working with a one-line warning. The cap value (32) is a
suggestion; the implementing slice can pick a different
number if there is a more principled reason.

**Cost.** ~8 lines in `PathTracer.cpp`.

**Risk.** None for `max_bounces ∈ [0, 32]` (byte-identical
behaviour). For `max_bounces > 32` the renderer now
produces shorter paths than authored; the warning makes
the divergence visible.

### 4.4 Environment fallback clarity

**Status today.**
`PathTraceConfig::environment_color` defaults to
`(0.55, 0.70, 1.00)` (cool sky tint) and
`environment_intensity` defaults to `0.30`. The kernel
multiplies them together once per miss and adds
`throughput * env` to the radiance.

The defaults produce a moderate-brightness sky for any
scene. There are two clarity gaps:

1. The `PathTraceConfig` doc-comment mentions the defaults
   but does NOT say what `environment_intensity = 0` means
   (= "no environment radiance"; rays that miss every
   primitive contribute zero — a fully black background).
2. The CLI dispatcher (`run_render_pathtrace`) does NOT
   echo the environment fields in its info log; a future
   operator wondering why their scene looks blue has no
   way to confirm the default is firing without reading
   source.

**Polish item.**
Two-line additions:

- Extend `PathTraceConfig`'s `environment_intensity`
  doc-comment with: "`environment_intensity == 0` produces
  a fully black background for missed rays." (one line of
  doc; zero functional change).
- Have `run_render_pathtrace` print the environment fields
  alongside the existing `pathtrace : N spp, M bounces,
  ... light(s)` info line, e.g.
  "environment : (0.550, 0.700, 1.000) * 0.300".
  Mirrors the existing `framebuffer : ...` and
  `scene file : ...` output shape.

**Cost.** ~5 lines (1 doc, 4 logging).

**Risk.** None. Doc-only + post-render info log; no kernel
change.

### 4.5 Emission handling

**Status today.**
The kernel adds `material.emissionColor * emissionStrength`
modulated by throughput on EVERY hit, regardless of
whether the surface is meaningfully emissive. For a non-
emissive material `emissionColor == (0, 0, 0)` so the add
is `+= 0`, which is correct but wastes the load + the
multiply.

The bigger clarity gap: the kernel uses `emissionColor *
emissionStrength` rather than splitting them in the doc.
A material with `emissionColor = (1, 1, 1)` and
`emissionStrength = 5` produces the same radiance as
`emissionColor = (5, 5, 5)` and `emissionStrength = 1`,
but the validator (and any future material-graph editor)
might want to distinguish "white emitter at 5x"
from "5x-white emitter at 1x". Today nothing in the
validator or the kernel makes the distinction.

**Polish item.**
Two cleanups:

- Add a `bool is_emissive(const MaterialParams&)` inline
  helper in `material/MaterialTypes.h` that returns
  `emissionStrength > 0 && (emissionColor.x + .y + .z) > 0`.
  Use it in the path-tracer kernel to short-circuit the
  emission add: `if (is_emissive(m)) radiance += ...`.
  Saves three multiplies and three adds per hit on the
  common (non-emissive) path; the branch is uniform per-
  warp because `MaterialParams` is shared by every pixel
  hitting the same surface.
- Document the `emissionColor` * `emissionStrength`
  factorisation on the path-tracer kernel comment so the
  next reader sees the convention without needing to
  reverse-engineer it.

**Cost.** ~6 lines (1 helper, 3 kernel edits, 2 doc).

**Risk.** Trivial. The kernel branch is uniform; no warp
divergence on a typical scene.

### 4.6 Sample count validation

**Status today.**
`PathTracer::render` rejects `cfg.samples_per_pixel <= 0`
with a diagnostic. The host loop is `for (s = 0; s <
samples_per_pixel; ++s)`, so a zero spp produces zero
launches and the resolve step emits an empty image
(`samples_count() == 0` -> empty image short-circuit).

There is no UPPER bound. A caller passing
`samples_per_pixel = 100000` runs the per-sample kernel
that many times. The kernel itself is fast; the wall-clock
cost is the kernel-launch overhead per sample (the kernel
has fixed per-pixel work). For very large spp the launch
loop dominates; the operator might want a warning.

**Polish item.**
Identical shape to §4.3 (max-bounce cap) but for spp:

- Soft cap `samples_per_pixel <= 4096` (suggestion;
  implementor's call). Above the cap, log a
  `Logger::warning` and clamp.
- The `pathtrace_spp_*.ppm` defaults (1, 16) are well
  within any sensible cap; the soft cap does not affect
  the existing dispatcher.
- Add ONE test that calls `render(.., spp = cap + 1)` and
  asserts the warning fires + the clamped run produces
  the same image as `render(.., spp = cap)`.

**Cost.** ~8 lines in `PathTracer.cpp` + ~15 lines of new
test.

**Risk.** As §4.3: callers who explicitly request higher
spp now get clamped. Soft warning, easily auditable.

### 4.7 Firefly clamp placeholder

**Status today.**
The path tracer accumulates raw radiance per sample. With
the current diffuse-Lambert + emissive-only model and
modest `max_bounces`, runaway samples ("fireflies") are
rare, but they will become common as soon as direct-light
sampling (NEE) lands and a small light source produces a
high-variance contribution.

There is no firefly clamp today. Adding one prematurely
would change the bias of the integrator; adding it as a
deliberate, opt-in placeholder NOW lets the next slice
that adds NEE flip the flag without touching the
integrator's hot path.

**Polish item.**
Add a single optional field to `PathTraceConfig`:

```cpp
// Firefly clamp. When > 0, individual sample radiance is
// clamped per channel to this maximum before it is added
// to the accumulator. 0 disables the clamp (default - the
// integrator is unbiased; deliberately opt-in because
// clamping introduces a small downward bias in scenes
// with high-variance light paths).
float firefly_clamp = 0.0f;
```

Honour it in `k_pathtrace_sample` right before the final
write:

```cpp
if (firefly_clamp > 0.0f) {
    radiance.x = fminf(radiance.x, firefly_clamp);
    radiance.y = fminf(radiance.y, firefly_clamp);
    radiance.z = fminf(radiance.z, firefly_clamp);
}
```

The default `firefly_clamp = 0.0f` ensures every existing
scene renders byte-identically. NEE / area-light slices
that produce visibly noisy fireflies can opt in to a
small clamp (e.g. `8.0f` is a common default) without
needing to refactor the integrator.

**Cost.** ~5 lines in `PathTracer.h` + 5 lines in the
kernel + ~5 lines in `OptixPrograms.cu`'s pathtrace raygen
to mirror the same clamp on the OptiX backend.

**Risk.** Default-off; byte-identical for every existing
render. The OptiX-side mirror is required so the two
backends' outputs stay convergent at non-zero clamp.

---

## 5. Recommended first polish item

**§4.2 — Accumulation reset correctness.**

Rationale (one paragraph): all seven items are safe, but
§4.2 is the smallest viable diff (~6 source lines + ~15
test lines, no new CLI surface, no schema change, no
kernel change, no `__device__` code touched). It tightens
the host-side state machine of `AccumulationBuffer` (the
shared dependency of both the CUDA and OptiX path
tracers) without altering any rendered pixel. The two
fixups it ships — counter-then-launch ordering on
`reset()`, and no-op fast path on `resize()` — both have
zero behavioural change for callers in the hot path
(byte-identical pathtrace_spp_*.ppm output) and improve
robustness on the cold path (host-only builds; repeated
renders of the same scene). After §4.2 lands, §4.3
(max-bounce cap) is the natural follow-up because it
reuses the same `Logger::warning` clamp shape and
operates on the same `PathTraceConfig` POD.

The remaining items (§4.1 RNG stability, §4.4 environment
clarity, §4.5 emission handling, §4.6 sample-count
validation, §4.7 firefly clamp placeholder) are all
larger or have a behaviour-altering footprint that
deserves its own slice.

### Why not §4.1 first

RNG stability is a real correctness improvement, but it
unavoidably changes every `pathtrace_spp_*.ppm` byte-
exactly. Even though the visual / statistical result is
preserved, ANY change to the noise pattern requires an
operator-host re-run of the CUDA-host verification suite
+ a refresh of the post-stage-21 audit's references to
the existing PPMs. §4.2 has zero such ripple; landing it
first means the next slice (§4.1) can flip the noise
pattern without compounding two risk surfaces.

### Why not §4.7 first

Firefly clamp is the second-smallest item but it touches
both backends (CUDA + OptiX path-trace raygen). §4.2 is
host-only; the kernel side is byte-identical. Pick §4.2 to
sequence the polish arc with the cheapest viable first
slice.

---

## Closing notes

This plan adds no source. The next concrete slice should
be:

1. **PT-P.2 — Accumulation reset correctness implementation
   slice.** Implement §4.2 in the smallest possible diff.
   Keep the OFF + ON-audit-host ctest baselines green;
   add a BUILD_PLAN entry.

After that lands the operator may continue with §4.3 (max-
bounce cap), §4.6 (sample-count cap), §4.4 (environment
clarity), §4.5 (emission helper), §4.1 (RNG stability),
§4.7 (firefly clamp placeholder) in any order, or pick a
different master-order item entirely. Per
`docs/CUDA_HOST_VERIFICATION_AUDIT.md` §5 and the
post-Stage-21 capstone audit, the path-tracer polish arc is
one of three viable next-step buckets (along with the
empirical CUDA-host verification run + OptiX Gap A
follow-up); this plan captures the smallest, safest items
inside that bucket.

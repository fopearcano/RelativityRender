# CUDA-host Verification Plan (CUDA-H.1)

Date: 2026-05-04
Branch: `relativity-core-v1`
Scope: define the exact commands a CUDA + (optional)
OptiX-SDK host operator runs to empirically verify every
`--render-*` artifact this project produces. Documentation
only — this file contains NO commands the audit author runs;
the operator runs them on the right hardware.

This plan closes the long-running "runtime deferred, not
code failure" gate that appears in every Stage 13/14/15/19/
20/21 audit. Audits on this no-CUDA / no-OptiX-SDK host can
only verify (a) builds compile cleanly, (b) audit-host
fallbacks fire with documented errors, and (c) the source
wires the SDK calls correctly. Producing the actual PPMs
requires the right hardware; this plan tells the operator
exactly what to run + what to look for.

---

## 0. Host preconditions

Before running any of the commands below the operator must
have:

- A CUDA-capable NVIDIA GPU (compute capability 5.0+).
- The CUDA Toolkit installed and on `PATH` (`which nvcc`
  resolves; `nvidia-smi` reports the GPU).
- (Optional, but required for OptiX commands) the OptiX
  SDK installed; `OPTIX_ROOT` points at the SDK install
  directory containing `include/optix.h`.
- A clean working tree on `relativity-core-v1` at the
  commit being verified (or later).
- Write access to a fresh `output/` directory under the
  repo root. The build creates this directory on demand
  via `std::filesystem::create_directories`; an existing
  empty `output/` is fine.

---

## 1. Build

### 1.1 CUDA-only (no OptiX)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DRR_ENABLE_CUDA=ON -DRR_BUILD_TESTS=ON
cmake --build build -j
```

Expected:
- Configure prints
  `RelativityRender 0.1.0 (Stage 20N: OptiX AOVs)` banner.
- Configure prints `CUDA backend : ON` and the detected
  `nvcc` location.
- Build completes with 0 errors.
- `build/bin/RelativityRender` exists; size > 1 MB
  (debug-info-stripped CUDA executable).

### 1.2 CUDA + OptiX

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DRR_ENABLE_CUDA=ON -DRR_ENABLE_OPTIX=ON \
    -DOPTIX_ROOT=/path/to/optix-sdk -DRR_BUILD_TESTS=ON
cmake --build build -j
```

Expected:
- Configure prints the CUDA stanza above, plus
  `OptiX backend: requested ...` and the detected
  `<optix.h>` path.
- Build completes with 0 errors.
- `build/bin/RelativityRender` exists.

### 1.3 Tests

```
cd build && ctest --output-on-failure
```

Expected:
- CUDA-only build: `100% tests passed, 0 tests failed
  out of N` (N varies by SDK / OptiX availability;
  audit-host baseline is 6/6 OFF, 7/7 ON-no-SDK).
- CUDA + OptiX build: every test green, including the
  `optix_tests` target.

PASS: every cmake / build / ctest step exits 0; binary
exists at `build/bin/RelativityRender`.
REPAIR: any non-zero exit, missing binary, or test
failure → fix and re-run from §1.1.

---

## 2. Device info

```
build/bin/RelativityRender --device-info
```

Expected:
- Process exits 0.
- stdout/stderr include lines describing:
  - GPU backend name (e.g. "CUDA"),
  - device count and per-device summary
    (`[0] <name> (sm_<cc>, <vram>, <SM count> SMs)`),
  - OptiX availability stanza (when ON):
    `OptiX build enabled: yes` /
    `OptiX SDK found: yes` /
    `OptiX renderer status: scaffold only`.
- No PPM produced; no `output/` writes.

PASS: device count ≥ 1, every OptiX stanza line is "yes"
on the OptiX-enabled build.
REPAIR: device count = 0 → check NVIDIA driver +
`nvidia-smi`; "OptiX SDK found: no" on OptiX-enabled
build → check `OPTIX_ROOT` / SDK install.

---

## 3. CUDA render commands (no server)

For every command below the expected behaviour is:
- Process exits 0.
- The listed PPM file(s) appear under `output/` after the
  command completes.
- Each PPM is a valid P6 file (header `P6\n<w> <h>\n255\n`
  followed by binary RGB data); easiest sanity check is
  `file output/<name>.ppm` reporting "Netpbm image data,
  size = <w> x <h>, ..." and `wc -c output/<name>.ppm`
  reporting > 1 KB (most renders produce ≥ 1 MB at the
  default 800×600 / 1280×720 resolutions; tiny test
  renders may be smaller but always > 0).

### 3.1 Gradient test

```
build/bin/RelativityRender --render-gradient
```

Expected file: `output/gpu_gradient.ppm` (size > 1 KB).

### 3.2 Camera rays test

```
build/bin/RelativityRender --render-rays
```

Expected file: `output/gpu_camera_rays.ppm` (size > 1 KB).

### 3.3 Sphere test

```
build/bin/RelativityRender --render-sphere
```

Expected file: `output/gpu_sphere.ppm` (size > 1 KB).

### 3.4 Relativistic sphere test

```
build/bin/RelativityRender --render-relativistic
```

Expected files (four renders at fixed beta values):
- `output/sphere_beta_000.ppm` (size > 1 KB)
- `output/sphere_beta_025.ppm` (size > 1 KB)
- `output/sphere_beta_075.ppm` (size > 1 KB)
- `output/sphere_beta_095.ppm` (size > 1 KB)

Visual sanity check: as beta increases, the sphere should
shift colour (Doppler) and the apparent size should change
(aberration); pixel values should NOT be identical across
the four files.

### 3.5 Scene render (`test_spheres.rrscene`)

```
build/bin/RelativityRender \
    --render scenes/test_spheres.rrscene \
    --output output/render.ppm
```

Expected file: `output/render.ppm` (size > 1 KB).

### 3.6 Texture outputs

```
build/bin/RelativityRender --render-texture-sample-test
build/bin/RelativityRender --render-textured-material
```

Expected files:
- `output/gpu_texture_sample_test.ppm` (size > 1 KB).
- `output/gpu_textured_material.ppm` (size > 1 KB).

Visual sanity check: the texture-sample-test should show
the 2×2 four-colour reference texture (red top-left, green
top-right, blue bottom-left, yellow bottom-right) tiled
across the framebuffer; the textured-material test shows
the same texture mapped onto the quad geometry per the
Stage 13B.3 fixture.

### 3.7 AOV outputs

```
build/bin/RelativityRender --render-aovs
```

Expected files (six AOV passes):
- `output/aov_beauty.ppm` (size > 1 KB; lit RGB)
- `output/aov_normal.ppm` (size > 1 KB; encoded
  `0.5 n + 0.5` per pixel)
- `output/aov_depth.ppm` (size > 1 KB; `1 / (1 + t)`
  per pixel; closer = brighter)
- `output/aov_albedo.ppm` (size > 1 KB; raw baseColor)
- `output/aov_doppler.ppm` (size > 1 KB; D per pixel)
- `output/aov_searchlight.ppm` (size > 1 KB; D⁴ per pixel)

Visual sanity check: beauty / albedo show the multi-sphere
+ ground-bulb scene; normal shows the encoded normals as
RGB; depth shows close=bright / far=dark; doppler +
searchlight show the radial gradient from the
beta=(0,0,-0.5) observer velocity (forward-cone bright,
backward-cone dim).

### 3.8 Pathtrace (spp 1, spp 16)

```
build/bin/RelativityRender --render-pathtrace
```

Expected files (the dispatcher writes both spp variants
in one invocation):
- `output/pathtrace_spp_1.ppm` (size > 1 KB; noisy)
- `output/pathtrace_spp_16.ppm` (size > 1 KB; less
  noisy than spp_1)

Visual sanity check: spp_1 should show visible per-pixel
Monte Carlo noise; spp_16 should be visibly smoother
(noise amplitude ∝ 1/sqrt(spp); 16 samples ≈ 4× lower
noise than 1 sample).

---

## 4. OptiX render commands (only if `RR_ENABLE_OPTIX=ON` + SDK present)

Skip this entire section if the build did not link
against the OptiX SDK; every `--render-optix-*` command
exits 1 with the documented "requires OptiX SDK" error
on a CUDA-only host.

### 4.1 OptiX pipeline-skeleton + raygen

```
build/bin/RelativityRender --render-optix-test
build/bin/RelativityRender --render-optix-raygen
```

Expected files:
- `output/optix_test.ppm` (size > 1 KB; flat-colour
  test; verifies the OptiX pipeline / SBT / launch
  machinery).
- `output/optix_raygen.ppm` (size > 1 KB; sky gradient
  from miss program; verifies raygen + miss).

### 4.2 OptiX triangle + relativity

```
build/bin/RelativityRender --render-optix-triangle
build/bin/RelativityRender --render-optix-relativity
```

Expected files:
- `output/optix_triangle.ppm` (size > 1 KB; single
  triangle GAS + closest-hit; normal-as-color shading).
- `output/optix_relativity.ppm` (size > 1 KB; same
  triangle GAS with non-zero observer beta; visible
  Doppler shift relative to `optix_triangle.ppm`).

### 4.3 OptiX mesh + material + lighting

```
build/bin/RelativityRender \
    --render-optix-mesh-scene scenes/test_full_scene.rrscene
build/bin/RelativityRender \
    --render-optix-material-scene scenes/test_full_scene.rrscene
build/bin/RelativityRender \
    --render-optix-direct-lighting scenes/test_full_scene.rrscene
build/bin/RelativityRender \
    --render-optix-shadow-test scenes/test_full_scene.rrscene
```

Expected files:
- `output/optix_mesh_scene.ppm` (normal-as-color)
- `output/optix_material_scene.ppm` (material flat /
  baseColor + emission)
- `output/optix_direct_lighting.ppm` (Lambert direct +
  ambient over scene lights)
- `output/optix_shadow_test.ppm` (same as direct
  lighting, plus shadow rays)
All sizes > 1 KB.

### 4.4 OptiX textured material

```
build/bin/RelativityRender --render-optix-textured-material
```

Expected file: `output/optix_textured_material.ppm`
(size > 1 KB; should match the CUDA-path
`output/gpu_textured_material.ppm` byte-for-byte for
the same fixture under the same observer state).

### 4.5 OptiX path tracer

```
build/bin/RelativityRender \
    --render-optix-pathtrace scenes/test_full_scene.rrscene
```

Expected files (the dispatcher writes both spp variants
in one invocation):
- `output/optix_pathtrace_spp1.ppm` (size > 1 KB; noisy)
- `output/optix_pathtrace_spp16.ppm` (size > 1 KB;
  less noisy)

Visual sanity check: same noise-amplitude relationship
as the CUDA pathtrace (§3.8).

### 4.6 OptiX AOVs

```
build/bin/RelativityRender --render-optix-aovs
```

Expected files (six AOV passes via the OptiX path; same
labels as §3.7 but distinct filenames):
- `output/optix_aov_beauty.ppm`
- `output/optix_aov_normal.ppm`
- `output/optix_aov_depth.ppm`
- `output/optix_aov_albedo.ppm`
- `output/optix_aov_doppler.ppm`
- `output/optix_aov_searchlight.ppm`
All sizes > 1 KB.

### 4.7 OptiX denoise

```
build/bin/RelativityRender --render-optix-denoise
```

Expected file: `output/optix_denoised.ppm` — wait, this
is NOT what `--render-optix-denoise` actually writes.
Per Stage 21D.6, the dispatcher writes
`output/denoised.ppm` (the same path the legacy
`--render-denoise` uses). Re-reading the dispatcher
confirms: `out_path = "output/denoised.ppm"` by default.
Operators verifying both `--render-denoise` and
`--render-optix-denoise` in the same session should pass
`--output output/optix_denoised.ppm` to the OptiX
variant to avoid clobbering, e.g.:

```
build/bin/RelativityRender --render-denoise
# ...inspect output/denoised.ppm...
build/bin/RelativityRender --render-optix-denoise \
    --output output/optix_denoised.ppm
```

Expected files:
- `output/denoised.ppm` (legacy CUDA-path orchestration
  via `denoise_aov_buffers_to_ppm`; currently always
  takes the Stage 19C.3 noisy-Beauty fallback because
  the legacy `OptixDenoiser::invoke` is the Stage 21B.1
  stub).
- `output/optix_denoised.ppm` (new Stage 21D path via
  `OptixDenoiser::denoise(...)` +
  `denoise_and_save_ppm`; if denoise succeeds, carries
  denoised radiance; on failure, the Stage 21D.5 noisy-
  Beauty fallback fires and the file carries the noisy
  Beauty AOV).

Visual sanity check: `output/optix_denoised.ppm` should
be visibly smoother than the Beauty AOV from §4.6
(`output/optix_aov_beauty.ppm`) when the denoiser
runs successfully. If the file is byte-identical with
the Beauty AOV, the Stage 21D.5 noisy fallback fired —
check stderr for the `[WARN] denoise: ...; falling back
to noisy Beauty AOV` line and report the cause.

### 4.8 `--render <scene> --denoise` end-to-end

```
build/bin/RelativityRender \
    --render scenes/test_full_scene.rrscene \
    --output output/render.ppm \
    --denoise
```

Expected files:
- `output/render.ppm` (size > 1 KB; same noisy beauty
  the no-`--denoise` invocation produces; byte-identical
  per the Stage 21E.2 "do not change normal render path"
  rule).
- `output/denoised.ppm` (size > 1 KB; denoised radiance
  on a CUDA + OptiX SDK host; noisy Beauty fallback if
  the denoiser fails).

Visual sanity check: `output/denoised.ppm` should be
visibly smoother than `output/render.ppm`. If
byte-identical, check stderr for the noisy-fallback
warning line.

---

## 5. Aggregate PASS / REPAIR criteria

A CUDA-host verification PASSES when ALL of these hold:

| Criterion                                                  | Verified at  |
|------------------------------------------------------------|--------------|
| Build completes with 0 errors                              | §1           |
| `ctest` reports 100% pass                                  | §1.3         |
| `--device-info` reports ≥ 1 CUDA device                    | §2           |
| Every CUDA render command in §3 produces the expected PPM  | §3           |
| Every PPM in §3 is non-empty (size > 1 KB)                 | §3           |
| Every PPM in §3 is a valid P6 file (header check)          | §3           |
| (When OptiX SDK is present) every OptiX render command in  | §4           |
| §4 produces the expected PPM (size > 1 KB; valid P6)       |              |
| `--render --denoise` end-to-end produces both              | §4.8         |
| `output/render.ppm` and `output/denoised.ppm`              |              |
| No segfault / abort / unhandled exception in any command   | every step   |
| No "[ERROR]" log line on a successful exit-0 invocation    | every step   |

A verification REQUIRES REPAIR when ANY of these fail:

- Build / ctest non-zero exit → fix the build first
  (regression in current commit; do not proceed).
- A `--render-*` command exits non-zero on a CUDA host
  with the right hardware → check the error message;
  if "requires CUDA + OptiX" surfaces despite the build
  having both, check `OPTIX_ROOT` / SDK install.
- Expected PPM file does not appear → check stderr for
  the dispatcher's `Logger::error` line; if the kernel
  failed, capture the message and the
  `--device-info` output for a separate bug report.
- PPM file appears but size = 0 or < ~100 bytes → likely
  a save-time failure (out of disk / permissions / write
  to read-only filesystem); check the directory's
  permissions and free space.
- PPM file appears but `file <name>.ppm` does not
  report Netpbm format → save-time corruption; capture
  the stderr log + the file's first 32 bytes for a
  separate bug report.
- `--render --denoise` produces only `output/render.ppm`
  with no `output/denoised.ppm` AND no warning line on
  stderr → the new Stage 21E.2 path's denoise block
  was not reached (the operator might be on a CUDA-only
  build; rebuild with `RR_ENABLE_OPTIX=ON` per §1.2).
- `--render --denoise` produces both files BUT
  `output/denoised.ppm` is byte-identical with
  `output/render.ppm` AND no warning line on stderr →
  the denoiser silently no-op'd; this is a regression,
  capture stderr + the Beauty AOV state for diagnosis.

After the verification PASSES, the operator commits the
log of every command's stdout/stderr (or attaches them to
the verification record) so the empirical-deferral gate
that appears in every prior audit (Stage 13/14/15/19/20/
21) can be marked closed in a follow-up audit.

---

## 6. Coverage matrix

| Master order item              | Verified by §            |
|--------------------------------|---------------------------|
| #6 CUDA device layer           | §2 (`--device-info`)      |
| #7 Framebuffer / kernel infra  | §3.1 (gradient)           |
| #8 Camera system               | §3.2 (camera rays)        |
| #9 GPU primitive rendering     | §3.3 (sphere)             |
| #10 Relativistic camera model  | §3.4 (relativistic sphere) |
| #11 GPU scene upload           | §3.5 (test_spheres.rrscene)|
| #12 Mesh system                | §3.5 + §4.3               |
| #13 Material system            | §3.5 + §4.3 + §4.4        |
| #14 Lighting system            | §3.5 + §4.3               |
| #15 Scene format / parser      | §3.5 + §4.3 + §4.5        |
| #16 Path tracing foundation    | §3.8 + §4.5               |
| #17 OptiX upgrade path         | §4 (entire section)       |
| #18 Texture system             | §3.6 + §4.4               |
| #19 AOV / render passes        | §3.7 + §4.6               |
| #20 Renderer server            | NOT verified (deferred per |
|                                | `STAGE_15_SERVER_DEFERRED.md`)|
| #21+ (C4D / UI / node editor / | NOT verified (blocked     |
| native C4D renderer)           | per master rule 4)        |
| #24 Denoising                  | §4.7 + §4.8               |

The coverage table above + this plan's PASS criteria
together close the runtime-deferred gate every prior audit
documented. After a successful run the operator can mark
master order items #6 through #19 + #24 as
"empirically verified end-to-end on a real CUDA + OptiX
host", with the existing deferrals on #20 and #21+
preserved per the project's own documentation.

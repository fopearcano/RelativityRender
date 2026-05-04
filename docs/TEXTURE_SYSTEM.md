# Texture System

Status: v1 (TEX-P.4).
Branch: `relativity-core-v1`.
Master order: #18 (Texture system).
Scope: nearest-neighbour texture sampling on the GPU. Bilinear /
trilinear filtering, mipmaps, wrap-mode authoring, anisotropic
filtering, and texture node graphs are explicitly out of scope
for v1; see "Future work" below.

This file is the single normative spec for the texture system's
public contract. Code references `docs/TEXTURE_SYSTEM.md` from
header comments. The contract is allowed to grow (new format
slots, new fallback constants), but EVERY change must update
this file in the same slice.

---

## 1. Chosen UV policy: clamp-to-edge

The renderer's v1 UV addressing policy is **clamp-to-edge**.

For a sampled UV `uv = (u, v)`:

1. NaN components are replaced with `0.0` (TEX-P.3 GPU-safety
   guard; see §3 below).
2. The remaining components are clamped to `[0.0, 1.0]`:
   - `u_clamped = clamp(u, 0, 1)`
   - `v_clamped = clamp(v, 0, 1)`
3. The clamped UV is quantised to the nearest texel index:
   - `tx = floor(u_clamped * width )`, then clamped to
     `[0, width  - 1]` (covers the `u = 1.0` edge case).
   - `ty = floor(v_clamped * height)`, then clamped to
     `[0, height - 1]`.
4. The texel at `(tx, ty)` is read with row-major indexing and
   the format-appropriate stride (`Rgba8`: 4 bytes;
   `Rgba32F`: 16 bytes).

UV origin is the **top-left** texel: `(0, 0)` maps to texel
`(0, 0)`, and `(1, 1)` maps to texel `(width - 1, height - 1)`.

### Why clamp-to-edge (and not wrap)

- **No extra per-texture metadata.** A wrap policy makes sense
  alongside a per-texture `WrapMode` enum (`Clamp` /
  `Repeat` / `MirrorRepeat`); v1 does not author wrap modes,
  so committing to one global policy is the smaller surface.
- **Predictable seam behaviour.** Clamp-to-edge never folds
  the texture against itself, so a UV bug in authoring shows
  up as a colour smear at the edge rather than a silent tiled
  repetition.
- **Matches `GL_CLAMP_TO_EDGE` / `D3D11_TEXTURE_ADDRESS_CLAMP`**,
  which is the sane default in every other GPU API.
- **Matches the existing implementation** at
  `src/cuda/CudaTexture.cuh`'s `sampleTextureNearest`, which
  has used clamp-to-edge since Stage 13B.2. TEX-P.4 only
  formalises the choice; it does not change the byte output.

### Where the policy is enforced

ONE function — `rr::cuda::sampleTextureNearest`
(`src/cuda/CudaTexture.cuh`) — is the sole place where UV
addressing is interpreted. Both backends call it:

- CUDA: `src/cuda/CudaTestKernel.cu`'s shading branch
  invokes the sampler at hit time when
  `useBaseColorTexture` is set and the texture id is in
  range.
- OptiX: `src/optix/OptixPrograms.cu`'s closest-hit
  invokes the same `RR_HD inline` helper.

Because the helper is `RR_HD inline`, both backends inline
identical PTX / CPU code; the policy cannot diverge between
them without deleting the shared helper.

---

## 2. Invalid texture fallback behaviour

The sampler defends against six classes of bad input. Each
class returns a fallback colour rather than crashing the GPU
or reading out of bounds:

| Class                                   | Behaviour                                |
|-----------------------------------------|------------------------------------------|
| `view.pixels == nullptr`                | `kInvalidTextureFallback` (magenta).     |
| `view.width  <= 0`                      | `kInvalidTextureFallback`.               |
| `view.height <= 0`                      | `kInvalidTextureFallback`.               |
| `uv.x` or `uv.y` is NaN                 | NaN replaced with `0.0`, then clamped.   |
|                                         | Texel at the top-left corner is read.    |
| `uv.x` or `uv.y` is +/-inf              | Clamped to `[0, 1]` by the standard      |
|                                         | clamp; `+inf` collapses to `1.0`,        |
|                                         | `-inf` collapses to `0.0`.               |
| `view.format` is not a declared         | `kInvalidTextureFallback`.               |
| `ImageTextureFormat` value (corrupted   |                                          |
| / uninitialised byte)                   |                                          |

`kInvalidTextureFallback` is the single named constant
(`inline constexpr rr::math::Vec3{1.0f, 0.0f, 1.0f}`) defined
in `src/cuda/CudaTexture.cuh`. The colour is magenta so a
corrupted texture sample is unmistakable in the framebuffer.

### Material-aware fallback (kernel call sites)

Material-aware shaders gate the sampler call BEFORE invoking
it:

```
if (mat.useBaseColorTexture
 && mat.baseColorTextureId >= 0
 && mat.baseColorTextureId < scene.texture_count
 && scene.textures != nullptr) {
    albedo = sampleTextureNearest(
        scene.textures[mat.baseColorTextureId], hit.uv);
} else {
    albedo = mat.baseColor;
}
```

(`src/cuda/CudaTestKernel.cu` + `src/optix/OptixPrograms.cu`
both implement this gate.) Consequently, in correctly-authored
scenes the magenta fallback never surfaces — a missing or
mis-flagged texture surfaces as the material's flat
`baseColor` instead. The magenta is reserved for the case
where a caller invokes the sampler with a genuinely broken
view despite the gate, which is now defended against rather
than silently fatal.

### Three material flag/id cases (TEX-P.5)

The `useBaseColorTexture` flag and `baseColorTextureId`
integer combine into three artist-meaningful cases. Each row
gives the case's gate behaviour, the kernel result, and the
host-side validator action:

| `useBaseColorTexture` | `baseColorTextureId`            | Kernel-side gate          | Result                                   | Host validator                                       |
|-----------------------|---------------------------------|---------------------------|------------------------------------------|------------------------------------------------------|
| `false`               | any value                       | Short-circuits on flag    | Flat `baseColor` (Case 1).               | `Logger::info` if `id >= 0` (dangling id audit);     |
|                       |                                 | (id never evaluated)      |                                          | state NOT modified.                                  |
| `true`                | `[0, texture_count)`            | Full gate passes          | Sampled texel (Case 2).                  | No-op (happy path).                                  |
| `true`                | `< 0` OR `>= texture_count`     | Range-check fails         | Flat `baseColor` (Case 3).               | `Logger::warning` + clears the flag (counted as a    |
|                       |                                 |                           |                                          | "fixup" in the validator's return value).            |

Case ordering matches the validator's source comments at
`src/scene/Scene.cpp:validate_material_texture_ids` and the
kernel-side comments at `src/cuda/CudaTestKernel.cu` +
`src/optix/OptixPrograms.cu`.

Two corollaries:

- **Case 1 is intentional and silent at the kernel.** An
  artist who sets `baseColorTextureId = 5` but leaves
  `useBaseColorTexture = false` ships a flat-baseColor
  material; the kernel never reads the id, so there is no
  per-frame cost. The validator emits an info-level note
  (not a warning) so the artist can find the dangling
  assignment if they intended to enable the texture.
- **Case 3 is rare in production**: the host validator
  clears the flag on the first call, so any subsequent
  re-render hits Case 1 instead. The kernel-side range
  check is therefore defence-in-depth that fires only when
  a caller skipped validation OR when a render uses a
  scene that was constructed entirely inline (no
  `validate_material_texture_ids` call).

### Host-side validators (defence in depth)

TEX-P.2 added `rr::scene::validate_material_texture_ids`
(`src/scene/Scene.h`) which runs at scene-build / scene-load
time and clears `useBaseColorTexture` (with a
`Logger::warning` line) for any material whose
`baseColorTextureId` is outside `[0, texture_count)`. TEX-P.5
extends the same validator with the Case 1 info-note pass
(no state mutation). This gives the operator a visible
warning + info log; the kernel-time gate is the safety net.

---

## 3. Format dispatch and texel layout

| `ImageTextureFormat` | Stride | Channel mapping                          |
|----------------------|--------|------------------------------------------|
| `Rgba8`              | 4 B    | `bp[0..2] / 255.0` -> RGB; alpha dropped.|
| `Rgba32F`            | 16 B   | `fp[0..2]` -> RGB; alpha dropped.        |

Alpha is dropped at the sampler boundary because the only v1
shaders consuming texture samples (the texture-sample test
kernel and the material flat-shading branches) write opaque
output. Alpha-aware callers must sample directly without using
this helper.

The buffer-size invariant — `pixels` points to at least
`width * height * stride(format)` bytes — is enforced by
`rr::gpu::GpuTexture::upload_from`. The sampler relies on it;
violating it from a custom upload path is the caller's
responsibility.

---

## 4. Future work (explicitly out of scope for v1)

The following extensions are documented here so future slices
can find the deliberate-deferral note and avoid duplicating
the rationale:

- **Bilinear / trilinear filtering.** v1 is nearest-neighbour
  only. Adding bilinear would change the sampler's signature
  to take a filter mode and would interact with the texel-
  index clamp.
- **Mipmaps.** v1 has no mip chain. Mipmap support requires
  per-texture LOD metadata + a per-sample LOD computation
  (screen-space derivatives in CUDA, hardware texture units
  in OptiX), neither of which exists today.
- **Wrap-mode metadata.** v1 commits globally to
  clamp-to-edge. A future slice may add a `WrapMode` enum
  (`Clamp` / `Repeat` / `MirrorRepeat`) on
  `rr::texture::ImageTexture` plumbed through the
  `DeviceTextureView` POD. The sampler's clamp branch would
  then dispatch on the enum.
- **Anisotropic filtering.** Out of scope for v1.
- **Texture node graph.** Master order #23 work, deferred
  until the renderer has materials + lighting + path tracing
  shipped. The current `MaterialParams::useBaseColorTexture`
  / `baseColorTextureId` pair is the v1 binding; a node
  graph would replace it.
- **Hardware texture units / `cudaTextureObject_t`.** v1
  uses raw pointer + manual nearest fetch so the same sampler
  works under both CUDA `__device__` and OptiX program
  contexts. Switching to hardware texture objects would
  require splitting the sampler into per-backend
  implementations.
- **Additional formats.** Only `Rgba8` and `Rgba32F` are
  declared today. Adding `R8` / `Rg8` / `Bc1` / `EAC` /
  `ASTC` / etc. requires a new stride entry + a new
  `case` arm in `sampleTextureNearest`'s format switch.
  TEX-P.3's default arm guarantees an unknown format byte
  cannot misinterpret pixel bytes during the migration.

---

## 5. Verification

The host-side build type-checks the sampler header through
`tests/optix_tests.cpp`'s unconditional include of
`OptixLaunchParams.h`, which transitively pulls
`cuda/CudaTexture.cuh`. Both audit-host configs (`build`
with `RR_ENABLE_CUDA=OFF + RR_ENABLE_OPTIX=OFF`, and
`build-ON` with `RR_ENABLE_OPTIX=ON`) compile the header
and run their respective ctest suites green (6/6 and 7/7).

Runtime sampling — both happy-path texel reads and the six
fallback classes — is exercised on a CUDA host through:

- `--render-texture-sample-test` (Stage 13B.2 reference).
- `--render-textured-material` (Stage 13B.3).
- `--render-optix-textured-material` (Stage 20M).

The audit host in this repository has neither `nvcc` nor an
OptiX SDK installed, so empirical PPM verification is the
operator's job on a CUDA + (optional) SDK host. The
`tools/verify_cuda_host.py` runner records expected PASS /
FAIL outcomes per `docs/CUDA_HOST_VERIFICATION_PLAN.md`.

---

## 6. Change log

- TEX-P.6: added `scenes/test_textured_material.rrscene`
  fixture (four materials covering Cases 1 / 2-collapse-to-3
  / no-binding / 3) + minimal `.rrscene` loader support for
  `use_base_color_texture` (bool) and `base_color_texture_id`
  (int). The `--scene-info` dispatcher now runs
  `validate_material_texture_ids` post-load so the three
  cases fire visibly when the fixture is loaded. The
  `textures` top-level scene key is still reserved for a
  future slice (no inline pixel-data loading yet).
- TEX-P.5: extended `validate_material_texture_ids` with the
  Case 1 (flag-OFF) info-note audit, added TEX-P.5 references
  to both kernel-side gate comments
  (`CudaTestKernel.cu`, `OptixPrograms.cu`), and added §2's
  three-case flag/id table to make the rule unambiguous.
- TEX-P.4: created this doc; committed the renderer to
  clamp-to-edge as the v1 UV policy; documented the TEX-P.3
  invalid-texture fallback contract; listed future-work
  deferrals.
- TEX-P.3: GPU sampler safety hardening (NaN-UV guard +
  explicit format default arm).
- TEX-P.2: host-side `validate_material_texture_ids`
  defence-in-depth for material texture-id authoring.
- TEX-P.1: texture polish plan (`docs/TEXTURE_POLISH_PLAN.md`).
- Stage 20M: OptiX path picks up the same `RR_HD` helper.
- Stage 13B.2 / 13B.3: original CUDA-side sampler + textured-
  material dispatcher.

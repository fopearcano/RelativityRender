# Texture System — Polish Plan

Date: 2026-05-04
Branch: `relativity-core-v1`
Master order: #18 (Texture system).
Sources read: `docs/CUDA_HOST_VERIFICATION_REPORT.md`,
`docs/CUDA_HOST_VERIFICATION_AUDIT.md`,
`docs/STAGE_13_AUDIT_VERDICT.md`,
`docs/BUILD_PLAN.md`.
Mode: documentation-only. No source code is modified by
this plan. NO new CLI flags. The plan is the spec; future
slices implement individual items per the project's
established slice cadence.

The plan answers four prompt sections in order; the closing
section recommends the smallest first polish item.

---

## 1. Current texture status

The texture system landed across Stage 13B and was extended
in Stage 20M (OptiX-side). Today's surface:

### Host-side data model (Stage 13B.1 baseline)

- `rr::texture::ImageTexture` (`src/texture/ImageTexture.h`):
  host-side image with width / height / format
  (`Rgba8` or `Rgba32F`) + a row-major top-left pixel
  buffer.
- `rr::texture::Texture` (`src/texture/Texture.h`): tagged
  union of `ImageTexture` (today the only kind) +
  metadata (id, name).
- `rr::material::MaterialParams` (`src/material/MaterialTypes.h:52-53`):
  carries `int baseColorTextureId = -1` (index into
  `scene.textures`, `-1` = none) + `bool useBaseColorTexture
  = false` (gate; `false` uses flat `baseColor`).
- `rr::scene::Scene::textures`: a flat `vector<Texture>`
  the renderer uploads at scene-load time.

### Device-side sampler (Stage 13B.2)

- `rr::cuda::DeviceTextureView` (`src/cuda/CudaTexture.cuh:54-59`):
  POD with `pixels` device pointer + width / height + format
  flag.
- `rr::cuda::sampleTextureNearest(view, uv)`
  (`src/cuda/CudaTexture.cuh:90-128`): RR_HD inline
  nearest-neighbour sampler with hard-coded
  clamp-to-edge UV addressing. UVs outside `[0, 1]` are
  clamped via `rr::math::clamp(...)` (no wrap mode). On
  invalid view (null pixels OR non-positive dims) the
  function returns magenta `(1, 0, 1)` so the failure is
  visible in the framebuffer. Format dispatch on `Rgba8`
  vs `Rgba32F` — alpha is dropped.

### Render-side wiring

- CUDA path (`src/cuda/CudaTestKernel.cu:401-406`): the
  `k_render_scene` shading branch checks
  `mat.useBaseColorTexture &&
  mat.baseColorTextureId >= 0 &&
  mat.baseColorTextureId < scene.texture_count` before
  calling `sampleTextureNearest(scene.textures[id], hit.uv)`.
- OptiX path (`src/optix/OptixPrograms.cu:586-626`):
  closest-hit's `shading_mode == 1` branch performs the
  same three-fold gate (`useBaseColorTexture` true,
  `baseColorTextureId` in range, three launch-params
  pointers non-null) before calling the same RR_HD
  helper. Falls back to flat `params.baseColor`
  otherwise.

### CLI surface

| CLI                                  | Output                                | Stage  |
|--------------------------------------|---------------------------------------|--------|
| `--render-texture-sample-test`       | `output/gpu_texture_sample_test.ppm`  | 13B.2  |
| `--render-textured-material`         | `output/gpu_textured_material.ppm`    | 13B.3  |
| `--render-optix-textured-material`   | `output/optix_textured_material.ppm`  | 20M    |

All three dispatchers build the scene inline (no
`.rrscene` test fixture today exercises textured
materials).

### Known caveats (from STAGE_13_AUDIT_VERDICT § 2)

- "No mipmap, no wrap-mode metadata, no normal /
  metallic / roughness texture slots" — verbatim from
  the verdict.
- Texture filtering is nearest-neighbour only.
- The Stage 13 PPM artifacts have NOT been empirically
  confirmed on a CUDA host (the audit host has no
  `nvcc`); STAGE_13_AUDIT_VERDICT § 3 recommends a
  one-time visual confirmation as the only remaining
  gap.

---

## 2. Known deferred runtime checks

The CUDA-host verification runner
(`tools/verify_cuda_host.py`,
`docs/CUDA_HOST_VERIFICATION_PLAN.md`) covers the three
texture CLI surfaces above:

- `render-texture-sample-test` →
  `output/gpu_texture_sample_test.ppm`.
- `render-textured-material` →
  `output/gpu_textured_material.ppm`.
- (when `--optix` is set)
  `render-optix-textured-material` →
  `output/optix_textured_material.ppm` would land here
  too once the OptiX command catalogue grows beyond
  the CUDA-H.8 minimum (raygen / triangle / pathtrace).
  Currently the texture-on-OptiX command is NOT in
  the runner's `optix_commands()` list; adding it is a
  separate runner-extension slice.

The audit host's `docs/CUDA_HOST_VERIFICATION_REPORT.md`
records both texture commands as `FAIL` (the
documented "requires CUDA" audit-host fallback fires).
On a CUDA + (optional) OptiX-SDK host the same
commands flip to `PASS` per the runner's
deterministic spec.

---

## 3. Small safe polish items

Five polish items, each scoped small enough for a single
slice (target diff ≤ 50 lines per item, plus a
BUILD_PLAN entry).

### 3.1 Invalid-texture fallback consistency

**Status today.** The CUDA-side sampler
(`sampleTextureNearest`) returns magenta `(1, 0, 1)` on
an invalid view. The OptiX-side closest-hit checks
`useBaseColorTexture && baseColorTextureId >= 0 &&
baseColorTextureId < texture_count && all-three-launch-
params-non-null` BEFORE calling the sampler; if any
gate fails the OptiX path falls back to flat
`params.baseColor` (NOT magenta). The two backends
diverge on what an artist sees when a texture is
broken.

**Polish item.** Pick ONE behaviour for both backends.
Two reasonable choices:

- **Magenta fallback everywhere.** The OptiX gate's
  fallback would change to `(1, 0, 1)` so a missing
  texture is always visually loud. Pro: matches the
  established Stage 13B.2 contract documented in the
  sampler's header comment. Con: surprises operators
  who expect "missing texture" to silently use the
  material's `baseColor`.
- **`baseColor` fallback everywhere.** The CUDA
  sampler's magenta path would change to return
  `mat.baseColor`. Pro: matches the OptiX path's
  behaviour today. Con: requires the sampler to take
  an extra argument; loses the "loud failure" property.

The first choice (magenta everywhere) is the smaller
diff: one OptiX `else` branch becomes `Vec3{1, 0, 1}`
instead of `params.baseColor`. The second requires
plumbing `baseColor` through the sampler signature.

**Out-of-scope for this slice.** Deciding which is
right; that is the operator's call. The polish item is
just to FLAG the inconsistency; the implementation
slice picks a side.

### 3.2 UV clamp / wrap consistency

**Status today.** Both backends use clamp-to-edge UV
addressing (the CUDA sampler explicitly via
`rr::math::clamp`, the OptiX path implicitly because it
calls the same RR_HD inline helper). The texture
sampler's header comment (`CudaTexture.cuh:73-78`)
documents the clamp choice + the absence of
wrap-mode metadata.

**Polish item.** Add a per-texture `WrapMode` enum
field (`Clamp` / `Repeat`) to either
`rr::texture::ImageTexture` (host-side metadata) or
`MaterialParams` (per-material override) +
`rr::cuda::DeviceTextureView` (so the device side can
read it without a host roundtrip). Update the
`sampleTextureNearest` body to honour the chosen mode:

```
const float u_eff = (mode == Repeat)
    ? uv.x - std::floor(uv.x)
    : rr::math::clamp(uv.x, 0.0f, 1.0f);
```

Default stays `Clamp` so existing renders are byte-
identical. Add a new `--render-texture-sample-test
--wrap-repeat` modifier (or a second test scene) so
the polish has a verifiable artifact.

**Caveat.** This grows `MaterialParams` (a POD that
already touches the GAS hit-record's SBT data on the
OptiX side) — the slice must verify that the OptiX
hit-record's `MaterialParams` carrier still aligns +
hashes correctly through `set_hit_material`.

### 3.3 Texture-id validation at scene load

**Status today.** The CUDA + OptiX kernels both check
`baseColorTextureId >= 0 &&
baseColorTextureId < texture_count` AT KERNEL TIME,
every render, every pixel. The check itself is cheap,
but it surfaces a bad scene file silently (the
material falls back to flat `baseColor`; the artist
gets no log line).

**Polish item.** Add a one-pass scene validator that
runs after `rr::io::load(scene_path)` returns, before
the renderer uploads anything to the GPU. For each
material with `useBaseColorTexture == true`:

- Verify `baseColorTextureId >= 0`.
- Verify `baseColorTextureId < scene.textures.size()`.
- Verify the referenced texture's `pixels` is non-empty
  + dimensions are positive.

On any failure: `Logger::warning("scene: material '" +
mat.name + "' references invalid texture id " +
std::to_string(mat.baseColorTextureId) + "; falling
back to baseColor")` AND set `useBaseColorTexture =
false` host-side so the kernel skips the runtime check.

**Cost.** ~15 lines in `src/io/SceneLoader.cpp` (or a
new `src/scene/SceneValidator.cpp` if the operator
wants a separate module). No new CLI flag.

### 3.4 Material texture-flag validation

**Status today.** A material can set `baseColorTextureId
= 5` while leaving `useBaseColorTexture = false` (the
ID is stored but ignored). And vice-versa: a material
can set `useBaseColorTexture = true` with
`baseColorTextureId = -1` (the kernel's `>= 0` gate
falls back to flat). Both states are accidentally-
consistent today (no crash, no wrong pixel) but
silently wrong from the artist's perspective.

**Polish item.** Add a one-pass material validator
after `rr::io::load` (could share a module with §3.3).
Three rules:

- **Rule 1.** If `useBaseColorTexture == true` AND
  `baseColorTextureId < 0`: log a warning + set
  `useBaseColorTexture = false`.
- **Rule 2.** If `useBaseColorTexture == false` AND
  `baseColorTextureId >= 0`: log an info-level note
  ("material 'X' has baseColorTextureId=N but
  useBaseColorTexture is false; texture is not
  applied"). Do NOT modify state — the artist may have
  intended this as a draft.
- **Rule 3.** Cross-check texture id is in range
  (covered by §3.3; could fold into one validator
  pass).

**Cost.** ~10 lines on top of §3.3. Can land as the
same slice or a follow-up.

### 3.5 Test-scene coverage

**Status today.** No `.rrscene` test fixture under
`scenes/` exercises `useBaseColorTexture` or
`baseColorTextureId`. The three texture CLI surfaces
all build their scenes inline (the tests run is hard-
coded to a 2x2 four-colour reference texture). This
means:

- A Stage 15 SceneLoader regression that mishandles
  `baseColorTextureId` parsing wouldn't be caught by
  any audit-host smoke or by the CUDA-H.x runner.
- An operator wanting to verify texture support on a
  custom scene has no fixture to copy from.

**Polish item.** Add `scenes/test_textured.rrscene`
that authors a single textured-quad mesh + a single
material with `useBaseColorTexture = true` +
`baseColorTextureId = 0` + a single inline texture
(2x2 four-colour pattern matching the CUDA test
kernel's reference). Wire the runner's
`render-scene-spheres` row to ALSO accept
`scenes/test_textured.rrscene` as an alternative
target via a new optional Command, OR add a sibling
`render-scene-textured` Command per the
CUDA_HOST_VERIFICATION_PLAN's §3.5 pattern.

**Caveat.** The `.rrscene` format must already
support per-texture pixel-data inlining (or a
companion .ppm reference). If it doesn't, this polish
item depends on a Stage 15 SceneLoader extension that
is OUT OF SCOPE for the texture system polish; in
that case §3.5 deferes to a separate slice.

**Verification cost.** Once the fixture exists, the
runner's existing CUDA-H.5 file-existence check
covers the new PPM output. No new runner code.

---

## 4. Recommended first polish item

**§3.4 — Material texture-flag validation.**

Rationale (one paragraph): all five items are safe, but
§3.4 is the smallest viable diff (~10 lines in a single
file, no new CLI surface, no schema change, no
backward-compat risk). It catches a real authoring
class of bugs (silent mis-flagged textures) without
requiring any kernel or scene-format change. It also
naturally subsumes §3.3 if the implementer wants to do
both texture-id range checks + flag validation in one
sweep — both are scene-validation passes that run
post-`rr::io::load` and emit `Logger::warning` lines.
After §3.4 lands, §3.1 (invalid-texture fallback
consistency) is the natural follow-up because it
aligns the two backends' run-time fallback semantics
once the host-side input contract is enforced.

The remaining items
(§3.2 UV clamp/wrap, §3.5 test-scene coverage) are
larger / depend on Stage 15 work and should be
deferred to separate slices.

### Why not §3.5 first

Test-scene coverage looks like the smallest "operator
wins" item, but it depends on the `.rrscene` schema's
ability to inline per-texture pixel data. The current
SceneLoader (Stage 15) parses materials + texture id
references but the texture pixel data path through the
loader is not audited in this plan; a §3.5 attempt
might surface a schema gap that turns it into a
multi-slice item. Keeping §3.5 deferred avoids that
risk.

### Why not §3.1 first

The OptiX vs CUDA fallback divergence is real but the
existing behaviour (OptiX falls back to `baseColor`)
is the more "production-friendly" choice, so artists
using the OptiX path today are not inconvenienced.
§3.1 is best landed AFTER §3.4 because once the
host-side input contract is enforced (no
mis-flagged textures reach the kernel), the runtime
fallback only fires on actual GPU-side issues
(invalid `DeviceTextureView`), which are rarer and
benefit from the loud magenta indicator.

---

## Closing notes

This plan adds no source. The next concrete slice
should be:

1. **TEX-P.2 — Material texture-flag validation
   implementation slice.** Implement §3.4 in the
   smallest possible diff. Optionally fold in §3.3.
   Keep the OFF + ON-audit-host ctest baselines
   green; add a BUILD_PLAN entry.

After that lands the operator may continue with §3.1,
§3.2, or §3.5 in any order, or pick a different
master-order item entirely. Per
`docs/STAGE_21_DENOISER_AUDIT.md` §9 the texture polish
arc is one of three viable next steps (along with
master #16 path-tracer polish + the empirical
CUDA-host verification run).

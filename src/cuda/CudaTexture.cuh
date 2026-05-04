#pragma once

// CUDA-side view + nearest-neighbor sampler for the texture data
// model (Stage 13B.2; master order #18).
//
// Two pieces:
//
// 1. `DeviceTextureView` - a small POD describing one device-resident
//    texture: device pointer to pixel bytes, dimensions, and the
//    `rr::texture::ImageTextureFormat` flag. The host-side
//    `rr::gpu::GpuTexture` builds this view at launch time from its
//    own buffer + metadata; kernels receive the view by value as a
//    launch argument (the same pattern as `CudaSceneView` /
//    `CudaMeshView`). `GpuTexture` is the host-side RAII owner;
//    `DeviceTextureView` is the kernel-side reader.
//
// 2. `sampleTextureNearest(view, uv)` - an `RR_HD inline` nearest-
//    neighbor sampler. UVs outside `[0, 1] x [0, 1]` are
//    clamp-to-edge'd (matching `GL_CLAMP_TO_EDGE` /
//    `D3D11_TEXTURE_ADDRESS_CLAMP`). Returns RGB as
//    `rr::math::Vec3`; alpha is dropped because the only consumer
//    today (a CUDA validation kernel) writes opaque output. On an
//    invalid view the function returns a fallback magenta
//    `(1, 0, 1)` so the failure is unmistakable in any output
//    framebuffer.
//
// No `cudaTextureObject_t` lifecycle, no bindless-texture upload, no
// mipmap chain, no filter/wrap-mode metadata yet. Stage 13B.2 ships
// the *minimum* device-side sampler the renderer can build on; the
// hardware texture-unit path is a future sub-stage.
//
// This header is host-friendly: it depends only on `<cstddef>`,
// `RR_HD`, `Vec2`, `Vec3`, and the host-side `Texture` /
// `ImageTexture` headers. No `<cuda_runtime.h>` include is required
// because `RR_HD` already paints the function with both
// `__host__` and `__device__` when compiled by nvcc.

#include "math/MathUtils.h"  // RR_HD + clamp
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "texture/ImageTexture.h"
#include "texture/Texture.h"

#include <cstddef>

namespace rr::cuda {

// Small POD that describes a device-resident texture for kernels.
// Built host-side from `rr::gpu::GpuTexture` (see
// `gpu/GpuTexture.h`). Default-constructed is "no texture": all
// fields zero / null. `device_texture_view_valid` reports whether
// the view is usable; the sampler falls back to magenta on any
// invalid view rather than dereferencing a null pointer.
struct DeviceTextureView {
    const std::byte*                pixels = nullptr;
    int                             width  = 0;
    int                             height = 0;
    rr::texture::ImageTextureFormat format = rr::texture::ImageTextureFormat::Rgba8;
};

// True iff the view has a non-null pointer + positive dimensions.
// Format is not validated here - the sampler dispatches on it
// directly.
[[nodiscard]] RR_HD inline bool device_texture_view_valid(
        const DeviceTextureView& v) noexcept {
    return v.pixels != nullptr && v.width > 0 && v.height > 0;
}

// Magenta `(1, 0, 1)` is the universal "invalid texture sample"
// fallback. Anywhere the sampler cannot safely produce a real
// texel value it returns this colour so the failure shows up
// unmistakably in the framebuffer rather than silently corrupting
// neighbouring pixels. Material-aware call sites (e.g.
// `CudaTestKernel.cu`, `OptixPrograms.cu`) gate around the
// sampler at hit time and substitute `params.baseColor` instead
// of letting the magenta surface — TEX-P.3 deliberately keeps the
// sampler-level fallback distinct so a sampler invoked with a
// genuinely broken view (caller forgot the kernel-side gate)
// still cannot crash the GPU.
inline constexpr rr::math::Vec3 kInvalidTextureFallback{1.0f, 0.0f, 1.0f};

// Nearest-neighbor sampling with clamp-to-edge UV addressing.
//
// `uv` is in `[0, 1] x [0, 1]` with origin at the top-left texel
// (texel (0, 0) -> uv = (0, 0); texel (W-1, H-1) -> uv = (1, 1)).
// UVs outside the unit square are clamped to it (no `floor` /
// `frac` wrap) - the spec calls for "clamp or wrap UVs
// consistently"; clamp is chosen here because it requires no
// extra metadata, never folds the texture against itself, and
// matches what a sampler with default GL_CLAMP_TO_EDGE settings
// would do.
//
// Returns the texel's RGB components as a `Vec3` in `[0, 1]`:
// - `Rgba8`   : each unsigned byte / 255.0f
// - `Rgba32F` : the stored float values
// Alpha is dropped (validation kernel writes opaque output;
// alpha-aware callers can bypass this helper).
//
// TEX-P.3 GPU safety contract. Every input that could otherwise
// crash the GPU or read out-of-bounds is converted into a
// fallback colour return:
//   1. `view.pixels == nullptr`             -> fallback
//   2. `view.width  <= 0` (incl. negative)  -> fallback
//   3. `view.height <= 0` (incl. negative)  -> fallback
//   4. `uv.x` or `uv.y` is NaN              -> treated as 0
//   5. `uv.x` or `uv.y` is +/-inf           -> clamped to [0, 1]
//   6. `view.format` is not a declared      -> fallback
//      `ImageTextureFormat` value
// The texel index produced by the clamp + quantise step is
// always in `[0, width*height)` provided checks 1-3 hold, so the
// pointer arithmetic in the format branches cannot read past the
// owning buffer (assuming the upload path sized the buffer for
// `width * height * stride(format)` bytes — which is an upstream
// invariant that `GpuTexture::upload_from` already enforces).
[[nodiscard]] RR_HD inline rr::math::Vec3 sampleTextureNearest(
        const DeviceTextureView& view,
        rr::math::Vec2           uv) noexcept {
    // Guard 1-3: pointer + dimension validity. Folded into one
    // helper so the contract is a single boolean test.
    if (!device_texture_view_valid(view)) {
        return kInvalidTextureFallback;
    }

    // Guard 4-5: NaN UV would propagate through `clamp` and turn
    // `static_cast<int>(NaN * width)` into undefined behaviour
    // (no defined integer mapping for NaN). Replace NaN with 0
    // before the clamp; +/-inf is handled correctly by `clamp`
    // (-inf < 0 -> returns 0; 1 < +inf -> returns 1) so it
    // needs no special case.
    const float u_in   = (uv.x == uv.x) ? uv.x : 0.0f;
    const float v_in   = (uv.y == uv.y) ? uv.y : 0.0f;
    const float u_clamped = rr::math::clamp(u_in, 0.0f, 1.0f);
    const float v_clamped = rr::math::clamp(v_in, 0.0f, 1.0f);

    int tx = static_cast<int>(u_clamped * static_cast<float>(view.width));
    int ty = static_cast<int>(v_clamped * static_cast<float>(view.height));
    // A UV of exactly 1.0 lands on `width`/`height`; clamp the
    // last texel too. Negative dimensions are caught by the
    // valid-view check above.
    if (tx >= view.width)  tx = view.width  - 1;
    if (ty >= view.height) ty = view.height - 1;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;

    const std::size_t texel_index =
        static_cast<std::size_t>(ty) * static_cast<std::size_t>(view.width)
      + static_cast<std::size_t>(tx);

    // Guard 6: explicit format dispatch. The default arm catches
    // any future / corrupted enum value rather than letting it
    // fall through and misinterpret the pixel bytes.
    switch (view.format) {
        case rr::texture::ImageTextureFormat::Rgba32F: {
            // 16 bytes / texel = 4 floats.
            const float* fp = reinterpret_cast<const float*>(view.pixels)
                            + texel_index * 4u;
            return rr::math::Vec3{fp[0], fp[1], fp[2]};
        }
        case rr::texture::ImageTextureFormat::Rgba8: {
            // 4 bytes / texel; each channel byte maps
            // `[0, 255]` -> `[0, 1]`.
            const unsigned char* bp =
                reinterpret_cast<const unsigned char*>(view.pixels)
              + texel_index * 4u;
            constexpr float kInv255 = 1.0f / 255.0f;
            return rr::math::Vec3{
                static_cast<float>(bp[0]) * kInv255,
                static_cast<float>(bp[1]) * kInv255,
                static_cast<float>(bp[2]) * kInv255
            };
        }
    }
    // Unknown / corrupted format value: fall through to fallback
    // rather than guess a stride. Reachable in practice only if
    // the upload path or the launch params carry a bogus enum
    // byte; both contracts are stricter than that today, so this
    // arm is defence in depth.
    return kInvalidTextureFallback;
}

}  // namespace rr::cuda

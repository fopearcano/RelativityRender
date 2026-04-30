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
// Safe fallback: if `device_texture_view_valid(view)` is false
// (null pixels OR non-positive dimensions) the function returns
// `(1, 0, 1)` magenta. The kernel never crashes; the output
// pixel reflects the failure visually.
[[nodiscard]] RR_HD inline rr::math::Vec3 sampleTextureNearest(
        const DeviceTextureView& view,
        rr::math::Vec2           uv) noexcept {
    if (!device_texture_view_valid(view)) {
        return rr::math::Vec3{1.0f, 0.0f, 1.0f};
    }

    // Clamp-to-edge in UV space, then quantize to a texel index.
    const float u_clamped = rr::math::clamp(uv.x, 0.0f, 1.0f);
    const float v_clamped = rr::math::clamp(uv.y, 0.0f, 1.0f);

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

    if (view.format == rr::texture::ImageTextureFormat::Rgba32F) {
        // 16 bytes / texel = 4 floats.
        const float* fp = reinterpret_cast<const float*>(view.pixels)
                        + texel_index * 4u;
        return rr::math::Vec3{fp[0], fp[1], fp[2]};
    }

    // Default branch covers `Rgba8`, the only other declared
    // format today. 4 bytes / texel; each channel byte maps
    // `[0, 255]` -> `[0, 1]`.
    const unsigned char* bp = reinterpret_cast<const unsigned char*>(view.pixels)
                            + texel_index * 4u;
    constexpr float kInv255 = 1.0f / 255.0f;
    return rr::math::Vec3{
        static_cast<float>(bp[0]) * kInv255,
        static_cast<float>(bp[1]) * kInv255,
        static_cast<float>(bp[2]) * kInv255
    };
}

}  // namespace rr::cuda

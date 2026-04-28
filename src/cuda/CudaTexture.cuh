#pragma once

#include "math/MathUtils.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "texture/Texture.h"

#include <cstdint>

// CUDA-side launch-argument POD for a single texture, plus an
// `RR_HD inline` sampler. Despite the `.cuh` extension the header
// pulls in **no** CUDA-runtime types - it is host- and device-
// callable so the same code paths run inside kernels and inside
// host unit tests.
//
// The `.cuh` name is the project convention for "CUDA-side
// surface"; future device-specific overrides (texture object
// fetches, `tex2D<...>` paths, fast-math intrinsics) land here
// without changing the host surface.

namespace rr::cuda {

// Tagged-union device-side view of a texture. The host builds
// one of these per uploaded texture before launch. `type` decides
// which other fields are meaningful:
//
//   Constant  -> only `constant_color` is read.
//   Image     -> `image_data` + `image_width` + `image_height` +
//                `image_channels` describe the device pixel
//                buffer; `wrap_*` and `filter` select sampling
//                semantics.
//
// Layout matches the host's `rr::image::Image` storage: row-major,
// channel-interleaved floats with `image_channels in {3, 4}`.
struct TextureView {
    rr::texture::TextureType type = rr::texture::TextureType::Constant;

    // Used by Constant. Also serves as the fallback colour when
    // an Image texture has no device data uploaded yet (e.g.
    // host-only build) - the kernel returns `constant_color`
    // instead of crashing on a null pointer.
    rr::math::Vec3 constant_color = rr::math::Vec3{1.0f, 1.0f, 1.0f};

    // Used by Image.
    const float* image_data     = nullptr;
    int          image_width    = 0;
    int          image_height   = 0;
    int          image_channels = 3;

    // Sampling params (matches `rr::texture::ImageTexture`'s
    // wrap / filter enums by ordinal).
    int wrap_u = 0;   // 0=Clamp, 1=Repeat, 2=Mirror
    int wrap_v = 0;
    int filter = 0;   // 0=Nearest, 1=Bilinear
};

// Device- and host-callable sampler. Nearest-neighbor with clamp
// wrapping is the only filter / wrap pair honored at this
// milestone; the others fall through to the same path so callers
// get a sane value until the full sampler lands. Bilinear and
// repeat / mirror arrive in a later M16 slice.
RR_HD inline rr::math::Vec3 sample_texture(const TextureView& tex,
                                           rr::math::Vec2 uv) {
    // Constant texture - or an Image with no device data. The
    // null-data fallback keeps the kernel predictable on
    // host-only builds where the upload path is a no-op.
    if (tex.type == rr::texture::TextureType::Constant
        || tex.image_data == nullptr
        || tex.image_width  <= 0
        || tex.image_height <= 0) {
        return tex.constant_color;
    }

    // Clamp wrap.
    float u = rr::math::clamp(uv.x, 0.0f, 1.0f);
    float v = rr::math::clamp(uv.y, 0.0f, 1.0f);

    // Top-left origin (matches Image layout); v = 0 sits at the
    // bottom of the texture.
    int x = static_cast<int>(u * tex.image_width);
    int y = static_cast<int>((1.0f - v) * tex.image_height);
    if (x >= tex.image_width)  x = tex.image_width  - 1;
    if (y >= tex.image_height) y = tex.image_height - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    const int idx = (y * tex.image_width + x) * tex.image_channels;
    return rr::math::Vec3{tex.image_data[idx + 0],
                          tex.image_data[idx + 1],
                          tex.image_data[idx + 2]};
}

}

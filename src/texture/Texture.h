#pragma once

#include "math/MathUtils.h"   // RR_HD
#include "math/Vec2.h"
#include "math/Vec3.h"

#include <cstdint>

// Texture system foundation.
//
// Two host-side concrete types: `ConstantTexture` (returns a fixed
// RGB regardless of UV) and `ImageTexture` (samples a host
// `rr::image::Image`; lives in `texture/ImageTexture.h`). Their
// shared discriminator is the `TextureType` enum below; the
// device-side launch-argument POD (`rr::cuda::TextureView`) tags
// itself with the same enum so kernels can branch on it.
//
// At this milestone the sampler is nearest-neighbor with clamp
// wrapping; bilinear / mipmap / anisotropic filtering arrive in a
// later slice. UV coordinates are surfaced as a `Vec2` parameter
// throughout; nothing in the renderer's kernels reads them yet
// (no material slot is textured), so per the "UV coordinates
// placeholder" requirement the parameter is reserved at every
// public surface for forward compatibility.

namespace rr::texture {

// Tagged-union discriminator. The CUDA `TextureView` branches on
// this to pick its sample path. Stable ordinals so the upload
// shape stays forward-compatible if more texture kinds land
// (procedural, layered, gradient, etc.).
enum class TextureType : std::int32_t {
    Constant = 0,
    Image    = 1,
};

// Constant-color texture. Ignores UV and returns a fixed RGB.
// The renderer's default for any material slot without a real
// texture binding.
struct ConstantTexture {
    rr::math::Vec3 color = rr::math::Vec3{1.0f, 1.0f, 1.0f};

    // Host- and device-callable. `uv` is accepted but unused.
    RR_HD inline rr::math::Vec3 sample(rr::math::Vec2 /*uv*/) const {
        return color;
    }

    [[nodiscard]] static constexpr TextureType type_tag() {
        return TextureType::Constant;
    }
};

// Convenience factories for common constants.
[[nodiscard]] ConstantTexture make_white_texture();
[[nodiscard]] ConstantTexture make_black_texture();
[[nodiscard]] ConstantTexture make_constant_texture(rr::math::Vec3 color);

}

#pragma once

#include "image/Image.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "texture/Texture.h"

#include <cstdint>

// Image-backed texture.
//
// Holds a host-side `rr::image::Image` (the same Rgb32F / Rgba32F
// pixel buffer the rest of the project already produces) plus
// sampling parameters. Sampling parameters are surfaced now so the
// scene format / GPU upload paths can carry them; the host
// sampler implements only the simplest combination today
// (nearest-neighbor, clamp wrap) per the M16 milestone's
// "no complex filtering yet" rule.
//
// The eventual GPU sampler (`rr::cuda::sample_texture` in
// `cuda/CudaTexture.cuh`) consumes the same parameter set through
// the device-side `TextureView` POD. Keeping them aligned today
// means the device path stays bit-equivalent to the host path
// when the rest of the texture system catches up.

namespace rr::texture {

class ImageTexture {
public:
    // Wrapping mode for out-of-`[0,1]` UVs. Only `Clamp` is honored
    // by the sampler at this milestone; `Repeat` and `Mirror` are
    // documented for the device-side kernel and the .rrscene
    // format that lands later.
    enum class Wrap : std::int32_t {
        Clamp  = 0,
        Repeat = 1,
        Mirror = 2,
    };

    // Filtering mode. Only `Nearest` is honored today; `Bilinear`
    // ships with the next M16 slice.
    enum class Filter : std::int32_t {
        Nearest  = 0,
        Bilinear = 1,
    };

    ImageTexture() = default;
    explicit ImageTexture(rr::image::Image image);

    // Always succeeds; the eventual GPU upload may or may not
    // succeed depending on backend availability.
    void set_image(rr::image::Image image);
    void set_wrap_u(Wrap w)    { wrap_u_ = w; }
    void set_wrap_v(Wrap w)    { wrap_v_ = w; }
    void set_filter(Filter f)  { filter_ = f; }

    [[nodiscard]] const rr::image::Image& image()  const { return image_; }
    [[nodiscard]] Wrap                    wrap_u() const { return wrap_u_; }
    [[nodiscard]] Wrap                    wrap_v() const { return wrap_v_; }
    [[nodiscard]] Filter                  filter() const { return filter_; }

    [[nodiscard]] bool empty() const { return image_.empty(); }
    [[nodiscard]] int  width()  const { return image_.width(); }
    [[nodiscard]] int  height() const { return image_.height(); }

    // Host-side sampler. Returns the texel at `uv`. Out-of-range
    // UVs are clamped; mirror / repeat wrapping is documented but
    // not yet implemented (treated as `Clamp`). Empty images
    // return black so callers don't have to special-case
    // unconfigured slots.
    [[nodiscard]] rr::math::Vec3 sample(rr::math::Vec2 uv) const;

    // Tag for the device-side TextureView discriminator.
    [[nodiscard]] static constexpr TextureType type_tag() {
        return TextureType::Image;
    }

private:
    rr::image::Image image_;
    Wrap             wrap_u_ = Wrap::Clamp;
    Wrap             wrap_v_ = Wrap::Clamp;
    Filter           filter_ = Filter::Nearest;
};

}

#include "texture/ImageTexture.h"

#include "image/Color.h"
#include "math/MathUtils.h"

#include <utility>

namespace rr::texture {

ImageTexture::ImageTexture(rr::image::Image image)
    : image_(std::move(image)) {}

void ImageTexture::set_image(rr::image::Image image) {
    image_ = std::move(image);
}

rr::math::Vec3 ImageTexture::sample(rr::math::Vec2 uv) const {
    if (image_.empty()) {
        return rr::math::Vec3{0.0f, 0.0f, 0.0f};
    }

    // Wrap. Only Clamp is honored at this milestone; Repeat /
    // Mirror are documented in the header for the future device
    // sampler and silently fall through to Clamp here so callers
    // get sane output until the full sampler lands.
    const float u = rr::math::clamp(uv.x, 0.0f, 1.0f);
    const float v = rr::math::clamp(uv.y, 0.0f, 1.0f);

    // Top-left origin for the underlying Image, but UV convention
    // has v = 0 at the bottom of the texture. Flip v to match.
    const float fw = static_cast<float>(image_.width());
    const float fh = static_cast<float>(image_.height());
    int x = static_cast<int>(u * fw);
    int y = static_cast<int>((1.0f - v) * fh);

    if (x < 0)               x = 0;
    if (x >= image_.width())  x = image_.width()  - 1;
    if (y < 0)               y = 0;
    if (y >= image_.height()) y = image_.height() - 1;

    const auto pixel = image_.get_pixel(x, y);
    return rr::math::Vec3{pixel.r, pixel.g, pixel.b};
}

}

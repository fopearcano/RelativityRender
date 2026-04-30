#include "texture/ImageTexture.h"

#include <utility>

namespace rr::texture {

std::size_t image_texture_bpp(ImageTextureFormat format) noexcept {
    switch (format) {
        case ImageTextureFormat::Rgba8:    return 4;     // 1 byte  * 4 channels
        case ImageTextureFormat::Rgba32F:  return 16;    // 4 bytes * 4 channels
    }
    return 0;
}

ImageTexture::ImageTexture(int width, int height, ImageTextureFormat format,
                           std::string name)
    : width_(width), height_(height), format_(format),
      name_(std::move(name)) {}

bool ImageTexture::empty() const noexcept {
    return width_ == 0 || height_ == 0 || pixels_.empty();
}

std::size_t ImageTexture::expected_byte_size() const noexcept {
    if (width_ <= 0 || height_ <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(width_)
         * static_cast<std::size_t>(height_)
         * image_texture_bpp(format_);
}

void ImageTexture::set_name(std::string name) {
    name_ = std::move(name);
}

void ImageTexture::resize(int width, int height, ImageTextureFormat format) {
    width_  = width;
    height_ = height;
    format_ = format;
    pixels_.clear();
}

}

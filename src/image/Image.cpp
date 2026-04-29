#include "image/Image.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>

namespace rr::image {

namespace {

constexpr std::uint8_t to_byte(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

}

Image::Image(int width, int height, PixelFormat format)
    : width_(width), height_(height), format_(format) {
    assert(width  >= 0);
    assert(height >= 0);
    data_.assign(static_cast<std::size_t>(width_) * height_ * channels(), 0.0f);
}

int Image::channels() const {
    return format_ == PixelFormat::Rgba32F ? 4 : 3;
}

void Image::set_pixel(int x, int y, Rgba color) {
    assert(x >= 0 && x < width_);
    assert(y >= 0 && y < height_);

    const int  ch  = channels();
    const auto idx = (static_cast<std::size_t>(y) * width_ + x) * ch;

    data_[idx + 0] = color.r;
    data_[idx + 1] = color.g;
    data_[idx + 2] = color.b;
    if (ch == 4) {
        data_[idx + 3] = color.a;
    }
}

Rgba Image::get_pixel(int x, int y) const {
    assert(x >= 0 && x < width_);
    assert(y >= 0 && y < height_);

    const int  ch  = channels();
    const auto idx = (static_cast<std::size_t>(y) * width_ + x) * ch;

    const float r = data_[idx + 0];
    const float g = data_[idx + 1];
    const float b = data_[idx + 2];
    const float a = (ch == 4) ? data_[idx + 3] : 1.0f;
    return Rgba(r, g, b, a);
}

void Image::clear(Rgba color) {
    const int ch = channels();
    for (std::size_t i = 0; i < data_.size(); i += ch) {
        data_[i + 0] = color.r;
        data_[i + 1] = color.g;
        data_[i + 2] = color.b;
        if (ch == 4) {
            data_[i + 3] = color.a;
        }
    }
}

void Image::resize(int width, int height) {
    assert(width  >= 0);
    assert(height >= 0);
    width_  = width;
    height_ = height;
    data_.assign(static_cast<std::size_t>(width_) * height_ * channels(), 0.0f);
}

bool Image::save_ppm(const std::filesystem::path& path) const {
    if (empty()) return false;

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    out << "P6\n" << width_ << ' ' << height_ << "\n255\n";

    const int ch = channels();
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const auto    idx = (static_cast<std::size_t>(y) * width_ + x) * ch;
            std::uint8_t  rgb[3] = {
                to_byte(data_[idx + 0]),
                to_byte(data_[idx + 1]),
                to_byte(data_[idx + 2]),
            };
            out.write(reinterpret_cast<const char*>(rgb), 3);
        }
    }

    return out.good();
}

}

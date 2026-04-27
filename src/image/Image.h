#pragma once

#include "image/Color.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace rr::image {

enum class PixelFormat {
    Rgb32F,   // 3 floats per pixel.
    Rgba32F   // 4 floats per pixel.
};

// Host-side 2D float image.
//
// Storage is row-major, tightly packed, channel-interleaved. Origin is the
// top-left pixel; row stride is `width * channels`. Production GPU code
// will mirror the buffer on the device side later (M5+); the layout here is
// chosen to be compatible with that path (no padding, contiguous floats).
//
// CPU pixel writes are intended for host-driven cases only - clearing,
// debug fills, IO validation. No CPU ray tracing, per the engineering
// rules.
class Image {
public:
    Image() = default;
    Image(int width, int height, PixelFormat format);

    int         width()    const { return width_; }
    int         height()   const { return height_; }
    PixelFormat format()   const { return format_; }
    int         channels() const;          // 3 or 4
    bool        empty()    const { return width_ == 0 || height_ == 0; }

    // Pixel access. The Rgba in/out form covers both formats; for Rgb32F
    // the alpha is dropped on set and reported as 1 on get.
    void set_pixel(int x, int y, Rgba color);
    Rgba get_pixel(int x, int y) const;

    // Fill every pixel with the given color. Same alpha rules as above.
    void clear(Rgba color);

    // Reallocate to the new size. Pixel data is reset to zero; format
    // is preserved.
    void resize(int width, int height);

    // Raw access for GPU upload paths and IO. Layout is described above.
    float*       data()       { return data_.data(); }
    const float* data() const { return data_.data(); }
    std::size_t  size_in_floats() const { return data_.size(); }

    // Save as 8-bit PPM (P6 binary). Floats are clamped to [0,1] and
    // quantized; HDR values above 1 are lost. Alpha is dropped because
    // PPM has no alpha channel. Returns false on IO failure or if the
    // image is empty.
    bool save_ppm(const std::filesystem::path& path) const;

private:
    int                width_  = 0;
    int                height_ = 0;
    PixelFormat        format_ = PixelFormat::Rgba32F;
    std::vector<float> data_;
};

}

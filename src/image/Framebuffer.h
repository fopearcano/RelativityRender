#pragma once

#include "image/Color.h"
#include "image/Image.h"

#include <filesystem>

namespace rr::image {

// A render target. At this milestone it owns a single color image; AOVs,
// accumulation buffers, and tile metadata join later (M14 / M17). The
// distinction between Image and Framebuffer is intentional: Image is a
// generic 2D pixel buffer (also used for textures and saved files);
// Framebuffer is what the renderer writes into during a frame.
class Framebuffer {
public:
    Framebuffer() = default;
    Framebuffer(int width, int height, PixelFormat format = PixelFormat::Rgba32F);

    int         width()  const { return color_.width(); }
    int         height() const { return color_.height(); }
    PixelFormat format() const { return color_.format(); }

    Image&       color()       { return color_; }
    const Image& color() const { return color_; }

    void resize(int width, int height);
    void clear(Rgba color);

    // Convenience IO; forwards to the color image.
    bool save_ppm(const std::filesystem::path& path) const;

private:
    Image color_;
};

}

#include "image/Framebuffer.h"

namespace rr::image {

Framebuffer::Framebuffer(int width, int height, PixelFormat format)
    : color_(width, height, format) {}

void Framebuffer::resize(int width, int height) {
    color_.resize(width, height);
}

void Framebuffer::clear(Rgba color) {
    color_.clear(color);
}

bool Framebuffer::save_ppm(const std::filesystem::path& path) const {
    return color_.save_ppm(path);
}

}

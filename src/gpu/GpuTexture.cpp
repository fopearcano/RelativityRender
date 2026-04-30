#include "gpu/GpuTexture.h"

namespace rr::gpu {

bool GpuTexture::upload(const std::byte*                host_pixels,
                        std::size_t                     pixel_bytes,
                        int                             width,
                        int                             height,
                        rr::texture::ImageTextureFormat format) {
    if (pixel_bytes == 0) {
        // Empty upload is a successful clear, mirroring
        // GpuMesh::upload_vertices(host, 0)'s precedent.
        reset();
        return true;
    }

    if (host_pixels == nullptr || width <= 0 || height <= 0) {
        reset();
        return false;
    }

    const std::size_t expected =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height)
      * rr::texture::image_texture_bpp(format);
    if (expected == 0 || pixel_bytes != expected) {
        // Either the format yields zero bytes-per-pixel (defensive;
        // the enum currently has no such case) or the caller's byte
        // count contradicts the (w, h, format) metadata. Refuse to
        // upload mismatched data - silent acceptance would corrupt
        // the eventual sampler.
        reset();
        return false;
    }

    if (!pixels_.upload(host_pixels, pixel_bytes)) {
        // No CUDA backend, no visible device, or the cudaMemcpy /
        // cudaMalloc failed. Leave the texture empty - no partial
        // state.
        reset();
        return false;
    }

    width_  = width;
    height_ = height;
    format_ = format;
    return true;
}

bool GpuTexture::upload_from(const rr::texture::ImageTexture& src) {
    return upload(src.pixels().data(),
                  src.pixels().size(),
                  src.width(),
                  src.height(),
                  src.format());
}

void GpuTexture::reset() noexcept {
    pixels_.reset();
    width_  = 0;
    height_ = 0;
    format_ = rr::texture::ImageTextureFormat::Rgba8;
}

}

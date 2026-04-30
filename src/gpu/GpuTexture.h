#pragma once

#include "gpu/GpuBuffer.h"
#include "texture/ImageTexture.h"

#include <cstddef>

namespace rr::gpu {

// Backend-agnostic owner for a single image-texture's GPU resources.
//
// Stage 13B.1 scope: device-side pixel storage + dimensions / format
// metadata, plus safe free. There is NO sampler, NO bindless texture
// object, NO `cudaTextureObject_t` lifecycle, NO mipmap chain, NO
// kernel integration. The renderer cannot yet `tex2D` from this
// resource; that wiring lands in subsequent 13B sub-stages.
//
// Holds a `GpuBuffer<std::byte>` so the same type carries Rgba8 and
// Rgba32F payloads without a templated split. The buffer is sized
// `width * height * image_texture_bpp(format)` bytes when populated.
//
// `GpuTexture` is move-only, like `GpuBuffer<T>` and `GpuMesh`. When
// `RR_HAS_CUDA` is not defined, an empty upload (zero pixel bytes)
// still succeeds (it is a pure host write); a non-empty upload
// fails predictably and leaves the texture empty - the same "honest
// absence" behaviour the rest of the GPU layer uses.
class GpuTexture {
public:
    GpuTexture() = default;
    ~GpuTexture() = default;

    GpuTexture(const GpuTexture&)            = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&&) noexcept            = default;
    GpuTexture& operator=(GpuTexture&&) noexcept = default;

    // Upload `pixel_bytes` of raw image data from `host_pixels` into a
    // device-side buffer, recording (width, height, format) as the
    // texture's metadata. Returns true on success, false on:
    // - inconsistent inputs (host_pixels == nullptr with non-zero
    //   bytes; width / height non-positive with non-zero bytes;
    //   pixel_bytes != width * height * image_texture_bpp(format));
    // - device-side allocation / copy failure (no CUDA backend or
    //   no visible device).
    //
    // `pixel_bytes == 0` is a successful clear (mirroring
    // `GpuMesh::upload_vertices(host, 0)`'s "empty upload is a
    // no-op success" precedent): the device buffer is freed and
    // metadata is reset to zero. On any failure the texture is
    // left empty (no partial state).
    bool upload(const std::byte*                 host_pixels,
                std::size_t                      pixel_bytes,
                int                              width,
                int                              height,
                rr::texture::ImageTextureFormat  format);

    // Convenience: upload from a host-side `ImageTexture`. Forwards to
    // `upload(...)`; an `ImageTexture` with `empty() == true` (zero
    // dims OR no pixels loaded) clears the device texture and returns
    // true.
    bool upload_from(const rr::texture::ImageTexture& src);

    // Free device memory and reset metadata. Safe to call repeatedly
    // and on a moved-from / never-uploaded texture. The destructor
    // calls this automatically via `GpuBuffer<std::byte>`'s RAII.
    void reset() noexcept;

    [[nodiscard]] int                             width()  const noexcept { return width_;  }
    [[nodiscard]] int                             height() const noexcept { return height_; }
    [[nodiscard]] rr::texture::ImageTextureFormat format() const noexcept { return format_; }
    [[nodiscard]] std::size_t size_in_bytes() const noexcept { return pixels_.size_in_bytes(); }

    // True iff the device buffer holds no data. Mirrors
    // `GpuBuffer::empty()` semantics. A texture whose host source
    // was an empty `ImageTexture` reports `true`.
    [[nodiscard]] bool empty() const noexcept { return pixels_.empty(); }

    // True iff a non-empty upload has succeeded. Convenience for
    // call sites that want a positive form of `!empty()`.
    [[nodiscard]] bool has_data() const noexcept { return !pixels_.empty(); }

    // Device pointer for the eventual sampler / kernel integration to
    // read. Returns nullptr when no upload has happened.
    [[nodiscard]] const std::byte* device_pixels() const noexcept {
        return pixels_.device_ptr();
    }

private:
    rr::gpu::GpuBuffer<std::byte>   pixels_;
    int                             width_  = 0;
    int                             height_ = 0;
    rr::texture::ImageTextureFormat format_ = rr::texture::ImageTextureFormat::Rgba8;
};

}

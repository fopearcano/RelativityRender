#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rr::texture {

// On-disk / in-memory pixel format an `ImageTexture` is decoded
// into. Stage 13A defines two slots; the loader / sampler layers
// (later stages) decide which authored formats decode into which
// slot.
//
// - Rgba8:   four 8-bit channels, sRGB colour space by convention.
//            Used for albedo / colour textures. 4 bytes per pixel.
// - Rgba32F: four 32-bit float channels, linear colour space. Used
//            for HDR environments and data textures (normal /
//            metallic / roughness packed maps). 16 bytes per pixel.
enum class ImageTextureFormat : std::uint32_t {
    Rgba8   = 0,
    Rgba32F = 1,
};

// Bytes-per-pixel for each `ImageTextureFormat`. The sampler /
// uploader use this to size the pixel buffer + the GPU resource.
[[nodiscard]] std::size_t image_texture_bpp(ImageTextureFormat format) noexcept;

// Host-side image-texture entry.
//
// Stage 13A scope: metadata + a placeholder pixel buffer. There
// is no decode path, no GPU upload, no mipmap chain. The pixel
// buffer is `std::vector<std::byte>` so it can carry both the
// 8-bit and the 32-bit-float formats without a templated split;
// the loader (later stage) writes into it from disk, the
// uploader (also later) ships it to the GPU as a CUDA texture
// object via `CudaTexture.cuh`'s eventual descriptor type.
//
// Field semantics:
// - `width` / `height`  pixel dimensions. Both 0 = empty / not
//   loaded.
// - `format` describes how the pixel buffer is interpreted. The
//   buffer should be `width * height * image_texture_bpp(format)`
//   bytes when fully populated.
// - `name` is for authoring / debugging; GPU side never sees it.
// - `pixels` holds the decoded raster. Empty by default; the
//   loader fills it in a later sub-stage.
class ImageTexture {
public:
    ImageTexture() = default;
    ImageTexture(int width, int height, ImageTextureFormat format,
                 std::string name = {});

    [[nodiscard]] int                width()  const noexcept { return width_;  }
    [[nodiscard]] int                height() const noexcept { return height_; }
    [[nodiscard]] ImageTextureFormat format() const noexcept { return format_; }
    [[nodiscard]] const std::string& name()   const noexcept { return name_;   }

    // Raw pixel storage. The mutable overload lets the loader fill
    // pixels in place without an extra copy.
    [[nodiscard]] const std::vector<std::byte>& pixels() const noexcept { return pixels_; }
    [[nodiscard]] std::vector<std::byte>&       pixels()       noexcept { return pixels_; }

    // True when the dimensions are still 0 (default-constructed) OR
    // the pixel buffer has not yet been populated. Either alone is
    // enough for "no texture data to upload".
    [[nodiscard]] bool empty() const noexcept;

    // Expected byte size of `pixels` for the current
    // (width, height, format). Loaders use this to size the buffer
    // before reading from disk; samplers use it to validate.
    // Returns 0 for empty dimensions.
    [[nodiscard]] std::size_t expected_byte_size() const noexcept;

    void set_name(std::string name);

    // Re-set the dimensions + format. Pixel storage is cleared (set
    // to zero bytes); callers that want to keep their pixel data
    // must re-fill `pixels()` afterwards.
    void resize(int width, int height, ImageTextureFormat format);

private:
    int                    width_  = 0;
    int                    height_ = 0;
    ImageTextureFormat     format_ = ImageTextureFormat::Rgba8;
    std::string            name_;
    std::vector<std::byte> pixels_;
};

}

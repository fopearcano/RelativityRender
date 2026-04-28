#pragma once

#include "image/Image.h"

#include <cstdint>
#include <filesystem>

// Render-pass / AOV (Arbitrary Output Variable) foundation.
//
// Every AOV is a per-pixel buffer the renderer writes during a
// launch. Six v1 kinds:
//
//   Beauty             - final shaded colour (Rgba)
//   Normal             - encoded surface normal `0.5 * N + 0.5` (Rgba)
//   Depth              - hit distance from camera (scalar in R)
//   Albedo             - per-hit base colour (Rgba; pre-lighting)
//   DopplerFactor      - relativistic Doppler factor `D` (scalar)
//   SearchlightFactor  - bolometric beaming factor `D^4` (scalar)
//
// Storage uses `rr::image::PixelFormat::Rgba32F` uniformly so the
// upload / download path is the same for every AOV. Scalar AOVs
// pack their value in the R channel; G / B / A are written but
// unused.
//
// `save_ppm` is the v1 viewer hook. Colour AOVs save through
// `Image::save_ppm` directly. Scalar AOVs are normalised so the
// brightest pixel maps to 1.0 and re-emitted as a grayscale
// triple. That keeps the saved PPMs human-readable without
// committing the renderer to a tone-mapping policy at this
// milestone.

namespace rr::renderer {

enum class AOVKind : std::int32_t {
    Beauty            = 0,
    Normal            = 1,
    Depth             = 2,
    Albedo            = 3,
    DopplerFactor     = 4,
    SearchlightFactor = 5,
};

inline constexpr int kAOVCount = 6;

[[nodiscard]] const char* aov_kind_name(AOVKind kind);

// True iff the AOV stores a Vec3-per-pixel value (kept in
// [0, 1]-ish range and saved as RGB). False for scalar AOVs,
// which are normalised at save time.
[[nodiscard]] bool aov_is_color(AOVKind kind);

// Host-side AOV buffer. Owns the downloaded pixel data; the GPU
// kernel writes through a `CudaAOVPack` of raw device pointers
// (see `cuda/CudaAOV.cuh`) into a parallel device allocation
// that the renderer downloads into this `Image`.
class AOV {
public:
    AOV() = default;
    AOV(AOVKind kind, int width, int height);

    [[nodiscard]] AOVKind                 kind()   const noexcept { return kind_; }
    [[nodiscard]] int                     width()  const          { return image_.width(); }
    [[nodiscard]] int                     height() const          { return image_.height(); }
    [[nodiscard]] const rr::image::Image& image()  const          { return image_; }
    [[nodiscard]] rr::image::Image&       image()                  { return image_; }
    [[nodiscard]] bool                    empty()  const          { return image_.empty(); }

    // Save as 8-bit P6 PPM. Colour AOVs (`Beauty` / `Normal` /
    // `Albedo`) go through `Image::save_ppm` directly. Scalar
    // AOVs (`Depth` / `DopplerFactor` / `SearchlightFactor`)
    // are normalised so the brightest pixel becomes 1.0, then
    // saved as grayscale. Returns false on IO failure or when
    // the image is empty.
    [[nodiscard]] bool save_ppm(const std::filesystem::path& path) const;

private:
    AOVKind          kind_ = AOVKind::Beauty;
    rr::image::Image image_;
};

}
